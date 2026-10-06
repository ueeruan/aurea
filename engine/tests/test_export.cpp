// =============================================================================
//  Export (Fase 8F): pipeline sobreposto, equivalência de saída e benchmarks.
//
//  Testes de sempre (rápidos, GPU real do host):
//    - o pipeline com quadros em voo entrega OS MESMOS BYTES que o serial,
//      na ordem, sem perder nem repetir quadro, com o áudio amostra-exato;
//    - cancelar no meio responde rápido e solta tudo (sink abortado).
//
//  Benchmarks (lentos) só com AUREA_BENCH_EXPORT=1:
//      aurea_tests.exe ExportBench
//  Uma linha por cenário: quadros/s do pipeline inteiro e ms por estágio. O
//  "encoder" do host é um stub que copia os planos (o mesmo trabalho que o
//  MediaCodecExport faz para o buffer de entrada do codec); o custo dele sai
//  na coluna própria. Os modelos 3D vêm de AUREA_BENCH_GLTF (pasta com
//  DamagedHelmet.glb e Fox.glb) ou de tests/data/gltf.
// =============================================================================
#include "TestFramework.hpp"

#if defined(AUREA_TEST_VULKAN)

#include "SyntheticVideo.hpp"
#include "MockBackend.hpp"
#if defined(AUREA_TEST_GLES)
#include "GlesBackend.hpp"
namespace aurea { namespace vk = gles; }
#else
#include "VulkanBackend.hpp"
#endif

#include "aurea/Engine.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/export/BitratePolicy.hpp"
#include "aurea/export/ExportWatchdog.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextTransform.hpp"
#include "aurea/scene3d/Text3D.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace aurea;
using namespace aurea::test;

