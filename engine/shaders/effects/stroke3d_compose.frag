#version 450
// =============================================================================
//  Aurea / shaders / effects / stroke3d_compose.frag
//
//  Fecha o 3D Stroke: o traço sobre a camada, só o traço, ou a camada sobre
//  o traço (traço atrás). Pré-multiplicado.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // o traço
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 dst;
    vec4 path;
    vec4 window;
    vec4 taper;
    vec4 rot;
    vec4 center;
    vec4 copies;
    vec4 color0;
    vec4 color1;
    vec4 compose;  // uv da saída → uv da camada (xy escala, zw deslocamento)
} p;

void main() {
    const vec4 s = clamp(texture(u_tex0, v_uv), 0.0, 1.0);
    const vec2 uv = v_uv * p.compose.xy + p.compose.zw;
    const bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
    const vec4 layer = inside ? texture(u_tex1, uv) : vec4(0.0);
    const float mode = p.taper.w;   // 0 sobre, 1 só o traço, 2 atrás
    if (mode < 0.5) o_color = s + layer * (1.0 - s.a);          // traço sobre a camada
    else if (mode < 1.5) o_color = s;                            // só o traço
    else o_color = layer + s * (1.0 - layer.a);                  // traço atrás da camada
}
