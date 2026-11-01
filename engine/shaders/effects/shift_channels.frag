#version 450
// =============================================================================
//  Aurea / shaders / effects / shift_channels.frag  (0.4.5, pedido de usuário)
//
//  Trocar canais (o "Shift Channels" do After Effects): cada canal de saída
//  (alfa, vermelho, verde, azul) é TIRADO de uma fonte: alfa, vermelho, verde,
//  azul, luminância, matiz, claridade, saturação, cheio (1) ou desligado (0).
//
//  As fontes são lidas na cor como ela aparece (sRGB, alfa reto) — é assim que
//  "luminância" e "matiz" fazem sentido; o resultado volta ao espaço de
//  trabalho (linear, pré-multiplicado).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x fonte do alfa, y do vermelho, z do verde, w do azul
    vec4 p1;      // x mistura
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

// 0 alfa, 1 vermelho, 2 verde, 3 azul, 4 luminância, 5 matiz, 6 claridade,
// 7 saturação, 8 cheio, 9 desligado.
float take(int src, vec3 s, float a, vec3 hsl) {
    if (src == 0) return a;
    if (src == 1) return s.r;
    if (src == 2) return s.g;
    if (src == 3) return s.b;
    if (src == 4) return dot(s, vec3(0.299, 0.587, 0.114));
    if (src == 5) return hsl.x;
    if (src == 6) return hsl.z;
    if (src == 7) return hsl.y;
    if (src == 8) return 1.0;
    return 0.0;
}

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec4 original = texture(u_tex0, uv);
    vec4 c = unpremultiply(original);
    vec3 s = clamp(aurea_linear_to_srgb(c.rgb), 0.0, 1.0);
    // HSL (matiz em voltas, saturação e claridade do HSL, como no AE).
    float mx = max(s.r, max(s.g, s.b)), mn = min(s.r, min(s.g, s.b)), d = mx - mn;
    float l = 0.5 * (mx + mn);
    float h = 0.0;
    if (d > 1e-6) {
        if (mx == s.r)      h = fract((s.g - s.b) / d / 6.0);
        else if (mx == s.g) h = ((s.b - s.r) / d + 2.0) / 6.0;
        else                h = ((s.r - s.g) / d + 4.0) / 6.0;
    }
    float sat = d > 1e-6 ? d / max(1e-6, 1.0 - abs(2.0 * l - 1.0)) : 0.0;
    vec3 hsl = vec3(h, clamp(sat, 0.0, 1.0), l);
    float a = clamp(c.a, 0.0, 1.0);
    ivec4 src = ivec4(p.p0 + 0.5);
    vec4 outStraight = vec4(take(src.y, s, a, hsl), take(src.z, s, a, hsl), take(src.w, s, a, hsl),
                            take(src.x, s, a, hsl));
    vec4 shifted = premultiply(vec4(aurea_srgb_to_linear(clamp(outStraight.rgb, 0.0, 1.0)), clamp(outStraight.a, 0.0, 1.0)));
    o_color = mix(original, shifted, clamp(p.p1.x, 0.0, 1.0));
}
