#version 450
// =============================================================================
//  Aurea / shaders / effects / glitchify_reference.frag
//
//  Glitchify reference, compose pass (order of the native render 0x18005E570):
//    O = T (the transform/transition world of the first pass)
//    Channel Glitch:  O = mode(O, scale(split(T)), 100 %)
//    Color Glitch:    O = mode(O, corrupt(T), Opacity)      (bytes from the compute pass)
//    Image Glitch:    T' = pixel streak(T)
//                     O = mode(O, slice(T'), Slice Opacity)
//                     O = mode(O, blocks(T'), 100 %)
//    Compression:     O = compress(O)
//    Composite Over:  O over the original layer
//  Exact (REA): channel split offsets, block copy rule, mode selection. Own
//  approximations (documented in GlitchifyReference.cpp): channel-scale
//  ordering/fill, pixel-streak segments, the WebP slice damage, JPEG/dither.
// =============================================================================
#include "glitchify_reference_common.glsl"
#include "glitchify_reference_xfer.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_base;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_bytes;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_original;

ivec2 tex_size() { return textureSize(u_base, 0); }
vec2 to_px(vec2 texel) { return u.region.xy + (texel + 0.5) / vec2(tex_size()) * u.region.zw; }
ivec2 to_texel(vec2 px) { return ivec2(floor((px - u.region.xy) / u.region.zw * vec2(tex_size()))); }
vec4 base_at(ivec2 t, bool clampEdges, out bool inside) {
    const ivec2 s = tex_size();
    inside = all(greaterThanEqual(t, ivec2(0))) && all(lessThan(t, s));
    if (!inside && !clampEdges) return vec4(0.0);
    return texelFetch(u_base, clamp(t, ivec2(0), s - 1), 0);
}
vec4 base_at(ivec2 t) { bool unused; return base_at(t, true, unused); }

// --- Channel Glitch ------------------------------------------------------------
vec3 rgb2ycc(vec3 c) {
    const float y = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
    return vec3(y, 128.0 + (c.b - y) * 0.564, 128.0 + (c.r - y) * 0.713);
}
vec3 ycc2rgb(vec3 y) {
    return vec3(y.x + 1.403 * (y.z - 128.0), y.x - 0.344 * (y.y - 128.0) - 0.714 * (y.z - 128.0), y.x + 1.773 * (y.y - 128.0));
}
vec3 rgb2hsv(vec3 c) {
    const float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float h = 0.0;
    if (d > 0.0) {
        if (mx == c.r) h = (c.g - c.b) / d; else if (mx == c.g) h = 2.0 + (c.b - c.r) / d; else h = 4.0 + (c.r - c.g) / d;
        h = h < 0.0 ? h + 6.0 : h;
    }
    return vec3(h / 6.0 * 255.0, mx > 0.0 ? d / mx * 255.0 : 0.0, mx);
}
vec3 hsv2rgb(vec3 h) {
    const float s = h.y / 255.0, v = h.z, hh = mod(h.x / 255.0 * 6.0, 6.0);
    const float c = v * s, x = c * (1.0 - abs(mod(hh, 2.0) - 1.0)), m = v - c;
    vec3 r = hh < 1.0 ? vec3(c, x, 0) : hh < 2.0 ? vec3(x, c, 0) : hh < 3.0 ? vec3(0, c, x)
           : hh < 4.0 ? vec3(0, x, c) : hh < 5.0 ? vec3(x, 0, c) : vec3(c, 0, x);
    return r + m;
}

