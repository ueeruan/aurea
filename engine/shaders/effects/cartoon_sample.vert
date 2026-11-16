#version 450
layout(push_constant) uniform Push {
    mat4 matrix;
    vec4 region;
    vec4 tint;
    vec4 params;
}
pc;
layout(location = 0) out vec2 v_uv;
const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
void main() {
    v_uv = corners[gl_VertexIndex];
    gl_Position = pc.matrix * vec4(pc.region.xy + v_uv * pc.region.zw, 0, 1);
}
