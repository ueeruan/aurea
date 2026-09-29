// =============================================================================
//  Ferramentas de edição trazidas do app antigo: otimizar keyframes, copiar e
//  colar a animação inteira, formas novas (trapézio, paralelogramo,
//  engrenagem, seta dupla, octógono…) e o afinar do contorno vetorial.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/animation/KeyframeOptimize.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aurea;

namespace {

struct ToolsRig {
    Engine e;
    ToolsRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~ToolsRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
};

Track sine_track(i64 step, Interpolation interp) {
    Track t;
    t.property = TrackProperty::PositionX;
    for (i64 f = 0; f <= 120; f += step) t.set(FrameIndex{f}, 100.0f * std::sin(static_cast<f32>(f) * 0.05f), interp);
    return t;
}

bool no_keys(const Layer* l, TrackProperty p) {
    const Track* t = l ? l->tracks.find(p) : nullptr;
    return !t || t->keys.empty();
}

} // namespace

AUREA_TEST(KeyframeOptimize, StraightMotionCollapsesToItsEnds) {
    Track t;
    t.property = TrackProperty::PositionX;
    for (i64 f = 0; f <= 100; f += 5) t.set(FrameIndex{f}, 2.0f * static_cast<f32>(f) + 10.0f, Interpolation::Linear);
    const Track before = t;
    AUREA_CHECK_EQ(animation::optimize_track(t, 0.01f), 19u);
    AUREA_CHECK_EQ(t.keys.size(), usize{2});
    AUREA_CHECK_EQ(t.keys.front().time.value, i64{0});
    AUREA_CHECK_EQ(t.keys.back().time.value, i64{100});
    AUREA_CHECK(animation::max_deviation(before, t) <= 0.01f);
}

AUREA_TEST(KeyframeOptimize, MaxDeviationNeverExceedsTheTolerance) {
    for (Interpolation interp : {Interpolation::Linear, Interpolation::Bezier, Interpolation::Hold}) {
        usize previous = 1000;
        for (f32 tol : {0.001f, 0.05f, 0.5f, 5.0f, 40.0f}) {
            Track t = sine_track(2, interp);
            const Track before = t;
            const u32 removed = animation::optimize_track(t, tol);
            AUREA_CHECK_EQ(t.keys.size() + removed, before.keys.size());
            // A garantia pedida: nenhum quadro sai mais que a tolerância.
            AUREA_CHECK(animation::max_deviation(before, t) <= tol);
            AUREA_CHECK_EQ(t.keys.front().time.value, before.keys.front().time.value);
            AUREA_CHECK_EQ(t.keys.back().time.value, before.keys.back().time.value);
            // Mais tolerância nunca guarda mais keyframes.
            AUREA_CHECK(t.keys.size() <= previous);
            previous = t.keys.size();
        }
        if (interp == Interpolation::Linear) AUREA_CHECK(previous < sine_track(2, interp).keys.size());
    }
    // Um canto vivo fica: tirar o pico mudaria o movimento.
    Track peak;
    peak.property = TrackProperty::Opacity;
    peak.set(FrameIndex{0}, 0.0f);
    peak.set(FrameIndex{10}, 1.0f);
    peak.set(FrameIndex{20}, 0.0f);
    AUREA_CHECK_EQ(animation::optimize_track(peak, 0.01f), 0u);
    AUREA_CHECK_EQ(peak.keys.size(), usize{3});
}

AUREA_TEST(KeyframeOptimize, EngineActionIsOneUndoStep) {
    ToolsRig r;
    const u64 id = *r.e.add_null(false);
    {
        Track& x = r.L(id)->tracks.get_or_create(TrackProperty::PositionX);
        for (i64 f = 0; f <= 60; f += 3) x.set(FrameIndex{f}, 4.0f * static_cast<f32>(f), Interpolation::Linear);
        Track& op = r.L(id)->tracks.get_or_create(TrackProperty::Opacity);
        op.set(FrameIndex{0}, 0.0f);
        op.set(FrameIndex{10}, 1.0f);
        op.set(FrameIndex{20}, 0.0f);
    }
    const usize xs = r.L(id)->tracks.find(TrackProperty::PositionX)->keys.size();
    // Só a opacidade (nada redundante nela): nada sai, nenhum desfazer abre.
    AUREA_CHECK_EQ(r.e.optimize_keyframes(id, static_cast<i32>(TrackProperty::Opacity), 0.01f), 0u);
    AUREA_CHECK_EQ(r.e.optimize_keyframes(id, -1, 0.01f), static_cast<u32>(xs - 2));
    AUREA_CHECK_EQ(r.L(id)->tracks.find(TrackProperty::PositionX)->keys.size(), usize{2});
    AUREA_CHECK_EQ(r.L(id)->tracks.find(TrackProperty::Opacity)->keys.size(), usize{3});
    r.undo();
    AUREA_CHECK_EQ(r.L(id)->tracks.find(TrackProperty::PositionX)->keys.size(), xs);
}

