#include "SceneTargetFixture.hpp"

namespace {
struct SceneTargetBudgetRun { Status status{}; u64 peak = 0; u32 shadowSize = 0, stages = 0; };
SceneTargetBudgetRun scene_target_budget_run(bool staged, u64 limit) {
    RenderFixture f;
    f.backend.mapBuffers = true;
    for (u32 i = 0; i < 8; ++i) {
        const auto id = f.solid("expanded scene neighbor", Vec4{.3f, .6f, .9f, .65f}, 120.f + 240.f * i, 180.f);
        auto* layer = f.comp->layer(id); layer->shape.bounds = Rect{0, 0, 1920, 1080};
        layer->transform.anchor = Vec3{960, 540, 0}; layer->transform.scale = Vec3{.105f, .105f, 1};
        layer->transform.rotation.z = 90.f * (i % 2);
        auto tile = make_effect(f.effects, effect_keys::kMotionTile, 0);
        tile.params[motion_tile::kOutputWidth].constant.v[0] = 150;
        tile.params[motion_tile::kOutputHeight].constant.v[0] = 150;
        tile.params[motion_tile::kMirror].constant.v[0] = 1; tile.params[motion_tile::kScale].constant.v[0] = 70;
        layer->effects.push_back(std::move(tile));
        auto blur = make_effect(f.effects, effect_keys::kGaussianBlur, 1); blur.params[0].constant.v[0] = 3;
        layer->effects.push_back(std::move(blur));
        auto glow = make_effect(f.effects, effect_keys::kGlow, 2); glow.params[0].constant.v[0] = 20;
        layer->effects.push_back(std::move(glow));
    }
    SceneTargetBudgetRun result;
    f.backend.queryMemoryStats = [&] {
        GpuMemoryStats memory;
        for (usize i = 0; i < f.backend.textures.size(); ++i) if (f.backend.textureAlive[i])
            memory.usedBytes += f.backend.textures[i].estimated_bytes();
        memory.reservedBytes = memory.usedBytes;
        result.peak = std::max(result.peak, memory.usedBytes + (u64{12} << 20));
        return memory;
    };
    f.renderer.set_transient_cache_budget(1); // Retain live targets; idle targets cannot hide the cause.
    f.renderer.set_tracked_resource_budget(limit, [](void*) noexcept -> u64 { return u64{12} << 20; });
    RenderSettings settings; settings.finalQuality = true; settings.stageUnblurredScenes = staged; settings.dither = false;
    FrameSnapshot snapshot;
    f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, nullptr, nullptr, nullptr, settings, 1, 0, DecodeMode::Still, 1, snapshot);
    test_fixtures::scene_target::add_scene(snapshot, test_fixtures::material_override::asset());
    TextureDesc desc; desc.width = 320; desc.height = 180; desc.renderTarget = desc.sampled = true; desc.format = SurfaceFormat::RGBA16F;
    const auto texture = f.backend.create_texture(desc); AUREA_CHECK(texture.ok()); if (!texture) return result;
    OffscreenTarget target{*texture, desc.width, desc.height}; FrameStats stats; RenderTimings timings;
    result.status = f.renderer.render(snapshot, settings, &target, stats, timings);
    if (result.status.ok()) AUREA_CHECK(!f.renderer.take_incomplete());
    result.shadowSize = f.renderer.heavy_stats().lastShadowMapSize; result.stages = timings.sceneExposureBatches;
    for (const auto& d : f.backend.textures) if (d.debugName && std::strcmp(d.debugName, "3d-sombra") == 0)
        AUREA_CHECK_EQ(d.width, 4096u);
    f.backend.queryMemoryStats = {}; f.backend.destroy_texture(*texture);
    return result;
}
}

