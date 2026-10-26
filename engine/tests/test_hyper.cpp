// =============================================================================
//  AureaHyper (0.3.2) — partes de CPU: receita, plano/passagens, estimativa,
//  grafo (fila limitada, ordem, vizinhos, ajuste, cancelamento), detecção de
//  entrelaçado, estabilização e o arquivo do medidor. A GPU fica em
//  HyperGpu.inl (test_gpu.cpp) e o job de ponta a ponta em HyperExport.inl
//  (test_export.cpp).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/hyper/Hyper.hpp"
#include "aurea/hyper/HyperStage.hpp"
#include "hyper/HyperAnalysis.hpp"
#include "hyper/HyperGraph.hpp"
#include "hyper/HyperPlan.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

using namespace aurea;
using namespace aurea::hyper;

namespace {

u64 splitmix(u64& s) {
    u64 z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

SourceInfo source(u32 w, u32 h, f64 fps, f64 seconds, bool audio = true) {
    SourceInfo s;
    s.width = w;
    s.height = h;
    s.fps = fps;
    s.durationUs = static_cast<i64>(std::llround(seconds * 1e6));
    s.hasAudio = audio;
    return s;
}

DeviceProfile measured(Path def) {
    DeviceProfile p;
    p.measured = true;
    p.abi64 = true;
    p.vulkan = p.vulkanFp16 = true;
    p.freeRamBytes = 4ull << 30;
    p.turboMsPerFrame = 8.0f;
    p.liteMsPerTile = 40.0f;
    p.defaultPath = def;
    return p;
}

/// Etapa de teste: Y de saída = média do Y do atual e dos vizinhos (raio r);
/// guarda os índices dos vizinhos que viu (borda repetida).
class AverageStage final : public HyperStage {
public:
    explicit AverageStage(u32 r, std::vector<std::vector<u32>>* seen = nullptr) : r_(r), seen_(seen) {}
    const char* name() const noexcept override { return "teste.media"; }
    StageKind kind() const noexcept override { return StageKind::Clean; }
    Path path() const noexcept override { return Path::Lite; }
    u32 temporal_radius() const noexcept override { return r_; }
    Status prepare(const DeviceProfile&, const Recipe&, const StageFormat& in, StageFormat& out) noexcept override {
        out = in;
        return OkStatus;
    }
    Status process(const StageInput& in, FrameBuffer& out, const StageContext& ctx) noexcept override {
        if (ctx.cancelled()) return Errc::Cancelled;
        const u32 n = in.current.width * in.current.height;
        std::vector<u32> idx{in.current.index};
        for (u32 i = 0; i < n; ++i) {
            u32 sum = in.current.y[i];
            for (const FrameView& nb : in.neighbors) sum += nb.y[i];
            out.y()[i] = static_cast<u8>(sum / (1 + in.neighbors.size()));
        }
        for (const FrameView& nb : in.neighbors) idx.push_back(nb.index);
        std::memcpy(out.uv(), in.current.uv, n / 2);
        if (seen_) seen_->push_back(idx);
        return OkStatus;
    }
private:
    u32 r_;
    std::vector<std::vector<u32>>* seen_;
};

/// Etapa "×2": dobra o tamanho (vizinho mais próximo).
class DoubleStage final : public HyperStage {
public:
    const char* name() const noexcept override { return "teste.x2"; }
    StageKind kind() const noexcept override { return StageKind::Upscale; }
    Path path() const noexcept override { return Path::Max; }
    Status prepare(const DeviceProfile&, const Recipe&, const StageFormat& in, StageFormat& out) noexcept override {
        out = in;
        out.width *= 2;
        out.height *= 2;
        return OkStatus;
    }
    Status process(const StageInput& in, FrameBuffer& out, const StageContext&) noexcept override {
        const FrameView& v = in.current;
        for (u32 y = 0; y < out.height; ++y)
            for (u32 x = 0; x < out.width; ++x) out.y()[y * out.width + x] = v.y[(y / 2) * v.yStride + x / 2];
        for (u32 y = 0; y < out.height / 2; ++y)
            for (u32 x = 0; x < out.width; x += 2) {
                const u8* s = v.uv + (y / 2) * v.uvStride + ((x / 2) & ~1u);
                out.uv()[y * out.width + x] = s[0];
                out.uv()[y * out.width + x + 1] = s[1];
            }
        return OkStatus;
    }
};

FrameBuffer gray_frame(u32 w, u32 h, u8 value, u32 index) {
    FrameBuffer f;
    (void)f.resize(w, h);
    std::memset(f.y(), value, static_cast<usize>(w) * h);
    std::memset(f.uv(), 128, static_cast<usize>(w) * h / 2);
    f.index = index;
    f.ptsUs = static_cast<i64>(index) * 33333;
    return f;
}

/// Roda um grafo: empurra `count` quadros (Y = 3·i) e devolve os de saída.
std::vector<FrameBuffer> run_graph(HyperGraph& g, u32 w, u32 h, u32 count) {
    std::vector<FrameBuffer> out;
    for (u32 i = 0; i < count; ++i) {
        const FrameBuffer in = gray_frame(w, h, static_cast<u8>(3 * i), i);
        for (;;) {
            auto pushed = g.try_push(in.view());
            if (!pushed.ok()) return out;
            if (*pushed) break;
            const FrameBuffer* f = nullptr;
            auto got = g.pop(f, 100'000'000ull);
            if (!got.ok()) return out;
            if (*got) out.push_back(*f);
        }
        for (;;) {
            const FrameBuffer* f = nullptr;
            auto got = g.pop(f, 0);
            if (!got.ok() || !*got) break;
            out.push_back(*f);
        }
    }
    g.end_input();
    for (;;) {
        const FrameBuffer* f = nullptr;
        auto got = g.pop(f, 200'000'000ull);
        if (!got.ok()) break;
        if (*got) out.push_back(*f);
    }
    return out;
}

/// Imagem com textura (cantos e bordas em toda parte) para o rastreio.
tracking::Gray texture(u32 w, u32 h, f64 ox, f64 oy, f64 angle = 0.0) {
    tracking::Gray g;
    g.width = w;
    g.height = h;
    g.px.resize(static_cast<usize>(w) * h);
    const f64 c = std::cos(angle), s = std::sin(angle);
    const f64 cx = 0.5 * (w - 1), cy = 0.5 * (h - 1);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            // Conteúdo do mundo em (u, v): o quadro vê o mundo deslocado.
            const f64 dx = x - cx, dy = y - cy;
            const f64 u = c * dx + s * dy + cx - ox, v = -s * dx + c * dy + cy - oy;
            // Banda limitada (sem serrilhado): o deslocamento subpixel existe de fato na imagem.
            const f64 val = 0.5 + 0.16 * std::sin(u * 0.21) * std::cos(v * 0.17) + 0.12 * std::sin((u + 2 * v) * 0.09)
                          + 0.10 * std::cos(u * 0.37 - v * 0.29) + 0.08 * std::sin(v * 0.43 + std::sin(u * 0.05) * 3.0);
            g.px[static_cast<usize>(y) * w + x] = static_cast<f32>(std::clamp(val, 0.0, 1.0));
        }
    return g;
}

} // namespace

