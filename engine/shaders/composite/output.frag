#version 450
// =============================================================================
//  Aurea / shaders / composite / output.frag
//
//  Composição (linear, pré-multiplicada) → display.
//
//  É o ÚNICO lugar onde a cor sai do espaço de trabalho. O fundo da área de
//  preview é composto AQUI, em linear, e só depois codificado — misturar no
//  espaço codificado (blend de hardware sobre o swapchain UNORM) escurece as
//  bordas semitransparentes.
//
//  Dither de 1/2 LSB antes de quantizar para 8 bits: sem ele, um degradê
//  suave de céu vira faixas no preview, e o usuário acha que o vídeo está
//  estragado. Desligado nos testes visuais (resultado determinístico).
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;    // x=dither (0/1)
} pc;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 background;   // cor linear atrás de áreas transparentes
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

float interleaved_gradient_noise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec4 c = texture(u_tex0, v_uv);
    vec3 lin = c.rgb + p.background.rgb * (1.0 - c.a);
    vec3 enc = linear_to_srgb(lin);
    if (pc.params.x > 0.5) {
        enc += (interleaved_gradient_noise(gl_FragCoord.xy) - 0.5) / 255.0;
    }
    o_color = vec4(clamp(enc, 0.0, 1.0), 1.0);
}
