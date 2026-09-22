#version 450
// =============================================================================
//  Aurea / shaders / effects / pixel_sort.frag
//
//  Ordenação de pixels (Fase 7.3 §33): o efeito que ficou famoso — a imagem
//  vira riscos porque os pixels de uma linha são ordenados pela luz.
//
//  Ordenar de verdade num shader exigiria uma rede de ordenação com centenas
//  de comparações. O que se faz (e o que os plugins fazem) é uma REDE DE
//  ORDENAÇÃO DE PASSOS: cada amostra dá um passo na direção `+` ou `−` se o
//  vizinho for maior, o que empurra os claros para um lado. `passos` controla
//  quanto a linha anda; com poucos passos o risco fica curto, com muitos ele
//  atravessa a imagem. É o mesmo resultado visual, com custo fixo.
//
//  O LIMIAR decide quem entra: abaixo dele o pixel fica onde está (é o que
//  separa "a imagem derreteu" de "choveu listras").
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = limiar baixo, y = limiar alto, z = comprimento máximo (0..1), w = aleatoriedade (0..1)
    vec4 p1;   // x = direção (0 horiz, 1 vert, 2 diag, 3 radial), y = 1 inverter o sentido, z = modo (0 luz, 1 tom, 2 R, 3 G, 4 B), w = semente
    vec4 p2;   // x = 1 ordenar por faixa de tom (em vez de por limiar)
    vec4 p3;   // x = passo em texels
    vec4 color;
} p;

/// A chave de ordenação: o que decide quem é "maior".
float key_of(vec3 c) {
    const int mode = int(p.p1.z + 0.5);
    if (mode == 1) return aurea_luma(aurea_linear_to_srgb(max(c, vec3(0.0))));
    if (mode == 2) return c.r;
    if (mode == 3) return c.g;
    if (mode == 4) return c.b;
    return aurea_luma(aurea_linear_to_srgb(max(c, vec3(0.0))));
}

void main() {
    const vec4 src = unpremultiply(texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw));

    // O eixo do risco, em pixels da entrada.
    const int dir = int(p.p1.x + 0.5);
    vec2 axis;
    if (dir == 0)      axis = vec2(1.0, 0.0);
    else if (dir == 1) axis = vec2(0.0, 1.0);
    else if (dir == 2) axis = vec2(0.7071, 0.7071);
    else               axis = normalize(v_uv - vec2(0.5) + vec2(1e-6));
    if (p.p1.y > 0.5) axis = -axis;

    const vec2 stepUv = axis * p.texel.xy * max(p.p3.x, 1.0);
    const float len = clamp(p.p0.z, 0.0, 1.0);
    const float lo = p.p0.x, hi = p.p0.y;

    // A ordem da amostra na rede. O embaralhamento (`aleatoriedade`) tira o
    // aspecto de "pente" que uma rede perfeita deixa.
    const uint salt = uint(p.p1.w) * 2654435761u;
    const float jitter = (aurea_hash(uvec2(ivec2(floor(v_uv / max(p.texel.xy, vec2(1e-6)))))
                                     ^ uvec2(salt, 0u))) * 2.0 - 1.0;

    vec4 cur = src;
    float curKey = key_of(cur.rgb);

    for (int i = 0; i < 96; ++i) {
        // A rede percorre a linha para trás e para frente: cada passo compara
        // com o vizinho e troca se estiver fora de ordem.
        const float t = float(i) / 96.0;
        if (t > len) break;
        const vec2 off = stepUv * (float(i) + 1.0) * (1.0 + jitter * p.p0.w * 3.0);
        const vec4 other = unpremultiply(texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw + off));
        const float k = key_of(other.rgb);

        // Máscara: só os pixels dentro da faixa entram na ordenação. Fora
        // dela, a imagem fica exatamente como estava.
        const float inBand = (curKey >= lo && curKey <= hi) ? 1.0 : 0.0;
        const float otherBand = (k >= lo && k <= hi) ? 1.0 : 0.0;
        if (inBand > 0.5 && otherBand > 0.5 && k > curKey) {
            cur = other;
            curKey = k;
        }
    }

    // Fora da faixa, `cur` nunca foi trocado e continua sendo o próprio pixel:
    // a máscara sai de graça, sem um segundo teste aqui.
    o_color = premultiply(vec4(max(cur.rgb, vec3(0.0)), src.a));
}
