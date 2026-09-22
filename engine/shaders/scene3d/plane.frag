#version 450
// =============================================================================
//  Aurea / shaders / scene3d / plane.frag
//
//  Camada 2D no espaço 3D desenhada DENTRO da cena: a imagem da camada (com
//  efeitos, pré-multiplicada, linear) num plano com teste e escrita de
//  profundidade — ela passa por trás e pela frente dos modelos e das outras
//  camadas 3D de verdade, e se cruza com elas. Transparente não escreve
//  profundidade (não tapa o que está atrás).
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = opacidade
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

void main() {
    const vec4 c = texture(u_tex0, v_uv) * pc.params.x;
    if (c.a < 0.02) discard;
    o_color = c;
}
