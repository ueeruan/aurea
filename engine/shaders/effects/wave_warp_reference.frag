#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_wave;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;

float pinWeight(vec2 point) {
    int mode = int(p.p1.x + 0.5);
    vec2 extent = max(p.p3.xy * 0.25, vec2(1.0));
    vec2 start = clamp(point / extent, 0.0, 1.0);
    vec2 end = clamp((p.p3.xy - vec2(1.0) - point) / extent, 0.0, 1.0);
    if (mode == 1) return min(start.x, end.x) * min(start.y, end.y);
    if (mode == 2) {
        vec2 delta = point - floor(p.p3.xy * 0.5);
        return clamp(dot(delta, delta) / (extent.x * extent.x), 0.0, 1.0);
    }
    if (mode == 3) return start.x;
    if (mode == 4) return start.y;
    if (mode == 5) return end.x;
    if (mode == 6) return end.y;
    if (mode == 7) return min(start.y, end.y);
    if (mode == 8) return min(start.x, end.x);
    return 1.0;
}

int splitInt(float high, float low) { return int(high) * 65536 + int(low); }

// One sub-sample of the supersampled layer. `high` is the integer
// high-resolution layer pixel (layer pixel * N + sub-sample index).
vec4 sampleWarp(ivec2 high, int samples) {
    // AE 2027 oracle: the waveform index comes from a 16.16 accumulator that
    // advances round(65536 * 1024 / (width * N)) per high-resolution pixel and
    // starts one step in. Unsigned arithmetic keeps the modular wrap defined.
    int stepX = splitInt(p.color.x, p.color.y), stepY = splitInt(p.color.z, p.color.w);
    uint fixedPhase = uint(high.x + 1) * uint(stepX) + uint(high.y) * uint(stepY) -
        uint((samples * stepY) / 2) + uint(splitInt(p.p1.z, p.p1.w));
    float wave = texelFetch(u_wave, ivec2(int((fixedPhase >> 16) & 2047u), 0), 0).r;
    float angle = p.p0.z;
    vec2 amplitude = vec2(sin(angle), -cos(angle)) * p.p0.x;
    vec2 layerPoint = vec2(high) / float(samples);
    vec2 displacement = amplitude * (wave * pinWeight(layerPoint)) * float(samples);
    ivec2 source = high + ivec2(floor(displacement));
    // Bounds are tested on the high-resolution layer before checking out a
    // source pixel through the 16.16 inverse scale (floor(65536 / N)).
    ivec2 highSize = ivec2(p.p3.xy + 0.5) * samples;
    if (any(lessThan(source, ivec2(0))) || any(greaterThanEqual(source, highSize)))
        return vec4(0.0);
    int inverseStep = 65536 / samples;
    ivec2 layerPixel = (source * inverseStep) / 65536;
    vec2 scale = p.p2.zw / vec2(textureSize(u_tex0, 0));
    ivec2 texel = ivec2(floor((vec2(layerPixel) + vec2(0.5) - p.p2.xy) / scale));
    if (any(lessThan(texel, ivec2(0))) || any(greaterThanEqual(texel, textureSize(u_tex0, 0))))
        return vec4(0.0);
    return texelFetch(u_tex0, texel, 0);
}

void main() {
    vec2 size = vec2(textureSize(u_tex0, 0));
    vec2 scale = p.p2.zw / size;
    vec2 pixel = floor(v_uv * size);
    int samples = clamp(int(p.p1.y + 0.5), 2, 4);
    vec4 sum = vec4(0.0);
    for (int y = 0; y < samples; ++y) {
        for (int x = 0; x < samples; ++x) {
            // Layer-space sub-sample origin; exact integers at full resolution.
            vec2 layer = p.p2.xy + (pixel + vec2(x, y) / float(samples)) * scale;
            sum += sampleWarp(ivec2(floor(layer * float(samples) + vec2(0.5))), samples);
        }
    }
    o_color = sum / float(samples * samples);
}
