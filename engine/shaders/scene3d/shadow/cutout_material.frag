#version 450
#include "cutout.glsl"

layout(push_constant) uniform Push {
    mat4 lightFromLocal;
    vec4 extra;
} pc;
layout(location = 0) out vec4 o_material;

void main() {
    apply_cutout();
    o_material = vec4(clamp(pc.extra.zw, 0.0, 1.0), 0.0, 1.0);
}