AUREA_TEST(SceneTargets, ExactScenePhasesAvoidTheUniqueEffectAndShadowUnion) {
    const auto old = scene_target_budget_run(false, 1ull << 30);
    const auto staged = scene_target_budget_run(true, 1ull << 30);
    AUREA_CHECK(old.status.ok()); AUREA_CHECK(staged.status.ok());
    AUREA_CHECK(staged.peak + (40ull << 20) < old.peak);
    AUREA_CHECK_EQ(old.shadowSize, 4096u); AUREA_CHECK_EQ(staged.shadowSize, 4096u);
    // Frozen after measurement: 383,322,468 B legacy vs 309,818,724 B staged.
    // A regression cannot raise its own admission envelope by changing a peak.
    constexpr u64 limit = 330ull << 20;
    const auto refused = scene_target_budget_run(false, limit);
    const auto accepted = scene_target_budget_run(true, limit);
    std::printf("\n    scene phases: oldPeak=%llu stagedPeak=%llu sameBudget=%llu old=%u staged=%u\n",
        static_cast<unsigned long long>(old.peak), static_cast<unsigned long long>(staged.peak),
        static_cast<unsigned long long>(limit), refused.status.raw(), accepted.status.raw());
    AUREA_CHECK_EQ(refused.status.code(), Errc::OutOfDeviceMemory); AUREA_CHECK(accepted.status.ok());
    AUREA_CHECK_EQ(accepted.stages, 1u);
}

AUREA_TEST(SceneTargets, AutomaticCapturePolicyBoundsEveryNestedTargetWithoutChangingManualPhases) {
    FrameSnapshot snapshot; snapshot.compWidth = 1920; snapshot.compHeight = 1080;
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    snapshot.scenes.resize(4);
    AUREA_CHECK(Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 1920, 1080));
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 180, 321));
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 321, 180));
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 0, 180));
    snapshot.scenes.emplace_back();
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    snapshot.scenes.resize(3);
    auto child = std::make_unique<FrameSnapshot>(); child->compWidth = 1920; child->compHeight = 1080;
    child->scenes.emplace_back(); snapshot.nested.push_back(std::move(child));
    AUREA_CHECK(Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    snapshot.nested[0]->compWidth = 3840;
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    snapshot.nested[0]->compWidth = 1920;
    snapshot.nested[0]->scenes[0].planeLayers.push_back(0);
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    snapshot.nested[0]->scenes[0].planeLayers.clear();
    snapshot.nested[0]->scenes[0].particleLayers.push_back(0);
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    snapshot.nested[0]->scenes[0].particleLayers.clear();
    snapshot.nested[0]->scenes[0].blurFrames.emplace_back();
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(snapshot, 320, 180));
    const auto oddNested = test_fixtures::scene_target::pixels();
    AUREA_CHECK(Renderer::can_stage_small_scene_capture(oddNested, 214, 120));
    AUREA_CHECK(!Renderer::can_stage_small_scene_capture(oddNested, 428, 240));
}

AUREA_TEST(SceneTargets, HeldOutputsAndBackendRetirementRemainAuthoritative) {
    MockBackend backend; TransientTexturePool pool;
    TextureDesc desc; desc.width = desc.height = 1024; desc.format = SurfaceFormat::RGBA16F; desc.sampled = desc.renderTarget = true;
    const u64 bytes = desc.estimated_bytes(); u64 actual = 2 * bytes;
    backend.queryMemoryStats = [&] { GpuMemoryStats memory; memory.usedBytes = memory.reservedBytes = actual; return memory; };
    pool.begin_frame(backend, 1); const auto held = pool.acquire(desc); const auto idle = pool.acquire(desc);
    AUREA_CHECK(held.valid() && idle.valid()); pool.release(idle);
    AUREA_CHECK_EQ(pool.trim_unreferenced(0), 0u);
    AUREA_CHECK_EQ(pool.trim_unreferenced(1), 1u); AUREA_CHECK(backend.textureAlive[held.id - 1]);
    pool.set_tracked_resource_budget(2 * bytes);
    pool.begin_frame(backend, 2);
    // Pool accounting went down, but delayed backend destruction gives no credit.
    AUREA_CHECK(!pool.acquire(desc).valid()); actual = bytes;
    const auto next = pool.acquire(desc); AUREA_CHECK(next.valid());
    pool.release(held); pool.release(next); pool.clear();
}

