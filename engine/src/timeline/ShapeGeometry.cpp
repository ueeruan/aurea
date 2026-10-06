// =============================================================================
//  Aurea / timeline / ShapeGeometry.cpp
//
//  A conta de distância das formas 2D na CPU — espelho linha a linha de
//  shaders/shape/shape.frag (mudou lá, muda aqui). Usada pelas partículas que
//  nascem da forma e pelos testes de geometria.
// =============================================================================

#include "aurea/timeline/ShapeGeometry.hpp"

#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::shape {
namespace {

constexpr f32 kPi = 3.14159265358979f;

f32 gmod(f32 x, f32 y) { return x - y * std::floor(x / y); }
f32 dot2(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
f32 sign_of(f32 v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

f32 sd_round_box(Vec2 q, Vec2 b, f32 r) {
    r = std::min(r, std::min(b.x, b.y));
    const Vec2 d{std::fabs(q.x) - b.x + r, std::fabs(q.y) - b.y + r};
    const Vec2 m{std::max(d.x, 0.0f), std::max(d.y, 0.0f)};
    return m.length() + std::min(std::max(d.x, d.y), 0.0f) - r;
}

f32 sd_ellipse(Vec2 q, Vec2 ab) {
    const f32 k0 = Vec2{q.x / ab.x, q.y / ab.y}.length();
    const f32 k1 = Vec2{q.x / (ab.x * ab.x), q.y / (ab.y * ab.y)}.length();
    return k0 < 1e-6f ? -std::min(ab.x, ab.y) : k0 * (k0 - 1.0f) / k1;
}

f32 sd_ngon(Vec2 q, f32 r, f32 n) {
    const f32 an = kPi / n;
    const f32 bn = gmod(std::atan2(q.x, -q.y), 2.0f * an) - an;
    const f32 L = q.length();
    q = Vec2{L * std::cos(bn), std::fabs(L * std::sin(bn))};
    q = q - Vec2{r * std::cos(an), r * std::sin(an)};
    q.y += std::clamp(-q.y, 0.0f, r * std::sin(an));
    return q.length() * sign_of(q.x);
}

f32 sd_star(Vec2 q, f32 r, f32 n, f32 inner) {
    const f32 an = kPi / n;
    const f32 a = gmod(std::atan2(q.x, -q.y), 2.0f * an) - an;
    const f32 L = q.length();
    q = Vec2{L * std::cos(a), std::fabs(L * std::sin(a))};
    const Vec2 tip{r, 0.0f};
    const Vec2 valley{inner * r * std::cos(an), inner * r * std::sin(an)};
    const Vec2 e = valley - tip, w = q - tip;
    const f32 h = std::clamp(dot2(w, e) / dot2(e, e), 0.0f, 1.0f);
    const f32 d = (w - e * h).length();
    const f32 s = e.x * w.y - e.y * w.x;
    return s > 0.0f ? -d : d;
}

f32 sd_triangle(Vec2 q, Vec2 p0, Vec2 p1, Vec2 p2) {
    const Vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    const Vec2 v0 = q - p0, v1 = q - p1, v2 = q - p2;
    const Vec2 pq0 = v0 - e0 * std::clamp(dot2(v0, e0) / dot2(e0, e0), 0.0f, 1.0f);
    const Vec2 pq1 = v1 - e1 * std::clamp(dot2(v1, e1) / dot2(e1, e1), 0.0f, 1.0f);
    const Vec2 pq2 = v2 - e2 * std::clamp(dot2(v2, e2) / dot2(e2, e2), 0.0f, 1.0f);
    const f32 s = (e0.x * e2.y - e0.y * e2.x) >= 0.0f ? 1.0f : -1.0f;
    f32 dx = dot2(pq0, pq0), dy = s * (v0.x * e0.y - v0.y * e0.x);
    dx = std::min(dx, dot2(pq1, pq1)); dy = std::min(dy, s * (v1.x * e1.y - v1.y * e1.x));
    dx = std::min(dx, dot2(pq2, pq2)); dy = std::min(dy, s * (v2.x * e2.y - v2.y * e2.x));
    return -std::sqrt(dx) * sign_of(dy);
}

f32 sd_trapezoid(Vec2 q, f32 top, f32 bottom, f32 he) {
    const Vec2 k1{bottom, he}, k2{bottom - top, 2.0f * he};
    q.x = std::fabs(q.x);
    const Vec2 ca{q.x - std::min(q.x, q.y < 0.0f ? top : bottom), std::fabs(q.y) - he};
    const Vec2 cb = q - k1 + k2 * std::clamp(dot2(k1 - q, k2) / dot2(k2, k2), 0.0f, 1.0f);
    const f32 s = (cb.x < 0.0f && ca.y < 0.0f) ? -1.0f : 1.0f;
    return s * std::sqrt(std::min(dot2(ca, ca), dot2(cb, cb)));
}

f32 sd_parallelogram(Vec2 q, f32 wi, f32 he, f32 sk) {
    const Vec2 e{sk, he};
    if (q.y < 0.0f) q = Vec2{-q.x, -q.y};
    Vec2 w = q - e;
    w.x -= std::clamp(w.x, -wi, wi);
    f32 dx = dot2(w, w), dy = -w.y;
    const f32 s = q.x * e.y - q.y * e.x;
    if (s < 0.0f) q = Vec2{-q.x, -q.y};
    Vec2 v = q - Vec2{wi, 0.0f};
    v = v - e * std::clamp(dot2(v, e) / dot2(e, e), -1.0f, 1.0f);
    dx = std::min(dx, dot2(v, v));
    dy = std::min(dy, wi * he - std::fabs(s));
    return std::sqrt(dx) * -sign_of(dy);
}

// Engrenagem: disco da raiz + `n` dentes retos + furo do cubo. `depth` = altura
// do dente (fração do raio; 0,22 é a engrenagem de sempre).
f32 sd_gear(Vec2 q, f32 r, f32 n, f32 hub, f32 depth) {
    const f32 root = r * (1.0f - depth);
    const f32 sector = 2.0f * kPi / n;
    const f32 a = gmod(std::atan2(q.x, -q.y) + sector * 0.5f, sector) - sector * 0.5f;
    const f32 L = q.length();
    const Vec2 f{L * std::sin(a), L * std::cos(a)};
    const f32 tw = root * std::sin(sector * 0.25f);
    const f32 tooth = sd_round_box(f - Vec2{0.0f, (root + r) * 0.5f}, Vec2{tw, (r - root) * 0.5f + 1e-3f}, 0.0f);
    return std::max(std::min(L - root, tooth), hub * root - L);
}

// Polígono qualquer (par-ímpar): distância até a aresta mais próxima, sinal
// pelo número de cruzamentos.
f32 sd_polygon(Vec2 q, const Vec2* v, int n) {
    f32 d = dot2(q - v[0], q - v[0]);
    f32 s = 1.0f;
    for (int i = 0, j = n - 1; i < n; j = i, ++i) {
        const Vec2 e = v[j] - v[i];
        const Vec2 w = q - v[i];
        const Vec2 b = w - e * std::clamp(dot2(w, e) / dot2(e, e), 0.0f, 1.0f);
        d = std::min(d, dot2(b, b));
        const bool c0 = q.y >= v[i].y, c1 = q.y < v[j].y, c2 = e.x * w.y > e.y * w.x;
        if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) s = -s;
    }
    return s * std::sqrt(d);
}

// Setor angular que começa no leste (x+) e gira no sentido horário da tela
// (y para baixo) por `sweepDeg`: negativo dentro. Até meia volta é a
// interseção de dois semiplanos; acima, a união.
f32 sd_wedge(Vec2 q, f32 sweepDeg) {
    const f32 s = sweepDeg * kPi / 180.0f;
    const f32 h1 = -q.y;
    const f32 h2 = q.y * std::cos(s) - q.x * std::sin(s);
    return s <= kPi ? std::max(h1, h2) : std::min(h1, h2);
}

// Coração na caixa [-m, m]² (ponta embaixo): dois lóbulos circulares que
// tocam o topo e os lados + o polígono das tangentes até a ponta. `depth` =
// profundidade do entalhe de cima (0..1 → até meia altura do lóbulo).
f32 sd_heart(Vec2 p, f32 m, f32 depth) {
    const Vec2 u = p / m;
    const f32 dl = std::clamp(depth, 0.05f, 0.95f) * 0.5f;
    // Raio do lóbulo que dá o entalhe `dl` com os lóbulos encostados nas bordas.
    const f32 R = (1.0f + dl) - std::sqrt(2.0f * dl);
    const Vec2 C{1.0f - R, R - 1.0f};
    const Vec2 P{0.0f, 1.0f};
    const Vec2 v = P - C;
    const f32 L = v.length();
    const Vec2 vh = v / L;
    const f32 ca = R / L, sa = std::sqrt(std::max(0.0f, 1.0f - ca * ca));
    const Vec2 T = C + (vh * ca + Vec2{vh.y, -vh.x} * sa) * R;   // tangente do lado de fora
    const f32 lobe = (Vec2{std::fabs(u.x), u.y} - C).length() - R;
    const Vec2 poly[5] = {P, T, C, Vec2{-C.x, C.y}, Vec2{-T.x, T.y}};
    return std::min(lobe, sd_polygon(u, poly, 5)) * m;
}

// Selo: círculo com `count` saliências em arco (uma em cima). Cada saliência
// é um disco que passa pelos dois vales; `depth` 1 = saliência em meio círculo.
f32 sd_seal(Vec2 p, f32 m, f32 count, f32 depth) {
    const f32 n = std::clamp(std::round(count), 3.0f, 64.0f);
    const f32 h = kPi / n;
    const f32 dmax = 1.0f - 1.0f / (std::cos(h) + std::sin(h));
    const f32 dl = 0.01f + std::clamp(depth, 0.0f, 1.0f) * std::max(0.0f, dmax - 0.01f);
    const f32 v = 1.0f - dl;
    const f32 c = (1.0f - v * v) / (2.0f * (1.0f - v * std::cos(h)));
    const f32 b = 1.0f - c;
    const Vec2 u = p / m;
    const f32 L = u.length();
    const f32 a = gmod(std::atan2(u.x, -u.y) + h, 2.0f * h) - h;
    const Vec2 f{L * std::fabs(std::sin(a)), L * std::cos(a)};
    const f32 bump = (f - Vec2{0.0f, c}).length() - b;
    const f32 core = f.y - v * std::cos(h);
    return std::min(bump, core) * m;
}

// Distância até a curva y = amp·sen(k·(x + hx)): busca no período em volta
// do x do ponto + 3 passos de Newton.
f32 wave_distance(Vec2 q, f32 hx, f32 amp, f32 k) {
    if (amp < 1e-3f) return std::fabs(q.y);
    const f32 lam = 2.0f * kPi / k;
    f32 best = 1e30f, bt = q.x;
    for (int i = 0; i <= 8; ++i) {
        const f32 t = q.x + (static_cast<f32>(i) / 8.0f - 0.5f) * lam;
        const f32 dx = t - q.x, dy = amp * std::sin(k * (t + hx)) - q.y;
        const f32 d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; bt = t; }
    }
    for (int j = 0; j < 3; ++j) {
        const f32 ph = k * (bt + hx);
        const f32 sv = amp * std::sin(ph), s1 = amp * k * std::cos(ph), s2 = -amp * k * k * std::sin(ph);
        const f32 g1 = (bt - q.x) + (sv - q.y) * s1;
        const f32 g2 = 1.0f + s1 * s1 + (sv - q.y) * s2;
        if (g2 > 1e-4f) bt -= g1 / g2;
    }
    const f32 dx = bt - q.x, dy = amp * std::sin(k * (bt + hx)) - q.y;
    return std::sqrt(std::min(best, dx * dx + dy * dy));
}

// Blob: raio polar com três harmônicos (n, n+1 e 2) de fases sorteadas pela
// variante; normalizado para caber em [-m, m]². Distância de primeira ordem.
f32 sd_blob(Vec2 p, f32 m, f32 count, f32 variation, Vec4 ph) {
    const f32 n = std::clamp(std::round(count), 2.0f, 16.0f);
    const f32 a = std::clamp(variation, 0.05f, 0.95f) * 0.35f;
    const f32 R = m / (1.0f + a);
    const f32 L = p.length();
    const f32 th = std::atan2(p.y, p.x);
    const f32 t1 = n * th + ph.x, t2 = (n + 1.0f) * th + ph.y, t3 = 2.0f * th + ph.z;
    const f32 r = 1.0f + a * (0.55f * std::cos(t1) + 0.30f * std::cos(t2) + 0.15f * std::cos(t3));
    const f32 dr = -a * (0.55f * n * std::sin(t1) + 0.30f * (n + 1.0f) * std::sin(t2) + 0.30f * std::sin(t3));
    const f32 g = R * dr / std::max(L, 1e-3f * m);
    return (L - R * r) / std::sqrt(1.0f + g * g);
}

// Balão de fala: corpo arredondado (76% da altura) + rabicho até a base da
// caixa; `tip` 0..1 leva a ponta da esquerda à direita.
f32 sd_bubble(Vec2 q, Vec2 half, f32 corner, f32 tip) {
    const f32 bodyH = half.y * 2.0f * 0.76f;
    const f32 bottom = -half.y + bodyH;
    const Vec2 bh{half.x, bodyH * 0.5f};
    const f32 r = std::clamp(corner, 0.0f, std::min(bh.x, bh.y));
    const f32 body = sd_round_box(q - Vec2{0.0f, -half.y + bh.y}, bh, r);
    const f32 tx = std::clamp(half.x * (2.0f * std::clamp(tip, 0.0f, 1.0f) - 1.0f), -half.x * 0.92f, half.x * 0.92f);
    const f32 bw = half.x * 0.16f;
    const f32 lo = -half.x + r + bw, hi = half.x - r - bw;
    const f32 xb = lo < hi ? std::clamp(tx * 0.55f, lo, hi) : 0.0f;
    const f32 yb = bottom - std::min(bodyH * 0.25f, std::max(r, 2.0f));
    const f32 tail = sd_triangle(q, Vec2{xb - bw, yb}, Vec2{tx, half.y}, Vec2{xb + bw, yb});
    return std::min(body, tail);
}

// Raio (relâmpago): 7 vértices na caixa [-1, 1]²; `tip` inclina (0,5 = reto).
f32 sd_bolt(Vec2 q, Vec2 half, f32 tip) {
    static constexpr f32 kBase[14] = {-0.05f, -1.0f, 0.60f, -1.0f, 0.12f, -0.12f, 0.70f, -0.12f,
                                      -0.42f, 1.0f, -0.02f, 0.14f, -0.62f, 0.14f};
    const f32 k = (std::clamp(tip, 0.0f, 1.0f) - 0.5f) * 0.8f;
    Vec2 v[7];
    for (int i = 0; i < 7; ++i) {
        const f32 x = kBase[2 * i], y = kBase[2 * i + 1];
        v[i] = Vec2{(x - k * y) * half.x, y * half.y};
    }
    return sd_polygon(q, v, 7);
}

bool type_default(u32 param) {
    return param == kParamDepth || param == kParamTip || param == kParamThickness || param == kParamHead;
}

} // namespace

