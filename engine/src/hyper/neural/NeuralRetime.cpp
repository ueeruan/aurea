// =============================================================================
//  Aurea / hyper / neural / NeuralRetime.cpp — ver NeuralRetime.hpp.
// =============================================================================
#include "hyper/neural/NeuralRetime.hpp"

#include "aurea/hyper/Hyper.hpp"
#include "aurea/hyper/HyperNeural.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace aurea {

namespace {

u32 vis_w(const DecodedFrame& f) noexcept { return f.visibleWidth ? f.visibleWidth : f.width; }
u32 vis_h(const DecodedFrame& f) noexcept { return f.visibleHeight ? f.visibleHeight : f.height; }

u16 to_half(f32 f) noexcept {
    const u32 x = std::bit_cast<u32>(f);
    const u32 sign = (x >> 16) & 0x8000u;
    const i32 exp = static_cast<i32>((x >> 23) & 0xFFu) - 127 + 15;
    u32 mant = x & 0x7FFFFFu;
    if (exp <= 0) return static_cast<u16>(sign);              // muito pequeno: zero
    if (exp >= 31) return static_cast<u16>(sign | 0x7BFFu);   // satura no maior finito
    mant += 0x1000u;                                          // arredonda
    if (mant & 0x800000u) { mant = 0; return static_cast<u16>(sign | ((exp + 1) << 10)); }
    return static_cast<u16>(sign | (static_cast<u32>(exp) << 10) | (mant >> 13));
}

constexpr usize kKeep = 4;   // campos prontos guardados (pausa + vizinhos)

} // namespace

NeuralRetime::NeuralRetime() = default;

NeuralRetime::~NeuralRetime() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

u64 NeuralRetime::key_of(u64 pairKey, f32 t) noexcept {
    const u64 q = static_cast<u64>(std::lround(std::clamp(t, 0.0f, 1.0f) * 4096.0f));
    return (pairKey ^ 0x4E455552414Cull) * 0x9E3779B97F4A7C15ull + q;
}

bool NeuralRetime::supported(DecodedFrame& a, DecodedFrame& b) noexcept {
    for (DecodedFrame* f : {&a, &b}) {
        if (f->format != PixelFormat::NV12 || f->rotation != 0) return false;
        if ((!f->planes[0] || f->planeCount < 2) && !f->prepare_cpu_planes()) return false;
        if (!f->planes[0] || !f->planes[1]) return false;
    }
    return vis_w(a) == vis_w(b) && vis_h(a) == vis_h(b);
}

bool NeuralRetime::to_rgb(DecodedFrame& f, std::vector<u8>& rgb, u32& w, u32& h) noexcept try {
    w = vis_w(f) & ~1u;
    h = vis_h(f) & ~1u;
    if (!w || !h) return false;
    hyper::FrameView v;
    v.y = f.planes[0] + static_cast<usize>(f.cropTop) * f.strides[0] + f.cropLeft;
    v.uv = f.planes[1] + static_cast<usize>(f.cropTop / 2) * f.strides[1] + (f.cropLeft & ~1u);
    v.width = w;
    v.height = h;
    v.yStride = f.strides[0];
    v.uvStride = f.strides[1];
    rgb.resize(static_cast<usize>(w) * h * 3);
    hyper::nv12_to_rgb24(v, rgb.data(), w * 3);
    return true;
} catch (...) {
    return false;
}

