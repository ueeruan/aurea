#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_solve.frag
//
//  Aprimorar: os PESOS finais de cada etapa, numa textura 2×1 que todos os
//  passes seguintes leem (enhance_common.glsl). Três modos, como nos
//  aprimoradores de vídeo:
//   - Manual: os sliders são os pesos;
//   - modelo (Vídeo comprimido, Antigo, Animação, Baixa resolução): os pesos
//     do modelo, e os sliders SOMAM a eles (deslocamento, −100..100);
//   - Automático: os pesos saem da estimativa deste quadro (as médias que a
//     redução deixou num texel por ladrilho) e os sliders somam a eles.
//  Tudo na GPU: a estimativa nunca volta para a CPU nem espera um quadro.
//
//  σ do ruído: o Automático mede; nos outros a força do "Reduzir ruído" diz
//  qual σ tratar (a inversa exata da conta do Automático — o mesmo slider
//  vale o mesmo nos dois modos).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_stats;   // 2×1 (só no Automático)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 baseA;   // modelo: (compressão, detalhes, nitidez, ruído), 0..1
    vec4 baseB;   // modelo: (halos, serrilhado/desborrar, −, −)
    vec4 offA;    // sliders (−1..1), mesma ordem
    vec4 offB;
    vec4 flags;   // x = Automático (0/1)
} p;

void main() {
    vec4 a = p.baseA;
    vec4 b = p.baseB;
    if (p.flags.x > 0.5) {
        const vec4 s0 = texelFetch(u_stats, ivec2(0, 0), 0);
        const vec4 s1 = texelFetch(u_stats, ivec2(1, 0), 0);
        // Immerkær: σ = √(π/2)/6 · média |L| (no liso).
        const float sigma = 0.2089 * s0.x / max(s0.y, 1e-4);
        const float flatShare = s0.y;                                // fração de pontos lisos (um por bloco)
        const float block = s0.w > 1e-4 ? s0.z / s0.w : 0.0;
        const float edges = smoothstep(0.004, 0.04, s1.y);          // fração de pontos de borda forte
        const float sharp = s1.y > 1e-4 ? s1.x / s1.y : 1.0;
        const float blurry = (1.0 - sharp) * edges;
        const float rn = pow(clamp((sigma - 0.004) / 0.05, 0.0, 1.0), 0.7) * smoothstep(0.02, 0.15, flatShare);
        const float rc = smoothstep(0.003, 0.02, block) * smoothstep(0.02, 0.15, flatShare);
        const float sh = 0.12 + 0.43 * smoothstep(0.2, 0.7, blurry);
        const float aa = 0.5 * smoothstep(0.4, 0.85, blurry);
        const float rd = 0.12 + 0.3 * rn + 0.15 * rc;
        const float dh = 0.3 * smoothstep(0.85, 1.0, sharp) * edges;
        a = vec4(rc, rd, sh, rn);
        b = vec4(dh, aa, 0.0, 0.0);
    }
    a = clamp(a + p.offA, 0.0, 1.0);
    b = clamp(b + p.offB, 0.0, 1.0);
    const float sigma = a.w > 0.0 ? 0.004 + 0.05 * pow(a.w, 1.0 / 0.7) : 0.0;
    o_color = v_uv.x < 0.5 ? a : vec4(b.x, b.y, sigma, 0.0);
}
