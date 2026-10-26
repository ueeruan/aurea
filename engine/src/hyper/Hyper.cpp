// =============================================================================
//  Aurea / hyper / Hyper.cpp — receita (faixas), quadros NV12 e conversões.
// =============================================================================
#include "aurea/hyper/Hyper.hpp"
#include "aurea/hyper/HyperStage.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::hyper {

namespace {
f32 unit(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback; }
u8 byte_of(f32 v) noexcept { return static_cast<u8>(std::clamp(std::lround(v), 0l, 255l)); }
} // namespace

Recipe sanitize(const Recipe& in) noexcept {
    Recipe r = in;
    r.upscale = in.upscale >= 3 ? 4u : (in.upscale >= 2 ? 2u : 1u);
    if (static_cast<u8>(r.upscaleMode) > static_cast<u8>(UpscaleMode::Anime)) r.upscaleMode = UpscaleMode::General;
    r.denoise = unit(in.denoise, 0.0f);
    r.deinterlace = std::min<u8>(in.deinterlace, 2);
    r.faceStrength = unit(in.faceStrength, 0.5f);
    // 0 = manter; senão 1..240 (o teto dos encoders do motor).
    r.targetFps = (std::isfinite(in.targetFps) && in.targetFps >= 1.0) ? std::min(in.targetFps, 240.0) : 0.0;
    r.slowMotion = in.slowMotion >= 6 ? 8u : (in.slowMotion >= 3 ? 4u : (in.slowMotion >= 2 ? 2u : 1u));
    r.stabilizeStrength = unit(in.stabilizeStrength, 0.5f);
    if (static_cast<u8>(r.path) > static_cast<u8>(Path::Max)) r.path = Path::Auto;
    return r;
}

bool is_identity(const Recipe& in) noexcept {
    const Recipe r = sanitize(in);
    return r.upscale == 1 && r.denoise <= 0.0f && !r.deblock && r.deinterlace == 0 && !r.face
        && r.targetFps <= 0.0 && r.slowMotion == 1 && !r.stabilize;
}

const char* stage_kind_name(StageKind kind) noexcept {
    switch (kind) {
        case StageKind::Clean: return "clean";
        case StageKind::Stable: return "stable";
        case StageKind::Upscale: return "upscale";
        case StageKind::Face: return "face";
        case StageKind::Motion: return "motion";
        default: return "?";
    }
}

Status FrameBuffer::resize(u32 w, u32 h) noexcept {
    if (!w || !h || (w | h) & 1u) return Status{Errc::InvalidArgument, "NV12 exige dimensoes pares"};
    const usize bytes = static_cast<usize>(w) * h * 3 / 2;
    try {
        nv12.resize(bytes);
    } catch (...) {
        return Status{Errc::OutOfMemory, "memoria insuficiente para o quadro do Hyper"};
    }
    width = w;
    height = h;
    return OkStatus;
}

FrameView FrameBuffer::view() const noexcept {
    FrameView v;
    if (nv12.empty()) return v;
    v.y = nv12.data();
    v.uv = nv12.data() + static_cast<usize>(width) * height;
    v.width = width;
    v.height = height;
    v.yStride = width;
    v.uvStride = width;
    v.ptsUs = ptsUs;
    v.index = index;
    v.sourcePosition = sourcePosition;
    return v;
}

void nv12_to_rgb24(const FrameView& in, u8* rgb, u32 rgbStride) noexcept {
    if (!in.valid() || !rgb) return;
    for (u32 row = 0; row < in.height; ++row) {
        const u8* yl = in.y + static_cast<usize>(row) * in.yStride;
        const u8* cl = in.uv + static_cast<usize>(row / 2) * in.uvStride;
        u8* out = rgb + static_cast<usize>(row) * rgbStride;
        for (u32 col = 0; col < in.width; ++col) {
            const f32 l = (yl[col] - 16.0f) * (255.0f / 219.0f);
            const f32 cb = (cl[col & ~1u] - 128.0f) * (255.0f / 224.0f);
            const f32 cr = (cl[(col & ~1u) + 1] - 128.0f) * (255.0f / 224.0f);
            out[col * 3 + 0] = byte_of(l + 1.5748f * cr);
            out[col * 3 + 1] = byte_of(l - 0.187324f * cb - 0.468124f * cr);
            out[col * 3 + 2] = byte_of(l + 1.8556f * cb);
        }
    }
}

bool rgb24_to_nv12(const u8* rgb, u32 rgbStride, u32 width, u32 height, FrameBuffer& out) noexcept {
    if (!rgb || width != out.width || height != out.height || (width | height) & 1u
        || out.nv12.size() < static_cast<usize>(width) * height * 3 / 2) return false;
    u8* y = out.y();
    u8* uv = out.uv();
    for (u32 row = 0; row < height; row += 2) {
        for (u32 col = 0; col < width; col += 2) {
            f32 cb = 0.0f, cr = 0.0f;
            for (u32 dy = 0; dy < 2; ++dy)
                for (u32 dx = 0; dx < 2; ++dx) {
                    const u8* p = rgb + static_cast<usize>(row + dy) * rgbStride + (col + dx) * 3;
                    const f32 l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
                    y[static_cast<usize>(row + dy) * width + col + dx] = byte_of(16.0f + l * (219.0f / 255.0f));
                    cb += (p[2] - l) / 1.8556f;
                    cr += (p[0] - l) / 1.5748f;
                }
            const usize at = static_cast<usize>(row / 2) * width + col;
            uv[at] = byte_of(128.0f + cb * (224.0f / (4.0f * 255.0f)));
            uv[at + 1] = byte_of(128.0f + cr * (224.0f / (4.0f * 255.0f)));
        }
    }
    return true;
}

} // namespace aurea::hyper
