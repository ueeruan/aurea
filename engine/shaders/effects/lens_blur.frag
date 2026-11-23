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
    vec4 p2;   // rack: x = gama, y = limiar das luzes, z = bokeh, w = sinal do rack
    vec4 p3;   // x = 1 mostrar só o desfoque, y = curvatura da íris (−1 estrela .. +1 círculo), zw = escala x/y do disco
    vec4 color;
} p;

/// Quanto a íris alcança na direção `a` (0 = meio de um lado), relativo ao
/// raio do meio do lado: 1 no meio, 1/cos(setor/2) na quina — o polígono de
/// sempre. Curvatura positiva encurta as quinas até o círculo; negativa puxa
/// o meio dos lados para dentro (estrela). `s` = cos(setor/2)/cos(a) ≤ 1 é a
/// forma normalizada (1 na quina), o que dá base segura ao pow.
float iris_reach(float a, float sect, float curve) {
    const float k = max(cos(a), 0.35);
    const float edge = cos(sect * 0.5);
    const float s = min(edge / k, 1.0);
    const float shaped = curve >= 0.0 ? mix(s, 1.0, curve) : pow(s, 1.0 + 4.0 * (-curve));
    return shaped / edge;
}

vec4 rack_defocus(vec2 uv, vec4 center) {
    const int sides = int(p.p0.z + 0.5);
    const int samples = int(clamp(p.p1.y, 4.0, 24.0));
    const int rings = int(clamp(p.p1.z, 1.0, 6.0));
    const float gamma = p.p2.x;
    const float bokeh = p.p2.z;
    const float rotation = radians(p.p1.x);
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int ring = 0; ring < rings; ++ring) {
        // Equal-area rings keep the convolution unbiased across the aperture.
        const float r = sqrt((float(ring) + 0.5) / float(rings));
        const float bokehWeight = bokeh < 0.0 ? mix(1.0, r * r, -bokeh)
            : mix(1.0, 1.0 - smoothstep(0.65, 1.0, r), bokeh);
        const float weight = bokehWeight * mix(1.0 - smoothstep(0.5, 1.0, r), 1.0, clamp(p.p0.w, 0.0, 1.0));
        for (int i = 0; i < samples; ++i) {
            const float a = (float(i) + float(ring) * 0.381966) * AUREA_TAU / float(samples);
            vec2 dir = vec2(cos(a + rotation), sin(a + rotation));
            if (sides >= 3) {
                const float sector = AUREA_TAU / float(sides);
                dir *= iris_reach(mod(a, sector) - sector * 0.5, sector, p.p3.y) * cos(sector * 0.5);
            }
            const vec2 offset = dir * p.p3.zw * p.texel.xy * (r * p.p0.x);
            const vec4 tap = texture(u_tex0, uv + offset);
            vec3 signal = max(unpremultiply(tap).rgb, vec3(0.0));
            if (p.p0.y > 0.0) signal += max(signal - vec3(p.p2.y), vec3(0.0)) * p.p0.y;
            if (gamma != 1.0) signal = pow(signal, vec3(gamma));
            sum += vec4(signal * tap.a, tap.a) * weight;
            total += weight;
        }
    }
    vec4 filtered = sum / max(total, 1e-6);
    if (gamma != 1.0) filtered.rgb = pow(max(unpremultiply(filtered).rgb, vec3(0.0)), vec3(1.0 / gamma)) * filtered.a;
    const vec4 signal = p.p3.x > 0.5 ? filtered : mix(center, filtered, clamp(p.p1.w, 0.0, 1.0));
    return vec4(srgb_to_linear(max(unpremultiply(signal).rgb, vec3(0.0))) * signal.a, signal.a);
}

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 center = texture(u_tex0, inUv);
    if (p.p2.w > 0.5) {
        o_color = rack_defocus(inUv, center);
        return;
    }

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
                dir *= iris_reach(mod(a, sect) - sect * 0.5, sect, clamp(p.p3.y, -1.0, 1.0));
            }
            // Escala x/y: o disco vira elipse (bokeh anamórfico).
            dir *= vec2(max(p.p3.z, 0.01), max(p.p3.w, 0.01));
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
