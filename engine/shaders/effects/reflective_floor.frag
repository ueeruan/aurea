#version 450
// =============================================================================
//  Aurea / shaders / effects / reflective_floor.frag
//
//  Reflective Floor (0.4.1; ReflectiveFloorEffect.cpp). Numa camada de ajuste,
//  a entrada é TUDO o que está abaixo dela (2D, 3D, partículas…).
//
//  Dois modos:
//   - 3D (camada 3D, q0.w = 1): o chão é o PLANO da camada no mundo. Cada pixel
//     acha o seu ponto do chão pela inversa da homografia px ← chão (h0..h2);
//     o reflexo é o raio da câmera refletido no plano até o plano z = 0 da
//     composição (onde moram as camadas 2D), projetado de volta na tela. Segue
//     a câmera 3D e os pais (nulo 3D).
//   - 2D: horizonte fixo na tela, chão em perspectiva e a imagem espelhada na
//     linha do horizonte.
//  Em ambos: textura (lisa, ladrilhos, ladrilhos gastos ou uma camada do
//  projeto), desfoque e distorção que crescem com a distância, desbotamento.
//  Implementação própria. Cores lineares, pré-multiplicadas.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D src;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D floorTex;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap, texel, p0, p1, p2, p3, color;
    // p0 = horizonte (0..1), reflexo, distância do reflexo, desfoque (px)
    // p1 = distorção, estilo (0 liso, 1 ladrilhos, 2 gasto), ladrilho (px), perspectiva
    // p2 = região da entrada (px; a composição inteira numa camada de ajuste)
    // p3 = textura, brilho do chão, opacidade do chão, 1 = textura de camada
    // q0 = câmera no mundo (xyz), 1 = modo 3D; h0..h2 = inversa da homografia (linhas)
    vec4 q0, h0, h1, h2;
    mat4 world;   // mundo ← px da camada (o plano do chão)
    mat4 proj;    // px da composição (homogêneo) ← mundo
} p;

float hash21(vec2 q) {
    q = fract(q * vec2(123.34, 456.21));
    q += dot(q, q + 45.32);
    return fract(q.x * q.y);
}
float vnoise(vec2 q) {
    vec2 i = floor(q), f = fract(q);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), u.x), mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 q) {
    float a = 0.5, s = 0.0;
    for (int k = 0; k < 5; ++k) { s += a * vnoise(q); q *= 2.03; a *= 0.5; }
    return s;
}
/// A entrada no ponto `px` (px da camada); fora da região, transparente.
vec4 at(vec2 px) {
    vec2 uv = (px - p.p2.xy) / max(p.p2.zw, vec2(1.0));
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    return texture(src, uv * p.uvMap.xy + p.uvMap.zw);
}
/// Reflexo desfocado em volta de `q` (13 amostras num disco), raio em px.
vec4 blurred(vec2 q, float radius) {
    if (radius < 0.5) return at(q);
    vec4 acc = vec4(0.0);
    float total = 0.0;
    for (int k = 0; k < 13; ++k) {
        float r = sqrt((float(k) + 0.5) / 13.0) * radius;
        float a = float(k) * 2.39996;
        float w = 1.0 - 0.5 * float(k) / 13.0;
        acc += at(q + r * vec2(cos(a), sin(a) * 0.6)) * w;
        total += w;
    }
    return acc / total;
}

/// O chão em `t` (coordenadas em ladrilhos): cor, desgaste e junta.
struct FloorLook { vec3 color; float wear; float grout; };
FloorLook floor_look(vec2 t) {
    int style = int(p.p1.y + 0.5);
    vec2 cell = floor(t);
    vec2 g = abs(fract(t) - 0.5);
    float line = max(g.x, g.y);
    // Ladrilho menor que alguns pixels (longe): o desenho vira a média.
    vec2 fw = fwidth(t);
    float detail = clamp(1.0 - max(fw.x, fw.y) * 4.0, 0.0, 1.0);
    float aa = max(fwidth(line), 1e-4);
    float grout = smoothstep(0.5 - 0.03 - aa, 0.5 - 0.03, line) * detail;
    float tex = 1.0;
    if (style >= 1) tex = mix(mix(0.85, 0.82 + 0.3 * hash21(cell), detail), 0.12, grout);
    float wear = 1.0;
    if (style >= 2) {
        float n = fbm(t * 2.5 + cell * 0.37);
        wear = mix(0.6, smoothstep(0.30, 0.66, n), detail);
        tex *= mix(0.3, 1.0, wear);
    }
    float amount = clamp(p.p3.x, 0.0, 1.0);
    vec3 base = p.color.rgb;
    if (p.p3.w > 0.5) {
        // Textura de camada: a imagem inteira em cada ladrilho (linear, pré-multiplicada).
        vec4 img = texture(floorTex, fract(t));
        base = mix(base, img.rgb / max(img.a, 1e-4), img.a);
    }
    FloorLook f;
    f.color = base * mix(1.0, tex, amount) * (clamp(p.p3.y, 0.0, 1.0) * 2.0);
    f.wear = wear;
    f.grout = grout * float(style >= 1);
    return f;
}

