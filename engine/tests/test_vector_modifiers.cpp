// =============================================================================
//  Operadores de caminho da camada vetorial (shape layer do After Effects):
//  Deslocar caminhos, Franzir e inflar, Aspereza, Ondular caminhos,
//  Arredondar cantos, Torcer, Zigue-zague, Ondular transform, escala X/Y do
//  repetidor, o documento v6 (ida e volta, documento antigo) e a API do motor
//  (adicionar, remover, reordenar com as trilhas, keyframes, desfazer).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/render/ParticleExtras.hpp"
#include "aurea/animation/Curve.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::vector;

namespace {

/// Quadrado de lado `s` centrado na origem (cantos sem alças).
BezierPath square(f32 s = 100.0f) { return make_rect(Vec2{0, 0}, Vec2{s, s}, 0.0f); }

struct Box {
    Vec2 mn{0, 0}, mx{0, 0};
};

Box box_of(const std::vector<BezierPath>& ps) {
    Box b;
    bool any = false;
    for (const BezierPath& p : ps) {
        for (const BezierVertex& v : p.v) {
            if (!any) { b.mn = b.mx = v.p; any = true; continue; }
            b.mn = Vec2{std::min(b.mn.x, v.p.x), std::min(b.mn.y, v.p.y)};
            b.mx = Vec2{std::max(b.mx.x, v.p.x), std::max(b.mx.y, v.p.y)};
        }
    }
    return b;
}

f64 area_of_paths(const std::vector<BezierPath>& ps, f32 tol = 0.02f) {
    std::vector<Contour> cs, rings;
    for (const BezierPath& p : ps) {
        Contour c;
        flatten(p, tol, c);
        cs.push_back(std::move(c));
    }
    resolve_fill(cs, FillRule::NonZero, rings);
    return area_of(rings);
}

bool same_paths(const std::vector<BezierPath>& a, const std::vector<BezierPath>& b) { return a == b; }

VectorGroup square_group() {
    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Rect;
    p.center = Vec2{0, 0};
    p.size = Vec2{100, 100};
    g.paths.push_back(p);
    g.fill.enabled = true;
    return g;
}

struct ModRig {
    Engine e;
    ModRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    }
    ~ModRig() { e.shutdown(); }
    bool undo() {
        Command c;
        c.type = CommandType::Undo;
        return e.apply_command(c).ok();
    }
    Layer* layer(u64 id) {
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        return comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    }
};

} // namespace

AUREA_TEST(VectorModifiers, OffsetGrowsAndShrinksByTheAmount) {
    std::vector<BezierPath> grow{square()};
    offset_paths(grow, 10.0f, 0, 4.0f, 1.0f, 0.0f, 0.05f);
    const Box g = box_of(grow);
    std::printf("    +10 px: [%.2f, %.2f] .. [%.2f, %.2f]\n", g.mn.x, g.mn.y, g.mx.x, g.mx.y);
    AUREA_CHECK_NEAR(g.mn.x, -60.0f, 0.05f);
    AUREA_CHECK_NEAR(g.mx.y, 60.0f, 0.05f);
    AUREA_CHECK_NEAR(area_of_paths(grow), 120.0 * 120.0, 4.0);   // junta em ponta: quadrado inteiro

    std::vector<BezierPath> shrink{square()};
    offset_paths(shrink, -10.0f, 0, 4.0f, 1.0f, 0.0f, 0.05f);
    const Box s = box_of(shrink);
    AUREA_CHECK_NEAR(s.mn.x, -40.0f, 0.05f);
    AUREA_CHECK_NEAR(s.mx.x, 40.0f, 0.05f);
    AUREA_CHECK_NEAR(area_of_paths(shrink), 80.0 * 80.0, 4.0);

    // Junta redonda: os cantos viram quartos de círculo (área do quadrado
    // crescido menos os quatro cantos que faltam).
    std::vector<BezierPath> round{square()};
    offset_paths(round, 10.0f, 1, 4.0f, 1.0f, 0.0f, 0.02f);
    const f64 expected = 100.0 * 100.0 + 4.0 * 100.0 * 10.0 + 3.14159265 * 100.0;
    std::printf("    junta redonda: area %.1f (esperado %.1f)\n", area_of_paths(round), expected);
    AUREA_CHECK_NEAR(area_of_paths(round), expected, 8.0);

    // Cópias: cada uma um passo mais longe (a última a 3 × 10 px).
    std::vector<BezierPath> copies{square()};
    offset_paths(copies, 10.0f, 0, 4.0f, 3.0f, 0.0f, 0.05f);
    AUREA_CHECK_EQ(copies.size(), static_cast<usize>(3));
    AUREA_CHECK_NEAR(box_of(copies).mx.x, 80.0f, 0.05f);
    // Deslocamento das cópias: todas andam meio passo.
    std::vector<BezierPath> shifted{square()};
    offset_paths(shifted, 10.0f, 0, 4.0f, 1.0f, 0.5f, 0.05f);
    AUREA_CHECK_NEAR(box_of(shifted).mx.x, 65.0f, 0.05f);
}

AUREA_TEST(VectorModifiers, PuckerAndBloatMoveVerticesAndFlipHandles) {
    std::vector<BezierPath> bloat{square()};
    pucker_bloat(bloat, 50.0f);
    for (const BezierVertex& v : bloat[0].v) {
        AUREA_CHECK_NEAR(v.p.length(), 0.5f * std::sqrt(2.0f) * 50.0f, 1e-3f);    // metade do caminho até o centro
        AUREA_CHECK((v.p + v.out).length() > v.p.length() + 1.0f);                // a alça vai para fora
        AUREA_CHECK((v.p + v.in).length() > v.p.length() + 1.0f);
    }
    // Inflar: a área cresce em relação aos vértices (os lados ficam bojudos).
    const f64 inner = 50.0 * 50.0;
    AUREA_CHECK(area_of_paths(bloat) > inner * 1.2);
    std::vector<BezierPath> pucker{square()};
    pucker_bloat(pucker, -50.0f);
    for (const BezierVertex& v : pucker[0].v) AUREA_CHECK_NEAR(v.p.length(), 1.5f * std::sqrt(2.0f) * 50.0f, 1e-3f);
    std::vector<BezierPath> none{square()};
    pucker_bloat(none, 0.0f);
    AUREA_CHECK(same_paths(none, {square()}));
}

AUREA_TEST(VectorModifiers, RoundCornersSoftensTheCorners) {
    std::vector<BezierPath> r{square()};
    round_corners(r, 20.0f);
    AUREA_CHECK_EQ(r[0].v.size(), static_cast<usize>(8));
    Contour c;
    flatten(r[0], 0.02f, c);
    f32 nearest = 1e9f;
    for (Vec2 q : c.pts) nearest = std::min(nearest, (q - Vec2{50, 50}).length());
    std::printf("    canto: ponto mais perto a %.3f px (arco: %.3f)\n", nearest, 20.0f * (std::sqrt(2.0f) - 1.0f));
    AUREA_CHECK(nearest > 7.5f);
    AUREA_CHECK_NEAR(area_of_paths(r), 10000.0 - (4.0 - 3.14159265) * 400.0, 6.0);
    // O raio para na metade do lado (lado de 10 px, raio pedido 50).
    std::vector<BezierPath> small{square(10.0f)};
    round_corners(small, 50.0f);
    AUREA_CHECK_NEAR(area_of_paths(small), 3.14159265 * 25.0, 1.0);   // vira um círculo
    // Vértices com alças (elipse) ficam como estão.
    std::vector<BezierPath> e{make_ellipse(Vec2{0, 0}, Vec2{100, 60})};
    round_corners(e, 20.0f);
    AUREA_CHECK(same_paths(e, {make_ellipse(Vec2{0, 0}, Vec2{100, 60})}));
}

AUREA_TEST(VectorModifiers, ZigZagMakesRidgesOnBothSides) {
    std::vector<BezierPath> z{square()};
    zigzag(z, 8.0f, 5.0f, false);
    AUREA_CHECK_EQ(z[0].v.size(), static_cast<usize>(4 * 6));   // 4 segmentos × (5 + 1) pontos
    // No lado de cima (y = −50) os pontos do meio alternam ±8 px da borda.
    f32 lo = 1e9f, hi = -1e9f;
    for (usize i = 1; i < 6; ++i) { lo = std::min(lo, z[0].v[i].p.y); hi = std::max(hi, z[0].v[i].p.y); }
    AUREA_CHECK_NEAR(lo, -58.0f, 1e-3f);
    AUREA_CHECK_NEAR(hi, -42.0f, 1e-3f);
    for (const BezierVertex& v : z[0].v) AUREA_CHECK(v.in.length_sq() == 0.0f && v.out.length_sq() == 0.0f);
    std::vector<BezierPath> smooth{square()};
    zigzag(smooth, 8.0f, 5.0f, true);
    AUREA_CHECK(smooth[0].v[2].out.length_sq() > 0.0f);   // suave: alças ao longo do caminho
    // Aberto: o último vértice também entra.
    BezierPath line;
    line.closed = false;
    line.v = {BezierVertex{{0, 0}, {}, {}}, BezierVertex{{100, 0}, {}, {}}};
    std::vector<BezierPath> zl{line};
    zigzag(zl, 5.0f, 3.0f, false);
    AUREA_CHECK_EQ(zl[0].v.size(), static_cast<usize>(5));
    // Pontos espaçados pelo comprimento (e não pelo parâmetro do segmento reto).
    AUREA_CHECK_NEAR(zl[0].v[1].p.x, 25.0f, 0.2f);
    AUREA_CHECK_NEAR(zl[0].v[2].p.x, 50.0f, 0.2f);
}

