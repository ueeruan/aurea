// Testes da fachada: o contrato que a bridge JNI e a ObjC++ enxergam.
//
// Rodam headless — sem janela e sem GPU. Isso é deliberado: a timeline, a
// animação, os comandos e a serialização precisam funcionar sem backend
// gráfico, e é o que permite testá-los no CI.
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"
#include "MockBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/scene3d/Text3D.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <filesystem>
#include <chrono>
#include <future>
#include <limits>

using namespace aurea;

namespace {

EngineConfig headless_config() {
    EngineConfig cfg;
    // Sem backend: config.backend nulo. Timeline, comandos e serialização
    // funcionam sem GPU.
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

} // namespace

#include "ProjectLifecycle.inl"
#include "CaptureResources.inl"
#include "PreviewIdle.inl"
#include "Beta008Scenarios.inl"
#include "ContentBounds.inl"
#include "PropertyCoverageTests.inl"
#include "ShapePuppetControls.inl"

AUREA_TEST(TextOutline, NonTextTargetPreservesAnchorAnimationAndProjectState) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30., "outline target").ok());
    const auto id = e.add_shape(0);
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    auto* layer = comp->layer(LayerId::unpack(*id));
    layer->transform.anchor = {21, 34, 8};
    layer->text.strokeWidth = 3;
    Track track; track.property = TrackProperty::AnchorX; track.staticValue = 21;
    Keyframe key; key.time = FrameIndex{7}; key.value = 45;
    key.interp = Interpolation::Bezier; key.tangentIn = -2; key.tangentOut = 5;
    track.keys.push_back(key); layer->tracks.add(std::move(track));
    e.project()->mark_clean();
    const auto history = e.history().depth();
    const auto generation = e.project()->edit_generation();
    const auto revision = e.project()->revision();
    Command command; command.type = CommandType::TextSetStrokeWidth;
    command.text_stroke_width.layer = LayerId::unpack(*id);
    command.text_stroke_width.width = 18;
    AUREA_CHECK(e.apply_command(command).code() == Errc::InvalidArgument);
    AUREA_CHECK_EQ(layer->transform.anchor.x, 21.f);
    AUREA_CHECK_EQ(layer->transform.anchor.y, 34.f);
    AUREA_CHECK_EQ(layer->transform.anchor.z, 8.f);
    AUREA_CHECK_EQ(layer->text.strokeWidth, 3.f);
    const auto* unchanged = layer->tracks.find(TrackProperty::AnchorX);
    AUREA_CHECK(unchanged != nullptr); if (!unchanged) return;
    AUREA_CHECK_EQ(unchanged->staticValue, 21.f);
    AUREA_CHECK_EQ(unchanged->keys.size(), 1u);
    AUREA_CHECK_EQ(unchanged->keys[0].time.value, 7ll);
    AUREA_CHECK_EQ(unchanged->keys[0].value, 45.f);
    AUREA_CHECK(unchanged->keys[0].interp == Interpolation::Bezier);
    AUREA_CHECK_EQ(unchanged->keys[0].tangentIn, -2.f);
    AUREA_CHECK_EQ(unchanged->keys[0].tangentOut, 5.f);
    AUREA_CHECK_EQ(e.history().depth(), history);
    AUREA_CHECK_EQ(e.project()->edit_generation(), generation);
    AUREA_CHECK_EQ(e.project()->revision(), revision);
    AUREA_CHECK(!e.project()->dirty());
}

AUREA_TEST(MotionBlur, AtomicSettingsValidatePreserveUndoAndKeepPhaseIndependent) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(128, 128, 30., "shutter").ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    comp->motion_blur().vectorBlur = true;
    comp->motion_blur().previewSamples = 9;
    MotionBlurSettings state;
    AUREA_CHECK(e.query_motion_blur_settings(state));
    AUREA_CHECK_EQ(state.samples, 16u);
    const auto depth = e.history().depth();
    AUREA_CHECK(e.set_motion_blur_settings(true, 270, -40, 32, 160));
    AUREA_CHECK_EQ(e.history().depth(), depth + 1);
    AUREA_CHECK(e.set_motion_blur_settings(true, 270, -40, 32, 160));
    AUREA_CHECK_EQ(e.history().depth(), depth + 1);
    for (const f32 bad : {std::numeric_limits<f32>::quiet_NaN(), std::numeric_limits<f32>::infinity()}) {
        AUREA_CHECK(!e.set_motion_blur_settings(true, bad, -40, 32, 160));
        AUREA_CHECK(!e.set_motion_blur_settings(true, 270, bad, 32, 160));
        AUREA_CHECK(!e.set_shutter_angle(bad));
    }
    AUREA_CHECK(!e.set_motion_blur_settings(true, 270, -40, 0, 160));
    AUREA_CHECK(!e.set_motion_blur_settings(true, 270, -40, 65, 160));
    AUREA_CHECK(!e.set_motion_blur_settings(true, 270, -40, 32, 16));
    AUREA_CHECK(!e.set_motion_blur_settings(true, 270, -40, 32, 257));
    AUREA_CHECK_EQ(e.history().depth(), depth + 1);
    AUREA_CHECK(e.query_motion_blur_settings(state));
    AUREA_CHECK_EQ(state.shutterPhase, -40.f);
    AUREA_CHECK(state.vectorBlur);
    AUREA_CHECK_EQ(state.previewSamples, 9u);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.query_motion_blur_settings(state));
    AUREA_CHECK(!state.enabled);
    AUREA_CHECK_EQ(state.shutterAngle, 180.f);
    AUREA_CHECK_EQ(state.shutterPhase, -90.f);
    AUREA_CHECK_EQ(state.samples, 16u);
    AUREA_CHECK_EQ(state.adaptiveLimit, 128u);
    AUREA_CHECK(e.set_motion_blur_settings(true, 900, -900, 64, 256));
    AUREA_CHECK(e.set_shutter_angle(90));
    AUREA_CHECK(e.set_composition_motion_blur(false));
    AUREA_CHECK(e.query_motion_blur_settings(state));
    AUREA_CHECK_EQ(state.shutterAngle, 90.f);
    AUREA_CHECK_EQ(state.shutterPhase, -360.f);
    AUREA_CHECK_EQ(state.samples, 64u);
    AUREA_CHECK_EQ(state.adaptiveLimit, 256u);
    AUREA_CHECK(state.vectorBlur);
}

AUREA_TEST(ImportStability, AggregateImageBudgetRejectsBeforeHistoryAndPreservesUndoSources) {
    Engine e; auto cfg = headless_config(); cfg.memoryBudgetBytes = 3 * 100 * 1024;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "quota").ok());
    std::vector<u8> pixels(128 * 128 * 4, 255);
    const auto first = e.import_image(pixels.data(), 128, 128, "one");
    AUREA_CHECK(first.ok());
    const u32 depth = e.history().depth(), assets = e.project()->asset_count();
    const auto second = e.import_image(pixels.data(), 128, 128, "two");
    AUREA_CHECK(!second.ok() && second.status().code() == Errc::BudgetExceeded);
    AUREA_CHECK_EQ(e.history().depth(), depth);
    AUREA_CHECK_EQ(e.project()->asset_count(), assets);
    const auto impossible = e.import_image(pixels.data(), ~0u, ~0u, "overflow");
    AUREA_CHECK(!impossible.ok() && impossible.status().code() == Errc::BudgetExceeded);
    if (first.ok()) {
        const auto replace = e.replace_layer_image(*first, pixels.data(), 128, 128, "replace", nullptr);
        AUREA_CHECK(!replace.ok() && replace.status().code() == Errc::BudgetExceeded);
        AUREA_CHECK_EQ(e.history().depth(), depth);
    }
    AUREA_CHECK(e.new_project(64, 64, 30., "new").ok());
    AUREA_CHECK(e.import_image(pixels.data(), 128, 128, "fits again").ok());
}

AUREA_TEST(ImportStability, ReopenBoundsAggregateImagesSkipsOrphansAndRejectsStaleLoader) {
    const auto file = std::filesystem::temp_directory_path() / "aurea-image-source-quota.aurea";
    auto cfg = headless_config();
    std::vector<u8> pixels(128 * 128 * 4, 255);
    {
        Engine writer; AUREA_CHECK(writer.initialize(cfg).ok()); AUREA_CHECK(writer.new_project(64, 64, 30., "sources").ok());
        AUREA_CHECK(writer.import_image(pixels.data(), 128, 128, "one", "one.png").ok());
        AUREA_CHECK(writer.import_image(pixels.data(), 128, 128, "two", "two.png").ok());
        Asset orphan; orphan.kind = AssetKind::Image; orphan.sourcePath = "unused.png";
        (void)writer.project()->add_asset(std::move(orphan));
        AUREA_CHECK(writer.save_project(file.string().c_str()).ok());
    }
    struct Loader { Engine* engine = nullptr; u32 calls = 0; bool replace = false; bool resize = false; } context;
    cfg.memoryBudgetBytes = 3 * 100 * 1024;
    cfg.imageLoaderContext = &context;
    cfg.imageLoader = [](const char*, ImagePixels& out, void* opaque) {
        auto& loader = *static_cast<Loader*>(opaque); ++loader.calls;
        out.width = out.height = loader.resize ? 64 : 128;
        out.rgba.assign(static_cast<usize>(out.width) * out.height * 4, 255);
        if (loader.replace) (void)loader.engine->new_project(64, 64, 30., "replacement");
        return true;
    };
    Engine reader; context.engine = &reader; AUREA_CHECK(reader.initialize(cfg).ok());
    AUREA_CHECK(reader.load_project(file.string().c_str()).ok());
    AUREA_CHECK_EQ(context.calls, 1u);
    AUREA_CHECK_EQ(reader.last_load_missing_assets(), 1u);
    context.calls = 0; context.resize = true;
    AUREA_CHECK(reader.load_project(file.string().c_str()).ok());
    AUREA_CHECK_EQ(context.calls, 2u);
    AUREA_CHECK_EQ(reader.last_load_missing_assets(), 2u);
    const auto* reopened = reader.project()->timeline().composition(reader.project()->timeline().root());
    reopened->layers().for_each([&](LayerId, const Layer& layer) {
        const auto* asset = reader.project()->asset(layer.source);
        AUREA_CHECK(asset != nullptr);
        if (asset) { AUREA_CHECK_EQ(asset->video.width, 128u); AUREA_CHECK_EQ(asset->video.height, 128u); }
        AUREA_CHECK_NEAR(layer.transform.anchor.x, 64.f, .001f);
        AUREA_CHECK_NEAR(layer.transform.anchor.y, 64.f, .001f);
    });
    context.resize = false;
    context.calls = 0; context.replace = true;
    AUREA_CHECK(reader.load_project(file.string().c_str()).code() == Errc::Cancelled);
    AUREA_CHECK_EQ(reader.project()->asset_count(), 0u);
    AUREA_CHECK(reader.import_image(pixels.data(), 128, 128, "quota clear").ok());
    std::error_code error; std::filesystem::remove(file, error);
}

AUREA_TEST(PreviewBuffer, PreparesOffscreenWithoutAdvancingPlayheadAndPauseCancels) {
    auto* mock = new aurea::test::MockBackend();
    Engine e;
    auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30.0, "buffer").ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    comp->set_duration(FrameIndex{90});
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 64, 64).ok());
    Command play; play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    bridge::EngineStatusPOD status;
    e.fill_status(status);
    AUREA_CHECK(status.previewBufferStatus & 0x80000000u);
    AUREA_CHECK_EQ(status.playing, 1u);
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(mock->acquires, 0u);
    AUREA_CHECK_EQ(e.read_status().playhead.value, 0ll);
    Command pause; pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(e.apply_command(pause).ok());
    e.fill_status(status);
    AUREA_CHECK_EQ(status.previewBufferStatus, 0u);
    AUREA_CHECK_EQ(status.playing, 0u);
    AUREA_CHECK(e.apply_command(play).ok());
    for (u32 i = 0; i < 32; ++i) {
        AUREA_CHECK(e.render_frame().ok());
        e.fill_status(status);
        if (!(status.previewBufferStatus & 0x80000000u)) break;
        AUREA_CHECK_EQ(status.playhead, 0ll);
    }
    AUREA_CHECK(!(status.previewBufferStatus & 0x80000000u));
    AUREA_CHECK_EQ(status.playing, 1u);
    AUREA_CHECK((status.previewBufferStatus & 255u) > 0);
    AUREA_CHECK(mock->offscreenFrames > 0);
    AUREA_CHECK_EQ(mock->acquires, 1u);
    e.shutdown();
}

AUREA_TEST(PreviewBuffer, ElapsedStartupDeadlineDoesNotRequireTheWholeTarget) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "slow buffer").ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 64, 64).ok());
    Command play; play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    AUREA_CHECK(e.render_frame().ok());
    // Only one frame has been prepared when the startup deadline expires.
    // The next iteration must present it instead of waiting for all six.
    std::this_thread::sleep_for(std::chrono::milliseconds(370));
    AUREA_CHECK(e.render_frame().ok());
    bridge::EngineStatusPOD status{}; e.fill_status(status);
    AUREA_CHECK_EQ(status.playing, 1u);
    AUREA_CHECK(!(status.previewBufferStatus & 0x80000000u));
    AUREA_CHECK(status.previewBufferStatus & 0x40000000u);
    AUREA_CHECK_EQ(mock->offscreenFrames, 1u);
    AUREA_CHECK_EQ(mock->acquires, 1u);
    e.shutdown();
}

AUREA_TEST(PreviewBuffer, ResourceReadyAfterRetryWindowRefreshesThePausedFrame) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "late resource").ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 64, 64).ok());
    std::vector<u8> rgba(64 * 64 * 4, 255);
    AUREA_CHECK(e.import_image(rgba.data(), 64, 64, "late image").ok());
    bool blocked = true;
    mock->beforeTextureUpload = [&](TextureHandle, const void*, u32) {
        return blocked ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    // Exhaust the immediate retries, just as a slow first neural inference can.
    for (int i = 0; i < 65; ++i) (void)e.render_frame();
    const u32 presented = mock->acquires;
    AUREA_CHECK(e.render_frame(true).ok());
    AUREA_CHECK_EQ(mock->acquires, presented);
    blocked = false;
    void (*ready)(void*) = nullptr; void* context = nullptr;
    e.media().ready_callback(ready, context);
    AUREA_CHECK(ready != nullptr);
    if (ready) ready(context);
    AUREA_CHECK(e.render_frame(true).ok());
    AUREA_CHECK(mock->acquires > presented);
    AUREA_CHECK(e.renderer().preview_cached(FrameIndex{0}));
    e.shutdown();
}

AUREA_TEST(PreviewBuffer, DetachedSurfaceDoesNotSpinPendingPlayback) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "surface").ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 64, 64).ok());
    Command play; play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    e.detach_surface();
    e.start_render_thread();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const u64 before = e.render_wakeups();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    AUREA_CHECK(e.render_wakeups() - before <= 1);
    e.stop_render_thread();
    e.shutdown();
}

AUREA_TEST(PreviewBuffer, UiRangesInvalidateBeforeRenderingAndNeverWaitForTheRenderLock) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "ranges").ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 64, 64).ok());
    AUREA_CHECK(e.render_frame().ok());
    i64 ranges[60]{};
    AUREA_CHECK_EQ(e.copy_preview_buffer_ranges(ranges, 30), 1u);
    AUREA_CHECK_EQ(ranges[0], 0ll); AUREA_CHECK_EQ(ranges[1], 1ll);
    const auto shape = e.add_shape(0);
    AUREA_CHECK(shape.ok());
    // The old cached texture still exists, but is no longer this model revision.
    AUREA_CHECK_EQ(e.copy_preview_buffer_ranges(ranges, 30), 0u);
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(e.copy_preview_buffer_ranges(ranges, 30), 1u);
    auto& timeline = e.project()->timeline();
    const CompositionId original = timeline.current();
    const CompositionId other = timeline.create_composition("other", 64, 64, 30.);
    AUREA_CHECK(timeline.set_current(other));
    AUREA_CHECK_EQ(e.copy_preview_buffer_ranges(ranges, 30), 0u);
    AUREA_CHECK(timeline.set_current(original));
    AUREA_CHECK_EQ(e.copy_preview_buffer_ranges(ranges, 30), 1u);

    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto releaseFuture = release.get_future();
    mock->beforeBeginFrame = [&] { entered.set_value(); releaseFuture.wait(); };
    auto rendering = std::async(std::launch::async, [&] { return e.render_frame(); });
    const bool rendererBlocked = enteredFuture.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    AUREA_CHECK(rendererBlocked);
    auto query = std::async(std::launch::async, [&] { return e.copy_preview_buffer_ranges(ranges, 30); });
    const bool queryReady = query.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    release.set_value(); // always release before asserting/joining, including failures
    AUREA_CHECK(queryReady);
    AUREA_CHECK_EQ(query.get(), 1u);
    AUREA_CHECK(rendering.get().ok());
    mock->beforeBeginFrame = {};
    e.shutdown();
    AUREA_CHECK_EQ(e.copy_preview_buffer_ranges(ranges, 30), 0u);
}

AUREA_TEST(Engine, DuplicateSelectsOnlyCopiesAndMovingThemPreservesOriginals) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 240, 30.0, nullptr).ok());
    auto* project = e.project();
    auto* comp = project->timeline().composition(project->timeline().root());
    for (const auto kind : {LayerKind::Text, LayerKind::Shape, LayerKind::Model3D}) {
        const auto original = comp->add_layer(kind, "Original");
        comp->layer(original)->transform.position = {80, 120, 30};
        comp->layer(original)->tracks.get_or_create(TrackProperty::PositionY).set(FrameIndex{0}, 120);
        const u64 selected = original.pack();
        e.set_selection(&selected, 1);
        Command duplicate{};
        duplicate.type = CommandType::LayerDuplicate;
        duplicate.layer_ref.layer = original;
        AUREA_CHECK(e.apply_command(duplicate).ok());
        u64 created = 0;
        AUREA_CHECK_EQ(e.get_selection(&created, 1), 1u);
        AUREA_CHECK(created != selected);
        AUREA_CHECK(!e.is_selected(selected));
        Command move{};
        move.type = CommandType::LayerLayoutTransform;
        move.shape_param = {LayerId::unpack(created), 1, 40};
        AUREA_CHECK(e.apply_command(move).ok());
        AUREA_CHECK_NEAR(comp->layer(original)->transform.position.y, 120.f, 0.001f);
        AUREA_CHECK_NEAR(comp->layer(original)->tracks.sample_or(TrackProperty::PositionY, FrameIndex{0}, 0), 120.f, 0.001f);
        AUREA_CHECK_NEAR(comp->layer(LayerId::unpack(created))->tracks.sample_or(TrackProperty::PositionY, FrameIndex{0}, 0), 40.f, 0.001f);
    }
    e.shutdown();
}

