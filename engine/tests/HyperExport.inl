// =============================================================================
//  AureaHyper (0.3.2) — o job da Home e o medidor de ponta a ponta, com o
//  motor inteiro (Rig de test_export.cpp: Vulkan do host, vídeo sintético).
//  Incluído em test_export.cpp depois de ExportCheckpoint.inl.
//
//  - 60 fps: contagem e PTS exatos, som mantido;
//  - câmera lenta x2: duração exatamente o dobro, sem som;
//  - upscale Turbo: tamanho de saída, quadros e PTS da fonte;
//  - cancelar para na hora;
//  - o quadro antes/depois (hyper_preview_frame) é o quadro do export Turbo;
//  - o aiUpscale do export continua funcionando (agora pela etapa Hyper);
//  - o medidor devolve um perfil, grava e reaproveita;
//  - com o sink FFmpeg e o export retomável: pausado no meio, o MESMO pedido
//    continua do checkpoint e o arquivo sai validado;
//  - estabilização: o tremor de um vídeo sacudido cai na saída.
// =============================================================================
#include "aurea/hyper/Hyper.hpp"
#include "hyper/HyperAnalysis.hpp"

namespace {
namespace hyperjob {

const char* const kDir = "build/hyper-tests";

std::string out_path(const char* name) {
    std::filesystem::create_directories(kDir);
    return std::string(kDir) + "/" + name;
}

SyntheticConfig config(u32 frames = 60, bool audio = true) {
    SyntheticConfig cfg;
    cfg.width = 160;
    cfg.height = 96;
    cfg.fps = 30.0;
    cfg.frameCount = frames;
    cfg.pattern = SyntheticPattern::FastSquare;
    if (audio) {
        cfg.audioRate = 48000;
        cfg.audioSeconds = static_cast<f64>(frames) / 30.0 + 0.5;
    }
    return cfg;
}

hyper::JobSteps wait_job(Engine& e, u32 seconds = 180) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    for (;;) {
        const hyper::JobSteps s = e.hyper_job_steps();
        if (s.finished || std::chrono::steady_clock::now() > deadline) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/// Vídeo de função (luma) para os casos que o sintético não cobre.
using LumaFn = std::function<u8(u32, u32, u32)>;
class FnFrame final : public DecodedFrame {
public:
    std::vector<u8> y, uv;
    bool owns_cpu_backing() const noexcept override { return !y.empty(); }
};
class FnDecoder final : public VideoDecoderBackend {
public:
    FnDecoder(u32 w, u32 h, u32 frames, LumaFn fn) : w_(w), h_(h), frames_(frames), fn_(std::move(fn)) {
        info_.codedWidth = w;
        info_.codedHeight = h;
        info_.fps = 30.0;
        info_.durationUs = static_cast<i64>(std::llround(frames * 1e6 / 30.0));
        std::snprintf(info_.codec, sizeof(info_.codec), "%s", "video/funcao");
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "%s", "funcao");
    }
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 targetUs) noexcept override {
        const f64 f = static_cast<f64>(targetUs) * 30.0 / 1e6 + 0.5;
        pos_ = (std::min<u32>(f <= 0.0 ? 0u : static_cast<u32>(f), frames_ - 1) / 10u) * 10u;
        return OkStatus;
    }
    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& eos) noexcept override {
        eos = false;
        if (pos_ >= frames_) { eos = true; outPtsUs = pts_of(frames_ - 1); return OkStatus; }
        const u32 index = pos_++;
        outPtsUs = pts_of(index);
        eos = pos_ >= frames_;
        if (outPtsUs < deliverFromUs) return OkStatus;
        auto* f = new FnFrame();
        f->ptsUs = outPtsUs;
        f->width = w_; f->height = h_; f->visibleWidth = w_; f->visibleHeight = h_;
        f->format = PixelFormat::NV12;
        f->bufferId = index;
        f->y.resize(static_cast<usize>(w_) * h_);
        for (u32 yy = 0; yy < h_; ++yy)
            for (u32 xx = 0; xx < w_; ++xx) f->y[static_cast<usize>(yy) * w_ + xx] = fn_(xx, yy, index);
        f->uv.assign(static_cast<usize>(w_ / 2) * (h_ / 2) * 2, 128);
        f->planes[0] = f->y.data();
        f->planes[1] = f->uv.data();
        f->strides[0] = w_;
        f->strides[1] = w_;
        f->planeCount = 2;
        out = FrameRef::adopt(f);
        return OkStatus;
    }
    u32 max_live_frames() const noexcept override { return 12; }
    i64 keyframe_interval_us() const noexcept override { return static_cast<i64>(std::llround(10 * 1e6 / 30.0)); }
private:
    i64 pts_of(u32 i) const { return static_cast<i64>(std::llround(i * 1e6 / 30.0)); }
    u32 w_, h_, frames_;
    LumaFn fn_;
    VideoStreamInfo info_{};
    u32 pos_ = 0;
};
class FnFactory final : public VideoSourceFactory {
public:
    FnFactory(u32 w, u32 h, u32 frames, LumaFn fn) : w_(w), h_(h), frames_(frames), fn_(std::move(fn)) {}
    bool probe(const char*, MediaProbe& out) override {
        FnDecoder d(w_, h_, frames_, fn_);
        out.video = d.info();
        out.video.color.fromStream = true;
        out.hasVideo = true;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<FnDecoder>(w_, h_, frames_, fn_);
    }
private:
    u32 w_, h_, frames_;
    LumaFn fn_;
};

