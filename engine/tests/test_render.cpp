// =============================================================================
//  Testes do renderer SEM GPU: FrameGraph, EffectGraph, Motion Tile (geometria
//  portada do Aurea antigo), ShaderLibrary e o compositor contra um backend
//  que só grava comandos. Os testes com a GPU de verdade estão em test_gpu.cpp.
// =============================================================================
#include "TestFramework.hpp"
#include "aurea/render/PreviewCachePolicy.hpp"
#include "aurea/render/ShutterPlan.hpp"
#include "aurea/render/MotionBlurBounds.hpp"
#include <limits>

AUREA_TEST(MotionBlur, ExposurePhaseAdaptivePathAndMemoryLimits) {
    using namespace aurea;
    MotionBlurSettings settings; settings.enabled = true; settings.samples = 8; settings.adaptiveLimit = 128;
    auto shutter = shutter_window(settings); shutter.count = 8;
    AUREA_CHECK_NEAR(shutter.begin, -.25, 1e-8);
    AUREA_CHECK_NEAR(shutter.duration, .5, 1e-8);
    AUREA_CHECK_NEAR((shutter.offset(0) + shutter.offset(7)) * .5, 0., 1e-8);
    settings.shutterPhase = 0;
    shutter = shutter_window(settings, 2); shutter.count = 8;
    AUREA_CHECK(shutter.offset(0) > 0);
    AUREA_CHECK_NEAR(shutter.duration, 1., 1e-8);
    AUREA_CHECK_EQ(shutter_sample_count(settings, true, 1, 0), 1u);
    AUREA_CHECK_EQ(shutter_sample_count(settings, true, 1, 200), 128u);
    AUREA_CHECK(shutter_sample_count(settings, false, .1f, 200) <= 13u);
    AUREA_CHECK_EQ(shutter_sample_count(settings, true, 1, 200, true, 12), 12u);
    AUREA_CHECK_EQ(shutter_window(settings, 0).duration, 0.);
    // Uniform 1/8 probes would miss all motion over exactly eight revolutions.
    const f64 travel = shutter_projected_path(shutter, 40, 10, [&](f64 t) {
        return Mat4::from_quat(Quat::from_axis_angle(Vec3{0,0,1}, static_cast<f32>(t * 16 * kPi)));
    });
    AUREA_CHECK(travel > 200);
    settings.shutterAngle = std::numeric_limits<f32>::quiet_NaN();
    AUREA_CHECK_EQ(shutter_window(settings).duration, 0.);
}

AUREA_TEST(MotionBlur, CroppedExposureKeepsThePixelGridAndFallsBackAtTheCameraPlane) {
    using namespace aurea;
    const std::array<Mat4, 2> samples{Mat4::translation(Vec3{10.25f, 20.5f, 0}),
        Mat4::translation(Vec3{20.75f, 25.25f, 0})};
    for (u32 den : {1u, 2u}) {
        const auto crop = motion_blur_bounds(Rect{0,0,80,40}, samples, Mat4::identity(), 1920,1080,1920/den,1080/den);
        AUREA_CHECK(crop.width < 110 / den + 4);
        AUREA_CHECK(crop.height < 60 / den + 4);
        AUREA_CHECK_NEAR(crop.region.x / den, std::floor(crop.region.x / den), 1e-6);
        AUREA_CHECK_NEAR(crop.region.y / den, std::floor(crop.region.y / den), 1e-6);
        AUREA_CHECK_NEAR(crop.region.w / den, crop.width, 1e-6);
        AUREA_CHECK_NEAR(crop.region.h / den, crop.height, 1e-6);
        AUREA_CHECK(crop.region.x <= 10.25f - 2 * den);
        AUREA_CHECK(crop.region.x + crop.region.w >= 100.75f + 2 * den);
    }
    auto crossing = Mat4::identity(); crossing.col[0].w = -.1f;
    const std::array<Mat4, 1> invalid{crossing};
    const auto full = motion_blur_bounds(Rect{0,0,80,40}, invalid, Mat4::identity(),1920,1080,960,540);
    AUREA_CHECK_EQ(full.width,960u); AUREA_CHECK_EQ(full.height,540u);
    const auto shifted = motion_blur_bounds(Rect{-15,-10,30,20}, samples,
        Mat4::translation(Vec3{100,100,0}),1920,1080,1920,1080);
    AUREA_CHECK(shifted.region.x > 90); AUREA_CHECK(shifted.region.y > 105);
}

AUREA_TEST(PreviewBuffer, BudgetAndRangeAreHardLimits) {
    using namespace aurea;
    AUREA_CHECK_EQ(preview_cache_capacity(3840, 2160, 48ull << 20), 0u);
    AUREA_CHECK_EQ(preview_cache_capacity(1920, 1080, 48ull << 20), 3u);
    AUREA_CHECK_EQ(preview_cache_capacity(0, 1080, 48ull << 20), 0u);
    AUREA_CHECK_EQ(preview_cache_capacity(0xffffffffu, 0xffffffffu, 48ull << 20), 0u);
    AUREA_CHECK_EQ(preview_cache_target(30., 30, 300), 6u);
    AUREA_CHECK_EQ(preview_cache_target(30., 3, 300), 3u);
    AUREA_CHECK_EQ(preview_cache_target(30., 30, 2), 2u);
    AUREA_CHECK_EQ(preview_cache_target(30., 30, 0), 0u);
    AUREA_CHECK_EQ(preview_cache_target(60., 30, 300), 11u);
    AUREA_CHECK_EQ(preview_cache_target(30., 30, 300, 2.f), 11u);
    AUREA_CHECK_EQ(preview_cache_target(30., 30, 300, .5f), 3u);
    AUREA_CHECK_EQ(preview_cache_target(240., 30, 300, 16.f), 30u);
    AUREA_CHECK_EQ(preview_cache_target(std::numeric_limits<f64>::quiet_NaN(), 30, 300), 6u);
    AUREA_CHECK(!preview_buffer_expired(349'999'999, 1));
    AUREA_CHECK(preview_buffer_expired(350'000'000, 1));
    AUREA_CHECK(!preview_buffer_expired(1'199'999'999, 0));
    AUREA_CHECK(preview_buffer_expired(1'200'000'000, 0));
    AUREA_CHECK_EQ(preview_buffer_status(3, 15, true), 0x80000f03u);
    AUREA_CHECK_EQ(preview_buffer_status(3, 15, false, true), 0x40000f03u);
}

AUREA_TEST(PreviewBuffer, LowMemoryClassCapsTheBudgetAndPressureScalesIt) {
    using namespace aurea;
    constexpr u64 MiB = 1ull << 20, GiB = 1ull << 30;
    // Faixas de sempre (classe não-LOW): nada muda.
    AUREA_CHECK_EQ(preview_cache_budget(2 * GiB, 0), 32 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 0), 64 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(6 * GiB, 0), 320 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(12 * GiB, 0), 512 * MiB);
    // Classe LOW (Galaxy A15/A16 de 4 GB, realme RMX2020, moto g52): 32 MiB no
    // máximo em qualquer faixa, e ainda limitado por 1/4 do processo.
    AUREA_CHECK_EQ(kPreviewCacheLowClassBudget, 32 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 0, 0.f, true), 32 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(6 * GiB, 0, 0.f, true), 32 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(0, 0, 0.f, true), 32 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 64 * MiB, 0.f, true), 16 * MiB);
    // moto g52 medido: orcamento_mb=308 → 77 MiB de 1/4, a faixa LOW manda.
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 308 * MiB, 0.f, true), 32 * MiB);
    // Pressão: metade, um quarto, nada.
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 0, .5f, true), 16 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 0, .7f, true), 8 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 0, .85f, true), 0ull);
    AUREA_CHECK_EQ(preview_cache_budget(3700 * MiB, 0, std::numeric_limits<f32>::quiet_NaN(), true), 0ull);
    // 32 MiB a 1/4 de 1080p (480×270 RGBA8): 64 quadros, nunca os 300.
    AUREA_CHECK_EQ(preview_cache_capacity(480, 270, 32 * MiB, 4), 64u);
    AUREA_CHECK(kPreviewPressureHoldNs >= 10'000'000'000ull);
}
#include "MockBackend.hpp"
#include "P010VideoFixture.hpp"
#include "SyntheticVideo.hpp"
#include "RgbaVideoFixture.hpp"

#include "aurea/audio/Spectrum.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/MotionTile.hpp"
#include "aurea/effects/ShakeMotion.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/render/PreviewRefill.hpp"

#include <cmath>
#include <algorithm>
#include <limits>
#include <string_view>
#include <vector>

using namespace aurea;
using aurea::test::MockBackend;

AUREA_TEST(PreviewRefill, SeekHoldsAreBoundedAndRecoverOnDecodeAndProjectChange) {
    PreviewRefill refill;
    AUREA_CHECK(!refill.hold(true, false, 100)); // initial frame, no previous picture
    AUREA_CHECK(refill.hold(true, true, 1000));
    AUREA_CHECK(refill.hold(true, true, 200001000)); // rapid seeks do not restart timeout
    AUREA_CHECK(!refill.hold(true, true, 250001000));
    AUREA_CHECK(!refill.hold(true, true, 900001000)); // failed source cannot freeze forever
    AUREA_CHECK(!refill.hold(false, true, 900002000)); // decoded picture releases hold
    AUREA_CHECK(refill.hold(true, true, 900003000));
    AUREA_CHECK(!refill.hold(true, false, 900004000)); // new project/playback
    AUREA_CHECK(refill.hold(true, true, 1900000000));
}

AUREA_TEST(CameraShake, ContinuousSeededMotionWorksInBothTimeDirections) {
    shake::Settings s; s.rotation = 20; s.zoom = 10;
    for (u32 style = 0; style < 3; ++style) {
        s.style = style;
        for (int cell = -8; cell < 8; ++cell) {
            const double t = cell / 8.0;
            const auto a = shake::sample(s, t - 1e-7), b = shake::sample(s, t + 1e-7);
            AUREA_CHECK_NEAR(a.x, b.x, .01); AUREA_CHECK_NEAR(a.y, b.y, .01);
            AUREA_CHECK_NEAR(a.rotation, b.rotation, .01); AUREA_CHECK(a.scale > 0);
        }
        const auto a = shake::sample(s, 1.25);
        (void)shake::sample(s, 8.0);
        const auto b = shake::sample(s, 1.25);
        AUREA_CHECK_EQ(a.x, b.x); AUREA_CHECK_EQ(a.scale, b.scale);
    }
    s.style = 0; s.separate = false;
    AUREA_CHECK_EQ(shake::sample(s, .73).x, shake::sample(s, .73).y);
    s.amount = 0;
    AUREA_CHECK_EQ(shake::sample(s, .73).scale, 1.f);
    AUREA_CHECK_EQ(shake::sample(s, .73).rotation, 0.f);
}

AUREA_TEST(ParentingHelper, LocksOrientationAndScaleButKeepsTheAnchorOnItsOrbit) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    auto project = Project::create_new(320, 180, 30, "parenting");
    auto* comp = project->timeline().composition(project->timeline().root());
    const auto parent = comp->add_layer(LayerKind::Null, "parent");
    const auto child = comp->add_layer(LayerKind::Shape, "child");
    auto* p = comp->layer(parent); auto* c = comp->layer(child);
    p->transform.position = {80, 60, 0};
    p->transform.rotation.z = 90;
    p->transform.scale = {2, 3, 1};
    c->parent = parent; c->transform.position = {40, 0, 0};
    c->transform.anchor = {10, 15, 0};
    const Mat4 normal = layer_world_matrix(*comp, *c, FrameIndex{0});
    EffectInstance helper; helper.type = effect_type_id(effect_keys::kParentingHelper); helper.id = 0;
    initialize_instance(helper, *registry.params(helper.type));
    helper.params[0].constant = ParamValue::scalar(0);
    helper.params[1].constant = ParamValue::scalar(0);
    c->effects.push_back(helper);
    const Mat4 locked = layer_world_matrix(*comp, *c, FrameIndex{0});
    AUREA_CHECK((normal.transform_point(c->transform.anchor) - locked.transform_point(c->transform.anchor)).length() < .001f);
    AUREA_CHECK_NEAR(locked.col[0].x, 1.f, .001f);
    AUREA_CHECK_NEAR(locked.col[0].y, 0.f, .001f);
    AUREA_CHECK_NEAR(locked.col[1].y, 1.f, .001f);
    c->effects[0].enabled = false;
    const Mat4 disabled = layer_world_matrix(*comp, *c, FrameIndex{0});
    for (u32 i = 0; i < 4; ++i) {
        AUREA_CHECK_NEAR(disabled.col[i].x, normal.col[i].x, .001f);
        AUREA_CHECK_NEAR(disabled.col[i].y, normal.col[i].y, .001f);
    }
    c->effects[0].enabled = true;
    c->tracks.get_or_create(TrackProperty::EffectParam, 0, param_track_key(0, 0)).set(FrameIndex{0}, 50.f);
    const Mat4 half = layer_world_matrix(*comp, *c, FrameIndex{0});
    AUREA_CHECK_NEAR(half.col[0].x, std::sqrt(.5f), .001f);
    AUREA_CHECK_NEAR(half.col[0].y, std::sqrt(.5f), .001f);
    for (f32 degrees : {179.f, 181.f, 540.f, -540.f}) {
        p->transform.rotation.z = degrees;
        const Mat4 weighted = layer_world_matrix(*comp, *c, FrameIndex{0});
        AUREA_CHECK_NEAR(weighted.col[0].x, std::cos(degrees * .5f * kDeg2Rad), .001f);
        AUREA_CHECK_NEAR(weighted.col[0].y, std::sin(degrees * .5f * kDeg2Rad), .001f);
    }
}

AUREA_TEST(ParentingHelper, ThreeDHierarchyKeepsPivotAndNormalDefault) {
    EffectRegistry registry; register_builtin_effects(registry);
    auto project = Project::create_new(320, 180, 30, "parenting3d");
    auto* comp = project->timeline().composition(project->timeline().root());
    auto parent = comp->add_layer(LayerKind::Null, "parent");
    auto child = comp->add_layer(LayerKind::Null, "child");
    auto grandchild = comp->add_layer(LayerKind::Shape, "grandchild");
    auto* p = comp->layer(parent); auto* c = comp->layer(child); auto* g = comp->layer(grandchild);
    p->threeD = true; p->transform.rotation = {20, 30, 45}; p->transform.scale = {2, 3, 1};
    c->parent = parent; c->transform.position = {20, 10, 5}; c->transform.anchor = {4, 3, 2};
    g->parent = child; g->transform.position = {10, 0, 0};
    const Mat4 original = layer_world_3d(*comp, *c, FrameIndex{0});
    EffectInstance helper; helper.type = effect_type_id(effect_keys::kParentingHelper); helper.id = 0;
    initialize_instance(helper, *registry.params(helper.type)); c->effects.push_back(helper);
    const Mat4 normal = layer_world_3d(*comp, *c, FrameIndex{0});
    AUREA_CHECK((normal.transform_point({2, 3, 4}) - original.transform_point({2, 3, 4})).length() < .001f);
    c->effects[0].params[0].constant = ParamValue::scalar(0);
    c->effects[0].params[1].constant = ParamValue::scalar(0);
    const Mat4 locked = layer_world_3d(*comp, *c, FrameIndex{0});
    AUREA_CHECK((normal.transform_point(c->transform.anchor) - locked.transform_point(c->transform.anchor)).length() < .001f);
    AUREA_CHECK_NEAR(locked.col[0].x, 1.f, .001f);
    AUREA_CHECK_NEAR(locked.col[1].y, 1.f, .001f);
    AUREA_CHECK_NEAR(locked.col[2].z, 1.f, .001f);
    const Mat4 inherited = layer_world_3d(*comp, *g, FrameIndex{0});
    AUREA_CHECK_NEAR(inherited.col[0].x, 1.f, .001f);
    AUREA_CHECK_NEAR(inherited.col[1].y, 1.f, .001f);
}

AUREA_TEST(TextureMemory, MipsBlocksLayersAndVolumesUseTheirActualFootprint) {
    TextureDesc d;
    d.width = 8; d.height = 4; d.mipLevels = 4;
    AUREA_CHECK_EQ(d.estimated_bytes(), 172ull); // (32 + 8 + 2 + 1) RGBA texels
    d.width = 1; d.height = 8;
    AUREA_CHECK_EQ(d.estimated_bytes(), 60ull); // thin chain is almost 2x, not 4/3
    d.width = 5; d.height = 7; d.mipLevels = 3;
    for (SurfaceFormat f : {SurfaceFormat::BC7, SurfaceFormat::ETC2_RGBA8, SurfaceFormat::ASTC4x4}) {
        d.format = f;
        AUREA_CHECK_EQ(d.estimated_bytes(), 96ull); // 4 + 1 + 1 complete blocks
    }
    d.width = d.height = 4; d.cube = true; d.layers = 6;
    AUREA_CHECK_EQ(d.estimated_bytes(), 288ull); // 3 blocks per face
    d.cube = false; d.layers = 2; d.format = SurfaceFormat::RGBA16F;
    AUREA_CHECK_EQ(d.estimated_bytes(), 336ull);
    d.layers = 1; d.depth = 4;
    AUREA_CHECK_EQ(d.estimated_bytes(), 584ull); // 4^3 + 2^3 + 1^3 RGBA16F texels
    d.depth = 1; d.sampleCount = 4; d.mipLevels = 1;
    AUREA_CHECK_EQ(d.estimated_bytes(), 512ull);
    d.width = d.height = d.depth = UINT32_MAX;
    AUREA_CHECK_EQ(d.estimated_bytes(), UINT64_MAX);
    d.width = 0;
    AUREA_CHECK_EQ(d.estimated_bytes(), 0ull);
}

namespace {

TextureDesc rt(u32 w = 64, u32 h = 64) {
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.sampled = true;
    return d;
}

struct GraphFixture {
    MockBackend backend;
    TransientTexturePool pool;
    FrameGraph graph;

    Status run() {
        FrameBegin fb;
        (void)backend.begin_frame(fb);
        pool.begin_frame(backend, fb.frameNumber);
        const Status s = graph.compile(pool);
        if (s.ok()) graph.execute(*fb.commands, false);
        graph.release(pool);
        pool.end_frame();
        (void)backend.end_frame();
        return s;
    }
};

u32 first_event(const MockBackend& b, MockBackend::Event::Kind k, u64 tex) {
    for (u32 i = 0; i < b.events.size(); ++i) {
        if (b.events[i].kind == k && b.events[i].texture == tex) return i;
    }
    return kInvalidIndex;
}

} // namespace

