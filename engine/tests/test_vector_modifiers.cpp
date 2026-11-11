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