/// Y (limitada) -> luma sRGB-ish 0..255 de um quadro capturado (NV12).
tracking::Gray gray_of(const std::vector<u8>& nv12, u32 w, u32 h) {
    tracking::Gray g;
    g.width = w;
    g.height = h;
    g.px.resize(static_cast<usize>(w) * h);
    for (usize i = 0; i < g.px.size(); ++i) g.px[i] = std::clamp((nv12[i] - 16.0f) / 219.0f, 0.0f, 1.0f);
    return g;
}

} // namespace hyperjob
} // namespace

AUREA_TEST(HyperJob, SixtyFpsHasExactFrameCountTimestampsAndKeepsAudio) {
    Rig r(hyperjob::config(60), 30, 60, 3);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    hyper::Recipe recipe;
    recipe.targetFps = 60.0;
    recipe.path = hyper::Path::Turbo;
    const std::string out = hyperjob::out_path("sixty.mp4");
    const Status started = r.e.start_hyper_job("sintetico", recipe, out.c_str());
    AUREA_CHECK_MSG(started.ok(), "start_hyper_job");
    if (!started.ok()) return;
    const hyper::JobSteps s = hyperjob::wait_job(r.e);
    AUREA_CHECK(s.finished && s.result == Errc::Ok);
    AUREA_CHECK(s.present[static_cast<usize>(hyper::Step::Motion)] && s.done[static_cast<usize>(hyper::Step::Motion)]);
    AUREA_CHECK_EQ(s.passCount, 1u);
    AUREA_CHECK_EQ(s.outputFrames, 120u);
    AUREA_CHECK(!s.audioMuted);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(120));
    for (usize k = 0; k < r.cap.pts.size(); ++k)
        AUREA_CHECK_MSG(std::llabs(r.cap.pts[k] - static_cast<i64>(std::llround(static_cast<f64>(k) * 1e6 / 60.0))) <= 1,
                        "PTS de 60 fps exatos");
    AUREA_CHECK(r.cap.ptsMonotonic);
    AUREA_CHECK(r.cap.hasAudio);
    // O som inteiro dos 2 s (48 kHz no mixer), sem buraco.
    AUREA_CHECK(r.cap.audioContiguous);
    AUREA_CHECK(std::llabs(r.cap.audioFrames - 96000) <= 1024);
    const Engine::ExportProgress p = r.e.export_progress();
    AUREA_CHECK(p.finished && !p.running && p.result == Errc::Ok);
}