// =============================================================================
// FrameGraph
// =============================================================================
AUREA_TEST(FrameGraph, OrderFollowsDependenciesNotDeclaration) {
    GraphFixture f;
    const FGTexture x = f.graph.create_texture("x", rt());
    const FGTexture y = f.graph.create_texture("y", rt());
    // B é declarado ANTES de A, mas lê o que A escreve.
    const u32 b = f.graph.add_raster_pass("B", PassStage::Effects, y, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.read(b, x);
    const u32 a = f.graph.add_raster_pass("A", PassStage::Effects, x, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.set_output(y, ResourceState::ShaderRead);
    AUREA_CHECK(f.run().ok());
    AUREA_CHECK_EQ(f.graph.order().size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(f.graph.order()[0], a);
    AUREA_CHECK_EQ(f.graph.order()[1], b);
}

AUREA_TEST(FrameGraph, PassWithoutReaderIsCulled) {
    GraphFixture f;
    const FGTexture out = f.graph.create_texture("saida", rt());
    const FGTexture orphan = f.graph.create_texture("orfa", rt());
    (void)f.graph.add_raster_pass("util", PassStage::Composite, out, LoadOp::Clear, {}, [](PassContext&) {});
    (void)f.graph.add_raster_pass("ninguem-le", PassStage::Effects, orphan, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.set_output(out, ResourceState::ShaderRead);
    AUREA_CHECK(f.run().ok());
    AUREA_CHECK_EQ(f.graph.stats().passesCulled, static_cast<u32>(1));
    AUREA_CHECK_EQ(f.graph.stats().passesExecuted, static_cast<u32>(1));
    // Recurso de passe podado não aloca textura.
    AUREA_CHECK_EQ(f.graph.stats().transientTextures, static_cast<u32>(1));
}

AUREA_TEST(FrameGraph, RecordingFailureStopsLaterPassesAndOutputTransitions) {
    struct RecordingCommands final : CommandList {
        u32 failAt = 0, completed = 0, passDepth = 0, timerDepth = 0, labelDepth = 0;
        u32 presentTransitions = 0, commandsAfterFailure = 0;
        bool failed = false, safeBoundaries = true;
        void observe() { if (failed) ++commandsAfterFailure; }
        void barrier(TextureHandle, ResourceState state, bool) noexcept override {
            observe(); if (state == ResourceState::Present) ++presentTransitions;
        }
        void begin_render_pass(const RenderPassBegin&) noexcept override { observe(); ++passDepth; }
        void end_render_pass() noexcept override { observe(); --passDepth; }
        void begin_timer(const char*) noexcept override { observe(); ++timerDepth; }
        void end_timer() noexcept override { observe(); --timerDepth; }
        void begin_label(const char*) noexcept override { observe(); ++labelDepth; }
        void end_label() noexcept override { observe(); --labelDepth; }
        Status finish_pass() noexcept override {
            observe();
            safeBoundaries &= passDepth == 0 && timerDepth == 0 && labelDepth == 0;
            failed = ++completed == failAt;
            return failed ? Status{Errc::OutOfMemory, "injected recording failure"} : OkStatus;
        }
        void bind_pipeline(PipelineHandle) noexcept override { observe(); }
        void bind_texture(u32, TextureHandle, SamplerHandle) noexcept override { observe(); }
        void bind_storage_image(u32, TextureHandle) noexcept override { observe(); }
        void bind_storage_buffer(BufferHandle) noexcept override { observe(); }
        void bind_storage_buffer_at(u32, BufferHandle) noexcept override { observe(); }
        void set_uniforms(const void*, u32) noexcept override { observe(); }
        void push_constants(const void*, u32) noexcept override { observe(); }
        void set_viewport(f32, f32, f32, f32) noexcept override { observe(); }
        void set_scissor(i32, i32, u32, u32) noexcept override { observe(); }
        void draw(u32, u32, u32) noexcept override { observe(); }
        void bind_vertex_buffer(u32, BufferHandle, u64) noexcept override { observe(); }
        void bind_index_buffer(BufferHandle, u64, IndexType) noexcept override { observe(); }
        void draw_indexed(u32, u32, u32, i32, u32) noexcept override { observe(); }
        void dispatch(u32, u32, u32) noexcept override { observe(); }
        void copy_texture(TextureHandle, TextureHandle) noexcept override { observe(); }
        void copy_texture_to_buffer(TextureHandle, BufferHandle) noexcept override { observe(); }
    };
    for (bool timers : {false, true}) for (u32 failAt : {0u, 1u, 3u, 5u}) {
        GraphFixture fixture;
        RecordingCommands commands;
        commands.failAt = failAt;
        std::vector<u32> visited;
        FGTexture previous;
        for (u32 i = 0; i < 5; ++i) {
            const auto target = fixture.graph.create_texture("checkpoint target", rt(8, 8));
            const auto pass = fixture.graph.add_raster_pass("checkpoint pass", PassStage::Effects,
                target, LoadOp::Clear, {}, [&, i](PassContext& context) {
                    visited.push_back(i);
                    context.cmds.draw(3);
                });
            if (previous.valid()) fixture.graph.read(pass, previous);
            previous = target;
        }
        fixture.graph.set_output(previous, ResourceState::Present);
        FrameBegin frame;
        AUREA_CHECK(fixture.backend.begin_frame(frame).ok());
        fixture.pool.begin_frame(fixture.backend, frame.frameNumber);
        AUREA_CHECK(fixture.graph.compile(fixture.pool).ok());
        fixture.graph.execute(commands, timers);
        const u32 expected = failAt ? failAt : 5;
        AUREA_CHECK_EQ(visited.size(), static_cast<usize>(expected));
        for (u32 i = 0; i < visited.size(); ++i) AUREA_CHECK_EQ(visited[i], i);
        AUREA_CHECK_EQ(commands.completed, expected);
        AUREA_CHECK(commands.safeBoundaries);
        AUREA_CHECK_EQ(commands.commandsAfterFailure, 0u);
        AUREA_CHECK_EQ(commands.presentTransitions, failAt ? 0u : 1u);
        fixture.graph.release(fixture.pool);
        fixture.pool.end_frame();
        AUREA_CHECK(fixture.backend.end_frame().ok());
    }
}

AUREA_TEST(FrameGraph, TextureMemoryIsReusedWhenLifetimesDoNotOverlap) {
    // A textura A morre no passe 2; a mesma memória vira a textura C no 3.
    GraphFixture g;
    const FGTexture a = g.graph.create_texture("t1", rt());
    const FGTexture b = g.graph.create_texture("t2", rt());
    const FGTexture c = g.graph.create_texture("t3", rt());
    (void)g.graph.add_raster_pass("p1", PassStage::Effects, a, LoadOp::Clear, {}, [](PassContext&) {});
    const u32 q2 = g.graph.add_raster_pass("p2", PassStage::Effects, b, LoadOp::DontCare, {}, [](PassContext&) {});
    g.graph.read(q2, a);
    const u32 q3 = g.graph.add_raster_pass("p3", PassStage::Effects, c, LoadOp::DontCare, {}, [](PassContext&) {});
    g.graph.read(q3, b);
    g.graph.set_output(c, ResourceState::ShaderRead);
    FrameBegin fb;
    (void)g.backend.begin_frame(fb);
    g.pool.begin_frame(g.backend, 1);
    AUREA_CHECK(g.graph.compile(g.pool).ok());
    AUREA_CHECK_EQ(g.graph.stats().transientTextures, static_cast<u32>(3));
    AUREA_CHECK_EQ(g.graph.stats().physicalTextures, static_cast<u32>(2));
    AUREA_CHECK_EQ(g.graph.stats().aliasedTextures, static_cast<u32>(1));
    AUREA_CHECK_EQ(g.graph.physical_slot(a), g.graph.physical_slot(c));
    AUREA_CHECK(g.graph.physical_slot(a) != g.graph.physical_slot(b));
    g.graph.release(g.pool);
}

AUREA_TEST(FrameGraph, OverlappingLifetimesNeverShareMemory) {
    GraphFixture g;
    const FGTexture a = g.graph.create_texture("a", rt());
    const FGTexture b = g.graph.create_texture("b", rt());
    const FGTexture c = g.graph.create_texture("c", rt());
    (void)g.graph.add_raster_pass("p1", PassStage::Effects, a, LoadOp::Clear, {}, [](PassContext&) {});
    const u32 p2 = g.graph.add_raster_pass("p2", PassStage::Effects, b, LoadOp::DontCare, {}, [](PassContext&) {});
    g.graph.read(p2, a);
    const u32 p3 = g.graph.add_raster_pass("p3", PassStage::Effects, c, LoadOp::DontCare, {}, [](PassContext&) {});
    g.graph.read(p3, b);
    g.graph.read(p3, a);   // `a` ainda vivo quando `c` nasce
    g.graph.set_output(c, ResourceState::ShaderRead);
    FrameBegin fb;
    (void)g.backend.begin_frame(fb);
    g.pool.begin_frame(g.backend, 1);
    AUREA_CHECK(g.graph.compile(g.pool).ok());
    AUREA_CHECK_EQ(g.graph.stats().physicalTextures, static_cast<u32>(3));
    AUREA_CHECK(g.graph.physical_slot(a) != g.graph.physical_slot(c));
    g.graph.release(g.pool);
}

AUREA_TEST(FrameGraph, DifferentShapesNeverAlias) {
    GraphFixture g;
    const FGTexture a = g.graph.create_texture("a", rt(64, 64));
    const FGTexture b = g.graph.create_texture("b", rt(32, 32));
    const FGTexture c = g.graph.create_texture("c", rt(32, 64));
    (void)g.graph.add_raster_pass("p1", PassStage::Effects, a, LoadOp::Clear, {}, [](PassContext&) {});
    const u32 p2 = g.graph.add_raster_pass("p2", PassStage::Effects, b, LoadOp::DontCare, {}, [](PassContext&) {});
    g.graph.read(p2, a);
    const u32 p3 = g.graph.add_raster_pass("p3", PassStage::Effects, c, LoadOp::DontCare, {}, [](PassContext&) {});
    g.graph.read(p3, b);
    g.graph.set_output(c, ResourceState::ShaderRead);
    FrameBegin fb;
    (void)g.backend.begin_frame(fb);
    g.pool.begin_frame(g.backend, 1);
    AUREA_CHECK(g.graph.compile(g.pool).ok());
    AUREA_CHECK_EQ(g.graph.stats().aliasedTextures, static_cast<u32>(0));
    g.graph.release(g.pool);
}

AUREA_TEST(FrameGraph, BarriersComeBeforeEveryUse) {
    GraphFixture f;
    const FGTexture t1 = f.graph.create_texture("t1", rt());
    const FGTexture t2 = f.graph.create_texture("t2", rt());
    (void)f.graph.add_raster_pass("p1", PassStage::Effects, t1, LoadOp::Clear, {}, [](PassContext&) {});
    const u32 p2 = f.graph.add_raster_pass("p2", PassStage::Effects, t2, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.read(p2, t1);
    f.graph.set_output(t2, ResourceState::ShaderRead);
    AUREA_CHECK(f.run().ok());

    const auto& ev = f.backend.events;
    const u64 id1 = f.backend.textures.size() >= 1 ? 1 : 0;
    // t1: vira anexo (descartando) antes do render pass dele...
    const u32 barrierAttach = first_event(f.backend, MockBackend::Event::Barrier, id1);
    const u32 pass1 = first_event(f.backend, MockBackend::Event::BeginPass, id1);
    AUREA_CHECK(barrierAttach < pass1);
    AUREA_CHECK(ev[barrierAttach].state == ResourceState::ColorAttachment);
    AUREA_CHECK(ev[barrierAttach].discard);
    // ...e vira leitura de shader antes do passe que o lê.
    bool readBarrier = false;
    for (u32 i = pass1; i < ev.size(); ++i) {
        if (ev[i].kind == MockBackend::Event::Barrier && ev[i].texture == id1
            && ev[i].state == ResourceState::ShaderRead) {
            readBarrier = true;
            // Nenhum render pass aberto no momento da barreira.
            u32 open = 0;
            for (u32 k = 0; k < i; ++k) {
                if (ev[k].kind == MockBackend::Event::BeginPass) ++open;
                if (ev[k].kind == MockBackend::Event::EndPass) --open;
            }
            AUREA_CHECK_EQ(open, static_cast<u32>(0));
            break;
        }
    }
    AUREA_CHECK(readBarrier);
}

AUREA_TEST(FrameGraph, OutputEndsInTheRequestedState) {
    GraphFixture f;
    TextureDesc bb;
    bb.width = 100;
    bb.height = 50;
    bb.format = SurfaceFormat::RGBA8;
    const FGTexture swap = f.graph.import_texture("swapchain", TextureHandle{777}, bb);
    (void)f.graph.add_raster_pass("saida", PassStage::Output, swap, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.set_output(swap, ResourceState::Present);
    AUREA_CHECK(f.run().ok());
    const auto& last = f.backend.events.back();
    AUREA_CHECK(last.kind == MockBackend::Event::Barrier);
    AUREA_CHECK_EQ(last.texture, static_cast<u64>(777));
    AUREA_CHECK(last.state == ResourceState::Present);
}

AUREA_TEST(FrameGraph, CycleIsRejected) {
    GraphFixture f;
    const FGTexture x = f.graph.create_texture("x", rt());
    const FGTexture y = f.graph.create_texture("y", rt());
    const u32 a = f.graph.add_raster_pass("A", PassStage::Effects, x, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.read(a, y);
    const u32 b = f.graph.add_raster_pass("B", PassStage::Effects, y, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.read(b, x);
    f.graph.set_output(x, ResourceState::ShaderRead);
    AUREA_CHECK(!f.run().ok());
}

AUREA_TEST(FrameGraph, ReadingBeforeWritingIsRejected) {
    GraphFixture f;
    const FGTexture never = f.graph.create_texture("nunca-escrita", rt());
    const FGTexture out = f.graph.create_texture("saida", rt());
    const u32 p = f.graph.add_raster_pass("le", PassStage::Effects, out, LoadOp::Clear, {}, [](PassContext&) {});
    f.graph.read(p, never);
    f.graph.set_output(out, ResourceState::ShaderRead);
    AUREA_CHECK(!f.run().ok());
}

AUREA_TEST(FrameGraph, LoadOpLoadKeepsThePreviousWriterAlive) {
    GraphFixture f;
    const FGTexture t = f.graph.create_texture("acumula", rt());
    const u32 a = f.graph.add_raster_pass("limpa", PassStage::Composite, t, LoadOp::Clear, {}, [](PassContext&) {});
    const u32 b = f.graph.add_raster_pass("soma", PassStage::Composite, t, LoadOp::Load, {}, [](PassContext&) {});
    f.graph.set_output(t, ResourceState::ShaderRead);
    AUREA_CHECK(f.run().ok());
    AUREA_CHECK_EQ(f.graph.stats().passesExecuted, static_cast<u32>(2));
    AUREA_CHECK_EQ(f.graph.order()[0], a);
    AUREA_CHECK_EQ(f.graph.order()[1], b);
}

AUREA_TEST(FrameGraph, SteadyStateCreatesNoTextures) {
    // Playback: o mesmo grafo frame após frame NÃO cria nem destrói textura.
    MockBackend backend;
    TransientTexturePool pool;
    FrameGraph graph;
    u32 createdAfterWarmup = 0;
    for (u32 frame = 1; frame <= 10; ++frame) {
        graph.reset();
        const FGTexture a = graph.create_texture("a", rt(128, 72));
        const FGTexture b = graph.create_texture("b", rt(64, 36));
        const FGTexture c = graph.create_texture("c", rt(128, 72));
        (void)graph.add_raster_pass("p1", PassStage::Effects, a, LoadOp::Clear, {}, [](PassContext&) {});
        const u32 p2 = graph.add_raster_pass("p2", PassStage::Effects, b, LoadOp::DontCare, {}, [](PassContext&) {});
        graph.read(p2, a);
        const u32 p3 = graph.add_raster_pass("p3", PassStage::Effects, c, LoadOp::DontCare, {}, [](PassContext&) {});
        graph.read(p3, b);
        graph.set_output(c, ResourceState::ShaderRead);
        FrameBegin fb;
        (void)backend.begin_frame(fb);
        pool.begin_frame(backend, frame);
        AUREA_CHECK(graph.compile(pool).ok());
        graph.execute(*fb.commands, false);
        graph.release(pool);
        pool.end_frame();
        (void)backend.end_frame();
        if (frame == 1) createdAfterWarmup = backend.texturesCreated;
        if (frame > 1) AUREA_CHECK_EQ(pool.stats().createdThisFrame, static_cast<u32>(0));
    }
    AUREA_CHECK_EQ(backend.texturesCreated, createdAfterWarmup);
    AUREA_CHECK_EQ(backend.texturesDestroyed, static_cast<u32>(0));
}

AUREA_TEST(FrameGraph, PoolDestroysTexturesLeftIdle) {
    MockBackend backend;
    TransientTexturePool pool(5);
    pool.begin_frame(backend, 1);
    const TextureHandle t = pool.acquire(rt());
    pool.release(t);
    pool.end_frame();
    for (u64 f = 2; f < 10; ++f) {
        pool.begin_frame(backend, f);
        pool.end_frame();
    }
    AUREA_CHECK_EQ(backend.texturesDestroyed, static_cast<u32>(1));
    AUREA_CHECK_EQ(pool.stats().alive, static_cast<u32>(0));
}

AUREA_TEST(FrameGraph, PoolBudgetRetiresOldResolutionsWithoutBreakingCurrentPasses) {
    MockBackend backend;
    TransientTexturePool pool;
    const auto large = rt(256, 256);
    const auto small = rt(64, 64);
    pool.set_budget(large.estimated_bytes());
    pool.begin_frame(backend, 1);
    const auto old = pool.acquire(large);
    pool.release(old); pool.end_frame();
    pool.begin_frame(backend, 2);
    const auto current = pool.acquire(small);
    AUREA_CHECK(current.valid());
    AUREA_CHECK_EQ(backend.texturesDestroyed, 1u);
    AUREA_CHECK_EQ(pool.stats().bytes, small.estimated_bytes());
    pool.release(current);
    // A previous pass may still reference current after its graph lifetime
    // ended. Budget pressure must not destroy it before GPU submission.
    const auto oversized = pool.acquire(large);
    AUREA_CHECK(oversized.valid());
    AUREA_CHECK_EQ(backend.texturesDestroyed, 1u);
    AUREA_CHECK_EQ(pool.acquire(small), current);
    pool.release(current); pool.release(oversized); pool.end_frame();
    const u32 created = backend.texturesCreated;
    pool.begin_frame(backend, 3);
    AUREA_CHECK_EQ(pool.acquire(small), current);
    AUREA_CHECK_EQ(pool.acquire(large), oversized);
    AUREA_CHECK_EQ(backend.texturesCreated, created);
    pool.release(current); pool.release(oversized); pool.end_frame();
    // A third resolution evicts idle old targets immediately, not 120 frames later.
    pool.begin_frame(backend, 4);
    const auto third = pool.acquire(rt(128, 128));
    AUREA_CHECK(third.valid());
    AUREA_CHECK(pool.stats().bytes <= large.estimated_bytes());
    pool.release(third); pool.end_frame(); pool.clear();
}

AUREA_TEST(Beta008Memory, EffectGraphAdmissionFailureReleasesTargetsAndCanRecover) {
    MockBackend backend; TransientTexturePool pool; FrameGraph graph;
    const auto size = rt(256, 256); pool.set_allocation_limit(size.estimated_bytes());
    pool.begin_frame(backend, 1);
    const auto input = graph.create_texture("source", size), tinted = graph.create_texture("tint", size);
    graph.add_raster_pass("source", PassStage::Upload, input, LoadOp::Clear, {}, [](PassContext&) {});
    const auto pass = graph.add_raster_pass("tint", PassStage::Effects, tinted, LoadOp::Clear, {}, [](PassContext&) {});
    graph.read(pass, input); graph.set_output(tinted, ResourceState::ShaderRead);
    AUREA_CHECK(graph.compile(pool).code() == Errc::OutOfDeviceMemory);
    AUREA_CHECK_EQ(backend.texturesCreated, 1u);
    AUREA_CHECK(pool.stats().bytes <= size.estimated_bytes());
    graph.release(pool); pool.end_frame(); AUREA_CHECK_EQ(pool.stats().inUse, 0u);
    // A reduced frame can reuse retained targets after a failed effect frame.
    pool.set_budget(0); pool.begin_frame(backend, 2);
    const auto smaller = pool.acquire(rt(64, 64)); AUREA_CHECK(smaller.valid());
    pool.release(smaller); pool.end_frame(); pool.clear();
}

#include "TrackedResourceAdmissionTests.inl"

// =============================================================================
// EffectGraph — planejamento
// =============================================================================
namespace {

struct FakeResources final : EffectResources {
    TextureHandle curve_lut(const CurveData&) noexcept override { return TextureHandle{4242}; }
};

EffectInstance make_effect(const EffectRegistry& reg, const char* key, u32 id) {
    EffectInstance e;
    e.id = id;
    e.type = effect_type_id(key);
    initialize_instance(e, *reg.params(e.type));
    return e;
}

LayerPlacement placement(u32 w = 1920, u32 h = 1080) {
    LayerPlacement p;
    p.compWidth = w;
    p.compHeight = h;
    p.layerWidth = w;
    p.layerHeight = h;
    return p;
}

} // namespace

AUREA_TEST(EffectGraph, ConsecutivePerPixelEffectsFuseIntoOnePass) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kExposure, 0));
    l.effects.back().params[0].constant.v[0] = 1.0f;
    l.effects.push_back(make_effect(reg, effect_keys::kBrightnessContrast, 1));
    l.effects.back().params[1].constant.v[0] = 30.0f;
    l.effects.push_back(make_effect(reg, effect_keys::kSaturation, 2));
    l.effects.back().params[0].constant.v[0] = 20.0f;
    l.effects.push_back(make_effect(reg, effect_keys::kTint, 3));

    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(1));
    AUREA_CHECK(plan.stages[0].kind == EffectStage::Kind::FusedColor);
    AUREA_CHECK_EQ(plan.stages[0].count, static_cast<u32>(4));
    AUREA_CHECK_EQ(plan.fusedEffects, static_cast<u32>(4));
}

AUREA_TEST(EffectGraph, NeutralEffectsCostNothing) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kGaussianBlur, 0));   // raio 0
    l.effects.push_back(make_effect(reg, effect_keys::kLevels, 1));         // padrões
    l.effects.push_back(make_effect(reg, effect_keys::kExposure, 2));       // 0 stops
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK(plan.empty());
    AUREA_CHECK_EQ(plan.droppedIdentity, static_cast<u32>(3));
}

AUREA_TEST(EffectGraph, NeighborhoodBreaksTheFusionRun) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kExposure, 0));
    l.effects.back().params[0].constant.v[0] = 0.5f;
    l.effects.push_back(make_effect(reg, effect_keys::kGaussianBlur, 1));
    l.effects.back().params[0].constant.v[0] = 12.0f;
    l.effects.push_back(make_effect(reg, effect_keys::kSaturation, 2));
    l.effects.back().params[0].constant.v[0] = -50.0f;
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(3));
    AUREA_CHECK(plan.stages[0].kind == EffectStage::Kind::FusedColor);
    AUREA_CHECK(plan.stages[1].kind == EffectStage::Kind::Single);
    AUREA_CHECK(plan.stages[2].kind == EffectStage::Kind::FusedColor);
    // O passe de cor ANTES do blur precisa deixar a margem que o blur lê.
    AUREA_CHECK_NEAR(plan.stages[0].margin, 12.0f, 1e-4);
    AUREA_CHECK_NEAR(plan.stages[2].margin, 0.0f, 1e-4);
}

AUREA_TEST(EffectGraph, FusedPassHasAnOperationLimit) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    for (u32 i = 0; i < 14; ++i) {
        l.effects.push_back(make_effect(reg, effect_keys::kExposure, i));
        l.effects.back().params[0].constant.v[0] = 0.1f;
    }
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(plan.stages[0].count, kMaxFusedColorOps);
    AUREA_CHECK_EQ(plan.stages[1].count, static_cast<u32>(2));
    AUREA_CHECK(!plan.blockers.empty());
}

AUREA_TEST(EffectGraph, OneCurveLutPerFusedPass) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    FakeResources res;
    Layer l;
    for (u32 i = 0; i < 2; ++i) {
        l.effects.push_back(make_effect(reg, effect_keys::kCurves, i));
        l.effects.back().curves[0].channel[0] = {{0, 0}, {0.5f, 0.6f}, {1, 1}};
    }
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), &res, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(plan.colorOps[0].lut.id, static_cast<u64>(4242));
}

AUREA_TEST(EffectGraph, TransformAtTheEndIsFoldedIntoTheComposite) {
    // Transform no fim da pilha não cria textura: vira matriz da composição.
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kExposure, 0));
    l.effects.back().params[0].constant.v[0] = 1.0f;
    l.effects.push_back(make_effect(reg, effect_keys::kTransform, 1));
    l.effects.back().params[1].constant = ParamValue::vec2(0.75f, 0.5f);   // posição deslocada
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(100, 100), nullptr, plan);
    AUREA_CHECK(plan.hasFold);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(1));   // só a exposição
    AUREA_CHECK_NEAR(plan.foldMatrix.col[3].x, 25.0f, 1e-4);     // (0.75-0.5)*100
}

AUREA_TEST(EffectGraph, TransformInTheMiddleNeedsItsOwnPass) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kTransform, 0));
    l.effects.back().params[3].constant.v[0] = 30.0f;   // rotação
    l.effects.push_back(make_effect(reg, effect_keys::kGaussianBlur, 1));
    l.effects.back().params[0].constant.v[0] = 5.0f;
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK(!plan.hasFold);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(2));
}

AUREA_TEST(EffectGraph, KeyframesAreEvaluatedAtTheLayerTime) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kExposure, 7));
    Track& t = l.tracks.get_or_create(TrackProperty::EffectParam, 7, param_track_key(0, 0));
    (void)t.set(FrameIndex{0}, 0.0f);
    (void)t.set(FrameIndex{10}, 2.0f);
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{5}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK_EQ(plan.evals.size(), static_cast<usize>(1));
    AUREA_CHECK_NEAR(plan.colorOps[0].p[1], 1.0f, 1e-5);
    // No frame 0 o valor é 0 → neutro → sai da cadeia.
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK(plan.empty());
}

AUREA_TEST(EffectGraph, KeyframeKeyIsTheEffectIdNotItsPosition) {
    // Reordenar efeitos não pode fazer a animação de um passar para outro.
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kSaturation, 3));
    l.effects.push_back(make_effect(reg, effect_keys::kExposure, 9));
    Track& t = l.tracks.get_or_create(TrackProperty::EffectParam, 9, param_track_key(0, 0));
    (void)t.set(FrameIndex{0}, 1.5f);
    std::swap(l.effects[0], l.effects[1]);   // a exposição vai para o índice 0
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK_EQ(plan.evals.size(), static_cast<usize>(1));
    AUREA_CHECK(plan.colorOps[0].code == ColorOpCode::Exposure);
    AUREA_CHECK_NEAR(plan.colorOps[0].p[1], 1.5f, 1e-5);
}

AUREA_TEST(EffectGraph, ExpressionFallsBackToConstantAndSaysSo) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    EffectInstance e = make_effect(reg, effect_keys::kExposure, 0);
    e.params[0].constant.v[0] = 0.25f;
    e.params[0].source = ParamSource::Expression;
    e.params[0].expression = 1;
    TrackSet tracks;
    bool fallback = false;
    const ParamValue v = evaluate_param(tracks, e, 0, reg.params(e.type)->at(0), FrameIndex{0}, &fallback);
    AUREA_CHECK(fallback);
    AUREA_CHECK_NEAR(v.v[0], 0.25f, 1e-6);
}

// Faixa do SLIDER x faixa DIGITADA (edição extrema): toda declaração do
// catálogo tem hardMin <= min <= max <= hardMax, e o que não é número
// contínuo (contagem, semente, amostras, enum, bool) não alarga.
AUREA_TEST(EffectParams, TypedRangeAlwaysContainsTheSliderRange) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    AUREA_CHECK(reg.count() > 0);
    u32 checked = 0, widened = 0;
    for (u32 i = 0; i < reg.count(); ++i) {
        const ParameterRegistry& params = reg.params_at(i);
        for (u32 p = 0; p < params.count(); ++p) {
            const ParamSpec& s = params.at(p);
            // O registro grava a faixa digitada EXPLÍCITA (nunca NaN).
            AUREA_CHECK(!std::isnan(s.hardMin) && !std::isnan(s.hardMax));
            AUREA_CHECK_MSG(s.hardMin <= s.minValue && s.minValue <= s.maxValue && s.maxValue <= s.hardMax,
                            s.id);
            AUREA_CHECK_EQ(s.typed_min(), s.hardMin);
            AUREA_CHECK_EQ(s.typed_max(), s.hardMax);
            const bool wide = s.hardMin < s.minValue || s.hardMax > s.maxValue;
            if (s.type != ParamType::Float && s.type != ParamType::Angle && s.type != ParamType::Point2D
                && s.type != ParamType::Point3D) {
                AUREA_CHECK_MSG(!wide, s.id);
            }
            if (wide) ++widened;
            ++checked;
        }
    }
    AUREA_CHECK(checked > 100);
    AUREA_CHECK(widened >= 20);
}

AUREA_TEST(EffectParams, CatalogueEnumsHaveKeyframesAndHoldTheirChoicesUntilTheNextKey) {
    EffectRegistry registry;
    register_builtin_effects(registry);
    u32 checked = 0;
    for (u32 i = 0; i < registry.count(); ++i) {
        const ParameterRegistry& params = registry.params_at(i);
        EffectInstance effect;
        effect.id = 7;
        initialize_instance(effect, params);
        for (u32 p = 0; p < params.count(); ++p) {
            const ParamSpec& spec = params.at(p);
            if (spec.type != ParamType::Enum || (spec.flags & kParamHidden) != 0) continue;
            AUREA_CHECK_MSG(spec.animatable(), spec.id);
            if (spec.enumCount < 2) continue;
            TrackSet tracks;
            Track& track = tracks.get_or_create(TrackProperty::EffectParam, effect.id, param_track_key(p, 0));
            const f32 last = static_cast<f32>(spec.enumCount - 1);
            (void)track.set(FrameIndex{0}, 0, Interpolation::Linear);
            (void)track.set(FrameIndex{30}, last, Interpolation::Linear);
            AUREA_CHECK_EQ(evaluate_param_f(tracks, effect, p, spec, -1).v[0], 0.0f);
            AUREA_CHECK_EQ(evaluate_param_f(tracks, effect, p, spec, 15).v[0], 0.0f);
            AUREA_CHECK_EQ(evaluate_param_f(tracks, effect, p, spec, 29.9).v[0], 0.0f);
            AUREA_CHECK_EQ(evaluate_param_f(tracks, effect, p, spec, 30).v[0], last);
            AUREA_CHECK_EQ(evaluate_param_f(tracks, effect, p, spec, 60).v[0], last);
            ++checked;
        }
    }
    AUREA_CHECK(checked > 20);
    std::printf("(%u discrete catalogue parameters) ", checked);
}

