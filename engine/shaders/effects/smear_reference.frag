#version 450
// =============================================================================
//  Aurea / shaders / effects / smear_reference.frag
//
//  Smear reference (aurea.distort.smear_mask_reference): reads the layer at
//  p + D(p), D = the backward displacement grid solved on the CPU
//  (SmearReference.cpp). Interpolation Method: 0 Discrete (nearest node),
//  1 Linear (bilinear), 2 Smooth (Catmull-Rom). Outside the grid D = 0.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_grid;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 inRegion;   // layer px of the input texture
    vec4 outRegion;  // layer px of the output
    vec4 grid;       // xy = layer px of node (0,0), z = cell px, w = interpolation
    vec4 gridSize;   // xy = nodes
} u;

vec2 node(ivec2 n) {
    const ivec2 size = ivec2(u.gridSize.xy);
    if (any(lessThan(n, ivec2(0))) || any(greaterThanEqual(n, size))) return vec2(0.0);
    return texelFetch(u_grid, n, 0).xy;
}
vec2 catmull(vec2 a, vec2 b, vec2 c, vec2 d, float t) {
    return b + 0.5 * t * (c - a + t * (2.0 * a - 5.0 * b + 4.0 * c - d + t * (3.0 * (b - c) + d - a)));
}
vec2 displacement(vec2 px) {
    const vec2 g = (px - u.grid.xy) / max(u.grid.z, 1e-4);
    if (any(lessThan(g, vec2(-1.0))) || any(greaterThan(g, u.gridSize.xy))) return vec2(0.0);
    const int mode = int(u.grid.w + 0.5);
    if (mode == 0) return node(ivec2(floor(g + 0.5)));
    const ivec2 i = ivec2(floor(g));
    const vec2 f = g - vec2(i);
    if (mode == 1) {
        return mix(mix(node(i), node(i + ivec2(1, 0)), f.x), mix(node(i + ivec2(0, 1)), node(i + ivec2(1, 1)), f.x), f.y);
    }
    vec2 rows[4];
    for (int r = 0; r < 4; ++r) {
        const ivec2 b = i + ivec2(-1, r - 1);
        rows[r] = catmull(node(b), node(b + ivec2(1, 0)), node(b + ivec2(2, 0)), node(b + ivec2(3, 0)), f.x);
    }
    return catmull(rows[0], rows[1], rows[2], rows[3], f.y);
}

void main() {
    const vec2 px = u.outRegion.xy + v_uv * u.outRegion.zw;
    const vec2 from = px + displacement(px);
    o_color = texture(u_src, (from - u.inRegion.xy) / u.inRegion.zw);
}
