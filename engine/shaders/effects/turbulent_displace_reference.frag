#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_lattice;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region; vec4 source; vec4 shape; vec4 offset; vec4 pin; vec4 quality;
    // xy contains the fractional bias, zw the complete 12-bit corner anchor.
    vec4 coordinates[30];
    vec4 octaveScale[10];
} p;

float corner(ivec2 point) {
    ivec2 cell = ivec2((point.y ^ (point.x >> 6)) & 63, (point.x ^ (point.y >> 6)) & 63);
    return texelFetch(u_lattice, cell, 0).r;
}
float spline(vec4 values, float t) {
    float square = t * t, cube = square * t, inverse = 1.0 - t;
    return dot(values, vec4(inverse * inverse * inverse, 3.0 * cube - 6.0 * square + 4.0,
        -3.0 * cube + 3.0 * square + 3.0 * t + 1.0, cube)) * 0.2;
}
float noise2(vec2 point, ivec2 anchor, bool smoothField) {
    ivec2 cell = anchor + ivec2(floor(point));
    vec2 fraction = fract(point);
    if (!smoothField) {
        vec2 weight = fraction * fraction * (3.0 - 2.0 * fraction);
        return mix(mix(corner(cell), corner(cell + ivec2(1, 0)), weight.x),
            mix(corner(cell + ivec2(0, 1)), corner(cell + ivec2(1, 1)), weight.x), weight.y);
    }
    vec4 rows;
    for (int y = 0; y < 4; ++y) {
        rows[y] = spline(vec4(corner(cell + ivec2(-1, y - 1)), corner(cell + ivec2(0, y - 1)),
            corner(cell + ivec2(1, y - 1)), corner(cell + ivec2(2, y - 1))), fraction.x);
    }
    return spline(rows, fraction.y);
}
float fractal2(vec2 point, int branch, bool smoothField) {
    if (!smoothField) point = vec2(point.x - point.y, point.x + point.y);
    float value = 0.0, gain = 0.25;
    for (int octave = 0; octave < 10; ++octave) {
        float weight = clamp(p.shape.z - float(octave), 0.0, 1.0);
        if (weight <= 0.0) break;
        vec4 coordinate = p.coordinates[branch * 10 + octave];
        vec2 local = point * p.octaveScale[octave].x + coordinate.xy;
        value += noise2(local, ivec2(coordinate.zw), smoothField) * (gain * weight);
        gain *= 0.7;
    }
    return value;
}
float noise1(float point, int anchor) {
    int cell = anchor + int(floor(point));
    float fraction = fract(point);
    fraction = fraction * fraction * (3.0 - 2.0 * fraction);
    float a = texelFetch(u_lattice, ivec2(((cell ^ 0x440) >> 6) & 63, cell & 63), 0).r;
    int next = cell + 1;
    float b = texelFetch(u_lattice, ivec2(((next ^ 0x440) >> 6) & 63, next & 63), 0).r;
    return mix(a, b, fraction);
}
float fractal1(float point, int axis) {
    float value = 0.0, gain = 0.25;
    for (int octave = 0; octave < 10; ++octave) {
        float weight = clamp(p.shape.z - float(octave), 0.0, 1.0);
        if (weight <= 0.0) break;
        vec4 coordinate = p.coordinates[octave];
        float local = point * p.octaveScale[octave].x + coordinate[axis];
        value += noise1(local, int(coordinate[axis + 2])) * gain * weight;
        gain *= 0.7;
    }
    return value;
}
vec2 field(vec2 point) {
    vec2 q = point - p.offset.xy;
    int type = int(p.shape.w + 0.5);
    if (type >= 6) return vec2(type == 6 ? 0.0 : fractal1(q.y, 1), type == 7 ? 0.0 : fractal1(q.x, 0));
    bool smoothField = type >= 3;
    float value = fractal2(q, 0, smoothField);
    if (type == 0 || type == 3) return vec2(value, fractal2(q, 1, smoothField));
    vec2 gradient = vec2(value - fractal2(q, 1, smoothField), value - fractal2(q, 2, smoothField));
    return type == 2 || type == 5 ? vec2(-gradient.y, gradient.x) : gradient;
}
vec2 pinned(vec2 point, vec2 displacement) {
    int mode = int(p.pin.x + 0.5);
    if (mode == 0 || p.pin.y <= 0.0 || p.pin.z <= 0.0) return displacement;
    bool locked = mode >= 8;
    int side = locked ? mode - 7 : mode;
    vec2 weight = vec2(1.0);
    if (side == 1 || side == 3 || side == 4) weight.x *= smoothstep(0.0, p.pin.y, point.x);
    if (side == 1 || side == 3 || side == 5) weight.x *= smoothstep(0.0, p.pin.y, p.offset.z - 1.0 - point.x);
    if (side == 1 || side == 2 || side == 6) weight.y *= smoothstep(0.0, p.pin.z, point.y);
    if (side == 1 || side == 2 || side == 7) weight.y *= smoothstep(0.0, p.pin.z, p.offset.w - 1.0 - point.y);
    return displacement * (locked ? vec2(weight.x * weight.y) : weight);
}
vec4 warped(vec2 point) {
    vec2 source = point + pinned(point, field(point)) * p.shape.x;
    return texture(u_tex0, (source + vec2(0.5) - p.source.xy) / p.source.zw);
}
void main() {
    vec2 point = p.region.xy + v_uv * p.region.zw - vec2(0.5);
    if (p.quality.x < 1.5) { o_color = warped(point); return; }
    vec2 step = p.quality.yz * 0.25;
    o_color = (warped(point + step) + warped(point - step)
        + warped(point + vec2(step.x, -step.y)) + warped(point + vec2(-step.x, step.y))) * 0.25;
}