AUREA_TEST(AnimationClipboard, CopiesEveryPropertyAndPastesAtThePlayhead) {
    ToolsRig r;
    const u64 a = *r.e.add_null(false);
    const u64 b = *r.e.add_null(false);
    const u64 c = *r.e.add_null(false);
    {
        Layer* la = r.L(a);
        la->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{12}, 10.0f, Interpolation::Bezier);
        la->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{30}, 90.0f);
        la->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{20}, 45.0f);
        la->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{40}, 0.25f);
    }
    AUREA_CHECK_EQ(r.e.copy_animation(a), 4u);
    AUREA_CHECK((r.e.clipboard_state() & 8u) != 0);
    const u64 targets[] = {b, c};
    AUREA_CHECK_EQ(r.e.paste_keyframes(targets, 2, 50), 8u);
    for (u64 id : targets) {
        const Layer* l = r.L(id);
        const i64 base = l->local_time(FrameIndex{50}).value;   // o 1º keyframe (12) cai no cabeçote
        const Track* px = l->tracks.find(TrackProperty::PositionX);
        const Track* rot = l->tracks.find(TrackProperty::RotationZ);
        const Track* op = l->tracks.find(TrackProperty::Opacity);
        AUREA_CHECK(px && px->keys.size() == 2);
        if (px && px->keys.size() == 2) {
            AUREA_CHECK_EQ(px->keys[0].time.value, base);
            AUREA_CHECK_EQ(px->keys[1].time.value, base + 18);
            AUREA_CHECK_EQ(px->keys[1].value, 90.0f);
            AUREA_CHECK(px->keys[0].interp == Interpolation::Bezier);
        }
        AUREA_CHECK(rot && rot->keys.size() == 1 && rot->keys[0].time.value == base + 8);
        AUREA_CHECK(op && op->keys.size() == 1 && op->keys[0].time.value == base + 28 && op->keys[0].value == 0.25f);
    }
    // Colar em várias camadas é um passo só.
    r.undo();
    AUREA_CHECK(no_keys(r.L(b), TrackProperty::RotationZ));
    AUREA_CHECK(no_keys(r.L(c), TrackProperty::RotationZ));
    // Sem keyframe nenhum não há o que copiar.
    AUREA_CHECK_EQ(r.e.copy_animation(b), 0u);
}

AUREA_TEST(ShapePresets, NewShapesAreEditableShapeLayers) {
    ToolsRig r;
    struct Expect { u32 preset, type, points; };
    const Expect cases[] = {{15, 3, 8}, {16, 3, 5}, {17, 12, 5}, {18, 13, 5}, {19, 4, 4}, {20, 4, 6}, {21, 14, 12}, {22, 15, 5}};
    for (const Expect& c : cases) {
        const auto id = r.e.add_shape(c.preset);
        AUREA_CHECK(id.ok());
        if (!id.ok()) continue;
        const Layer* l = r.L(*id);
        AUREA_CHECK(l && l->kind == LayerKind::Shape);
        if (!l) continue;
        AUREA_CHECK_EQ(l->shape.shapeType, c.type);
        AUREA_CHECK_EQ(static_cast<u32>(l->shape.points), c.points);
    }
    // A troca de forma aceita os tipos novos e nunca vira a camada vetorial.
    const u64 id = *r.e.add_shape(0);
    for (f32 type : {12.0f, 13.0f, 14.0f, 15.0f, 11.0f, 99.0f}) {
        Command cmd;
        cmd.type = CommandType::ShapeSetParam;
        cmd.shape_param.layer = LayerId::unpack(id);
        cmd.shape_param.param = 0;
        cmd.shape_param.value = type;
        AUREA_CHECK(r.e.apply_command(cmd).ok());
        const u32 got = r.L(id)->shape.shapeType;
        AUREA_CHECK(got != kShapeVector);
        if (type >= 12.0f && type <= 15.0f) AUREA_CHECK_EQ(got, static_cast<u32>(type));
    }
}

