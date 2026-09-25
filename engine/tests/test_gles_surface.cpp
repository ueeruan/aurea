#include "TestFramework.hpp"
#include "GlesBackend.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/Engine.hpp"
#include "MediaCodecSource.hpp"
#include <cstdlib>
#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <chrono>
#include <thread>

using namespace aurea;

AUREA_TEST(GlesMedia, RealMediaCodecFrameReachesRenderer) {
    const char* path = std::getenv("AUREA_GLES_MEDIA");
    if (!path) { std::printf("(set AUREA_GLES_MEDIA to a device video path) "); return; }
    android::MediaCodecFactory factory;
    factory.set_zero_copy(false);
    Engine engine;
    EngineConfig config;
    config.backend = new gles::Backend();
    config.mediaFactory = &factory;
    config.disableAutosave = true;
    config.workerCount = 2;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(160, 90, 30, "GLES real video").ok());
    VideoImport media; media.sourcePath = path; media.displayName = "GLES real video";
    AUREA_CHECK(engine.import_video(media).ok());
    std::vector<u8> pixels; u32 w = 0, h = 0;
    AUREA_CHECK(engine.capture_frame_rgba(160, pixels, w, h).ok());
    u64 brightness = 0;
    for (usize i = 0; i + 3 < pixels.size(); i += 4) brightness += pixels[i] + pixels[i + 1] + pixels[i + 2];
    AUREA_CHECK(brightness > 160 * 90 * 20);
    engine.shutdown();
}

AUREA_TEST(GlesSurface, ContextMovesToRenderThreadAndPresents) {
    gles::Backend backend;
    AUREA_CHECK(backend.initialize({}).ok());
    ShaderLibrary shaders;
    AUREA_CHECK(shaders.initialize(backend).ok());
    AImageReader* reader = nullptr;
    const auto usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
    AUREA_CHECK_EQ(AImageReader_newWithUsage(64, 64, AIMAGE_FORMAT_RGBA_8888, usage, 3, &reader), AMEDIA_OK);
    if (!reader) return;
    ANativeWindow* window = nullptr;
    AUREA_CHECK_EQ(AImageReader_getWindow(reader, &window), AMEDIA_OK);
    SurfaceDesc surface; surface.nativeWindow = window; surface.width = surface.height = 64; surface.vsync = false;
    const auto attached = backend.attach_surface(surface);
    AUREA_CHECK(attached.ok());
    bool rendered = false;
    if (attached.ok()) {
        std::thread render([&] {
            FrameBegin frame;
            if (!backend.begin_frame(frame).ok()) return;
            const auto pipeline = shaders.pipeline(PipelineKey::fullscreen(ShaderId::effects_rgb_merge_frag, SurfaceFormat::RGBA8));
            RenderPassBegin pass; pass.color = frame.backbuffer; pass.load = LoadOp::Clear;
            pass.clear[0] = pass.clear[3] = 1.0f;
            frame.commands->begin_render_pass(pass);
            frame.commands->end_render_pass();
            rendered = pipeline.ok() && backend.end_frame().ok();
        });
        render.join();
        AUREA_CHECK(rendered);
        AImage* image = nullptr;
        for (int attempt = 0; attempt < 50 && !image; ++attempt) {
            AImageReader_acquireLatestImage(reader, &image);
            if (!image) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        AUREA_CHECK(image != nullptr);
        if (image) {
            uint8_t* pixels = nullptr; int bytes = 0;
            AUREA_CHECK_EQ(AImage_getPlaneData(image, 0, &pixels, &bytes), AMEDIA_OK);
            AUREA_CHECK(pixels && bytes >= 4);
            if (pixels && bytes >= 4) {
                AUREA_CHECK(pixels[0] > 250 && pixels[1] < 5 && pixels[2] < 5 && pixels[3] > 250);
            }
            AImage_delete(image);
        }
    }
    backend.detach_surface();
    shaders.shutdown();
    backend.shutdown();
    AImageReader_delete(reader);
}
