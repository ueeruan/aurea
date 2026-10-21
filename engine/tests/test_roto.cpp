// Roto Brush (traços + propagação) sobre a probabilidade do Rotobrush IA.
#include "TestFramework.hpp"
#include "RotoSynthetic.hpp"
#include "aurea/ai/RotoMatte.hpp"
#include "aurea/ai/RotoRefine.hpp"
#include "aurea/ai/RotoService.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/render/Renderer.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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

// =============================================================================
//  Refinar borda (RotoRefine): sequência sintética (RotoSynthetic.hpp) na
//  resolução cheia, com o alfa verdadeiro. O recorte da grade 320² vem do
//  caminho de sempre (traços + "rede" simulada = verdade borrada na grade +
//  propagação); o "hoje" é o que o shader mostrava: bilinear da grade e o
//  smoothstep 0,45..0,55 do limite/suavidade padrão.
// =============================================================================
namespace {
namespace rsyn = aurea::roto_synthetic;

struct RefineRun {
    f64 edgeGrid = 0, edgeRefined = 0, edgeChatter = 0;          // erro médio |α − verdade| na borda
    f64 movingRefined = 0, movingChatter = 0;                     // só quadros em movimento
    f64 hairGrid = 0, hairRefined = 0;                            // fios fora do corpo
    f64 flickerGrid = 0, flickerRefined = 0, flickerChatter = 0;  // |α_t − α_t−1| nos quadros parados
    f64 colorObserved = 0, colorDecontaminated = 0;               // |cor − cor verdadeira do objeto| na borda
    f64 msRefine = 0, msFull = 0;
    usize packedBytes = 0;
    bool threadsMatch = true;
};

/// "Rede" simulada: a verdade na grade 320, borrada (incerta perto da borda).
void simulated_network(const rsyn::Frame& gt, std::vector<f32>& prob) {
    const u32 W = gt.width, H = gt.height;
    std::vector<f32> g(N * N, 0.f), t(N * N);
    for (u32 gy = 0; gy < N; ++gy) for (u32 gx = 0; gx < N; ++gx) {
        const u32 x0 = gx * W / N, x1 = (gx + 1) * W / N, y0 = gy * H / N, y1 = (gy + 1) * H / N;
        f32 s = 0; u32 c = 0;
        for (u32 y = y0; y < y1; ++y) for (u32 x = x0; x < x1; ++x) { s += gt.alpha[static_cast<usize>(y) * W + x]; ++c; }
        g[gy * N + gx] = c ? s / c : 0;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (u32 y = 0; y < N; ++y) for (u32 x = 0; x < N; ++x) {
            f32 s = 0; for (int d = -2; d <= 2; ++d) s += g[y * N + static_cast<u32>(std::clamp(static_cast<int>(x) + d, 0, static_cast<int>(N) - 1))];
            t[y * N + x] = s / 5;
        }
        for (u32 y = 0; y < N; ++y) for (u32 x = 0; x < N; ++x) {
            f32 s = 0; for (int d = -2; d <= 2; ++d) s += t[static_cast<u32>(std::clamp(static_cast<int>(y) + d, 0, static_cast<int>(N) - 1)) * N + x];
            g[y * N + x] = s / 5;
        }
    }
    prob.swap(g);
}

RefineRun run_refine_sequence(u32 W, u32 H, f32 radius, u32 frames) {
    RefineRun r;
    ai::RotoStrokes strokes;
    {
        rsyn::Frame f0; rsyn::make(W, H, 0, f0);
        ai::RotoStroke fg; fg.frame = 0; fg.radius = 0.03f;
        fg.points = {Vec2{f0.cx / W, f0.cy / H}, Vec2{(f0.cx + 0.3f * f0.rx) / W, (f0.cy + 0.2f * f0.ry) / H}};
        ai::RotoStroke bg; bg.frame = 0; bg.background = true; bg.radius = 0.02f;
        // Fundo nas beiradas, acima do cabelo e dos lados (como quem corrige um vazamento).
        const f32 top = (f0.cy - f0.ry) / H - 0.14f, right = (f0.cx + f0.rx) / W + 0.09f, left = (f0.cx - f0.rx) / W - 0.09f;
        strokes.push_back(fg);
        for (const auto& line : {std::vector<Vec2>{{0.05f, 0.95f}, {0.95f, 0.95f}, {0.95f, 0.05f}}, std::vector<Vec2>{{0.05f, 0.05f}, {0.05f, 0.9f}},
                                 std::vector<Vec2>{{0.1f, top}, {0.9f, top}}, std::vector<Vec2>{{right, 0.1f}, {right, 0.9f}},
                                 std::vector<Vec2>{{left, 0.1f}, {left, 0.9f}}}) {
            bg.points = line; strokes.push_back(bg);
        }
    }
    std::vector<u8> labels, grid, gridPrev;
    ai::roto_rasterize(strokes, 0, N, static_cast<f32>(W), static_cast<f32>(H), labels);
    std::vector<f32> prob, matte, next, gridUp, gridUpPrev;
    ai::RotoRefined plain, chat, plainPrev, chatPrev, full;
    u32 moving = 0, still = 0;
    u64 colorCount = 0;
    for (u32 f = 0; f < frames; ++f) {
        rsyn::Frame gt; rsyn::make(W, H, f, gt);
        ai::RotoImage img; img.width = W; img.height = H; img.rgb = gt.rgb;
        ai::roto_grid_rgb(img, N, grid);
        simulated_network(gt, prob);
        if (f == 0) ai::roto_segment(prob.data(), grid.data(), labels.data(), nullptr, N, matte);
        else { ai::roto_propagate(matte, gridPrev.data(), grid.data(), prob.data(), nullptr, N, next); matte.swap(next); }
        { std::vector<u8> p; ai::roto_pack(matte, p); (void)ai::roto_unpack(p, matte.size(), matte); }
        std::vector<Vec2> flow;
        if (f) ai::roto_flow(gridPrev.data(), grid.data(), N, flow);
        ai::RotoRefineSettings s; s.radius = radius;
        u64 t0 = monotonic_ns();
        ai::roto_refine(matte.data(), N, img, s, nullptr, plain);
        r.msRefine += (monotonic_ns() - t0) * 1e-6;
        ai::RotoRefineSettings sc = s; sc.chatter = 0.5f;
        ai::RotoTemporal tmp; tmp.prev = f ? &chatPrev : nullptr; tmp.flow = f ? flow.data() : nullptr; tmp.n = N;
        ai::roto_refine(matte.data(), N, img, sc, f ? &tmp : nullptr, chat);
        ai::RotoRefineSettings sd = sc; sd.decontaminate = true;
        t0 = monotonic_ns();
        ai::roto_refine(matte.data(), N, img, sd, f ? &tmp : nullptr, full);
        r.msFull += (monotonic_ns() - t0) * 1e-6;
        if (f == 1) {
            // Blocos independentes: 1 thread e 4 threads dão o mesmo resultado.
            const u32 keep = ai::g_rotoRefineThreads;
            ai::RotoRefined one, many;
            ai::g_rotoRefineThreads = 1; ai::roto_refine(matte.data(), N, img, sd, &tmp, one);
            ai::g_rotoRefineThreads = 4; ai::roto_refine(matte.data(), N, img, sd, &tmp, many);
            ai::g_rotoRefineThreads = keep;
            r.threadsMatch = one.alpha == many.alpha && one.fg == many.fg && one.alpha == full.alpha;
        }
        // O de hoje: bilinear da grade + smoothstep(0,45; 0,55) do shader.
        gridUp.resize(static_cast<usize>(W) * H);
        for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) {
            const f32 gx = std::clamp((x + .5f) * N / W - .5f, 0.f, f32(N - 1)), gy = std::clamp((y + .5f) * N / H - .5f, 0.f, f32(N - 1));
            const u32 x0 = std::min(u32(gx), N - 2), y0 = std::min(u32(gy), N - 2);
            const f32 u = gx - x0, v = gy - y0;
            const f32 c = (matte[y0 * N + x0] * (1 - u) + matte[y0 * N + x0 + 1] * u) * (1 - v)
                        + (matte[(y0 + 1) * N + x0] * (1 - u) + matte[(y0 + 1) * N + x0 + 1] * u) * v;
            const f32 t = std::clamp((c - 0.45f) / 0.1f, 0.f, 1.f);
            gridUp[static_cast<usize>(y) * W + x] = t * t * (3 - 2 * t);
        }
        f64 eG = 0, eR = 0, eC = 0, hG = 0, hR = 0, fG = 0, fR = 0, fC = 0; u64 n = 0, hn = 0;
        for (u32 y = 3; y + 3 < H; ++y) for (u32 x = 3; x + 3 < W; ++x) {
            const usize i = static_cast<usize>(y) * W + x;
            const f32 g = gt.alpha[i];
            bool edge = g > 0.02f && g < 0.98f;
            if (!edge) {
                const bool in = g >= 0.5f;
                for (int dy = -3; dy <= 3 && !edge; dy += 3) for (int dx = -3; dx <= 3 && !edge; dx += 3)
                    edge = (gt.alpha[static_cast<usize>(static_cast<i64>(i) + dy * static_cast<i64>(W) + dx)] >= 0.5f) != in;
            }
            if (!edge) continue;
            ++n;
            eG += std::fabs(gridUp[i] - g); eR += std::fabs(plain.alpha[i] / 255.f - g); eC += std::fabs(chat.alpha[i] / 255.f - g);
            const f32 dx = (x + .5f - gt.cx) / gt.rx, dy = (y + .5f - gt.cy) / gt.ry;
            if (dx * dx + dy * dy > 1.02f && g > 0.02f) { hG += std::fabs(gridUp[i] - g); hR += std::fabs(plain.alpha[i] / 255.f - g); ++hn; }
            if (f >= 1 && f <= 9) {
                fG += std::fabs(gridUp[i] - gridUpPrev[i]);
                fR += std::fabs(plain.alpha[i] / 255.f - plainPrev.alpha[i] / 255.f);
                fC += std::fabs(chat.alpha[i] / 255.f - chatPrev.alpha[i] / 255.f);
            }
            if (g > 0.15f && g < 0.85f && full.alpha[i] > 0 && full.alpha[i] < 255) {
                for (u32 k = 0; k < 3; ++k) {
                    r.colorObserved += std::fabs(gt.rgb[i * 3 + k] / 255.f - gt.fg[i * 3 + k]);
                    r.colorDecontaminated += std::fabs(full.fg[i * 3 + k] / 255.f - gt.fg[i * 3 + k]);
                }
                colorCount += 3;
            }
        }
        r.edgeGrid += eG / n; r.edgeRefined += eR / n; r.edgeChatter += eC / n;
        if (hn) { r.hairGrid += hG / hn; r.hairRefined += hR / hn; }
        if (f >= 10) { r.movingRefined += eR / n; r.movingChatter += eC / n; ++moving; }
        if (f >= 1 && f <= 9) { r.flickerGrid += fG / n; r.flickerRefined += fR / n; r.flickerChatter += fC / n; ++still; }
        plainPrev = plain; chatPrev = chat; gridPrev = grid; gridUpPrev = gridUp;
    }
    const f64 nf = frames;
    r.edgeGrid /= nf; r.edgeRefined /= nf; r.edgeChatter /= nf; r.hairGrid /= nf; r.hairRefined /= nf;
    r.movingRefined /= std::max(1u, moving); r.movingChatter /= std::max(1u, moving);
    r.flickerGrid /= std::max(1u, still); r.flickerRefined /= std::max(1u, still); r.flickerChatter /= std::max(1u, still);
    r.colorObserved /= std::max<u64>(1, colorCount); r.colorDecontaminated /= std::max<u64>(1, colorCount);
    r.msRefine /= nf; r.msFull /= nf;
    std::vector<u8> packed; ai::roto_refined_pack(full, packed); r.packedBytes = packed.size();
    ai::RotoRefined back;
    if (!ai::roto_refined_unpack(packed, back) || back.alpha != full.alpha || back.fg != full.fg) r.threadsMatch = false;
    return r;
}