AUREA_TEST(SceneTargets, SubmittedCancellationAndTimeoutDoNotStartTheMainGraph) {
    for (const bool cancel : {false, true}) {
        RenderFixture f; f.backend.mapBuffers = true; std::atomic<bool> cancelled{false};
        std::vector<std::pair<void (*)(void*), void*>> pending;
        f.backend.beforeDeferUntilGpuDone = [&](void (*release)(void*), void* value) { pending.emplace_back(release, value); };
        u32 waits = 0;
        f.backend.beforeWaitFrame = [&](u64, u64) { ++waits; if (cancel) cancelled.store(true); return Status{Errc::Timeout}; };
        FrameSnapshot snapshot; snapshot.compWidth = 320; snapshot.compHeight = 180;
        test_fixtures::scene_target::add_scene(snapshot, test_fixtures::material_override::asset());
        TextureDesc desc; desc.width = 320; desc.height = 180; desc.renderTarget = true; desc.format = SurfaceFormat::RGBA16F;
        const auto texture = f.backend.create_texture(desc); AUREA_CHECK(texture.ok()); if (!texture) continue;
        RenderSettings settings; settings.finalQuality = settings.stageUnblurredScenes = true;
        // Cancellation is injected by wait_frame after submitting the stage.
        // Allow cold environment preparation to reach that callback; the
        // separate timeout row deliberately expires before its first wait.
        settings.cancelFlag = &cancelled; settings.sceneExposureFenceTimeoutNs = cancel ? 10'000'000'000 : 1;
        OffscreenTarget target{*texture, 320, 180}; FrameStats stats; RenderTimings timings;
        const u32 destroyed = f.backend.texturesDestroyed;
        const u64 started = monotonic_ns();
        const auto result = f.renderer.render(snapshot, settings, &target, stats, timings);
        std::printf("\n    submitted stage stop: cancel=%u result=%u elapsed_ms=%.3f waits=%u submitted=%u\n",
            cancel ? 1u : 0u, static_cast<u32>(result.code()),
            static_cast<double>(monotonic_ns() - started) * 1e-6, waits, f.backend.framesSubmitted);
        if (cancel) AUREA_CHECK(waits > 0);
        AUREA_CHECK_EQ(result.code(), cancel ? Errc::Cancelled : Errc::Timeout);
        AUREA_CHECK_EQ(f.backend.framesSubmitted, 1u); AUREA_CHECK(!f.backend.frameOpen);
        AUREA_CHECK(f.renderer.take_incomplete()); AUREA_CHECK(!pending.empty());
        AUREA_CHECK_EQ(f.backend.texturesDestroyed, destroyed);
        for (const auto& item : pending) item.first(item.second); pending.clear();
        AUREA_CHECK(f.backend.texturesDestroyed > destroyed);
        f.backend.beforeDeferUntilGpuDone = {}; f.backend.beforeWaitFrame = {}; f.backend.destroy_texture(*texture);
    }
}

