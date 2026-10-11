// Roto Brush (traços + propagação) sobre a probabilidade do Rotobrush IA.
#include "TestFramework.hpp"
#include "aurea/ai/RotoMatte.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/render/Renderer.hpp"

#include <cmath>
#include <cstdio>

using namespace aurea;

namespace {
constexpr u32 N = 320;

/// Fundo verde com textura fixa (o fluxo tem o que seguir) e um disco vermelho.
void scene_frame(f32 cx, f32 cy, f32 r, std::vector<u8>& rgb, std::vector<u8>* truth = nullptr) {
    rgb.resize(N * N * 3);
    if (truth) truth->assign(N * N, 0);
    for (u32 y = 0; y < N; ++y) for (u32 x = 0; x < N; ++x) {
        u8* p = &rgb[(y * N + x) * 3];
        const u32 h = (x * 73856093u) ^ (y * 19349663u);
        const int tex = int(h % 23) - 11;
        p[0] = u8(30 + tex); p[1] = u8(110 + x / 8 + tex); p[2] = u8(70 + y / 10);
        const f32 dx = x + .5f - cx, dy = y + .5f - cy;
        if (dx * dx + dy * dy < r * r) {
            p[0] = u8(215 + tex / 2); p[1] = 45; p[2] = 35;
            if (truth) (*truth)[y * N + x] = 1;
        }
    }
}

ai::RotoStroke stroke(i64 frame, bool bg, f32 radius, std::initializer_list<Vec2> pts) {
    ai::RotoStroke s; s.frame = frame; s.background = bg; s.radius = radius;
    for (const Vec2& p : pts) s.points.push_back(Vec2{p.x / N, p.y / N});
    return s;
}

f32 iou(const std::vector<f32>& m, const std::vector<u8>& truth) {
    u32 inter = 0, uni = 0;
    for (usize i = 0; i < truth.size(); ++i) {
        const bool a = m[i] >= .5f, b = truth[i] != 0;
        inter += a && b; uni += a || b;
    }
    return uni ? f32(inter) / f32(uni) : 1.f;
}
} // namespace

AUREA_TEST(Roto, PaintedObjectExcludesAnotherConfidentAutomaticObject) {
    std::vector<u8> rgb, truth;
    scene_frame(200, 170, 45, rgb, &truth);
    std::vector<f32> prob(N * N, .5f);
    for (u32 y = 30; y < 80; ++y) for (u32 x = 30; x < 80; ++x) {
        const usize at = y * N + x;
        rgb[at * 3] = 20; rgb[at * 3 + 1] = 20; rgb[at * 3 + 2] = 230;
        prob[at] = .99f;
    }
    ai::RotoStrokes s{stroke(0, false, .03f, {{190, 170}, {210, 170}})};
    std::vector<u8> labels;
    ai::roto_rasterize(s, 0, N, N, N, labels);
    std::vector<f32> matte;
    ai::roto_segment(prob.data(), rgb.data(), labels.data(), nullptr, N, matte);
    AUREA_CHECK(matte[170 * N + 200] > .99f);
    AUREA_CHECK(matte[55 * N + 55] < .1f);
    const f32 selectedIou = iou(matte, truth);
    std::printf("    painted selection without background strokes IoU %.3f\n", selectedIou);
    AUREA_CHECK(selectedIou > .9f);
}

