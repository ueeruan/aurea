#version 450
// =============================================================================
//  Aurea / shaders / effects / luma_dissolve.frag
//
//  Luma Dissolve: a camada some (ou aparece) pelo valor de um MAPA 0..1 — o
//  brilho dela, um ruído, um degradê linear ou radial, ou o brilho de outra
//  camada. A frente `t` anda de −(suavidade+borda) até 1; o pixel fica se o
//  mapa passa da frente (rampa da suavidade) e, logo depois da frente, a
//  borda pinta a cor da borda (a "queimadura").
//
//  0% = camada exata (o efeito sai da cadeia no C++); 100% = alfa 0.
//
//  p0 = frente t, suavidade, largura da borda, mapa
//  p1 = inverter, escala do ruído (px), semente, ângulo (rad)
//  p2 = centro (px), meia extensão do degradê linear (px), raio do radial (px)
//  p3 = composição ← camada (a.x, a.y, c.x, c.y); p4 = translação xy, região do mapa xy
//      (padrões 5..21, sem mapa: p4.zw = rotação do padrão (rad), detalhe 0..1)
//  p5 = região do mapa zw, tem mapa (0/1), tudo apagado (0/1)
//  color = cor da borda (linear pré-multiplicada)
// =============================================================================
#include "cc_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // outra camada (px da composição)

// --- Padrões gerados (0.2.1): escritos do zero, determinísticos -------------
// q = px girado pela rotação do padrão em volta do centro da caixa, na escala
// pedida (1 unidade = "Escala do ruído" px). d = detalhe 0..1.
uint ld_salt() { return uint(p.p1.z) * 2654435761u + 1013904223u; }
float ld_h(vec2 cell) { return aurea_hash3(cell, ld_salt()); }
vec2 ld_h2(vec2 cell) { return vec2(ld_h(cell), aurea_hash3(cell + vec2(71.0, 113.0), ld_salt())); }

// Voronoi: distância ao ponto mais próximo (x), à 2ª (y) e a célula dele (zw).
vec4 ld_voronoi(vec2 q) {
    const vec2 i = floor(q);
    float d1 = 8.0, d2 = 8.0;
    vec2 best = i;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            const vec2 c = i + vec2(x, y);
            const float d = length(c + ld_h2(c) - q);
            if (d < d1) { d2 = d1; d1 = d; best = c; }
            else if (d < d2) d2 = d;
        }
    return vec4(d1, d2, best);
}

float ld_fbm(vec2 q, int oct) { return aurea_fbm(q + vec2(p.p1.z * 17.13, p.p1.z * 31.71), oct, 0.5); }