// --- Receita --------------------------------------------------------------------
AUREA_TEST(Hyper, SanitizeClampsEveryField) {
    Recipe r;
    r.upscale = 3;
    r.denoise = 7.0f;
    r.deinterlace = 9;
    r.faceStrength = -1.0f;
    r.targetFps = 1000.0;
    r.slowMotion = 5;
    r.stabilizeStrength = std::nanf("");
    r.path = static_cast<Path>(17);
    const Recipe s = sanitize(r);
    AUREA_CHECK_EQ(s.upscale, 4u);
    AUREA_CHECK_NEAR(s.denoise, 1.0f, 1e-6f);
    AUREA_CHECK_EQ(s.deinterlace, 2u);
    AUREA_CHECK_NEAR(s.faceStrength, 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(s.targetFps, 240.0, 1e-9);
    AUREA_CHECK_EQ(s.slowMotion, 4u);
    AUREA_CHECK_NEAR(s.stabilizeStrength, 0.5f, 1e-6f);
    AUREA_CHECK(s.path == Path::Auto);
    Recipe idle;
    AUREA_CHECK(is_identity(idle));
    idle.slowMotion = 2;
    AUREA_CHECK(!is_identity(idle));
    Recipe low;
    low.targetFps = 0.5;   // abaixo de 1 fps = manter
    AUREA_CHECK_NEAR(sanitize(low).targetFps, 0.0, 1e-12);
}

// --- Plano ----------------------------------------------------------------------
AUREA_TEST(HyperPlan, AllTurboIsOnePassWithExactFramesAndCaps) {
    StageRegistry empty;
    Recipe r;
    r.upscale = 2;
    r.denoise = 0.5f;
    r.targetFps = 60.0;
    r.path = Path::Turbo;
    OutputCaps caps;
    caps.capLong = 3840;
    caps.capShort = 2160;
    caps.maxTexture = 8192;
    auto plan = plan_job(r, measured(Path::Turbo), source(640, 360, 30.0, 2.0), caps, empty);
    AUREA_CHECK(plan.ok());
    if (!plan.ok()) return;
    AUREA_CHECK_EQ(plan->passes.size(), static_cast<usize>(1));
    const PassPlan& p = plan->passes[0];
    AUREA_CHECK(p.final && p.renderClean && p.renderDenoise && p.renderUpscale && p.renderMotion);
    AUREA_CHECK_EQ(p.renderW, 1280u);
    AUREA_CHECK_EQ(p.renderH, 720u);
    AUREA_CHECK_EQ(plan->outFrames, static_cast<u64>(120));   // 2 s a 60 fps
    AUREA_CHECK(!plan->muteAudio);
    AUREA_CHECK(plan->present[static_cast<usize>(Step::Motion)] && plan->present[static_cast<usize>(Step::Upscale)]);
    // Câmera lenta x2 a 30: o dobro de quadros e sem som.
    Recipe slow;
    slow.slowMotion = 2;
    auto sp = plan_job(slow, measured(Path::Turbo), source(640, 360, 30.0, 2.0), caps, empty);
    AUREA_CHECK(sp.ok());
    if (!sp.ok()) return;
    AUREA_CHECK_EQ(sp->outFrames, static_cast<u64>(120));
    AUREA_CHECK_NEAR(sp->outFps, 30.0, 1e-9);
    AUREA_CHECK(sp->muteAudio);
    AUREA_CHECK_EQ(sp->passes[0].slowMotion, 2u);
    // Saída limitada pelo aparelho (1080p) e pelo fps do encoder.
    OutputCaps small;
    small.capLong = 1920;
    small.capShort = 1080;
    small.maxFps = 30.0;
    Recipe big;
    big.upscale = 4;
    big.targetFps = 60.0;
    auto bp = plan_job(big, measured(Path::Turbo), source(1280, 720, 30.0, 1.0), small, empty);
    AUREA_CHECK(bp.ok());
    if (!bp.ok()) return;
    AUREA_CHECK(bp->resolutionCapped && bp->fpsCapped);
    AUREA_CHECK_EQ(bp->outW, 1920u);
    AUREA_CHECK_EQ(bp->outH, 1080u);
    AUREA_CHECK_NEAR(bp->outFps, 30.0, 1e-9);
}

AUREA_TEST(HyperPlan, NeuralUpscaleThenMotionIsTwoPassesAndUpscalesOnlyOriginals) {
    // Só uma rede de upscale (sem rede de Motion): o Motion Turbo vem depois.
    StageRegistry reg;
    StageDescriptor up;
    up.id = "teste.anime";
    up.kind = StageKind::Upscale;
    up.path = Path::Lite;
    up.mode = static_cast<i32>(UpscaleMode::Anime);
    up.create = []() -> std::unique_ptr<HyperStage> { return std::make_unique<DoubleStage>(); };
    reg.add(up);
    Recipe r;
    r.upscale = 2;
    r.upscaleMode = UpscaleMode::Anime;
    r.targetFps = 60.0;
    r.path = Path::Lite;
    auto plan = plan_job(r, measured(Path::Lite), source(320, 180, 30.0, 1.0), OutputCaps{}, reg);
    AUREA_CHECK(plan.ok());
    if (!plan.ok()) return;
    AUREA_CHECK_EQ(plan->passes.size(), static_cast<usize>(2));
    if (plan->passes.size() != 2) return;
    const PassPlan& a = plan->passes[0];
    const PassPlan& b = plan->passes[1];
    // 1ª: a rede no grafo, nos quadros ORIGINAIS (30 fps), 320x180 -> 640x360.
    AUREA_CHECK_EQ(a.graph.size(), static_cast<usize>(1));
    AUREA_CHECK(!a.final && !a.renderMotion);
    AUREA_CHECK_NEAR(a.fps, 30.0, 1e-9);
    AUREA_CHECK_EQ(a.frames, static_cast<u64>(30));
    AUREA_CHECK_EQ(a.renderW, 320u);
    AUREA_CHECK_EQ(a.outW, 640u);
    // 2ª: o Motion Turbo depois do upscale, a 60.
    AUREA_CHECK(b.final && b.renderMotion && b.graph.empty() && !b.renderUpscale);
    AUREA_CHECK_EQ(b.sourceW, 640u);
    AUREA_CHECK_EQ(b.frames, static_cast<u64>(60));
    AUREA_CHECK(plan->paths[static_cast<usize>(Step::Upscale)] != Path::Turbo);
    // Turbo pedido: uma passagem, tudo no render.
    r.path = Path::Turbo;
    auto turbo = plan_job(r, measured(Path::Lite), source(320, 180, 30.0, 1.0), OutputCaps{}, reg);
    AUREA_CHECK(turbo.ok() && turbo->passes.size() == 1);
}

AUREA_TEST(HyperPlan, TurboAfterANetworkOpensANewPassAndEstimateMarksMissingPaths) {
    // Registro só com uma rede de Clean: o Upscale General fica Turbo DEPOIS dela.
    StageRegistry reg;
    StageDescriptor clean;
    clean.id = "teste.clean";
    clean.kind = StageKind::Clean;
    clean.path = Path::Lite;
    clean.create = []() -> std::unique_ptr<HyperStage> { return std::make_unique<AverageStage>(0); };
    reg.add(clean);
    Recipe r;
    r.denoise = 0.8f;
    r.deblock = true;
    r.upscale = 2;
    r.path = Path::Lite;
    auto plan = plan_job(r, measured(Path::Lite), source(320, 180, 25.0, 1.0), OutputCaps{}, reg);
    AUREA_CHECK(plan.ok());
    if (!plan.ok()) return;
    AUREA_CHECK_EQ(plan->passes.size(), static_cast<usize>(2));
    if (plan->passes.size() != 2) return;
    AUREA_CHECK(plan->passes[0].renderClean && !plan->passes[0].renderDenoise);   // deblock Turbo, ruído na rede
    AUREA_CHECK_EQ(plan->passes[0].graph.size(), static_cast<usize>(1));
    AUREA_CHECK(plan->passes[1].renderUpscale && plan->passes[1].final);
    AUREA_CHECK(plan->paths[static_cast<usize>(Step::Clean)] == Path::Lite);
    AUREA_CHECK(plan->paths[static_cast<usize>(Step::Upscale)] == Path::Turbo);
    // Estimativa: Turbo e Lite existem; Max (sem rede de Max) também cai na
    // Lite do Clean (Max -> Lite); sem nenhuma rede, Lite/Max = -1.
    const Estimate e = estimate_job(r, measured(Path::Lite), source(320, 180, 25.0, 1.0), OutputCaps{}, reg);
    AUREA_CHECK(e.seconds[static_cast<usize>(Path::Turbo)] > 0.0);
    AUREA_CHECK(e.seconds[static_cast<usize>(Path::Lite)] > 0.0);
    AUREA_CHECK(e.seconds[0] > 0.0);
    AUREA_CHECK_EQ(e.outputWidth, 640u);
    AUREA_CHECK(e.peakBytes > 0);
    StageRegistry none;
    const Estimate t = estimate_job(r, measured(Path::Lite), source(320, 180, 25.0, 1.0), OutputCaps{}, none);
    AUREA_CHECK(t.seconds[static_cast<usize>(Path::Turbo)] > 0.0);
    AUREA_CHECK(t.seconds[static_cast<usize>(Path::Lite)] < 0.0);
    AUREA_CHECK(t.seconds[static_cast<usize>(Path::Max)] < 0.0);
    AUREA_CHECK_NEAR(t.seconds[0], t.seconds[static_cast<usize>(Path::Turbo)], 1e-9);
}

// --- Grafo ----------------------------------------------------------------------
AUREA_TEST(HyperGraph, KeepsOrderPtsAndBoundedMemoryWithTemporalNeighbors) {
    std::vector<std::vector<u32>> seen;
    std::vector<std::unique_ptr<HyperStage>> stages;
    stages.push_back(std::make_unique<AverageStage>(2, &seen));
    stages.push_back(std::make_unique<DoubleStage>());
    HyperGraph g;
    static const std::atomic<bool> never{false};
    const Status s = g.start_with(DeviceProfile{}, Recipe{}, std::move(stages), StageFormat{16, 8, 30.0}, 0, 0, &never, nullptr, nullptr);
    AUREA_CHECK(s.ok());
    if (!s.ok()) return;
    AUREA_CHECK(!g.resumable());   // vizinhos: a passagem não retoma no meio
    const auto out = run_graph(g, 16, 8, 60);
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(60));
    for (usize i = 0; i < out.size(); ++i) {
        AUREA_CHECK_EQ(out[i].index, static_cast<u32>(i));
        AUREA_CHECK_EQ(out[i].ptsUs, static_cast<i64>(i) * 33333);
        AUREA_CHECK_EQ(out[i].width, 32u);
    }
    // Quadro do meio: média de i-2..i+2 = 3i. Na borda: o 1º quadro repetido.
    if (out.size() == 60) {
        AUREA_CHECK_EQ(out[10].nv12[0], static_cast<u8>(30));
        AUREA_CHECK_EQ(out[0].nv12[0], static_cast<u8>((0 + 0 + 0 + 3 + 6) / 5));
        AUREA_CHECK_EQ(out[59].nv12[0], static_cast<u8>((171 + 174 + 177 + 177 + 177) / 5));
    }
    if (!seen.empty()) {
        AUREA_CHECK((seen.front() == std::vector<u32>{0, 0, 0, 1, 2}));
        AUREA_CHECK((seen.back() == std::vector<u32>{59, 57, 58, 59, 59}));
    }
    // Nada cresce com o vídeo: janelas (2r+2) + saídas + o que está em uso.
    AUREA_CHECK_MSG(g.max_live_frames() <= 14, "a fila do grafo tem de ser limitada");
    AUREA_CHECK(g.peak_bytes() > 0);
}