AUREA_TEST(SceneTargets, ColdResourcesRemainIncompleteUntilTheSameSceneCanRender) {
    RenderFixture f;
    f.backend.mapBuffers = true;
    const auto asset = test_fixtures::material_override::asset();
    auto snapshot = [&] {
        FrameSnapshot result; result.compWidth = 320; result.compHeight = 180;
        test_fixtures::scene_target::add_scene(result, asset);
        test_fixtures::scene_target::add_scene(result, asset);
        return result;
    };
    TextureDesc desc; desc.width = 320; desc.height = 180; desc.renderTarget = true; desc.format = SurfaceFormat::RGBA16F;
    const auto texture = f.backend.create_texture(desc); AUREA_CHECK(texture.ok()); if (!texture) return;
    OffscreenTarget target{*texture, 320, 180};
    RenderSettings settings; settings.finalQuality = settings.stageUnblurredScenes = true;
    FrameStats stats; RenderTimings timings;
    auto cold = snapshot();
    // A failed instance allocation is a genuine incomplete scene build.
    f.backend.beforeCreateBuffer = [](const BufferDesc& buffer) {
        return buffer.debugName && std::strcmp(buffer.debugName, "3d-instancias") == 0
            ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    AUREA_CHECK(f.renderer.render(cold, settings, &target, stats, timings).ok());
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK_EQ(f.backend.framesSubmitted, 1u); AUREA_CHECK(!f.backend.frameOpen);
    f.backend.beforeCreateBuffer = {};
    auto ready = snapshot(); stats = {}; timings = {};
    AUREA_CHECK(f.renderer.render(ready, settings, &target, stats, timings).ok());
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(timings.sceneExposureBatches, 2u);
    AUREA_CHECK_EQ(f.backend.framesSubmitted, 4u); AUREA_CHECK(!f.backend.frameOpen);
    f.backend.destroy_texture(*texture);
}

AUREA_TEST(SceneTargets, PartialGraphPressureWaitsBeforeReclaimAndUsesTheSameTarget) {
    RenderFixture f;
    const auto id = f.solid("unchanged pressure source", Vec4{.3f, .6f, .9f, .65f});
    f.comp->layer(id)->shape.bounds = Rect{0, 0, 1024, 1024};
    // A plain rectangle is optimized to a 1 x 1 clear. Use an actual
    // rasterized ellipse plus its unchanged full-density blur so the first
    // source fits and the next distinct physical target causes partial OOM.
    f.comp->layer(id)->shape.shapeType = 1;
    auto blur = make_effect(f.effects, effect_keys::kGaussianBlur, 0);
    blur.params[0].constant.v[0] = 3;
    f.comp->layer(id)->effects.push_back(std::move(blur));
    u64 known = 24ull << 20;
    u64 retainedReservation = 0;
    auto memory = [&] {
        GpuMemoryStats result;
        for (usize i = 0; i < f.backend.textures.size(); ++i) if (f.backend.textureAlive[i])
            result.usedBytes += f.backend.textures[i].estimated_bytes();
        result.reservedBytes = std::max(result.usedBytes, retainedReservation);
        return result;
    };
    f.backend.queryMemoryStats = memory;
    f.renderer.set_transient_cache_budget(256ull << 20);
    // Available GPU backing is 16 MiB after the 24 MiB known residency.
    // The 4 MiB glyph atlas + 460,800 B target + 8 MiB ellipse fit; a
    // second full-density blur target exceeds this fixed 40 MiB envelope.
    f.renderer.set_tracked_resource_budget(40ull << 20,
        [](void* p) noexcept -> u64 { return *static_cast<u64*>(p); }, &known);
    TextureDesc desc; desc.width = 320; desc.height = 180; desc.format = SurfaceFormat::RGBA16F;
    desc.renderTarget = desc.sampled = true;
    const auto targetTexture = f.backend.create_texture(desc); AUREA_CHECK(targetTexture.ok()); if (!targetTexture) return;
    OffscreenTarget target{*targetTexture, desc.width, desc.height};
    RenderSettings settings; settings.finalQuality = true; settings.dither = false;
    FrameSnapshot snapshot;
    f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, nullptr, nullptr, nullptr, settings, 1, 0, DecodeMode::Still, 1, snapshot);
    FrameStats stats; RenderTimings timings;
    AUREA_CHECK_EQ(f.renderer.render(snapshot, settings, &target, stats, timings).code(), Errc::OutOfDeviceMemory);
    const u64 partial = f.renderer.pool_stats().bytes;
    AUREA_CHECK(partial > 0); AUREA_CHECK_EQ(f.renderer.pool_stats().inUse, 0u);
    AUREA_CHECK_EQ(f.backend.framesSubmitted, 1u);
    const u32 created = f.backend.texturesCreated, destroyed = f.backend.texturesDestroyed;
    const auto before = memory();
    bool complete = false;
    f.backend.beforeWaitFrame = [&](u64 frame, u64 timeout) {
        AUREA_CHECK_EQ(frame, 1u); AUREA_CHECK(timeout <= 5'000'000);
        return complete ? OkStatus : Status{Errc::Timeout};
    };
    AUREA_CHECK_EQ(f.renderer.reclaim_failed_transients(5'000'000).code(), Errc::Timeout);
    AUREA_CHECK_EQ(f.renderer.pool_stats().bytes, partial);
    AUREA_CHECK_EQ(memory().usedBytes, before.usedBytes);
    known = 0; // The optional backing was actually released, as in the engine.
    AUREA_CHECK_EQ(f.renderer.render(snapshot, settings, &target, stats, timings).code(), Errc::Timeout);
    AUREA_CHECK_EQ(f.backend.texturesCreated, created); AUREA_CHECK_EQ(f.backend.texturesDestroyed, destroyed);
    AUREA_CHECK_EQ(f.backend.framesSubmitted, 1u);
    complete = true;
    retainedReservation = 64ull << 20; // Delayed driver blocks never create admission credit.
    AUREA_CHECK(f.renderer.reclaim_failed_transients(5'000'000).ok());
    AUREA_CHECK_EQ(f.renderer.pool_stats().bytes, 0ull);
    AUREA_CHECK_EQ(before.usedBytes - memory().usedBytes, partial);
    AUREA_CHECK(f.backend.textureAlive[targetTexture->id - 1]);
    f.backend.beforeWaitFrame = {};
    AUREA_CHECK_EQ(f.renderer.render(snapshot, settings, &target, stats, timings).code(), Errc::OutOfDeviceMemory);
    retainedReservation = 0;
    AUREA_CHECK(f.renderer.reclaim_failed_transients().ok());
    stats = {}; timings = {};
    AUREA_CHECK(f.renderer.render(snapshot, settings, &target, stats, timings).ok());
    AUREA_CHECK(!f.renderer.take_incomplete()); AUREA_CHECK(stats.passesExecuted > 0);
    AUREA_CHECK_EQ(snapshot.compWidth, 1920u); AUREA_CHECK_EQ(snapshot.compHeight, 1080u);
    AUREA_CHECK_EQ(snapshot.layers[0].opacity, 1.f);
    AUREA_CHECK(f.backend.textureAlive[targetTexture->id - 1]);
    f.backend.queryMemoryStats = {}; f.backend.destroy_texture(*targetTexture);
}

