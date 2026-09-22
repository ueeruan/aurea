// =============================================================================
//  Gráficos vetoriais (Fase 7D) na CPU: achatamento, regra de preenchimento,
//  booleanas, aparar, tracejado, repetidor, morph, ajuste de curva e SVG.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/animation/Curve.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::vector;

namespace {
Contour square(f32 x, f32 y, f32 s, bool ccw = true) {
    Contour c;
    c.pts = {{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}};
    if (!ccw) std::reverse(c.pts.begin(), c.pts.end());
    return c;
}
bool rel_near(f64 a, f64 b, f64 rel) { return std::fabs(a - b) <= std::fabs(b) * rel; }
} // namespace

AUREA_TEST(Vector, FlatteningStaysWithinTolerance) {
    // Círculo de raio 100: todo ponto e todo meio de corda a no máximo tol
    // do círculo (+ o erro do kappa, 0,03%).
    for (f32 tol : {1.0f, 0.25f, 0.05f}) {
        Contour c;
        flatten(make_ellipse(Vec2{0, 0}, Vec2{200, 200}), tol, c);
        f32 worst = 0.0f;
        for (usize i = 0; i < c.pts.size(); ++i) {
            const Vec2 a = c.pts[i], b = c.pts[(i + 1) % c.pts.size()];
            worst = std::max(worst, std::fabs(a.length() - 100.0f));
            worst = std::max(worst, std::fabs(((a + b) * 0.5f).length() - 100.0f));
        }
        std::printf("    tol %.2f: %zu pontos, desvio max %.4f px\n", tol, c.pts.size(), worst);
        AUREA_CHECK(worst <= tol + 0.03f);
        // Adaptativo: tolerância 20x menor não pode dar 20x mais pontos (~√20).
        AUREA_CHECK(c.pts.size() < static_cast<usize>(40.0f / std::sqrt(tol)));
    }
}

AUREA_TEST(Vector, EvenOddMakesAHoleNonZeroDoesNot) {
    std::vector<Contour> in{square(0, 0, 100), square(25, 25, 50)};   // mesmo sentido
    std::vector<Contour> nz, eo;
    resolve_fill(in, FillRule::NonZero, nz);
    resolve_fill(in, FillRule::EvenOdd, eo);
    AUREA_CHECK_NEAR(area_of(nz), 10000.0, 1e-3);
    AUREA_CHECK_NEAR(area_of(eo), 7500.0, 1e-3);
    AUREA_CHECK_EQ(eo.size(), static_cast<usize>(2));   // borda + furo
    // Sentido oposto: furo também em não-zero.
    std::vector<Contour> in2{square(0, 0, 100), square(25, 25, 50, false)};
    resolve_fill(in2, FillRule::NonZero, nz);
    AUREA_CHECK_NEAR(area_of(nz), 7500.0, 1e-3);
    // Auto-interseção (gravata): as duas metades com área positiva.
    std::vector<Contour> bow{Contour{{{0, 0}, {100, 100}, {100, 0}, {0, 100}}, true}};
    resolve_fill(bow, FillRule::NonZero, nz);
    AUREA_CHECK_NEAR(area_of(nz), 5000.0, 1e-2);
    for (const Contour& c : nz) AUREA_CHECK(signed_area(c.pts) > 0.0);
}