namespace {

// -----------------------------------------------------------------------------
// Sink de medição: guarda o hash de cada quadro (e, se pedido, os bytes), mede
// o custo da cópia para o "buffer de entrada" e confere pts e áudio.
// -----------------------------------------------------------------------------
struct BenchCapture {
    VideoStreamConfig video{};
    bool hasAudio = false;
    AudioStreamConfig audio{};
    bool opened = false, finished = false, aborted = false;
    bool keepFrames = false;
    u32 encodeUs = 0;                    ///< latência extra simulada do encoder
    u32 openDelayUs = 0;
    std::atomic<bool>* openEntered = nullptr;
    std::atomic<bool>* writeEntered = nullptr;
    std::atomic<bool>* allowWrite = nullptr;
    Status writeFailure{};              ///< fault injection at the platform boundary
    Status openFailure{};
    Status finishFailure{};
    bool invalidateDiagnosticOnDestroy = false;
    char platformDiagnostic[192]{};
    std::vector<u64> hashes;             ///< FNV-1a de Y+CbCr por quadro
    std::vector<std::vector<u8>> frames; ///< Y + CbCr (keepFrames)
    std::vector<i64> pts;
    i64 audioFrames = 0;                 ///< amostras por canal recebidas
    i64 lastAudioPts = -1;
    u64 audioHash = 1469598103934665603ull;
    bool ptsMonotonic = true;
    bool audioContiguous = true;
    std::vector<u8> input;               ///< "buffer de entrada do codec"
    ExportSink::Acceleration accel = ExportSink::Acceleration::Unknown;   ///< o que o sink diz ter aberto
    // Watchdog (export/ExportWatchdog.hpp): o encoder do aparelho preso DENTRO
    // da plataforma — a chamada não volta e ninguém bate o coração.
    i64 hangWriteAt = -1;                ///< n-ésima chamada de write_video que trava
    bool hangFinish = false;             ///< o finish trava
    std::atomic<bool>* hangEntered = nullptr;
    std::atomic<bool>* hangRelease = nullptr;   ///< solta a chamada presa (fim do teste)
    std::atomic<bool>* hangReturned = nullptr;
    u32 beatingWriteMs = 0;              ///< a 1ª escrita demora isto BATENDO (encoder lento, vivo)
    std::atomic<u64>* beat = nullptr;    ///< o que o motor entregou em set_heartbeat
    u32 writes = 0;
};

/// FNV-1a em palavras de 64 bits (o de byte a byte custaria ~3 ms por quadro
/// 1080p e entraria na coluna do "encoder"). Resto final byte a byte.
u64 fnv(u64 h, const u8* p, usize n) {
    usize i = 0;
    for (; i + 8 <= n; i += 8) {
        u64 w;
        std::memcpy(&w, p + i, 8);
        h ^= w;
        h *= 1099511628211ull;
    }
    for (; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

class BenchSink final : public ExportSink {
public:
    explicit BenchSink(BenchCapture* c) : c_(c) {}
    void set_heartbeat(std::atomic<u64>* beatNs) noexcept override { c_->beat = beatNs; }
    /// Preso na "plataforma": não bate, só volta quando o teste soltar. Ao
    /// voltar não toca em mais nada da captura (o motor já desistiu dele).
    Status hang() noexcept {
        if (c_->hangEntered) c_->hangEntered->store(true, std::memory_order_release);
        std::atomic<bool>* release = c_->hangRelease;
        std::atomic<bool>* returned = c_->hangReturned;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!(release && release->load(std::memory_order_acquire)) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (returned) returned->store(true, std::memory_order_release);
        return Errc::Timeout;
    }
    ~BenchSink() override {
        // Stable test storage models the end of a platform-owned diagnostic
        // lifetime without making the regression itself read freed memory.
        if (c_->invalidateDiagnosticOnDestroy)
            std::snprintf(c_->platformDiagnostic, sizeof(c_->platformDiagnostic), "diagnostico expirado");
    }
    Status open(const char*, const VideoStreamConfig& v, const AudioStreamConfig* a) noexcept override {
        if (c_->openEntered) c_->openEntered->store(true, std::memory_order_release);
        if (c_->openDelayUs) std::this_thread::sleep_for(std::chrono::microseconds(c_->openDelayUs));
        if (!c_->openFailure.ok()) return c_->openFailure;
        c_->video = v;
        c_->hasAudio = a != nullptr;
        if (a) c_->audio = *a;
        c_->opened = true;
        c_->input.resize(static_cast<usize>(v.width) * v.height * 3 / 2);
        return OkStatus;
    }
    Status write_video(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 ptsUs) noexcept override {
        if (c_->writeEntered) c_->writeEntered->store(true, std::memory_order_release);
        if (c_->allowWrite) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!c_->allowWrite->load(std::memory_order_acquire)) {
                if (std::chrono::steady_clock::now() >= deadline) return Errc::Timeout;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (!c_->writeFailure.ok()) return c_->writeFailure;
        const u32 call = c_->writes++;
        if (static_cast<i64>(call) == c_->hangWriteAt) return hang();
        if (call == 0 && c_->beatingWriteMs) {
            // Lento mas vivo: o laço do encoder volta da plataforma e bate.
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(c_->beatingWriteMs);
            while (std::chrono::steady_clock::now() < until) {
                if (c_->beat) c_->beat->store(monotonic_ns(), std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        const u32 w = c_->video.width, h = c_->video.height;
        // O trabalho do MediaCodecExport: planos → buffer de entrada do codec.
        u8* dst = c_->input.data();
        for (u32 r = 0; r < h; ++r) std::memcpy(dst + static_cast<usize>(r) * w, y + static_cast<usize>(r) * yStride, w);
        u8* dc = dst + static_cast<usize>(w) * h;
        for (u32 r = 0; r < h / 2; ++r) std::memcpy(dc + static_cast<usize>(r) * w, uv + static_cast<usize>(r) * uvStride, w);
        c_->hashes.push_back(fnv(1469598103934665603ull, dst, c_->input.size()));
        if (c_->keepFrames) c_->frames.push_back(c_->input);
        if (!c_->pts.empty() && ptsUs <= c_->pts.back()) c_->ptsMonotonic = false;
        c_->pts.push_back(ptsUs);
        if (c_->encodeUs) std::this_thread::sleep_for(std::chrono::microseconds(c_->encodeUs));
        return OkStatus;
    }
    Status write_audio(const i16* pcm, u32 frames, i64 ptsUs) noexcept override {
        const i64 expect = audio::sample_to_ns(c_->audioFrames) / 1000;
        if (ptsUs != expect) c_->audioContiguous = false;
        c_->audioFrames += frames;
        c_->lastAudioPts = ptsUs;
        c_->audioHash = fnv(c_->audioHash, reinterpret_cast<const u8*>(pcm),
                            static_cast<usize>(frames) * c_->audio.channels * sizeof(i16));
        return OkStatus;
    }
    Status finish() noexcept override {
        if (c_->hangFinish) return hang();
        c_->finished = true;
        return c_->finishFailure;
    }
    void abort() noexcept override { c_->aborted = true; }
    EncoderInfo encoder_info() const noexcept override {
        EncoderInfo i;
        std::snprintf(i.name, sizeof(i.name), "%s", "stub-do-host");
        i.acceleration = c_->accel;
        return i;
    }
private:
    BenchCapture* c_;
};

std::unique_ptr<ExportSink> make_bench_sink(void* user) {
    return std::make_unique<BenchSink>(static_cast<BenchCapture*>(user));
}

// -----------------------------------------------------------------------------
// Memória do processo (Windows): bytes privados e handles.
// -----------------------------------------------------------------------------
struct ProcMem { u64 privateBytes = 0; u64 workingSet = 0; u32 handles = 0; };
ProcMem proc_mem() {
    ProcMem m;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.privateBytes = pmc.PrivateUsage;
        m.workingSet = pmc.WorkingSetSize;
    }
    DWORD h = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &h)) m.handles = h;
#endif
    return m;
}

// -----------------------------------------------------------------------------
// Cenários
// -----------------------------------------------------------------------------
struct Rig {
    SyntheticFactory factory;
    BenchCapture cap;
    Engine e;
    bool ok = false;
    Rig(const SyntheticConfig& cfg, f64 compFps, i64 frames, u32 depth,
        VideoSourceFactory* mediaFactory = nullptr, GPUBackend* backend = nullptr, u32 workerHangMs = 0) : factory(cfg) {
        EngineConfig ec;
        ec.exportWorkerHangMs = workerHangMs;
        ec.backend = backend ? backend : new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.mediaFactory = mediaFactory ? mediaFactory : &factory;
        ec.exportSinkFactory = &make_bench_sink;
        ec.exportSinkContext = &cap;
        ec.exportPipelineDepth = depth;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        // Tabela de codecs como a de um aparelho com decode/encode 4K de
        // hardware (sem ela o motor fica no teto conservador de 1080p).
        ec.hasPlatformInfo = true;
        ec.platformInfo.totalMemoryBytes = 8ull << 30;
        ec.platformInfo.availableMemoryBytes = 4ull << 30;
        CodecCapability c4k;
        c4k.supported = true;
        c4k.hardwareAccelerated = true;
        c4k.maxWidth = 3840;
        c4k.maxHeight = 2160;
        ec.platformInfo.decoders[0] = c4k;
        ec.platformInfo.set_decoder_tag(0x61766331u, 0);
        ec.platformInfo.decoderCount = 1;
        ec.platformInfo.encoders[0] = c4k;
        ec.platformInfo.set_encoder_tag(0x61766331u, 0);
        ec.platformInfo.encoderCount = 1;
        if (!e.initialize(ec).ok()) return;
        if (!e.new_project(cfg.width, cfg.height, compFps, nullptr).ok()) return;
        VideoImport vi;
        vi.sourcePath = "sintetico";
        vi.displayName = "sintetico";
        if (!e.import_video(vi).ok()) return;
        Command fps;
        fps.type = CommandType::CompositionSetFps;
        fps.comp_fps.comp = e.project()->timeline().current();
        fps.comp_fps.fps = compFps;
        (void)e.apply_command(fps);
        Command d;
        d.type = CommandType::CompositionSetDuration;
        d.comp_duration.comp = e.project()->timeline().current();
        d.comp_duration.duration = FrameIndex{frames};
        ok = e.apply_command(d).ok();
    }
    ~Rig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    LayerId video_layer() {
        LayerId id{};
        comp()->layers().for_each([&](LayerId l, const Layer& layer) { if (layer.kind == LayerKind::Video) id = l; });
        return id;
    }
    EffectInstance* add_effect(LayerId layer, const char* key) {
        Command fx;
        fx.type = CommandType::EffectAdd;
        fx.effect_add.layer = layer;
        fx.effect_add.effectType = effect_type_id(key);
        fx.effect_add.index = kInvalidIndex;
        if (!e.apply_command(fx).ok()) return nullptr;
        Layer* l = comp()->layer(layer);
        return l && !l->effects.empty() ? &l->effects.back() : nullptr;
    }
};

// Model the finite image leases of a platform decoder, including CPU-plane
// fallback: uploading pixels must not keep those leases until a later GPU frame.
struct ImageLeases {
    std::atomic<u32> live{0}, peak{0}, exhausted{0};
    u32 limit = 12;
};
class LeasedFrame final : public DecodedFrame {
public:
    LeasedFrame(FrameRef source, std::shared_ptr<ImageLeases> leases)
        : source_(std::move(source)), leases_(std::move(leases)) {
        const auto& f = *source_.get();
        ptsUs = f.ptsUs; durationUs = f.durationUs;
        width = f.width; height = f.height;
        cropLeft = f.cropLeft; cropTop = f.cropTop;
        visibleWidth = f.visibleWidth; visibleHeight = f.visibleHeight;
        rotation = f.rotation; format = f.format; color = f.color;
        planeCount = f.planeCount; bufferId = f.bufferId;
        for (u32 i = 0; i < 3; ++i) { planes[i] = f.planes[i]; strides[i] = f.strides[i]; }
        const u32 live = ++leases_->live;
        u32 peak = leases_->peak.load();
        while (peak < live && !leases_->peak.compare_exchange_weak(peak, live)) {}
    }
    ~LeasedFrame() override { --leases_->live; }
private:
    FrameRef source_;
    std::shared_ptr<ImageLeases> leases_;
};
class LeasedDecoder final : public VideoDecoderBackend {
public:
    LeasedDecoder(const SyntheticConfig& cfg, std::shared_ptr<ImageLeases> leases)
        : decoder_(cfg), leases_(std::move(leases)) {}
    const VideoStreamInfo& info() const noexcept override { return decoder_.info(); }
    Status seek_to_keyframe(i64 us) noexcept override { return decoder_.seek_to_keyframe(us); }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        if (leases_->live.load() >= leases_->limit) {
            ++leases_->exhausted;
            return Errc::BudgetExceeded;
        }
        FrameRef frame;
        const Status status = decoder_.next_frame(from, frame, pts, eos);
        if (frame) out = FrameRef::adopt(new LeasedFrame(std::move(frame), leases_));
        return status;
    }
    u32 max_live_frames() const noexcept override { return leases_->limit; }
    i64 keyframe_interval_us() const noexcept override { return decoder_.keyframe_interval_us(); }
private:
    SyntheticDecoder decoder_;
    std::shared_ptr<ImageLeases> leases_;
};
class LeasedFactory final : public VideoSourceFactory {
public:
    explicit LeasedFactory(const SyntheticConfig& cfg) : cfg_(cfg) {}
    bool probe(const char* path, MediaProbe& out) override { return SyntheticFactory(cfg_).probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<LeasedDecoder>(cfg_, leases);
    }
    std::shared_ptr<ImageLeases> leases = std::make_shared<ImageLeases>();
private:
    SyntheticConfig cfg_;
};

/// 5 camadas + desfoque + brilho + cor + texto + desfoque de movimento.
void build_effects_scene(Rig& r) {
    const LayerId video = r.video_layer();
    if (EffectInstance* b = r.add_effect(video, effect_keys::kGaussianBlur)) b->params[0].constant.v[0] = 12.0f;
    if (EffectInstance* x = r.add_effect(video, effect_keys::kExposure)) x->params[0].constant.v[0] = 0.4f;
    (void)r.add_effect(video, effect_keys::kCurves);
    const u32 W = r.comp()->width(), H = r.comp()->height();
    for (int k = 0; k < 2; ++k) {
        auto s = r.e.add_shape(10);
        if (!s.ok()) continue;
        const LayerId id = LayerId::unpack(*s);
        Layer* l = r.comp()->layer(id);
        Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
        px.set(l->local_time(FrameIndex{0}), W * 0.2f);
        px.set(l->local_time(FrameIndex{60}), W * 0.8f);
        Track& py = l->tracks.get_or_create(TrackProperty::PositionY);
        py.set(l->local_time(FrameIndex{0}), H * (0.3f + 0.4f * k));
        (void)r.e.set_motion_blur(*s, true);
        if (EffectInstance* g = r.add_effect(id, effect_keys::kGlow)) g->params[0].constant.v[0] = 40.0f;
        if (k == 1) {
            if (EffectInstance* sat = r.add_effect(id, effect_keys::kSaturation)) sat->params[0].constant.v[0] = 50.0f;
        }
    }
    std::vector<u8> rgba(512u * 512u * 4u);
    for (u32 y = 0; y < 512; ++y)
        for (u32 x = 0; x < 512; ++x) {
            u8* p = &rgba[(static_cast<usize>(y) * 512 + x) * 4];
            p[0] = static_cast<u8>(x / 2); p[1] = static_cast<u8>(y / 2); p[2] = 160; p[3] = 200;
        }
    if (auto img = r.e.import_image(rgba.data(), 512, 512, "degrade"); img.ok()) {
        (void)r.add_effect(LayerId::unpack(*img), effect_keys::kGlow);
    }
    (void)r.e.add_text("AUREA 8F EXPORT");
}

std::string gltf_dir() {
    if (const char* d = std::getenv("AUREA_BENCH_GLTF")) return d;
    return std::string(AUREA_TEST_DATA_DIR) + "/gltf";
}
bool exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

/// PBR (DamagedHelmet) + animação (Fox) + sombras (padrão dos modelos) + partículas.
bool build_3d_scene(Rig& r) {
    const std::string helmet = gltf_dir() + "/DamagedHelmet.glb", fox = gltf_dir() + "/Fox.glb";
    if (!exists(helmet) || !exists(fox)) return false;
    ModelImport a;
    a.path = helmet;
    ModelImport b;
    b.path = fox;
    if (!r.e.import_model(a).ok() || !r.e.import_model(b).ok()) return false;
    (void)r.e.add_particles(0);
    (void)r.e.add_particles(1);
    return true;
}

struct Outcome {
    f64 seconds = 0.0;
    Engine::ExportProgress p{};
    bool finished = false;
};

Outcome run_export(Rig& r, u32 shortSide, f64 fps, bool dither = true, int timeoutS = 600, u32 aiScale = 0) {
    Outcome o;
    ExportSettings s;
    s.height = shortSide;
    s.fps = fps;
    s.dither = dither;
    s.aiUpscale = aiScale;
    const auto t0 = std::chrono::steady_clock::now();
    if (!r.e.start_export(s, "nao-usado.mp4").ok()) return o;
    for (int i = 0; i < timeoutS * 1000; ++i) {
        if (r.e.export_progress().finished) { o.finished = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    o.seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    o.p = r.e.export_progress();
    return o;
}

u64 combined_hash(const BenchCapture& c) {
    u64 h = 1469598103934665603ull;
    for (u64 x : c.hashes) h = fnv(h, reinterpret_cast<const u8*>(&x), sizeof(x));
    return h;
}

void print_line(const char* name, const Rig& r, const Outcome& o) {
    const u32 n = static_cast<u32>(r.cap.hashes.size());
    std::printf("\n    %-22s %4ux%-4u %5.1f fps | %6.2f q/s | decode %6.2f render %6.2f readback %6.2f encoder %6.2f audio %5.2f ms/q | em voo %u | hash %016llx",
                name, r.cap.video.width, r.cap.video.height, r.cap.video.fps,
                o.seconds > 0 ? n / o.seconds : 0.0, o.p.decodeWaitMs, o.p.renderMs, o.p.readbackMs,
                o.p.encodeMs, o.p.audioMs, o.p.pipelineDepth, static_cast<unsigned long long>(combined_hash(r.cap)));
}

bool bench_enabled() {
    const char* v = std::getenv("AUREA_BENCH_EXPORT");
    return v && *v && *v != '0';
}

/// AUREA_BENCH_DEPTH=1 mede o mesmo pipeline em serial (separa o ganho da
/// sobreposição do ganho de não ler a GPU de forma síncrona). 0 = automático.
u32 bench_depth() {
    const char* v = std::getenv("AUREA_BENCH_DEPTH");
    return v ? static_cast<u32>(std::atoi(v)) : 0u;
}

bool gpu_ok() {
    static const bool ok = [] {
        vk::Backend b;
        BackendConfig c;
        c.enableValidation = false;
        const bool r = b.initialize(c).ok();
        b.shutdown();
        return r;
    }();
    return ok;
}

} // namespace

// =============================================================================
// Equivalência: pipeline × serial, bytes idênticos
// =============================================================================
AUREA_TEST(Export, TextAnimatorColorsReachExportFramesAndRemainStableAfterReopen) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg; cfg.width = 320; cfg.height = 180;
    Rig r(cfg, 30, 12, 2); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    auto id = r.e.add_text("AUREA"); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* layer = r.comp()->layer(LayerId::unpack(*id));
    layer->end = FrameIndex{12};
    AUREA_CHECK_EQ(r.e.add_text_animator(*id, kTextPropFill), 0);
    for (u32 p : {text::kFillR, text::kFillG, text::kFillB}) {
        auto& tr = layer->tracks.get_or_create(TrackProperty::TextAnimParam, 0, p);
        (void)tr.set(FrameIndex{0}, p == text::kFillR ? 1.f : 0.f, Interpolation::Linear);
        (void)tr.set(FrameIndex{11}, p == text::kFillB ? 1.f : 0.f, Interpolation::Linear);
    }
    Command duration; duration.type = CommandType::CompositionSetDuration;
    duration.comp_duration.comp = r.e.project()->timeline().current(); duration.comp_duration.duration = FrameIndex{12};
    AUREA_CHECK(r.e.apply_command(duration).ok());
    r.cap.keepFrames = true;
    auto result = run_export(r, 180, 30, false, 30);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), 12u);
    if (r.cap.hashes.size() != 12) return;
    AUREA_CHECK(r.cap.hashes.front() != r.cap.hashes.back());
    AUREA_CHECK(r.cap.hashes[5] != r.cap.hashes.front());
    AUREA_CHECK(r.cap.ptsMonotonic);
    const auto frames = r.cap.frames;
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_text_export_colors.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok()); AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    r.cap = BenchCapture{}; r.cap.keepFrames = true;
    result = run_export(r, 180, 30, false, 30);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK(frames == r.cap.frames);
    std::remove(path.c_str());
}

