// =============================================================================
//  Aurea / hyper / neural / NeuralFace.cpp
//
//  Hyper Face Lite/Max: GFPGAN v1.4 (TencentARC, Apache-2.0) SÓ nos recortes
//  de rosto 512×512 que o detector da PLATAFORMA acha (ML Kit / Vision, pela
//  função registrada em HyperNeural.hpp). Sem detector = nenhum rosto.
//
//    * alinhamento: com os 5 pontos, a semelhança (rotação + escala +
//      translação, mínimos quadrados) até o gabarito FFHQ 512 que o GFPGAN
//      espera; sem pontos, recorte quadrado pela caixa;
//    * rostos pequenos (lado < 48 px no Max, 80 no Lite) e de perfil (|yaw| >
//      45°) ficam como estão;
//    * a rede roda no rosto a cada N quadros (Lite 8, Max 2) ou quando o rosto
//      aparece; entre esses, o detector segue o rosto (id ou sobreposição) e o
//      DETALHE restaurado (restaurado − original, no espaço alinhado) é
//      reaplicado sobre o rosto atual;
//    * volta ao quadro com borda suave (rampa de 40 px no recorte), mistura
//      pela força; fora da área do recorte NADA muda (o NV12 é copiado e só os
//      pixels tocados são reescritos).
//
//  O modelo (178 MB em fp16) é sob demanda: <pasta de modelos>/gfpgan-v1.4/.
// =============================================================================
#include "hyper/neural/NeuralCommon.hpp"
#include "hyper/neural/NeuralStages.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/platform/AddressSpace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>