AUREA_TEST(Engine, SurfaceResizeDoesNotWaitForABlockedRenderAndUsesLatestSize) {
    auto* mock = new aurea::test::MockBackend();
    Engine e;
    auto cfg = headless_config();
    cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(320, 240, 30.0, nullptr).ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 320, 240).ok());
    std::promise<void> rendering, release;
    auto entered = rendering.get_future();
    auto unblock = release.get_future();
    mock->beforeBeginFrame = [&] { rendering.set_value(); unblock.wait(); };
    auto render = std::async(std::launch::async, [&] { return e.render_frame(); });
    const bool started = entered.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    AUREA_CHECK(started);
    auto resize = std::async(std::launch::async, [&] {
        (void)e.resize_surface(480, 320);
        return e.resize_surface(720, 480);
    });
    const bool responsive = resize.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
    release.set_value(); // Always unblock, including when the regression fails.
    AUREA_CHECK(render.get().ok());
    AUREA_CHECK(resize.get().ok());
    AUREA_CHECK(responsive);
    mock->beforeBeginFrame = {};
    AUREA_CHECK(e.render_frame(true).ok());
    AUREA_CHECK_EQ(mock->surfaceWidth, 720u);
    AUREA_CHECK_EQ(mock->surfaceHeight, 480u);
    AUREA_CHECK(!e.resize_surface(0, 480).ok());
    // A queued resize from the old view must not resize a replacement view.
    AUREA_CHECK(e.resize_surface(900, 600).ok());
    e.detach_surface();
    AUREA_CHECK(e.attach_surface(&window, 720, 480).ok());
    AUREA_CHECK(e.render_frame(true).ok());
    AUREA_CHECK_EQ(mock->surfaceWidth, 720u);
    AUREA_CHECK_EQ(mock->surfaceHeight, 480u);
    e.shutdown();
}

AUREA_TEST(Engine, MemoryTrimPublishesDrainedGpuCountersWithoutRedrawing) {
    auto* mock = new aurea::test::MockBackend();
    Engine e;
    auto cfg = headless_config(); cfg.backend = mock;
    auto textureMemory = [&] {
        GpuMemoryStats result;
        for (usize i = 0; i < mock->textures.size(); ++i) {
            if (!mock->textureAlive[i]) continue;
            result.usedBytes += mock->textures[i].estimated_bytes();
            ++result.allocationCount;
        }
        // Model allocator reservation separately from used bytes.
        result.reservedBytes = ((result.usedBytes + 4095) / 4096) * 4096;
        return result;
    };
    bool deferAccounting = false;
    GpuMemoryStats drained;
    mock->queryMemoryStats = [&] { return deferAccounting ? drained : textureMemory(); };
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(320, 240, 30., "memory accounting").ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 320, 240).ok());
    AUREA_CHECK(e.add_shape(0).ok());
    AUREA_CHECK(e.render_frame(true).ok());
    bridge::PerfPOD before{}, after{};
    e.fill_perf(before);
    AUREA_CHECK(before.gpuMemoryBytes > 0);
    AUREA_CHECK(before.gpuAllocations > 0);
    AUREA_CHECK(before.passesExecuted > 0);
    AUREA_CHECK(before.physicalTextures > 0);
    const u32 submissions = mock->framesSubmitted;
    const u32 idleWaits = mock->idleWaits.load();
    drained = textureMemory();
    deferAccounting = true;
    // Resource retirement is visible only after the GPU drain, as on Vulkan.
    mock->beforeWaitIdle = [&] { drained = textureMemory(); };
    (void)e.trim_memory(80);
    e.fill_perf(after); // No new frame or UI/GPU synchronization is required.
    const auto actual = mock->memory_stats();
    AUREA_CHECK(mock->idleWaits.load() >= idleWaits + 2);
    AUREA_CHECK_EQ(mock->framesSubmitted, submissions);
    AUREA_CHECK(actual.usedBytes < before.gpuMemoryBytes);
    AUREA_CHECK(actual.allocationCount < before.gpuAllocations);
    AUREA_CHECK_EQ(after.gpuMemoryBytes, actual.usedBytes);
    AUREA_CHECK_EQ(after.gpuReservedBytes, actual.reservedBytes);
    AUREA_CHECK_EQ(after.gpuAllocations, actual.allocationCount);
    before.gpuMemoryBytes = after.gpuMemoryBytes;
    before.gpuReservedBytes = after.gpuReservedBytes;
    before.gpuAllocations = after.gpuAllocations;
    AUREA_CHECK(std::memcmp(&before, &after, sizeof(before)) == 0);
    mock->beforeWaitIdle = {};
    mock->queryMemoryStats = {};
    e.shutdown();
}

AUREA_TEST(Engine, RunningLowReleasesPreviewCacheToTheDriverAndHoldsRefill) {
    // Galaxy A15/A16 (LOW_MEMORY em primeiro plano): RUNNING_LOW (10) e
    // UI_HIDDEN (20) soltam a prévia guardada da GPU na hora — com wait_idle,
    // que no Vulkan devolve ao driver os blocos vazios (vkFreeMemory) — e o
    // render não reenche durante kPreviewPressureHoldNs.
    auto* mock = new aurea::test::MockBackend();
    Engine e;
    auto cfg = headless_config(); cfg.backend = mock;
    auto textureMemory = [&] {
        GpuMemoryStats result;
        for (usize i = 0; i < mock->textures.size(); ++i) {
            if (!mock->textureAlive[i]) continue;
            result.usedBytes += mock->textures[i].estimated_bytes();
            ++result.allocationCount;
        }
        result.reservedBytes = ((result.usedBytes + 4095) / 4096) * 4096;
        return result;
    };
    mock->queryMemoryStats = textureMemory;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "pressure").ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 64, 64).ok());
    std::vector<u8> rgba(64 * 64 * 4, 200);
    AUREA_CHECK(e.import_image(rgba.data(), 64, 64, "pressure image").ok());
    AUREA_CHECK(e.render_frame(true).ok());
    AUREA_CHECK(e.renderer().preview_cached(FrameIndex{0}));
    AUREA_CHECK(!e.preview_memory_hold());
    const u32 cached = e.renderer().preview_cached_count();
    AUREA_CHECK(cached > 0u);
    const GpuMemoryStats before = textureMemory();
    const u32 idleWaits = mock->idleWaits.load();
    const u32 submissions = mock->framesSubmitted;

    // RUNNING_MODERATE (5) não toca a prévia.
    (void)e.trim_memory(5);
    AUREA_CHECK_EQ(e.renderer().preview_cached_count(), cached);
    AUREA_CHECK(!e.preview_memory_hold());

    const auto report = e.trim_memory(10);
    AUREA_CHECK_EQ(report.upTo, TrimStage::UnusedDecodedFrames);
    AUREA_CHECK_EQ(e.renderer().preview_cached_count(), 0u);
    AUREA_CHECK(!e.renderer().preview_cached(FrameIndex{0}));
    AUREA_CHECK(e.preview_memory_hold());
    AUREA_CHECK(mock->idleWaits.load() > idleWaits);       // destruições adiadas drenadas
    AUREA_CHECK_EQ(mock->framesSubmitted, submissions);    // sem quadro novo
    const GpuMemoryStats after = textureMemory();
    AUREA_CHECK(after.usedBytes < before.usedBytes);
    AUREA_CHECK(after.reservedBytes < before.reservedBytes);
    AUREA_CHECK(after.allocationCount < before.allocationCount);
    AUREA_CHECK(e.memory().last_trim().freed[static_cast<u8>(TrimStage::UnusedDecodedFrames)]
                >= before.usedBytes - after.usedBytes);
    bridge::PerfPOD perf{};
    e.fill_perf(perf);
    AUREA_CHECK_EQ(perf.gpuMemoryBytes, after.usedBytes);

    // Durante a pausa o quadro na tela continua sendo desenhado, mas nada
    // volta para a prévia guardada.
    AUREA_CHECK(e.render_frame(true).ok());
    AUREA_CHECK_EQ(e.renderer().preview_cached_count(), 0u);
    AUREA_CHECK(e.preview_memory_hold());

    // UI_HIDDEN (20) é o mesmo estágio: solta e renova a pausa.
    const auto hidden = e.trim_memory(20);
    AUREA_CHECK_EQ(hidden.upTo, TrimStage::UnusedDecodedFrames);
    AUREA_CHECK_EQ(e.renderer().preview_cached_count(), 0u);
    mock->queryMemoryStats = {};
    e.shutdown();
}

AUREA_TEST(Engine, MemoryWarningReleasesHiddenImageUploadsAndRebuildsThem) {
    auto* mock = new aurea::test::MockBackend();
    Engine e;
    auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(320, 240, 30.0, nullptr).ok());
    int window = 0;
    AUREA_CHECK(e.attach_surface(&window, 320, 240).ok());
    std::vector<u8> pixels(96 * 64 * 4, 255);
    const auto imported = e.import_image(pixels.data(), 96, 64, "Image");
    AUREA_CHECK(imported.ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    comp->layer(LayerId::unpack(*imported))->end = FrameIndex{2};
    auto imageUploads = [&] {
        u32 alive = 0;
        for (usize i = 0; i < mock->textures.size(); ++i) {
            const auto& d = mock->textures[i];
            if (mock->textureAlive[i] && d.width == 96 && d.height == 64 && d.format == SurfaceFormat::RGBA8) ++alive;
        }
        return alive;
    };
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(imageUploads(), 1u);
    (void)e.trim_memory(15);
    AUREA_CHECK_EQ(imageUploads(), 1u); // The visible image stays available.
    Command seek{}; seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK(e.render_frame().ok());
    (void)e.trim_memory(15);
    AUREA_CHECK_EQ(imageUploads(), 0u);
    AUREA_CHECK_EQ(comp->order().size(), 1u); // Only cache was discarded.
    seek.seek.time = tick_at(FrameIndex{0}, 30.0);
    AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(imageUploads(), 1u);
    e.shutdown();
}

AUREA_TEST(Engine, InitializeHeadlessSucceeds) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK_EQ(e.state(), EngineState::Ready);
    e.shutdown();
    AUREA_CHECK_EQ(e.state(), EngineState::Uninitialized);
}

AUREA_TEST(Engine, AndroidManualEditingFixtureResolvesPortableAssets) {
    Engine e;
    auto config=headless_config();
    config.documentsDirectory=std::string(AUREA_TEST_DATA_DIR)+"/manual-editing";
    AUREA_CHECK(e.initialize(config).ok());
    const auto path=config.documentsDirectory+"/manual-editing.aurea";
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(e.last_load_missing_assets(),0u);
    auto* comp=e.project()?e.project()->timeline().composition(e.project()->timeline().root()):nullptr;
    AUREA_CHECK(comp!=nullptr);if(!comp)return;
    AUREA_CHECK_EQ(comp->layers().count(),14u);
    u32 videos=0,effects=0,parents=0;
    comp->layers().for_each([&](LayerId,const Layer& layer) {
        videos+=layer.kind==LayerKind::Video;
        effects+=static_cast<u32>(layer.effects.size());
        parents+=layer.parent.valid();
    });
    AUREA_CHECK_EQ(videos,9u);
    AUREA_CHECK_EQ(effects,6u);
    AUREA_CHECK_EQ(parents,2u);
}

AUREA_TEST(Engine, DoubleInitializeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(!e.initialize(headless_config()).ok());
    e.shutdown();
}

AUREA_TEST(Engine, NewProjectCreatesComposition) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 60.0, "Meu projeto").ok());

    const Project* p = e.project();
    AUREA_CHECK(p != nullptr);
    AUREA_CHECK(p->timeline().root().valid());
    AUREA_CHECK(p->timeline().current().valid());

    const Composition* c = p->timeline().composition(p->timeline().root());
    AUREA_CHECK(c != nullptr);
    AUREA_CHECK_EQ(c->width(), static_cast<u32>(1920));
    AUREA_CHECK_EQ(c->height(), static_cast<u32>(1080));
    AUREA_CHECK_NEAR(c->fps(), 60.0, 1e-9);
    e.shutdown();
}

AUREA_TEST(Engine, KeyframeEasingQueryKeepsTrackAddressAndLocalTime) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    const auto added = e.add_shape(0);
    AUREA_CHECK(added.ok());
    const auto layer = LayerId::unpack(*added);
    Command insert;
    insert.type = CommandType::KeyframeInsert;
    insert.keyframe.track = TrackRef{layer, TrackProperty::ShapeParam, kInvalidIndex, 5};
    insert.keyframe.time = FrameIndex{18};
    insert.keyframe.value = 320;
    AUREA_CHECK(e.apply_command(insert).ok());
    Command easing;
    easing.type = CommandType::KeyframeSetInterpolation;
    easing.keyframe_interp.track = insert.keyframe.track;
    easing.keyframe_interp.time = FrameIndex{18};
    easing.keyframe_interp.interp = Interpolation::Bezier;
    easing.keyframe_interp.bx1 = 0.2f; easing.keyframe_interp.by1 = -0.3f;
    easing.keyframe_interp.bx2 = 0.8f; easing.keyframe_interp.by2 = 1.4f;
    AUREA_CHECK(e.apply_command(easing).ok());
    float values[4]{};
    AUREA_CHECK(e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 5, 18, values));
    AUREA_CHECK_NEAR(values[0], 0.2f, 0.0001f);
    AUREA_CHECK_NEAR(values[1], -0.3f, 0.0001f);
    AUREA_CHECK_NEAR(values[2], 0.8f, 0.0001f);
    AUREA_CHECK_NEAR(values[3], 1.4f, 0.0001f);
    AUREA_CHECK(!e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 4, 18, values));
    AUREA_CHECK(!e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 5, 19, values));
    AUREA_CHECK(!e.query_keyframe_easing(*added, 999, kInvalidIndex, 5, 18, values));
    AUREA_CHECK(!e.query_keyframe_easing(*added, 0, kInvalidIndex, 0, 18, nullptr));
    insert.keyframe.time = FrameIndex{48}; insert.keyframe.value = 640;
    AUREA_CHECK(e.apply_command(insert).ok());
    float samples[3]{};
    AUREA_CHECK_EQ(e.query_track_curve(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 5, 18, 48, samples, 3), 3u);
    AUREA_CHECK_NEAR(samples[0], 320, 0.001f);
    AUREA_CHECK_NEAR(samples[2], 640, 0.001f);
    AUREA_CHECK_EQ(e.query_track_curve(*added, static_cast<u32>(TrackProperty::ShapeParam), kInvalidIndex, 4, 18, 48, samples, 3), 0u);
    AUREA_CHECK_EQ(e.query_track_curve(*added, 999, kInvalidIndex, 5, 18, 48, samples, 3), 0u);
    e.shutdown();
}

AUREA_TEST(Engine, ObjectHdriImportPreservesSceneAndAssetIdentity) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, nullptr).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const auto object = comp->add_layer(LayerKind::Model3D, "objeto");
    const auto path = (std::filesystem::temp_directory_path() / ("aurea_object_hdri_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".hdr")).string();
    FILE* file = std::fopen(path.c_str(), "wb");
    AUREA_CHECK(file != nullptr);
    const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
    std::fwrite(header, 1, sizeof(header) - 1, file);
    const u8 pixel[4] = {128, 64, 32, 129};
    for (int n = 0; n < 4; ++n) std::fwrite(pixel, 1, 4, file);
    std::fclose(file);
    const auto scene = e.import_hdri(path.c_str());
    const auto own = e.import_hdri(path.c_str(), object.pack());
    std::remove(path.c_str());
    AUREA_CHECK(scene.ok()); AUREA_CHECK(own.ok());
    AUREA_CHECK_EQ(comp->environment().hdri.pack(), *scene);
    f32 values[5]{}; u64 asset = 0;
    AUREA_CHECK(e.query_object_environment(object.pack(), values, &asset));
    AUREA_CHECK_EQ(asset, *own); AUREA_CHECK_EQ(values[0], 1.f);
    const u64 precise = (1ull << 40) | 123456789ull;
    AUREA_CHECK(e.set_object_environment(object.pack(), 1, precise, 1, 0, 1));
    AUREA_CHECK(e.query_object_environment(object.pack(), values, &asset));
    AUREA_CHECK_EQ(asset, precise);
    e.shutdown();
}

namespace {
struct HdriPathFixture {
    std::filesystem::path root;
    bool owns = false;
    HdriPathFixture() {
        const auto temporary = std::filesystem::temp_directory_path();
        root = temporary / ("aurea_hdri_portability_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code error;
        owns = std::filesystem::create_directory(root, error);
    }
    ~HdriPathFixture() {
        // Delete only the unique directory this fixture actually created.
        std::error_code error;
        if (owns && root.is_absolute() && root.parent_path() == std::filesystem::temp_directory_path(error)
            && root.filename().string().rfind("aurea_hdri_portability_", 0) == 0)
            std::filesystem::remove_all(root, error);
    }
    static std::string utf8(const std::filesystem::path& path) {
        const auto s = path.generic_u8string();
        return {reinterpret_cast<const char*>(s.data()), s.size()};
    }
    static bool write(const std::filesystem::path& path) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        FILE* f = fileio::open_file(utf8(path), "wb");
        if (!f) return false;
        const char header[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
        const u8 pixel[4] = {128, 32, 16, 129};
        bool ok = std::fwrite(header, 1, sizeof(header) - 1, f) == sizeof(header) - 1;
        for (int n = 0; n < 4; ++n) ok = std::fwrite(pixel, 1, 4, f) == 4 && ok;
        return std::fclose(f) == 0 && ok;
    }
};
} // namespace

AUREA_TEST(Engine, HdriInternalPathSurvivesSaveAndRelocatedDocuments) {
    HdriPathFixture fixture;
    AUREA_CHECK(fixture.owns);
    const auto oldDocs = fixture.root / "old" / "Documents";
    const auto newDocs = fixture.root / "new" / "Documents";
    const auto media = oldDocs / "modelos" / "environment.hdr";
    const auto companion = newDocs / "modelos" / "environment.hdr";
    AUREA_CHECK(HdriPathFixture::write(media));
    AUREA_CHECK(HdriPathFixture::write(companion));
    const auto projectPath = HdriPathFixture::utf8(fixture.root / "portable.aurea");
    u64 assetId = 0;
    {
        Engine e;
        auto config = headless_config(); config.documentsDirectory = HdriPathFixture::utf8(oldDocs);
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.new_project(320, 180, 30, "portable HDRI").ok());
        const auto imported = e.import_hdri(HdriPathFixture::utf8(media).c_str());
        AUREA_CHECK(imported.ok());
        assetId = *imported;
        const Asset* asset = e.project()->asset(AssetId::unpack(assetId));
        AUREA_CHECK(asset && asset->sourcePath == "docs:modelos/environment.hdr");
        AUREA_CHECK(e.save_project(projectPath.c_str()).ok());
        e.shutdown();
    }
    std::error_code error;
    AUREA_CHECK(std::filesystem::remove(media, error)); // old sandbox is unavailable
    {
        Engine e;
        auto config = headless_config(); config.documentsDirectory = HdriPathFixture::utf8(newDocs);
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.load_project(projectPath.c_str()).ok());
        const Asset* asset = e.project()->asset(AssetId::unpack(assetId));
        AUREA_CHECK(asset && asset->sourcePath == "docs:modelos/environment.hdr");
        const auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        AUREA_CHECK_EQ(comp->environment().hdri.pack(), assetId);
        // The real Radiance decoder consumes the saved reference through the
        // same resolver as hdri_lookup; no private API or fake media loader.
        const std::string savedPath = asset->sourcePath;
        AUREA_CHECK(e.import_hdri(savedPath.c_str()).ok());
        AUREA_CHECK(e.save_project(projectPath.c_str()).ok());
        AUREA_CHECK(e.load_project(projectPath.c_str()).ok());
        e.shutdown();
    }
}