f32 clamp_param(u32 param, f32 v) noexcept {
    switch (param) {
        case kParamCorner: return std::clamp(v, 0.0f, 100000.0f);
        case kParamCount: return std::clamp(std::round(v), 1.0f, 64.0f);
        case kParamInner: return std::clamp(v, 0.05f, 0.95f);
        case kParamStroke: return std::clamp(v, 0.0f, 500.0f);
        case kParamWidth:
        case kParamHeight: return std::clamp(v, 1.0f, 16384.0f);
        case kParamDepth: return std::clamp(v, 0.05f, 0.95f);
        case kParamTip: return std::clamp(v, 0.0f, 1.0f);
        case kParamThickness: return std::clamp(v, 0.02f, 1.0f);
        case kParamSweep: return std::clamp(v, 1.0f, 360.0f);
        case kParamHead: return std::clamp(v, 0.1f, 0.9f);
        case kParamShaft: return std::clamp(v, 0.05f, 1.0f);
        case kParamAmplitude: return std::clamp(v, 0.0f, 1.0f);
        case kParamSeed: return std::clamp(std::round(v), 0.0f, 9999.0f);
        default: return v;
    }
}

f32 default_param(u32 type, u32 param) noexcept {
    switch (param) {
        case kParamDepth:
            switch (type) {
                case kGear: return 0.22f;     // raiz em 0,78 do raio (a engrenagem de sempre)
                case kFlower: return 0.28f;   // pétala de sempre (0,72 + 0,28·cos)
                case kSeal: return 0.6f;
                case kBlob: return 0.5f;
                default: return 0.4f;         // coração
            }
        case kParamTip: return type == kBubble ? 0.3f : 0.5f;
        case kParamThickness:
            switch (type) {
                case kLine: return 1.0f;
                case kWave: return 0.4f;
                default: return 0.2f;         // arco
            }
        case kParamHead: return type == kDoubleArrow ? 0.275f : 0.45f;
        case kParamSweep: return 270.0f;
        case kParamShaft: return 0.44f;
        case kParamAmplitude: return 1.0f;
        case kParamSeed: return 0.0f;
        default: return 0.0f;
    }
}

