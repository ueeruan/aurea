#include "ImageIO.hpp"
#include <sstream>

namespace {

// A magnified 32-texel strip from a wide source distinguishes Original maps
// from the preview's averaged pixels without a large fixture or a screenshot oracle.
struct OriginalModelFixture {
    std::string prefix;
    explicit OriginalModelFixture(const char* name) : prefix(name) {
        const char identity[] = "stable synthetic video identity";
        (void)fileio::write_atomic(prefix + ".bin", identity, sizeof(identity));
    }
    ~OriginalModelFixture() {
        for (const auto& suffix : {".bin", "-source.glb", "-reference.glb", "-bad.glb", "-source.gltf", "-source.obj", "-source.mtl", "-map.png",
                                  ".aurea", ".aurea.bak", ".mp4", ".mp4.aurea-export", ".mp4.aurea-export.project.aurea"})
            fileio::remove_file(prefix + suffix);
        for (const auto& suffix : {"nao-usado.mp4", "nao-usado.mp4.aurea-export", "nao-usado.mp4.aurea-export.project.aurea"})
            fileio::remove_file(suffix);
    }
    std::string glb(u32 width, const char* suffix, bool corrupt = false, bool jsonFormat = false) {
        Image8 image; image.width = width; image.height = 16;
        image.rgba.resize(static_cast<usize>(width) * image.height * 4);
        for (u32 y = 0; y < image.height; ++y) for (u32 x = 0; x < width; ++x) {
            u8* p = &image.rgba[(static_cast<usize>(y) * width + x) * 4];
            p[0] = x & 1 ? 10 : 240; p[1] = 20; p[2] = x & 1 ? 240 : 10; p[3] = 255;
        }
        const std::string png = prefix + "-map.png";
        if (!write_png(png, image)) return {};
        std::vector<u8> pixels;
        if (!fileio::read_all(png, pixels, 2ull << 20)) return {};
        if (corrupt) pixels.assign({'i', 'n', 'v', 'a', 'l', 'i', 'd'});
        std::vector<u8> bin;
        const auto append = [&](const void* p, usize n) {
            const auto* b = static_cast<const u8*>(p); bin.insert(bin.end(), b, b + n);
        };
        const f32 position[] = {-1, -.5f, 0, 1, -.5f, 0, -1, .5f, 0, 1, .5f, 0};
        const f32 u = 32.f / width;
        const f32 uv[] = {0, 0, u, 0, 0, 1, u, 1};
        const u16 index[] = {0, 1, 2, 2, 1, 3};
        append(position, sizeof(position)); append(uv, sizeof(uv)); append(index, sizeof(index));
        const usize pngOffset = bin.size(); append(pixels.data(), pixels.size());
        while (bin.size() % 4) bin.push_back(0);
        std::ostringstream json;
        json << R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_unlit"],"buffers":[{"byteLength":)" << bin.size()
             << R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":32},{"buffer":0,"byteOffset":80,"byteLength":12},{"buffer":0,"byteOffset":)"
             << pngOffset << R"(,"byteLength":)" << pixels.size()
             << R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-1,-0.5,0],"max":[1,0.5,0]},{"bufferView":1,"componentType":5126,"count":4,"type":"VEC2"},{"bufferView":2,"componentType":5123,"count":6,"type":"SCALAR"}],"images":[{"bufferView":3,"mimeType":"image/png"}],"samplers":[{"magFilter":9728,"minFilter":9728}],"textures":[{"source":0,"sampler":0}],"materials":[{"doubleSided":true,"extensions":{"KHR_materials_unlit":{}},"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0,"roughnessFactor":1}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
        std::string text = json.str(); while (text.size() % 4) text.push_back(' ');
        const std::string path = prefix + suffix;
        if (jsonFormat) {
            static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string encoded;
            encoded.reserve((bin.size() + 2) / 3 * 4);
            for (usize i = 0; i < bin.size(); i += 3) {
                const u32 a = bin[i], b = i + 1 < bin.size() ? bin[i + 1] : 0, c = i + 2 < bin.size() ? bin[i + 2] : 0;
                encoded.push_back(alphabet[a >> 2]); encoded.push_back(alphabet[((a & 3) << 4) | (b >> 4)]);
                encoded.push_back(i + 1 < bin.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=');
                encoded.push_back(i + 2 < bin.size() ? alphabet[c & 63] : '=');
            }
            const auto buffer = text.find("\"buffers\":[{");
            if (buffer == std::string::npos) return {};
            text.insert(buffer + std::strlen("\"buffers\":[{"), "\"uri\":\"data:application/octet-stream;base64," + encoded + "\",");
            return fileio::write_atomic(path, text.data(), text.size()).ok() ? path : std::string{};
        }
        std::vector<u8> file;
        const auto put = [&](u32 n) { for (u32 s = 0; s < 32; s += 8) file.push_back(static_cast<u8>(n >> s)); };
        put(0x46546c67u); put(2); put(static_cast<u32>(12 + 8 + text.size() + 8 + bin.size()));
        put(static_cast<u32>(text.size())); put(0x4e4f534au);
        file.insert(file.end(), text.begin(), text.end());
        put(static_cast<u32>(bin.size())); put(0x004e4942u); file.insert(file.end(), bin.begin(), bin.end());
        return fileio::write_atomic(path, file.data(), file.size()).ok() ? path : std::string{};
    }
    LayerId model(Rig& rig, const std::string& path, scene3d::ModelQuality quality) {
        rig.comp()->layer(rig.video_layer())->visible = false;
        ModelImport request; request.path = path; request.quality = quality;
        const auto imported = rig.e.import_model(request);
        if (!imported.ok()) return {};
        const auto id = LayerId::unpack(*imported);
        auto* layer = rig.comp()->layer(id);
        layer->model.unitScale = 40;
        layer->model.castShadows = layer->model.receiveShadows = false;
        layer->end = FrameIndex{4};
        return id;
    }
    void serial_capture(Rig& rig) {
        rig.cap.keepFrames = true;
        rig.cap.persistFinishMarker = true;
        rig.cap.validateCapturedPlanes = true;
    }
};

} // namespace

