// =============================================================================
//  AE compositing (2026-11-10): contrato de arquivo e regras do motor para os
//  modos de mesclagem e de máscara novos, a queda do feather, tímidas,
//  preservar transparência e os bugs B1 (ciclo de track matte), B2 (curva de
//  tempo entre quadros), B3 (cache de índice atômico) e B9 (luz ambiente) do
//  gap_system. Os pixels são conferidos em AeCompositingGpu.inl (test_gpu.cpp).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace aurea;

namespace {

struct AeRig {
    Engine e;
    AeRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~AeRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 shape() {
        const auto id = e.add_shape(0);
        AUREA_CHECK(id.ok());
        return id.ok() ? *id : 0;
    }
    std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
    Layer* named(const char* name) {
        Layer* found = nullptr;
        comp()->layers().for_each([&](LayerId, Layer& l) { if (l.name == name) found = &l; });
        return found;
    }
};

Mask square_mask(u32 id, f32 x0, f32 y0, f32 x1, f32 y1, MaskOperation op) {
    Mask m;
    m.id = id;
    m.operation = op;
    m.points = {MaskPoint{Vec2{x0, y0}}, MaskPoint{Vec2{x1, y0}}, MaskPoint{Vec2{x1, y1}}, MaskPoint{Vec2{x0, y1}}};
    return m;
}

f32 coverage(const Layer& l, Vec2 p) {
    std::vector<Vec4> block;
    f32 start = 0;
    u64 key = 0;
    const u32 n = mask::build_block(l, 0.0, Vec2{0, 0}, 0.2f, block, start, key);
    return mask::coverage_at(block.data(), n, start, p, 1.0f);
}

} // namespace

// -----------------------------------------------------------------------------
// Contrato dos enums: tudo acrescentado no fim
// -----------------------------------------------------------------------------
AUREA_TEST(AeCompositing, NewEnumsAppendAtTheEnd) {
    static_assert(static_cast<u16>(BlendMode::SoftOverlay) == 28);
    static_assert(static_cast<u16>(BlendMode::StencilLuma) == 29);
    static_assert(static_cast<u16>(BlendMode::DancingDissolve) == 32);
    static_assert(static_cast<u16>(BlendMode::LuminescentPremul) == 38);
    static_assert(kLastBlendMode == BlendMode::LuminescentPremul);
    static_assert(static_cast<u8>(MaskOperation::None) == 4);
    static_assert(static_cast<u8>(MaskOperation::Lighten) == 5 && static_cast<u8>(MaskOperation::Darken) == 6);
    static_assert(kLastMaskOperation == MaskOperation::Darken);
    static_assert(blend_is_stencil(BlendMode::StencilLuma) && blend_is_stencil(BlendMode::SilhouetteLuma));
    static_assert(blend_keeps_inside_only(BlendMode::StencilLuma) && !blend_keeps_inside_only(BlendMode::SilhouetteLuma));
    static_assert(bridge::kLayerRowFlagShy != bridge::kLayerRowFlagPreserveTransparency);
    static_assert((bridge::kLayerRowFlagShy & bridge::kLayerRowLabelMask) == 0);
    AUREA_CHECK(true);
}

AUREA_TEST(AeCompositing, EveryNewBlendModeIsAcceptedAndOutOfRangeRefused) {
    AeRig r;
    const u64 id = r.shape();
    if (!id) return;
    for (u16 m = static_cast<u16>(BlendMode::StencilLuma); m <= static_cast<u16>(kLastBlendMode); ++m) {
        Command c;
        c.type = CommandType::LayerSetBlendMode;
        c.layer_blend.layer = LayerId::unpack(id);
        c.layer_blend.mode = static_cast<BlendMode>(m);
        AUREA_CHECK(r.e.apply_command(c).ok());
        AUREA_CHECK(r.L(id)->blendMode == static_cast<BlendMode>(m));
    }
    Command bad;
    bad.type = CommandType::LayerSetBlendMode;
    bad.layer_blend.layer = LayerId::unpack(id);
    bad.layer_blend.mode = static_cast<BlendMode>(static_cast<u16>(kLastBlendMode) + 1);
    AUREA_CHECK(!r.e.apply_command(bad).ok());
}

