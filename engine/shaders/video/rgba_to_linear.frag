#version 450
// =============================================================================
//  Aurea / shaders / video / rgba_to_linear.frag
//
//  Imagem importada (RGBA8 em sRGB, alfa reto) → espaço de trabalho (linear,
//  pré-multiplicado). A imagem é enviada UMA vez; este passe só roda quando a
//  resolução de trabalho muda.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 flags;   // x=a textura já é linear (0/1)  y=alfa já pré-multiplicado (0/1)
} p;

void main() {
    vec4 c = texture(u_tex0, v_uv);
    if (p.flags.y > 0.5) c = unpremultiply(c);
    vec3 lin = p.flags.x > 0.5 ? c.rgb : srgb_to_linear(c.rgb);
    o_color = premultiply(vec4(lin, c.a));
}
