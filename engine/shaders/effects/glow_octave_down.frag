#version 450
// =============================================================================
//  Aurea / shaders / effects / glow_octave_down.frag
//
//  Brilho em oitavas: desce uma oitava (redução por 2) com o filtro de 13
//  amostras — um quadrado central e quatro nos cantos, pesos 1/8 a 1/32. Uma
//  caixa simples "pulsa" quando um detalhe claro cruza a fronteira de um
//  texel, e no brilho isso aparece como piscar.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;    // xy = texel da oitava de ORIGEM (a maior) em uv
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

vec4 tap(vec2 uv, float x, float y) { return texture(u_tex0, uv + vec2(x, y) * p.texel.xy); }

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec4 corners = tap(uv, -2.0, 2.0) + tap(uv, 2.0, 2.0) + tap(uv, -2.0, -2.0) + tap(uv, 2.0, -2.0);
    vec4 edges = tap(uv, 0.0, 2.0) + tap(uv, -2.0, 0.0) + tap(uv, 2.0, 0.0) + tap(uv, 0.0, -2.0);
    vec4 inner = tap(uv, -1.0, 1.0) + tap(uv, 1.0, 1.0) + tap(uv, -1.0, -1.0) + tap(uv, 1.0, -1.0);
    o_color = tap(uv, 0.0, 0.0) * 0.125 + corners * 0.03125 + edges * 0.0625 + inner * 0.125;
}