// -----------------------------------------------------------------------------
// Máscaras: Clarear / Escurecer e queda linear (a matemática da CPU = shader)
// -----------------------------------------------------------------------------
AUREA_TEST(AeCompositing, MaskLightenTakesTheMaxAndDarkenTheMin) {
    Layer l;
    l.kind = LayerKind::Shape;
    l.masks = {square_mask(0, 10, 10, 70, 70, MaskOperation::Add), square_mask(1, 40, 40, 100, 90, MaskOperation::Lighten)};
    l.masks[0].opacity = 0.6f;
    l.masks[1].opacity = 0.4f;
    const Vec2 overlap{55.5f, 55.5f}, onlyA{20.5f, 20.5f}, onlyB{90.5f, 85.5f}, outside{5.5f, 5.5f};
    AUREA_CHECK_NEAR(coverage(l, overlap), 0.6f, 1e-4f);   // Somar daria 0,76
    AUREA_CHECK_NEAR(coverage(l, onlyA), 0.6f, 1e-4f);
    AUREA_CHECK_NEAR(coverage(l, onlyB), 0.4f, 1e-4f);
    AUREA_CHECK_NEAR(coverage(l, outside), 0.0f, 1e-4f);
    l.masks[1].operation = MaskOperation::Darken;
    AUREA_CHECK_NEAR(coverage(l, overlap), 0.4f, 1e-4f);   // Intersectar daria 0,24
    AUREA_CHECK_NEAR(coverage(l, onlyA), 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(coverage(l, onlyB), 0.0f, 1e-4f);
    // Primeira máscara em Escurecer: começa de "tudo dentro".
    l.masks = {square_mask(0, 10, 10, 70, 70, MaskOperation::Darken)};
    AUREA_CHECK_NEAR(coverage(l, onlyA), 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(coverage(l, onlyB), 0.0f, 1e-4f);
}

AUREA_TEST(AeCompositing, LinearFalloffIsAStraightRampOfTheSameWidth) {
    Layer l;
    l.kind = LayerKind::Shape;
    l.masks = {square_mask(0, 20, 16, 100, 80, MaskOperation::Add)};
    l.masks[0].feather = 20.0f;
    const f32 smoothIn = coverage(l, Vec2{25.5f, 48.5f});
    l.masks[0].featherFalloff = 1;
    const f32 w = std::sqrt(16.0f * 25.0f + 1.0f);
    for (const f32 sd : {-9.5f, -5.5f, -0.5f, 0.5f, 5.5f, 9.5f, 12.5f}) {
        const f32 want = std::clamp(sd / w + 0.5f, 0.0f, 1.0f);
        AUREA_CHECK_NEAR(coverage(l, Vec2{20.0f + sd, 48.5f}), want, 1e-4f);
    }
    std::printf("    5,5 px dentro: linear %.3f suave %.3f ", coverage(l, Vec2{25.5f, 48.5f}), smoothIn);
    AUREA_CHECK(smoothIn - coverage(l, Vec2{25.5f, 48.5f}) > 0.05f);
    // A queda entra na chave do bitmap (cache): trocar refaz a máscara.
    std::vector<Vec4> a, b;
    f32 s0 = 0, s1 = 0;
    u64 k0 = 0, k1 = 0;
    (void)mask::build_block(l, 0.0, Vec2{0, 0}, 0.2f, a, s0, k0);
    l.masks[0].featherFalloff = 0;
    (void)mask::build_block(l, 0.0, Vec2{0, 0}, 0.2f, b, s1, k1);
    AUREA_CHECK(k0 != k1);
}

AUREA_TEST(AeCompositing, MaskModesAndFalloffGoThroughTheEngine) {
    AeRig r;
    const u64 id = r.shape();
    if (!id) return;
    const f32 pts[] = {0, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 50, 50, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0};
    const i32 m = r.e.add_mask(id, pts, 4, true);
    AUREA_CHECK(m >= 0);
    if (m < 0) return;
    AUREA_CHECK(r.e.set_mask_props(id, static_cast<u32>(m), 5, false, 4.0f, 0.0f, 1.0f));
    AUREA_CHECK(r.L(id)->masks[0].operation == MaskOperation::Lighten);
    AUREA_CHECK(r.e.set_mask_props(id, static_cast<u32>(m), 6, false, 4.0f, 0.0f, 1.0f));
    AUREA_CHECK(r.L(id)->masks[0].operation == MaskOperation::Darken);
    AUREA_CHECK(!r.e.set_mask_props(id, static_cast<u32>(m), 7, false, 4.0f, 0.0f, 1.0f));
    AUREA_CHECK(r.e.set_mask_falloff(id, static_cast<u32>(m), 1));
    AUREA_CHECK_EQ(r.L(id)->masks[0].featherFalloff, u8{1});
    AUREA_CHECK(!r.e.set_mask_falloff(id, static_cast<u32>(m), 2));
    std::vector<f32> out(r.e.query_masks(id, nullptr, 0));
    AUREA_CHECK(r.e.query_masks(id, out.data(), static_cast<u32>(out.size())) == out.size());
    AUREA_CHECK_NEAR(out[7 + 1], 6.0f, 0.0f);    // modo
    AUREA_CHECK_NEAR(out[7 + 11], 1.0f, 0.0f);   // queda linear
    // Desfazer volta à queda suave.
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(u).ok());
    AUREA_CHECK_EQ(r.L(id)->masks[0].featherFalloff, u8{0});
}

// -----------------------------------------------------------------------------
// Tímidas, preservar transparência: arquivo e comandos
// -----------------------------------------------------------------------------
AUREA_TEST(AeCompositing, ShyPreserveAndFalloffRoundTripThroughTheProjectFile) {
    AeRig r;
    const u64 a = r.shape(), b = r.shape();
    if (!a || !b) return;
    r.L(a)->name = "tímida";
    r.L(b)->name = "preserva";
    // Padrão desligado (projeto antigo abre igual).
    AUREA_CHECK(!r.L(a)->shy && !r.L(a)->preserveTransparency && !r.comp()->hide_shy());
    AUREA_CHECK(r.e.set_layer_shy(a, true));
    AUREA_CHECK(r.e.set_layer_preserve_transparency(b, true));
    AUREA_CHECK(r.e.set_hide_shy(true));
    AUREA_CHECK(r.e.hide_shy());
    const f32 pts[] = {0, 0, 0, 0, 0, 0, 50, 0, 0, 0, 0, 0, 50, 50, 0, 0, 0, 0};
    const i32 m0 = r.e.add_mask(b, pts, 3, true), m1 = r.e.add_mask(b, pts, 3, true);
    AUREA_CHECK(m0 >= 0 && m1 >= 0);
    AUREA_CHECK(r.e.set_mask_falloff(b, static_cast<u32>(m1), 1));
    AUREA_CHECK(r.e.set_mask_props(b, static_cast<u32>(m0), 6, false, 0, 0, 1));
    // As flags da fileira (o que as duas interfaces leem).
    bridge::LayerRow rows[8]{};
    const u32 n = r.e.query_layers(rows, 8, nullptr, 0);
    bool sawShy = false, sawPreserve = false;
    for (u32 i = 0; i < n && i < 8; ++i) {
        if (rows[i].id == a) sawShy = (rows[i].flags & bridge::kLayerRowFlagShy) != 0;
        if (rows[i].id == b) sawPreserve = (rows[i].flags & bridge::kLayerRowFlagPreserveTransparency) != 0;
    }
    AUREA_CHECK(sawShy && sawPreserve);
    const std::string path = r.temp("aurea_ae_compositing.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK(r.comp()->hide_shy());
    Layer* la = r.named("tímida");
    Layer* lb = r.named("preserva");
    AUREA_CHECK(la && lb);
    if (!la || !lb) return;
    AUREA_CHECK(la->shy && !la->preserveTransparency);
    AUREA_CHECK(!lb->shy && lb->preserveTransparency);
    AUREA_CHECK_EQ(lb->masks.size(), static_cast<usize>(2));
    if (lb->masks.size() == 2) {
        AUREA_CHECK_EQ(lb->masks[0].featherFalloff, u8{0});
        AUREA_CHECK_EQ(lb->masks[1].featherFalloff, u8{1});
        AUREA_CHECK(lb->masks[0].operation == MaskOperation::Darken);
    }
    std::remove(path.c_str());
}

AUREA_TEST(AeCompositing, ShyAndHideShyAreUndoableAndDoNotTouchPixels) {
    AeRig r;
    const u64 a = r.shape();
    if (!a) return;
    AUREA_CHECK(r.e.set_layer_shy(a, true));
    AUREA_CHECK(r.e.set_hide_shy(true));
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(u).ok());
    AUREA_CHECK(!r.e.hide_shy());
    AUREA_CHECK(r.L(a)->shy);
    AUREA_CHECK(r.e.apply_command(u).ok());
    AUREA_CHECK(!r.L(a)->shy);
    // Preservar transparência recusa camada travada.
    r.L(a)->locked = true;
    AUREA_CHECK(!r.e.set_layer_preserve_transparency(a, true));
    AUREA_CHECK(!r.e.set_layer_shy(0xDEADull, true));
}

// -----------------------------------------------------------------------------
// B1 · ciclo de track matte
// -----------------------------------------------------------------------------
AUREA_TEST(AeCompositing, TrackMatteCyclesAreRefused) {
    AeRig r;
    const u64 a = r.shape(), b = r.shape(), c = r.shape();
    if (!a || !b || !c) return;
    AUREA_CHECK(r.e.set_track_matte(a, b, 1));       // A ← B
    AUREA_CHECK(!r.e.set_track_matte(b, a, 1));      // B ← A fecharia A→B→A
    AUREA_CHECK(r.L(b)->matteMode == MatteMode::None);
    AUREA_CHECK(r.e.set_track_matte(b, c, 3));       // B ← C (cadeia, sem ciclo)
    AUREA_CHECK(!r.e.set_track_matte(c, a, 1));      // C ← A fecharia A→B→C→A
    AUREA_CHECK(r.L(c)->matteMode == MatteMode::None);
    AUREA_CHECK(!r.e.set_track_matte(a, a, 1));      // a própria camada
    // Tirar a matte do meio libera o fecho.
    AUREA_CHECK(r.e.set_track_matte(b, 0, 0));
    AUREA_CHECK(r.e.set_track_matte(c, a, 1));
}

// -----------------------------------------------------------------------------
// B2 · curva de tempo entre quadros
// -----------------------------------------------------------------------------
AUREA_TEST(AeCompositing, TimeRemapFollowsTheCurveBetweenFramesAndKeepsWholeFrames) {
    Layer l;
    l.kind = LayerKind::Video;
    l.start = FrameIndex{0};
    l.end = FrameIndex{60};
    l.timeRemapEnabled = true;
    l.timeRemap.property = TrackProperty::TimeRemap;
    l.timeRemap.set(FrameIndex{0}, 0.0f, Interpolation::EaseInOut);
    l.timeRemap.set(FrameIndex{10}, 100.0f, Interpolation::EaseInOut);
    // Quadros inteiros: exatamente o valor de antes (o seek cai no mesmo quadro).
    for (i64 f = -2; f <= 12; ++f) {
        AUREA_CHECK_EQ(l.source_frame(FrameIndex{f}), static_cast<f64>(l.timeRemap.sample(FrameIndex{f})));
        AUREA_CHECK_EQ(l.source_frame_f(static_cast<f64>(f)), static_cast<f64>(l.timeRemap.sample(FrameIndex{f})));
    }
    // Entre quadros: a curva (ease), não a reta entre os dois quadros.
    f64 maxGap = 0;
    for (const f64 t : {0.5, 1.5, 2.25, 4.5, 8.75}) {
        const f64 got = l.source_frame_f(t);
        AUREA_CHECK_NEAR(got, static_cast<f64>(l.timeRemap.sample_f(t)), 1e-4);
        const f64 a = l.timeRemap.sample(FrameIndex{static_cast<i64>(std::floor(t))});
        const f64 b = l.timeRemap.sample(FrameIndex{static_cast<i64>(std::floor(t)) + 1});
        const f64 poly = a + (b - a) * (t - std::floor(t));
        maxGap = std::max(maxGap, std::fabs(got - poly));
        // Monótona entre os vizinhos inteiros.
        AUREA_CHECK(got >= std::min(a, b) - 1e-4 && got <= std::max(a, b) + 1e-4);
    }
    std::printf("    maior distância da poligonal: %.4f quadros ", maxGap);
    AUREA_CHECK(maxGap > 1e-3);
}

// -----------------------------------------------------------------------------
// B3 · cache de índice atômico (cópia continua valor comum)
// -----------------------------------------------------------------------------
AUREA_TEST(AeCompositing, TrackIndexCacheIsAtomicAndTracksStayCopyable) {
    static_assert(std::is_copy_constructible_v<Track> && std::is_copy_assignable_v<Track>);
    static_assert(std::is_nothrow_move_constructible_v<Track>);
    Track t;
    t.property = TrackProperty::Opacity;
    for (i64 f = 0; f <= 100; f += 10) t.set(FrameIndex{f}, static_cast<f32>(f));
    Track copy = t;
    AUREA_CHECK_NEAR(copy.sample(FrameIndex{55}), 55.0f, 1e-4f);
    // Duas threads lendo a MESMA trilha (render + áudio): resultados certos.
    std::atomic<u32> wrong{0};
    auto reader = [&](i64 phase) {
        for (int i = 0; i < 20000; ++i) {
            const i64 f = (i * 7 + phase) % 101;
            if (std::fabs(t.sample(FrameIndex{f}) - static_cast<f32>(f)) > 1e-3f) ++wrong;
            const f64 tf = static_cast<f64>(f) + 0.5;
            if (f < 100 && std::fabs(t.sample_f(tf) - static_cast<f32>(tf)) > 1e-3f) ++wrong;
        }
    };
    std::thread th1(reader, 0), th2(reader, 53);
    th1.join();
    th2.join();
    AUREA_CHECK_EQ(wrong.load(), 0u);
}

// -----------------------------------------------------------------------------
// B9 · luz ambiente
// -----------------------------------------------------------------------------
AUREA_TEST(AeCompositing, AmbientLightIsCreatableAndItsKindCanBeSet) {
    AeRig r;
    const auto amb = r.e.add_light(3);
    AUREA_CHECK(amb.ok());
    if (!amb.ok()) return;
    AUREA_CHECK(r.L(*amb)->light.kind == LightKind::Ambient);
    AUREA_CHECK(r.L(*amb)->effects.empty());   // não é o Flare
    f32 v[11]{};
    AUREA_CHECK(r.e.query_light(*amb, v, 11));
    AUREA_CHECK_NEAR(v[0], 3.0f, 0.0f);
    // Flare 3D tem id próprio (era o 3).
    const auto flare = r.e.add_light(Engine::kAddLightFlare);
    AUREA_CHECK(flare.ok());
    if (flare.ok()) {
        AUREA_CHECK(r.L(*flare)->light.kind == LightKind::Point);
        AUREA_CHECK(!r.L(*flare)->effects.empty());
    }
    AUREA_CHECK(!r.e.add_light(Engine::kAddLightFlare + 1).ok());
    // O tipo volta e vai para ambiente pelo comando (antes recusava > 2).
    const auto spot = r.e.add_light(2);
    AUREA_CHECK(spot.ok());
    if (!spot.ok()) return;
    auto set_kind = [&](f32 kind) {
        Command c;
        c.type = CommandType::LayerSetLightParam;
        c.shape_param.layer = LayerId::unpack(*spot);
        c.shape_param.param = 0;
        c.shape_param.value = kind;
        return r.e.apply_command(c);
    };
    AUREA_CHECK(set_kind(3.0f).ok());
    AUREA_CHECK(r.L(*spot)->light.kind == LightKind::Ambient);
    AUREA_CHECK(set_kind(2.0f).ok());
    AUREA_CHECK(r.L(*spot)->light.kind == LightKind::Spot);
    AUREA_CHECK(!set_kind(4.0f).ok());
    AUREA_CHECK(r.L(*spot)->light.kind == LightKind::Spot);
    // Sobrevive ao arquivo.
    AUREA_CHECK(set_kind(3.0f).ok());
    const std::string path = r.temp("aurea_ae_ambient.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    u32 ambients = 0;
    r.comp()->layers().for_each([&](LayerId, const Layer& l) {
        if (l.kind == LayerKind::Light && l.light.kind == LightKind::Ambient) ++ambients;
    });
    AUREA_CHECK_EQ(ambients, 2u);
    std::remove(path.c_str());
}
