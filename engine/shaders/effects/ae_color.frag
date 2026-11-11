#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_color.frag
//
//  Lote "AE 2026", cor e canais (AeEffects2026Color.cpp). Uma operação por
//  efeito, escolhida por u.p[11].w (contrato `AeOp` no C++):
//    0 Unmult · 1 Manter cor · 2 Mudar cor · 3 Mudar para cor · 4 Filtro de
//    foto · 5 Preto e branco · 6 Cor seletiva · 7 Níveis do alfa · 8 Remover
//    fosco de cor · 9 Aritmética · 10 Faixa de cor · 11 Luz estroboscópica ·
//    12 Ruído HLS · 13 Ruído alfa
//  Entrada e saída no espaço de trabalho (linear, pré-multiplicado); quem
//  julga cor converte para o valor codificado (sRGB) e volta.
// =============================================================================
#include "ae_common.glsl"

// --- 0 Unmult: o canal mais forte vira alfa ---------------------------------------
vec4 op_unmult(vec4 src) {
    const vec4 s = unpremultiply(src);
    const vec3 lin = max(s.rgb, vec3(0.0));
    const bool white = u.p[0].x > 0.5;
    const float raw = white ? 1.0 - min(lin.r, min(lin.g, lin.b)) : max(lin.r, max(lin.g, lin.b));
    const float black = u.p[0].y, whiteLevel = max(u.p[0].z, black + 1e-3);
    float a = clamp((raw - black) / (whiteLevel - black), 0.0, 1.0);
    a = mix(a, smoothstep(0.0, 1.0, a), clamp(u.p[0].w, 0.0, 1.0));
    vec3 col = lin;
    if (u.p[1].x > 0.5) {
        // C = a·F + (1 − a)·fundo  ⇒  F = (C − (1 − a)·fundo) / a
        if (white) col = a > 1e-5 ? (lin - vec3(1.0 - a)) / a : vec3(1.0);
        else       col = a > 1e-5 ? lin / a : vec3(0.0);
    }
    col = u.p[1].y > 0.5 ? clamp(col, 0.0, 1.0) : max(col, vec3(0.0));
    const float outA = a * clamp(s.a, 0.0, 1.0);
    return vec4(col * outA, outA);
}

// --- distância de cor (Manter / Mudar cor) ----------------------------------------------
vec2 ae_chroma(vec3 c) {
    return vec2(dot(c, vec3(-0.1146, -0.3854, 0.5)), dot(c, vec3(0.5, -0.4542, -0.0458)));
}
/// 0 = mesma cor, 1 = o mais longe. `mode`: 0 RGB, 1 matiz, 2 croma.
float color_distance(vec3 c, vec3 key, int mode) {
    if (mode == 0) return length(c - key) / 1.7320508;
    if (mode == 2) return clamp(length(ae_chroma(c) - ae_chroma(key)) / 0.7071, 0.0, 1.0);
    const vec3 h = ae_rgb2hsl(c), hk = ae_rgb2hsl(key);
    const float dh = ae_hue_dist(h.x, hk.x) * 2.0;
    // Cinza não tem matiz: quanto menos saturado, mais longe de qualquer cor.
    return mix(1.0, dh, smoothstep(0.02, 0.15, h.y));
}
float match_mask(float dist, float tol, float soft) {
    return 1.0 - smoothstep(tol, tol + max(soft, 1e-4), dist);
}
vec3 key_color(vec4 c) { return clamp(aurea_linear_to_srgb(max(c.rgb, vec3(0.0))), 0.0, 1.0); }

// --- 1 Manter cor ------------------------------------------------------------------------
vec4 op_leave(vec4 src) {
    const vec4 d = ae_decode(src);
    const float keep = match_mask(color_distance(d.rgb, key_color(u.c[0]), int(u.p[0].w + 0.5)), u.p[0].y, u.p[0].z);
    const float g = aurea_linear_to_srgb(luminance709(aurea_srgb_to_linear(d.rgb)));
    const vec3 c = mix(d.rgb, vec3(g), clamp(u.p[0].x, 0.0, 1.0) * (1.0 - keep));
    return ae_encode(c, d.a);
}