AUREA_TEST(EffectParams, TypedRangeApiOnlyWidensContinuousNumbers) {
    ParameterRegistry p;
    const u32 f = p.add_float("r", "R", 10.0f, 0.0f, 100.0f);
    const u32 n = p.add_int("n", "N", 4, 1, 16);
    const u32 b = p.add_bool("b", "B", false);
    // Padrão explícito: a faixa digitada nasce igual à do slider.
    AUREA_CHECK_EQ(p.at(f).hardMin, 0.0f);
    AUREA_CHECK_EQ(p.at(f).hardMax, 100.0f);
    AUREA_CHECK_EQ(p.set_typed_range(f, 50.0f, 1000.0f), f);   // não estreita o mínimo
    AUREA_CHECK_EQ(p.at(f).hardMin, 0.0f);
    AUREA_CHECK_EQ(p.at(f).hardMax, 1000.0f);
    (void)p.set_typed_range(f, -std::numeric_limits<f32>::infinity(), std::nanf(""));   // não finito: ignorado
    AUREA_CHECK_EQ(p.at(f).hardMin, 0.0f);
    AUREA_CHECK_EQ(p.at(f).hardMax, 1000.0f);
    (void)p.set_typed_range(n, -100.0f, 100.0f);   // contagem: fica
    AUREA_CHECK_EQ(p.at(n).hardMin, 1.0f);
    AUREA_CHECK_EQ(p.at(n).hardMax, 16.0f);
    (void)p.typed_range(-5.0f, 5.0f);              // o último é o bool: fica
    AUREA_CHECK_EQ(p.at(b).hardMin, 0.0f);
    AUREA_CHECK_EQ(p.at(b).hardMax, 1.0f);
    AUREA_CHECK_EQ(p.set_typed_range(99, 0.0f, 1.0f), kInvalidIndex);
    // Spec montada à mão, fora do registro: NaN = a faixa do slider.
    ParamSpec loose;
    loose.minValue = -2.0f; loose.maxValue = 3.0f;
    AUREA_CHECK_EQ(loose.typed_min(), -2.0f);
    AUREA_CHECK_EQ(loose.typed_max(), 3.0f);
}

AUREA_TEST(EffectParams, ValueBeyondTheSliderReachesTheEffectUntilTheTypedLimit) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    EffectInstance e = make_effect(reg, effect_keys::kGaussianBlur, 0);
    const ParamSpec& spec = reg.params(e.type)->at(0);   // "blurriness": slider 0..500
    AUREA_CHECK_EQ(spec.maxValue, 500.0f);
    AUREA_CHECK(spec.hardMax > 1200.0f);
    TrackSet tracks;
    // Além do slider, dentro da faixa digitada: chega como foi digitado.
    e.params[0].constant.v[0] = 1200.0f;
    AUREA_CHECK_NEAR(evaluate_param(tracks, e, 0, spec, FrameIndex{0}).v[0], 1200.0f, 1e-4);
    // Além da faixa digitada: preso nela (não no slider).
    e.params[0].constant.v[0] = 1e7f;
    AUREA_CHECK_NEAR(evaluate_param(tracks, e, 0, spec, FrameIndex{0}).v[0], spec.hardMax, 1e-4);
    e.params[0].constant.v[0] = -50.0f;
    AUREA_CHECK_NEAR(evaluate_param(tracks, e, 0, spec, FrameIndex{0}).v[0], spec.hardMin, 1e-6);
    // NaN/inf continuam virando o padrão.
    e.params[0].constant.v[0] = std::numeric_limits<f32>::infinity();
    AUREA_CHECK_NEAR(evaluate_param(tracks, e, 0, spec, FrameIndex{0}).v[0], spec.defaultValue.v[0], 1e-6);
    // Keyframe extremo passa pelo mesmo contrato.
    Track& t = tracks.get_or_create(TrackProperty::EffectParam, e.id, param_track_key(0, 0));
    (void)t.set(FrameIndex{0}, 2500.0f);
    AUREA_CHECK_NEAR(evaluate_param(tracks, e, 0, spec, FrameIndex{0}).v[0], 2500.0f, 1e-3);

    // Contagem (Int) não alarga: além do slider, preso no slider.
    EffectInstance echo = make_effect(reg, effect_keys::kEchoTrail, 1);
    const ParamSpec& copies = reg.params(echo.type)->at(0);   // "copies": 0..16
    AUREA_CHECK(copies.type == ParamType::Int);
    echo.params[0].constant.v[0] = 400.0f;
    AUREA_CHECK_NEAR(evaluate_param(tracks, echo, 0, copies, FrameIndex{0}).v[0], copies.maxValue, 1e-6);
}

AUREA_TEST(EffectGraph, UnknownAndDisabledEffectsAreDropped) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    EffectInstance unknown;
    unknown.id = 0;
    unknown.type = effect_type_id("aurea.inexistente");
    l.effects.push_back(unknown);
    l.effects.push_back(make_effect(reg, effect_keys::kExposure, 1));
    l.effects.back().params[0].constant.v[0] = 1.0f;
    l.effects.back().enabled = false;
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK(plan.empty());
    AUREA_CHECK_EQ(plan.droppedUnknown, static_cast<u32>(1));
    AUREA_CHECK(!plan.blockers.empty());
}

AUREA_TEST(EffectGraph, RegistryRefusesDuplicateKeys) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const u32 before = reg.count();
    register_builtin_effects(reg);
    AUREA_CHECK_EQ(reg.count(), before);
    // 12 efeitos + chaves de luma e croma (7E) + 5 controles de expressão (7G)
    // + os 27 do pacote da Fase 7.3:
    //   estilizar 6 (inverter, varredura, grão, meio-tom, minimax, máscara de nitidez)
    //   distorcer 5 (tremor, turbulência, onda, lente, ondulação que dissolve)
    //   luz e cor 5 (brilho profundo, raios, faixa de luz, desfoque de lente, colorama)
    //   glitch e dano 9 (glitchify, VHS, VHS fita, sinal, cruz, holomatrix, filme, JPEG, ordenar pixels)
    //   tempo 2 (posterizar tempo, RGB no tempo)
    // + remapear tempo (9.3): o remapeamento da camada com o nome e a cara do
    //   After Effects, no navegador de efeitos ao lado do Posterizar tempo.
    // Three transitions, three optical effects and two spatial blurs.
    // Hotspots and three audio sends.
    // Stripes, Radial Rays, Grid, Parenting Helper and Text 3D Layout.
    // Corner Pin and the five Media Lab effects.
    // RGB Split and Chromatic Aberration (independent spatial channel effects).
    // + o pacote de paridade (13): Oscilar, Balançar, Agitar; Íris, Caixa,
    //   Persianas; Desfoque radial, Espelho, Cortar bordas, Vinheta,
    //   Mosaico/LED, Detectar bordas, Matiz e saturação.
    // + geradores e recorte do editor antigo (6): Ruído fractal, Degradê,
    //   Degradê de 4 cores, Espectro de áudio; Contorno da silhueta,
    //   Refinar recorte.
    // + o VHS de Estilizar (1): look de fita com OSD do videocassete.
    // + o pacote do editor antigo (8): Preencher, Equilíbrio de cor (HLS),
    //   Desfoque de zoom, Bojo, Xadrez, Matriz hexagonal; Sombra projetada,
    //   Borda.
    // + o Mapa de profundidade (IA) (1).
    // + o pacote de áudio (12): Reverso, Atraso, Flange e chorus, Passa-alta/
    //   baixa, Mixer estéreo, Modulador, EQ paramétrico, Reverb, Tom; Forma de
    //   onda de áudio, Espectro de áudio (bandas) e Bolas.
    // + o Tremor em trancos do app antigo (1).
    // + o Particular (as partículas do app antigo) (1).
    // + o Shape 3D Layout (o layout por parte das formas 3D) (1).
    // + Text Transform e Oscillate por ciclos (2).
    // + Detectar movimento (1).
    // + o que entrou junto no mesmo lote (Rotobrush, layout em grade e os
    //   demais efeitos novos da árvore de trabalho): total medido 130.
    // + Emulador CRT, Tremor dissolvente e Mapa de deslocamento (3).
    // + Datamosh (1).
    // The catalog grows independently. This case checks idempotent registration;
    // individual effect contracts are covered by the EffectPack cases.
    AUREA_CHECK(before > 0);
}

AUREA_TEST(EffectGraph, DatamoshDeclaresItsParameters) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const EffectTypeId id = reg.find_key(effect_keys::kDatamosh);
    AUREA_CHECK(id != 0);
    const Effect* fx = reg.find(id);
    const ParameterRegistry* params = reg.params(id);
    AUREA_CHECK(fx && params);
    if (!fx || !params) return;
    AUREA_CHECK_EQ(params->count(), 7u);
    // O renderer lê os quadros segurados pelo id: o índice não pode sumir.
    AUREA_CHECK_EQ(params->find("amount"), 0u);
    AUREA_CHECK_EQ(params->find("block_size"), 1u);
    AUREA_CHECK_EQ(params->find("hold_frames"), 2u);
    AUREA_CHECK_EQ(params->find("drag"), 3u);
    AUREA_CHECK_EQ(params->find("corruption"), 4u);
    AUREA_CHECK_EQ(params->find("color_bleed"), 5u);
    AUREA_CHECK_EQ(params->find("seed"), 6u);
    AUREA_CHECK(params->at(2).type == ParamType::Int);
    AUREA_CHECK_NEAR(params->at(2).maxValue, static_cast<f32>(kDatamoshMaxHold), 1e-6);
    AUREA_CHECK(fx->wants_history());
    AUREA_CHECK(std::string(fx->info().category) == "Glitch");
    // Intensidade 0 sai da cadeia; o padrão não.
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kDatamosh, 1));
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK(!plan.empty());
    l.effects.back().params[0].constant.v[0] = 0.0f;
    EffectPlan off;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, off);
    AUREA_CHECK(off.empty());
}

AUREA_TEST(EffectGraph, CurveIsMonotoneBetweenPoints) {
    // Spline monotônica: entre dois pontos crescentes, nunca passa deles.
    CurveData c = CurveData::identity();
    c.channel[0] = {{0.0f, 0.0f}, {0.3f, 0.1f}, {0.6f, 0.9f}, {1.0f, 1.0f}};
    f32 prev = -1.0f;
    for (u32 i = 0; i <= 100; ++i) {
        const f32 x = static_cast<f32>(i) / 100.0f;
        const f32 y = c.evaluate(0, x);
        AUREA_CHECK(y >= prev - 1e-6f);
        AUREA_CHECK(y >= -1e-6f && y <= 1.0f + 1e-6f);
        prev = y;
    }
    AUREA_CHECK_NEAR(c.evaluate(0, 0.3f), 0.1f, 1e-5);
    AUREA_CHECK_NEAR(c.evaluate(0, 0.6f), 0.9f, 1e-5);
}

// =============================================================================
// Motion Tile — geometria (casos portados de test/motion_tile_test.dart)
// =============================================================================
namespace {

LayerPlacement tile_placement(f32 compW, f32 compH, f32 layerW, f32 layerH, f32 posX, f32 posY,
                              f32 scale, f32 rotDeg = 0.0f) {
    LayerPlacement p;
    p.compWidth = static_cast<u32>(compW);
    p.compHeight = static_cast<u32>(compH);
    p.layerWidth = static_cast<u32>(layerW);
    p.layerHeight = static_cast<u32>(layerH);
    p.compFromLayer = Mat4::translation(Vec3{posX, posY, 0})
                    * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, rotDeg * kDeg2Rad))
                    * Mat4::scale(Vec3{scale, scale, 1})
                    * Mat4::translation(Vec3{-layerW * 0.5f, -layerH * 0.5f, 0});
    return p;
}

} // namespace

AUREA_TEST(MotionTile, At100PercentTheRegionIsTheLayer) {
    motion_tile::Params p;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 1.0f));
    AUREA_CHECK_NEAR(f.x, 1.0f, 1e-4);
    AUREA_CHECK_NEAR(f.y, 1.0f, 1e-4);
}

AUREA_TEST(MotionTile, ProjectedCoverageUsesHomogeneousCoordinates) {
    LayerPlacement pl;
    pl.compWidth = 200; pl.compHeight = 120;
    pl.layerWidth = 100; pl.layerHeight = 60;
    pl.inScene3d = true;
    pl.compFromLayer = Mat4::identity();
    pl.compFromLayer.col[0] = {2.f, 0, 0, .001f};
    pl.compFromLayer.col[1] = {0, 2.f, 0, .002f};
    pl.compFromLayer.col[3] = {0, 0, 0, 1};
    const Rect visible = motion_tile::projected_region(pl);
    AUREA_CHECK(visible.w > 100.f);
    AUREA_CHECK(visible.h > 60.f);
    for (const f32 cy : {0.f, 120.f}) for (const f32 cx : {0.f, 200.f}) {
        const f64 a = 2. - cx * .001, c = -cx * .002;
        const f64 b = -cy * .001, d = 2. - cy * .002;
        const f64 det = a * d - b * c;
        const f64 x = (d * cx - c * cy) / det;
        const f64 y = (a * cy - b * cx) / det;
        AUREA_CHECK(x >= visible.x - .001 && x <= visible.x + visible.w + .001);
        AUREA_CHECK(y >= visible.y - .001 && y <= visible.y + visible.h + .001);
    }
    pl.compFromLayer.col[3].w = -1;
    AUREA_CHECK_EQ(motion_tile::projected_region(pl).w, 0.f);
    const Vec2 fallback = motion_tile::coverage_factors({}, pl);
    AUREA_CHECK(std::isfinite(fallback.x) && std::isfinite(fallback.y));
}

AUREA_TEST(MotionTile, At50PercentTheRegionDoubles) {
    // O RELATO: camada em 50% deixava moldura vazia. A região tem de dobrar.
    motion_tile::Params p;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 0.5f));
    AUREA_CHECK_NEAR(f.x, 2.0f, 1e-3);
    AUREA_CHECK_NEAR(f.y, 2.0f, 1e-3);
    const Vec2 q = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 0.25f));
    AUREA_CHECK_NEAR(q.x, 4.0f, 1e-3);
}

AUREA_TEST(MotionTile, EnlargedLayerDoesNotShrinkTheRegion) {
    motion_tile::Params p;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 2.0f));
    AUREA_CHECK_NEAR(f.x, 1.0f, 1e-4);
    AUREA_CHECK_NEAR(f.y, 1.0f, 1e-4);
}

AUREA_TEST(MotionTile, OutputWindowNeverChangesTheCoverage) {
    // A saída é uma janela no QUADRO: recorta, não amplia nem encolhe a região.
    motion_tile::Params p;
    for (f32 o : {0.0f, 0.4f, 1.0f, 3.0f}) {
        p.outputX = o;
        p.outputY = o;
        const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 1.0f));
        AUREA_CHECK_NEAR(f.x, 1.0f, 1e-4);
        AUREA_CHECK_NEAR(f.y, 1.0f, 1e-4);
        const Vec2 g = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 0.5f));
        AUREA_CHECK_NEAR(g.x, 2.0f, 1e-3);
    }
}

AUREA_TEST(MotionTile, OutputWindowIsCentredOnTheFrame) {
    motion_tile::Params p;
    AUREA_CHECK(motion_tile::inside_output(p, Vec2{0.0f, 1.0f}));
    AUREA_CHECK(motion_tile::inside_output(p, Vec2{-0.3f, 1.4f}));   // 100%: sem corte (a margem segue)
    p.outputX = 0.5f;
    p.outputY = 0.8f;
    AUREA_CHECK(motion_tile::inside_output(p, Vec2{0.26f, 0.5f}));
    AUREA_CHECK(!motion_tile::inside_output(p, Vec2{0.24f, 0.5f}));
    AUREA_CHECK(motion_tile::inside_output(p, Vec2{0.5f, 0.89f}));
    AUREA_CHECK(!motion_tile::inside_output(p, Vec2{0.5f, 0.91f}));
    p.outputX = 0.0f;
    AUREA_CHECK(!motion_tile::inside_output(p, Vec2{0.49f, 0.5f}));
}

AUREA_TEST(MotionTile, DisplacedLayerAsksForMoreOnTheFarSide) {
    motion_tile::Params p;
    // Camada em x = 25% do quadro: o lado direito está a 75% de distância.
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1000, 1000, 1000, 1000, 250, 500, 1.0f));
    AUREA_CHECK_NEAR(f.x, 1.5f, 1e-3);
    AUREA_CHECK_NEAR(f.y, 1.0f, 1e-3);
}

AUREA_TEST(MotionTile, TileSizeDoesNotChangeTheCoverage) {
    motion_tile::Params p;
    p.tileX = p.tileY = 0.25f;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 0.5f));
    AUREA_CHECK_NEAR(f.x, 2.0f, 1e-3);
}

AUREA_TEST(MotionTile, ZeroScaleNeverBecomesInfinity) {
    motion_tile::Params p;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 0.0f));
    AUREA_CHECK(std::isfinite(f.x) && std::isfinite(f.y));
    AUREA_CHECK_NEAR(f.x, 1.0f, 1e-4);
}

AUREA_TEST(MotionTile, CoverageHasACeiling) {
    motion_tile::Params p;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 0.001f));
    AUREA_CHECK_NEAR(f.x, motion_tile::kMaxCoverage, 1e-3);
}

AUREA_TEST(MotionTile, FortyFiveDegreesOnASquareIsExactlySqrt2) {
    motion_tile::Params p;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1000, 1000, 1000, 1000, 500, 500, 1.0f, 45.0f));
    AUREA_CHECK_NEAR(f.x, std::sqrt(2.0f), 1e-3);
    AUREA_CHECK_NEAR(f.y, std::sqrt(2.0f), 1e-3);
    const Vec2 g = motion_tile::coverage_factors(p, tile_placement(1000, 1000, 1000, 1000, 500, 500, 1.0f, 90.0f));
    AUREA_CHECK_NEAR(g.x, 1.0f, 1e-3);
    const Vec2 h = motion_tile::coverage_factors(p, tile_placement(1000, 1000, 1000, 1000, 500, 500, 1.0f, 180.0f));
    AUREA_CHECK_NEAR(h.x, 1.0f, 1e-3);
}

AUREA_TEST(MotionTile, AnchorOffCenterStillCoversFromTheLayerCenter) {
    // Generalização do porte: com âncora fora do centro, a conta inverte a
    // matriz inteira — e a região continua centrada no CENTRO da layer.
    motion_tile::Params p;
    LayerPlacement pl;
    pl.compWidth = pl.compHeight = 1000;
    pl.layerWidth = pl.layerHeight = 1000;
    // Âncora no canto (0,0), posição no centro do quadro: o centro da layer
    // fica em (1000,1000) — só o quadrante superior esquerdo aparece.
    pl.compFromLayer = Mat4::translation(Vec3{500, 500, 0});
    const Vec2 f = motion_tile::coverage_factors(p, pl);
    // Canto (0,0) do quadro está a (-500,-500)-(500,500)... = 1000 do centro.
    AUREA_CHECK_NEAR(f.x, 2.0f, 1e-3);
    const Rect r = motion_tile::tiled_region(p, pl);
    AUREA_CHECK_NEAR(r.x + r.w * 0.5f, 500.0f, 1e-3);
    AUREA_CHECK_NEAR(r.y + r.h * 0.5f, 500.0f, 1e-3);
}

AUREA_TEST(MotionTile, CentralCopyStaysTheLayerAndCornersAreCovered) {
    // A região contém a caixa da layer (a cópia central não muda de lugar nem
    // de tamanho) e os QUATRO CANTOS do quadro caem dentro dela depois do
    // transform — conferido pela ida (matriz direta + produto vetorial), não
    // pela volta que a própria função usa.
    motion_tile::Params p;
    u32 checked = 0;
    for (f32 rot : {0.0f, 17.0f, 45.0f, 90.0f, 133.0f, -60.0f}) {
        for (f32 scale : {0.2f, 0.5f, 0.9f, 1.0f, 1.7f}) {
            for (f32 px : {0.0f, 300.0f, 960.0f, 1920.0f}) {
                const LayerPlacement pl = tile_placement(1920, 1080, 800, 600, px, 540.0f, scale, rot);
                const Rect r = motion_tile::tiled_region(p, pl);
                AUREA_CHECK(r.x <= 1e-3f && r.y <= 1e-3f);
                AUREA_CHECK(r.x + r.w >= 800.0f - 1e-3f && r.y + r.h >= 600.0f - 1e-3f);
                AUREA_CHECK_NEAR(r.x + r.w * 0.5f, 400.0f, 1e-2);
                if (r.w >= 800.0f * motion_tile::kMaxCoverage - 1.0f || r.h >= 600.0f * motion_tile::kMaxCoverage - 1.0f) {
                    continue;   // no teto de cobertura a garantia não vale (por projeto)
                }
                // O canto mais distante fica EXATAMENTE na borda da região (é
                // ele que define o fator): meio pixel de folga absorve o erro
                // de ponto flutuante do teste, não da conta.
                const Rect rr{r.x - 0.5f, r.y - 0.5f, r.w + 1.0f, r.h + 1.0f};
                const Mat4& m = pl.compFromLayer;
                auto fwd = [&](f32 x, f32 y) {
                    return Vec2{m.col[0].x * x + m.col[1].x * y + m.col[3].x,
                                m.col[0].y * x + m.col[1].y * y + m.col[3].y};
                };
                const Vec2 q[4] = {fwd(rr.x, rr.y), fwd(rr.x + rr.w, rr.y), fwd(rr.x + rr.w, rr.y + rr.h),
                                   fwd(rr.x, rr.y + rr.h)};
                for (Vec2 c : {Vec2{0, 0}, Vec2{1920, 0}, Vec2{0, 1080}, Vec2{1920, 1080}}) {
                    bool pos = false, neg = false;
                    for (int i = 0; i < 4; ++i) {
                        const Vec2 a = q[i], b = q[(i + 1) % 4];
                        const f32 cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                        if (cross > 1e-2f) pos = true;
                        if (cross < -1e-2f) neg = true;
                    }
                    AUREA_CHECK(!(pos && neg));
                    ++checked;
                }
            }
        }
    }
    AUREA_CHECK(checked > 100);
}

AUREA_TEST(MotionTile, NeverNaNOrInfinity) {
    motion_tile::Params p;
    u32 n = 0;
    for (f32 s : {0.0f, 1e-6f, 0.3f, 1.0f, 5.0f, -1.0f}) {
        for (f32 rot : {0.0f, 30.0f, 89.999f, 270.0f}) {
            for (f32 x : {-5000.0f, 0.0f, 960.0f, 1e6f}) {
                for (f32 ox : {0.01f, 1.0f, 6.0f}) {
                    p.outputX = ox;
                    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 640, 360, x, 540, s, rot));
                    AUREA_CHECK(std::isfinite(f.x) && std::isfinite(f.y));
                    AUREA_CHECK(f.x >= 0.01f && f.x <= motion_tile::kMaxCoverage);
                    ++n;
                }
            }
        }
    }
    AUREA_CHECK_EQ(n, static_cast<u32>(288));
}

AUREA_TEST(MotionTile, GridRepeatsAtTheTilePeriod) {
    motion_tile::Params p;
    p.tileX = p.tileY = 0.5f;
    for (u32 i = 0; i < 50; ++i) {
        const f32 x = 0.005f + 0.01f * static_cast<f32>(i);
        const Vec2 a = motion_tile::reference_lookup(p, Vec2{x, 0.2f});
        const Vec2 b = motion_tile::reference_lookup(p, Vec2{x + 0.5f, 0.2f});
        AUREA_CHECK_NEAR(a.x, b.x, 1e-5);
    }
}

AUREA_TEST(MotionTile, IdentityParamsShowTheSourceUntouched) {
    motion_tile::Params p;
    AUREA_CHECK(p.identity_params());
    for (u32 i = 1; i < 20; ++i) {
        const f32 v = static_cast<f32>(i) / 20.0f;
        const Vec2 f = motion_tile::reference_lookup(p, Vec2{v, 1.0f - v});
        AUREA_CHECK_NEAR(f.x, v, 1e-5);
        AUREA_CHECK_NEAR(f.y, 1.0f - v, 1e-5);
    }
}