AUREA_TEST(Engine, HdriLegacyAndroidPathResolvesOnlyExistingContainedCompanion) {
    HdriPathFixture fixture;
    AUREA_CHECK(fixture.owns);
    const auto docs = fixture.root / "Documents";
    AUREA_CHECK(HdriPathFixture::write(docs / "modelos" / "environment.hdr"));
    AUREA_CHECK(HdriPathFixture::write(fixture.root / "outside.hdr"));
    AUREA_CHECK(HdriPathFixture::write(fixture.root / "Documents-other" / "external.hdr"));
    Engine e;
    auto config = headless_config(); config.documentsDirectory = HdriPathFixture::utf8(docs);
    AUREA_CHECK(e.initialize(config).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "legacy HDRI").ok());
    const char* legacy = "/data/user/0/com.aurea.aurea/files/modelos/environment.hdr";
    auto imported = e.import_hdri(legacy);
    AUREA_CHECK(imported.ok());
    AUREA_CHECK(e.project()->asset(AssetId::unpack(*imported))->sourcePath == "docs:modelos/environment.hdr");
    AUREA_CHECK(e.import_hdri("/data/data/com.aurea.aurea/files/modelos/environment.hdr").ok());
    // Simulate the original serialized Android reference, without rewriting
    // an external golden fixture. Loading preserves it until a new import.
    e.project()->asset(AssetId::unpack(*imported))->sourcePath = legacy;
    const auto path = HdriPathFixture::utf8(fixture.root / "legacy.aurea");
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    const auto stored = e.project()->asset(AssetId::unpack(*imported))->sourcePath;
    AUREA_CHECK_EQ(stored, std::string(legacy));
    AUREA_CHECK(e.import_hdri(stored.c_str()).ok());
    for (const char* invalid : {
        "/data/user/0/com.aurea.aurea/files/modelos/missing.hdr",
        "/data/user/0/com.other.application/files/modelos/environment.hdr",
        "/data/user/0/com.aurea.aurea.evil/files/modelos/environment.hdr",
        "/data/user/0/com.aurea.aurea/files/../outside.hdr",
        "/data/user/0/com.aurea.aurea/files/..\\outside.hdr",
        "docs:../outside.hdr", "docs:..\\outside.hdr",
        "docs:/modelos/environment.hdr", "docs:C:/outside.hdr",
        "docs:modelos/environment.hdr:stream"}) {
        AUREA_CHECK(!e.import_hdri(invalid).ok());
    }
    AUREA_CHECK(e.import_hdri("docs:modelos\\environment.hdr").ok());
    const auto outside = HdriPathFixture::utf8(fixture.root / "Documents-other" / "external.hdr");
    const auto external = e.import_hdri(outside.c_str());
    AUREA_CHECK(external.ok());
    AUREA_CHECK_EQ(e.project()->asset(AssetId::unpack(*external))->sourcePath, outside);
    std::error_code error;
    std::filesystem::create_symlink(fixture.root / "outside.hdr", docs / "escape.hdr", error);
    if (!error) {
        AUREA_CHECK(!e.import_hdri("docs:escape.hdr").ok());
        AUREA_CHECK(!e.import_hdri("/data/user/0/com.aurea.aurea/files/escape.hdr").ok());
    } else {
        std::printf("    symlink containment not exercised: host denied symlink creation\n");
    }
    e.shutdown();
}

AUREA_TEST(Engine, LegacyAndroidModelSurvivesRepeatedProjectReopen) {
    for (const char* sample : {"constant-take.fbx", "model-triangle.glb"}) {
        const std::string ext = std::filesystem::path(sample).extension().string();
        HdriPathFixture fixture;
        AUREA_CHECK(fixture.owns);
        const auto docs = fixture.root / "files" / "projetos";
        const auto model = fixture.root / "files" / "modelos" / ("model" + ext);
        std::error_code error;
        std::filesystem::create_directories(docs, error);
        std::filesystem::create_directories(model.parent_path(), error);
        std::filesystem::copy_file(std::filesystem::u8path(AUREA_TEST_DATA_DIR) / sample, model, error);
        AUREA_CHECK(!error);
        Engine e;
        auto config = headless_config(); config.documentsDirectory = HdriPathFixture::utf8(docs);
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.new_project(320, 180, 30, "legacy model").ok());
        ModelImport request; request.path = HdriPathFixture::utf8(model);
        const auto imported = e.import_model(request);
        AUREA_CHECK(imported.ok());
        auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        const auto asset = comp->layer(LayerId::unpack(*imported))->model.scene;
        e.project()->asset(asset)->sourcePath = "/data/user/0/com.aurea.aurea/files/modelos/model" + ext;
        const auto path = HdriPathFixture::utf8(docs / "model.aurea");
        for (int i = 0; i < 3; ++i) {
            AUREA_CHECK(e.save_project(path.c_str()).ok());
            AUREA_CHECK(e.load_project(path.c_str()).ok());
            AUREA_CHECK_EQ(e.last_load_missing_assets(), 0u);
            AUREA_CHECK(e.model_asset(asset.pack()) != nullptr);
        }
        // New imports live inside the project root and serialize portable paths.
        const auto portable = docs / "modelos" / ("new" + ext);
        std::filesystem::create_directories(portable.parent_path(), error);
        std::filesystem::copy_file(model, portable, error);
        AUREA_CHECK(!error);
        request.path = HdriPathFixture::utf8(portable);
        const auto added = e.import_model(request);
        AUREA_CHECK(added.ok());
        comp = e.project()->timeline().composition(e.project()->timeline().current());
        const auto newAsset = comp->layer(LayerId::unpack(*added))->model.scene;
        AUREA_CHECK_EQ(e.project()->asset(newAsset)->sourcePath, std::string("docs:modelos/new") + ext);
        AUREA_CHECK(e.save_project(path.c_str()).ok());
        AUREA_CHECK(e.load_project(path.c_str()).ok());
        AUREA_CHECK_EQ(e.last_load_missing_assets(), 0u);
        AUREA_CHECK(e.model_asset(newAsset.pack()) != nullptr);
        e.shutdown();
        // iOS can relocate Documents after reinstall/restore. The same serialized
        // project must reopen with its companion models and no original sandbox.
        const auto relocated = fixture.root / "restored" / "Documents";
        std::filesystem::create_directories(relocated / "modelos", error);
        std::filesystem::copy_file(model, relocated / "modelos" / ("model" + ext), error);
        std::filesystem::copy_file(portable, relocated / "modelos" / ("new" + ext), error);
        const auto restoredPath = HdriPathFixture::utf8(relocated / "model.aurea");
        std::filesystem::copy_file(std::filesystem::u8path(path), std::filesystem::u8path(restoredPath), error);
        AUREA_CHECK(!error);
        std::filesystem::rename(fixture.root / "files", fixture.root / "old-unavailable", error);
        AUREA_CHECK(!error);
        config.documentsDirectory = HdriPathFixture::utf8(relocated);
        AUREA_CHECK(e.initialize(config).ok());
        for (int i = 0; i < 2; ++i) {
            AUREA_CHECK(e.load_project(restoredPath.c_str()).ok());
            AUREA_CHECK_EQ(e.last_load_missing_assets(), 0u);
            AUREA_CHECK(e.model_asset(asset.pack()) != nullptr);
            AUREA_CHECK(e.model_asset(newAsset.pack()) != nullptr);
            AUREA_CHECK(e.save_project(restoredPath.c_str()).ok());
        }
        e.shutdown();
    }
}

AUREA_TEST(Engine, CommandsThroughTheQueueReachTheModel) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    // O caminho REAL da UI: escreve na fila, o motor drena no frame. Testar
    // pelo atalho `apply_command` não cobriria o contrato da fila.
    const char name[] = "Camada do usuario";
    u32 offset = 0, length = 0;
    AUREA_CHECK(e.commands().push_string(name, sizeof(name) - 1, offset, length));

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    create.stringOffset = offset;
    create.stringLength = length;

    Command opacity;
    opacity.type = CommandType::LayerSetOpacity;
    opacity.opacity.layer = LayerId{};
    opacity.opacity.opacity = 0.4f;

    const Command batch[1] = {create};
    AUREA_CHECK_EQ(e.submit_commands(batch, 1), static_cast<u32>(1));

    AUREA_CHECK(e.render_frame().ok());

    const Project* p = e.project();
    const Composition* c = p->timeline().composition(p->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    const Layer* l = c->layer(c->order().at(0));
    AUREA_CHECK(l != nullptr);
    AUREA_CHECK_EQ(l->name, std::string("Camada do usuario"));

    (void)opacity;
    e.shutdown();
}

AUREA_TEST(Engine, SubmitCommandsAcceptsPartialBatch) {
    // A fila nunca bloqueia: se encher, o motor aceita o que couber e a UI
    // reenvia o resto. Nenhum comando é perdido em silêncio.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    std::vector<Command> flood(CommandQueue::kCapacity + 500);
    for (auto& c : flood) c.type = CommandType::Nop;

    const u32 accepted = e.submit_commands(flood.data(), static_cast<u32>(flood.size()));
    AUREA_CHECK(accepted <= static_cast<u32>(flood.size()));
    AUREA_CHECK(accepted > 0);
    e.shutdown();
}

AUREA_TEST(Engine, SaveAppliesQueuedEditsBeforeSnapshotWithoutRendering) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, "Queued save").ok());
    const auto added = e.add_shape(0);
    AUREA_CHECK(added.ok());
    if (!added.ok()) { e.shutdown(); return; }
    const auto path = (std::filesystem::temp_directory_path() /
        ("aurea_queued_save_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".aurea")).string();

    // Both bridges submit to this queue. Saving immediately after an edit
    // must include it even when no preview frame or thumbnail has run.
    for (u32 pass = 0; pass < 2; ++pass) {
        const f32 rotation = pass == 0 ? 15.0f : 30.0f;
        const f32 opacity = pass == 0 ? 0.75f : 0.5f;
        Command edits[2];
        edits[0].type = CommandType::LayerSetRotation;
        edits[0].rotation.layer = LayerId::unpack(*added);
        edits[0].rotation.rx = 0; edits[0].rotation.ry = 0; edits[0].rotation.rz = rotation;
        edits[1].type = CommandType::LayerSetOpacity;
        edits[1].opacity.layer = LayerId::unpack(*added);
        edits[1].opacity.opacity = opacity;
        AUREA_CHECK_EQ(e.submit_commands(edits, 2), 2u);
        AUREA_CHECK_EQ(e.commands().available(), 2u);
        AUREA_CHECK((pass == 0 ? e.save_project(path.c_str()) : e.save_project()).ok());
        AUREA_CHECK_EQ(e.commands().available(), 0u);

        // A separate engine ensures queued in-memory edits cannot disguise
        // a stale file when it is reopened (including the pathless overload).
        Engine reopened;
        AUREA_CHECK(reopened.initialize(headless_config()).ok());
        AUREA_CHECK(reopened.load_project(path.c_str()).ok());
        const Composition* comp = reopened.project()->timeline().composition(reopened.project()->timeline().current());
        AUREA_CHECK(comp != nullptr);
        if (comp) {
            AUREA_CHECK_EQ(comp->layers().count(), 1u);
            const Layer* layer = comp->order().size() ? comp->layer(comp->order().at(0)) : nullptr;
            AUREA_CHECK(layer != nullptr);
            if (layer) {
                AUREA_CHECK_NEAR(layer->transform.rotation.z, rotation, 0.0001f);
                AUREA_CHECK_NEAR(layer->transform.opacity, opacity, 0.0001f);
            }
        }
        reopened.shutdown();
    }
    e.shutdown();
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
}

AUREA_TEST(Engine, AddLayerViaCommandThenQueryRows) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    for (int i = 0; i < 5; ++i) {
        Command c;
        c.type = CommandType::LayerCreate;
        c.layer_create.kind = (i % 2) ? LayerKind::Text : LayerKind::Video;
        AUREA_CHECK(e.apply_command(c).ok());
    }

    bridge::LayerRow rows[16];
    char names[512];
    const u32 n = e.query_layers(rows, 16, names, sizeof(names));
    AUREA_CHECK_EQ(n, static_cast<u32>(5));

    // A lista vem da FRENTE para o fundo: é o que a UI mostra, e inverter na UI
    // seria a mesma regra em dois lugares — uma chance de divergirem.
    AUREA_CHECK(rows[0].zIndex < rows[4].zIndex);
    for (u32 i = 0; i < n; ++i) {
        AUREA_CHECK(rows[i].nameLength > 0);
    }
    e.shutdown();
}

AUREA_TEST(Engine, QueryLayersRespectsCapacity) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    for (int i = 0; i < 20; ++i) {
        Command c;
        c.type = CommandType::LayerCreate;
        c.layer_create.kind = LayerKind::Video;
        (void)e.apply_command(c);
    }

    bridge::LayerRow rows[4];
    const u32 n = e.query_layers(rows, 4, nullptr, 0);
    AUREA_CHECK_EQ(n, static_cast<u32>(4));
    e.shutdown();
}

AUREA_TEST(Engine, LayerVisibilityFlagIsReported) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    create.correlationId = 7;
    (void)e.apply_command(create);

    const Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command hide;
    hide.type = CommandType::LayerSetVisible;
    hide.layer_visible.layer = id;
    hide.layer_visible.visible = false;
    AUREA_CHECK(e.apply_command(hide).ok());

    bridge::LayerRow rows[4];
    const u32 n = e.query_layers(rows, 4, nullptr, 0);
    AUREA_CHECK_EQ(n, static_cast<u32>(1));
    AUREA_CHECK_EQ(rows[0].flags & 1u, static_cast<u32>(0));   // bit 0 = visivel
    e.shutdown();
}

AUREA_TEST(Engine, LayerSplitProducesTwoAdjacentLayers) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId original = c->order().at(0);
    c->layer(original)->start = FrameIndex{0};
    c->layer(original)->end = FrameIndex{100};

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = original;
    split.layer_split.at = FrameIndex{40};
    AUREA_CHECK(e.apply_command(split).ok());

    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(2));

    const Layer* first = c->layer(original);
    AUREA_CHECK_EQ(first->start.value, static_cast<i64>(0));
    AUREA_CHECK_EQ(first->end.value, static_cast<i64>(40));

    // A segunda metade continua de onde a primeira parou. Sem ajustar o offset,
    // a segunda repetiria o começo do vídeo — o erro clássico de corte.
    LayerId second{};
    c->layers().for_each([&](LayerId id, const Layer&) {
        if (!(id == original)) second = id;
    });
    AUREA_CHECK(second.valid());
    const Layer* s = c->layer(second);
    AUREA_CHECK_EQ(s->start.value, static_cast<i64>(40));
    AUREA_CHECK_EQ(s->end.value, static_cast<i64>(100));
    AUREA_CHECK_EQ(s->offset.value, static_cast<i64>(40));
    e.shutdown();
}

AUREA_TEST(Engine, SplitOutsideRangeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);
    c->layer(id)->start = FrameIndex{0};
    c->layer(id)->end = FrameIndex{100};

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = id;
    split.layer_split.at = FrameIndex{500};
    AUREA_CHECK(!e.apply_command(split).ok());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, ParentingCycleIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command createA;
    createA.type = CommandType::LayerCreate;
    createA.layer_create.kind = LayerKind::Null;
    (void)e.apply_command(createA);
    Command createB;
    createB.type = CommandType::LayerCreate;
    createB.layer_create.kind = LayerKind::Null;
    (void)e.apply_command(createB);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId a = c->order().at(0);
    const LayerId b = c->order().at(1);

    Command parent;
    parent.type = CommandType::LayerSetParent;
    parent.layer_parent.layer = a;
    parent.layer_parent.parent = b;
    AUREA_CHECK(e.apply_command(parent).ok());

    // Fechar o ciclo faria a avaliação de transform entrar em laço infinito.
    Command cycle;
    cycle.type = CommandType::LayerSetParent;
    cycle.layer_parent.layer = b;
    cycle.layer_parent.parent = a;
    AUREA_CHECK(!e.apply_command(cycle).ok());
    e.shutdown();
}

