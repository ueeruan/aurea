#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_block_load.frag
//
//  Carga em blocos (o comportamento do CC Block Load, na direção das outras
//  transições do app: 0% inteiro, 100% sumido): a imagem passa por blocos cada
//  vez maiores, como um JPEG progressivo ao contrário — 1, 2, 4... px. Com
//  "Linhas de varredura" cada etapa troca de cima para baixo, linha de blocos
//  por linha; sem elas a etapa troca inteira. "Terminar vazio" acrescenta uma
//  última etapa que apaga os blocos. "Bilinear" suaviza os blocos (mistura dos
//  quatro centros vizinhos).
//
//  p0 = conclusão (0..1), etapas, bloco base (px), varredura;
//  p1 = terminar vazio, bilinear.
// =============================================================================
#include "cc_common.glsl"

vec4 blocks(vec2 px, float size, bool smoothBlocks) {
    const vec2 rel = (px - p.box.xy) / size;
    if (!smoothBlocks) return cc_at(p.box.xy + (floor(rel) + 0.5) * size);
    const vec2 g = rel - 0.5;
    const vec2 i = floor(g);
    const vec2 f = g - i;
    const vec4 a = cc_at(p.box.xy + (i + vec2(0.5, 0.5)) * size);
    const vec4 b = cc_at(p.box.xy + (i + vec2(1.5, 0.5)) * size);
    const vec4 c = cc_at(p.box.xy + (i + vec2(0.5, 1.5)) * size);
    const vec4 d = cc_at(p.box.xy + (i + vec2(1.5, 1.5)) * size);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

/// A imagem na etapa `level` (0 = original; a última, com "terminar vazio", some).
vec4 at_level(vec2 px, int level, int levels, bool cleared) {
    if (level <= 0) return cc_at(px);
    if (cleared && level >= levels) return vec4(0.0);
    return blocks(px, p.p0.z * exp2(float(level)), p.p1.y > 0.5);
}

void main() {
    const vec2 px = cc_px();
    const float k = clamp(p.p0.x, 0.0, 1.0);
    const int levels = max(int(p.p0.y + 0.5), 1);
    const bool cleared = p.p1.x > 0.5;
    const float x = k * float(levels);
    int level = int(floor(x));
    if (level >= levels) { o_color = at_level(px, levels, levels, cleared); return; }
    if (p.p0.w > 0.5) {
        // A próxima etapa desce em linhas de blocos dela mesma.
        const float rowSize = p.p0.z * exp2(float(level + 1));
        const float rows = max(ceil(p.box.w / rowSize), 1.0);
        const float row = floor((px.y - p.box.y) / rowSize);
        if (row < floor(fract(x) * (rows + 1.0))) level += 1;
    } else if (fract(x) >= 0.5) {
        level += 1;
    }
    o_color = at_level(px, level, levels, cleared);
}