AUREA_TEST(Export, MaskScalarAnimationSurvivesProjectReopenInExportedFrames) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg; cfg.width = 320; cfg.height = 180;
    Rig r(cfg, 30, 12, 2); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    auto id = r.e.add_text("AUREA"); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* layer = r.comp()->layer(LayerId::unpack(*id));
    layer->end = FrameIndex{12};
    const f32 pts[] = {-1000,-1000,0,0,0,0, 1000,-1000,0,0,0,0, 1000,1000,0,0,0,0, -1000,1000,0,0,0,0};
    const i32 mask = r.e.add_mask(*id, pts, 4, true); AUREA_CHECK(mask >= 0);
    if (mask < 0) return;
    auto& opacity = layer->tracks.get_or_create(TrackProperty::MaskParam, static_cast<u32>(mask), 2);
    (void)opacity.set(FrameIndex{0}, 1.f, Interpolation::Linear);
    (void)opacity.set(FrameIndex{11}, 0.f, Interpolation::Linear);
    Command duration; duration.type = CommandType::CompositionSetDuration;
    duration.comp_duration.comp = r.e.project()->timeline().current(); duration.comp_duration.duration = FrameIndex{12};
    AUREA_CHECK(r.e.apply_command(duration).ok());
    r.cap.keepFrames = true;
    auto result = run_export(r, 180, 30, false, 30);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), 12u);
    if (r.cap.hashes.size() != 12) return;
    AUREA_CHECK(r.cap.hashes.front() != r.cap.hashes.back());
    AUREA_CHECK(r.cap.hashes[5] != r.cap.hashes.front());
    AUREA_CHECK(r.cap.ptsMonotonic);
    const auto frames = r.cap.frames;
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_mask_export.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok()); AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    r.cap = BenchCapture{}; r.cap.keepFrames = true;
    result = run_export(r, 180, 30, false, 30);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK(frames == r.cap.frames);
    std::remove(path.c_str());
}

AUREA_TEST(Export, NeuralUpscaleKeepsOutputTimingAudioAndDimensions) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 64; cfg.height = 36;
    cfg.pattern = SyntheticPattern::MovingSquare;
    cfg.audioRate = 44100; cfg.audioSeconds = 1;
    for (const u32 scale : {2u, 4u}) {
        std::vector<std::vector<u8>> serial;
        u64 audioHash = 0;
        for (const u32 depth : {1u, 3u}) {
            Rig r(cfg, 29.97, 3, depth);
            AUREA_CHECK(r.ok); if (!r.ok) return;
            r.cap.keepFrames = true;
            const Outcome o = run_export(r, 36 * scale, 0, false, 60, scale);
            AUREA_CHECK(o.finished); AUREA_CHECK_EQ(o.p.result, Errc::Ok);
            AUREA_CHECK(r.cap.finished && !r.cap.aborted);
            AUREA_CHECK_EQ(r.cap.video.width, 64u * scale);
            AUREA_CHECK_EQ(r.cap.video.height, 36u * scale);
            AUREA_CHECK_EQ(r.cap.frames.size(), static_cast<usize>(3));
            AUREA_CHECK(r.cap.ptsMonotonic && r.cap.audioContiguous && r.cap.hasAudio);
            AUREA_CHECK_EQ(r.cap.audioFrames, audio::frame_to_sample(3, 29.97));
            for (usize i=0; i<r.cap.pts.size(); ++i)
                AUREA_CHECK_EQ(r.cap.pts[i], static_cast<i64>(std::llround(i * 1e6 / 29.97)));
            if (depth == 1) { serial = r.cap.frames; audioHash = r.cap.audioHash; }
            else { AUREA_CHECK(r.cap.frames == serial); AUREA_CHECK_EQ(r.cap.audioHash, audioHash); }
        }
    }
}

AUREA_TEST(Export, PipelinedOutputIsByteIdenticalToSerial) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.pattern = SyntheticPattern::MovingSquare;
    cfg.audioRate = 44100;
    cfg.audioSeconds = 5.0;
    u64 hashes[2]{};
    i64 audio[2]{};
    u64 audioHash[2]{};
    std::vector<std::vector<u8>> frames[2];
    const u32 depths[2] = {1, 3};
    for (int k = 0; k < 2; ++k) {
        Rig r(cfg, 30.0, 40, depths[k]);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        build_effects_scene(r);
        r.cap.keepFrames = true;
        const Outcome o = run_export(r, 180, 0.0, true, 120);
        AUREA_CHECK(o.finished);
        AUREA_CHECK_EQ(o.p.result, Errc::Ok);
        AUREA_CHECK(r.cap.finished && !r.cap.aborted);
        AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(40));
        AUREA_CHECK(r.cap.ptsMonotonic);
        AUREA_CHECK(r.cap.audioContiguous);
        for (usize i = 0; i < r.cap.pts.size(); ++i) {
            AUREA_CHECK_EQ(r.cap.pts[i], static_cast<i64>(std::llround(static_cast<f64>(i) * 1e6 / 30.0)));
        }
        hashes[k] = combined_hash(r.cap);
        audio[k] = r.cap.audioFrames;
        audioHash[k] = r.cap.audioHash;
        frames[k] = std::move(r.cap.frames);
        if (k == 1) AUREA_CHECK(o.p.pipelineDepth > 1);
    }
    // 40 quadros a 30 fps = 64000 amostras exatas a 48 kHz, nos dois.
    AUREA_CHECK_EQ(audio[0], static_cast<i64>(64000));
    AUREA_CHECK_EQ(audio[1], static_cast<i64>(64000));
    AUREA_CHECK_EQ(audioHash[0], audioHash[1]);
    AUREA_CHECK_EQ(hashes[0], hashes[1]);
    // Onde diverge, se divergir (diferença máxima por byte).
    int maxDiff = 0;
    for (usize f = 0; f < std::min(frames[0].size(), frames[1].size()); ++f)
        for (usize i = 0; i < std::min(frames[0][f].size(), frames[1][f].size()); ++i)
            maxDiff = std::max(maxDiff, std::abs(frames[0][f][i] - frames[1][f][i]));
    AUREA_CHECK_EQ(maxDiff, 0);
}

/// Desfoque de movimento 3D: 32 cenas por quadro, cada uma com as suas juntas e
/// instâncias em buffers de upload. Com o anel fixo de 8 as cenas de um quadro
/// (e as do quadro seguinte, em voo) sobrescreviam poses que a GPU ainda ia
/// ler: o arquivo mudava a cada export. Serial e em voo têm de dar o mesmo byte.
AUREA_TEST(Export, MotionBlur3DPipelinedIsByteIdenticalToSerial) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.frameCount = 20;
    cfg.pattern = SyntheticPattern::FrameGray;
    u64 hashes[2]{};
    const u32 depths[2] = {1, 3};
    for (int k = 0; k < 2; ++k) {
        Rig r(cfg, 30.0, 8, depths[k]);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        if (!build_3d_scene(r)) { std::printf("(sem modelos glTF: pulado) "); return; }
        (void)r.e.set_composition_motion_blur(true);
        const OrderedIds<LayerId>& order = r.comp()->order();
        for (u32 i = 0; i < order.size(); ++i) (void)r.e.set_motion_blur(order.at(i).pack(), true);
        const Outcome o = run_export(r, 180, 0.0, false, 120);
        AUREA_CHECK(o.finished);
        AUREA_CHECK_EQ(o.p.result, Errc::Ok);
        AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(8));
        hashes[k] = combined_hash(r.cap);
        if (k == 1) AUREA_CHECK(o.p.pipelineDepth > 1);
    }
    AUREA_CHECK_EQ(hashes[0], hashes[1]);
}

AUREA_TEST(Regression2134Gpu, FullHdMotionBlurExportsVideoAnimatedTextAnd3D) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    for (u32 mode = 0; mode < 3; ++mode) {
        const i64 frames = mode == 2 ? 12 : 60;
        SyntheticConfig cfg;
        cfg.width = 1920; cfg.height = 1080; cfg.frameCount = static_cast<u32>(frames);
        cfg.pattern = SyntheticPattern::FastSquare;
        Rig r(cfg, 30, frames, 3);
        AUREA_CHECK(r.ok); if (!r.ok) return;
        r.comp()->motion_blur().samples = 64;
        const u64 video = r.video_layer().pack();
        if (mode == 0) AUREA_CHECK(r.e.set_vector_blur(video, 1));
        u64 moving = video;
        if (mode == 1) {
            const auto text = r.e.add_text("AUREA MOTION BLUR");
            AUREA_CHECK(text.ok()); if (!text.ok()) return;
            moving = *text;
            AUREA_CHECK(r.e.add_text_animator(moving, kTextPropPosition) == 0);
            auto& tr = r.comp()->layer(LayerId::unpack(moving))->tracks.get_or_create(TrackProperty::TextAnimParam, 0, text::kPosX);
            tr.set(FrameIndex{0}, -150); tr.set(FrameIndex{frames - 1}, 150);
        } else if (mode == 2) {
            const auto shape = r.e.add_shape3d(0);
            AUREA_CHECK(shape.ok()); if (!shape.ok()) return;
            moving = *shape;
        }
        auto& position = r.comp()->layer(LayerId::unpack(moving))->tracks.get_or_create(TrackProperty::PositionX);
        position.set(FrameIndex{0}, 700); position.set(FrameIndex{frames - 1}, 1200);
        AUREA_CHECK(r.e.set_motion_blur(moving, true));
        AUREA_CHECK(r.e.set_composition_motion_blur(true));
        const auto result = run_export(r, 1080, 30, false, 180);
        AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
        if (!result.finished) return;
        AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(frames));
        AUREA_CHECK(r.cap.finished && r.cap.ptsMonotonic && !r.cap.aborted);
        AUREA_CHECK((result.p.flags & Engine::kExportFrameFallback) == 0);
        AUREA_CHECK_EQ(r.cap.video.width, 1920u);
        AUREA_CHECK_EQ(r.cap.video.height, 1080u);
        AUREA_CHECK(r.cap.hashes.front() != r.cap.hashes.back());
        std::printf("    mode=%u: %lld frames at 1080p/64 samples in %.2fs\n", mode,
            static_cast<long long>(frames), result.seconds);
    }
}

AUREA_TEST(Export, VectorAndTransformMotionBlurFinishEveryFrame) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 128; cfg.height = 72; cfg.frameCount = 24;
    cfg.pattern = SyntheticPattern::FastSquare;
    cfg.decodeCostUs = 2000;
    for (u32 playback : {0u, 1u, 2u}) for (u32 mode : {1u, 2u, 3u}) {
        LeasedFactory factory(cfg);
        factory.leases->limit = 6; // Android DriverGl's bounded pool for 4K input
        Rig r(cfg, 30, 24, 3, &factory);
        AUREA_CHECK(r.ok); if (!r.ok) return;
        const auto id = r.video_layer();
        auto* layer = r.comp()->layer(id);
        layer->reversed = playback == 1;
        if (playback == 2) {
            layer->timeRemapEnabled = true;
            layer->timeRemap.set(FrameIndex{0}, 0);
            layer->timeRemap.set(FrameIndex{12}, 20);
            layer->timeRemap.set(FrameIndex{23}, 0);
        }
        auto& position = layer->tracks.get_or_create(TrackProperty::PositionX);
        position.set(FrameIndex{0}, 40); position.set(FrameIndex{23}, 80);
        AUREA_CHECK(r.e.set_vector_blur(id.pack(), mode & 1u ? 1.f : 0.f));
        AUREA_CHECK(r.e.set_motion_blur(id.pack(), (mode & 2u) != 0));
        AUREA_CHECK(r.e.set_composition_motion_blur(true));
        const auto result = run_export(r, 72, 30, false, 20);
        AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
        if (!result.finished) return; // Rig teardown joins the worker before capture is destroyed.
        std::printf("    blur mode=%u playback=%u frames=%zu elapsed=%.2fs flags=%u\n", mode, playback, r.cap.hashes.size(), result.seconds, result.p.flags);
        AUREA_CHECK(r.cap.finished && !r.cap.aborted);
        AUREA_CHECK_EQ(r.cap.hashes.size(), usize{24});
        AUREA_CHECK_EQ(result.p.flags & Engine::kExportFrameFallback, 0u);
        AUREA_CHECK_EQ(factory.leases->exhausted.load(), 0u);
        if (mode & 1u) {
            u32 hits = 0, misses = 0;
            r.e.flow_cache_stats(hits, misses);
            AUREA_CHECK(misses > 0);
        }
    }
}