AUREA_TEST(Engine, ParentingKeepsChildInPlace) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    auto pid = e.add_null(false);
    auto cid = e.add_null(false);
    AUREA_CHECK(pid.ok() && cid.ok());
    Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* par = c->layer(LayerId::unpack(*pid));
    Layer* ch = c->layer(LayerId::unpack(*cid));
    AUREA_CHECK(par && ch && par->kind == LayerKind::Null);
    par->transform.position = Vec3{300, 200, 0};
    par->transform.rotation = Vec3{0, 0, 30};
    par->transform.scale = Vec3{2, 2, 1};
    ch->transform.position = Vec3{900, 500, 0};
    ch->transform.rotation = Vec3{0, 0, -10};
    const FrameIndex t0{0};
    auto corner = [&](const Mat4& m, f32 x, f32 y) { return m * Vec4{x, y, 0, 1}; };
    const Mat4 before = layer_world_matrix(*c, *ch, t0);
    bridge::LayerDetailPOD d0, d1;
    AUREA_CHECK(e.query_layer_detail(*cid, d0));
    AUREA_CHECK((d0.geomFlags & bridge::kGeomCornersValid) != 0);
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(*cid);
    pc.layer_parent.parent = LayerId::unpack(*pid);
    AUREA_CHECK(e.apply_command(pc).ok());
    const Mat4 after = layer_world_matrix(*c, *ch, t0);
    f32 worst = 0;
    for (f32 x : {0.0f, 100.0f}) for (f32 y : {0.0f, 100.0f}) {
        const Vec4 a = corner(before, x, y), b = corner(after, x, y);
        worst = std::max({worst, std::fabs(a.x - b.x), std::fabs(a.y - b.y)});
    }
    std::printf("    filho: escala local %.3f rot %.2f; desvio %.4f px\n", ch->transform.scale.x, ch->transform.rotation.z, worst);
    AUREA_CHECK(worst < 0.05f);
    AUREA_CHECK(std::fabs(ch->transform.scale.x - 0.5f) < 1e-3f);
    AUREA_CHECK(std::fabs(ch->transform.rotation.z + 40.0f) < 1e-2f);
    AUREA_CHECK(ch->transform.rotation.x == 0.0f && ch->transform.rotation.y == 0.0f);
    // O palco recebe os mesmos cantos (mundo) e o afim do pai.
    AUREA_CHECK(e.query_layer_detail(*cid, d1));
    f32 cw = 0;
    for (int i = 0; i < 8; ++i) cw = std::max(cw, std::fabs(d0.corners[i] - d1.corners[i]));
    AUREA_CHECK(cw < 0.05f);
    AUREA_CHECK(std::fabs(d1.parentAffine[0] - 2.0f * std::cos(30.0f * 3.14159265f / 180.0f)) < 1e-3f);
    AUREA_CHECK(std::fabs(d1.parentAffine[4] - (300.0f - 2.0f * (50.0f * std::cos(0.5235988f) - 50.0f * std::sin(0.5235988f)))) < 0.05f);
    // Mover o pai arrasta o filho.
    par->transform.position.x += 50;
    const Vec4 moved = corner(layer_world_matrix(*c, *ch, t0), 0, 0);
    AUREA_CHECK(std::fabs(moved.x - corner(after, 0, 0).x - 50.0f) < 0.05f);
    par->transform.position.x -= 50;
    // Soltar o pai: volta ao mesmo lugar, transform original.
    pc.layer_parent.parent = LayerId{};
    AUREA_CHECK(e.apply_command(pc).ok());
    const Mat4 freed = layer_world_matrix(*c, *ch, t0);
    f32 worst2 = 0;
    for (f32 x : {0.0f, 100.0f}) for (f32 y : {0.0f, 100.0f}) {
        const Vec4 a = corner(before, x, y), b = corner(freed, x, y);
        worst2 = std::max({worst2, std::fabs(a.x - b.x), std::fabs(a.y - b.y)});
    }
    AUREA_CHECK(worst2 < 0.05f);
    AUREA_CHECK(std::fabs(ch->transform.position.x - 900.0f) < 0.05f);
    AUREA_CHECK(std::fabs(ch->transform.scale.x - 1.0f) < 1e-3f);
    e.shutdown();
}

AUREA_TEST(Engine, MobileUnlinkKeepsChildrenInPlaceAndUndoes) {
    for (bool threeD : {false, true}) for (u32 keyed : {0u, 1u, 2u}) {
        Engine e;
        AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(1280, 720, 30, nullptr).ok());
        const auto parentId = e.add_null(threeD), childId = e.add_shape(0);
        AUREA_CHECK(parentId.ok() && childId.ok());
        if (!parentId.ok() || !childId.ok()) continue;
        auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        auto child = [&] { return comp->layer(LayerId::unpack(*childId)); };
        auto* parent = comp->layer(LayerId::unpack(*parentId));
        parent->transform.position = {280, 150, threeD ? 40.f : 0.f};
        parent->transform.rotation = {threeD ? 25.f : 0.f, threeD ? -18.f : 0.f, 35};
        parent->transform.scale = {1.4f, 1.4f, 1};
        child()->parent = LayerId::unpack(*parentId);
        child()->transform.position = {200, 180, 0};
        if (keyed) {
            for (auto property : {TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ,
                                  TrackProperty::ScaleX, TrackProperty::ScaleY, TrackProperty::ScaleZ,
                                  TrackProperty::AnchorX, TrackProperty::AnchorY, TrackProperty::AnchorZ}) {
                auto& track = child()->tracks.get_or_create(property);
                const bool scale = property >= TrackProperty::ScaleX && property <= TrackProperty::ScaleZ;
                const bool planar = property == TrackProperty::RotationZ || property == TrackProperty::AnchorX || property == TrackProperty::AnchorY;
                track.set(FrameIndex{0}, scale ? 1.f : (threeD || planar ? 10.f : 0.f));
                if (keyed == 2) track.set(FrameIndex{30}, scale ? 1.4f : (threeD || planar ? 35.f : 0.f));
            }
        }
        Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{15}, 30.0);
        AUREA_CHECK(e.apply_command(seek).ok());
        // A 2D child inherits the dimension of a 3D parent.
        const auto before = threeD ? layer_world_3d(*comp, *child(), FrameIndex{15}) : layer_world_matrix(*comp, *child(), FrameIndex{15});
        Command unlink; unlink.type = CommandType::LayerSetParent;
        unlink.layer_parent.layer = LayerId::unpack(*childId);
        unlink.layer_parent.parent = LayerId::unpack(0); // Exact payload used by both mobile UIs.
        AUREA_CHECK(e.apply_command(unlink).ok());
        AUREA_CHECK(!child()->parent.valid());
        if (threeD) AUREA_CHECK(child()->threeD);
        const auto after = threeD ? layer_world_3d(*comp, *child(), FrameIndex{15}) : layer_world_matrix(*comp, *child(), FrameIndex{15});
        for (Vec3 point : {Vec3{0, 0, 0}, Vec3{100, 0, 0}, Vec3{0, 100, 0}, Vec3{10, 20, 30}})
            AUREA_CHECK((before.transform_point(point) - after.transform_point(point)).length() < .05f);
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(undo).ok());
        AUREA_CHECK(child()->parent == LayerId::unpack(*parentId));
        e.shutdown();
    }
}

// Beta 2140: apagar o nulo ANIMADO (no fim da animação) fazia o filho pular
// para o quadro do 1º keyframe ou sair do centro. O filho tem que guardar o
// resultado da tela em TODOS os quadros (pai cozido por quadro), ao apagar o
// pai (com e sem avô) e ao soltar; desfazer volta tudo num passo só.
AUREA_TEST(Engine, ExplicitUnlinkBakesTheAnimatedParentMotion) {
    for (u32 mode : {2u}) {   // Explicit unlink keeps the existing world-motion contract.
        Engine e;
        AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(1280, 720, 30, nullptr).ok());
        const auto grandId = e.add_null(false), parentId = e.add_null(false), childId = e.add_shape(0);
        AUREA_CHECK(grandId.ok() && parentId.ok() && childId.ok());
        if (!grandId.ok() || !parentId.ok() || !childId.ok()) continue;
        auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        auto child = [&] { return comp->layer(LayerId::unpack(*childId)); };
        auto* grand = comp->layer(LayerId::unpack(*grandId));
        grand->transform.position = {500, 300, 0};
        grand->transform.rotation = {0, 0, 20};
        auto* parent = comp->layer(LayerId::unpack(*parentId));
        if (mode == 1) parent->parent = LayerId::unpack(*grandId);
        parent->transform.position = {200, 360, 0};
        // Anda, dá DUAS voltas e cresce entre os quadros 0 e 30; depois fica.
        parent->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0}, 200);
        parent->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{30}, 900);
        parent->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{0}, 0);
        parent->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{30}, 720);
        for (auto p : {TrackProperty::ScaleX, TrackProperty::ScaleY}) {
            parent->tracks.get_or_create(p).set(FrameIndex{0}, 1.0f);
            parent->tracks.get_or_create(p).set(FrameIndex{30}, 1.5f);
        }
        child()->parent = LayerId::unpack(*parentId);
        child()->transform.position = {120, 40, 0};
        child()->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{0}, 0);
        child()->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{60}, 90);
        const FrameIndex probe[] = {FrameIndex{0}, FrameIndex{7}, FrameIndex{15}, FrameIndex{22}, FrameIndex{29}, FrameIndex{30}, FrameIndex{45}};
        Mat4 before[7];
        for (u32 i = 0; i < 7; ++i) before[i] = layer_world_matrix(*comp, *child(), probe[i]);
        // O usuário está no fim da animação.
        Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{30}, 30.0);
        AUREA_CHECK(e.apply_command(seek).ok());
        Command cmd{};
        if (mode < 2) {
            cmd.type = CommandType::LayerDelete;
            cmd.layer_ref.layer = LayerId::unpack(*parentId);
        } else {
            cmd.type = CommandType::LayerSetParent;
            cmd.layer_parent = {LayerId::unpack(*childId), LayerId::unpack(0)};
        }
        AUREA_CHECK(e.apply_command(cmd).ok());
        AUREA_CHECK(child() != nullptr);
        if (!child()) continue;
        if (mode == 1) AUREA_CHECK(child()->parent == LayerId::unpack(*grandId));
        else AUREA_CHECK(!child()->parent.valid());
        f32 worst = 0.0f;
        for (u32 i = 0; i < 7; ++i) {
            const Mat4 after = layer_world_matrix(*comp, *child(), probe[i]);
            for (Vec3 point : {Vec3{0, 0, 0}, Vec3{100, 0, 0}, Vec3{0, 100, 0}})
                worst = std::max(worst, (before[i].transform_point(point) - after.transform_point(point)).length());
        }
        std::printf("    modo %u: pior desvio %.3f px, %zu chaves de rotação ", mode, worst,
                    child()->tracks.find(TrackProperty::RotationZ) ? child()->tracks.find(TrackProperty::RotationZ)->keys.size() : 0u);
        AUREA_CHECK(worst < 0.5f);
        // As duas voltas do pai continuam voltas: quadro a quadro o giro anda
        // os ~25° do pai, sem pulo de meia volta (desembrulhar errado).
        if (const Track* rz = child()->tracks.find(TrackProperty::RotationZ)) {
            AUREA_CHECK(rz->keys.size() >= 2);
            for (i64 f = 1; f <= 45; ++f)
                AUREA_CHECK(std::fabs(rz->sample_keys(FrameIndex{f}) - rz->sample_keys(FrameIndex{f - 1})) < 90.0f);
            AUREA_CHECK(std::fabs(rz->sample_keys(FrameIndex{30}) - rz->sample_keys(FrameIndex{0})) > 700.0f);
        }
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(undo).ok());
        AUREA_CHECK(comp->layer(LayerId::unpack(*parentId)) != nullptr);
        AUREA_CHECK(child() && child()->parent == LayerId::unpack(*parentId));
        if (child()) {
            const Mat4 again = layer_world_matrix(*comp, *child(), FrameIndex{15});
            AUREA_CHECK((again.transform_point(Vec3{0, 0, 0}) - before[2].transform_point(Vec3{0, 0, 0})).length() < 0.05f);
        }
        e.shutdown();
    }
}

AUREA_TEST(Engine, ParentRowsExposeNestedLinksInDisplayOrder) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30, nullptr).ok());
    const auto root = e.add_null(false), child = e.add_null(true), leaf = e.add_shape(0);
    AUREA_CHECK(root.ok() && child.ok() && leaf.ok());
    if (!root.ok() || !child.ok() || !leaf.ok()) return;
    Command link; link.type = CommandType::LayerSetParent;
    link.layer_parent = {LayerId::unpack(*child), LayerId::unpack(*root)};
    AUREA_CHECK(e.apply_command(link).ok());
    link.layer_parent = {LayerId::unpack(*leaf), LayerId::unpack(*child)};
    AUREA_CHECK(e.apply_command(link).ok());
    bridge::LayerRow rows[3]{};
    auto check = [&] {
        AUREA_CHECK_EQ(e.query_layers(rows, 3, nullptr, 0), 3u);
        for (const auto& row : rows) {
            if (row.id == *root) AUREA_CHECK_EQ(row.parentIndex, kInvalidIndex);
            else {
                AUREA_CHECK(row.parentIndex < 3);
                if (row.parentIndex < 3) AUREA_CHECK_EQ(rows[row.parentIndex].id, row.id == *leaf ? *child : *root);
            }
        }
    };
    check();
    AUREA_CHECK_EQ(rows[0].id, *leaf);
    AUREA_CHECK_EQ(e.query_layers(rows, 1, nullptr, 0), 1u);
    AUREA_CHECK_EQ(rows[0].parentIndex, kInvalidIndex); // Parent omitted by capacity.
    Command reorder; reorder.type = CommandType::LayerReorder;
    reorder.layer_reorder.layer = LayerId::unpack(*root); reorder.layer_reorder.newIndex = 2;
    AUREA_CHECK(e.apply_command(reorder).ok());
    check();
    link.layer_parent = {LayerId::unpack(*child), LayerId::unpack(0)};
    AUREA_CHECK(e.apply_command(link).ok());
    AUREA_CHECK_EQ(e.query_layers(rows, 3, nullptr, 0), 3u);
    for (const auto& row : rows) if (row.id == *child) AUREA_CHECK_EQ(row.parentIndex, kInvalidIndex);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    check();
    e.shutdown();
}

AUREA_TEST(Engine, NestedNull3DLinkPreservesAnimatedDescendants) {
    for (bool root3D : {false, true}) for (bool nonuniform : {false, true}) {
        Engine e;
        AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(1280, 720, 30, nullptr).ok());
        const auto rootId = e.add_null(root3D), nullId = e.add_null(true), shapeId = e.add_shape(0);
        AUREA_CHECK(rootId.ok() && nullId.ok() && shapeId.ok());
        if (!rootId.ok() || !nullId.ok() || !shapeId.ok()) continue;
        auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        auto* root = comp->layer(LayerId::unpack(*rootId));
        auto* node = comp->layer(LayerId::unpack(*nullId));
        auto* shape = comp->layer(LayerId::unpack(*shapeId));
        root->transform.position = {280, 170, root3D ? 75.f : 0.f};
        root->transform.rotation = {root3D ? 25.f : 0.f, root3D ? -35.f : 0.f, 40};
        root->transform.scale = {1.6f, nonuniform ? .65f : 1.6f, nonuniform ? .7f : 1.f};
        node->transform.position = {710, 420, 80};
        node->transform.rotation = {15, 30, -20};
        node->tracks.get_or_create(TrackProperty::RotationX).set(FrameIndex{0}, 15);
        node->tracks.get_or_create(TrackProperty::RotationX).set(FrameIndex{30}, 70);
        node->tracks.get_or_create(TrackProperty::RotationY).set(FrameIndex{0}, 30);
        node->tracks.get_or_create(TrackProperty::RotationY).set(FrameIndex{30}, -45);
        node->tracks.get_or_create(TrackProperty::PositionY).set(FrameIndex{0}, 420);
        node->tracks.get_or_create(TrackProperty::PositionY).set(FrameIndex{30}, 510);
        shape->parent = LayerId::unpack(*nullId);
        shape->transform.position = {80, 120, 35};
        Mat4 before[3];
        for (int i = 0; i < 3; ++i) before[i] = layer_world_3d(*comp, *shape, FrameIndex{i * 15});
        Command link; link.type = CommandType::LayerSetParent;
        link.layer_parent.layer = LayerId::unpack(*nullId);
        link.layer_parent.parent = LayerId::unpack(*rootId);
        AUREA_CHECK(e.apply_command(link).ok());
        f32 worst = 0;
        for (int i = 0; i < 3; ++i) {
            const auto after = layer_world_3d(*comp, *shape, FrameIndex{i * 15});
            for (Vec3 p : {Vec3{0, 0, 0}, Vec3{100, 0, 0}, Vec3{0, 100, 0}, Vec3{10, 20, 30}})
                worst = std::max(worst, (before[i].transform_point(p) - after.transform_point(p)).length());
        }
        std::printf("    nested null: root3D=%d nonuniform=%d drift=%.3f px\n", root3D, nonuniform, worst);
        AUREA_CHECK(worst < .05f);
        root->transform.position.x += 100;
        const auto moved = layer_world_3d(*comp, *shape, FrameIndex{0}).transform_point({0, 0, 0});
        AUREA_CHECK((moved - before[0].transform_point({0, 0, 0}) - Vec3{100, 0, 0}).length() < .05f);
        root->transform.position.x -= 100;
        // World-axis dragging must account for the bind compensation too.
        f32 dragged[3]{};
        AUREA_CHECK(e.gizmo_move_local(*nullId, 0, 25, dragged));
        const Vec3 oldPosition = node->transform.position;
        node->transform.position.x = dragged[0];
        AUREA_CHECK((layer_world_3d(*comp, *shape, FrameIndex{0}).transform_point({0, 0, 0})
            - before[0].transform_point({0, 0, 0}) - Vec3{25, 0, 0}).length() < .05f);
        node->transform.position = oldPosition;
        link.layer_parent.parent = LayerId::unpack(0);
        AUREA_CHECK(e.apply_command(link).ok());
        for (int i = 0; i < 3; ++i)
            AUREA_CHECK((before[i].transform_point({25, 50, 40}) - layer_world_3d(*comp, *shape, FrameIndex{i * 15}).transform_point({25, 50, 40})).length() < .05f);
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(undo).ok());
        node = comp->layer(LayerId::unpack(*nullId));
        shape = comp->layer(LayerId::unpack(*shapeId));
        AUREA_CHECK(node->parent == LayerId::unpack(*rootId));
        AUREA_CHECK(node->tracks.find(TrackProperty::RotationY)->keys.size() == 2);
        const std::string path = (std::filesystem::temp_directory_path() / "aurea_nested_null_3d_test.aurea").string();
        AUREA_CHECK(e.save_project(path.c_str()).ok());
        AUREA_CHECK(e.load_project(path.c_str()).ok());
        comp = e.project()->timeline().composition(e.project()->timeline().current());
        shape = nullptr;
        comp->layers().for_each([&](LayerId, Layer& layer) { if (layer.kind == LayerKind::Shape) shape = &layer; });
        AUREA_CHECK(shape != nullptr);
        if (shape) for (int i = 0; i < 3; ++i)
            AUREA_CHECK((before[i].transform_point({25, 50, 40}) - layer_world_3d(*comp, *shape, FrameIndex{i * 15}).transform_point({25, 50, 40})).length() < .05f);
        // Resizing acts once on roots, including any retained bind space.
        comp->resize_content(2560, 1440);
        if (shape) AUREA_CHECK((before[0].transform_point({25, 50, 40}) * 2.f
            - layer_world_3d(*comp, *shape, FrameIndex{0}).transform_point({25, 50, 40})).length() < .05f);
        std::remove(path.c_str());
        e.shutdown();
    }
}

