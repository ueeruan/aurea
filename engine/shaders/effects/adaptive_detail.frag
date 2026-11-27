#version 450
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 geometry; // source size and its reciprocal
    vec4 detail;   // strength, animation profile, enlargement
} p;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

vec3 restore_detail(vec4 base, vec4 center, vec4 l, vec4 r, vec4 t, vec4 b, vec2 phase) {
    vec4 alpha = vec4(l.a, r.a, t.a, b.a);
    if (max(abs(center.a - base.a), max(max(abs(alpha.x - base.a), abs(alpha.y - base.a)),
                                       max(abs(alpha.z - base.a), abs(alpha.w - base.a)))) > 0.001) return base.rgb;

    // Measure edges in approximate perceptual luma. Suppress small variations
    // (sensor/compression noise) and taper at already strong transitions.
    float invAlpha = 1.0 / base.a;
    vec4 y = sqrt(max(vec4(dot(l.rgb, kLuma), dot(r.rgb, kLuma),
                           dot(t.rgb, kLuma), dot(b.rgb, kLuma)) * invAlpha, 0.0));
    float yc = sqrt(max(dot(center.rgb, kLuma) * invAlpha, 0.0));
    float yb = sqrt(max(dot(base.rgb, kLuma) * invAlpha, 0.0));
    float lo = min(yc, min(min(y.x, y.y), min(y.z, y.w)));
    float hi = max(yc, max(max(y.x, y.y), max(y.z, y.w)));
    float span = hi - lo;
    float weight = smoothstep(0.009, 0.035, span) * (1.0 - smoothstep(0.22, 0.60, span));
    vec2 gradient = y.yw - y.xz;
    float alignment = clamp(length(gradient) / max(dot(abs(y - yc), vec4(1.0)), 0.0001), 0.0, 1.0);
    float mean = 0.5 * (mix(y.x, y.y, phase.x) + mix(y.z, y.w, phase.y));
    float gain = mix(1.15, 1.55, p.detail.y) * mix(0.45, 1.0, alignment);
    float limit = mix(0.033, 0.045, p.detail.y);
    float residual = clamp((yc - mean) * gain * weight, -limit, limit) * p.detail.x;
    float adjusted = clamp(yb + residual, min(yb, lo), max(yb, hi));
    return base.rgb * (1.0 + (adjusted * adjusted - yb * yb) / max(yb * yb, 0.012));
}

void reconstruct(vec4 center) {
    // Cubic Hermite reconstruction. Fold the two inner taps into one
    // hardware bilinear sample per axis. Keep the cross and renormalize its
    // weights, omitting the four small corner terms for the mobile path.
    vec2 pos = v_uv * p.geometry.xy - 0.5;
    vec2 f = fract(pos), origin = floor(pos) + 0.5;
    vec2 g = 1.0 - f;
    float tension = mix(0.45, 0.60, p.detail.y);
    vec2 before = -tension * f * g * g;
    vec2 after = -tension * g * f * f;
    vec2 w2 = f * f * (3.0 - 2.0 * f) + tension * f * g * g;
    vec2 core = 1.0 - before - after;
    vec2 mid = origin + w2 / core;
    vec2 stepUV = p.geometry.zw;

    vec4 c = texture(u_src, mid * stepUV);
    vec4 l = texture(u_src, vec2(origin.x - 1.0, mid.y) * stepUV);
    vec4 r = texture(u_src, vec2(origin.x + 2.0, mid.y) * stepUV);
    vec4 t = texture(u_src, vec2(mid.x, origin.y - 1.0) * stepUV);
    vec4 b = texture(u_src, vec2(mid.x, origin.y + 2.0) * stepUV);
    // Keep bilinear coverage at matte edges. Constant translucent regions can
    // still be reconstructed and sharpened in premultiplied colour.
    vec4 alpha = vec4(l.a, r.a, t.a, b.a);
    if (any(greaterThan(abs(alpha - center.a), vec4(0.001))) || abs(c.a - center.a) > 0.001) return;

    float wc = core.x * core.y;
    vec4 w = vec4(before.x * core.y, after.x * core.y, before.y * core.x, after.y * core.x);
    vec3 result = (c.rgb * wc + l.rgb * w.x + r.rgb * w.y + t.rgb * w.z + b.rgb * w.w)
                  / (wc + dot(w, vec4(1.0)));
    // Constrain each channel to the sampled neighborhood to prevent halos.
    vec3 lo = min(center.rgb, min(c.rgb, min(min(l.rgb, r.rgb), min(t.rgb, b.rgb))));
    vec3 hi = max(center.rgb, max(c.rgb, max(max(l.rgb, r.rgb), max(t.rgb, b.rgb))));
    float remaining = 1.0 - p.detail.x;
    float cubicMix = 1.0 - remaining * remaining * remaining;
    vec3 base = mix(center.rgb, clamp(result, lo, hi), cubicMix);
    // Reuse the four cubic taps. Interpolate their mean at the position of
    // the folded tap so alternating output phases don't sharpen a ramp.
    vec2 phase = (mid - origin + 1.0) / 3.0;
    o_color.rgb = clamp(restore_detail(vec4(base, center.a), c, l, r, t, b, phase), lo, hi);
}

void main() {
    vec4 center = texture(u_src, v_uv);
    o_color = center;
    if (center.a <= 0.001) return;
    if (p.detail.z > 0.5) {
        reconstruct(center);
        return;
    }

    vec2 d = p.geometry.zw;
    vec4 l = texture(u_src, v_uv - vec2(d.x, 0.0));
    vec4 r = texture(u_src, v_uv + vec2(d.x, 0.0));
    vec4 t = texture(u_src, v_uv - vec2(0.0, d.y));
    vec4 b = texture(u_src, v_uv + vec2(0.0, d.y));
    vec3 lo = min(center.rgb, min(min(l.rgb, r.rgb), min(t.rgb, b.rgb)));
    vec3 hi = max(center.rgb, max(max(l.rgb, r.rgb), max(t.rgb, b.rgb)));
    o_color.rgb = clamp(restore_detail(center, center, l, r, t, b, vec2(0.5)), lo, hi);
}
