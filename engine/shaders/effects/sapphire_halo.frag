#version 450
#include "../common/bindings.glsl"
#include "common.glsl"
#include "light_energy.glsl"

layout(location=0) in vec2 v_uv;
layout(location=0) out vec4 o_color;
layout(set=0,binding=AUREA_TEX0) uniform sampler2D source;
layout(set=0,binding=AUREA_TEX1) uniform sampler2D redHalo;
layout(set=0,binding=AUREA_TEX2) uniform sampler2D greenHalo;
layout(set=0,binding=AUREA_TEX3) uniform sampler2D blueHalo;
layout(set=0,binding=AUREA_PARAMS,std140) uniform Params {
    vec4 uvMap;
    vec4 haloMap;
    vec4 mode;      // extract/composite, model, threshold, gain
    vec4 options;   // show halo, add mode, saturation, alpha gain
    vec4 edge;      // derivative step XY (UV), strength, layer width
    vec4 thresholdColor;
    vec4 tint;
} p;

vec4 encoded_source(vec2 uv) {
    vec4 samplePixel = texture(source, uv);
    return vec4(linear_to_srgb(unpremultiply(samplePixel).rgb) * samplePixel.a, samplePixel.a);
}

float encoded_luma(vec2 uv) {
    return dot(encoded_source(uv).rgb, encodedLightLuma);
}

vec4 compose_halo(vec4 original, vec3 field, bool shadow) {
    float grey = dot(field, encodedLightLuma);
    vec3 halo = max(mix(vec3(grey), field, p.options.z) * p.mode.w * linear_to_srgb(p.tint.rgb), 0.0);
    vec3 color;
    float alpha = original.a;
    if (shadow) {
        color = p.options.x > 0.5 ? halo : original.rgb * (1.0 - clamp(halo, 0.0, 1.0));
    } else {
        color = p.options.x > 0.5 ? halo : original.rgb + halo * (p.options.y > 0.5
            ? vec3(1.0) : 1.0 - min(original.rgb, vec3(1.0)));
    }
    if (!shadow || p.options.x > 0.5) {
        float peak = max(halo.r, max(halo.g, halo.b));
        float coverage = clamp(peak * p.options.w, 0.0, 1.0);
        alpha = p.options.x > 0.5 ? max(coverage, clamp(peak, 0.0, 1.0))
                                : original.a + coverage * (1.0 - original.a);
    }
    vec3 straight = clamp(color / max(alpha, 1e-5), 0.0, 16.0);
    return vec4(srgb_to_linear(straight) * alpha, alpha);
}

// Byte packing preserves integer seed coordinates in either an RGBA8 or FP16
// effect texture. The zero value is reserved for pixels without a valid seed.
vec4 encode_seed(ivec2 pixel) {
    ivec2 v = pixel + 1;
    return vec4(v.x & 255, v.x >> 8, v.y & 255, v.y >> 8) / 255.0;
}
ivec2 decode_seed(vec4 encoded) {
    ivec4 bytes = ivec4(round(encoded * 255.0));
    return ivec2(bytes.x + bytes.y * 256, bytes.z + bytes.w * 256) - 1;
}

vec4 nearest_seed_pass() {
    ivec2 size = textureSize(source, 0);
    ivec2 pixel = min(ivec2(v_uv * vec2(size)), size - 1);
    int jump = max(int(p.edge.x + 0.5), 1);
    ivec2 closest = ivec2(-1);
    float best = 3.0e30;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 tap = pixel + ivec2(x, y) * jump;
            if (any(lessThan(tap, ivec2(0))) || any(greaterThanEqual(tap, size))) continue;
            ivec2 seed = decode_seed(texelFetch(source, tap, 0));
            if (seed.x < 0) continue;
            vec2 delta = vec2(seed - pixel) * p.edge.yz;
            float distSquared = dot(delta, delta);
            if (distSquared < best) {
                best = distSquared;
                closest = seed;
            }
        }
    }
    return closest.x >= 0 ? encode_seed(closest) : vec4(0.0);
}

