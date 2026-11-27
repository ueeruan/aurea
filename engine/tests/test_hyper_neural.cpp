// Tests for the optional cleanup, interpolation and face models.
#include "TestFramework.hpp"

#include "aurea/hyper/HyperNeural.hpp"
#include "aurea/hyper/HyperStage.hpp"

#include "../src/hyper/neural/NeuralStages.hpp"
#include "../src/hyper/neural/NeuralRetime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::hyper;

namespace {

f64 now_ms() {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

DeviceProfile cpu_profile() {
    DeviceProfile p;
    p.measured = false;
    p.abi64 = true;
    p.vulkan = false;
    p.tile = 96;
    p.tileMargin = 12;
    return p;
}

/// Imagem sintética "natural": gradientes, discos com borda suave, retângulos,
/// linhas finas, traços tipo texto e textura leve. Determinística.
std::vector<u8> synthetic_rgb(u32 w, u32 h, u32 seed = 1) {
    std::vector<u8> img(static_cast<usize>(w) * h * 3);
    u32 state = 0x9E3779B9u * seed + 12345u;
    auto rnd = [&]() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return (state & 0xFFFFFF) / 16777216.0f; };
    struct Disc { f32 x, y, r, c[3]; };
    std::vector<Disc> discs;
    for (int i = 0; i < 7; ++i) discs.push_back({rnd() * w, rnd() * h, 6 + rnd() * w * 0.18f, {rnd(), rnd(), rnd()}});
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const f32 fx = static_cast<f32>(x) / w, fy = static_cast<f32>(y) / h;
            f32 c[3] = {0.25f + 0.5f * fx, 0.3f + 0.4f * fy, 0.6f - 0.3f * fx * fy};
            for (const auto& d : discs) {
                const f32 dist = std::sqrt((x - d.x) * (x - d.x) + (y - d.y) * (y - d.y));
                const f32 a = std::clamp(d.r - dist + 0.5f, 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k) c[k] = c[k] * (1 - a) + d.c[k] * a;
            }
            // listras finas e retângulo de "texto"
            if ((x / 3) % 7 == 0 && y > h / 2 && y < h * 3 / 4) for (int k = 0; k < 3; ++k) c[k] *= 0.35f;
            if (x > w / 8 && x < w / 2 && ((y / 2) % 5 == 0) && y < h / 3) for (int k = 0; k < 3; ++k) c[k] = 0.95f;
            const f32 tex = 0.02f * std::sin(x * 0.15f) * std::cos(y * 0.11f);
            for (int k = 0; k < 3; ++k)
                img[(static_cast<usize>(y) * w + x) * 3 + k] = static_cast<u8>(std::clamp((c[k] + tex) * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    return img;
}

FrameBuffer to_nv12(const std::vector<u8>& rgb, u32 w, u32 h) {
    FrameBuffer fb;
    (void)fb.resize(w, h);
    (void)rgb24_to_nv12(rgb.data(), w * 3, w, h, fb);
    return fb;
}

f64 psnr_y(const FrameBuffer& a, const FrameBuffer& b, u32 border = 0) {
    f64 se = 0;
    u64 n = 0;
    for (u32 y = border; y + border < a.height; ++y)
        for (u32 x = border; x + border < a.width; ++x) {
            const f64 d = f64(a.nv12[static_cast<usize>(y) * a.width + x]) - f64(b.nv12[static_cast<usize>(y) * b.width + x]);
            se += d * d;
            ++n;
        }
    if (se == 0) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / (se / f64(n)));
}

struct RunResult {
    Status status = OkStatus;
    FrameBuffer out;
    f64 ms = 0;
};

RunResult run_stage(HyperStage& stage, const FrameBuffer& in, u32 outW, u32 outH, u32 index = 0) {
    RunResult r;
    (void)r.out.resize(outW, outH);
    StageInput input;
    input.current = in.view();
    input.current.index = index;
    StageContext ctx;
    const f64 t0 = now_ms();
    r.status = stage.process(input, r.out, ctx);
    r.ms = now_ms() - t0;
    return r;
}

std::string msg(const Status& s) {
    return std::string(s.message()) + (s.detail().empty() ? "" : ": " + std::string(s.detail()));
}

std::unique_ptr<HyperStage> prepared_clean(Path path, const DeviceProfile& p, const Recipe& recipe,
                                     u32 w, u32 h, StageFormat& out) {
    auto stage = neural::make_clean_stage(path);
    StageFormat in{w, h, 30.0};
    const Status s = stage->prepare(p, recipe, in, out);
    if (!s.ok()) {
        std::printf("\n    prepare falhou: %s ", s.message().data());
        return nullptr;
    }
    return stage;
}

std::string repo_root() {
    // Os testes rodam com cwd = engine/ (host-neural.cmd).
    for (const char* c : {"..", ".", "../..", "../../.."})
        if (std::filesystem::exists(std::string(c) + "/build/hyper-tools/out/ondemand")) return c;
    return "";
}

} // namespace

// -----------------------------------------------------------------------------
AUREA_TEST(HyperNeural, RegistryHasEveryLiteAndMaxStage) {
    StageRegistry r;
    register_neural_stages(r);
    DeviceProfile p = cpu_profile();
    const i32 g = static_cast<i32>(UpscaleMode::General), a = static_cast<i32>(UpscaleMode::Anime);
    for (Path path : {Path::Lite, Path::Max}) {
        AUREA_CHECK(r.find(StageKind::Upscale, path, g, p) == nullptr);
        AUREA_CHECK(r.find(StageKind::Upscale, path, a, p) == nullptr);
        AUREA_CHECK(r.find(StageKind::Clean, path, -1, p) != nullptr);
        AUREA_CHECK(r.find(StageKind::Motion, path, -1, p) != nullptr);
    }
    // Face: só com o modelo sob demanda baixado e conferido.
    set_model_directory("");
    AUREA_CHECK(r.find(StageKind::Face, Path::Max, -1, p) == nullptr);
    AUREA_CHECK_EQ(r.all().size(), static_cast<usize>(6));
}

// 0.3.4 (bugs-034, hyper): nenhum caminho depende do aparelho. A Face só
// depende do modelo baixado; 32 bits e pouca RAM não a tiram mais do registro.
AUREA_TEST(HyperNeural, NoStageIsRefusedByDeviceTraits) {
    StageRegistry r;
    register_neural_stages(r);
    DeviceProfile weak = cpu_profile();
    weak.measured = true;
    weak.abi64 = false;
    weak.freeRamBytes = 300ull << 20;
    weak.vulkan = weak.vulkanFp16 = weak.gles3 = weak.npu = false;
    weak.liteMsPerTile = 5000.0f;
    const i32 g = static_cast<i32>(UpscaleMode::General), a = static_cast<i32>(UpscaleMode::Anime);
    for (Path path : {Path::Lite, Path::Max}) {
        AUREA_CHECK(r.find(StageKind::Upscale, path, g, weak) == nullptr);
        AUREA_CHECK(r.find(StageKind::Upscale, path, a, weak) == nullptr);
        AUREA_CHECK(r.find(StageKind::Clean, path, -1, weak) != nullptr);
        AUREA_CHECK(r.find(StageKind::Motion, path, -1, weak) != nullptr);
    }
    DeviceProfile strong = cpu_profile();
    strong.freeRamBytes = 8ull << 30;
    AUREA_CHECK(neural::face_available(weak) == neural::face_available(strong));
    AUREA_CHECK(neural::face_available(weak) == ondemand_model_ready("gfpgan-v1.4"));
}

AUREA_TEST(HyperNeural, MeterChecksReferenceTileOnCpuAndVulkan) {
    DeviceProfile p = cpu_profile();
    p.vulkan = true;   // o medidor tenta; recusa sozinho se o host não tiver
    const NeuralMeter m = measure_neural(p);
    std::printf("\n    %s\n    melhor: %.2f ms/tile (%s), tile %u+%u ", m.note.c_str(), m.bestMsPerTile,
                m.bestIsVulkan ? "Vulkan" : "CPU", m.tile, m.margin);
    AUREA_CHECK(m.cpuOk);
    AUREA_CHECK(m.bestMsPerTile > 0.0f);
    DeviceProfile applied = p;
    apply_neural_meter(m, applied);
    AUREA_CHECK_NEAR(applied.liteMsPerTile, m.bestMsPerTile, 1e-6f);
    AUREA_CHECK(applied.vulkan == m.bestIsVulkan);
    AUREA_CHECK(!applied.backendNote.empty());
}

AUREA_TEST(HyperNeural, DenoiseStrengthInterpolatesX4v3AndWdnWeights) {
    std::vector<unsigned char> a, b, mid;
    AUREA_CHECK(neural::esrgan_general_weights(1.0f, a).ok());
    AUREA_CHECK(neural::esrgan_general_weights(0.0f, b).ok());
    AUREA_CHECK(neural::esrgan_general_weights(0.25f, mid).ok());
    AUREA_CHECK(a.size() == b.size() && a.size() == mid.size() && a.size() > 4'000'000);
    if (a.size() != b.size() || a.size() != mid.size()) return;
    const f32* fa = reinterpret_cast<const f32*>(a.data());
    const f32* fb = reinterpret_cast<const f32*>(b.data());
    const f32* fm = reinterpret_cast<const f32*>(mid.data());
    usize differ = 0;
    f64 worst = 0;
    for (usize i = 0; i < a.size() / 4; ++i) {
        differ += fa[i] != fb[i];
        worst = std::max(worst, std::fabs(f64(fm[i]) - (0.25 * fa[i] + 0.75 * fb[i])));
    }
    AUREA_CHECK(differ > a.size() / 8);   // os dois modelos são diferentes
    AUREA_CHECK(worst < 1e-6);            // w·x4v3 + (1−w)·wdn, palavra a palavra
}

AUREA_TEST(HyperNeural, TiledOutputMatchesSingleTileWithin8To12PxMargins) {
    const u32 w = 128, h = 128;
    const auto rgb = synthetic_rgb(w, h, 11);
    const FrameBuffer in = to_nv12(rgb, w, h);
    Recipe recipe;
    recipe.denoise = 0.5f;
    DeviceProfile whole = cpu_profile();
    whole.tile = 128;   // um tile só: sem costura
    StageFormat out{};
    auto ref = prepared_clean(Path::Lite, whole, recipe, w, h, out);
    AUREA_CHECK(ref != nullptr);
    if (!ref) return;
    const RunResult a = run_stage(*ref, in, out.width, out.height);
    for (u32 margin : {8u, 12u}) {
        DeviceProfile tiled = cpu_profile();
        tiled.tile = 64;
        tiled.tileMargin = margin;
        auto t = prepared_clean(Path::Lite, tiled, recipe, w, h, out);
        const RunResult b = run_stage(*t, in, out.width, out.height);
        const f64 p = psnr_y(a.out, b.out);
        std::printf("\n    tile 64, margem %u px vs tile unico: %.2f dB (%u tiles)", margin, p,
                    neural::clean_stage_stats(*t).tiles);
        AUREA_CHECK(p > 38.0);
    }
    std::printf("\n  ");
}

AUREA_TEST(HyperNeural, StaticTilesAreSkippedAndMatchRecomputedOutput) {
    const u32 w = 192, h = 128;
    auto rgb = synthetic_rgb(w, h, 5);
    Recipe recipe;
    recipe.denoise = 0.5f;
    DeviceProfile p = cpu_profile();
    p.tile = 64;
    p.tileMargin = 8;
    StageFormat out{};
    auto stage = prepared_clean(Path::Lite, p, recipe, w, h, out);
    AUREA_CHECK(stage != nullptr);
    if (!stage) return;
    const FrameBuffer f0 = to_nv12(rgb, w, h);
    const RunResult r0 = run_stage(*stage, f0, out.width, out.height, 0);
    AUREA_CHECK(r0.status.ok());
    // Quadro 1: o mesmo, com ruído de ±1 em alguns pixels (clipe parado).
    auto noisy = rgb;
    for (usize i = 0; i < noisy.size(); i += 97) noisy[i] = static_cast<u8>(std::clamp(int(noisy[i]) + ((i / 97) % 2 ? 1 : -1), 0, 255));
    const FrameBuffer f1 = to_nv12(noisy, w, h);
    const RunResult r1 = run_stage(*stage, f1, out.width, out.height, 1);
    const auto st1 = neural::clean_stage_stats(*stage);
    std::printf("\n    parado: %u de %u tiles pulados (%.0f ms; quadro cheio %.0f ms)", st1.skipped, st1.tiles, r1.ms, r0.ms);
    AUREA_CHECK_EQ(st1.skipped, st1.tiles);
    AUREA_CHECK(r1.out.nv12 == r0.out.nv12);
    // A mesma entrada sem pular (etapa nova): dentro da tolerância.
    auto fresh = prepared_clean(Path::Lite, p, recipe, w, h, out);
    const RunResult rf = run_stage(*fresh, f1, out.width, out.height, 0);
    const f64 diff = psnr_y(r1.out, rf.out);
    std::printf("\n    pulado vs recalculado: %.2f dB", diff);
    AUREA_CHECK(diff > 40.0);
    // Quadro 2: um retângulo muda num canto: só os tiles tocados (+ margem) rodam.
    auto moved = noisy;
    for (u32 y = 4; y < 40; ++y)
        for (u32 x = 4; x < 40; ++x)
            for (u32 c = 0; c < 3; ++c) moved[(static_cast<usize>(y) * w + x) * 3 + c] = 250;
    const RunResult r2 = run_stage(*stage, to_nv12(moved, w, h), out.width, out.height, 2);
    const auto st2 = neural::clean_stage_stats(*stage);
    std::printf("\n    canto mudou: %u de %u tiles pulados (%.0f ms)\n  ", st2.skipped, st2.tiles, r2.ms);
    AUREA_CHECK(r2.status.ok());
    AUREA_CHECK(st2.skipped >= st2.tiles - 2 && st2.skipped < st2.tiles);
}

AUREA_TEST(HyperNeural, CleanStageKeepsSizeAndRemovesNoise) {
    const u32 w = 128, h = 96;
    const auto clean = synthetic_rgb(w, h, 13);
    auto noisy = clean;
    u32 state = 777;
    for (auto& v : noisy) {
        state = state * 1664525u + 1013904223u;
        const int n = static_cast<int>((state >> 24) % 25) - 12;
        v = static_cast<u8>(std::clamp(int(v) + n, 0, 255));
    }
    Recipe recipe;
    recipe.denoise = 1.0f;
    StageFormat out{};
    auto stage = prepared_clean(Path::Max, cpu_profile(), recipe, w, h, out);
    AUREA_CHECK(stage != nullptr);
    if (!stage) return;
    AUREA_CHECK(out.width == w && out.height == h);
    const FrameBuffer gt = to_nv12(clean, w, h), in = to_nv12(noisy, w, h);
    const RunResult r = run_stage(*stage, in, w, h);
    AUREA_CHECK(r.status.ok());
    const f64 before = psnr_y(in, gt, 4), after = psnr_y(r.out, gt, 4);
    std::printf("\n    ruido: %.2f dB -> limpo pela rede %.2f dB (%.0f ms)\n  ", before, after, r.ms);
    AUREA_CHECK(after > before);
}

// --- Motion ------------------------------------------------------------------
namespace {
std::vector<u8> square_frame(u32 w, u32 h, u32 sx, u32 sy, u32 side) {
    std::vector<u8> img(static_cast<usize>(w) * h * 3);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            // fundo com textura fixa (o RIFE precisa de algo para "prender")
            const u8 g = static_cast<u8>(60 + ((x / 8 + y / 8) % 2) * 30 + (x * 3 + y * 5) % 17);
            const bool in = x >= sx && x < sx + side && y >= sy && y < sy + side;
            for (u32 c = 0; c < 3; ++c) img[(static_cast<usize>(y) * w + x) * 3 + c] = in ? static_cast<u8>(c == 0 ? 240 : 40) : g;
        }
    return img;
}
f64 red_centroid_x(const std::vector<u8>& img, u32 w, u32 h) {
    f64 sx = 0, n = 0;
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u8* p = &img[(static_cast<usize>(y) * w + x) * 3];
            if (p[0] > 180 && p[1] < 90) { sx += x; n += 1; }
        }
    return n > 0 ? sx / n : -1;
}
} // namespace

