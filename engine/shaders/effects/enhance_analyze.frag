#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_analyze.frag
//
//  Aprimorar, modo Automático: a ESTIMATIVA do quadro (o "Estimate" dos
//  aprimoradores de vídeo), toda na GPU — sem leitura para a CPU, sem espera.
//  Cada texel de saída resume um bloco 4×4 da entrada num ponto (o centro de
//  um sub-bloco 2×2). A saída tem DOIS ladrilhos lado a lado, que a
//  redução (enhance_reduce.frag) reduz cada um a um texel:
//
//   ladrilho 0 — ruído e blocos, nas regiões LISAS (sem borda):
//     R = Σ peso·|L|, G = Σ peso. L é a máscara de Immerkær [1 −2 1; −2 4 −2;
//         1 −2 1] (cego a degradês e a bordas retas): σ = √(π/2)/6 · R/G.
//     B = Σ peso·excesso, A = Σ peso. Excesso = degrau na borda da grade de
//         8 px menos o degrau dos vizinhos de dentro do bloco (média zero sem
//         blocos: o ruído não finge bloco).
//   ladrilho 1 — nitidez, nas bordas FORTES:
//     R = Σ peso·q, G = Σ peso. q = 2·d1/d2 − 1 (d1 = maior diferença em 2 px,
//         d2 = diferença em 4 px): 1 = degrau seco, 0 = rampa larga (borrado).
// =============================================================================
#include "../common/bindings.glsl"
#include "enhance_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;   // entrada (linear pré-multiplicada)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho da entrada, zw = tamanho da entrada (texels)
    vec4 tiles;   // xy = tamanho de UM ladrilho de saída
    vec4 grid;    // x = texels por px da camada, yz = origem da região (px da camada), w = grade medível (0/1)
} p;

float luma_at(ivec2 q) {
    const ivec2 hi = ivec2(p.texel.zw) - ivec2(1);
    return enh_from_linear(texelFetch(u_src, clamp(q, ivec2(0), hi), 0)).x;
}

/// Luma num ponto contínuo (texels): as amostras ao lado da borda da grade.
float luma_texel(vec2 t) { return enh_from_linear(textureLod(u_src, t * p.texel.xy, 0.0)).x; }

void main() {
    const vec2 tiles = max(p.tiles.xy, vec2(1.0));
    const vec2 o = floor(v_uv * vec2(tiles.x * 2.0, tiles.y));
    const bool second = o.x >= tiles.x;
    const ivec2 block = ivec2(int(o.x - (second ? tiles.x : 0.0)), int(o.y)) * 4;
    vec4 acc = vec4(0.0);
    // UM ponto por bloco (¼ do custo; em 1080p ainda são 130 mil pontos): o
    // sub-bloco 2×2 alterna entre blocos vizinhos, e os quatro aparecem —
    // a medida da grade de 8 px vê as quatro fases.
    const int phase = ((block.x >> 2) & 1) | (((block.y >> 2) & 1) << 1);
    for (int k = phase; k <= phase; ++k) {
        const ivec2 c = block + ivec2((k & 1) * 2 + 1, (k >> 1) * 2 + 1);
        if (!second) {
            float y[9];
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i) y[j * 3 + i] = luma_at(c + ivec2(i - 1, j - 1));
            const float lap = abs(y[0] - 2.0 * y[1] + y[2] - 2.0 * y[3] + 4.0 * y[4] - 2.0 * y[5] + y[6] - 2.0 * y[7] + y[8]);
            const float gx = (y[2] + 2.0 * y[5] + y[8]) - (y[0] + 2.0 * y[3] + y[6]);
            const float gy = (y[6] + 2.0 * y[7] + y[8]) - (y[0] + 2.0 * y[1] + y[2]);
            const float grad = max(abs(gx), abs(gy)) * 0.25;
            const float flatness = 1.0 - smoothstep(0.06, 0.2, grad);
            acc.x += flatness * lap;
            acc.y += flatness;
            if (p.grid.w > 0.5) {
                // A borda da grade de 8 px (px da camada) mais perto deste
                // ponto, nos dois eixos: o degrau nela contra os de dentro.
                for (int axis = 0; axis < 2; ++axis) {
                    const float s = max(p.grid.x, 1e-3);
                    const float origin = axis == 0 ? p.grid.y : p.grid.z;
                    const float t = float(axis == 0 ? c.x : c.y) + 0.5;
                    const float bnd = floor((origin + t / s) / 8.0 + 0.5) * 8.0;
                    const float tb = (bnd - origin) * s;
                    if (abs(t - tb) > 2.0) continue;
                    const float other = float(axis == 0 ? c.y : c.x) + 0.5;
                    const vec2 dirv = axis == 0 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
                    const vec2 base = axis == 0 ? vec2(tb, other) : vec2(other, tb);
                    const float l1 = luma_texel(base - 1.5 * dirv), l0 = luma_texel(base - 0.5 * dirv);
                    const float r0 = luma_texel(base + 0.5 * dirv), r1 = luma_texel(base + 1.5 * dirv);
                    const float inner = 0.5 * (abs(l0 - l1) + abs(r1 - r0));
                    const float excess = abs(r0 - l0) - inner;
                    const float w = 1.0 - smoothstep(0.008, 0.04, inner);
                    acc.z += w * excess;
                    acc.w += w;
                }
            }
        } else {
            // Cruz de raio 2: a maior diferença em 2 px perto do ponto e a
            // diferença em 4 px, no eixo com a borda mais forte.
            float hx[5], hy[5];
            for (int i = 0; i < 5; ++i) {
                hx[i] = luma_at(c + ivec2(i - 2, 0));
                hy[i] = luma_at(c + ivec2(0, i - 2));
            }
            const float d2x = abs(hx[4] - hx[0]), d2y = abs(hy[4] - hy[0]);
            const float d1x = max(abs(hx[2] - hx[0]), max(abs(hx[3] - hx[1]), abs(hx[4] - hx[2])));
            const float d1y = max(abs(hy[2] - hy[0]), max(abs(hy[3] - hy[1]), abs(hy[4] - hy[2])));
            const bool useX = d2x >= d2y;
            const float d1 = useX ? d1x : d1y, d2 = useX ? d2x : d2y;
            const float gate = smoothstep(0.03, 0.08, d2);
            const float q = clamp(2.0 * d1 / max(d2, 1e-4) - 1.0, 0.0, 1.0);
            acc.x += gate * q;
            acc.y += gate;
        }
    }
    o_color = acc;
}