AUREA_TEST(Vector, BooleanAreasOfTwoSquaresAreExact) {
    const std::vector<std::vector<Contour>> sets{{square(0, 0, 100)}, {square(50, 50, 100)}};
    std::vector<Contour> r;
    boolean_op(sets, FillRule::NonZero, BoolOp::Union, r);
    const f64 u = area_of(r);
    boolean_op(sets, FillRule::NonZero, BoolOp::Intersect, r);
    const f64 in = area_of(r);
    boolean_op(sets, FillRule::NonZero, BoolOp::Subtract, r);
    const f64 sub = area_of(r);
    boolean_op(sets, FillRule::NonZero, BoolOp::Exclude, r);
    const f64 ex = area_of(r);
    std::printf("    uniao %.2f intersecao %.2f subtrair %.2f excluir %.2f\n", u, in, sub, ex);
    AUREA_CHECK(rel_near(u, 17500.0, 0.005));
    AUREA_CHECK(rel_near(in, 2500.0, 0.005));
    AUREA_CHECK(rel_near(sub, 7500.0, 0.005));
    AUREA_CHECK(rel_near(ex, 15000.0, 0.005));
    // Círculos (curvas achatadas): interseção de dois discos r=100 a 100 de distância.
    Contour a, b;
    flatten(make_ellipse(Vec2{0, 0}, Vec2{200, 200}), 0.01f, a);
    flatten(make_ellipse(Vec2{100, 0}, Vec2{200, 200}), 0.01f, b);
    boolean_op({{a}, {b}}, FillRule::NonZero, BoolOp::Intersect, r);
    const f64 lens = 2.0 * 100.0 * 100.0 * std::acos(0.5) - 50.0 * std::sqrt(4.0 * 100.0 * 100.0 - 100.0 * 100.0);
    std::printf("    lente %.2f (exata %.2f)\n", area_of(r), lens);
    AUREA_CHECK(rel_near(area_of(r), lens, 0.005));
    // Arestas colineares coincidentes (quadrados lado a lado): união = um retângulo.
    boolean_op({{square(0, 0, 100)}, {square(100, 0, 100)}}, FillRule::NonZero, BoolOp::Union, r);
    AUREA_CHECK_NEAR(area_of(r), 20000.0, 1e-2);
    AUREA_CHECK_EQ(r.size(), static_cast<usize>(1));
    if (!r.empty()) AUREA_CHECK_EQ(r[0].pts.size(), static_cast<usize>(4));   // pontos colineares somem
}

AUREA_TEST(Vector, TrimHalfCoversHalfTheLength) {
    Contour c;
    flatten(make_ellipse(Vec2{0, 0}, Vec2{200, 200}), 0.05f, c);
    const f64 L = length_of(c);
    std::vector<Contour> cs{c};
    trim(cs, 0.0f, 0.5f, 0.0f, false);
    f64 t = 0.0;
    for (const Contour& x : cs) t += length_of(x);
    AUREA_CHECK(rel_near(t, L * 0.5, 1e-3));
    // Deslocamento que cruza o começo: continua metade, num trecho só (fechado).
    cs = {c};
    trim(cs, 0.0f, 0.5f, 0.75f, false);
    t = 0.0;
    for (const Contour& x : cs) t += length_of(x);
    AUREA_CHECK(rel_near(t, L * 0.5, 1e-3));
    AUREA_CHECK_EQ(cs.size(), static_cast<usize>(1));
    // Sequencial: dois segmentos de 100; 25%–75% = 50 de cada.
    std::vector<Contour> two{Contour{{{0, 0}, {100, 0}}, false}, Contour{{{0, 50}, {100, 50}}, false}};
    trim(two, 0.25f, 0.75f, 0.0f, true);
    AUREA_CHECK_EQ(two.size(), static_cast<usize>(2));
    if (two.size() == 2) {
        AUREA_CHECK_NEAR(length_of(two[0]), 50.0, 1e-3);
        AUREA_CHECK_NEAR(length_of(two[1]), 50.0, 1e-3);
    }
}

