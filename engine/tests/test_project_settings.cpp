// =============================================================================
//  Ajustes do projeto: fps livre (1–240, frações NTSC exatas) e a cor de fundo
//  da composição — tempo, histórico, gravação, prévia e export.
//
//  Pedido de beta: "coloca fps ilimitado e escolher a cor do projeto".
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/timeline/Composition.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace aurea;

namespace {

EngineConfig headless() {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

Composition* current(Engine& e) {
    return e.project()->timeline().composition(e.project()->timeline().current());
}

Status set_fps(Engine& e, f64 fps) {
    Command c;
    c.type = CommandType::CompositionSetFps;
    c.comp_fps.comp = e.project()->timeline().current();
    c.comp_fps.fps = fps;
    return e.apply_command(c);
}

Status set_background(Engine& e, f32 r, f32 g, f32 b) {
    Command c;
    c.type = CommandType::CompositionSetBackground;
    c.comp_background.comp = e.project()->timeline().current();
    c.comp_background.r = r; c.comp_background.g = g; c.comp_background.b = b; c.comp_background.a = 1.0f;
    return e.apply_command(c);
}

Status undo(Engine& e) {
    Command c;
    c.type = CommandType::Undo;
    return e.apply_command(c);
}

constexpr f64 kNtsc30 = 30000.0 / 1001.0;
constexpr f64 kNtsc24 = 24000.0 / 1001.0;
constexpr f64 kNtsc60 = 60000.0 / 1001.0;

} // namespace

// -----------------------------------------------------------------------------
// Tempo com fps fracionário
// -----------------------------------------------------------------------------
AUREA_TEST(ProjectFps, NormalizeSnapsNtscClampsAndKeepsFreeValues) {
    AUREA_CHECK_EQ(normalize_fps(29.97), kNtsc30);
    AUREA_CHECK_EQ(normalize_fps(static_cast<f64>(29.97f)), kNtsc30);   // o Android passa float
    AUREA_CHECK_EQ(normalize_fps(23.976), kNtsc24);
    AUREA_CHECK_EQ(normalize_fps(59.94), kNtsc60);
    AUREA_CHECK_EQ(normalize_fps(119.88), 120000.0 / 1001.0);
    AUREA_CHECK_EQ(normalize_fps(144.0), 144.0);
    AUREA_CHECK_EQ(normalize_fps(37.5), 37.5);                           // livre de verdade
    AUREA_CHECK_EQ(normalize_fps(static_cast<f64>(144.0f)), 144.0);
    AUREA_CHECK_EQ(normalize_fps(1000.0), kMaxCompositionFps);
    AUREA_CHECK_EQ(normalize_fps(0.25), kMinCompositionFps);
    AUREA_CHECK_EQ(normalize_fps(0.0), 30.0);
    AUREA_CHECK_EQ(normalize_fps(std::nan("")), 30.0);
    AUREA_CHECK_EQ(normalize_fps(-24.0), 30.0);
}

AUREA_TEST(ProjectFps, FrameTimeRoundTripAtFractionalRates) {
    for (const f64 fps : {kNtsc24, kNtsc30, kNtsc60, 120000.0 / 1001.0, 144.0, 37.5, 1.0, 240.0}) {
        bool ok = true;
        // Uma hora inteira em passos primos + o começo quadro a quadro.
        for (i64 f = 0; f < 2000 && ok; ++f) ok = frame_at(tick_at(FrameIndex{f}, fps), fps).value == f;
        const i64 hour = static_cast<i64>(std::ceil(3600.0 * fps));
        for (i64 f = 0; f <= hour && ok; f += 997) ok = frame_at(tick_at(FrameIndex{f}, fps), fps).value == f;
        AUREA_CHECK(ok);
        if (!ok) std::printf("fps %.6f quebrou o vai-e-volta ", fps);
    }
    // 29,97 exato: 30 000 quadros = 1 001 s, sem deriva.
    AUREA_CHECK_EQ(tick_at(FrameIndex{30000}, kNtsc30).value, static_cast<i64>(1001'000'000'000ll));
    AUREA_CHECK_EQ(frame_at(TickNs{1001'000'000'000ll}, kNtsc30).value, static_cast<i64>(30000));
}

AUREA_TEST(ProjectFps, CommandAcceptsAnyRateInRangeAndUndoes) {
    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    // A folha "Novo projeto" digita 29,97; o motor guarda a razão exata.
    AUREA_CHECK(e.new_project(320, 180, 29.97, nullptr).ok());
    AUREA_CHECK_EQ(current(e)->fps(), kNtsc30);
    AUREA_CHECK_EQ(current(e)->duration().value, static_cast<i64>(300));   // 10 s iniciais
    AUREA_CHECK_EQ(e.project()->export_settings().fps, kNtsc30);

    // Camada de 0 a 3 s e uma chave em 2 s, na taxa de agora.
    const LayerId id = current(e)->add_layer(LayerKind::Shape, "caixa");
    Layer* l = current(e)->layer(id);
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    l->start = FrameIndex{0};
    l->end = FrameIndex{90};
    l->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{60}, 0.5f, Interpolation::Linear);

    for (const f64 bad : {0.5, 0.0, 240.5, 1000.0, -30.0}) AUREA_CHECK(!set_fps(e, bad).ok());
    AUREA_CHECK_EQ(current(e)->fps(), kNtsc30);

    // 23,976: tempos ficam no mesmo SEGUNDO (a chave de 2 s cai no quadro 48).
    AUREA_CHECK(set_fps(e, 23.976).ok());
    AUREA_CHECK_EQ(current(e)->fps(), kNtsc24);
    AUREA_CHECK_NEAR(static_cast<f64>(e.read_status().compFps), kNtsc24, 1e-5);
    l = current(e)->layer(id);
    AUREA_CHECK_EQ(l->end.value, static_cast<i64>(72));
    const Track* key = l->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(key && key->keys.size() == 1 && key->keys[0].time.value == 48);

    // Presets altos e valores livres.
    for (const f64 ok : {144.0, 240.0, 1.0, 37.5, 90.0}) {
        AUREA_CHECK(set_fps(e, ok).ok());
        AUREA_CHECK_EQ(current(e)->fps(), ok);
    }
    // Desfazer volta a taxa e os quadros, passo a passo.
    for (int i = 0; i < 5; ++i) AUREA_CHECK(undo(e).ok());
    AUREA_CHECK_EQ(current(e)->fps(), kNtsc24);
    AUREA_CHECK(undo(e).ok());
    AUREA_CHECK_EQ(current(e)->fps(), kNtsc30);
    AUREA_CHECK_EQ(current(e)->layer(id)->end.value, static_cast<i64>(90));
    e.shutdown();
}

// -----------------------------------------------------------------------------
// Cor de fundo: histórico e gravação
// -----------------------------------------------------------------------------
AUREA_TEST(ProjectBackground, NewProjectColorCommandUndoAndSaveRoundTrip) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::absolute("build/prompt03/project-settings");
    fs::create_directories(dir);
    const std::string path = (dir / "fundo.aurea").string();

    Engine e;
    AUREA_CHECK(e.initialize(headless()).ok());
    // Projeto antigo / sem cor: preto opaco, como sempre.
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    AUREA_CHECK_EQ(current(e)->background().r, 0.0f);
    AUREA_CHECK_EQ(current(e)->background().a, 1.0f);

    // A cor da folha "Novo projeto" nasce fora do histórico.
    const f32 teal[4] = {0.1f, 0.6f, 0.55f, 1.0f};
    AUREA_CHECK(e.new_project(320, 180, kNtsc30, "fundo", teal).ok());
    AUREA_CHECK_NEAR(current(e)->background().g, 0.6f, 1e-6f);
    AUREA_CHECK(!e.history().can_undo());

    // Trocar a cor é desfazível.
    AUREA_CHECK(set_background(e, 1.0f, 0.5f, 0.0f).ok());
    AUREA_CHECK_NEAR(current(e)->background().r, 1.0f, 1e-6f);
    AUREA_CHECK(undo(e).ok());
    AUREA_CHECK_NEAR(current(e)->background().g, 0.6f, 1e-6f);
    AUREA_CHECK(set_background(e, 0.25f, 0.5f, 0.75f).ok());

    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    AUREA_CHECK_EQ(current(e)->background().b, 0.0f);
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    const Color bg = current(e)->background();
    AUREA_CHECK_NEAR(bg.r, 0.25f, 1e-6f);
    AUREA_CHECK_NEAR(bg.g, 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(bg.b, 0.75f, 1e-6f);
    AUREA_CHECK_EQ(bg.a, 1.0f);
    AUREA_CHECK(!current(e)->transparent_background());
    AUREA_CHECK_EQ(current(e)->fps(), kNtsc30);   // a fração volta exata

    u64 comp = 0; u32 w = 0, h = 0; f64 fps = 0; i64 dur = 0; f32 q[4]{};
    AUREA_CHECK(e.query_composition(comp, w, h, fps, dur, q));
    AUREA_CHECK_NEAR(q[2], 0.75f, 1e-6f);
    AUREA_CHECK_EQ(fps, kNtsc30);
    e.shutdown();
}

// -----------------------------------------------------------------------------
// GPU: a cor aparece na prévia e no export; o export conta os quadros certos
// -----------------------------------------------------------------------------
#if defined(AUREA_TEST_VULKAN)
#if defined(AUREA_TEST_GLES)
#include "GlesBackend.hpp"
namespace aurea { namespace vk = gles; }
#else
#include "VulkanBackend.hpp"
#endif
#include "aurea/export/ExportSink.hpp"

#include <chrono>
#include <thread>

namespace {

bool gpu_available() {
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

struct Capture {
    VideoStreamConfig video{};
    std::vector<i64> pts;
    std::vector<u8> firstY;   ///< linha do meio do 1º quadro (luma)
    bool finished = false;
};

class CountingSink final : public ExportSink {
public:
    explicit CountingSink(Capture* c) : c_(c) {}
    Status open(const char*, const VideoStreamConfig& v, const AudioStreamConfig*) noexcept override {
        c_->video = v;
        return OkStatus;
    }
    Status write_video(const u8* y, u32 yStride, const u8*, u32, i64 ptsUs) noexcept override {
        if (c_->firstY.empty()) {
            const u8* row = y + static_cast<usize>(c_->video.height / 2) * yStride;
            c_->firstY.assign(row, row + c_->video.width);
        }
        c_->pts.push_back(ptsUs);
        return OkStatus;
    }
    Status write_audio(const i16*, u32, i64) noexcept override { return OkStatus; }
    Status finish() noexcept override { c_->finished = true; return OkStatus; }
    void abort() noexcept override {}
private:
    Capture* c_;
};

std::unique_ptr<ExportSink> make_counting_sink(void* user) {
    return std::make_unique<CountingSink>(static_cast<Capture*>(user));
}

struct GpuRig {
    Capture cap;
    Engine e;
    bool ok = false;
    GpuRig() {
        EngineConfig ec = headless();
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.exportSinkFactory = &make_counting_sink;
        ec.exportSinkContext = &cap;
        ok = e.initialize(ec).ok();
    }
    ~GpuRig() { e.shutdown(); }

    /// Exporta e espera; devolve o resultado.
    Errc export_all(f64 fps, u32 height) {
        ExportSettings s;
        s.height = height;
        s.fps = fps;
        s.dither = false;
        s.trimToContent = false;
        if (!e.start_export(s, "nao-usado.mp4").ok()) return Errc::InvalidState;
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            const Engine::ExportProgress p = e.export_progress();
            if (p.finished) return p.result;
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) {
                (void)e.cancel_export();
                return Errc::Timeout;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};

i64 expected_pts(i64 i, f64 fps) { return static_cast<i64>(std::llround(static_cast<f64>(i) * 1e6 / fps)); }

} // namespace

AUREA_TEST(ProjectBackground, ClearColorShowsInPreviewAndExport) {
    if (!gpu_available()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    GpuRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    // Quadro vazio: só o fundo. sRGB (0.2, 0.6, 0.9) → 51, 153, 230.
    const f32 blue[4] = {0.2f, 0.6f, 0.9f, 1.0f};
    AUREA_CHECK(r.e.new_project(64, 36, 30.0, nullptr, blue).ok());
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    AUREA_CHECK(r.e.capture_frame_rgba(64, rgba, w, h).ok());
    AUREA_CHECK(w == 64 && h == 36 && rgba.size() == static_cast<usize>(w) * h * 4);
    if (rgba.size() < static_cast<usize>(w) * h * 4 || w == 0) return;
    const usize mid = (static_cast<usize>(h / 2) * w + w / 2) * 4;
    AUREA_CHECK(std::abs(static_cast<int>(rgba[mid + 0]) - 51) <= 3);
    AUREA_CHECK(std::abs(static_cast<int>(rgba[mid + 1]) - 153) <= 3);
    AUREA_CHECK(std::abs(static_cast<int>(rgba[mid + 2]) - 230) <= 3);
    AUREA_CHECK_EQ(static_cast<int>(rgba[mid + 3]), 255);

    // Trocar pelo comando aparece no próximo quadro.
    AUREA_CHECK(set_background(r.e, 1.0f, 0.5f, 0.0f).ok());
    AUREA_CHECK(r.e.capture_frame_rgba(64, rgba, w, h).ok());
    AUREA_CHECK(rgba[mid + 0] >= 252 && std::abs(static_cast<int>(rgba[mid + 1]) - 128) <= 3 && rgba[mid + 2] <= 3);

    // Export: o mesmo fundo vira luma BT.709 (faixa limitada) ≈ 16 + 219·Y'.
    Command d;
    d.type = CommandType::CompositionSetDuration;
    d.comp_duration.comp = r.e.project()->timeline().current();
    d.comp_duration.duration = FrameIndex{3};
    AUREA_CHECK(r.e.apply_command(d).ok());
    AUREA_CHECK_EQ(r.export_all(0.0, 36), Errc::Ok);
    AUREA_CHECK(!r.cap.firstY.empty());
    if (!r.cap.firstY.empty()) {
        const int y = r.cap.firstY[r.cap.firstY.size() / 2];
        const int expect = static_cast<int>(std::lround(16.0 + 219.0 * (0.2126 * 1.0 + 0.7152 * (128.0 / 255.0))));
        AUREA_CHECK(std::abs(y - expect) <= 8);
        if (std::abs(y - expect) > 8) std::printf("luma %d, esperado ~%d ", y, expect);
    }
}

AUREA_TEST(ProjectFps, ExportFrameCountAndTimestampsAtFractionalRates) {
    if (!gpu_available()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    GpuRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    AUREA_CHECK(r.e.new_project(64, 36, 30.0, nullptr).ok());
    Command d;
    d.type = CommandType::CompositionSetDuration;
    d.comp_duration.comp = r.e.project()->timeline().current();
    d.comp_duration.duration = FrameIndex{300};   // 10 s a 30 fps
    AUREA_CHECK(r.e.apply_command(d).ok());

    // 10 s saindo a 29,97: ⌈10 · 30000/1001⌉ = 300 quadros, carimbos exatos.
    AUREA_CHECK_EQ(r.export_all(29.97, 36), Errc::Ok);
    AUREA_CHECK_EQ(r.cap.video.fps, kNtsc30);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(300));
    bool exact = true;
    for (usize i = 0; i < r.cap.pts.size(); ++i) exact = exact && r.cap.pts[i] == expected_pts(static_cast<i64>(i), kNtsc30);
    AUREA_CHECK(exact);
    if (!r.cap.pts.empty()) AUREA_CHECK_EQ(r.cap.pts.back(), static_cast<i64>(9'976'633));
    AUREA_CHECK(r.cap.finished);

    // Composição a 23,976 exportando na própria taxa: 10 s = 240 quadros.
    r.cap = Capture{};
    AUREA_CHECK(set_fps(r.e, 23.976).ok());
    AUREA_CHECK_EQ(r.e.project()->timeline().composition(r.e.project()->timeline().current())->duration().value,
                   static_cast<i64>(240));
    AUREA_CHECK_EQ(r.export_all(0.0, 36), Errc::Ok);
    AUREA_CHECK_EQ(r.cap.video.fps, kNtsc24);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(240));
    if (r.cap.pts.size() == 240) AUREA_CHECK_EQ(r.cap.pts[239], expected_pts(239, kNtsc24));

    // 144 fps livre sobre os 240 quadros de 23,976 (10,01 s): ⌈10,01 · 144⌉ =
    // 1 442 quadros. Acima do teto do encoder o motor recusa.
    r.cap = Capture{};
    AUREA_CHECK_EQ(r.export_all(144.0, 36), Errc::Ok);
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(std::ceil(240.0 / kNtsc24 * 144.0 - 1e-6)));
    AUREA_CHECK_EQ(r.cap.pts.size(), static_cast<usize>(1442));
    ExportSettings tooFast;
    tooFast.height = 36;
    tooFast.fps = 300.0;
    const Status refused = r.e.start_export(tooFast, "nao-usado.mp4");
    AUREA_CHECK(!refused.ok());
    AUREA_CHECK_EQ(refused.code(), Errc::NotSupported);
}
#endif // AUREA_TEST_VULKAN