AUREA_TEST(Engine, ParentSurvivesSaveAndReopenAfterReorder) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    auto a = e.add_null(false);   // slot 0
    auto b = e.add_null(false);   // slot 1
    auto c = e.add_null(false);   // slot 2
    AUREA_CHECK(a.ok() && b.ok() && c.ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    comp->layer(LayerId::unpack(*a))->name = "filho";
    comp->layer(LayerId::unpack(*c))->name = "pai";
    // Ordem vertical diferente da de criação: o pai vai para o fundo.
    Command ro;
    ro.type = CommandType::LayerReorder;
    ro.layer_reorder.layer = LayerId::unpack(*c);
    ro.layer_reorder.newIndex = 0;
    AUREA_CHECK(e.apply_command(ro).ok());
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(*a);
    pc.layer_parent.parent = LayerId::unpack(*c);
    AUREA_CHECK(e.apply_command(pc).ok());
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_pai.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* child = nullptr;
    comp->layers().for_each([&](LayerId, const Layer& l) { if (l.name == "filho") child = &l; });
    AUREA_CHECK(child != nullptr);
    const Layer* par = child ? comp->layer(child->parent) : nullptr;
    AUREA_CHECK(par != nullptr);
    AUREA_CHECK(par && par->name == "pai");
    e.shutdown();
    std::remove(path.c_str());
}

AUREA_TEST(Engine, SelectionIsSortedAndDeduplicated) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    const u64 ids[] = {50, 10, 50, 30};
    e.set_selection(ids, 4);
    AUREA_CHECK_EQ(e.selection_count(), static_cast<u32>(3));
    AUREA_CHECK(e.is_selected(10));
    AUREA_CHECK(e.is_selected(30));
    AUREA_CHECK(e.is_selected(50));
    AUREA_CHECK(!e.is_selected(20));

    u64 out[8];
    AUREA_CHECK_EQ(e.get_selection(out, 8), static_cast<u32>(3));
    AUREA_CHECK_EQ(out[0], static_cast<u64>(10));

    e.clear_selection();
    AUREA_CHECK_EQ(e.selection_count(), static_cast<u32>(0));
    e.shutdown();
}

AUREA_TEST(Engine, SeekUpdatesPlayhead) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = TickNs{1'000'000'000};   // 1 segundo
    AUREA_CHECK(e.apply_command(seek).ok());

    // 1 s a 30 fps = frame 30.
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, static_cast<i64>(30));
    e.shutdown();
}

AUREA_TEST(Engine, PlayPauseControlsClock) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command play;
    play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    AUREA_CHECK(e.project()->timeline().playing());

    Command pause;
    pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(e.apply_command(pause).ok());
    AUREA_CHECK(!e.project()->timeline().playing());
    e.shutdown();
}

AUREA_TEST(Engine, ExtendingFiveSecondClipExtendsPlaybackAndUndoRestoresBoth) {
    for (bool ripple : {false, true}) {
        Engine e;
        AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
        auto& timeline = e.project()->timeline();
        auto* c = timeline.composition(timeline.current());
        Command duration; duration.type = CommandType::CompositionSetDuration;
        duration.comp_duration.comp = timeline.current(); duration.comp_duration.duration = FrameIndex{150};
        AUREA_CHECK(e.apply_command(duration).ok());
        const auto id = c->add_layer(LayerKind::Shape, "five seconds");
        c->set_edit_mode(ripple);
        Command trim; trim.type = CommandType::LayerSetTimeRange;
        trim.layer_range.layer = id; trim.layer_range.start = FrameIndex{0}; trim.layer_range.end = FrameIndex{900};
        AUREA_CHECK(e.apply_command(trim).ok());
        AUREA_CHECK_EQ(e.read_status().duration.value, 900);
        Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{600}, 30);
        AUREA_CHECK(e.apply_command(seek).ok());
        AUREA_CHECK_EQ(e.read_status().playhead.value, 600);
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(undo).ok());
        c = timeline.composition(timeline.current());
        AUREA_CHECK_EQ(c->duration().value, 150);
        AUREA_CHECK_EQ(c->layer(id)->end.value, 150);
        e.shutdown();
    }
}

AUREA_TEST(Engine, CompositionResolutionKeepsVideoFramingAndUndo) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30, nullptr).ok());
    auto& timeline = e.project()->timeline();
    auto* c = timeline.composition(timeline.current());
    const auto id = c->add_layer(LayerKind::Video, "video");
    c->layer(id)->transform.position = Vec3{640, 360, 0};
    c->layer(id)->transform.scale = Vec3{0.5f, 0.5f, 1};
    Command size; size.type = CommandType::CompositionSetSize;
    size.comp_size.comp = timeline.current(); size.comp_size.width = 640; size.comp_size.height = 360;
    AUREA_CHECK(e.apply_command(size).ok());
    AUREA_CHECK_NEAR(c->layer(id)->transform.position.x / c->width(), 0.5f, 0.001f);
    AUREA_CHECK_NEAR(c->layer(id)->transform.scale.x, 0.25f, 0.001f);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    c = timeline.composition(timeline.current());
    AUREA_CHECK_EQ(c->width(), 1280u);
    AUREA_CHECK_NEAR(c->layer(id)->transform.position.x, 640, 0.001f);
    AUREA_CHECK_NEAR(c->layer(id)->transform.scale.x, 0.5f, 0.001f);
    e.shutdown();
}

AUREA_TEST(Engine, CompositionSizeBeyondDeviceIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());

    Command tooBig;
    tooBig.type = CommandType::CompositionSetSize;
    tooBig.comp_size.comp = e.project()->timeline().current();
    tooBig.comp_size.width = 16384;
    tooBig.comp_size.height = 16384;
    // Acima do que o aparelho decodifica, o preview não acompanha e o export
    // não fecha. Recusar com código claro é melhor que aceitar e o usuário
    // descobrir no export.
    AUREA_CHECK(!e.apply_command(tooBig).ok());
    AUREA_CHECK_EQ(c->width(), static_cast<u32>(1280));
    e.shutdown();
}

AUREA_TEST(Engine, SeekMovesThePlayheadAndRenderKeepsIt) {
    // Parado, o playhead é o que o usuário pediu: renderizar não o move.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(e.apply_command(seek).ok());
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, static_cast<i64>(10));
    AUREA_CHECK_EQ(e.read_status().playhead.value, static_cast<i64>(10));
    e.shutdown();
}

AUREA_TEST(Engine, PlayAdvancesThePlayheadFromTheClock) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command play;
    play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(e.apply_command(play).ok());
    AUREA_CHECK(e.read_status().playing);
    // O relógio anda em tempo real: 120 ms depois, pelo menos 2 frames.
    const u64 t0 = monotonic_ns();
    while (monotonic_ns() - t0 < 120'000'000ull) {}
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.project()->timeline().playhead().value >= 2);
    Command pause;
    pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(e.apply_command(pause).ok());
    AUREA_CHECK(!e.read_status().playing);
    e.shutdown();
}

AUREA_TEST(Engine, AnimationSkipsLayersOutsideTheirRange) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);
    c->layer(id)->start = FrameIndex{1000};
    c->layer(id)->end = FrameIndex{2000};

    AUREA_CHECK(e.render_frame().ok());

    bridge::LayerRow rows[4];
    // A camada existe na timeline, mas o motor não a avaliou — ela está fora
    // do tempo. Avaliar camadas inativas é trabalho jogado fora a 60 Hz.
    AUREA_CHECK_EQ(e.query_layers(rows, 4, nullptr, 0), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, StatusReflectsProjectState) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, "Status").ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);
    (void)e.render_frame();

    const EngineStatus st = e.read_status();
    AUREA_CHECK_EQ(st.layerCount, static_cast<u32>(1));
    AUREA_CHECK_EQ(st.duration.value, static_cast<i64>(300));
    AUREA_CHECK(!st.playing);
    AUREA_CHECK(st.previewDenominator >= 1);
    e.shutdown();
}

AUREA_TEST(Engine, TelemetryReportsWorkersAndQueues) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    const EngineTelemetry t = e.read_telemetry();
    AUREA_CHECK_EQ(t.workerCount, static_cast<u32>(2));
    AUREA_CHECK_EQ(t.commandsDropped, static_cast<u64>(0));
    e.shutdown();
}

AUREA_TEST(Engine, ExportWithoutEncoderIsRefusedNotFaked) {
    // Sem GPU ou sem encoder da plataforma, o export é recusado na hora — nada
    // de "ok" seguido de um arquivo vazio que o usuário acharia que é o
    // trabalho dele.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    ExportSettings settings;
    const Status s = e.start_export(settings, "saida.mp4");
    AUREA_CHECK(!s.ok());
    AUREA_CHECK_EQ(s.code(), Errc::NotSupported);
    AUREA_CHECK(!e.export_progress().running);

    std::FILE* f = std::fopen("saida.mp4", "rb");
    AUREA_CHECK(f == nullptr);
    if (f) std::fclose(f);
    e.shutdown();
}

AUREA_TEST(Engine, SuspendAndResumeKeepProject) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, "Sobrevive").ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    AUREA_CHECK(e.suspend().ok());
    AUREA_CHECK_EQ(e.state(), EngineState::Suspended);

    AUREA_CHECK(e.resume().ok());

    // Suspender NÃO perde trabalho: o app pode ser morto em background a
    // qualquer momento, e o projeto tem que estar lá quando ele voltar.
    AUREA_CHECK(e.project() != nullptr);
    AUREA_CHECK_EQ(e.project()->metadata().title, std::string("Sobrevive"));
    const Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), static_cast<u32>(1));
    e.shutdown();
}

AUREA_TEST(Engine, RenderWithoutProjectFails) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(!e.render_frame().ok());
    e.shutdown();
}

AUREA_TEST(SceneCuts, CaptureRejectsThePreviousDeviceEvenWhenTheBackendPointerIsReused) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "device-generation").ok());
    TextureDesc desc;
    desc.width = desc.height = 64;
    desc.format = SurfaceFormat::RGBA16F;
    desc.sampled = desc.renderTarget = desc.transferSrc = true;
    const auto oldTarget = mock->create_texture(desc);
    AUREA_CHECK(oldTarget.ok());
    if (!oldTarget.ok()) return;
    // The first initialization is generation 1. Prove this target was accepted
    // before forcing recovery of the same backend object.
    AUREA_CHECK(e.render_offscreen(*oldTarget, 64, 64, false, 1).ok());
    mock->deviceLost = true;
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.gpu() == mock);
    AUREA_CHECK(!mock->deviceLost);
    const u32 submitted = mock->framesSubmitted;
    const Status stale = e.render_offscreen(*oldTarget, 64, 64, false, 1);
    AUREA_CHECK_EQ(stale.code(), Errc::Cancelled);
    AUREA_CHECK_EQ(mock->framesSubmitted, submitted);
    const auto newTarget = mock->create_texture(desc);
    AUREA_CHECK(newTarget.ok());
    if (newTarget.ok()) {
        AUREA_CHECK(e.render_offscreen(*newTarget, 64, 64).ok());
        mock->destroy_texture(*newTarget);
    }
    e.shutdown();
}

AUREA_TEST(RenderRecovery, RecordingFaultWaitsForAllWorkAndPreservesEditableProject) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30, "recoverable recording").ok());
    const auto id = e.add_shape(0); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* layer = comp->layer(LayerId::unpack(*id));
    layer->transform.position.x = 37;
    layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{30}, 51);
    mock->recordingFault = true; mock->recoveryReady = false;
    AUREA_CHECK_EQ(e.render_frame().code(), Errc::Timeout);
    AUREA_CHECK_EQ(mock->initializations, 1u); // no destruction/reuse while busy
    Command rename; rename.type = CommandType::LayerSetName;
    rename.layer_ref.layer = LayerId::unpack(*id);
    AUREA_CHECK(e.apply_command(rename, "edited while pending").ok());
    AUREA_CHECK_EQ(e.render_frame().code(), Errc::Timeout);
    AUREA_CHECK_EQ(mock->initializations, 1u);
    mock->recoveryReady = true;
    mock->failInitializations = 1;
    AUREA_CHECK_EQ(e.render_frame().code(), Errc::OutOfMemory);
    AUREA_CHECK_EQ(mock->initializations, 2u);
    AUREA_CHECK(e.render_frame().ok()); // failed recreation remains retryable
    AUREA_CHECK_EQ(mock->initializations, 3u);
    AUREA_CHECK(!mock->recordingFault && !mock->deviceLost);
    AUREA_CHECK_EQ(e.project()->metadata().title, std::string("recoverable recording"));
    comp = e.project()->timeline().composition(e.project()->timeline().current());
    layer = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK_EQ(layer->name, std::string("edited while pending"));
    AUREA_CHECK_NEAR(layer->transform.position.x, 37, 1e-5f);
    AUREA_CHECK_NEAR(layer->tracks.sample_or(TrackProperty::PositionX, FrameIndex{30}, 0), 51, 1e-5f);
    e.shutdown();
}

AUREA_TEST(Engine, UnknownEffectTypeIsRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command addEffect;
    addEffect.type = CommandType::EffectAdd;
    addEffect.effect_add.layer = id;
    addEffect.effect_add.effectType = 4242;   // nao registrado
    AUREA_CHECK(!e.apply_command(addEffect).ok());
    AUREA_CHECK_EQ(c->layer(id)->effects.size(), static_cast<usize>(0));
    e.shutdown();
}

AUREA_TEST(Engine, MaskOperationsOnMissingMaskAreRefused) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());

    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Video;
    (void)e.apply_command(create);

    Composition* c = e.project()->timeline().composition(
        e.project()->timeline().current());
    const LayerId id = c->order().at(0);

    Command maskOp;
    maskOp.type = CommandType::MaskSetOperation;
    maskOp.mask_op.layer = id;
    maskOp.mask_op.mask = MaskId{99, 1};
    maskOp.mask_op.op = MaskOperation::Subtract;
    AUREA_CHECK(!e.apply_command(maskOp).ok());
    e.shutdown();
}

// -----------------------------------------------------------------------------
// Histórico (desfazer/refazer por snapshot da composição)
// -----------------------------------------------------------------------------
namespace {

LayerId first_layer(Engine& e) {
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    LayerId id{};
    c->layers().for_each([&](LayerId lid, const Layer&) { if (!id.valid()) id = lid; });
    return id;
}

const Layer* layer_of(Engine& e, LayerId id) {
    return e.project()->timeline().composition(e.project()->timeline().current())->layer(id);
}

Command position_cmd(LayerId id, f32 x, f32 y) {
    Command c;
    c.type = CommandType::LayerSetPosition;
    c.position.layer = id;
    c.position.x = x;
    c.position.y = y;
    return c;
}

} // namespace

AUREA_TEST(History, UndoAndRedoRestoreTheExactValue) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(id.valid());

    AUREA_CHECK(e.apply_command(position_cmd(id, 100, 200)).ok());
    AUREA_CHECK(e.apply_command(position_cmd(id, 300, 400)).ok());
    AUREA_CHECK(e.read_status().canUndo);

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 100.0f, 1e-6f);
    AUREA_CHECK(e.read_status().canRedo);

    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 300.0f, 1e-6f);

    // Desfaz tudo, inclusive a criação: a layer some; refazer a devolve com o MESMO id.
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(layer_of(e, id) == nullptr);
    AUREA_CHECK(!e.read_status().canUndo);
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK(layer_of(e, id) != nullptr);
    e.shutdown();
}

AUREA_TEST(History, AGestureGroupUndoesAtOnce) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(e.apply_command(position_cmd(id, 10, 10)).ok());
    const u32 depthBefore = e.read_status().undoDepth;

    Command begin;
    begin.type = CommandType::UndoBeginGroup;
    AUREA_CHECK(e.apply_command(begin, "arrastar").ok());
    for (int i = 1; i <= 60; ++i) AUREA_CHECK(e.apply_command(position_cmd(id, 10.0f + i, 10.0f)).ok());
    Command end;
    end.type = CommandType::UndoEndGroup;
    AUREA_CHECK(e.apply_command(end).ok());
    AUREA_CHECK_EQ(e.read_status().undoDepth, depthBefore + 1);

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 10.0f, 1e-6f);
    e.shutdown();
}

AUREA_TEST(History, SplitAndEffectAreUndoable) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    Command range;
    range.type = CommandType::LayerSetTimeRange;
    range.layer_range.layer = id;
    range.layer_range.start = FrameIndex{0};
    range.layer_range.end = FrameIndex{90};
    AUREA_CHECK(e.apply_command(range).ok());

    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = id;
    split.layer_split.at = FrameIndex{30};
    AUREA_CHECK(e.apply_command(split).ok());
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), 2u);
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(30));

    Command fx;
    fx.type = CommandType::EffectAdd;
    fx.effect_add.layer = id;
    fx.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    fx.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(fx).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->effects.size(), static_cast<usize>(1));

    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->effects.size(), static_cast<usize>(0));
    AUREA_CHECK(e.apply_command(undo).ok());
    c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->layers().count(), 1u);
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(90));
    e.shutdown();
}

AUREA_TEST(History, DeleteAndFpsChangeAreUndoable) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    AUREA_CHECK(e.apply_command(position_cmd(id, 123, 45)).ok());
    Command range;
    range.type = CommandType::LayerSetTimeRange;
    range.layer_range.layer = id;
    range.layer_range.start = FrameIndex{0};
    range.layer_range.end = FrameIndex{90};
    AUREA_CHECK(e.apply_command(range).ok());

    // Apagar e desfazer: volta com o MESMO id e o mesmo estado.
    Command del;
    del.type = CommandType::LayerDelete;
    del.layer_ref.layer = id;
    AUREA_CHECK(e.apply_command(del).ok());
    AUREA_CHECK(layer_of(e, id) == nullptr);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(layer_of(e, id) != nullptr);
    AUREA_CHECK_NEAR(layer_of(e, id)->transform.position.x, 123.0f, 1e-6f);

    // 30 -> 60 fps preserva os segundos; desfazer volta a taxa E os frames.
    Command fps;
    fps.type = CommandType::CompositionSetFps;
    fps.comp_fps.comp = e.project()->timeline().current();
    fps.comp_fps.fps = 60.0;
    AUREA_CHECK(e.apply_command(fps).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(180));
    AUREA_CHECK_EQ(e.read_status().compFps, 60.0f);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layer_of(e, id)->end.value, static_cast<i64>(90));
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(c->fps(), 30.0);
    e.shutdown();
}

