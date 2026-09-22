#version 450
// =============================================================================
//  Aurea / shaders / composite / blend.frag
//
//  Modos de mistura que o blend de hardware não faz (Multiply, Screen, Overlay,
//  Hue...). O destino não pode ser lido pelo próprio alvo, então o renderer
//  faz ping-pong: o passe escreve um alvo NOVO — primeiro a cópia do fundo
//  acumulado, depois este quad — e este shader lê o fundo como textura
//  (`u_dst`, mesmo tamanho do alvo: texelFetch no pixel exato) e devolve a
//  cor FINAL (sem blend de hardware).
//
//  Espaço: o de trabalho do renderer — linear, alfa PRÉ-MULTIPLICADO. A
//  fórmula de mistura B(Cb, Cs) usa as cores RETAS (despré-multiplicadas) e a
//  composição é a do W3C Compositing and Blending:
//
//      co = cs·(1 − ab) + as·ab·B(Cb, Cs) + cb·(1 − as)       (pré-multiplicado)
//      ao = as + ab·(1 − as)
//
//  Onde a camada é transparente (as = 0) sobra o fundo; sobre fundo
//  transparente (ab = 0) sobra a camada — igual ao Normal nas bordas.
//
//  Modo (params.y) = valor de `BlendMode` no C++. 100 = mistura de camada de
//  ajuste: mix(fundo, efeitos-sobre-o-fundo, opacidade).
//
//  As fórmulas que supõem cor em [0,1] (Screen, Overlay, Dodge, Burn, luz...)
//  recebem as cores presas em [0,1]; as que valem para HDR (Add, Multiply,
//  Darken, Lighten, Difference, Subtract) usam o valor linear inteiro.
//  Luminância dos não separáveis: Rec.709 linear (o espaço de trabalho), não
//  os pesos 0.3/0.59/0.11 do W3C (que são para sRGB com gama).
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = opacidade, y = modo
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_dst;

const int kAdd = 1, kSubtract = 2, kMultiply = 3, kScreen = 4, kOverlay = 5, kDarken = 6, kLighten = 7,
          kColorDodge = 8, kColorBurn = 9, kHardLight = 10, kSoftLight = 11, kDifference = 12,
          kExclusion = 13, kHue = 14, kSaturation = 15, kColor = 16, kLuminosity = 17, kAdjustMix = 100;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

float dodge(float b, float s) {
    if (b <= 0.0) return 0.0;
    if (s >= 1.0) return 1.0;
    return min(1.0, b / (1.0 - s));
}
float burn(float b, float s) {
    if (b >= 1.0) return 1.0;
    if (s <= 0.0) return 0.0;
    return 1.0 - min(1.0, (1.0 - b) / s);
}
vec3 hard_light(vec3 b, vec3 s) {
    const vec3 mul = b * (2.0 * s);
    const vec3 s2 = 2.0 * s - 1.0;
    const vec3 scr = b + s2 - b * s2;
    return mix(scr, mul, lessThanEqual(s, vec3(0.5)));
}
vec3 soft_light(vec3 b, vec3 s) {
    const vec3 d = mix(sqrt(b), ((16.0 * b - 12.0) * b + 4.0) * b, lessThanEqual(b, vec3(0.25)));
    const vec3 lo = b - (1.0 - 2.0 * s) * b * (1.0 - b);
    const vec3 hi = b + (2.0 * s - 1.0) * (d - b);
    return mix(hi, lo, lessThanEqual(s, vec3(0.5)));
}

float lum(vec3 c) { return dot(c, kLuma); }
vec3 clip_color(vec3 c) {
    const float l = lum(c);
    const float n = min(c.r, min(c.g, c.b));
    const float x = max(c.r, max(c.g, c.b));
    if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-6);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-6);
    return c;
}
vec3 set_lum(vec3 c, float l) { return clip_color(c + (l - lum(c))); }
float sat(vec3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
vec3 set_sat(vec3 c, float s) {
    // W3C: o maior canal vira s, o menor 0, o do meio proporcional.
    const float mx = max(c.r, max(c.g, c.b));
    const float mn = min(c.r, min(c.g, c.b));
    const float range = mx - mn;
    if (range <= 1e-6) return vec3(0.0);
    return (c - mn) * (s / range);
}

vec3 blend(int mode, vec3 b, vec3 s) {
    switch (mode) {
        case kAdd:        return b + s;   // HDR: sem teto (a soma de luz)
        case kSubtract:   return max(b - s, vec3(0.0));
        case kMultiply:   return b * s;
        case kDarken:     return min(b, s);
        case kLighten:    return max(b, s);
        case kDifference: return abs(b - s);
        default: break;
    }
    b = clamp(b, 0.0, 1.0);
    s = clamp(s, 0.0, 1.0);
    switch (mode) {
        case kScreen:     return b + s - b * s;
        case kOverlay:    return hard_light(s, b);
        case kColorDodge: return vec3(dodge(b.r, s.r), dodge(b.g, s.g), dodge(b.b, s.b));
        case kColorBurn:  return vec3(burn(b.r, s.r), burn(b.g, s.g), burn(b.b, s.b));
        case kHardLight:  return hard_light(b, s);
        case kSoftLight:  return soft_light(b, s);
        case kExclusion:  return b + s - 2.0 * b * s;
        case kHue:        return set_lum(set_sat(s, sat(b)), lum(b));
        case kSaturation: return set_lum(set_sat(b, sat(s)), lum(b));
        case kColor:      return set_lum(s, lum(b));
        case kLuminosity: return set_lum(b, lum(s));
        default:          return s;   // modo desconhecido: Normal
    }
}

void main() {
    const vec4 dst = texelFetch(u_dst, ivec2(gl_FragCoord.xy), 0);
    const int mode = int(pc.params.y + 0.5);
    if (mode == kAdjustMix) {
        // `u_src` é o próprio fundo com os efeitos da camada de ajuste: a
        // opacidade é o peso entre o fundo e ele (não um "over").
        o_color = mix(dst, texture(u_src, v_uv), pc.params.x);
        return;
    }
    const vec4 src = texture(u_src, v_uv) * pc.params.x;
    const float as = src.a, ab = dst.a;
    const vec3 cs = as > 1e-6 ? src.rgb / as : vec3(0.0);
    const vec3 cb = ab > 1e-6 ? dst.rgb / ab : vec3(0.0);
    const vec3 B = blend(mode, cb, cs);
    o_color = vec4(src.rgb * (1.0 - ab) + as * ab * B + dst.rgb * (1.0 - as),
                   as + ab * (1.0 - as));
}
