#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
void main() {
    vec4 source = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    float local = texture(u_tex1, v_uv * p.p1.xy + p.p1.zw).r;
    float diffuse = texture(u_tex2, v_uv * p.p2.xy + p.p2.zw).r;
    float energy = max(diffuse - local * p.p0.y, 0.0) * p.p0.x;
    vec3 halo = energy * max(p.color.rgb, vec3(0.0)) * clamp(p.color.a, 0.0, 1.0);
    float coverage = clamp(max(halo.r, max(halo.g, halo.b)), 0.0, 1.0);
    if (p.p0.z > 0.5) source = vec4(0.0);
    // Add light in the shared linear/premultiplied working space. A halo may
    // extend over transparency without turning the complete margin opaque.
    o_color = vec4(source.rgb + halo, max(source.a, coverage));
}
