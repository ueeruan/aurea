#version 450
// =============================================================================
//  Aurea / shaders / video / flow_pack.frag
//
//  Junta os dois fluxos do nível base numa textura só (a do cache): RG = do
//  quadro atual para o seguinte, BA = do seguinte para o atual. O desfoque
//  vetorial lê só o RG, como antes.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_forward;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_backward;

void main() {
    o_color = vec4(textureLod(u_forward, v_uv, 0.0).rg, textureLod(u_backward, v_uv, 0.0).rg);
}
