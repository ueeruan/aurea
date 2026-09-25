// =============================================================================
//  Testes do renderer SEM GPU: FrameGraph, EffectGraph, Motion Tile (geometria
//  portada do Aurea antigo), ShaderLibrary e o compositor contra um backend
//  que só grava comandos. Os testes com a GPU de verdade estão em test_gpu.cpp.
// =============================================================================
#include "TestFramework.hpp"
#include "MockBackend.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/MotionTile.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/render/ShaderLibrary.hpp"

#include <cmath>
#include <vector>

using namespace aurea;
using aurea::test::MockBackend;

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
    AUREA_CHECK_EQ(before, static_cast<u32>(49)); // includes the shared Halation effect
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