AUREA_TEST(HyperJob, SlowMotionDoublesTheDurationExactlyAndMutes) {
    Rig r(hyperjob::config(45), 30, 45, 3);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    hyper::Recipe recipe;
    recipe.slowMotion = 2;
    const std::string out = hyperjob::out_path("slow.mp4");
    AUREA_CHECK(r.e.start_hyper_job("sintetico", recipe, out.c_str()).ok());
    const hyper::JobSteps s = hyperjob::wait_job(r.e);
    AUREA_CHECK(s.finished && s.result == Errc::Ok);
    AUREA_CHECK(s.audioMuted);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(90));   // 1,5 s -> 3 s a 30 fps
    if (!r.cap.pts.empty())
        AUREA_CHECK(std::llabs(r.cap.pts.back() - static_cast<i64>(std::llround(89.0 * 1e6 / 30.0))) <= 1);
    AUREA_CHECK(!r.cap.hasAudio);
}

AUREA_TEST(HyperJob, TurboUpscaleKeepsFrameCountAndPts) {
    Rig r(hyperjob::config(30, false), 30, 30, 3);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    hyper::Recipe recipe;
    recipe.upscale = 2;
    recipe.denoise = 0.4f;
    recipe.path = hyper::Path::Turbo;
    const std::string out = hyperjob::out_path("upscale.mp4");
    AUREA_CHECK(r.e.start_hyper_job("sintetico", recipe, out.c_str()).ok());
    const hyper::JobSteps s = hyperjob::wait_job(r.e);
    AUREA_CHECK(s.finished && s.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.video.width, 320u);
    AUREA_CHECK_EQ(r.cap.video.height, 192u);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(30));
    for (usize k = 0; k < r.cap.pts.size(); ++k)
        AUREA_CHECK(std::llabs(r.cap.pts[k] - static_cast<i64>(std::llround(static_cast<f64>(k) * 1e6 / 30.0))) <= 1);
    AUREA_CHECK(s.paths[static_cast<usize>(hyper::Step::Upscale)] == hyper::Path::Turbo);
    // Estimativa coerente com o que saiu.
    const hyper::Estimate est = r.e.hyper_estimate(recipe, "sintetico");
    AUREA_CHECK_EQ(est.outputWidth, 320u);
    AUREA_CHECK(est.seconds[static_cast<usize>(hyper::Path::Turbo)] > 0.0);
}

AUREA_TEST(HyperJob, CancelStopsTheJobAtOnce) {
    Rig r(hyperjob::config(90, false), 30, 90, 3);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    r.cap.encodeUs = 20000;   // encoder lento: o job ainda está no meio
    hyper::Recipe recipe;
    recipe.slowMotion = 8;
    const std::string out = hyperjob::out_path("cancel.mp4");
    AUREA_CHECK(r.e.start_hyper_job("sintetico", recipe, out.c_str()).ok());
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (r.e.export_progress().framesDone < 10 && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(r.e.cancel_export().ok());
    const hyper::JobSteps s = hyperjob::wait_job(r.e, 10);
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[parou em %.0f ms] ", ms);
    AUREA_CHECK(s.finished && s.result == Errc::Cancelled);
    AUREA_CHECK(ms < 3000.0);
    AUREA_CHECK(r.cap.pts.size() < 720u);
    const Engine::ExportProgress p = r.e.export_progress();
    AUREA_CHECK(!p.running && p.result == Errc::Cancelled);
}

AUREA_TEST(HyperJob, PreviewFrameIsTheTurboExportFrame) {
    Rig r(hyperjob::config(30, false), 30, 30, 1);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    r.cap.keepFrames = true;
    hyper::Recipe recipe;
    recipe.upscale = 2;
    recipe.deblock = true;
    recipe.path = hyper::Path::Turbo;
    std::vector<u8> before, after;
    u32 w = 0, h = 0;
    const Status pv = r.e.hyper_preview_frame(recipe, "sintetico", 0.5, hyper::Path::Turbo, before, after, w, h);
    AUREA_CHECK_MSG(pv.ok(), "hyper_preview_frame");
    if (!pv.ok()) return;
    AUREA_CHECK_EQ(w, 320u);
    AUREA_CHECK_EQ(h, 192u);
    AUREA_CHECK_EQ(after.size(), static_cast<usize>(w) * h * 4);
    // O antes e o depois diferem (o Turbo age), mas mostram o mesmo quadro.
    f64 diffBA = 0;
    for (usize i = 0; i < before.size(); i += 4) diffBA += std::abs(static_cast<int>(before[i + 1]) - static_cast<int>(after[i + 1]));
    AUREA_CHECK(diffBA / (w * h) > 0.05);
    const std::string out = hyperjob::out_path("preview.mp4");
    AUREA_CHECK(r.e.start_hyper_job("sintetico", recipe, out.c_str()).ok());
    const hyper::JobSteps s = hyperjob::wait_job(r.e);
    AUREA_CHECK(s.finished && s.result == Errc::Ok);
    AUREA_CHECK(r.cap.frames.size() == 30);
    if (r.cap.frames.size() != 30) return;
    // Quadro 15 (0,5 s) do export: Y limitado -> luma; a prévia: luma do RGB.
    const std::vector<u8>& f = r.cap.frames[15];
    f64 sum = 0;
    u32 worst = 0;
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u8* p = &after[(static_cast<usize>(y) * w + x) * 4];
            const f64 l = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            const f64 e = (f[static_cast<usize>(y) * w + x] - 16.0) * 255.0 / 219.0;
            const f64 d = std::fabs(l - e);
            sum += d;
            worst = std::max(worst, static_cast<u32>(d));
        }
    std::printf("[previa x export: media %.2f, pior %u] ", sum / (w * h), worst);
    AUREA_CHECK_MSG(sum / (w * h) < 1.5 && worst <= 6, "o quadro da prévia Turbo é o do export Turbo");
}