AUREA_TEST(MotionTile, MirrorFlipsTheNeighbor) {
    motion_tile::Params p;
    p.tileX = p.tileY = 0.5f;
    p.mirror = true;
    const Vec2 a = motion_tile::reference_lookup(p, Vec2{0.3f, 0.3f});
    const Vec2 b = motion_tile::reference_lookup(p, Vec2{0.8f, 0.3f});
    AUREA_CHECK_NEAR(b.x, 1.0f - a.x, 1e-5);
}

AUREA_TEST(MotionTile, PhaseShiftsEachRowByThePhaseTimesItsIndex) {
    // A fase do app antigo: a linha n anda n × fase em X (180° = tijolo; 90° =
    // escada de quarto em quarto). Com a opção horizontal, a coluna n anda em Y.
    motion_tile::Params p;
    p.tileX = p.tileY = 0.5f;
    p.phaseTurns = 0.25f;   // 90°
    const Vec2 row0 = motion_tile::reference_lookup(p, Vec2{0.30f, 0.40f});
    const Vec2 row1 = motion_tile::reference_lookup(p, Vec2{0.30f, 0.90f});
    const Vec2 row2 = motion_tile::reference_lookup(p, Vec2{0.30f, 1.40f});
    AUREA_CHECK_NEAR(row1.x - row0.x, 0.25f, 1e-5);
    AUREA_CHECK_NEAR(row2.x - row0.x, 0.50f, 1e-5);
    AUREA_CHECK_NEAR(row1.y, row0.y, 1e-5);
    p.horizontalPhase = true;
    const Vec2 col0 = motion_tile::reference_lookup(p, Vec2{0.40f, 0.30f});
    const Vec2 col1 = motion_tile::reference_lookup(p, Vec2{0.90f, 0.30f});
    AUREA_CHECK_NEAR(col1.y - col0.y, 0.25f, 1e-5);
    AUREA_CHECK_NEAR(col1.x, col0.x, 1e-5);
}

AUREA_TEST(MotionTile, LegacyLayoutUpgradesOnceWithItsKeyframes) {
    // Motion Tile gravado na disposição anterior (9 slots): saída que só
    // ampliava vira a janela no quadro inteiro; a fase troca de eixo e de
    // sentido (a coluna vizinha fica onde estava); o slot 9 marca a conversão.
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    EffectInstance fx = make_effect(reg, effect_keys::kMotionTile, 7);
    AUREA_CHECK_EQ(fx.params.size(), static_cast<usize>(motion_tile::kScale + 1));
    AUREA_CHECK(!motion_tile::upgrade_legacy_layout(l, fx));          // já é a atual
    fx.params.resize(motion_tile::kLegacyParamCount);
    fx.params[motion_tile::kTileWidth].constant.v[0] = 50.0f;
    fx.params[motion_tile::kOutputWidth].constant.v[0] = 300.0f;
    fx.params[motion_tile::kOutputHeight].constant.v[0] = 40.0f;
    fx.params[motion_tile::kPhase].constant.v[0] = 90.0f;
    l.tracks.get_or_create(TrackProperty::EffectParam, 7, param_track_key(motion_tile::kPhase, 0)).set(FrameIndex{10}, 45.0f);
    l.tracks.get_or_create(TrackProperty::EffectParam, 7, param_track_key(motion_tile::kOutputWidth, 0)).set(FrameIndex{10}, 250.0f);
    AUREA_CHECK(motion_tile::upgrade_legacy_layout(l, fx));
    AUREA_CHECK_EQ(fx.params.size(), static_cast<usize>(motion_tile::kLayout + 1));
    AUREA_CHECK_NEAR(fx.params[motion_tile::kTileWidth].constant.v[0], 50.0f, 1e-6);
    AUREA_CHECK_NEAR(fx.params[motion_tile::kOutputWidth].constant.v[0], 100.0f, 1e-6);
    AUREA_CHECK_NEAR(fx.params[motion_tile::kOutputHeight].constant.v[0], 100.0f, 1e-6);
    AUREA_CHECK_NEAR(fx.params[motion_tile::kPhase].constant.v[0], -90.0f, 1e-6);
    AUREA_CHECK(fx.params[motion_tile::kHorizontalPhase].constant.as_bool());
    const Track* ph = l.tracks.find(TrackProperty::EffectParam, 7, param_track_key(motion_tile::kPhase, 0));
    AUREA_CHECK(ph && ph->keys.size() == 1 && std::fabs(ph->keys[0].value + 45.0f) < 1e-6f);
    AUREA_CHECK(!l.tracks.find(TrackProperty::EffectParam, 7, param_track_key(motion_tile::kOutputWidth, 0)));
    AUREA_CHECK(!motion_tile::upgrade_legacy_layout(l, fx));          // uma vez só

    // A coluna vizinha do tijolo (180°) cai no mesmo ponto nas duas contas.
    motion_tile::Params oldGrid;
    oldGrid.tileX = oldGrid.tileY = 0.5f;
    motion_tile::Params newGrid = oldGrid;
    newGrid.horizontalPhase = true;
    newGrid.phaseTurns = -0.5f;
    for (f32 x : {0.1f, 0.3f, 0.6f, 0.8f}) {
        for (f32 y : {0.05f, 0.35f, 0.65f, 0.95f}) {
            // Conta anterior: colunas ímpares descem meio ladrilho.
            f32 qx = (x - 0.5f) / 0.5f + 0.5f, qy = (y - 0.5f) / 0.5f + 0.5f;
            qy -= std::fmod(std::floor(qx) + 1000.0f, 2.0f) * 0.5f;
            const Vec2 before{qx - std::floor(qx), qy - std::floor(qy)};
            const Vec2 after = motion_tile::reference_lookup(newGrid, Vec2{x, y});
            AUREA_CHECK_NEAR(after.x, before.x, 1e-5);
            AUREA_CHECK_NEAR(after.y, before.y, 1e-5);
        }
    }
}

AUREA_TEST(MotionTile, ClampStretchesTheEdgeAndBeatsMirror) {
    motion_tile::Params p;
    p.tileX = p.tileY = 0.5f;
    p.legacyClamp = true;
    p.mirror = true;
    const Vec2 f = motion_tile::reference_lookup(p, Vec2{0.95f, 0.05f});
    AUREA_CHECK_NEAR(f.x, 1.0f, 1e-5);
    AUREA_CHECK_NEAR(f.y, 0.0f, 1e-5);
}

AUREA_TEST(MotionTile, IdentityOnlyWhenTheLayerAlreadyCoversTheFrame) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kMotionTile, 0));
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f,
                      tile_placement(1920, 1080, 1920, 1080, 960, 540, 1.0f), nullptr, plan);
    AUREA_CHECK(plan.empty());
    // A MESMA configuração com a layer em 50% já não é identidade.
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f,
                      tile_placement(1920, 1080, 1920, 1080, 960, 540, 0.5f), nullptr, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(1));
}

AUREA_TEST(MotionTile, AFullFrameWallIsKeptWhenAnotherEffectNeedsItsPixels) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer layer;
    layer.effects.push_back(make_effect(reg, effect_keys::kMotionTile, 0));
    layer.effects.push_back(make_effect(reg, effect_keys::kGaussianBlur, 1));
    layer.effects.back().params[0].constant.v[0] = 12.0f;
    EffectPlan plan;
    EffectGraph::plan(layer, reg, FrameIndex{0}, 1.0f, placement(160, 90), nullptr, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(2));
    AUREA_CHECK_NEAR(plan.stages.front().margin, 12.0f, 1e-5f);
    // Disabled effects do not prevent the sole neutral tile optimization.
    layer.effects.back().enabled = false;
    EffectGraph::plan(layer, reg, FrameIndex{0}, 1.0f, placement(160, 90), nullptr, plan);
    AUREA_CHECK(plan.empty());
}

AUREA_TEST(MotionTile, ADownstreamBlurMarginUsesTheTransformInputCoordinates) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer layer;
    layer.effects.push_back(make_effect(reg, effect_keys::kMotionTile, 0));
    layer.effects.push_back(make_effect(reg, effect_keys::kTransform, 1));
    layer.effects.back().params[2].constant = ParamValue::vec2(-10.0f, 25.0f);
    layer.effects.back().params[3].constant.v[0] = 30.0f;
    layer.effects.push_back(make_effect(reg, effect_keys::kGaussianBlur, 2));
    layer.effects.back().params[0].constant.v[0] = 24.0f;
    EffectPlan plan;
    EffectGraph::plan(layer, reg, FrameIndex{0}, 1.0f, placement(160, 90), nullptr, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(3));
    const f32 reach = (std::cos(30.0f * kDeg2Rad) + std::sin(30.0f * kDeg2Rad)) / 0.1f;
    AUREA_CHECK_NEAR(plan.stages[0].margin, 24.0f * reach, 1e-3f);
    AUREA_CHECK_NEAR(plan.stages[1].margin, 24.0f, 1e-5f);
    AUREA_CHECK_NEAR(plan.stages[2].margin, 0.0f, 1e-5f);
}

AUREA_TEST(MotionTile, EarlierEffectsKeepPixelsThatTheTileMayReadOffscreen) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer layer;
    layer.effects.push_back(make_effect(reg, effect_keys::kGaussianBlur, 0));
    layer.effects.back().params[0].constant.v[0] = 4.0f;
    layer.effects.push_back(make_effect(reg, effect_keys::kMotionTile, 1));
    layer.effects.back().params[motion_tile::kMirror].constant.v[0] = 1.0f;
    layer.effects.push_back(make_effect(reg, effect_keys::kGlow, 2));
    EffectPlan plan;
    EffectGraph::plan(layer, reg, FrameIndex{0}, 1.0f, placement(160, 90), nullptr, plan);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(3));
    AUREA_CHECK(plan.stages[0].preserveFullExtent);
    AUREA_CHECK(!plan.stages[1].preserveFullExtent);
    AUREA_CHECK(!plan.stages[2].preserveFullExtent);
    // Keeping full input suppresses viewport trimming, not the true canvas
    // size or transforms used by other effects.
    LayerPlacement beforeTile = plan.placement;
    beforeTile.preserveFullExtent = true;
    AUREA_CHECK_EQ(beforeTile.compWidth, 160u);
    AUREA_CHECK_EQ(visible_layer_rect(beforeTile).w, 0.0f);
    AUREA_CHECK_NEAR(visible_layer_rect(plan.placement).w, 160.0f, 1e-5f);
}

// =============================================================================
// ShaderLibrary
// =============================================================================
AUREA_TEST(ShaderLibrary, CreatesEveryEmbeddedShader) {
    MockBackend b;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(b).ok());
    AUREA_CHECK_EQ(b.shadersCreated, kShaderCount);
    AUREA_CHECK(kShaderCount >= 16);
    for (u32 i = 0; i < kShaderCount; ++i) {
        const ShaderBlob& blob = shader_blob(static_cast<ShaderId>(i));
        AUREA_CHECK(blob.bytes > 20 && blob.bytes % 4 == 0);
        AUREA_CHECK_EQ(blob.words[0], 0x07230203u);   // número mágico do SPIR-V
    }
}

AUREA_TEST(ShaderLibrary, SameKeySamePipelineCompiledOnce) {
    MockBackend b;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(b).ok());
    const PipelineKey k = PipelineKey::fullscreen(ShaderId::effects_color_stack_frag, SurfaceFormat::RGBA16F);
    auto p1 = lib.pipeline(k);
    auto p2 = lib.pipeline(k);
    AUREA_CHECK(p1.ok() && p2.ok());
    AUREA_CHECK(*p1 == *p2);
    AUREA_CHECK_EQ(b.pipelinesCreated, static_cast<u32>(1));
    PipelineKey add = PipelineKey::graphics(ShaderId::composite_layer_vert, ShaderId::composite_layer_frag,
                                            SurfaceFormat::RGBA16F, true, BlendMode::Add);
    PipelineKey normal = add;
    normal.blend = BlendMode::Normal;
    AUREA_CHECK(!(*lib.pipeline(add) == *lib.pipeline(normal)));
}

AUREA_TEST(ShaderLibrary, CompilesDuringPlaybackAreCounted) {
    MockBackend b;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(b).ok());
    const PipelineKey k = PipelineKey::fullscreen(ShaderId::effects_sharpen_frag, SurfaceFormat::RGBA16F);
    AUREA_CHECK_EQ(lib.prewarm(&k, 1), static_cast<u32>(1));
    lib.mark_steady_state();
    (void)lib.pipeline(k);   // já em cache: não conta
    AUREA_CHECK_EQ(lib.compiles_since_mark(), static_cast<u32>(0));
    (void)lib.pipeline(PipelineKey::fullscreen(ShaderId::effects_glow_combine_frag, SurfaceFormat::RGBA16F));
    AUREA_CHECK_EQ(lib.compiles_since_mark(), static_cast<u32>(1));
}

AUREA_TEST(ShaderLibrary, FailureIsReportedNotHidden) {
    MockBackend b;
    b.failPipelines = true;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(b).ok());
    auto p = lib.pipeline(PipelineKey::fullscreen(ShaderId::effects_sharpen_frag, SurfaceFormat::RGBA16F));
    AUREA_CHECK(!p.ok());
    AUREA_CHECK_EQ(lib.compile_failures(), static_cast<u32>(1));
    AUREA_CHECK(!lib.last_error().empty());
}

// Beta "nem vejo a prévia": no OpenGL ES (aparelho sem Vulkan) cada shader é
// compilado pelo driver na abertura; UM shader novo recusado derrubava o
// renderer inteiro (sem GPU = sem prévia). Agora só os passes dele somem.
AUREA_TEST(ShaderLibrary, RejectedNewShaderDoesNotTakeDownTheRenderer) {
    const ShaderId newShaders[] = {
        ShaderId::effects_disintegrate_vert, ShaderId::effects_disintegrate_frag,
        ShaderId::effects_disintegrate_compose_frag, ShaderId::effects_glow_octave_prefilter_frag,
        ShaderId::effects_glow_octave_down_frag, ShaderId::effects_glow_octave_up_frag,
        ShaderId::effects_glow_octave_composite_frag, ShaderId::effects_mesh_warp_vert,
        ShaderId::effects_mesh_warp_frag, ShaderId::effects_puppet_vert, ShaderId::effects_puppet_frag,
        ShaderId::effects_ball_grid_vert, ShaderId::effects_ball_grid_frag, ShaderId::effects_rotobrush_frag,
        ShaderId::scene3d_plane_frag, ShaderId::composite_blend_frag,
    };
    for (const ShaderId id : newShaders) {
        MockBackend b;
        b.failShader = kShaderNames[static_cast<u32>(id)];
        ShaderLibrary lib;
        AUREA_CHECK_MSG(lib.initialize(b).ok(), b.failShader);
        AUREA_CHECK_EQ(lib.missing_shaders(), 1u);
        AUREA_CHECK(!lib.shader(id).valid());
        // O pipeline que pede o shader recusado falha sem chegar ao backend…
        const PipelineKey bad = PipelineKey::graphics(
            kShaderStages[static_cast<u32>(id)] == ShaderStage::Vertex ? id : ShaderId::common_fullscreen_vert,
            kShaderStages[static_cast<u32>(id)] == ShaderStage::Vertex ? ShaderId::common_copy_frag : id,
            SurfaceFormat::RGBA16F);
        const u32 created = b.pipelinesCreated;
        AUREA_CHECK(!lib.pipeline(bad).ok());
        AUREA_CHECK(!lib.pipeline(bad).ok());
        AUREA_CHECK_EQ(b.pipelinesCreated, created);
        AUREA_CHECK_EQ(lib.compile_failures(), 2u);   // 1 shader + 1 pipeline: o 2º pedido não reloga
        // …e o resto segue.
        AUREA_CHECK(lib.pipeline(PipelineKey::fullscreen(ShaderId::common_copy_frag, SurfaceFormat::RGBA16F)).ok());
        lib.shutdown();

        // O renderer sobe (antes: renderer nao inicializou → Engine sem GPU).
        MockBackend rb;
        rb.failShader = kShaderNames[static_cast<u32>(id)];
        EffectRegistry reg;
        register_builtin_effects(reg);
        Renderer r;
        AUREA_CHECK_MSG(r.initialize(rb, reg).ok(), rb.failShader);
        AUREA_CHECK_EQ(r.shaders().missing_shaders(), 1u);
        r.shutdown();
    }
}

AUREA_TEST(ShaderLibrary, TestHookFailsOnlyPipelinesOfThatShaderAndRecovers) {
    MockBackend b;
    ShaderLibrary lib;
    AUREA_CHECK(lib.initialize(b).ok());
    const PipelineKey k = PipelineKey::fullscreen(ShaderId::effects_glow_octave_up_frag, SurfaceFormat::RGBA16F);
    AUREA_CHECK(lib.pipeline(k).ok());
    lib.set_test_failing_shader(ShaderId::effects_glow_octave_up_frag);
    AUREA_CHECK(!lib.pipeline(k).ok());   // mesmo em cache
    AUREA_CHECK(lib.pipeline(PipelineKey::fullscreen(ShaderId::common_copy_frag, SurfaceFormat::RGBA16F)).ok());
    lib.set_test_failing_shader(ShaderId::Count);
    AUREA_CHECK(lib.pipeline(k).ok());
}

// =============================================================================
// Compositor (Renderer contra o backend falso)
// =============================================================================
namespace {

struct RenderFixture {
    MockBackend backend;
    EffectRegistry effects;
    Renderer renderer;
    Project project;
    Composition* comp = nullptr;

    RenderFixture() {
        register_builtin_effects(effects);
        (void)renderer.initialize(backend, effects);
        auto p = Project::create_new(1920, 1080, 30.0, "teste");
        project = std::move(*p);
        comp = project.timeline().composition(project.timeline().root());
    }

    LayerId solid(const char* name, Vec4 color, f32 x = 960, f32 y = 540) {
        const LayerId id = comp->add_layer(LayerKind::Shape, name);
        Layer* l = comp->layer(id);
        l->shape.bounds = Rect{0, 0, 400, 300};
        l->shape.fillColor = color;
        l->transform.anchor = Vec3{200, 150, 0};
        l->transform.position = Vec3{x, y, 0};
        return id;
    }

    void prepare(FrameSnapshot& snap, FrameIndex t = FrameIndex{0}, u64 frame = 1) {
        RenderSettings rs;
        renderer.prepare(*comp, project, t, nullptr, nullptr, nullptr, rs, frame, 0, DecodeMode::Still, 1.0f, snap);
    }
};

} // namespace

// =============================================================================
// A prévia de efeito NÃO encosta no swapchain (Fase 7.3 §13)
//
// A prévia abre um quadro, desenha a cartela fora da tela e lê de volta. Ela
// roda em thread de trabalho, ao lado do quadro da tela. Se abrisse um quadro
// COM superfície, disputaria o swapchain com o editor e, no fim, apresentaria
// o quadro da cartela no lugar do quadro da composição. É o caminho que
// derruba o app no aparelho quando o ciclo de vida refaz o swapchain no mesmo
// instante (rotação, app indo para segundo plano).
//
// O teste prende o contrato: com superfície anexada, a prévia abre um quadro
// offscreen, não adquire nada e não apresenta nada.
// =============================================================================
AUREA_TEST(PreviewBuffer, PendingCompositionAndObjectHdriKeepTheFrameIncomplete) {
    RenderFixture f;
    auto built = scene3d::build_shape3d(scene3d::default_shape3d(scene3d::Shape3DKind::Cube));
    AUREA_CHECK(built.ok()); if (!built.ok()) return;
    std::shared_ptr<const scene3d::SceneAsset> model(std::move(built.asset));
    f.renderer.set_model_lookup([](void* context, AssetId) { return *static_cast<std::shared_ptr<const scene3d::SceneAsset>*>(context); }, &model);
    Asset modelAsset; modelAsset.kind = AssetKind::Model3D;
    const auto modelId = f.project.add_asset(std::move(modelAsset));
    const auto layerId = f.comp->add_layer(LayerKind::Model3D, "model");
    Layer* layer = f.comp->layer(layerId); layer->model.scene = modelId; layer->threeD = true;
    Asset environment; environment.kind = AssetKind::Environment;
    const auto environmentId = f.project.add_asset(std::move(environment));
    auto pixels = std::make_shared<scene3d::HdriPixels>(); pixels->width = pixels->height = 1; pixels->rgb = {1.f, 1.f, 1.f};
    std::shared_ptr<const scene3d::HdriPixels> ready;
    f.renderer.set_hdri_lookup([](void* context, AssetId) { return *static_cast<std::shared_ptr<const scene3d::HdriPixels>*>(context); }, &ready);
    f.comp->environment().hdri = environmentId;
    FrameSnapshot snapshot;
    f.prepare(snapshot); AUREA_CHECK(f.renderer.take_incomplete());
    ready = pixels; f.prepare(snapshot); AUREA_CHECK(!f.renderer.take_incomplete());
    f.comp->environment().hdri = {};
    layer->environmentSource = 1; layer->environmentAsset = environmentId.pack();
    ready.reset(); f.prepare(snapshot); AUREA_CHECK(f.renderer.take_incomplete());
    ready = pixels; f.prepare(snapshot); AUREA_CHECK(!f.renderer.take_incomplete());
}

AUREA_TEST(MotionBlur, LayerExposureDoesNotAnticipateHoldAndPreservesRotationLength) {
    RenderFixture f;
    const auto id = f.solid("hold", Vec4{1,1,1,1});
    auto* layer = f.comp->layer(id); layer->motionBlur = true;
    auto& mb = f.comp->motion_blur(); mb.enabled = true; mb.shutterAngle = 180; mb.shutterPhase = -180;
    auto& position = layer->tracks.get_or_create(TrackProperty::PositionX);
    position.set(FrameIndex{0}, 700, Interpolation::Hold); position.set(FrameIndex{5}, 1000);
    FrameSnapshot frame; f.prepare(frame, FrameIndex{5});
    AUREA_CHECK_EQ(frame.layers.size(), 1u); if (frame.layers.empty()) return;
    // The entire requested exposure is before the discontinuity, not halfway
    // along an invented interpolation from the previous integer frame.
    AUREA_CHECK(!frame.layers[0].blurMatrices.empty());
    for (const auto& matrix : frame.layers[0].blurMatrices) AUREA_CHECK_NEAR(matrix.col[3].x, 500, .01);
    layer->tracks.clear();
    auto effect = make_effect(f.effects, effect_keys::kTransform, 1);
    const auto* params = f.effects.params(effect.type);
    u32 rotation = kInvalidIndex;
    for (u32 p = 0; p < params->count(); ++p) if (std::string_view(params->at(p).id) == "rotation") rotation = p;
    AUREA_CHECK(rotation != kInvalidIndex); if (rotation == kInvalidIndex) return;
    const u32 effectId = effect.id; layer->effects.push_back(std::move(effect));
    auto& track = layer->tracks.get_or_create(TrackProperty::EffectParam, effectId, param_track_key(rotation, 0));
    track.set(FrameIndex{4}, -180); track.set(FrameIndex{5}, 0); track.set(FrameIndex{6}, 180);
    mb.shutterPhase = -90; f.prepare(frame, FrameIndex{5});
    AUREA_CHECK(frame.layers[0].blurMatrices.size() > mb.samples);
    for (const auto& m : frame.layers[0].blurMatrices) {
        AUREA_CHECK_NEAR((Vec2{m.col[0].x,m.col[0].y}.length()), 1, .001);
        AUREA_CHECK_NEAR((Vec2{m.col[1].x,m.col[1].y}.length()), 1, .001);
    }
}

