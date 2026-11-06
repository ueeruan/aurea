#version 450
// =============================================================================
//  Aurea / shaders / effects / hollow_box.frag
//
//  Hollow Box (HollowBoxEffect.cpp): a camada vira a textura de uma caixa 3D
//  oca — seis lados com espessura — girada, em perspectiva e iluminada.
//
//  A caixa é um campo de distância (SDF): cada lado é uma placa arredondada
//  (meia espessura no eixo dele, a caixa inteira nos outros dois), empurrada
//  para fora pelo "Afastar". Os estilos recortam (tiram) ou cortam (guardam
//  só) um círculo/quadrado no meio da placa. O raio de cada pixel marcha no
//  espaço do objeto; ao bater num lado, a cor vem da camada esticada naquele
//  lado e a luz é a dos efeitos CC (cc_light.glsl). Lado com transparência
//  deixa ver o que está atrás: a marcha atravessa a placa e continua, somando
//  de frente para trás.
//
//  Espaço da vista: x para a direita, y para BAIXO, z para quem olha. A frente
//  é o lado +z.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 dst;     // região de saída (px da camada)
    vec4 src;     // região da entrada
    vec4 box;     // caixa natural da camada
    vec4 info;    // x = px por texel de saída
    vec4 p0;      // xyz = linha 0 da rotação inversa (vista → objeto)
    vec4 p1;
    vec4 p2;
    vec4 p3;      // meia largura, meia altura, meia profundidade, espessura (px)
    vec4 p4;      // luz: direção para a luz, intensidade
    vec4 p5;      // ambiente, difusa, especular, expoente
    vec4 color;   // cor da luz, metal
    vec4 shape;   // afastar, arredondar (px), recorte (0..1), passos
    vec4 view;    // centro (px), distância da câmera (0 = ortográfica), luz ligada
    vec4 sides0;  // estilos: frente, trás, esquerda, direita
    vec4 sides1;  // estilos: topo, base
} p;

#include "cc_light.glsl"

float hb_round_box(vec3 q, vec3 h, float r) {
    const vec3 d = abs(q) - h + r;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0) - r;
}

float hb_box2(vec2 q, vec2 h) {
    const vec2 d = abs(q) - h;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

/// Estilo do lado `i` (0 frente +z, 1 trás −z, 2 esquerda −x, 3 direita +x,
/// 4 topo −y, 5 base +y).
int hb_style(int i) {
    const float s = i < 4 ? p.sides0[i] : p.sides1[i - 4];
    return int(s + 0.5);
}

/// Distância até um lado; `axis` = eixo da normal, `sgn` = de que lado.
float hb_side(vec3 o, int axis, float sgn, int style) {
    const vec3 h = p.p3.xyz;
    const float t = p.p3.w;
    vec3 c = vec3(0.0);
    vec3 ext = h;
    c[axis] = sgn * (h[axis] - 0.5 * t + p.shape.x);
    ext[axis] = 0.5 * t;
    float d = hb_round_box(o - c, ext, p.shape.y);
    if (style >= 2) {
        // As duas direções do plano do lado.
        const int a = axis == 0 ? 1 : 0;
        const int b = axis == 2 ? 1 : 2;
        const vec2 q = vec2(o[a], o[b]);
        const vec2 hh = vec2(h[a], h[b]);
        const float k = clamp(p.shape.z, 0.0, 1.0);
        const float shape = style <= 3 ? length(q) - k * min(hh.x, hh.y) : hb_box2(q, k * hh);
        // Recorte tira a forma; corte guarda só ela.
        d = (style == 2 || style == 4) ? max(d, -shape) : max(d, shape);
    }
    return d;
}

/// A caixa inteira; `side` = o lado mais perto (para a textura).
float hb_map(vec3 o, out int side) {
    float best = 1e30;
    side = 0;
    for (int i = 0; i < 6; ++i) {
        const int style = hb_style(i);
        if (style == 1) continue;   // oculto
        const int axis = i < 2 ? 2 : (i < 4 ? 0 : 1);
        const float sgn = (i == 0 || i == 3 || i == 5) ? 1.0 : -1.0;
        const float d = hb_side(o, axis, sgn, style);
        if (d < best) { best = d; side = i; }
    }
    return best;
}

float hb_dist(vec3 o) {
    int side;
    return hb_map(o, side);
}

/// A camada esticada no lado `side`, lida de fora (sem espelhar).
vec4 hb_texture(vec3 o, int side) {
    const vec3 h = max(p.p3.xyz, vec3(1e-3));
    const vec3 n = o / (2.0 * h);   // −0.5..0.5 dentro da caixa
    vec2 uv;
    if (side == 0) uv = vec2(n.x, n.y);
    else if (side == 1) uv = vec2(-n.x, n.y);
    else if (side == 2) uv = vec2(n.z, n.y);
    else if (side == 3) uv = vec2(-n.z, n.y);
    else if (side == 4) uv = vec2(n.x, n.z);
    else uv = vec2(n.x, -n.z);
    uv = clamp(uv + 0.5, 0.0, 1.0);
    const vec2 px = p.box.xy + uv * p.box.zw;
    return texture(u_tex0, (px - p.src.xy) / max(p.src.zw, vec2(1e-6)));
}

vec3 hb_to_object(vec3 v) { return vec3(dot(p.p0.xyz, v), dot(p.p1.xyz, v), dot(p.p2.xyz, v)); }
vec3 hb_to_view(vec3 o) { return p.p0.xyz * o.x + p.p1.xyz * o.y + p.p2.xyz * o.z; }

vec3 hb_normal(vec3 o, float eps) {
    const vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * hb_dist(o + k.xyy * eps) + k.yyx * hb_dist(o + k.yyx * eps)
                   + k.yxy * hb_dist(o + k.yxy * eps) + k.xxx * hb_dist(o + k.xxx * eps));
}