AUREA_TEST(Vector, DashAndStrokeGeometry) {
    // Linha de 100 px, padrão 10/10: 5 traços de 10.
    std::vector<Contour> line{Contour{{{0, 0}, {100, 0}}, false}};
    dash(line, {10.0f, 10.0f}, 0.0f);
    AUREA_CHECK_EQ(line.size(), static_cast<usize>(5));
    for (const Contour& c : line) AUREA_CHECK_NEAR(length_of(c), 10.0, 1e-4);
    // Deslocamento de 5: começa no meio de um traço (5 + 4×10 + 5).
    std::vector<Contour> l2{Contour{{{0, 0}, {100, 0}}, false}};
    dash(l2, {10.0f, 10.0f}, 5.0f);
    f64 on = 0.0;
    for (const Contour& c : l2) on += length_of(c);
    AUREA_CHECK_NEAR(on, 50.0, 1e-3);
    // Contorno de 10 px numa linha de 100 com ponta reta: 1000 px².
    std::vector<Contour> rings;
    stroke_to_rings({Contour{{{0, 0}, {100, 0}}, false}}, 10.0f, 0, 0, 4.0f, 0.1f, rings);
    AUREA_CHECK_NEAR(area_of(rings), 1000.0, 0.5);
    // Ponta quadrada: +5 de cada lado.
    stroke_to_rings({Contour{{{0, 0}, {100, 0}}, false}}, 10.0f, 2, 0, 4.0f, 0.1f, rings);
    AUREA_CHECK_NEAR(area_of(rings), 1100.0, 0.5);
    // Quadrado fechado 100×100 com contorno 10 (miter): (110² − 90²).
    stroke_to_rings({square(0, 0, 100)}, 10.0f, 0, 0, 4.0f, 0.1f, rings);
    AUREA_CHECK_NEAR(area_of(rings), 110.0 * 110.0 - 90.0 * 90.0, 0.5);
    AUREA_CHECK_EQ(rings.size(), static_cast<usize>(2));
    // Chanfro corta os 4 cantos (triângulos de 5×5/2 cada).
    stroke_to_rings({square(0, 0, 100)}, 10.0f, 0, 2, 4.0f, 0.1f, rings);
    AUREA_CHECK_NEAR(area_of(rings), 110.0 * 110.0 - 90.0 * 90.0 - 4 * 12.5, 0.5);
}

AUREA_TEST(Vector, StrokeOfCurvedPathsHasTheAnnulusArea) {
    // Anel de raio 80 com contorno 8: área π(84² − 76²); metade aparada = metade.
    for (f32 tol : {0.2f, 0.05f, 0.01f}) {
        Contour c;
        flatten(make_ellipse(Vec2{128, 128}, Vec2{160, 160}), tol, c);
        std::vector<Contour> rings;
        stroke_to_rings({c}, 8.0f, 0, 0, 4.0f, tol, rings);
        const f64 full = area_of(rings);
        std::vector<Contour> half{c};
        trim(half, 0.0f, 0.5f, 0.0f, false);
        stroke_to_rings(half, 8.0f, 0, 0, 4.0f, tol, rings);
        const f64 h = area_of(rings);
        std::printf("    tol %.2f: anel %.2f (exato %.2f), metade %.2f\n", tol, full, 3.14159265 * (84.0 * 84.0 - 76.0 * 76.0), h);
        AUREA_CHECK(rel_near(full, 3.14159265 * (84.0 * 84.0 - 76.0 * 76.0), 0.005));
        AUREA_CHECK(rel_near(h, full * 0.5, 0.005));
    }
}