AUREA_TEST(MotionBlur, SceneExposureSharesRigidPosesAndKeepsPerLayerCamera) {
    RenderFixture f;
    auto built = scene3d::build_shape3d(scene3d::default_shape3d(scene3d::Shape3DKind::Cube));
    AUREA_CHECK(built.ok()); if (!built.ok()) return;
    built.asset->shapeParts = false;
    std::shared_ptr<const scene3d::SceneAsset> model(std::move(built.asset));
    f.renderer.set_model_lookup([](void* context, AssetId) { return *static_cast<std::shared_ptr<const scene3d::SceneAsset>*>(context); }, &model);
    Asset asset; asset.kind = AssetKind::Model3D; const auto assetId = f.project.add_asset(std::move(asset));
    const auto a = f.comp->add_layer(LayerKind::Model3D, "blurred");
    const auto b = f.comp->add_layer(LayerKind::Model3D, "sharp");
    for (auto id : {a,b}) { auto* l = f.comp->layer(id); l->model.scene = assetId; l->threeD = true; l->transform.position = Vec3{960,540,0}; l->model.unitScale = 100; }
    f.comp->layer(a)->motionBlur = true;
    const auto cameraId = f.comp->add_layer(LayerKind::Camera, "zoom");
    auto* camera = f.comp->layer(cameraId); camera->transform.position = Vec3{960,540,-1000};
    camera->tracks.get_or_create(TrackProperty::FocalLength).set(FrameIndex{0},24);
    camera->tracks.find(TrackProperty::FocalLength)->set(FrameIndex{10},72);
    f.comp->motion_blur().enabled = true;
    f.comp->floor().mode = 1;
    FrameSnapshot frame; f.prepare(frame, FrameIndex{5});
    AUREA_CHECK_EQ(frame.scenes.size(),1u); if (frame.scenes.empty()) return;
    const auto& scene = frame.scenes[0];
    AUREA_CHECK(scene.blurFrames.size() >= 2); if (scene.blurFrames.size() < 2) return;
    const scene3d::SceneInstance *firstMoving=nullptr,*lastMoving=nullptr,*firstSharp=nullptr,*lastSharp=nullptr;
    for (const auto& instance : scene.blurFrames.front().instances) {
        if(instance.layerKey==a.pack()) firstMoving=&instance; else if(instance.layerKey==b.pack()) firstSharp=&instance;
        AUREA_CHECK(instance.sharedPose != nullptr); AUREA_CHECK(instance.nodeWorld.empty());
    }
    for (const auto& instance : scene.blurFrames.back().instances) {
        if(instance.layerKey==a.pack()) lastMoving=&instance; else if(instance.layerKey==b.pack()) lastSharp=&instance;
    }
    AUREA_CHECK(firstMoving && lastMoving && firstSharp && lastSharp); if(!firstMoving||!lastMoving||!firstSharp||!lastSharp)return;
    AUREA_CHECK(firstMoving->sharedPose == lastMoving->sharedPose);
    AUREA_CHECK(std::fabs(firstMoving->sampleViewProj.col[0].x-lastMoving->sampleViewProj.col[0].x) > .001);
    AUREA_CHECK_NEAR(firstSharp->sampleViewProj.col[0].x,lastSharp->sampleViewProj.col[0].x,.00001);
    AUREA_CHECK_EQ(scene.blurFrames.front().floor.mode, scene.floor.mode);
    f.comp->layer(a)->transform.motionBlurAmount=0;
    f.prepare(frame, FrameIndex{5});
    AUREA_CHECK(frame.scenes[0].blurFrames.empty());
}

AUREA_TEST(MotionBlur, NestedSceneResolvesItsSaltedRenderIds) {
    RenderFixture f;
    auto built=scene3d::build_shape3d(scene3d::default_shape3d(scene3d::Shape3DKind::Cube));
    AUREA_CHECK(built.ok());if(!built.ok())return;
    built.asset->shapeParts=false;
    std::shared_ptr<const scene3d::SceneAsset> model(std::move(built.asset));
    f.renderer.set_model_lookup([](void* context,AssetId){return *static_cast<std::shared_ptr<const scene3d::SceneAsset>*>(context);},&model);
    Asset asset;asset.kind=AssetKind::Model3D;const auto assetId=f.project.add_asset(std::move(asset));
    const auto childId=f.project.timeline().create_composition("child",1920,1080,30);
    auto* child=f.project.timeline().composition(childId);
    const auto id=child->add_layer(LayerKind::Model3D,"moving");auto* layer=child->layer(id);
    layer->model.scene=assetId;layer->model.unitScale=100;layer->threeD=true;layer->motionBlur=true;
    layer->transform.position=Vec3{960,540,0};
    auto& x=layer->tracks.get_or_create(TrackProperty::PositionX);x.set(FrameIndex{0},900);x.set(FrameIndex{10},1020);
    child->motion_blur().enabled=true;
    f.comp=f.project.timeline().composition(f.project.timeline().root());
    const auto wrapper=f.comp->add_layer(LayerKind::Composition,"child");
    f.comp->layer(wrapper)->nested.composition=childId;
    FrameSnapshot snapshot;f.prepare(snapshot,FrameIndex{5});
    AUREA_CHECK_EQ(snapshot.nested.size(),1u);if(snapshot.nested.empty())return;
    const auto& scenes=snapshot.nested[0]->scenes;
    AUREA_CHECK_EQ(scenes.size(),1u);if(scenes.empty())return;
    AUREA_CHECK(scenes[0].blurFrames.size()>=2);if(scenes[0].blurFrames.size()<2)return;
    const auto& first=scenes[0].blurFrames.front().instances[0];
    const auto& last=scenes[0].blurFrames.back().instances[0];
    AUREA_CHECK(first.layerKey!=id.pack());
    AUREA_CHECK(last.world.col[3].x-first.world.col[3].x>4);
}

AUREA_TEST(PrecompInstances, NamespaceTracksTheEntireInstancePathAndStaysStableAcrossSeeks) {
    RenderFixture f;
    auto& timeline = f.project.timeline();
    const auto leafId = timeline.create_composition("leaf", 1920, 1080, 30);
    const auto wrapperId = timeline.create_composition("wrapper", 1920, 1080, 30);
    f.comp = timeline.composition(timeline.root());
    auto* leaf = timeline.composition(leafId);
    auto* wrapper = timeline.composition(wrapperId);
    const auto shape = leaf->add_layer(LayerKind::Shape, "shape");
    leaf->layer(shape)->shape.bounds = Rect{0, 0, 80, 80};
    const auto innerGroup = wrapper->add_layer(LayerKind::Composition, "same leaf");
    wrapper->layer(innerGroup)->nested.composition = leafId;
    for (int i = 0; i < 2; ++i) {
        const auto outer = f.comp->add_layer(LayerKind::Composition, "same wrapper");
        f.comp->layer(outer)->nested.composition = wrapperId;
        f.comp->layer(outer)->offset = FrameIndex{i * 15};
    }
    LayerId previous[2]{};
    for (i64 t : {0, 7, 0}) {
        FrameSnapshot snap; f.prepare(snap, FrameIndex{t});
        AUREA_CHECK_EQ(snap.nested.size(), 2u);
        if (snap.nested.size() != 2) return;
        LayerId ids[2]{};
        for (usize i = 0; i < 2; ++i) {
            AUREA_CHECK_EQ(snap.nested[i]->nested.size(), 1u);
            if (snap.nested[i]->nested.empty()) return;
            const auto& content = *snap.nested[i]->nested[0];
            AUREA_CHECK_EQ(content.layers.size(), 1u);
            if (content.layers.empty()) return;
            ids[i] = content.layers[0].id;
            AUREA_CHECK(ids[i] != shape);
            if (previous[i].valid()) AUREA_CHECK_EQ(ids[i].pack(), previous[i].pack());
            previous[i] = ids[i];
        }
        AUREA_CHECK(ids[0] != ids[1]);
    }
}

AUREA_TEST(SceneCuts, EndedOuterCameraReleasesThePrecompositionCamera) {
    RenderFixture f;
    const auto childId = f.project.timeline().create_composition("inside", 1920, 1080, 30);
    f.comp = f.project.timeline().composition(f.project.timeline().root());
    auto* child = f.project.timeline().composition(childId);
    const auto childCam = child->add_layer(LayerKind::Camera, "inside camera");
    child->layer(childCam)->camera.active = true;
    child->layer(childCam)->transform.position = Vec3{1100, 540, -1000};
    const auto shape = child->add_layer(LayerKind::Shape, "inside object");
    child->layer(shape)->threeD = true;
    child->layer(shape)->shape.bounds = Rect{0, 0, 80, 80};
    const auto light = child->add_layer(LayerKind::Light, "inside light");
    child->layer(light)->light.kind = LightKind::Directional;
    child->layer(light)->light.intensity = 2.5f;
    const auto outerCam = f.comp->add_layer(LayerKind::Camera, "outside camera");
    f.comp->layer(outerCam)->camera.active = true;
    f.comp->layer(outerCam)->transform.position = Vec3{900, 540, -1000};
    f.comp->layer(outerCam)->end = FrameIndex{10};
    const auto group = f.comp->add_layer(LayerKind::Composition, "inside");
    f.comp->layer(group)->nested.composition = childId;
    f.comp->layer(group)->nested.cameraPassThrough = true;
    f.comp->layer(group)->transform.anchor = Vec3{};
    f.comp->layer(group)->transform.position = Vec3{};
    for (i64 t : {9, 10, 11, 9, 10}) {
        FrameSnapshot snap; f.prepare(snap, FrameIndex{t});
        AUREA_CHECK_EQ(snap.nested.size(), 1u); if (snap.nested.empty()) return;
        const auto& scenes = snap.nested[0]->scenes;
        AUREA_CHECK(!scenes.empty()); if (scenes.empty()) return;
        AUREA_CHECK_NEAR(scenes[0].camera.position.x, t < 10 ? 900.f : 1100.f, .001f);
        AUREA_CHECK_EQ(scenes[0].lights.size(), 1u);
        AUREA_CHECK_NEAR(scenes[0].lights[0].intensity, 2.5f, .001f);
    }
    const auto reset = comp_camera(*f.comp, FrameIndex{10}, 1920, 1080);
    AUREA_CHECK_NEAR(reset.position.x, scene3d::default_camera(1920, 1080).position.x, .001f);
}

AUREA_TEST(SceneCuts, PanoramaRespectsItsOwnHalfOpenIntervalWithoutRequiringACamera) {
    RenderFixture f;
    auto& env = f.comp->environment();
    env.showBackground = true; env.studioPreset = 1;
    env.backgroundStart = FrameIndex{10}; env.backgroundEnd = FrameIndex{20};
    for (i64 t : {9, 10, 19, 20, 10, 20}) {
        FrameSnapshot snap; f.prepare(snap, FrameIndex{t});
        AUREA_CHECK_EQ(snap.scenes.size(), t >= 10 && t < 20 ? 1u : 0u);
    }
    env.backgroundStart = FrameIndex{0}; env.backgroundEnd = FrameIndex{-1};
    FrameSnapshot snap; f.prepare(snap, FrameIndex{100});
    AUREA_CHECK_EQ(snap.scenes.size(), 1u);
}

AUREA_TEST(SceneCuts, AlternatingPrecompEnvironmentsReuseMapsAndKeepCapturedTexturesAlive) {
    RenderFixture f;
    auto& sr = f.renderer.scene_renderer();
    sr.set_environment_quality({16, 16, 1}, {16, 16, 1});
    auto pixels = std::make_shared<scene3d::HdriPixels>();
    pixels->width = 32; pixels->height = 16; pixels->rgb.assign(32 * 16 * 3, .3f);
    scene3d::SceneEnvironment a, b;
    a.hdri = b.hdri = pixels; a.hdriKey = 100; b.hdriKey = 200;
    a.showBackground = b.showBackground = true;
    sr.finish_environment(a);
    const auto oneEnvironmentBytes = sr.resident_bytes();
    AUREA_CHECK(oneEnvironmentBytes > 0);
    const auto uploads = sr.environment_uploads();
    const auto destroyed = f.backend.texturesDestroyed;
    sr.finish_environment(b);
    AUREA_CHECK_EQ(sr.resident_bytes(), 2 * oneEnvironmentBytes);
    for (u32 frame = 0; frame < 20; ++frame) {
        for (const auto* env : {&a, &b}) {
            sr.request_environment(*env);
            AUREA_CHECK_EQ(sr.environment_key(), env->hdriKey);
            AUREA_CHECK(!sr.environment_pending());
        }
    }
    AUREA_CHECK_EQ(sr.environment_uploads(), uploads + 1);
    // All passes are built before they execute; switching precompositions
    // cannot invalidate a texture captured by an earlier pass in this frame.
    AUREA_CHECK_EQ(f.backend.texturesDestroyed, destroyed);
    // Per-object HDRIs share this lifetime requirement, including the fifth
    // distinct map beyond the old fixed four-entry cache.
    const auto beforeOwn = f.backend.texturesDestroyed;
    std::vector<TextureHandle> captured;
    for (u64 key = 300; key < 306; ++key) {
        a.hdriKey = key;
        const auto* maps = sr.environment_set(a, 1);
        AUREA_CHECK(maps != nullptr);
        if (maps) captured.push_back(maps->irradiance);
    }
    AUREA_CHECK_EQ(f.backend.texturesDestroyed, beforeOwn);
    for (auto texture : captured) AUREA_CHECK(f.backend.textureAlive[texture.id - 1]);
    AUREA_CHECK(sr.resident_bytes() > 2 * oneEnvironmentBytes);
    sr.release_all();
    AUREA_CHECK_EQ(sr.resident_bytes(), 0u);
}

AUREA_TEST(SceneCuts, FailedFinalEnvironmentCannotBeAcceptedAsACompletedPreviewEnvironment) {
    RenderFixture f;
    auto& scene = f.renderer.scene_renderer();
    scene3d::SceneEnvironment environment;
    scene.set_environment_quality({16, 0, 1}, {16, 0, 1});
    scene.finish_environment(environment);
    AUREA_CHECK(!scene.incomplete());
    const u64 original = scene.environment_uploads();
    scene.set_environment_quality({16, 0, 1}, {32, 0, 1});
    f.backend.beforeTextureUpload = [](TextureHandle, const void*, u32) { return Status{Errc::Timeout}; };
    scene.finish_environment(environment);
    AUREA_CHECK(scene.incomplete());
    AUREA_CHECK_EQ(scene.environment_uploads(), original);
    // Preview remains a valid fallback, but must not clear the failed final
    // flag and let the export encoder accept this frame as complete.
    scene.request_environment(environment);
    AUREA_CHECK(scene.incomplete());
    f.backend.beforeTextureUpload = {};
    scene.reset_incomplete();
    scene.finish_environment(environment);
    AUREA_CHECK(!scene.incomplete());
    AUREA_CHECK_EQ(scene.environment_uploads(), original + 1);
}

AUREA_TEST(SceneCuts, ColdPbrDescriptorsKeepCubeFallbackAcrossProjectResets) {
    RenderFixture f;
    auto& scene = f.renderer.scene_renderer();
    scene.set_environment_quality({16, 0, 1}, {16, 0, 1});
    auto built = scene3d::build_shape3d(scene3d::default_shape3d(scene3d::Shape3DKind::Cube));
    AUREA_CHECK(built.ok()); if (!built.ok()) return;
    scene3d::SceneFrame frame;
    frame.camera = scene3d::default_camera(256, 256);
    frame.post.bloom = false;
    scene3d::SceneInstance instance;
    instance.asset = std::shared_ptr<const scene3d::SceneAsset>(std::move(built.asset));
    instance.assetKey = 1;
    instance.world = Mat4::translation(Vec3{128, 128, 0});
    frame.instances.push_back(instance);
    TextureHandle fallback;
    for (usize i = 0; i < f.backend.textures.size(); ++i) {
        const auto& texture = f.backend.textures[i];
        if (texture.cube && texture.width == 1 && texture.layers == 6)
            fallback = TextureHandle{i + 1};
    }
    AUREA_CHECK(fallback.valid()); if (!fallback.valid()) return;
    TransientTexturePool pool;
    for (u64 project = 1; project <= 3; ++project) {
        // new_project/reopen clears project caches but keeps the renderer and
        // device. Its first preview cannot wait for the asynchronous IBL job.
        scene.release_all();
        AUREA_CHECK(f.backend.textureAlive[fallback.id - 1]);
        FrameGraph graph;
        Arena arena;
        FrameBegin begin;
        AUREA_CHECK(f.backend.begin_offscreen_frame(begin).ok());
        FGTexture output;
        AUREA_CHECK(scene.build(graph, arena, frame, 256, 256, project, output));
        graph.set_output(output, ResourceState::ShaderRead);
        pool.begin_frame(f.backend, project);
        AUREA_CHECK(graph.compile(pool).ok());
        f.backend.events.clear();
        graph.execute(*begin.commands, false);
        u32 seen[2]{};
        for (const auto& event : f.backend.events) {
            if (event.kind != MockBackend::Event::BindTexture || event.slot < 5 || event.slot > 6) continue;
            ++seen[event.slot - 5];
            const auto desc = f.backend.texture_desc(TextureHandle{event.texture});
            AUREA_CHECK_EQ(event.texture, fallback.id);
            AUREA_CHECK(desc.cube);
            AUREA_CHECK_EQ(desc.layers, 6u);
            AUREA_CHECK(event.texture > 0 && event.texture <= f.backend.textureAlive.size()
                        && f.backend.textureAlive[event.texture - 1]);
        }
        AUREA_CHECK(seen[0] > 0 && seen[1] > 0);
        graph.release(pool);
        pool.end_frame();
        AUREA_CHECK(f.backend.end_frame().ok());
        (void)scene.trim_environment_cache(project + 1);
        AUREA_CHECK(f.backend.textureAlive[fallback.id - 1]);
    }
    pool.clear();
    scene.shutdown();
    AUREA_CHECK(!f.backend.textureAlive[fallback.id - 1]);
}

AUREA_TEST(MemoryPressure, IdlePrecompAndObjectEnvironmentsAreReleasedAndRebuilt) {
    RenderFixture f;
    auto& scene = f.renderer.scene_renderer();
    scene.set_environment_quality({16, 16, 1}, {16, 16, 1});
    auto pixels = std::make_shared<scene3d::HdriPixels>();
    pixels->width = 32; pixels->height = 16; pixels->rgb.assign(32 * 16 * 3, .3f);
    scene3d::SceneEnvironment a, b;
    a.hdri = b.hdri = pixels; a.hdriKey = 100; b.hdriKey = 200;
    a.showBackground = b.showBackground = true;
    FrameGraph graph;
    Arena arena;
    auto build = [&](const scene3d::SceneEnvironment& environment, u64 frameNumber) {
        scene.finish_environment(environment);
        scene3d::SceneFrame frame; frame.environment = environment;
        FGTexture output;
        AUREA_CHECK(scene.build(graph, arena, frame, 16, 16, frameNumber, output));
        // The trim API is only called between completed GPU frames. This
        // mock records no submissions; discard its unused graph before trim.
        graph.reset(); arena.reset();
    };
    build(a, 1);
    AUREA_CHECK(scene.environment_set(a, 1) != nullptr);
    build(b, 1);
    AUREA_CHECK(scene.environment_set(b, 1) != nullptr);
    const u64 both = scene.resident_bytes();
    AUREA_CHECK(both > 0);
    build(b, 2);
    const auto* kept = scene.environment_set(b, 2);
    AUREA_CHECK(kept != nullptr); if (!kept) return;
    const TextureHandle live = kept->irradiance;
    const u64 uploads = scene.environment_uploads();
    AUREA_CHECK(scene.trim_environment_cache(2) >= 7u);
    AUREA_CHECK(scene.resident_bytes() > 0 && scene.resident_bytes() < both);
    AUREA_CHECK_EQ(scene.environment_key(), b.hdriKey);
    AUREA_CHECK(f.backend.textureAlive[live.id - 1]);
    build(b, 2);
    AUREA_CHECK_EQ(scene.environment_uploads(), uploads);
    // A subsequent 2D-only frame means no environment is still visible.
    AUREA_CHECK(scene.trim_environment_cache(3) >= 7u);
    AUREA_CHECK_EQ(scene.resident_bytes(), 0u);
    build(a, 4);
    AUREA_CHECK(scene.resident_bytes() > 0);
    AUREA_CHECK_EQ(scene.environment_uploads(), uploads + 1);
    AUREA_CHECK_EQ(scene.environment_key(), a.hdriKey);
}

#include "P010VideoRender.inl"