vec4 split_at(ivec2 t) {
    const vec4 here = gx_to_bytes(base_at(t));
    bool inside;
    // Native: the moved component is read at (x + dx, y + dy), clamped with Repeat Edge Pixels.
    const vec4 moved = gx_to_bytes(base_at(t + ivec2(u.split.yz), u.split.w > 0.5, inside));
    const int mode = int(u.split.x + 0.5);
    vec4 b = here;
    if (mode < 3) {
        b[mode] = moved[mode];
    } else if (mode < 6) {
        vec3 a = rgb2ycc(here.rgb);
        a[mode - 3] = rgb2ycc(moved.rgb)[mode - 3];
        b.rgb = clamp(floor(ycc2rgb(a) + 0.5), 0.0, 255.0);
    } else {
        vec3 a = rgb2hsv(here.rgb);
        a[mode - 6] = rgb2hsv(moved.rgb)[mode - 6];
        b.rgb = clamp(floor(hsv2rgb(a) + 0.5), 0.0, 255.0);
    }
    return b;
}
float scale_key(vec4 b) {
    const int ch = int(u.scale.x + 0.5);
    const float mx = max(b.r, max(b.g, b.b)), mn = min(b.r, min(b.g, b.b));
    if (ch == 0) return floor((299.0 * b.r + 587.0 * b.g + 114.0 * b.b) / 1000.0);
    if (ch == 1) return floor((mx + mn) * 0.5);
    if (ch == 2) return rgb2hsv(b.rgb).x;
    if (ch == 3) return mx > 0.0 ? floor((mx - mn) * 255.0 / mx) : 0.0;
    if (ch == 4) return b.r;
    if (ch == 5) return b.g;
    if (ch == 6) return b.b;
    return b.a;
}
/// Channel Scale (native 0x180057E60/0x1800589F0): each pixel of the split
/// world is SCATTERED along the axis to
///     x' = ((255 − trunc(S·k/100)) · (x − c)) / 255 + c      (integer math)
/// k = its key, c = the Offset line; when several land on one pixel the
/// larger key wins (a later equal key replaces). Unwritten pixels stay
/// transparent. Reproduced here as a gather over a bounded window.
int scatter_target(int x, int c, float key) {
    const int s = int(u.scale.w);
    return ((255 - (s * int(key)) / 100) * (x - c)) / 255 + c;
}
vec4 channel_at(ivec2 t) {
    if (abs(u.scale.w) < 0.5) return split_at(t);
    const bool vertical = u.scale.y > 0.5;
    const ivec2 size = tex_size();
    const int axis = vertical ? t.y : t.x;
    const int line = int(u.scale.z);
    const int extent = vertical ? size.y : size.x;
    const int reach = min(96, int(ceil(abs(u.scale.w) * 2.55 / 255.0 * float(extent))) + 1);
    vec4 best = vec4(0.0);
    float bestKey = -1.0;
    for (int d = -reach; d <= reach; ++d) {
        const int src = axis + d;
        if (src < 0 || src >= extent) continue;
        const ivec2 q = vertical ? ivec2(t.x, src) : ivec2(src, t.y);
        const vec4 b = split_at(q);
        const float k = scale_key(b);
        if (scatter_target(src, line, k) != axis) continue;
        if (k >= bestKey) { bestKey = k; best = b; }
    }
    return best;
}

// --- Image Glitch --------------------------------------------------------------
float luma_byte(vec4 b) { return (299.0 * b.r + 587.0 * b.g + 114.0 * b.b) / 1000.0; }
/// Pixel Streak: in rows (columns with Vertical Sort) picked by the noise
/// cutoff, the brightest pixel within `strength` behind wins (own segment rule).
vec4 streak_at(ivec2 t) {
    vec4 best = base_at(t);
    const int len = int(u.streak.x + 0.5);
    if (len <= 0) return best;
    const bool vertical = u.streak.y > 0.5;
    const float lineIndex = vertical ? float(t.x) : float(t.y);
    if (u.streak.z > 0.5 && gx_noise_map(lineIndex, 0.0, u.block2.y, 101.0) < u.streak.w) return best;
    float key = luma_byte(gx_to_bytes(best));
    for (int i = 1; i <= 64; ++i) {
        if (i > len) break;
        const vec4 c = base_at(vertical ? t - ivec2(0, i) : t - ivec2(i, 0));
        const float k = luma_byte(gx_to_bytes(c));
        if (k > key) { key = k; best = c; }
    }
    return best;
}
vec4 slice_at(ivec2 t, vec2 px) {
    vec4 c = base_at(t);
    if (px.y < u.slice.x || px.y >= u.slice.x + u.slice.y || u.slice.y <= 0.0) return c;
    // Own stand-in for the WebP re-encode with one flipped bit: from a noise-
    // picked column the band desynchronises (shift + 8×8 block quantisation).
    const float breakX = gx_noise_map(7.0, 7.0, u.block2.w, 1.0) * u.region.z;
    const float shift = floor((gx_noise_map(11.0, 3.0, u.block2.w, 1.0) - 0.5) * 64.0 * u.slice.w);
    ivec2 q = t;
    if (px.x >= breakX) q.x += int(shift);
    const ivec2 blockOrigin = (q / 8) * 8;
    const vec4 blocky = base_at(blockOrigin + ivec2(4));
    return mix(base_at(q), blocky, u.slice.w * 0.5);
}
vec4 block_at(ivec2 t, vec2 px) {
    if (u.block.y < 1.0 || u.block.z < 1.0) return base_at(t);
    const vec2 local = px - u.region.xy;
    const ivec2 nb = ivec2(max(vec2(1.0), ceil(u.region.zw / u.block.yz)));
    const ivec2 cell = ivec2(floor(local / u.block.yz));
    const float v = gx_noise_map(float(cell.x) * u.block2.x, float(cell.y) * u.block2.x, u.block2.y, 101.0);
    if (v <= u.block.x) return base_at(t);
    ivec2 to = cell;
    const int dir = int(u.block.w + 0.5);
    if (dir != 1) to.x = int(mod(float(cell.x - 3 + int(mod(floor(v / 5.0), 6.0))), float(nb.x)));
    if (dir != 0) to.y = int(mod(float(cell.y - 3 + int(mod(floor(v / 5.0 + 4.0), 6.0))), float(nb.y)));
    const vec2 fromPx = px + vec2(to - cell) * u.block.yz;
    return base_at(to_texel(fromPx));
}

