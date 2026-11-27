// =============================================================================
//  Aurea / hyper / neural / NeuralStages.cpp
//
//  `register_neural_stages` (HyperStage.hpp) e o medidor das redes.
//
//  Etapas opcionais do grafo:
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
    add("clean.esrgan-x4v3.lite", StageKind::Clean, Path::Lite, -1,
        [] { return make_clean_stage(Path::Lite); }, nullptr, &cost_esrgan);
    add("clean.esrgan-x4v3.max", StageKind::Clean, Path::Max, -1,
        [] { return make_clean_stage(Path::Max); }, nullptr, &cost_esrgan);
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

// Compare the optional Clean network on Vulkan against its CPU output.
constexpr f64 kPsnrFloat = 38.0;

struct Probe {
    bool ok = false;
    f64 psnr = 0.0;
    f32 ms = 0.0f;
};

f64 now_ms() noexcept {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Probe probe(Backend backend, bool fp16, u32 tile, u32 margin, std::vector<u8>& reference,
            const std::atomic<bool>* cancel) noexcept try {
    Probe p;
    if (cancel && cancel->load()) return p;
    if (backend == Backend::Vulkan && reference.empty()) return p;
    NetRunner runner;
    std::vector<unsigned char> owned;
    if (!load_clean_net(0.5f, backend, fp16, runner, owned).ok() || runner.backend() != backend) return p;
    BoundedAllocator allocator(256ull << 20);
    std::vector<u8> rgb(32u * 32u * 3u), out(128u * 128u * 3u);
    reference_pattern(32, 32, rgb.data());
    static constexpr float kNorm[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    ncnn::Mat in = ncnn::Mat::from_pixels(rgb.data(), ncnn::Mat::PIXEL_RGB, 32, 32, &allocator);
    in.substract_mean_normalize(nullptr, kNorm);
    ncnn::Mat result;
    if (!runner.infer(in, result, &allocator) || result.w != 128 || result.h != 128 || result.c != 3) return p;
    mat_to_rgb(result, 1, 0, 0, 128, 128, out.data(), 128 * 3);
    if (backend == Backend::Cpu) {
        reference = std::move(out);
        p.ok = true;
    } else {
        p.psnr = psnr_rgb(out.data(), reference.data(), reference.size());
        p.ok = p.psnr >= kPsnrFloat;
    }
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

} // namespace

NeuralMeter measure_neural(const DeviceProfile& profile, const std::atomic<bool>* cancel) noexcept try {
    NeuralMeter m;
    const u32 cap = profile.abi64 && !kAddressSpace32 ? 128u : 96u;
    m.tile = std::clamp<u32>(profile.tile ? profile.tile : 96u, 64u, cap);
    m.margin = std::clamp<u32>(profile.tileMargin ? profile.tileMargin : 12u, 8u, 12u);
    char note[256];
    std::string text;
    std::vector<u8> reference;
    const Probe cpu = probe(Backend::Cpu, false, m.tile, m.margin, reference, cancel);
    m.cpuOk = cpu.ok;
    m.cpuMsPerTile = cpu.ms;
    std::snprintf(note, sizeof(note), "clean CPU %.1f ms", cpu.ms);
    text += note;
    if (profile.vulkan && vulkan_device_available()) {
        const Probe vk = probe(Backend::Vulkan, false, m.tile, m.margin, reference, cancel);
        const Probe vk16 = probe(Backend::Vulkan, true, m.tile, m.margin, reference, cancel);
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
    const bool cpuUsable = m.cpuOk && m.cpuMsPerTile > 0.0f;
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
