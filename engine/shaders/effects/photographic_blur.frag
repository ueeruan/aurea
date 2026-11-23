#version 450
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_first;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_second;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_third;
layout(set = 0, binding = AUREA_TEX4) uniform sampler2D u_fourth;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 flags;
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 p4;
} p;

const vec3 lumaWeights = vec3(0.3125, 0.5, 0.1875);

vec4 decode_signal(vec4 value) {
    float alpha = clamp(value.a, 0.0, 1.0);
    vec3 color = alpha > 1e-6 ? srgb_to_linear(max(value.rgb / alpha, vec3(0.0))) : vec3(0.0);
    return vec4(color * alpha, alpha);
}

vec4 sample_path(sampler2D signal, bool motion, int samples) {
    vec4 total = vec4(0.0);
    float sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        float t = (float(i) + 0.5) / float(samples);
        float weight = mix(1.0 - p.p0.w, p.p0.w, t);
        vec2 uv;
        if (motion) {
            float angle = mix(p.p1.z, p.p1.w, t);
            float c = cos(angle);
            float s = sin(angle);
            vec2 aspect = vec2(max(p.p0.z, 1e-6), 1.0);
            vec2 shift = mix(p.p2.xy, p.p2.zw, t);
            vec2 delta = (v_uv - p.p0.xy - shift) * aspect;
            delta = mat2(c, s, -s, c) * delta * mix(p.p1.x, p.p1.y, t);
            uv = p.p0.xy + delta / aspect;
        } else {
            float position = t - 0.5;
            if (p.p1.x < 0.5) weight *= exp(-18.0 * position * position);
            if (p.p1.x > 1.5) weight *= 1.0 - 2.0 * abs(position);
            uv = v_uv + p.p0.xy * (position + p.p0.z);
        }
        total += texture(signal, uv) * weight;
        sum += weight;
    }
    return total / max(sum, 1e-6);
}

vec4 sample_prism(vec4 source) {
    float separation = p.p0.z;
    vec3 radialOffset = separation < 0.0 ? vec3(0.525, 0.054, -0.621) : vec3(-0.621, 0.054, 0.525);
    vec3 thicknessOffset = separation < 0.0 ? vec3(2.82, 0.003, -1.732) : vec3(-1.732, 0.003, 2.82);
    vec3 radius = vec3(1.04) + radialOffset * abs(separation);
    vec3 halfWidth = p.p0.w * max(vec3(0.08), vec3(1.0) + thicknessOffset * abs(separation));
    vec3 inner = max(radius - halfWidth, vec3(0.0));
    vec3 outer = radius + halfWidth;
    vec3 sum = vec3(0.0);
    const int count = 48;
    for (int i = 0; i < count; ++i) {
        float t = (float(i) + 0.5) / float(count);
        float theta = float(i) * 2.39996323;
        float aperture = 1.0;
        if (p.p1.y >= 3.0) {
            float sector = 6.28318531 / p.p1.y;
            aperture = cos(sector * 0.5) / cos(mod(theta, sector) - sector * 0.5);
        }
        vec2 travel = vec2(cos(theta + p.p1.x), sin(theta + p.p1.x)) * p.p0.xy * aperture;
        vec3 r = sqrt(mix(inner * inner, outer * outer, t));
        vec2 shift = (v_uv - vec2(0.5)) * dot(v_uv - vec2(0.5), v_uv - vec2(0.5)) * p.p1.z;
        vec3 tap = vec3(texture(u_source, v_uv + travel * r.r - shift).r,
                        texture(u_source, v_uv + travel * r.g).g,
                        texture(u_source, v_uv + travel * r.b + shift).b);
        sum += p.p1.w == 1.0 ? tap : pow(max(tap, vec3(0.0)), vec3(p.p1.w));
    }
    vec3 value = sum / float(count);
    if (p.p1.w != 1.0) value = pow(max(value, vec3(0.0)), vec3(1.0 / p.p1.w));
    return vec4(value, source.a);
}