AUREA_TEST(VectorModifiers, RoughenIsDeterministicAndBounded) {
    auto rough = [](u32 seed, f64 t, f32 freq) {
        std::vector<BezierPath> p{square()};
        roughen(p, 6.0f, 4.0f, false, t, freq, 0.0f, 0.0f, 0.0f, seed);
        return p;
    };
    const auto a = rough(7, 0.0, 0.0f), b = rough(7, 0.0, 0.0f), c = rough(8, 0.0, 0.0f);
    AUREA_CHECK(same_paths(a, b));
    AUREA_CHECK(!same_paths(a, c));
    AUREA_CHECK_EQ(a[0].v.size(), static_cast<usize>(4 * 5));
    // Frequência 0 (Aspereza): parada no tempo.
    AUREA_CHECK(same_paths(rough(7, 0.0, 0.0f), rough(7, 3.7, 0.0f)));
    // Nenhum ponto sai mais que size·√2 do quadrado de origem.
    f32 worst = 0.0f;
    for (const BezierVertex& v : a[0].v) {
        const f32 dx = std::max(0.0f, std::fabs(v.p.x) - 50.0f), dy = std::max(0.0f, std::fabs(v.p.y) - 50.0f);
        worst = std::max(worst, std::sqrt(dx * dx + dy * dy));
    }
    AUREA_CHECK(worst <= 6.0f * std::sqrt(2.0f) + 1e-3f);
    // Correlação 100 %: todos os pontos andam juntos (a forma só se desloca).
    std::vector<BezierPath> rigid{square()};
    roughen(rigid, 6.0f, 1.0f, false, 0.0, 0.0f, 100.0f, 0.0f, 0.0f, 3);
    const Vec2 d0 = rigid[0].v[0].p - Vec2{-50, -50};
    const Vec2 d2 = rigid[0].v[2].p - Vec2{50, -50};
    AUREA_CHECK_NEAR(d0.x, d2.x, 1e-4f);
    AUREA_CHECK_NEAR(d0.y, d2.y, 1e-4f);
}

AUREA_TEST(VectorModifiers, WigglePathsAndWiggleTransformMoveOverTime) {
    VectorGroup g = square_group();
    g.modifiers.push_back(make_modifier(VectorModifierType::WigglePaths));
    AUREA_CHECK(modifier_animated(g.modifiers[0]));
    TrackSet none;
    GroupGeometry g0, g0b, g1;
    group_geometry(evaluate_group(g, none, 0, 0.0, 30.0), 0.0, 0.05f, g0);
    group_geometry(evaluate_group(g, none, 0, 0.0, 30.0), 0.0, 0.05f, g0b);
    group_geometry(evaluate_group(g, none, 0, 11.0, 30.0), 11.0, 0.05f, g1);
    AUREA_CHECK(!g0.fill.empty());
    AUREA_CHECK_EQ(g0.fill[0].pts.size(), g0b.fill[0].pts.size());
    AUREA_CHECK(std::fabs(area_of(g0.fill) - area_of(g1.fill)) > 1.0);
    // O cache da malha muda com o tempo só com operador animado.
    AUREA_CHECK(content_hash({evaluate_group(g, none, 0, 0.0)}, 0.0) != content_hash({evaluate_group(g, none, 0, 11.0)}, 11.0));
    VectorGroup still = square_group();
    still.modifiers.push_back(make_modifier(VectorModifierType::Roughen));
    AUREA_CHECK(!modifier_animated(still.modifiers[0]));
    AUREA_CHECK_EQ(content_hash({evaluate_group(still, none, 0, 0.0)}, 0.0), content_hash({evaluate_group(still, none, 0, 11.0)}, 11.0));

    // Ondular transform: o centro anda até o máximo da posição, e varia no tempo.
    std::vector<BezierPath> w0{square()}, w1{square()};
    wiggle_transform(w0, {}, Vec2{20, 20}, {}, 0.0f, 0.0, 2.0f, 50.0f, 0.0f, 0.0f, 1);
    wiggle_transform(w1, {}, Vec2{20, 20}, {}, 0.0f, 0.8, 2.0f, 50.0f, 0.0f, 0.0f, 1);
    const Box b0 = box_of(w0), b1 = box_of(w1);
    const Vec2 c0 = (b0.mn + b0.mx) * 0.5f, c1 = (b1.mn + b1.mx) * 0.5f;
    std::printf("    ondular transform: centro (%.2f, %.2f) -> (%.2f, %.2f)\n", c0.x, c0.y, c1.x, c1.y);
    AUREA_CHECK(std::fabs(c0.x) <= 20.001f && std::fabs(c0.y) <= 20.001f);
    AUREA_CHECK((c0 - c1).length() > 0.01f);
    AUREA_CHECK_NEAR(b0.mx.x - b0.mn.x, 100.0f, 1e-3f);   // só posição: o tamanho não muda
    // Escala e giro também entram.
    std::vector<BezierPath> ws{square()};
    wiggle_transform(ws, {}, {}, Vec2{50, 50}, 30.0f, 0.3, 2.0f, 0.0f, 0.0f, 0.0f, 5);
    AUREA_CHECK(!same_paths(ws, {square()}));
}

AUREA_TEST(VectorModifiers, TwistRotatesByDistanceFromTheCenter) {
    std::vector<BezierPath> t{square()};
    twist(t, 90.0f, Vec2{0, 0});
    AUREA_CHECK(t[0].v.size() > 4);   // subdividido antes de torcer
    // O canto (o ponto mais longe) gira os 90° inteiros: (−50, −50) → (50, −50).
    AUREA_CHECK_NEAR(t[0].v[0].p.x, 50.0f, 1e-2f);
    AUREA_CHECK_NEAR(t[0].v[0].p.y, -50.0f, 1e-2f);
    // Os pontos mantêm a distância até o centro.
    const std::vector<BezierPath> sub{square()};
    for (const BezierVertex& v : t[0].v) AUREA_CHECK(v.p.length() <= 50.0f * std::sqrt(2.0f) + 1e-2f);
    std::vector<BezierPath> zero{square()};
    twist(zero, 0.0f, Vec2{0, 0});
    AUREA_CHECK(same_paths(zero, sub));
}

AUREA_TEST(VectorModifiers, ModifiersRunInOrderBeforeMergeAndFill) {
    VectorGroup g = square_group();
    VectorModifier off = make_modifier(VectorModifierType::OffsetPaths);
    off.params[kVecModOffsetAmount] = 10.0f;
    g.modifiers.push_back(off);
    GroupGeometry geo;
    group_geometry(g, 0.0, 0.05f, geo);
    AUREA_CHECK_NEAR(area_of(geo.fill), 14400.0, 4.0);
    // Desligado: o grupo de sempre.
    g.modifiers[0].enabled = false;
    group_geometry(g, 0.0, 0.05f, geo);
    AUREA_CHECK_NEAR(area_of(geo.fill), 10000.0, 1.0);
    // Ordem: arredondar depois de deslocar ≠ deslocar depois de arredondar.
    g.modifiers[0].enabled = true;
    VectorModifier rc = make_modifier(VectorModifierType::RoundCorners);
    rc.params[kVecModRoundRadius] = 30.0f;
    g.modifiers.push_back(rc);
    group_geometry(g, 0.0, 0.05f, geo);
    const f64 offThenRound = area_of(geo.fill);
    std::swap(g.modifiers[0], g.modifiers[1]);
    group_geometry(g, 0.0, 0.05f, geo);
    const f64 roundThenOff = area_of(geo.fill);
    std::printf("    deslocar+arredondar %.1f, arredondar+deslocar %.1f\n", offThenRound, roundThenOff);
    AUREA_CHECK(std::fabs(offThenRound - roundThenOff) > 10.0);
    // A caixa do palco acompanha o caminho modificado.
    Vec2 mn, mx;
    g.modifiers = {off};
    AUREA_CHECK(bounds_of({g}, 0.0, mn, mx));
    AUREA_CHECK_NEAR(mx.x, 60.0f, 0.1f);
}

AUREA_TEST(VectorModifiers, RepeaterScaleXY) {
    VectorRepeater r;
    r.scale = 50.0f;
    r.scaleY = 200.0f;
    const Affine2 linked = repeater_matrix(r, 1.0f);
    AUREA_CHECK_NEAR(linked.a, 0.5f, 1e-5f);
    AUREA_CHECK_NEAR(linked.d, 0.5f, 1e-5f);   // ligada: Y acompanha X (projetos antigos)
    r.scaleLinked = 0;
    const Affine2 m = repeater_matrix(r, 2.0f);
    AUREA_CHECK_NEAR(m.a, 0.25f, 1e-5f);
    AUREA_CHECK_NEAR(m.d, 4.0f, 1e-4f);
    // Animável: a trilha da escala Y.
    VectorGroup g;
    g.repeater.scaleLinked = 0;
    TrackSet ts;
    Track& t = ts.get_or_create(TrackProperty::VectorParam, 0, kVecRepScaleY);
    t.set(FrameIndex{0}, 100.0f);
    t.set(FrameIndex{10}, 300.0f);
    AUREA_CHECK_NEAR(evaluate_group(g, ts, 0, 5.0).repeater.scaleY, 200.0f, 1e-3f);
}