AUREA_TEST(OriginalModelExportGpu, SourceTexturePixelsSurviveSerialExportReopenAndPreviewRestoration) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    OriginalModelFixture fixture("aurea-original-textures");
    const auto source = fixture.glb(4096, "-source.glb");
    const auto reference = fixture.glb(1024, "-reference.glb");
    AUREA_CHECK(!source.empty() && !reference.empty()); if (source.empty() || reference.empty()) return;
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    const std::string video = fixture.prefix + ".bin";
    Rig original(cfg, 30, 4, 1, nullptr, nullptr, 0, false, video.c_str(),
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 768ull << 20, true, true);
    Rig exact(cfg, 30, 4, 1, nullptr, nullptr, 0, false, video.c_str(),
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 768ull << 20, true, true);
    Rig proxy(cfg, 30, 4, 1, nullptr, nullptr, 0, false, video.c_str(),
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 768ull << 20, true, true);
    AUREA_CHECK(original.ok && exact.ok && proxy.ok); if (!original.ok || !exact.ok || !proxy.ok) return;
    const auto id = fixture.model(original, source, scene3d::ModelQuality::Original);
    const auto refId = fixture.model(exact, reference, scene3d::ModelQuality::Original);
    const auto proxyId = fixture.model(proxy, source, scene3d::ModelQuality::Balanced);
    AUREA_CHECK(id.valid() && refId.valid() && proxyId.valid()); if (!id.valid() || !refId.valid() || !proxyId.valid()) return;
    const auto assetId = original.comp()->layer(id)->model.scene;
    const auto prior = original.e.model_asset(assetId.pack());
    AUREA_CHECK(prior && prior->stats.texturesReduced == 1); if (!prior) return;
    AUREA_CHECK_EQ(prior->images[0].width, 2048u);
    AUREA_CHECK_EQ(exact.e.last_model_import().texturesReduced, 0u);
    std::vector<u8> before, after; u32 bw = 0, bh = 0, aw = 0, ah = 0;
    AUREA_CHECK(original.e.capture_frame_rgba(96, before, bw, bh).ok());
    fixture.serial_capture(original); fixture.serial_capture(exact); fixture.serial_capture(proxy);
    u32 captureAttempts = 0;
    original.cap.beforeOpen = [&] {
        std::vector<u8> preserved{1, 2, 3}; u32 w = 0, h = 0;
        AUREA_CHECK_EQ(original.e.capture_frame_rgba(96, preserved, w, h).code(), Errc::Busy);
        AUREA_CHECK_EQ(original.e.capture_preview_frame_rgba(96, preserved, w, h).code(), Errc::Busy);
        AUREA_CHECK_EQ(original.e.render_offscreen({}, 96, 64, true).code(), Errc::Busy);
        AUREA_CHECK(preserved == std::vector<u8>({1, 2, 3}));
        ++captureAttempts;
        return OkStatus;
    };
    const auto actual = run_export(original, 64, 30, false, 60);
    const auto expected = run_export(exact, 64, 30, false, 60);
    const auto degraded = run_export(proxy, 64, 30, false, 60);
    AUREA_CHECK(actual.finished && actual.p.result == Errc::Ok);
    AUREA_CHECK(expected.finished && expected.p.result == Errc::Ok);
    AUREA_CHECK(degraded.finished && degraded.p.result == Errc::Ok);
    AUREA_CHECK_EQ(actual.p.framesDone, 4u); AUREA_CHECK_EQ(original.cap.validationCalls, 1u);
    AUREA_CHECK_EQ(original.cap.frames.size(), usize{4});
    AUREA_CHECK_EQ(captureAttempts, 1u);
    AUREA_CHECK(original.cap.frames == exact.cap.frames);
    AUREA_CHECK(original.cap.frames != proxy.cap.frames); // A grey/downscaled result cannot satisfy the oracle.
    AUREA_CHECK(original.e.model_asset(assetId.pack()) == prior);
    AUREA_CHECK_EQ(prior->images[0].width, 2048u);
    AUREA_CHECK_EQ(original.e.project()->asset(assetId)->model.importQuality, 0u);
    AUREA_CHECK(original.e.capture_frame_rgba(96, after, aw, ah).ok());
    AUREA_CHECK_EQ(aw, bw); AUREA_CHECK_EQ(ah, bh); AUREA_CHECK(before == after);
    const auto savedFrames = original.cap.frames;
    const auto project = fixture.prefix + ".aurea";
    AUREA_CHECK(original.e.save_project(project.c_str()).ok());
    AUREA_CHECK(original.e.load_project(project.c_str()).ok());
    original.cap = BenchCapture{}; fixture.serial_capture(original);
    const auto reopened = run_export(original, 64, 30, false, 60);
    AUREA_CHECK(reopened.finished && reopened.p.result == Errc::Ok);
    AUREA_CHECK(original.cap.frames == savedFrames);
    // Embedded data URI inspection guesses a 2048 side. The final importer
    // must instead decode the actual 4096 source, with the same exact pixels.
    const auto jsonSource = fixture.glb(4096, "-source.gltf", false, true);
    AUREA_CHECK(!jsonSource.empty()); if (jsonSource.empty()) return;
    original.e.project()->asset(assetId)->sourcePath = jsonSource;
    original.cap = BenchCapture{}; fixture.serial_capture(original);
    const auto embedded = run_export(original, 64, 30, false, 60);
    AUREA_CHECK(embedded.finished && embedded.p.result == Errc::Ok);
    AUREA_CHECK(original.cap.frames == savedFrames);
}

