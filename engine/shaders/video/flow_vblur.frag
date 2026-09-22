#version 450
// =============================================================================
//  Aurea / shaders / video / flow_vblur.frag
//
//  Desfoque pelo movimento do próprio vídeo (vetores do optical flow): cada
//  pixel é a média de N amostras ao longo do seu vetor, no trecho que o
//  obturador cobre, centrado no quadro. Parado = nítido; o que corre borra na
//  direção em que corre.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_img;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_flow;   // RG = px do nível base, por quadro da fonte

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 blur;   // x = fração do quadro no obturador, yz = uv por pixel do nível base, w = amostras
} p;

void main() {
    vec2 f = texture(u_flow, v_uv).xy * p.blur.yz * p.blur.x;
    int n = int(p.blur.w);
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 32; ++i) {
        if (i >= n) break;
        float s = (float(i) + 0.5) / float(n) - 0.5;
        acc += texture(u_img, v_uv + s * f);
    }
    o_color = acc / float(n);
}
