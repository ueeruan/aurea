#version 450
#include "../../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out float o_depth;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_depth;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params { vec4 texel; } p;
void main() {
    o_depth = textureLod(t_depth, v_uv + p.texel.zw, 0.0).r;
}
