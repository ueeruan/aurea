#version 450
// =============================================================================
//  Aurea / shaders / mask / raster.frag
//
//  Cobertura das máscaras de UMA camada (R8), na resolução de trabalho dela.
//  O bloco (render/MaskRaster.hpp) chega no storage buffer: por máscara, 3
//  vec4 de cabeçalho e as arestas do polígono achatado na CPU.
//
//  Por texel: enrolamento (regra não-zero) e distância assinada ao polígono.
//  Sem feather, cobertura = clamp(d·k + ½) — a área exata do texel para uma
//  borda reta. Com feather, o gaussiano do semiplano: ½(1 + erf(d/σ√2)). A
//  expansão soma na distância (dilata/erode com cantos redondos). Fora da
//  caixa da máscara (mais a margem do feather) nem entra no laço de arestas.
//  A matemática é a mesma de mask::coverage_at (C++), que os testes comparam.
//  h1.w = 1: queda LINEAR do feather (rampa reta de largura 4σ, com o texel);
//  0 = a gaussiana de sempre. Modos 5 Clarear = max(acc, cov) e 6 Escurecer =
//  min(acc, cov) (AE compositing, 2026-11-10).
//  Lote M: h1.w += 2 = bloco ESTENDIDO. Depois das arestas vem 1 vec4 (σ do
//  feather Y, tem raio variável, maior |raio|, 0) e, com raio variável, 1 vec4
//  por aresta (raio no começo, raio no fim). O raio (px, + para fora) desloca
//  o meio da rampa e soma na largura; o feather X/Y vale pela normal da borda.
//  Máscara sem nada disso roda as mesmas contas de antes.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer MaskData { vec4 d[]; } md;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;   // x, y, w, h: px da fonte que o alvo cobre
    vec4 info;     // x = 1º cabeçalho no buffer, y = nº de máscaras, z = texels por px, w = cobertura inicial
    vec4 maskX;
    vec4 maskY;
} p;

float erf_approx(float x) {
    // Abramowitz–Stegun 7.1.26 (erro < 1,5e-7) — a mesma do C++.
    float s = sign(x);
    x = abs(x);
    float t = 1.0 / (1.0 + 0.3275911 * x);
    float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);
    return s * y;
}

void main() {
    vec3 source = vec3(p.region.xy + v_uv * p.region.zw, 1.0);
    vec2 pos = vec2(dot(p.maskX.xyz, source), dot(p.maskY.xyz, source));
    int first = int(p.info.x + 0.5);
    int count = int(p.info.y + 0.5);
    float k = p.info.z;
    float acc = p.info.w;
    for (int m = 0; m < count; ++m) {
        int hb = first + m * 3;
        vec4 h0 = md.d[hb];
        vec4 h1 = md.d[hb + 1];
        vec4 h2 = md.d[hb + 2];
        int op = int(h0.x + 0.5);
        float sigma = h0.w;
        float expand = h1.x;
        int e0 = first + int(h1.y + 0.5);
        int ne = int(h1.z + 0.5);
        int flags = int(h1.w + 0.5);
        bool linear = (flags == 1 || flags == 3);
        bool extended = flags >= 2;
        float sigmaY = sigma;
        float maxRadius = 0.0;
        bool variable = false;
        if (extended) {
            vec4 x0 = md.d[e0 + ne];
            sigmaY = x0.x;
            variable = x0.y > 0.5;
            maxRadius = x0.z;
        }
        float margin = abs(expand) + 4.0 * max(sigma, sigmaY) + maxRadius + 2.0 / k;
        float cov = 0.0;
        if (!(pos.x < h2.x - margin || pos.y < h2.y - margin || pos.x > h2.z + margin || pos.y > h2.w + margin)) {
            float d2 = 1e30;
            int wind = 0;
            float bestT = 0.0;
            vec2 bestQ = vec2(0.0);
            int bestI = 0;
            for (int i = 0; i < ne; ++i) {
                vec4 e = md.d[e0 + i];
                vec2 ab = e.zw - e.xy;
                vec2 ap = pos - e.xy;
                float t = clamp(dot(ap, ab) / max(dot(ab, ab), 1e-12), 0.0, 1.0);
                vec2 q = ap - ab * t;
                float dq = dot(q, q);
                if (dq < d2) { d2 = dq; bestT = t; bestQ = q; bestI = i; }
                float cr = ab.x * ap.y - ab.y * ap.x;
                if (e.y <= pos.y) {
                    if (e.w > pos.y && cr > 0.0) wind += 1;
                } else if (e.w <= pos.y && cr < 0.0) {
                    wind -= 1;
                }
            }
            float dist = sqrt(d2);
            float sd = (wind != 0 ? dist : -dist) + expand;
            if (extended) {
                float radius = 0.0;
                if (variable) {
                    vec4 rr = md.d[e0 + ne + 1 + bestI];
                    radius = rr.x + (rr.y - rr.x) * bestT;
                }
                vec2 nrm = dist > 1e-6 ? bestQ / dist : vec2(1.0, 0.0);
                float su2 = sigma * sigma * nrm.x * nrm.x + sigmaY * sigmaY * nrm.y * nrm.y;
                float sr = 0.25 * abs(radius);
                float sdv = sd + 0.5 * radius;
                if (su2 <= 0.0 && sr <= 0.0) {
                    cov = clamp(sd * k + 0.5, 0.0, 1.0);
                } else if (linear) {
                    float w = sqrt(16.0 * (su2 + sr * sr) + 1.0 / (k * k));
                    cov = clamp(sdv / w + 0.5, 0.0, 1.0);
                } else {
                    float s = sqrt(su2 + sr * sr + 1.0 / (12.0 * k * k));
                    cov = 0.5 * (1.0 + erf_approx(sdv / (s * 1.41421356)));
                }
            } else if (sigma > 0.0 && linear) {
                float w = sqrt(16.0 * sigma * sigma + 1.0 / (k * k));
                cov = clamp(sd / w + 0.5, 0.0, 1.0);
            } else if (sigma > 0.0) {
                float s = sqrt(sigma * sigma + 1.0 / (12.0 * k * k));
                cov = 0.5 * (1.0 + erf_approx(sd / (s * 1.41421356)));
            } else {
                cov = clamp(sd * k + 0.5, 0.0, 1.0);
            }
        }
        if (h0.y > 0.5) cov = 1.0 - cov;
        cov *= h0.z;
        if (op == 0)      acc = acc + cov * (1.0 - acc);          // Add (união)
        else if (op == 1) acc = acc * (1.0 - cov);                // Subtract
        else if (op == 2) acc = acc * cov;                        // Intersect
        else if (op == 3) acc = acc + cov - 2.0 * acc * cov;      // Difference
        else if (op == 5) acc = max(acc, cov);                    // Lighten (Clarear)
        else if (op == 6) acc = min(acc, cov);                    // Darken (Escurecer)
    }
    o_color = vec4(clamp(acc, 0.0, 1.0), 0.0, 0.0, 1.0);
}