AUREA_TEST(VideoPreview, NativeRgbaFailureUsesLazyOwnedPlanesAndRetriesFailedUploads) {
    RenderFixture f;
    f.comp->set_size(32, 16);
    f.solid("fallback video", Vec4{1,1,1,1}, 16, 8);
    FrameSnapshot snapshot; f.prepare(snapshot);
    auto& layer = snapshot.layers[0];
    layer.source.kind = LayerSource::Kind::Video;
    layer.source.width = 32; layer.source.height = 16;
    layer.compFromLayer = Mat4::identity();
    auto* raw = new test::RgbaVideoFrame(true);
    FrameRef frame = FrameRef::adopt(raw);
    u32 uploads = 0;
    bool failUpload = true;
    f.backend.beforeTextureUpload = [&](TextureHandle texture, const void* bytes, u32 stride) {
        const auto desc = f.backend.texture_desc(texture);
        if (!desc.debugName || std::string_view(desc.debugName) != "plano-de-video") return OkStatus;
        ++uploads;
        AUREA_CHECK(desc.format == SurfaceFormat::RGBA8);
        AUREA_CHECK_EQ(desc.width, raw->width); AUREA_CHECK_EQ(desc.height, raw->height);
        AUREA_CHECK_EQ(stride, raw->rowStride);
        AUREA_CHECK(bytes == raw->bytes.data());
        return failUpload ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    RenderSettings settings;
    settings.previewCacheRevision = settings.previewCacheComposition = 1;
    settings.previewCacheOnly = true;
    f.renderer.set_preview_cache_budget(48ull << 20);
    AUREA_CHECK(f.renderer.configure_preview_cache(32, 16, settings) > 0);
    FrameStats stats; RenderTimings timings;
    layer.source.frame = frame;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK_EQ(uploads, 1u); AUREA_CHECK_EQ(raw->prepareCalls, 1u);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK(!f.renderer.preview_cached(FrameIndex{0}));
    failUpload = false;
    layer.source.frame = frame;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK_EQ(uploads, 2u);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK(f.renderer.preview_cached(FrameIndex{0}));
    AUREA_CHECK_EQ(stats.layersRendered, 1u);
    // Re-render without composition caching: same immutable decoded content
    // reuses the uploaded plane, even though native import still fails.
    settings.previewCacheOnly = false; settings.previewCacheRevision = settings.previewCacheComposition = 0;
    layer.source.frame = frame;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK_EQ(uploads, 2u); AUREA_CHECK(!f.renderer.take_incomplete());
}

AUREA_TEST(VideoPreview, WorkingNativeImportDoesNotMaterializeCpuPlanes) {
    RenderFixture f;
    f.solid("native video", Vec4{1,1,1,1});
    FrameSnapshot snapshot; f.prepare(snapshot);
    auto* raw = new test::RgbaVideoFrame(true);
    FrameRef frame = FrameRef::adopt(raw);
    auto& source = snapshot.layers[0].source;
    source.kind = LayerSource::Kind::Video; source.width = 32; source.height = 16; source.frame = frame;
    TextureDesc desc; desc.width = raw->width; desc.height = raw->height; desc.format = SurfaceFormat::RGBA8;
    const auto texture = f.backend.create_texture(desc);
    f.backend.beforeImportExternalImage = [&](const ExternalImageDesc& ext) -> Result<ExternalTexture> {
        AUREA_CHECK(ext.nativeHandle == raw->hardwareBuffer);
        ExternalTexture imported; imported.texture = *texture; imported.sampler = SamplerHandle{123}; imported.rgb = true;
        return imported;
    };
    u32 uploads = 0;
    f.backend.beforeTextureUpload = [&](TextureHandle texture, const void*, u32) {
        const auto uploaded = f.backend.texture_desc(texture);
        if (uploaded.debugName && std::string_view(uploaded.debugName) == "plano-de-video") ++uploads;
        return OkStatus;
    };
    FrameStats stats; RenderTimings timings; RenderSettings settings;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(raw->prepareCalls, 0u); AUREA_CHECK_EQ(raw->planeCount, 0u); AUREA_CHECK_EQ(uploads, 0u);
    AUREA_CHECK(f.renderer.last_frame_zero_copy());
}

AUREA_TEST(VideoPreview, ShortRgbaRowsAreRejectedBeforeDriverUploadAndRecover) {
    RenderFixture f;
    f.solid("short rows", Vec4{1,1,1,1});
    FrameSnapshot snapshot; f.prepare(snapshot);
    auto* raw = new DecodedFrame;
    auto pixels = std::vector<u8>(48 * 32 * 4, 127);
    raw->width = 48; raw->height = 32; raw->format = PixelFormat::RGBA8;
    raw->planes[0] = pixels.data(); raw->planeCount = 1; raw->strides[0] = 191;
    FrameRef frame = FrameRef::adopt(raw);
    auto& source = snapshot.layers[0].source;
    source.kind = LayerSource::Kind::Video; source.width = 48; source.height = 32; source.frame = frame;
    u32 uploads = 0;
    f.backend.beforeTextureUpload = [&](TextureHandle texture, const void*, u32) {
        const auto uploaded = f.backend.texture_desc(texture);
        if (uploaded.debugName && std::string_view(uploaded.debugName) == "plano-de-video") ++uploads;
        return OkStatus;
    };
    FrameStats stats; RenderTimings timings; RenderSettings settings;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(f.renderer.take_incomplete()); AUREA_CHECK_EQ(uploads, 0u);
    raw->strides[0] = 192; source.frame = frame;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(!f.renderer.take_incomplete()); AUREA_CHECK_EQ(uploads, 1u);
}

AUREA_TEST(VideoPreview, RawPlaybackRetainsNativePixelsUntilGpuCompletion) {
    RenderFixture f;
    f.solid("raw native video", Vec4{1,1,1,1});
    FrameSnapshot snapshot; f.prepare(snapshot);
    u32 deaths = 0;
    auto* raw = new test::RgbaVideoFrame(true); raw->deaths = &deaths;
    auto& source = snapshot.layers[0].source;
    source.kind = LayerSource::Kind::Video; source.width = 32; source.height = 16;
    source.frame = FrameRef::adopt(raw);
    TextureDesc desc; desc.width = raw->width; desc.height = raw->height; desc.format = SurfaceFormat::RGBA8;
    const auto texture = f.backend.create_texture(desc);
    f.backend.beforeImportExternalImage = [&](const ExternalImageDesc&) -> Result<ExternalTexture> {
        ExternalTexture imported; imported.texture = *texture; imported.sampler = SamplerHandle{123}; imported.rgb = true;
        return imported;
    };
    std::vector<std::pair<void (*)(void*), void*>> submitted;
    f.backend.beforeDeferUntilGpuDone = [&](void (*release)(void*), void* pixels) { submitted.emplace_back(release, pixels); };
    FrameStats stats; RenderTimings timings; RenderSettings settings; settings.rawPlayback = true;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK(!source.frame); // the snapshot has relinquished its reference
    AUREA_CHECK_EQ(deaths, 0u); // the decoder still cannot recycle its buffer
    AUREA_CHECK_EQ(submitted.size(), 1u);
    for (auto [release, pixels] : submitted) release(pixels);
    submitted.clear();
    AUREA_CHECK_EQ(deaths, 1u);
}

AUREA_TEST(PreviewBuffer, FailedImageUploadIsRetriedBeforeTheFrameCanBeCached) {
    RenderFixture f;
    ImagePixels image;
    image.width = image.height = 4;
    image.rgba.assign(4 * 4 * 4, 173);
    Asset asset; asset.kind = AssetKind::Image;
    const AssetId aid = f.project.add_asset(std::move(asset));
    const LayerId id = f.comp->add_layer(LayerKind::Image, "retry image");
    f.comp->layer(id)->source = aid;
    f.comp->layer(id)->transform.position = Vec3{960, 540, 0};
    RenderSettings settings;
    settings.previewCacheRevision = 1;
    settings.previewCacheComposition = 1;
    settings.previewCacheOnly = true;
    f.renderer.set_preview_cache_budget(48ull << 20);
    AUREA_CHECK(f.renderer.configure_preview_cache(1920, 1080, settings) > 0);
    FrameSnapshot snap;
    f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, nullptr,
        [](void* ctx, AssetId) -> const ImagePixels* { return static_cast<ImagePixels*>(ctx); },
        &image, settings, 1, 0, DecodeMode::Still, 1.f, snap);

    u32 imageAttempts = 0;
    TextureHandle uploaded;
    bool rejectUpload = true;
    f.backend.beforeTextureUpload = [&](TextureHandle texture, const void* data, u32 stride) -> Status {
        const auto desc = f.backend.texture_desc(texture);
        if (!desc.debugName || std::string_view(desc.debugName) != "imagem") return OkStatus;
        ++imageAttempts;
        if (uploaded.valid()) AUREA_CHECK_EQ(texture.id, uploaded.id);
        uploaded = texture;
        AUREA_CHECK_EQ(stride, 16u);
        AUREA_CHECK(std::equal(image.rgba.begin(), image.rgba.end(), static_cast<const u8*>(data)));
        return rejectUpload ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    FrameStats stats; RenderTimings timings;
    AUREA_CHECK(f.renderer.render(snap, settings, nullptr, stats, timings).ok());
    AUREA_CHECK_EQ(imageAttempts, 1u);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK(!f.renderer.preview_cached(FrameIndex{0}));
    i64 ranges[60]{};
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(1, 1, ranges, 30), 0u);

    // No second prepare or asset lookup: the queued bytes must survive failure.
    rejectUpload = false;
    AUREA_CHECK(f.renderer.render(snap, settings, nullptr, stats, timings).ok());
    AUREA_CHECK_EQ(imageAttempts, 2u);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK(f.renderer.preview_cached(FrameIndex{0}));
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(1, 1, ranges, 30), 1u);
    AUREA_CHECK_EQ(ranges[0], 0ll); AUREA_CHECK_EQ(ranges[1], 1ll);
    AUREA_CHECK(f.renderer.render(snap, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(f.renderer.last_preview_cache_hit());
    AUREA_CHECK_EQ(imageAttempts, 2u);
}

AUREA_TEST(PreviewBuffer, QueuedUploadRejectsExtentMismatchBeforeCallingTheBackend) {
    RenderFixture f;
    ImagePixels image; image.width = image.height = 4; image.rgba.assign(64, 173);
    Asset asset; asset.kind = AssetKind::Image;
    const auto aid = f.project.add_asset(std::move(asset));
    const auto id = f.comp->add_layer(LayerKind::Image, "bounded upload");
    f.comp->layer(id)->source = aid;
    RenderSettings settings;
    settings.previewCacheRevision = settings.previewCacheComposition = 1;
    settings.previewCacheOnly = true;
    f.renderer.set_preview_cache_budget(48ull << 20);
    AUREA_CHECK(f.renderer.configure_preview_cache(1920, 1080, settings) > 0);
    FrameSnapshot snapshot; FrameStats stats; RenderTimings timings;
    f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, nullptr,
        [](void* context, AssetId) { return static_cast<const ImagePixels*>(context); },
        &image, settings, 1, 0, DecodeMode::Still, 1.f, snapshot);
    usize imageTexture = f.backend.textures.size();
    for (usize i = 0; i < f.backend.textures.size(); ++i)
        if (f.backend.textures[i].debugName && std::string_view(f.backend.textures[i].debugName) == "imagem") imageTexture = i;
    AUREA_CHECK(imageTexture < f.backend.textures.size());
    if (imageTexture == f.backend.textures.size()) return;
    const auto original = f.backend.textures[imageTexture];
    u32 imageUploads = 0;
    f.backend.beforeTextureUpload = [&](TextureHandle texture, const void* data, u32) {
        if (texture.id == imageTexture + 1) {
            ++imageUploads;
            AUREA_CHECK(std::equal(image.rgba.begin(), image.rgba.end(), static_cast<const u8*>(data)));
        }
        return OkStatus;
    };
    // Covers insufficient extent, short stride, and arithmetic overflow without
    // relying on a real driver to fault on the read beyond the owned vector.
    for (const auto dimensions : {std::pair{4u, 5u}, std::pair{5u, 4u}, std::pair{0xffffffffu, 0xffffffffu}}) {
        f.backend.textures[imageTexture].width = dimensions.first;
        f.backend.textures[imageTexture].height = dimensions.second;
        AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
        AUREA_CHECK(f.renderer.take_incomplete());
        AUREA_CHECK(!f.renderer.preview_cached(FrameIndex{0}));
        AUREA_CHECK_EQ(imageUploads, 0u);
    }
    f.backend.textures[imageTexture] = original;
    AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(imageUploads, 1u);
    AUREA_CHECK(f.renderer.preview_cached(FrameIndex{0}));
}

AUREA_TEST(PreviewBuffer, RangesContainOnlyCompletedRunsAndRejectOldCompositionOrRevision) {
    RenderFixture f;
    f.comp->set_size(32, 32);
    RenderSettings settings;
    settings.previewCacheRevision = 7; settings.previewCacheComposition = 11;
    settings.previewCacheOnly = true;
    f.renderer.set_preview_cache_budget(48ull << 20);
    AUREA_CHECK_EQ(f.renderer.configure_preview_cache(32, 32, settings), kPreviewCacheMaxFrames);
    auto render = [&](i64 time, u32 missing = 0, u32 stale = 0) {
        FrameSnapshot snap; FrameStats stats; RenderTimings timings;
        f.renderer.prepare(*f.comp, f.project, FrameIndex{time}, nullptr, nullptr, nullptr,
            settings, 1, 0, DecodeMode::Still, 1.f, snap);
        snap.missingVideoFrames = missing; snap.staleVideoFrames = stale;
        AUREA_CHECK(f.renderer.render(snap, settings, nullptr, stats, timings).ok());
    };
    for (i64 frame : {12, 3, 4, 9, 11}) render(frame);
    render(5, 1); render(10, 0, 1); // neither missing nor approximate pixels paint a blue run
    i64 ranges[62]{};
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(7, 11, ranges, 31), 3u);
    const i64 expected[] = {3, 5, 9, 10, 11, 13};
    AUREA_CHECK(std::equal(std::begin(expected), std::end(expected), ranges));
    ranges[2] = 987;
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(7, 11, ranges, 1), 1u);
    AUREA_CHECK_EQ(ranges[2], 987ll); // capacity counts pairs, never individual elements
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(7, 11, nullptr, 30), 0u);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(7, 11, ranges, 0), 0u);
    // An edit or composition switch is visible to the UI before another render.
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(8, 11, ranges, 30), 0u);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(7, 12, ranges, 30), 0u);
    settings.previewCacheRevision = 8;
    AUREA_CHECK_EQ(f.renderer.configure_preview_cache(32, 32, settings), kPreviewCacheMaxFrames);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(8, 11, ranges, 30), 0u);
    render(40);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(8, 11, ranges, 30), 1u);
    settings.previewDenominator = 2;
    AUREA_CHECK_EQ(f.renderer.configure_preview_cache(32, 32, settings), kPreviewCacheMaxFrames);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(8, 11, ranges, 30), 0u);
    render(40);
    f.renderer.set_preview_cache_budget(0);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(8, 11, ranges, 30), 0u);
}

AUREA_TEST(PreviewBuffer, RangePublicationTracksEvictionWithoutWaitingForGpu) {
    RenderFixture f;
    f.comp->set_size(32, 32);
    RenderSettings settings;
    settings.previewCacheRevision = 5; settings.previewCacheComposition = 6;
    settings.previewCacheOnly = true;
    f.renderer.set_preview_cache_budget(32ull * 32 * 4 * 3);   // RGBA8 sRGB slots
    AUREA_CHECK_EQ(f.renderer.configure_preview_cache(32, 32, settings), 3u);
    auto render = [&](i64 time) {
        FrameSnapshot snap; FrameStats stats; RenderTimings timings;
        f.renderer.prepare(*f.comp, f.project, FrameIndex{time}, nullptr, nullptr, nullptr,
            settings, 1, 0, DecodeMode::Still, 1.f, snap);
        AUREA_CHECK(f.renderer.render(snap, settings, nullptr, stats, timings).ok());
    };
    for (i64 time : {1, 2, 4, 1, 8}) render(time); // touching 1 makes 2 the eviction victim
    i64 ranges[60]{};
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(5, 6, ranges, 30), 3u);
    const i64 expected[] = {1, 2, 4, 5, 8, 9};
    AUREA_CHECK(std::equal(std::begin(expected), std::end(expected), ranges));
    u32 gpuPolls = 0;
    f.backend.beforeWaitFrame = [&](u64, u64 timeout) {
        ++gpuPolls; AUREA_CHECK_EQ(timeout, 0ull);
        return Status{Errc::Timeout};
    };
    render(9); // in-flight slot cannot be overwritten or counted as frame 9
    AUREA_CHECK_EQ(gpuPolls, 1u);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(5, 6, ranges, 30), 3u);
    AUREA_CHECK_EQ(gpuPolls, 1u); // UI never asks the GPU for a fence
    AUREA_CHECK(std::equal(std::begin(expected), std::end(expected), ranges));
    f.backend.beforeWaitFrame = {};
    f.renderer.set_preview_cache_budget(48ull << 20);
    AUREA_CHECK_EQ(f.renderer.configure_preview_cache(32, 32, settings), kPreviewCacheMaxFrames);
    for (i64 time = 0; time < 64; time += 2) render(time);
    // Growing the budget kept frames 1, 4 and 8; with 0..62 that is 33 frames,
    // more than the old 30-frame/1 s ceiling, all kept: [0,3) then 4, 6, ...
    AUREA_CHECK_EQ(f.renderer.preview_cached_count(), 33u);
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(5, 6, ranges, 30), 30u);
    AUREA_CHECK_EQ(ranges[0], 0ll); AUREA_CHECK_EQ(ranges[1], 3ll);
    for (u32 i = 1; i < 30; ++i) {
        AUREA_CHECK_EQ(ranges[2 * i], static_cast<i64>(2 + 2 * i));
        AUREA_CHECK_EQ(ranges[2 * i + 1], ranges[2 * i] + 1);
    }
    i64 all[kPreviewCacheMaxFrames * 2]{};
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(5, 6, all, kPreviewCacheMaxFrames), 31u);
    f.renderer.forget_device();
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(5, 6, ranges, 30), 0u);
}

AUREA_TEST(PreviewCache, BeyondThirtyFramesWithinBudgetAndLocalEditKeepsOtherFrames) {
    RenderFixture f;
    f.comp->set_size(32, 32);
    RenderSettings settings;
    settings.previewCacheRevision = 9; settings.previewCacheComposition = 3;
    settings.previewCacheOnly = true;
    // 120 RGBA8 frames of 32x32 = 480 KiB: the byte budget, not a 30 cap, decides.
    f.renderer.set_preview_cache_budget(32ull * 32 * 4 * 120);
    AUREA_CHECK_EQ(f.renderer.configure_preview_cache(32, 32, settings), 120u);
    auto render = [&](i64 time) {
        FrameSnapshot snap; FrameStats stats; RenderTimings timings;
        f.renderer.prepare(*f.comp, f.project, FrameIndex{time}, nullptr, nullptr, nullptr,
            settings, 1, 0, DecodeMode::Still, 1.f, snap);
        AUREA_CHECK(f.renderer.render(snap, settings, nullptr, stats, timings).ok());
    };
    for (i64 time = 0; time < 90; ++time) render(time);
    AUREA_CHECK_EQ(f.renderer.preview_cached_count(), 90u);
    i64 ranges[kPreviewCacheMaxFrames * 2]{};
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(9, 3, ranges, kPreviewCacheMaxFrames), 1u);
    AUREA_CHECK_EQ(ranges[0], 0ll); AUREA_CHECK_EQ(ranges[1], 90ll);
    // An edit on a layer living in [40, 60) drops only those frames.
    f.renderer.invalidate_preview_frames(40, 60);
    AUREA_CHECK_EQ(f.renderer.preview_cached_count(), 70u);
    AUREA_CHECK(f.renderer.preview_cached(FrameIndex{39}));
    AUREA_CHECK(!f.renderer.preview_cached(FrameIndex{40}));
    AUREA_CHECK(!f.renderer.preview_cached(FrameIndex{59}));
    AUREA_CHECK(f.renderer.preview_cached(FrameIndex{60}));
    AUREA_CHECK_EQ(f.renderer.copy_preview_buffer_ranges(9, 3, ranges, kPreviewCacheMaxFrames), 2u);
    AUREA_CHECK_EQ(ranges[0], 0ll); AUREA_CHECK_EQ(ranges[1], 40ll);
    AUREA_CHECK_EQ(ranges[2], 60ll); AUREA_CHECK_EQ(ranges[3], 90ll);
    // Re-rendering the hole reuses the freed slots without evicting kept frames.
    for (i64 time = 40; time < 60; ++time) render(time);
    AUREA_CHECK_EQ(f.renderer.preview_cached_count(), 90u);
    // Growing the budget keeps what is cached; shrinking (pressure) frees it.
    f.renderer.set_preview_cache_budget(32ull * 32 * 4 * 240);
    AUREA_CHECK_EQ(f.renderer.preview_cached_count(), 90u);
    f.renderer.set_preview_cache_budget(32ull * 32 * 4 * 10);
    AUREA_CHECK_EQ(f.renderer.preview_cached_count(), 0u);
}

AUREA_TEST(Renderer, TemporalRgbVideoResolvesAllThreeDistantFrames) {
    RenderFixture f;
    aurea::test::SyntheticConfig config;
    config.decodeCostUs = 1000;
    aurea::test::SyntheticFactory factory(config);
    MemoryManager memory;
    memory.set_budget(MemoryClass::DecodedFrames, config.width * config.height * 3 / 2);
    MediaManager media;
    media.set_factory(&factory);
    media.set_memory(&memory);
    Asset asset;
    asset.kind = AssetKind::Video;
    asset.video.width = config.width; asset.video.height = config.height; asset.video.fps = config.fps;
    const AssetId aid = f.project.add_asset(std::move(asset));
    const LayerId id = f.comp->add_layer(LayerKind::Video, "RGB temporal");
    Layer* layer = f.comp->layer(id);
    layer->source = aid; layer->end = FrameIndex{300};
    layer->transform.position = Vec3{960, 540, 0};
    auto effect = make_effect(f.effects, effect_keys::kTimeWarpRgb, 1);
    effect.params[0].constant.v[0] = 0;
    effect.params[1].constant.v[0] = -15;
    effect.params[2].constant.v[0] = -30;
    effect.params[3].constant.v[0] = 0;
    effect.params[4].constant.v[0] = 100;
    layer->effects.push_back(std::move(effect));
    RenderSettings settings; settings.finalQuality = true;
    FrameSnapshot snapshot;
    bool ready = false;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        f.renderer.prepare(*f.comp, f.project, FrameIndex{60}, &media, nullptr, nullptr,
                           settings, 1, 0, DecodeMode::Still, 1, snapshot);
        ready = snapshot.layers.size() == 1 && snapshot.missingVideoFrames == 0 && snapshot.staleVideoFrames == 0;
        if (ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(ready);
    if (ready) {
        const auto& source = snapshot.layers[0].source;
        AUREA_CHECK_EQ(source.channelCount, 3u);
        for (u32 c = 0; c < 3; ++c) {
            AUREA_CHECK(static_cast<bool>(source.channel[c].frame));
            if (source.channel[c].frame)
                AUREA_CHECK_NEAR(source.channel[c].frame->ptsUs, (60 - c * 15) * 1e6 / 30, 1);
        }
    }
}

AUREA_TEST(Renderer, RemapPlaybackChangesDirectionWithoutShowingForwardPreroll) {
    RenderFixture f;
    aurea::test::SyntheticConfig config;
    config.decodeCostUs = 2000;
    aurea::test::SyntheticFactory factory(config);
    MemoryManager memory;
    memory.set_budget(MemoryClass::DecodedFrames, config.width * config.height * 3 / 2);
    MediaManager media;
    media.set_factory(&factory); media.set_memory(&memory);
    Asset asset; asset.kind = AssetKind::Video;
    asset.video.width = config.width; asset.video.height = config.height; asset.video.fps = config.fps;
    const AssetId aid = f.project.add_asset(std::move(asset));
    const LayerId id = f.comp->add_layer(LayerKind::Video, "remap forward and return");
    Layer* layer = f.comp->layer(id);
    layer->source = aid; layer->start = FrameIndex{40}; layer->end = FrameIndex{340}; layer->offset = FrameIndex{12};
    layer->transform.position = Vec3{960, 540, 0};
    layer->timeRemapEnabled = true;
    layer->timeRemap.set(FrameIndex{12}, 0);
    layer->timeRemap.set(FrameIndex{162}, 150);
    layer->timeRemap.set(FrameIndex{312}, 0);
    RenderSettings settings; settings.mediaGeneration = 1;
    FrameSnapshot snapshot;
    u64 render = 1;
    i64 lastShown = -1;
    for (i64 local : {0LL, 75LL, 150LL, 180LL, 210LL, 240LL, 270LL, 299LL}) {
        const i64 wanted = std::llround((local <= 150 ? local : 300 - local) * 1e6 / 30.);
        bool ready = false;
        u32 forwardSteps = 0, premature = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!ready && std::chrono::steady_clock::now() < deadline) {
            f.renderer.prepare(*f.comp, f.project, FrameIndex{40 + local}, &media, nullptr, nullptr,
                               settings, render++, 1, DecodeMode::Playback, 1.f, snapshot);
            if (!snapshot.layers.empty() && snapshot.layers[0].source.frame) {
                const auto& shown = snapshot.layers[0].source;
                if (local > 150) {
                    if (lastShown >= 0 && shown.frame->ptsUs > lastShown) ++forwardSteps;
                    if (shown.frame->ptsUs < wanted - 16667) ++premature;
                }
                lastShown = shown.frame->ptsUs;
                ready = shown.frameExact;
                if (ready) AUREA_CHECK_NEAR(lastShown, wanted, 1);
            }
            snapshot.release_video_frames();
            if (!ready) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        AUREA_CHECK(ready);
        AUREA_CHECK_EQ(forwardSteps, 0u);
        AUREA_CHECK_EQ(premature, 0u);
    }
}

AUREA_TEST(EffectPreview, NeverTouchesTheSwapchain) {
    RenderFixture f;
    AUREA_CHECK(f.backend.attach_surface(SurfaceDesc{}).ok());
    AUREA_CHECK(f.backend.has_surface());

    std::vector<u8> rgba;
    (void)f.renderer.render_effect_preview(f.effects, effect_type_id(effect_keys::kGaussianBlur), 96, 60, rgba);

    AUREA_CHECK_EQ(f.backend.offscreenFrames, static_cast<u32>(1));
    AUREA_CHECK_EQ(f.backend.acquires, static_cast<u32>(0));
    AUREA_CHECK_EQ(f.backend.presents, static_cast<u32>(0));
}

AUREA_TEST(Compositor, CompositionOrderIsTheCoreOrder) {
    // duplicate_layer já teve bug de ordem. A ordem de desenho TEM que ser a
    // do Core: fundo → frente, com a cópia logo acima do original.
    RenderFixture f;
    const LayerId a = f.solid("A", Vec4{1, 0, 0, 1});
    const LayerId b = f.solid("B", Vec4{0, 1, 0, 1});
    const LayerId c = f.solid("C", Vec4{0, 0, 1, 1});
    const LayerId b2 = f.comp->duplicate_layer(b, FrameIndex{0});
    FrameSnapshot snap;
    f.prepare(snap);
    AUREA_CHECK_EQ(snap.layers.size(), static_cast<usize>(4));
    AUREA_CHECK(snap.layers[0].id == a);
    AUREA_CHECK(snap.layers[1].id == b);
    AUREA_CHECK(snap.layers[2].id == b2);
    AUREA_CHECK(snap.layers[3].id == c);
    for (u32 i = 0; i < f.comp->order().size(); ++i) AUREA_CHECK(snap.layers[i].id == f.comp->order().at(i));
    // Reordenar no Core reordena a composição, sem lista paralela.
    AUREA_CHECK(f.comp->reorder_layer(c, 0));
    f.prepare(snap);
    AUREA_CHECK(snap.layers[0].id == c);
}

AUREA_TEST(Compositor, InvisibleAndOutOfRangeLayersCostNothing) {
    RenderFixture f;
    const LayerId a = f.solid("visivel", Vec4{1, 1, 1, 1});
    const LayerId hidden = f.solid("oculta", Vec4{1, 1, 1, 1});
    const LayerId transparent = f.solid("opacidade-zero", Vec4{1, 1, 1, 1});
    const LayerId later = f.solid("depois", Vec4{1, 1, 1, 1});
    f.comp->layer(hidden)->visible = false;
    f.comp->layer(transparent)->transform.opacity = 0.0f;
    f.comp->layer(later)->start = FrameIndex{100};
    FrameSnapshot snap;
    f.prepare(snap);
    AUREA_CHECK_EQ(snap.layers.size(), static_cast<usize>(1));
    AUREA_CHECK(snap.layers[0].id == a);
}

AUREA_TEST(Compositor, LayerTransformBecomesTheCompositeMatrix) {
    RenderFixture f;
    const LayerId id = f.solid("t", Vec4{1, 1, 1, 1});
    Layer* l = f.comp->layer(id);
    l->transform.scale = Vec3{2, 2, 1};
    l->transform.rotation = Vec3{0, 0, 90};
    FrameSnapshot snap;
    f.prepare(snap);
    const Mat4& m = snap.layers[0].compFromLayer;
    auto apply = [&](f32 x, f32 y) {
        return Vec2{m.col[0].x * x + m.col[1].x * y + m.col[3].x, m.col[0].y * x + m.col[1].y * y + m.col[3].y};
    };
    const Vec2 anchor = apply(200, 150);
    AUREA_CHECK_NEAR(anchor.x, 960.0f, 1e-3);
    AUREA_CHECK_NEAR(anchor.y, 540.0f, 1e-3);
    // 50 px à direita da âncora, escala 2, girado 90° → 100 px para baixo.
    const Vec2 p = apply(250, 150);
    AUREA_CHECK_NEAR(p.x, 960.0f, 1e-3);
    AUREA_CHECK_NEAR(p.y, 640.0f, 1e-3);
}

AUREA_TEST(Compositor, KeyframedTransformIsEvaluatedWithoutTouchingTheModel) {
    RenderFixture f;
    const LayerId id = f.solid("anim", Vec4{1, 1, 1, 1});
    Layer* l = f.comp->layer(id);
    Track& t = l->tracks.get_or_create(TrackProperty::PositionX);
    (void)t.set(FrameIndex{0}, 0.0f);
    (void)t.set(FrameIndex{100}, 1000.0f);
    FrameSnapshot snap;
    f.prepare(snap, FrameIndex{50});
    AUREA_CHECK_NEAR(snap.layers[0].compFromLayer.col[3].x, 500.0f - 200.0f, 1e-2);
    // O valor estático não foi sobrescrito pela animação.
    AUREA_CHECK_NEAR(l->transform.position.x, 960.0f, 1e-4);
}

AUREA_TEST(Compositor, TwoLayersBlurAndGlowCreateNoTexturesInSteadyState) {
    RenderFixture f;
    const LayerId a = f.solid("fundo", Vec4{0.2f, 0.3f, 0.8f, 1}, 960, 540);
    const LayerId b = f.solid("frente", Vec4{1, 0.8f, 0.2f, 1}, 1100, 600);
    Layer* la = f.comp->layer(a);
    la->transform.scale = Vec3{4, 4, 1};
    EffectInstance blur = make_effect(f.effects, effect_keys::kGaussianBlur, 0);
    blur.params[0].constant.v[0] = 40.0f;
    la->effects.push_back(blur);
    Layer* lb = f.comp->layer(b);
    EffectInstance glow = make_effect(f.effects, effect_keys::kGlow, 0);
    lb->effects.push_back(glow);

    FrameSnapshot snap;
    RenderSettings rs;
    rs.previewDenominator = 2;
    u32 createdAfterWarmup = 0;
    for (u64 frame = 1; frame <= 8; ++frame) {
        f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, nullptr, nullptr, nullptr, rs, frame, 0,
                           DecodeMode::Still, 1.0f, snap);
        FrameStats stats;
        RenderTimings timings;
        AUREA_CHECK(f.renderer.render(snap, rs, nullptr, stats, timings).ok());
        if (frame == 2) createdAfterWarmup = f.backend.texturesCreated;
        if (frame > 2) AUREA_CHECK_EQ(f.renderer.pool_stats().createdThisFrame, static_cast<u32>(0));
        AUREA_CHECK_EQ(stats.layersRendered, static_cast<u32>(2));
    }
    AUREA_CHECK_EQ(f.backend.texturesCreated, createdAfterWarmup);
    AUREA_CHECK(f.renderer.graph_stats().passesExecuted > 6);
    // Preview em 1/2: a composição é 960x540, as coordenadas continuam 1920x1080.
    AUREA_CHECK_EQ(snap.compWidth, static_cast<u32>(1920));
}