AUREA_TEST(Vector, RepeaterMakesCopiesAndMorphInterpolates) {
    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Rect;
    p.size = Vec2{40, 40};
    g.paths.push_back(p);
    VectorMesh one, five;
    build_mesh({g}, 0.0, 1.0f, one);
    g.repeater.enabled = true;
    g.repeater.copies = 5.0f;
    g.repeater.position = Vec2{60, 0};
    build_mesh({g}, 0.0, 1.0f, five);
    AUREA_CHECK_EQ(five.vertex_count(), one.vertex_count() * 5);
    // Largura: 4 passos de 60 + 40 (a franja de AA soma 1 px de cada lado).
    AUREA_CHECK_NEAR(five.max.x - five.min.x, 4 * 60.0f + 40.0f + 2.0f, 0.01f);
    // Morph: dois quadrados (4 vértices) em 0 e 10; em 5 cada vértice no meio.
    VectorPath m;
    PathKey k0, k1;
    k0.frame = 0; k0.ease = 0; k0.path = make_rect(Vec2{0, 0}, Vec2{100, 100}, 0.0f);
    k1.frame = 10; k1.ease = 0; k1.path = make_rect(Vec2{200, 100}, Vec2{50, 50}, 0.0f);
    m.keys = {k0, k1};
    const BezierPath mid = path_at(m, 5.0);
    AUREA_CHECK_EQ(mid.v.size(), static_cast<usize>(4));
    for (usize i = 0; i < 4 && i < mid.v.size(); ++i) {
        const Vec2 e = (k0.path.v[i].p + k1.path.v[i].p) * 0.5f;
        AUREA_CHECK_NEAR(mid.v[i].p.x, e.x, 1e-4f);
        AUREA_CHECK_NEAR(mid.v[i].p.y, e.y, 1e-4f);
    }
    // Contagens diferentes: reamostra sem mudar a forma (área igual à do quadrado).
    k1.path = make_ellipse(Vec2{0, 0}, Vec2{100, 100});
    k1.path = resample(k1.path, 9);
    AUREA_CHECK_EQ(k1.path.v.size(), static_cast<usize>(9));
    Contour c;
    flatten(k1.path, 0.01f, c);
    AUREA_CHECK_NEAR(signed_area(c.pts), 3.14159265 * 2500.0, 3.0);
    m.keys = {k0, k1};
    AUREA_CHECK_EQ(path_at(m, 3.0).v.size(), static_cast<usize>(9));
}

AUREA_TEST(Vector, AnimatedParamsComeFromTracks) {
    VectorGroup g;
    g.trim.enabled = true;
    TrackSet ts;
    Track& t = ts.get_or_create(TrackProperty::VectorParam, 0, kVecTrimEnd);
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 100.0f);
    AUREA_CHECK_NEAR(evaluate_group(g, ts, 0, 5.0).trim.end, 50.0f, 1e-3f);
    AUREA_CHECK_NEAR(evaluate_group(g, ts, 0, 2.5).trim.end, 25.0f, 1e-3f);
    AUREA_CHECK_NEAR(evaluate_group(g, ts, 1, 5.0).trim.end, 100.0f, 1e-3f);   // outro grupo: sem trilha
}

AUREA_TEST(Vector, FreehandFitStaysCloseToTheStroke) {
    // Meia volta de senoide amostrada como o dedo (ruído determinístico pequeno).
    std::vector<Vec2> pts;
    for (int i = 0; i <= 200; ++i) {
        const f32 x = static_cast<f32>(i) * 2.0f;
        pts.push_back(Vec2{x, 80.0f * std::sin(x * 0.02f) + 0.3f * std::sin(static_cast<f32>(i) * 1.7f)});
    }
    const BezierPath b = fit_curve(pts, 2.0f, false);
    AUREA_CHECK(!b.closed);
    AUREA_CHECK(b.v.size() >= 2 && b.v.size() < 20);   // poucas cúbicas, não 200 pontos
    Contour c;
    flatten(b, 0.05f, c);
    // Todo ponto do dedo a no máximo ~erro da curva.
    f32 worst = 0.0f;
    for (Vec2 p : pts) {
        f32 best = 1e9f;
        for (usize i = 0; i + 1 < c.pts.size(); ++i) {
            const Vec2 a = c.pts[i], d = c.pts[i + 1] - a;
            const f32 t = std::clamp((p - a).dot(d) / std::max(d.length_sq(), 1e-9f), 0.0f, 1.0f);
            best = std::min(best, (a + d * t - p).length());
        }
        worst = std::max(worst, best);
    }
    std::printf("    %zu vertices, erro max %.3f px\n", b.v.size(), worst);
    AUREA_CHECK(worst < 2.5f);
    // Círculo desenhado: fecha sozinho.
    std::vector<Vec2> circ;
    for (int i = 0; i <= 100; ++i) {
        const f32 a = static_cast<f32>(i) / 100.0f * 2.0f * 3.14159265f;
        circ.push_back(Vec2{100.0f * std::cos(a), 100.0f * std::sin(a)});
    }
    const BezierPath cb = fit_curve(circ, 1.5f, true);
    AUREA_CHECK(cb.closed);
    flatten(cb, 0.05f, c);
    AUREA_CHECK_NEAR(std::fabs(signed_area(c.pts)), 3.14159265 * 10000.0, 3.14159265 * 10000.0 * 0.02);
}