void print_refine_run(const char* what, const RefineRun& r) {
    std::printf("    %s: borda |a-verdade| grade %.4f -> refinado %.4f (trepidacao 50%% %.4f); cabelo %.4f -> %.4f\n",
                what, r.edgeGrid, r.edgeRefined, r.edgeChatter, r.hairGrid, r.hairRefined);
    std::printf("    %s: tremido parado %.5f -> %.5f (50%%: %.5f); em movimento %.4f (50%%: %.4f); cor da borda %.4f -> %.4f\n",
                what, r.flickerGrid, r.flickerRefined, r.flickerChatter, r.movingRefined, r.movingChatter, r.colorObserved, r.colorDecontaminated);
    std::printf("    %s: %.1f ms/quadro (so alfa), %.1f ms com trepidacao+descontaminar; compactado %zu bytes\n", what, r.msRefine, r.msFull, r.packedBytes);
}
} // namespace

AUREA_TEST(Roto, RefineEdgeBeatsTheGridOnSyntheticHairSequence) {
    // Raio 8: a borda nítida. Medido (720p): grade 0,163 → refinado 0,113.
    const RefineRun r = run_refine_sequence(1280, 720, 8.0f, 20);
    print_refine_run("720p raio 8", r);
    AUREA_CHECK(r.threadsMatch);
    AUREA_CHECK(r.edgeRefined < 0.8 * r.edgeGrid);                 // margem clara na borda
    AUREA_CHECK(r.flickerRefined < r.flickerGrid);                 // sem regressão de tremido
    AUREA_CHECK(r.flickerChatter <= r.flickerRefined);             // reduzir trepidação reduz
    AUREA_CHECK(r.movingChatter < 1.05 * r.movingRefined);         // e não atrasa no movimento
    AUREA_CHECK(r.colorDecontaminated < 0.6 * r.colorObserved);    // menos tinta do fundo na borda
}