AUREA_TEST(HyperNeural, RifePutsTheMovingSquareHalfway) {
    for (int variant = 0; variant < 3; ++variant) {
        const Path path = variant == 1 ? Path::Lite : Path::Max;
        DeviceProfile profile = cpu_profile();
        profile.vulkan = variant == 2;   // Max no Vulkan (se o host tiver)
        const u32 w = 256, h = 160, side = 40;
        const auto a = square_frame(w, h, 60, 60, side);
        const auto b = square_frame(w, h, 92, 60, side);
        MotionInterpolator interp;
        AUREA_CHECK(interp.prepare(profile, path, w, h).ok());
        std::vector<u8> out(a.size());
        StageContext ctx;
        const f64 t0 = now_ms();
        const Status s = interp.interpolate_rgb(a.data(), b.data(), 0.5f, out.data(), ctx);
        const f64 ms = now_ms() - t0;
        AUREA_CHECK(s.ok());
        AUREA_CHECK_EQ(interp.last_decision(), 0u);
        const f64 cx = red_centroid_x(out, w, h);
        const f64 expected = (60 + 92) / 2.0 + side / 2.0 - 0.5;
        std::printf("\n    %s%s: centro do quadrado %.2f (esperado %.2f), %.0f ms 256x160", path == Path::Max ? "Max" : "Lite",
                    variant == 2 ? " (Vulkan pedido)" : "", cx, expected, ms);
        AUREA_CHECK(std::fabs(cx - expected) <= 3.0);
        // t = 0,25: um quarto do caminho.
        AUREA_CHECK(interp.interpolate_rgb(a.data(), b.data(), 0.25f, out.data(), ctx).ok());
        const f64 q = red_centroid_x(out, w, h);
        AUREA_CHECK(std::fabs(q - (60 + 8 + side / 2.0 - 0.5)) <= 4.0);
        // Iguais = cópia; corte de cena = o mais próximo.
        AUREA_CHECK(interp.interpolate_rgb(a.data(), a.data(), 0.5f, out.data(), ctx).ok());
        AUREA_CHECK_EQ(interp.last_decision(), 1u);
        AUREA_CHECK(out == a);
        std::vector<u8> cut(a.size(), 250);
        AUREA_CHECK(interp.interpolate_rgb(a.data(), cut.data(), 0.7f, out.data(), ctx).ok());
        AUREA_CHECK_EQ(interp.last_decision(), 2u);
        AUREA_CHECK(out == cut);
    }
    std::printf("\n  ");
}