AUREA_TEST(Export, TemporalRgbReleasesFiniteDecoderImages) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 128; cfg.height = 72;
    cfg.pattern = SyntheticPattern::FrameGray;
    LeasedFactory factory(cfg);
    {
        Rig r(cfg, 30.0, 90, 3, &factory);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        auto* fx = r.add_effect(r.video_layer(), effect_keys::kTimeWarpRgb);
        AUREA_CHECK(fx != nullptr);
        if (!fx) return;
        fx->params[0].constant.v[0] = 3;
        fx->params[1].constant.v[0] = 0;
        fx->params[2].constant.v[0] = -3;
        fx->params[3].constant.v[0] = 0;
        fx->params[4].constant.v[0] = 100;
        const Outcome o = run_export(r, 72, 30, false, 30);
        AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
        AUREA_CHECK(r.cap.finished && !r.cap.aborted);
        AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(90));
        AUREA_CHECK(r.cap.ptsMonotonic);
        AUREA_CHECK_EQ(factory.leases->exhausted.load(), 0u);
        AUREA_CHECK(factory.leases->peak.load() <= 12u);
    }
    AUREA_CHECK_EQ(factory.leases->live.load(), 0u);
}

/// Never report a successful black/incomplete export if no frame of a visible
/// source can be decoded. Stop cleanly, remove the partial output and identify
/// the media failure, while still allowing approximate decoded frames below.
AUREA_TEST(Export, MissingVideoFramesAbortInsteadOfReportingBlankVideoAsSuccess) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    LeasedFactory factory(cfg);
    factory.leases->limit = 0;
    Rig r(cfg, 30.0, 3, 3, &factory);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const auto t0 = std::chrono::steady_clock::now();
    const Outcome o = run_export(r, 36, 30, false, 30);
    const f64 secs = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::DecodeFailed);
    AUREA_CHECK(!r.cap.finished && r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(0));
    AUREA_CHECK_EQ(o.p.failure, 5u); // ExportFailure::Media
    AUREA_CHECK(factory.leases->exhausted.load() > 0);
    std::printf("falha de midia tratada em %.1f s ", secs);
    AUREA_CHECK(secs < 7.0);
}

namespace {
/// Real decoder until `stallFromUs`, then DecodeFailed forever (corrupt tail,
/// vendor codec that stops mid-file).
class StallingDecoder final : public VideoDecoderBackend {
public:
    StallingDecoder(const SyntheticConfig& cfg, i64 stallFromUs) : decoder_(cfg), stallFromUs_(stallFromUs) {}
    const VideoStreamInfo& info() const noexcept override { return decoder_.info(); }
    Status seek_to_keyframe(i64 us) noexcept override { return decoder_.seek_to_keyframe(us); }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        FrameRef frame;
        const Status s = decoder_.next_frame(from, frame, pts, eos);
        if (s.ok() && pts >= stallFromUs_) return Status{Errc::DecodeFailed, "decoder parou (teste)"};
        out = std::move(frame);
        return s;
    }
    u32 max_live_frames() const noexcept override { return 12; }
    i64 keyframe_interval_us() const noexcept override { return decoder_.keyframe_interval_us(); }
private:
    SyntheticDecoder decoder_;
    i64 stallFromUs_;
};
class StallingFactory final : public VideoSourceFactory {
public:
    StallingFactory(const SyntheticConfig& cfg, i64 stallFromUs) : cfg_(cfg), stallFromUs_(stallFromUs) {}
    bool probe(const char* path, MediaProbe& out) override { return SyntheticFactory(cfg_).probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<StallingDecoder>(cfg_, stallFromUs_);
    }
private:
    SyntheticConfig cfg_;
    i64 stallFromUs_;
};

/// Platform decoder instances are finite (hardware codecs: often 4-16, fewer
/// at 4K). Opening past the limit fails exactly like a real device.
struct DecoderSlots {
    std::atomic<u32> live{0}, peak{0}, refused{0}, opened{0};
    u32 limit = 3;
};
class SlotDecoder final : public VideoDecoderBackend {
public:
    SlotDecoder(const SyntheticConfig& cfg, std::shared_ptr<DecoderSlots> slots)
        : decoder_(cfg), slots_(std::move(slots)) {
        const u32 live = ++slots_->live;
        u32 peak = slots_->peak.load();
        while (peak < live && !slots_->peak.compare_exchange_weak(peak, live)) {}
    }
    ~SlotDecoder() override { --slots_->live; }
    const VideoStreamInfo& info() const noexcept override { return decoder_.info(); }
    Status seek_to_keyframe(i64 us) noexcept override { return decoder_.seek_to_keyframe(us); }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        return decoder_.next_frame(from, out, pts, eos);
    }
    u32 max_live_frames() const noexcept override { return 12; }
    i64 keyframe_interval_us() const noexcept override { return decoder_.keyframe_interval_us(); }
private:
    SyntheticDecoder decoder_;
    std::shared_ptr<DecoderSlots> slots_;
};
class SlotFactory final : public VideoSourceFactory {
public:
    explicit SlotFactory(const SyntheticConfig& cfg) : cfg_(cfg) {}
    bool probe(const char* path, MediaProbe& out) override { return SyntheticFactory(cfg_).probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        if (slots->live.load() >= slots->limit) { ++slots->refused; return nullptr; }
        ++slots->opened;
        return std::make_unique<SlotDecoder>(cfg_, slots);
    }
    std::shared_ptr<DecoderSlots> slots = std::make_shared<DecoderSlots>();
private:
    SyntheticConfig cfg_;
};
} // namespace

/// The decoder dies in the middle of the clip: the export keeps the nearest
/// decoded frame for the rest instead of throwing the whole video away.
AUREA_TEST(Export, DecoderStallUsesNearestDecodedFrameAndFinishes) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 64; cfg.height = 36;
    cfg.pattern = SyntheticPattern::FrameGray;
    cfg.gop = 30;
    const i64 stallUs = static_cast<i64>(std::llround(5 * 1e6 / 30.0));   // frames 0..4 decode
    StallingFactory factory(cfg, stallUs);
    Rig r(cfg, 30.0, 7, 3, &factory);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    r.cap.keepFrames = true;
    const Outcome o = run_export(r, 36, 30, false, 30);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.frames.size(), static_cast<usize>(7));
    AUREA_CHECK((o.p.flags & Engine::kExportFrameFallback) != 0);
    if (r.cap.frames.size() == 7) {
        // Frames before the stall are distinct; after it, the last good one repeats.
        AUREA_CHECK(r.cap.frames[3] != r.cap.frames[4]);
        AUREA_CHECK(r.cap.frames[5] == r.cap.frames[4]);
        AUREA_CHECK(r.cap.frames[6] == r.cap.frames[4]);
    }
}

/// Many clips in sequence, device with just one decoder instance. The preview used to
/// be the only one retiring idle decoders, and it does not run during export:
/// every finished clip kept its codec until the end, the 4th clip could not
/// open and the export died with "quadros de video indisponiveis".
AUREA_TEST(Export, ManyClipsRetireDecodersDuringExport) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 64; cfg.height = 36;
    cfg.pattern = SyntheticPattern::FrameGray;
    cfg.frameCount = 60;
    SlotFactory factory(cfg);
    factory.slots->limit = 1;
    constexpr i64 kClips = 12, kClipFrames = 10;
    Rig r(cfg, 30.0, kClips * kClipFrames, 3, &factory);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    std::vector<LayerId> clips{r.video_layer()};
    for (i64 k = 1; k < kClips; ++k) {
        VideoImport vi;
        vi.sourcePath = "sintetico";
        vi.displayName = "sintetico";
        const auto id = r.e.import_video(vi);
        AUREA_CHECK(id.ok());
        if (!id.ok()) return;
        clips.push_back(LayerId::unpack(*id));
    }
    for (i64 k = 0; k < kClips; ++k) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        cmd.layer_range.layer = clips[static_cast<usize>(k)];
        cmd.layer_range.start = FrameIndex{k * kClipFrames};
        cmd.layer_range.end = FrameIndex{(k + 1) * kClipFrames};
        cmd.layer_range.offset = FrameIndex{0};
        cmd.layer_range.setOffset = true;
        AUREA_CHECK(r.e.apply_command(cmd).ok());
    }
    // The composition may have grown with the imports: pin it back.
    Command d;
    d.type = CommandType::CompositionSetDuration;
    d.comp_duration.comp = r.e.project()->timeline().current();
    d.comp_duration.duration = FrameIndex{kClips * kClipFrames};
    AUREA_CHECK(r.e.apply_command(d).ok());
    const Outcome o = run_export(r, 36, 30, false, 60);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(kClips * kClipFrames));
    // Every frame was the exact one: no clip needed the fallback.
    AUREA_CHECK((o.p.flags & Engine::kExportFrameFallback) == 0);
    std::printf("decoders: pico %u vivos, %u abertos, %u recusados ", factory.slots->peak.load(),
                factory.slots->opened.load(), factory.slots->refused.load());
    AUREA_CHECK(factory.slots->peak.load() <= factory.slots->limit);
}

/// Galaxy A32: "trava em 68% e fecha em 70%, com qualquer ajuste". O prepare
/// pula a camada com opacidade 0 antes de pedir o decoder; com o collect de 2
/// preparos do export, um fade no MEIO do clipe fechava o codec e o reabria na
/// volta (seek do keyframe, espera, um fecha/abre de codec por fade). Camada
/// ainda no seu trecho mantém o decoder: abre uma vez só.
AUREA_TEST(Export, InvisibleStretchInsideTheClipKeepsItsDecoder) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 64; cfg.height = 36;
    cfg.pattern = SyntheticPattern::FrameGray;
    cfg.frameCount = 60;
    SlotFactory factory(cfg);
    Rig r(cfg, 30.0, 40, 3, &factory);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    Layer* l = r.comp()->layer(r.video_layer());
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    // Visível 0..9, invisível 10..29 (opacidade 0, a camada nem entra no
    // prepare), visível de novo 30..39.
    Track& op = l->tracks.get_or_create(TrackProperty::Opacity);
    op.set(l->local_time(FrameIndex{9}), 1.0f);
    op.set(l->local_time(FrameIndex{10}), 0.0f);
    op.set(l->local_time(FrameIndex{29}), 0.0f);
    op.set(l->local_time(FrameIndex{30}), 1.0f);
    const Outcome o = run_export(r, 36, 30, false, 60);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(40));
    AUREA_CHECK_EQ(factory.slots->opened.load(), 1u);
    AUREA_CHECK((o.p.flags & Engine::kExportFrameFallback) == 0);
}