AUREA_TEST(Vector, StrokeTaperThinsTheEndsAndRoundTrips) {
    using namespace aurea::vector;
    const StrokeTaper none{};
    AUREA_CHECK(!none.active());
    const StrokeTaper half{0.5f, 0.0f, 0.0f};
    AUREA_CHECK_NEAR(taper_factor(half, 0.0f), 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(taper_factor(half, 0.25f), 0.5f, 1e-5f);
    AUREA_CHECK_NEAR(taper_factor(half, 0.75f), 1.0f, 1e-6f);
    const StrokeTaper eased{0.5f, 0.0f, 1.0f};
    AUREA_CHECK(taper_factor(eased, 0.25f) > 0.8f);   // bojuda: engorda antes

    Contour line;
    line.pts = {{0.0f, 0.0f}, {200.0f, 0.0f}};
    line.closed = false;
    std::vector<Contour> plain, tapered, both;
    stroke_to_rings({line}, 20.0f, 0, 0, 4.0f, 0.1f, plain);
    stroke_to_rings({line}, 20.0f, 0, 0, 4.0f, 0.1f, tapered, half);
    stroke_to_rings({line}, 20.0f, 0, 0, 4.0f, 0.1f, both, StrokeTaper{0.5f, 0.5f, 0.0f});
    // 200 × 20 = 4000; a primeira metade vira um triângulo (−1000); as duas, um losango.
    AUREA_CHECK_NEAR(std::fabs(area_of(plain)), 4000.0, 1.0);
    AUREA_CHECK_NEAR(std::fabs(area_of(tapered)), 3000.0, 15.0);
    AUREA_CHECK_NEAR(std::fabs(area_of(both)), 2000.0, 15.0);

    // O documento leva o afinar (v2) e o lê de volta.
    VectorData d;
    VectorGroup g;
    g.stroke.enabled = true;
    g.stroke.taperStart = 30.0f;
    g.stroke.taperEnd = 12.5f;
    g.stroke.taperEase = 80.0f;
    g.stroke.dashOffset = 7.0f;
    d.groups = {g};
    std::vector<f32> s;
    std::string names;
    encode_document(d, s, names);
    AUREA_CHECK_EQ(s[0], 2.0f);
    VectorData back;
    AUREA_CHECK(decode_document(s.data(), s.size(), names, back));
    AUREA_CHECK(back == d);
}

// Copiar animação leva também os keyframes de FORMA (morph) da camada
// vetorial: colam no mesmo caminho de outra camada vetorial, no cabeçote, num
// passo de desfazer; numa camada sem esse caminho só as trilhas colam.
AUREA_TEST(AnimationClipboard, CopiesVectorShapeMorphKeys) {
    ToolsRig r;
    const u64 a = *r.e.add_shape(0);
    const u64 b = *r.e.add_shape(0);
    const u64 c = *r.e.add_null(false);
    auto make_vector = [&](u64 id, bool keyed) {
        Layer* l = r.L(id);
        l->shape.shapeType = kShapeVector;
        VectorGroup g;
        VectorPath p;
        p.kind = VectorPathKind::Free;
        p.path = vector::make_rect(Vec2{0, 0}, Vec2{100, 100}, 0.0f);
        if (keyed) {
            p.keys.push_back(PathKey{10, vector::make_rect(Vec2{0, 0}, Vec2{100, 100}, 0.0f), 1});
            p.keys.push_back(PathKey{25, vector::make_ellipse(Vec2{0, 0}, Vec2{80, 80}), 0});
        }
        g.paths.push_back(p);
        l->shape.vector.groups.assign(1, g);
    };
    make_vector(a, true);
    make_vector(b, false);
    r.L(a)->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{15}, 42.0f);
    AUREA_CHECK_EQ(r.e.copy_animation(a), 3u);   // 1 de trilha + 2 de forma
    AUREA_CHECK((r.e.clipboard_state() & 8u) != 0);
    const u64 targets[] = {b, c};
    AUREA_CHECK_EQ(r.e.paste_keyframes(targets, 2, 40), 4u);
    const Layer* lb = r.L(b);
    const i64 base = lb->local_time(FrameIndex{40}).value;   // o 1º keyframe (10) cai no cabeçote
    const auto& keys = lb->shape.vector.groups[0].paths[0].keys;
    AUREA_CHECK_EQ(keys.size(), usize{2});
    if (keys.size() == 2) {
        AUREA_CHECK_EQ(keys[0].frame, base);
        AUREA_CHECK_EQ(keys[1].frame, base + 15);
        AUREA_CHECK_EQ(keys[1].ease, u8{0});
        AUREA_CHECK(keys[1].path == r.L(a)->shape.vector.groups[0].paths[0].keys[1].path);
    }
    const Track* px = lb->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(px && px->keys.size() == 1 && px->keys[0].time.value == base + 5);
    const Track* pc = r.L(c)->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(pc && pc->keys.size() == 1);
    r.undo();
    AUREA_CHECK(r.L(b)->shape.vector.groups[0].paths[0].keys.empty());
    // Copiar keyframes de um instante troca a área: a forma antiga não volta.
    (void)r.e.copy_keyframes(a, r.L(a)->start.value + 15);
    const u64 onlyB[] = {b};
    (void)r.e.paste_keyframes(onlyB, 1, 40);
    AUREA_CHECK(r.L(b)->shape.vector.groups[0].paths[0].keys.empty());
}
