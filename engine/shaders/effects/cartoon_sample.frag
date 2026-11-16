#version 450
#include "../common/bindings.glsl"
layout(push_constant) uniform Push {
    mat4 matrix;
    vec4 region;
    vec4 tint;
    vec4 params;
}
pc;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D source;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 color;
void main() {
    vec4 c = texture(source, v_uv);
    if (pc.tint.a >= 0)
        c = vec4(pc.tint.rgb * c.a * pc.tint.a, c.a * pc.tint.a);
    color = c * pc.params.x;
}
