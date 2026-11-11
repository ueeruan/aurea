#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_blur.frag
//
//  Lote "AE 2026", desfoques que olham a cor. u.p[11].w escolhe:
//    0 Bilateral — média ponderada pela distância E pela diferença de cor
//      (σ de cor = limiar): suaviza sem borrar bordas. p0: x = raio (px),
//      y = limiar (0..1), z = colorir (0 = sai em cinza).
//    1 Inteligente — média só dos vizinhos dentro do limiar (corte seco).
//      p0: x = raio, y = limiar, z = qualidade (0..2: 16/32/64 amostras),
//      w = modo (0 normal, 1 só as bordas, 2 sobrepor bordas).
//    2 Em cruz — um desfoque horizontal e um vertical somados (a cruz).
//      p0: x = raio X, y = raio Y (px), z = modo de transferência
//      (0 nenhum, 1 somar, 2 tela, 3 mais claro, 4 mais escuro, 5 multiplicar).
//  As amostras seguem uma espiral de ângulo áureo no disco do raio: o custo é
//  fixo por pixel, qualquer que seja o raio.
// =============================================================================
#include "ae_common.glsl"

const float GOLDEN = 2.39996323;

vec4 bilateral(vec4 src, bool smart) {
    const vec2 px = ae_px();
    const float R = u.p[0].x;
    const float thr = max(u.p[0].y, 0.005);
    const vec4 c0 = ae_decode(src);
    int count = 64;
    if (smart) count = int(u.p[0].z + 0.5) == 0 ? 16 : (int(u.p[0].z + 0.5) == 1 ? 32 : 64);
    vec4 sum = src;
    float wsum = 1.0, rejected = 0.0;
    if (R > 0.0) {
        const float sigmaS = max(R * 0.5, 1e-3);
        for (int i = 0; i < 64; ++i) {
            if (i >= count) break;
            const float r = R * sqrt((float(i) + 0.5) / float(count));
            const float a = float(i) * GOLDEN;
            const vec4 s = ae_at_px(px + vec2(cos(a), sin(a)) * r);
            const vec4 cs = ae_decode(s);
            const float dc = length(cs.rgb - c0.rgb) / 1.7320508 + abs(cs.a - c0.a);
            float w;
            if (smart) {
                w = dc <= thr ? 1.0 : 0.0;
                rejected += 1.0 - w;
            } else {
                w = exp(-(r * r) / (2.0 * sigmaS * sigmaS)) * exp(-(dc * dc) / (2.0 * thr * thr));
            }
            sum += s * w;
            wsum += w;
        }
    }
    vec4 outc = sum / max(wsum, 1e-6);
    if (!smart) {
        if (u.p[0].z < 0.5) {
            const vec4 d = ae_decode(outc);
            outc = ae_encode(vec3(ae_luma(d.rgb)), d.a);
        }
        return outc;
    }
    const int mode = int(u.p[0].w + 0.5);
    if (mode == 0) return outc;
    // Borda = parte dos vizinhos que passou do limiar.
    const float edge = R > 0.0 ? smoothstep(0.15, 0.5, rejected / float(count)) : 0.0;
    if (mode == 1) return vec4(vec3(edge), 1.0) * max(src.a, edge);
    return mix(outc, vec4(src.a), edge);
}

vec4 cross_blur(vec4 src) {
    const vec2 px = ae_px();
    const float rx = u.p[0].x, ry = u.p[0].y;
    vec4 h = vec4(0.0), v = vec4(0.0);
    float wh = 0.0, wv = 0.0;
    for (int i = -16; i <= 16; ++i) {
        const float t = float(i) / 16.0;
        const float w = exp(-t * t * 4.5);
        if (rx > 0.0) { h += ae_at_px(px + vec2(t * rx, 0.0)) * w; wh += w; }
        if (ry > 0.0) { v += ae_at_px(px + vec2(0.0, t * ry)) * w; wv += w; }
    }
    vec4 blur;
    if (rx > 0.0 && ry > 0.0) blur = 0.5 * (h / wh + v / wv);
    else if (rx > 0.0) blur = h / wh;
    else if (ry > 0.0) blur = v / wv;
    else blur = src;
    const int mode = int(u.p[0].z + 0.5);
    if (mode == 0) return blur;
    if (mode == 1) return vec4(src.rgb + blur.rgb, max(src.a, blur.a));
    if (mode == 2) return src + blur - src * blur;
    if (mode == 3) return max(src, blur);
    if (mode == 4) return min(src, blur);
    return src * blur;
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const int op = int(u.p[11].w + 0.5);
    if (op == 0) o_color = bilateral(src, false);
    else if (op == 1) o_color = bilateral(src, true);
    else o_color = cross_blur(src);
}