namespace aurea::hyper {

namespace {
std::mutex g_detectorMutex;
FaceDetectorFn g_detector = nullptr;
void* g_detectorContext = nullptr;
} // namespace

void set_face_detector(FaceDetectorFn fn, void* context) noexcept {
    std::lock_guard<std::mutex> lock(g_detectorMutex);
    g_detector = fn;
    g_detectorContext = context;
}

bool face_detector_available() noexcept {
    std::lock_guard<std::mutex> lock(g_detectorMutex);
    return g_detector != nullptr;
}

namespace neural {

u32 detect_faces(const u8* rgb, u32 w, u32 h, u32 stride, FaceBox* boxes, u32 maxBoxes) noexcept {
    FaceDetectorFn fn;
    void* ctx;
    {
        std::lock_guard<std::mutex> lock(g_detectorMutex);
        fn = g_detector;
        ctx = g_detectorContext;
    }
    if (!fn) return 0;
    return std::min(fn(ctx, rgb, w, h, stride, boxes, maxBoxes), maxBoxes);
}

namespace {

constexpr u32 kCrop = 512;
constexpr u32 kFeather = 40;
// Gabarito de 5 pontos do FFHQ para o recorte 512 (o do FaceRestoreHelper do
// facexlib, MIT, usado pelo GFPGAN): olhos, nariz, cantos da boca.
constexpr f32 kTemplate[10] = {192.98138f, 239.94708f, 318.90277f, 240.1936f, 256.63416f, 314.01935f,
                               201.26117f, 371.41043f, 313.08905f, 371.15118f};

/// Semelhança 2D: crop = [a −b; b a]·frame + [tx ty].
struct Similarity {
    f32 a = 1, b = 0, tx = 0, ty = 0;
    void apply(f32 x, f32 y, f32& u, f32& v) const noexcept { u = a * x - b * y + tx; v = b * x + a * y + ty; }
    Similarity inverse() const noexcept {
        const f32 d = a * a + b * b;
        Similarity r;
        r.a = a / d; r.b = -b / d;
        r.tx = -(r.a * tx - r.b * ty);
        r.ty = -(r.b * tx + r.a * ty);
        return r;
    }
};

Similarity fit_similarity(const f32* src, const f32* dst, u32 n) noexcept {
    // Mínimos quadrados de u = a·x − b·y + tx, v = b·x + a·y + ty.
    f32 mx = 0, my = 0, mu = 0, mv = 0;
    for (u32 i = 0; i < n; ++i) { mx += src[2 * i]; my += src[2 * i + 1]; mu += dst[2 * i]; mv += dst[2 * i + 1]; }
    mx /= n; my /= n; mu /= n; mv /= n;
    f32 sxx = 0, sab = 0, sbb = 0;
    for (u32 i = 0; i < n; ++i) {
        const f32 x = src[2 * i] - mx, y = src[2 * i + 1] - my;
        const f32 u = dst[2 * i] - mu, v = dst[2 * i + 1] - mv;
        sxx += x * x + y * y;
        sab += x * u + y * v;
        sbb += x * v - y * u;
    }
    Similarity s;
    if (sxx <= 1e-6f) return s;
    s.a = sab / sxx;
    s.b = sbb / sxx;
    s.tx = mu - (s.a * mx - s.b * my);
    s.ty = mv - (s.b * mx + s.a * my);
    return s;
}

Similarity box_similarity(const FaceBox& f) noexcept {
    // Sem pontos: o rosto ocupa ~280 px dos 512 do FFHQ, centro um pouco
    // abaixo do centro do recorte.
    const f32 side = std::max(f.width, f.height) * (static_cast<f32>(kCrop) / 280.0f);
    const f32 cx = f.x + f.width * 0.5f, cy = f.y + f.height * 0.5f - 0.086f * side;
    Similarity s;
    s.a = static_cast<f32>(kCrop) / side;
    s.b = 0;
    s.tx = kCrop * 0.5f - s.a * cx;
    s.ty = kCrop * 0.5f - s.a * cy;
    return s;
}

bool profile_face(const FaceBox& f) noexcept {
    if (std::fabs(f.yawDegrees) > 45.0f) return true;
    if (!f.hasLandmarks) return false;
    // Nariz fora do vão entre os olhos = perfil.
    const f32 lx = std::min(f.landmarks[0], f.landmarks[2]), rx = std::max(f.landmarks[0], f.landmarks[2]);
    const f32 nose = f.landmarks[4];
    return nose < lx || nose > rx;
}

f32 iou(const FaceBox& a, const FaceBox& b) noexcept {
    const f32 x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const f32 x1 = std::min(a.x + a.width, b.x + b.width), y1 = std::min(a.y + a.height, b.y + b.height);
    const f32 inter = std::max(0.0f, x1 - x0) * std::max(0.0f, y1 - y0);
    const f32 uni = a.width * a.height + b.width * b.height - inter;
    return uni > 0 ? inter / uni : 0.0f;
}

inline f32 bilinear(const u8* img, u32 w, u32 h, u32 stride, f32 x, f32 y, u32 c) noexcept {
    x = std::clamp(x, 0.0f, static_cast<f32>(w - 1));
    y = std::clamp(y, 0.0f, static_cast<f32>(h - 1));
    const u32 x0 = static_cast<u32>(x), y0 = static_cast<u32>(y);
    const u32 x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const f32 fx = x - static_cast<f32>(x0), fy = y - static_cast<f32>(y0);
    const f32 p00 = img[static_cast<usize>(y0) * stride + x0 * 3 + c], p01 = img[static_cast<usize>(y0) * stride + x1 * 3 + c];
    const f32 p10 = img[static_cast<usize>(y1) * stride + x0 * 3 + c], p11 = img[static_cast<usize>(y1) * stride + x1 * 3 + c];
    const f32 top = p00 + (p01 - p00) * fx, bot = p10 + (p11 - p10) * fx;
    return top + (bot - top) * fy;
}

f32 feather(f32 u, f32 v) noexcept {
    const f32 d = std::min(std::min(u, v), std::min(static_cast<f32>(kCrop - 1) - u, static_cast<f32>(kCrop - 1) - v));
    if (d <= 0.0f) return 0.0f;
    if (d >= static_cast<f32>(kFeather)) return 1.0f;
    const f32 t = d / static_cast<f32>(kFeather);
    return t * t * (3.0f - 2.0f * t);
}

struct Track {
    FaceBox box{};
    Similarity toCrop{};
    std::vector<i16> delta;        ///< (restaurado − original) no recorte alinhado, 512²×3
    u32 lastKey = 0;
    u32 seen = 0;                  ///< último quadro em que o detector achou
    bool valid = false;
};

class GfpganStage final : public HyperStage {
public:
    explicit GfpganStage(Path p) : path_(p) {}
    const char* name() const noexcept override { return path_ == Path::Max ? "face.gfpgan-v1.4.max" : "face.gfpgan-v1.4.lite"; }
    StageKind kind() const noexcept override { return StageKind::Face; }
    Path path() const noexcept override { return path_; }
    bool stateful() const noexcept override { return true; }