/// 10 hours of timeline (1,080,000 composition frames at 30 fps), exported at
/// a very low output rate and tiny size so it runs in seconds: timestamps reach
/// 36e9 us (past 2^32), and memory stays flat (nothing accumulates per frame
/// in the engine).
AUREA_TEST(Export, TenHourTimelineTimestampsAndFlatMemory) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    constexpr i64 kFrames = 10ll * 3600 * 30;
    SyntheticConfig cfg;
    cfg.width = 32; cfg.height = 18;
    cfg.pattern = SyntheticPattern::FrameGray;
    cfg.frameCount = static_cast<u32>(kFrames + 30);
    cfg.gop = 30;
    Rig r(cfg, 30.0, kFrames, 3);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    constexpr f64 kOutFps = 0.25;   // one output frame every 4 s of timeline: 9000 frames
    const i64 outFrames = static_cast<i64>(std::ceil(10.0 * 3600 * kOutFps - 1e-6));
    ExportSettings s;
    s.height = 18;
    s.fps = kOutFps;
    s.dither = false;
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(r.e.start_export(s, "nao-usado.mp4").ok());
    u64 at10 = 0, peakAfter10 = 0;
    u32 lastDone = 0;
    auto lastMove = std::chrono::steady_clock::now();
    bool stalled = false;
    for (;;) {
        const Engine::ExportProgress p = r.e.export_progress();
        if (p.finished) break;
        if (p.framesDone != lastDone) { lastDone = p.framesDone; lastMove = std::chrono::steady_clock::now(); }
        if (std::chrono::steady_clock::now() - lastMove > std::chrono::seconds(10)) { stalled = true; break; }
        const ProcMem pm = proc_mem();
        if (at10 == 0 && p.framesDone >= outFrames / 10) at10 = pm.privateBytes;
        if (at10) peakAfter10 = std::max(peakAfter10, pm.privateBytes);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    AUREA_CHECK(!stalled);
    if (stalled) { (void)r.e.cancel_export(); return; }
    const f64 secs = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    const Engine::ExportProgress p = r.e.export_progress();
    AUREA_CHECK_EQ(p.result, Errc::Ok);
    AUREA_CHECK_EQ(static_cast<i64>(p.framesTotal), outFrames);
    AUREA_CHECK_EQ(static_cast<i64>(r.cap.pts.size()), outFrames);
    AUREA_CHECK(r.cap.ptsMonotonic);
    AUREA_CHECK((p.flags & Engine::kExportFrameFallback) == 0);
    if (!r.cap.pts.empty()) {
        // Last frame at 9 h 59 min 56 s: 35,996,000,000 us, exact.
        AUREA_CHECK_EQ(r.cap.pts.back(), static_cast<i64>(std::llround(static_cast<f64>(outFrames - 1) * 1e6 / kOutFps)));
        AUREA_CHECK(r.cap.pts.back() > (i64{1} << 32));
    }
    // Distinct source frames up to the very end (the 10 h source is really read).
    if (r.cap.hashes.size() > 2)
        AUREA_CHECK(r.cap.hashes[r.cap.hashes.size() - 1] != r.cap.hashes[r.cap.hashes.size() - 2]);
    std::printf("%lld q em %.1f s, memoria em 10%% %.0f MB, pico depois %.0f MB ", static_cast<long long>(outFrames), secs,
                at10 / 1048576.0, peakAfter10 / 1048576.0);
#if defined(_WIN32)
    AUREA_CHECK(at10 > 0);
    AUREA_CHECK(peakAfter10 < at10 + (32ull << 20));
#endif
}

AUREA_TEST(Export, HeatReducesParallelismNeverQuality) {
    // §37: quente, o export anda com 1 quadro em voo — e os bytes são os
    // MESMOS do export frio (resolução, fps, efeitos e amostras intactos).
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.pattern = SyntheticPattern::MovingSquare;
    u64 hashes[2]{};
    u32 flags[2]{};
    for (int k = 0; k < 2; ++k) {
        Rig r(cfg, 30.0, 30, 0);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        build_effects_scene(r);
        if (k == 1) r.e.set_thermal(static_cast<u32>(ThermalState::Level::Serious), true);
        const Outcome o = run_export(r, 180, 0.0, true, 120);
        AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
        AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(30));
        AUREA_CHECK_EQ(r.cap.video.width, 320u);
        AUREA_CHECK_EQ(r.cap.video.height, 180u);
        hashes[k] = combined_hash(r.cap);
        flags[k] = o.p.flags;
    }
    AUREA_CHECK_EQ(hashes[0], hashes[1]);
    AUREA_CHECK((flags[0] & Engine::kExportThermalReduced) == 0);
    AUREA_CHECK((flags[1] & Engine::kExportThermalReduced) != 0);
}

AUREA_TEST(Export, SoftwareEncoderIsFlaggedNotHidden) {
    // §96–97: o encoder que o sink abriu chega à UI. Software = aviso; o
    // export segue com a MESMA saída.
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 128;
    cfg.height = 72;
    const ExportSink::Acceleration kinds[2] = {ExportSink::Acceleration::Hardware, ExportSink::Acceleration::Software};
    for (int k = 0; k < 2; ++k) {
        Rig r(cfg, 30.0, 5, 0);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        r.cap.accel = kinds[k];
        const Outcome o = run_export(r, 72, 0.0, true, 60);
        AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
        const bool hw = (o.p.flags & Engine::kExportHardwareEncoder) != 0;
        const bool sw = (o.p.flags & Engine::kExportSoftwareEncoder) != 0;
        AUREA_CHECK(k == 0 ? (hw && !sw) : (sw && !hw));
        // O que vai para a UI (ABI da bridge) leva os mesmos bits.
        bridge::ExportProgressPOD pod;
        r.e.fill_export_progress(pod);
        AUREA_CHECK_EQ(pod.flags, o.p.flags);
    }
}

AUREA_TEST(Export, EncoderFailurePreservesActionableDetailAndAborts) {
    if (!gpu_ok()) return;
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36; cfg.frameCount = 3;
    Rig r(cfg, 30, 3, 3); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.cap.writeFailure = Status{Errc::IoError, "falha ao gravar no MP4 (codigo -10000)"};
    const Outcome o = run_export(r, 36, 30, false, 5);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::IoError);
    AUREA_CHECK(std::strcmp(o.p.message, "falha ao gravar no MP4 (codigo -10000)") == 0);
    AUREA_CHECK(r.cap.aborted);
    AUREA_CHECK(!r.cap.finished);
}

AUREA_TEST(Export, PlatformDiagnosticSurvivesSinkDestructionAtEveryFailureStage) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36; cfg.frameCount = 1;
    for (int stage = 0; stage < 3; ++stage) {
        auto* backend = new MockBackend(); backend->mapBuffers = true;
        Rig r(cfg, 30, 1, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
        r.comp()->layer(r.video_layer())->visible = false;
        r.cap.invalidateDiagnosticOnDestroy = true;
        const char* expected = "falha nativa com diagnostico pertencente ao sink";
        std::snprintf(r.cap.platformDiagnostic, sizeof(r.cap.platformDiagnostic), "%s", expected);
        const Status fault{Errc::IoError, r.cap.platformDiagnostic};
        if (stage == 0) {
            r.cap.openFailure = fault;
            ExportSettings settings; settings.height = 36;
            const Status result = r.e.start_export(settings, "nao-usado.mp4");
            AUREA_CHECK_EQ(result.code(), Errc::IoError);
            AUREA_CHECK(result.detail() == expected);
        } else {
            if (stage == 1) r.cap.writeFailure = fault;
            else r.cap.finishFailure = fault;
            const Outcome o = run_export(r, 36, 30, false, 5);
            AUREA_CHECK(o.finished);
            AUREA_CHECK_EQ(o.p.result, Errc::IoError);
            AUREA_CHECK(std::strcmp(o.p.message, expected) == 0);
        }
        AUREA_CHECK(std::strcmp(r.cap.platformDiagnostic, "diagnostico expirado") == 0);
    }
}

AUREA_TEST(Export, InvalidSettingsAreRejectedBeforeOpeningTheEncoder) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    for (f64 rate : {-1.0, std::numeric_limits<f64>::quiet_NaN(),
                     std::numeric_limits<f64>::infinity(), std::numeric_limits<f64>::denorm_min()}) {
        ExportSettings settings; settings.fps = rate;
        AUREA_CHECK(!r.e.start_export(settings, "nao-usado.mp4").ok());
        AUREA_CHECK(!r.cap.opened);
    }
    ExportSettings settings; settings.height = std::numeric_limits<u32>::max();
    AUREA_CHECK(!r.e.start_export(settings, "nao-usado.mp4").ok());
    AUREA_CHECK(!r.cap.opened);
}

AUREA_TEST(Export, ReadbackMappingFailureNeverReachesTheEncoder) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 1, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    backend->beforeWaitFrame = [&](u64 frame, u64) {
        if (frame > 0) backend->mappedBuffers.clear();
        return OkStatus;
    };
    const Outcome o = run_export(r, 36, 30, false, 5);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::NotSupported);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    AUREA_CHECK(r.cap.hashes.empty());
}

AUREA_TEST(Export, TemporaryMissingGpuTextBufferRetriesBeforeEncoding) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 1, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    AUREA_CHECK(r.e.add_text("Text").ok());
    u32 refused = 0;
    backend->beforeCreateBuffer = [&](const BufferDesc& desc) {
        if (desc.debugName && std::strcmp(desc.debugName, "glifos") == 0 && refused++ == 0)
            return Status{Errc::OutOfDeviceMemory};
        return OkStatus;
    };
    const Outcome o = run_export(r, 36, 30, false, 8);
    AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{1});
    AUREA_CHECK(refused >= 2);
    AUREA_CHECK(backend->framesSubmitted >= 2);
}

AUREA_TEST(Export, PersistentMissingGpuTextBufferFailsInsteadOfSavingIncompleteVideo) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 1, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    AUREA_CHECK(r.e.add_text("Text").ok());
    backend->beforeCreateBuffer = [](const BufferDesc& desc) {
        return desc.debugName && std::strcmp(desc.debugName, "glifos") == 0
            ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    const Outcome o = run_export(r, 36, 30, false, 8);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::InvalidState);
    AUREA_CHECK_EQ(o.p.failure, 3u); // ExportFailure::Render
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    AUREA_CHECK(r.cap.hashes.empty());
}

AUREA_TEST(Export, CriticalMemoryTrimReleasesCachesWhileEncoderKeepsItsFrame) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    std::atomic<bool> entered{false}, allow{false};
    r.cap.writeEntered = &entered;
    r.cap.allowWrite = &allow;
    ExportSettings settings; settings.height = 36;
    AUREA_CHECK(r.e.start_export(settings, "nao-usado.mp4").ok());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(entered.load());
    AUREA_CHECK(r.e.export_progress().running);
    const u32 waits = backend->idleWaits.load();
    const auto trim = r.e.trim_memory(15);
    AUREA_CHECK(trim.upTo == TrimStage::Temporaries);
    AUREA_CHECK(backend->idleWaits.load() >= waits + 2);
    // The frame already handed to the encoder remains mapped and readable;
    // all subsequent frames must still encode, without altering the project.
    allow.store(true, std::memory_order_release);
    const auto finish = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!r.e.export_progress().finished && std::chrono::steady_clock::now() < finish)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto progress = r.e.export_progress();
    AUREA_CHECK(progress.finished && progress.result == Errc::Ok);
    r.e.shutdown(); // Join before inspecting the sink capture or destroying gates.
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{3});
    AUREA_CHECK(r.cap.ptsMonotonic);
}

AUREA_TEST(Export, PreviewSleepsThroughExportWithExpiredRefinementAndResumes) {
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 72;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    AUREA_CHECK(r.e.add_shape(0).ok());
    int window = 0;
    AUREA_CHECK(r.e.attach_surface(&window, 128, 72).ok());
    const auto initialQuality = r.e.read_telemetry();
    FrameStats expensive; expensive.gpuMs = 200; expensive.cpuMs = 100;
    for (u32 i = 0; i < 100; ++i) r.e.debug_feed_frame_stats(expensive);
    const auto reducedQuality = r.e.read_telemetry();
    AUREA_CHECK(reducedQuality.previewDenominator > initialQuality.previewDenominator
        || reducedQuality.previewHeavyLevel > initialQuality.previewHeavyLevel);
    AUREA_CHECK(r.e.render_frame().ok()); // leave a real reduced-quality refinement pending
    std::atomic<bool> entered{false}, allow{false};
    r.cap.writeEntered = &entered; r.cap.allowWrite = &allow;
    const u32 presentedBeforeExport = backend->presents;
    ExportSettings settings; settings.height = 72;
    AUREA_CHECK(r.e.start_export(settings, "nao-usado.mp4").ok());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!entered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(entered.load());
    r.e.start_render_thread();
    const u64 before = r.e.render_wakeups();
    // Simulate repeated mobile vsync/decoder callbacks past the 250 ms deadline.
    for (u32 i = 0; i < 40; ++i) {
        r.e.request_render();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    AUREA_CHECK(r.e.export_progress().running);
    AUREA_CHECK(r.e.render_wakeups() - before <= 1);
    const u64 sleepingWakeups = r.e.render_wakeups();
    allow.store(true, std::memory_order_release);
    const auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!r.e.export_progress().finished || r.e.render_wakeups() <= sleepingWakeups)
        && std::chrono::steady_clock::now() < resumeDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(r.e.export_progress().finished);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Ok);
    AUREA_CHECK(r.e.render_wakeups() > sleepingWakeups);
    // Stop joins the currently executing render before reading mock counters.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    r.e.stop_render_thread();
    AUREA_CHECK(backend->presents > presentedBeforeExport);
    r.e.shutdown();
}