AUREA_TEST(VectorModifiers, AnimatedModifierParamsComeFromTracks) {
    VectorGroup g = square_group();
    g.modifiers.push_back(make_modifier(VectorModifierType::OffsetPaths));
    g.modifiers.push_back(make_modifier(VectorModifierType::Twist));
    TrackSet ts;
    Track& t = ts.get_or_create(TrackProperty::VectorParam, 0, vector_modifier_param(1, kVecModTwistAngle));
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{10}, 100.0f);
    const VectorGroup e = evaluate_group(g, ts, 0, 5.0, 25.0);
    AUREA_CHECK_NEAR(e.modifiers[1].params[kVecModTwistAngle], 50.0f, 1e-3f);
    AUREA_CHECK_NEAR(e.modifiers[0].params[kVecModOffsetAmount], 10.0f, 1e-6f);   // sem trilha
    AUREA_CHECK_NEAR(static_cast<f32>(e.modifiers[0].time), 0.2f, 1e-6f);        // 5 quadros a 25 fps
    // param_ref alcança os parâmetros dos operadores; fora da lista, nada.
    VectorGroup w = g;
    AUREA_CHECK(param_ref(w, vector_modifier_param(1, kVecModTwistCenterY)) == &w.modifiers[1].params[kVecModTwistCenterY]);
    AUREA_CHECK(param_ref(w, vector_modifier_param(2, 0)) == nullptr);
    AUREA_CHECK(param_ref(w, kVecRepScaleY) == &w.repeater.scaleY);
}

AUREA_TEST(VectorModifiers, DocumentRoundTripAndOldDocuments) {
    VectorData d;
    VectorGroup g = square_group();
    g.repeater.scaleLinked = 0;
    g.repeater.scaleY = 75.0f;
    for (u32 t = 0; t < static_cast<u32>(VectorModifierType::Count); ++t) {
        VectorModifier m = make_modifier(static_cast<VectorModifierType>(t));
        m.params[kVecModSeed] = static_cast<f32>(t * 3);
        m.enabled = t % 2 == 0;
        g.modifiers.push_back(m);
    }
    d.groups.push_back(g);
    d.groups.push_back(square_group());
    std::vector<f32> stream;
    std::string names;
    encode_document(d, stream, names);
    AUREA_CHECK_NEAR(stream[0], 6.0f, 0.0f);
    VectorData back;
    AUREA_CHECK(decode_document(stream.data(), stream.size(), names, back));
    AUREA_CHECK(back == d);

    // Documento v5 (sem operadores e sem escala Y): o mesmo fluxo sem os campos
    // da v6. Grupo simples: o bloco do repetidor termina no float 65.
    VectorData old;
    VectorGroup og = square_group();
    og.repeater.scale = 80.0f;
    old.groups.push_back(og);
    encode_document(old, stream, names);
    AUREA_CHECK_EQ(stream.size(), static_cast<usize>(65 + 2 + 1 + 13 + 1 + 2 + 1 + 1 + 1));
    stream.erase(stream.end() - 2);                        // nº de operadores do grupo
    stream.erase(stream.begin() + 65, stream.begin() + 67); // escala Y e ligada
    stream[0] = 5.0f;
    VectorData loaded;
    AUREA_CHECK(decode_document(stream.data(), stream.size(), names, loaded));
    AUREA_CHECK_EQ(loaded.groups.size(), static_cast<usize>(1));
    AUREA_CHECK(loaded.groups[0].modifiers.empty());
    AUREA_CHECK_EQ(loaded.groups[0].repeater.scaleLinked, u8{1});
    AUREA_CHECK_NEAR(loaded.groups[0].repeater.scaleY, 80.0f, 0.0f);
    AUREA_CHECK(loaded.groups[0].paths == og.paths);
    // Fluxo cortado no meio dos operadores: falso, nada muda.
    encode_document(d, stream, names);
    VectorData broken;
    AUREA_CHECK(!decode_document(stream.data(), stream.size() - 30, names, broken));
}

AUREA_TEST(VectorModifiers, AfterEffectsMatchNames) {
    VectorModifierType t{};
    AUREA_CHECK(modifier_from_ae_match_name("ADBE Vector Filter - Offset", t) && t == VectorModifierType::OffsetPaths);
    AUREA_CHECK(modifier_from_ae_match_name("ADBE Vector Filter - PB", t) && t == VectorModifierType::PuckerBloat);
    AUREA_CHECK(modifier_from_ae_match_name("ADBE Vector Filter - Roughen", t) && t == VectorModifierType::WigglePaths);
    AUREA_CHECK(modifier_from_ae_match_name("ADBE Vector Filter - RC", t) && t == VectorModifierType::RoundCorners);
    AUREA_CHECK(modifier_from_ae_match_name("ADBE Vector Filter - Twist", t) && t == VectorModifierType::Twist);
    AUREA_CHECK(modifier_from_ae_match_name("ADBE Vector Filter - Zigzag", t) && t == VectorModifierType::ZigZag);
    AUREA_CHECK(modifier_from_ae_match_name("ADBE Vector Filter - Wiggler", t) && t == VectorModifierType::WiggleTransform);
    AUREA_CHECK(!modifier_from_ae_match_name("ADBE Vector Filter - Trim", t));
    AUREA_CHECK_EQ(modifier_param_from_ae_match_name(VectorModifierType::OffsetPaths, "ADBE Vector Offset Copy Offset"), static_cast<i32>(kVecModOffsetCopyOffset));
    AUREA_CHECK_EQ(modifier_param_from_ae_match_name(VectorModifierType::WigglePaths, "ADBE Vector Random Seed"), static_cast<i32>(kVecModSeed));
    AUREA_CHECK_EQ(modifier_param_from_ae_match_name(VectorModifierType::WiggleTransform, "ADBE Vector Xform Temporal Freq"), static_cast<i32>(kVecModWigFreq));
    AUREA_CHECK_EQ(modifier_param_from_ae_match_name(VectorModifierType::Twist, "ADBE Vector Twist Center"), static_cast<i32>(kVecModTwistCenterX));
    AUREA_CHECK_EQ(modifier_param_from_ae_match_name(VectorModifierType::ZigZag, "ADBE Vector Zigzag Detail"), static_cast<i32>(kVecModZigRidges));
    AUREA_CHECK_EQ(modifier_param_from_ae_match_name(VectorModifierType::PuckerBloat, "ADBE Vector Random Seed"), -1);
}

