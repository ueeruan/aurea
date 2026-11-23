#version 450
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_matte;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;
    vec4 response;
    vec4 midpoints;
    vec4 gains;
    vec4 sigma;
    vec4 sampling;
    vec4 matteRegion;
    vec4 compX;
    vec4 compY;
    vec4 compW;
} p;

vec3 sample_noise(ivec2 pixel, uint seed) {
    uvec2 cell = uvec2(pixel) ^ uvec2(seed, seed * 0x9e3779b9u);
    float red = aurea_hash(cell);
    vec3 values = p.sampling.w > 0.5 ? vec3(red)
        : vec3(red, aurea_hash(cell ^ uvec2(0x524bc873u, 0x594cac8du)),
                    aurea_hash(cell ^ uvec2(0xd7beb593u, 0xaa54ad71u)));
    return (values - 0.5) * 3.4641016; // unit variance
}

vec3 filtered_grain(vec2 point, uint seed) {
    ivec2 base = ivec2(floor(point));
    vec2 fraction = fract(point);
    vec3 sigma = clamp(p.sigma.xyz, 0.23, 1.5);
    vec3 inv = vec3(-0.72134752) / (sigma * sigma);
    vec3 h[4];
    vec3 v[4];
    for (int i = 0; i < 4; ++i) {
        float x = float(i - 1) - fraction.x;
        float y = float(i - 1) - fraction.y;
        // The window reaches zero at either end of the lattice support, so
        // crossing an integer coordinate cannot swap a nonzero sample.
        h[i] = exp2(inv * x * x) * min(2.0 - abs(x), 1.0);
        v[i] = exp2(inv * y * y) * min(2.0 - abs(y), 1.0);
    }
    vec3 sum = vec3(0.0);
    vec3 square = vec3(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            vec3 weight = h[x] * v[y];
            sum += sample_noise(base + ivec2(x - 1, y - 1), seed) * weight;
            square += weight * weight;
        }
    }
    return sum * inversesqrt(max(square, vec3(1e-8)));
}

float matte_weight(vec2 point) {
    if (p.matteRegion.w <= 0.0) return 1.0;
    vec3 pixel = vec3(point, 1.0);
    float w = dot(p.compW.xyz, pixel);
    if (w <= 1e-6) return 0.0;
    vec2 position = vec2(dot(p.compX.xyz, pixel), dot(p.compY.xyz, pixel)) / w;
    vec3 matte = texture(u_matte, (position - p.matteRegion.xy) / p.matteRegion.zw).rgb;
    return clamp(aurea_luma(matte), 0.0, 1.0);
}

void main() {
    vec4 inputColor = texture(u_source, v_uv);
    vec2 point = p.region.xy + v_uv * p.region.zw;
    float matte = matte_weight(point);
    if (inputColor.a <= 1e-6 || matte <= 0.0) {
        o_color = inputColor;
        return;
    }
    vec3 rgb = inputColor.rgb / inputColor.a;
    vec3 encoded = aurea_linear_to_srgb(rgb);
    vec3 midpoint = p.midpoints.xyz;
    vec3 low = mix(vec3(p.response.x), vec3(p.response.y), min(encoded / midpoint, vec3(1.0)));
    vec3 high = mix(vec3(p.response.z), vec3(p.response.y),
                    pow(clamp((1.0 - encoded) / (1.0 - midpoint), 0.0, 1.0), vec3(1.5)));
    vec3 response = mix(high, low, lessThanEqual(encoded, midpoint));
    uint seed = uint(p.sampling.y) | (uint(p.sampling.z) << 16);
    // A pixel at reduced preview density averages more of the noise field.
    float footprint = min(1.0, max(p.sampling.x, 1.0) * p.sigma.w);
    vec3 noise = filtered_grain((point - vec2(0.5)) / p.sampling.x, seed)
        * p.gains.xyz * response * matte * footprint;
    vec3 delta = aurea_srgb_to_linear(encoded + noise) - aurea_srgb_to_linear(encoded);
    // Preserve the stored alpha and any scene-referred range outside SDR.
    o_color = vec4((rgb + delta) * inputColor.a, inputColor.a);
}
