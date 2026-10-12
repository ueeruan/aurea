#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_vector_blur.frag
//
//  Desfoque vetorial (o comportamento do CC Vector Blur): o mapa é a própria
//  camada (luminância, um canal ou o alfa). O gradiente dele (Sobel, com o
//  passo de "Suavidade do mapa") dá a direção de cada pixel; a imagem é
//  borrada ao longo dessa direção, girada pelo "Ângulo":
//    0 natural       — o comprimento segue a força do gradiente;
//    1 constante     — todo pixel com gradiente borra o comprimento inteiro;
//    2 perpendicular — natural, mas atravessando o gradiente (redemoinhos).
//  "Suavidade das cristas" amortece os gradientes fracos (as cristas e os
//  vales do mapa), onde a direção oscila.
//
//  p0 = intensidade (px), ângulo (rad), suavidade das cristas, passo do mapa (px);
//  p1 = tipo, propriedade, amostras.
// =============================================================================
#include "cc_common.glsl"

float property(vec4 c, int k) {
    if (k == 1) return c.r;
    if (k == 2) return c.g;
    if (k == 3) return c.b;
    if (k == 4) return c.a;
    return aurea_luma(c.rgb);
}

void main() {
    const vec2 px = cc_px();
    const int kind = int(p.p1.x + 0.5);
    const int prop = int(p.p1.y + 0.5);
    const float e = max(p.p0.w, cc_texel());
    // Sobel 3×3 no passo e.
    const float tl = property(cc_at(px + vec2(-e, -e)), prop), tc = property(cc_at(px + vec2(0.0, -e)), prop);
    const float tr = property(cc_at(px + vec2(e, -e)), prop), ml = property(cc_at(px + vec2(-e, 0.0)), prop);
    const float mr = property(cc_at(px + vec2(e, 0.0)), prop), bl = property(cc_at(px + vec2(-e, e)), prop);
    const float bc = property(cc_at(px + vec2(0.0, e)), prop), br = property(cc_at(px + vec2(e, e)), prop);
    const vec2 g = vec2((tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl), (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr)) * 0.125;
    const float mag = length(g);
    if (mag < 1e-5) { o_color = cc_at(px); return; }
    vec2 dir = g / mag;
    if (kind == 2) dir = vec2(-dir.y, dir.x);
    dir = cc_rot(dir, p.p0.y);
    const float strength = kind == 1 ? 1.0 : clamp(mag * 4.0, 0.0, 1.0);
    const float ridge = mag / (mag + max(p.p0.z, 0.0) * 0.25 + 1e-4);
    const float len = p.p0.x * strength * ridge;
    const int n = clamp(int(p.p1.z + 0.5), 2, 32);
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 32; ++i) {
        if (i >= n) break;
        sum += cc_at(px + dir * (len * ((float(i) + 0.5) / float(n) - 0.5)));
    }
    o_color = sum / float(n);
}