AUREA_TEST(HyperGraph, FitStageResizesAndCancelStopsAtOnce) {
    {
        HyperGraph g;
        static const std::atomic<bool> never{false};
        std::vector<std::unique_ptr<HyperStage>> none;
        AUREA_CHECK(g.start_with(DeviceProfile{}, Recipe{}, std::move(none), StageFormat{16, 8, 30.0}, 24, 12, &never, nullptr, nullptr).ok());
        const auto out = run_graph(g, 16, 8, 5);
        AUREA_CHECK_EQ(out.size(), static_cast<usize>(5));
        if (!out.empty()) {
            AUREA_CHECK_EQ(out[0].width, 24u);
            AUREA_CHECK_EQ(out[0].height, 12u);
            AUREA_CHECK_EQ(out[4].nv12[0], static_cast<u8>(12));   // cinza uniforme: o mesmo nível
        }
    }
    {
        std::atomic<bool> cancel{false};
        std::vector<std::unique_ptr<HyperStage>> stages;
        stages.push_back(std::make_unique<AverageStage>(1));
        HyperGraph g;
        AUREA_CHECK(g.start_with(DeviceProfile{}, Recipe{}, std::move(stages), StageFormat{16, 8, 30.0}, 0, 0, &cancel, nullptr, nullptr).ok());
        const FrameBuffer f = gray_frame(16, 8, 9, 0);
        AUREA_CHECK(g.try_push(f.view()).ok());
        cancel.store(true);
        const FrameBuffer* out = nullptr;
        const auto got = g.pop(out, 1'000'000'000ull);
        AUREA_CHECK(!got.ok() && got.code() == Errc::Cancelled);
        AUREA_CHECK(!g.try_push(f.view()).ok());
    }
    // Embutido: o upscale Anime (Real-ESRGAN) existe nos dois caminhos.
    const StageRegistry& reg = stage_registry();
    DeviceProfile p;
    AUREA_CHECK(reg.find(StageKind::Upscale, Path::Max, static_cast<i32>(UpscaleMode::Anime), p) != nullptr);
    AUREA_CHECK(reg.find(StageKind::Upscale, Path::Lite, static_cast<i32>(UpscaleMode::Anime), p) != nullptr);
}

AUREA_TEST(Hyper, Nv12RgbRoundTripAndFrameBuffer) {
    FrameBuffer f;
    AUREA_CHECK(!f.resize(15, 8).ok());   // ímpar: recusado
    AUREA_CHECK(f.resize(8, 4).ok());
    std::vector<u8> rgb(8 * 4 * 3);
    for (usize i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<u8>((i * 37) % 256);
    // Blocos 2x2 da mesma cor (o croma 4:2:0 não perde nada).
    for (u32 y = 0; y < 4; ++y)
        for (u32 x = 0; x < 8; ++x)
            for (u32 c = 0; c < 3; ++c) rgb[(y * 8 + x) * 3 + c] = rgb[((y & ~1u) * 8 + (x & ~1u)) * 3 + c];
    AUREA_CHECK(rgb24_to_nv12(rgb.data(), 24, 8, 4, f));
    std::vector<u8> back(rgb.size());
    nv12_to_rgb24(f.view(), back.data(), 24);
    for (usize i = 0; i < rgb.size(); ++i) AUREA_CHECK(std::abs(static_cast<int>(rgb[i]) - static_cast<int>(back[i])) <= 3);
}

// --- Entrelaçado ----------------------------------------------------------------
AUREA_TEST(HyperAnalysis, CombScoreDetectsInterlacedMotionOnly) {
    constexpr u32 W = 160, H = 96;
    // Barras verticais andando 6 px entre os campos: linhas pares no tempo t,
    // ímpares em t + 1/2 (o pente do entrelaçado).
    auto bars = [](u32 x, f64 shift) { return (static_cast<i64>(std::floor((x + shift) / 8.0)) & 1) ? 200 : 50; };
    std::vector<u8> combed(W * H), progressive(W * H), still(W * H);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            combed[y * W + x] = static_cast<u8>(bars(x, (y & 1) ? 6.0 : 0.0));
            progressive[y * W + x] = static_cast<u8>(bars(x, 3.0));
            // Parado e com detalhe vertical forte (degradê + textura): não é pente.
            still[y * W + x] = static_cast<u8>(40 + (y * 150) / H + ((x / 5 + y / 7) % 2) * 30);
        }
    const CombScore c = comb_score(combed.data(), W, H, W);
    const CombScore p = comb_score(progressive.data(), W, H, W);
    const CombScore s = comb_score(still.data(), W, H, W);
    AUREA_CHECK_MSG(c.interlaced(), "o quadro com pente tem de ser detectado");
    AUREA_CHECK_MSG(!p.interlaced(), "progressivo com movimento não é entrelaçado");
    AUREA_CHECK_MSG(!s.interlaced(), "progressivo parado não é entrelaçado");
    AUREA_CHECK(c.ratio() > 3.0 * std::max(0.1, p.ratio()));
}

// --- Estabilização --------------------------------------------------------------
AUREA_TEST(HyperAnalysis, StabilizationReducesJitterOfAShakenClip) {
    constexpr u32 W = 240, H = 136, N = 48;
    u64 seed = 77;
    std::vector<f64> sx(N), sy(N), pan(N);
    for (u32 k = 0; k < N; ++k) {
        pan[k] = 0.6 * k;   // panorâmica lenta (fica)
        sx[k] = (static_cast<f64>(splitmix(seed) % 1000) / 1000.0 - 0.5) * 9.0;   // tremor (sai)
        sy[k] = (static_cast<f64>(splitmix(seed) % 1000) / 1000.0 - 0.5) * 7.0;
    }
    std::vector<Motion2D> steps(N);
    tracking::Gray prev = texture(W, H, pan[0] + sx[0], sy[0]);
    u32 valid = 0;
    for (u32 k = 1; k < N; ++k) {
        const tracking::Gray cur = texture(W, H, pan[k] + sx[k], sy[k]);
        steps[k] = estimate_motion(prev, cur);
        valid += steps[k].valid ? 1 : 0;
        // O movimento medido bate com o real (px).
        if (steps[k].valid) {
            AUREA_CHECK_NEAR(steps[k].dx, (pan[k] + sx[k]) - (pan[k - 1] + sx[k - 1]), 0.35);
            AUREA_CHECK_NEAR(steps[k].dy, sy[k] - sy[k - 1], 0.35);
        }
        prev = cur;
    }
    AUREA_CHECK(valid >= N - 3);
    const StabPlan plan = smooth_trajectory(steps, 0.6f, W, H, 1.0, true);
    AUREA_CHECK_EQ(plan.frames.size(), static_cast<usize>(N));
    // Tremor = o que sobra da posição depois de tirar a tendência (pan).
    auto jitter = [&](bool corrected) {
        f64 se = 0.0;
        for (u32 k = 2; k + 2 < N; ++k) {
            const f64 x = pan[k] + sx[k] + (corrected ? plan.frames[k].dx : 0.0);
            const f64 y = sy[k] + (corrected ? plan.frames[k].dy : 0.0);
            const f64 xn = 0.5 * ((pan[k - 1] + sx[k - 1] + (corrected ? plan.frames[k - 1].dx : 0.0))
                                + (pan[k + 1] + sx[k + 1] + (corrected ? plan.frames[k + 1].dx : 0.0)));
            const f64 yn = 0.5 * ((sy[k - 1] + (corrected ? plan.frames[k - 1].dy : 0.0)) + (sy[k + 1] + (corrected ? plan.frames[k + 1].dy : 0.0)));
            se += (x - xn) * (x - xn) + (y - yn) * (y - yn);
        }
        return std::sqrt(se / (N - 4));
    };
    const f64 before = jitter(false), after = jitter(true);
    AUREA_CHECK_MSG(after < 0.3 * before, "a estabilização tem de tirar pelo menos 70% do tremor");
    AUREA_CHECK(plan.zoom >= 1.0f && plan.zoom <= 1.3f);
    // Sem recorte (keyframes): a mesma correção, zoom 1.
    const StabPlan keys = smooth_trajectory(steps, 0.6f, W, H, 1.0, false);
    AUREA_CHECK_NEAR(keys.zoom, 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(keys.frames[10].dx, plan.frames[10].dx, 1e-3f);
}

AUREA_TEST(HyperAnalysis, MotionEstimateRecoversRotation) {
    const tracking::Gray a = texture(240, 136, 0, 0, 0.0);
    const tracking::Gray b = texture(240, 136, 2.0, -1.0, 0.02);
    const Motion2D m = estimate_motion(a, b);
    AUREA_CHECK(m.valid);
    AUREA_CHECK_NEAR(m.angle, 0.02, 0.006);
    AUREA_CHECK_NEAR(m.scale, 1.0, 0.01);
}

// --- Medidor (arquivo) -----------------------------------------------------------
AUREA_TEST(HyperProfile, JsonRoundTripAndStampDecidesRemeasure) {
    DeviceProfile p = measured(Path::Lite);
    p.npu = true;
    p.thermalAfter = 0.25f;
    p.tile = 96;
    p.backendNote = "vulkan \"ok\"\nfim";
    const std::string json = profile_to_json(p, "2.0.0|Vulkan|GPU X|1.2");
    DeviceProfile q;
    std::string stamp;
    AUREA_CHECK(profile_from_json(json, q, stamp));
    AUREA_CHECK(stamp == "2.0.0|Vulkan|GPU X|1.2");
    AUREA_CHECK(q.measured && q.abi64 && q.vulkanFp16 && q.npu);
    AUREA_CHECK_NEAR(q.turboMsPerFrame, p.turboMsPerFrame, 1e-3f);
    AUREA_CHECK_NEAR(q.liteMsPerTile, p.liteMsPerTile, 1e-3f);
    AUREA_CHECK(q.defaultPath == Path::Lite);
    AUREA_CHECK_EQ(q.tile, 96u);
    AUREA_CHECK(!profile_from_json("{\"version\": 2}", q, stamp));
    AUREA_CHECK(!profile_from_json("lixo", q, stamp));
    // A sugestão: sem medição, Turbo; rede lenta, Turbo; boa, Lite; muito boa, Max.
    AUREA_CHECK(suggest_path(DeviceProfile{}) == Path::Turbo);
    DeviceProfile slow = measured(Path::Turbo);
    slow.liteMsPerTile = 2000.0f;
    AUREA_CHECK(suggest_path(slow) == Path::Turbo);
    DeviceProfile mid = measured(Path::Turbo);
    mid.liteMsPerTile = 150.0f;
    AUREA_CHECK(suggest_path(mid) == Path::Lite);
    DeviceProfile fast = measured(Path::Turbo);
    fast.liteMsPerTile = 20.0f;
    AUREA_CHECK(suggest_path(fast) == Path::Max);
}

// --- Motion Lite/Max no grafo -----------------------------------------------------
namespace {
/// Conta quantas vezes processou (as etapas antes do Motion não reprocessam
/// o quadro que o render repetiu).
class CountingStage final : public HyperStage {
public:
    explicit CountingStage(u32* calls) : calls_(calls) {}
    const char* name() const noexcept override { return "teste.conta"; }
    StageKind kind() const noexcept override { return StageKind::Upscale; }
    Path path() const noexcept override { return Path::Lite; }
    Status prepare(const DeviceProfile&, const Recipe&, const StageFormat& in, StageFormat& out) noexcept override {
        out = in;
        return OkStatus;
    }
    Status process(const StageInput& in, FrameBuffer& out, const StageContext&) noexcept override {
        ++*calls_;
        std::memcpy(out.nv12.data(), in.current.y, out.nv12.size());
        return OkStatus;
    }
private:
    u32* calls_;
};
/// Motion de teste: Y = A·(1 − t) + B·t.
class BlendMotionStage final : public HyperStage {
public:
    const char* name() const noexcept override { return "teste.motion"; }
    StageKind kind() const noexcept override { return StageKind::Motion; }
    Path path() const noexcept override { return Path::Lite; }
    Status prepare(const DeviceProfile&, const Recipe&, const StageFormat& in, StageFormat& out) noexcept override {
        out = in;
        return OkStatus;
    }
    Status process(const StageInput& in, FrameBuffer& out, const StageContext&) noexcept override {
        const u32 n = in.current.width * in.current.height;
        for (u32 i = 0; i < n; ++i)
            out.y()[i] = static_cast<u8>(std::lround(in.motionA.y[i] * (1.0f - in.motionPhase) + in.motionB.y[i] * in.motionPhase));
        std::memset(out.uv(), 128, n / 2);
        return OkStatus;
    }
};
} // namespace

AUREA_TEST(HyperGraph, MotionStageGetsTheSourcePairAndPhaseAndEarlierStagesSkipRepeats) {
    u32 calls = 0;
    std::vector<std::unique_ptr<HyperStage>> stages;
    stages.push_back(std::make_unique<CountingStage>(&calls));
    stages.push_back(std::make_unique<BlendMotionStage>());
    HyperGraph g;
    static const std::atomic<bool> never{false};
    AUREA_CHECK(g.start_with(DeviceProfile{}, Recipe{}, std::move(stages), StageFormat{16, 8, 30.0}, 0, 0, &never, nullptr, nullptr).ok());
    AUREA_CHECK(!g.resumable());
    // Câmera lenta x4: o render repete cada quadro de origem 4 vezes.
    constexpr u32 kOut = 40;
    std::vector<FrameBuffer> out;
    u32 pushed = 0;
    auto push = [&](u32 j) {
        const f64 pos = j / 4.0;
        FrameBuffer f = gray_frame(16, 8, static_cast<u8>(20 * static_cast<u32>(std::floor(pos))), j);
        f.sourcePosition = pos;
        for (;;) {
            auto r = g.try_push(f.view());
            if (!r.ok()) return false;
            if (*r) return true;
            const FrameBuffer* o = nullptr;
            auto got = g.pop(o, 100'000'000ull);
            if (!got.ok()) return false;
            if (*got) out.push_back(*o);
        }
    };
    for (u32 j = 0; j < kOut; ++j) {
        AUREA_CHECK(push(j));
        ++pushed;
    }
    g.end_input();
    for (;;) {
        const FrameBuffer* o = nullptr;
        auto got = g.pop(o, 200'000'000ull);
        if (!got.ok()) break;
        if (*got) out.push_back(*o);
    }
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(kOut));
    AUREA_CHECK_EQ(calls, kOut / 4);   // só os 10 quadros de origem
    for (usize j = 0; j + 4 < out.size(); ++j) {
        // Entre 20·k e 20·(k+1) na fase j/4 − k.
        const f64 want = 20.0 * (j / 4.0);
        AUREA_CHECK_NEAR(static_cast<f64>(out[j].nv12[0]), want, 0.6);
        AUREA_CHECK_NEAR(out[j].sourcePosition, j / 4.0, 1e-9);
    }
    // O fim: sem B, o último original repetido.
    if (out.size() == kOut) AUREA_CHECK_EQ(out[kOut - 1].nv12[0], static_cast<u8>(180));
}

AUREA_TEST(HyperPlan, NeuralMotionRunsInTheGraphOfTheSamePass) {
    StageRegistry reg;
    StageDescriptor up;
    up.id = "teste.up";
    up.kind = StageKind::Upscale;
    up.path = Path::Lite;
    up.create = []() -> std::unique_ptr<HyperStage> { return std::make_unique<DoubleStage>(); };
    reg.add(up);
    StageDescriptor mo;
    mo.id = "teste.motion";
    mo.kind = StageKind::Motion;
    mo.path = Path::Lite;
    mo.create = []() -> std::unique_ptr<HyperStage> { return std::make_unique<BlendMotionStage>(); };
    reg.add(mo);
    Recipe r;
    r.upscale = 2;
    r.slowMotion = 4;
    r.path = Path::Lite;
    auto plan = plan_job(r, measured(Path::Lite), source(320, 180, 30.0, 1.0), OutputCaps{}, reg);
    AUREA_CHECK(plan.ok());
    if (!plan.ok()) return;
    AUREA_CHECK_EQ(plan->passes.size(), static_cast<usize>(1));
    const PassPlan& p = plan->passes[0];
    AUREA_CHECK(p.graph_motion() && !p.renderMotion);
    AUREA_CHECK_EQ(p.graph.size(), static_cast<usize>(2));
    AUREA_CHECK(p.graph.size() == 2 && p.graph[0]->kind == StageKind::Upscale && p.graph[1]->kind == StageKind::Motion);
    AUREA_CHECK_EQ(p.frames, static_cast<u64>(120));
    AUREA_CHECK(plan->paths[static_cast<usize>(Step::Motion)] == Path::Lite);
}

// --- FSR na CPU (o ×2 depois da rede no aparelho fraco) -------------------------
#include "aurea/hyper/HyperTurbo.hpp"
AUREA_TEST(HyperTurbo, CpuFsrIsSharperThanBilinearAndStaysInRange) {
    constexpr u32 W = 32, H = 16;
    std::vector<u8> src(W * H * 3);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x)
            for (u32 c = 0; c < 3; ++c) src[(y * W + x) * 3 + c] = x >= 16 ? 200 : 40;
    std::vector<u8> fsr(W * H * 12), bil(W * H * 12);
    fsr_upscale_rgb(src.data(), W, H, fsr.data(), W * 2, H * 2, 0.5f, 3);
    // Bilinear de referência (centros de pixel).
    for (u32 y = 0; y < H * 2; ++y)
        for (u32 x = 0; x < W * 2; ++x) {
            const f32 sx = std::clamp((x + 0.5f) * 0.5f - 0.5f, 0.0f, W - 1.0f);
            const u32 x0 = static_cast<u32>(sx), x1 = std::min(x0 + 1, W - 1);
            const f32 t = sx - x0;
            for (u32 c = 0; c < 3; ++c)
                bil[(y * W * 2 + x) * 3 + c] = static_cast<u8>(std::lround(src[x0 * 3 + c] * (1 - t) + src[x1 * 3 + c] * t));
        }
    int stepF = 0, stepB = 0, lo = 255, hi = 0;
    for (u32 x = 0; x + 1 < W * 2; ++x) {
        const usize a = (8 * W * 2 + x) * 3;
        stepF = std::max(stepF, fsr[a + 3] - fsr[a]);
        stepB = std::max(stepB, bil[a + 3] - bil[a]);
    }
    for (u8 v : fsr) { lo = std::min<int>(lo, v); hi = std::max<int>(hi, v); }
    AUREA_CHECK(stepF > stepB);
    AUREA_CHECK(lo >= 28 && hi <= 212);
    AUREA_CHECK_EQ(fsr[(8 * W * 2 + 4) * 3], 40);
    AUREA_CHECK_EQ(fsr[(8 * W * 2 + 60) * 3], 200);
}