float threshold_luma(vec2 uv) {
    vec3 offset = linear_to_srgb(p.thresholdColor.rgb);
    return dot(max(encoded_source(uv).rgb - offset, vec3(0.0)), encodedLightLuma);
}

float distance_to_seed(vec2 uv) {
    ivec2 size = textureSize(redHalo, 0);
    vec2 grid = uv * vec2(size) - 0.5;
    ivec2 pixel = clamp(ivec2(round(grid)), ivec2(0), size - 1);
    ivec2 seed = decode_seed(texelFetch(redHalo, pixel, 0));
    if (seed.x < 0) return -1.0;
    return length((vec2(seed) - grid) * p.mode.yz);
}

vec3 rainbow_field(vec2 uv) {
    float distancePx = distance_to_seed(uv);
    if (distancePx < 0.0) return vec3(0.0);
    float radius = p.edge.w;
    float falloff = radius > 1e-5 ? max(1.0 - distancePx / radius, 0.0) : 1.0;
    float phaseDistance = radius > 1e-5 ? distancePx : 0.0;
    vec3 phase = p.thresholdColor.rgb + p.edge.rgb * phaseDistance;
    return falloff * (0.5 - 0.5 * cos(phase * AUREA_TAU));
}

vec3 aura_field(vec2 uv) {
    vec2 dx = vec2(p.mode.y, 0.0);
    vec2 dy = vec2(0.0, p.mode.z);
    vec2 center = texture(redHalo, uv).ra;
    vec2 gradient = vec2(texture(redHalo, uv + dx).r - texture(redHalo, uv - dx).r,
                         texture(redHalo, uv + dy).r - texture(redHalo, uv - dy).r);
    float angle = dot(gradient, gradient) > 1e-10 ? atan(gradient.y, gradient.x) / AUREA_TAU : 0.0;
    vec3 phase = p.thresholdColor.rgb + p.edge.rgb * (1.6 * p.edge.w * center.x - angle);
    float coverage = smoothstep(0.003, 0.035, center.y);
    // The outer control scales the plume beyond the brighter portion of the source.
    float gain = mix(p.thresholdColor.a, 1.0, smoothstep(0.18, 0.45, center.y));
    return coverage * gain * (0.5 - 0.5 * cos(phase * AUREA_TAU));
}

