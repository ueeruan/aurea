#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_star_field.frag
//
//  Campo de estrelas (o comportamento do CC Star Burst): a camada vira uma
//  grade de pontos redondos, cada um com a cor da imagem no lugar dele, e as
//  grades voam em direção a quem vê a partir do "Centro".
//
//  Quatro camadas de profundidade em ciclo (fase f de cada uma): a camada em
//  profundidade z aparece escalada por 1/z em volta do centro. Para cada
//  pixel e camada, volta-se ao plano da grade (z = 1), acha-se a célula e o
//  ponto dela (com a dispersão sorteada por célula) — uma amostra por camada,
//  sem varrer vizinhas: a dispersão cabe dentro da célula. As camadas surgem
//  ao longe e somem depois de passar por quem vê; a ordem é de trás para
//  frente.
//
//  p0 = centro (px), espaçamento (px), tamanho do ponto (fração);
//  p1 = fase (ciclos), dispersão, mistura com o original, semente;
//  p2 = z perto, z longe.
// =============================================================================
#include "cc_common.glsl"

const int CC_LAYERS = 4;

vec4 star_layer(vec2 px, int k, float f) {
    const vec2 c = p.p0.xy;
    const float spacing = max(p.p0.z, 1.0);
    const float z = mix(p.p2.y, p.p2.x, f);
    const float s = 1.0 / max(z, 1e-3);
    const vec2 g = (px - c) / s;                 // no plano da grade
    const vec2 cell = floor(g / spacing);
    const uint salt = uint(k) * 7919u + uint(p.p1.w) * 104729u;
    const vec2 jitter = vec2(aurea_hash3(cell, salt), aurea_hash3(cell, salt + 31u)) - 0.5;
    const float size = clamp(p.p0.w, 0.02, 1.0);
    const vec2 star = (cell + 0.5 + jitter * clamp(p.p1.y, 0.0, 1.0) * (1.0 - size)) * spacing;
    const float radius = 0.5 * size * spacing * s;
    const float dist = length(g - star) * s;
    const float cov = clamp((radius - dist) / cc_texel() + 0.5, 0.0, 1.0);
    if (cov <= 0.0) return vec4(0.0);
    const float fade = smoothstep(0.0, 0.25, f) * (1.0 - smoothstep(0.93, 1.0, f));
    return cc_at(c + (cell + 0.5) * spacing) * (cov * fade);
}

void main() {
    const vec2 px = cc_px();
    // A camada mais longe (fase menor) primeiro.
    float phases[CC_LAYERS];
    int first = 0;
    for (int k = 0; k < CC_LAYERS; ++k) {
        phases[k] = fract(p.p1.x + float(k) / float(CC_LAYERS));
        if (phases[k] < phases[first]) first = k;
    }
    vec4 acc = vec4(0.0);
    for (int j = 0; j < CC_LAYERS; ++j) {
        const int k = (first + j) % CC_LAYERS;
        const vec4 layerColor = star_layer(px, k, phases[k]);
        acc = layerColor + acc * (1.0 - layerColor.a);
    }
    o_color = mix(acc, cc_at(px), clamp(p.p1.z, 0.0, 1.0));
}
