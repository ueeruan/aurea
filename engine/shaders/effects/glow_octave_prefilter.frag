#version 450
// =============================================================================
//  Aurea / shaders / effects / glow_octave_prefilter.frag
//
//  Brilho em oitavas (Brilho / Brilho profundo, algoritmo 1): primeira etapa.
//  Reduz por 2 com caixa 2x2 (um tap só deixaria realces finos cintilarem) e
//  separa o que passa do limiar.
//
//  O limiar é julgado no valor CODIFICADO (sRGB) do canal mais forte: 50% no
//  controle é o cinza médio que o usuário vê, e uma cor saturada acende como
//  uma clara do mesmo valor. A luz que segue é a linear (espaço de trabalho),
//  pré-multiplicada; o alfa vai junto, e é ele que deixa o brilho vazar para
//  fora da silhueta da layer.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;    // uv da entrada = v_uv * xy + zw
    vec4 texel;    // xy = texel da ENTRADA em uv
    vec4 p0;       // x = limiar (0..1, sRGB), y = meia largura do joelho
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 h = p.texel.xy * 0.5;
    vec4 c = 0.25 * (texture(u_tex0, uv + vec2(-h.x, -h.y)) + texture(u_tex0, uv + vec2(h.x, -h.y))
                   + texture(u_tex0, uv + vec2(-h.x, h.y)) + texture(u_tex0, uv + vec2(h.x, h.y)));
    float a = clamp(c.a, 0.0, 1.0);
    vec3 straight = clamp(c.rgb / max(a, 1e-4), 0.0, 1.0);
    vec3 encoded = linear_to_srgb(straight);
    float level = max(encoded.r, max(encoded.g, encoded.b));
    float lo = p.p0.x - p.p0.y;
    float hi = p.p0.x + p.p0.y + 1e-4;
    float mask = clamp((level - lo) / (hi - lo), 0.0, 1.0);
    mask = mask * mask * (3.0 - 2.0 * mask);
    o_color = vec4(straight * a, a) * mask;
}