AUREA_TEST(HyperNeural, FlowRetimeAcceptsSoftwareDecodedChroma) {
    constexpr u32 width = 128, height = 96;
    const FrameBuffer a = to_nv12(square_frame(width, height, 20, 30, 24), width, height);
    const FrameBuffer b = to_nv12(square_frame(width, height, 44, 30, 24), width, height);
    const FrameView first = a.view(), second = b.view();
    std::vector<u8> u(width * height / 4), v(u.size()), vu(u.size() * 2);
    for (usize i = 0; i < u.size(); ++i) {
        u[i] = first.uv[2 * i];
        v[i] = first.uv[2 * i + 1];
        vu[2 * i] = v[i];
        vu[2 * i + 1] = u[i];
    }
    DecodedFrame source, next;
    source.width = next.width = width;
    source.height = next.height = height;
    source.format = PixelFormat::YUV420P;
    source.planeCount = 3;
    source.planes[0] = first.y; source.planes[1] = u.data(); source.planes[2] = v.data();
    source.strides[0] = width;
    source.strides[1] = source.strides[2] = width / 2;
    next.format = PixelFormat::NV12;
    next.planeCount = 2;
    next.planes[0] = second.y; next.planes[1] = second.uv;
    next.strides[0] = next.strides[1] = width;
    AUREA_CHECK(NeuralRetime::supported(source, next));
    NeuralRetime retime;
    NeuralRetime::Field planar, swapped;
    AUREA_CHECK(retime.compute(1, source, next, 0.5f, planar));
    AUREA_CHECK(!planar.copyA && !planar.copyB && !planar.flow.empty());

    source.format = PixelFormat::NV21;
    source.planeCount = 2;
    source.planes[1] = vu.data();
    source.planes[2] = nullptr;
    source.strides[1] = width;
    AUREA_CHECK(NeuralRetime::supported(source, next));
    AUREA_CHECK(retime.compute(2, source, next, 0.5f, swapped));
    AUREA_CHECK(swapped.flow == planar.flow && swapped.mask == planar.mask);
    source.strides[1] = width - 1;
    AUREA_CHECK(!NeuralRetime::supported(source, next));
}

