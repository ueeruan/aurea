// =============================================================================
//  Aurea / hyper / neural / NeuralMotion.cpp
//
//  Hyper Motion Lite/Max: RIFE v4.6 (rife-ncnn-vulkan 20221029, MIT).
//
//  A rede só calcula o FLUXO e a máscara, numa versão reduzida dos quadros
//  (Lite 1/4; Max 1/2, inteira até 720p). O quadro novo é montado em
//  resolução cheia pelo warp dos quadros originais com o fluxo ampliado:
//      out = warp(A, f₀)·m + warp(B, f₁)·(1 − m)
//  (a mesma composição da última etapa do RIFE v4, blobs 327 e 332 do grafo).
//  Assim o quadro interpolado custa a rede pequena + um warp, não a rede em
//  4K. Quadros iguais = cópia; corte de cena = o mais próximo.
//
//  A camada personalizada `rife.Warp` do grafo é implementada AQUI (warp para
//  trás bilinear com a borda repetida — grid_sample align_corners=True,
//  padding "border", como o RIFE define), não copiada de outro projeto.
// =============================================================================
#include "hyper/neural/NeuralCommon.hpp"
#include "hyper/neural/NeuralStages.hpp"

#include "aurea/core/Log.hpp"

#include <layer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace aurea::hyper::neural {

namespace {

inline f32 sample_clamped(const float* plane, int w, int h, f32 x, f32 y) noexcept {
    x = std::clamp(x, 0.0f, static_cast<f32>(w - 1));
    y = std::clamp(y, 0.0f, static_cast<f32>(h - 1));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const f32 fx = x - static_cast<f32>(x0), fy = y - static_cast<f32>(y0);
    const f32 a = plane[y0 * w + x0], b = plane[y0 * w + x1];
    const f32 c = plane[y1 * w + x0], d = plane[y1 * w + x1];
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

/// rife.Warp: entrada [c,h,w] + fluxo [2,h,w] (pixels) → [c,h,w].
class RifeWarp final : public ncnn::Layer {
public:
    RifeWarp() {
        one_blob_only = false;
        support_inplace = false;
        support_packing = false;
        support_vulkan = false;
        support_fp16_storage = false;
        support_bf16_storage = false;
    }
    int forward(const std::vector<ncnn::Mat>& inputs, std::vector<ncnn::Mat>& outputs, const ncnn::Option& opt) const override {
        // A rede no Vulkan com fp16 no armazenamento pode entregar fp16 a esta
        // camada de CPU: converte para fp32 antes da conta.
        ncnn::Mat image = inputs[0], flow = inputs[1];
        if (image.elembits() == 16) ncnn::cast_float16_to_float32(inputs[0], image, opt);
        if (flow.elembits() == 16) ncnn::cast_float16_to_float32(inputs[1], flow, opt);
        if (image.elempack != 1 || flow.elempack != 1 || image.elembits() != 32 || flow.elembits() != 32) return -1;
        const int w = image.w, h = image.h, c = image.c;
        if (flow.w != w || flow.h != h || flow.c < 2) return -1;
        ncnn::Mat& top = outputs[0];
        top.create(w, h, c, static_cast<size_t>(4u), opt.blob_allocator);
        if (top.empty()) return -100;
        const float* fx = flow.channel(0);
        const float* fy = flow.channel(1);
        for (int q = 0; q < c; ++q) {
            const float* src = image.channel(q);
            float* dst = top.channel(q);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const int i = y * w + x;
                    dst[i] = sample_clamped(src, w, h, static_cast<f32>(x) + fx[i], static_cast<f32>(y) + fy[i]);
                }
        }
        return 0;
    }
};

} // namespace

ncnn::Layer* rife_warp_creator(void*) { return new RifeWarp; }

} // namespace aurea::hyper::neural

namespace aurea::hyper {

using namespace neural;

struct MotionInterpolator::Impl {
    NetRunner runner;
    WorkerPool pool;
    BoundedAllocator allocator{256ull << 20};
    u32 width = 0, height = 0, div = 1, nw = 0, nh = 0, pw = 0, ph = 0;
    std::vector<u8> rgbA, rgbB, rgbOut;
    ncnn::Mat in0, in1, in2, flow, mask;
    u32 decision = 0;
    u64 peak = 0;

