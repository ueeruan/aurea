#version 450
// =============================================================================
//  Aurea / shaders / effects / spectrum_analyzer.frag
//
//  Espectro de áudio: as faixas do som (u_tex1, `n`×1, R = magnitude linear,
//  1 = seno em escala cheia) desenhadas ao longo de um caminho:
//
//    reta     do ponto inicial ao final; as faixas sobem (lado A), descem
//             (lado B) ou os dois;
//    polar    raios saindo do ponto inicial, as faixas dando a volta a partir
//             da direção do ponto final.
//
//  Exibição: digital (barras), linhas analógicas (poligonal pelos topos) ou
//  pontos analógicos. O traço tem a cor interna no meio e a externa na borda;
//  a interpolação de matiz gira a cor ao longo das faixas (simetria espelha,
//  matiz dinâmica começa o giro na faixa mais forte). Misturar cores
//  sobrepostas soma as barras vizinhas que se cruzam; sem ela, fica a mais
//  próxima.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // as faixas (n × 1)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // xy = ponto inicial, zw = ponto final (px da camada)
    vec4 p1;   // x = altura máxima (px), y = espessura (px), z = suavidade 0..1, w = faixas
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;   // x = exibição, y = lado (0 A, 1 B, 2 A e B), z = polar, w = compor sobre o original
    vec4 color;   // cor interna (linear)
    vec4 q0;      // cor externa (linear)
    vec4 q1;      // x = interpolação de matiz (voltas), y = início dinâmico (0..1), z = simetria, w = misturar
    vec4 q2;      // x = 1 há som
    vec4 q3;
} p;

float band_at(float i, float n) {
    if (p.q2.x < 0.5 || i < 0.0 || i >= n) return 0.0;
    return max(texture(u_tex1, vec2((i + 0.5) / n, 0.5)).r, 0.0) * p.p1.x;
}

/// A cor da faixa `i`: interna/externa pelo meio/borda do traço e o giro de
/// matiz ao longo das faixas.
vec3 band_color(float i, float n, float edge) {
    vec3 col = mix(p.color.rgb, p.q0.rgb, edge);
    if (abs(p.q1.x) > 1e-5) {
        float t = fract((i + 0.5) / n - p.q1.y);
        if (p.q1.z > 0.5) t = 1.0 - abs(2.0 * t - 1.0);
        col = aurea_hsv_shift(col, p.q1.x * t, 1.0);
    }
    return clamp(col, 0.0, 1.0);
}

void main() {
    const vec4 base = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float n = max(floor(p.p1.w + 0.5), 1.0);
    const vec2 P0 = p.p0.xy;
    const vec2 P1 = p.p0.zw;
    const bool polar = p.p3.z > 0.5;

    // Coordenadas do pixel no caminho: `s` (posição em faixas, contínua),
    // `h` (altura acima do caminho), `w` (px por faixa AQUI).
    float s, h, w;
    if (!polar) {
        const vec2 d = P1 - P0;
        const float L = max(length(d), 1e-4);
        const vec2 dir = d / L;
        const vec2 rel = point - P0;
        s = dot(rel, dir) / L * n;
        h = dot(rel, vec2(dir.y, -dir.x));
        w = L / n;
    } else {
        const vec2 rel = point - P0;
        const float r = length(rel);
        const float a0 = atan(P1.y - P0.y, P1.x - P0.x);
        s = fract((atan(rel.y, rel.x) - a0) / AUREA_TAU) * n;
        h = r;
        w = AUREA_TAU * max(r, 1e-3) / n;
    }
    const int side = polar ? 0 : int(p.p3.y + 0.5);
    const float hw = 0.5 * max(p.p1.y, 0.0);
    const float px = 1.0 / max(p.texel.z, 1e-4);
    const float aa = px + p.p1.z * max(hw, px);
    const int mode = int(p.p3.x + 0.5);
    const bool blend = p.q1.w > 0.5;

    vec3 acc = vec3(0.0);
    float cov = 0.0;
    float bestD = 1e6;
    vec3 bestC = vec3(0.0);
    const float fi = floor(s);
    for (int k = -1; k <= 1; ++k) {
        float i = fi + float(k);
        // No polar a volta fecha: a faixa antes da 0 é a última.
        if (polar) i = mod(i + n, n);
        if (i < 0.0 || i >= n) continue;
        float dist;
        if (mode == 0) {
            const float H = band_at(i, n);
            // Digital: faixa sem som não desenha barra (nem um ponto no caminho).
            if (H < 0.5 * px) continue;
            const float dAlong = abs(s - (fi + float(k)) - 0.5) * w;
            float dy;
            if (side == 0) dy = h < 0.0 ? -h : max(h - H, 0.0);
            else if (side == 1) dy = h > 0.0 ? h : max(-h - H, 0.0);
            else dy = max(abs(h) - H, 0.0);
            dist = max(dAlong, dy);
        } else if (mode == 1) {
            // Segmento do topo da faixa i ao da i+1.
            const float j = polar ? mod(i + 1.0, n) : i + 1.0;
            const float Ha = band_at(i, n), Hb = band_at(j, n);
            const vec2 a = vec2((fi + float(k) + 0.5) * w, Ha);
            const vec2 b = vec2((fi + float(k) + 1.5) * w, Hb);
            const float hs = side == 1 ? -h : (side == 2 ? abs(h) : h);
            const vec2 q = vec2(s * w, hs);
            const vec2 ab = b - a;
            const float t = clamp(dot(q - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
            dist = length(q - (a + ab * t));
            if (!polar && (i + 1.0 >= n)) dist = 1e6;
        } else {
            const float H = band_at(i, n);
            const float hs = side == 1 ? -h : (side == 2 ? abs(h) : h);
            dist = length(vec2((s - (fi + float(k)) - 0.5) * w, hs - H));
        }
        const float c = aurea_band_coverage(dist, hw, aa);
        if (c <= 0.0) continue;
        const float edge = clamp(dist / max(hw + 0.5 * aa, 1e-4), 0.0, 1.0);
        const vec3 col = band_color(i, n, edge);
        if (blend) {
            acc += col * c;
            cov = 1.0 - (1.0 - cov) * (1.0 - c);
        } else if (dist < bestD) {
            bestD = dist;
            bestC = col;
            cov = max(cov, c);
        }
    }
    if (!polar && (s < 0.0 || s > n)) cov = 0.0;
    // Pré-multiplicado: a cor nunca passa da cobertura.
    const vec3 col = blend ? min(acc, vec3(cov)) : bestC * cov;
    const vec4 paint = vec4(col, cov);
    o_color = p.p3.w > 0.5 ? paint + base * (1.0 - cov) : paint;
}