AUREA_TEST(HyperNeural, MotionStageFillsRepeatedFramesAtTheMidpoint) {
    const u32 w = 128, h = 96;
    const FrameBuffer A = to_nv12(square_frame(w, h, 20, 30, 24), w, h);
    const FrameBuffer B = to_nv12(square_frame(w, h, 40, 30, 24), w, h);
    auto stage = neural::make_motion_stage(Path::Max);
    StageFormat out{};
    AUREA_CHECK(stage->prepare(cpu_profile(), Recipe{}, StageFormat{w, h, 60.0}, out).ok());
    AUREA_CHECK_EQ(stage->temporal_radius(), 1u);
    // A A B: o 2º A (igual ao anterior, diferente do seguinte) vira o meio.
    const FrameView views[2] = {A.view(), B.view()};
    const i32 offsets[2] = {-1, 1};
    StageInput in;
    in.current = A.view();
    in.neighbors = std::span<const FrameView>(views, 2);
    in.offsets = std::span<const i32>(offsets, 2);
    FrameBuffer mid;
    (void)mid.resize(w, h);
    StageContext ctx;
    AUREA_CHECK(stage->process(in, mid, ctx).ok());
    AUREA_CHECK(mid.nv12 != A.nv12 && mid.nv12 != B.nv12);
    // Um original (diferente do anterior) passa como está.
    const FrameView views2[2] = {A.view(), B.view()};
    in.current = B.view();
    in.neighbors = std::span<const FrameView>(views2, 2);
    FrameBuffer same;
    (void)same.resize(w, h);
    AUREA_CHECK(stage->process(in, same, ctx).ok());
    AUREA_CHECK(same.nv12 == B.nv12);
}

