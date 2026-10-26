// =============================================================================
//  Aurea / engine / EngineHyper.cpp — a API do AureaHyper (0.3.2).
//
//  * O MEDIDOR (hyper_device_profile): um quadro de FSR 1 480p -> 1080p na GPU
//    (os mesmos passes do efeito Hyper Upscale), um tile da rede Lite no
//    Vulkan e na CPU com a conferência entre os dois (driver que erra cai
//    para o próximo caminho), RAM, ABI, GLES3, NPU (a plataforma diz) e o
//    térmico depois. Guardado em cacheDirectory/hyper/device-profile.json com
//    a marca do build + GPU + driver.
//  * A ESTIMATIVA e o PLANO (hyper/HyperPlan.hpp) são puros.
//  * O QUADRO ANTES/DEPOIS (hyper_preview_frame): um renderizador offline
//    próprio sobre o projeto temporário — os MESMOS efeitos do export Turbo.
//  * O JOB DA HOME (start_hyper_job): Stable (análise da fonte) e as
//    passagens pelo export (start_export_checked com HyperExportOverride),
//    cada uma com checkpoint/retomada, validação e publicação atômica; as
//    passagens prontas ficam registradas num manifesto e o mesmo pedido
//    continua de onde parou.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/ai/Upscaler.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Version.hpp"
#include "aurea/export/OfflineFrameQueue.hpp"
#include "aurea/export/OfflineFrameRenderer.hpp"
#include "aurea/export/UpscaleColor.hpp"
#include "aurea/hyper/HyperStage.hpp"
#if defined(AUREA_HYPER_HAS_NEURAL)
#include "aurea/hyper/HyperNeural.hpp"
#endif
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"

#include "hyper/HyperAnalysis.hpp"
#include "hyper/HyperExport.hpp"
#include "hyper/HyperGraph.hpp"
#include "hyper/HyperPlan.hpp"
#include "hyper/HyperStabilize.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace aurea {

// -----------------------------------------------------------------------------
// Estado
// -----------------------------------------------------------------------------
struct Engine::HyperState {
    // Perfil (serializado pelo profileMutex; os campos sob `mutex`).
    std::mutex profileMutex;
    bool profileLoaded = false;
    hyper::DeviceProfile profile{};
    // Job.
    std::mutex startMutex;           ///< um start_hyper_job por vez
    mutable std::mutex mutex;
    std::thread thread;
    std::thread::id threadId{};
    std::atomic<bool> cancel{false};
    std::atomic<bool> running{false};
    hyper::JobSteps steps{};
    std::string source, output;
    hyper::Recipe recipe{};
    hyper::JobPlan plan{};
    hyper::SourceInfo info{};
    u64 unitsTotal = 0, unitsDone = 0;
    f32 lastFps = 0.0f;
    u32 lastFlags = 0, lastFailure = 0;
    char exportMessage[128]{};
};

void Engine::HyperStateDeleter::operator()(HyperState* s) const noexcept { delete s; }

Engine::HyperState& Engine::hyper_state() noexcept {
    static std::mutex creation;
    std::lock_guard<std::mutex> lock(creation);
    if (!hyper_) hyper_.reset(new HyperState());
    return *hyper_;
}

namespace {

using namespace std::chrono_literals;

// --- Pequenos ----------------------------------------------------------------
/// Caminho UTF-8 -> std::filesystem::path (sem o u8path, obsoleto no C++20).
std::filesystem::path fs_path(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),
                                               reinterpret_cast<const char8_t*>(s.data()) + s.size()));
}

std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

const ImagePixels* no_images(void*, AssetId) { return nullptr; }

