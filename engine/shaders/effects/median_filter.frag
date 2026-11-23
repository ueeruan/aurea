#version 450
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0; // xy: sample spacing in texels, z: wet mix
    vec4 p1, p2, p3, color;
} p;

vec4 median3(vec4 a, vec4 b, vec4 c) {
    return max(min(a, b), min(max(a, b), c));
}

struct SampleRow {
    vec4 low;
    vec4 middle;
    vec4 high;
};

float reflect_sample(float uv, float texel) {
    float first = 0.5 * texel;
    float last = 1.0 - first;
    if (uv >= first && uv <= last) return uv;
    float span = max(last - first, texel);
    return min(first + span - abs(mod(uv - first, 2.0 * span) - span), last);
}

SampleRow sample_row(vec3 columns, float row, vec4 center) {
    vec4 left = unpremultiply(texture(u_tex0, vec2(columns.x, row)));
    vec4 right = unpremultiply(texture(u_tex0, vec2(columns.z, row)));
    vec4 low = min(left, right);
    vec4 high = max(left, right);
    return SampleRow(min(low, center), clamp(center, low, high), max(high, center));
}

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 stepUv = p.p0.xy * p.texel.xy;
    vec3 columns = vec3(reflect_sample(uv.x - stepUv.x, p.texel.x), uv.x,
                        reflect_sample(uv.x + stepUv.x, p.texel.x));
    float topUv = reflect_sample(uv.y - stepUv.y, p.texel.y);
    float bottomUv = reflect_sample(uv.y + stepUv.y, p.texel.y);
    vec4 center = texture(u_tex0, uv);
    SampleRow top = sample_row(columns, topUv, unpremultiply(texture(u_tex0, vec2(columns.y, topUv))));
    SampleRow middle = sample_row(columns, uv.y, unpremultiply(center));
    SampleRow bottom = sample_row(columns, bottomUv, unpremultiply(texture(u_tex0, vec2(columns.y, bottomUv))));

    // Rank straight color so the opacity of an edge does not darken its rank.
    // After sorting the rows, only these three candidates can be the fifth.
    vec4 maxOfLows = max(top.low, max(middle.low, bottom.low));
    vec4 midOfMiddles = median3(top.middle, middle.middle, bottom.middle);
    vec4 minOfHighs = min(top.high, min(middle.high, bottom.high));
    o_color = mix(center, premultiply(median3(maxOfLows, midOfMiddles, minOfHighs)), p.p0.z);
}
