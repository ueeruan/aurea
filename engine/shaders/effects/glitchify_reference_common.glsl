// =============================================================================
//  Aurea / shaders / effects / glitchify_reference_common.glsl
//
//  Shared block of the Glitchify reference passes (GlitchifyReference.cpp
//  documents every lane) and the noise engine: octave-summed 3D simplex noise
//  at (y/height, x/height, z/height) × lacunarity^o, amplitude × gain^o,
//  result · 16 + 0.5. The permutation comes from the C++ side (CRT LCG
//  shuffle of 0..255 by Random Seed); gradient 10 equals gradient 11 (the
//  native table repeats that entry). Verified against the AE frame
//  glitchify-amount0-show-noise: ≤ 1 byte (batch-c/models/glitchify_noise.py).
// =============================================================================
#ifndef AUREA_GLITCHIFY_REFERENCE_COMMON_GLSL
#define AUREA_GLITCHIFY_REFERENCE_COMMON_GLSL

#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;      // layer px of the input/output texture (x, y, w, h)
    vec4 layer;       // xy = layer size px, z = byte encoding (0 sRGB, 1 linear), w = time z of the engine
    vec4 noise;       // x = octaves, y = lacunarity, z = gain, w = Glitchify Amount (0..1)
    vec4 stage;       // x = show noise, y = color byte offset, z = color k, w = color enabled
    vec4 inverse0;    // transform: src.x = dot(xy1, inverse0.xyz)
    vec4 inverse1;    // src.y = dot(xy1, inverse1.xyz)
    vec4 crop;        // destination crop rect (x0, y0, x1, y1), layer px
    vec4 transform;   // x = mode, y = opacity, z = completion (0..1), w = softness (0..255)
    vec4 transition;  // x = channel (0..8), y = composite over
    vec4 split;       // x = mode (0..8), y = dx, z = dy, w = repeat edges
    vec4 scale;       // x = channel (0..7), y = vertical, z = offset line px, w = signed strength
    vec4 modes;       // x = channel mode, y = color mode, z = slice mode, w = image mode
    vec4 enables;     // x = channel, y = color, z = image, w = compression
    vec4 streak;      // x = strength px, y = vertical, z = use noise, w = cutoff (0..100)
    vec4 slice;       // x = band top, y = band height, z = opacity, w = damage (0..1)
    vec4 block;       // x = threshold (0..100), y = width px, z = height px, w = direction
    vec4 block2;      // x = grouping, y = time z, z = color opacity, w = slice time z
    vec4 compress;    // x = jpeg strength, y = dither strength, z = 0, w = 0
} u;

layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_perm;

int gx_perm(int i) { return int(texelFetch(u_perm, ivec2(i & 255, 0), 0).x + 0.5); }

vec3 gx_grad(int g) {
    if (g == 0) return vec3(1, 1, 0);
    if (g == 1) return vec3(-1, 1, 0);
    if (g == 2) return vec3(1, -1, 0);
    if (g == 3) return vec3(-1, -1, 0);
    if (g == 4) return vec3(1, 0, 1);
    if (g == 5) return vec3(-1, 0, 1);
    if (g == 6) return vec3(1, 0, -1);
    if (g == 7) return vec3(-1, 0, -1);
    if (g == 8) return vec3(0, 1, 1);
    if (g == 9) return vec3(0, -1, 1);
    return vec3(0, -1, -1);
}

float gx_simplex(vec3 p) {
    const float F3 = 1.0 / 3.0, G3 = 1.0 / 6.0;
    const float s = (p.x + p.y + p.z) * F3;
    const vec3 cell = floor(p + s);
    const float t = (cell.x + cell.y + cell.z) * G3;
    const vec3 x0 = p - (cell - t);
    vec3 o1, o2;
    if (x0.x >= x0.y) {
        if (x0.y >= x0.z) { o1 = vec3(1, 0, 0); o2 = vec3(1, 1, 0); }
        else if (x0.x >= x0.z) { o1 = vec3(1, 0, 0); o2 = vec3(1, 0, 1); }
        else { o1 = vec3(0, 0, 1); o2 = vec3(1, 0, 1); }
    } else {
        if (x0.y < x0.z) { o1 = vec3(0, 0, 1); o2 = vec3(0, 1, 1); }
        else if (x0.x < x0.z) { o1 = vec3(0, 1, 0); o2 = vec3(0, 1, 1); }
        else { o1 = vec3(0, 1, 0); o2 = vec3(1, 1, 0); }
    }
    const ivec3 c = ivec3(cell) & 255;
    float n = 0.0;
    for (int k = 0; k < 4; ++k) {
        const vec3 o = k == 0 ? vec3(0) : (k == 1 ? o1 : (k == 2 ? o2 : vec3(1)));
        const vec3 d = x0 - o + float(k) * G3;
        float r = 0.6 - dot(d, d);
        if (r < 0.0) continue;
        const ivec3 oi = ivec3(o);
        const int g = gx_perm(c.x + oi.x + gx_perm(c.y + oi.y + gx_perm(c.z + oi.z))) % 12;
        r *= r;
        n += r * r * dot(gx_grad(g), d);
    }
    return n;
}

/// Native sample (x, y in layer px, z = time·multiplier): about 0..1.
float gx_noise(float x, float y, float z) {
    float f = 1.0 / max(u.layer.y, 1.0), amplitude = 1.0, total = 0.0;
    const int octaves = int(u.noise.x + 0.5);
    for (int o = 0; o < 16; ++o) {
        if (o >= octaves) break;
        total += amplitude * gx_simplex(vec3(y * f, x * f, z * f));
        f *= u.noise.y;
        amplitude *= u.noise.z;
    }
    return total * 16.0 + 0.5;
}
/// The 0..top map with the soft knees at 10 % and 90 %.
float gx_noise_map(float x, float y, float z, float top) {
    float v = gx_noise(x, y, z) * top;
    if (v > top * 0.9) v = v * 0.2 + top * 0.9 * 0.8;
    if (v < top * 0.1) v = v * 0.2 + top * 0.1 * 0.8;
    if (v >= top) v = top - 1.0;
    return max(v, 0.0);
}

// --- 8-bit domain --------------------------------------------------------------
// The native effect is 8 bpc ARGB. Bytes are sRGB-encoded (a standard AE
// project) or linear (the linearized 32 bpc oracle project): layer.z.
vec4 gx_to_bytes(vec4 premul) {
    const float a = clamp(premul.a, 0.0, 1.0);
    vec3 c = a > 1e-6 ? clamp(premul.rgb / a, 0.0, 1.0) : vec3(0.0);
    if (u.layer.z < 0.5) c = linear_to_srgb(c);
    return floor(vec4(c, a) * 255.0 + 0.5);
}
vec4 gx_from_bytes(vec4 b) {
    vec3 c = clamp(b.rgb, 0.0, 255.0) / 255.0;
    if (u.layer.z < 0.5) c = srgb_to_linear(c);
    const float a = clamp(b.a, 0.0, 255.0) / 255.0;
    return vec4(c * a, a);
}

#endif
