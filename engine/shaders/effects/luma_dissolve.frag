#version 450
// =============================================================================
//  Aurea / shaders / effects / luma_dissolve.frag
//
//  Luma Dissolve: a camada some (ou aparece) pelo valor de um MAPA 0..1 — o
//  brilho dela, um ruído, um degradê linear ou radial, ou o brilho de outra
//  camada. A frente `t` anda de −(suavidade+borda) até 1; o pixel fica se o
//  mapa passa da frente (rampa da suavidade) e, logo depois da frente, a
//  borda pinta a cor da borda (a "queimadura").
//
//  0% = camada exata (o efeito sai da cadeia no C++); 100% = alfa 0.
//
//  p0 = frente t, suavidade, largura da borda, mapa
//  p1 = inverter, escala do ruído (px), semente, ângulo (rad)
//  p2 = centro (px), meia extensão do degradê linear (px), raio do radial (px)
//  p3 = composição ← camada (a.x, a.y, c.x, c.y); p4 = translação xy, região do mapa xy
//  p5 = região do mapa zw, tem mapa (0/1), tudo apagado (0/1)
//  color = cor da borda (linear pré-multiplicada)
// =============================================================================
#include "cc_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // outra camada (px da composição)

float map_value(vec2 px, vec4 src) {
    const int kind = int(p.p0.w + 0.5);
    float m;
    if (kind == 1) {
        // Ruído: manchas na escala pedida, esticadas para cobrir 0..1.
        const float s = max(p.p1.y, 1.0);
        const vec2 q = px / s + vec2(p.p1.z * 17.13, p.p1.z * 31.71);
        m = clamp((aurea_fbm(q, 5, 0.5) - 0.5) * 2.4 + 0.5, 0.0, 1.0);
    } else if (kind == 2) {
        const vec2 dir = vec2(cos(p.p1.w), sin(p.p1.w));
        m = clamp(dot(px - p.p2.xy, dir) / max(2.0 * p.p2.z, 1e-3) + 0.5, 0.0, 1.0);
    } else if (kind == 3) {
        m = clamp(length(px - p.p2.xy) / max(p.p2.w, 1e-3), 0.0, 1.0);
    } else if (kind == 4) {
        if (p.p5.z < 0.5) return 0.0;
        const vec2 comp = vec2(p.p3.x * px.x + p.p3.z * px.y + p.p4.x, p.p3.y * px.x + p.p3.w * px.y + p.p4.y);
        const vec4 c = texture(u_tex1, (comp - p.p4.zw) / max(p.p5.xy, vec2(1e-6)));
        m = aurea_linear_to_srgb(clamp(aurea_luma(max(c.rgb, vec3(0.0))), 0.0, 1.0));
    } else {
        // O brilho da própria camada (cor sem o alfa), na escala que o olho vê.
        const vec3 rgb = src.a > 1e-4 ? src.rgb / src.a : vec3(0.0);
        m = aurea_linear_to_srgb(clamp(aurea_luma(max(rgb, vec3(0.0))), 0.0, 1.0));
    }
    return p.p1.x > 0.5 ? 1.0 - m : m;
}

void main() {
    const vec2 px = cc_px();
    const vec4 src = cc_at(px);
    if (p.p5.w > 0.5) { o_color = vec4(0.0); return; }
    const float m = map_value(px, src);
    const float t = p.p0.x;
    const float s = max(p.p0.y, 1e-4);
    const float w = max(p.p0.z, 0.0);
    // Fica o que passa da frente (rampa de largura `s`).
    const float keep = smoothstep(t, t + s, m);
    vec3 rgb = src.rgb;
    if (w > 0.0) {
        // Borda: logo depois da rampa, some suave até `w` adiante.
        const float band = 1.0 - smoothstep(t + s, t + s + w, m);
        const float k = clamp(band, 0.0, 1.0) * p.color.a;
        rgb = mix(rgb, p.color.rgb / max(p.color.a, 1e-4) * src.a, k);
    }
    o_color = vec4(rgb, src.a) * keep;
}