u64 fnv(u64 h, const void* data, usize n) noexcept {
    const u8* p = static_cast<const u8*>(data);
    for (usize i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
u64 fnv_str(u64 h, const std::string& s) noexcept { return fnv(h, s.data(), s.size()); }
template <typename T> u64 fnv_pod(u64 h, const T& v) noexcept { return fnv(h, &v, sizeof(v)); }

f32 thermal_unit(ThermalState::Level l) noexcept {
    switch (l) {
        case ThermalState::Level::Nominal: return 0.0f;
        case ThermalState::Level::Fair: return 0.25f;
        case ThermalState::Level::Serious: return 0.5f;
        case ThermalState::Level::Critical: return 0.75f;
        case ThermalState::Level::Emergency: return 1.0f;
        default: return 0.0f;
    }
}

/// Fonte sondada: o que o plano e o projeto temporário precisam.
struct Probed {
    std::string path;
    MediaProbe probe{};
    hyper::SourceInfo info{};
};

Status probe_source(VideoSourceFactory* factory, const std::string& path, Probed& out) {
    if (!factory) return Status{Errc::NotSupported, "sem decodificador de video nesta plataforma"};
    out = Probed{};
    out.path = path;
    if (!factory->probe(path.c_str(), out.probe) || !out.probe.hasVideo)
        return Status{Errc::UnsupportedFormat, "arquivo sem trilha de video decodificavel"};
    const VideoStreamInfo& v = out.probe.video;
    out.info.width = v.display_width();
    out.info.height = v.display_height();
    out.info.fps = v.fps > 0.0 ? v.fps : 30.0;
    out.info.durationUs = v.durationUs;
    out.info.hasAudio = out.probe.hasAudio;
    if (!out.info.width || !out.info.height || out.info.durationUs <= 0)
        return Status{Errc::AssetCorrupted, "video sem dimensoes ou duracao"};
    return OkStatus;
}

Asset video_asset(const Probed& p) {
    const VideoStreamInfo& v = p.probe.video;
    Asset a;
    a.kind = AssetKind::Video;
    a.name = "Hyper";
    a.sourcePath = p.path;
    a.originalFilename = utf8(fs_path(p.path).filename());
    a.video.width = p.info.width;
    a.video.height = p.info.height;
    a.video.fps = p.info.fps;
    a.timebaseFps = p.info.fps;
    a.video.frameCount = FrameIndex{static_cast<i64>(std::llround(static_cast<f64>(v.durationUs) * p.info.fps / 1e6))};
    a.duration = a.video.frameCount;
    a.profile.bitDepth = v.color.bitDepth;
    a.profile.hdr = v.color.hdr();
    a.fileSizeBytes = p.probe.fileBytes;
    a.profile.transfer = v.color.transfer == TransferFunction::PQ ? ColorSpace::HDR10
                       : v.color.transfer == TransferFunction::HLG ? ColorSpace::HLG : ColorSpace::Rec709;
    a.profile.primaries = v.color.primaries == ColorPrimaries::BT2020 ? ColorSpace::Rec2020
                        : v.color.primaries == ColorPrimaries::P3 ? ColorSpace::DisplayP3 : ColorSpace::Rec709;
    if (p.probe.hasAudio) {
        a.audio.sampleRate = p.probe.audioSampleRate;
        a.audio.channels = p.probe.audioChannels;
        a.audio.sampleCount = FrameIndex{p.probe.audioDurationUs * static_cast<i64>(p.probe.audioSampleRate) / 1'000'000};
    } else {
        a.audio.sampleRate = 0;
        a.audio.channels = 0;
    }
    return a;
}

/// Um efeito Hyper com os padrões da declaração e alguns valores.
bool add_effect(const EffectRegistry& registry, Layer& layer, const char* key,
                std::initializer_list<std::pair<const char*, f32>> values) {
    const EffectTypeId type = effect_type_id(key);
    const ParameterRegistry* specs = registry.params(type);
    if (!specs) return false;
    EffectInstance e;
    e.id = layer.alloc_effect_id();
    e.type = type;
    initialize_instance(e, *specs);
    for (const auto& [id, v] : values) {
        const u32 i = specs->find(id);
        if (i != kInvalidIndex && i < e.params.size()) e.params[i].constant = ParamValue::scalar(v);
    }
    layer.effects.push_back(std::move(e));
    return true;
}

/// O projeto temporário de UMA passagem. `effects` falso = a fonte ampliada
/// pelo compositor (o "antes" do deslizador).
struct PassProject {
    bool effects = true;
    bool clean = false, denoise = false, upscale = false, motion = false;
    const hyper::StabPlan* stab = nullptr;
};

Status build_project(const EffectRegistry& registry, const hyper::PassPlan& pass, const hyper::Recipe& r,
                     const Probed& src, const PassProject& opt, std::vector<u8>& bytes, CompositionId& comp,
                     Project* keep = nullptr) {
    auto created = Project::create_new(pass.renderW, pass.renderH, pass.fps, "AureaHyper");
    if (!created.ok()) return created.status();
    Project project = std::move(*created);
    // Os bytes têm de ser os MESMOS a cada pedido igual (chave da retomada).
    project.metadata().createdUnixMs = project.metadata().modifiedUnixMs = 0;
    const CompositionId root = project.timeline().root();
    Composition* c = project.timeline().composition(root);
    if (!c) return Errc::InvalidState;
    c->set_background_choice(Color{0, 0, 0, 1});
    c->set_duration(FrameIndex{static_cast<i64>(pass.frames)});
    const AssetId asset = project.add_asset(video_asset(src));
    const LayerId lid = c->add_layer(LayerKind::Video, "Hyper");
    Layer* l = c->layer(lid);
    if (!l) return Errc::OutOfMemory;
    l->source = asset;
    l->start = FrameIndex{0};
    l->end = FrameIndex{static_cast<i64>(pass.frames)};
    l->muted = pass.muteAudio;
    l->transform.anchor = Vec3{static_cast<f32>(src.info.width) * 0.5f, static_cast<f32>(src.info.height) * 0.5f, 0.0f};
    l->transform.position = Vec3{static_cast<f32>(pass.renderW) * 0.5f, static_cast<f32>(pass.renderH) * 0.5f, 0.0f};
    const f32 fit = std::min(static_cast<f32>(pass.renderW) / static_cast<f32>(src.info.width),
                             static_cast<f32>(pass.renderH) / static_cast<f32>(src.info.height));
    l->transform.scale = Vec3{fit, fit, 1.0f};
    if (opt.effects) {
        // Ordem: Clean (resolução da fonte) -> Upscale -> Motion (declarativo).
        if (opt.clean)
            add_effect(registry, *l, hyper::effect_keys::kHyperClean,
                       {{"denoise", opt.denoise ? r.denoise * 100.0f : 0.0f}, {"deblock", r.deblock ? 1.0f : 0.0f},
                        {"deinterlace", static_cast<f32>(r.deinterlace)}, {"frames", 2.0f}});
        if (opt.upscale)
            add_effect(registry, *l, hyper::effect_keys::kHyperUpscale,
                       {{"scale", r.upscale == 4 ? 1.0f : 0.0f}, {"mode", r.upscaleMode == hyper::UpscaleMode::Anime ? 1.0f : 0.0f},
                        {"sharpness", 50.0f}});
        if (opt.motion) {
            const f32 slow = pass.slowMotion >= 8 ? 3.0f : pass.slowMotion >= 4 ? 2.0f : pass.slowMotion >= 2 ? 1.0f : 0.0f;
            add_effect(registry, *l, hyper::effect_keys::kHyperMotion, {{"slow_motion", slow}, {"duplicates", 1.0f}});
        }
        if (opt.stab && !opt.stab->frames.empty())
            hyper::apply_stabilization(*l, *opt.stab, fit, pass.fps * static_cast<f64>(pass.slowMotion) / src.info.fps,
                                       pass.graph_motion());
    }
    if (pass.graph_motion() && pass.slowMotion > 1) {
        // Motion Lite/Max: a camada anda a 1/N e o render repete o quadro de
        // origem; a rede monta os instantes entre eles no grafo.
        l->speed = 1.0f / static_cast<f32>(pass.slowMotion);
    }
    SaveOptions save;
    save.writeThumbnail = false;
    if (const Status s = ProjectSerializer::encode(project, save, bytes); !s.ok()) return s;
    comp = root;
    if (keep) *keep = std::move(project);
    return OkStatus;
}

/// RGBA16F (half) linear pré-multiplicado -> RGBA8 sRGB reto.
void half_to_srgb8(const std::vector<u16>& half, u32 w, u32 h, std::vector<u8>& out) {
    auto h2f = [](u16 v) noexcept {
        const u32 sign = (v & 0x8000u) << 16, exp = (v >> 10) & 0x1Fu, mant = v & 0x3FFu;
        u32 bits;
        if (exp == 0) {
            if (mant == 0) bits = sign;
            else {
                u32 e = 113, m = mant;
                while (!(m & 0x400u)) { m <<= 1; --e; }
                bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
            }
        } else if (exp == 31) {
            bits = sign | 0x7F800000u | (mant << 13);
        } else {
            bits = sign | ((exp + 112) << 23) | (mant << 13);
        }
        f32 f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto enc = [](f32 v) noexcept {
        v = std::clamp(v, 0.0f, 1.0f);
        const f32 e = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        return static_cast<u8>(std::lround(std::clamp(e, 0.0f, 1.0f) * 255.0f));
    };
    out.resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < static_cast<usize>(w) * h; ++i) {
        const f32 a = h2f(half[i * 4 + 3]);
        const f32 inv = a > 1e-5f ? 1.0f / a : 0.0f;
        out[i * 4 + 0] = enc(h2f(half[i * 4 + 0]) * inv);
        out[i * 4 + 1] = enc(h2f(half[i * 4 + 1]) * inv);
        out[i * 4 + 2] = enc(h2f(half[i * 4 + 2]) * inv);
        out[i * 4 + 3] = static_cast<u8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
    }
}

/// Reduz RGBA8 (média de área) para caber em `maxSide`.
void shrink_rgba(std::vector<u8>& px, u32& w, u32& h, u32 maxSide) {
    if (std::max(w, h) <= maxSide) return;
    const f64 k = static_cast<f64>(maxSide) / std::max(w, h);
    const u32 nw = std::max(1u, static_cast<u32>(std::lround(w * k))), nh = std::max(1u, static_cast<u32>(std::lround(h * k)));
    std::vector<u8> out(static_cast<usize>(nw) * nh * 4);
    for (u32 y = 0; y < nh; ++y) {
        const u32 y0 = y * h / nh, y1 = std::max(y0 + 1, (y + 1) * h / nh);
        for (u32 x = 0; x < nw; ++x) {
            const u32 x0 = x * w / nw, x1 = std::max(x0 + 1, (x + 1) * w / nw);
            u32 acc[4]{};
            for (u32 yy = y0; yy < y1; ++yy)
                for (u32 xx = x0; xx < x1; ++xx)
                    for (u32 c = 0; c < 4; ++c) acc[c] += px[(static_cast<usize>(yy) * w + xx) * 4 + c];
            const u32 n = (y1 - y0) * (x1 - x0);
            for (u32 c = 0; c < 4; ++c) out[(static_cast<usize>(y) * nw + x) * 4 + c] = static_cast<u8>((acc[c] + n / 2) / n);
        }
    }
    px.swap(out);
    w = nw;
    h = nh;
}

void rgba_to_rgb24(const std::vector<u8>& rgba, std::vector<u8>& rgb) {
    rgb.resize(rgba.size() / 4 * 3);
    for (usize i = 0, n = rgba.size() / 4; i < n; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
}

const char* step_label(hyper::Step s) noexcept {
    switch (s) {
        case hyper::Step::Prepare: return "preparando";
        case hyper::Step::Clean: return "Hyper Clean";
        case hyper::Step::Stable: return "Hyper Stable";
        case hyper::Step::Upscale: return "Hyper Upscale";
        case hyper::Step::Face: return "Hyper Face";
        case hyper::Step::Motion: return "Hyper Motion";
        case hyper::Step::Encode: return "codificando";
        case hyper::Step::Finish: return "finalizando";
        default: return "";
    }
}

/// O passo que representa uma passagem na tela (o mais caro dela).
hyper::Step pass_step(const hyper::PassPlan& p) noexcept {
    for (const hyper::StageDescriptor* d : p.graph)
        if (d->kind == hyper::StageKind::Upscale) return hyper::Step::Upscale;
    for (const hyper::StageDescriptor* d : p.graph)
        if (d->kind == hyper::StageKind::Face) return hyper::Step::Face;
    if (!p.graph.empty()) return hyper::Step::Clean;
    if (p.renderMotion) return hyper::Step::Motion;
    if (p.renderUpscale) return hyper::Step::Upscale;
    if (p.renderClean) return hyper::Step::Clean;
    return hyper::Step::Encode;
}

std::string profile_path(const std::string& cacheDir) {
    if (cacheDir.empty()) return {};
    return cacheDir + "/hyper/device-profile.json";
}

/// Renderizador offline de poucos quadros (o quadro antes/depois e o medidor).
/// Tudo sob o renderMutex_ do motor: a prévia espera o tempo de um quadro.
struct MiniRender {
    GPUBackend& gpu;
    std::unique_ptr<OfflineFrameRenderer> offline;
    TextureHandle target{};
    u32 width = 0, height = 0;
    explicit MiniRender(GPUBackend& g) : gpu(g) {}
    Status init(const EffectRegistry& effects, VideoSourceFactory* sources, MemoryManager* memory, u32 w, u32 h) {
        offline = std::make_unique<OfflineFrameRenderer>();
        if (const Status s = offline->initialize(gpu, effects, sources, memory, 256ull << 20, 20'000'000'000ull); !s.ok()) return s;
        TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = SurfaceFormat::RGBA16F;
        d.sampled = true;
        d.renderTarget = true;
        d.transferSrc = true;
        d.debugName = "hyper-quadro";
        auto t = gpu.create_texture(d);
        if (!t.ok()) return t.status();
        target = *t;
        width = w;
        height = h;
        return OkStatus;
    }
    Status render(const Project& p, CompositionId comp, FrameIndex t, const ImagePixels* (*images)(void*, AssetId), void* ctx,
                  const std::atomic<bool>* cancel, f64* gpuMs = nullptr) {
        OffscreenTarget tg;
        tg.texture = target;
        tg.width = width;
        tg.height = height;
        RenderSettings settings;
        settings.finalQuality = true;
        settings.cancelFlag = cancel;
        u64 submitted = 0;
        const u64 t0 = monotonic_ns();
        const Status s = offline->render(p, comp, t, tg, images ? images : &no_images, ctx, settings, submitted);
        if (!s.ok()) return s;
        if (submitted) {
            const Status w = gpu.wait_frame(submitted, 20'000'000'000ull);
            if (!w.ok()) return w;
        }
        if (gpuMs) *gpuMs = static_cast<f64>(monotonic_ns() - t0) / 1e6;
        return OkStatus;
    }
    Status read(std::vector<u8>& rgba) {
        std::vector<u16> half(static_cast<usize>(width) * height * 4);
        if (const Status s = gpu.read_texture(target, half.data(), width * 8); !s.ok()) return s;
        half_to_srgb8(half, width, height, rgba);
        return OkStatus;
    }
    ~MiniRender() {
        gpu.wait_idle();
        if (target.valid()) gpu.destroy_texture(target);
        if (offline) {
            offline->retire_gpu_after_done();
            for (int i = 0; i < 400 && !offline->retire_sources(); ++i) std::this_thread::sleep_for(5ms);
            offline.reset();
        }
    }
};

} // namespace

// =============================================================================
// Medidor
// =============================================================================
hyper::DeviceProfile Engine::hyper_cached_profile() noexcept {
    HyperState& h = hyper_state();
    {
        std::lock_guard<std::mutex> l(h.mutex);
        if (h.profileLoaded) return h.profile;
    }
    hyper::DeviceProfile p;
    p.abi64 = sizeof(void*) == 8;
    p.freeRamBytes = caps_.cpu().availableMemoryBytes;
    p.vulkan = gpu_ && std::strcmp(gpu_->name(), "Vulkan") == 0;
    p.gles3 = caps_.gpu().openGLES || (gpu_ && std::strstr(gpu_->name(), "GLES") != nullptr);
    p.npu = config_.hyperNpuAvailable;
    p.tile = p.abi64 ? 128u : 64u;
    p.tileMargin = p.abi64 ? 12u : 8u;
    p.defaultPath = hyper::Path::Turbo;
    p.backendNote = "sem medicao (Turbo)";
    // Do disco, se a marca bate.
    const std::string path = profile_path(config_.cacheDirectory);
    std::vector<u8> bytes;
    if (!path.empty() && fileio::read_all(path, bytes, 64 * 1024)) {
        hyper::DeviceProfile loaded;
        std::string stamp;
        const std::string text(bytes.begin(), bytes.end());
        const GpuCapabilities& g = caps_.gpu();
        const std::string want = std::string(AUREA_VERSION_LABEL) + "|" + (gpu_ ? gpu_->name() : "sem-gpu") + "|"
                               + g.deviceName + "|" + g.driverVersion;
        if (hyper::profile_from_json(text, loaded, stamp) && stamp == want) {
            loaded.npu = config_.hyperNpuAvailable;
            p = loaded;
            std::lock_guard<std::mutex> l(h.mutex);
            h.profile = p;
            h.profileLoaded = true;
            return p;
        }
    }
    return p;
}

hyper::DeviceProfile Engine::hyper_device_profile(bool remeasure) noexcept {
    HyperState& h = hyper_state();
    std::lock_guard<std::mutex> serial(h.profileMutex);
    hyper::DeviceProfile p = hyper_cached_profile();
    if (p.measured && !remeasure) return p;
    if (exportActive_.load(std::memory_order_acquire) || h.running.load(std::memory_order_acquire) || !gpu_
        || state_ == EngineState::ShuttingDown || state_ == EngineState::Uninitialized)
        return p;
    const u64 began = monotonic_ns();
    std::string note;
    p.freeRamBytes = caps_.cpu().availableMemoryBytes;
    p.abi64 = sizeof(void*) == 8;
    // Tile das redes: 128 em 64 bits com folga de RAM, 96 com pouca, 64 em 32 bits.
    p.tile = p.abi64 ? (p.freeRamBytes && p.freeRamBytes < (2ull << 30) ? 96u : 128u) : 64u;
    p.tileMargin = p.abi64 ? 12u : 8u;
    // --- Turbo: FSR 1, 480p -> 1080p (o efeito Hyper Upscale de verdade) ------
    {
        Project project;
        if (auto created = Project::create_new(1920, 1080, 30.0, "medidor"); created.ok()) project = std::move(*created);
        Composition* c = project.timeline().composition(project.timeline().root());
        ImagePixels px;
        px.width = 854;
        px.height = 480;
        px.rgba.resize(static_cast<usize>(px.width) * px.height * 4);
        for (u32 y = 0; y < px.height; ++y)
            for (u32 x = 0; x < px.width; ++x) {
                u8* q = &px.rgba[(static_cast<usize>(y) * px.width + x) * 4];
                const u32 v = ((x / 7) ^ (y / 5)) * 37u + x * 3u + y;
                q[0] = static_cast<u8>(v); q[1] = static_cast<u8>(v * 7u); q[2] = static_cast<u8>(255u - v); q[3] = 255;
            }
        AssetId assetId{};
        if (c) {
            Asset a;
            a.kind = AssetKind::Image;
            a.name = "medidor";
            a.video.width = px.width;
            a.video.height = px.height;
            assetId = project.add_asset(std::move(a));
            const LayerId lid = c->add_layer(LayerKind::Image, "medidor");
            if (Layer* l = c->layer(lid)) {
                l->source = assetId;
                l->end = FrameIndex{30};
                l->transform.anchor = Vec3{px.width * 0.5f, px.height * 0.5f, 0};
                l->transform.position = Vec3{960.0f, 540.0f, 0};
                const f32 s = 1080.0f / static_cast<f32>(px.height);
                l->transform.scale = Vec3{s, s, 1};
                add_effect(effectRegistry_, *l, hyper::effect_keys::kHyperUpscale, {{"scale", 1.0f}, {"sharpness", 50.0f}});
            }
            c->set_duration(FrameIndex{30});
        }
        struct Img { AssetId id; const ImagePixels* px; } img{assetId, &px};
        auto images = [](void* p, AssetId id) -> const ImagePixels* {
            const auto& i = *static_cast<const Img*>(p);
            return id == i.id ? i.px : nullptr;
        };
        std::lock_guard<std::mutex> rl(renderMutex_);
        if (!exportActive_.load(std::memory_order_acquire) && c) {
            MiniRender mr(*gpu_);
            if (mr.init(effectRegistry_, config_.mediaFactory, &memory_, 1920, 1080).ok()) {
                f64 ms = 0.0, best = 1e9;
                // 1º quadro compila os pipelines; os seguintes medem.
                for (int i = 0; i < 4; ++i) {
                    if (!mr.render(project, project.timeline().root(), FrameIndex{i}, images, &img, nullptr, &ms).ok()) { best = -1; break; }
                    if (i > 0) best = std::min(best, ms);
                }
                if (best > 0.0 && best < 1e8) p.turboMsPerFrame = static_cast<f32>(best);
                else note += "turbo: falhou; ";
            } else {
                note += "turbo: renderizador indisponivel; ";
            }
        }
    }
    // --- Lite: um tile da rede (Vulkan e CPU) com a conferência ---------------
#if defined(AUREA_HYPER_HAS_NEURAL)
    // As redes (src/hyper/neural): a referência do SPAN Lite na CPU e no
    // Vulkan (fp32/fp16) e o tempo de um tile no melhor backend.
    {
        const hyper::NeuralMeter meter = hyper::measure_neural(p, nullptr);
        hyper::apply_neural_meter(meter, p);
        p.vulkan = p.vulkan || meter.vulkanOk;
        if (!meter.note.empty()) note += meter.note + "; ";
    }
    if (p.liteMsPerTile <= 0.0f)
#endif
    {
        constexpr u32 kTile = 64;
        std::vector<u8> rgb(static_cast<usize>(kTile) * kTile * 3);
        for (u32 y = 0; y < kTile; ++y)
            for (u32 x = 0; x < kTile; ++x) {
                u8* q = &rgb[(static_cast<usize>(y) * kTile + x) * 3];
                const bool line = ((x + y) % 11) < 2 || (x % 17) == 0;
                q[0] = line ? 20 : static_cast<u8>(180 + (x & 31));
                q[1] = line ? 30 : static_cast<u8>(140 + (y & 31));
                q[2] = line ? 40 : 90;
            }
        struct Out { std::vector<u8> px; u32 w; };
        auto run = [&](ai::Upscaler::Backend backend, Out& out, f64& ms) -> Status {
            ai::Upscaler up;
            if (const Status s = up.load(2, 2, 128, backend); !s.ok()) return s;
            if (backend != ai::Upscaler::Backend::Auto && up.backend() != backend) return Status{Errc::NotSupported, "backend recusado"};
            out.w = kTile * 2;
            out.px.assign(static_cast<usize>(out.w) * out.w * 3, 0);
            static const std::atomic<bool> never{false};
            f64 best = 1e9;
            for (int pass = 0; pass < 2; ++pass) {
                const u64 t0 = monotonic_ns();
                const Status s = up.run(rgb.data(), kTile, kTile, kTile * 3, never,
                    [](void* o, const ai::Upscaler::Tile& t) {
                        auto& dst = *static_cast<Out*>(o);
                        for (u32 r = 0; r < t.height; ++r)
                            std::memcpy(&dst.px[(static_cast<usize>(t.y + r) * dst.w + t.x) * 3], t.rgb + static_cast<usize>(r) * t.stride,
                                        static_cast<usize>(t.width) * 3);
                        return true;
                    }, &out);
                if (!s.ok()) return s;
                best = std::min(best, static_cast<f64>(monotonic_ns() - t0) / 1e6);
            }
            ms = best;
            return OkStatus;
        };
        Out cpu{}, vk{};
        f64 cpuMs = 0, vkMs = 0;
        const Status cpuOk = run(ai::Upscaler::Backend::Cpu, cpu, cpuMs);
        const Status vkOk = run(ai::Upscaler::Backend::Vulkan, vk, vkMs);
        p.vulkan = p.vulkan || vkOk.ok();
        if (vkOk.ok() && cpuOk.ok()) {
            // fp16 do driver: o tile tem de sair igual ao da CPU (tolerância de meia precisão).
            u64 sum = 0;
            u32 worst = 0;
            for (usize i = 0; i < cpu.px.size(); ++i) {
                const u32 d = static_cast<u32>(std::abs(static_cast<i32>(cpu.px[i]) - static_cast<i32>(vk.px[i])));
                sum += d;
                worst = std::max(worst, d);
            }
            const f64 mean = static_cast<f64>(sum) / std::max<usize>(1, cpu.px.size());
            p.vulkanFp16 = mean <= 2.0 && worst <= 40;
            if (!p.vulkanFp16) {
                char b[96];
                std::snprintf(b, sizeof(b), "vulkan: tile diferente da CPU (media %.2f, pior %u): recusado; ", mean, worst);
                note += b;
            }
        } else if (!vkOk.ok()) {
            note += "vulkan: rede indisponivel; ";
        }
        if (!cpuOk.ok()) note += "cpu: rede indisponivel; ";
        // O tile medido é 64²: o da produção é 128² (4x a área útil).
        if (p.vulkanFp16 && vkOk.ok()) p.liteMsPerTile = static_cast<f32>(vkMs * 4.0);
        else if (cpuOk.ok()) p.liteMsPerTile = static_cast<f32>(cpuMs * 4.0);
    }
    p.thermalAfter = thermal_unit(caps_.thermal().level);
    p.npu = config_.hyperNpuAvailable;
    p.measured = true;
    p.defaultPath = hyper::suggest_path(p);
    char tail[96];
    std::snprintf(tail, sizeof(tail), "medido em %.1f s", static_cast<f64>(monotonic_ns() - began) / 1e9);
    p.backendNote = note + tail;
    const std::string path = profile_path(config_.cacheDirectory);
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(fs_path(path).parent_path(), ec);
        const GpuCapabilities& g = caps_.gpu();
        const std::string stamp = std::string(AUREA_VERSION_LABEL) + "|" + gpu_->name() + "|" + g.deviceName + "|" + g.driverVersion;
        const std::string json = hyper::profile_to_json(p, stamp);
        if (!fileio::write_atomic(path, json.data(), json.size()).ok()) AUREA_LOG_WARN("hyper: perfil do aparelho nao gravado");
    }
    AUREA_LOG_INFO("hyper: medidor turbo=%.2f ms lite=%.2f ms/tile vulkanFp16=%d caminho=%u (%s)",
                   static_cast<f64>(p.turboMsPerFrame), static_cast<f64>(p.liteMsPerTile), p.vulkanFp16 ? 1 : 0,
                   static_cast<u32>(p.defaultPath), p.backendNote.c_str());
    {
        std::lock_guard<std::mutex> l(h.mutex);
        h.profile = p;
        h.profileLoaded = true;
    }
    return p;
}

// =============================================================================
// Estimativa
// =============================================================================
namespace {
hyper::OutputCaps output_caps(const DeviceCapabilities& caps, GPUBackend* gpu) noexcept {
    hyper::OutputCaps oc;
    oc.capLong = std::max(caps.max_export_width(), caps.max_export_height());
    oc.capShort = std::min(caps.max_export_width(), caps.max_export_height());
    oc.maxTexture = gpu ? gpu->capabilities().maxTexture2D : 0;
    const u32 encFps = caps.encoder_h264().supported ? caps.encoder_h264().maxFps : 0;
    oc.maxFps = encFps ? static_cast<f64>(encFps) : 240.0;
    return oc;
}
} // namespace

hyper::Estimate Engine::hyper_estimate(const hyper::Recipe& recipe, const char* sourcePath) noexcept {
    try {
        if (!sourcePath || !*sourcePath) return hyper::Estimate{};
        Probed src;
        if (!probe_source(config_.mediaFactory, resolve_asset_path(sourcePath), src).ok()) return hyper::Estimate{};
        const hyper::DeviceProfile profile = hyper_cached_profile();
        return hyper::estimate_job(recipe, profile, src.info, output_caps(caps_, gpu_.get()), hyper::stage_registry());
    } catch (...) {
        return hyper::Estimate{};
    }
}

// =============================================================================
// Quadro antes/depois
// =============================================================================
Status Engine::hyper_preview_frame(const hyper::Recipe& recipeIn, const char* sourcePath, f64 seconds, hyper::Path path,
                                   std::vector<u8>& beforeRgba, std::vector<u8>& afterRgba, u32& width, u32& height) noexcept {
    try {
        width = height = 0;
        if (!sourcePath || !*sourcePath || !std::isfinite(seconds)) return Errc::InvalidArgument;
        if (!gpu_) return Status{Errc::NotSupported, "sem GPU"};
        HyperState& h = hyper_state();
        if (exportActive_.load(std::memory_order_acquire) || h.running.load(std::memory_order_acquire))
            return Status{Errc::Busy, "a previa espera o export/job terminar"};
        Probed src;
        if (const Status s = probe_source(config_.mediaFactory, resolve_asset_path(sourcePath), src); !s.ok()) return s;
        hyper::Recipe recipe = hyper::sanitize(recipeIn);
        recipe.path = path;
        // Um quadro parado: o Motion não muda nada; Stable precisa do vídeo inteiro.
        recipe.targetFps = 0.0;
        recipe.slowMotion = 1;
        recipe.stabilize = false;
        const hyper::DeviceProfile profile = hyper_cached_profile();
        auto plan = hyper::plan_job(recipe, profile, src.info, output_caps(caps_, gpu_.get()), hyper::stage_registry());
        if (!plan.ok()) return plan.status();
        const hyper::PassPlan& first = plan->passes.front();
        // O "depois": a 1ª passagem com os efeitos dela; as redes no grafo.
        const u32 outW = plan->outW, outH = plan->outH;
        hyper::PassPlan before = first;
        before.renderW = outW;
        before.renderH = outH;
        before.frames = std::max<u64>(first.frames, 1);
        std::vector<u8> beforeBytes, afterBytes;
        CompositionId beforeComp{}, afterComp{};
        Project beforeProject, afterProject;
        PassProject none;
        none.effects = false;
        if (const Status s = build_project(effectRegistry_, before, recipe, src, none, beforeBytes, beforeComp, &beforeProject); !s.ok()) return s;
        PassProject fx;
        fx.clean = first.renderClean;
        fx.denoise = first.renderDenoise;
        fx.upscale = first.renderUpscale;
        if (const Status s = build_project(effectRegistry_, first, recipe, src, fx, afterBytes, afterComp, &afterProject); !s.ok()) return s;
        const FrameIndex t{static_cast<i64>(std::clamp(std::floor(seconds * first.fps + 1e-6), 0.0,
                                                      static_cast<f64>(std::max<u64>(first.frames, 1) - 1)))};
        std::vector<u8> beforePx, afterPx;
        {
            std::lock_guard<std::mutex> rl(renderMutex_);
            if (exportActive_.load(std::memory_order_acquire)) return Status{Errc::Busy, "a previa espera o export terminar"};
            {
                MiniRender mr(*gpu_);
                if (const Status s = mr.init(effectRegistry_, config_.mediaFactory, &memory_, outW, outH); !s.ok()) return s;
                if (const Status s = mr.render(beforeProject, beforeComp, t, nullptr, nullptr, nullptr); !s.ok()) return s;
                if (const Status s = mr.read(beforePx); !s.ok()) return s;
            }
            {
                MiniRender mr(*gpu_);
                if (const Status s = mr.init(effectRegistry_, config_.mediaFactory, &memory_, first.renderW, first.renderH); !s.ok()) return s;
                if (const Status s = mr.render(afterProject, afterComp, t, nullptr, nullptr, nullptr); !s.ok()) return s;
                if (const Status s = mr.read(afterPx); !s.ok()) return s;
            }
        }
        u32 aw = first.renderW, ah = first.renderH;
        // Etapas Lite/Max (o grafo, aqui mesmo, num quadro).
        std::vector<const hyper::StageDescriptor*> stages;
        for (const hyper::PassPlan& p : plan->passes)
            for (const hyper::StageDescriptor* d : p.graph) stages.push_back(d);
        if (!stages.empty()) {
            hyper::FrameBuffer in;
            if (const Status s = in.resize(aw, ah); !s.ok()) return s;
            std::vector<u8> rgb;
            rgba_to_rgb24(afterPx, rgb);
            if (!hyper::rgb24_to_nv12(rgb.data(), aw * 3, aw, ah, in)) return Errc::InvalidState;
            hyper::Recipe gr = recipe;
            gr.upscale = static_cast<u64>(std::min(aw, ah)) * 2 >= std::min(outW, outH) ? 2u : recipe.upscale;
            hyper::HyperGraph graph;
            static const std::atomic<bool> never{false};
            if (const Status s = graph.start(profile, gr, stages, hyper::StageFormat{aw, ah, first.fps}, outW, outH, &never, nullptr, nullptr);
                !s.ok()) return s;
            auto pushed = graph.try_push(in.view());
            if (!pushed.ok()) return pushed.status();
            graph.end_input();
            const hyper::FrameBuffer* outFrame = nullptr;
            for (int i = 0; i < 6000; ++i) {
                auto got = graph.pop(outFrame, 100'000'000ull);
                if (!got.ok()) return got.status();
                if (*got) break;
            }
            if (!outFrame) return Status{Errc::Timeout, "o grafo Hyper nao entregou o quadro"};
            aw = outFrame->width;
            ah = outFrame->height;
            std::vector<u8> rgbOut(static_cast<usize>(aw) * ah * 3);
            hyper::nv12_to_rgb24(outFrame->view(), rgbOut.data(), aw * 3);
            afterPx.resize(static_cast<usize>(aw) * ah * 4);
            for (usize i = 0, n = static_cast<usize>(aw) * ah; i < n; ++i) {
                afterPx[i * 4 + 0] = rgbOut[i * 3 + 0];
                afterPx[i * 4 + 1] = rgbOut[i * 3 + 1];
                afterPx[i * 4 + 2] = rgbOut[i * 3 + 2];
                afterPx[i * 4 + 3] = 255;
            }
            graph.shutdown();
        }
        // Uma Turbo depois da rede (passagem 2) mostraria o mesmo upscale: o
        // "depois" já tem o tamanho de saída. Diferença de tamanho: ajusta.
        u32 bw = outW, bh = outH;
        shrink_rgba(beforePx, bw, bh, 1920);
        shrink_rgba(afterPx, aw, ah, 1920);
        if (aw != bw || ah != bh) {
            // Mesmo quadro, densidades diferentes (passagem Turbo pendente): o
            // "depois" é reamostrado para o tamanho do "antes".
            std::vector<u8> fitted(static_cast<usize>(bw) * bh * 4);
            for (u32 y = 0; y < bh; ++y)
                for (u32 x = 0; x < bw; ++x) {
                    const u32 sx = std::min(aw - 1, x * aw / bw), sy = std::min(ah - 1, y * ah / bh);
                    std::memcpy(&fitted[(static_cast<usize>(y) * bw + x) * 4], &afterPx[(static_cast<usize>(sy) * aw + sx) * 4], 4);
                }
            afterPx.swap(fitted);
        }
        beforeRgba = std::move(beforePx);
        afterRgba = std::move(afterPx);
        width = bw;
        height = bh;
        return OkStatus;
    } catch (...) {
        return Errc::OutOfMemory;
    }
}

// =============================================================================
// Job
// =============================================================================
bool Engine::hyper_cancel_job() noexcept {
    if (!hyper_) return false;
    HyperState& h = *hyper_;
    if (!h.running.load(std::memory_order_acquire)) return false;
    h.cancel.store(true, std::memory_order_release);
    return true;
}

void Engine::hyper_shutdown() noexcept {
    if (!hyper_) return;
    HyperState& h = *hyper_;
    h.cancel.store(true, std::memory_order_release);
    if (h.thread.joinable()) h.thread.join();
}

void Engine::hyper_forget_job() noexcept {
    if (!hyper_) return;
    HyperState& h = *hyper_;
    if (h.running.load(std::memory_order_acquire)) return;   // o job recusa o export de qualquer forma
    std::lock_guard<std::mutex> l(h.mutex);
    h.steps.active = false;
}

hyper::JobSteps Engine::hyper_job_steps() const noexcept {
    if (!hyper_) return hyper::JobSteps{};
    std::lock_guard<std::mutex> l(hyper_->mutex);
    return hyper_->steps;
}

void Engine::hyper_overlay_progress(ExportProgress& p) const noexcept {
    if (!hyper_) return;
    const HyperState& h = *hyper_;
    std::lock_guard<std::mutex> l(h.mutex);
    if (!h.steps.active || std::this_thread::get_id() == h.threadId) return;
    const u64 total = std::max<u64>(1, h.unitsTotal);
    p.framesTotal = static_cast<u32>(std::min<u64>(total, 0xFFFFFFFFull));
    p.framesDone = static_cast<u32>(std::min<u64>(h.unitsDone, p.framesTotal));
    if (h.steps.running) {
        p.running = true;
        p.finished = false;
        p.result = Errc::Ok;
        if (!(p.fps > 0.0f)) p.fps = h.lastFps;
        p.etaSeconds = p.fps > 0.0f ? static_cast<u32>(static_cast<f64>(total - std::min(total, h.unitsDone)) / p.fps) : 0;
        p.flags = (p.flags & ~kExportPhaseMask) | (h.lastFlags & ~kExportPhaseMask)
                | (static_cast<u32>(ExportPhase::Rendering) << kExportPhaseShift);
        std::snprintf(p.message, sizeof(p.message), "%s", h.steps.message);
    } else if (h.steps.finished) {
        p.running = false;
        p.finished = true;
        p.result = h.steps.result;
        p.failure = h.lastFailure;
        if (h.steps.result == Errc::Ok) p.framesDone = p.framesTotal;
        std::snprintf(p.message, sizeof(p.message), "%s", h.steps.message);
    }
}

Status Engine::start_hyper_job(const char* sourcePath, const hyper::Recipe& recipeIn, const char* outputPath) noexcept {
    try {
        if (!sourcePath || !*sourcePath || !outputPath || !*outputPath) return Errc::InvalidArgument;
        if (state_ != EngineState::Ready && state_ != EngineState::Rendering) return Errc::InvalidState;
        if (!gpu_) return Status{Errc::NotSupported, "export precisa de GPU"};
        if (!config_.exportSinkFactory) return Status{Errc::NotSupported, "sem encoder de video nesta plataforma"};
        HyperState& h = hyper_state();
        std::lock_guard<std::mutex> starting(h.startMutex);
        if (h.running.load(std::memory_order_acquire)) return Status{Errc::InvalidState, "ja existe um job Hyper em andamento"};
        {
            const ExportProgress p = export_progress();
            if (p.running) return Status{Errc::InvalidState, "ja existe um export em andamento"};
        }
        if (h.thread.joinable()) h.thread.join();
        const hyper::Recipe recipe = hyper::sanitize(recipeIn);
        if (hyper::is_identity(recipe)) return Status{Errc::InvalidArgument, "receita sem nenhuma etapa"};
        Probed src;
        const std::string resolved = resolve_asset_path(sourcePath);
        if (const Status s = probe_source(config_.mediaFactory, resolved, src); !s.ok()) return s;
        const hyper::DeviceProfile profile = hyper_cached_profile();
        auto plan = hyper::plan_job(recipe, profile, src.info, output_caps(caps_, gpu_.get()), hyper::stage_registry());
        if (!plan.ok()) return plan.status();
        if (config_.maxExportSeconds > 0 && static_cast<f64>(plan->outFrames) / plan->outFps > config_.maxExportSeconds + 1e-6)
            return export_too_long_status();
        {
            std::lock_guard<std::mutex> l(h.mutex);
            h.source = resolved;
            h.output = outputPath;
            h.recipe = recipe;
            h.plan = *plan;
            h.info = src.info;
            h.steps = hyper::JobSteps{};
            h.steps.active = true;
            h.steps.running = true;
            h.steps.present = plan->present;
            h.steps.paths = plan->paths;
            h.steps.passCount = static_cast<u32>(plan->passes.size());
            h.steps.audioMuted = plan->muteAudio && src.info.hasAudio;
            h.steps.outputWidth = plan->outW;
            h.steps.outputHeight = plan->outH;
            h.steps.outputFps = plan->outFps;
            h.steps.outputFrames = static_cast<u32>(std::min<u64>(plan->outFrames, 0xFFFFFFFFull));
            h.steps.current = hyper::Step::Prepare;
            std::snprintf(h.steps.message, sizeof(h.steps.message), "%s", "preparando");
            const u64 srcFrames = static_cast<u64>(std::llround(static_cast<f64>(src.info.durationUs) * src.info.fps / 1e6));
            h.unitsTotal = recipe.stabilize ? srcFrames : 0;
            for (const hyper::PassPlan& p : plan->passes) h.unitsTotal += p.frames;
            h.unitsDone = 0;
            h.lastFps = 0.0f;
            h.lastFlags = 0;
            h.lastFailure = 0;
        }
        h.cancel.store(false, std::memory_order_release);
        h.running.store(true, std::memory_order_release);
        AUREA_LOG_INFO("hyper: job %ux%u %.3f fps, %u passagens, caminho %u (%s)", plan->outW, plan->outH, plan->outFps,
                       static_cast<u32>(plan->passes.size()), static_cast<u32>(hyper::effective_path(recipe, profile)),
                       plan->note.c_str());
        h.thread = std::thread([this] { hyper_job_main(); });
        return OkStatus;
    } catch (...) {
        if (hyper_) hyper_->running.store(false, std::memory_order_release);
        return Errc::OutOfMemory;
    }
}

void Engine::hyper_job_main() noexcept {
    set_current_thread_name("aurea-hyper-job");
    HyperState& h = *hyper_;
    hyper::JobPlan plan;
    std::string source, output;
    hyper::Recipe recipe;
    {
        std::lock_guard<std::mutex> l(h.mutex);
        h.threadId = std::this_thread::get_id();
        plan = h.plan;
        source = h.source;
        output = h.output;
        recipe = h.recipe;
    }
    auto set_step = [&](hyper::Step s, f32 frac, const char* extra) {
        std::lock_guard<std::mutex> l(h.mutex);
        h.steps.current = s;
        h.steps.stepProgress = std::clamp(frac, 0.0f, 1.0f);
        h.steps.progress = h.unitsTotal ? static_cast<f32>(static_cast<f64>(h.unitsDone) / h.unitsTotal) : 0.0f;
        if (h.steps.passCount > 1 && s != hyper::Step::Stable && s != hyper::Step::Prepare)
            std::snprintf(h.steps.message, sizeof(h.steps.message), "%s (%u/%u)%s%s", step_label(s), h.steps.pass + 1,
                          h.steps.passCount, extra && *extra ? " · " : "", extra ? extra : "");
        else
            std::snprintf(h.steps.message, sizeof(h.steps.message), "%s%s%s", step_label(s), extra && *extra ? " · " : "",
                          extra ? extra : "");
    };
    auto finish = [&](Status result) {
        {
            std::lock_guard<std::mutex> l(h.mutex);
            h.steps.running = false;
            h.steps.finished = true;
            h.steps.result = result.code();
            if (result.ok()) {
                for (u32 i = 0; i < hyper::kStepCount; ++i) if (h.steps.present[i]) h.steps.done[i] = true;
                h.steps.current = hyper::Step::Finish;
                h.steps.progress = 1.0f;
                h.unitsDone = h.unitsTotal;
                std::snprintf(h.steps.message, sizeof(h.steps.message), "%s", "concluido");
            } else {
                std::snprintf(h.steps.message, sizeof(h.steps.message), "%s",
                              result.code() == Errc::Cancelled ? "cancelado"
                              : (result.detail().empty() ? "falhou" : std::string(result.detail()).c_str()));
            }
            h.threadId = std::thread::id{};
        }
        h.running.store(false, std::memory_order_release);
        AUREA_LOG_INFO("hyper: job terminou (%d)", result.raw());
    };
    auto cancelled = [&] { return h.cancel.load(std::memory_order_acquire); };
    try {
        { std::lock_guard<std::mutex> l(h.mutex); h.steps.done[static_cast<usize>(hyper::Step::Prepare)] = true; }
        // --- Chave do pedido e a pasta dos intermediários ------------------------
        u64 key = 1469598103934665603ull;
        key = fnv_str(key, source);
        key = fnv_str(key, output);
        key = fnv_str(key, AUREA_VERSION_LABEL);
        {
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(fs_path(source), ec);
            if (!ec) key = fnv_pod(key, static_cast<u64>(bytes));
            const auto when = std::filesystem::last_write_time(fs_path(source), ec);
            if (!ec) key = fnv_pod(key, static_cast<i64>(when.time_since_epoch().count()));
        }
        key = fnv_pod(key, recipe.upscale); key = fnv_pod(key, recipe.upscaleMode); key = fnv_pod(key, recipe.denoise);
        key = fnv_pod(key, recipe.deblock); key = fnv_pod(key, recipe.deinterlace); key = fnv_pod(key, recipe.face);
        key = fnv_pod(key, recipe.faceStrength); key = fnv_pod(key, recipe.targetFps); key = fnv_pod(key, recipe.slowMotion);
        key = fnv_pod(key, recipe.stabilize); key = fnv_pod(key, recipe.stabilizeStrength);
        key = fnv_pod(key, recipe.stabilizeToKeyframes);
        for (const hyper::PassPlan& p : plan.passes)
            for (const hyper::StageDescriptor* d : p.graph) key = fnv_str(key, d->id);
        std::string dir = !config_.exportTemporaryDirectory.empty() ? config_.exportTemporaryDirectory
                        : !config_.cacheDirectory.empty() ? config_.cacheDirectory + "/hyper"
                        : utf8(fs_path(output).parent_path());
        if (dir.empty()) dir = ".";
        {
            std::error_code ec;
            std::filesystem::create_directories(fs_path(dir), ec);
        }
        char keyHex[24];
        std::snprintf(keyHex, sizeof(keyHex), "%016llx", static_cast<unsigned long long>(key));
        const std::string manifestPath = dir + "/aurea-hyper-" + keyHex + ".job";
        std::vector<u8> manifest;
        (void)fileio::read_all(manifestPath, manifest, 4096);
        const std::string manifestText(manifest.begin(), manifest.end());

        // --- Stable: a fonte inteira, uma vez -------------------------------------
        hyper::StabPlan stab;
        Probed src;
        if (const Status s = probe_source(config_.mediaFactory, source, src); !s.ok()) { finish(s); return; }
        if (recipe.stabilize && plan.present[static_cast<usize>(hyper::Step::Stable)]) {
            set_step(hyper::Step::Stable, 0.0f, nullptr);
            const Asset asset = video_asset(src);
            hyper::StabAnalysis rq;
            rq.factory = config_.mediaFactory;
            rq.asset = &asset;
            rq.fps = src.info.fps;
            rq.frames = static_cast<u64>(std::llround(static_cast<f64>(src.info.durationUs) * src.info.fps / 1e6));
            rq.strength = recipe.stabilizeStrength;
            rq.crop = !recipe.stabilizeToKeyframes;
            rq.cancel = &h.cancel;
            struct Ctx { HyperState* h; u64 frames; } pc{&h, rq.frames};
            rq.progress = [](void* p, f32 f) noexcept {
                auto& c = *static_cast<Ctx*>(p);
                std::lock_guard<std::mutex> l(c.h->mutex);
                c.h->unitsDone = static_cast<u64>(f * static_cast<f64>(c.frames));
                c.h->steps.stepProgress = f;
                c.h->steps.progress = c.h->unitsTotal ? static_cast<f32>(static_cast<f64>(c.h->unitsDone) / c.h->unitsTotal) : 0.0f;
            };
            rq.progressContext = &pc;
            const Status analyzed = hyper::analyze_stabilization(rq, stab);
            if (cancelled()) { finish(Errc::Cancelled); return; }
            if (!analyzed.ok()) { finish(analyzed); return; }
            std::lock_guard<std::mutex> l(h.mutex);
            h.steps.done[static_cast<usize>(hyper::Step::Stable)] = true;
            h.unitsDone = rq.frames;
        }
        const u64 stabUnits = recipe.stabilize ? static_cast<u64>(std::llround(static_cast<f64>(src.info.durationUs) * src.info.fps / 1e6)) : 0;

        // --- As passagens -------------------------------------------------------------
        std::vector<std::string> intermediates;
        u64 unitsBefore = stabUnits;
        Probed passSource = src;
        for (usize k = 0; k < plan.passes.size(); ++k) {
            if (cancelled()) { finish(Errc::Cancelled); return; }
            const hyper::PassPlan& pass = plan.passes[k];
            const std::string out = pass.final ? output : dir + "/aurea-hyper-" + keyHex + "-pass" + std::to_string(k) + ".mp4";
            if (!pass.final) intermediates.push_back(out);
            {
                std::lock_guard<std::mutex> l(h.mutex);
                h.steps.pass = static_cast<u32>(k);
            }
            const hyper::Step shown = pass_step(pass);
            // Passagem intermediária já pronta (o app morreu depois dela).
            const std::string doneLine = "pass " + std::to_string(k) + " ok\n";
            if (!pass.final && manifestText.find(doneLine) != std::string::npos && fileio::exists(out)) {
                Probed done;
                if (probe_source(config_.mediaFactory, out, done).ok()) {
                    AUREA_LOG_INFO("hyper: passagem %u pronta de uma sessao anterior", static_cast<u32>(k));
                    passSource = done;
                    unitsBefore += pass.frames;
                    std::lock_guard<std::mutex> l(h.mutex);
                    h.unitsDone = unitsBefore;
                    continue;
                }
            }
            // O projeto temporário desta passagem.
            std::vector<u8> bytes;
            CompositionId comp{};
            PassProject opt;
            opt.clean = pass.renderClean;
            opt.denoise = pass.renderDenoise;
            opt.upscale = pass.renderUpscale;
            opt.motion = pass.renderMotion;
            opt.stab = pass.renderStable ? &stab : nullptr;
            if (const Status s = build_project(effectRegistry_, pass, recipe, passSource, opt, bytes, comp); !s.ok()) { finish(s); return; }
            HyperExportOverride ov;
            ov.projectBytes = std::move(bytes);
            ov.composition = comp;
            ov.compFps = pass.fps;
            ov.graph = pass.graph;
            ov.graphRecipe = recipe;
            if (std::any_of(pass.graph.begin(), pass.graph.end(), [](const hyper::StageDescriptor* d) { return d->kind == hyper::StageKind::Upscale; }))
                ov.graphRecipe.upscale = static_cast<u64>(std::min(pass.renderW, pass.renderH)) * 2 >= std::min(pass.outW, pass.outH) ? 2u : 4u;
            ov.profile = hyper_cached_profile();
            ov.outputWidth = pass.graph.empty() ? 0 : pass.outW;
            ov.outputHeight = pass.graph.empty() ? 0 : pass.outH;
            if (pass.graph_motion()) {
                ov.motionSourceFps = passSource.info.fps;
                ov.motionSlow = pass.slowMotion;
            }
            ExportSettings es;
            es.width = pass.renderW;
            es.height = std::min(pass.renderW, pass.renderH);
            es.fps = pass.fps;
            es.videoCodec = ExportCodec::H264;
            es.quality = 2;
            es.keyframeIntervalFrames = 0;
            if (!pass.final) {
                // Intermediário: taxa alta (uma geração de codec a mais).
                const f64 mbps = static_cast<f64>(pass.outW) * pass.outH * pass.fps * 0.22 / 1e6;
                es.videoBitrateMbps = static_cast<u32>(std::clamp(mbps, 12.0, 120.0));
            }
            set_step(shown, 0.0f, nullptr);
            Status started = start_export_checked(es, out.c_str(), nullptr, &ov);
            if (!started.ok()) {
                AUREA_LOG_ERROR("hyper: passagem %u nao iniciou (%d)", static_cast<u32>(k), started.raw());
                finish(cancelled() ? Status{Errc::Cancelled} : started);
                return;
            }
            // Acompanha até terminar (o cancelamento vai pelo cancel_export).
            ExportProgress last{};
            for (;;) {
                if (cancelled()) (void)cancel_export();
                const ExportProgress p = export_progress();
                last = p;
                {
                    std::lock_guard<std::mutex> l(h.mutex);
                    h.unitsDone = unitsBefore + std::min<u64>(p.framesDone, pass.frames);
                    if (p.fps > 0.0f) h.lastFps = p.fps;
                    h.lastFlags = p.flags;
                    h.steps.stepProgress = p.framesTotal ? static_cast<f32>(p.framesDone) / p.framesTotal : 0.0f;
                    h.steps.progress = h.unitsTotal ? static_cast<f32>(static_cast<f64>(h.unitsDone) / h.unitsTotal) : 0.0f;
                    std::snprintf(h.exportMessage, sizeof(h.exportMessage), "%s", p.message);
                }
                set_step(shown, p.framesTotal ? static_cast<f32>(p.framesDone) / p.framesTotal : 0.0f, p.message);
                if (!p.running && p.finished) break;
                std::this_thread::sleep_for(20ms);
            }
            {
                std::lock_guard<std::mutex> l(h.mutex);
                h.lastFailure = last.failure;
            }
            if (last.result != Errc::Ok) {
                finish(cancelled() ? Status{Errc::Cancelled} : Status{last.result, "uma passagem do Hyper falhou"});
                return;
            }
            unitsBefore += pass.frames;
            {
                std::lock_guard<std::mutex> l(h.mutex);
                h.unitsDone = unitsBefore;
                auto mark = [&](hyper::Step s) { h.steps.done[static_cast<usize>(s)] = true; };
                if (pass.renderClean || std::any_of(pass.graph.begin(), pass.graph.end(),
                        [](const hyper::StageDescriptor* d) { return d->kind == hyper::StageKind::Clean; })) mark(hyper::Step::Clean);
                if (pass.renderUpscale || std::any_of(pass.graph.begin(), pass.graph.end(),
                        [](const hyper::StageDescriptor* d) { return d->kind == hyper::StageKind::Upscale; })) mark(hyper::Step::Upscale);
                if (std::any_of(pass.graph.begin(), pass.graph.end(),
                        [](const hyper::StageDescriptor* d) { return d->kind == hyper::StageKind::Face; })) mark(hyper::Step::Face);
                if (pass.renderMotion) mark(hyper::Step::Motion);
                if (pass.final) mark(hyper::Step::Encode);
            }
            if (!pass.final) {
                // Pronta: registra (o mesmo pedido continua daqui) e vira a fonte.
                std::string text = manifestText.rfind("aurea-hyper-job 1\n", 0) == 0 ? manifestText : std::string("aurea-hyper-job 1\n");
                for (usize j = 0; j <= k; ++j) {
                    const std::string line = "pass " + std::to_string(j) + " ok\n";
                    if (text.find(line) == std::string::npos) text += line;
                }
                (void)fileio::write_atomic(manifestPath, text.data(), text.size());
                Probed next;
                if (const Status s = probe_source(config_.mediaFactory, out, next); !s.ok()) { finish(s); return; }
                passSource = next;
            }
        }
        // Pronto: os intermediários e o manifesto saem.
        for (const std::string& f : intermediates) {
            std::error_code ec;
            std::filesystem::remove(fs_path(f), ec);
        }
        {
            std::error_code ec;
            std::filesystem::remove(fs_path(manifestPath), ec);
        }
        finish(OkStatus);
    } catch (...) {
        finish(Errc::OutOfMemory);
    }
}

} // namespace aurea
