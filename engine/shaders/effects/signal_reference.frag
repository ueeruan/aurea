#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
#include "signal_reference.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_original;

vec4 signalSample(ivec2 pixel) {
    return texelFetch(u_source, clamp(pixel, ivec2(0), textureSize(u_source, 0) - 1), 0);
}

float wave(int x, int row) {
    if (x < 0 || x >= textureSize(u_source, 0).x) return 0.0;
    return texelFetch(u_source, ivec2(x, row), 0).r;
}

float demodulatedLuma(int x, int row) {
    return floor((wave(x - 1, row) + wave(x, row) + wave(x + 1, row) + wave(x + 2, row)) * 0.25);
}

float residual(int x, int row) {
    float value = wave(x + 2, row) - demodulatedLuma(x, row);
    if (((x + signalCarrierOffset(row)) & 3) >= 2) value = -value;
    return trunc(value * 50.0 / float(max(int(setting(1)), 1)));
}

vec2 evenChroma(int x, int row) {
    int position = x + signalCarrierOffset(row);
    if (position + 1 >= textureSize(u_source, 0).x) return vec2(0.0);
    return -vec2(residual(position, row), residual(position + 1, row));
}

vec4 originalSample(ivec2 pixel) {
    return texelFetch(u_original, clamp(pixel, ivec2(0), textureSize(u_original, 0) - 1), 0);
}

void main() {
    int stage = int(p.raster.w + 0.5);
    if (stage == 6 || stage == 7) {
        vec2 axis = stage == 6 ? vec2(1.0 / p.raster.x, 0.0) : vec2(0.0, 1.0 / p.raster.y);
        float sigma = max(setting(34) * 0.4, 0.01);
        vec4 sum = vec4(0.0);
        float weightSum = 0.0;
        for (int tap = -16; tap <= 16; ++tap) {
            float distance = float(tap) * max(1.0, sigma * 0.25);
            float weight = exp(-0.5 * distance * distance / (sigma * sigma));
            sum += texture(u_source, v_uv + axis * distance) * weight;
            weightSum += weight;
        }
        o_color = sum / max(weightSum, 1e-6);
        return;
    }
    if (stage == 0) {
        float row = floor(v_uv.y * p.raster.y);
        float time = p.raster.z * setting(25) * 0.05;
        float thick = signalSmoothNoise(vec2(row / 80.0, time), 47u);
        float fine = signalSmoothNoise(vec2(row / 2.0, time * 1.7), 97u);
        float shift = (thick + sign(thick) * 0.1) * setting(23) * 0.01
            + (fine + sign(fine) * 0.1) * setting(24) * 0.0025;
        vec2 uv = vec2(fract(v_uv.x - shift), fract(v_uv.y + floor(p.raster.z * setting(26)) / p.raster.y));
        vec4 source = unpremultiply(texture(u_source, uv));
        vec3 rgb = floor(clamp(source.rgb, 0.0, 1.0) * 255.0 + 0.5);
        float luma = dot(rgb, vec3(0.3, 0.59, 0.11));
        vec2 chroma = vec2(0.74 * (rgb.r - luma) - 0.27 * (rgb.b - luma),
                           0.48 * (rgb.r - luma) + 0.41 * (rgb.b - luma));
        o_color = vec4(trunc(vec3(luma, chroma) * 256.0), source.a);
        return;
    }
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 center = signalSample(pixel);
    if (stage == 2) {
        vec2 iq = (pixel.x & 1) == 0 ? evenChroma(pixel.x, pixel.y)
            : floor((evenChroma(pixel.x - 1, pixel.y) + evenChroma(pixel.x + 1, pixel.y)) * 0.5);
        if (pixel.x >= ((textureSize(u_source, 0).x - 1) & ~1)) iq = vec2(0.0);
        o_color = vec4(demodulatedLuma(pixel.x, pixel.y), iq, center.a);
        return;
    }
    if (stage == 10 || stage == 11 || stage == 14) {
        ivec2 delay;
        if (stage == 11) delay = ivec2(signalChromaDelay());
        else delay = stage == 14 && setting(28) < 1.5 ? ivec2(1) : ivec2(2, 4);
        vec2 iq = originalSample(pixel).yz;
        int width = textureSize(u_source, 0).x;
        if (pixel.x + delay.x < width) iq.x = signalSample(pixel + ivec2(delay.x, 0)).g;
        if (pixel.x + delay.y < width) iq.y = signalSample(pixel + ivec2(delay.y, 0)).b;
        if (stage == 11) {
            float loss = max(setting(7), setting(11) > 0.5 ? 0.8 : 0.0);
            if (signalRandom(ivec2(0, pixel.y), 301u) < loss) iq = vec2(0.0);
        }
        o_color = vec4(center.r, iq, center.a);
        return;
    }
    if (stage == 12) {
        vec2 previous = pixel.y > 0 ? signalSample(pixel - ivec2(0, 1)).yz : vec2(0.0);
        o_color = vec4(center.r, floor((center.yz + previous + vec2(1.0)) * 0.5), center.a);
        return;
    }
    vec3 rgb = clamp(trunc(signalToRgb(center.rgb) / 256.0), 0.0, 255.0) / 255.0;
    float alpha = texture(u_original, v_uv).a;
    o_color = vec4(rgb * alpha, alpha);
}
