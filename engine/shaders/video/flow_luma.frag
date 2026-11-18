#version 450
// =============================================================================
//  Aurea / shaders / video / flow_luma.frag
//
//  Base da pirâmide do optical flow: luminância dos dois quadros (R = atual,
//  G = seguinte) numa resolução reduzida. Quatro amostras bilineares por pixel
//  de saída (caixa), para a redução não serrilhar texturas finas — serrilhado
//  vira movimento falso no Lucas-Kanade.
//
//  outTexel.w = 1 (1.0.3): a COR dos dois quadros no mesmo tamanho — RG = (Cb,
//  Cr) do atual, BA = do seguinte —, a guia que separa contornos que a
//  luminância não vê (flow_common.glsl).
// =============================================================================
#include "../common/bindings.glsl"
#include "flow_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_a;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_b;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 outTexel;   // xy = tamanho de um pixel de SAÍDA em uv, z = 1: curva perceptual, w = 1: crominância
} p;

// O vídeo chega em luz linear, onde as sombras quase não têm gradiente e o
// rastreio fica cego justamente nelas; a raiz devolve o contraste percebido.
float luma(vec3 c) {
    const float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return p.outTexel.z > 0.5 ? sqrt(max(l, 0.0)) : l;
}

void main() {
    vec2 d = p.outTexel.xy * 0.25;
    if (p.outTexel.w > 0.5) {
        vec3 ca = vec3(0.0), cb = vec3(0.0);
        for (int i = 0; i < 4; ++i) {
            vec2 o = vec2((i & 1) == 0 ? -d.x : d.x, (i & 2) == 0 ? -d.y : d.y);
            ca += texture(u_a, v_uv + o).rgb;
            cb += texture(u_b, v_uv + o).rgb;
        }
        o_color = vec4(flow_chroma(ca * 0.25), flow_chroma(cb * 0.25));
        return;
    }
    float a = 0.0, b = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2((i & 1) == 0 ? -d.x : d.x, (i & 2) == 0 ? -d.y : d.y);
        a += luma(texture(u_a, v_uv + o).rgb);
        b += luma(texture(u_b, v_uv + o).rgb);
    }
    o_color = vec4(a * 0.25, b * 0.25, 0.0, 1.0);
}
