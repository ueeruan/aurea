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
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer MaskData { vec4 d[]; } md;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;   // x, y, w, h: px da fonte que o alvo cobre
    vec4 info;     // x = 1º cabeçalho no buffer, y = nº de máscaras, z = texels por px, w = cobertura inicial
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
    vec2 pos = p.region.xy + v_uv * p.region.zw;
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
        float margin = abs(expand) + 4.0 * sigma + 2.0 / k;
        float cov = 0.0;
        if (!(pos.x < h2.x - margin || pos.y < h2.y - margin || pos.x > h2.z + margin || pos.y > h2.w + margin)) {
            float d2 = 1e30;
            int wind = 0;
            for (int i = 0; i < ne; ++i) {
                vec4 e = md.d[e0 + i];
                vec2 ab = e.zw - e.xy;
                vec2 ap = pos - e.xy;
                float t = clamp(dot(ap, ab) / max(dot(ab, ab), 1e-12), 0.0, 1.0);
                vec2 q = ap - ab * t;
                d2 = min(d2, dot(q, q));
                float cr = ab.x * ap.y - ab.y * ap.x;
                if (e.y <= pos.y) {
                    if (e.w > pos.y && cr > 0.0) wind += 1;
                } else if (e.w <= pos.y && cr < 0.0) {
                    wind -= 1;
                }
            }
            float dist = sqrt(d2);
            float sd = (wind != 0 ? dist : -dist) + expand;
            if (sigma > 0.0) {
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
    }
    o_color = vec4(clamp(acc, 0.0, 1.0), 0.0, 0.0, 1.0);
}
