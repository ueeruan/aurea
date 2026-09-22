#version 450
// =============================================================================
//  Aurea / shaders / effects / holomatrix.frag  (Fase 7.3 §49)
//
//  Holográfico: a imagem vira uma projeção. Cinco camadas, todas opcionais:
//
//    1. TINGIR por peso de luz — o corpo da imagem pega a cor do holograma
//    2. GRADE técnica em perspectiva, com brilho nas linhas
//    3. VARREDURA que sobe (ou desce) e deixa um rastro aceso
//    4. INTERFERÊNCIA: listras finas que correm na horizontal
//    5. BRILHO nas bordas — o holograma acende onde a imagem tem detalhe
//
//  A grade não é um padrão colado: ela é modulada pela luz da imagem, então
//  acompanha o que está sendo projetado.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = mistura da cor, y = força da grade, z = células da grade, w = brilho das bordas
    vec4 p1;   // x = altura da varredura (0..1), y = largura da varredura, z = interferência, w = velocidade da varredura
    vec4 p2;   // x = frequência da interferência, y = opacidade de fundo, z = linhas de varredura, w = mistura
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = unpremultiply(texture(u_tex0, inUv));
    const vec3 tint = max(p.color.rgb, vec3(0.0));

    const float luma = aurea_luma(aurea_linear_to_srgb(max(src.rgb, vec3(0.0))));
    const float frame = p.p3.x;

    // 1. Tingir: o holograma tem a cor da projeção, não a cor original.
    vec3 c = mix(src.rgb, tint * (0.25 + luma * 1.4), clamp(p.p0.x, 0.0, 1.0));

    // 2. Grade: linhas finas no espaço da TELA (não da imagem), com a
    // intensidade seguida pela luz — a grade só aparece onde há projeção.
    if (p.p0.y > 0.0) {
        const vec2 g = fract(v_uv * max(p.p0.z, 2.0));
        const float line = smoothstep(0.94, 1.0, max(g.x, g.y));
        c += tint * line * p.p0.y * (0.25 + luma * 1.6);
        // Cruzamento das linhas: um ponto mais aceso nas quinas da célula.
        const float node = smoothstep(0.06, 0.0, min(min(g.x, 1.0 - g.x), min(g.y, 1.0 - g.y)));
        c += tint * node * p.p0.y * 0.8 * luma;
    }

    // 3. Varredura: uma faixa clara que sobe, com rastro.
    if (p.p1.y > 0.0) {
        const float y = fract(p.p1.x - frame * p.p1.w * 0.01);
        const float d = abs(fract(v_uv.y - y + 0.5) - 0.5);
        const float sweep = smoothstep(p.p1.y, 0.0, d);
        c += tint * sweep * (0.35 + luma);
        // O rastro acima da varredura: linhas que ficam acesas um instante.
        const float trail = smoothstep(0.0, p.p1.y * 4.0, fract(v_uv.y - y + 1.0)) * (1.0 - sweep);
        c += tint * trail * 0.10 * luma;
    }

    // 4. Interferência: listras horizontais finas que correm.
    if (p.p2.x > 0.0) {
        const float w = sin((v_uv.y * p.p2.x * 200.0) + inUv.x * 2.0 + frame * 0.4);
        c += tint * max(w, 0.0) * 0.10 * luma;
    }

    // 5. Brilho nas bordas: o gradiente da imagem vira luz. É o que faz o
    // holograma parecer feito de linhas e não de pixels.
    if (p.p0.w > 0.0) {
        const vec2 e = vec2(1.5) * uvPerLayer;
        const float lx = aurea_luma(max(unpremultiply(texture(u_tex0, inUv + vec2(e.x, 0.0))).rgb, vec3(0.0)))
                       - aurea_luma(max(unpremultiply(texture(u_tex0, inUv - vec2(e.x, 0.0))).rgb, vec3(0.0)));
        const float ly = aurea_luma(max(unpremultiply(texture(u_tex0, inUv + vec2(0.0, e.y))).rgb, vec3(0.0)))
                       - aurea_luma(max(unpremultiply(texture(u_tex0, inUv - vec2(0.0, e.y))).rgb, vec3(0.0)));
        c += tint * clamp(length(vec2(lx, ly)) * 3.0, 0.0, 1.0) * p.p0.w * 1.5;
    }

    // 6. Linhas de varredura finas do próprio holograma.
    if (p.p2.z > 0.0) {
        const float yPx = v_uv.y / max(p.texel.y, 1e-6) / max(p.texel.w, 1e-6);
        c *= 1.0 - p.p2.z * 0.35 * (0.5 + 0.5 * sin(yPx * AUREA_TAU / 3.0));
    }

    // Fundo do holograma: o preto vira a cor da projeção apagada.
    c = mix(c, c + tint * p.p2.y * 0.05, 1.0 - luma);
    c = max(c, vec3(0.0));

    const float k = clamp(p.p2.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(src.rgb, c, k), vec3(0.0)), src.a));
}
