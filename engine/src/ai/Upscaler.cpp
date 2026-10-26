// Real-ESRGAN animevideov3 (0.1.4), hoje o Hyper Upscale Anime. A carga da
// rede, a escolha de backend (Vulkan com volta para a CPU) e o alocador
// limitado são os das redes do AureaHyper (src/hyper/neural/NeuralCommon);
// aqui fica só a API antiga (tiles com halo de 20 px entregues por callback),
// usada pelo export (ExportSettings::aiUpscale) e pela etapa embutida do motor.
#include "aurea/ai/Upscaler.hpp"

#include "hyper/neural/NeuralCommon.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <vector>

namespace aurea::ai {
namespace embedded {
const unsigned char* model_weights();
const char* model_param(unsigned scale);
}
namespace {
// 18 spatial 3x3 convolutions have input radius 18. Two extra pixels
// also cover the official x2 graph's final bicubic resampling footprint.
constexpr u32 kHalo = 20;
constexpr u64 kInferenceBudget = 48ull * 1024ull * 1024ull;
}

struct Upscaler::Impl {
    hyper::neural::NetRunner runner;
    hyper::neural::BoundedAllocator allocator{kInferenceBudget};
    u32 scale = 0;
    u32 tileSize = 64;
    u64 peak = 0;
};

Upscaler::Upscaler() noexcept {
    try { impl_.reset(new (std::nothrow) Impl); }
    catch (const std::bad_alloc&) { }
}
Upscaler::~Upscaler() = default;
u32 Upscaler::scale() const noexcept { return impl_ ? impl_->scale : 0; }
u64 Upscaler::peak_working_bytes() const noexcept { return impl_ ? impl_->peak : 0; }
Upscaler::Backend Upscaler::backend() const noexcept {
    return impl_ && impl_->runner.backend() == hyper::neural::Backend::Vulkan ? Backend::Vulkan : Backend::Cpu;
}

Status Upscaler::load(u32 scale, u32 threads, u32 tileSize, Backend requested) try {
    if (!impl_) return Errc::OutOfMemory;
    if ((scale != 2 && scale != 4) || threads < 1 || threads > 4 || tileSize < 32 || tileSize > 128)
        return Errc::InvalidArgument;
    impl_->scale = 0;
    impl_->runner.release();
    using hyper::neural::NetSource;
    NetSource src;
    src.param = {reinterpret_cast<const unsigned char*>(embedded::model_param(scale)), 1};
    src.weights = {embedded::model_weights(), 1};
    src.input = "data";
    src.output = "output";
    // This pinned ncnn bicubic Vulkan pass produced black x2 output in pixel
    // parity tests. Keep learned convolutions/shuffle on the GPU, but run the
    // model's final x4->x2 resample through its exact CPU op.
    src.cpuOnlyLayer = scale == 2 ? "Resize_40" : nullptr;
    // GPU: fp16 no armazenamento/empacotamento, conta em fp32 (paridade com a
    // CPU, teste Ai.VulkanMatchesCpu). CPU: Winograd nas 3x3. Um worker: sem
    // OpenMP no ncnn (AureaNcnn.cmake), `threads` fica só registrado.
    const bool wantGpu = requested != Backend::Cpu;
    const Status loaded = impl_->runner.load(src, wantGpu ? hyper::neural::Backend::Vulkan : hyper::neural::Backend::Cpu,
                                             false, requested == Backend::Auto);
    if (requested == Backend::Vulkan && (!loaded.ok() || impl_->runner.backend() != hyper::neural::Backend::Vulkan))
        return Status{Errc::NotSupported, "Vulkan inference unavailable"};
    if (!loaded.ok())
        return loaded.code() == Errc::OutOfMemory ? loaded : Status{Errc::CorruptData, "AI upscale model could not be loaded"};
    impl_->scale = scale;
    impl_->tileSize = tileSize;
    AUREA_LOG_INFO("AI upscale x%u: %s, tile %u", scale,
                   impl_->runner.backend() == hyper::neural::Backend::Vulkan ? "Vulkan" : "CPU", tileSize);
    return OkStatus;
} catch (const std::bad_alloc&) {
    if (impl_) impl_->scale = 0;
    return Errc::OutOfMemory;
}

Status Upscaler::run(const u8* rgb, u32 width, u32 height, u32 stride,
                     const std::atomic<bool>& cancel, TileCallback callback, void* context) try {
    if (!impl_) return Errc::OutOfMemory;
    if (!impl_->scale) return Errc::InvalidState;
    if (!rgb || !callback || !width || !height || width > 16384 || height > 16384 || stride < width * 3u)
        return Errc::InvalidArgument;
    if (cancel.load(std::memory_order_relaxed)) return Errc::Cancelled;
    const u32 scale = impl_->scale, tile = impl_->tileSize;
    const u64 count = static_cast<u64>((width + tile - 1) / tile) * ((height + tile - 1) / tile);
    u64 completed = 0;
    // All inference activations/workspaces share a strict cap, independent of
    // video dimensions. Input/output tile vectors are at most (128+40)^2*4^2*3 bytes.
    impl_->allocator.reset_peak();
    std::vector<u8> input, pixels;
    constexpr float norm[3] = {1.f / 255.f, 1.f / 255.f, 1.f / 255.f};
    constexpr float denorm[3] = {255.f, 255.f, 255.f};
    for (u32 y = 0; y < height; y += tile) for (u32 x = 0; x < width; x += tile) {
        if (cancel.load(std::memory_order_relaxed)) return Errc::Cancelled;
        const u32 x0 = x > kHalo ? x - kHalo : 0, y0 = y > kHalo ? y - kHalo : 0;
        const u32 x1 = std::min(width, x + tile + kHalo), y1 = std::min(height, y + tile + kHalo);
        const u32 tw = x1 - x0, th = y1 - y0;
        input.resize(static_cast<usize>(tw) * th * 3);
        for (u32 row = 0; row < th; ++row)
            std::copy_n(rgb + static_cast<usize>(y0 + row) * stride + x0 * 3u, tw * 3u,
                        input.data() + static_cast<usize>(row) * tw * 3);
        ncnn::Mat in = ncnn::Mat::from_pixels(input.data(), ncnn::Mat::PIXEL_RGB, static_cast<int>(tw), static_cast<int>(th), &impl_->allocator);
        if (in.empty()) return Errc::BudgetExceeded;
        in.substract_mean_normalize(nullptr, norm);
        ncnn::Mat out;
        bool inferred = impl_->runner.infer(in, out, &impl_->allocator);
        if (!inferred && impl_->runner.can_fallback()) {
            // Retry only the failing tile, before delivering it.
            out.release();
            const Status cpu = impl_->runner.fallback_to_cpu();
            if (!cpu.ok()) return cpu;
            AUREA_LOG_WARN("AI Vulkan tile failed; continuing on CPU");
            inferred = impl_->runner.infer(in, out, &impl_->allocator);
        }
        impl_->peak = std::max<u64>(impl_->peak, impl_->allocator.peak());
        if (!inferred)
            return Status{Errc::BudgetExceeded, "AI inference could not allocate or process a tile"};
        if (cancel.load(std::memory_order_relaxed)) return Errc::Cancelled;
        if (out.w != static_cast<int>(tw * scale) || out.h != static_cast<int>(th * scale) || out.c != 3)
            return Status{Errc::CorruptData, "AI model output shape differs from selected scale"};
        out.substract_mean_normalize(nullptr, denorm);
        const u32 outStride = static_cast<u32>(out.w) * 3;
        pixels.resize(static_cast<usize>(outStride) * out.h);
        out.to_pixels(pixels.data(), ncnn::Mat::PIXEL_RGB);
        Tile result;
        result.x = x * scale; result.y = y * scale;
        result.width = std::min(tile, width - x) * scale;
        result.height = std::min(tile, height - y) * scale;
        result.stride = outStride;
        result.rgb = pixels.data() + static_cast<usize>((y - y0) * scale) * outStride + (x - x0) * scale * 3;
        result.progress = static_cast<float>(++completed) / static_cast<float>(count);
        if (!callback(context, result)) return Errc::Cancelled;
    }
    return cancel.load(std::memory_order_relaxed) ? Status{Errc::Cancelled} : OkStatus;
} catch (const std::bad_alloc&) {
    return Errc::OutOfMemory;
}
} // namespace aurea::ai
