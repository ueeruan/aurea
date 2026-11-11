#version 450
// =============================================================================
//  Aurea / shaders / effects / sea_shore_wave.frag
//
//  Onda na praia (Transição, pacote "Mar"): uma onda atravessa a camada na
//  direção do ângulo. Na frente vai a ESPUMA (borda ondulada e rendada, com
//  bolhas que ficam para trás); logo atrás uma faixa de ÁGUA translúcida que
//  refrata o que está embaixo. Revelar: a camada aparece onde a onda já
//  passou. Remover: a onda leva a camada e a água escoa.
//
//  O avanço vai de "a espuma ainda fora da camada" (0) a "a água já saiu"
//  (1): nos extremos o efeito some de verdade. Função pura do progresso e do
//  tempo (a borda balança): o mesmo quadro sai igual no preview e no export.
//
//  p0 = progresso 0..1, modo (0 revelar, 1 remover), direção (x, y)
//  p1 = ondulação da borda (px), tamanho das ondas da borda (px), espuma (px), água (px)
//  p2 = distorção (px), recortar pela camada (0/1), semente, tempo (s)
//  p3 = posição da frente (px ao longo da direção, a partir do centro), —, —, —
//  color = cor da espuma (linear);  q0 = cor da água (linear)
// =============================================================================
#include "sea_common.glsl"

void main() {
    const vec2 px = sea_px();
    const float texel = sea_texel();
    const vec2 dir = p.p0.zw;
    const vec2 perp = vec2(-dir.y, dir.x);
    const vec2 center = p.box.xy + 0.5 * p.box.zw;
    const float s = dot(px - center, dir);
    const float along = dot(px - center, perp);
    const float t = p.p2.w;
    const uint seed = uint(max(p.p2.z, 0.0));
    const float foamW = max(p.p1.z, 0.0), waterW = max(p.p1.w, 0.0);
    const float edgeSize = max(p.p1.y, 1.0);

    // A borda ondulada: um seno largo + ruído, que balança com o tempo.
    const float qa = along / edgeSize;
    const float wobble = 0.55 * sin(qa * AUREA_TAU + t * 1.4 + float(seed % 61u))
                       + 0.45 * (sea_noise(vec2(qa * 1.7, t * 0.45), seed + 3u) * 2.0 - 1.0);
    const float front = p.p3.x + p.p1.x * wobble;
    // Distância ATRÁS da frente (> 0 = a onda já passou por aqui).
    const float d = front - s;

    // Faixa de água atrás da espuma: some aos poucos.
    const float band = foamW + waterW;
    const float wet = d > 0.0 && band > 0.0 ? 1.0 - smoothstep(foamW * 0.5, band, d) : 0.0;

    // Refração sob a água.
    vec2 shift = vec2(0.0);
    if (p.p2.x > 0.0 && wet > 0.0) {
        const vec2 q = px / max(edgeSize * 0.35, 4.0);
        shift = vec2(sin(q.y * 1.9 + t * 2.1 + 2.0 * sin(q.x * 1.3 - t)),
                     cos(q.x * 1.6 - t * 1.7 + 1.5 * sin(q.y * 1.2 + t * 0.8))) * p.p2.x * wet;
    }
    const vec4 layer = sea_at(px + shift);
    const float holder = p.p2.y > 0.5 ? sea_at(px).a : 1.0;

    const float edge = smoothstep(-texel, texel, d);
    const float vis = p.p0.y < 0.5 ? edge : 1.0 - edge;
    vec4 outc = layer * vis;

    // A água (translúcida, mais densa logo atrás da espuma).
    const vec3 water = p.q0.rgb;
    const float wa = 0.55 * wet * wet * holder;
    outc = vec4(water * wa, wa) + outc * (1.0 - wa);
    // Por baixo da água a camada pega o tom da água.
    if (wet > 0.0) outc.rgb = mix(outc.rgb, water * outc.a, 0.25 * wet);

    // Espuma: a borda rendada (ruído recorta a frente) e bolhas que ficam.
    if (foamW > 0.0) {
        const vec2 fq = px / max(foamW * 0.45, 3.0);
        const float n1 = sea_noise(fq + vec2(t * 0.6, 0.0), seed + 11u);
        const float n2 = sea_noise(fq * 2.3 + vec2(0.0, t * 0.9), seed + 13u);
        const float lace = n1 * 0.65 + n2 * 0.35;
        const float dd = d + (n1 - 0.5) * foamW * 0.6;   // a frente da espuma não é reta
        const float crest = smoothstep(-texel, texel, dd) * (1.0 - smoothstep(foamW * 0.35, foamW, dd));
        const float trail = step(0.0, d) * (1.0 - smoothstep(foamW, foamW * 2.4, d)) * smoothstep(0.56, 0.66, lace);
        const float fm = clamp(max(crest * smoothstep(0.25, 0.45, lace + 0.15), trail), 0.0, 1.0) * holder;
        const vec3 foam = p.color.rgb * (0.85 + 0.15 * n2);
        outc = vec4(foam * fm, fm) + outc * (1.0 - fm);
    }
    o_color = outc;
}