AUREA_TEST(History, TrimStartKeepsContentWithOffset) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    Command create;
    create.type = CommandType::LayerCreate;
    create.layer_create.kind = LayerKind::Shape;
    AUREA_CHECK(e.apply_command(create).ok());
    const LayerId id = first_layer(e);
    Command trim;
    trim.type = CommandType::LayerSetTimeRange;
    trim.layer_range.layer = id;
    trim.layer_range.start = FrameIndex{12};
    trim.layer_range.end = FrameIndex{60};
    trim.layer_range.offset = FrameIndex{12};
    trim.layer_range.setOffset = 1;
    AUREA_CHECK(e.apply_command(trim).ok());
    const Layer* l = layer_of(e, id);
    AUREA_CHECK_EQ(l->offset.value, static_cast<i64>(12));
    // O conteúdo não andou: o frame local no instante 20 continua 20.
    AUREA_CHECK_EQ(l->local_time(FrameIndex{20}).value, static_cast<i64>(20));

    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(id.pack(), d));
    AUREA_CHECK_EQ(d.startFrame, 12);
    AUREA_CHECK_EQ(d.offsetFrames, 12);
    e.shutdown();
}

AUREA_TEST(Engine, VideoImportPreservesChosenProjectResolutionAndFps) {
    for (const auto& [w, h] : {std::pair<u32,u32>{1080,1920}, {1920,1080}, {800,800}}) {
        test::SyntheticConfig cfg; cfg.width = 800; cfg.height = 800; cfg.fps = 24;
        test::SyntheticFactory factory(cfg);
        EngineConfig ec = headless_config(); ec.mediaFactory = &factory;
        Engine e;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(w, h, 60.0, nullptr).ok());
        VideoImport vi; vi.sourcePath = "sintetico";
        const auto id = e.import_video(vi); AUREA_CHECK(id.ok());
        const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        AUREA_CHECK_EQ(c->width(), w); AUREA_CHECK_EQ(c->height(), h); AUREA_CHECK_NEAR(c->fps(),60.0,0.001);
        if (id.ok()) {
            const Layer* layer = c->layer(LayerId::unpack(*id));
            AUREA_CHECK_NEAR(layer->transform.scale.x,layer->transform.scale.y,0.0001);
            AUREA_CHECK_NEAR(layer->transform.position.x,w*.5f,0.01);
            AUREA_CHECK_NEAR(layer->transform.position.y,h*.5f,0.01);
        }
        e.shutdown();
    }
}

namespace {
class SwitchingProbeFactory final : public VideoSourceFactory {
public:
    std::function<void()> duringProbe;
    bool probe(const char*, MediaProbe& out) override {
        out.hasVideo = out.hasAudio = true;
        out.video.codedWidth = 320; out.video.codedHeight = 180;
        out.video.fps = 30.; out.video.durationUs = 2'000'000;
        out.audioSampleRate = 48000; out.audioChannels = 2; out.audioDurationUs = 2'000'000;
        if (duringProbe) duringProbe();
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return nullptr; }
};
}

AUREA_TEST(ImportStability, MediaProbeCannotCommitIntoAReplacedProjectOrComposition) {
    for (bool audio : {false, true}) for (bool replaceProject : {false, true}) {
        SwitchingProbeFactory factory;
        Engine e; auto cfg = headless_config(); cfg.mediaFactory = &factory;
        AUREA_CHECK(e.initialize(cfg).ok());
        AUREA_CHECK(e.new_project(640, 360, 30., "original").ok());
        factory.duringProbe = [&] {
            if (replaceProject) {
                AUREA_CHECK(e.new_project(800, 600, 24., "replacement").ok());
            } else {
                auto& timeline = e.project()->timeline();
                const auto other = timeline.create_composition("other", 320, 240, 24.);
                AUREA_CHECK(timeline.set_current(other));
            }
        };
        VideoImport request; request.sourcePath = "slow-probe";
        const auto result = audio ? e.import_audio(request) : e.import_video(request);
        AUREA_CHECK(!result.ok());
        AUREA_CHECK_EQ(result.status().code(), Errc::Cancelled);
        AUREA_CHECK_EQ(e.project()->asset_count(), 0u);
        AUREA_CHECK_EQ(e.read_status().layerCount, 0u);
        AUREA_CHECK_EQ(e.read_status().undoDepth, 0u);
        e.shutdown();
    }
}

AUREA_TEST(Engine, ImagesComeBackWhenTheProjectIsReopened) {
    // A imagem importada guarda a origem; ao reabrir, o motor pede os pixels à
    // plataforma (imageLoader). Antes, a imagem só existia na sessão.
    static u32 loads = 0;
    loads = 0;
    EngineConfig ec = headless_config();
    ec.imageLoader = [](const char* src, ImagePixels& out, void*) {
        if (std::string(src) != "content://teste/imagem") return false;
        ++loads;
        out.width = 4;
        out.height = 2;
        out.rgba.assign(4 * 2 * 4, 200);
        return true;
    };
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    std::vector<u8> px(4 * 2 * 4, 200);
    const auto id = e.import_image(px.data(), 4, 2, "foto", "content://teste/imagem");
    AUREA_CHECK(id.ok());
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_imagem.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(loads, 1u);
    // A camada volta e a miniatura (feita dos pixels recarregados) existe.
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    LayerId lid{};
    c->layers().for_each([&](LayerId l, const Layer&) { lid = l; });
    std::vector<u8> thumb(64 * 64 * 4);
    u32 w = 0;
    AUREA_CHECK(e.query_thumbnail(lid.pack(), 0, 8, thumb.data(), static_cast<u32>(thumb.size()), &w) > 0);
    AUREA_CHECK_EQ(w, 16u);
    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(lid.pack(), d));
    AUREA_CHECK_EQ(d.sourceWidth, 4u);
    AUREA_CHECK_EQ(d.sourceHeight, 2u);
    e.shutdown();
    std::remove(path.c_str());
}

AUREA_TEST(Engine, QueuedStringsAreNotGluedToThePreviousOnes) {
    // As strings da fila ficam coladas no blob, sem terminador. Duas edições
    // de texto seguidas (a digitação da UI): a segunda não pode levar a
    // primeira junto.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    auto id = e.add_text("Texto");
    if (!id.ok()) { e.shutdown(); return; }   // sem fonte no host: nada a verificar
    const char* edits[] = {"Texto A", "Texto Au"};
    for (const char* s : edits) {
        u32 offset = 0, length = 0;
        AUREA_CHECK(e.commands().push_string(s, static_cast<u32>(std::strlen(s)), offset, length));
        Command c;
        c.type = CommandType::TextSetContent;
        c.layer_ref.layer = LayerId::unpack(*id);
        c.stringOffset = offset;
        c.stringLength = length;
        AUREA_CHECK_EQ(e.submit_commands(&c, 1), 1u);
    }
    AUREA_CHECK(e.render_frame().ok());
    TextData t;
    AUREA_CHECK(e.query_text(*id, t));
    AUREA_CHECK_EQ(t.content, std::string("Texto Au"));
    e.shutdown();
}

AUREA_TEST(Engine, BatchStringBlobOffsetsAreRebasedIntoTheQueue) {
    // O caminho da bridge: cada lote traz seu blob com deslocamentos a partir
    // de 0. Dois lotes seguidos com string: o segundo não pode ler a do primeiro.
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    auto id = e.add_text("Texto");
    if (!id.ok()) { e.shutdown(); return; }
    const char* blobs[] = {"editar texto", "Texto Aurea"};
    for (int i = 0; i < 2; ++i) {
        Command c;
        c.type = i == 0 ? CommandType::UndoBeginGroup : CommandType::TextSetContent;
        c.layer_ref.layer = LayerId::unpack(*id);
        c.stringOffset = 0;
        c.stringLength = static_cast<u32>(std::strlen(blobs[i]));
        AUREA_CHECK_EQ(e.submit_commands(&c, 1, blobs[i], c.stringLength), 1u);
    }
    AUREA_CHECK(e.render_frame().ok());
    TextData t;
    AUREA_CHECK(e.query_text(*id, t));
    AUREA_CHECK_EQ(t.content, std::string("Texto Aurea"));
    e.shutdown();
}

AUREA_TEST(PreviewGesture, InvalidQueuedTextPayloadDoesNotEraseTheLayer) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    auto id = e.add_text("Keep this text");
    AUREA_CHECK(id.ok());
    if (!id.ok()) { e.shutdown(); return; }
    Command c;
    c.type = CommandType::TextSetContent;
    c.layer_ref.layer = LayerId::unpack(*id);
    c.stringOffset = 20;
    c.stringLength = 4;
    AUREA_CHECK_EQ(e.submit_commands(&c, 1, "oops", 4), 0u);
    // Arena offsets (without a batch blob) must also be checked at consumption.
    c.stringOffset = 0xFFFFFFF0u;
    AUREA_CHECK_EQ(e.submit_commands(&c, 1), 1u);
    AUREA_CHECK(e.render_frame().ok());
    TextData text;
    AUREA_CHECK(e.query_text(*id, text));
    AUREA_CHECK_EQ(text.content, std::string("Keep this text"));
    // Intentional empty input still works.
    c.stringOffset = 0; c.stringLength = 0;
    AUREA_CHECK_EQ(e.submit_commands(&c, 1), 1u);
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.query_text(*id, text));
    AUREA_CHECK(text.content.empty());
    e.shutdown();
}

// =============================================================================
// 7H — papel e organização da camada: ajuste, guia, etiqueta, solo, busca
// =============================================================================
AUREA_TEST(Engine, LayerRoleFlagsReachTheRowsAndUndo) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    const auto b = e.add_shape(10);
    AUREA_CHECK(a.ok() && b.ok());

    AUREA_CHECK(e.set_layer_adjustment(*b, true));
    AUREA_CHECK(e.set_layer_guide(*a, true));
    AUREA_CHECK(e.set_layer_label(*a, 5));
    AUREA_CHECK(!e.set_layer_label(*a, kLayerLabelCount));   // fora da paleta: recusa
    AUREA_CHECK(e.set_layer_solo(*b, true));
    AUREA_CHECK(!e.set_layer_guide(0xDEADBEEFull, true));

    bridge::LayerRow rows[4];
    char names[256];
    const u32 n = e.query_layers(rows, 4, names, sizeof(names));
    AUREA_CHECK_EQ(n, 2u);
    // A frente primeiro: b, depois a.
    AUREA_CHECK_EQ(rows[0].id, *b);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagAdjustment) != 0);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagSolo) != 0);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagGuide) == 0);
    AUREA_CHECK((rows[1].flags & bridge::kLayerRowFlagGuide) != 0);
    AUREA_CHECK_EQ((rows[1].flags & bridge::kLayerRowLabelMask) >> bridge::kLayerRowLabelShift, 5u);
    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(*b, d));
    AUREA_CHECK((d.flags & bridge::kLayerRowFlagAdjustment) != 0 && (d.flags & bridge::kLayerRowFlagSolo) != 0);

    // Cada troca é um passo de desfazer: o último (solo) volta primeiro.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(!layer_of(e, LayerId::unpack(*b))->solo);
    AUREA_CHECK(layer_of(e, LayerId::unpack(*b))->adjustment);
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(layer_of(e, LayerId::unpack(*a))->label, static_cast<u8>(0));
    e.shutdown();
}

AUREA_TEST(Engine, SearchLayersFoldsCaseAndAccents) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    const auto b = e.add_shape(10);
    const auto t = e.add_shape(10);
    AUREA_CHECK(a.ok() && b.ok() && t.ok());
    Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    c->layer(LayerId::unpack(*a))->name = "TÍTULO Principal";
    c->layer(LayerId::unpack(*b))->name = "Fundo azul";
    // Texto: acha pelo conteúdo, não só pelo nome.
    Layer* tl = c->layer(LayerId::unpack(*t));
    tl->name = "Texto";
    tl->kind = LayerKind::Text;
    tl->text.content = "Coração de São Paulo";

    auto ids = e.search_layers("titulo");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *a);
    ids = e.search_layers("PRINCIPAL");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *a);
    ids = e.search_layers("sao paulo");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *t);
    ids = e.search_layers("CORAÇÃO");
    AUREA_CHECK(ids.size() == 1 && ids[0] == *t);
    ids = e.search_layers("u");   // as três: da frente para o fundo
    AUREA_CHECK(ids.size() == 3 && ids[0] == *t && ids[1] == *b && ids[2] == *a);
    AUREA_CHECK(e.search_layers("").empty());
    AUREA_CHECK(e.search_layers("xyz").empty());
    e.shutdown();
}

// =============================================================================
// Ficha do catálogo de efeitos (Fase 7.3 §20, §68)
//
// `query_effect_specs` é o que o navegador mostra antes de existir camada: a
// DECLARAÇÃO dos parâmetros de um tipo. Sem camada não há valor corrente, e o
// contrato é que `value` saia com o padrão — quem lê a ficha não pode ver lixo
// de memória nem um valor que o efeito nunca teria.
// =============================================================================
AUREA_TEST(Engine, EffectSpecsDescribeEveryParameterOfAType) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());

    const EffectTypeId gaussian = effect_type_id(effect_keys::kGaussianBlur);
    std::vector<bridge::EffectParamRow> rows(16);
    std::vector<char> blob(8 * 1024);
    const u32 n = e.query_effect_specs(gaussian, rows.data(), static_cast<u32>(rows.size()), blob.data(),
                                        static_cast<u32>(blob.size()));
    AUREA_CHECK(n > 0);

    // O efeito declara os parâmetros na mesma ordem; o índice é o contrato com
    // o projeto (é ele que vai no keyframe).
    for (u32 i = 0; i < n; ++i) {
        AUREA_CHECK_EQ(rows[i].index, i);
        AUREA_CHECK(rows[i].labelLength > 0);                 // sem rótulo a UI fica muda
        AUREA_CHECK(rows[i].minValue <= rows[i].maxValue);
        // A faixa digitada (teclado) sempre contém a do slider.
        AUREA_CHECK(rows[i].hardMin <= rows[i].minValue && rows[i].maxValue <= rows[i].hardMax);
        for (int c = 0; c < 4; ++c) {
            AUREA_CHECK_EQ(rows[i].value[c], rows[i].defaultValue[c]);
        }
        // Sem instância não há keyframe: a ficha nunca mente dizendo "animado".
        AUREA_CHECK_EQ(rows[i].animated, 0u);
    }
    // "Desfoque" (índice 0): slider até 500 px, digitado além (edição extrema).
    AUREA_CHECK_EQ(rows[0].maxValue, 500.0f);
    AUREA_CHECK(rows[0].hardMax > rows[0].maxValue);
    AUREA_CHECK_EQ(rows[0].hardMin, rows[0].minValue);

    // Tipo que não existe: nenhuma linha, sem escrever nada.
    AUREA_CHECK_EQ(e.query_effect_specs(0u, rows.data(), static_cast<u32>(rows.size()), blob.data(),
                                        static_cast<u32>(blob.size())), 0u);
    // Capacidade menor que o número de parâmetros: corta em vez de estourar.
    AUREA_CHECK_EQ(e.query_effect_specs(gaussian, rows.data(), 1u, blob.data(),
                                        static_cast<u32>(blob.size())), 1u);
    e.shutdown();
}

AUREA_TEST(Engine, EffectSpecsCoverTheWholeCatalog) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());

    std::vector<bridge::EffectCatalogRow> catalog(64);
    std::vector<char> blob(1024 * 1024);
    u32 count = 0;
    for (;;) {
        count = e.query_effect_catalog(catalog.data(), static_cast<u32>(catalog.size()), blob.data(),
                                      static_cast<u32>(blob.size()));
        if (count < catalog.size()) break;
        AUREA_CHECK(catalog.size() < 16384);
        if (catalog.size() >= 16384) { e.shutdown(); return; }
        catalog.resize(catalog.size() * 2);
    }
    AUREA_CHECK(count > 0);

    std::vector<bridge::EffectParamRow> rows(64);
    std::vector<char> specBlob(32 * 1024);
    u32 withParams = 0;
    for (u32 i = 0; i < count; ++i) {
        rows.resize(std::max(1u, catalog[i].paramCount));
        const u32 n = e.query_effect_specs(catalog[i].typeId, rows.data(), static_cast<u32>(rows.size()),
                                           specBlob.data(), static_cast<u32>(specBlob.size()));
        // O catálogo diz quantos parâmetros o efeito tem; a ficha tem de bater.
        AUREA_CHECK_EQ(n, catalog[i].paramCount);
        // Toda linha da ponte: hardMin <= min <= max <= hardMax.
        for (u32 k = 0; k < n && k < rows.size(); ++k) {
            AUREA_CHECK(rows[k].hardMin <= rows[k].minValue && rows[k].minValue <= rows[k].maxValue
                        && rows[k].maxValue <= rows[k].hardMax);
        }
        if (n > 0 && n <= rows.size()) ++withParams;
    }
    // Todo efeito do motor tem ficha — nenhum entra no catálogo mudo.
    AUREA_CHECK_EQ(withParams, count);
    e.shutdown();
}