AUREA_TEST(SceneTargets, TrackedAdmissionCanFailWithIdleTargetsBelowTheInternalRetentionBudget) {
    MockBackend backend; TransientTexturePool pool;
    backend.queryMemoryStats = [&] {
        GpuMemoryStats result;
        for (usize i = 0; i < backend.textures.size(); ++i) if (backend.textureAlive[i])
            result.usedBytes += backend.textures[i].estimated_bytes();
        result.reservedBytes = result.usedBytes; return result;
    };
    pool.set_budget(256ull << 20); pool.begin_frame(backend, 1);
    TextureDesc large; large.width = large.height = 1024; large.format = SurfaceFormat::RGBA16F;
    large.renderTarget = large.sampled = true;
    const auto idle = pool.acquire(large); AUREA_CHECK(idle.valid()); pool.release(idle);
    pool.set_tracked_resource_budget(10ull << 20,
        [](void*) noexcept -> u64 { return 2ull << 20; });
    pool.begin_frame(backend, 2);
    TextureDesc small = large; small.width = small.height = 512;
    AUREA_CHECK(!pool.acquire(small).valid());
    AUREA_CHECK_EQ(pool.stats().bytes, 8ull << 20); // trim_for's 256 MiB did not request eviction.
    AUREA_CHECK_EQ(pool.trim_unreferenced(0), 0u);
    AUREA_CHECK_EQ(pool.trim_unreferenced(1), 1u);
    const auto admitted = pool.acquire(small); AUREA_CHECK(admitted.valid());
    pool.release(admitted); pool.clear(); backend.queryMemoryStats = {};
}

