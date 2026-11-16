#version 450
// =============================================================================
//  Aurea / shaders / effects / glitchify_reference_base.frag
//
//  Glitchify reference, first pass (REA render 0x18005E570):
//    Show noise engine → the 0..255 noise map in grey, opaque, nothing else.
//    Otherwise the Transform Glitch world: the layer through the inverse
//    matrix (scale about the centre, Position, wiggles), only inside the
//    destination crop rectangle, composited with the transform mode/opacity
//    on transparent; then the Transition replaces alpha with
//      clamp(255 + 255·(v − c·(255 + S)) / S, 0, 255)
//    v = the chosen channel byte (Noise: the noise map), S = Softness·2.55.
// =============================================================================
#include "glitchify_reference_common.glsl"
#include "glitchify_reference_xfer.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

float channel_value(vec4 b, vec2 px) {
    const int ch = int(u.transition.x + 0.5);
    const float r = b.r, g = b.g, bl = b.b;
    const float mx = max(r, max(g, bl)), mn = min(r, min(g, bl));
    if (ch == 0) return r;
    if (ch == 1) return g;
    if (ch == 2) return bl;
    if (ch == 3) return b.a;
    if (ch == 4) return floor((299.0 * r + 587.0 * g + 114.0 * bl) / 1000.0);
    if (ch == 5) return floor((mx + mn) * 0.5);
    if (ch == 6) {
        if (mx <= mn) return 0.0;
        float h;
        if (mx == r) h = (g - bl) / (mx - mn);
        else if (mx == g) h = 2.0 + (bl - r) / (mx - mn);
        else h = 4.0 + (r - g) / (mx - mn);
        h = h < 0.0 ? h + 6.0 : h;
        return floor(h / 6.0 * 255.0);
    }
    if (ch == 7) return mx <= 0.0 ? 0.0 : floor((mx - mn) * 255.0 / mx);
    return floor(gx_noise_map(floor(px.x), floor(px.y), u.layer.w, 256.0));
}

void main() {
    const vec2 px = u.region.xy + v_uv * u.region.zw;
    if (u.stage.x > 0.5) {
        const float v = floor(gx_noise_map(floor(px.x), floor(px.y), u.layer.w, 256.0));
        o_color = gx_from_bytes(vec4(v, v, v, 255.0));
        return;
    }
    vec4 c = vec4(0.0);
    if (px.x >= u.crop.x && px.x < u.crop.z && px.y >= u.crop.y && px.y < u.crop.w) {
        const vec2 from = vec2(dot(vec3(px, 1.0), u.inverse0.xyz), dot(vec3(px, 1.0), u.inverse1.xyz));
        c = texture(u_src, (from - u.region.xy) / u.region.zw);
    }
    c = gx_transfer(int(u.transform.x + 0.5), vec4(0.0), c, u.transform.y);
    if (u.transform.z > 0.0) {
        const vec4 b = gx_to_bytes(c);
        const float s = max(u.transform.w, 1e-3);
        const float alpha = clamp(trunc((channel_value(b, px) - (s + 255.0) * u.transform.z) * 255.0 / s + 255.0), 0.0, 255.0);
        c = gx_from_bytes(vec4(b.rgb, alpha));
    }
    o_color = c;
}