f32* param_field(ShapeData& sh, u32 param) noexcept {
    switch (param) {
        case kParamCorner: return &sh.cornerRadius;
        case kParamCount: return &sh.points;
        case kParamInner: return &sh.innerRadius;
        case kParamStroke: return &sh.strokeWidth;
        case kParamWidth: return &sh.bounds.w;
        case kParamHeight: return &sh.bounds.h;
        case kParamDepth: return &sh.depth;
        case kParamTip: return &sh.tip;
        case kParamThickness: return &sh.thickness;
        case kParamSweep: return &sh.sweep;
        case kParamHead: return &sh.head;
        case kParamShaft: return &sh.shaft;
        case kParamAmplitude: return &sh.amplitude;
        case kParamSeed: return &sh.seed;
        default: return nullptr;   // 0 = tipo da forma
    }
}

f32 param_value(const ShapeData& sh, u32 param) noexcept {
    const f32* f = param_field(const_cast<ShapeData&>(sh), param);
    if (!f) return 0.0f;
    f32 v = *f;
    if (!std::isfinite(v) || (v < 0.0f && type_default(param))) v = default_param(sh.shapeType, param);
    return clamp_param(param, v);
}

void blob_phases(f32 seed, f32 out[3]) noexcept {
    const f32 s = std::isfinite(seed) ? std::clamp(std::round(seed), 0.0f, 9999.0f) : 0.0f;
    const u32 base = static_cast<u32>(s);
    for (u32 i = 0; i < 3; ++i) {
        u32 h = base * 0x9E3779B1u + (i + 1u) * 0x85EBCA77u;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        out[i] = static_cast<f32>(h & 0xFFFFFFu) / 16777216.0f * 2.0f * kPi;
    }
}