vec3 annulus_field(vec2 uv) {
    // The reference ring has an annular positive lobe and a tighter Gaussian
    // canceling its DC component. Stratified polar quadrature keeps the sample
    // count bounded even when the three channel widths are very different.
    const int samples = 64;
    vec3 radius = max(p.options.rgb, vec3(0.0));
    vec3 thickness = clamp(p.thresholdColor.rgb, 0.0, 1.0);
    vec3 sigma = 0.148 * radius;
    vec3 cutoff = 0.504 * radius * (1.0 - thickness);
    vec3 soft = max(radius * 0.22 * sin(thickness * 3.14159265), vec3(1e-4));
    vec3 minimum = max(cutoff - 0.5 * soft, 0.0);
    vec3 sum = vec3(0.0);
    vec3 weights = vec3(0.0);
    for (int i = 0; i < samples; ++i) {
        float q = (float(i) + 0.5) / float(samples);
        float azimuth = float(i) * 2.39996323;
        vec2 direction = vec2(cos(azimuth), sin(azimuth)) * p.edge.xy;
        vec3 r = sqrt(minimum * minimum - 2.0 * sigma * sigma * log(1.0 - q));
        vec3 w = clamp((r - minimum) / soft, 0.0, 1.0);
        vec3 sampleColor = vec3(texture(source, uv + direction * r.r).r,
                                 texture(source, uv + direction * r.g).g,
                                 texture(source, uv + direction * r.b).b);
        sum += sampleColor * w;
        weights += w;
    }
    vec3 inner = vec3(texture(redHalo, uv).r, texture(greenHalo, uv).g, texture(blueHalo, uv).b);
    vec3 ring = max(sum / max(weights, vec3(1e-5)) - inner, 0.0) * 2.84;
    return mix(vec3(0.0), ring, greaterThan(radius * thickness, vec3(1e-4)));
}

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 haloUv = v_uv * p.haloMap.xy + p.haloMap.zw;
    int stage = int(p.mode.x + 0.5);
    if (stage == 2) {
        ivec2 pixel = min(ivec2(v_uv * p.edge.xy), ivec2(p.edge.xy) - 1);
        o_color = threshold_luma(uv) > p.mode.z ? encode_seed(pixel) : vec4(0.0);
        return;
    }
    if (stage == 3) {
        o_color = nearest_seed_pass();
        return;
    }
    if (stage == 4) {
        o_color = compose_halo(encoded_source(uv), rainbow_field(haloUv), false);
        return;
    }
    if (stage == 5) {
        float luma = threshold_luma(uv);
        o_color = vec4(luma, 0.0, 0.0, luma > p.mode.z ? 1.0 : 0.0);
        return;
    }
    if (stage == 6) {
        o_color = compose_halo(encoded_source(uv), aura_field(haloUv), false);
        return;
    }
    if (stage == 7) {
        vec3 ring = annulus_field(uv);
        o_color = vec4(ring, max(ring.r, max(ring.g, ring.b)));
        return;
    }
    if (stage == 8) {
        float distancePx = distance_to_seed(haloUv);
        vec3 radius = p.edge.rgb;
        vec3 falloff = max(1.0 - distancePx / max(radius, vec3(1e-4)), vec3(0.0));
        vec3 field = distancePx < 0.0 ? vec3(0.0) : falloff * falloff * falloff;
        o_color = compose_halo(encoded_source(uv), field, false);
        return;
    }
    if (stage == 9) {
        vec3 field = texture(redHalo, haloUv).rgb * p.edge.x +
                     texture(greenHalo, haloUv).rgb * p.edge.y +
                     texture(blueHalo, haloUv).rgb * p.edge.z * linear_to_srgb(p.thresholdColor.rgb);
        o_color = compose_halo(encoded_source(uv), field, false);
        return;
    }

    int model = int(p.mode.y + 0.5);
    if (stage == 0) {
        vec4 inputPixel = encoded_source(uv);
        vec3 colorOffset = linear_to_srgb(p.thresholdColor.rgb);
        vec3 field;
        if (model == 1) {
            // Only opaque content can seed a shadow.
            vec3 threshold = max(vec3(p.mode.z) - colorOffset, vec3(0.0));
            field = max(threshold * inputPixel.a - inputPixel.rgb, vec3(0.0)) / max(p.mode.z, 1e-4);
        } else if (model == 2) {
            vec2 dx = vec2(p.edge.x, 0.0);
            vec2 dy = vec2(0.0, p.edge.y);
            vec2 gradient = 0.5 * vec2(encoded_luma(uv + dx) - encoded_luma(uv - dx),
                                      encoded_luma(uv + dy) - encoded_luma(uv - dy));
            float outline = clamp(length(gradient) * p.edge.z * p.edge.w * 0.04 - p.mode.z, 0.0, 1.0);
            field = vec3(outline);
        } else {
            field = hue_preserving_highlights(inputPixel.rgb, p.mode.z, colorOffset);
        }
        field = clamp(field, 0.0, 16.0);
        o_color = vec4(field, max(field.r, max(field.g, field.b)));
        return;
    }

    vec3 field = vec3(texture(redHalo, haloUv).r, texture(greenHalo, haloUv).g, texture(blueHalo, haloUv).b);
    o_color = compose_halo(encoded_source(uv), field, model == 1);
}