AUREA_TEST(Engine, ParentingAnimatedLayersPreserveTracksAndWorldSamples) {
    for (bool threeD : {false, true}) for (const u32 initialTracks : {1u, 15u, 16u}) {
        Engine e;
        AUREA_CHECK(e.initialize(headless_config()).ok());
        AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
        const auto parentId = e.add_null(threeD);
        const auto childId = e.add_null(threeD);
        AUREA_CHECK(parentId.ok() && childId.ok());
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* parent = comp->layer(LayerId::unpack(*parentId));
        Layer* child = comp->layer(LayerId::unpack(*childId));
        parent->transform.position = Vec3{300, 200, threeD ? 70.f : 0.f};
        parent->transform.rotation.z = 30;
        parent->transform.scale = Vec3{2, 2, 2};
        parent->transform.anchor = Vec3{0, 0, 0};
        child->transform.position = Vec3{500, 400, threeD ? 90.f : 0.f};
        child->transform.anchor = Vec3{0, 0, 0};
        Track& x = child->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{0}, 500.0f);
        x.set(FrameIndex{30}, 800.0f);
        for (u32 i = 1; i < initialTracks; ++i) {
            child->tracks.set_static(TrackProperty::EffectParam, static_cast<f32>(i), i, 0);
        }
        if (initialTracks == 1) {
            // History copies need not retain the default 16-track spare capacity.
            TrackSet copied = child->tracks;
            child->tracks = std::move(copied);
        }
        Vec4 expected[31];
        for (i64 frame = 0; frame <= 30; ++frame) {
            expected[frame] = layer_world_3d(*comp, *child, FrameIndex{frame}) * Vec4{0, 0, 0, 1};
        }
        Command command;
        command.type = CommandType::LayerSetParent;
        command.layer_parent.layer = LayerId::unpack(*childId);
        command.layer_parent.parent = LayerId::unpack(*parentId);
        AUREA_CHECK(e.apply_command(command).ok());
        // 3D retains authored tracks verbatim; 2D still compensates X/Y
        // and must safely grow storage when rotating an animated X track.
        AUREA_CHECK_EQ(child->tracks.size(), initialTracks + (threeD ? 0u : 1u));
        for (const TrackProperty property : {TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ}) {
            const Track* track = child->tracks.find(property);
            if (property == TrackProperty::PositionZ || (threeD && property == TrackProperty::PositionY)) {
                AUREA_CHECK(track == nullptr);
                continue;
            }
            AUREA_CHECK(track != nullptr);
            if (track) AUREA_CHECK_EQ(track->keys.size(), static_cast<usize>(2));
        }
        for (i64 frame = 30; frame >= 0; --frame) {
            const Vec4 actual = layer_world_3d(*comp, *child, FrameIndex{frame}) * Vec4{0, 0, 0, 1};
            AUREA_CHECK_NEAR(actual.x, expected[frame].x, 0.002f);
            AUREA_CHECK_NEAR(actual.y, expected[frame].y, 0.002f);
            AUREA_CHECK_NEAR(actual.z, expected[frame].z, 0.002f);
        }
        for (u32 i = 1; i < initialTracks; ++i) {
            AUREA_CHECK_NEAR(child->tracks.find(TrackProperty::EffectParam, i, 0)->staticValue,
                             static_cast<f32>(i), 1e-6);
        }
        e.shutdown();
    }
}

AUREA_TEST(Engine, LegacyEffectControlsExpandOnLoadWithoutLosingKeys) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320,180,30,"legacy effects").ok());
    scene3d::Text3DSpec text; text.content = "Legacy";
    const auto added=e.add_text3d(text); AUREA_CHECK(added.ok()); if (!added.ok()) return;
    const auto id=LayerId::unpack(*added);
    auto layer=[&]() { return e.project()->timeline().composition(e.project()->timeline().current())->layer(id); };
    const char* keys[]={"aurea.distort.shake","aurea.light.sweep","aurea.glitch.glitchify","aurea.text3d.layout"};
    // Shake: 15 → 17 com direção e decaimento (acrescentados no fim, neutros).
    const u32 oldCounts[]={8,9,17,9}, newCounts[]={17,13,35,14};
    for(u32 i=0;i<4;++i) {
        Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=id;
        add.effect_add.effectType=effect_type_id(keys[i]);add.effect_add.index=kInvalidIndex;
        const auto status=e.apply_command(add); AUREA_CHECK(status.ok()); if (!status.ok()) return;
        auto& fx=layer()->effects.back();fx.params.resize(oldCounts[i]);fx.params[0].constant.v[0]=7;
        layer()->tracks.get_or_create(TrackProperty::EffectParam,fx.id,0).set(FrameIndex{12},19);
    }
    const char* path="build/prompt03/legacy-controls.aurea";
    AUREA_CHECK(e.save_project(path).ok());AUREA_CHECK(e.load_project(path).ok());
    for(u32 i=0;i<4;++i) {
        auto& fx=layer()->effects[i];AUREA_CHECK_EQ(fx.params.size(),newCounts[i]);
        AUREA_CHECK_NEAR(fx.params[0].constant.v[0],7,.0001f);
        const auto* tr=layer()->tracks.find(TrackProperty::EffectParam,fx.id,0);
        AUREA_CHECK(tr && tr->find_exact(FrameIndex{12})!=kInvalidIndex);
        Command change;change.type=CommandType::EffectSetParam;change.effect_param.layer=id;
        change.effect_param.effect=EffectId{fx.id,0};change.effect_param.paramIndex=newCounts[i]-1;change.effect_param.value=1;
        AUREA_CHECK(e.apply_command(change).ok());
        AUREA_CHECK_NEAR(fx.params.back().constant.v[0],1,.0001f);
    }
    AUREA_CHECK(e.save_project(path).ok());AUREA_CHECK(e.load_project(path).ok());
    for(u32 i=0;i<4;++i)AUREA_CHECK_NEAR(layer()->effects[i].params.back().constant.v[0],1,.0001f);
}

AUREA_TEST(DropShadow, LegacyProjectAndShadowOnlyRoundTripPreserveParameterAddresses) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "shadow persistence").ok());
    const auto added = e.add_shape(0); AUREA_CHECK(added.ok()); if (!added.ok()) return;
    const auto id = LayerId::unpack(*added);
    auto layer = [&]() { return e.project()->timeline().composition(e.project()->timeline().current())->layer(id); };
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = id;
    add.effect_add.effectType = effect_type_id(effect_keys::kDropShadow); add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(add).ok());
    auto& original = layer()->effects.back();
    AUREA_CHECK_EQ(original.params.size(), 6u);
    original.params.resize(5); // actual previous saved layout
    original.params[3].constant = ParamValue::scalar(42);
    layer()->tracks.get_or_create(TrackProperty::EffectParam, original.id, param_track_key(3, 0))
        .set(FrameIndex{12}, 67);
    const char* path = "build/prompt03/drop-shadow-legacy.aurea";
    AUREA_CHECK(e.save_project(path).ok()); AUREA_CHECK(e.load_project(path).ok());
    const auto& loaded = layer()->effects.back();
    AUREA_CHECK_EQ(loaded.params.size(), 6u);
    AUREA_CHECK(!loaded.params[5].constant.as_bool());
    AUREA_CHECK_NEAR(loaded.params[3].constant.v[0], 42, .001);
    const auto* distance = layer()->tracks.find(TrackProperty::EffectParam, loaded.id, param_track_key(3, 0));
    AUREA_CHECK(distance && distance->keys.size() == 1 && distance->keys[0].value == 67);
    Command toggle; toggle.type = CommandType::EffectSetParam; toggle.effect_param.layer = id;
    toggle.effect_param.effect = EffectId{loaded.id, 0}; toggle.effect_param.paramIndex = 5;
    toggle.effect_param.value = 1;
    AUREA_CHECK(e.apply_command(toggle).ok());
    AUREA_CHECK(layer()->effects.back().params[5].constant.as_bool());
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(!layer()->effects.back().params[5].constant.as_bool());
    Command redo; redo.type = CommandType::Redo; AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK(layer()->effects.back().params[5].constant.as_bool());
    AUREA_CHECK(e.save_project(path).ok()); AUREA_CHECK(e.load_project(path).ok());
    AUREA_CHECK(layer()->effects.back().params[5].constant.as_bool());
    AUREA_CHECK_NEAR(layer()->effects.back().params[3].constant.v[0], 42, .001);
}

AUREA_TEST(Engine, OldMotionTileProjectLoadsInTheNewLayout) {
    // Projeto gravado com o Motion Tile anterior (9 slots): abre com a mesma
    // chave, convertido para os controles novos, keyframes junto, e desenha.
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30, "old motion tile").ok());
    const auto added = e.add_shape(0); AUREA_CHECK(added.ok()); if (!added.ok()) return;
    const auto id = LayerId::unpack(*added);
    auto layer = [&]() { return e.project()->timeline().composition(e.project()->timeline().current())->layer(id); };
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = id;
    add.effect_add.effectType = effect_type_id(effect_keys::kMotionTile); add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(add).ok());
    auto& fx = layer()->effects.back();
    AUREA_CHECK_EQ(fx.params.size(), 11u);
    AUREA_CHECK_NEAR(fx.params[10].constant.v[0], 100.0f, 1e-6);
    fx.params.resize(9);                            // a disposição anterior
    fx.params[1].constant.v[0] = 50.0f;             // ladrilho 50%
    fx.params[3].constant.v[0] = 300.0f;            // saída 300% (só ampliava)
    fx.params[7].constant.v[0] = 180.0f;            // tijolo (colunas alternadas)
    const u32 fxId = fx.id;
    layer()->tracks.get_or_create(TrackProperty::EffectParam, fxId, param_track_key(7, 0)).set(FrameIndex{12}, 90.0f);
    const char* path = "build/prompt03/old-motion-tile.aurea";
    AUREA_CHECK(e.save_project(path).ok()); AUREA_CHECK(e.load_project(path).ok());
    auto& back = layer()->effects.back();
    AUREA_CHECK_EQ(back.type, effect_type_id(effect_keys::kMotionTile));
    AUREA_CHECK_EQ(back.params.size(), 11u);
    AUREA_CHECK_NEAR(back.params[10].constant.v[0], 100.0f, 1e-6);
    AUREA_CHECK_NEAR(back.params[1].constant.v[0], 50.0f, 1e-4f);
    AUREA_CHECK_NEAR(back.params[3].constant.v[0], 100.0f, 1e-4f);
    AUREA_CHECK_NEAR(back.params[7].constant.v[0], -180.0f, 1e-4f);
    AUREA_CHECK(back.params[8].constant.as_bool());
    const Track* phase = layer()->tracks.find(TrackProperty::EffectParam, back.id, param_track_key(7, 0));
    AUREA_CHECK(phase && phase->keys.size() == 1 && std::fabs(phase->keys[0].value + 90.0f) < 1e-4f);
    AUREA_CHECK(e.render_frame().ok());
    // Salvo de novo, não converte duas vezes.
    AUREA_CHECK(e.save_project(path).ok()); AUREA_CHECK(e.load_project(path).ok());
    AUREA_CHECK_NEAR(layer()->effects.back().params[7].constant.v[0], -180.0f, 1e-4f);
    AUREA_CHECK(layer()->effects.back().params[8].constant.as_bool());
}

AUREA_TEST(MediaLab, FiveEffectsSaveReopenKeysAndUndo) {
    Engine e;AUREA_CHECK(e.initialize(headless_config()).ok());AUREA_CHECK(e.new_project(320,180,30,"Media Lab").ok());
    const auto added=e.add_shape(0);AUREA_CHECK(added.ok());if(!added.ok())return;const auto id=LayerId::unpack(*added);
    auto layer=[&](){return e.project()->timeline().composition(e.project()->timeline().current())->layer(id);};
    const char* keys[]={effect_keys::kJpegGlitch,effect_keys::kAnalogSignal,effect_keys::kDeepGlow2,effect_keys::kShadowStudio3,effect_keys::kTracery};
    for(auto key:keys){Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=id;add.effect_add.effectType=effect_type_id(key);add.effect_add.index=kInvalidIndex;AUREA_CHECK(e.apply_command(add).ok());
        auto& fx=layer()->effects.back();layer()->tracks.get_or_create(TrackProperty::EffectParam,fx.id,param_track_key(1,0)).set(FrameIndex{12},17);
    }
    AUREA_CHECK_EQ(layer()->effects.size(),5u);const char* path="build/effects-packages/five-effects.aurea";
    AUREA_CHECK(e.save_project(path).ok());AUREA_CHECK(e.load_project(path).ok());
    for(u32 i=0;i<5;++i){auto& fx=layer()->effects[i];AUREA_CHECK_EQ(fx.type,effect_type_id(keys[i]));const auto* track=layer()->tracks.find(TrackProperty::EffectParam,fx.id,param_track_key(1,0));AUREA_CHECK(track&&track->find_exact(FrameIndex{12})!=kInvalidIndex);}
    Command change;change.type=CommandType::EffectSetParam;change.effect_param.layer=id;change.effect_param.effect=EffectId{layer()->effects[0].id,0};change.effect_param.paramIndex=0;change.effect_param.value=75;
    const f32 before=layer()->effects[0].params[0].constant.v[0];AUREA_CHECK(e.apply_command(change).ok());AUREA_CHECK_NEAR(layer()->effects[0].params[0].constant.v[0],75,.001f);
    Command undo;undo.type=CommandType::Undo;AUREA_CHECK(e.apply_command(undo).ok());AUREA_CHECK_NEAR(layer()->effects[0].params[0].constant.v[0],before,.001f);
}

// "Adicionar um marcador faz o áudio ou vídeo começar do início": marcar (ou
// desmarcar, editar, apagar) nunca mexe no transporte. Tocando, a marca cai no
// relógio e o play segue de onde estava; parado, o cabeçote fica onde está.
AUREA_TEST(Engine, MarkersNeverMoveOrRestartPlayback) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    auto apply = [&](CommandType type, i64 frame = 0) {
        Command c;
        c.type = type;
        c.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    };
    apply(CommandType::PlaybackSeek, 60);
    apply(CommandType::PlaybackPlay);
    const u64 t0 = monotonic_ns();
    while (monotonic_ns() - t0 < 80'000'000ull) {}
    AUREA_CHECK(e.render_frame().ok());
    const i64 before = e.project()->timeline().playhead().value;
    AUREA_CHECK(before >= 60);
    // A UI com status atrasado pede "toggle" no cabeçote velho (0): tocando,
    // vale o relógio — nada de pausa, seek ou marca no começo.
    AUREA_CHECK(e.toggle_marker(0));
    AUREA_CHECK(e.read_status().playing);
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(e.project()->timeline().playhead().value >= before);
    i64 marks[3 * 8] = {};
    AUREA_CHECK_EQ(e.query_markers(marks, 8), 1u);
    AUREA_CHECK(marks[0] >= 60);

    apply(CommandType::PlaybackPause);
    apply(CommandType::PlaybackSeek, 45);
    AUREA_CHECK(e.render_frame().ok());
    const EngineStatus status = e.read_status();
    AUREA_CHECK(e.toggle_marker(45));
    AUREA_CHECK(e.edit_marker(45, 50, 0xFF00FF00u, "refrao"));
    AUREA_CHECK(e.delete_marker(50));
    AUREA_CHECK(e.render_frame().ok());
    AUREA_CHECK(!e.read_status().playing);
    AUREA_CHECK_EQ(e.project()->timeline().playhead().value, static_cast<i64>(45));
    AUREA_CHECK_EQ(e.read_status().playhead.value, status.playhead.value);
    e.shutdown();
}
// "Só dá pra usar 3 objetos nulos" (iOS): cada nulo novo nasce com nome
// próprio (Nulo, Nulo 2…), fora do ponto de um nulo parado já ali (o palco só
// pega o de cima de uma pilha) e a fila de comandos entra antes — um desfazer
// ainda na fila não pode apagar o nulo que acabou de nascer.
AUREA_TEST(Engine, ManyNullsAreDistinctTappableAndSurviveAQueuedUndo) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30.0, nullptr).ok());
    std::vector<u64> ids;
    for (int i = 0; i < 6; ++i) {
        const auto id = e.add_null(i >= 4);
        AUREA_CHECK(id.ok());
        if (id.ok()) ids.push_back(*id);
    }
    AUREA_CHECK_EQ(ids.size(), static_cast<usize>(6));
    Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    std::vector<std::string> names;
    std::vector<Vec3> spots;
    for (u64 id : ids) {
        const Layer* l = c->layer(LayerId::unpack(id));
        AUREA_CHECK(l && l->kind == LayerKind::Null);
        if (!l) continue;
        names.push_back(l->name);
        spots.push_back(l->transform.position);
    }
    AUREA_CHECK(names.size() == 6 && names[0] == "Nulo" && names[1] == "Nulo 2" && names[3] == "Nulo 4");
    AUREA_CHECK(names.size() == 6 && names[4] == "Nulo 3D" && names[5] == "Nulo 3D 2");
    AUREA_CHECK(!spots.empty() && std::fabs(spots[0].x - 540.0f) < 0.01f && std::fabs(spots[0].y - 960.0f) < 0.01f);
    for (usize a = 0; a < spots.size(); ++a)
        for (usize b = a + 1; b < spots.size(); ++b)
            AUREA_CHECK(std::fabs(spots[a].x - spots[b].x) >= 8.0f || std::fabs(spots[a].y - spots[b].y) >= 8.0f);
    // Todas na lista que a UI lê, cada uma na SUA linha da timeline.
    bridge::LayerRow rows[16]{};
    char blob[1024]{};
    AUREA_CHECK_EQ(e.query_layers(rows, 16, blob, sizeof blob), 6u);
    for (u32 a = 0; a < 6; ++a)
        for (u32 b = a + 1; b < 6; ++b) AUREA_CHECK(rows[a].trackId != rows[b].trackId);
    // Desfazer na fila (o botão do iOS só enfileira) e, logo depois, mais um nulo.
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK_EQ(e.submit_commands(&undo, 1, nullptr, 0), 1u);
    const auto seventh = e.add_null(false);
    AUREA_CHECK(seventh.ok());
    // Uma consulta que aplica a fila (a pilha de efeitos) — sem o dreno em
    // add_null, o desfazer rodaria AQUI e levaria o nulo novo.
    bridge::LayerEffectRow fx[2]{};
    char fxNames[128]{};
    (void)e.query_layer_effects(ids[0], fx, 2, fxNames, sizeof fxNames);
    AUREA_CHECK_EQ(e.query_layers(rows, 16, blob, sizeof blob), 6u);   // 6 − 1 (desfeito) + 1 (novo)
    AUREA_CHECK(seventh.ok() && c->layer(LayerId::unpack(*seventh)) != nullptr);
    e.shutdown();
}