// --- 2 Mudar cor ----------------------------------------------------------------------------
vec4 op_change(vec4 src) {
    const vec4 d = ae_decode(src);
    float m = match_mask(color_distance(d.rgb, key_color(u.c[0]), int(u.p[1].z + 0.5)), u.p[1].x, u.p[1].y);
    if (u.p[1].w > 0.5) m = 1.0 - m;
    if (u.p[0].x > 0.5) return ae_encode(vec3(m), d.a);
    vec3 hsl = ae_rgb2hsl(d.rgb);
    hsl.x = fract(hsl.x + u.p[0].y);
    const float lt = u.p[0].z, st = u.p[0].w;
    hsl.z = lt >= 0.0 ? hsl.z + (1.0 - hsl.z) * lt : hsl.z * (1.0 + lt);
    hsl.y = st >= 0.0 ? hsl.y + (1.0 - hsl.y) * st : hsl.y * (1.0 + st);
    return ae_encode(mix(d.rgb, ae_hsl2rgb(hsl), m), d.a);
}

// --- 3 Mudar para cor -------------------------------------------------------------------------
vec4 op_change_to(vec4 src) {
    const vec4 d = ae_decode(src);
    const vec3 hsl = ae_rgb2hsl(d.rgb);
    const vec3 hf = ae_rgb2hsl(key_color(u.c[0])), ht = ae_rgb2hsl(key_color(u.c[1]));
    const float dh = ae_hue_dist(hsl.x, hf.x) * 2.0;
    const float dl = abs(hsl.z - hf.z), ds = abs(hsl.y - hf.y);
    const float dist = max(dh / max(u.p[0].z, 1e-4), max(dl / max(u.p[0].w, 1e-4), ds / max(u.p[1].x, 1e-4)));
    float m = 1.0 - smoothstep(1.0, 1.0 + 2.0 * u.p[1].y + 1e-4, dist);
    m *= smoothstep(0.02, 0.1, hsl.y);   // cinza não tem matiz a trocar
    if (u.p[1].z > 0.5) return ae_encode(vec3(m), d.a);
    const int mode = int(u.p[0].x + 0.5);
    const bool light = mode == 1 || mode == 2, sat = mode == 2 || mode == 3;
    vec3 t = hsl;
    if (u.p[0].y < 0.5) {
        t.x = ht.x;
        if (light) t.z = ht.z;
        if (sat) t.y = ht.y;
    } else {
        t.x = fract(hsl.x + ht.x - hf.x);
        if (light) t.z = clamp(hsl.z + ht.z - hf.z, 0.0, 1.0);
        if (sat) t.y = clamp(hsl.y + ht.y - hf.y, 0.0, 1.0);
    }
    return ae_encode(mix(d.rgb, ae_hsl2rgb(t), m), d.a);
}

// --- 4 Filtro de foto (linear) -------------------------------------------------------------------
vec4 op_photo_filter(vec4 src) {
    const vec4 s = unpremultiply(src);
    const vec3 lin = max(s.rgb, vec3(0.0));
    const vec3 f = max(u.c[0].rgb, vec3(0.0));
    const bool preserve = u.p[0].y > 0.5;
    const float lf = luminance709(f);
    const vec3 filtered = preserve ? lin * (lf > 1e-4 ? f / lf : vec3(1.0)) : lin * f;
    vec3 c = mix(lin, filtered, clamp(u.p[0].x, 0.0, 1.0));
    if (preserve) {
        const float l0 = luminance709(lin), l1 = luminance709(c);
        if (l1 > 1e-6) c *= l0 / l1;
    }
    return vec4(max(c, vec3(0.0)) * s.a, s.a);
}

