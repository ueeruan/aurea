#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_mask;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 block; vec4 grid; vec4 region; vec4 unused;
    uvec4 limits;
} p;

float keepCell(ivec2 cell) {
    cell = clamp(cell, ivec2(0), ivec2(p.grid.xy) - ivec2(1));
    uint index = uint(cell.y) * uint(p.grid.x) + uint(cell.x);
    uint width = uint(p.grid.z);
    uvec4 bytes = uvec4(round(texelFetch(u_mask, ivec2(index % width, index / width), 0) * 255.0));
    uint rank = bytes.r | (bytes.g << 8) | (bytes.b << 16) | (bytes.a << 24);
    if (index == 0) return p.limits.x > 0 ? 0.0 : 1.0;
    return rank <= max(p.limits.x, 1u) ? 0.0 : 1.0;
}

void main() {
    vec4 source = texture(u_tex0, v_uv);
    if (p.limits.y == 1) {
        o_color = source * texture(u_mask, v_uv).r;
        return;
    }
    // Reconstruct the target pixel center before entering layer coordinates.
    // Interpolated UVs can fall one ulp below an integer row and select the
    // preceding hard-mask cell at an otherwise exact block boundary.
    vec2 size = vec2(textureSize(u_tex0, 0));
    vec2 pixelCenter = floor(v_uv * size) + vec2(0.5);
    vec2 point = p.region.xy + pixelCenter * (p.region.zw / size) - vec2(0.5);
    // The reference sampler receives a half-cell coordinate offset. Its
    // nearest mode rounds that coordinate, while bilinear mode interpolates it.
    vec2 gridPoint = point / p.block.xy + vec2(0.5);
    float keep;
    if (p.limits.z == 0) {
        keep = keepCell(ivec2(floor(gridPoint + vec2(0.5))));
    } else {
        ivec2 cell = ivec2(floor(gridPoint));
        vec2 fraction = fract(gridPoint);
        keep = mix(mix(keepCell(cell), keepCell(cell + ivec2(1, 0)), fraction.x),
            mix(keepCell(cell + ivec2(0, 1)), keepCell(cell + ivec2(1, 1)), fraction.x), fraction.y);
        // The reference generates and samples an eight-bit alpha mask even
        // when the source and destination use floating-point color.
        keep = floor(keep * 255.0 + 0.5) / 255.0;
    }
    o_color = p.limits.y == 0 ? vec4(keep) : source * keep;
}
