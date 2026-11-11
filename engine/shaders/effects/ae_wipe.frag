#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_wipe.frag
//
//  Lote "AE 2026", transições que apagam a camada (o que some fica
//  transparente e mostra o que está embaixo). u.p[11].w escolhe:
//    0 Limpar em grade — ladrilhos que somem com a forma escolhida, os do
//      centro primeiro. p0: x = conclusão, yz = centro (px), w = rotação (rad);
//      p1: x = borda (px), y = ladrilhos na largura, z = forma (0 portas,
//      1 radial, 2 retangular), w = inverter.
//    1 Limpar com luz — uma forma que cresce do centro com uma borda de luz.
//      p0: x = conclusão, yz = centro, w = direção (rad); p1: x = intensidade,
//      y = forma (0 portas, 1 redondo, 2 quadrado), z = cor da origem,
//      w = inverter; c0 = cor da luz.
//    2 Varredura em linhas — faixas paralelas que se apagam em escada.
//      p0: x = conclusão, y = direção (rad), z = espessura (px), w = inclinação
//      (px); p1: x = inverter direção.
// =============================================================================
#include "ae_common.glsl"

vec2 rot(vec2 v, float a) {
    const float c = cos(a), s = sin(a);
    return vec2(c * v.x + s * v.y, -s * v.x + c * v.y);
}

vec4 grid_wipe(vec4 src) {
    const float done = clamp(u.p[0].x, 0.0, 1.0);
    if (done <= 0.0) return src;
    const vec2 ls = ae_layer_size();
    const vec2 rel = rot(ae_px() - u.p[0].yz, u.p[0].w);
    const float cell = ls.x / max(u.p[1].y, 1.0);
    const vec2 id = floor(rel / cell + 0.5);
    const vec2 q = rel / cell - id;                                    // −0,5..0,5
    const float maxD = length(ls) / cell * 0.5 + 1.0;
    const float delay = clamp(length(id) / maxD, 0.0, 1.0);
    const float lp = clamp(done * 1.6 - delay * 0.6, 0.0, 1.0);
    const int shape = int(u.p[1].z + 0.5);
    float s;
    if (shape == 0) s = abs(q.x) * 2.0;
    else if (shape == 1) s = length(q) / 0.70710678;
    else s = max(abs(q.x), abs(q.y)) * 2.0;
    const float soft = max(u.p[1].x, ae_texel_px()) / cell;
    float hidden = u.p[1].w > 0.5 ? smoothstep(1.0 - lp - soft, 1.0 - lp, s) : 1.0 - smoothstep(lp - soft, lp, s);
    if (lp >= 1.0) hidden = 1.0;
    return src * (1.0 - hidden);
}

vec4 light_wipe(vec4 src) {
    const float done = clamp(u.p[0].x, 0.0, 1.0);
    if (done <= 0.0) return src;
    const vec2 ls = ae_layer_size();
    const vec2 rel = rot(ae_px() - u.p[0].yz, u.p[0].w);
    const int shape = int(u.p[1].y + 0.5);
    float d;
    if (shape == 0) d = abs(rel.x);
    else if (shape == 1) d = length(rel);
    else d = max(abs(rel.x), abs(rel.y));
    const vec2 far = max(abs(u.p[0].yz), abs(ls - u.p[0].yz));
    const float maxR = (shape == 1 ? length(far) : max(far.x, far.y) * 1.4142) * 1.05;
    const float edge = done * maxR;
    const float aa = ae_texel_px();
    float hidden = u.p[1].w > 0.5 ? smoothstep(maxR - edge - aa, maxR - edge, d) : 1.0 - smoothstep(edge - aa, edge, d);
    if (done >= 1.0) hidden = 1.0;
    const float edgeAt = u.p[1].w > 0.5 ? maxR - edge : edge;
    const float w = max(maxR * 0.06, 2.0);
    const float glow = exp(-pow((d - edgeAt) / w, 2.0)) * u.p[1].x * (1.0 - done);
    const vec4 st = unpremultiply(src);
    const vec3 lc = u.p[1].z > 0.5 ? st.rgb : max(u.c[0].rgb, vec3(0.0));
    const float vis = 1.0 - hidden;
    return vec4((src.rgb + lc * glow * src.a) * vis, src.a * vis);
}

vec4 line_sweep(vec4 src) {
    const float done = clamp(u.p[0].x, 0.0, 1.0);
    if (done <= 0.0) return src;
    const vec2 ls = ae_layer_size();
    const vec2 rel = rot(ae_px() - ls * 0.5, u.p[0].y);
    const float Lh = length(ls) * 0.5;
    const float t = max(u.p[0].z, 0.5);
    const float slant = u.p[0].w;
    const float band = floor((rel.y + Lh) / t);
    const float bands = max(ceil(2.0 * Lh / t), 1.0);
    const float fb = band / bands;
    const float pos = mix(-Lh - slant, Lh, done) + slant * fb;
    const float s = u.p[1].x > 0.5 ? -rel.x : rel.x;
    const float aa = ae_texel_px();
    float hidden = 1.0 - smoothstep(pos - aa, pos, s);
    if (done >= 1.0) hidden = 1.0;
    return src * (1.0 - hidden);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const int op = int(u.p[11].w + 0.5);
    if (op == 0) o_color = grid_wipe(src);
    else if (op == 1) o_color = light_wipe(src);
    else o_color = line_sweep(src);
}
