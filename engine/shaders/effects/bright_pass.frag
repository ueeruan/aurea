#version 450
// =============================================================================
//  Aurea / shaders / effects / bright_pass.frag
//
//  Primeira etapa do Glow: separa o que BRILHA, já reduzindo por 2.
//
//  Limiar com joelho suave na luminância: um corte seco faria o brilho
//  "acender" de repente quando um pixel cruza o limiar, e num vídeo isso
//  pisca. Com o joelho, a contribuição cresce continuamente.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;    // uv de entrada = v_uv * xy + zw
    vec4 texel;    // xy = texel da entrada em uv
    vec4 knee;     // x = limiar (linear), y = largura do joelho, z = 1/(4*joelho)
} p;

vec4 fetch(vec2 uv) { return texture(u_tex0, uv); }

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 d = p.texel.xy;
    vec4 c = 0.25 * (fetch(uv + vec2(-d.x, -d.y)) + fetch(uv + vec2(d.x, -d.y))
                   + fetch(uv + vec2(-d.x, d.y)) + fetch(uv + vec2(d.x, d.y)));

    // Trabalha na cor pré-multiplicada: o brilho de uma borda
    // semitransparente é proporcional à cobertura dela, que é o certo.
    float l = luminance709(c.rgb);
    float t = p.knee.x;
    float k = p.knee.y;
    float soft = clamp(l - t + k, 0.0, 2.0 * k);
    soft = soft * soft * p.knee.z;
    float contrib = max(soft, l - t) / max(l, 1e-5);
    o_color = c * clamp(contrib, 0.0, 1.0);
}