AUREA_TEST(Roto, ProjectedBrushReturnsToTheSourceUnder2DAndPerspectiveTransforms) {
    Engine engine;
    EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(640, 480, 30, nullptr).ok());
    std::vector<u8> pixels(160 * 120 * 4, 255);
    auto imported = engine.import_image(pixels.data(), 160, 120, "Roto plane");
    AUREA_CHECK(imported.ok()); if (!imported.ok()) return;
    const LayerId id = LayerId::unpack(*imported);
    auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = id;
    add.effect_add.effectType = effect_type_id(effect_keys::kRotobrush); add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(engine.apply_command(add).ok());
    auto* layer = comp->layer(id);
    const u32 effect = layer->effects.back().id;
    for (const bool perspective : {false, true}) {
        layer->threeD = perspective;
        layer->transform.position = Vec3{290, 210, perspective ? 20.f : 0.f};
        layer->transform.anchor = Vec3{80, 60, 0};
        layer->transform.scale = Vec3{1.3f, .8f, 1};
        layer->transform.rotation = Vec3{perspective ? 22.f : 0.f, perspective ? 38.f : 0.f, 17};
        bool projected = false;
        const Mat4 matrix = layer_comp_matrix(*comp, *layer, FrameIndex{0}, &projected);
        AUREA_CHECK_EQ(projected, perspective);
        const Vec4 screen = matrix * Vec4{48, 78, 0, 1};
        const f32 xy[] = {screen.x / screen.w, screen.y / screen.w};
        AUREA_CHECK(engine.roto_add_stroke(id.pack(), effect, false, 7, xy, 1));
        ai::RotoStrokes strokes;
        AUREA_CHECK(ai::roto_instance_strokes(layer->effects.back(), strokes));
        AUREA_CHECK(!strokes.empty());
        if (!strokes.empty()) {
            AUREA_CHECK_NEAR(strokes.back().points[0].x, .3f, 1e-4);
            AUREA_CHECK_NEAR(strokes.back().points[0].y, .65f, 1e-4);
        }
    }
    layer->locked = true;
    const f32 xy[] = {290, 210};
    AUREA_CHECK(!engine.roto_add_stroke(id.pack(), effect, true, 7, xy, 1));
    engine.shutdown();
}

AUREA_TEST(Roto, BackgroundStrokeRemovesWhatTheModelCallsForeground) {
    std::vector<u8> rgb, truth;
    scene_frame(200, 170, 50, rgb, &truth);
    // A rede diz "objeto" no quadro inteiro.
    const std::vector<f32> prob(N * N, 1.f);
    ai::RotoStrokes s{stroke(0, true, .02f, {{20, 40}, {300, 40}}), stroke(0, false, .02f, {{190, 170}, {210, 170}})};
    std::vector<u8> labels;
    ai::roto_rasterize(s, 0, N, 1920, 1080, labels);
    AUREA_CHECK_EQ(labels[40 * N + 100], 2);
    AUREA_CHECK_EQ(labels[170 * N + 200], 1);
    AUREA_CHECK_EQ(labels[300 * N + 10], 0);
    std::vector<f32> m;
    const u64 t0 = monotonic_ns();
    ai::roto_segment(prob.data(), rgb.data(), labels.data(), nullptr, N, m);
    std::printf("    segmentacao 320x320: %.1f ms, IoU %.3f\n", (monotonic_ns() - t0) * 1e-6, iou(m, truth));
    AUREA_CHECK_EQ(m[40 * N + 100], 0.f);          // o pintado de fundo sai
    AUREA_CHECK(m[290 * N + 30] < .1f);            // e a região parecida, mesmo longe do traço
    AUREA_CHECK(m[170 * N + 200] > .99f);
    AUREA_CHECK(m[200 * N + 220] > .9f);           // o disco inteiro, não só o traço
    AUREA_CHECK(iou(m, truth) > .9f);
    // Sem traços de fundo e a rede dizendo "fundo": o traço de objeto manda.
    const std::vector<f32> none(N * N, 0.f);
    ai::RotoStrokes fg{stroke(0, false, .02f, {{200, 160}, {200, 180}})};
    ai::roto_rasterize(fg, 0, N, 1, 1, labels);
    ai::roto_segment(none.data(), rgb.data(), labels.data(), nullptr, N, m);
    AUREA_CHECK(m[150 * N + 200] > .9f);
    AUREA_CHECK(m[10 * N + 10] < .1f);
    AUREA_CHECK(iou(m, truth) > .9f);
}

