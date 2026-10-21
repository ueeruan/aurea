#version 450
// =============================================================================
//  Aurea / shaders / effects / aurora.frag
//
//  Aurora (AuroraEffect.cpp). Cada pixel é um raio de uma câmera na altura
//  da água (inclinação, giro e campo de visão do grupo Ambiente). Acima do
//  horizonte, o raio cruza lâminas horizontais empilhadas no céu; em cada
//  lâmina, uma faixa sinuosa (o arco da aurora) com raios verticais finos.
//  A base da cortina é a cor principal e o topo puxa para a secundária.
//  Abaixo do horizonte o raio é espelhado: o reflexo, com ondulação.
//
//  Cores lineares; saída opaca sobre a camada (pré-multiplicada).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;     // x = distorção, y = quantidade, z = escala, w = comprimento
    vec4 p1;     // x = exposição, y = detalhe 0..1, z = curvatura, w = brilho do céu
    vec4 p2;     // região da imagem (px da camada)
    vec4 p3;     // x = relógio, y = horizonte (−1..1), z = reflexo 0..1, w = ondulação 0..1
    vec4 color;  // cor principal (linear)
    vec4 q0;     // rgb = secundária, w = estrelas 0..1
    vec4 q1;     // rgb = céu, w = campo de visão (graus)
    vec4 q2;     // x = inclinação °, y = giro °, zw = tamanho da camada
    vec4 q3;     // x = opacidade, y = altura das cortinas 0..1
} p;

const int kMaxSheets = 40;

/// Raio da câmera virtual pelo pixel (px da camada), y para cima.
vec3 view_ray(vec2 px) {
    const vec2 size = max(p.q2.zw, vec2(1.0));
    vec2 ndc = (px - 0.5 * size) / (0.5 * size.y);
    ndc.y = -ndc.y + p.p3.y;
    const float focal = 1.0 / tan(radians(clamp(p.q1.w, 20.0, 140.0)) * 0.5);
    vec3 d = normalize(vec3(ndc.x, ndc.y, focal));
    const float tilt = radians(p.q2.x), pan = radians(p.q2.y);
    d = vec3(d.x, d.y * cos(tilt) + d.z * sin(tilt), -d.y * sin(tilt) + d.z * cos(tilt));
    d = vec3(d.x * cos(pan) + d.z * sin(pan), d.y, -d.x * sin(pan) + d.z * cos(pan));
    return d;
}

/// Um arco da aurora (faixa sinuosa) em `u`, a `depth` do observador.
float arc(vec2 u, float t, float sheet, float depth, float seed) {
    // Distorção: o arco serpenteia e, com mais distorção, enrola em
    // redemoinhos (o sinal escolhe o sentido do giro).
    const float warp = p.p0.x;
    vec2 w = u;
    for (int i = 0; i < 2; ++i) {
        const vec2 n = vec2(aurea_fbm(w * 0.45 + vec2(t * 0.04, seed), 3, 0.5),
                            aurea_fbm(w * 0.45 + vec2(seed * 1.7, -t * 0.03), 3, 0.5)) - 0.5;
        w += vec2(-n.y, n.x) * warp * 2.4 + n * abs(warp) * 0.8;
    }
    const float meander = aurea_fbm(vec2(w.x * 0.22 + t * 0.035, 7.3 + seed), 3, 0.5) * 2.0 - 1.0;
    float center = meander * 1.3;
    // Curvatura: o arco dobra sobre o observador (parábola em profundidade):
    // nas laterais ele desce em direção ao horizonte.
    center += p.p1.z * w.x * w.x * 3.0;
    const float dist = w.y - depth - center;
    // Faixa: borda de baixo (perto) definida, a de cima mais difusa.
    const float width = 0.30 + 0.20 * sheet;
    const float band = exp(-dist * dist / (width * width));
    // Raios verticais: ruído fino ao longo do arco, mais fino com o detalhe.
    const float detail = p.p1.y;
    const float rayFreq = 3.0 + detail * 22.0;
    float rays = aurea_fbm(vec2(w.x * rayFreq + center * 4.0 + t * 0.25, t * 0.11 + sheet * 0.15 + seed),
                           2 + int(detail * 3.0), 0.55);
    rays = smoothstep(0.2, 0.9, rays);
    // Pulsação lenta do brilho ao longo do arco.
    const float pulse = 0.55 + 0.45 * aurea_value_noise(vec2(w.x * 0.5 - t * 0.12, 3.7 + seed));
    return band * (0.25 + 2.2 * rays * rays) * pulse;
}

/// A cortina numa lâmina: `q` no plano da lâmina (unidades da cena). Dois
/// arcos: um perto, alto no céu, e outro longe, que chega ao horizonte.
float curtain(vec2 q, float t, float sheet) {
    const float scale = max(p.p0.z, 0.05) * 0.06;
    const vec2 u = q * scale;
    return arc(u, t, sheet, 2.5, 0.0) + 0.75 * arc(u, t * 1.07, sheet, 6.5, 13.0);
}