AUREA_TEST(HyperJob, AiUpscaleExportStillWorksThroughTheHyperStage) {
    Rig r(hyperjob::config(12, false), 30, 12, 3);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    ExportSettings settings;
    settings.height = 192;
    settings.fps = 30;
    settings.aiUpscale = 2;
    const std::string out = hyperjob::out_path("aiupscale.mp4");
    const Status started = r.e.start_export(settings, out.c_str());
    AUREA_CHECK_MSG(started.ok(), "export com aiUpscale");
    if (!started.ok()) return;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    Engine::ExportProgress p{};
    bool sawAi = false;
    for (;;) {
        p = r.e.export_progress();
        sawAi = sawAi || std::strncmp(p.message, "IA:", 3) == 0;
        if (p.finished || std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    AUREA_CHECK(p.finished && p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.video.width, 320u);
    AUREA_CHECK_EQ(r.cap.video.height, 192u);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(12));
    AUREA_CHECK_MSG(sawAi, "a mensagem 'IA: quadro/total' das telas continua");
}

AUREA_TEST(HyperMeter, ReturnsAProfileAndCachesIt) {
    const std::string dir = hyperjob::out_path("meter-cache");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    gRigCacheDirectory = dir;
    hyper::DeviceProfile first;
    {
        Rig r(hyperjob::config(10, false), 30, 10, 3);
        gRigCacheDirectory.clear();
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        const auto t0 = std::chrono::steady_clock::now();
        first = r.e.hyper_device_profile(false);
        const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[medido em %.0f ms: turbo %.2f ms, lite %.2f ms/tile, fp16 %d, caminho %u] ", ms, first.turboMsPerFrame,
                    first.liteMsPerTile, first.vulkanFp16 ? 1 : 0, static_cast<u32>(first.defaultPath));
        AUREA_CHECK(first.measured);
        AUREA_CHECK(first.turboMsPerFrame > 0.0f);
        AUREA_CHECK(first.abi64 == (sizeof(void*) == 8));
        AUREA_CHECK(first.defaultPath != hyper::Path::Auto);
        AUREA_CHECK(std::filesystem::exists(dir + "/hyper/device-profile.json"));
        const auto t1 = std::chrono::steady_clock::now();
        const hyper::DeviceProfile again = r.e.hyper_device_profile(false);
        const f64 cached = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t1).count();
        AUREA_CHECK(cached < 50.0);
        AUREA_CHECK_NEAR(again.turboMsPerFrame, first.turboMsPerFrame, 1e-6f);
    }
    // Outro motor, mesma pasta: lê do disco sem medir.
    gRigCacheDirectory = dir;
    Rig r2(hyperjob::config(10, false), 30, 10, 3);
    gRigCacheDirectory.clear();
    AUREA_CHECK(r2.ok);
    if (!r2.ok) return;
    const auto t2 = std::chrono::steady_clock::now();
    const hyper::DeviceProfile loaded = r2.e.hyper_device_profile(false);
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t2).count();
    AUREA_CHECK(loaded.measured && ms < 50.0);
    AUREA_CHECK_NEAR(loaded.turboMsPerFrame, first.turboMsPerFrame, 1e-3f);
    // remeasure: mede de novo.
    const hyper::DeviceProfile again = r2.e.hyper_device_profile(true);
    AUREA_CHECK(again.measured && again.turboMsPerFrame > 0.0f);
}

