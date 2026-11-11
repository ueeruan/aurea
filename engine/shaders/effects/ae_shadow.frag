#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_shadow.frag
//
//  Lote "AE 2026", sombras. u.p[11].w escolhe:
//    0 Sombra radial — a camada projetada a partir de um ponto de luz numa
//      superfície atrás dela (escala k em volta da luz). p0: xy = luz (px da
//      camada), z = k, w = suavidade (px); p1: x = opacidade, y = renderizar
//      (0 normal, 1 borda de vidro), z = influência da cor, w = só a sombra;
//      c0 = cor da sombra. u_tex1 = a própria entrada (borda transparente).
//    1 Sombra interna — o alfa invertido, desfocado (u_tex1, região p2) e
//      deslocado (p0.xy px), dentro da camada. p0: z = opacidade, w = engasgar;
//      c0 = cor.
// =============================================================================
#include "ae_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;

vec4 radial_shadow() {
    const vec2 px = ae_px();
    const vec4 src = ae_at_px(px);
    const vec2 L = u.p[0].xy;
    const float k = max(u.p[0].z, 1e-3);
    const vec2 S = L + (px - L) / k;
    const float soft = u.p[0].w;
    // Alfa da silhueta projetada; suavidade = média num disco (12 + centro).
    vec4 sample0 = ae_at_px(S);
    float a = sample0.a;
    vec4 colSum = sample0;
    if (soft > 0.0) {
        float wsum = 1.0;
        for (int i = 0; i < 12; ++i) {
            const float ang = float(i) * 2.39996323;
            const float r = soft * sqrt((float(i) + 0.5) / 12.0);
            const vec4 s = ae_at_px(S + vec2(cos(ang), sin(ang)) * r);
            a += s.a;
            colSum += s;
            wsum += 1.0;
        }
        a /= wsum;
        colSum /= wsum;
    }
    const vec3 srcColor = colSum.a > 1e-5 ? colSum.rgb / colSum.a : vec3(0.0);
    vec3 col = mix(max(u.c[0].rgb, vec3(0.0)), srcColor, clamp(u.p[1].z, 0.0, 1.0));
    float alpha = a * clamp(u.p[1].x, 0.0, 1.0);
    if (u.p[1].y > 0.5) {
        // Borda de vidro: a luz passa pela camada e tinge a sombra com a cor dela,
        // mais forte onde a silhueta acaba.
        col = mix(col, srcColor, 0.7);
        alpha *= clamp(0.35 + 2.5 * abs(sample0.a - a) + 0.4 * (1.0 - sample0.a), 0.0, 1.0);
    }
    if (u.c[0].a <= 0.0 && u.p[1].z <= 0.0) alpha = 0.0;
    const vec4 shadow = vec4(col * alpha, alpha);
    if (u.p[1].w > 0.5) return shadow;
    return src + shadow * (1.0 - src.a);
}

vec4 inner_shadow() {
    const vec2 px = ae_px();
    const vec4 src = texture(u_tex0, ae_in_uv());
    if (src.a <= 0.0) return src;
    const vec2 q = px - u.p[0].xy;
    const vec2 buv = (q - u.p[2].xy) / max(u.p[2].zw, vec2(1e-3));
    const float blurredA = texture(u_tex1, buv).a;
    const float choke = clamp(u.p[0].w, 0.0, 0.99);
    const float inv = clamp((1.0 - blurredA - choke) / (1.0 - choke), 0.0, 1.0);
    const float s = inv * clamp(u.p[0].z, 0.0, 1.0) * clamp(u.c[0].a, 0.0, 1.0);
    return vec4(mix(src.rgb, max(u.c[0].rgb, vec3(0.0)) * src.a, s), src.a);
}

void main() {
    if (u.p[11].w < 0.5) o_color = radial_shadow();
    else o_color = inner_shadow();
}
