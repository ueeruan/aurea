#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_generate.frag
//
//  Lote "AE 2026", gerar. u.p[11].w escolhe:
//    0 Círculo · 1 Elipse · 2 Fractal · 3 Preencher com conta-gotas
//  Medidas em px da camada (o C++ já levou os pontos relativos para lá).
// =============================================================================
#include "ae_common.glsl"

/// Cobertura de um disco de raio R com difusão f (antialias de um texel).
float disc(float d, float R, float f) {
    const float w = max(f, ae_texel_px());
    return 1.0 - smoothstep(R - 0.5 * w, R + 0.5 * w, d);
}

// --- 0 Círculo ------------------------------------------------------------------------
// p0: xy = centro, z = raio, w = borda (0 nenhuma, 1 raio da borda, 2 espessura,
// 3 espessura × raio, 4 espessura e difusão × raio); p1: x = espessura, y = difusão
// externa, z = interna, w = inverter; p2: x = opacidade, y = modo; c0 = cor
vec4 circle(vec4 src) {
    const float d = length(ae_px() - u.p[0].xy);
    const float R = u.p[0].z;
    const int edge = int(u.p[0].w + 0.5);
    float t = u.p[1].x, fo = u.p[1].y, fi = u.p[1].z;
    if (edge >= 3) t = t * R / 100.0;
    if (edge == 4) { fo = fo * R / 100.0; fi = fi * R / 100.0; }
    float cov;
    if (edge == 0) cov = disc(d, R, fo);
    else if (edge == 1) cov = disc(d, R, fo) * (1.0 - disc(d, min(t, R), fi));
    else cov = disc(d, R + 0.5 * t, fo) * (1.0 - disc(d, max(R - 0.5 * t, 0.0), fi));
    if (u.p[1].w > 0.5) cov = 1.0 - cov;
    const float k = cov * clamp(u.p[2].x, 0.0, 1.0) * clamp(u.c[0].a, 0.0, 1.0);
    const vec3 col = max(u.c[0].rgb, vec3(0.0));
    const int mode = int(u.p[2].y + 0.5);
    if (mode == 0) return vec4(col * k, k);
    if (mode == 1) return vec4(col * k, k) + src * (1.0 - k);
    if (mode == 6) return src * cov;           // estêncil alfa
    if (mode == 7) return src * (1.0 - cov);   // silhueta alfa
    const vec4 base = ae_decode(src);
    const vec3 top = clamp(aurea_linear_to_srgb(col), 0.0, 1.0);
    // 2 somar, 3 multiplicar, 4 tela, 5 sobrepor → modos do lote (1, 2, 3, 4).
    const vec3 mixed = ae_blend_mode(base.rgb, top, mode - 1);
    return ae_encode(mix(base.rgb, mixed, k), base.a);
}

// --- 1 Elipse ---------------------------------------------------------------------------
// p0: xy = centro, z = largura, w = altura; p1: x = espessura, y = suavidade,
// z = compor sobre o original; c0 = cor de dentro, c1 = cor de fora
vec4 ellipse(vec4 src) {
    const vec2 half_ = max(u.p[0].zw * 0.5, vec2(1e-3));
    const vec2 q = (ae_px() - u.p[0].xy) / half_;
    const float dE = (length(q) - 1.0) * min(half_.x, half_.y);   // distância aproximada à curva (px)
    const float th = max(u.p[1].x * 0.5, ae_texel_px() * 0.5);
    const float t0 = abs(dE) / th;
    const float soft = clamp(u.p[1].y, 0.0, 1.0);
    const float alpha = 1.0 - smoothstep(1.0, 1.0 + soft * 6.0 + ae_texel_px() / th, t0);
    const vec3 inside = max(u.c[0].rgb, vec3(0.0)), outside = max(u.c[1].rgb, vec3(0.0));
    const vec3 col = mix(inside, outside, smoothstep(0.0, 1.0 + soft * 3.0, t0));
    const vec4 ring = vec4(col * alpha, alpha);
    return u.p[1].z > 0.5 ? ring + src * (1.0 - alpha) : ring;
}