AUREA_TEST(OriginalModelExport, UnsupportedDegradedSourceCannotSilentlyExportPreviewMaps) {
    OriginalModelFixture fixture("aurea-original-format");
    const auto source = fixture.glb(4096, "-source.glb");
    AUREA_CHECK(!source.empty()); if (source.empty()) return;
    const std::string obj = fixture.prefix + "-source.obj", mtl = fixture.prefix + "-source.mtl";
    const std::string object = "mtllib " + mtl + "\nv -1 -.5 0\nv 1 -.5 0\nv -1 .5 0\nv 1 .5 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\nusemtl painted\nf 1/1 2/2 3/3\nf 3/3 2/2 4/4\n";
    const std::string material = "newmtl painted\nKd 1 1 1\nmap_Kd " + fixture.prefix + "-map.png\n";
    AUREA_CHECK(fileio::write_atomic(obj, object.data(), object.size()).ok());
    AUREA_CHECK(fileio::write_atomic(mtl, material.data(), material.size()).ok());
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    const std::string video = fixture.prefix + ".bin";
    Rig rig(cfg, 30, 4, 1, nullptr, backend, 0, false, video.c_str(),
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, true);
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    const auto id = fixture.model(rig, obj, scene3d::ModelQuality::Original);
    AUREA_CHECK(id.valid()); if (!id.valid()) return;
    const auto key = rig.comp()->layer(id)->model.scene.pack();
    const auto prior = rig.e.model_asset(key);
    AUREA_CHECK(prior && prior->stats.texturesReduced > 0);
    ExportSettings settings; settings.height = 64;
    const auto failed = rig.e.start_export(settings, (fixture.prefix + ".mp4").c_str());
    AUREA_CHECK_EQ(failed.code(), Errc::NotSupported);
    AUREA_CHECK(failed.detail().find("Original") != std::string_view::npos);
    AUREA_CHECK(!rig.cap.opened && rig.cap.writes == 0);
    AUREA_CHECK(rig.e.model_asset(key) == prior);
}