AUREA_TEST(VectorModifiers, EngineAddRemoveMoveKeepTracksAndUndo) {
    ModRig rig;
    const auto made = rig.e.add_vector_layer(1);
    AUREA_CHECK(made.ok());
    const u64 id = *made;
    AUREA_CHECK_EQ(rig.e.add_vector_modifier(id, 0, static_cast<u32>(VectorModifierType::OffsetPaths)), 0);
    AUREA_CHECK_EQ(rig.e.add_vector_modifier(id, 0, static_cast<u32>(VectorModifierType::Twist)), 1);
    AUREA_CHECK_EQ(rig.e.add_vector_modifier(id, 0, static_cast<u32>(VectorModifierType::ZigZag)), 2);
    AUREA_CHECK_EQ(rig.e.add_vector_modifier(id, 0, 99u), -1);
    AUREA_CHECK_EQ(rig.e.add_vector_modifier(id, 5, 0u), -1);

    // Valor parado e keyframe pelos ids de operador (a mesma API do VectorParam).
    AUREA_CHECK(rig.e.set_vector_param(id, 0, vector_modifier_param(0, kVecModOffsetAmount), -25.0f, false));
    AUREA_CHECK(rig.e.set_vector_param(id, 0, vector_modifier_param(2, kVecModZigRidges), 7.4f, false));
    AUREA_CHECK(rig.e.toggle_vector_param_key(id, 0, vector_modifier_param(1, kVecModTwistAngle)));
    AUREA_CHECK(!rig.e.set_vector_param(id, 0, vector_modifier_param(9, 0), 1.0f, false));   // operador que não existe
    f32 out[3 * Engine::kVectorModifierFloats]{};
    AUREA_CHECK_EQ(rig.e.query_vector_modifiers(id, 0, out, 3 * Engine::kVectorModifierFloats), 3u);
    AUREA_CHECK_NEAR(out[0], static_cast<f32>(VectorModifierType::OffsetPaths), 0.0f);
    AUREA_CHECK_NEAR(out[4 + kVecModOffsetAmount], -25.0f, 1e-6f);
    AUREA_CHECK_NEAR(out[2 * Engine::kVectorModifierFloats + 4 + kVecModZigRidges], 7.0f, 1e-6f);   // inteiro
    const f32* twistRow = out + Engine::kVectorModifierFloats;
    AUREA_CHECK((static_cast<u32>(twistRow[2]) & (1u << kVecModTwistAngle)) != 0);   // animado
    AUREA_CHECK((static_cast<u32>(twistRow[3]) & (1u << kVecModTwistAngle)) != 0);   // keyframe aqui
    AUREA_CHECK(rig.layer(id)->tracks.find(TrackProperty::VectorParam, 0, vector_modifier_param(1, kVecModTwistAngle)) != nullptr);

    // Reordenar: a trilha do Torcer acompanha o operador (1 → 0).
    AUREA_CHECK(rig.e.move_vector_modifier(id, 0, 1, 0));
    AUREA_CHECK(rig.layer(id)->shape.vector.groups[0].modifiers[0].type == VectorModifierType::Twist);
    AUREA_CHECK(rig.layer(id)->tracks.find(TrackProperty::VectorParam, 0, vector_modifier_param(0, kVecModTwistAngle)) != nullptr);
    AUREA_CHECK(rig.layer(id)->tracks.find(TrackProperty::VectorParam, 0, vector_modifier_param(1, kVecModTwistAngle)) == nullptr);
    // Desligar e remover (a trilha do removido sai).
    AUREA_CHECK(rig.e.set_vector_modifier_enabled(id, 0, 2, false));
    AUREA_CHECK(!rig.layer(id)->shape.vector.groups[0].modifiers[2].enabled);
    AUREA_CHECK(rig.e.remove_vector_modifier(id, 0, 0));
    AUREA_CHECK_EQ(rig.layer(id)->shape.vector.groups[0].modifiers.size(), static_cast<usize>(2));
    AUREA_CHECK(rig.layer(id)->tracks.find(TrackProperty::VectorParam, 0, vector_modifier_param(0, kVecModTwistAngle)) == nullptr);
    AUREA_CHECK(rig.layer(id)->shape.vector.groups[0].modifiers[0].type == VectorModifierType::OffsetPaths);
    // Desfazer devolve o Torcer com a trilha dele.
    AUREA_CHECK(rig.undo());
    AUREA_CHECK_EQ(rig.layer(id)->shape.vector.groups[0].modifiers.size(), static_cast<usize>(3));
    AUREA_CHECK(rig.layer(id)->tracks.find(TrackProperty::VectorParam, 0, vector_modifier_param(0, kVecModTwistAngle)) != nullptr);

    // Um documento v5 vindo da UI antiga não apaga os operadores da camada.
    std::vector<f32> doc;
    std::string names;
    AUREA_CHECK(rig.e.vector_document(id, doc, names));
    VectorData plain;
    AUREA_CHECK(decode_document(doc.data(), doc.size(), names, plain));
    plain.groups[0].fill.paint.opacity = 50.0f;
    plain.groups[0].modifiers.clear();
    encode_document(plain, doc, names);
    doc.erase(doc.end() - 2);   // sem operadores (nenhum no fluxo)
    // Repetidor: os dois floats da v6 logo depois do bloco do repetidor.
    const VectorGroup& g0 = plain.groups[0];
    usize pos = 2 + 10 + 2 + 11 + 5 * g0.fill.paint.stops.size() + 7 + g0.stroke.dashes.size() + 11 + 5 * g0.stroke.paint.stops.size()
              + 3 + 1 + g0.stroke.widthProfile.size() + 1 + 5 + 12;
    doc.erase(doc.begin() + static_cast<std::ptrdiff_t>(pos), doc.begin() + static_cast<std::ptrdiff_t>(pos + 2));
    doc[0] = 5.0f;
    AUREA_CHECK(rig.e.set_vector_document(id, doc.data(), doc.size(), names, false));
    AUREA_CHECK_NEAR(rig.layer(id)->shape.vector.groups[0].fill.paint.opacity, 50.0f, 0.0f);
    AUREA_CHECK_EQ(rig.layer(id)->shape.vector.groups[0].modifiers.size(), static_cast<usize>(3));

    // Salvar e reabrir: os operadores e as trilhas voltam iguais.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_vector_modifiers.aurea";
    const VectorData before = rig.layer(id)->shape.vector;
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    const Layer* reopened = nullptr;
    if (comp) {
        comp->layers().for_each([&](LayerId, const Layer& l) {
            if (l.kind == LayerKind::Shape && l.shape.shapeType == kShapeVector) reopened = &l;
        });
    }
    AUREA_CHECK(reopened != nullptr);
    if (reopened) {
        AUREA_CHECK(reopened->shape.vector == before);
        AUREA_CHECK(reopened->tracks.find(TrackProperty::VectorParam, 0, vector_modifier_param(0, kVecModTwistAngle)) != nullptr);
    }
    std::remove(path.c_str());
}

// AE 2026 (segunda leva): Ondular anda em SEGUNDOS da composição. A caixa do
// conteúdo que o motor publica (Engine::query_layer_detail) e a chave da fonte
// das partículas usam o fps da composição, não os 30 padrão: numa composição a
// 60 qps, o quadro 60 é 1 s.
AUREA_TEST(VectorModifiers, WiggleUsesTheCompositionFpsInEngineBoxAndParticles) {
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 60.0, nullptr).ok());
    const auto made = e.add_vector_layer(1);
    AUREA_CHECK(made.ok());
    if (!made.ok()) { e.shutdown(); return; }
    const u64 id = *made;
    AUREA_CHECK_EQ(e.add_vector_modifier(id, 0, static_cast<u32>(VectorModifierType::WigglePaths)), 0);
    // Ondular forte para a caixa mudar de verdade com o tempo.
    AUREA_CHECK(e.set_vector_param(id, 0, vector_modifier_param(0, kVecModRoughSize), 40.0f, false));
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = TickNs{60 * 1'000'000'000ll / 60 + 1'000};   // quadro 60 a 60 qps = 1 s
    AUREA_CHECK(e.apply_command(seek).ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    AUREA_CHECK(l != nullptr);
    if (!l) { e.shutdown(); return; }
    const FrameIndex local = l->local_time(FrameIndex{60});
    auto box = [&](f64 fps, Vec2& mn, Vec2& mx) {
        std::vector<VectorGroup> ev;
        for (u32 gi = 0; gi < l->shape.vector.groups.size(); ++gi)
            ev.push_back(evaluate_group(l->shape.vector.groups[gi], l->tracks, gi, static_cast<f64>(local.value), fps));
        return bounds_of(ev, static_cast<f64>(local.value), mn, mx);
    };
    Vec2 mn60, mx60, mn30, mx30;
    AUREA_CHECK(box(60.0, mn60, mx60) && box(30.0, mn30, mx30));
    // O teste só prova algo se 1 s e 2 s dão caixas diferentes.
    AUREA_CHECK(std::fabs((mx60.x - mn60.x) - (mx30.x - mn30.x)) > 0.5f || std::fabs((mx60.y - mn60.y) - (mx30.y - mn30.y)) > 0.5f);
    bridge::LayerDetailPOD d{};
    AUREA_CHECK(e.query_layer_detail(id, d));
    std::printf("    caixa a 60 qps: %ux%u (esperado %.0fx%.0f; com 30 qps seria %.0fx%.0f)\n", d.sourceWidth, d.sourceHeight,
                std::ceil(mx60.x - mn60.x), std::ceil(mx60.y - mn60.y), std::ceil(mx30.x - mn30.x), std::ceil(mx30.y - mn30.y));
    AUREA_CHECK_EQ(d.sourceWidth, static_cast<u32>(std::max(1.0f, std::ceil(mx60.x - mn60.x))));
    AUREA_CHECK_EQ(d.sourceHeight, static_cast<u32>(std::max(1.0f, std::ceil(mx60.y - mn60.y))));
    // Partículas: a chave da fonte vetorial depende do fps (o mesmo quadro em outro tempo).
    const u64 k60 = particles::source_key(*l, static_cast<u32>(ParticleEmitter::Layer), 0, local, 60.0);
    const u64 k60b = particles::source_key(*l, static_cast<u32>(ParticleEmitter::Layer), 0, local, 60.0);
    const u64 k30 = particles::source_key(*l, static_cast<u32>(ParticleEmitter::Layer), 0, local, 30.0);
    AUREA_CHECK_EQ(k60, k60b);
    AUREA_CHECK(k60 != k30);
    e.shutdown();
}

// Aspereza com Frequência temporal > 0 anda no tempo (e volta igual no mesmo
// instante); com 0 é parada (RoughenIsDeterministicAndBounded).
AUREA_TEST(VectorModifiers, RoughenMovesOnlyWithTemporalFrequency) {
    auto rough = [](f64 t, f32 freq) {
        std::vector<BezierPath> p{square()};
        roughen(p, 6.0f, 3.0f, true, t, freq, 50.0f, 0.0f, 0.0f, 11);
        return p;
    };
    AUREA_CHECK(!same_paths(rough(0.0, 2.0f), rough(0.4, 2.0f)));
    AUREA_CHECK(same_paths(rough(0.4, 2.0f), rough(0.4, 2.0f)));
    AUREA_CHECK(same_paths(rough(0.0, 0.0f), rough(0.4, 0.0f)));
    // Pelo grupo: o operador Aspereza só vira "animado" com frequência > 0.
    VectorGroup g = square_group();
    VectorModifier m = make_modifier(VectorModifierType::Roughen);
    m.params[kVecModRoughFreq] = 2.0f;
    g.modifiers.push_back(m);
    AUREA_CHECK(modifier_animated(g.modifiers[0]));
    TrackSet none;
    GroupGeometry a, b;
    group_geometry(evaluate_group(g, none, 0, 0.0, 30.0), 0.0, 0.05f, a);
    group_geometry(evaluate_group(g, none, 0, 12.0, 30.0), 12.0, 0.05f, b);
    AUREA_CHECK(std::fabs(area_of(a.fill) - area_of(b.fill)) > 1.0);
}