AUREA_TEST(Export, SuspendStopsExportBeforeClosingMediaAndAllowsRestart) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 300, 2, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    r.cap.encodeUs = 20000;
    ExportSettings settings; settings.height = 36;
    AUREA_CHECK(r.e.start_export(settings, "nao-usado.mp4").ok());
    AUREA_CHECK(r.e.suspend().ok());
    const auto progress = r.e.export_progress();
    AUREA_CHECK_EQ(r.e.state(), EngineState::Suspended);
    AUREA_CHECK(progress.finished && !progress.running);
    AUREA_CHECK_EQ(progress.result, Errc::Cancelled);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    AUREA_CHECK(r.e.resume().ok());
    r.cap.encodeUs = 0;
    const Outcome restarted = run_export(r, 36, 30, false, 5);
    AUREA_CHECK(restarted.finished && restarted.p.result == Errc::Ok);
}

AUREA_TEST(Export, SuspendSerializesWithAnEncoderThatIsStillOpening) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 300, 2, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    std::atomic<bool> opening{false};
    r.cap.openEntered = &opening;
    r.cap.openDelayUs = 100000;
    r.cap.encodeUs = 20000;
    Status start;
    std::thread exporting([&] { ExportSettings s; s.height = 36; start = r.e.start_export(s, "nao-usado.mp4"); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!opening.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(opening.load());
    AUREA_CHECK(r.e.suspend().ok());
    exporting.join();
    AUREA_CHECK(start.ok());
    AUREA_CHECK_EQ(r.e.state(), EngineState::Suspended);
    const auto progress = r.e.export_progress();
    AUREA_CHECK(progress.finished && !progress.running);
    AUREA_CHECK_EQ(progress.result, Errc::Cancelled);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    ExportSettings settings;
    AUREA_CHECK_EQ(r.e.start_export(settings, "nao-usado.mp4").code(), Errc::InvalidState);
    r.cap.openEntered = nullptr;
}

AUREA_TEST(Export, CancelIsResponsiveAndReleasesTheSink) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 640;
    cfg.height = 360;
    cfg.pattern = SyntheticPattern::FrameGray;
    Rig r(cfg, 30.0, 3000, 0);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    r.cap.encodeUs = 20000;   // encoder lento: fila cheia na hora do cancelamento
    ExportSettings s;
    s.height = 360;
    AUREA_CHECK(r.e.start_export(s, "nao-usado.mp4").ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(r.e.cancel_export().ok());
    bool done = false;
    for (int i = 0; i < 5000 && !done; ++i) {
        done = r.e.export_progress().finished;
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    AUREA_CHECK(done);
    std::printf("    cancelamento em %.1f ms ", ms);
    AUREA_CHECK(ms < 250.0);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Cancelled);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    // A GPU volta ao preview.
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    AUREA_CHECK(r.e.capture_frame_rgba(64, rgba, w, h).ok());
}

AUREA_TEST(Export, NeuralUpscaleCancelReturnsPreviewAndAbortsPartialOutput) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 128; cfg.height = 72;
    cfg.pattern = SyntheticPattern::MovingSquare;
    Rig r(cfg, 30.0, 120, 3);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    ExportSettings settings; settings.height = 288; settings.aiUpscale = 4;
    AUREA_CHECK(r.e.start_export(settings, "nao-usado.mp4").ok());
    bool processing = false;
    for (int i=0; i<10000; ++i) {
        const auto p = r.e.export_progress();
        if (std::strncmp(p.message, "IA:", 3) == 0) { processing = true; break; }
        if (p.finished) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(processing);
    const auto start = std::chrono::steady_clock::now();
    AUREA_CHECK(r.e.cancel_export().ok());
    for (int i=0; i<5000 && !r.e.export_progress().finished; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(r.e.export_progress().finished);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Cancelled);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    const auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("neural cancel %.1f ms ", ms);
    AUREA_CHECK(ms < 2000);
    std::vector<u8> rgba; u32 w=0,h=0;
    AUREA_CHECK(r.e.capture_frame_rgba(64,rgba,w,h).ok());
}

// =============================================================================
// Benchmarks (AUREA_BENCH_EXPORT=1)
// =============================================================================
AUREA_TEST(ExportBench, Resolutions) {
    if (!bench_enabled() || !gpu_ok()) { std::printf("(AUREA_BENCH_EXPORT=1 para rodar) "); return; }
    struct Case { const char* name; u32 w, h; f64 fps; i64 frames; };
    const Case cases[] = {
        {"basico 1080p30", 1920, 1080, 30.0, 150},
        {"basico 1080p60", 1920, 1080, 60.0, 240},
        {"basico 4K30", 3840, 2160, 30.0, 90},
        {"basico 4K60", 3840, 2160, 60.0, 120},
    };
    for (const Case& c : cases) {
        SyntheticConfig cfg;
        cfg.width = c.w;
        cfg.height = c.h;
        cfg.fps = c.fps;
        cfg.frameCount = static_cast<u32>(c.frames + 30);
        cfg.pattern = SyntheticPattern::FrameGray;
        cfg.audioRate = 48000;
        cfg.audioSeconds = static_cast<f64>(c.frames) / c.fps + 1.0;
        Rig r(cfg, c.fps, c.frames, bench_depth());
        if (!r.ok) { AUREA_CHECK(r.ok); continue; }
        const Outcome o = run_export(r, c.h, 0.0);
        AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
        print_line(c.name, r, o);
    }
    std::printf("\n");
}

AUREA_TEST(ExportBench, EffectsAnd3D) {
    if (!bench_enabled() || !gpu_ok()) { std::printf("(AUREA_BENCH_EXPORT=1 para rodar) "); return; }
    struct Case { const char* name; u32 w, h; f64 fps; i64 frames; bool fx; bool blur = false; };
    const Case cases[] = {
        {"efeitos 1080p30", 1920, 1080, 30.0, 120, true},
        {"efeitos 4K30", 3840, 2160, 30.0, 60, true},
        {"3D 1080p30", 1920, 1080, 30.0, 120, false},
        {"3D 4K30", 3840, 2160, 30.0, 60, false},
        // Desfoque de movimento da composição: o export usa as amostras cheias.
        {"3D desfoque 1080p30", 1920, 1080, 30.0, 30, false, true},
    };
    for (const Case& c : cases) {
        SyntheticConfig cfg;
        cfg.width = c.w;
        cfg.height = c.h;
        cfg.fps = c.fps;
        cfg.frameCount = static_cast<u32>(c.frames + 30);
        cfg.pattern = SyntheticPattern::FrameGray;
        cfg.audioRate = 48000;
        cfg.audioSeconds = static_cast<f64>(c.frames) / c.fps + 1.0;
        Rig r(cfg, c.fps, c.frames, bench_depth());
        if (!r.ok) { AUREA_CHECK(r.ok); continue; }
        if (c.fx) build_effects_scene(r);
        else if (!build_3d_scene(r)) { std::printf("\n    %-22s sem modelos glTF (AUREA_BENCH_GLTF): pulado", c.name); continue; }
        if (c.blur) {
            // Composição E camadas (a chave por camada, como no editor).
            (void)r.e.set_composition_motion_blur(true);
            const OrderedIds<LayerId>& order = r.comp()->order();
            for (u32 k = 0; k < order.size(); ++k) (void)r.e.set_motion_blur(order.at(k).pack(), true);
        }
        const Outcome o = run_export(r, c.h, 0.0);
        AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
        print_line(c.name, r, o);
    }
    std::printf("\n");
}

/// Export longo em modo acelerado: a timeline inteira de 10/30/60 min a 30 fps,
/// com som, em resolução baixa (o que se vigia é memória, deadlock e deriva —
/// não o custo por pixel, que os cenários acima medem).
AUREA_TEST(ExportBench, LongExports) {
    if (!bench_enabled() || !gpu_ok()) { std::printf("(AUREA_BENCH_EXPORT=1 para rodar) "); return; }
    const char* only = std::getenv("AUREA_BENCH_LONG_MIN");   // ex.: "10" para só o de 10 min
    const u32 minutes[] = {10, 30, 60};
    for (u32 m : minutes) {
        if (only && static_cast<u32>(std::atoi(only)) != m) continue;
        const i64 frames = static_cast<i64>(m) * 60 * 30;
        SyntheticConfig cfg;
        cfg.width = 160;
        cfg.height = 90;
        cfg.fps = 30.0;
        cfg.frameCount = static_cast<u32>(frames + 30);
        cfg.gop = 30;
        cfg.pattern = SyntheticPattern::FrameGray;
        cfg.audioRate = 48000;
        cfg.audioSeconds = static_cast<f64>(frames) / 30.0 + 2.0;
        Rig r(cfg, 30.0, frames, 0);
        if (!r.ok) { AUREA_CHECK(r.ok); continue; }
        ExportSettings s;
        s.height = 90;
        const ProcMem before = proc_mem();
        const auto t0 = std::chrono::steady_clock::now();
        AUREA_CHECK(r.e.start_export(s, "nao-usado.mp4").ok());
        // Memória a cada 10% (a curva, não só o fim) e vigia de deadlock: sem
        // quadro novo em 10 s = travou.
        u64 peak = 0, at10 = 0, peakAfter10 = 0;
        u32 lastDone = 0;
        auto lastMove = std::chrono::steady_clock::now();
        bool stalled = false;
        std::printf("\n    longo %2u min (%lld q): memoria privada MB por 10%%:", m, static_cast<long long>(frames));
        u32 nextMark = 1;
        for (;;) {
            const Engine::ExportProgress p = r.e.export_progress();
            if (p.finished) break;
            if (p.framesDone != lastDone) { lastDone = p.framesDone; lastMove = std::chrono::steady_clock::now(); }
            if (std::chrono::steady_clock::now() - lastMove > std::chrono::seconds(10)) { stalled = true; break; }
            const ProcMem pm = proc_mem();
            peak = std::max(peak, pm.privateBytes);
            if (nextMark > 1) peakAfter10 = std::max(peakAfter10, pm.privateBytes);
            if (p.framesDone >= frames * nextMark / 10 && nextMark <= 10) {
                std::printf(" %.0f", pm.privateBytes / 1048576.0);
                if (nextMark == 1) at10 = pm.privateBytes;
                ++nextMark;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        AUREA_CHECK(!stalled);
        if (stalled) { (void)r.e.cancel_export(); continue; }
        const f64 secs = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
        const ProcMem after = proc_mem();
        const Engine::ExportProgress p = r.e.export_progress();
        AUREA_CHECK_EQ(p.result, Errc::Ok);
        AUREA_CHECK_EQ(static_cast<i64>(r.cap.hashes.size()), frames);
        // Deriva A/V: o som termina EXATAMENTE no fim do último quadro.
        const i64 wantSamples = audio::frame_to_sample(frames, 30.0);
        const i64 lastVideoEndUs = static_cast<i64>(std::llround(static_cast<f64>(frames) * 1e6 / 30.0));
        const i64 audioEndUs = audio::sample_to_ns(r.cap.audioFrames) / 1000;
        AUREA_CHECK_EQ(r.cap.audioFrames, wantSamples);
        AUREA_CHECK(r.cap.ptsMonotonic && r.cap.audioContiguous);
        std::printf("\n      %.1f s (%.0f q/s, %.0fx tempo real) | pico %.0f MB (apos 10%%: %.0f), em 10%% %.0f MB, antes %.0f MB, depois %.0f MB | handles %u -> %u | deriva A/V %lld us",
                    secs, frames / secs, (frames / 30.0) / secs, peak / 1048576.0, peakAfter10 / 1048576.0,
                    at10 / 1048576.0,
                    before.privateBytes / 1048576.0, after.privateBytes / 1048576.0, before.handles, after.handles,
                    static_cast<long long>(audioEndUs - lastVideoEndUs));
        // Crescimento depois dos primeiros 10%: sem vazamento por quadro.
        AUREA_CHECK(peakAfter10 < at10 + (64ull << 20));
    }
    std::printf("\n");
}

AUREA_TEST(Regression2135Gpu, CompletedFramesReturnDecoderLeasesBeforeAnotherSubmission) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    vk::Backend backend;
    BackendConfig cfg; cfg.framesInFlight = 3;
    AUREA_CHECK(backend.initialize(cfg).ok());
    u32 released[2]{};
    for (u32 i = 0; i < 2; ++i) {
        FrameBegin frame;
        AUREA_CHECK(backend.begin_offscreen_frame(frame).ok());
        AUREA_CHECK(backend.end_frame().ok());
        backend.defer_until_gpu_done([](void* count) { ++*static_cast<u32*>(count); }, &released[i]);
    }
    AUREA_CHECK_EQ(released[0] + released[1], 0u);
    const u64 last = backend.last_submitted_frame();
    AUREA_CHECK(backend.wait_frame(last, 5'000'000'000ull).ok());
    AUREA_CHECK_EQ(released[0], 1u);
    AUREA_CHECK_EQ(released[1], 1u);
    AUREA_CHECK(backend.wait_frame(last, 0).ok());
    backend.wait_idle();
    AUREA_CHECK_EQ(released[0] + released[1], 2u);
    backend.shutdown();
}

AUREA_TEST(Regression2135Gpu, CancelWhileDecodeNeedsGpuDoesNotWaitIdle) {
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 72;
    cfg.frameCount = 30; cfg.decodeCostUs = 500000;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    std::atomic<bool> waiting{false};
    std::atomic<u64> waitingFrame{~u64{0}};
    std::atomic<u32> nonBlockingPolls{0};
    backend->beforeWaitFrame = [&](u64 frame, u64 timeout) {
        if (timeout == 0) { ++nonBlockingPolls; return Status{Errc::Timeout}; }
        waitingFrame.store(frame);
        waiting.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return Status{Errc::Timeout};
    };
    Rig r(cfg, 30, 30, 2, nullptr, backend);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    AUREA_CHECK(r.e.set_vector_blur(r.video_layer().pack(), 1));
    ExportSettings settings; settings.height = 72;
    AUREA_CHECK(r.e.start_export(settings, "test-cancel-gpu.mp4").ok());
    const u32 idleBefore = backend->idleWaits.load();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!waiting.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    AUREA_CHECK(waiting.load());
    AUREA_CHECK_EQ(waitingFrame.load(), 0u); // Still preparing the very first video frame.
    r.e.cancel_export();
    while (!r.e.export_progress().finished && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    AUREA_CHECK(r.e.export_progress().finished);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Cancelled);
    AUREA_CHECK_EQ(backend->idleWaits.load(), idleBefore);
    AUREA_CHECK_EQ(nonBlockingPolls.load(), 1u);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
}


AUREA_TEST(ExportPendingGpu, CancelledSubmittedFrameBlocksNewGpuWorkUntilItsFenceCompletes) {
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 72; cfg.frameCount = 30;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    std::atomic<bool> waiting{false}, completed{false};
    std::atomic<u64> pendingFrame{0};
    backend->beforeWaitFrame = [&](u64 frame, u64 timeout) {
        if (!frame || completed.load()) return OkStatus;
        pendingFrame = frame;
        if (timeout) { waiting = true; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        return Status{Errc::Timeout};
    };
    Rig r(cfg, 30, 30, 1, nullptr, backend);
    struct CompleteOnExit { std::atomic<bool>& done; ~CompleteOnExit() { done = true; } } complete{completed};
    AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    AUREA_CHECK(r.e.attach_surface(reinterpret_cast<void*>(1), 128, 72).ok());
    TextureDesc desc; desc.width = 64; desc.height = 36; desc.format = SurfaceFormat::RGBA16F;
    desc.sampled = desc.renderTarget = true;
    const auto target = backend->create_texture(desc); AUREA_CHECK(target.ok()); if (!target.ok()) return;
    ExportSettings settings; settings.height = 72;
    AUREA_CHECK(r.e.start_export(settings, "test-pending-gpu.mp4").ok());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!waiting.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK(waiting.load()); AUREA_CHECK(pendingFrame.load() > 0);
    r.e.cancel_export();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!r.e.export_progress().finished && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK(r.e.export_progress().finished);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Cancelled);
    // Joining the previous worker also establishes exportActive=false before
    // checking the preview gate (progress.finished is published just before it).
    AUREA_CHECK_EQ(r.e.start_export(settings, "test-pending-gpu-retry.mp4").code(), Errc::Timeout);
    const u32 submitted = backend->framesSubmitted, textures = backend->texturesCreated;
    const u32 waits = backend->idleWaits.load(), acquires = backend->acquires;
    const u32 oldWidth = backend->surfaceWidth, oldHeight = backend->surfaceHeight;
    AUREA_CHECK(r.e.resize_surface(256, 144).ok());
    Command duration; duration.type = CommandType::CompositionSetDuration;
    duration.comp_duration.comp = r.e.project()->timeline().current(); duration.comp_duration.duration = FrameIndex{44};
    AUREA_CHECK_EQ(r.e.submit_commands(&duration, 1), 1u);
    AUREA_CHECK_EQ(r.e.render_frame().code(), Errc::Timeout);
    AUREA_CHECK_EQ(r.comp()->duration().value, 44);
    AUREA_CHECK_EQ(backend->surfaceWidth, oldWidth); AUREA_CHECK_EQ(backend->surfaceHeight, oldHeight);
    AUREA_CHECK_EQ(r.e.render_offscreen(*target, 64, 36).code(), Errc::Timeout);
    std::vector<u8> pixels; u32 width = 0, height = 0;
    AUREA_CHECK_EQ(r.e.capture_frame_rgba(64, pixels, width, height).code(), Errc::Timeout);
    AUREA_CHECK_EQ(r.e.render_effect_preview(1, 64, 36, pixels, width, height).code(), Errc::Timeout);
    AUREA_CHECK_EQ(r.e.start_export(settings, "test-pending-gpu-again.mp4").code(), Errc::Timeout);
    AUREA_CHECK_EQ(r.e.start_image_export(ImageExportSettings{}, "test-pending-gpu.png").code(), Errc::Timeout);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea-pending-gpu.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    const Project* project = r.e.project();
    AUREA_CHECK_EQ(r.e.new_project(128, 72, 30, "blocked replacement").code(), Errc::Timeout);
    AUREA_CHECK_EQ(r.e.load_project(path.c_str()).code(), Errc::Timeout);
    AUREA_CHECK(r.e.project() == project);
    AUREA_CHECK_EQ(r.e.suspend().code(), Errc::Timeout);
    AUREA_CHECK(r.e.state() != EngineState::Suspended);
    (void)r.e.trim_memory(80);
    AUREA_CHECK_EQ(backend->framesSubmitted, submitted);
    AUREA_CHECK_EQ(backend->texturesCreated, textures);
    AUREA_CHECK_EQ(backend->idleWaits.load(), waits);
    AUREA_CHECK_EQ(backend->acquires, acquires);
    AUREA_CHECK(!backend->deviceLost);
    completed = true;
    AUREA_CHECK(r.e.render_frame().ok());
    AUREA_CHECK_EQ(backend->surfaceWidth, 256u); AUREA_CHECK_EQ(backend->surfaceHeight, 144u);
    AUREA_CHECK(backend->framesSubmitted > submitted); AUREA_CHECK(backend->acquires > acquires);
    backend->destroy_texture(*target);
    std::remove(path.c_str()); std::remove((path + ".bak").c_str());
}

AUREA_TEST(ExportPendingGpu, CancelWithCompletedFenceImmediatelyResumesPreview) {
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 72; cfg.frameCount = 30;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    std::atomic<bool> waiting{false};
    backend->beforeWaitFrame = [&](u64 frame, u64 timeout) {
        if (!frame || !timeout) return OkStatus;
        waiting = true; std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return Status{Errc::Timeout};
    };
    Rig r(cfg, 30, 30, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    AUREA_CHECK(r.e.attach_surface(reinterpret_cast<void*>(1), 128, 72).ok());
    ExportSettings settings; settings.height = 72;
    AUREA_CHECK(r.e.start_export(settings, "test-completed-cancel.mp4").ok());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!waiting.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK(waiting.load());
    r.e.cancel_export();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!r.e.export_progress().finished && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK(r.e.export_progress().finished);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Cancelled);
    const u32 before = backend->framesSubmitted;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    do {
        AUREA_CHECK(r.e.render_frame().ok());
        if (backend->framesSubmitted > before) break;
        std::this_thread::yield(); // worker publishes exportActive=false after finished
    } while (std::chrono::steady_clock::now() < deadline);
    AUREA_CHECK(backend->framesSubmitted > before);
    AUREA_CHECK(!backend->deviceLost);
}

AUREA_TEST(Regression2135Gpu, SharedTextTransformAndPresetReach3DExportAndSurviveReopen) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg; cfg.width = 320; cfg.height = 180;
    Rig r(cfg, 30, 12, 2); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    scene3d::Text3DSpec spec; spec.content = "AUREA";
    const auto id = r.e.add_text3d(spec); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    AUREA_CHECK(r.e.apply_text_preset(*id, 2));
    auto* effect = r.add_effect(LayerId::unpack(*id), text::kTransformEffect);
    AUREA_CHECK(effect != nullptr); if (!effect) return;
    auto* layer = r.comp()->layer(LayerId::unpack(*id)); layer->end = FrameIndex{12};
    auto& track = layer->tracks.get_or_create(TrackProperty::EffectParam, effect->id, param_track_key(text::kOffset, 0));
    track.set(FrameIndex{0}, -20, Interpolation::Linear); track.set(FrameIndex{11}, 20, Interpolation::Linear);
    Command duration; duration.type = CommandType::CompositionSetDuration;
    duration.comp_duration.comp = r.e.project()->timeline().current(); duration.comp_duration.duration = FrameIndex{12};
    AUREA_CHECK(r.e.apply_command(duration).ok());
    r.cap.keepFrames = true;
    auto result = run_export(r, 180, 30, false, 60);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), 12u);
    if (r.cap.hashes.size() != 12) return;
    AUREA_CHECK(r.cap.hashes.front() != r.cap.hashes.back());
    const auto frames = r.cap.frames;
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_shared_text3d_export.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok()); AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    r.cap = BenchCapture{}; r.cap.keepFrames = true;
    result = run_export(r, 180, 30, false, 60);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK(frames == r.cap.frames);
    std::remove(path.c_str());
}

