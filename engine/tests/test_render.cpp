// =============================================================================
//  Testes do renderer SEM GPU: FrameGraph, EffectGraph, Motion Tile (geometria
//  portada do Aurea antigo), ShaderLibrary e o compositor contra um backend
//  que só grava comandos. Os testes com a GPU de verdade estão em test_gpu.cpp.
// =============================================================================
#include "TestFramework.hpp"
#include "MockBackend.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/audio/Spectrum.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/MotionTile.hpp"
#include "aurea/effects/ShakeMotion.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/render/PreviewRefill.hpp"

#include <cmath>
#include <limits>
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
    AUREA_CHECK_EQ(before, static_cast<u32>(116));
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

AUREA_TEST(MotionTile, RequestedOutputWinsWhenLarger) {
    motion_tile::Params p;
    p.outputX = 3.0f;
    p.outputY = 1.5f;
    const Vec2 f = motion_tile::coverage_factors(p, tile_placement(1920, 1080, 1920, 1080, 960, 540, 1.0f));
    AUREA_CHECK_NEAR(f.x, 3.0f, 1e-4);
    AUREA_CHECK_NEAR(f.y, 1.5f, 1e-4);
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

AUREA_TEST(MotionTile, PhaseShiftsAlternateColumns) {
    motion_tile::Params p;
    p.tileX = p.tileY = 0.5f;
    p.phaseTurns = 0.5f;   // 180°
    const Vec2 even = motion_tile::reference_lookup(p, Vec2{0.3f, 0.3f});
    const Vec2 odd = motion_tile::reference_lookup(p, Vec2{0.8f, 0.3f});
    AUREA_CHECK_NEAR(std::fabs(odd.y - even.y), 0.5f, 1e-5);
    AUREA_CHECK_NEAR(odd.x, even.x, 1e-5);
}

AUREA_TEST(MotionTile, ClampStretchesTheEdgeAndBeatsMirror) {
    motion_tile::Params p;
    p.tileX = p.tileY = 0.5f;
    p.clamp = true;
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
