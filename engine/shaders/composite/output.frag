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
    vec4 params;    // x=dither (0/1), y=texels da composição por pixel do display,
                    // z=lado da casa do xadrez em px (0 = fundo opaco, sem xadrez)
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

// Minificação: com a composição encolhida no display (zoom < 0,75), uma única
// amostra bilinear pula texels — a imagem cintila quando algo se move (grade,
// texto fino, arestas do 3D). Quatro amostras bilineares a ±1/4 da pegada do
// pixel cobrem uma caixa de ~2×2 pegadas: média de área quase de graça.
vec4 sample_footprint(vec2 uv) {
    const float k = pc.params.y;
    if (k < 1.33) return texture(u_tex0, uv);
    const vec2 o = (0.25 * min(k, 4.0)) / vec2(textureSize(u_tex0, 0));
    return 0.25 * (texture(u_tex0, uv + vec2(-o.x, -o.y)) + texture(u_tex0, uv + vec2(o.x, -o.y))
                 + texture(u_tex0, uv + vec2(-o.x, o.y)) + texture(u_tex0, uv + vec2(o.x, o.y)));
}

void main() {
    vec4 c = sample_footprint(v_uv);
    // Composição de fundo transparente: xadrez cinza claro/médio atrás dela
    // (só na tela — o PNG/GIF exportado guarda o alfa de verdade).
    vec3 under = p.background.rgb;
    if (pc.params.z > 0.5) {
        vec2 cell = floor(gl_FragCoord.xy / pc.params.z);
        under = mod(cell.x + cell.y, 2.0) < 0.5 ? vec3(0.58) : vec3(0.36);
    }
    vec3 lin = c.rgb + under * (1.0 - c.a);
    vec3 enc = linear_to_srgb(lin);
    if (pc.params.x > 0.5) {
        enc += (interleaved_gradient_noise(gl_FragCoord.xy) - 0.5) / 255.0;
    }
    o_color = vec4(clamp(enc, 0.0, 1.0), 1.0);
}