/// A aurora vista na direção `d` (só acima do horizonte).
vec3 aurora(vec3 d, float t) {
    if (d.y <= 0.002) return vec3(0.0);
    const float length01 = clamp(p.p0.w, 0.0, 10.0);
    const int sheets = clamp(12 + int(p.p1.y * 28.0), 8, kMaxSheets);
    const float base = mix(1.6, 4.0, clamp(p.q3.y, 0.0, 1.0));   // altura da borda de baixo
    const float span = 0.1 + length01 * 2.4;                       // altura da cortina
    vec3 acc = vec3(0.0);
    for (int i = 0; i < kMaxSheets; ++i) {
        if (i >= sheets) break;
        const float k = (float(i) + 0.5) / float(sheets);
        const float height = base + k * span;
        const float dist = height / d.y;
        const vec2 q = d.xz * dist;
        const float c = curtain(q, t, k);
        // De baixo para cima: forte na base, some no topo (a cauda da aurora).
        const float fade = pow(1.0 - k, 1.6) * (0.35 + 0.65 * smoothstep(0.0, 0.12, k + 0.05));
        const vec3 tint = mix(p.color.rgb, p.q0.rgb, smoothstep(0.15, 0.9, k));
        acc += tint * c * fade;
    }
    acc *= 2.2 / float(sheets);
    // Núcleo: onde a cortina é mais densa ela estoura para um ciano quase branco.
    acc += vec3(0.55, 1.0, 0.95) * pow(max(max(acc.r, acc.g), acc.b), 2.0) * 0.45;
    // Longe, perto do horizonte, a aurora some na névoa.
    acc *= smoothstep(0.0, 0.10, d.y);
    return acc * p.p0.y;
}

/// Estrelas: uma por célula de um mapa da esfera, com cintilação.
vec3 stars(vec3 d, float t) {
    if (p.q0.w <= 0.0 || d.y <= 0.0) return vec3(0.0);
    const vec2 sph = vec2(atan(d.x, d.z), asin(clamp(d.y, -1.0, 1.0)));
    const vec2 grid = sph * 120.0;
    const vec2 cell = floor(grid);
    const float h = aurea_hash2(cell);
    const float density = p.q0.w * 0.35;
    if (h > density) return vec3(0.0);
    const vec2 pos = vec2(aurea_hash3(cell, 11u), aurea_hash3(cell, 23u));
    const float r = length(fract(grid) - pos);
    const float twinkle = 0.6 + 0.4 * sin(t * 3.0 + h * 40.0);
    const float glow = exp(-r * r * 160.0) * twinkle * smoothstep(0.0, 0.25, d.y);
    return vec3(0.75, 0.82, 1.0) * glow * (0.3 + 1.2 * aurea_hash3(cell, 41u));
}

/// O céu inteiro numa direção acima do horizonte.
vec3 sky(vec3 d, float t) {
    const float up = clamp(d.y, 0.0, 1.0);
    const vec3 aur = aurora(d, t);
    vec3 col = p.q1.rgb * (0.55 + 0.9 * pow(1.0 - up, 3.0));
    // Brilho do céu: a luz da aurora espalhada perto do horizonte.
    const vec3 glowTint = mix(p.color.rgb, p.q0.rgb, 0.35);
    col += glowTint * p.p1.w * 0.12 * pow(1.0 - up, 5.0) * p.p0.y;
    col += aur * (1.0 + p.p1.w * 0.6);
    col += stars(d, t) * (1.0 - clamp(dot(aur, vec3(0.33)) * 3.0, 0.0, 0.9));
    return col;
}

void main() {
    const vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const float amount = clamp(p.q3.x, 0.0, 1.0);
    if (amount <= 0.0) { o_color = src; return; }
    const vec2 px = p.p2.xy + v_uv * p.p2.zw;
    const float t = p.p3.x;
    const vec3 d = view_ray(px);
    vec3 col;
    if (d.y >= 0.0) {
        col = sky(d, t);
    } else {
        // Reflexo: o raio espelhado na água, com ondulação que cresce perto.
        const float ripple = p.p3.w;
        const vec2 water = d.xz / max(-d.y, 0.02);
        const vec2 wave = vec2(aurea_fbm(water * 0.8 + vec2(t * 0.15, 0.0), 3, 0.5),
                               aurea_fbm(water * 0.8 + vec2(3.7, t * 0.12), 3, 0.5)) - 0.5;
        vec3 r = vec3(d.x + wave.x * ripple * 0.06, -d.y + abs(wave.y) * ripple * 0.03, d.z);
        r = normalize(r);
        // Fresnel simples: água calma é quase espelho; mais ainda perto do horizonte.
        const float fresnel = mix(0.6, 1.0, pow(1.0 - clamp(-d.y, 0.0, 1.0), 3.0));
        const vec3 deep = p.q1.rgb * 0.35;
        col = deep + sky(r, t) * clamp(p.p3.z, 0.0, 1.0) * fresnel;
        // Faixa de névoa na linha do horizonte.
        col = mix(col, p.q1.rgb * 1.4 + mix(p.color.rgb, p.q0.rgb, 0.4) * p.p1.w * 0.05,
                  exp(-abs(d.y) * 220.0) * 0.5);
    }
    // Exposição: 40 é a referência neutra; cada 10 pontos ≈ meia parada.
    col *= exp2((p.p1.x - 40.0) / 20.0);
    col = max(col, vec3(0.0));
    o_color = mix(src, vec4(col, 1.0), amount);
}
