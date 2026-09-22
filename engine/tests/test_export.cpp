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
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/project/Project.hpp"

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
    Status open(const char*, const VideoStreamConfig& v, const AudioStreamConfig* a) noexcept override {
        c_->video = v;
        c_->hasAudio = a != nullptr;
        if (a) c_->audio = *a;
        c_->opened = true;
        c_->input.resize(static_cast<usize>(v.width) * v.height * 3 / 2);
        return OkStatus;
    }
    Status write_video(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 ptsUs) noexcept override {
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
    Status finish() noexcept override { c_->finished = true; return OkStatus; }
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
    Rig(const SyntheticConfig& cfg, f64 compFps, i64 frames, u32 depth) : factory(cfg) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.mediaFactory = &factory;
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

Outcome run_export(Rig& r, u32 shortSide, f64 fps, bool dither = true, int timeoutS = 600) {
    Outcome o;
    ExportSettings s;
    s.height = shortSide;
    s.fps = fps;
    s.dither = dither;
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
    struct Case { const char* name; u32 w, h; f64 fps; i64 frames; bool fx; };
    const Case cases[] = {
        {"efeitos 1080p30", 1920, 1080, 30.0, 120, true},
        {"efeitos 4K30", 3840, 2160, 30.0, 60, true},
        {"3D 1080p30", 1920, 1080, 30.0, 120, false},
        {"3D 4K30", 3840, 2160, 30.0, 60, false},
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

#endif // AUREA_TEST_VULKAN
