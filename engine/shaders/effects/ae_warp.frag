#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_warp.frag
//
//  Deformar (estilos), lote "AE 2026": quinze deformações de envelope sobre a
//  caixa da camada. Cada estilo é uma fórmula de DESTINO → ORIGEM em
//  coordenadas normalizadas da camada (x, y em −1..1, y para baixo); curvatura
//  0 e distorções 0 = identidade exata. O eixo vertical troca x e y antes e
//  depois da fórmula. As fórmulas são nossas (o estilo foi especificado pelo
//  nome e pelo desenho de cada um), não as de outro programa.
//
//  u.p[0]: x = estilo (0..14), y = eixo (0 horizontal, 1 vertical), z = curvatura (−1..1)
//  u.p[1]: x = distorção horizontal, y = distorção vertical (−1..1)
// =============================================================================
#include "ae_common.glsl"

const float PI = 3.14159265359;

float safe_div(float a, float b) { return a / (abs(b) < 0.1 ? (b < 0.0 ? -0.1 : 0.1) : b); }

vec2 style_src(vec2 q, int style, float b) {
    const float x = q.x, y = q.y;
    const float bow = 1.0 - x * x;   // 1 no meio, 0 nas pontas
    if (style == 0) {
        // Arco: a faixa inteira vira um arco de círculo (ângulo total b·π).
        if (abs(b) < 1e-4) return q;
        const float s = b < 0.0 ? -1.0 : 1.0;
        const float A = abs(b) * PI;
        const float R = 2.0 / A;              // o meio mantém o comprimento
        const vec2 c = vec2(0.0, R);
        const vec2 d = vec2(x, y * s) - c;
        const float r = length(d);
        const float phi = atan(d.x, -d.y);
        return vec2(phi * 2.0 / A, (R - r) * s);
    }
    if (style == 1) return vec2(x, y - b * 0.5 * bow * (y + 1.0) * 0.5);                // arco inferior
    if (style == 2) return vec2(x, y + b * 0.5 * bow * (1.0 - y) * 0.5);                // arco superior
    if (style == 3) return vec2(x, y + b * 0.5 * bow);                                  // arcada
    if (style == 4) return vec2(x, safe_div(y, 1.0 + b * 0.5 * bow));                   // bojo
    if (style == 5) {                                                                   // concha inferior
        const float t = (y + 1.0) * 0.5;
        return vec2(x * (1.0 + b * 0.35 * t), y - b * 0.5 * bow * t);
    }
    if (style == 6) {                                                                   // concha superior
        const float t = (1.0 - y) * 0.5;
        return vec2(x * (1.0 + b * 0.35 * t), y + b * 0.5 * bow * t);
    }
    if (style == 7) return vec2(x, y - b * 0.25 * sin(x * PI));                         // bandeira
    if (style == 8) return vec2(x, y - b * 0.2 * sin(x * 2.0 * PI));                    // onda
    if (style == 9) return vec2(x, safe_div(y, 1.0 + b * 0.45 * sin((x + 1.0) * PI)));  // peixe
    if (style == 10) return vec2(x, y + b * 0.5 * sin(x * PI * 0.5));                   // subir
    if (style == 11) {                                                                  // olho de peixe
        const float r = length(q) / 1.41421356;
        if (r < 1e-5 || r >= 1.0) return q;
        return q * (pow(r, 1.0 + b * 0.7) / r);
    }
    if (style == 12) return vec2(safe_div(x, 1.0 + b * 0.5 * (1.0 - y * y)), safe_div(y, 1.0 + b * 0.5 * bow)); // inflar
    if (style == 13) return vec2(safe_div(x, 1.0 - b * 0.4 * (1.0 - y * y)), y * (1.0 - b * 0.25 * bow));       // espremer
    // 14 torcer: gira mais no meio, nada nos cantos.
    const float r = min(length(q) / 1.41421356, 1.0);
    const float a = b * PI * (1.0 - r) * (1.0 - r);
    const float c = cos(a), s = sin(a);
    return vec2(c * x - s * y, s * x + c * y);
}

void main() {
    const vec2 ls = ae_layer_size();
    const vec2 px = ae_px();
    vec2 q = px / ls * 2.0 - 1.0;
    const bool vertical = u.p[0].y > 0.5;
    if (vertical) q = q.yx;
    const vec2 d = q;
    vec2 s = style_src(q, int(u.p[0].x + 0.5), clamp(u.p[0].z, -1.0, 1.0));
    // Distorções: perspectiva de um lado maior que o outro.
    s.y = safe_div(s.y, 1.0 + u.p[1].x * 0.5 * d.x);
    s.x = safe_div(s.x, 1.0 + u.p[1].y * 0.5 * d.y);
    if (vertical) s = s.yx;
    const vec2 srcPx = (s * 0.5 + 0.5) * ls;
    o_color = texture(u_tex0, ae_uv_of_px(srcPx));
}
