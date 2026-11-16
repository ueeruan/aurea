// =============================================================================
//  Aurea / shaders / effects / glitchify_reference_xfer.glsl
//
//  The transfer modes of the Glitchify reference (aurea.glitch.glitchify_
//  reference). Each glitch module composites its layer onto the running
//  result with one of these 39 modes (the After Effects transfer-mode list
//  without separators; index = the Aurea enum value). AE is the behaviour
//  specification only; the formulas below are Aurea's own.
//
//  In/out: linear premultiplied RGBA. The blend function B(b, s) works on
//  straight colours; composition is W3C:
//      co = cs·(1 − ab) + as·ab·B(cb, cs) + cb·(1 − as),  ao = as + ab·(1 − as)
//  `opacity` scales the source alpha first.
// =============================================================================
#ifndef AUREA_GLITCHIFY_REFERENCE_XFER_GLSL
#define AUREA_GLITCHIFY_REFERENCE_XFER_GLSL

const int GX_NORMAL = 0, GX_COPY = 1, GX_BEHIND = 2, GX_IN_FRONT = 3,
          GX_DARKEN = 4, GX_MULTIPLY = 5, GX_COLOR_BURN = 6, GX_CLASSIC_BURN = 7, GX_LINEAR_BURN = 8,
          GX_DARKER = 9, GX_ADD = 10, GX_LIGHTEN = 11, GX_SCREEN = 12, GX_COLOR_DODGE = 13,
          GX_CLASSIC_DODGE = 14, GX_LINEAR_DODGE = 15, GX_LIGHTER = 16, GX_OVERLAY = 17,
          GX_SOFT_LIGHT = 18, GX_HARD_LIGHT = 19, GX_LINEAR_LIGHT = 20, GX_VIVID_LIGHT = 21,
          GX_PIN_LIGHT = 22, GX_HARD_MIX = 23, GX_DIFFERENCE = 24, GX_CLASSIC_DIFFERENCE = 25,
          GX_EXCLUSION = 26, GX_SUBTRACT = 27, GX_DIVIDE = 28, GX_HUE = 29, GX_SATURATION = 30,
          GX_COLOR = 31, GX_LUMINOSITY = 32, GX_STENCIL_ALPHA = 33, GX_STENCIL_LUMA = 34,
          GX_SILHOUETTE_ALPHA = 35, GX_SILHOUETTE_LUMA = 36, GX_ADDITIVE_PREMUL = 37, GX_ALPHA_ADD = 38;

float gx_lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 gx_clip(vec3 c) {
    float l = gx_lum(c), n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));
    if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-6);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-6);
    return c;
}
vec3 gx_set_lum(vec3 c, float l) { return gx_clip(c + (l - gx_lum(c))); }
float gx_sat(vec3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
vec3 gx_set_sat(vec3 c, float s) {
    float n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));
    return x > n ? (c - n) * s / (x - n) : vec3(0.0);
}
float gx_soft(float b, float s) {
    if (s <= 0.5) return b - (1.0 - 2.0 * s) * b * (1.0 - b);
    float d = b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b);
    return b + (2.0 * s - 1.0) * (d - b);
}
float gx_burn(float b, float s) { return b >= 1.0 ? 1.0 : (s <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - b) / s)); }
float gx_dodge(float b, float s) { return b <= 0.0 ? 0.0 : (s >= 1.0 ? 1.0 : min(1.0, b / (1.0 - s))); }
float gx_vivid(float b, float s) { return s <= 0.5 ? gx_burn(b, 2.0 * s) : gx_dodge(b, 2.0 * s - 1.0); }

