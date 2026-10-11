// Test-only Android device benchmark. It exercises the application's shared
// Renderer and actual Vulkan/GLES backend offscreen. No app data is touched.
// argv: vertices static|rigid|morph|zero floor(0|1) hdri(0|1|2) blur(0|1)
//       [width height measuredFrames finalQuality(0|1) bounded(0|1) halfPixelDump batchBudgetMiB objectCount shared|identical|varied]
// Real assets: replace `vertices mode` with `--model path.glb|path.gltf` and
// optionally append animationClip (-1 = bind pose, default = first clip).
// hdri=1: group HDRI; hdri=2: own-object HDRI (asynchronous preview job).
// Timings include an explicit GPU wait per frame and are not UI playback FPS.
#if defined(AUREA_PROBE_VULKAN)
#include "VulkanBackend.hpp"
namespace probe_backend = aurea::vk;
#else
#include "GlesBackend.hpp"
#include <GLES3/gl31.h>
namespace probe_backend = aurea::gles;
#endif
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/scene3d/Importer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <sys/resource.h>
#include <thread>
#include <vector>

using namespace aurea;
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point from, Clock::time_point to = Clock::now()) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<size_t>(std::ceil(fraction * values.size())) - 1)];
}
u64 process_rss_bytes() {
    auto* file = std::fopen("/proc/self/status", "r");
    if (!file) return 0;
    char line[256]; unsigned long long kb = 0;
    while (std::fgets(line, sizeof(line), file)) {
        if (std::sscanf(line, "VmRSS: %llu kB", &kb) == 1) break;
    }
    std::fclose(file); return static_cast<u64>(kb) * 1024;
}
double waited_fps(const std::vector<double>& frameMs) {
    const double wall = std::accumulate(frameMs.begin(), frameMs.end(), 0.0);
    return wall > 0 ? 1000.0 * frameMs.size() / wall : 0;
}
double waited_one_percent_low(std::vector<double> frameMs) {
    if (frameMs.empty()) return 0;
    std::sort(frameMs.begin(), frameMs.end(), std::greater<double>());
    const auto count = std::max<size_t>(1, static_cast<size_t>(std::ceil(frameMs.size() * .01)));
    const double slow = std::accumulate(frameMs.begin(), frameMs.begin() + count, 0.0);
    return slow > 0 ? 1000.0 * count / slow : 0;
}
bool checked(Status status, const char* stage) {
    if (status.ok()) return true;
    std::fprintf(stderr, "FAIL stage=%s status=%d detail=%.*s\n", stage, status.raw(),
        static_cast<int>(status.detail().size()), status.detail().data());
    return false;
}
std::shared_ptr<const scene3d::SceneAsset> make_model(u32 vertices, const char* mode) {
    auto asset = std::make_shared<scene3d::SceneAsset>();
    scene3d::Primitive p;
    const u32 columns = static_cast<u32>(std::sqrt(static_cast<double>(vertices)));
    const u32 rows = vertices / columns;
    p.positions.resize(vertices); p.normals.resize(vertices);
    constexpr f32 pi = 3.14159265358979323846f;
    for (u32 i = 0; i < vertices; ++i) {
        const f32 phi = (i % columns) * 2 * pi / (columns - 1);
        const f32 theta = std::min(i / columns, rows - 1) * pi / (rows - 1);
        const Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
        p.positions[i] = n; p.normals[i] = n; p.bounds.add(n);
    }
    for (u32 y = 0; y + 1 < rows; ++y) for (u32 x = 0; x + 1 < columns; ++x) {
        const u32 a = y * columns + x, b = a + 1, c = a + columns, d = c + 1;
        p.indices.insert(p.indices.end(), {a, c, b, b, c, d});
    }
    p.material = 0;
    const bool morph = std::strcmp(mode, "morph") == 0;
    const bool dormant = std::strcmp(mode, "zero") == 0;
    if (morph || dormant) {
        p.morphTargets.resize(52);
        for (u32 target : {3u, 31u}) {
            auto& stream = p.morphTargets[target].positions;
            stream.resize(vertices);
            for (u32 i = 0; i < vertices; ++i)
                stream[i] = p.normals[i] * (target == 3 ? .12f : -.07f);
        }
    }
    scene3d::Mesh mesh; mesh.bounds = p.bounds;
    if (morph || dormant) mesh.morphWeights.assign(52, 0);
    mesh.primitives.push_back(std::move(p)); asset->meshes.push_back(std::move(mesh));
    scene3d::Node node; node.mesh = 0; asset->nodes.push_back(node);
    asset->roots = {0}; asset->bounds = asset->meshes[0].bounds;
    scene3d::Material material; material.baseColor = Vec4{.12f, .4f, .8f, 1};
    material.metallic = .15f; material.roughness = .5f; material.emissive = Vec3{.01f, .015f, .02f};
    material.doubleSided = true; asset->materials.push_back(material);
    if (morph || std::strcmp(mode, "rigid") == 0) {
        scene3d::Animation animation; animation.duration = 4;
        scene3d::AnimSampler sampler; sampler.times = {0, 2, 4};
        if (morph) {
            sampler.components = 52; sampler.values.assign(3 * 52, 0);
            sampler.values[3] = .1f; sampler.values[31] = .2f;
            sampler.values[52 + 3] = .8f; sampler.values[52 + 31] = .6f;
            sampler.values[104 + 3] = .1f; sampler.values[104 + 31] = .2f;
        } else {
            sampler.components = 3; sampler.values = {-.2f, 0, 0, .2f, 0, 0, -.2f, 0, 0};
        }
        animation.samplers.push_back(std::move(sampler));
        animation.channels.push_back({0, morph ? scene3d::AnimPath::Weights : scene3d::AnimPath::Translation, 0});
        asset->animations.push_back(std::move(animation));
    }
    return asset;
}
std::shared_ptr<const scene3d::HdriPixels> make_hdri() {
    auto hdri = std::make_shared<scene3d::HdriPixels>(); hdri->width = 1024; hdri->height = 512;
    hdri->rgb.resize(static_cast<size_t>(hdri->width) * hdri->height * 3);
    for (u32 y = 0; y < hdri->height; ++y) for (u32 x = 0; x < hdri->width; ++x) {
        const size_t offset = (static_cast<size_t>(y) * hdri->width + x) * 3;
        const f32 sky = .1f + (1 - y / static_cast<f32>(hdri->height)) * .8f;
        const f32 lamp = x > 700 && x < 780 && y > 100 && y < 180 ? 8.f : 0.f;
        hdri->rgb[offset] = sky + lamp; hdri->rgb[offset + 1] = sky * .8f + lamp;
        hdri->rgb[offset + 2] = sky * .6f + lamp;
    }
    return hdri;
}
}