#include "ExportAudioProgress.inl"
#include "CutFrameGpu.inl"
#include "ExportWatchdog.inl"

#endif // AUREA_TEST_VULKAN

// =============================================================================
//  Taxa de bits do export (BitratePolicy) — sem GPU. "1 minuto = 1 GB" foi o
//  bug: a regra antiga (0,2 bit/pixel·s, ×1,6 na Alta, sem teto) pedia até
//  160 Mbps. Aqui ficam os números que um editor comum usa.
// =============================================================================
#include "aurea/export/BitratePolicy.hpp"

AUREA_TEST(ExportBitrate, OneMinute1080p30NormalStaysSmall) {
    using namespace aurea;
    const u32 v = export_video_bitrate_bps(1920, 1080, 30.0, ExportCodec::H264, ExportQuality::Normal);
    AUREA_CHECK(v >= 12'000'000u && v <= 16'000'000u);
    const u64 bytes = export_estimated_bytes(v, kExportAudioKbps * 1000u, 60.0);
    AUREA_CHECK_MSG(bytes < 150ull * 1000 * 1000, "1 min 1080p30 Normal deve ficar abaixo de ~150 MB");
    AUREA_CHECK(bytes > 60ull * 1000 * 1000);
}

AUREA_TEST(ExportBitrate, ResolutionFpsQualityAndCodecScaleSanely) {
    using namespace aurea;
    const auto bps = [](u32 w, u32 h, f64 fps, ExportQuality q = ExportQuality::Normal, ExportCodec c = ExportCodec::H264) {
        return export_video_bitrate_bps(w, h, fps, c, q);
    };
    const u32 p720 = bps(1280, 720, 30), p1080 = bps(1920, 1080, 30), p4k = bps(3840, 2160, 30);
    AUREA_CHECK(p720 >= 6'000'000u && p720 <= 8'000'000u);
    AUREA_CHECK(p4k >= 35'000'000u && p4k <= 45'000'000u);
    AUREA_CHECK(bps(854, 480, 30) < p720 && p720 < p1080 && p1080 < bps(2560, 1440, 30) && bps(2560, 1440, 30) < p4k);
    // Vertical = mesma quantidade de pixels, mesma taxa.
    AUREA_CHECK_EQ(bps(1080, 1920, 30), p1080);
    // 60 fps custa mais, mas não o dobro; 24 fps custa menos.
    const u32 p1080_60 = bps(1920, 1080, 60);
    AUREA_CHECK(p1080_60 > p1080 && p1080_60 < p1080 * 2);
    AUREA_CHECK(bps(1920, 1080, 24) < p1080);
    // Qualidade e codec.
    AUREA_CHECK(bps(1920, 1080, 30, ExportQuality::Low) < p1080);
    AUREA_CHECK(bps(1920, 1080, 30, ExportQuality::High) > p1080);
    AUREA_CHECK(bps(1920, 1080, 30, ExportQuality::Normal, ExportCodec::HEVC) < p1080);
    // Nada passa do teto, nem 4K60 Alta nem Mbps manual absurdo; 1 min nunca 1 GB.
    const u32 worst = bps(3840, 2160, 60, ExportQuality::High);
    AUREA_CHECK(worst <= static_cast<u32>(kExportMaxVideoBps));
    AUREA_CHECK(export_video_bitrate_bps(1920, 1080, 30, ExportCodec::H264, ExportQuality::Normal, 900) <= 100'000'000u);
    AUREA_CHECK(export_estimated_bytes(worst, 192'000u, 60.0) < 800ull * 1000 * 1000);
    // Mbps manual é respeitado.
    AUREA_CHECK_EQ(export_video_bitrate_bps(1920, 1080, 30, ExportCodec::H264, ExportQuality::Low, 10), 10'000'000u);
    AUREA_CHECK_EQ(export_estimated_bytes(10'000'000u, 0u, 0.0), 0ull);
}

// =============================================================================
//  Regras do watchdog do export (export/ExportWatchdog.hpp) — sem GPU. "A
//  exportação para num percentual e nunca termina": toda espera tem prazo sem
//  progresso e teto, e a recuperação (modo de segurança) é regra do motor.
// =============================================================================
#include "aurea/export/ExportWatchdog.hpp"

AUREA_TEST(ExportWatchdogRules, DeadlineRenewsOnProgressButNeverPassesTheCeiling) {
    using namespace aurea;
    ProgressDeadline d(1000, 100, 250);
    AUREA_CHECK(!d.expired(1100));
    AUREA_CHECK(d.expired(1101));
    d.progress(1050);                       // renova: 1150
    AUREA_CHECK(!d.expired(1150));
    AUREA_CHECK(d.expired(1151));
    d.progress(1200);                       // 1300, mas o teto é 1250
    AUREA_CHECK_EQ(d.deadline(), u64{1250});
    d.exclude(40);                          // tempo que não conta empurra prazo e teto
    AUREA_CHECK_EQ(d.deadline(), u64{1290});
    d.progress(1280);
    AUREA_CHECK_EQ(d.deadline(), u64{1290});
    ProgressDeadline open(0, 10, 0);        // sem teto
    open.progress(1'000'000);
    AUREA_CHECK_EQ(open.deadline(), u64{1'000'010});
}

/// Antes: depois do 1º quadro aproximado a paciência caía para 1 s, mas o teto
/// de 60 s continuava valendo sempre que OUTRO decoder entregava algo — um
/// quadro por minuto, o export "parado". Agora o teto cai junto.
AUREA_TEST(ExportWatchdogRules, SourcePatienceShrinksWithConsecutiveFallbacks) {
    using namespace aurea;
    const ExportSourceWait exact = export_source_wait(0);
    AUREA_CHECK_EQ(exact.patienceNs, u64{4'000'000'000});
    AUREA_CHECK_EQ(exact.hardCapNs, u64{60'000'000'000});
    u64 previousPatience = exact.patienceNs, previousCap = exact.hardCapNs;
    u64 worst = 0;   // 300 quadros quebrados seguidos, com decoders entregando o tempo todo
    for (u32 k = 0; k < 300; ++k) {
        const ExportSourceWait w = export_source_wait(k);
        AUREA_CHECK(w.patienceNs <= previousPatience && w.hardCapNs <= previousCap);
        AUREA_CHECK(w.patienceNs <= w.hardCapNs);
        previousPatience = w.patienceNs;
        previousCap = w.hardCapNs;
        worst += w.hardCapNs;
    }
    AUREA_CHECK_EQ(export_source_wait(kExportFallbackStreak).hardCapNs, u64{1'000'000'000});
    // 300 × 60 s = 5 horas antes; agora poucos minutos no pior caso.
    AUREA_CHECK(worst < u64{420} * 1'000'000'000ull);
}

AUREA_TEST(ExportWatchdogRules, OnlyAWorkerStuckInsideThePlatformIsHung) {
    using namespace aurea;
    const u64 s = 1'000'000'000ull;
    // Parado na fila (o produtor está lento) não é travar.
    AUREA_CHECK(!export_worker_hung(false, 100 * s, 0, 45 * s));
    AUREA_CHECK(!export_worker_hung(true, 44 * s, 0, 45 * s));
    AUREA_CHECK(export_worker_hung(true, 46 * s, 0, 45 * s));
    // Batida recente (laço do sink voltando da plataforma) = vivo.
    AUREA_CHECK(!export_worker_hung(true, 100 * s, 99 * s, 45 * s));
    // Relógio da batida à frente (outra thread acabou de bater) não é travar.
    AUREA_CHECK(!export_worker_hung(true, 10 * s, 11 * s, 1));
    // Limites: 45 s padrão; soltar o encoder 10 s; cancelado 3 s.
    AUREA_CHECK_EQ(export_worker_hang_limit_ns(ExportWorkerPhase::Video, 0), kExportWorkerHangNs);
    AUREA_CHECK_EQ(export_worker_hang_limit_ns(ExportWorkerPhase::Abort, 0), kExportWorkerAbortHangNs);
    AUREA_CHECK_EQ(export_worker_hang_limit_ns(ExportWorkerPhase::Finish, 0, true), kExportWorkerCancelHangNs);
    AUREA_CHECK_EQ(export_worker_hang_limit_ns(ExportWorkerPhase::Video, 400'000'000ull, true), u64{400'000'000});
    AUREA_CHECK(export_worker_hang_code(ExportWorkerPhase::AudioMix) == Errc::DecodeFailed);
    AUREA_CHECK(export_worker_hang_code(ExportWorkerPhase::Video) == Errc::Timeout);
    // A tela só desiste bem depois de todos os prazos do motor.
    AUREA_CHECK(u64{kExportUiStallSeconds} * s > kExportWorkerHangNs + 120 * s);
}

AUREA_TEST(ExportWatchdogRules, SafeModeLadderAndItsVideoRecipe) {
    using namespace aurea;
    AUREA_CHECK_EQ(export_retry_safe_mode(ExportFailure::EncoderStalled, 0), 1u);
    AUREA_CHECK_EQ(export_retry_safe_mode(ExportFailure::Encoder, 1), 2u);
    AUREA_CHECK_EQ(export_retry_safe_mode(ExportFailure::EncoderStalled, 2), 0u);   // última volta
    for (ExportFailure f : {ExportFailure::None, ExportFailure::Render, ExportFailure::GpuMemory, ExportFailure::Media,
                            ExportFailure::File, ExportFailure::Storage, ExportFailure::Unsupported, ExportFailure::Other})
        AUREA_CHECK_EQ(export_retry_safe_mode(f, 0), 0u);
    // Bits 16..17 do progresso: abaixo do motivo (24..31) e acima das flags.
    const u32 flags = (2u << kExportRetryShift) | (2u << kExportFailureShift) | 0x1Fu;
    AUREA_CHECK_EQ(export_retry_from_flags(flags), 2u);
    AUREA_CHECK_EQ(kExportRetryMask & ((1u << kExportFailureShift) - 1u), kExportRetryMask);
    // Sempre H.264 no modo de segurança.
    AUREA_CHECK(export_safe_codec(ExportCodec::HEVC, 1) == ExportCodec::H264);
    AUREA_CHECK(export_safe_codec(ExportCodec::HEVC, 0) == ExportCodec::HEVC);
    // Taxa menor, nunca abaixo de 0,5 Mbps (nem sobe o que já era menor).
    AUREA_CHECK_EQ(export_safe_bitrate_bps(14'000'000u, 0), 14'000'000u);
    AUREA_CHECK_EQ(export_safe_bitrate_bps(14'000'000u, 1), 10'500'000u);
    AUREA_CHECK_EQ(export_safe_bitrate_bps(14'000'000u, 2), 8'400'000u);
    AUREA_CHECK_EQ(export_safe_bitrate_bps(600'000u, 2), 500'000u);
    AUREA_CHECK_EQ(export_safe_bitrate_bps(300'000u, 1), 300'000u);
    // Os dois lados em múltiplo de 16, para baixo (nunca acima do teto do aparelho).
    const u32 cases[][5] = {
        {1920, 1080, 1080, 1920, 1072}, {1080, 1920, 1080, 1072, 1920}, {1920, 1080, 480, 848, 480},
        {1920, 1080, 720, 1280, 720},   {1080, 1350, 1080, 1072, 1344}, {1080, 1080, 1080, 1072, 1072},
        {3840, 2160, 2160, 3840, 2160}, {64, 36, 36, 64, 32},
    };
    for (const auto& c : cases) {
        const ExportFrameSize z = export_safe_frame_size(c[0], c[1], c[2]);
        AUREA_CHECK_EQ(z.width, c[3]);
        AUREA_CHECK_EQ(z.height, c[4]);
        const ExportFrameSize n = export_frame_size(c[0], c[1], c[2]);
        AUREA_CHECK(z.width <= n.width && z.height <= n.height);
    }
    AUREA_CHECK_EQ(export_safe_frame_size(0, 1080, 480).width, 0u);
}

/// Encoder que entrega todos os quadros mas engole a marca de fim não pode
/// custar o arquivo; um que perdeu quadros, sim (nada de MP4 incompleto).
AUREA_TEST(ExportWatchdogRules, StreamEndsWithoutEosOnlyWhenEverythingCameOut) {
    using namespace aurea;
    AUREA_CHECK(!export_video_complete_without_eos(0, 0));
    AUREA_CHECK(!export_video_complete_without_eos(300, 299));
    AUREA_CHECK(export_video_complete_without_eos(300, 300));
    AUREA_CHECK(export_audio_complete_without_eos(-1, 0));
    AUREA_CHECK(export_audio_complete_without_eos(10'000'000, 9'800'000));
    AUREA_CHECK(!export_audio_complete_without_eos(10'000'000, 9'700'000));
}