// Os sete operadores empilhados num retângulo (1920 vértices, ~10 mil pontos
// achatados) com preenchimento e contorno redondo de 8 px: a geometria sai em
// dezenas de milissegundos e CERTA. Antes, o contorno (uma peça por segmento,
// unidas no arranjo) passava de 2 minutos e travava a prévia e a exportação.
// O contorno desta forma tem centenas de milhares de cruzamentos consigo
// mesmo: passa do orçamento do arranjo exato e sai pela amostragem fina — a
// conferência ponto a ponto (dentro = a até meia largura do caminho) cobre
// esse caminho também.
AUREA_TEST(VectorModifiers, SevenOperatorStackBuildsFast) {
    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Rect;
    p.center = Vec2{0, 0};
    p.size = Vec2{300, 200};
    g.paths.push_back(p);
    g.fill.enabled = true;
    g.stroke.enabled = true;
    g.stroke.width = 8.0f;
    g.stroke.join = 1;
    for (VectorModifierType t : {VectorModifierType::OffsetPaths, VectorModifierType::PuckerBloat, VectorModifierType::RoundCorners,
                                 VectorModifierType::Twist, VectorModifierType::ZigZag, VectorModifierType::Roughen,
                                 VectorModifierType::WigglePaths}) {
        g.modifiers.push_back(make_modifier(t));
    }
    const f32 tol = 0.1f;   // a malha usa 0,2 / densidade
    using clk = std::chrono::steady_clock;
    auto ms = [](clk::time_point a, clk::time_point b) { return std::chrono::duration<f64, std::milli>(b - a).count(); };

    // Os caminhos depois dos operadores, achatados (o que o preenchimento e o contorno recebem).
    std::vector<BezierPath> beziers{path_at(p, 0.0)};
    apply_modifiers(g.modifiers, beziers, tol);
    std::vector<Contour> lines;
    usize verts = 0, pts = 0;
    for (const BezierPath& b : beziers) {
        verts += b.v.size();
        Contour c;
        flatten(b, tol, c);
        c.closed = true;
        pts += c.pts.size();
        lines.push_back(std::move(c));
    }
    // O melhor de três: a máquina de teste roda outras compilações ao mesmo tempo.
    std::vector<Contour> fill, stroke;
    GroupGeometry geo;
    f64 fillMs = 1e9, strokeMs = 1e9, groupMs = 1e9;
    for (int run = 0; run < 3; ++run) {
        const auto t0 = clk::now();
        resolve_fill(lines, FillRule::NonZero, fill);
        const auto t1 = clk::now();
        stroke_to_rings(lines, 8.0f, 0, 1, 4.0f, tol, stroke);
        const auto t2 = clk::now();
        group_geometry(g, 0.0, tol, geo);
        const auto t3 = clk::now();
        fillMs = std::min(fillMs, ms(t0, t1));
        strokeMs = std::min(strokeMs, ms(t1, t2));
        groupMs = std::min(groupMs, ms(t2, t3));
    }
    std::printf("    %zu vertices, %zu pontos: preenchimento %.1f ms (%zu aneis), contorno %.1f ms (%zu aneis), grupo inteiro %.1f ms\n",
                verts, pts, fillMs, fill.size(), strokeMs, stroke.size(), groupMs);
    // O preenchimento desta forma cabe no arranjo exato; o contorno (centenas
    // de milhares de cruzamentos consigo mesmo) passa do orçamento.
    {
        std::vector<Contour> again;
        const ClipStats s0 = clip_stats();
        resolve_fill(lines, FillRule::NonZero, again);
        const ClipStats s1 = clip_stats();
        stroke_to_rings(lines, 8.0f, 0, 1, 4.0f, tol, again);
        const ClipStats s2 = clip_stats();
        std::printf("    preenchimento: %llu arestas, %llu cruzamentos (exato); contorno: %llu arestas (amostragem)\n",
                    static_cast<unsigned long long>(s1.lastEdges), static_cast<unsigned long long>(s1.lastCrossings),
                    static_cast<unsigned long long>(s2.lastEdges));
        AUREA_CHECK_EQ(s1.sampled, s0.sampled);
        AUREA_CHECK_EQ(s2.sampled, s1.sampled + 1);
    }
    AUREA_CHECK(verts > 1000);
    AUREA_CHECK(!fill.empty() && !stroke.empty());
    AUREA_CHECK(!geo.fill.empty() && !geo.stroke.empty());
    AUREA_CHECK(fillMs < 200.0);
    AUREA_CHECK(strokeMs < 200.0);
    AUREA_CHECK(groupMs < 400.0);

    // Conferência ponto a ponto contra a definição: o contorno redondo é tudo
    // que fica a até meia largura do caminho; o preenchimento, o enrolamento
    // não-zero do caminho. Pontos colados na borda (tolerância do arco e da
    // amostragem) ficam de fora da conta.
    auto dist_to_path = [&](Vec2 q) {
        f32 best = 1e18f;
        for (const Contour& c : lines) {
            const usize n = c.pts.size();
            for (usize i = 0, j = n - 1; i < n; j = i++) {
                const Vec2 a = c.pts[j], d = c.pts[i] - a;
                const f32 l2 = d.length_sq();
                const f32 t = l2 > 0.0f ? std::clamp((q - a).dot(d) / l2, 0.0f, 1.0f) : 0.0f;
                best = std::min(best, (q - (a + d * t)).length_sq());
            }
        }
        return std::sqrt(best);
    };
    auto winding = [](const std::vector<Contour>& rings, Vec2 q) {
        int w = 0;
        for (const Contour& c : rings) {
            const usize n = c.pts.size();
            for (usize i = 0, j = n - 1; i < n; j = i++) {
                const Vec2 a = c.pts[j], b = c.pts[i];
                if ((a.y <= q.y) == (b.y <= q.y)) continue;
                if (a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y) > q.x) w += b.y > a.y ? 1 : -1;
            }
        }
        return w;
    };
    Vec2 mn = lines[0].pts[0], mx = mn;
    for (const Contour& c : lines) for (Vec2 q : c.pts) { mn = Vec2{std::min(mn.x, q.x), std::min(mn.y, q.y)}; mx = Vec2{std::max(mx.x, q.x), std::max(mx.y, q.y)}; }
    u32 st = 2026;
    auto rnd = [&]() { st = st * 1664525u + 1013904223u; return static_cast<f32>((st >> 8) & 0xFFFF) / 65535.0f; };
    usize tested = 0, strokeWrong = 0, fillWrong = 0, groupWrong = 0, strokeIn = 0;
    for (int k = 0; k < 2500; ++k) {
        const Vec2 q{mn.x - 6.0f + rnd() * (mx.x - mn.x + 12.0f), mn.y - 6.0f + rnd() * (mx.y - mn.y + 12.0f)};
        const f32 d = dist_to_path(q);
        if (std::fabs(d - 4.0f) < 0.2f || d < 0.1f) continue;
        ++tested;
        const bool wantStroke = d < 4.0f;
        strokeIn += wantStroke ? 1 : 0;
        if ((winding(stroke, q) != 0) != wantStroke) ++strokeWrong;
        if ((winding(geo.stroke, q) != 0) != wantStroke) ++groupWrong;
        if ((winding(fill, q) != 0) != (winding(lines, q) != 0)) ++fillWrong;
    }
    std::printf("    %zu pontos conferidos (%zu dentro do contorno): contorno %zu errados, grupo %zu, preenchimento %zu\n",
                tested, strokeIn, strokeWrong, groupWrong, fillWrong);
    AUREA_CHECK(tested > 1500 && strokeIn > 200);
    AUREA_CHECK_EQ(strokeWrong, static_cast<usize>(0));
    AUREA_CHECK_EQ(fillWrong, static_cast<usize>(0));
    // O grupo refaz os operadores (o Deslocar passa pelo contorno): mesma forma, mesma conferência.
    AUREA_CHECK_EQ(groupWrong, static_cast<usize>(0));
}