AUREA_TEST(Roto, RefineEdgeLargeRadiusRecoversHairStrands) {
    // Raio 24: a faixa alcança os fios (o 320² não tem como ver um fio de 1 px).
    const RefineRun r = run_refine_sequence(1280, 720, 24.0f, 12);
    print_refine_run("720p raio 24", r);
    AUREA_CHECK(r.threadsMatch);
    AUREA_CHECK(r.hairRefined < 0.85 * r.hairGrid);
    AUREA_CHECK(r.edgeRefined < 0.8 * r.edgeGrid);
    AUREA_CHECK(r.colorDecontaminated < 0.7 * r.colorObserved);
}

AUREA_TEST(Roto, BenchRefineEdge1080p) {
    // Custo por quadro 1080p (alfa; com trepidação + descontaminar).
    const RefineRun r = run_refine_sequence(1920, 1080, 8.0f, 4);
    print_refine_run("1080p raio 8", r);
    AUREA_CHECK(r.threadsMatch);
    AUREA_CHECK(r.edgeRefined < r.edgeGrid);
}

AUREA_TEST(Roto, RefineServiceCachesTheRefinedChainAndOldInstancesStayOnTheGrid) {
    // Camada de imagem: a cadeia do serviço dá o mesmo refinado da conta
    // direta, guarda no cache certo e um ajuste novo não reaproveita o velho.
    const u32 W = 640, H = 360;
    rsyn::Frame gt; rsyn::make(W, H, 0, gt);
    ai::RotoImage img; img.width = W; img.height = H; img.rgb = gt.rgb;
    auto map = std::make_shared<ai::DepthMap>();
    simulated_network(gt, map->disparity);
    ai::roto_grid_rgb(img, N, map->foregroundRgb);
    ai::RotoStrokes strokes{stroke(0, false, .03f, {{gt.cx * N / W, gt.cy * N / H}})};
    ai::RotoService service;
    ai::RotoService::Source src;
    src.image = map;
    src.imageFrame = std::make_shared<ai::RotoImage>(img);
    src.layerW = static_cast<f32>(W); src.layerH = static_cast<f32>(H);
    src.refine.radius = 6.0f; src.refine.decontaminate = true;
    const ai::RotoService::Result r = service.compute_all(77, strokes, src, 0);
    AUREA_CHECK(r.matte && r.refined);
    if (!r.matte || !r.refined) return;
    AUREA_CHECK_EQ(r.refined->width, W);
    AUREA_CHECK_EQ(r.refined->fg.size(), static_cast<usize>(W) * H * 3);
    ai::RotoRefined direct;
    ai::RotoRefineSettings s = src.refine;
    ai::roto_refine(r.matte->data(), N, img, s, nullptr, direct);
    AUREA_CHECK(direct.alpha == r.refined->alpha);
    const auto hit = service.cached_refined(77, strokes, src.refine, 0);
    AUREA_CHECK(hit && hit->alpha == r.refined->alpha);
    AUREA_CHECK(service.ready(77, strokes, 0, src.refine.hash()));
    ai::RotoRefineSettings other = src.refine; other.radius = 12.0f;
    AUREA_CHECK(!service.ready(77, strokes, 0, other.hash()));
    AUREA_CHECK(!service.cached_refined(77, strokes, other, 0));
    AUREA_CHECK(service.ready(77, strokes, 0));                    // a grade continua pronta
    // Instância do Rotobrush: nova nasce com "Raio da borda" 6 px; salva antes
    // do slot (12 parâmetros) fica em 0 = a grade, igual ao que era.
    EffectRegistry registry;
    register_builtin_effects(registry);
    const ParameterRegistry* specs = registry.params(effect_type_id(effect_keys::kRotobrush));
    AUREA_CHECK(specs != nullptr);
    if (!specs) return;
    AUREA_CHECK_EQ(specs->count(), ai::kRotoEdgeRadiusParam + 1);
    AUREA_CHECK_EQ(std::string(specs->at(ai::kRotoEdgeRadiusParam).id), std::string("edge_radius"));
    EffectInstance fresh;
    initialize_instance(fresh, *specs);
    AUREA_CHECK_NEAR(ai::roto_instance_refine(fresh).radius, 6.0f, 1e-6);
    AUREA_CHECK_NEAR(ai::roto_instance_refine(fresh).chatter, 0.5f, 1e-6);
    EffectInstance legacy = fresh;
    legacy.params.resize(ai::kRotoEdgeRadiusParam);
    legacy.params[ai::kRotoChatterParam].constant = ParamValue::scalar(0.0f);   // o padrão de antes
    AUREA_CHECK(!ai::roto_instance_refine(legacy).on());
    AUREA_CHECK_EQ(evaluate_param_f(TrackSet{}, legacy, ai::kRotoEdgeRadiusParam, specs->at(ai::kRotoEdgeRadiusParam), 0.0).v[0], 0.0f);
}
