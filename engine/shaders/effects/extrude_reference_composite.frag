#version 450
#include "../common/bindings.glsl"
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_geometry;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Parameters { vec4 sourceUv; vec4 controls; } p;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() {
    vec4 source = texture(u_source, v_uv * p.sourceUv.xy + p.sourceUv.zw) * p.controls.x;
    int samples = clamp(int(ceil(p.controls.y)), 1, 10);
    vec4 geometry = vec4(0);
    for (int row = 0; row < samples; ++row) for (int column = 0; column < samples; ++column) {
        vec2 offset = (vec2(column, row) + 0.5) / float(samples) - 0.5;
        geometry += texture(u_geometry, v_uv + offset / p.controls.zw);
    }
    geometry /= float(samples * samples);
    o_color = geometry + source * (1.0 - geometry.a);
}