    Status prepare(const DeviceProfile& profile, const Recipe& recipe, const StageFormat& in,
                   StageFormat& out) noexcept override try {
        release();
        if (!in.width || !in.height || (in.width | in.height) & 1u) return Errc::InvalidArgument;
        Status s = load_ondemand("gfpgan-v1.4", "gfpgan-v1.4-fp16.param", param_);
        std::string weights;
        if (s.ok()) s = verify_ondemand("gfpgan-v1.4", "gfpgan-v1.4-fp16.bin", weights);
        if (!s.ok()) return s;
        NetSource src;
        src.param = param_.blob();
        src.weightsFile = weights;   // o ncnn lê do arquivo e copia: nada nosso fica referenciado
        src.cpuFp16 = true;
        const bool vulkan = profile.vulkan && vulkan_device_available();
        s = runner_.load(src, vulkan ? Backend::Vulkan : Backend::Cpu, profile.vulkanFp16, true);
        if (!s.ok()) return s;
        const u64 fixed = static_cast<u64>(in.width) * in.height * 3 + kCrop * kCrop * 3 * 4ull + (maxFaces() * kCrop * kCrop * 6ull);
        const u64 budget = inference_budget(profile, fixed);
        // Ativações do GFPGAN a 512²: ~260 MB no pico (medido no host, fp32).
        if (budget < (300ull << 20)) return Status{Errc::BudgetExceeded, "memoria insuficiente para o GFPGAN"};
        allocator_.set_budget(budget);
        strength_ = std::clamp(recipe.faceStrength, 0.0f, 1.0f);
        every_ = path_ == Path::Max ? 2u : 8u;
        minSide_ = path_ == Path::Max ? 48.0f : 80.0f;
        rgb_.resize(static_cast<usize>(in.width) * in.height * 3);
        crop_.resize(kCrop * kCrop * 3);
        restored_.resize(kCrop * kCrop * 3);
        tracks_.assign(maxFaces(), Track{});
        for (auto& t : tracks_) t.delta.assign(kCrop * kCrop * 3, 0);
        patch_.nv12.reserve(static_cast<usize>(in.width) * in.height * 3 / 2);
        in_ = in;
        out = in;
        return OkStatus;
    } catch (const std::bad_alloc&) {
        release();
        return Errc::OutOfMemory;
    }

    Status process(const StageInput& in, FrameBuffer& out, const StageContext& ctx) noexcept override try {
        const FrameView& c = in.current;
        if (!runner_.loaded() || !c.valid() || c.width != in_.width || c.height != in_.height) return Errc::InvalidState;
        // Começa com a cópia exata: o que não for rosto não muda.
        for (u32 y = 0; y < c.height; ++y) std::memcpy(out.y() + static_cast<usize>(y) * c.width, c.y + static_cast<usize>(y) * c.yStride, c.width);
        for (u32 y = 0; y < c.height / 2; ++y) std::memcpy(out.uv() + static_cast<usize>(y) * c.width, c.uv + static_cast<usize>(y) * c.uvStride, c.width);
        restoredCount_ = 0;
        if (strength_ <= 0.0f) return OkStatus;
        const u32 stride = in_.width * 3;
        nv12_to_rgb24(c, rgb_.data(), stride);
        FaceBox boxes[8];
        const u32 found = detect_faces(rgb_.data(), in_.width, in_.height, stride, boxes, 8);
        const u32 frame = c.index;
        for (auto& t : tracks_) if (t.valid && frame > t.seen + every_) t.valid = false;
        for (u32 i = 0; i < found; ++i) {
            if (ctx.cancelled()) return Errc::Cancelled;
            const FaceBox& f = boxes[i];
            if (f.confidence < 0.5f || std::max(f.width, f.height) < minSide_ || profile_face(f)) continue;
            Track* track = match(f);
            if (!track) continue;
            const bool fresh = !track->valid;
            track->box = f;
            track->toCrop = f.hasLandmarks ? fit_similarity(f.landmarks, kTemplate, 5) : box_similarity(f);
            warp_crop(track->toCrop);
            if (fresh || frame >= track->lastKey + every_ || frame < track->lastKey) {
                const Status s = restore(ctx);
                if (!s.ok()) return s;
                for (usize k = 0; k < crop_.size(); ++k)
                    track->delta[k] = static_cast<i16>(static_cast<int>(restored_[k]) - static_cast<int>(crop_[k]));
                track->lastKey = frame;
            } else {
                for (usize k = 0; k < crop_.size(); ++k)
                    restored_[k] = static_cast<u8>(std::clamp(static_cast<int>(crop_[k]) + track->delta[k], 0, 255));
            }
            track->seen = frame;
            track->valid = true;
            paste(track->toCrop, out);
            ++restoredCount_;
            ctx.beat();
        }
        return OkStatus;
    } catch (const std::bad_alloc&) {
        return Errc::OutOfMemory;
    }

