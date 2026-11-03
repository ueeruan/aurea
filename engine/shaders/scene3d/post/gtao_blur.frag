#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / gtao_blur.frag
//
//  Desfoque bilateral separável da AO (0.4.6): 9 amostras numa direção, cada
//  uma pesada pela diferença de profundidade de vista (G) — a AO de um objeto
//  não vaza para o fundo nem a do fundo para o objeto. Limpa o ruído das
//  direções giradas do gtao.frag.
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_ao;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_ao;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 dir;   // xy = passo de 1 texel na direção do desfoque, z = nitidez da borda
} p;

void main() {
    vec2 c = textureLod(t_ao, v_uv, 0.0).rg;
    if (c.g <= 0.0) { o_ao = vec4(c.r, c.g, 0.0, 1.0); return; }
    float sum = c.r, wsum = 1.0;
    for (int i = -4; i <= 4; ++i) {
        if (i == 0) continue;
        vec2 s = textureLod(t_ao, v_uv + p.dir.xy * float(i), 0.0).rg;
        if (s.g <= 0.0) continue;
        float g = exp(-float(i * i) / 8.0);
        float dz = abs(s.g - c.g) / max(c.g, 1e-4);
        float w = g * exp(-dz * p.dir.z);
        sum += s.r * w;
        wsum += w;
    }
    o_ao = vec4(sum / wsum, c.g, 0.0, 1.0);
}