void main() {
    int mode = int(p.flags.x + 0.5);
    vec4 source = texture(u_source, v_uv);
    if (mode == 0) {
        // A blur in the encoded signal averages brightness differently from
        // our normal linear-light blur. Keep this signal local to the passes.
        o_color = vec4(linear_to_srgb(unpremultiply(source).rgb) * source.a, source.a);
        return;
    }

    if (mode == 5 || mode == 6) {
        vec4 value = sample_path(u_source, mode == 6, 48);
        value.rgb = value.rgb * p.flags.y + p.flags.z * value.a;
        o_color = decode_signal(mix(value, source, p.flags.w));
        return;
    }
    if (mode == 13) {
        o_color = sample_path(u_source, true, int(p.p3.x));
        return;
    }
    if (mode == 9) {
        o_color = sample_prism(source);
        return;
    }
    if (mode == 11) {
        vec3 threshold = vec3(p.p3.x) + linear_to_srgb(p.p4.rgb);
        float energy = dot(max(source.rgb - threshold, vec3(0.0)), vec3(1.0));
        float total = dot(source.rgb, vec3(1.0));
        o_color = vec4(source.rgb * (energy / max(total, 1e-6)), 0.0);
        return;
    }
    if (mode == 12) {
        vec3 brights = texture(u_first, v_uv).rgb;
        vec3 trails = sample_path(u_first, true, int(p.p3.w)).rgb * p.flags.y;
        vec3 background = max(source.rgb - brights, vec3(0.0)) * p.p3.x + brights * p.p3.y;
        vec3 result = background + trails;
        if (p.p3.z > 0.5) result -= min(background, vec3(1.0)) * min(trails, vec3(1.0));
        float alpha = clamp(source.a + max(max(trails.r, trails.g), trails.b) * p.flags.w, 0.0, 1.0);
        o_color = decode_signal(vec4(result * p.flags.z, alpha));
        return;
    }

    vec4 first = texture(u_first, v_uv);
    vec4 value = first;
    if (mode == 1) {
        float alpha = clamp(first.a * p.p0.x, 0.0, 1.0);
        value = vec4(first.a > 1e-6 ? first.rgb * (alpha / first.a) : vec3(0.0), alpha);
    } else if (mode == 2) {
        value = vec4(first.r, texture(u_second, v_uv).g,
                     texture(u_third, v_uv).b, texture(u_fourth, v_uv).a);
    } else if (mode == 3) {
        vec4 second = texture(u_second, v_uv);
        float luma = dot(second.rgb, lumaWeights);
        vec3 chroma = first.rgb - vec3(dot(first.rgb, lumaWeights));
        value = vec4(chroma * p.p0.x + vec3(luma * p.p0.y), source.a);
    } else if (mode == 4) {
        vec3 original = unpremultiply(source).rgb;
        vec3 blurred = unpremultiply(first).rgb;
        value = vec4(mix(original, blurred, p.p0.x) * source.a, source.a);
    } else if (mode == 7) {
        vec4 edge = texture(u_second, v_uv);
        float contrast = abs(dot(source.rgb - edge.rgb, lumaWeights));
        float weight = clamp(max(0.0, contrast - p.p0.y) * p.p0.x * 2.4, 0.0, 1.0);
        value = mix(source, first, weight);
    } else if (mode == 8) {
        vec3 base = unpremultiply(source).rgb;
        vec3 tiny = unpremultiply(first).rgb;
        vec3 broad = unpremultiply(texture(u_second, v_uv)).rgb;
        vec3 detail = (base - tiny) * 2.0 + (tiny - broad) * 0.1;
        detail = mix(vec3(dot(detail, lumaWeights)), detail, p.p0.y);
        value = vec4((base + detail * p.p0.x) * source.a, source.a);
    } else if (mode == 10) {
        value = vec4(first.rgb, source.a);
    }
    value.rgb = value.rgb * p.flags.y + p.flags.z * value.a;
    o_color = decode_signal(mix(value, source, p.flags.w));
}
