#version 450
// =============================================================================
//  Aurea / shaders / effects / hue_saturation.frag
//
//  Matiz e saturação no modelo HSL, sobre a cor CODIFICADA (sRGB, sem alfa):
//  é o espaço em que "girar o matiz 180°" leva o vermelho ao ciano que o olho
//  espera. A luminosidade clareia para o branco (+) ou escurece para o preto
//  (-) sem lavar a saturação; "Colorir" troca matiz e saturação por um tom
//  único e mantém a luz de cada pixel.
//
//  Volta para linear pré-multiplicado no fim; HDR acima de 1 é preso a 1 (HSL
//  é de faixa fechada — o mesmo que os programas de cor fazem).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = matiz (voltas), y = saturação -1..1, z = luminosidade -1..1, w = 1 colorir
    vec4 p1;   // x = matiz ao colorir (voltas), y = saturação ao colorir 0..1, z = mistura
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

vec3 rgb_to_hsl(vec3 c) {
    const float mx = max(c.r, max(c.g, c.b));
    const float mn = min(c.r, min(c.g, c.b));
    const float l = (mx + mn) * 0.5;
    const float d = mx - mn;
    if (d < 1e-6) return vec3(0.0, 0.0, l);
    const float s = l > 0.5 ? d / max(2.0 - mx - mn, 1e-6) : d / max(mx + mn, 1e-6);
    float h;
    if (mx == c.r)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
    else                h = (c.r - c.g) / d + 4.0;
    return vec3(h / 6.0, s, l);
}

vec3 hsl_to_rgb(vec3 hsl) {
    const vec3 k = clamp(abs(mod(hsl.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    const float chroma = (1.0 - abs(2.0 * hsl.z - 1.0)) * hsl.y;
    return hsl.z + chroma * (k - 0.5);
}

void main() {
    const vec4 source = texture(u_tex0, v_uv);
    if (source.a <= 1e-5) { o_color = source; return; }
    const vec3 straight = clamp(source.rgb / source.a, 0.0, 1.0);
    vec3 hsl = rgb_to_hsl(aurea_linear_to_srgb(straight));
    if (p.p0.w > 0.5) {
        hsl.x = fract(p.p1.x);
        hsl.y = clamp(p.p1.y, 0.0, 1.0);
    } else {
        hsl.x = fract(hsl.x + p.p0.x);
        hsl.y = clamp(hsl.y * (1.0 + clamp(p.p0.y, -1.0, 1.0)), 0.0, 1.0);
    }
    vec3 enc = clamp(hsl_to_rgb(hsl), 0.0, 1.0);
    const float light = clamp(p.p0.z, -1.0, 1.0);
    enc = light >= 0.0 ? mix(enc, vec3(1.0), light) : enc * (1.0 + light);
    const vec4 adjusted = vec4(aurea_srgb_to_linear(enc) * source.a, source.a);
    o_color = mix(source, adjusted, clamp(p.p1.z, 0.0, 1.0));
}