namespace {
/// O contorno de antes, peça por peça (faixa por segmento, junta, ponta), unido
/// no arranjo: a referência do contorno em anéis (que tem de dar a mesma área).
void stroke_pieces(const Contour& src, f32 width, u8 cap, u8 join, f32 miterLimit, f32 tol, const StrokeTaper& taper,
                   std::vector<Contour>& pieces) {
    auto lnorm = [](Vec2 d) { return Vec2{-d.y, d.x}; };
    auto cross = [](Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; };
    auto push = [&](std::vector<Vec2> pts) {
        if (pts.size() < 3 || std::fabs(signed_area(pts)) < 1e-9) return;
        if (signed_area(pts) < 0.0) std::reverse(pts.begin(), pts.end());
        pieces.push_back(Contour{std::move(pts), true});
    };
    auto arc = [&](Vec2 c, f32 r, f32 a0, f32 a1, std::vector<Vec2>& out) {
        const f32 step = 2.0f * std::acos(std::clamp(1.0f - tol / std::max(r, 1e-3f), -1.0f, 1.0f));
        const int n = std::clamp(static_cast<int>(std::ceil(std::fabs(a1 - a0) / std::max(step, 1e-3f))), 1, 256);
        for (int i = 0; i <= n; ++i) {
            const f32 a = a0 + (a1 - a0) * static_cast<f32>(i) / static_cast<f32>(n);
            out.push_back(c + Vec2{std::cos(a), std::sin(a)} * r);
        }
    };
    const f32 hw0 = width * 0.5f;
    std::vector<Vec2> P;
    for (Vec2 q : src.pts) if (P.empty() || (q - P.back()).length_sq() > 1e-8f) P.push_back(q);
    bool closed = src.closed;
    if (closed && P.size() > 2 && (P.back() - P.front()).length_sq() <= 1e-8f) P.pop_back();
    std::vector<f32> H(P.size(), hw0);
    if (taper.active()) {
        if (closed) { P.push_back(P.front()); closed = false; }
        f64 L = 0.0;
        for (usize i = 1; i < P.size(); ++i) L += (P[i] - P[i - 1]).length();
        const f64 maxSeg = std::max(0.5, L / 96.0);
        std::vector<Vec2> R{P[0]};
        for (usize i = 1; i < P.size(); ++i) {
            const int k = std::clamp(static_cast<int>(std::ceil((P[i] - P[i - 1]).length() / maxSeg)), 1, 512);
            for (int j = 1; j <= k; ++j) R.push_back(P[i - 1] + (P[i] - P[i - 1]) * (static_cast<f32>(j) / static_cast<f32>(k)));
        }
        P.swap(R);
        H.assign(P.size(), 0.0f);
        f64 s = 0.0;
        for (usize i = 0; i < P.size(); ++i) {
            if (i) s += (P[i] - P[i - 1]).length();
            H[i] = hw0 * taper_factor(taper, L > 0.0 ? static_cast<f32>(s / L) : 0.0f);
        }
    }
    const usize n = P.size();
    const usize segs = closed ? n : n - 1;
    for (usize i = 0; i < segs; ++i) {
        const Vec2 a = P[i], b = P[(i + 1) % n], d = (b - a).normalized();
        push({a + lnorm(d) * H[i], b + lnorm(d) * H[(i + 1) % n], b - lnorm(d) * H[(i + 1) % n], a - lnorm(d) * H[i]});
    }
    for (usize i = closed ? 0 : 1; i < (closed ? n : n - 1); ++i) {
        const Vec2 v = P[i];
        const f32 hw = H[i];
        if (hw <= 1e-4f) continue;
        const Vec2 d0 = (v - P[(i + n - 1) % n]).normalized(), d1 = (P[(i + 1) % n] - v).normalized();
        const f32 cr = cross(d0, d1), dt = d0.dot(d1);
        if (std::fabs(cr) < 1e-6f && dt > 0.0f) continue;
        const f32 side = cr > 0.0f ? -1.0f : 1.0f;
        const Vec2 n0 = lnorm(d0) * side, n1 = lnorm(d1) * side;
        const Vec2 A = v + n0 * hw, B = v + n1 * hw;
        if (join == 1) {
            std::vector<Vec2> fan{v};
            f32 a0 = std::atan2(n0.y, n0.x), a1 = std::atan2(n1.y, n1.x);
            while (a1 - a0 > kPi) a1 -= 2.0f * kPi;
            while (a0 - a1 > kPi) a1 += 2.0f * kPi;
            arc(v, hw, a0, a1, fan);
            push(fan);
        } else {
            const Vec2 bis = (n0 + n1).normalized();
            const f32 cosHalf = bis.dot(n0);
            if (join == 0 && cosHalf > 1e-4f && 1.0f / cosHalf <= std::max(1.0f, miterLimit)) push({v, A, v + bis * (hw / cosHalf), B});
            else push({v, A, B});
        }
    }
    if (!closed && cap != 0) {
        for (int end = 0; end < 2; ++end) {
            const Vec2 p = end == 0 ? P[0] : P[n - 1];
            const f32 hw = end == 0 ? H[0] : H[n - 1];
            if (hw <= 1e-4f) continue;
            const Vec2 d = end == 0 ? (P[0] - P[1]).normalized() : (P[n - 1] - P[n - 2]).normalized();
            const Vec2 nn = lnorm(d) * hw;
            if (cap == 2) { push({p + nn, p + nn + d * hw, p - nn + d * hw, p - nn}); continue; }
            std::vector<Vec2> fan;
            const f32 a0 = std::atan2(nn.y, nn.x);
            arc(p, hw, a0, a0 - kPi, fan);
            push(fan);
        }
    }
}

/// As peças de todos os caminhos unidas pelo arranjo EXATO (sem o teto do
/// orçamento: a referência é justamente o jeito caro de antes).
std::vector<Contour> stroke_by_pieces(const std::vector<Contour>& srcs, f32 width, u8 cap, u8 join, f32 miterLimit, f32 tol,
                                      const StrokeTaper& taper = {}) {
    std::vector<Contour> pieces, out;
    for (const Contour& c : srcs) stroke_pieces(c, width, cap, join, miterLimit, tol, taper, pieces);
    clip_set_budget_scale(1.0e12);
    resolve_fill(pieces, FillRule::NonZero, out);
    clip_set_budget_scale(1.0);
    return out;
}

/// Área da diferença simétrica entre dois conjuntos de anéis (arranjo exato).
f64 xor_area(const std::vector<Contour>& a, const std::vector<Contour>& b) {
    std::vector<Contour> x;
    clip_set_budget_scale(1.0e12);
    boolean_op({a, b}, FillRule::NonZero, BoolOp::Exclude, x);
    clip_set_budget_scale(1.0);
    f64 s = 0.0;
    for (const Contour& r : x) s += std::fabs(signed_area(r.pts));
    return s;
}

f64 perimeter_of(const std::vector<Contour>& rings) {
    f64 s = 0.0;
    for (const Contour& r : rings) s += length_of(r);
    return s;
}
} // namespace

// O contorno em anéis (um por lado, resolvido de uma vez) cobre a mesma área
// que a união das peças de antes: linhas, voltas de 180°, zigue-zague, estrela,
// passeios aleatórios abertos e fechados, círculo menor que a meia largura,
// com as três juntas, as três pontas e o afinar.
AUREA_TEST(VectorModifiers, StrokeRingsMatchTheUnionOfPieces) {
    std::vector<std::pair<const char*, Contour>> shapes;
    shapes.push_back({"quadrado", Contour{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true}});
    shapes.push_back({"quadrado aberto", Contour{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, false}});
    shapes.push_back({"volta exata", Contour{{{0, 0}, {100, 0}, {0, 0.0f}}, false}});
    shapes.push_back({"volta fechada", Contour{{{0, 0}, {100, 0}, {0, 3.0f}, {80, 6}}, false}});
    shapes.push_back({"reta", Contour{{{0, 0}, {100, 0}}, false}});
    Contour zz;
    for (int i = 0; i < 40; ++i) zz.pts.push_back(Vec2{static_cast<f32>(i * 3), (i % 2) ? 10.0f : -10.0f});
    shapes.push_back({"zigue-zague", zz});
    Contour star;
    star.closed = true;
    for (int i = 0; i < 24; ++i) {
        const f32 a = static_cast<f32>(i) * kPi * 2.0f / 24.0f, r = (i % 2) ? 20.0f : 80.0f;
        star.pts.push_back(Vec2{r * std::cos(a), r * std::sin(a)});
    }
    shapes.push_back({"estrela", star});
    u32 st = 12345;
    auto rnd = [&]() { st = st * 1664525u + 1013904223u; return static_cast<f32>((st >> 8) & 0xFFFF) / 65535.0f; };
    for (int k = 0; k < 6; ++k) {
        Contour w;
        w.closed = k % 2 == 0;
        Vec2 q{0, 0};
        for (int i = 0; i < 80; ++i) { q += Vec2{rnd() * 6 - 3, rnd() * 6 - 3}; w.pts.push_back(q); }
        shapes.push_back({"passeio", w});
    }
    Contour circ;
    circ.closed = true;
    for (int i = 0; i < 64; ++i) {
        const f32 a = static_cast<f32>(i) * kPi * 2.0f / 64.0f;
        circ.pts.push_back(Vec2{5 * std::cos(a), 5 * std::sin(a)});
    }
    shapes.push_back({"circulo pequeno", circ});
    f64 worst = 0.0;
    usize cases = 0;
    const u64 sampledBefore = clip_stats().sampled;
    for (auto& [name, c] : shapes) {
        for (u8 join = 0; join < 3; ++join) for (u8 cap = 0; cap < 3; ++cap) for (int tp = 0; tp < 2; ++tp) for (f32 w : {2.0f, 12.0f}) {
            if (c.closed && cap) continue;
            StrokeTaper t{};
            if (tp) { t.start = 0.3f; t.end = 0.2f; }
            std::vector<Contour> rings;
            stroke_to_rings({c}, w, cap, join, 4.0f, 0.05f, rings, t);
            const std::vector<Contour> ref = stroke_by_pieces({c}, w, cap, join, 4.0f, 0.05f, t);
            const f64 xa = xor_area(rings, ref);
            worst = std::max(worst, xa);
            ++cases;
            if (xa > 0.5) std::printf("    %s junta %d ponta %d afinar %d largura %.0f: %.3f x %.3f (xor %.4f)\n", name, join, cap, tp, w, area_of(rings), area_of(ref), xa);
        }
    }
    std::printf("    %zu casos, maior diferenca %.4f px2\n", cases, worst);
    // Os dois lados pelo arranjo exato: só o arredondamento na grade de 1/256 px.
    AUREA_CHECK(worst < 0.5);
    AUREA_CHECK_EQ(clip_stats().sampled, sampledBefore);   // nenhum destes contornos passa do orçamento
}