AUREA_TEST(OriginalModelExport, InsufficientSourceMemoryFailsBeforeOpeningTheEncoderWithoutChangingPreview) {
    OriginalModelFixture fixture("aurea-original-budget");
    const auto source = fixture.glb(4096, "-source.glb");
    AUREA_CHECK(!source.empty()); if (source.empty()) return;
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    const std::string video = fixture.prefix + ".bin";
    Rig rig(cfg, 30, 4, 1, nullptr, backend, 0, false, video.c_str(),
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 1ull << 20, true, true);
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    const auto id = fixture.model(rig, source, scene3d::ModelQuality::Original);
    AUREA_CHECK(id.valid()); if (!id.valid()) return;
    const auto asset = rig.comp()->layer(id)->model.scene;
    const auto prior = rig.e.model_asset(asset.pack());
    AUREA_CHECK(prior && prior->stats.texturesReduced == 1);
    ExportSettings settings; settings.height = 64;
    const auto failed = rig.e.start_export(settings, (fixture.prefix + ".mp4").c_str());
    AUREA_CHECK_EQ(failed.code(), Errc::BudgetExceeded);
    AUREA_CHECK(failed.detail().find("Original") != std::string_view::npos);
    AUREA_CHECK(!rig.cap.opened && rig.cap.writes == 0 && !rig.cap.finished);
    AUREA_CHECK(rig.e.model_asset(asset.pack()) == prior);
    AUREA_CHECK_EQ(rig.e.project()->asset(asset)->model.importQuality, 0u);
    AUREA_CHECK(rig.e.new_project(96, 64, 30, "startup failure released ownership").ok());
}

AUREA_TEST(OriginalModelExport, MissingRequiredTextureDoesNotPublishAnUntexturedVideo) {
    OriginalModelFixture fixture("aurea-original-missing");
    const auto source = fixture.glb(4096, "-bad.glb", true);
    AUREA_CHECK(!source.empty()); if (source.empty()) return;
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    const std::string video = fixture.prefix + ".bin";
    Rig rig(cfg, 30, 4, 1, nullptr, backend, 0, false, video.c_str(),
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, true);
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    const auto id = fixture.model(rig, source, scene3d::ModelQuality::Original);
    AUREA_CHECK(id.valid()); if (!id.valid()) return;
    const auto scene = rig.e.model_asset(rig.comp()->layer(id)->model.scene.pack());
    AUREA_CHECK(scene && scene->images[0].pixels().empty());
    AUREA_CHECK_EQ(rig.e.last_model_import().texturesSkipped, 0u); // Legacy decode warnings are not counted here.
    ExportSettings settings; settings.height = 64;
    const auto failed = rig.e.start_export(settings, (fixture.prefix + ".mp4").c_str());
    AUREA_CHECK_EQ(failed.code(), Errc::AssetCorrupted);
    AUREA_CHECK(!rig.cap.opened && rig.cap.writes == 0 && !rig.cap.finished);
}

