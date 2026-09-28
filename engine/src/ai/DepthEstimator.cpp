#include "aurea/ai/DepthEstimator.hpp"
#include <net.h>
#include <datareader.h>
#include <gpu.h>
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

namespace aurea::ai {
namespace embedded {
const unsigned char* depth_model_weights();
const char* depth_model_param();
}

// Nomes dos blobs no grafo convertido (ver engine/assets/ai/README.md).
namespace {
constexpr const char* kInputBlob = "in0";
constexpr const char* kOutputBlob = "out0";
}

struct DepthEstimator::Impl {
    // Ponteiro, e não membro: ver `drop_net`.
    std::unique_ptr<ncnn::Net> net;
    bool ready = false;
    bool fallback = false;
    Backend backend = Backend::Cpu;
    f32 lastMs = 0.0f;
    std::vector<u8> rgb = std::vector<u8>(static_cast<usize>(kPixels) * 3);
};

DepthEstimator::DepthEstimator() noexcept {
    try { impl_.reset(new (std::nothrow) Impl); }
    catch (const std::bad_alloc&) { }
}
namespace {
/// Solta a rede. O ncnn destrói o dispositivo Vulkan dele num `atexit`; uma
/// rede que ainda existir depois disso (um Renderer estático, no fim do
/// processo) apontaria para um dispositivo morto e derrubaria o processo ao
/// liberar os buffers. Nesse caso — e só nele — a memória fica com o sistema.
void drop_net(std::unique_ptr<ncnn::Net>& net) noexcept {
    if (!net) return;
#if NCNN_VULKAN
    if (net->opt.use_vulkan_compute && ncnn::get_gpu_instance() == VK_NULL_HANDLE) {
        (void)net.release();
        return;
    }
#endif
    net.reset();
}
}

DepthEstimator::~DepthEstimator() {
    if (impl_) drop_net(impl_->net);
}
bool DepthEstimator::loaded() const noexcept { return impl_ && impl_->ready; }
DepthEstimator::Backend DepthEstimator::backend() const noexcept { return impl_ ? impl_->backend : Backend::Cpu; }
f32 DepthEstimator::last_inference_ms() const noexcept { return impl_ ? impl_->lastMs : 0.0f; }

void DepthEstimator::unload() noexcept {
    if (!impl_) return;
    drop_net(impl_->net);
    impl_->ready = false;
}

Status DepthEstimator::load(Backend requested) try {
    if (!impl_) return Errc::OutOfMemory;
    impl_->ready = false;
    drop_net(impl_->net);
    impl_->net = std::make_unique<ncnn::Net>();
    impl_->backend = Backend::Cpu;
    impl_->fallback = requested == Backend::Auto;
    // As escolhas do Upscaler: um worker de CPU (sem OpenMP), Vulkan quando há
    // GPU de verdade, e tudo em fp32 — o mapa sai igual no host, no Android
    // (Vulkan) e no iOS (CPU). Os pesos vêm em fp16 e viram fp32 aqui.
    // Diferença deliberada: Winograd LIGADO na CPU. Nesta rede ele não muda o
    // mapa (mesmo erro contra o onnxruntime) e corta ~40% do tempo na CPU — a
    // única via da Apple. No Vulkan fica desligado, como no Upscaler.
    impl_->net->opt.num_threads = 1;
    impl_->net->opt.use_vulkan_compute = false;
#if NCNN_VULKAN
    if (requested != Backend::Cpu && ncnn::get_gpu_count() > 0) {
        const int index = ncnn::get_default_gpu_index();
        if (index >= 0 && ncnn::get_gpu_device(index) && ncnn::get_gpu_device(index)->is_valid()
            && (requested == Backend::Vulkan || ncnn::get_gpu_info(index).type() != 3)) {
            impl_->net->set_vulkan_device(index);
            impl_->net->opt.use_vulkan_compute = true;
            impl_->backend = Backend::Vulkan;
        }
    }
#endif
    if (requested == Backend::Vulkan && impl_->backend != Backend::Vulkan)
        return Status{Errc::NotSupported, "Vulkan inference unavailable"};
    impl_->net->opt.use_fp16_packed = false;
    impl_->net->opt.use_fp16_storage = false;
    impl_->net->opt.use_fp16_uniform = false;
    impl_->net->opt.use_fp16_arithmetic = false;
    impl_->net->opt.use_bf16_storage = false;
    impl_->net->opt.use_winograd_convolution = impl_->backend == Backend::Cpu;
    impl_->net->opt.lightmode = true;
    // Os kernels Vulkan com memória local (shared) do ncnn fixado derrubam o
    // dispositivo (VK_ERROR_DEVICE_LOST) na convolução 1×1 816→232 do fim do
    // encoder — visto numa RTX 3050; o Upscaler não tem 1×1 e não passa por
    // eles. Sem memória local: mesmo resultado da CPU (diferença ~5e-6 do
    // intervalo) e ~15 ms por quadro nessa GPU.
    impl_->net->opt.use_shader_local_memory = false;
    const unsigned char* weights = embedded::depth_model_weights();
    ncnn::DataReaderFromMemory reader(weights);   // o leitor avança o ponteiro
    if (impl_->net->load_param_mem(embedded::depth_model_param()) != 0 || impl_->net->load_model(reader) != 0) {
        if (impl_->backend == Backend::Vulkan && impl_->fallback) return load(Backend::Cpu);
        return Status{Errc::CorruptData, "AI depth model could not be loaded"};
    }
    impl_->ready = true;
    AUREA_LOG_INFO("AI depth: %s", impl_->backend == Backend::Vulkan ? "Vulkan" : "CPU");
    return OkStatus;
} catch (const std::bad_alloc&) {
    if (impl_) impl_->ready = false;
    return Errc::OutOfMemory;
}

Status DepthEstimator::run(const u8* pixels, u32 width, u32 height, u32 stride, u32 channels,
                           const std::atomic<bool>& cancel, f32* out) try {
    if (!impl_) return Errc::OutOfMemory;
    if (!impl_->ready) return Errc::InvalidState;
    if (!pixels || !out || !width || !height || width > 16384 || height > 16384
        || (channels != 3 && channels != 4) || stride < width * channels)
        return Errc::InvalidArgument;
    if (cancel.load(std::memory_order_relaxed)) return Errc::Cancelled;
    depth_input_rgb(pixels, width, height, stride, channels, impl_->rgb.data());
    // O grafo recebe RGB 0..1 e subtrai média/divide desvio (ImageNet) sozinho.
    constexpr float norm[3] = {1.f / 255.f, 1.f / 255.f, 1.f / 255.f};
    ncnn::Mat in = ncnn::Mat::from_pixels(impl_->rgb.data(), ncnn::Mat::PIXEL_RGB,
                                          static_cast<int>(kSize), static_cast<int>(kSize));
    if (in.empty()) return Errc::OutOfMemory;
    in.substract_mean_normalize(nullptr, norm);
    ncnn::Mat result;
    auto infer = [&]() {
        auto ex = impl_->net->create_extractor();
        return ex.input(kInputBlob, in) == 0 && ex.extract(kOutputBlob, result) == 0 && !result.empty();
    };
    const u64 t0 = monotonic_ns();
    bool ok = infer();
    if (!ok && impl_->backend == Backend::Vulkan && impl_->fallback) {
        result.release();
        const Status cpu = load(Backend::Cpu);
        if (!cpu.ok()) return cpu;
        AUREA_LOG_WARN("AI depth: Vulkan falhou; seguindo na CPU");
        ok = infer();
    }
    impl_->lastMs = static_cast<f32>(static_cast<f64>(monotonic_ns() - t0) * 1e-6);
    if (!ok) return Status{Errc::BudgetExceeded, "AI depth inference failed"};
    if (result.w != static_cast<int>(kSize) || result.h != static_cast<int>(kSize) || result.c != 1)
        return Status{Errc::CorruptData, "AI depth output shape differs from 256x256"};
    for (u32 y = 0; y < kSize; ++y) {
        const float* row = result.row(static_cast<int>(y));
        for (u32 x = 0; x < kSize; ++x) {
            const float v = row[x];
            out[y * kSize + x] = std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;
        }
    }
    return cancel.load(std::memory_order_relaxed) ? Status{Errc::Cancelled} : OkStatus;
} catch (const std::bad_alloc&) {
    return Errc::OutOfMemory;
}

void depth_input_rgb(const u8* pixels, u32 width, u32 height, u32 stride, u32 channels, u8* out) noexcept {
    constexpr u32 n = DepthEstimator::kSize;
    for (u32 oy = 0; oy < n; ++oy) {
        // Faixa [y0, y1) da fonte coberta por esta linha (ao menos 1 pixel).
        const u32 y0 = static_cast<u32>(static_cast<u64>(oy) * height / n);
        const u32 y1 = std::max(y0 + 1, static_cast<u32>(static_cast<u64>(oy + 1) * height / n));
        for (u32 ox = 0; ox < n; ++ox) {
            const u32 x0 = static_cast<u32>(static_cast<u64>(ox) * width / n);
            const u32 x1 = std::max(x0 + 1, static_cast<u32>(static_cast<u64>(ox + 1) * width / n));
            u32 sum[3] = {0, 0, 0};
            for (u32 y = y0; y < y1 && y < height; ++y) {
                const u8* p = pixels + static_cast<usize>(y) * stride + static_cast<usize>(x0) * channels;
                for (u32 x = x0; x < x1 && x < width; ++x, p += channels) {
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
                }
            }
            const u32 count = (std::min(y1, height) - y0) * (std::min(x1, width) - x0);
            u8* d = out + (static_cast<usize>(oy) * n + ox) * 3;
            for (u32 c = 0; c < 3; ++c) d[c] = static_cast<u8>((sum[c] + count / 2) / std::max(1u, count));
        }
    }
}

void depth_percentiles(const f32* disparity, u32 count, f32& p2, f32& p98) noexcept {
    p2 = 0.0f;
    p98 = 1.0f;
    if (!disparity || count == 0) return;
    std::vector<f32> sorted(disparity, disparity + count);
    const usize lo = static_cast<usize>(count) * 2 / 100;
    const usize hi = std::min<usize>(count - 1, static_cast<usize>(count) * 98 / 100);
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(lo), sorted.end());
    p2 = sorted[lo];
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(hi), sorted.end());
    p98 = sorted[hi];
}

} // namespace aurea::ai