    void release() noexcept override {
        runner_.release();
        param_.bytes.clear();
        std::vector<u8>().swap(rgb_);
        tracks_.clear();
    }
    u64 peak_bytes() const noexcept override {
        return allocator_.peak() + rgb_.size() + crop_.size() * 2 + tracks_.size() * kCrop * kCrop * 6ull + (180ull << 20);
    }
    [[nodiscard]] u32 restored() const noexcept { return restoredCount_; }

private:
    u32 maxFaces() const noexcept { return path_ == Path::Max ? 4u : 2u; }

    Track* match(const FaceBox& f) noexcept {
        Track* best = nullptr;
        f32 bestIou = 0.3f;
        for (auto& t : tracks_) {
            if (!t.valid) continue;
            if (f.trackId && t.box.trackId == f.trackId) return &t;
            const f32 o = iou(t.box, f);
            if (o > bestIou) { bestIou = o; best = &t; }
        }
        if (best) return best;
        for (auto& t : tracks_) if (!t.valid) return &t;
        return nullptr;
    }

    void warp_crop(const Similarity& toCrop) noexcept {
        const Similarity toFrame = toCrop.inverse();
        const u32 stride = in_.width * 3;
        for (u32 v = 0; v < kCrop; ++v)
            for (u32 u = 0; u < kCrop; ++u) {
                f32 x, y;
                toFrame.apply(static_cast<f32>(u), static_cast<f32>(v), x, y);
                for (u32 ch = 0; ch < 3; ++ch) {
                    const f32 val = bilinear(rgb_.data(), in_.width, in_.height, stride, x, y, ch) + 0.5f;
                    crop_[(static_cast<usize>(v) * kCrop + u) * 3 + ch] = static_cast<u8>(std::min(255.0f, val));
                }
            }
    }

    Status restore(const StageContext& ctx) noexcept {
        ncnn::Mat inMat = ncnn::Mat::from_pixels(crop_.data(), ncnn::Mat::PIXEL_RGB, kCrop, kCrop, &allocator_);
        if (inMat.empty()) return Errc::BudgetExceeded;
        static constexpr float kMean[3] = {127.5f, 127.5f, 127.5f};
        static constexpr float kNorm[3] = {1.0f / 127.5f, 1.0f / 127.5f, 1.0f / 127.5f};
        inMat.substract_mean_normalize(kMean, kNorm);
        ncnn::Mat outMat;
        bool ok = runner_.infer(inMat, outMat, &allocator_);
        if (!ok && runner_.can_fallback() && runner_.fallback_to_cpu().ok()) ok = runner_.infer(inMat, outMat, &allocator_);
        if (!ok || outMat.w != static_cast<int>(kCrop) || outMat.h != static_cast<int>(kCrop) || outMat.c != 3)
            return Status{allocator_.exceeded() ? Errc::BudgetExceeded : Errc::InvalidState, "o GFPGAN falhou"};
        ctx.beat();
        // [-1,1] → 0..255, misturado ao original pela força.
        for (u32 ch = 0; ch < 3; ++ch) {
            const float* plane = outMat.channel(static_cast<int>(ch));
            for (u32 i = 0; i < kCrop * kCrop; ++i) {
                const f32 g = std::clamp((plane[i] + 1.0f) * 127.5f, 0.0f, 255.0f);
                const f32 o = crop_[static_cast<usize>(i) * 3 + ch];
                restored_[static_cast<usize>(i) * 3 + ch] = static_cast<u8>(o + (g - o) * strength_ + 0.5f);
            }
        }
        return OkStatus;
    }