// --- Face --------------------------------------------------------------------
namespace {
FaceBox g_injected{};
u32 g_injectedCount = 0;
u32 inject_faces(void*, const u8*, u32, u32, u32, FaceBox* boxes, u32 maxBoxes) noexcept {
    const u32 n = std::min(g_injectedCount, maxBoxes);
    for (u32 i = 0; i < n; ++i) boxes[i] = g_injected;
    return n;
}
} // namespace

AUREA_TEST(HyperNeural, GfpganOnlyTouchesTheInjectedFaceArea) {
    const std::string root = repo_root();
    if (root.empty()) {
        std::printf("PULADO (modelo sob demanda ausente: rode tools/hyper_models.py --only gfpgan) ");
        return;
    }
    set_model_directory(root + "/build/hyper-tools/out/ondemand");
    AUREA_CHECK(ondemand_model_ready("gfpgan-v1.4"));
    const u32 w = 640, h = 480;
    const auto rgb = synthetic_rgb(w, h, 21);
    const FrameBuffer in = to_nv12(rgb, w, h);
    DeviceProfile p = cpu_profile();
    // A conversão confere com o PyTorch (padrão 512², saída fp32 guardada).
    f64 refPsnr = 0, refMs = 0;
    AUREA_CHECK(neural::gfpgan_reference_check(p, refPsnr, refMs).ok());
    std::printf("\n    GFPGAN CPU vs PyTorch fp32: %.2f dB, %.0f ms por rosto 512x512", refPsnr, refMs);
    AUREA_CHECK(refPsnr > 35.0);
    if (std::getenv("AUREA_TEST_VULKAN_GFPGAN")) {
        DeviceProfile v = p;
        v.vulkan = true;
        f64 vp = 0, vms = 0;
        const Status vs = neural::gfpgan_reference_check(v, vp, vms);
        std::printf("\n    GFPGAN Vulkan vs PyTorch fp32: %s %.2f dB, %.0f ms", vs.ok() ? "ok" : msg(vs).c_str(), vp, vms);
    }
    auto stage = neural::make_face_stage(Path::Max);
    Recipe recipe;
    recipe.face = true;
    recipe.faceStrength = 1.0f;
    StageFormat out{};
    const Status prep = stage->prepare(p, recipe, StageFormat{w, h, 30.0}, out);
    AUREA_CHECK(prep.ok());
    if (!prep.ok()) { std::printf("%s ", msg(prep).c_str()); return; }
    // Sem detector: nenhum rosto, saída = entrada (byte a byte).
    set_face_detector(nullptr, nullptr);
    RunResult none = run_stage(*stage, in, w, h, 0);
    AUREA_CHECK(none.status.ok());
    AUREA_CHECK(none.out.nv12 == in.nv12);
    AUREA_CHECK_EQ(neural::face_stage_restored(*stage), 0u);
    // Caixa injetada com os 5 pontos (rosto de frente de ~120 px).
    g_injected = FaceBox{};
    g_injected.x = 260; g_injected.y = 180; g_injected.width = 120; g_injected.height = 140;
    const f32 lm[10] = {290, 230, 350, 230, 320, 265, 296, 295, 344, 295};
    std::memcpy(g_injected.landmarks, lm, sizeof(lm));
    g_injected.hasLandmarks = true;
    g_injectedCount = 1;
    set_face_detector(&inject_faces, nullptr);
    RunResult r = run_stage(*stage, in, w, h, 1);
    std::printf("\n    GFPGAN 512x512: %.0f ms (rosto 1 restaurado)", r.ms);
    AUREA_CHECK(r.status.ok());
    AUREA_CHECK_EQ(neural::face_stage_restored(*stage), 1u);
    // Área do recorte alinhado no quadro: olhos a 60 px ⇒ recorte ~244 px em
    // volta do rosto. Fora de um retângulo folgado, NADA muda.
    const u32 x0 = 140, y0 = 60, x1 = 500, y1 = 440;
    usize outsideChanged = 0, insideChanged = 0;
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const bool changed = r.out.nv12[static_cast<usize>(y) * w + x] != in.nv12[static_cast<usize>(y) * w + x];
            const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            (inside ? insideChanged : outsideChanged) += changed;
        }
    std::printf("\n    pixels Y mudados: %zu dentro da area do rosto, %zu fora\n  ", insideChanged, outsideChanged);
    AUREA_CHECK_EQ(outsideChanged, static_cast<usize>(0));
    AUREA_CHECK(insideChanged > 1000);
    // Entre quadros-chave (Max: a cada 2) o detalhe é reaplicado sem a rede.
    RunResult r2 = run_stage(*stage, in, w, h, 2);
    AUREA_CHECK(r2.status.ok());
    AUREA_CHECK_EQ(neural::face_stage_restored(*stage), 1u);
    // Rosto pequeno ou de perfil: ignorado.
    g_injected.width = 30; g_injected.height = 30;
    RunResult small = run_stage(*stage, in, w, h, 10);
    AUREA_CHECK(small.out.nv12 == in.nv12);
    set_face_detector(nullptr, nullptr);
}

