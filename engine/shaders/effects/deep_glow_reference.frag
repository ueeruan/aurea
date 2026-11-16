#version 450
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_other;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_prepared;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Parameters {
    vec4 sourceUv;
    vec4 maskRegion;
    vec4 region;
    vec4 haloUv;
    vec4 inputControls;
    vec4 composite;
    vec4 chromatic;
    vec4 tintColor;
    vec4 tint;
    vec4 noise;
    vec4 kernel;
    vec4 accumulation;
    vec4 extra;
    mat4 compFromLayer;
} p;

vec3 source_light(vec2 uv) {
    vec3 color = texture(u_source, uv).rgb;
    if (p.chromatic.x > 0.5) {
        vec3 shifts = vec3(p.chromatic.z, 0, -p.chromatic.z);
        if (p.chromatic.y < 0.5) shifts = vec3(p.chromatic.z, -p.chromatic.z, 0);
        if (p.chromatic.y > 1.5) shifts = vec3(0, p.chromatic.z, -p.chromatic.z);
        color.r = texture(u_source, uv + vec2(shifts.r)).r;
        color.g = texture(u_source, uv + vec2(shifts.g)).g;
        color.b = texture(u_source, uv + vec2(shifts.b)).b;
    }
    return max(color, vec3(0));
}

vec3 tint_light(vec3 light) {
    if (p.tint.x < 0.5) return light;
    vec3 ink = max(p.tintColor.rgb, vec3(0));
    vec3 colored = light * ink;
    if (p.tint.y > 0.5 && p.tint.y < 1.5) {
        float exponent = p.inputControls.w > 0.5 ? 1.0 : 20.0 / 9.0;
        vec3 base = pow(max(light, vec3(0)), vec3(1.0 / exponent));
        vec3 pigment = pow(ink, vec3(1.0 / exponent));
        vec3 low = 2.0 * base * pigment;
        vec3 high = 1.0 - 2.0 * (1.0 - base) * (1.0 - pigment);
        colored = pow(max(mix(low, high, step(vec3(0.5), base)), vec3(0)), vec3(exponent));
    }
    if (p.tint.y > 1.5) {
        vec3 base = min(light, vec3(1));
        colored = base * base + 2.0 * ink * base * (1.0 - base);
    }
    return mix(light, colored, p.tint.z);
}

float noise_value(uvec2 pixel, uint channel) {
    uint value = pixel.x * 1973u + pixel.y * 9277u + channel * 26699u + 89173u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    return float(value & 0xffffu) / 65535.0;
}

void main() {
    int stage = int(p.kernel.w + 0.5);
    if (stage == 0) {
        vec2 uv = v_uv * p.sourceUv.xy + p.sourceUv.zw;
        vec3 light = source_light(uv);
        if (p.extra.x > 0.5) {
            vec2 position = p.region.xy + v_uv * p.region.zw;
            vec4 projected = p.compFromLayer * vec4(position, 0, 1);
            vec2 maskUv = (projected.xy / max(abs(projected.w), 1e-6) - p.maskRegion.xy) / p.maskRegion.zw;
            vec4 mask = texture(u_other, maskUv);
            float coverage = p.tint.w < 0.5 ? mask.a : dot(mask.rgb, vec3(0.2126, 0.7152, 0.0722));
            if (p.noise.w > 0.5) coverage = 1.0 - coverage;
            light *= clamp(coverage, 0.0, 1.0);
        }
        float threshold = p.inputControls.x;
        if (threshold > 0.0) {
            threshold = max(threshold, 0.001);
            float smoothness = min(p.inputControls.y, threshold);
            vec3 below = light * light * (smoothness / threshold);
            light = mix(light, below, lessThan(light, vec3(threshold)));
        }
        if (p.accumulation.w > 0.5) {
            if (p.inputControls.w > 0.5) light = pow(light, vec3(20.0 / 9.0));
            if (p.composite.x < 0.5) light = min(light, vec3(1));
            else light *= p.inputControls.z;
        }
        o_color = vec4(light, 1);
        return;
    }
    if (stage == 1 || stage == 5) {
        int steps = clamp(int(p.kernel.z + 0.5), 1, 10000);
        vec3 sum = vec3(0);
        float normalization = 0.0;
        for (int sampleIndex = -steps; sampleIndex <= steps; ++sampleIndex) {
            float distance = float(sampleIndex) / float(steps);
            float weight = exp(-distance * distance / 0.12);
            sum += texture(u_source, v_uv + p.kernel.xy * float(sampleIndex)).rgb * weight;
            normalization += weight;
        }
        vec3 light = sum / max(normalization, 1e-6);
        if (stage == 5) {
            vec3 previous = texture(u_other, v_uv).rgb * p.accumulation.z;
            vec3 combined = light + previous;
            if (p.composite.x < 0.5) combined -= light * previous;
            light = combined;
        }
        o_color = vec4(light, 1);
        return;
    }
    vec4 original = texture(u_source, v_uv * p.sourceUv.xy + p.sourceUv.zw);
    if (stage == 4) {
        o_color = vec4(texture(u_prepared, v_uv).rgb, 1);
        return;
    }
    vec3 glow = max(texture(u_other, v_uv * p.haloUv.xy + p.haloUv.zw).rgb, vec3(0));
    if (p.composite.x < 0.5) glow *= p.inputControls.z;
    if (p.inputControls.w > 0.5) glow = pow(glow, vec3(9.0 / 20.0));
    if (p.noise.x > 0.5) {
        uvec2 pixel = uvec2(max(p.region.xy + v_uv * p.region.zw, vec2(0)));
        float red = noise_value(pixel, 0u);
        vec3 grain = p.noise.y > 0.5 ? vec3(red) : vec3(red, noise_value(pixel, 1u), noise_value(pixel, 2u));
        float amount = p.noise.z * (p.inputControls.w > 0.5 ? 1.0 : 0.1);
        glow += grain * (0.0035 * amount * min(dot(glow, vec3(1.0 / 3.0)), 1.0));
    }
    glow = max(tint_light(glow), vec3(0));
    float maximum = clamp(max(glow.r, max(glow.g, glow.b)), 0.0, 1.0);
    float glowAlpha = p.composite.w > 0.5 ? pow(maximum, 9.0 / 20.0) : 1.0;
    if (p.composite.w > 0.5) glow = maximum > 0.0 ? glow * (glowAlpha / maximum) : vec3(0);
    if (p.composite.x < 0.5) glow = min(glow, vec3(1));
    vec3 source = original.rgb * p.composite.z;
    float sourceAlpha = original.a * p.composite.z;
    vec3 rgb = glow + source;
    if (p.composite.x < 0.5) {
        rgb -= min(glow, vec3(1)) * min(source, vec3(1));
        if (p.composite.y > 0.5) rgb = source + glow * (1.0 - clamp(source, 0.0, 1.0));
    }
    float alpha = sourceAlpha + glowAlpha * (1.0 - sourceAlpha);
    o_color = vec4(clamp(rgb, vec3(0), vec3(65504)), alpha);
}
