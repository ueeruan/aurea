#include "aurea/ai/Upscaler.hpp"
#include <net.h>
#include <datareader.h>
#include <gpu.h>
#include <layer.h>
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
constexpr usize kInferenceBudget = 48u * 1024u * 1024u;

class BoundedAllocator final : public ncnn::Allocator {
public:
    explicit BoundedAllocator(u64& peak) : peak_(peak) { peak_ = 0; }
    void* fastMalloc(size_t size) override {
        if (size > kInferenceBudget - 64 || live_ > kInferenceBudget - size - 64) return nullptr;
        auto* base = static_cast<unsigned char*>(ncnn::fastMalloc(size + 64));
        if (!base) return nullptr;
        *reinterpret_cast<size_t*>(base) = size + 64;
        live_ += size + 64;
        peak_ = std::max<u64>(peak_, live_);
        return base + 64;
    }
    void fastFree(void* ptr) override {
        if (!ptr) return;
        auto* base = static_cast<unsigned char*>(ptr) - 64;
        live_ -= *reinterpret_cast<size_t*>(base);
        ncnn::fastFree(base);
    }
private:
    size_t live_ = 0;
    u64& peak_;
};
}

struct Upscaler::Impl {
    ncnn::Net net;
    u32 scale = 0;
    u32 tileSize = 64;
    u64 peak = 0;
    Backend backend = Backend::Cpu;
    bool fallback = false;
};

Upscaler::Upscaler() noexcept {
    try { impl_.reset(new (std::nothrow) Impl); }
    catch (const std::bad_alloc&) { }
}
Upscaler::~Upscaler() = default;
u32 Upscaler::scale() const noexcept { return impl_ ? impl_->scale : 0; }
u64 Upscaler::peak_working_bytes() const noexcept { return impl_ ? impl_->peak : 0; }
Upscaler::Backend Upscaler::backend() const noexcept { return impl_ ? impl_->backend : Backend::Cpu; }

Status Upscaler::load(u32 scale, u32 threads, u32 tileSize, Backend requested) try {
    if (!impl_) return Errc::OutOfMemory;
    if ((scale != 2 && scale != 4) || threads < 1 || threads > 4 || tileSize < 32 || tileSize > 128)
        return Errc::InvalidArgument;
    impl_->scale = 0;
    impl_->net.clear();
    impl_->backend = Backend::Cpu;
    impl_->fallback = requested == Backend::Auto;
    // Baseline is a single CPU worker on every platform. No OpenMP runtime or
    // graphics-device dependency is injected into the editor's render backend.
    impl_->net.opt.num_threads = 1;
    impl_->net.opt.use_vulkan_compute = false;
#if NCNN_VULKAN
    if (requested != Backend::Cpu && ncnn::get_gpu_count() > 0) {
        const int index = ncnn::get_default_gpu_index();
        if (index >= 0 && ncnn::get_gpu_device(index) && ncnn::get_gpu_device(index)->is_valid()
            && (requested == Backend::Vulkan || ncnn::get_gpu_info(index).type() != 3)) {
            impl_->net.set_vulkan_device(index);
            impl_->net.opt.use_vulkan_compute = true;
            impl_->backend = Backend::Vulkan;
        }
    }
#endif
    if (requested == Backend::Vulkan && impl_->backend != Backend::Vulkan)
        return Status{Errc::NotSupported, "Vulkan inference unavailable"};
    impl_->net.opt.use_fp16_packed = false;
    impl_->net.opt.use_fp16_storage = false;
    impl_->net.opt.use_fp16_uniform = false;
    impl_->net.opt.use_fp16_arithmetic = false;
    impl_->net.opt.use_bf16_storage = false;
    impl_->net.opt.use_winograd_convolution = false;
    impl_->net.opt.lightmode = true;
    const unsigned char* weights = embedded::model_weights();
    ncnn::DataReaderFromMemory reader(weights);
    // The raw-pointer overload reports bytes consumed, ignoring load errors.
    // The reader overload propagates allocation/weight parsing failures.
    const int paramResult = impl_->net.load_param_mem(embedded::model_param(scale));
#if NCNN_VULKAN
    if (paramResult == 0 && impl_->backend == Backend::Vulkan && scale == 2) {
        // This pinned ncnn bicubic Vulkan pass produced black x2 output in
        // pixel parity tests. Keep learned convolutions/shuffle on the GPU,
        // but run the model's final x4->x2 resample through its exact CPU op.
        for (auto* layer : impl_->net.mutable_layers())
            if (layer && layer->name == "Resize_40") layer->support_vulkan = false;
    }
#endif
    if (paramResult != 0 || impl_->net.load_model(reader) != 0)
    {
        if (impl_->backend == Backend::Vulkan && impl_->fallback)
            return load(scale, threads, tileSize, Backend::Cpu);
        return Status{Errc::CorruptData, "AI upscale model could not be loaded"};
    }
    impl_->scale = scale;
    impl_->tileSize = tileSize;
    AUREA_LOG_INFO("AI upscale x%u: %s, tile %u", scale,
                   impl_->backend == Backend::Vulkan ? "Vulkan" : "CPU", tileSize);
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
    // This allocator only sees this worker: NCNN_OPENMP is disabled. All
    // inference activations/workspaces share a strict cap, independent of video
    // dimensions. Input/output tile vectors are at most (128+40)^2*4^2*3 bytes.
    BoundedAllocator allocator(impl_->peak);
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
        ncnn::Mat in = ncnn::Mat::from_pixels(input.data(), ncnn::Mat::PIXEL_RGB, static_cast<int>(tw), static_cast<int>(th), &allocator);
        if (in.empty()) return Errc::BudgetExceeded;
        in.substract_mean_normalize(nullptr, norm);
        ncnn::Mat out;
        auto infer = [&]() {
            auto ex = impl_->net.create_extractor();
            ex.set_blob_allocator(&allocator);
            ex.set_workspace_allocator(&allocator);
            return ex.input("data", in) == 0 && ex.extract("output", out) == 0 && !out.empty();
        };
        bool inferred = infer();
        if (!inferred && impl_->backend == Backend::Vulkan && impl_->fallback) {
            // Retry only the failing tile, before delivering it. The extractor
            // has released its GPU resources before the network is cleared.
            out.release();
            const Status cpu = load(scale, 1, tile, Backend::Cpu);
            if (!cpu.ok()) return cpu;
            AUREA_LOG_WARN("AI Vulkan tile failed; continuing on CPU");
            inferred = infer();
        }
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