// --- 5 Preto e branco ---------------------------------------------------------------------------------
vec4 op_black_white(vec4 src) {
    const vec4 d = ae_decode(src);
    const vec3 c = d.rgb;
    const float reds = u.p[0].x, yellows = u.p[0].y, greens = u.p[0].z, cyans = u.p[0].w;
    const float blues = u.p[1].x, magentas = u.p[1].y;
    float mx, md, mn, wp, ws;
    if (c.r >= c.g && c.r >= c.b) {
        mx = c.r; md = max(c.g, c.b); mn = min(c.g, c.b);
        wp = reds; ws = c.g >= c.b ? yellows : magentas;
    } else if (c.g >= c.b) {
        mx = c.g; md = max(c.r, c.b); mn = min(c.r, c.b);
        wp = greens; ws = c.r >= c.b ? yellows : cyans;
    } else {
        mx = c.b; md = max(c.r, c.g); mn = min(c.r, c.g);
        wp = blues; ws = c.g >= c.r ? cyans : magentas;
    }
    const float gray = clamp(mn + (md - mn) * ws + (mx - md) * wp, 0.0, 1.0);
    vec3 outc = vec3(gray);
    if (u.p[1].z > 0.5) {
        const vec3 th = ae_rgb2hsl(key_color(u.c[0]));
        outc = ae_hsl2rgb(vec3(th.x, th.y, gray));
    }
    return ae_encode(outc, d.a);
}

// --- 6 Cor seletiva ------------------------------------------------------------------------------------
vec4 op_selective(vec4 src) {
    const vec4 d = ae_decode(src);
    const vec3 c = d.rgb;
    const float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
    const float md = clamp(c.r + c.g + c.b - mx - mn, mn, mx);
    float w[9];
    w[0] = (c.r >= c.g && c.r >= c.b) ? mx - md : 0.0;                   // vermelhos
    w[1] = (c.b <= c.r && c.b <= c.g) ? min(c.r, c.g) - c.b : 0.0;       // amarelos
    w[2] = (c.g > c.r && c.g >= c.b) ? mx - md : 0.0;                    // verdes
    w[3] = (c.r <= c.g && c.r <= c.b) ? min(c.g, c.b) - c.r : 0.0;       // cianos
    w[4] = (c.b > c.r && c.b > c.g) ? mx - md : 0.0;                     // azuis
    w[5] = (c.g <= c.r && c.g <= c.b) ? min(c.r, c.b) - c.g : 0.0;       // magentas
    w[6] = mn > 0.5 ? (mn - 0.5) * 2.0 : 0.0;                            // brancos
    w[7] = clamp(1.0 - (abs(mx - 0.5) + abs(mn - 0.5)), 0.0, 1.0);       // neutros
    w[8] = mx < 0.5 ? (0.5 - mx) * 2.0 : 0.0;                            // pretos
    const bool relative = u.p[9].x < 0.5;
    const vec3 ink = 1.0 - c;                                            // C, M, Y
    vec3 delta = vec3(0.0);
    for (int i = 0; i < 9; ++i) {
        if (w[i] <= 0.0) continue;
        const vec4 a = u.p[i];
        const vec3 amt = a.xyz + vec3(a.w);
        delta += w[i] * amt * (relative ? ink : vec3(1.0));
    }
    const vec3 outc = 1.0 - clamp(ink + delta, 0.0, 1.0);
    return ae_encode(outc, d.a);
}

// --- 7 Níveis do alfa -------------------------------------------------------------------------------------
vec4 op_alpha_levels(vec4 src) {
    const float a = clamp(src.a, 0.0, 1.0);
    const float inB = u.p[0].x, inW = u.p[0].y;
    const float span = inW - inB;
    float t = abs(span) < 1e-5 ? step(inB, a) : clamp((a - inB) / span, 0.0, 1.0);
    t = pow(t, 1.0 / max(u.p[0].z, 0.01));
    const float a2 = clamp(mix(u.p[0].w, u.p[1].x, t), 0.0, 1.0);
    const vec3 straight = a > 1e-6 ? src.rgb / a : vec3(0.0);
    return vec4(straight * a2, a2);
}

// --- 8 Remover fosco de cor ------------------------------------------------------------------------------------
vec4 op_remove_matting(vec4 src) {
    const float a = clamp(src.a, 0.0, 1.0);
    if (a <= 1e-5) return vec4(0.0);
    const vec3 C = src.rgb / a;
    vec3 F = (C - (1.0 - a) * max(u.c[0].rgb, vec3(0.0))) / a;
    F = u.p[0].x > 0.5 ? clamp(F, 0.0, 1.0) : max(F, vec3(0.0));
    return vec4(F * a, a);
}

