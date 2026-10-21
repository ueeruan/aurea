#version 450
// =============================================================================
//  Aurea / shaders / effects / color_grade.frag
//
//  Correção de cor (ColorGradeEffect.cpp, 0.3.0): a correção inteira num passe.
//  A ordem é a de um corretor de cor:
//   1. entrada log (S-Log3, LogC3, V-Log, Apple Log, Cineon) → linear da cena,
//      gamut → Rec.709 e uma curva de exibição suave;
//   2. balanço de branco e exposição (em luz linear);
//   3. em valores de tela: contraste com pivô, realces, sombras, brancos,
//      pretos e brilho; rodas lift/gamma/gain/offset; curvas RGB;
//   4. matiz: curvas de matiz, HSL em 8 faixas, qualificador secundário;
//   5. saturação, vibração, tonalização dividida, desbotado, nitidez;
//   6. LUT criativa (.cube), vinheta e grão; mistura com o original.
//  Valores de tela sem prender em 1: o realce estourado ainda pode descer.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_curves;  // 256×1: RGB = mestra∘canal
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_hue;     // 256×1: matiz→matiz, matiz→sat, matiz→luz, luz→sat (diagonal = neutro)
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_lut;     // .cube no formato do cube_lut.frag

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;     // x = perfil log, y = temperatura, z = matiz (tint), w = exposição (paradas)
    vec4 p1;     // x = contraste, y = pivô, z = realces, w = sombras
    vec4 p2;     // x = brancos, y = pretos, z = brilho, w = saturação (1 = neutro)
    vec4 p3;     // x = vibração, y = mistura, z = relógio (s), w = bandeiras
    vec4 color;  // não usado
    vec4 g[20];
} p;

// Bandeiras (p3.w): 1 curvas RGB, 2 curvas de matiz, 4 LUT, 8 qualificador,
// 16 mostrar máscara, 32 inverter máscara, 64 HSL ativa.
bool flag(int bit) { return (int(p.p3.w + 0.5) & bit) != 0; }

// sRGB sem prender em 1 (o resto da cadeia é linear e pode passar de 1).
float enc(float c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }
float dec(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
vec3 enc3(vec3 c) { c = max(c, vec3(0.0)); return vec3(enc(c.r), enc(c.g), enc(c.b)); }
vec3 dec3(vec3 c) { c = max(c, vec3(0.0)); return vec3(dec(c.r), dec(c.g), dec(c.b)); }
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 rgb2hsv(vec3 c) {
    const vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    const vec4 q = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    const vec4 r = mix(vec4(q.xyw, c.r), vec4(c.r, q.yzx), step(q.x, c.r));
    const float d = r.x - min(r.w, r.y);
    return vec3(abs(r.z + (r.w - r.y) / (6.0 * d + 1e-10)), d / (r.x + 1e-10), r.x);
}
vec3 hsv2rgb(vec3 c) {
    const vec3 q = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(q - 1.0, 0.0, 1.0), c.y);
}

// --- Entrada log (fórmulas públicas dos fabricantes) --------------------------
float slog3(float x) {
    return x >= 171.2102946929 / 1023.0 ? pow(10.0, (x * 1023.0 - 420.0) / 261.5) * 0.19 - 0.01
                                        : (x * 1023.0 - 95.0) * 0.01125 / (171.2102946929 - 95.0);
}
float logc3(float x) {   // ARRI LogC3, EI 800
    return x > 0.149658 ? (pow(10.0, (x - 0.385537) / 0.247190) - 0.052272) / 5.555556 : (x - 0.092809) / 5.367655;
}
float vlog(float x) {
    return x < 0.181 ? (x - 0.125) / 5.6 : pow(10.0, (x - 0.598206) / 0.241514) - 0.00873;
}
float applelog(float x) {
    const float r0 = -0.05641088, c = 47.28711236, beta = 0.00964052, gamma = 0.08550479, delta = 0.69336945;
    const float pt = c * (0.01 - r0) * (0.01 - r0);
    if (x >= pt) return exp2((x - delta) / gamma) - beta;
    if (x >= 0.0) return sqrt(x / c) + r0;
    return r0;
}
float cineon(float x) { return (pow(10.0, (x * 1023.0 - 685.0) / 300.0) - 0.0108) / (1.0 - 0.0108); }

