#version 450
// =============================================================================
//  Aurea / shaders / effects / water_reflection.frag
//
//  Reflexo na água (0.4.5): abaixo da linha do horizonte a camada aparece
//  espelhada, como numa lâmina d'água. Três ondas planas somadas (direção,
//  frequência e velocidade do usuário, as outras duas giradas e mais curtas)
//  deslocam a leitura do espelho; a perspectiva encolhe as ondas perto do
//  horizonte (longe) e as abre perto de quem olha. O reflexo desfoca com a
//  distância, ganha o tom da água, desvanece e vai POR CIMA do que a camada já
//  tinha abaixo da linha (numa foto inteira, a água de verdade; num logo, o
//  vazio embaixo dele).
//
//  Tudo em px da camada (cc_common.glsl): prévia e export desenham igual.
//  p0 = horizonte (y px), comprimento (px), opacidade, desvanecimento
//  p1 = ondulação (px), número de onda (rad/px), fase do tempo (rad), perspectiva
//  p2 = direção (x, y), desfoque (px), tom (0..1)
//  color = cor da água (linear)
// =============================================================================
#include "cc_common.glsl"

vec4 mirror_at(vec2 px) {
    // Nunca lê abaixo do horizonte: o espelho só tem o que está acima.
    px.y = min(px.y, p.p0.x - 0.5);
    return cc_at(px);
}

void main() {
    const vec2 px = cc_px();
    const vec4 base = cc_at(px);
    const float d = px.y - p.p0.x;
    const float L = max(p.p0.y, 1.0);
    if (d <= 0.0 || d >= L || p.p0.z <= 0.0) { o_color = base; return; }

    const float t = d / L;                         // 0 no horizonte, 1 no fim do reflexo
    // Perspectiva: perto do horizonte as ondas são pequenas e apertadas.
    const float sc = mix(1.0, 0.12 + 0.88 * t, clamp(p.p1.w, 0.0, 1.0));
    const vec2 q = vec2((px.x - cc_box_center().x) / sc, d / sc);

    const vec2 dir = p.p2.xy;
    const vec2 side = vec2(-dir.y, dir.x);
    const float k = p.p1.y, ph = p.p1.z;
    const float a1 = k * dot(q, dir) - ph;
    const float a2 = 1.73 * k * dot(q, normalize(dir + 0.6 * side)) - 1.31 * ph + 1.3;
    const float a3 = 0.61 * k * dot(q, normalize(dir - 0.8 * side)) - 0.77 * ph + 4.1;
    const float wy = (sin(a1) + 0.5 * sin(a2) + 0.35 * sin(a3)) / 1.85;
    const float wx = (cos(a1) + 0.4 * cos(a2)) / 1.4;
    const float amp = p.p1.x * sc;
    const vec2 src = vec2(px.x + wx * amp * 0.6, p.p0.x - d + wy * amp);

    // Desfoque que cresce com a distância (5 amostras, alongadas na vertical).
    const float r = p.p2.z * (0.25 + t);
    vec4 refl = mirror_at(src);
    if (r > 0.25 * cc_texel()) {
        refl += mirror_at(src + vec2(r, 0.6 * r));
        refl += mirror_at(src + vec2(-r, -0.6 * r));
        refl += mirror_at(src + vec2(0.45 * r, -1.4 * r));
        refl += mirror_at(src + vec2(-0.45 * r, 1.4 * r));
        refl *= 0.2;
    }

    // Tom da água: multiplica pela cor normalizada (mantém o brilho).
    const vec3 tint = p.color.rgb / max(aurea_luma(p.color.rgb), 0.05);
    refl.rgb = mix(refl.rgb, refl.rgb * tint, clamp(p.p2.w, 0.0, 1.0));

    const float fade = 1.0 - clamp(p.p0.w, 0.0, 1.0) * smoothstep(0.0, 1.0, t);
    // Meio texel de rampa na ponta do reflexo: sem degrau no fim do comprimento.
    const float tail = clamp((L - d) / max(cc_texel(), 1e-3), 0.0, 1.0);
    refl *= clamp(p.p0.z, 0.0, 1.0) * fade * tail;
    o_color = refl + base * (1.0 - refl.a);
}