void main() {
    const vec2 d = p.dst.xy + v_uv * p.dst.zw - p.view.xy;
    const float texel = max(p.info.x, 1e-3);
    const vec3 h = p.p3.xyz;
    const float bound = length(h + p.shape.x) + 1.0;
    // A caixa inteira (lados afastados inclusive) com 1 px de folga.
    const vec3 hull = h + p.shape.x + 1.0;

    // O raio na vista: ortográfico (de frente, de longe) ou da câmera em +z.
    vec3 ro, rd;
    if (p.view.z > 0.0) {
        ro = vec3(0.0, 0.0, p.view.z);
        rd = normalize(vec3(d, 0.0) - ro);
    } else {
        ro = vec3(d, bound + 1.0);
        rd = vec3(0.0, 0.0, -1.0);
    }
    // A marcha no espaço do objeto (a rotação não muda distâncias).
    const vec3 oo = hb_to_object(ro);
    const vec3 od = hb_to_object(rd);
    // Só o trecho dentro da CAIXA (antes era a esfera que a contém): o raio
    // que não a toca sai já e a marcha começa na borda dela — os passos
    // gastos no vazio em volta deixavam a prévia travada (beta, nov/2026).
    const vec3 safe = vec3(abs(od.x) < 1e-8 ? 1e-8 : od.x, abs(od.y) < 1e-8 ? 1e-8 : od.y,
                           abs(od.z) < 1e-8 ? 1e-8 : od.z);
    const vec3 ta = (-hull - oo) / safe;
    const vec3 tb = (hull - oo) / safe;
    const vec3 tn = min(ta, tb);
    const vec3 tf = max(ta, tb);
    float t = max(max(max(tn.x, tn.y), tn.z), 0.0);
    const float tEnd = min(min(tf.x, tf.y), tf.z);
    if (tEnd <= t) { o_color = vec4(0.0); return; }
    const float eps = 0.35 * texel;
    const int steps = int(clamp(p.shape.w, 10.0, 200.0));
    vec4 acc = vec4(0.0);
    bool inside = false;
    for (int i = 0; i < 200; ++i) {
        if (i >= steps || t > tEnd) break;
        const vec3 o = oo + od * t;
        int side;
        const float dist = hb_map(o, side);
        if (inside) {
            // Atravessando a placa até sair dela.
            if (dist > eps) inside = false;
            t += max(abs(dist), eps);
            continue;
        }
        if (dist < eps) {
            vec4 c = hb_texture(o, side);
            if (p.view.w > 0.5) {
                vec3 n = hb_to_view(hb_normal(o, max(eps, 0.5)));
                if (dot(n, rd) > 0.0) n = -n;   // a parede vista por dentro
                c.rgb = cc_shade(c.rgb, c.a, n);
            }
            acc += c * (1.0 - acc.a);
            if (acc.a > 0.995) break;
            inside = true;
            t += 2.0 * eps;
            continue;
        }
        t += dist;
    }
    o_color = acc;
}