vec3 from_log(vec3 code, int profile) {
    vec3 lin;
    mat3 gamut = mat3(1.0);
    if (profile == 1) {
        lin = vec3(slog3(code.r), slog3(code.g), slog3(code.b));
        gamut = mat3(1.6269474, -0.1785155, -0.0444361, -0.5401385, 1.4179409, -0.1959199, -0.0868089, -0.2394254, 1.2403560);
    } else if (profile == 2) {
        lin = vec3(logc3(code.r), logc3(code.g), logc3(code.b));
        gamut = mat3(1.617523, -0.070573, -0.021102, -0.537287, 1.334613, -0.226954, -0.080237, -0.264040, 1.248056);
    } else if (profile == 3) {
        lin = vec3(vlog(code.r), vlog(code.g), vlog(code.b));
        gamut = mat3(1.806576, -0.170090, -0.025206, -0.695697, 1.305955, -0.154468, -0.110879, -0.135865, 1.179674);
    } else if (profile == 4) {
        lin = vec3(applelog(code.r), applelog(code.g), applelog(code.b));
        gamut = mat3(1.6605, -0.1246, -0.0182, -0.5876, 1.1329, -0.1006, -0.0728, -0.0083, 1.1187);   // Rec.2020 → 709
    } else {
        lin = vec3(cineon(code.r), cineon(code.g), cineon(code.b));
    }
    lin = max(gamut * lin, vec3(0.0));
    // Curva de exibição: 18 % fica perto de 18 %, os realces dobram devagar.
    const vec3 x = lin * 1.25;
    return x * (1.0 + x / 256.0) / (1.0 + x);
}

/// Cor de uma roda (ponto no disco): deslocamento de cor sem luz (luma 0).
vec3 wheel(vec2 xy) {
    const float r = min(length(xy), 1.0);
    if (r < 1e-5) return vec3(0.0);
    const float hue = fract(atan(xy.y, xy.x) / 6.28318530718);
    const vec3 c = hsv2rgb(vec3(hue, 1.0, 1.0));
    return (c - luma(c)) * r;
}

vec3 lut_entry(int i) {
    const int width = textureSize(u_lut, 0).x;
    return texelFetch(u_lut, ivec2(i % width, i / width), 0).rgb;
}
vec3 lut_table3(ivec3 i, int n) { return lut_entry(i.x + n * (i.y + n * i.z)); }
vec3 apply_lut(vec3 rgb) {
    const int n = max(2, int(p.g[14].w + 0.5));
    const vec3 lo = p.g[16].xyz, hi = p.g[17].xyz;
    const vec3 q = clamp((rgb - lo) / max(hi - lo, vec3(1e-7)), 0.0, 1.0) * float(n - 1);
    const ivec3 a = ivec3(floor(q)), b = min(a + 1, ivec3(n - 1));
    const vec3 f = fract(q);
    if (p.g[15].x < 2.0) {
        return vec3(mix(lut_entry(a.x).r, lut_entry(b.x).r, f.x), mix(lut_entry(a.y).g, lut_entry(b.y).g, f.y),
                    mix(lut_entry(a.z).b, lut_entry(b.z).b, f.z));
    }
    const vec3 z0 = mix(mix(lut_table3(a, n), lut_table3(ivec3(b.x, a.y, a.z), n), f.x),
                        mix(lut_table3(ivec3(a.x, b.y, a.z), n), lut_table3(ivec3(b.x, b.y, a.z), n), f.x), f.y);
    const vec3 z1 = mix(mix(lut_table3(ivec3(a.x, a.y, b.z), n), lut_table3(ivec3(b.x, a.y, b.z), n), f.x),
                        mix(lut_table3(ivec3(a.x, b.y, b.z), n), lut_table3(b, n), f.x), f.y);
    return mix(z0, z1, f.z);
}

float lut256(float x) { return (clamp(x, 0.0, 1.0) * 255.0 + 0.5) / 256.0; }