// O conteúdo de todo dia fica no caminho EXATO do recorte (o orçamento é para
// o caso patológico): traço à mão livre de 300 a 600 pontos com contorno largo
// e junta redonda, um rabisco de vai e vem que passa por cima de si mesmo,
// contornos de texto (letras com furo e haste) e um conjunto de estrelas e
// polígonos com contorno. Nenhum sai pela amostragem (clip_stats), e a
// geometria é a da união das peças de antes, a menos do arredondamento na
// grade de 1/256 px — o desenho do usuário não muda de cara.
AUREA_TEST(VectorModifiers, TypicalContentStaysOnTheExactPath) {
    using clk = std::chrono::steady_clock;
    u32 st = 4242;
    auto rnd = [&]() { st = st * 1664525u + 1013904223u; return static_cast<f32>((st >> 8) & 0xFFFF) / 65535.0f; };
    // Mão livre: passos de ~4 px com a direção variando devagar (curvas largas e algumas voltas).
    auto freehand = [&](int n) {
        Contour c;
        Vec2 q{0, 0};
        f32 ang = 0.0f, turn = 0.0f;
        for (int i = 0; i < n; ++i) {
            turn = turn * 0.9f + (rnd() - 0.5f) * 0.25f;
            ang += turn;
            q += Vec2{std::cos(ang), std::sin(ang)} * 4.0f;
            c.pts.push_back(q);
        }
        return c;
    };
    // Rabisco: vai e vem preenchendo uma área, cada passada por cima da anterior.
    auto scribble = [&](int n) {
        Contour c;
        for (int i = 0; i < n; ++i) {
            const f32 t = static_cast<f32>(i);
            c.pts.push_back(Vec2{80.0f + 70.0f * std::sin(t * 0.35f) + (rnd() - 0.5f) * 6.0f, t * 0.6f + 10.0f * std::sin(t * 0.9f) + (rnd() - 0.5f) * 4.0f});
        }
        return c;
    };
    auto ellipse = [](Vec2 o, f32 rx, f32 ry, int n, bool reversed) {
        Contour c;
        c.closed = true;
        for (int i = 0; i < n; ++i) {
            const f32 a = static_cast<f32>(reversed ? n - i : i) * kPi * 2.0f / static_cast<f32>(n);
            c.pts.push_back(o + Vec2{rx * std::cos(a), ry * std::sin(a)});
        }
        return c;
    };
    // Letras: um bojo com furo (o, e, a…) e, em metade delas, uma haste que entra no bojo (b, d, p, q).
    std::vector<Contour> glyphs;
    for (int k = 0; k < 28; ++k) {
        const Vec2 o{static_cast<f32>(k % 14) * 34.0f, static_cast<f32>(k / 14) * 70.0f};
        glyphs.push_back(ellipse(o, 13.0f, 17.0f, 28, false));
        glyphs.push_back(ellipse(o, 7.5f, 11.0f, 20, true));
        if (k % 2 == 0) {
            const f32 x = k % 4 == 0 ? -15.0f : 9.0f, y0 = k % 3 == 0 ? -40.0f : -17.0f, y1 = k % 3 == 0 ? 17.0f : 38.0f;
            glyphs.push_back(Contour{{o + Vec2{x, y0}, o + Vec2{x + 6.0f, y0}, o + Vec2{x + 6.0f, y1}, o + Vec2{x, y1}}, true});
        }
    }
    // Estrelas e polígonos, uns por cima dos outros.
    std::vector<Contour> stars;
    for (int k = 0; k < 14; ++k) {
        Contour c;
        c.closed = true;
        const Vec2 o{static_cast<f32>(k % 5) * 70.0f + rnd() * 20.0f, static_cast<f32>(k / 5) * 75.0f + rnd() * 20.0f};
        const bool star = k % 2 == 0;
        const int n = star ? 2 * (5 + k % 6) : 3 + k % 6;
        const f32 r = 40.0f + rnd() * 25.0f, phase = rnd() * 3.0f;
        for (int i = 0; i < n; ++i) {
            const f32 a = phase + static_cast<f32>(i) * kPi * 2.0f / static_cast<f32>(n);
            c.pts.push_back(o + Vec2{std::cos(a), std::sin(a)} * (star && (i % 2) ? r * 0.45f : r));
        }
        stars.push_back(c);
    }
    struct Case { const char* name; std::vector<Contour> lines; f32 width; u8 cap, join; };
    const std::vector<Case> cases = {
        {"mao livre 300 pts, largura 20", {freehand(300)}, 20.0f, 1, 1},
        {"mao livre 450 pts, largura 30", {freehand(450)}, 30.0f, 1, 1},
        {"mao livre 600 pts, largura 40", {freehand(600)}, 40.0f, 1, 1},
        {"rabisco 260 pts, largura 10", {scribble(260)}, 10.0f, 1, 1},
        {"rabisco 400 pts, largura 6", {scribble(400)}, 6.0f, 1, 1},
        {"letras, contorno 2,5 em ponta", glyphs, 2.5f, 0, 0},
        {"letras, contorno 6 redondo", glyphs, 6.0f, 0, 1},
        {"estrelas, contorno 8 em ponta", stars, 8.0f, 0, 0},
        {"estrelas, contorno 8 chanfrado", stars, 8.0f, 0, 2},
        {"estrelas, contorno 14 redondo", stars, 14.0f, 0, 1},
    };
    const f32 tol = 0.1f;
    for (const Case& k : cases) {
        const ClipStats before = clip_stats();
        std::vector<Contour> rings;
        const auto t0 = clk::now();
        stroke_to_rings(k.lines, k.width, k.cap, k.join, 4.0f, tol, rings);
        const f64 ms = std::chrono::duration<f64, std::milli>(clk::now() - t0).count();
        const ClipStats after = clip_stats();
        const std::vector<Contour> ref = stroke_by_pieces(k.lines, k.width, k.cap, k.join, 4.0f, tol);
        const f64 xa = xor_area(rings, ref), per = perimeter_of(ref);
        std::printf("    %-32s %5llu arestas, %5llu cruzamentos, %5.1f ms, %s; contra as pecas: %.3f px2 em %.0f px de borda (%.4f px)\n", k.name,
                    static_cast<unsigned long long>(after.lastEdges), static_cast<unsigned long long>(after.lastCrossings), ms,
                    after.sampled == before.sampled ? "exato" : "AMOSTRAGEM", xa, per, per > 0.0 ? xa / per : 0.0);
        AUREA_CHECK_EQ(after.sampled, before.sampled);
        AUREA_CHECK_EQ(after.exact, before.exact + 1);
        AUREA_CHECK(!rings.empty() && per > 0.0);
        // Desvio médio da borda abaixo de 1/256 px (o passo da grade do arranjo).
        AUREA_CHECK(xa <= per / 256.0);
    }
    // O preenchimento das letras (furos e hastes) e das estrelas, nas duas
    // regras: exato, e igual ao enrolamento dos caminhos ponto a ponto.
    auto winding = [](const std::vector<Contour>& rings, Vec2 q) {
        int w = 0;
        for (const Contour& c : rings) {
            const usize n = c.pts.size();
            for (usize i = 0, j = n - 1; i < n; j = i++) {
                const Vec2 a = c.pts[j], b = c.pts[i];
                if ((a.y <= q.y) == (b.y <= q.y)) continue;
                if (a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y) > q.x) w += b.y > a.y ? 1 : -1;
            }
        }
        return w;
    };
    auto near_edge = [](const std::vector<Contour>& cs, Vec2 q, f32 d) {
        for (const Contour& c : cs) {
            const usize n = c.pts.size();
            for (usize i = 0, j = n - 1; i < n; j = i++) {
                const Vec2 a = c.pts[j], e = c.pts[i] - a;
                const f32 l2 = e.length_sq();
                const f32 t = l2 > 0.0f ? std::clamp((q - a).dot(e) / l2, 0.0f, 1.0f) : 0.0f;
                if ((q - (a + e * t)).length_sq() < d * d) return true;
            }
        }
        return false;
    };
    for (const std::vector<Contour>* set : {&glyphs, &stars}) {
        for (FillRule rule : {FillRule::NonZero, FillRule::EvenOdd}) {
            const ClipStats before = clip_stats();
            std::vector<Contour> fill;
            resolve_fill(*set, rule, fill);
            AUREA_CHECK_EQ(clip_stats().sampled, before.sampled);
            Vec2 mn = (*set)[0].pts[0], mx = mn;
            for (const Contour& c : *set) for (Vec2 q : c.pts) { mn = Vec2{std::min(mn.x, q.x), std::min(mn.y, q.y)}; mx = Vec2{std::max(mx.x, q.x), std::max(mx.y, q.y)}; }
            usize tested = 0, wrong = 0;
            for (int i = 0; i < 1500; ++i) {
                const Vec2 q{mn.x + rnd() * (mx.x - mn.x), mn.y + rnd() * (mx.y - mn.y)};
                if (near_edge(*set, q, 0.02f)) continue;
                ++tested;
                const int w = winding(*set, q);
                const bool want = rule == FillRule::EvenOdd ? (w & 1) != 0 : w != 0;
                if ((winding(fill, q) != 0) != want) ++wrong;
            }
            AUREA_CHECK(tested > 1000);
            AUREA_CHECK_EQ(wrong, static_cast<usize>(0));
        }
    }
}

