#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_threshold_rgb.frag
//
//  Limiar RGB (o comportamento do CC Threshold RGB): cada canal vira 0 ou 1
//  pelo seu próprio limite, julgado no valor sRGB que o olho vê (cor reta).
//  Cada canal pode ser invertido; o alfa fica como estava.
//
//  p0.rgb = limites (0..1), p0.a = mistura com o original; p1.rgb = inverter.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec4 src = cc_at(cc_px());
    const vec4 s = unpremultiply(src);
    const vec3 e = linear_to_srgb(s.rgb);
    vec3 b = step(p.p0.rgb, e);
    b = mix(b, 1.0 - b, step(0.5, p.p1.rgb));
    o_color = mix(vec4(b * s.a, s.a), src, clamp(p.p0.a, 0.0, 1.0));
}