// --- Compression ---------------------------------------------------------------
vec4 compress(vec4 c, ivec2 t) {
    vec4 b = gx_to_bytes(c);
    if (u.compress.x > 0.0) {
        // Own stand-in for the JPEG pass: 8×8 block mean pulled in by strength.
        const ivec2 o = (t / 8) * 8;
        vec4 m = vec4(0.0);
        for (int y = 0; y < 8; y += 2) for (int x = 0; x < 8; x += 2) m += gx_to_bytes(base_at(o + ivec2(x, y)));
        m /= 16.0;
        b.rgb = mix(b.rgb, m.rgb, clamp(u.compress.x, 0.0, 1.0));
        const float q = 1.0 + 31.0 * clamp(u.compress.x, 0.0, 1.0);
        b.rgb = floor(b.rgb / q + 0.5) * q;
    }
    if (u.compress.y > 0.0) {
        // Own stand-in for the Floyd–Steinberg palette: ordered 4×4 threshold.
        const int idx = (t.x & 3) + (t.y & 3) * 4;
        const float bayer[16] = float[16](0., 8., 2., 10., 12., 4., 14., 6., 3., 11., 1., 9., 15., 7., 13., 5.);
        const float levels = mix(255.0, 2.0, clamp(u.compress.y, 0.0, 1.0));
        const float step_ = 255.0 / max(levels - 1.0, 1.0);
        b.rgb = clamp(floor(b.rgb / step_ + bayer[idx] / 16.0) * step_, 0.0, 255.0);
    }
    return gx_from_bytes(vec4(b.rgb, b.a));
}

void main() {
    // The native modules all work on ONE running world (local_ae8), each
    // composited back into it before the next reads it; stage = u.compress.z.
    const ivec2 t = ivec2(gl_FragCoord.xy);
    const vec2 px = to_px(vec2(t));
    vec4 O = base_at(t);
    const int stage = int(u.compress.z + 0.5);
    if (stage == 1) {
        O = gx_transfer(int(u.modes.x + 0.5), O, gx_from_bytes(channel_at(t)), 1.0);
    } else if (stage == 2) {
        const int x4 = t.x * 4;
        const vec4 b = vec4(texelFetch(u_bytes, ivec2(x4 + 1, t.y), 0).x, texelFetch(u_bytes, ivec2(x4 + 2, t.y), 0).x,
                            texelFetch(u_bytes, ivec2(x4 + 3, t.y), 0).x, texelFetch(u_bytes, ivec2(x4, t.y), 0).x);
        O = gx_transfer(int(u.modes.y + 0.5), O, gx_from_bytes(b), u.block2.z);
    } else if (stage == 3) {
        O = streak_at(t);
    } else if (stage == 4) {
        O = gx_transfer(int(u.modes.z + 0.5), O, slice_at(t, px), u.slice.z);
    } else {
        if (u.enables.z > 0.5) O = gx_transfer(int(u.modes.w + 0.5), O, block_at(t, px), 1.0);
        if (u.enables.w > 0.5) O = compress(O, t);
        if (u.transition.y > 0.5) {
            const vec4 original = texelFetch(u_original, t, 0);
            O = O + original * (1.0 - clamp(O.a, 0.0, 1.0));
        }
    }
    o_color = O;
}