// --- 9 Aritmética (valores de 8 bits) ------------------------------------------------------------------------------
vec4 op_arithmetic(vec4 src) {
    const vec4 d = ae_decode(src);
    const vec3 v = floor(d.rgb * 255.0 + 0.5);
    const vec3 k = floor(clamp(u.p[0].yzw, 0.0, 255.0) + 0.5);
    const uvec3 vi = uvec3(v), ki = uvec3(k);
    const int op = int(u.p[0].x + 0.5);
    vec3 r = v;
    if (op == 0) r = vec3(vi & ki);
    else if (op == 1) r = vec3(vi | ki);
    else if (op == 2) r = vec3(vi ^ ki);
    else if (op == 3) r = v + k;
    else if (op == 4) r = v - k;
    else if (op == 5) r = abs(v - k);
    else if (op == 6) r = min(v, k);
    else if (op == 7) r = max(v, k);
    else if (op == 8) r = mix(v, vec3(0.0), greaterThan(v, k));
    else if (op == 9) r = mix(v, vec3(0.0), lessThan(v, k));
    else if (op == 10) r = mix(vec3(0.0), vec3(255.0), greaterThanEqual(v, k));
    else if (op == 11) r = v * k / 255.0;
    else if (op == 12) r = 255.0 - (255.0 - v) * (255.0 - k) / 255.0;
    r = u.p[1].x > 0.5 ? clamp(r, 0.0, 255.0) : mod(r, 256.0);
    return ae_encode(r / 255.0, d.a);
}

// --- 10 Faixa de cor -------------------------------------------------------------------------------------------------
vec3 ae_lab01(vec3 lin) {
    const vec3 xyz = vec3(dot(lin, vec3(0.4124, 0.3576, 0.1805)), dot(lin, vec3(0.2126, 0.7152, 0.0722)),
                          dot(lin, vec3(0.0193, 0.1192, 0.9505))) / vec3(0.95047, 1.0, 1.08883);
    const vec3 f = mix(pow(max(xyz, vec3(0.0)), vec3(1.0 / 3.0)), xyz * 7.787 + 16.0 / 116.0, lessThanEqual(xyz, vec3(0.008856)));
    const float L = 116.0 * f.y - 16.0, A = 500.0 * (f.x - f.y), B = 200.0 * (f.y - f.z);
    return vec3(L / 100.0, (A + 128.0) / 255.0, (B + 128.0) / 255.0);
}
vec4 op_color_range(vec4 src) {
    const vec4 d = ae_decode(src);
    const int space = int(u.p[0].x + 0.5);
    vec3 q = d.rgb;
    if (space == 0) q = ae_lab01(aurea_srgb_to_linear(d.rgb));
    else if (space == 1) q = vec3(dot(d.rgb, vec3(0.299, 0.587, 0.114)), dot(d.rgb, vec3(-0.169, -0.331, 0.5)) + 0.5,
                                  dot(d.rgb, vec3(0.5, -0.419, -0.081)) + 0.5);
    const vec3 lo = vec3(u.p[1].x, u.p[1].z, u.p[2].x), hi = vec3(u.p[1].y, u.p[1].w, u.p[2].y);
    const vec3 outside = max(max(lo - q, q - hi), vec3(0.0));
    const float inside = 1.0 - smoothstep(0.0, u.p[0].y + 1e-4, length(outside));
    return src * (1.0 - inside);
}

// --- 11 Luz estroboscópica -------------------------------------------------------------------------------------------
vec4 op_strobe(vec4 src) {
    if (u.p[0].w < 0.5) return src;
    const float keep = clamp(u.p[0].z, 0.0, 1.0);
    if (u.p[0].x > 0.5) return src * keep;   // torna a camada transparente
    const vec4 d = ae_decode(src);
    const vec3 s = key_color(u.c[0]);
    const int op = int(u.p[0].y + 0.5);
    vec3 r = s;
    if (op == 1) r = d.rgb + s;
    else if (op == 2) r = d.rgb - s;
    else if (op == 3) r = d.rgb * s;
    else if (op == 4) r = abs(d.rgb - s);
    else if (op == 5) r = min(d.rgb, s);
    else if (op == 6) r = max(d.rgb, s);
    else if (op == 7) r = 1.0 - (1.0 - d.rgb) * (1.0 - s);
    return ae_encode(mix(clamp(r, 0.0, 1.0), d.rgb, keep), d.a);
}