AUREA_TEST(SceneTargets, CompletedPreviewBackingIsRetiredBeforeTheFirstExactShadowStage) {
    RenderFixture f; f.backend.mapBuffers = true;
    const auto id = f.solid("old preview ellipse", Vec4{.3f, .6f, .9f, 1});
    auto* layer = f.comp->layer(id);
    layer->shape.shapeType = 1; layer->shape.bounds = Rect{0, 0, 1024, 1024};
    auto blur = make_effect(f.effects, effect_keys::kGaussianBlur, 0);
    blur.params[0].constant.v[0] = 3; layer->effects.push_back(std::move(blur));
    TextureDesc desc; desc.width = 320; desc.height = 180;
    desc.format = SurfaceFormat::RGBA16F; desc.renderTarget = desc.sampled = true;
    const auto texture = f.backend.create_texture(desc); AUREA_CHECK(texture.ok()); if (!texture) return;
    OffscreenTarget target{*texture, 320, 180}; FrameStats stats; RenderTimings timings;
    RenderSettings preview; preview.dither = false;
    FrameSnapshot prior;
    f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, nullptr, nullptr, nullptr, preview, 1, 0, DecodeMode::Still, 1, prior);
    AUREA_CHECK(f.renderer.render(prior, preview, &target, stats, timings).ok());
    AUREA_CHECK(f.renderer.pool_stats().bytes > 0);
    std::vector<TextureHandle> priorTargets;
    for (usize i = 0; i < f.backend.textures.size(); ++i) {
        const auto& d = f.backend.textures[i];
        if (f.backend.textureAlive[i] && d.width == 1024 && d.height == 1024)
            priorTargets.push_back(TextureHandle{i + 1});
    }
    AUREA_CHECK(!priorTargets.empty());
    // Model a driver that collects empty reserved blocks at begin-frame.
    // A pool eviction after begin cannot make this physical reservation vanish.
    u64 retained = 128ull << 20; bool collected = false;
    f.backend.beforeBeginOffscreenFrame = [&] {
        bool live = false;
        for (const auto h : priorTargets) live = live || f.backend.textureAlive[h.id - 1];
        if (!live) { retained = 0; collected = true; }
    };
    f.backend.queryMemoryStats = [&] {
        GpuMemoryStats memory;
        for (usize i = 0; i < f.backend.textures.size(); ++i) if (f.backend.textureAlive[i])
            memory.usedBytes += f.backend.textures[i].estimated_bytes();
        memory.reservedBytes = std::max(memory.usedBytes, retained); return memory;
    };
    f.renderer.set_tracked_resource_budget(96ull << 20);
    FrameSnapshot exact; exact.compWidth = 1920; exact.compHeight = 1080;
    test_fixtures::scene_target::add_scene(exact, test_fixtures::material_override::asset());
    RenderSettings settings; settings.dither = false; settings.finalQuality = settings.stageUnblurredScenes = true;
    stats = {}; timings = {};
    AUREA_CHECK(f.renderer.render(exact, settings, &target, stats, timings).ok());
    AUREA_CHECK(collected); AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(f.renderer.heavy_stats().lastShadowMapSize, 4096u);
    AUREA_CHECK_EQ(timings.sceneExposureBatches, 1u);
    AUREA_CHECK(f.backend.textureAlive[texture->id - 1]);
    f.backend.beforeBeginOffscreenFrame = {}; f.backend.queryMemoryStats = {};
    f.backend.destroy_texture(*texture);
}