// Quando o recorte passa do orçamento, o passo da amostragem é a tolerância de
// quem chama (a do achatamento, que já traz a escala na tela) — não um número
// preso ao tamanho da forma. Um polígono de 400 cordas aleatórias (~19 mil
// cruzamentos) contra o arranjo exato: com tolerância fina o desvio médio da
// borda fica em centésimos de px e é várias vezes menor que com a grossa. E um
// contorno do tamanho de uma tela cheia (a pilha dos sete operadores ampliada
// para ~2200 px) sai certo a 0,2 px em tempo limitado — com a grade antiga,
// presa a ~4 M células, o passo ali era de ~0,7 px.
AUREA_TEST(VectorModifiers, SampledFallbackFollowsTheCallerTolerance) {
    u32 st = 99;
    auto rnd = [&]() { st = st * 1664525u + 1013904223u; return static_cast<f32>((st >> 8) & 0xFFFF) / 65535.0f; };
    Contour chords;
    chords.closed = true;
    for (int i = 0; i < 400; ++i) chords.pts.push_back(Vec2{rnd() * 500.0f, rnd() * 500.0f});
    std::vector<Contour> exact, coarse, fine;
    clip_set_budget_scale(1.0e12);
    resolve_fill({chords}, FillRule::NonZero, exact);
    const u64 crossings = clip_stats().lastCrossings;
    clip_set_budget_scale(1.0);
    const u64 s0 = clip_stats().sampled;
    {
        const ClipToleranceScope scale(0.4f);
        resolve_fill({chords}, FillRule::NonZero, coarse);
    }
    {
        const ClipToleranceScope scale(0.05f);
        resolve_fill({chords}, FillRule::NonZero, fine);
    }
    AUREA_CHECK_EQ(clip_stats().sampled, s0 + 2);   // os dois passaram do orçamento
    const f64 per = perimeter_of(exact), xc = xor_area(coarse, exact), xf = xor_area(fine, exact);
    std::printf("    400 cordas (%llu cruzamentos): borda %.0f px; desvio medio %.4f px com tolerancia 0,4 e %.4f px com 0,05\n",
                static_cast<unsigned long long>(crossings), per, xc / per, xf / per);
    AUREA_CHECK(per > 1000.0);
    AUREA_CHECK(xf / per < 0.02);
    AUREA_CHECK(xf * 3.0 < xc);

    // Tela cheia: a pilha dos sete operadores ampliada 6×, contorno de 48 px.
    VectorPath p;
    p.kind = VectorPathKind::Rect;
    p.center = Vec2{0, 0};
    p.size = Vec2{300, 200};
    std::vector<VectorModifier> mods;
    for (VectorModifierType t : {VectorModifierType::OffsetPaths, VectorModifierType::PuckerBloat, VectorModifierType::RoundCorners,
                                 VectorModifierType::Twist, VectorModifierType::ZigZag, VectorModifierType::Roughen,
                                 VectorModifierType::WigglePaths}) {
        mods.push_back(make_modifier(t));
    }
    std::vector<BezierPath> beziers{path_at(p, 0.0)};
    apply_modifiers(mods, beziers, 0.1f);
    std::vector<Contour> lines;
    Vec2 mn{1e9f, 1e9f}, mx{-1e9f, -1e9f};
    for (const BezierPath& b : beziers) {
        Contour c;
        flatten(b, 0.1f, c);
        c.closed = true;
        for (Vec2& q : c.pts) {
            q = q * 6.0f;
            mn = Vec2{std::min(mn.x, q.x), std::min(mn.y, q.y)};
            mx = Vec2{std::max(mx.x, q.x), std::max(mx.y, q.y)};
        }
        lines.push_back(std::move(c));
    }
    const u64 s1 = clip_stats().sampled;
    std::vector<Contour> stroke;
    const auto t0 = std::chrono::steady_clock::now();
    stroke_to_rings(lines, 48.0f, 0, 1, 4.0f, 0.05f, stroke);
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    AUREA_CHECK_EQ(clip_stats().sampled, s1 + 1);
    auto dist_to_path = [&](Vec2 q) {
        f32 best = 1e18f;
        for (const Contour& c : lines) {
            const usize n = c.pts.size();
            for (usize i = 0, j = n - 1; i < n; j = i++) {
                const Vec2 a = c.pts[j], d = c.pts[i] - a;
                const f32 l2 = d.length_sq();
                const f32 t = l2 > 0.0f ? std::clamp((q - a).dot(d) / l2, 0.0f, 1.0f) : 0.0f;
                best = std::min(best, (q - (a + d * t)).length_sq());
            }
        }
        return std::sqrt(best);
    };
    auto inside = [&](Vec2 q) {
        int w = 0;
        for (const Contour& c : stroke) {
            const usize n = c.pts.size();
            for (usize i = 0, j = n - 1; i < n; j = i++) {
                const Vec2 a = c.pts[j], b = c.pts[i];
                if ((a.y <= q.y) == (b.y <= q.y)) continue;
                if (a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y) > q.x) w += b.y > a.y ? 1 : -1;
            }
        }
        return w != 0;
    };
    usize tested = 0, wrong = 0, nearBorder = 0;
    for (int k = 0; k < 4000; ++k) {
        const Vec2 q{mn.x - 30.0f + rnd() * (mx.x - mn.x + 60.0f), mn.y - 30.0f + rnd() * (mx.y - mn.y + 60.0f)};
        const f32 d = dist_to_path(q);
        if (std::fabs(d - 24.0f) < 0.2f) continue;
        ++tested;
        if (std::fabs(d - 24.0f) < 1.0f) ++nearBorder;
        if (inside(q) != (d < 24.0f)) ++wrong;
    }
    std::printf("    contorno de %.0f x %.0f px por amostragem: %.1f ms, %zu aneis; %zu pontos conferidos (%zu a menos de 1 px da borda), %zu errados\n",
                mx.x - mn.x + 48.0f, mx.y - mn.y + 48.0f, ms, stroke.size(), tested, nearBorder, wrong);
    AUREA_CHECK(mx.x - mn.x > 1500.0f);
    AUREA_CHECK(tested > 3000 && nearBorder > 20);
    AUREA_CHECK_EQ(wrong, static_cast<usize>(0));
    AUREA_CHECK(ms < 400.0);
}

// Traços 1..3 / vãos 1..3 animáveis (VectorParam kVecDash1..kVecGap3): a
// trilha vale só enquanto o par existe no documento; a consulta do motor
// traz os bits dos ids 24.. num bloco próprio.
AUREA_TEST(VectorModifiers, DashValuesAreAnimatable) {
    VectorGroup g = square_group();
    g.stroke.enabled = true;
    g.stroke.dashes = {10.0f, 5.0f, 20.0f, 8.0f, 30.0f, 4.0f};
    TrackSet ts;
    Track& t = ts.get_or_create(TrackProperty::VectorParam, 0, kVecDash2);
    t.set(FrameIndex{0}, 20.0f);
    t.set(FrameIndex{10}, 40.0f);
    Track& gap3 = ts.get_or_create(TrackProperty::VectorParam, 0, kVecGap3);
    gap3.set(FrameIndex{0}, 4.0f);
    gap3.set(FrameIndex{10}, 14.0f);
    const VectorGroup e = evaluate_group(g, ts, 0, 5.0);
    AUREA_CHECK_NEAR(e.stroke.dashes[2], 30.0f, 1e-4f);
    AUREA_CHECK_NEAR(e.stroke.dashes[5], 9.0f, 1e-4f);
    AUREA_CHECK_NEAR(e.stroke.dashes[0], 10.0f, 0.0f);   // sem trilha
    // Só um par no documento: a trilha do vão 3 não vale (nem cria o par).
    VectorGroup one = g;
    one.stroke.dashes = {10.0f, 5.0f};
    const VectorGroup e1 = evaluate_group(one, ts, 0, 5.0);
    AUREA_CHECK_EQ(e1.stroke.dashes.size(), static_cast<usize>(2));
    VectorGroup w = one;
    AUREA_CHECK(param_ref(w, kVecGap1) == &w.stroke.dashes[1]);
    AUREA_CHECK(param_ref(w, kVecDash2) == nullptr);
    AUREA_CHECK_NEAR(clamp_param(kVecDash1, -3.0f), 0.0f, 0.0f);

    // Pelo motor: valor parado, keyframe e os bits do bloco alto.
    ModRig rig;
    const auto made = rig.e.add_vector_layer(1);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const u64 id = *made;
    AUREA_CHECK(!rig.e.set_vector_param(id, 0, kVecDash1, 12.0f, false));   // sem tracejado
    std::vector<f32> doc;
    std::string names;
    AUREA_CHECK(rig.e.vector_document(id, doc, names));
    VectorData d;
    AUREA_CHECK(decode_document(doc.data(), doc.size(), names, d));
    d.groups[0].stroke.enabled = true;
    d.groups[0].stroke.dashes = {10.0f, 5.0f, 20.0f, 8.0f, 30.0f, 4.0f};
    encode_document(d, doc, names);
    AUREA_CHECK(rig.e.set_vector_document(id, doc.data(), doc.size(), names, false));
    AUREA_CHECK(rig.e.set_vector_param(id, 0, kVecDash1, 12.0f, false));
    AUREA_CHECK_NEAR(rig.layer(id)->shape.vector.groups[0].stroke.dashes[0], 12.0f, 0.0f);
    AUREA_CHECK(rig.e.toggle_vector_param_key(id, 0, kVecGap3));
    AUREA_CHECK(rig.e.toggle_vector_param_key(id, 0, kVecTrimEnd));
    f32 out[Engine::kVectorParamFloats]{};
    AUREA_CHECK_EQ(rig.e.query_vector_params(id, 0, out, Engine::kVectorParamFloats), Engine::kVectorParamFloats);
    AUREA_CHECK_NEAR(out[kVecGap3], 4.0f, 0.0f);
    const u32 animLo = static_cast<u32>(out[kVecParamCount]), keysLo = static_cast<u32>(out[kVecParamCount + 1]);
    const u32 animHi = static_cast<u32>(out[kVecParamCount + 2]), keysHi = static_cast<u32>(out[kVecParamCount + 3]);
    AUREA_CHECK_EQ(animLo, 1u << kVecTrimEnd);        // o bloco baixo continua exato
    AUREA_CHECK_EQ(keysLo, 1u << kVecTrimEnd);
    AUREA_CHECK_EQ(animHi, 1u << (kVecGap3 - kVecParamBitsPerFloat));
    AUREA_CHECK_EQ(keysHi, 1u << (kVecGap3 - kVecParamBitsPerFloat));
    // O documento não muda de versão por isso: os traços continuam no fluxo de sempre.
    AUREA_CHECK(rig.e.vector_document(id, doc, names));
    AUREA_CHECK_NEAR(doc[0], 6.0f, 0.0f);
}