int main(int argc, char** argv) {
    const auto processStarted = Clock::now();
    const bool modelFile = argc > 1 && std::strcmp(argv[1], "--model") == 0;
    if (argc < 6 || argc > (modelFile ? 16 : 15)) { std::fprintf(stderr, "usage: probe vertices static|rigid|morph|zero floor hdri blur [width height frames finalQuality bounded halfPixelDump batchBudgetMiB objectCount shared|identical|varied]\n       probe --model path.glb|path.gltf floor hdri blur [same options, then animationClip]\n"); return 2; }
    u32 vertices = modelFile ? 0 : std::atoi(argv[1]);
    const u32 width = argc > 6 ? std::atoi(argv[6]) : 1280;
    const u32 height = argc > 7 ? std::atoi(argv[7]) : 720, frames = argc > 8 ? std::atoi(argv[8]) : 60;
    const bool floor = std::atoi(argv[3]) != 0, blur = std::atoi(argv[5]) != 0;
    const u32 hdrMode = std::atoi(argv[4]); const bool finalQuality = argc > 9 && std::atoi(argv[9]);
    const bool bounded = argc > 10 && std::atoi(argv[10]);
    const u32 batchBudgetMiB = argc > 12 ? std::atoi(argv[12]) : 32;
    const u32 objectCount = argc > 13 ? std::atoi(argv[13]) : 1;
    const bool variedMaterials = argc > 14 && std::strcmp(argv[14], "varied") == 0;
    const bool identicalOverrides = argc > 14 && std::strcmp(argv[14], "identical") == 0;
    const char* materialMode = variedMaterials ? "varied" : identicalOverrides ? "identical" : "shared";
    if ((!modelFile && (vertices < 100 || vertices > 200000)) || !width || !height || !frames || frames > 600 || hdrMode > 2) return 2;
    if (!batchBudgetMiB || batchBudgetMiB > 32) return 2;
    if (!objectCount || objectCount > 1000) return 2;
    if (argc > 14 && std::strcmp(argv[14], "shared") && !variedMaterials && !identicalOverrides) return 2;
    if (!modelFile && std::strcmp(argv[2], "static") && std::strcmp(argv[2], "rigid") && std::strcmp(argv[2], "morph") && std::strcmp(argv[2], "zero")) return 2;
    const auto importStarted = Clock::now();
    std::shared_ptr<const scene3d::SceneAsset> model;
    if (modelFile) {
        scene3d::ImportOptions options;
        auto imported = scene3d::import_gltf_file(argv[2], options);
        if (!imported.ok()) {
            std::fprintf(stderr, "FAIL model import error=%u detail=%s\n", static_cast<u32>(imported.error), imported.detail.c_str());
            return 4;
        }
        model = std::move(imported.asset);
    } else model = make_model(vertices, argv[2]);
    const double importMs = milliseconds(importStarted);
    u64 inputTriangles = 0; u32 primitives = 0, drawnPrimitives = 0, morphPrimitives = 0, skinnedPrimitives = 0;
    if (modelFile) vertices = 0;
    for (const auto& mesh : model->meshes) for (const auto& primitive : mesh.primitives) {
        if (modelFile) vertices += primitive.positions.size();
        inputTriangles += primitive.indices.size() / 3; ++primitives;
        morphPrimitives += !primitive.morphTargets.empty(); skinnedPrimitives += primitive.skinned();
    }
    for (const auto& node : model->nodes) if (node.inScene && node.mesh >= 0 && static_cast<usize>(node.mesh) < model->meshes.size())
        drawnPrimitives += model->meshes[node.mesh].primitives.size();
    const i32 clip = modelFile && argc > 15 ? std::atoi(argv[15]) : model->animations.empty() ? -1 : 0;
    if (!drawnPrimitives || !model->bounds.valid() || clip < -1 || clip >= static_cast<i32>(model->animations.size())) return 2;
    probe_backend::Backend backend; BackendConfig config; config.framesInFlight = 2;
    if (!checked(backend.initialize(config), "initialize GPU")) return 3;
    std::printf("DEVICE backend=%s gpu=%s driver=%s timers=%d\n", backend.name(), backend.capabilities().deviceName.c_str(),
        backend.capabilities().driverInfo.c_str(), backend.capabilities().timestampQueries);
    EffectRegistry effects; register_builtin_effects(effects); Renderer renderer;
    if (!checked(renderer.initialize(backend, effects), "initialize Renderer")) return 3;
    auto hdri = hdrMode ? make_hdri() : nullptr;
    renderer.set_model_lookup([](void* ctx, AssetId) { return *static_cast<std::shared_ptr<const scene3d::SceneAsset>*>(ctx); }, &model);
    renderer.set_hdri_lookup([](void* ctx, AssetId) { return *static_cast<std::shared_ptr<const scene3d::HdriPixels>*>(ctx); }, &hdri);
    auto projectResult = Project::create_new(width, height, 30, "isolated device 3D benchmark");
    if (!projectResult) return 4;
    Project project = std::move(*projectResult); auto* comp = project.timeline().composition(project.timeline().root());
    comp->set_duration(FrameIndex{900});
    Asset asset; asset.kind = AssetKind::Model3D; const auto modelId = project.add_asset(std::move(asset));
    const u32 gridColumns = objectCount == 1 ? 1 : static_cast<u32>(std::ceil(std::sqrt(objectCount * static_cast<double>(width) / height)));
    const u32 gridRows = (objectCount + gridColumns - 1) / gridColumns;
    const f32 cellWidth = width / static_cast<f32>(gridColumns), cellHeight = height / static_cast<f32>(gridRows);
    std::vector<LayerId> layerIds;
    for (u32 object = 0; object < objectCount; ++object) {
        const auto layerId = comp->add_layer(LayerKind::Model3D, modelFile ? model->sourceName : "sphere"); auto* layer = comp->layer(layerId);
        layerIds.push_back(layerId);
        layer->model.scene = modelId;
        // The original single-object fixture retains precisely its transform.
        layer->model.unitScale = objectCount == 1 ? height * .3f : std::min(cellWidth, cellHeight) * .3f;
        if (modelFile) {
            const Vec3 extent = model->bounds.extent();
            const f32 longest = std::max({extent.x, extent.y, extent.z, 1e-6f});
            layer->model.unitScale *= 2.f / longest;
            layer->model.pivot = model->bounds.center();
        }
        layer->model.animationClip = clip;
        layer->transform.position = objectCount == 1 ? Vec3{width * .5f, height * .45f, 0}
            : Vec3{(object % gridColumns + .5f) * cellWidth, (object / gridColumns + .5f) * cellHeight, 0};
        layer->threeD = true; layer->motionBlur = blur;
        if ((variedMaterials || identicalOverrides) && !model->materials.empty()) {
            // One immutable mesh is shared. Only native layer factors vary;
            // cloning all vertex arrays would confound an instancing benchmark.
            MaterialOverride material; material.materialIndex = 0; material.mask = 0x37;
            // Overrides replace glTF factors. Copy the source values for an
            // identical edit, rather than changing the source appearance.
            material.baseColor = model->materials[0].baseColor;
            material.metallic = model->materials[0].metallic; material.roughness = model->materials[0].roughness;
            if (variedMaterials) {
                const f32 hue = object / static_cast<f32>(std::max(1u, objectCount - 1));
                material.baseColor = Vec4{.15f + .7f * hue, .8f - .5f * hue, .4f, 1};
                material.metallic = .1f + .7f * hue; material.roughness = .25f + .5f * hue;
            }
            layer->model.materials.push_back(material);
        }
        if (!modelFile && std::strcmp(argv[2], "zero") == 0) {
            auto& track = layer->tracks.get_or_create(TrackProperty::PositionX);
            const f32 center = layer->transform.position.x;
            const f32 amplitude = objectCount == 1 ? width * .1f : cellWidth * .1f;
            track.set(FrameIndex{0}, objectCount == 1 ? width * .4f : center - amplitude);
            track.set(FrameIndex{120}, objectCount == 1 ? width * .6f : center + amplitude);
        }
    }
    if (hdrMode) {
        Asset panorama; panorama.kind = AssetKind::Image; const auto id = project.add_asset(std::move(panorama));
        if (hdrMode == 1) comp->environment().hdri = id;
        else for (const auto layerId : layerIds) { auto* layer = comp->layer(layerId); layer->environmentSource = 1; layer->environmentAsset = id.pack(); }
    }
    comp->floor().mode = floor ? 1 : 0;
    comp->motion_blur().enabled = blur; comp->motion_blur().samples = 16; comp->motion_blur().previewSamples = 16;
    comp->motion_blur().adaptiveLimit = 16;
    TextureDesc desc; desc.width = width; desc.height = height; desc.format = SurfaceFormat::RGBA16F;
    desc.renderTarget = desc.sampled = desc.transferSrc = true; auto texture = backend.create_texture(desc);
    if (!texture) return 4;
    OffscreenTarget target{*texture, width, height}; RenderSettings settings;
    settings.finalQuality = finalQuality; settings.dither = false;
#if defined(AUREA_PROBE_BASELINE)
    if (bounded) { std::fprintf(stderr, "baseline has no bounded-scene-exposure mode\n"); return 2; }
    std::printf("INSTRUMENTATION original_baseline=1 bounded_scene_exposure=unavailable exposure_batches=unavailable batch_fence_wait=unavailable\n");
#else
    settings.boundedSceneExposure = bounded;
    settings.sceneExposureBatchBudgetBytes = static_cast<u64>(batchBudgetMiB) << 20;
#endif
    std::vector<double> prepare, record, wait, total, gpu, cpuRecord, batchWait;
    u64 peakUsed = 0, peakReserved = 0, peakUpload = 0, peakRss = process_rss_bytes();
    u32 peakSamples = 0, peakBatches = 0, draws = 0, triangles = 0, visible = 0, culled = 0, instancedDraws = 0;
    u32 warmup = 0, measured = 0, pendingFrames = 0;
    const auto environmentStarted = Clock::now();
    double environmentReadyMs = -1;
    std::printf("CASE vertices=%u triangles=%zu mode=%s floor=%d hdri=%u blur=%d width=%u height=%u final=%d bounded=%d batch_budget_MiB=%u warmup=30 frames=%u objects=%u materials=%s grid_columns=%u grid_rows=%u total_input_triangles=%llu\n",
        vertices, static_cast<usize>(inputTriangles), modelFile ? "imported" : argv[2], floor, hdrMode, blur, width, height, finalQuality, bounded, batchBudgetMiB, frames,
        objectCount, materialMode, gridColumns, gridRows,
        static_cast<unsigned long long>(objectCount) * inputTriangles);
    std::printf("MODEL fixture=%s import_wall_ms=%.3f nodes=%zu meshes=%zu primitives=%u drawable_primitives=%u images=%zu materials=%zu skins=%zu skinned_primitives=%u morph_primitives=%u animations=%zu clip=%d texture_import_limit=4096 triangle_import_limit=0\n",
        modelFile ? argv[2] : "synthetic", importMs, model->nodes.size(), model->meshes.size(), primitives, drawnPrimitives,
        model->images.size(), model->materials.size(), model->skins.size(), skinnedPrimitives, morphPrimitives, model->animations.size(), clip);
    std::printf("SETUP cold_setup_wall_ms=%.3f rss_bytes=%llu scope=direct_renderer_no_ui_or_engine_scheduler\n", milliseconds(processStarted), static_cast<unsigned long long>(peakRss));
    for (u32 i = 0; measured < frames; ++i) {
        const auto begin = Clock::now(); FrameSnapshot snapshot;
        // Extra environment warmup repeats the current logical clock rather
        // than shifting the animation times relative to the original engine.
        const u32 logicalFrame = warmup < 30 ? warmup : 30 + measured;
        renderer.prepare(*comp, project, FrameIndex{15 + logicalFrame}, nullptr, nullptr, nullptr, settings, i + 1, 0, DecodeMode::Still, 1, snapshot);
        const auto prepared = Clock::now();
        if (renderer.take_incomplete() || snapshot.scenes.empty() || snapshot.scenes[0].instances.empty()) {
            std::printf("FAIL prepare frame=%u scenes=%zu\n", i, snapshot.scenes.size()); return 5;
        }
        if (snapshot.scenes[0].instances.size() != objectCount) { std::printf("FAIL instance fixture mismatch requested=%u actual=%zu\n", objectCount, snapshot.scenes[0].instances.size()); return 5; }
        peakSamples = std::max(peakSamples, static_cast<u32>(snapshot.scenes[0].blurFrames.size()));
        FrameStats stats; RenderTimings timings;
        if (!checked(renderer.render(snapshot, settings, &target, stats, timings), "render")) return 5;
        const bool pendingEnvironment = renderer.environment_pending();
        if (renderer.take_incomplete() && !pendingEnvironment) { std::printf("FAIL incomplete frame=%u\n", i); return 5; }
        if (pendingEnvironment) ++pendingFrames;
        else if (environmentReadyMs < 0) {
            environmentReadyMs = milliseconds(environmentStarted);
            if (hdrMode && environmentReadyMs > 30000) {
                std::printf("FAIL environment settled after %.3fms; 30s deadline exceeded, benchmark invalid\n", environmentReadyMs);
                return 5;
            }
        }
#if !defined(AUREA_PROBE_BASELINE)
        peakBatches = std::max(peakBatches, timings.sceneExposureBatches);
#endif
        const auto submitted = Clock::now();
        const u64 submittedFrame = backend.last_submitted_frame();
        if (!submittedFrame) { std::printf("FAIL no submitted frame\n"); return 5; }
        const auto frameDeadline = submitted + std::chrono::seconds(120);
        for (;;) {
            if (pendingEnvironment && Clock::now() - environmentStarted >= std::chrono::seconds(30)) {
                std::printf("FAIL environment did not settle within 30s; benchmark invalid\n"); return 5;
            }
            if (Clock::now() >= frameDeadline) { std::printf("FAIL frame wait exceeded 120s\n"); return 5; }
            const Status status = backend.wait_frame(submittedFrame, 100ull * 1000 * 1000);
            if (status.ok()) break;
            if (status.code() != Errc::Timeout && !checked(status, "wait frame")) return 5;
        }
        const auto complete = Clock::now();
#if !defined(AUREA_PROBE_VULKAN)
        if (const auto error = glGetError()) { std::printf("FAIL GL error=%u frame=%u\n", error, i); return 6; }
#endif
        const auto memory = backend.memory_stats(); peakUsed = std::max(peakUsed, memory.usedBytes);
        peakReserved = std::max(peakReserved, memory.reservedBytes); peakRss = std::max(peakRss, process_rss_bytes());
        peakUpload = std::max(peakUpload, memory.uploadBytesThisFrame);
        draws = renderer.heavy_stats().lastSceneDrawCalls;
        triangles = renderer.heavy_stats().lastSceneTriangles;
        visible = renderer.heavy_stats().lastSceneVisible; culled = renderer.heavy_stats().lastSceneCulled;
        instancedDraws = renderer.heavy_stats().lastSceneInstancedDraws;
        const u32 expected = objectCount * drawnPrimitives;
        if ((!modelFile && (visible != expected || culled)) || (modelFile && (!visible || visible + culled != expected))) {
            std::printf("FAIL grid visibility expected_primitives=%u visible=%u culled=%u\n", expected, visible, culled); return 5;
        }
        if (i == 0) std::printf("COLD prepare_ms=%.3f render_ms=%.3f wait_ms=%.3f total_ms=%.3f samples=%u\n",
            milliseconds(begin, prepared), milliseconds(prepared, submitted), milliseconds(submitted, complete), milliseconds(begin, complete), peakSamples);
        if (i % 15 == 0) { std::printf("PROGRESS frame=%u total_ms=%.3f gpuBytes=%llu environment_pending=%d\n", i, milliseconds(begin, complete), static_cast<unsigned long long>(memory.usedBytes), pendingEnvironment); std::fflush(stdout); }
        if (warmup < 30 && !pendingEnvironment) ++warmup;
        else if (!pendingEnvironment) {
            prepare.push_back(milliseconds(begin, prepared)); record.push_back(milliseconds(prepared, submitted));
            wait.push_back(milliseconds(submitted, complete)); total.push_back(milliseconds(begin, complete));
            cpuRecord.push_back(timings.cpuRecordMs);
#if !defined(AUREA_PROBE_BASELINE)
            batchWait.push_back(timings.sceneExposureWaitMs);
#endif
            if (timings.gpuMeasured && timings.gpuTotalMs > 0) gpu.push_back(timings.gpuTotalMs);
            ++measured;
        }
        // Warmup can show a valid fallback while the asynchronous environment
        // runs, but fallback frames never enter the comparable measurements.
        if (pendingEnvironment) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::vector<u16> pixels(static_cast<size_t>(width) * height * 4);
    if (!checked(backend.read_texture(*texture, pixels.data(), width * 8), "readback")) return 6;
    if (argc > 11) {
        std::FILE* dump = std::fopen(argv[11], "wb");
        if (!dump) { std::printf("FAIL pixel dump file\n"); return 6; }
        const usize written = std::fwrite(pixels.data(), sizeof(u16), pixels.size(), dump);
        std::fclose(dump);
        if (written != pixels.size()) return 6;
    }
    u64 lit = 0; bool finite = true;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const f32 r = scene3d::half_to_float(pixels[i]), g = scene3d::half_to_float(pixels[i + 1]), b = scene3d::half_to_float(pixels[i + 2]);
        finite &= std::isfinite(r) && std::isfinite(g) && std::isfinite(b);
        if (std::max({r, g, b}) > .005f) ++lit;
    }
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    std::printf("ENVIRONMENT pending_warmup_frames=%u ready_wall_ms=%.3f measured_ready_frames=%u deadline_s=30\n", pendingFrames, environmentReadyMs, measured);
    std::printf("RESULT prepare_p50=%.3f prepare_p95=%.3f prepare_p99=%.3f render_p50=%.3f render_p95=%.3f render_p99=%.3f wait_p50=%.3f wait_p95=%.3f wait_p99=%.3f total_p50=%.3f total_p95=%.3f total_p99=%.3f gpu_p50=%.3f gpu_p95=%.3f gpu_p99=%.3f gpu_measured=%zu peak_gpu_bytes=%llu peak_reported_upload_bytes=%llu rss_peak_KB=%ld scene_primary_draws=%u scene_triangles=%u samples=%u exposure_batches=%u lit=%llu finite=%d\n",
        percentile(prepare, .5), percentile(prepare, .95), percentile(prepare, .99), percentile(record, .5), percentile(record, .95), percentile(record, .99),
        percentile(wait, .5), percentile(wait, .95), percentile(wait, .99), percentile(total, .5), percentile(total, .95), percentile(total, .99), percentile(gpu, .5), percentile(gpu, .95), percentile(gpu, .99), gpu.size(),
        static_cast<unsigned long long>(peakUsed), static_cast<unsigned long long>(peakUpload), usage.ru_maxrss, draws, triangles, peakSamples, peakBatches, static_cast<unsigned long long>(lit), finite);
    std::printf("STAGES record_and_submit_wall_p50=%.3f record_and_submit_wall_p95=%.3f record_and_submit_wall_p99=%.3f batch_fence_wait_p50=%.3f batch_fence_wait_p95=%.3f batch_fence_wait_p99=%.3f gpu_timer_scope=%s record_metric=commands_plus_bounded_submit_wall\n",
        percentile(cpuRecord, .5), percentile(cpuRecord, .95), percentile(cpuRecord, .99), percentile(batchWait, .5), percentile(batchWait, .95), percentile(batchWait, .99),
        bounded ? "latest_completed_submission_only" : "whole_frame");
    std::printf("SCALING objects=%u materials=%s resolved_measured_frames=%u offscreen_waited_fps=%.3f offscreen_waited_1pct_low_fps=%.3f offscreen_waited_p99_fps=%.3f peak_reserved_gpu_bytes=%llu sampled_peak_rss_bytes=%llu visible_primitives=%u culled_primitives=%u instanced_draws=%u fps_scope=offscreen_prepare_record_explicit_wait_not_ui_playback rss_scope=process_includes_driver_not_cpu_only one_percent_low=reciprocal_mean_slowest_ceil_1pct\n",
        objectCount, materialMode, measured, waited_fps(total), waited_one_percent_low(total),
        percentile(total, .99) > 0 ? 1000 / percentile(total, .99) : 0,
        static_cast<unsigned long long>(peakReserved), static_cast<unsigned long long>(peakRss), visible, culled, instancedDraws);
#if !defined(AUREA_PROBE_BASELINE)
    const auto& sceneStats = renderer.scene_stats();
    std::printf("DEFORMATION unique_morph_uploads=%u deformed_vertices=%llu morph_upload_bytes=%llu scope=last_frame_all_scene_and_shutter_builds\n",
        sceneStats.morphUploads, static_cast<unsigned long long>(sceneStats.morphVertices), static_cast<unsigned long long>(sceneStats.morphUploadBytes));
#endif
    backend.wait_idle(); backend.destroy_texture(*texture); renderer.shutdown(); backend.shutdown();
    if (!finite || lit < 100) { std::printf("FAIL blank or nonfinite rendered image\n"); return 7; }
    std::printf("PASS device offscreen benchmark; explicit per-frame waits; not UI playback FPS\n"); return 0;
}
