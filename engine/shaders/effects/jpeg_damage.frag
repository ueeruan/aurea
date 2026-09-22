#version 450
// =============================================================================
//  Aurea / shaders / effects / jpeg_damage.frag  (Fase 7.3 §32)
//
//  Dano de JPEG: não é "borrar e pixelizar". O JPEG quebra em três artefatos
//  distintos, e cada um tem a sua cara:
//
//    BLOCO      — os 8×8 pixels viram degraus porque cada bloco foi quantizado
//                 por conta própria. Aparece como quadriculado nas áreas lisas.
//    ANELAMENTO — o ringing ao redor das bordas duras (o "fantasma" que sai
//                 do alto contraste). É um borrão oscilante, não um borrão.
//    CROMA      — a cor é guardada em metade da resolução: as bordas de cor
//                 ficam desalinhadas das de luz. É o artefato mais visível.
//
//  `qualidade` escala os três; os controles individuais permitem exagerar um
//  só, que é o que se quer quando o efeito é estético e não uma simulação.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = qualidade (1 = perfeito, 0 = destruído), y = blocos, z = anelamento, w = croma
    vec4 p1;   // x = tamanho do bloco (px), y = suavizar o bloco, z = reforço de borda, w = mistura
    vec4 p2;   // x = corrupção de blocos (0..1), y = semente, zw livres
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

/// A média de um bloco 8×8 (aqui, `p1.x` pixels): quantizar pela média é o
/// que o JPEG faz a menos — o bloco perde o interior.
vec3 block_average(vec2 inUv, vec2 blockUv) {
    vec3 acc = vec3(0.0);
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const vec2 o = (vec2(float(i), float(j)) / 3.0 - 0.5) * blockUv;
            acc += max(unpremultiply(texture(u_tex0, inUv + o)).rgb, vec3(0.0));
        }
    }
    return acc / 16.0;
}

void main() {
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = unpremultiply(texture(u_tex0, inUv));
    const vec3 original = max(src.rgb, vec3(0.0));

    const float blockPx = max(p.p1.x, 2.0);
    const vec2 blockUv = vec2(blockPx) * uvPerLayer;

    // Qualidade é o botão único: 1 = intacto, 0 = destruído. Ela ESCALA os três
    // artefatos de uma vez, e os controles individuais dizem quanto de cada um
    // sobrevive a essa escala — assim dá para exagerar um só sem mexer nos
    // outros (que é o que se quer quando o efeito é estético).
    const float damage = clamp(1.0 - p.p0.x, 0.0, 1.0);
    const float blocks = clamp(p.p0.y, 0.0, 1.0) * damage;
    const float ringing = clamp(p.p0.z, 0.0, 1.0) * damage;
    const float chroma = clamp(p.p0.w, 0.0, 1.0) * damage;

    // --- BLOCO: cada célula vira a sua própria média. O `suavizar` interpola
    // entre a média e o pixel, que é o degrau do bloco aparecendo.
    vec3 c = original;
    if (blocks > 0.0) {
        const vec2 cell = floor(inUv / blockUv);
        const vec2 cellCenter = (cell + 0.5) * blockUv;
        vec3 avg = block_average(cellCenter, blockUv);

        // Corrupção: alguns blocos saem com a média de outro lugar (o bloco
        // que chegou corrompido na transmissão).
        if (p.p2.x > 0.0) {
            const float r = aurea_hash(uvec2(uvec2(ivec2(cell)) ^ uvec2(uint(p.p2.y) * 2654435761u, 0u)));
            if (r < p.p2.x * 0.15) {
                const float dx = (aurea_hash(uvec2(uvec2(ivec2(cell)) + uvec2(1u, 0u))) * 2.0 - 1.0) * blockPx * 3.0;
                const float dy = (aurea_hash(uvec2(uvec2(ivec2(cell)) + uvec2(0u, 1u))) * 2.0 - 1.0) * blockPx * 3.0;
                avg = max(unpremultiply(texture(u_tex0, cellCenter + vec2(dx, dy) * uvPerLayer)).rgb, vec3(0.0));
            }
        }
        c = mix(c, avg, blocks);
    }

    // --- ANELAMENTO: o fantasma ao redor das bordas duras. Aproximado pela
    // diferença entre o pixel e a média de uma vizinhança larga, oscilando.
    if (ringing > 0.0) {
        vec3 wide = vec3(0.0);
        for (int i = 0; i < 8; ++i) {
            const float a = float(i) / 8.0 * AUREA_TAU;
            const vec2 o = vec2(cos(a), sin(a)) * blockUv * 1.5;
            wide += max(unpremultiply(texture(u_tex0, inUv + o)).rgb, vec3(0.0));
        }
        wide /= 8.0;
        const vec3 ring = (original - wide) * -0.9;
        // A oscilação é o que separa o anelamento de um contorno qualquer.
        const vec2 g = fract(inUv / blockUv);
        const float osc = sin((g.x + g.y) * 12.56) * 0.5 + 0.5;
        c += ring * ringing * (0.6 + osc * 0.8);
    }

    // --- CROMA: a cor numa resolução menor, desalinhada da luz. Este é o
    // artefato que mais se reconhece como JPEG.
    if (chroma > 0.0) {
        const vec2 chromaUv = floor(inUv / (blockUv * 2.0)) * blockUv * 2.0 + blockUv;
        const vec3 sub = max(unpremultiply(texture(u_tex0, chromaUv)).rgb, vec3(0.0));
        const float yc = aurea_luma(c);
        const float ys = aurea_luma(sub);
        // Leva o CROMA do bloco e mantém a LUMA do pixel: é exatamente o que o
        // 4:2:0 faz com a imagem.
        c = mix(c, (c - vec3(yc)) + vec3(ys), chroma);
    }

    const float k = clamp(p.p1.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(original, c, k), vec3(0.0)), src.a));
}