bool NeuralRetime::run(Job& job, Field& out) noexcept try {
    std::lock_guard<std::mutex> lock(netMutex_);
    if (!net_ || netW_ != job.width || netH_ != job.height) {
        net_ = std::make_unique<hyper::MotionInterpolator>();
        hyper::DeviceProfile profile;
        profile.abi64 = sizeof(void*) == 8;
#if defined(__APPLE__)
        profile.vulkan = false;   // ncnn na CPU (Core ML: próximo passo)
#else
        profile.vulkan = true;    // ncnn decide (vulkan_device_available) e cai para a CPU
#endif
        if (!net_->prepare(profile, hyper::Path::Max, job.width, job.height).ok()) {
            net_.reset();
            netW_ = netH_ = 0;
            return false;
        }
        netW_ = job.width;
        netH_ = job.height;
    }
    hyper::MotionInterpolator::Fields fields;
    if (!net_->fields_rgb(job.rgbA.data(), job.rgbB.data(), job.t, fields, hyper::StageContext{}).ok()) return false;
    out = Field{};
    out.key = job.key;
    if (fields.decision == 1) { out.copyA = true; return true; }
    if (fields.decision == 2) { (job.t < 0.5f ? out.copyA : out.copyB) = true; return true; }
    out.width = fields.gridW;
    out.height = fields.gridH;
    out.validU = static_cast<f32>(fields.netW) / static_cast<f32>(fields.gridW);
    out.validV = static_cast<f32>(fields.netH) / static_cast<f32>(fields.gridH);
    const usize n = static_cast<usize>(fields.gridW) * fields.gridH;
    out.flow.resize(n * 4);
    out.mask.assign(n * 4, 0);
    // Px da rede → uv da imagem (a parte válida da grade é a imagem inteira).
    const f32 sx = 1.0f / static_cast<f32>(fields.netW), sy = 1.0f / static_cast<f32>(fields.netH);
    for (usize i = 0; i < n; ++i) {
        out.flow[i * 4 + 0] = to_half(fields.flow[0 * n + i] * sx);
        out.flow[i * 4 + 1] = to_half(fields.flow[1 * n + i] * sy);
        out.flow[i * 4 + 2] = to_half(fields.flow[2 * n + i] * sx);
        out.flow[i * 4 + 3] = to_half(fields.flow[3 * n + i] * sy);
        out.mask[i * 4 + 0] = to_half(fields.mask[i]);
        out.mask[i * 4 + 3] = to_half(1.0f);
    }
    return true;
} catch (...) {
    return false;
}

bool NeuralRetime::compute(u64 key, DecodedFrame& a, DecodedFrame& b, f32 t, Field& out) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Field& f : done_)
            if (f.key == key) { out = f; return true; }
    }
    Job job;
    job.key = key;
    job.t = t;
    u32 wb = 0, hb = 0;
    if (!to_rgb(a, job.rgbA, job.width, job.height) || !to_rgb(b, job.rgbB, wb, hb) || wb != job.width || hb != job.height)
        return false;
    if (!run(job, out)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    done_.insert(done_.begin(), out);
    if (done_.size() > kKeep) done_.resize(kKeep);
    return true;
}

bool NeuralRetime::request(u64 key, DecodedFrame& a, DecodedFrame& b, f32 t, Field& out) noexcept try {
    std::unique_lock<std::mutex> lock(mutex_);
    for (const Field& f : done_)
        if (f.key == key) { out = f; return true; }
    if (runningKey_ == key || (pending_ && pending_->key == key)) return false;
    lock.unlock();
    auto job = std::make_unique<Job>();
    job->key = key;
    job->t = t;
    u32 wb = 0, hb = 0;
    if (!to_rgb(a, job->rgbA, job->width, job->height) || !to_rgb(b, job->rgbB, wb, hb) || wb != job->width || hb != job->height)
        return false;
    lock.lock();
    pending_ = std::move(job);
    if (!thread_.joinable()) thread_ = std::thread([this] { worker(); });
    lock.unlock();
    wake_.notify_one();
    return false;
} catch (...) {
    return false;
}

void NeuralRetime::worker() noexcept {
    for (;;) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || pending_ != nullptr; });
            if (stop_) return;
            job = std::move(pending_);
            runningKey_ = job->key;
        }
        Field field;
        const bool ok = run(*job, field);
        void (*fn)(void*) = nullptr;
        void* ctx = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            runningKey_ = 0;
            if (ok) {
                done_.insert(done_.begin(), std::move(field));
                if (done_.size() > kKeep) done_.resize(kKeep);
                fn = readyFn_;
                ctx = readyCtx_;
            }
        }
        if (fn) fn(ctx);
    }
}

void NeuralRetime::set_ready_callback(void (*fn)(void*), void* ctx) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    readyFn_ = fn;
    readyCtx_ = ctx;
}

void NeuralRetime::clear() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_.clear();
        pending_.reset();
    }
    std::lock_guard<std::mutex> lock(netMutex_);
    net_.reset();
    netW_ = netH_ = 0;
}

} // namespace aurea
