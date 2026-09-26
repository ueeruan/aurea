#version 450
// =============================================================================
//  Aurea / shaders / effects / lens_blur.frag
//
//  Desfoque de lente (Fase 7.3 §41): não é um gaussiano. Uma lente de verdade
//  tem ÍRIS (o formato do diafragma decide o desenho do bokeh), trata as luzes
//  de forma diferente das sombras (`ganho das luzes` — o "highlight boost" dos
//  plugins), e o resultado é um disco de amostras, não uma curva de pesos.
//
//  Núcleo em disco (ou em polígono de N lados, que é a íris fechada) com um
//  número fixo de amostras em anéis concêntricos. Raio grande: o C++ reduz a
//  imagem antes, porque um disco de raio r na escala 1/k é um disco de raio
//  r/k — o custo por pixel fica preso e o desenho não muda.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = raio em texels, y = ganho das luzes, z = lados da íris (0 = redonda), w = suavidade
    vec4 p1;   // x = rotação da íris (graus), y = amostras por anel (4..24), z = anéis (1..6), w = mistura
    vec4 p2;   // x = 1 brilhar o que passa do limiar, y = limiar
    vec4 p3;   // x = 1 mostrar só o desfoque
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 center = texture(u_tex0, inUv);

    const int sides = int(p.p0.z + 0.5);
    const int ringSamples = int(clamp(p.p1.y, 4.0, 24.0));
    const int rings = int(clamp(p.p1.z, 1.0, 6.0));
    const float radius = max(p.p0.x, 0.0);
    const float rot = radians(p.p1.x);

    vec3 acc = center.rgb;
    float alpha = center.a;
    float wsum = 1.0;
    const float boost = max(p.p0.y, 0.0);

    for (int ring = 1; ring <= 6; ++ring) {
        if (ring > rings) break;
        const float rn = float(ring) / float(rings);
        const float r = radius * rn;
        for (int i = 0; i < 24; ++i) {
            if (i >= ringSamples) break;
            float a = float(i) / float(ringSamples) * AUREA_TAU + rot + rn * 0.7;
            vec2 dir = vec2(cos(a), sin(a));
            // Íris poligonal: o raio é cortado pelo lado do diafragma, o que
            // transforma o disco num pentágono/hexágono — o bokeh com quinas.
            if (sides >= 3) {
                const float sect = AUREA_TAU / float(sides);
                const float k = cos(mod(a, sect) - sect * 0.5);
                dir *= 1.0 / max(k, 0.35);
            }
            const vec4 s = texture(u_tex0, inUv + dir * r * p.texel.xy);
            // Ganho das luzes: o que é claro pesa mais no desfoque — é o que
            // faz uma lâmpada virar uma bola de luz em vez de um borrão cinza.
            const float luma = aurea_luma(aurea_linear_to_srgb(max(unpremultiply(s).rgb, vec3(0.0))));
            const float w = 1.0 + boost * luma * luma;
            acc += s.rgb * w;
            alpha += s.a * w;
            wsum += w;
        }
    }
    acc /= wsum;
    alpha /= wsum;

    const float k = clamp(p.p1.w, 0.0, 1.0);
    vec3 outc = mix(center.rgb, acc, k);
    if (p.p3.x > 0.5) outc = acc;
    o_color = vec4(max(outc, vec3(0.0)), p.p3.x > .5 ? alpha : mix(center.a, alpha, k));
}