float hue_band_weight(float hueDeg, float center, float width) {
    float d = abs(hueDeg - center);
    d = min(d, 360.0 - d);
    const float t = clamp(1.0 - d / width, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

vec4 straight_at(vec2 uv) {
    const vec4 c = texture(u_src, uv);
    return c.a > 1e-6 ? vec4(c.rgb / c.a, c.a) : vec4(0.0);
}

void main() {
    const vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = texture(u_src, uv);
    if (src.a <= 1e-6) { o_color = src; return; }
    const vec3 original = src.rgb / src.a;
    vec3 lin = original;

    // 1. Entrada log: o código é o valor de tela que o decoder entregou.
    const int profile = int(p.p0.x + 0.5);
    if (profile > 0) lin = from_log(enc3(lin), profile);

    // 2. Balanço de branco (luminância preservada) e exposição.
    const float temp = p.p0.y, tint = p.p0.z;
    if (abs(temp) + abs(tint) > 1e-5) {
        vec3 gain = vec3(1.0 + 0.30 * temp + 0.10 * tint, 1.0 - 0.25 * tint, 1.0 - 0.30 * temp + 0.10 * tint);
        gain = max(gain, vec3(0.05));
        lin *= gain / luma(gain);
    }
    lin *= exp2(p.p0.w);

    // 3. Valores de tela.
    vec3 e = enc3(lin);
    // Nitidez: realce do detalhe da luz pelos 4 vizinhos (antes do contraste).
    const float sharpen = p.g[13].x;
    if (sharpen > 0.0) {
        const vec2 t = p.texel.xy;
        const float cY = luma(enc3(original));
        const float nY = luma(enc3(straight_at(uv + vec2(t.x, 0)).rgb)) + luma(enc3(straight_at(uv - vec2(t.x, 0)).rgb))
                       + luma(enc3(straight_at(uv + vec2(0, t.y)).rgb)) + luma(enc3(straight_at(uv - vec2(0, t.y)).rgb));
        e += clamp(cY - nY * 0.25, -0.25, 0.25) * sharpen * 2.0;
    }
    // Contraste em volta do pivô.
    e = (e - p.p1.y) * max(0.0, 1.0 + p.p1.x) + p.p1.y;
    // Realces, sombras, brancos, pretos e brilho: uma curva na luz, somada.
    {
        const float Y = clamp(luma(e), 0.0, 1.5);
        float Y1 = Y;
        const float hi = p.p1.z, sh = p.p1.w, wh = p.p2.x, bl = p.p2.y, br = p.p2.z;
        Y1 += 0.35 * hi * smoothstep(0.35, 1.0, Y) * (hi > 0.0 ? max(1.2 - Y, 0.0) : Y);
        Y1 += 0.35 * sh * (1.0 - smoothstep(0.0, 0.65, Y)) * (sh > 0.0 ? max(1.0 - Y, 0.0) : Y);
        Y1 += 0.25 * wh * smoothstep(0.55, 1.0, Y);
        Y1 += 0.20 * bl * (1.0 - smoothstep(0.0, 0.45, Y));
        Y1 += 0.30 * br * 4.0 * Y * max(1.0 - Y, 0.0);
        e += vec3(Y1 - Y);
    }
    // Rodas: lift (sombras), gamma (meios), gain (realces), offset (tudo).
    {
        const vec3 lift = wheel(p.g[0].xy) * 0.25 + vec3(p.g[0].z * 0.25);
        const vec3 gain = vec3(1.0) + wheel(p.g[1].zw) * 0.5 + vec3(p.g[2].x * 0.5);
        const vec3 gam = max(vec3(1.0) + wheel(p.g[1].xy) * 0.5 + vec3(p.g[0].w * 0.5), vec3(0.1));
        const vec3 off = wheel(p.g[2].yz) * 0.2 + vec3(p.g[2].w * 0.2);
        e = e * gain + lift * (vec3(1.0) - clamp(e, 0.0, 1.0));
        e = sign(e) * pow(abs(e), 1.0 / gam);
        e += off;
    }
    // Curvas RGB (a mestra já está composta em cada canal).
    if (flag(1)) {
        const vec3 c = clamp(e, 0.0, 1.0);
        const vec3 over = e - c;
        e = vec3(texture(u_curves, vec2(lut256(c.r), 0.5)).r, texture(u_curves, vec2(lut256(c.g), 0.5)).g,
                 texture(u_curves, vec2(lut256(c.b), 0.5)).b) + over;
    }

    // 4. Matiz: curvas de matiz, HSL em 8 faixas.
    if (flag(2) || flag(64)) {
        const vec3 hdr = max(e - vec3(1.0), vec3(0.0));
        vec3 hsv = rgb2hsv(clamp(e, 0.0, 1.0));
        if (flag(2)) {
            // Diagonal = neutro: o que vale é o quanto a curva sobe ou desce
            // da diagonal na matiz (ou na luz) do pixel.
            const float h0 = hsv.x;
            const vec4 h = texture(u_hue, vec2(lut256(h0), 0.5));
            const float Y = clamp(luma(e), 0.0, 1.0);
            const float ls = texture(u_hue, vec2(lut256(Y), 0.5)).a - Y;
            hsv.x = fract(h0 + (h.r - h0));
            hsv.y *= max(0.0, 1.0 + 2.0 * (h.g - h0)) * max(0.0, 1.0 + 2.0 * ls);
            hsv.z *= max(0.0, 1.0 + (h.b - h0));
        }
        if (flag(64)) {
            const float hueDeg = hsv.x * 360.0;
            const float centers[8] = float[8](0.0, 30.0, 60.0, 120.0, 180.0, 240.0, 270.0, 300.0);
            const float widths[8] = float[8](30.0, 30.0, 45.0, 60.0, 60.0, 45.0, 30.0, 45.0);
            float dh = 0.0, ds = 0.0, dl = 0.0;
            for (int i = 0; i < 8; ++i) {
                const float w = hue_band_weight(hueDeg, centers[i], widths[i]);
                if (w <= 0.0) continue;
                const int k = i * 3;
                const float hh = p.g[3 + k / 4][k % 4];
                const float ss = p.g[3 + (k + 1) / 4][(k + 1) % 4];
                const float ll = p.g[3 + (k + 2) / 4][(k + 2) % 4];
                dh += w * hh; ds += w * ss; dl += w * ll;
            }
            hsv.x = fract(hsv.x + dh * (30.0 / 360.0));
            hsv.y = clamp(hsv.y * (1.0 + ds), 0.0, 1.0);
            hsv.z *= 1.0 + 0.5 * dl * hsv.y;
        }
        e = hsv2rgb(vec3(hsv.x, clamp(hsv.y, 0.0, 1.0), max(hsv.z, 0.0))) + hdr;
    }
    // Qualificador secundário: faixa de matiz × saturação × luz, com borda suave.
    if (flag(8)) {
        const vec3 hsv = rgb2hsv(clamp(e, 0.0, 1.0));
        const float soft = max(p.g[10].z, 1e-3);
        float d = abs(hsv.x * 360.0 - p.g[9].x);
        d = min(d, 360.0 - d);
        const float halfW = p.g[9].y;
        const float mh = 1.0 - smoothstep(halfW - soft * 60.0, halfW + soft * 60.0, d);
        const float ms = smoothstep(p.g[9].z - soft, p.g[9].z + soft, hsv.y) * (1.0 - smoothstep(p.g[9].w - soft, p.g[9].w + soft, hsv.y));
        const float Y = clamp(luma(e), 0.0, 1.0);
        const float ml = smoothstep(p.g[10].x - soft, p.g[10].x + soft, Y) * (1.0 - smoothstep(p.g[10].y - soft, p.g[10].y + soft, Y));
        float m = clamp(mh * ms * ml, 0.0, 1.0);
        if (flag(32)) m = 1.0 - m;
        if (flag(16)) {
            const vec3 shown = vec3(dec(m));
            o_color = vec4(shown * src.a, src.a);
            return;
        }
        vec3 q = rgb2hsv(clamp(e, 0.0, 1.0));
        q.x = fract(q.x + p.g[10].w / 360.0);
        q.y = clamp(q.y * (1.0 + p.g[11].x), 0.0, 1.0);
        vec3 eq = hsv2rgb(q) + max(e - vec3(1.0), vec3(0.0));
        eq = enc3(dec3(eq) * exp2(p.g[11].y));
        e = mix(e, eq, m);
    }

    // 5. Saturação, vibração, tonalização dividida, desbotado.
    {
        const float Y = luma(e);
        e = vec3(Y) + (e - vec3(Y)) * p.p2.w;
        const float mx = max(e.r, max(e.g, e.b)), mn = min(e.r, min(e.g, e.b));
        const float s = mx > 1e-5 ? clamp((mx - mn) / mx, 0.0, 1.0) : 0.0;
        e = vec3(Y) + (e - vec3(Y)) * max(0.0, 1.0 + p.p3.x * (1.0 - s));
        const float t = clamp(Y + p.g[11].w * 0.5, 0.0, 1.0);
        e += wheel(p.g[12].xy) * 0.2 * (1.0 - t) + wheel(p.g[12].zw) * 0.2 * t;
        e = mix(e, e * 0.85 + vec3(0.12), p.g[11].z);
    }

    // 6. LUT criativa, vinheta, grão.
    if (flag(4)) e = mix(e, apply_lut(clamp(e, 0.0, 1.0)), clamp(p.g[14].z, 0.0, 1.0));
    {
        const vec4 region = p.g[18];
        const vec2 layer = max(p.g[19].xy, vec2(1.0));
        const vec2 px = region.xy + v_uv * region.zw;
        if (abs(p.g[13].y) > 1e-4) {
            const vec2 q = (px - 0.5 * layer) / (0.5 * layer);
            const float r = length(q) * 0.7071;
            const float v = smoothstep(p.g[13].z, p.g[13].z + max(p.g[13].w, 1e-3), r);
            e *= max(0.0, 1.0 + p.g[13].y * v);
        }
        if (p.g[14].x > 0.0) {
            const vec2 cell = floor(px / max(p.g[14].y, 0.5));
            const float n = aurea_hash3(cell, uint(floor(p.p3.z * 60.0)) * 747796405u + 1u) - 0.5;
            const float Y = clamp(luma(e), 0.0, 1.0);
            e += vec3(n * p.g[14].x * 0.18 * (0.35 + 2.6 * Y * (1.0 - Y)));
        }
    }

    vec3 graded = dec3(e);
    graded = clamp(graded, vec3(0.0), vec3(65504.0));
    o_color = vec4(mix(original, graded, clamp(p.p3.y, 0.0, 1.0)) * src.a, src.a);
}
