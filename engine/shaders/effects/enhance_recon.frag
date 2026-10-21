#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_recon.frag
//
//  Aprimorar, na resolução cheia, depois do desbloqueio:
//
//  1. "Reverter compressão" — o ANEL do codec (mosquito/ringing): as ondinhas
//     que o DCT deixa perto de bordas fortes. Filtro sigma 3×3 (média dos
//     vizinhos de luz parecida) só onde há borda forte no 3×3: a borda passa
//     do limiar e fica; a ondinha em volta dela é alisada.
//  2. "Reduzir ruído" — a pirâmide de dois níveis: o grosso vem da meia
//     resolução já filtrada (enhance_bilateral) e o DETALHE da cheia passa
//     pelo encolhimento de garrote (v·max(0, 1 − τ²/v²)): o que é do tamanho
//     do ruído some, a borda e a textura forte ficam quase intactas.
// =============================================================================
#include "../common/bindings.glsl"
#include "enhance_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;    // cheia (linear ou do efeito)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_half;   // meia resolução filtrada
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_w;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho da cheia
    vec4 flags;   // x = entrada linear (0/1), y = anel (0/1), z = ruído (0/1)
} p;

void main() {
    const bool lin = p.flags.x > 0.5;
    const vec4 c = enh_load(u_src, v_uv, lin);
    const vec4 w0 = enh_w0(u_w);
    const vec4 w1 = enh_w1(u_w);
    const float rc = w0.x, rn = w0.w;
    vec3 x = c.xyz;
    if (p.flags.y > 0.5 && rc > 0.0) {
        vec3 n[8];
        float mn = c.x, mx = c.x;
        for (int k = 0; k < 8; ++k) {
            const int i = k < 4 ? k : k + 1;   // o 3×3 sem o centro
            n[k] = enh_load(u_src, v_uv + vec2(float(i % 3 - 1), float(i / 3 - 1)) * p.texel.xy, lin).xyz;
            mn = min(mn, n[k].x);
            mx = max(mx, n[k].x);
        }
        const float near = smoothstep(0.08, 0.25, mx - mn);
        if (near > 0.0) {
            const float limit = rc * (0.01 + 0.04 * near) + 2.0 * w1.z;
            vec3 acc = x;
            float wsum = 1.0;
            for (int k = 0; k < 8; ++k) {
                const float w = 1.0 - smoothstep(0.5 * limit, limit, abs(n[k].x - c.x));
                acc += n[k] * w;
                wsum += w;
            }
            x = mix(x, acc / wsum, near * min(1.0, 1.5 * rc));
        }
    }
    if (p.flags.z > 0.5 && rn > 0.0) {
        const vec3 base = textureLod(u_half, v_uv, 0.0).xyz;
        const float sigma = w1.z / sqrt(max(c.w, 1.0));
        const vec3 detail = x - base;
        const vec3 tau = vec3(2.0, 2.6, 2.6) * sigma;
        const vec3 kept = detail * max(vec3(0.0), vec3(1.0) - tau * tau / (detail * detail + vec3(1e-8)));
        x = base + mix(detail, kept, smoothstep(0.0, 0.5, rn));
    }
    o_color = vec4(x, c.w);
}