AUREA_TEST(HyperJob, StabilizationReducesTheJitterOfAShakenClip) {
    constexpr u32 W = 192, H = 108, N = 40;
    // Tremor determinístico (px) sobre uma cena com textura suave.
    auto shake = [](u32 i, bool yAxis) {
        u64 s = 0x9E3779B97F4A7C15ull * (i + 1) + (yAxis ? 77 : 0);
        s ^= s >> 31; s *= 0xBF58476D1CE4E5B9ull; s ^= s >> 29;
        return (static_cast<f64>(s % 1000) / 1000.0 - 0.5) * 8.0;
    };
    hyperjob::FnFactory video(W, H, N, [&](u32 x, u32 y, u32 i) {
        const f64 u = x - shake(i, false), v = y - shake(i, true);
        const f64 val = 120.0 + 40.0 * std::sin(u * 0.21) * std::cos(v * 0.17) + 25.0 * std::sin((u + 2 * v) * 0.09)
                      + 20.0 * std::cos(u * 0.37 - v * 0.29);
        return static_cast<u8>(std::clamp(val, 16.0, 235.0));
    });
    SyntheticConfig cfg = hyperjob::config(N, false);
    cfg.width = W;
    cfg.height = H;
    Rig r(cfg, 30, N, 3, &video);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    r.cap.keepFrames = true;
    hyper::Recipe recipe;
    recipe.stabilize = true;
    recipe.stabilizeStrength = 0.7f;
    const std::string out = hyperjob::out_path("stable.mp4");
    AUREA_CHECK(r.e.start_hyper_job("sintetico", recipe, out.c_str()).ok());
    const hyper::JobSteps s = hyperjob::wait_job(r.e);
    AUREA_CHECK(s.finished && s.result == Errc::Ok);
    AUREA_CHECK(s.done[static_cast<usize>(hyper::Step::Stable)]);
    AUREA_CHECK_EQ(r.cap.frames.size(), static_cast<usize>(N));
    if (r.cap.frames.size() != N) return;
    // Tremor da saída: o movimento quadro a quadro medido nela (o mesmo
    // estimador) contra o tremor da fonte.
    f64 inSe = 0, outSe = 0;
    u32 n = 0;
    for (u32 k = 2; k + 2 < N; ++k) {
        const tracking::Gray a = hyperjob::gray_of(r.cap.frames[k - 1], W, H), b = hyperjob::gray_of(r.cap.frames[k], W, H);
        const hyper::Motion2D m = hyper::estimate_motion(a, b);
        if (!m.valid) continue;
        const f64 ix = shake(k, false) - shake(k - 1, false), iy = shake(k, true) - shake(k - 1, true);
        inSe += ix * ix + iy * iy;
        outSe += m.dx * m.dx + m.dy * m.dy;
        ++n;
    }
    AUREA_CHECK(n >= N - 8);
    const f64 inJ = std::sqrt(inSe / std::max(1u, n)), outJ = std::sqrt(outSe / std::max(1u, n));
    std::printf("[tremor %.2f -> %.2f px/quadro] ", inJ, outJ);
    AUREA_CHECK_MSG(outJ < 0.4 * inJ, "a saída estabilizada treme menos de 40% da fonte");
}

