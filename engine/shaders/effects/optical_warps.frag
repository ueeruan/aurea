#version 450
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0, binding=AUREA_TEX0) uniform sampler2D source;
layout(set=0, binding=AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap, texel;
    vec4 p0; // center uv, region height/width, warp kind
    vec4 p1, p2, p3, color;
} p;

vec4 fetch_encoded(ivec2 coord, ivec2 size) {
    if (p.p3.w > 1.5) {
        coord = (coord % size + size) % size;
    } else if (p.p3.w > 0.5) {
        ivec2 period = size * 2;
        ivec2 wrapped = (coord % period + period) % period;
        coord = min(wrapped, period - 1 - wrapped);
    } else if (any(lessThan(coord, ivec2(0))) || any(greaterThanEqual(coord, size))) {
        return vec4(0.0);
    }
    vec4 sampleColor = unpremultiply(texelFetch(source, coord, 0));
    return vec4(linear_to_srgb(sampleColor.rgb) * sampleColor.a, sampleColor.a);
}

vec4 sample_encoded(vec2 uv) {
    ivec2 size = textureSize(source, 0);
    if (p.p3.w > 1.5) {
        uv = fract(uv);
    } else if (p.p3.w > 0.5) {
        uv = 1.0 - abs(mod(uv, 2.0) - 1.0);
    } else {
        vec2 halfTexel = 0.5 / vec2(size);
        if (any(lessThan(uv, -halfTexel)) || any(greaterThan(uv, 1.0 + halfTexel))) return vec4(0.0);
    }
    vec2 pos = uv * vec2(size) - 0.5;
    ivec2 cell = ivec2(floor(pos));
    vec2 part = fract(pos);
    vec4 upper = mix(fetch_encoded(cell, size), fetch_encoded(cell + ivec2(1, 0), size), part.x);
    vec4 lower = mix(fetch_encoded(cell + ivec2(0, 1), size), fetch_encoded(cell + ivec2(1, 1), size), part.x);
    return mix(upper, lower, part.y);
}

void main() {
    vec2 center = p.p0.xy;
    vec2 aspect = vec2(1.0, max(p.p0.z, 1e-5));
    vec2 delta = (v_uv - center) * aspect;
    int kind = int(p.p0.w + 0.5);
    vec4 encoded;
    if (kind == 0) {
        vec2 normalized = delta * 2.0 / max(p.p1.w, 1e-5);
        float factor = (1.0 + p.p1.x * dot(normalized, normalized)) / (1.0 + p.p1.x);
        vec2 warped = aurea_rot2(-p.p1.z) * delta * (factor * p.p1.y);
        encoded = sample_encoded(center + warped / aspect - p.p2.xy);
    } else if (kind == 1) {
        vec2 lens = aurea_rot2(-p.p2.z) * delta / p.p2.xy;
        float outerRadius = p.p1.x;
        float width = outerRadius * p.p1.y;
        float r = length(lens);
        float t = width > 1e-5 ? clamp((outerRadius - r) / width, 0.0, 1.0) : step(r, outerRadius);
        float curve = t * t * (3.0 - 2.0 * t);
        t = clamp(t + (curve - t) * p.p1.w, 0.0, 1.0);
        float zoom = mix(1.0, p.p1.z, t);
        encoded = sample_encoded(center + delta / (zoom * aspect));
    } else if (kind == 2) {
        int steps = int(p.p3.x + 0.5);
        encoded = vec4(0.0);
        for (int i = 0; i < steps; ++i) {
            float t = float(i) / float(steps - 1);
            float scale = max(mix(1.0, mix(p.p1.x, p.p1.y, t), p.p3.y), 0.01);
            vec2 rotated = aurea_rot2(-mix(p.p1.z, p.p1.w, t) * p.p3.y) * delta;
            vec2 shift = mix(p.p2.xy, p.p2.zw, t) * p.p3.y;
            float weight = 2.0 * mix(1.0 - p.p3.z, p.p3.z, t);
            encoded += weight * sample_encoded(center + rotated * scale / aspect - shift);
        }
        encoded /= float(steps);
    } else {
        vec2 ray = aurea_rot2(-p.p1.z) * delta;
        float latitude = p.p1.x;
        float perspective = 1.0 + 2.0 * ray.y * tan(latitude) / max(p.p2.y * p.p2.x, 1e-5);
        if (abs(perspective) < 1e-4 || (perspective < 0.0 && p.p3.x < 0.5)) {
            o_color = vec4(0.0);
            return;
        }
        vec2 projected = vec2(ray.x, ray.y / max(cos(latitude), 1e-4)) * (p.p1.w / perspective);
        projected = aurea_rot2(-p.p1.y) * projected;
        encoded = sample_encoded(center + projected / aspect - p.p2.zw);
    }
    vec4 straight = unpremultiply(encoded);
    o_color = vec4(srgb_to_linear(straight.rgb) * straight.a, straight.a);
}
