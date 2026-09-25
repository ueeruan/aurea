#version 450
#include "../common/bindings.glsl"
#include "../common/color.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
void main() {
    vec4 source = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    // Premultiplied energy naturally follows coverage on transparent edges.
    float light = max(luminance709(source.rgb), 0.0);
    float knee = max(p.p0.y, 0.00001);
    float soft = clamp(light - p.p0.x + knee, 0.0, 2.0 * knee);
    float energy = min(light, max(light - p.p0.x, soft * soft / (4.0 * knee)));
    o_color = vec4(vec3(energy), 1.0);
}
