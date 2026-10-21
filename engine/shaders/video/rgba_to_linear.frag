#version 450
// =============================================================================
//  Aurea / shaders / video / rgba_to_linear.frag
//
//  Imagem RGBA → espaço de trabalho (linear, pré-multiplicado).
//
//  - Imagem importada: RGBA8 em sRGB, alfa reto. Enviada UMA vez; este passe
//    só roda quando a resolução de trabalho muda.
//  - Quadro de sequência de imagens (ImageSequence.hpp): RGBA8 ou RGBA16F,
//    sRGB ou linear (EXR), alfa reto, pré-multiplicado ou ignorado. Em redução
//    grande (4K no preview de 960 px), quatro amostras por pixel de saída,
//    como o vídeo (yuv_convert.glsl): sem elas a sequência cintila tocando.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 flags;   // x=a textura já é linear (0/1)  y=alfa já pré-multiplicado (0/1)
                  // z=ignorar o alfa (opaco, 0/1)  w=quatro amostras de redução (0/1)
    vec4 texel;   // xy=texel da fonte em uv  zw=pixel de saída em uv da fonte
} p;

vec4 fetch(vec2 uv) {
    vec4 c = texture(u_tex0, uv);
    // Opaco: a cor como está (sem dividir por um alfa que não vale).
    if (p.flags.z > 0.5) return vec4(c.rgb, 1.0);
    // Reto → pré-multiplicado ANTES de filtrar: a média de quatro amostras de
    // alfa reto puxaria a cor dos pixels transparentes para a borda.
    return p.flags.y > 0.5 ? c : vec4(c.rgb * c.a, c.a);
}

void main() {
    if (p.flags.w < 0.5) {
        // Uma amostra (a imagem importada sempre vem por aqui).
        vec4 c = texture(u_tex0, v_uv);
        if (p.flags.z > 0.5) c.a = 1.0;
        else if (p.flags.y > 0.5) c = unpremultiply(c);
        vec3 lin = p.flags.x > 0.5 ? c.rgb : srgb_to_linear(c.rgb);
        o_color = premultiply(vec4(lin, c.a));
        return;
    }
    vec2 d = p.texel.zw * 0.25;
    vec4 c = 0.25 * (fetch(v_uv + vec2(-d.x, -d.y)) + fetch(v_uv + vec2(d.x, -d.y))
                   + fetch(v_uv + vec2(-d.x, d.y)) + fetch(v_uv + vec2(d.x, d.y)));
    // A curva sRGB vale sobre a cor reta: desfaz o alfa, decodifica, refaz.
    vec4 s = unpremultiply(c);
    vec3 lin = p.flags.x > 0.5 ? s.rgb : srgb_to_linear(s.rgb);
    o_color = premultiply(vec4(lin, s.a));
}
