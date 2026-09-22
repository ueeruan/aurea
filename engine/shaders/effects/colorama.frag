#version 450
// =============================================================================
//  Aurea / shaders / effects / colorama.frag
//
//  Remapeamento de cor (Fase 7.3 §37): a entrada (luminância, matiz ou um
//  canal) vira uma posição num arco-íris que gira. `ciclos` diz quantas vezes
//  o arco-íris cabe na faixa, `fase` gira o conjunto — animar a fase faz a cor
//  correr pela imagem.
//
//  A cor sai do HSV com saturação e brilho próprios, então dá para fazer do
//  duotone fechado ao arco-íris estourado. `mistura` devolve o original.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = fase (voltas), y = ciclos, z = saturação (0..2), w = brilho (0..2)
    vec4 p1;   // x = entrada (0 luma, 1 matiz, 2 R, 3 G, 4 B), y = mistura (0..1), z = 1 inverter o arco, w = croma (0..1)
    vec4 p2;   // x = deslocamento do valor (-1..1), y = ganho, zw livres
    vec4 p3;
    vec4 color;
} p;

/// A rampa do arco-íris: três ondas defasadas, o clássico "espectro" barato.
vec3 rainbow(float t) {
    const vec3 k = vec3(0.0, 2.0 / 3.0, 1.0 / 3.0);
    return clamp(abs(fract(vec3(t) + k) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}

void main() {
    const vec4 src = unpremultiply(texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw));
    const vec3 lin = max(src.rgb, vec3(0.0));
    const vec3 enc = aurea_linear_to_srgb(lin);

    // A ENTRADA: o que decide onde cada pixel cai no arco-íris.
    const int source = int(p.p1.x + 0.5);
    float x;
    if (source == 0) {
        x = dot(enc, vec3(0.2126, 0.7152, 0.0722));
    } else if (source == 1) {
        const float mx = max(enc.r, max(enc.g, enc.b));
        const float mn = min(enc.r, min(enc.g, enc.b));
        const float d = mx - mn;
        float h = 0.0;
        if (d > 1e-6) {
            if (mx == enc.r)      h = fract((enc.g - enc.b) / d / 6.0);
            else if (mx == enc.g) h = ((enc.b - enc.r) / d + 2.0) / 6.0;
            else                  h = ((enc.r - enc.g) / d + 4.0) / 6.0;
        }
        // A matiz já é circular: aqui o arco-íris vira um giro de matiz.
        x = h;
    } else {
        x = enc[source - 2];
    }

    // Ganho e deslocamento primeiro (esticar a faixa), depois ciclos e fase —
    // é a ordem em que se pensa o efeito: "quantas voltas" e "onde começa".
    x = x * p.p2.y + p.p2.x;
    x = x * p.p0.y + p.p0.x;
    if (p.p1.z > 0.5) x = -x;

    const float y = fract(x);
    // O brilho acompanha a cor de origem: o arco-íris não achata a imagem.
    const float source_val = source == 1 ? 0.55 + 0.45 * max(enc.r, max(enc.g, enc.b)) : 0.75;
    vec3 mapped = rainbow(y) * p.p0.w * source_val;

    // Saturação: `rainbow` sai com saturação cheia; abaixo de 1 o resultado
    // desliza para o cinza da própria cor mapeada.
    const float sat = clamp(p.p0.z, 0.0, 2.0);
    mapped = mix(vec3(aurea_luma(mapped)), mapped, sat);

    // A rampa é uma PALETA (definida em sRGB), não luz: volta ao espaço de
    // trabalho antes de misturar com o original.
    mapped = aurea_srgb_to_linear(clamp(mapped, 0.0, 1.0));
    const vec3 outc = mix(lin, max(mapped * max(p.color.rgb, vec3(0.0)), vec3(0.0)), clamp(p.p1.y, 0.0, 1.0));
    o_color = premultiply(vec4(outc, src.a));
}
