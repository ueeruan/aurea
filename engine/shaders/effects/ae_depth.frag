#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_depth.frag
//
//  Lote "AE 2026", sobre o mapa de profundidade da IA (u_tex1: disparidade
//  256×256 da fonte, maior = mais perto — o mesmo contrato de depth_map.frag).
//  u.p[8]: x = 1 há mapa, y = 1 mapa de outra camada (ajuste), zw = limites
//  (longe, perto); p[9]/p[10] = as linhas do ponto da camada → px da fonte do
//  mapa (camada de ajuste). u.p[11].w escolhe:
//    0 Fosco por profundidade — p0: x = profundidade (0 longe..1 perto), y = difusão, z = inverter
//    1 Neblina 3D — p0: x = início, y = fim (distância 0 perto..1 longe), z = opacidade,
//      w = densidade; p1.x = fundo com neblina; c0 = cor da neblina
// =============================================================================
#include "ae_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;

float nearness() {
    vec2 point = ae_px();
    vec2 size = ae_layer_size();
    if (u.p[8].y > 0.5) {
        point = vec2(dot(u.p[9].xy, point) + u.p[10].z, dot(u.p[9].zw, point) + u.p[10].w);
        size = max(u.p[10].xy, vec2(1.0));
    }
    const float raw = texture(u_tex1, clamp(point / size, vec2(0.0), vec2(1.0))).r;
    return clamp((raw - u.p[8].z) / max(u.p[8].w - u.p[8].z, 1e-5), 0.0, 1.0);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    if (u.p[8].x < 0.5) { o_color = src; return; }
    const float d = nearness();
    if (u.p[11].w < 0.5) {
        const float f = max(u.p[0].y, 1e-3);
        float keep = smoothstep(u.p[0].x - 0.5 * f, u.p[0].x + 0.5 * f, d);
        if (u.p[0].z > 0.5) keep = 1.0 - keep;
        o_color = src * keep;
        return;
    }
    const float dist = 1.0 - d;
    const float span = u.p[0].y - u.p[0].x;
    float fog = abs(span) < 1e-4 ? step(u.p[0].x, dist) : clamp((dist - u.p[0].x) / span, 0.0, 1.0);
    fog = pow(fog, mix(2.0, 0.4, clamp(u.p[0].w, 0.0, 1.0))) * clamp(u.p[0].z, 0.0, 1.0);
    const vec3 fc = max(u.c[0].rgb, vec3(0.0));
    vec4 outc = vec4(mix(src.rgb, fc * src.a, fog), src.a);
    if (u.p[1].x > 0.5) {
        // Fundo com neblina: onde a camada é transparente, a neblina toda.
        const float bg = clamp(u.p[0].z, 0.0, 1.0) * (1.0 - src.a);
        outc += vec4(fc * bg, bg);
    }
    o_color = outc;
}