AUREA_TEST(Vector, SvgImportsPathsArcsTransformsAndGradients) {
    const std::string svg = R"svg(<?xml version="1.0"?>
<!-- amostra -->
<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100" viewBox="0 0 400 200">
  <defs>
    <linearGradient id="g1" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#ff0000"/>
      <stop offset="100%" style="stop-color:#0000ff;stop-opacity:0.5"/>
    </linearGradient>
  </defs>
  <g transform="translate(100,0)">
    <rect x="0" y="0" width="100" height="50" fill="url(#g1)"/>
    <path d="M200,100 a50,50 0 1,0 100,0 a50,50 0 1,0 -100,0z" fill="none" stroke="#00ff00" stroke-width="4"/>
  </g>
  <circle cx="50" cy="150" r="40" style="fill:rgb(255,255,0);opacity:0.5"/>
  <polygon points="0,0 20,0 10,20" fill-rule="evenodd"/>
</svg>)svg";
    SvgResult r;
    std::string err;
    AUREA_CHECK_MSG(parse_svg(svg, r, &err), err.c_str());
    AUREA_CHECK_EQ(r.elements, 4u);
    AUREA_CHECK_NEAR(r.size.x, 200.0f, 1e-4f);
    if (r.data.groups.size() < 4) return;
    // viewBox 400×200 em 200×100: escala 0,5. O retângulo (translate 100) vai
    // de (50, 0) a (100, 25).
    Vec2 mn, mx;
    AUREA_CHECK(bounds_of({r.data.groups[0]}, 0.0, mn, mx));
    AUREA_CHECK_NEAR(mn.x, 50.0f, 1e-3f);
    AUREA_CHECK_NEAR(mx.x, 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(mx.y, 25.0f, 1e-3f);
    const VectorPaint& gp = r.data.groups[0].fill.paint;
    AUREA_CHECK_EQ(gp.type, static_cast<u8>(1));
    AUREA_CHECK_NEAR(gp.start.x, 50.0f, 1e-3f);
    AUREA_CHECK_NEAR(gp.end.x, 100.0f, 1e-3f);
    AUREA_CHECK_EQ(gp.stops.size(), static_cast<usize>(2));
    if (gp.stops.size() == 2) AUREA_CHECK_NEAR(gp.stops[1].color.w, 0.5f, 1e-4f);
    // Círculo feito de dois arcos: centro (250, 100) → (175, 50) px, raio 25; contorno 4 → 2.
    const VectorGroup& arc = r.data.groups[1];
    AUREA_CHECK(!arc.fill.enabled && arc.stroke.enabled);
    AUREA_CHECK_NEAR(arc.stroke.width, 2.0f, 1e-4f);
    Contour c;
    flatten(path_at(arc.paths[0], 0.0), 0.01f, c);
    f32 worst = 0.0f;
    for (Vec2 p : c.pts) worst = std::max(worst, std::fabs((p - Vec2{175, 50}).length() - 25.0f));
    AUREA_CHECK(worst < 0.05f);
    AUREA_CHECK(arc.paths[0].path.closed);
    AUREA_CHECK_NEAR(r.data.groups[2].opacity, 50.0f, 1e-3f);
    AUREA_CHECK_EQ(r.data.groups[3].fill.rule, static_cast<u8>(1));
}