// --- 2 Fractal ----------------------------------------------------------------------------
// p0: x = conjunto (0 Mandelbrot, 1 Julia), y = potência, zw = centro (real, imag.)
// p1: x = ampliação (×), y = limite de escape, zw = constante de Julia
// p2: x = paleta, y = matiz (voltas), z = passos do ciclo, w = deslocamento
// p3: x = interior transparente, y = mistura
vec2 cpow(vec2 z, int n) {
    vec2 r = z;
    for (int i = 1; i < 7; ++i) {
        if (i >= n) break;
        r = vec2(r.x * z.x - r.y * z.y, r.x * z.y + r.y * z.x);
    }
    return r;
}
vec3 palette(float t, int pal, float hue) {
    if (pal == 1) return vec3(t);
    if (pal == 2) return clamp(vec3(t * 3.0, t * 3.0 - 1.0, t * 3.0 - 2.0), 0.0, 1.0);
    if (pal == 3) return clamp(vec3(t * 3.0 - 2.0, t * 3.0 - 1.0, t * 3.0), 0.0, 1.0);
    return ae_hsl2rgb(vec3(fract(t + hue), 1.0, 0.5));
}
vec4 fractal(vec4 src) {
    const vec2 ls = ae_layer_size();
    const float scale = 3.0 / max(ls.y, 1.0) / max(u.p[1].x, 1e-6);
    const vec2 pt = (ae_px() - ls * 0.5) * scale + u.p[0].zw;
    const bool julia = u.p[0].x > 0.5;
    vec2 z = julia ? pt : vec2(0.0);
    const vec2 c = julia ? u.p[1].zw : pt;
    const int n = int(u.p[0].y + 0.5);
    const int limit = int(u.p[1].y + 0.5);
    int i = 0;
    float r2 = 0.0;
    for (; i < 1000; ++i) {
        if (i >= limit) break;
        z = cpow(z, n) + c;
        r2 = dot(z, z);
        if (r2 > 256.0) break;
    }
    vec4 gen;
    if (i >= limit) {
        gen = u.p[3].x > 0.5 ? vec4(0.0) : vec4(0.0, 0.0, 0.0, 1.0);
    } else {
        const float nu = float(i) + 1.0 - log(max(log(r2) * 0.5, 1e-6)) / log(float(max(n, 2)));
        const float t = fract((nu + u.p[2].w) / max(u.p[2].z, 1.0));
        const int pal = int(u.p[2].x + 0.5);
        gen = ae_encode(palette(t, pal, u.p[2].y), 1.0);
    }
    return mix(src, gen, clamp(u.p[3].y, 0.0, 1.0));
}

// --- 3 Preencher com conta-gotas -------------------------------------------------------------
// p0: xy = ponto, z = raio (px), w = média (0 ignorar vazios, 1 todos, 2 todos
// pré-multiplicados, 3 incluindo alfa); p1: x = manter alfa, y = misturar
vec4 eyedropper(vec4 src) {
    const float R = u.p[0].z;
    const int mode = int(u.p[0].w + 0.5);
    vec4 sumP = vec4(0.0);
    vec3 sumS = vec3(0.0);
    float n = 0.0;
    const int count = R > 0.0 ? 49 : 1;
    for (int i = 0; i < 49; ++i) {
        if (i >= count) break;
        const float r = R * sqrt((float(i) + 0.5) / float(count));
        const float a = float(i) * 2.39996323;
        const vec4 s = ae_at_px(u.p[0].xy + vec2(cos(a), sin(a)) * (count > 1 ? r : 0.0));
        sumP += s;
        sumS += s.a > 1e-5 ? s.rgb / s.a : vec3(0.0);
        n += 1.0;
    }
    vec3 col;
    float avgA = sumP.a / max(n, 1.0);
    if (mode == 0 || mode == 3) col = sumP.a > 1e-5 ? sumP.rgb / sumP.a : vec3(0.0);
    else if (mode == 1) col = sumS / max(n, 1.0);
    else col = sumP.rgb / max(n, 1.0);
    const float a = u.p[1].x > 0.5 ? src.a : (mode == 3 ? avgA : 1.0);
    return mix(vec4(col * a, a), src, clamp(u.p[1].y, 0.0, 1.0));
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const int op = int(u.p[11].w + 0.5);
    if (op == 0) o_color = circle(src);
    else if (op == 1) o_color = ellipse(src);
    else if (op == 2) o_color = fractal(src);
    else o_color = eyedropper(src);
}
