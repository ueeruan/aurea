#version 450
// =============================================================================
//  Aurea / shaders / effects / halftone.frag
//
//  Meio-tom (Fase 7.3 §42): a imagem vira pontos. O tamanho do ponto carrega a
//  luz; a ROTAÇÃO de cada canal separa as grades de R, G e B, que é o que
//  impede a moiré de três retículas alinhadas.
//
//  Padrões: ponto redondo clássico, linha (coberto de traços) e losango.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = tamanho da célula (px), y = contraste, z = ângulo da grade (graus), w = suavidade
    vec4 p1;   // x = rotação por canal (graus), y = padrão (0 ponto, 1 linha, 2 losango), z = grades separadas (0/1)
    vec4 p2;   // x = fundo (0 preto, 1 papel), y = ganho do ponto, zw livres
    vec4 p3;
    vec4 color;
} p;

/// A cobertura de UM canal: 1 onde o ponto está cheio, 0 no fundo da célula.
/// `channel` escolhe o componente da cor (0 = R, 1 = G, 2 = B).
float halftone_channel(int channel, vec2 px, bool separate, float softness, int pattern) {
    const float chAngle = separate ? radians(p.p0.z + p.p1.x * float(channel)) : radians(p.p0.z);
    const vec2 cellPx = vec2(max(p.p0.x, 2.0));
    const vec2 q = aurea_rot2(chAngle) * px / cellPx;
    const vec2 f = fract(q) - 0.5;   // posição dentro da célula, centrada

    // A luz vem do CENTRO da célula: o ponto carrega a luz da área, não a do
    // pixel — é o que dá o aspecto de retícula em vez de xadrez.
    const vec2 centerPx = (floor(q) + 0.5) * cellPx;
    const vec2 centerUv = aurea_rot2(-chAngle) * centerPx * p.texel.xy;
    const vec3 s = max(unpremultiply(texture(u_tex0, centerUv * p.uvMap.xy + p.uvMap.zw)).rgb, vec3(0.0));

    vec3 enc = aurea_linear_to_srgb(s);
    enc = clamp((enc - 0.5) * (1.0 + p.p0.y) + 0.5, 0.0, 1.0);
    float luma = enc[channel];

    const float size = clamp(luma * p.p2.y * 2.0, 0.0, 1.0);
    if (pattern == 1) {
        const float r = 0.5 - size * 0.5;
        return smoothstep(r + softness * 0.5, r - softness * 0.5, abs(f.y));
    }
    if (pattern == 2) {
        const float r = size * 0.5;
        const float d = (abs(f.x) + abs(f.y)) * 0.92;
        return smoothstep(r + softness * 0.5, r - softness * 0.5, d);
    }
    // Redondo: o raio cresce com a raiz da luz, porque é a ÁREA que a carrega.
    const float r = sqrt(size) * 0.62;
    return smoothstep(r + softness * 0.5, r - softness * 0.5, length(f));
}

void main() {
    const vec2 px = v_uv / max(p.texel.xy, vec2(1e-6));
    const float softness = max(p.p0.w, 0.01);
    const int pattern = int(p.p1.y + 0.5);
    const bool separate = p.p1.z > 0.5;
    const float bg = clamp(p.p2.x, 0.0, 1.0);

    const vec3 cover = vec3(
        halftone_channel(0, px, separate, softness, pattern),
        halftone_channel(1, px, separate, softness, pattern),
        halftone_channel(2, px, separate, softness, pattern));

    // Fundo de papel: o que não é ponto sai claro em vez de preto.
    vec3 outC = mix(vec3(bg), vec3(1.0), cover);
    outC = aurea_srgb_to_linear(clamp(outC, 0.0, 1.0)) * max(p.color.rgb, vec3(0.0));
    o_color = premultiply(vec4(outC, 1.0));
}
