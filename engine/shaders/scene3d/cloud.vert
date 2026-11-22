#version 450
#include "common/scene.glsl"
#include "common/cloud_push.glsl"

layout(location = 0) in vec3 a_position;
layout(location = 0) out vec3 v_local;

void main() {
    v_local = a_position;
    gl_Position = u.viewProj * (pc.model * vec4(a_position, 1.0));
}