// Pedido 2026-10-02 (iOS): "vincula mas não mexe" e "o nulo normal faz o objeto
// vinculado sair da cena". Mesmos comandos das telas
// (LayerSetParent + LayerSetPosition), com o texto ligado ao 1º e ao 4º nulo,
// 2D e 3D. O filho não pode pular ao ganhar o pai e tem de seguir o pai.
AUREA_TEST(Engine, NullParentKeepsChildInPlaceAndDragsItForEveryNull) {
    // childMode: 0 = texto 2D, 1 = texto feito 3D (o caso da cena 3D).
    for (int childMode = 0; childMode < 2; ++childMode)
    for (int threeD = 0; threeD < 2; ++threeD) {
        for (int which : {0, 3}) {
            Engine e;
            AUREA_CHECK(e.initialize(headless_config()).ok());
            AUREA_CHECK(e.new_project(1080, 1920, 30.0, nullptr).ok());
            const auto text = e.add_text("ABC");
            AUREA_CHECK(text.ok());
            std::vector<u64> nulls;
            for (int i = 0; i < 4; ++i) { const auto n = e.add_null(threeD != 0); AUREA_CHECK(n.ok()); if (n.ok()) nulls.push_back(*n); }
            if (!text.ok() || nulls.size() != 4) { e.shutdown(); continue; }
            Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
            Layer* child = c->layer(LayerId::unpack(*text));
            AUREA_CHECK(child != nullptr);
            if (!child) { e.shutdown(); continue; }
            child->threeD = childMode == 1;
            const FrameIndex t0{0};
            auto origin = [&](const Layer& l) {
                const Mat4 m = (child->threeD || l.threeD || threeD) ? layer_world_3d(*c, l, t0) : layer_world_matrix(*c, l, t0);
                return m * Vec4{l.transform.anchor.x, l.transform.anchor.y, 0, 1};
            };
            const Vec4 before = origin(*child);
            Command link; link.type = CommandType::LayerSetParent;
            link.layer_parent.layer = LayerId::unpack(*text);
            link.layer_parent.parent = LayerId::unpack(nulls[which]);
            AUREA_CHECK(e.apply_command(link).ok());
            AUREA_CHECK(child->parent == LayerId::unpack(nulls[which]));
            const Vec4 linked = origin(*child);
            const f32 jump = std::max(std::fabs(linked.x - before.x), std::fabs(linked.y - before.y));
            // Mover o nulo +120 px em X (o arrasto do palco grava a posição).
            const Layer* n = c->layer(LayerId::unpack(nulls[which]));
            Command mv; mv.type = CommandType::LayerSetPosition;
            mv.position.layer = LayerId::unpack(nulls[which]);
            mv.position.x = n->transform.position.x + 120.0f; mv.position.y = n->transform.position.y; mv.position.z = n->transform.position.z;
            AUREA_CHECK(e.apply_command(mv).ok());
            const Vec4 moved = origin(*child);
            std::printf("    filho %s, nulo %s #%d: pulo %.2f px, seguiu %.2f px\n", childMode ? "3D" : "2D", threeD ? "3D" : "2D", which + 1, jump, moved.x - linked.x);
            AUREA_CHECK(jump < 0.5f);
            AUREA_CHECK(std::fabs((moved.x - linked.x) - 120.0f) < 0.5f && std::fabs(moved.y - linked.y) < 0.5f);
            e.shutdown();
        }
    }
}

// Curvas com parâmetros pela fila de comandos (o caminho das duas UIs): o
// motor aceita o tipo novo, grava os parâmetros e um desfazer volta a curva.
AUREA_TEST(Engine, ParametricEasingCommandIsValidatedAndUndoable) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    const auto added = e.add_shape(0);
    AUREA_CHECK(added.ok());
    const auto layer = LayerId::unpack(*added);
    Command insert;
    insert.type = CommandType::KeyframeInsert;
    insert.keyframe.track = TrackRef{layer, TrackProperty::Opacity, kInvalidIndex, 0};
    insert.keyframe.time = FrameIndex{0}; insert.keyframe.value = 0.0f;
    AUREA_CHECK(e.apply_command(insert).ok());
    insert.keyframe.time = FrameIndex{30}; insert.keyframe.value = 1.0f;
    AUREA_CHECK(e.apply_command(insert).ok());
    auto ease = [&](Interpolation kind, f32 a, f32 b) {
        Command c;
        c.type = CommandType::KeyframeSetInterpolation;
        c.keyframe_interp.track = insert.keyframe.track;
        c.keyframe_interp.time = FrameIndex{0};
        c.keyframe_interp.interp = kind;
        c.keyframe_interp.bx1 = a; c.keyframe_interp.by1 = b;
        c.keyframe_interp.bx2 = 1.0f; c.keyframe_interp.by2 = kEaseParamMarker;
        return e.apply_command(c);
    };
    AUREA_CHECK(ease(Interpolation::Overshoot, 0.6f, 0.0f).ok());
    float h[4]{}; u8 power = 0;
    AUREA_CHECK(e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::Opacity), kInvalidIndex, 0, 0, h, &power));
    AUREA_CHECK_EQ(h[0], 0.6f);
    AUREA_CHECK_EQ(h[3], kEaseParamMarker);
    AUREA_CHECK(ease(Interpolation::Elastic, 0.5f, 0.75f).ok());
    AUREA_CHECK(e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::Opacity), kInvalidIndex, 0, 0, h, &power));
    AUREA_CHECK_EQ(h[0], 0.5f);
    AUREA_CHECK_EQ(h[1], 0.75f);
    // Um tipo além do último conhecido é recusado (não vira índice de tabela).
    AUREA_CHECK(!ease(static_cast<Interpolation>(static_cast<u8>(kLastInterpolation) + 1), 0.5f, 0.5f).ok());
    // Desfazer volta ao Overshoot, desfazer de novo à reta original.
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(e.query_keyframe_easing(*added, static_cast<u32>(TrackProperty::Opacity), kInvalidIndex, 0, 0, h, &power));
    AUREA_CHECK_EQ(h[0], 0.6f);
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Track* tr = comp ? comp->layer(layer)->tracks.find(TrackProperty::Opacity) : nullptr;
    AUREA_CHECK(tr && tr->keys[0].interp == Interpolation::Overshoot);
    AUREA_CHECK(e.apply_command(undo).ok());
    comp = e.project()->timeline().composition(e.project()->timeline().current());
    tr = comp ? comp->layer(layer)->tracks.find(TrackProperty::Opacity) : nullptr;
    AUREA_CHECK(tr && tr->keys[0].interp == Interpolation::Linear);
    e.shutdown();
}

// Beta: "o Y não acompanha o X quando aplico o gráfico". Posição X 0→100 e
// Y 0→300 nos MESMOS frames; a curva vai pelo comando que as duas UIs mandam
// (KeyframeSetInterpolation com `linkAxes`) endereçado só ao X. O progresso
// normalizado de X e Y tem de ser igual em todo frame, para toda família de
// curva; sem `linkAxes` (o gráfico editando uma dimensão) só o X muda.
AUREA_TEST(Engine, CurveOnPositionEasesEveryAxisWithTheSameTiming) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(1280, 720, 30.0, nullptr).ok());
    const auto added = e.add_shape(0);
    AUREA_CHECK(added.ok());
    const auto layer = LayerId::unpack(*added);
    auto key = [&](TrackProperty p, i64 t, f32 v, u32 param = 0) {
        Command c;
        c.type = CommandType::KeyframeInsert;
        c.keyframe.track = TrackRef{layer, p, kInvalidIndex, param};
        c.keyframe.time = FrameIndex{t}; c.keyframe.value = v;
        return e.apply_command(c).ok();
    };
    // X 0→100 e Y 0→300 em 0..30; o Y também segue para 60 (o trecho seguinte não muda).
    AUREA_CHECK(key(TrackProperty::PositionX, 0, 0.f) && key(TrackProperty::PositionX, 30, 100.f));
    AUREA_CHECK(key(TrackProperty::PositionY, 0, 0.f) && key(TrackProperty::PositionY, 30, 300.f));
    AUREA_CHECK(key(TrackProperty::PositionY, 60, 900.f));
    AUREA_CHECK(key(TrackProperty::ScaleX, 0, 1.f) && key(TrackProperty::ScaleX, 30, 2.f));
    AUREA_CHECK(key(TrackProperty::ScaleY, 0, 1.f) && key(TrackProperty::ScaleY, 30, 4.f));
    AUREA_CHECK(key(TrackProperty::Opacity, 0, 0.f) && key(TrackProperty::Opacity, 30, 1.f));
    auto curve = [&](TrackProperty p, Interpolation kind, f32 x1, f32 y1, f32 x2, f32 y2, bool link, u8 power = 0) {
        Command c;
        c.type = CommandType::KeyframeSetInterpolation;
        c.keyframe_interp.track = TrackRef{layer, p, kInvalidIndex, 0};
        c.keyframe_interp.time = FrameIndex{0};
        c.keyframe_interp.interp = kind; c.keyframe_interp.power = power;
        c.keyframe_interp.linkAxes = link ? 1 : 0;
        c.keyframe_interp.bx1 = x1; c.keyframe_interp.by1 = y1; c.keyframe_interp.bx2 = x2; c.keyframe_interp.by2 = y2;
        return e.apply_command(c).ok();
    };
    auto track = [&](TrackProperty p) -> const Track* {
        const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        return comp ? comp->layer(layer)->tracks.find(p) : nullptr;
    };
    // Progresso de X e Y no trecho 0..30, frame a frame (e em meio-frame).
    auto same_timing = [&](TrackProperty a, f32 a0, f32 a1, TrackProperty b, f32 b0, f32 b1) {
        const Track* ta = track(a); const Track* tb = track(b);
        if (!ta || !tb) return false;
        for (i64 t = 0; t <= 30; ++t) {
            const f32 pa = (ta->sample(FrameIndex{t}) - a0) / (a1 - a0);
            const f32 pb = (tb->sample(FrameIndex{t}) - b0) / (b1 - b0);
            if (std::fabs(pa - pb) > 1e-5f) {
                std::printf("    frame %lld: %s %.6f vs %.6f\n", static_cast<long long>(t), "progresso", pa, pb);
                return false;
            }
        }
        return true;
    };
    struct Family { Interpolation kind; f32 x1, y1, x2, y2; u8 power; };
    const Family families[] = {
        {Interpolation::Bezier, 0.42f, 0.0f, 0.58f, 1.0f, 2},
        {Interpolation::Bezier, 0.34f, 1.56f, 0.64f, 1.0f, 1},      // overshoot por alças (sai de 0..1)
        {Interpolation::EaseIn, 0.33f, 0.0f, 0.67f, 1.0f, 3},
        {Interpolation::Overshoot, 0.6f, 0.0f, 1.0f, kEaseParamMarker, 0},
        {Interpolation::Elastic, 0.5f, 0.75f, 1.0f, kEaseParamMarker, 0},
        {Interpolation::Bounce, 0.375f, 0.5f, 1.0f, kEaseParamMarker, 0},
    };
    for (const Family& f : families) {
        AUREA_CHECK(curve(TrackProperty::PositionX, f.kind, f.x1, f.y1, f.x2, f.y2, true, f.power));
        AUREA_CHECK(track(TrackProperty::PositionY)->keys[0].interp == f.kind);
        AUREA_CHECK(same_timing(TrackProperty::PositionX, 0.f, 100.f, TrackProperty::PositionY, 0.f, 300.f));
        // O caminho: Y/X constante (3) em todo frame — o objeto anda na reta.
        for (i64 t = 1; t <= 30; ++t) {
            const f32 x = track(TrackProperty::PositionX)->sample(FrameIndex{t});
            if (std::fabs(x) > 1e-3f) AUREA_CHECK_NEAR(track(TrackProperty::PositionY)->sample(FrameIndex{t}) / x, 3.0f, 1e-3f);
        }
    }
    // Pelo Y também liga o X (qualquer eixo endereçado serve).
    AUREA_CHECK(curve(TrackProperty::PositionY, Interpolation::EaseOut, 0.33f, 0.f, 0.67f, 1.f, true));
    AUREA_CHECK(track(TrackProperty::PositionX)->keys[0].interp == Interpolation::EaseOut);
    // O trecho seguinte do Y (30→60) e as outras propriedades não mudam.
    AUREA_CHECK(track(TrackProperty::PositionY)->keys[1].interp == Interpolation::Linear);
    AUREA_CHECK(track(TrackProperty::Opacity)->keys[0].interp == Interpolation::Linear);
    AUREA_CHECK(track(TrackProperty::ScaleX)->keys[0].interp == Interpolation::Linear);
    // Escala X/Y também ligada.
    AUREA_CHECK(curve(TrackProperty::ScaleX, Interpolation::Bezier, 0.2f, 0.8f, 0.6f, 1.f, true));
    AUREA_CHECK(same_timing(TrackProperty::ScaleX, 1.f, 2.f, TrackProperty::ScaleY, 1.f, 4.f));
    // Uma dimensão de propósito (gráfico): sem `linkAxes`, só a trilha endereçada.
    AUREA_CHECK(curve(TrackProperty::PositionX, Interpolation::Bezier, 0.9f, 0.1f, 0.9f, 0.2f, false));
    AUREA_CHECK(track(TrackProperty::PositionX)->keys[0].bx1 == 0.9f);
    AUREA_CHECK(track(TrackProperty::PositionY)->keys[0].interp == Interpolation::EaseOut);
    // Um desfazer volta a curva dos DOIS eixos (era um comando só).
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());   // desfaz a edição só do X
    AUREA_CHECK(e.apply_command(undo).ok());   // desfaz a escala
    AUREA_CHECK(e.apply_command(undo).ok());   // desfaz o EaseOut ligado
    AUREA_CHECK(track(TrackProperty::PositionX)->keys[0].interp == Interpolation::Bounce);
    AUREA_CHECK(track(TrackProperty::PositionY)->keys[0].interp == Interpolation::Bounce);
    // Valor fora do contrato é recusado.
    Command bad;
    bad.type = CommandType::KeyframeSetInterpolation;
    bad.keyframe_interp.track = TrackRef{layer, TrackProperty::PositionX, kInvalidIndex, 0};
    bad.keyframe_interp.time = FrameIndex{0};
    bad.keyframe_interp.interp = Interpolation::Linear;
    bad.keyframe_interp.linkAxes = 2;
    AUREA_CHECK(!e.apply_command(bad).ok());
    e.shutdown();
}

// Quais componentes andam juntos: o grupo é do motor (as duas UIs só mandam o pedido).
AUREA_TEST(Engine, LinkedAxisGroupsCoverTransformLightAndShapeParts) {
    TrackRef out[3];
    const LayerId l{};
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::PositionY, kInvalidIndex, 0}, out), 3u);
    AUREA_CHECK(out[0].property == TrackProperty::PositionX && out[2].property == TrackProperty::PositionZ);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::AnchorZ, kInvalidIndex, 0}, out), 3u);
    AUREA_CHECK(out[0].property == TrackProperty::AnchorX);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::RotationX, kInvalidIndex, 0}, out), 3u);
    AUREA_CHECK(out[2].property == TrackProperty::RotationZ);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::SkewY, kInvalidIndex, 0}, out), 2u);
    AUREA_CHECK(out[0].property == TrackProperty::SkewX);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::LightColorG, kInvalidIndex, 0}, out), 3u);
    AUREA_CHECK(out[0].property == TrackProperty::LightColorR);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::ShapePart, 2, 7}, out), 3u);
    AUREA_CHECK(out[0].effectParamIndex == 6 && out[2].effectParamIndex == 8 && out[1].effectIndex == 2);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::Opacity, kInvalidIndex, 0}, out), 1u);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::EffectParam, 0, 1}, out), 1u);
    AUREA_CHECK_EQ(linked_axis_refs(TrackRef{l, TrackProperty::ShapePart, 0, 9}, out), 1u);
}

#include "EffectCompatibility.inl"
#include "PreviewViewport.inl"
#include "ImportInsertion.inl"
#include "TransformClipboard.inl"

AUREA_TEST(EffectCurves, EditingUsesSharedBoundsPreservesChannelsUndoAndSave) {
    Engine e; AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(64, 64, 30, "curves").ok());
    auto added = e.add_shape(0); AUREA_CHECK(added.ok()); if (!added.ok()) return;
    const u64 layer = *added;
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = LayerId::unpack(layer);
    add.effect_add.effectType = effect_type_id(effect_keys::kCurves); add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(add).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const u32 effect = comp->layer(LayerId::unpack(layer))->effects.back().id;
    f32 points[128]{}, samples[256]{};
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 0, false, points, 128), 4u);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 1, 0, .5f, .8f), 1);
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 0, true, samples, 256), 256u);
    AUREA_CHECK_NEAR(samples[128], .8f, .004f);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 1, 1, 0, .3f, .1f), 1);
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 2, false, points, 128), 4u);
    AUREA_CHECK_EQ(points[1], 0.f); AUREA_CHECK_EQ(points[3], 1.f);
    Command begin; begin.type = CommandType::UndoBeginGroup; AUREA_CHECK(e.apply_command(begin).ok());
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 0, 1, 2.f, -1.f), 1);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 0, 1, .7f, .9f), 1);
    Command end; end.type = CommandType::UndoEndGroup; AUREA_CHECK(e.apply_command(end).ok());
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 0, false, points, 128), 6u);
    AUREA_CHECK_NEAR(points[2], .5f, .0001f); AUREA_CHECK_NEAR(points[3], .8f, .0001f);
    const auto depth = e.history().depth();
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 2, 0, 0, 0), -1);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 4, 1, 0, .2f, .3f), -1);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 0, 1, std::numeric_limits<f32>::quiet_NaN(), 0), -1);
    AUREA_CHECK_EQ(e.history().depth(), depth);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 0, 0, .5f, .2f), 0);
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 0, false, points, 128), 6u);
    AUREA_CHECK_EQ(points[0], 0.f);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 0, 1, 3.f, 2.f), 1);
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 0, false, points, 128), 6u);
    AUREA_CHECK(points[2] < points[4]); AUREA_CHECK_EQ(points[3], 1.f);
    const auto path = (std::filesystem::temp_directory_path() / "aurea-curves-editor.aurea").string();
    AUREA_CHECK(e.save_project(path.c_str()).ok()); AUREA_CHECK(e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 1, false, points, 128), 6u);
    AUREA_CHECK_NEAR(points[2], .3f, .0001f); AUREA_CHECK_NEAR(points[3], .1f, .0001f);
    AUREA_CHECK_EQ(e.edit_effect_curve(layer, effect, 0, 0, 3, 0, 0, 0), 0);
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 0, false, points, 128), 4u);
    AUREA_CHECK_EQ(points[1], 0.f); AUREA_CHECK_EQ(points[3], 1.f);
    AUREA_CHECK_EQ(e.query_effect_curve(layer, effect, 0, 1, false, points, 128), 6u);
}