AUREA_TEST(OriginalModelExport, CancellationKeepsSourceTexturesUntilFenceThenRestoresPreviewProxyWithoutWaitingIdle) {
    OriginalModelFixture fixture("aurea-original-cancel");
    const auto source = fixture.glb(4096, "-source.glb");
    AUREA_CHECK(!source.empty()); if (source.empty()) return;
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    std::atomic<bool> waiting{false}, completed{false};
    backend->beforeWaitFrame = [&](u64 frame, u64 timeout) {
        if (!frame || completed.load()) return OkStatus;
        if (timeout) { waiting = true; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        return Status{Errc::Timeout};
    };
    const std::string video = fixture.prefix + ".bin";
    Rig rig(cfg, 30, 4, 1, nullptr, backend, 0, false, video.c_str(),
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, true);
    struct Complete { std::atomic<bool>& flag; ~Complete() { flag = true; } } guard{completed};
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    const auto id = fixture.model(rig, source, scene3d::ModelQuality::Original);
    AUREA_CHECK(id.valid()); if (!id.valid()) return;
    AUREA_CHECK(rig.e.attach_surface(reinterpret_cast<void*>(1), 96, 64).ok());
    ExportSettings settings; settings.height = 64;
    AUREA_CHECK(rig.e.start_export(settings, (fixture.prefix + ".mp4").c_str()).ok());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!waiting && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK(waiting.load());
    std::vector<u8> preserved{7, 8, 9}; u32 captureWidth = 0, captureHeight = 0;
    AUREA_CHECK_EQ(rig.e.capture_preview_frame_rgba(96, preserved, captureWidth, captureHeight).code(), Errc::Busy);
    AUREA_CHECK(preserved == std::vector<u8>({7, 8, 9}));
    const auto idle = backend->idleWaits.load();
    const auto cancelledAt = std::chrono::steady_clock::now(); (void)rig.e.cancel_export();
    deadline = cancelledAt + std::chrono::seconds(3);
    while (!rig.e.export_progress().finished && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK(rig.e.export_progress().finished);
    AUREA_CHECK_EQ(rig.e.export_progress().result, Errc::Cancelled);
    AUREA_CHECK(std::chrono::steady_clock::now() - cancelledAt < std::chrono::seconds(3));
    // Join the producer before inspecting the mock's resource records.
    AUREA_CHECK_EQ(rig.e.start_export(settings, (fixture.prefix + ".mp4").c_str()).code(), Errc::Timeout);
    usize sourceTexture = 0;
    for (usize i = 0; i < backend->textures.size(); ++i) {
        const auto& desc = backend->textures[i];
        if (desc.width == 4096 && desc.height == 16 && desc.debugName && std::strcmp(desc.debugName, "3d-textura-cor") == 0)
            sourceTexture = i + 1;
    }
    AUREA_CHECK(sourceTexture != 0); if (!sourceTexture) return;
    AUREA_CHECK(backend->textureAlive[sourceTexture - 1]);
    AUREA_CHECK_EQ(rig.e.render_frame().code(), Errc::Timeout);
    AUREA_CHECK_EQ(rig.e.capture_preview_frame_rgba(96, preserved, captureWidth, captureHeight).code(), Errc::Timeout);
    AUREA_CHECK(backend->textureAlive[sourceTexture - 1]);
    AUREA_CHECK_EQ(backend->idleWaits.load(), idle);
    completed = true; AUREA_CHECK(rig.e.render_frame().ok());
    AUREA_CHECK(!backend->textureAlive[sourceTexture - 1]);
    bool previewTexture = false;
    for (usize i = 0; i < backend->textures.size(); ++i)
        if (backend->textureAlive[i] && backend->textures[i].width == 2048 && backend->textures[i].height == 8)
            previewTexture = true;
    AUREA_CHECK(previewTexture);
    AUREA_CHECK_EQ(backend->idleWaits.load(), idle);
}
