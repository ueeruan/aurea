// =============================================================================
//  Aurea / hyper / neural / NeuralStages.cpp
//
//  `register_neural_stages` (HyperStage.hpp) e o medidor das redes.
//
//  Etapas (prioridade 10: ganham do Real-ESRGAN embutido do motor, 0):
//    upscale.general.span.lite         SPAN ×2/×4 (int8 CPU / fp16 Vulkan)
//    upscale.general.esrgan-x4v3.max   realesr-general-x4v3 ⊕ wdn (denoise)
//    upscale.anime.animevideov3.lite   realesr-animevideov3 (int8 CPU)
//    upscale.anime.animevideov3.max    realesr-animevideov3
//    clean.esrgan-x4v3.lite / .max     x4v3 ⊕ wdn + média 4×4 (ruído, blocos)
//    motion.rife-v4.6.lite / .max      RIFE v4.6, fluxo a 1/4 · 1/2 + warp cheio
//    face.gfpgan-v1.4.lite / .max      GFPGAN v1.4 nos rostos (sob demanda)
// =============================================================================
#include "hyper/neural/NeuralCommon.hpp"
#include "hyper/neural/NeuralStages.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/platform/AddressSpace.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <new>

namespace aurea::hyper {

using namespace neural;

void register_neural_stages(StageRegistry& r) {
    auto add = [&](const char* id, StageKind kind, Path path, i32 mode, std::unique_ptr<HyperStage> (*create)(),
                   bool (*available)(const DeviceProfile&) noexcept, f64 (*cost)(const DeviceProfile&) noexcept) {
        StageDescriptor d;
        d.id = id;
        d.kind = kind;
        d.path = path;
        d.mode = mode;
        d.priority = 10;
        d.create = create;
        d.available = available;
        d.msPerMegapixel = cost;
        r.add(d);
    };
    constexpr i32 kGeneral = static_cast<i32>(UpscaleMode::General);
    constexpr i32 kAnime = static_cast<i32>(UpscaleMode::Anime);
    add("upscale.general.span.lite", StageKind::Upscale, Path::Lite, kGeneral,
        [] { return make_upscale_stage(StageKind::Upscale, Path::Lite, UpscaleMode::General); }, nullptr, &cost_span_lite);
    add("upscale.general.esrgan-x4v3.max", StageKind::Upscale, Path::Max, kGeneral,
        [] { return make_upscale_stage(StageKind::Upscale, Path::Max, UpscaleMode::General); }, nullptr, &cost_esrgan);
    add("upscale.anime.animevideov3.lite", StageKind::Upscale, Path::Lite, kAnime,
        [] { return make_upscale_stage(StageKind::Upscale, Path::Lite, UpscaleMode::Anime); }, nullptr, &cost_anime);
    add("upscale.anime.animevideov3.max", StageKind::Upscale, Path::Max, kAnime,
        [] { return make_upscale_stage(StageKind::Upscale, Path::Max, UpscaleMode::Anime); }, nullptr, &cost_anime);
    add("clean.esrgan-x4v3.lite", StageKind::Clean, Path::Lite, -1,
        [] { return make_upscale_stage(StageKind::Clean, Path::Lite, UpscaleMode::General); }, nullptr, &cost_esrgan);
    add("clean.esrgan-x4v3.max", StageKind::Clean, Path::Max, -1,
        [] { return make_upscale_stage(StageKind::Clean, Path::Max, UpscaleMode::General); }, nullptr, &cost_esrgan);
    add("motion.rife-v4.6.lite", StageKind::Motion, Path::Lite, -1, [] { return make_motion_stage(Path::Lite); }, nullptr,
        &cost_rife);
    add("motion.rife-v4.6.max", StageKind::Motion, Path::Max, -1, [] { return make_motion_stage(Path::Max); }, nullptr,
        &cost_rife);
    add("face.gfpgan-v1.4.lite", StageKind::Face, Path::Lite, -1, [] { return make_face_stage(Path::Lite); },
        &face_available, &cost_gfpgan);
    add("face.gfpgan-v1.4.max", StageKind::Face, Path::Max, -1, [] { return make_face_stage(Path::Max); },
        &face_available, &cost_gfpgan);
}

// --- Medidor ---------------------------------------------------------------------
namespace {

// Conferência contra a saída fp32 do PyTorch (tools/hyper_models.py) no
// padrão de referência 32×32 do SPAN ×2. Medido no host: fp32/fp16 ≥ 45 dB,
// int8 ≈ 33 dB; um driver que erra dá < 20 dB (lixo, preto, faixas).
constexpr f64 kPsnrFloat = 38.0;
constexpr f64 kPsnrInt8 = 30.0;

struct Probe {
    bool ok = false;
    f64 psnr = 0.0;
    f32 ms = 0.0f;
};

f64 now_ms() noexcept {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Probe probe(Backend backend, bool fp16, bool int8, u32 tile, u32 margin, const std::atomic<bool>* cancel) noexcept try {
    Probe p;
    const Blob ref = embedded("span-x2/reference-32-x2.rgb");
    if (!ref || ref.size != 64u * 64u * 3u) return p;
    NetRunner runner;
    std::vector<unsigned char> owned;
    if (!load_upscale_net(0, 0.5f, backend, fp16, int8, runner, owned).ok() || runner.backend() != backend) return p;
    BoundedAllocator allocator(256ull << 20);
    std::vector<u8> rgb(32u * 32u * 3u), out(64u * 64u * 3u);
    reference_pattern(32, 32, rgb.data());
    static constexpr float kNorm[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    ncnn::Mat in = ncnn::Mat::from_pixels(rgb.data(), ncnn::Mat::PIXEL_RGB, 32, 32, &allocator);
    in.substract_mean_normalize(nullptr, kNorm);
    ncnn::Mat result;
    if (!runner.infer(in, result, &allocator) || result.w != 64 || result.h != 64 || result.c != 3) return p;
    mat_to_rgb(result, 1, 0, 0, 64, 64, out.data(), 64 * 3);
    p.psnr = psnr_rgb(out.data(), ref.data, ref.size);
    p.ok = p.psnr >= kPsnrFloat;   // `int8` aqui = modo rápido do SPAN (fp16 na conta da CPU)
    if (!p.ok || (cancel && cancel->load())) return p;
    // Tempo: um tile Lite inteiro (miolo + 2·margem), o melhor de 3.
    const u32 ext = tile + 2 * margin;
    std::vector<u8> big(static_cast<usize>(ext) * ext * 3);
    reference_pattern(ext, ext, big.data());
    ncnn::Mat tin = ncnn::Mat::from_pixels(big.data(), ncnn::Mat::PIXEL_RGB, static_cast<int>(ext), static_cast<int>(ext), &allocator);
    tin.substract_mean_normalize(nullptr, kNorm);
    f64 best = 1e30;
    for (int i = 0; i < 3; ++i) {
        if (cancel && cancel->load()) break;
        ncnn::Mat tout;
        const f64 t0 = now_ms();
        if (!runner.infer(tin, tout, &allocator)) { p.ok = false; return p; }
        const f64 dt = now_ms() - t0;
        if (i > 0 || backend == Backend::Cpu) best = std::min(best, dt);   // GPU: a 1ª compila/aquece
    }
    p.ms = best < 1e29 ? static_cast<f32>(best) : 0.0f;
    return p;
} catch (...) {
    return Probe{};
}

/// O int8 misto do animevideov3 (Lite Anime na CPU) contra o mesmo modelo
/// em fp32 NESTA CPU, no padrão 32×32 (não há saída de PyTorch para ele).
f64 anime_int8_psnr() noexcept try {
    NetRunner fp, q;
    std::vector<unsigned char> owned;
    if (!load_upscale_net(3, 0.5f, Backend::Cpu, false, false, fp, owned).ok()) return 0.0;
    if (!load_upscale_net(3, 0.5f, Backend::Cpu, false, true, q, owned).ok()) return 0.0;
    BoundedAllocator allocator(256ull << 20);
    std::vector<u8> rgb(32u * 32u * 3u), a(128u * 128u * 3u), b(128u * 128u * 3u);
    reference_pattern(32, 32, rgb.data());
    static constexpr float kNorm[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    ncnn::Mat in = ncnn::Mat::from_pixels(rgb.data(), ncnn::Mat::PIXEL_RGB, 32, 32, &allocator);
    in.substract_mean_normalize(nullptr, kNorm);
    ncnn::Mat ra, rb;
    if (!fp.infer(in, ra, &allocator) || !q.infer(in, rb, &allocator) || ra.w != 128 || rb.w != 128) return 0.0;
    mat_to_rgb(ra, 1, 0, 0, 128, 128, a.data(), 128 * 3);
    mat_to_rgb(rb, 1, 0, 0, 128, 128, b.data(), 128 * 3);
    return psnr_rgb(a.data(), b.data(), a.size());
} catch (...) {
    return 0.0;
}

} // namespace

NeuralMeter measure_neural(const DeviceProfile& profile, const std::atomic<bool>* cancel) noexcept try {
    NeuralMeter m;
    const u32 cap = profile.abi64 && !kAddressSpace32 ? 128u : 96u;
    m.tile = std::clamp<u32>(profile.tile ? profile.tile : 96u, 64u, cap);
    m.margin = std::clamp<u32>(profile.tileMargin ? profile.tileMargin : 12u, 8u, 12u);
    char note[256];
    std::string text;
    const Probe cpu = probe(Backend::Cpu, false, false, m.tile, m.margin, cancel);
    const Probe fast = probe(Backend::Cpu, false, true, m.tile, m.margin, cancel);
    const f64 anime8 = anime_int8_psnr();
    m.cpuOk = cpu.ok && fast.ok;
    m.cpuInt8Ok = anime8 >= kPsnrInt8;
    m.cpuMsPerTile = fast.ok ? fast.ms : cpu.ms;
    std::snprintf(note, sizeof(note), "cpu fp32 %.1f dB %.1f ms; cpu fp16-onde-houver %.1f dB %.1f ms; anime int8 vs fp32 %.1f dB",
                  cpu.psnr, cpu.ms, fast.psnr, fast.ms, anime8);
    text += note;
    if (profile.vulkan && vulkan_device_available()) {
        const Probe vk = probe(Backend::Vulkan, false, false, m.tile, m.margin, cancel);
        const Probe vk16 = probe(Backend::Vulkan, true, false, m.tile, m.margin, cancel);
        m.vulkanOk = vk.ok;
        m.vulkanFp16Ok = vk16.ok;
        m.vulkanMsPerTile = vk16.ok ? vk16.ms : vk.ms;
        std::snprintf(note, sizeof(note), "; vulkan fp32 %.1f dB %.1f ms; fp16 %.1f dB %.1f ms%s", vk.psnr, vk.ms, vk16.psnr,
                      vk16.ms, (!vk.ok && !vk16.ok) ? " (recusado: resultado errado)" : "");
        text += note;
    } else {
        text += "; vulkan ausente";
    }
    const bool vkUsable = (m.vulkanOk || m.vulkanFp16Ok) && m.vulkanMsPerTile > 0.0f;
    const bool cpuUsable = (m.cpuOk || m.cpuInt8Ok) && m.cpuMsPerTile > 0.0f;
    if (vkUsable && (!cpuUsable || m.vulkanMsPerTile < m.cpuMsPerTile)) {
        m.bestIsVulkan = true;
        m.bestMsPerTile = m.vulkanMsPerTile;
    } else if (cpuUsable) {
        m.bestMsPerTile = m.cpuMsPerTile;
    }
    m.note = text;
    return m;
} catch (...) {
    NeuralMeter m;
    m.note = "medidor das redes falhou (memoria)";
    return m;
}

void apply_neural_meter(const NeuralMeter& meter, DeviceProfile& profile) noexcept try {
    // `vulkan` passa a ser "Vulkan USÁVEL pelas redes": presente E com o tile
    // de referência certo. Um driver que erra fica na CPU.
    profile.vulkan = profile.vulkan && (meter.vulkanOk || meter.vulkanFp16Ok) && meter.bestIsVulkan;
    profile.vulkanFp16 = profile.vulkan && meter.vulkanFp16Ok;
    profile.liteMsPerTile = meter.bestMsPerTile;
    profile.tile = meter.tile;
    profile.tileMargin = meter.margin;
    if (!profile.backendNote.empty()) profile.backendNote += "; ";
    profile.backendNote += "redes: " + meter.note;
} catch (...) {
}

} // namespace aurea::hyper