float pattern_value(vec2 px, int kind) {
    const float s = max(p.p1.y, 1.0);
    const float d = clamp(p.p4.w, 0.0, 1.0);
    const vec2 q = cc_rot(px - cc_box_center(), p.p4.z) / s;
    const int oct = 2 + int(d * 4.0 + 0.5);
    float m = 0.0;
    if (kind == 5) {            // Orgânico: blobs suaves, domínio torcido
        const vec2 w = vec2(ld_fbm(q * 0.6, 3), ld_fbm(q * 0.6 + 5.2, 3)) - 0.5;
        m = ld_fbm(q * 0.7 + w * 1.5, 3);
        m = (m - 0.5) * 2.2 + 0.5;
    } else if (kind == 6) {     // Poeira de pixels: grão fino por célula
        m = ld_h(floor(q * mix(6.0, 30.0, d)));
    } else if (kind == 7) {     // Linhas de varredura com glitch
        const float rows = mix(4.0, 16.0, d);
        const float row = floor(q.y * rows);
        const float shift = (ld_h(vec2(row, 3.0)) - 0.5) * 3.0;
        m = 0.65 * ld_h(vec2(row, 1.0)) + 0.35 * aurea_value_noise(vec2((q.x + shift) * 3.0, row * 7.0));
    } else if (kind == 8) {     // Grão grosso / ruído
        const float grain = ld_h(floor(q * mix(3.0, 10.0, d)));
        m = mix(ld_fbm(q * 1.5, 3), grain, 0.55);
        m = (m - 0.5) * 1.8 + 0.5;
    } else if (kind == 9) {     // Blocos quadrados
        m = ld_h(floor(q * mix(0.6, 2.5, d)));
    } else if (kind == 10) {    // Grade / hachura cruzada: células, linhas por último
        const vec2 g = q * mix(1.0, 3.0, d);
        const vec2 f = abs(fract(g) - 0.5);
        const float line = 1.0 - min(f.x, f.y) * 2.0;
        m = 0.55 * ld_h(floor(g)) + 0.45 * line * line;
    } else if (kind == 11) {    // Chamas: manchas grandes que sobem
        const vec2 fq = vec2(q.x * 0.6, q.y * 0.35);
        const float t = aurea_turbulence(fq + vec2(0.0, ld_fbm(fq * 2.0, 2)), oct, 0.55);
        const float up = clamp((px.y - p.box.y) / max(p.box.w > 0.0 ? p.box.w : 1.0, 1.0), 0.0, 1.0);
        m = clamp(0.6 * (1.0 - t) + 0.4 * (1.0 - up), 0.0, 1.0);
    } else if (kind == 12) {    // Manchas de tinta
        const float t = aurea_turbulence(q * 0.8, oct, 0.6);
        m = 1.0 - t * 1.6 + (ld_fbm(q * 0.3, 2) - 0.5) * 0.8;
    } else if (kind == 13) {    // Camuflagem média: ruído em patamares
        const float levels = mix(3.0, 8.0, d);
        const float n = clamp((ld_fbm(q * 0.9, 4) - 0.5) * 2.4 + 0.5, 0.0, 0.999);
        const float lvl = floor(n * levels);
        m = (lvl + 0.5) / levels * 0.8 + 0.2 * ld_h(vec2(lvl, 9.0));
    } else if (kind == 14) {    // Grade hexagonal: cada hexágono some de uma vez
        const vec2 r = vec2(1.0, 1.7320508);
        const vec2 g = q * mix(0.8, 2.5, d);
        const vec2 a = mod(g, r) - r * 0.5, b = mod(g - r * 0.5, r) - r * 0.5;
        const vec2 h = dot(a, a) < dot(b, b) ? a : b;
        const vec2 cell = floor((g - h) * 2.0 + 0.5);
        m = 0.85 * ld_h(cell) + 0.15 * (1.0 - length(h) * 1.7);
    } else if (kind == 15) {    // Faixas horizontais longas
        const float rows = mix(1.5, 6.0, d);
        const float row = floor(q.y * rows);
        m = 0.8 * ld_h(vec2(row, 5.0)) + 0.2 * clamp(q.x * 0.1 + 0.5, 0.0, 1.0);
    } else if (kind == 16) {    // Meio-tom / anéis: pontos em grade, miolo por último
        const vec2 g = q * mix(1.5, 5.0, d);
        const float spot = length(fract(g) - 0.5) * 1.41421;
        const float ring = 0.5 + 0.5 * cos(length(q) * 6.2831853 * 0.5);
        m = clamp((1.0 - spot) * 0.75 + ring * 0.25, 0.0, 1.0);
    } else if (kind == 17) {    // Topográfico: curvas de nível
        const float levels = mix(3.0, 12.0, d);
        const float n = ld_fbm(q * 0.5, 4);
        const float band = fract(n * levels);
        m = 0.6 * band + 0.4 * n;
    } else if (kind == 18) {    // Borda áspera: frente reta com bordas rasgadas
        const float along = clamp(q.x / max(p.box.z / s, 1e-3) + 0.5, 0.0, 1.0);
        const float rough = aurea_turbulence(q * 2.0, oct, 0.6) - 0.5;
        m = along + rough * mix(0.15, 0.6, d);
    } else if (kind == 19) {    // Respingos afiados: pontas em volta de cada mancha
        const vec2 g = q * 0.8;
        const vec4 v = ld_voronoi(g);
        const vec2 to = g - (v.zw + ld_h2(v.zw));
        const float spikes = 0.5 + 0.5 * sin(atan(to.y, to.x) * mix(5.0, 11.0, d) + ld_h(v.zw) * 6.28);
        m = v.x * (1.0 + 0.9 * spikes * spikes);
    } else if (kind == 20) {    // Bolinhas / partículas circulares
        const vec4 v = ld_voronoi(q * mix(0.8, 2.5, d));
        const float radius = mix(0.25, 0.55, ld_h(v.zw + 3.0));
        m = clamp(v.x / radius, 0.0, 1.0) * 0.8 + 0.2 * ld_h(v.zw);
    } else {                    // Estilhaços: cacos de vidro, a fresta por último
        const vec4 v = ld_voronoi(q * mix(0.6, 2.0, d));
        const float crack = clamp((v.y - v.x) * 4.0, 0.0, 1.0);
        m = 0.85 * ld_h(v.zw) + 0.15 * crack;
    }
    return clamp(m, 0.0, 1.0);
}

