#version 450
// =============================================================================
//  Aurea / shaders / effects / audio_waveform.frag
//
//  Forma de onda de áudio: as amostras do trecho (u_tex1, `n`×1, R = valor
//  com sinal, −1..1) desenhadas ao longo da reta do ponto inicial ao final.
//  Positivo sobe (lado A), negativo desce.
//
//    Digital            uma barra por amostra, do caminho até o valor
//    Linhas analógicas  a poligonal pelos valores
//    Pontos analógicos  um ponto em cada valor
//
//  No analógico, a semente sorteia uma pequena variação de altura por ponto
//  (o traço "à mão" de um osciloscópio). O traço tem a cor interna no meio e
//  a externa na borda; a suavidade abre a borda.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // as amostras (n × 1)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // xy = ponto inicial, zw = ponto final (px da camada)
    vec4 p1;   // x = altura máxima (px), y = espessura (px), z = suavidade 0..1, w = amostras exibidas
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;   // x = exibição (0 digital, 1 linhas, 2 pontos), y = compor sobre o original, z = 1 há som, w = semente
    vec4 color;   // cor interna (linear)
    vec4 q0;      // cor externa (linear)
    vec4 q1;
    vec4 q2;
    vec4 q3;
} p;

/// Valor (px, com sinal) da amostra `i` de `n`.
float height_at(float i, float n, bool analog) {
    if (p.p3.z < 0.5 || i < 0.0 || i >= n) return 0.0;
    float v = texture(u_tex1, vec2((i + 0.5) / n, 0.5)).r;
    if (analog) {
        const float r = aurea_hash(uvec2(uint(i), uint(p.p3.w) * 7919u + 17u));
        v *= 0.85 + 0.3 * r;
    }
    return v * p.p1.x;
}

void main() {
    const vec4 base = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float n = max(floor(p.p1.w + 0.5), 2.0);
    const vec2 d = p.p0.zw - p.p0.xy;
    const float L = max(length(d), 1e-4);
    const vec2 dir = d / L;
    const vec2 rel = point - p.p0.xy;
    const float along = dot(rel, dir);                 // px ao longo do caminho
    const float v = dot(rel, vec2(dir.y, -dir.x));     // px acima do caminho (lado A)
    const float u = along / L;
    const float hw = 0.5 * max(p.p1.y, 0.0);
    const float px = 1.0 / max(p.texel.z, 1e-4);
    const float aa = px + p.p1.z * max(hw, px);
    const int mode = int(p.p3.x + 0.5);
    const bool analog = mode != 0;
    const float seg = L / max(n - 1.0, 1.0);

    float dist = 1e6;
    if (mode == 0) {
        // Digital: barras verticais do caminho até o valor, centradas nas amostras.
        const float x = u * (n - 1.0);
        const float fi = floor(x + 0.5);
        for (int k = -1; k <= 1; ++k) {
            const float i = fi + float(k);
            if (i < 0.0 || i >= n) continue;
            const float H = height_at(i, n, false);
            const float dx = abs(along - i * seg);
            const float lo = min(0.0, H), hi = max(0.0, H);
            const float dy = v < lo ? lo - v : (v > hi ? v - hi : 0.0);
            dist = min(dist, max(dx, dy));
        }
    } else if (mode == 1) {
        // Linhas: distância à poligonal (dois segmentos vizinhos).
        const float x = u * (n - 1.0);
        const float i0 = clamp(floor(x), 0.0, n - 2.0);
        for (int k = -1; k <= 1; ++k) {
            const float i = i0 + float(k);
            if (i < 0.0 || i > n - 2.0) continue;
            const vec2 a = vec2(i * seg, height_at(i, n, true));
            const vec2 b = vec2((i + 1.0) * seg, height_at(i + 1.0, n, true));
            const vec2 q = vec2(along, v);
            const vec2 ab = b - a;
            const float t = clamp(dot(q - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
            dist = min(dist, length(q - (a + ab * t)));
        }
    } else {
        // Pontos: um disco em cada amostra.
        const float x = u * (n - 1.0);
        const float fi = floor(x + 0.5);
        for (int k = -1; k <= 1; ++k) {
            const float i = fi + float(k);
            if (i < 0.0 || i >= n) continue;
            dist = min(dist, length(vec2(along - i * seg, v - height_at(i, n, true))));
        }
    }
    const float cov = aurea_band_coverage(dist, hw, aa);
    // Meio do traço = cor interna; borda = externa.
    const float t = clamp(dist / max(hw + 0.5 * aa, 1e-4), 0.0, 1.0);
    const vec3 col = mix(p.color.rgb, p.q0.rgb, t);
    const vec4 paint = vec4(clamp(col, 0.0, 1.0), 1.0) * cov;
    o_color = p.p3.y > 0.5 ? paint + base * (1.0 - cov) : paint;
}
