#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_stylize.frag
//
//  Lote "AE 2026", estilizar (AeEffects2026Stylize.cpp). u.p[11].w escolhe:
//    0 Relevo · 1 Relevo colorido · 2 Bisel das bordas · 3 Pinceladas ·
//    4 Remoção simples de fio
// =============================================================================
#include "ae_common.glsl"

// --- 0/1 Relevo --------------------------------------------------------------------
// p0: x = direção (rad, y para cima), y = relevo (px), z = contraste, w = misturar com o original
vec4 emboss(vec4 src, bool color) {
    const vec2 px = ae_px();
    const float ang = u.p[0].x;
    const vec2 dir = vec2(cos(ang), -sin(ang)) * max(u.p[0].y, ae_texel_px());
    const vec4 a = ae_decode(ae_at_px(px + dir));   // lado de onde vem a luz
    const vec4 b = ae_decode(ae_at_px(px - dir));
    const vec4 d = ae_decode(src);
    const float k = u.p[0].z * 2.0;
    vec3 c;
    if (color) {
        c = clamp(d.rgb + (a.rgb - b.rgb) * k, 0.0, 1.0);
    } else {
        c = vec3(clamp(0.5 + (ae_luma(a.rgb) - ae_luma(b.rgb)) * k, 0.0, 1.0));
    }
    const vec4 outc = ae_encode(c, d.a);
    return mix(outc, src, clamp(u.p[0].w, 0.0, 1.0));
}

// --- 2 Bisel das bordas ----------------------------------------------------------------------
// p0: x = espessura (fração do menor lado), y = ângulo da luz (rad), z = intensidade; c0 = cor da luz
vec4 bevel_edges(vec4 src) {
    if (src.a <= 0.0) return src;
    const vec2 ls = ae_layer_size();
    const vec2 px = ae_px();
    const float bw = max(u.p[0].x * min(ls.x, ls.y), 1e-3);
    const float dl = px.x, dr = ls.x - px.x, dt = px.y, db = ls.y - px.y;
    const float m = min(min(dl, dr), min(dt, db));
    if (m < 0.0 || m >= bw) return src;
    vec2 n;
    if (m == dl) n = vec2(-1.0, 0.0);
    else if (m == dr) n = vec2(1.0, 0.0);
    else if (m == dt) n = vec2(0.0, -1.0);
    else n = vec2(0.0, 1.0);
    const vec2 light = vec2(cos(u.p[0].y), -sin(u.p[0].y));
    const float shade = dot(n, light) * u.p[0].z * 2.0;
    const vec4 d = ae_decode(src);
    const vec3 lc = clamp(aurea_linear_to_srgb(max(u.c[0].rgb, vec3(0.0))), 0.0, 1.0);
    const vec3 c = shade >= 0.0 ? d.rgb + (1.0 - d.rgb) * lc * shade : d.rgb * (1.0 + shade);
    return ae_encode(c, d.a);
}

// --- 3 Pinceladas --------------------------------------------------------------------------------
// p0: x = ângulo (rad), y = tamanho do pincel (px), z = comprimento (px), w = densidade
// p1: x = aleatoriedade, y = superfície (0 original, 1 transparente, 2 branco, 3 preto), z = misturar
vec4 brush_strokes(vec4 src) {
    const vec2 px = ae_px();
    const float bs = max(u.p[0].y, 0.25);
    const float len = max(u.p[0].z, bs);
    const float cell = max(len * 0.6, bs * 1.5);
    const int perCell = clamp(int(u.p[0].w * 2.0 + 0.5), 1, 4);
    const float rnd = u.p[1].x;
    const vec2 cc = floor(px / cell);
    float best = -1.0;
    vec2 bestCenter = px;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const vec2 cid = cc + vec2(float(dx), float(dy));
            for (int k = 0; k < 4; ++k) {
                if (k >= perCell) break;
                const float fk = float(k) * 5.0;
                const vec2 center = (cid + vec2(ae_hash(cid, 11.0 + fk), ae_hash(cid, 12.0 + fk))) * cell;
                const float ang = u.p[0].x + (ae_hash(cid, 13.0 + fk) - 0.5) * rnd * 0.9;
                const vec2 dir = vec2(cos(ang), -sin(ang));
                const vec2 rel = px - center;
                const float along = dot(rel, dir), across = dot(rel, vec2(-dir.y, dir.x));
                const float halfLen = len * 0.5 * (1.0 + (ae_hash(cid, 14.0 + fk) - 0.5) * rnd * 0.6);
                const float dist = length(vec2(max(abs(along) - halfLen, 0.0), across));
                if (dist <= bs * 0.5) {
                    const float prio = ae_hash(cid, 15.0 + fk);
                    if (prio > best) { best = prio; bestCenter = center; }
                }
            }
        }
    }
    vec4 painted;
    if (best >= 0.0) {
        painted = ae_at_px(bestCenter);
    } else {
        const int surface = int(u.p[1].y + 0.5);
        if (surface == 0) painted = src;
        else if (surface == 1) painted = vec4(0.0);
        else if (surface == 2) painted = vec4(1.0);
        else painted = vec4(0.0, 0.0, 0.0, 1.0);
    }
    return mix(painted, src, clamp(u.p[1].z, 0.0, 1.0));
}

// --- 4 Remoção simples de fio ------------------------------------------------------------------------
// p0 = ponto A (xy) e B (zw), px da camada; p1: x = estilo (0 esmaecer, 1 deslocar,
// 2 deslocar na horizontal), y = espessura (px), z = inclinação, w = mistura espelhada
vec4 wire_removal(vec4 src) {
    const vec2 px = ae_px();
    const vec2 a = u.p[0].xy, b = u.p[0].zw;
    const vec2 ab = b - a;
    const float L2 = max(dot(ab, ab), 1e-6);
    const float t = clamp(dot(px - a, ab) / L2, 0.0, 1.0);
    const vec2 onLine = a + ab * t;
    const float d = length(px - onLine);
    const float th = max(u.p[1].y, 1e-3);
    if (d >= th) return src;
    // Inclinação: quanto da faixa é borda suave (0 = corte seco).
    const float w = 1.0 - smoothstep(th * (1.0 - clamp(u.p[1].z, 0.0, 1.0)), th, d);
    vec2 n = vec2(-ab.y, ab.x) / sqrt(L2);
    const int style = int(u.p[1].x + 0.5);
    vec4 fill;
    if (style == 0) {
        // Esmaecer: a média dos dois lados, logo fora da faixa.
        const float s = dot(px - onLine, n);
        const vec4 c1 = ae_at_px(onLine + n * th * 1.5), c2 = ae_at_px(onLine - n * th * 1.5);
        const float f = clamp(s / (2.0 * th) + 0.5, 0.0, 1.0);
        fill = mix(c2, c1, f);
    } else {
        const vec2 off = style == 1 ? n * th * 2.0 : vec2(th * 2.0, 0.0);
        const vec4 c1 = ae_at_px(px + off), c2 = ae_at_px(px - off);
        fill = mix(c1, c2, clamp(u.p[1].w, 0.0, 1.0) * 0.5);
    }
    return mix(src, fill, w);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const int op = int(u.p[11].w + 0.5);
    if (op == 0) o_color = emboss(src, false);
    else if (op == 1) o_color = emboss(src, true);
    else if (op == 2) o_color = bevel_edges(src);
    else if (op == 3) o_color = brush_strokes(src);
    else o_color = wire_removal(src);
}