float map_value(vec2 px, vec4 src) {
    const int kind = int(p.p0.w + 0.5);
    float m;
    if (kind >= 5) {
        m = pattern_value(px, kind);
    } else if (kind == 1) {
        // Ruído: manchas na escala pedida, esticadas para cobrir 0..1.
        const float s = max(p.p1.y, 1.0);
        const vec2 q = px / s + vec2(p.p1.z * 17.13, p.p1.z * 31.71);
        m = clamp((aurea_fbm(q, 5, 0.5) - 0.5) * 2.4 + 0.5, 0.0, 1.0);
    } else if (kind == 2) {
        const vec2 dir = vec2(cos(p.p1.w), sin(p.p1.w));
        m = clamp(dot(px - p.p2.xy, dir) / max(2.0 * p.p2.z, 1e-3) + 0.5, 0.0, 1.0);
    } else if (kind == 3) {
        m = clamp(length(px - p.p2.xy) / max(p.p2.w, 1e-3), 0.0, 1.0);
    } else if (kind == 4) {
        if (p.p5.z < 0.5) return 0.0;
        const vec2 comp = vec2(p.p3.x * px.x + p.p3.z * px.y + p.p4.x, p.p3.y * px.x + p.p3.w * px.y + p.p4.y);
        const vec4 c = texture(u_tex1, (comp - p.p4.zw) / max(p.p5.xy, vec2(1e-6)));
        m = aurea_linear_to_srgb(clamp(aurea_luma(max(c.rgb, vec3(0.0))), 0.0, 1.0));
    } else {
        // O brilho da própria camada (cor sem o alfa), na escala que o olho vê.
        const vec3 rgb = src.a > 1e-4 ? src.rgb / src.a : vec3(0.0);
        m = aurea_linear_to_srgb(clamp(aurea_luma(max(rgb, vec3(0.0))), 0.0, 1.0));
    }
    return p.p1.x > 0.5 ? 1.0 - m : m;
}

float ink_coverage(vec2 px) {
    const float progress = p.p0.x;
    if (progress <= 0.0) return 0.0;
    if (progress >= 1.0) return 1.0;
    const float edge = max(p.p0.y, max(p.box.z, p.box.w) * 0.0007);
    const float fibers = p.p2.w;
    float total = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (float(i) >= p.p1.x) break;
        const vec2 id = vec2(i, 17.0);
        const vec2 random = ld_h2(id);
        const vec2 center = i == 0 ? p.p2.xy : p.p2.xy + (random - 0.5) * p.box.zw * 0.82;
        const float delay = i == 0 ? 0.0 : 0.08 + 0.26 * random.y;
        if (progress <= delay) continue;
        const float growth = (progress - delay) / (1.0 - delay);
        const float radius = p.p2.z * growth * growth * (i == 0 ? 1.45 : 0.42 + 0.48 * random.x);
        const vec2 local = px - center;
        const float dist = length(local);
        if (dist > radius * 1.7 + edge) continue;
        const vec2 direction = local / max(dist, 0.001);
        const vec2 offset = random * 48.0;
        const float lobes = ld_fbm(direction * 4.0 + offset, 3) - 0.5;
        const float texture = ld_fbm(direction * 22.0 + local * 0.04 + offset, 3);
        const float wisps = pow(clamp(texture * 1.35, 0.0, 1.0), 3.0) - 0.25;
        const float rim = radius * (1.0 + fibers * (0.48 * lobes + 0.53 * wisps));
        const float wet = smoothstep(-edge, edge, rim - dist);
        const float dilute = smoothstep(-2.5 * edge, edge, rim - dist);
        total = max(total, mix(dilute * 0.45, 1.0, wet));
    }
    return total;
}

void main() {
    const vec2 px = cc_px();
    const vec4 src = cc_at(px);
    if (p.p0.w > 21.5) {
        const float coverage = ink_coverage(px);
        if (p.p0.z < 0.5) o_color = p.color * coverage;
        else o_color = src * (p.p0.z < 1.5 ? coverage : 1.0 - coverage);
        return;
    }
    if (p.p5.w > 0.5) { o_color = vec4(0.0); return; }
    const float m = map_value(px, src);
    const float t = p.p0.x;
    const float s = max(p.p0.y, 1e-4);
    const float w = max(p.p0.z, 0.0);
    // Fica o que passa da frente (rampa de largura `s`).
    const float keep = smoothstep(t, t + s, m);
    vec3 rgb = src.rgb;
    if (w > 0.0) {
        // Borda: logo depois da rampa, some suave até `w` adiante.
        const float band = 1.0 - smoothstep(t + s, t + s + w, m);
        const float k = clamp(band, 0.0, 1.0) * p.color.a;
        rgb = mix(rgb, p.color.rgb / max(p.color.a, 1e-4) * src.a, k);
    }
    o_color = vec4(rgb, src.a) * keep;
}
