#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_kaleidoscope.frag
//
//  Caleidoscópio (o comportamento do CC Kaleida): um pedaço da camada perto do
//  centro é espelhado para cobrir o quadro. Cada pixel é DOBRADO para dentro
//  da peça-mãe e lê a camada lá — uma amostra por pixel, sem laço que cresça.
//
//    0 Flor       — fatias em volta do centro, alternadas em espelho, e o raio
//                   dobrado em "Tamanho" (anéis espelhados);
//    1 Roda       — as fatias repetidas sem espelho (simetria de giro);
//    2 Desdobrar  — grade de quadrados espelhados, com os eixos do centro
//                   como espelhos;
//    3 Triângulos — os três espelhos do caleidoscópio de verdade (triângulo
//                   equilátero): o vértice-mãe mais próximo na rede hexagonal
//                   e as seis fatias de 60° em volta dele.
//
//  p0 = centro (px), tamanho (px), rotação (rad); p1.x = modo, p1.y = fatias.
// =============================================================================
#include "cc_common.glsl"

/// Ângulo dobrado em [0, seg] (espelho) ou repetido (roda).
float fold_angle(float a, float seg, bool mirror) {
    if (!mirror) return mod(a, seg);
    const float k = mod(a, 2.0 * seg);
    return k > seg ? 2.0 * seg - k : k;
}

/// Raio dobrado em [0, size]: onda triangular (anéis espelhados).
float fold_radius(float r, float size) {
    return size - abs(mod(r, 2.0 * size) - size);
}

vec2 fold_polar(vec2 d, float segs, bool mirror, float size) {
    const float seg = 2.0 * CC_PI / segs;
    const float a = fold_angle(atan(d.y, d.x), seg, mirror);
    return vec2(cos(a), sin(a)) * fold_radius(length(d), size);
}

vec2 fold_square(vec2 d, float size) {
    // Onda triangular par: o centro é espelho nos dois eixos (o quadrado-mãe
    // fica entre o centro e "Tamanho", e se desdobra para os quatro lados).
    const vec2 t = d / size;
    return (1.0 - abs(mod(t, 2.0) - 1.0)) * size;
}

vec2 fold_triangle(vec2 d, float s) {
    // Vértices "A" do ladrilhamento por triângulos de lado s: rede de passo
    // s·√3 nas direções 30° e 90°. A célula de Voronoi de cada um é o
    // hexágono formado pelos seis triângulos que tocam nele.
    const float k = s * 1.7320508;
    const vec2 a1 = k * vec2(0.8660254, 0.5);
    const vec2 a2 = vec2(0.0, k);
    const float fi = d.x / a1.x;
    const vec2 base = floor(vec2(fi, (d.y - fi * a1.y) / a2.y));
    vec2 best = d;
    float bestD = 1e30;
    for (int u = 0; u < 2; ++u) {
        for (int v = 0; v < 2; ++v) {
            const vec2 off = d - ((base.x + float(u)) * a1 + (base.y + float(v)) * a2);
            const float dd = dot(off, off);
            if (dd < bestD) { bestD = dd; best = off; }
        }
    }
    const float a = fold_angle(atan(best.y, best.x), CC_PI / 3.0, true);
    return vec2(cos(a), sin(a)) * length(best);
}

void main() {
    const vec2 c = p.p0.xy;
    const float size = max(p.p0.z, 0.5);
    const float rot = p.p0.w;
    const int mode = int(p.p1.x + 0.5);
    const vec2 d = cc_rot(cc_px() - c, -rot);
    vec2 f;
    if (mode == 2) f = fold_square(d, size);
    else if (mode == 3) f = fold_triangle(d, size);
    else f = fold_polar(d, max(p.p1.y, 1.0), mode == 0, size);
    o_color = cc_at(c + cc_rot(f, rot));
}