AUREA_TEST(MemoryPressure, DenseMotionBlurCompositesReleaseEarlierLayerTargets) {
    // The old single blend pass retained one full-HD accumulator per layer:
    // 96 * 1920 * 1080 * 8 = 1.48 GiB before effects and the output target.
    // Verify the live graph itself, not merely the pool's soft cache budget.
    for (const u32 den : {1u, 2u}) {
        u64 smallPeak = 0;
        for (const u32 count : {8u, 96u}) {
            RenderFixture f;
            f.comp->motion_blur().enabled = true;
            for (u32 i = 0; i < count; ++i) {
                const auto id = f.solid("moving", Vec4{.3f, .6f, .9f, 1});
                auto* layer = f.comp->layer(id);
                layer->motionBlur = true;
                layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0}, 800);
                layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{60}, 1000);
                auto blur = make_effect(f.effects, effect_keys::kGaussianBlur, 0);
                blur.params[0].constant.v[0] = 3;
                layer->effects.push_back(std::move(blur));
            }
            RenderSettings settings; settings.previewDenominator = den;
            settings.finalQuality = den == 1;
            for (u64 frame = 1; frame <= 3; ++frame) {
                FrameSnapshot snapshot;
                f.renderer.prepare(*f.comp, f.project, FrameIndex{30}, nullptr, nullptr, nullptr,
                                   settings, frame, 0, DecodeMode::Still, 1, snapshot);
                AUREA_CHECK_EQ(snapshot.layers.size(), count);
                for (const auto& layer : snapshot.layers) AUREA_CHECK(!layer.blurMatrices.empty());
                FrameStats stats; RenderTimings timings;
                AUREA_CHECK(f.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
                AUREA_CHECK_EQ(stats.layersRendered, count);
                const u64 targetBytes = u64(1920 / den) * (1080 / den) * 8;
                const u64 peak = f.renderer.graph_stats().transientBytes;
                AUREA_CHECK(peak <= 8 * targetBytes);
                if (count == 8) smallPeak = peak;
                else AUREA_CHECK(peak <= smallPeak + targetBytes);
                if (frame == 3) AUREA_CHECK_EQ(f.renderer.pool_stats().createdThisFrame, 0u);
            }
        }
    }
}

#include "NormalCompositeLivenessTests.inl"
#include "MaterialOverrideTests.inl"
#include "SceneTargetTests.inl"
#include "MinimaxPlan.inl"

AUREA_TEST(MotionBlur, SmallMovingLayersUseSmallAccumulatorsWithoutDroppingSamples) {
    for (const u32 den : {1u,2u}) {
        RenderFixture f;
        const auto id = f.solid("small moving overlay", Vec4{.3f,.6f,.9f,.5f});
        auto* layer = f.comp->layer(id);
        layer->shape.bounds = Rect{0,0,80,60}; layer->transform.anchor = Vec3{40,30,0};
        layer->motionBlur = true;
        auto& x = layer->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{0},800); x.set(FrameIndex{60},1000);
        auto& mb = f.comp->motion_blur(); mb.enabled = true; mb.samples = mb.adaptiveLimit = 64;
        RenderSettings settings; settings.previewDenominator = den; settings.finalQuality = true;
        FrameSnapshot snapshot;
        f.renderer.prepare(*f.comp,f.project,FrameIndex{30},nullptr,nullptr,nullptr,
            settings,1,0,DecodeMode::Still,1,snapshot);
        AUREA_CHECK_EQ(snapshot.layers[0].blurMatrices.size(),64u);
        FrameStats stats; RenderTimings timings;
        f.renderer.set_motion_blur_crop_enabled(false);
        AUREA_CHECK(f.renderer.render(snapshot,settings,nullptr,stats,timings).ok());
        const auto fullBytes = f.renderer.graph_stats().transientBytes;
        f.renderer.set_motion_blur_crop_enabled(true);
        AUREA_CHECK(f.renderer.render(snapshot,settings,nullptr,stats,timings).ok());
        AUREA_CHECK(!f.renderer.take_incomplete());
        const auto croppedBytes = f.renderer.graph_stats().transientBytes;
        const u64 fullTargetBytes = u64(1920/den)*(1080/den)*8;
        AUREA_CHECK(fullBytes - croppedBytes > fullTargetBytes * 99 / 100);
        AUREA_CHECK_EQ(snapshot.layers[0].blurMatrices.size(),64u);
        std::printf("    crop 1080p/den%u: %llu -> %llu transient bytes, 64 samples\n",den,
            static_cast<unsigned long long>(fullBytes),static_cast<unsigned long long>(croppedBytes));
    }
}

AUREA_TEST(MotionBlur, MissingIntegrationPipelineCannotCompleteASharpExportFrame) {
    RenderFixture f;
    const auto id = f.solid("requested motion blur",Vec4{1,1,1,1});
    auto* layer = f.comp->layer(id); layer->motionBlur = true;
    layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0},800);
    layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{60},1000);
    f.comp->motion_blur().enabled = true;
    FrameSnapshot snapshot; f.prepare(snapshot,FrameIndex{30});
    AUREA_CHECK(!snapshot.layers[0].blurMatrices.empty());
    RenderSettings settings; settings.finalQuality = true;
    FrameStats stats; RenderTimings timings;
    f.renderer.shaders().set_test_failing_shader(ShaderId::composite_layer_frag);
    AUREA_CHECK(f.renderer.render(snapshot,settings,nullptr,stats,timings).ok());
    AUREA_CHECK(f.renderer.take_incomplete());
    f.renderer.shaders().set_test_failing_shader(ShaderId::Count);
    AUREA_CHECK(f.renderer.render(snapshot,settings,nullptr,stats,timings).ok());
    AUREA_CHECK(!f.renderer.take_incomplete());
}

AUREA_TEST(MotionBlur, RejectedVectorBlurPassRetainsAValidSourceAndRecovers) {
    for (const auto failedShader : {ShaderId::video_flow_vblur_frag,ShaderId::video_flow_lk_frag}) {
        RenderFixture f;
        const auto id = f.solid("footage blur",Vec4{1,1,1,1});
        FrameSnapshot snapshot; f.prepare(snapshot);
        auto& source = snapshot.layers[0].source;
        test::SyntheticConfig config; config.pattern = test::SyntheticPattern::FastSquare;
        test::SyntheticDecoder decoder(config);
        i64 pts = 0; bool eos = false;
        AUREA_CHECK(decoder.next_frame(0,source.frame,pts,eos).ok());
        AUREA_CHECK(decoder.next_frame(0,source.frameB,pts,eos).ok());
        const FrameRef current = source.frame, next = source.frameB;
        source.kind = LayerSource::Kind::Video;
        source.width = config.width; source.height = config.height; source.vectorBlur = .5f;
        RenderSettings settings; settings.finalQuality = true;
        FrameStats stats; RenderTimings timings;
        f.renderer.shaders().set_test_failing_shader(failedShader);
        AUREA_CHECK(f.renderer.render(snapshot,settings,nullptr,stats,timings).ok());
        AUREA_CHECK(f.renderer.take_incomplete());
        // A rejected pass must not publish an unwritten transient texture.
        // The graph still contains the valid footage conversion/composition.
        AUREA_CHECK_EQ(stats.layersRendered,1u);
        f.renderer.shaders().set_test_failing_shader(ShaderId::Count);
        source.frame = current; source.frameB = next;
        AUREA_CHECK(f.renderer.render(snapshot,settings,nullptr,stats,timings).ok());
        AUREA_CHECK(!f.renderer.take_incomplete());
    }
}

AUREA_TEST(Compositor, TransformEffectAtTheEndAddsNoPass) {
    RenderFixture f;
    const LayerId id = f.solid("t", Vec4{1, 1, 1, 1});
    FrameSnapshot snap;
    RenderSettings rs;
    FrameStats stats;
    RenderTimings timings;
    f.prepare(snap);
    AUREA_CHECK(f.renderer.render(snap, rs, nullptr, stats, timings).ok());
    const u32 baseline = f.renderer.graph_stats().passesExecuted;

    EffectInstance tr = make_effect(f.effects, effect_keys::kTransform, 0);
    tr.params[3].constant.v[0] = 25.0f;
    f.comp->layer(id)->effects.push_back(tr);
    f.prepare(snap, FrameIndex{0}, 2);
    AUREA_CHECK(snap.plans[0].hasFold);
    AUREA_CHECK(f.renderer.render(snap, rs, nullptr, stats, timings).ok());
    AUREA_CHECK_EQ(f.renderer.graph_stats().passesExecuted, baseline);
}

// -----------------------------------------------------------------------------
// Passe de saída — o caminho do aparelho, que nenhum teste offscreen percorre.
// Com superfície (backbuffer em paisagem, pré-girado 90°), preview em 1/2,
// calor e zoom < 1, o quadro de vídeo vai para a composição e a composição é
// LIDA pelo passe de saída: a sua última transição antes do passe é para
// leitura (ShaderRead) SEM descartar o conteúdo, e o passe desenha no
// backbuffer. É a sequência que, errada, deixa o vídeo preto na tela e
// invisível em todo teste que lê a composição de volta.
// -----------------------------------------------------------------------------
AUREA_TEST(Compositor, OutputPassReadsTheCompositionWithPhoneSettings) {
    RenderFixture f;
    f.backend.surfaceWidth = 2400;                             // backbuffer físico em paisagem
    f.backend.surfaceHeight = 1080;
    f.backend.surfaceRotation = SurfaceRotation::Rotate90;     // lógico 1080×2400, em pé
    AUREA_CHECK(f.backend.attach_surface(SurfaceDesc{}).ok());

    aurea::test::SyntheticConfig config;
    aurea::test::SyntheticFactory factory(config);
    MemoryManager memory;
    memory.set_budget(MemoryClass::DecodedFrames, static_cast<u64>(config.width) * config.height * 3 / 2 * 8);
    MediaManager media;
    media.set_factory(&factory);
    media.set_memory(&memory);
    Asset asset;
    asset.kind = AssetKind::Video;
    asset.video.width = config.width;
    asset.video.height = config.height;
    asset.video.fps = config.fps;
    const AssetId aid = f.project.add_asset(std::move(asset));
    const LayerId id = f.comp->add_layer(LayerKind::Video, "video");
    Layer* layer = f.comp->layer(id);
    layer->source = aid;
    layer->end = FrameIndex{300};
    layer->transform.anchor = Vec3{config.width * 0.5f, config.height * 0.5f, 0};
    layer->transform.position = Vec3{960, 540, 0};

    RenderSettings rs;
    rs.previewDenominator = 2;
    rs.heavyScale = 0.5f;
    rs.viewportZoom = 0.5f;
    FrameSnapshot snap;
    bool ready = false;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, &media, nullptr, nullptr, rs, 1, 0,
                           DecodeMode::Still, 1, snap);
        ready = snap.layers.size() == 1 && snap.missingVideoFrames == 0 && snap.staleVideoFrames == 0;
        if (ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(ready);
    if (!ready) return;
    FrameStats stats;
    RenderTimings timings;
    AUREA_CHECK(f.renderer.render(snap, rs, nullptr, stats, timings).ok());
    AUREA_CHECK_EQ(stats.layersRendered, 1u);
    AUREA_CHECK_EQ(f.backend.presents, 1u);

    // O passe de saída é o único que desenha no backbuffer (id da superfície,
    // acima dos ids das texturas criadas). Dentro dele: a composição é
    // amostrada e há um draw.
    const auto& ev = f.backend.events;
    constexpr u32 kNone = 0xFFFFFFFFu;
    u32 outBegin = kNone, outEnd = kNone;
    u64 compTex = 0;
    bool drew = false;
    for (u32 i = 0; i < ev.size(); ++i) {
        if (ev[i].kind != MockBackend::Event::BeginPass || ev[i].texture <= f.backend.textures.size()) continue;
        outBegin = i;
        for (u32 k = i + 1; k < ev.size(); ++k) {
            if (ev[k].kind == MockBackend::Event::BindTexture) compTex = ev[k].texture;
            if (ev[k].kind == MockBackend::Event::Draw) drew = true;
            if (ev[k].kind == MockBackend::Event::EndPass) { outEnd = k; break; }
        }
        break;
    }
    AUREA_CHECK(outBegin != kNone && outEnd != kNone);
    AUREA_CHECK(drew);
    AUREA_CHECK(compTex != 0);
    if (outBegin == kNone || !compTex) return;
    // A composição foi escrita (anexo de cor) antes; a última transição dela
    // antes do passe de saída é para leitura e NÃO descarta o conteúdo.
    bool written = false, lastRead = false, lastDiscard = true;
    for (u32 i = 0; i < outBegin; ++i) {
        if (ev[i].kind != MockBackend::Event::Barrier || ev[i].texture != compTex) continue;
        // A PRIMEIRA escrita descarta (textura nova, sem conteúdo a guardar);
        // depois de escrita, nenhuma transição pode jogar o conteúdo fora.
        if (written) AUREA_CHECK(!ev[i].discard);
        if (ev[i].state == ResourceState::ColorAttachment) written = true;
        lastRead = ev[i].state == ResourceState::ShaderRead;
        lastDiscard = ev[i].discard;
    }
    AUREA_CHECK(written);
    AUREA_CHECK(lastRead);
    AUREA_CHECK(!lastDiscard);
    // Nada mais é desenhado na composição entre a leitura e o passe de saída.
    for (u32 i = outBegin; i < outEnd; ++i) AUREA_CHECK(ev[i].kind != MockBackend::Event::BeginPass || i == outBegin);
}

// -----------------------------------------------------------------------------
// Motion Tile — cenários dos testes do Aurea antigo que faltavam no porte
// (motion_tile_test.dart / motion_tile_escala_test.dart).
// -----------------------------------------------------------------------------
AUREA_TEST(MotionTile, TileXAndYCanDiffer) {
    motion_tile::Params p;
    p.tileX = 0.5f;
    p.tileY = 0.25f;
    for (u32 i = 0; i < 20; ++i) {
        const f32 v = 0.01f + 0.02f * static_cast<f32>(i);
        const Vec2 a = motion_tile::reference_lookup(p, Vec2{v, v});
        const Vec2 bx = motion_tile::reference_lookup(p, Vec2{v + 0.5f, v});
        const Vec2 by = motion_tile::reference_lookup(p, Vec2{v, v + 0.25f});
        AUREA_CHECK_NEAR(a.x, bx.x, 1e-5);   // período 1/2 em X
        AUREA_CHECK_NEAR(a.y, by.y, 1e-5);   // período 1/4 em Y
    }
}

AUREA_TEST(DropShadow, ShadowOnlyAppendsABooleanWithoutChangingLegacyDefaults) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const EffectInstance fx = make_effect(reg, effect_keys::kDropShadow, 7);
    const auto* specs = reg.params(fx.type);
    AUREA_CHECK_EQ(specs->count(), 6u);
    AUREA_CHECK(specs->at(5).type == ParamType::Bool);
    AUREA_CHECK(std::string(specs->at(5).id) == "shadow_only");
    AUREA_CHECK(!specs->at(5).defaultValue.as_bool());
    std::vector<ParamValue> values;
    for (const auto& slot : fx.params) values.push_back(slot.constant);
    EffectEval eval; eval.values = values.data(); eval.count = 6;
    values[1] = ParamValue::scalar(0);
    AUREA_CHECK(reg.find(fx.type)->is_identity(eval));
    values[5] = ParamValue::scalar(1);
    AUREA_CHECK(!reg.find(fx.type)->is_identity(eval));
    eval.count = 5; // old instance: absence means composite, without an out-of-bounds read
    AUREA_CHECK(reg.find(fx.type)->is_identity(eval));
}

AUREA_TEST(MotionTile, UniformScalePreservesAspectAndOldProjects) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    EffectInstance fx = make_effect(reg, effect_keys::kMotionTile, 7);
    std::vector<ParamValue> values;
    for (const auto& slot : fx.params) values.push_back(slot.constant);
    values[motion_tile::kTileWidth] = ParamValue::scalar(40.0f);
    values[motion_tile::kTileHeight] = ParamValue::scalar(80.0f);
    values[motion_tile::kScale] = ParamValue::scalar(50.0f);
    EffectEval eval;
    eval.values = values.data();
    eval.count = static_cast<u32>(values.size());
    const auto scaled = motion_tile::params_from(eval);
    AUREA_CHECK_NEAR(scaled.tileX, 0.2f, 1e-6);
    AUREA_CHECK_NEAR(scaled.tileY, 0.4f, 1e-6);
    // Ausência do novo slot em uma instância anterior equivale a 100%.
    eval.count = motion_tile::kLayout + 1;
    const auto old = motion_tile::params_from(eval);
    AUREA_CHECK_NEAR(old.tileX, 0.4f, 1e-6);
    AUREA_CHECK_NEAR(old.tileY, 0.8f, 1e-6);
    const auto& spec = reg.params(fx.type)->at(motion_tile::kScale);
    AUREA_CHECK(spec.flags & kParamAnimatable);
    AUREA_CHECK_NEAR(spec.defaultValue.v[0], 100.0f, 1e-6);
    AUREA_CHECK_NEAR(spec.typed_max(), 1000.0f, 1e-6);
}

AUREA_TEST(MotionTile, WideFrameAsksMoreOnWidthThanHeight) {
    motion_tile::Params p;
    // Layer 1000×1000 a 50 % no centro de um quadro 2000×1000.
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(2000, 1000, 1000, 1000, 1000, 500, 0.5f));
    AUREA_CHECK_NEAR(f.x, 4.0f, 1e-2);
    AUREA_CHECK_NEAR(f.y, 2.0f, 1e-2);
}

AUREA_TEST(MotionTile, ScaleAndRotationAddUp) {
    motion_tile::Params p;
    // Metade do tamanho (×2) e 45° num quadrado (×√2): as duas coisas se somam.
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1000, 1000, 1000, 1000, 500, 500, 0.5f, 45.0f));
    AUREA_CHECK_NEAR(f.x, 2.0f * std::sqrt(2.0f), 2e-3);
    AUREA_CHECK_NEAR(f.y, 2.0f * std::sqrt(2.0f), 2e-3);
}

// =============================================================================
// Pacote de paridade — comportamentos de movimento (sem GPU).
//
// Oscilar, Balançar e Agitar são função pura do tempo: no fim da pilha viram
// a matriz da composição, então o plano do EffectGraph já diz a pose inteira.
// =============================================================================
namespace {

const char* const kEffectPackKeys[] = {
    effect_keys::kOscillate, effect_keys::kSwing, effect_keys::kWiggle,
    effect_keys::kIrisWipe, effect_keys::kBoxWipe, effect_keys::kVenetianBlinds,
    effect_keys::kRadialBlur, effect_keys::kMirror, effect_keys::kCrop, effect_keys::kVignette,
    effect_keys::kMosaic, effect_keys::kFindEdges, effect_keys::kHueSaturation,
    // Geradores e recorte do editor antigo.
    effect_keys::kFractalNoise, effect_keys::kGradientRamp, effect_keys::kFourColorGradient,
    effect_keys::kAudioSpectrum, effect_keys::kStrokeOutline, effect_keys::kMatteRefine,
    // O VHS de Estilizar (look de fita com OSD).
    effect_keys::kVhsLook,
};

/// Recursos de teste que "têm" espectro: devolvem uma textura fixa e anotam
/// o pedido que o efeito fez no planejamento.
struct SpectrumResources final : EffectResources {
    AudioSpectrumRequest last{};
    u32 calls = 0;
    TextureHandle curve_lut(const CurveData&) noexcept override { return TextureHandle{4242}; }
    TextureHandle audio_spectrum(const AudioSpectrumRequest& r) noexcept override {
        last = r;
        ++calls;
        return TextureHandle{77};
    }
};

/// Plano de UMA camada 100x100 com o efeito no fim, no quadro pedido (30 qps).
EffectPlan plan_motion(const EffectRegistry& reg, const EffectInstance& fx, i64 frame) {
    Layer l;
    l.effects.push_back(fx);
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{frame}, 1.0f, placement(100, 100), nullptr, plan, 30.0);
    return plan;
}

Vec2 apply(const Mat4& m, Vec2 p) {
    const Vec4 r = m * Vec4{p.x, p.y, 0.0f, 1.0f};
    return Vec2{r.x, r.y};
}

} // namespace

AUREA_TEST(EffectPack, EveryNewEffectRegistersWithStableIdsAndTypedRanges) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    for (const char* key : kEffectPackKeys) {
        const EffectTypeId id = reg.find_key(key);
        AUREA_CHECK_MSG(id != 0 && reg.find(id) != nullptr, key);
        const ParameterRegistry* p = reg.params(id);
        AUREA_CHECK_MSG(p && p->count() > 0, key);
        if (!p) continue;
        for (u32 i = 0; i < p->count(); ++i) {
            const ParamSpec& s = p->at(i);
            // A faixa digitada nunca é mais estreita que a do slider.
            AUREA_CHECK(s.typed_min() <= s.minValue && s.typed_max() >= s.maxValue);
            // Números contínuos são keyframáveis (o pedido do dono).
            if (s.type == ParamType::Float || s.type == ParamType::Angle || s.type == ParamType::Point2D)
                AUREA_CHECK_MSG(s.animatable(), key);
        }
    }
    // As amplitudes dos comportamentos aceitam digitação além do slider.
    const ParameterRegistry* osc = reg.params(reg.find_key(effect_keys::kOscillate));
    AUREA_CHECK(osc && osc->at(osc->find("magnitude")).typed_max() > osc->at(osc->find("magnitude")).maxValue);
    AUREA_CHECK(osc && osc->at(osc->find("decay")).typed_max() > osc->at(osc->find("decay")).maxValue);
    const ParameterRegistry* shake = reg.params(reg.find_key(effect_keys::kShake));
    AUREA_CHECK(shake && shake->find("direction") != kInvalidIndex && shake->find("decay") != kInvalidIndex);
}

