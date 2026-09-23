#version 450
// =============================================================================
//  Aurea / shaders / effects / halftone.frag
//
//  Meio-tom (Fase 7.3 §42) com a geometria do COLOR HALFTONE do After Effects
//  (Fase 9.3): a retícula é uma grade QUADRADA medida em pixels da imagem,
//  girada pelo ângulo de cada canal e com o centro deslocado por `Center`. O
//  RAIO DO PONTO é proporcional a (1 − luz) daquele canal — o ponto chega ao
//  Max Radius onde o canal é 0 e some onde o canal é 1.
//
//  MAPEAMENTO DOS CANAIS — o AE usa quatro ângulos (C, M, Y, K) porque imprime
//  em CMYK; o Aurea é RGB, então o mapeamento é o de impressão, sem fingir CMYK
//  (nenhuma conversão de espaço de cor acontece aqui):
//      canal 1 → tinta C, que ABSORVE o vermelho → luz = R
//      canal 2 → tinta M, que ABSORVE o verde    → luz = G
//      canal 3 → tinta Y, que ABSORVE o azul     → luz = B
//      canal 4 → tinta K, que absorve os três    → luz = max(R, G, B)
//  A saída de um canal é 1 − cobertura, porque a tinta SUBTRAI luz: é o que faz
//  um canal escuro virar ponto GRANDE e a imagem continuar positiva, como no AE.
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
    vec4 texel;   // xy = 1/texels da SAÍDA (o px daqui é texel); zw = texels por pixel de layer
    vec4 p0;      // x = passo da grade (texels), y = contraste, z = ângulo base (graus), w = suavidade
    vec4 p1;      // x = rotação por canal legada (graus), y = padrão (0 ponto, 1 linha, 2 losango),
                  // z = grades separadas (0/1), w = raio máximo (texels)
    vec4 p2;      // x = fundo, y = ganho do ponto, zw = centro da retícula (texels, na saída)
    vec4 p3;      // x..w = ângulos dos canais 1..4 (graus) — os do AE
    vec4 color;
} p;

/// A cor do CENTRO da célula do canal, já em sRGB e com o contraste aplicado.
///
/// A luz vem do centro da célula, não do pixel: é o que dá o aspecto de
/// retícula em vez de xadrez — o ponto carrega a luz média da área. `f` sai com
/// a posição do pixel DENTRO da célula, centrada (0,5 = a borda).
vec3 halftone_cell(vec2 px, float angle, out vec2 f) {
    const vec2 cellPx = vec2(max(p.p0.x, 2.0));
    const vec2 q = aurea_rot2(angle) * (px - p.p2.zw) / cellPx;
    f = fract(q) - 0.5;
    const vec2 centerPx = (floor(q) + 0.5) * cellPx;
    const vec2 rel = aurea_rot2(-angle) * centerPx + p.p2.zw;
    const vec3 s = max(unpremultiply(texture(u_tex0, rel * p.texel.xy * p.uvMap.xy + p.uvMap.zw)).rgb, vec3(0.0));
    const vec3 enc = aurea_linear_to_srgb(s);
    return clamp((enc - 0.5) * (1.0 + p.p0.y) + 0.5, 0.0, 1.0);
}

/// A cobertura da TINTA daquele canal: 1 onde o ponto está cheio. O raio é
/// `(1 − luz) × raio máximo`, em unidades de célula — 0,5 é a borda, e é o que
/// faz o ponto tocar o vizinho onde o canal zera, como no AE.
float halftone_ink(vec2 f, float light, float softness, int pattern) {
    const float r = clamp(1.0 - light, 0.0, 1.0) * p.p2.y * p.p1.w / max(p.p0.x, 2.0);
    if (r <= 0.0) return 0.0;   // canal cheio: nenhuma tinta, nenhum ponto
    if (pattern == 1) return smoothstep(r + softness * 0.5, r - softness * 0.5, abs(f.y));
    if (pattern == 2) return smoothstep(r + softness * 0.5, r - softness * 0.5, abs(f.x) + abs(f.y));
    return smoothstep(r + softness * 0.5, r - softness * 0.5, length(f));
}

void main() {
    const vec2 px = v_uv / max(p.texel.xy, vec2(1e-6));
    const float softness = max(p.p0.w, 0.01);
    const int pattern = int(p.p1.y + 0.5);
    const bool separate = p.p1.z > 0.5;
    const float paper = clamp(p.p2.x, 0.0, 1.0);

    // Os quatro ângulos. Sem "grades separadas" as quatro tintas usam a MESMA
    // grade (só o ângulo base): é o que o interruptor sempre quis dizer.
    const float base = radians(p.p0.z);
    const float legacy = radians(p.p1.x);
    const float a1 = separate ? base + radians(p.p3.x) : base;
    const float a2 = separate ? base + legacy + radians(p.p3.y) : base;
    const float a3 = separate ? base + legacy * 2.0 + radians(p.p3.z) : base;
    const float a4 = separate ? base + legacy * 3.0 + radians(p.p3.w) : base;

    vec2 f1, f2, f3, f4;
    const vec3 c1 = halftone_cell(px, a1, f1);
    const vec3 c2 = halftone_cell(px, a2, f2);
    const vec3 c3 = halftone_cell(px, a3, f3);
    const vec3 c4 = halftone_cell(px, a4, f4);

    // A mancha de cada canal de saída: a retícula da tinta que ABSORVE aquele
    // canal mais a do preto, que absorve os três (tinta sobre tinta escurece).
    const vec3 ink = vec3(
        halftone_ink(f1, c1.r, softness, pattern),
        halftone_ink(f2, c2.g, softness, pattern),
        halftone_ink(f3, c3.b, softness, pattern));
    const float inkK = halftone_ink(f4, max(max(c4.r, c4.g), c4.b), softness, pattern);
    const vec3 stain = vec3(1.0) - (vec3(1.0) - ink) * (1.0 - inkK);

    // Tinta sobre papel: a tinta subtrai luz (canal escuro = ponto grande, e a
    // imagem continua positiva). `Fundo claro` clareia a mancha até o papel
    // puro — em 100% a folha sai branca, que é o que o nome promete.
    vec3 outC = vec3(1.0) - stain * (1.0 - paper);

    outC = aurea_srgb_to_linear(clamp(outC, 0.0, 1.0)) * max(p.color.rgb, vec3(0.0));
    o_color = premultiply(vec4(outC, 1.0));
}