SdfParams sdf_params(const ShapeData& sh, bool filled, f32 strokeWidth) noexcept {
    SdfParams p;
    p.shape = Vec4{sh.cornerRadius, sh.points, sh.innerRadius, filled ? 1.0f : 0.0f};
    p.extra = Vec4{strokeWidth, param_value(sh, kParamDepth), param_value(sh, kParamTip), param_value(sh, kParamThickness)};
    p.more = Vec4{param_value(sh, kParamSweep), param_value(sh, kParamHead), param_value(sh, kParamShaft),
                  param_value(sh, kParamAmplitude)};
    f32 ph[3];
    blob_phases(sh.seed, ph);
    p.blob = Vec4{ph[0], ph[1], ph[2], 0.0f};
    return p;
}

f32 signed_distance(u32 type, const SdfParams& p, Vec2 q, Vec2 half) noexcept {
    const f32 m = std::min(half.x, half.y);
    // Formas "redondas" num retângulo não quadrado: esticadas pela proporção.
    const Vec2 st{half.x / m, half.y / m};
    const f32 mst = std::min(st.x, st.y);
    const Vec2 qs{q.x / st.x, q.y / st.y};
    const f32 corner = p.shape.x, count = p.shape.y;
    const f32 inner = std::clamp(p.shape.z, 0.05f, 0.95f);
    const f32 depth = p.extra.y, tip = p.extra.z, thick = p.extra.w;
    const f32 sweep = p.more.x, head = p.more.y, shaft = p.more.z, amp = p.more.w;
    switch (type) {
        case kRect: return sd_round_box(q, half, corner);
        case kEllipse: return sd_ellipse(q, half);
        case kPolygon: return sd_ngon(qs, m, std::max(3.0f, count)) * mst;
        case kStar: return sd_star(qs, m, std::max(3.0f, count), inner) * mst;
        case kCross: {
            const Vec2 a{std::fabs(q.x), std::fabs(q.y)};
            const f32 t = m * inner;
            return std::min(sd_round_box(a, Vec2{half.x, t}, 0.0f), sd_round_box(a, Vec2{t, half.y}, 0.0f));
        }
        case kRing: {
            const f32 ring = m * (1.0f - inner) * 0.5f;
            return std::fabs(sd_ellipse(q, Vec2{half.x - ring, half.y - ring})) - ring;
        }
        case kPie: {
            const f32 e = sd_ellipse(q, half);
            return sweep >= 359.9f ? e : std::max(e, sd_wedge(q, sweep));
        }
        case kFlower: {
            const f32 n = std::max(3.0f, count);
            const f32 r = m * ((1.0f - depth) + depth * std::cos(n * std::atan2(q.y, q.x)));
            return (qs.length() - r) * mst * 0.8f;
        }
        case kArrow: {
            // Haste até 4/9 da ponta adentro (a seta de sempre com ponta 0,45).
            const f32 s = half.x * (1.0f - 2.0f * head);
            const f32 end = s + (half.x - s) * (4.0f / 9.0f);
            const f32 body = sd_round_box(q - Vec2{(end - half.x) * 0.5f, 0.0f}, Vec2{(end + half.x) * 0.5f, half.y * shaft * 0.5f}, 0.0f);
            const f32 point = sd_triangle(q, Vec2{s, -half.y}, Vec2{half.x, 0.0f}, Vec2{s, half.y});
            return std::min(body, point);
        }
        case kRightTriangle: return sd_triangle(q, Vec2{-half.x, -half.y}, Vec2{-half.x, half.y}, Vec2{half.x, half.y});
        case kTrapezoid: return sd_trapezoid(q, half.x * inner, half.x, half.y);
        case kParallelogram: {
            const f32 sk = half.x * inner * 0.5f;
            return sd_parallelogram(Vec2{q.x, -q.y}, half.x - sk, half.y, sk);
        }
        case kGear: return sd_gear(qs, m, std::max(3.0f, count), inner, depth) * mst;
        case kDoubleArrow: {
            const f32 t = half.y * inner;
            const f32 s = half.x * (1.0f - 2.0f * head);
            const f32 end = s + (half.x - s) * (3.0f / 11.0f);
            const f32 body = sd_round_box(q, Vec2{end, t}, 0.0f);
            const f32 right = sd_triangle(q, Vec2{s, -half.y}, Vec2{half.x, 0.0f}, Vec2{s, half.y});
            const f32 left = sd_triangle(q, Vec2{-s, half.y}, Vec2{-half.x, 0.0f}, Vec2{-s, -half.y});
            return std::min(body, std::min(left, right));
        }
        case kLine: return sd_round_box(q, Vec2{half.x, std::max(0.5f, half.y * thick)}, corner);
        case kDiamond: {
            const f32 yw = half.y * (2.0f * std::clamp(tip, 0.05f, 0.95f) - 1.0f);
            const Vec2 v[4] = {Vec2{0.0f, -half.y}, Vec2{half.x, yw}, Vec2{0.0f, half.y}, Vec2{-half.x, yw}};
            return sd_polygon(q, v, 4);
        }
        case kHeart: return sd_heart(qs, m, depth) * mst;
        case kSeal: return sd_seal(qs, m, count, depth) * mst;
        case kArc: {
            const f32 T = std::clamp(thick, 0.02f, 0.5f) * 2.0f * m;
            const f32 ring = std::fabs(qs.length() - (m - T * 0.5f)) - T * 0.5f;
            return (sweep >= 359.9f ? ring : std::max(ring, sd_wedge(qs, sweep))) * mst;
        }
        case kBubble: return sd_bubble(q, half, corner, tip);
        case kBolt: return sd_bolt(q, half, tip);
        case kWave: {
            const f32 n = std::clamp(std::round(count), 1.0f, 32.0f);
            const f32 T = std::clamp(thick, 0.02f, 1.0f) * 2.0f * half.y;
            const f32 a = std::clamp(amp, 0.0f, 1.0f) * std::max(0.0f, half.y - T * 0.5f);
            const f32 k = kPi * n / std::max(half.x, 1e-3f);
            return std::max(wave_distance(q, half.x, a, k) - T * 0.5f, std::fabs(q.x) - half.x);
        }
        case kBlob: return sd_blob(qs, m, count, depth, p.blob) * mst;
        default: return sd_round_box(q, half, 0.0f);
    }
}

f32 signed_distance(const ShapeData& sh, Vec2 q, Vec2 half) noexcept {
    const bool stroke = sh.strokeWidth > 0.0f && sh.strokeColor.w > 0.0f;
    return signed_distance(sh.shapeType, sdf_params(sh, sh.filled, stroke ? sh.strokeWidth : 0.0f), q, half);
}

} // namespace aurea::shape
