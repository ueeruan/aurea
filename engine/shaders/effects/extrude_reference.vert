#version 450
#include "../common/bindings.glsl"
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_vertices;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Parameters {
    vec4 region, inputRegion, frontColor, sideControls;
    vec4 angles[2];
    vec4 colors[5];
    vec4 controls;
} p;
layout(location = 0) out vec4 v_source;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec4 v_stroke;

vec4 data_at(int offset) {
    int width = int(p.controls.w);
    return texelFetch(u_vertices, ivec2(offset % width, offset / width), 0);
}
void main() {
    int index = gl_VertexIndex * 4;
    vec4 position = data_at(index);
    v_source = data_at(index + 1);
    v_color = data_at(index + 2);
    v_stroke = data_at(index + 3);
    vec2 clip = (position.xy - p.region.xy) / p.region.zw * 2.0 - 1.0;
    float perspective = max(position.w, 1e-6);
    gl_Position = vec4(clip * perspective, position.z * perspective, perspective);
}