/// O pixel final: o chão com o reflexo, misturado ao que já está ali.
vec4 compose(vec4 s, FloorLook f, vec4 refl, float fall) {
    float amount = clamp(p.p3.x, 0.0, 1.0);
    float strength = clamp(p.p0.y, 0.0, 1.0) * fall
                   * mix(1.0, mix(0.25, 1.0, f.wear) * mix(0.6, 1.0, 1.0 - f.grout), amount);
    vec4 floorPx = vec4(f.color + refl.rgb * strength, 1.0);
    // Numa camada de ajuste a entrada já traz o fundo (opaco): o chão substitui o que
    // está ali, na opacidade do chão; com entrada transparente, ele fica atrás.
    float op = clamp(p.p3.z, 0.0, 1.0);
    return mix(s + (1.0 - s.a) * floorPx, floorPx, op);
}

void main() {
    vec2 px = p.p2.xy + v_uv * p.p2.zw;
    vec4 s = at(px);
    float H = max(p.p2.w, 1.0), W = max(p.p2.z, 1.0);
    float tile = max(p.p1.z, 4.0);
    float distortion = clamp(p.p1.x, 0.0, 1.0);

    if (p.q0.w > 0.5) {
        // ---- 3D: o plano da camada no mundo ------------------------------------
        vec3 hq = vec3(dot(p.h0.xyz, vec3(px, 1.0)), dot(p.h1.xyz, vec3(px, 1.0)), dot(p.h2.xyz, vec3(px, 1.0)));
        if (abs(hq.z) < 1e-9) { o_color = s; return; }
        vec2 uv = hq.xy / hq.z;                                  // px da camada (no plano)
        vec3 wf = (p.world * vec4(uv, 0.0, 1.0)).xyz;            // o ponto do chão no mundo
        vec4 back = p.proj * vec4(wf, 1.0);
        if (back.w <= 0.0) { o_color = s; return; }              // atrás da câmera: céu
        vec3 cam = p.q0.xyz;
        // O chão só aparece À FRENTE do plano z = 0 (onde ficam as camadas 2D): atrás
        // dele estaria escondido pelos objetos, e a entrada não diz o que é objeto e o
        // que é fundo. Assim o chão começa na base do que está em pé nele.
        if (wf.z * (-cam.z) > 0.0) { o_color = s; return; }
        vec3 n = normalize((p.world * vec4(0.0, 0.0, 1.0, 0.0)).xyz);
        vec3 dir = normalize(wf - cam);
        if (dot(dir, n) > 0.0) n = -n;                           // a face vista
        vec3 r = reflect(dir, n);
        float dist = length(wf - cam);
        vec2 t = uv / tile;
        FloorLook f = floor_look(t);
        // Raio refletido até z = 0 (o plano das camadas 2D da composição).
        vec4 refl = vec4(0.0);
        float travel = 0.0;
        if (abs(r.z) > 1e-6) {
            travel = -wf.z / r.z;
            if (travel > 0.0) {
                vec4 cq = p.proj * vec4(wf + r * travel, 1.0);
                if (cq.w > 0.0) {
                    float d = clamp(travel / H, 0.0, 1.0);
                    vec2 n2 = vec2(fbm(t * 2.0 + 3.1), fbm(t * 2.0 + 7.7)) - 0.5;
                    vec2 offset = n2 * distortion * (8.0 + 60.0 * d) * mix(1.0, 1.0 + (1.0 - f.wear) * 2.0, clamp(p.p3.x, 0.0, 1.0));
                    refl = blurred(cq.xy / cq.w + offset, max(p.p0.w, 0.0) * (0.25 + 3.0 * d));
                }
            }
        }
        float fall = 1.0 - smoothstep(0.0, max(p.p0.z, 0.01) * H * 1.5, travel);
        // Muito longe, o chão some no horizonte (sem serrilhado infinito).
        float horizonFade = 1.0 - smoothstep(H * 6.0, H * 12.0, dist);
        vec4 outPx = compose(s, f, refl, fall);
        o_color = mix(s, outPx, horizonFade);
        return;
    }

    // ---- 2D: horizonte fixo na tela --------------------------------------------
    float hy = p.p2.y + clamp(p.p0.x, 0.0, 1.0) * H;
    float bottom = p.p2.y + H;
    if (px.y < hy) { o_color = s; return; }
    float d = clamp((px.y - hy) / max(bottom - hy, 1.0), 0.0, 1.0);
    float persp = clamp(p.p1.w, 0.0, 1.0);
    float cx = p.p2.x + 0.5 * W;
    float dy = max(px.y - hy, 0.5);
    float camH = 0.45 * H / tile;                       // altura da câmera, em ladrilhos
    vec2 deep = vec2((px.x - cx) * camH / dy, H * camH / dy);
    vec2 flatT = vec2(px.x - cx, px.y - hy) / tile;
    vec2 t = mix(flatT, deep, persp);
    FloorLook f = floor_look(t);
    vec2 n2 = vec2(fbm(t * 2.0 + 3.1), fbm(t * 2.0 + 7.7)) - 0.5;
    vec2 offset = n2 * distortion * (8.0 + 60.0 * d) * mix(1.0, 1.0 + (1.0 - f.wear) * 2.0, clamp(p.p3.x, 0.0, 1.0));
    vec4 refl = blurred(vec2(px.x, hy - (px.y - hy)) + offset, max(p.p0.w, 0.0) * (0.25 + 3.0 * d));
    float fall = 1.0 - smoothstep(0.0, max(p.p0.z, 0.01), d);
    o_color = compose(s, f, refl, fall);
}