// --- 12 Ruído HLS ------------------------------------------------------------------------------------------------------
float noise_slice(vec2 cell, float z, float salt, bool smoothNoise) {
    const vec2 off = vec2(z * 157.0 + salt * 311.0, z * 61.0 - salt * 97.0);
    if (!smoothNoise) return ae_hash(floor(cell) + off, salt);
    const vec2 i = floor(cell), f = fract(cell), w = f * f * (3.0 - 2.0 * f);
    const float a = ae_hash(i + off, salt), b = ae_hash(i + vec2(1.0, 0.0) + off, salt);
    const float c = ae_hash(i + vec2(0.0, 1.0) + off, salt), e = ae_hash(i + vec2(1.0) + off, salt);
    return mix(mix(a, b, w.x), mix(c, e, w.x), w.y);
}
float noise_value(vec2 cell, float slice, float salt, int type, bool animated) {
    const float z0 = floor(slice);
    float n = noise_slice(cell, z0, salt, type == 2);
    if (animated) n = mix(n, noise_slice(cell, z0 + 1.0, salt, type == 2), smoothstep(0.0, 1.0, fract(slice)));
    float s = n * 2.0 - 1.0;
    if (type == 1) s = sign(s) * s * s;
    return s;
}
vec4 op_noise_hls(vec4 src) {
    const vec4 d = ae_decode(src);
    const int type = int(u.p[0].x + 0.5);
    const vec2 cell = ae_px() / max(u.p[1].x, 0.25);
    const float slice = u.p[1].y;
    vec3 hsl = ae_rgb2hsl(d.rgb);
    hsl.x = fract(hsl.x + noise_value(cell, slice, 1.0, type, true) * u.p[0].y * 0.5);
    hsl.z = clamp(hsl.z + noise_value(cell, slice, 2.0, type, true) * u.p[0].z * 0.5, 0.0, 1.0);
    hsl.y = clamp(hsl.y + noise_value(cell, slice, 3.0, type, true) * u.p[0].w, 0.0, 1.0);
    return ae_encode(ae_hsl2rgb(hsl), d.a);
}

// --- 13 Ruído alfa --------------------------------------------------------------------------------------------------------
vec4 op_noise_alpha(vec4 src) {
    const int type = int(u.p[0].x + 0.5);
    const bool animated = type >= 2;
    const vec2 cell = ae_px() / max(ae_texel_px(), 0.25);
    float n = noise_value(cell, u.p[1].y, u.p[1].x + 7.0, 0, animated) * 0.5 + 0.5;
    if (type == 1 || type == 3) n *= n;
    const float v = (n - 0.5) * 2.0 * clamp(u.p[0].y, 0.0, 1.0);
    const float a0 = clamp(src.a, 0.0, 1.0);
    const int mode = int(u.p[0].z + 0.5);
    float a = a0 + v;
    if (mode == 1) a = min(a0 + v, a0);
    else if (mode == 2) a = a0 * (1.0 + v);
    else if (mode == 3) a = a0 + v * 4.0 * a0 * (1.0 - a0);
    const int overflow = int(u.p[0].w + 0.5);
    if (overflow == 1) a = 1.0 - abs(1.0 - mod(abs(a), 2.0));
    else if (overflow == 2) a = a >= 1.0 && a < 1.0 + 1e-6 ? 1.0 : fract(a);
    a = clamp(a, 0.0, 1.0);
    const vec3 straight = a0 > 1e-6 ? src.rgb / a0 : vec3(0.0);
    return vec4(straight * a, a);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const int op = int(u.p[11].w + 0.5);
    vec4 r = src;
    if (op == 0) r = op_unmult(src);
    else if (op == 1) r = op_leave(src);
    else if (op == 2) r = op_change(src);
    else if (op == 3) r = op_change_to(src);
    else if (op == 4) r = op_photo_filter(src);
    else if (op == 5) r = op_black_white(src);
    else if (op == 6) r = op_selective(src);
    else if (op == 7) r = op_alpha_levels(src);
    else if (op == 8) r = op_remove_matting(src);
    else if (op == 9) r = op_arithmetic(src);
    else if (op == 10) r = op_color_range(src);
    else if (op == 11) r = op_strobe(src);
    else if (op == 12) r = op_noise_hls(src);
    else if (op == 13) r = op_noise_alpha(src);
    o_color = r;
}