    void downscale(const u8* rgb, ncnn::Mat& m) const noexcept {
        // Média em caixa div×div, depois a borda repetida até o múltiplo de 32.
        const f32 inv = 1.0f / (255.0f * static_cast<f32>(div * div));
        for (u32 c = 0; c < 3; ++c) {
            float* plane = m.channel(static_cast<int>(c));
            for (u32 y = 0; y < ph; ++y) {
                const u32 sy = std::min(y, nh - 1);
                for (u32 x = 0; x < pw; ++x) {
                    const u32 sx = std::min(x, nw - 1);
                    u32 acc = 0;
                    for (u32 dy = 0; dy < div; ++dy) {
                        const u32 yy = std::min(height - 1, sy * div + dy);
                        const u8* row = rgb + static_cast<usize>(yy) * width * 3;
                        for (u32 dx = 0; dx < div; ++dx) acc += row[std::min(width - 1, sx * div + dx) * 3 + c];
                    }
                    plane[y * pw + x] = static_cast<f32>(acc) * inv;
                }
            }
        }
    }
};

MotionInterpolator::MotionInterpolator() noexcept {
    try { impl_.reset(new Impl); } catch (...) {}
}
MotionInterpolator::~MotionInterpolator() = default;
u64 MotionInterpolator::peak_bytes() const noexcept {
    if (!impl_) return 0;
    return impl_->allocator.peak() + impl_->rgbA.size() * 3 + 11ull * 1024 * 1024;
}
u32 MotionInterpolator::last_decision() const noexcept { return impl_ ? impl_->decision : 0; }

Status MotionInterpolator::prepare(const DeviceProfile& profile, Path path, u32 width, u32 height) noexcept try {
    if (!impl_) return Errc::OutOfMemory;
    if (!width || !height || (width | height) & 1u) return Errc::InvalidArgument;
    Impl& m = *impl_;
    m.width = width;
    m.height = height;
    const u64 px = static_cast<u64>(width) * height;
    if (path == Path::Lite) m.div = px >= 1280ull * 720 ? 4 : 2;
    else m.div = px > 1280ull * 720 ? 2 : 1;
    m.nw = (width + m.div - 1) / m.div;
    m.nh = (height + m.div - 1) / m.div;
    m.pw = (m.nw + 31) / 32 * 32;
    m.ph = (m.nh + 31) / 32 * 32;
    NetSource src;
    src.param = embedded("rife-v4.6/flownet.param");
    src.weights = embedded("rife-v4.6/flownet.bin");
    src.rifeWarp = true;
    src.input = "in0";
    src.output = "327";
    const bool vulkan = profile.vulkan && vulkan_device_available();
    Status s = m.runner.load(src, vulkan ? Backend::Vulkan : Backend::Cpu, profile.vulkanFp16, true);
    if (!s.ok()) return s;
    // Ativações do RIFE na resolução da rede: ~1,1 KB por pixel (medido no host).
    const u64 need = static_cast<u64>(m.pw) * m.ph * 1200ull;
    const u64 fixed = px * 3 * 3 + static_cast<u64>(m.pw) * m.ph * 4 * 12;
    const u64 budget = inference_budget(profile, fixed);
    if (need > budget) return Status{Errc::BudgetExceeded, "memoria insuficiente para a interpolacao"};
    m.allocator.set_budget(budget);
    m.in0.create(static_cast<int>(m.pw), static_cast<int>(m.ph), 3, static_cast<size_t>(4u));
    m.in1.create(static_cast<int>(m.pw), static_cast<int>(m.ph), 3, static_cast<size_t>(4u));
    m.in2.create(static_cast<int>(m.pw), static_cast<int>(m.ph), 1, static_cast<size_t>(4u));
    if (m.in0.empty() || m.in1.empty() || m.in2.empty()) return Errc::OutOfMemory;
    m.rgbA.resize(px * 3);
    m.rgbB.resize(px * 3);
    m.rgbOut.resize(px * 3);
    s = m.pool.start(m.runner.backend() == Backend::Vulkan ? cpu_workers(profile) : cpu_workers(profile));
    if (!s.ok()) return s;
    return OkStatus;
} catch (const std::bad_alloc&) {
    return Errc::OutOfMemory;
}

Status MotionInterpolator::interpolate_rgb(const u8* a, const u8* b, f32 t, u8* out, const StageContext& ctx) noexcept try {
    if (!impl_ || !impl_->runner.loaded()) return Errc::InvalidState;
    Impl& m = *impl_;
    const usize bytes = static_cast<usize>(m.width) * m.height * 3;
    t = std::clamp(t, 0.0f, 1.0f);
    // Iguais (quadro duplicado na fonte) ou corte de cena: sem rede.
    u64 diff = 0, count = 0;
    for (usize i = 0; i < bytes; i += 3 * 7) { diff += static_cast<u64>(std::abs(int(a[i]) - int(b[i]))); ++count; }
    const f64 mean = count ? static_cast<f64>(diff) / static_cast<f64>(count) : 0.0;
    if (mean < 0.5) { std::memcpy(out, a, bytes); m.decision = 1; return OkStatus; }
    if (mean > 48.0) { std::memcpy(out, t < 0.5f ? a : b, bytes); m.decision = 2; return OkStatus; }
    m.decision = 0;
    m.downscale(a, m.in0);
    m.downscale(b, m.in1);
    m.in2.fill(t);
    ctx.beat();
    if (ctx.cancelled()) return Errc::Cancelled;
    auto run = [&]() -> bool {
        ncnn::Extractor ex = m.runner.net().create_extractor();
        ex.set_blob_allocator(&m.allocator);
        ex.set_workspace_allocator(&m.allocator);
        if (ex.input("in0", m.in0) != 0 || ex.input("in1", m.in1) != 0 || ex.input("in2", m.in2) != 0) return false;
        // O fluxo ANTES da máscara: no modo leve o Split que consome o 327
        // ainda não rodou quando ele é extraído.
        if (ex.extract("327", m.flow) != 0 || ex.extract("332", m.mask) != 0) return false;
        return !m.flow.empty() && !m.mask.empty() && m.flow.c == 4 && m.mask.c == 1 &&
               m.flow.w == static_cast<int>(m.pw) && m.flow.h == static_cast<int>(m.ph);
    };
    bool ok = run();
    if (!ok && m.runner.can_fallback() && m.runner.fallback_to_cpu().ok()) ok = run();
    m.peak = std::max(m.peak, m.allocator.peak());
    if (!ok) return Status{Errc::InvalidState, "a interpolacao falhou"};
    ctx.beat();
    // Warp em resolução cheia: o fluxo (pixels da rede) ampliado por div.
    const f32 div = static_cast<f32>(m.div);
    const int pw = static_cast<int>(m.pw), ph = static_cast<int>(m.ph);
    const float* f0x = m.flow.channel(0);
    const float* f0y = m.flow.channel(1);
    const float* f1x = m.flow.channel(2);
    const float* f1y = m.flow.channel(3);
    const float* mk = m.mask.channel(0);
    const u32 W = m.width, H = m.height;
    const u32 bands = std::max<u32>(1, m.pool.size() * 4);
    std::function<void(u32, u32)> job = [&](u32 band, u32) {
        const u32 y0 = H * band / bands, y1 = H * (band + 1) / bands;
        for (u32 y = y0; y < y1; ++y) {
            const f32 ny = (static_cast<f32>(y) + 0.5f) / div - 0.5f;
            u8* dst = out + static_cast<usize>(y) * W * 3;
            for (u32 x = 0; x < W; ++x) {
                const f32 nx = (static_cast<f32>(x) + 0.5f) / div - 0.5f;
                const f32 ax = static_cast<f32>(x) + sample_clamped(f0x, pw, ph, nx, ny) * div;
                const f32 ay = static_cast<f32>(y) + sample_clamped(f0y, pw, ph, nx, ny) * div;
                const f32 bx = static_cast<f32>(x) + sample_clamped(f1x, pw, ph, nx, ny) * div;
                const f32 by = static_cast<f32>(y) + sample_clamped(f1y, pw, ph, nx, ny) * div;
                const f32 w0 = std::clamp(sample_clamped(mk, pw, ph, nx, ny), 0.0f, 1.0f);
                // Bilinear com a borda repetida nos quadros RGB8 cheios.
                auto px = [&](const u8* img, f32 sx, f32 sy, u32 c) -> f32 {
                    sx = std::clamp(sx, 0.0f, static_cast<f32>(W - 1));
                    sy = std::clamp(sy, 0.0f, static_cast<f32>(H - 1));
                    const u32 x0 = static_cast<u32>(sx), yy0 = static_cast<u32>(sy);
                    const u32 x1 = std::min(x0 + 1, W - 1), yy1 = std::min(yy0 + 1, H - 1);
                    const f32 fx = sx - static_cast<f32>(x0), fy = sy - static_cast<f32>(yy0);
                    const f32 p00 = img[(static_cast<usize>(yy0) * W + x0) * 3 + c], p01 = img[(static_cast<usize>(yy0) * W + x1) * 3 + c];
                    const f32 p10 = img[(static_cast<usize>(yy1) * W + x0) * 3 + c], p11 = img[(static_cast<usize>(yy1) * W + x1) * 3 + c];
                    const f32 top = p00 + (p01 - p00) * fx, bot = p10 + (p11 - p10) * fx;
                    return top + (bot - top) * fy;
                };
                for (u32 c = 0; c < 3; ++c) {
                    const f32 v = px(a, ax, ay, c) * w0 + px(b, bx, by, c) * (1.0f - w0) + 0.5f;
                    dst[x * 3 + c] = static_cast<u8>(v <= 0.0f ? 0 : v >= 255.0f ? 255 : static_cast<int>(v));
                }
            }
        }
    };
    m.pool.run(bands, job);
    m.flow.release();
    m.mask.release();
    return ctx.cancelled() ? Status{Errc::Cancelled} : OkStatus;
} catch (const std::bad_alloc&) {
    return Errc::OutOfMemory;
}

Status MotionInterpolator::interpolate(const FrameView& a, const FrameView& b, f32 t, FrameBuffer& out,
                                       const StageContext& ctx) noexcept {
    if (!impl_ || !a.valid() || !b.valid()) return Errc::InvalidArgument;
    Impl& m = *impl_;
    if (a.width != m.width || a.height != m.height || b.width != m.width || b.height != m.height ||
        out.width != m.width || out.height != m.height)
        return Errc::InvalidArgument;
    nv12_to_rgb24(a, m.rgbA.data(), m.width * 3);
    nv12_to_rgb24(b, m.rgbB.data(), m.width * 3);
    const Status s = interpolate_rgb(m.rgbA.data(), m.rgbB.data(), t, m.rgbOut.data(), ctx);
    if (!s.ok()) return s;
    if (m.decision == 1 || m.decision == 2) {
        // Cópia exata do NV12 (sem ida e volta pelo RGB).
        const FrameView& src = (m.decision == 1 || t < 0.5f) ? a : b;
        for (u32 y = 0; y < m.height; ++y) std::memcpy(out.y() + static_cast<usize>(y) * m.width, src.y + static_cast<usize>(y) * src.yStride, m.width);
        for (u32 y = 0; y < m.height / 2; ++y) std::memcpy(out.uv() + static_cast<usize>(y) * m.width, src.uv + static_cast<usize>(y) * src.uvStride, m.width);
        return OkStatus;
    }
    return rgb24_to_nv12(m.rgbOut.data(), m.width * 3, m.width, m.height, out) ? OkStatus : Status{Errc::InvalidArgument};
}

namespace neural {
namespace {

bool same_frame(const FrameView& a, const FrameView& b) noexcept {
    if (a.y == b.y) return true;
    if (a.width != b.width || a.height != b.height) return false;
    u64 diff = 0, count = 0;
    for (u32 y = 0; y < a.height; y += 3) {
        const u8* ra = a.y + static_cast<usize>(y) * a.yStride;
        const u8* rb = b.y + static_cast<usize>(y) * b.yStride;
        for (u32 x = 0; x < a.width; x += 5) { diff += static_cast<u64>(std::abs(int(ra[x]) - int(rb[x]))); ++count; }
    }
    return count && diff * 2 < count;   // média < 0,5 nível
}

/// Etapa do grafo. Convenção enquanto o grafo não passar (A, B, fase) — ver
/// docs/diagnostico/hyper-neural-requests.md: o render repete o quadro de
/// origem; um quadro IGUAL ao anterior e DIFERENTE do seguinte é o meio de
/// (anterior, seguinte). Cobre ×2 (30→60, câmera lenta ×2); o resto passa.
class RifeStage final : public HyperStage {
public:
    explicit RifeStage(Path p) : path_(p) {}
    const char* name() const noexcept override { return path_ == Path::Max ? "motion.rife-v4.6.max" : "motion.rife-v4.6.lite"; }
    StageKind kind() const noexcept override { return StageKind::Motion; }
    Path path() const noexcept override { return path_; }
    u32 temporal_radius() const noexcept override { return 1; }
    Status prepare(const DeviceProfile& profile, const Recipe&, const StageFormat& in, StageFormat& out) noexcept override {
        out = in;
        return interp_.prepare(profile, path_, in.width, in.height);
    }
    Status process(const StageInput& in, FrameBuffer& out, const StageContext& ctx) noexcept override {
        // O grafo dá o par de origem e a fase (StageInput::motionA/B/Phase)
        // quando sabe onde o quadro cai na fonte (FrameView::sourcePosition):
        // vale para x2, x4 e x8. Sem isso, a convenção da repetição abaixo.
        if (in.current.sourcePosition >= 0.0 && in.motionA.valid()) {
            if (in.motionPhase > 0.0f && in.motionB.valid() && in.motionB.y != in.motionA.y)
                return interp_.interpolate(in.motionA, in.motionB, in.motionPhase, out, ctx);
            const FrameView& c = in.current;
            for (u32 y = 0; y < c.height; ++y) std::memcpy(out.y() + static_cast<usize>(y) * c.width, c.y + static_cast<usize>(y) * c.yStride, c.width);
            for (u32 y = 0; y < c.height / 2; ++y) std::memcpy(out.uv() + static_cast<usize>(y) * c.width, c.uv + static_cast<usize>(y) * c.uvStride, c.width);
            return OkStatus;
        }
        const FrameView* prev = nullptr;
        const FrameView* next = nullptr;
        for (usize i = 0; i < in.neighbors.size() && i < in.offsets.size(); ++i) {
            if (in.offsets[i] == -1) prev = &in.neighbors[i];
            if (in.offsets[i] == 1) next = &in.neighbors[i];
        }
        if (prev && next && same_frame(in.current, *prev) && !same_frame(in.current, *next))
            return interp_.interpolate(*prev, *next, 0.5f, out, ctx);
        const FrameView& c = in.current;
        for (u32 y = 0; y < c.height; ++y) std::memcpy(out.y() + static_cast<usize>(y) * c.width, c.y + static_cast<usize>(y) * c.yStride, c.width);
        for (u32 y = 0; y < c.height / 2; ++y) std::memcpy(out.uv() + static_cast<usize>(y) * c.width, c.uv + static_cast<usize>(y) * c.uvStride, c.width);
        return OkStatus;
    }
    u64 peak_bytes() const noexcept override { return interp_.peak_bytes(); }
private:
    Path path_;
    MotionInterpolator interp_;
};

} // namespace

std::unique_ptr<HyperStage> make_motion_stage(Path path) {
    return std::unique_ptr<HyperStage>(new (std::nothrow) RifeStage(path));
}

f64 cost_rife(const DeviceProfile& p) noexcept {
    // Por MP de SAÍDA: a rede a 1/16 (Lite) dos pixels custa ~6× o SPAN por
    // pixel da rede (medido no host) + o warp (~25 ms/MP numa CPU de celular).
    const f64 tile = p.tile ? p.tile : 96.0;
    const f64 ms = p.liteMsPerTile > 0.0f ? p.liteMsPerTile : 300.0;
    return ms * 1e6 / (tile * tile) * 6.0 / 16.0 + 25.0;
}

} // namespace neural
} // namespace aurea::hyper