AUREA_TEST(EffectPack, GeneratorsAndMatteEffectsDeclareTheirTypedRangesAndAppendedParams) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    // Ruído fractal: a célula digitada vai muito além do slider; a semente é
    // inteira (fica no slider) e a evolução é um ângulo animável.
    const ParameterRegistry* noise = reg.params(reg.find_key(effect_keys::kFractalNoise));
    AUREA_CHECK(noise && noise->at(noise->find("scale")).typed_max() > noise->at(noise->find("scale")).maxValue);
    AUREA_CHECK(noise && noise->at(noise->find("seed")).type == ParamType::Int);
    AUREA_CHECK(noise && noise->at(noise->find("evolution")).type == ParamType::Angle
                && noise->at(noise->find("evolution")).animatable());
    // Contorno: largura e suavidade em px, digitadas até 10x o slider.
    const ParameterRegistry* stroke = reg.params(reg.find_key(effect_keys::kStrokeOutline));
    AUREA_CHECK(stroke && stroke->at(stroke->find("width")).typed_max() >= 1000.0f);
    AUREA_CHECK(stroke && stroke->at(stroke->find("softness")).typed_max() > stroke->at(stroke->find("softness")).maxValue);
    AUREA_CHECK(stroke && stroke->at(stroke->find("position")).type == ParamType::Enum);
    // Espectro: faixas inteiras limitadas ao teto da textura; pontos relativos.
    const ParameterRegistry* spectrum = reg.params(reg.find_key(effect_keys::kAudioSpectrum));
    AUREA_CHECK(spectrum && spectrum->at(spectrum->find("bands")).maxValue == static_cast<f32>(kAudioSpectrumMaxBands));
    AUREA_CHECK(spectrum && (spectrum->at(spectrum->find("start")).flags & kParamRelative));
    // Os acrescentados aos efeitos que já existiam vêm DEPOIS dos antigos, com
    // padrão neutro: projeto antigo abre igual.
    const ParameterRegistry* chroma = reg.params(reg.find_key(effect_keys::kChromaKey));
    AUREA_CHECK(chroma && chroma->find("pre_blur") == chroma->count() - 1);
    AUREA_CHECK(chroma && chroma->at(chroma->find("pre_blur")).defaultValue.as_float() == 0.0f);
    const ParameterRegistry* lens = reg.params(reg.find_key(effect_keys::kLensBlur));
    AUREA_CHECK(lens && lens->find("iris_curvature") == lens->count() - 3
                && lens->find("scale_x") == lens->count() - 2 && lens->find("scale_y") == lens->count() - 1);
    AUREA_CHECK(lens && lens->at(lens->find("iris_curvature")).defaultValue.as_float() == 0.0f
                && lens->at(lens->find("scale_x")).defaultValue.as_float() == 100.0f
                && lens->at(lens->find("scale_y")).typed_max() > lens->at(lens->find("scale_y")).maxValue);
    // Neutros saem da cadeia: contorno sem largura, refinar sem nada.
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kStrokeOutline, 0));
    l.effects.back().params[0].constant = ParamValue::scalar(0.0f);
    l.effects.push_back(make_effect(reg, effect_keys::kMatteRefine, 1));
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK(plan.empty());
    AUREA_CHECK_EQ(plan.droppedIdentity, 2u);
}

AUREA_TEST(EffectPack, SpectrumAnalysisFindsATestToneInItsBand) {
    // Um seno de 1 kHz em escala 0,5 (−6 dB): a faixa dele, em 16 faixas
    // logarítmicas de 30 Hz a 16 kHz, é a 8 — e só ela.
    std::vector<f32> tone(audio::kSpectrumWindow);
    for (u32 i = 0; i < tone.size(); ++i) tone[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
    f32 bands[16] = {};
    f32 level = 0.0f;
    audio::analyze_spectrum(tone.data(), static_cast<u32>(tone.size()), 16, 1.0f, bands, &level);
    u32 best = 0;
    for (u32 b = 1; b < 16; ++b) if (bands[b] > bands[best]) best = b;
    AUREA_CHECK_EQ(best, 8u);
    AUREA_CHECK(bands[8] > 0.8f && bands[8] <= 1.0f);   // −6 dB numa régua de 60 dB
    for (u32 b = 0; b < 6; ++b) AUREA_CHECK(bands[b] < 0.05f);
    for (u32 b = 11; b < 16; ++b) AUREA_CHECK(bands[b] < 0.05f);
    AUREA_CHECK(level > 0.7f && level <= 1.0f);
    // O centro da faixa 8 fica perto do tom; a 0 é grave, a 15 é aguda.
    AUREA_CHECK(audio::spectrum_band_center_hz(8, 16) > 700.0f && audio::spectrum_band_center_hz(8, 16) < 1400.0f);
    AUREA_CHECK(audio::spectrum_band_center_hz(0, 16) < 60.0f && audio::spectrum_band_center_hz(15, 16) > 10000.0f);
    // Determinístico: a mesma janela dá os mesmos bits.
    f32 again[16] = {};
    audio::analyze_spectrum(tone.data(), static_cast<u32>(tone.size()), 16, 1.0f, again, nullptr);
    for (u32 b = 0; b < 16; ++b) AUREA_CHECK(again[b] == bands[b]);
    // Sensibilidade: o dobro do ganho sobe a barra (+6 dB = 0,1 da régua).
    audio::analyze_spectrum(tone.data(), static_cast<u32>(tone.size()), 16, 2.0f, again, nullptr);
    AUREA_CHECK_NEAR(again[8] - bands[8], 0.1f, 0.02f);
    // Silêncio: tudo em zero, sem NaN.
    std::vector<f32> silence(audio::kSpectrumWindow, 0.0f);
    audio::analyze_spectrum(silence.data(), static_cast<u32>(silence.size()), 16, 1.0f, bands, &level);
    for (u32 b = 0; b < 16; ++b) AUREA_CHECK(bands[b] == 0.0f);
    AUREA_CHECK(level == 0.0f);
}

AUREA_TEST(EffectPack, AudioSpectrumResolvesItsSpectrumAtPlanTimeAndCarriesItToBuild) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kAudioSpectrum, 0));
    l.effects.back().params[1].constant = ParamValue::scalar(24.0f);    // faixas
    l.effects.back().params[14].constant = ParamValue::scalar(200.0f);  // sensibilidade
    // Com recursos: o pedido leva a camada dona, as faixas e o ganho; a
    // textura viaja no eval.
    SpectrumResources res;
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), &res, plan);
    AUREA_CHECK_EQ(plan.evals.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(res.calls, 1u);
    AUREA_CHECK(res.last.host == &l);
    AUREA_CHECK_EQ(res.last.bands, 24u);
    AUREA_CHECK_NEAR(res.last.gain, 2.0f, 1e-5);
    AUREA_CHECK(res.last.source == AudioSpectrumSource::Automatic);
    AUREA_CHECK(!plan.evals.empty() && plan.evals[0].aux.valid() && plan.evals[0].aux.id == 77u);
    AUREA_CHECK(!plan.evals.empty() && plan.evals[0].auxInfo.x == 1.0f);
    // O que só valia sob o lock não sai do planejamento.
    AUREA_CHECK(!plan.evals.empty() && plan.evals[0].layer == nullptr && plan.evals[0].resources == nullptr);
    // A fonte escolhida chega ao renderer.
    l.effects.back().params[0].constant = ParamValue::scalar(2.0f);
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), &res, plan);
    AUREA_CHECK(res.last.source == AudioSpectrumSource::FirstWithAudio);
    // Sem recursos (teste sem GPU): o efeito fica no plano, sem textura.
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK_EQ(plan.evals.size(), static_cast<usize>(1));
    AUREA_CHECK(!plan.evals.empty() && !plan.evals[0].aux.valid() && plan.evals[0].auxInfo.x == 0.0f);
}

AUREA_TEST(EffectPack, OscillateCyclesAccumulatesFrequencyAndPreservesPhaseThroughSeeks) {
    EffectRegistry reg; register_builtin_effects(reg);
    Layer layer; layer.effects.push_back(make_effect(reg,effect_keys::kOscillateCycles,37));
    auto& fx=layer.effects.back();
    AUREA_CHECK_EQ(fx.params.size(),usize{6});
    AUREA_CHECK_NEAR(fx.params[1].constant.v[0],45,1e-6);
    AUREA_CHECK_NEAR(fx.params[2].constant.v[0],2,1e-6);
    fx.params[1].constant=ParamValue::scalar(0);
    auto& frequency=layer.tracks.get_or_create(TrackProperty::EffectParam,37,param_track_key(2,0));
    for(int fps:{24,30,60}) {
        frequency.keys.clear();frequency.set(FrameIndex{0},0);frequency.set(FrameIndex{fps},2);
        for(int frame:{fps/2, fps*2, 0, fps/2, fps}) {
            EffectPlan plan;EffectGraph::plan(layer,reg,FrameIndex{frame},1,placement(),nullptr,plan,fps);
            AUREA_CHECK(plan.hasFold);
            const float expected=frame==fps/2?25.f:0.f;
            AUREA_CHECK_NEAR(plan.foldMatrix.col[3].x,expected,.002f);
        }
    }
    frequency.keys.clear(); fx.params[2].constant=ParamValue::scalar(0);
    fx.params[5].constant=ParamValue::scalar(.25f);
    for(int wave=0;wave<2;++wave) for(int mode=0;mode<3;++mode) {
        fx.params[4].constant=ParamValue::scalar(float(wave));fx.params[0].constant=ParamValue::scalar(float(mode));
        EffectPlan plan;EffectGraph::plan(layer,reg,FrameIndex{300},1,placement(),nullptr,plan);
        AUREA_CHECK_NEAR(plan.foldMatrix.col[3].x,mode==1?0.f:25.f,.001f);
        AUREA_CHECK_NEAR(plan.foldMatrix.col[3].z,mode==1?25.f:0.f,.001f);
    }
    // Trim and placement don't change the effect's local clock.
    layer.start=FrameIndex{120}; layer.offset=FrameIndex{30};
    fx.params[0].constant=ParamValue::scalar(0);fx.params[5].constant=ParamValue::scalar(0);
    fx.params[2].constant=ParamValue::scalar(1);
    EffectPlan plan;EffectGraph::plan(layer,reg,FrameIndex{15},1,placement(),nullptr,plan,60);
    AUREA_CHECK_NEAR(plan.foldMatrix.col[3].x,25,.001f);
}

AUREA_TEST(EffectPack, OscillateTravelsAlongItsDirectionAndDecaysToIdentity) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    EffectInstance fx = make_effect(reg, effect_keys::kOscillate, 1);
    fx.params[0].constant = ParamValue::scalar(90.0f);   // direção: para baixo
    fx.params[1].constant = ParamValue::scalar(40.0f);   // amplitude
    fx.params[2].constant = ParamValue::scalar(1.0f);    // 1 Hz
    // Fase 0 no quadro 0: seno 0 → nenhuma mudança, efeito sai da cadeia.
    EffectPlan still = plan_motion(reg, fx, 0);
    AUREA_CHECK(!still.hasFold);
    AUREA_CHECK_EQ(still.droppedIdentity, 1u);
    // Um quarto de ciclo (7,5 quadros ≈ quadro 7 e 8): pico para baixo.
    fx.params[3].constant = ParamValue::scalar(90.0f);   // fase = pico no início
    EffectPlan peak = plan_motion(reg, fx, 0);
    AUREA_CHECK(peak.hasFold);
    AUREA_CHECK_NEAR(peak.foldMatrix.col[3].x, 0.0f, 1e-3);
    AUREA_CHECK_NEAR(peak.foldMatrix.col[3].y, 40.0f, 1e-3);
    // Meio ciclo depois (0,5 s = 15 quadros): o outro extremo.
    EffectPlan other = plan_motion(reg, fx, 15);
    AUREA_CHECK_NEAR(other.foldMatrix.col[3].y, -40.0f, 1e-3);
    // Decaimento 2/s: em 1 s a amplitude cai a e^-2; em 10 s é identidade.
    fx.params[8].constant = ParamValue::scalar(2.0f);
    EffectPlan oneSecond = plan_motion(reg, fx, 30);
    AUREA_CHECK_NEAR(oneSecond.foldMatrix.col[3].y, 40.0f * std::exp(-2.0f), 1e-2);
    EffectPlan settled = plan_motion(reg, fx, 300);
    AUREA_CHECK(!settled.hasFold);
    AUREA_CHECK_EQ(settled.droppedIdentity, 1u);
    // Rotação e pulso de escala giram/escalam em volta do pivô: ele não anda.
    fx.params[8].constant = ParamValue::scalar(0.0f);
    fx.params[1].constant = ParamValue::scalar(0.0f);
    fx.params[4].constant = ParamValue::scalar(20.0f);
    fx.params[5].constant = ParamValue::scalar(50.0f);
    EffectPlan spin = plan_motion(reg, fx, 0);
    const Vec2 pivot = apply(spin.foldMatrix, Vec2{50.0f, 50.0f});
    AUREA_CHECK_NEAR(pivot.x, 50.0f, 1e-3);
    AUREA_CHECK_NEAR(pivot.y, 50.0f, 1e-3);
    const Vec2 corner = apply(spin.foldMatrix, Vec2{100.0f, 50.0f});
    AUREA_CHECK_NEAR(std::hypot(corner.x - 50.0f, corner.y - 50.0f), 75.0f, 1e-2);   // 50 px × 150%
}

AUREA_TEST(EffectPack, OscillateWaveformsStayInsideTheAmplitude) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    EffectInstance fx = make_effect(reg, effect_keys::kOscillate, 1);
    fx.params[1].constant = ParamValue::scalar(25.0f);
    fx.params[2].constant = ParamValue::scalar(3.0f);
    for (u32 shape = 0; shape < 5; ++shape) {
        fx.params[6].constant = ParamValue::scalar(static_cast<f32>(shape));
        f32 lo = 0.0f, hi = 0.0f;
        for (i64 f = 0; f < 60; ++f) {
            const EffectPlan pl = plan_motion(reg, fx, f);
            const f32 x = pl.hasFold ? pl.foldMatrix.col[3].x : 0.0f;
            AUREA_CHECK(std::isfinite(x));
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        AUREA_CHECK(hi <= 25.0f + 1e-3f && lo >= -25.0f - 1e-3f);
        AUREA_CHECK_MSG(hi - lo > 10.0f, "a onda devia andar");
    }
}

AUREA_TEST(EffectPack, SwingTurnsAroundThePivotAndSettles) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    EffectInstance fx = make_effect(reg, effect_keys::kSwing, 1);
    fx.params[0].constant = ParamValue::scalar(30.0f);   // 30° para cada lado
    fx.params[3].constant = ParamValue::scalar(90.0f);   // pico no início
    const EffectPlan peak = plan_motion(reg, fx, 0);
    AUREA_CHECK(peak.hasFold);
    // Pivô padrão: meio da borda de cima.
    const Vec2 pivot = apply(peak.foldMatrix, Vec2{50.0f, 0.0f});
    AUREA_CHECK_NEAR(pivot.x, 50.0f, 1e-3);
    AUREA_CHECK_NEAR(pivot.y, 0.0f, 1e-3);
    const Vec2 bob = apply(peak.foldMatrix, Vec2{50.0f, 100.0f});
    AUREA_CHECK_NEAR(std::fabs(bob.x - 50.0f), 50.0f, 1e-2);   // 100 · sen 30°
    AUREA_CHECK_NEAR(bob.y, 100.0f * std::cos(30.0f * kDeg2Rad), 1e-2);
    // Meio ciclo depois (1 Hz → 15 quadros) o pêndulo está do outro lado.
    const Vec2 back = apply(plan_motion(reg, fx, 15).foldMatrix, Vec2{50.0f, 100.0f});
    AUREA_CHECK((back.x - 50.0f) * (bob.x - 50.0f) < 0.0f);
    fx.params[4].constant = ParamValue::scalar(5.0f);    // decaimento
    AUREA_CHECK(!plan_motion(reg, fx, 300).hasFold);
}

AUREA_TEST(EffectPack, WiggleChannelsAreIndependentDeterministicAndHold) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    EffectInstance fx = make_effect(reg, effect_keys::kWiggle, 1);
    fx.params[2].constant = ParamValue::scalar(0.0f);    // só X
    const EffectPlan a = plan_motion(reg, fx, 7);
    const EffectPlan again = plan_motion(reg, fx, 7);
    AUREA_CHECK(a.hasFold);
    AUREA_CHECK_NEAR(a.foldMatrix.col[3].x, again.foldMatrix.col[3].x, 1e-6);
    AUREA_CHECK_NEAR(a.foldMatrix.col[3].y, 0.0f, 1e-4);
    AUREA_CHECK_NEAR(a.foldMatrix.col[0].y, 0.0f, 1e-5);   // sem rotação
    AUREA_CHECK(std::fabs(a.foldMatrix.col[3].x) <= 30.0f + 1e-3f);
    // Outra semente, outro caminho.
    f32 diff = 0.0f;
    EffectInstance other = fx;
    other.params[8].constant = ParamValue::scalar(77.0f);
    for (i64 f = 1; f < 40; f += 3) {
        diff += std::fabs(plan_motion(reg, fx, f).foldMatrix.col[3].x - plan_motion(reg, other, f).foldMatrix.col[3].x);
    }
    AUREA_CHECK(diff > 1.0f);
    // Segurar: 2 Hz → um valor por meio segundo (15 quadros). 16 e 20 caem no
    // mesmo degrau; sem segurar, a curva anda entre eles.
    fx.params[7].constant = ParamValue::boolean(true);
    AUREA_CHECK_NEAR(plan_motion(reg, fx, 16).foldMatrix.col[3].x, plan_motion(reg, fx, 20).foldMatrix.col[3].x, 1e-5);
    fx.params[7].constant = ParamValue::boolean(false);
    AUREA_CHECK(std::fabs(plan_motion(reg, fx, 16).foldMatrix.col[3].x - plan_motion(reg, fx, 20).foldMatrix.col[3].x) > 1e-3f);
    // Intensidade 0 = nenhum movimento.
    fx.params[5].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(!plan_motion(reg, fx, 7).hasFold);
}

AUREA_TEST(EffectPack, MotionBehaviorInTheMiddleOfTheStackGetsItsOwnPass) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    Layer l;
    l.effects.push_back(make_effect(reg, effect_keys::kSwing, 0));
    l.effects.back().params[3].constant = ParamValue::scalar(90.0f);
    l.effects.push_back(make_effect(reg, effect_keys::kGaussianBlur, 1));
    l.effects.back().params[0].constant.v[0] = 4.0f;
    EffectPlan plan;
    EffectGraph::plan(l, reg, FrameIndex{0}, 1.0f, placement(), nullptr, plan);
    AUREA_CHECK(!plan.hasFold);
    AUREA_CHECK_EQ(plan.stages.size(), static_cast<usize>(2));
}

// Split de um vídeo, tocando por cima do corte: o pedaço seguinte já tem o
// decoder aberto e o 1º quadro pronto ANTES do corte. No quadro do corte a
// camada entra com o quadro exato — nunca some (preto) esperando o decoder.
AUREA_TEST(Renderer, PlaybackPrerollsTheNextClipSoTheCutNeverFlashesBlack) {
    RenderFixture f;
    aurea::test::SyntheticConfig config;
    config.decodeCostUs = 2000;
    aurea::test::SyntheticFactory factory(config);
    MemoryManager memory;
    memory.set_budget(MemoryClass::DecodedFrames, 64ull * 1024 * 1024);
    MediaManager media;
    media.set_factory(&factory);
    media.set_memory(&memory);
    Asset asset;
    asset.kind = AssetKind::Video;
    asset.video.width = config.width; asset.video.height = config.height; asset.video.fps = config.fps;
    asset.video.frameCount = FrameIndex{config.frameCount};
    const AssetId aid = f.project.add_asset(std::move(asset));
    auto clip = [&](i64 start, i64 end) {
        const LayerId id = f.comp->add_layer(LayerKind::Video, "pedaco");
        Layer* l = f.comp->layer(id);
        l->source = aid; l->start = FrameIndex{start}; l->end = FrameIndex{end}; l->offset = FrameIndex{start};
        l->transform.position = Vec3{960, 540, 0};
        return id;
    };
    (void)clip(0, 60);
    const LayerId second = clip(60, 120);
    RenderSettings settings;
    settings.mediaGeneration = 7;
    FrameSnapshot snapshot;
    // Tocando, meio segundo antes do corte.
    const auto t0 = std::chrono::steady_clock::now();
    u64 frame = 1;
    bool prepared = false;
    while (!prepared && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
        f.renderer.prepare(*f.comp, f.project, FrameIndex{45}, &media, nullptr, nullptr,
                           settings, frame++, 1, DecodeMode::Playback, 1.0f, snapshot);
        snapshot.release_video_frames();
        if (factory.opened.load() >= 2) {
            if (VideoSource* src = media.source_for(second, aid, *f.project.asset(aid), frame, false)) {
                bool exact = false;
                (void)src->frame_for(2000000, &exact);
                prepared = exact;
            }
        }
        if (!prepared) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    AUREA_CHECK(prepared);
    // O quadro do corte: só a segunda camada, já com o quadro exato.
    f.renderer.prepare(*f.comp, f.project, FrameIndex{60}, &media, nullptr, nullptr,
                       settings, frame++, 1, DecodeMode::Playback, 1.0f, snapshot);
    AUREA_CHECK_EQ(snapshot.missingVideoFrames, 0u);
    AUREA_CHECK_EQ(snapshot.layers.size(), static_cast<usize>(1));
    if (!snapshot.layers.empty()) {
        AUREA_CHECK(static_cast<bool>(snapshot.layers[0].source.frame));
        AUREA_CHECK(snapshot.layers[0].source.frameExact);
    }
    snapshot.release_video_frames();
}

// Beta 2140: "Amostras por quadro" alto não mudava nada na prévia (ela lia só
// o `previewSamples` fixo). A escolha vale na prévia também, reduzida apenas
// pela folga do aparelho.
AUREA_TEST(MotionBlur, SamplesPerFrameDrivesThePreviewToo) {
    using namespace aurea;
    MotionBlurSettings settings; settings.enabled = true; settings.adaptiveLimit = 128;
    settings.previewSamples = 16;
    for (const u32 samples : {2u, 16u, 32u, 64u}) {
        settings.samples = samples;
        // Animação interna (texto, partículas): o mínimo é a escolha da pessoa.
        AUREA_CHECK_EQ(shutter_sample_count(settings, false, 1.0f, 0, true), samples);
        AUREA_CHECK_EQ(shutter_sample_count(settings, true, 1.0f, 0, true), samples);
        // Movimento curto (beta 2026-10-07, "o motion blur trava"): na prévia
        // bastam amostras a 0,75 px umas das outras — 3 px = 5 amostras, com
        // 2 ou com 64 pedidas. O export continua com o piso da pessoa.
        AUREA_CHECK_EQ(shutter_sample_count(settings, false, 1.0f, 3.0), 5u);
        AUREA_CHECK_EQ(shutter_sample_count(settings, true, 1.0f, 3.0), std::max(samples, 7u));
        // Rastro longo: a prévia usa todas as amostras pedidas.
        AUREA_CHECK(shutter_sample_count(settings, false, 1.0f, 200.0) >= samples);
        // Sob carga (qualidade 0,5) num rastro médio, a escolha da pessoa ainda
        // sobe a contagem até o que o rastro pede a 0,75 px.
        AUREA_CHECK_EQ(shutter_sample_count(settings, false, 0.5f, 30.0), std::max(21u, std::min(samples / 2, 41u)));
    }
    // Aparelho quente (qualidade 0,5): metade, nunca abaixo de 2.
    settings.samples = 64;
    AUREA_CHECK_EQ(shutter_sample_count(settings, false, 0.5f, 0, true), 32u);
    // O teto adaptativo ainda manda.
    settings.adaptiveLimit = 40;
    AUREA_CHECK_EQ(shutter_sample_count(settings, true, 1.0f, 0, true), 40u);
}

#include "RaysFootprintPlan.inl"
