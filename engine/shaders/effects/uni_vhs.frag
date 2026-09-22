#version 450
// =============================================================================
//  Aurea / shaders / effects / uni_vhs.frag  (Fase 7.3 §48)
//
//  A outra família de VHS: o estilizado, não o realista. Aqui não se imita a
//  fita — usa-se o vocabulário dela (separação RGB, ondulação, brilho sujo,
//  vinheta) para fazer uma imagem que PARECE um clipe dos anos 80.
//
//  A diferença em relação ao `vhs.frag` é de intenção, e ela está nos padrões:
//  aqui o croma é o protagonista (separação grande, saturação alta, brilho),
//  enquanto lá o que manda é o defeito (tracking, dropout, ruído).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = separação RGB (px), y = ondulação (px), z = brilho sujo, w = vinheta
    vec4 p1;   // x = varredura, y = ruído, z = saturação, w = mistura
    vec4 p2;   // x = semente, y = frequência da ondulação, zw livres
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = unpremultiply(texture(u_tex0, inUv));

    // Ondulação: a imagem respira na horizontal, mais forte no alto e embaixo
    // (onde a fita passa mais devagar pelo cabeçote).
    const float warp = sin(inUv.y * p.p2.y * AUREA_TAU + p.p3.x * 0.08 + p.p2.x) * p.p0.y
                     + sin(inUv.y * 1.7 + p.p3.x * 0.031) * p.p0.y * 0.4;
    const float edge = 0.35 + 0.65 * abs(inUv.y - 0.5) * 2.0;
    const vec2 base = inUv + vec2(warp * edge * uvPerLayer.x, 0.0);

    // Separação RGB com sangramento por linha: partes da imagem se separam
    // mais que outras — é o que dá o aspecto "quase sincronizado".
    const float row = floor(inUv.y * 48.0);
    const float jitter = aurea_hash(uvec2(uint(int(row)), uint(p.p2.x))) * 2.0 - 1.0;
    const float sep = p.p0.x * (1.0 + jitter * 0.6) * uvPerLayer.x;

    vec4 c;
    c.r = unpremultiply(texture(u_tex0, base + vec2(sep, 0.0))).r;
    c.g = unpremultiply(texture(u_tex0, base)).g;
    c.b = unpremultiply(texture(u_tex0, base - vec2(sep, 0.0))).b;
    c.a = unpremultiply(texture(u_tex0, base)).a;

    // Varredura fina.
    if (p.p1.x > 0.0) {
        const float yPx = inUv.y / max(p.texel.y, 1e-6) / max(p.texel.w, 1e-6);
        c.rgb *= 1.0 - p.p1.x * 0.3 * (0.5 + 0.5 * sin(yPx * AUREA_TAU / 2.33));
    }

    // Ruído fino, mais visível no escuro.
    if (p.p1.y > 0.0) {
        const vec2 px = inUv / uvPerLayer;
        const float n = aurea_hash3(px, uint(p.p2.x) ^ uint(int(p.p3.x))) * 2.0 - 1.0;
        const float dark = 1.0 - clamp(aurea_luma(c.rgb), 0.0, 1.0) * 0.5;
        c.rgb += vec3(n) * p.p1.y * 0.18 * dark;
    }

    // Brilho sujo: as luzes ganham um halo quente que "vaza" (bloom barato).
    if (p.p0.z > 0.0) {
        vec3 glow = vec3(0.0);
        for (int i = 0; i < 5; ++i) {
            const float a = float(i) / 5.0 * AUREA_TAU;
            const vec2 o = vec2(cos(a), sin(a)) * 2.5 * uvPerLayer;
            glow += max(unpremultiply(texture(u_tex0, base + o)).rgb - vec3(0.45), vec3(0.0));
        }
        c.rgb += glow * p.p0.z * 0.5;
    }

    // Saturação e vinheta, no valor codificado.
    vec3 enc = aurea_linear_to_srgb(max(c.rgb, vec3(0.0)));
    const float y = dot(enc, vec3(0.2126, 0.7152, 0.0722));
    enc = mix(vec3(y), enc, max(p.p1.z, 0.0));
    if (p.p0.w > 0.0) {
        const float d = length(v_uv - vec2(0.5)) * 1.4142;
        enc *= 1.0 - p.p0.w * smoothstep(0.45, 1.0, d);
    }
    c.rgb = aurea_srgb_to_linear(clamp(enc, 0.0, 1.0));

    const float k = clamp(p.p1.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(src.rgb, c.rgb, k), vec3(0.0)), mix(src.a, c.a, k)));
}