// --- Tempos (impressos; o host não é o aparelho) ------------------------------
AUREA_TEST(HyperNeural, PrintsMsPerFrameOnCpuAndVulkan) {
    const u32 w = 640, h = 360;
    const auto rgb = synthetic_rgb(w, h, 17);
    const FrameBuffer in = to_nv12(rgb, w, h);
    for (bool vulkan : {false, true}) {
        DeviceProfile p = cpu_profile();
        p.vulkan = vulkan;
        Recipe recipe;
        recipe.denoise = 0.5f;
        StageFormat fmt{};
        auto stage = prepared_clean(Path::Lite, p, recipe, w, h, fmt);
        AUREA_CHECK(stage != nullptr);
        if (stage && (!vulkan || neural::clean_stage_stats(*stage).vulkan)) {
            const RunResult r = run_stage(*stage, in, fmt.width, fmt.height);
            AUREA_CHECK(r.status.ok());
            std::printf("\n    %-6s Clean Lite: %.0f ms/quadro 640x360", vulkan ? "Vulkan" : "CPU", r.ms);
        }
        MotionInterpolator interp;
        for (Path path : {Path::Lite, Path::Max}) {
            if (!interp.prepare(p, path, w, h).ok()) continue;
            const auto a = square_frame(w, h, 100, 100, 60);
            const auto b = square_frame(w, h, 130, 104, 60);
            std::vector<u8> out(a.size());
            StageContext ctx;
            const f64 t0 = now_ms();
            AUREA_CHECK(interp.interpolate_rgb(a.data(), b.data(), 0.5f, out.data(), ctx).ok());
            std::printf("\n    %-6s %-24s %7.0f ms/quadro 640x360", vulkan ? "Vulkan" : "CPU",
                        path == Path::Lite ? "Motion Lite (RIFE 1/4)" : "Motion Max (RIFE 1/2)", now_ms() - t0);
        }
    }
    std::printf("\n  ");
}