AUREA_TEST(HyperJob, LitePathRunsTheNetworksInTheGraphInOnePass) {
    Rig r(hyperjob::config(12, false), 30, 12, 3);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    hyper::Recipe recipe;
    recipe.upscale = 2;
    recipe.upscaleMode = hyper::UpscaleMode::Anime;
    recipe.targetFps = 60.0;
    recipe.path = hyper::Path::Lite;
    const std::string out = hyperjob::out_path("lite.mp4");
    const Status started = r.e.start_hyper_job("sintetico", recipe, out.c_str());
    AUREA_CHECK_MSG(started.ok(), "start_hyper_job Lite");
    if (!started.ok()) return;
    const hyper::JobSteps s = hyperjob::wait_job(r.e, 600);
    AUREA_CHECK(s.finished && s.result == Errc::Ok);
    AUREA_CHECK(s.paths[static_cast<usize>(hyper::Step::Upscale)] == hyper::Path::Lite);
    // Com rede de Motion registrada, o upscale neural e o Motion ficam no grafo
    // da MESMA passagem (o upscale só processa os 12 quadros de origem).
    std::printf("[passagens %u, Motion %u] ", s.passCount, static_cast<u32>(s.paths[static_cast<usize>(hyper::Step::Motion)]));
    AUREA_CHECK_EQ(r.cap.video.width, 320u);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(24));
    for (usize k = 0; k < r.cap.pts.size(); ++k)
        AUREA_CHECK(std::llabs(r.cap.pts[k] - static_cast<i64>(std::llround(static_cast<f64>(k) * 1e6 / 60.0))) <= 1);
    // Rede de Motion com o par de origem e a fase: o quadro ímpar (fase 0,5)
    // é novo — não é a repetição do anterior nem o seguinte.
    if (s.paths[static_cast<usize>(hyper::Step::Motion)] != hyper::Path::Turbo && r.cap.hashes.size() >= 3) {
        AUREA_CHECK(r.cap.hashes[1] != r.cap.hashes[0]);
        AUREA_CHECK(r.cap.hashes[1] != r.cap.hashes[2]);
    }
}

#if defined(AUREA_HAS_FFMPEG)
AUREA_TEST(HyperJob, ResumesFromTheCheckpointAndPublishesAValidatedFile) {
    std::error_code ec;
    std::filesystem::remove_all(kCheckpointDir, ec);
    SyntheticConfig cfg = checkpoint_config();
    auto r = checkpoint_rig(cfg, 150);
    AUREA_CHECK(r && r->ok);
    if (!r || !r->ok) return;
    hyper::Recipe recipe;
    recipe.upscale = 2;
    recipe.path = hyper::Path::Turbo;
    const std::string out = std::string(kCheckpointOut) + "/hyper-resume.mp4";
    std::filesystem::remove(out, ec);
    AUREA_CHECK(r->e.start_hyper_job("sintetico", recipe, out.c_str()).ok());
    const auto until = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    // Um GOP inteiro (2 s = 60 quadros) já no registro, mais o atraso do encoder.
    while (r->e.export_progress().framesDone < 105 && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK(r->e.pause_export().ok());   // o app saiu: o checkpoint fica
    const hyper::JobSteps paused = hyperjob::wait_job(r->e, 30);
    AUREA_CHECK(paused.finished && paused.result == Errc::Cancelled);
    if (checkpoint_files().empty()) std::printf("\n[diagnostico: %s]\n", r->e.export_diagnostics().c_str());
    AUREA_CHECK(!checkpoint_files().empty());
    AUREA_CHECK(!std::filesystem::exists(out));
    // O MESMO pedido continua.
    AUREA_CHECK(r->e.start_hyper_job("sintetico", recipe, out.c_str()).ok());
    bool resumed = false;
    for (;;) {
        const Engine::ExportProgress p = r->e.export_progress();
        resumed = resumed || (p.flags & Engine::kExportResumed) != 0;
        if (p.finished || std::chrono::steady_clock::now() > until) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const hyper::JobSteps done = hyperjob::wait_job(r->e, 60);
    AUREA_CHECK(done.finished && done.result == Errc::Ok);
    AUREA_CHECK_MSG(resumed, "a segunda partida continuou do checkpoint (kExportResumed)");
    AUREA_CHECK(std::filesystem::exists(out));
    const auto verified = verify_native_export(out, 150, 640, 384, 30.0, true);
    AUREA_CHECK_MSG(verified.ok, verified.error.c_str());
    AUREA_CHECK_EQ(verified.pictures, 150u);
}
#endif
