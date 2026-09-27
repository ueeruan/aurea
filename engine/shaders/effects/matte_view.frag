#version 450
// =============================================================================
//  Aurea / shaders / effects / matte_view.frag
//
//  Mostrar a máscara: o alfa da camada como cinza opaco (branco = opaco). É
//  como se confere um recorte — a cor some, a borda aparece.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const float a = clamp(texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw).a, 0.0, 1.0);
    o_color = vec4(a, a, a, 1.0);
}
