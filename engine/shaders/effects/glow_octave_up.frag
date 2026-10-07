#version 450
// =============================================================================
//  Aurea / shaders / effects / glow_octave_up.frag
//
//  Brilho em oitavas: sobe uma oitava. Amplia o que já foi somado das
//  oitavas menores com uma tenda 3x3 (bilinear puro deixa degraus quadrados
//  quando as oitavas se juntam) e soma à luz desta oitava, com o peso que o
//  raio e o decaimento deram a ela.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // oitavas menores, já somadas
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a luz desta oitava

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;    // xy = texel da oitava MENOR em uv
    vec4 p0;       // x = peso do que vem de baixo, y = peso da luz desta oitava
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

vec4 tap(vec2 uv, float x, float y) { return texture(u_tex0, uv + vec2(x, y) * p.texel.xy); }

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec4 s = tap(uv, -1.0, 1.0) + tap(uv, 0.0, 1.0) * 2.0 + tap(uv, 1.0, 1.0)
           + tap(uv, -1.0, 0.0) * 2.0 + tap(uv, 0.0, 0.0) * 4.0 + tap(uv, 1.0, 0.0) * 2.0
           + tap(uv, -1.0, -1.0) + tap(uv, 0.0, -1.0) * 2.0 + tap(uv, 1.0, -1.0);
    o_color = texture(u_tex1, v_uv) * p.p0.y + s * (0.0625 * p.p0.x);
}