    void paste(const Similarity& toCrop, FrameBuffer& out) noexcept {
        // Retângulo do quadro coberto pelo recorte (pares, para o croma).
        const Similarity toFrame = toCrop.inverse();
        f32 x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        const f32 corners[4][2] = {{0, 0}, {kCrop - 1.0f, 0}, {0, kCrop - 1.0f}, {kCrop - 1.0f, kCrop - 1.0f}};
        for (const auto& p : corners) {
            f32 x, y;
            toFrame.apply(p[0], p[1], x, y);
            x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
        const u32 W = in_.width, H = in_.height;
        const u32 rx0 = static_cast<u32>(std::clamp(std::floor(x0), 0.0f, static_cast<f32>(W))) & ~1u;
        const u32 ry0 = static_cast<u32>(std::clamp(std::floor(y0), 0.0f, static_cast<f32>(H))) & ~1u;
        const u32 rx1 = std::min(W, (static_cast<u32>(std::clamp(std::ceil(x1) + 1.0f, 0.0f, static_cast<f32>(W))) + 1u) & ~1u);
        const u32 ry1 = std::min(H, (static_cast<u32>(std::clamp(std::ceil(y1) + 1.0f, 0.0f, static_cast<f32>(H))) + 1u) & ~1u);
        if (rx1 <= rx0 || ry1 <= ry0) return;
        const u32 pw = rx1 - rx0, ph = ry1 - ry0;
        // RGB do retângulo com o rosto colado, e a máscara (onde mudou).
        patchRgb_.resize(static_cast<usize>(pw) * ph * 3);
        patchMask_.assign(static_cast<usize>(pw) * ph, 0);
        const u32 stride = W * 3;
        for (u32 y = 0; y < ph; ++y)
            for (u32 x = 0; x < pw; ++x) {
                const u8* src = rgb_.data() + static_cast<usize>(ry0 + y) * stride + (rx0 + x) * 3;
                u8* dst = patchRgb_.data() + (static_cast<usize>(y) * pw + x) * 3;
                f32 u, v;
                toCrop.apply(static_cast<f32>(rx0 + x), static_cast<f32>(ry0 + y), u, v);
                const f32 m = (u >= 0 && v >= 0 && u <= kCrop - 1.0f && v <= kCrop - 1.0f) ? feather(u, v) : 0.0f;
                if (m <= 0.0f) { std::memcpy(dst, src, 3); continue; }
                patchMask_[static_cast<usize>(y) * pw + x] = 1;
                for (u32 ch = 0; ch < 3; ++ch) {
                    const f32 r = bilinear(restored_.data(), kCrop, kCrop, kCrop * 3, u, v, ch);
                    const f32 val = src[ch] + (r - src[ch]) * m + 0.5f;
                    dst[ch] = static_cast<u8>(std::clamp(val, 0.0f, 255.0f));
                }
            }
        if (!patch_.resize(pw, ph).ok()) return;
        if (!rgb24_to_nv12(patchRgb_.data(), pw * 3, pw, ph, patch_)) return;
        // Só os pixels tocados (Y) e os blocos 2×2 com algum pixel tocado (UV).
        for (u32 y = 0; y < ph; ++y)
            for (u32 x = 0; x < pw; ++x)
                if (patchMask_[static_cast<usize>(y) * pw + x])
                    out.y()[static_cast<usize>(ry0 + y) * W + rx0 + x] = patch_.y()[static_cast<usize>(y) * pw + x];
        for (u32 y = 0; y < ph; y += 2)
            for (u32 x = 0; x < pw; x += 2) {
                const usize m = static_cast<usize>(y) * pw + x;
                if (!(patchMask_[m] | patchMask_[m + 1] | patchMask_[m + pw] | patchMask_[m + pw + 1])) continue;
                const usize at = static_cast<usize>((ry0 + y) / 2) * W + rx0 + x;
                const usize from = static_cast<usize>(y / 2) * pw + x;
                out.uv()[at] = patch_.uv()[from];
                out.uv()[at + 1] = patch_.uv()[from + 1];
            }
    }

    Path path_;
    NetRunner runner_;
    BoundedAllocator allocator_{512ull << 20};
    OwnedBlob param_;
    std::vector<u8> rgb_, crop_, restored_, patchRgb_, patchMask_;
    std::vector<Track> tracks_;
    FrameBuffer patch_;
    StageFormat in_{};
    f32 strength_ = 0.5f, minSide_ = 80.0f;
    u32 every_ = 8, restoredCount_ = 0;
};

} // namespace

Status gfpgan_reference_check(const DeviceProfile& profile, f64& psnr, f64& ms) noexcept try {
    psnr = 0.0;
    ms = 0.0;
    OwnedBlob param, ref;
    std::string weights;
    Status s = load_ondemand("gfpgan-v1.4", "gfpgan-v1.4-fp16.param", param);
    if (s.ok()) s = verify_ondemand("gfpgan-v1.4", "gfpgan-v1.4-fp16.bin", weights);
    if (s.ok()) s = load_ondemand("gfpgan-v1.4", "reference-512.rgb", ref);
    if (!s.ok()) return s;
    NetSource src;
    src.param = param.blob();
    src.weightsFile = weights;
    src.cpuFp16 = true;
    NetRunner runner;
    const bool vulkan = profile.vulkan && vulkan_device_available();
    s = runner.load(src, vulkan ? Backend::Vulkan : Backend::Cpu, profile.vulkanFp16, true);
    if (!s.ok()) return s;
    std::vector<u8> pattern(kCrop * kCrop * 3), out(kCrop * kCrop * 3);
    reference_pattern(kCrop, kCrop, pattern.data());
    BoundedAllocator allocator(1ull << 30);
    ncnn::Mat in = ncnn::Mat::from_pixels(pattern.data(), ncnn::Mat::PIXEL_RGB, kCrop, kCrop, &allocator);
    static constexpr float kMean[3] = {127.5f, 127.5f, 127.5f};
    static constexpr float kNorm[3] = {1.0f / 127.5f, 1.0f / 127.5f, 1.0f / 127.5f};
    in.substract_mean_normalize(kMean, kNorm);
    ncnn::Mat result;
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = runner.infer(in, result, &allocator);
    if (!ok && runner.can_fallback() && runner.fallback_to_cpu().ok()) ok = runner.infer(in, result, &allocator);
    ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!ok || result.w != static_cast<int>(kCrop) || result.c != 3) return Status{Errc::InvalidState, "o GFPGAN falhou"};
    for (u32 ch = 0; ch < 3; ++ch) {
        const float* plane = result.channel(static_cast<int>(ch));
        for (u32 i = 0; i < kCrop * kCrop; ++i)
            out[static_cast<usize>(i) * 3 + ch] =
                static_cast<u8>(std::min(255.0f, std::clamp((plane[i] + 1.0f) * 127.5f, 0.0f, 255.0f) + 0.5f));
    }
    psnr = psnr_rgb(out.data(), ref.bytes.data(), out.size());
    return OkStatus;
} catch (...) {
    return Errc::OutOfMemory;
}

std::unique_ptr<HyperStage> make_face_stage(Path path) {
    return std::unique_ptr<HyperStage>(new (std::nothrow) GfpganStage(path));
}

u32 face_stage_restored(const HyperStage& stage) noexcept {
    // Sem RTTI (-fno-rtti no Android/iOS): reconhece pelo nome desta classe.
    const char* name = stage.name();
    if (name && (std::strcmp(name, "face.gfpgan-v1.4.max") == 0 || std::strcmp(name, "face.gfpgan-v1.4.lite") == 0))
        return static_cast<const GfpganStage&>(stage).restored();
    return 0;
}

bool face_available(const DeviceProfile&) noexcept {
    // 0.3.4 (bugs-034, hyper): sem restrição de aparelho (32 bits e RAM ficam
    // por conta e risco da pessoa; a tela avisa). Só o modelo baixado e
    // conferido decide. O orçamento de memória do processamento continua.
    return ondemand_model_ready("gfpgan-v1.4");
}

f64 cost_gfpgan(const DeviceProfile& p) noexcept {
    // Um rosto 512² ≈ 900× o tile do SPAN (medido no host); por MP de quadro
    // com um rosto a cada N quadros (Lite 8): o grafo pede ms por MP.
    const f64 ms = p.liteMsPerTile > 0.0f ? p.liteMsPerTile : 300.0;
    return ms * 900.0 / 4.0;
}

} // namespace neural
} // namespace aurea::hyper