vec3 gx_blend(int mode, vec3 b, vec3 s) {
    vec3 cb = clamp(b, 0.0, 1.0), cs = clamp(s, 0.0, 1.0);
    if (mode == GX_DARKEN) return min(b, s);
    if (mode == GX_MULTIPLY) return b * s;
    if (mode == GX_COLOR_BURN) return vec3(gx_burn(cb.r, cs.r), gx_burn(cb.g, cs.g), gx_burn(cb.b, cs.b));
    if (mode == GX_CLASSIC_BURN) return clamp(1.0 - (1.0 - cb) / max(cs, vec3(1e-6)), 0.0, 1.0) * step(vec3(1e-6), cs);
    if (mode == GX_LINEAR_BURN) return max(cb + cs - 1.0, 0.0);
    if (mode == GX_DARKER) return gx_lum(s) < gx_lum(b) ? s : b;
    if (mode == GX_ADD || mode == GX_LINEAR_DODGE) return b + s;
    if (mode == GX_LIGHTEN) return max(b, s);
    if (mode == GX_SCREEN) return cb + cs - cb * cs;
    if (mode == GX_COLOR_DODGE) return vec3(gx_dodge(cb.r, cs.r), gx_dodge(cb.g, cs.g), gx_dodge(cb.b, cs.b));
    if (mode == GX_CLASSIC_DODGE) return mix(clamp(cb / max(1.0 - cs, vec3(1e-6)), 0.0, 1.0), vec3(1.0), step(vec3(1.0), cs));
    if (mode == GX_LIGHTER) return gx_lum(s) > gx_lum(b) ? s : b;
    if (mode == GX_OVERLAY) return mix(2.0 * cb * cs, 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs), step(vec3(0.5), cb));
    if (mode == GX_SOFT_LIGHT) return vec3(gx_soft(cb.r, cs.r), gx_soft(cb.g, cs.g), gx_soft(cb.b, cs.b));
    if (mode == GX_HARD_LIGHT) return mix(2.0 * cb * cs, 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs), step(vec3(0.5), cs));
    if (mode == GX_LINEAR_LIGHT) return clamp(cb + 2.0 * cs - 1.0, 0.0, 1.0);
    if (mode == GX_VIVID_LIGHT) return vec3(gx_vivid(cb.r, cs.r), gx_vivid(cb.g, cs.g), gx_vivid(cb.b, cs.b));
    if (mode == GX_PIN_LIGHT) return mix(min(cb, 2.0 * cs), max(cb, 2.0 * cs - 1.0), step(vec3(0.5), cs));
    if (mode == GX_HARD_MIX) return step(vec3(1.0), cb + cs);
    if (mode == GX_DIFFERENCE) return abs(b - s);
    if (mode == GX_CLASSIC_DIFFERENCE) return abs(cb - cs);
    if (mode == GX_EXCLUSION) return cb + cs - 2.0 * cb * cs;
    if (mode == GX_SUBTRACT) return max(b - s, 0.0);
    if (mode == GX_DIVIDE) return mix(cb / max(cs, vec3(1e-6)), vec3(1.0), step(cs, vec3(1e-6)) * step(vec3(1e-6), cb));
    if (mode == GX_HUE) return gx_set_lum(gx_set_sat(cs, gx_sat(cb)), gx_lum(cb));
    if (mode == GX_SATURATION) return gx_set_lum(gx_set_sat(cb, gx_sat(cs)), gx_lum(cb));
    if (mode == GX_COLOR) return gx_set_lum(cs, gx_lum(cb));
    if (mode == GX_LUMINOSITY) return gx_set_lum(cb, gx_lum(cs));
    return s;
}

/// Composite premultiplied `src` (scaled by `opacity`) onto premultiplied `dst`.
vec4 gx_transfer(int mode, vec4 dst, vec4 src, float opacity) {
    src *= clamp(opacity, 0.0, 1.0);
    float as = clamp(src.a, 0.0, 1.0), ab = clamp(dst.a, 0.0, 1.0);
    if (mode == GX_COPY) return src;
    if (mode == GX_BEHIND) return dst + src * (1.0 - ab);
    if (mode == GX_STENCIL_ALPHA) return dst * as;
    if (mode == GX_STENCIL_LUMA) return dst * clamp(gx_lum(src.rgb), 0.0, 1.0);
    if (mode == GX_SILHOUETTE_ALPHA) return dst * (1.0 - as);
    if (mode == GX_SILHOUETTE_LUMA) return dst * (1.0 - clamp(gx_lum(src.rgb), 0.0, 1.0));
    if (mode == GX_ALPHA_ADD) return vec4(src.rgb + dst.rgb, min(1.0, as + ab));
    if (mode == GX_ADDITIVE_PREMUL) return vec4(src.rgb + dst.rgb * (1.0 - as), as + ab * (1.0 - as));
    if (mode == GX_NORMAL || mode == GX_IN_FRONT) return src + dst * (1.0 - as);
    vec3 s = as > 1e-6 ? src.rgb / as : vec3(0.0);
    vec3 b = ab > 1e-6 ? dst.rgb / ab : vec3(0.0);
    vec3 co = src.rgb * (1.0 - ab) + as * ab * gx_blend(mode, b, s) + dst.rgb * (1.0 - as);
    return vec4(co, as + ab * (1.0 - as));
}

#endif