AUREA_TEST(Roto, PropagationFollowsAMovingObjectAcross30Frames) {
    std::vector<u8> prevRgb, rgb, truth;
    // A rede não ajuda (0,5 em tudo): só os traços do quadro 0 e o fluxo.
    const std::vector<f32> prob(N * N, .5f);
    auto center = [](u32 f) { return Vec2{90.f + 5.f * f, 150.f + 2.f * std::sin(f * .4f) * 6.f}; };
    scene_frame(center(0).x, center(0).y, 45, prevRgb, &truth);
    ai::RotoStrokes s{stroke(0, false, .03f, {{80, 150}, {100, 150}}), stroke(0, true, .02f, {{10, 300}, {310, 300}})};
    std::vector<u8> labels;
    ai::roto_rasterize(s, 0, N, 1, 1, labels);
    std::vector<f32> matte, next;
    ai::roto_segment(prob.data(), prevRgb.data(), labels.data(), nullptr, N, matte);
    f32 worst = iou(matte, truth);
    std::printf("    quadro 0 IoU %.3f\n", worst);
    const u64 t0 = monotonic_ns();
    for (u32 f = 1; f < 30; ++f) {
        scene_frame(center(f).x, center(f).y, 45, rgb, &truth);
        ai::roto_propagate(matte, prevRgb.data(), rgb.data(), prob.data(), nullptr, N, next);
        matte.swap(next);
        prevRgb.swap(rgb);
        worst = std::min(worst, iou(matte, truth));
        if (iou(matte, truth) < .9f) std::printf("    quadro %u IoU %.3f\n", f, iou(matte, truth));
    }
    std::printf("    propagacao 29 quadros: %.1f ms/quadro, pior IoU %.3f\n", (monotonic_ns() - t0) * 1e-6 / 29, worst);
    AUREA_CHECK(worst > .9f);
}

AUREA_TEST(Roto, StrokesRoundTripAndOnlyLaterFramesDependOnANewBase) {
    ai::RotoStrokes s{stroke(4, false, .03f, {{10, 10}, {20, 30}}), stroke(4, true, .05f, {{100, 100}})};
    CurveData c = CurveData::identity();
    ai::RotoStrokes none;
    AUREA_CHECK(!ai::roto_decode(c, none));          // curva padrão = sem traços
    ai::roto_encode(s, c);
    ai::RotoStrokes back;
    AUREA_CHECK(ai::roto_decode(c, back));
    AUREA_CHECK_EQ(back.size(), 2u);
    if (back.size() != 2) return;
    AUREA_CHECK_EQ(back[1].frame, 4);
    AUREA_CHECK(back[1].background);
    AUREA_CHECK(std::fabs(back[1].radius - .05f) < 1e-6f);
    AUREA_CHECK_EQ(back[0].points.size(), 2u);
    const u64 at2 = ai::roto_dependency(s, 2), at6 = ai::roto_dependency(s, 6), at12 = ai::roto_dependency(s, 12);
    s.push_back(stroke(10, true, .02f, {{50, 50}}));
    AUREA_CHECK_EQ(ai::roto_dependency(s, 2), at2);    // antes da base: só a primeira
    AUREA_CHECK_EQ(ai::roto_dependency(s, 6), at6);    // entre as bases: intacto
    AUREA_CHECK(ai::roto_dependency(s, 12) != at12);   // depois da base nova: refaz
    std::vector<f32> m(N * N);
    for (usize i = 0; i < m.size(); ++i) m[i] = (i / N) > 100 ? 1.f : 0.f;
    std::vector<u8> packed; std::vector<f32> un;
    ai::roto_pack(m, packed);
    AUREA_CHECK(packed.size() < 4096);
    AUREA_CHECK(ai::roto_unpack(packed, m.size(), un));
    AUREA_CHECK(un == m);
}
