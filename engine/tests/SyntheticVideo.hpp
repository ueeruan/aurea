// =============================================================================
//  Vídeo sintético para os testes: um "decoder" que se comporta como H.264.
//
//  Tem GOP (seek cai no keyframe anterior e precisa andar até o alvo), conta
//  seeks e frames descartados, e pode simular custo de decode. Os frames são
//  NV12 de verdade (planos Y e CbCr na memória), então o MESMO caminho de
//  conversão de cor do fallback sem zero-copy roda na GPU do host.
//
//  Padrões:
//    Quadrants   quatro quadrantes vermelho/verde/azul/branco em BT.709
//                limitado — a cor exata que o shader tem de devolver.
//    FrameGray   cinza uniforme cujo nível codifica o índice do frame: o teste
//                sabe QUAL frame foi mostrado lendo um pixel.
// =============================================================================
#pragma once

#include "aurea/media/MediaManager.hpp"
#include "aurea/media/VideoSource.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace aurea::test {

enum class SyntheticPattern : u8 { Quadrants = 0, FrameGray };

struct SyntheticConfig {
    u32 width = 64;
    u32 height = 36;
    f64 fps = 30.0;
    u32 frameCount = 300;
    u32 gop = 30;                 ///< keyframe a cada N frames
    u32 decodeCostUs = 0;         ///< custo artificial por frame
    SyntheticPattern pattern = SyntheticPattern::Quadrants;
    VideoColorInfo color{};
    u32 rotation = 0;
};

/// Código Y'CbCr (8 bits) de uma cor R'G'B' codificada, na matriz e faixa dadas.
inline void rgb_to_ycbcr8(f32 r, f32 g, f32 b, const VideoColorInfo& c, u8& y, u8& cb, u8& cr) {
    f32 kr = 0, kb = 0;
    c.coefficients(kr, kb);
    const f32 kg = 1.0f - kr - kb;
    const f32 Y = kr * r + kg * g + kb * b;
    const f32 Cb = (b - Y) / (2.0f * (1.0f - kb));
    const f32 Cr = (r - Y) / (2.0f * (1.0f - kr));
    auto q = [](f32 v) { return static_cast<u8>(std::lround(std::fmin(255.0f, std::fmax(0.0f, v)))); };
    if (c.fullRange) {
        y = q(Y * 255.0f);
        cb = q(Cb * 255.0f + 128.0f);
        cr = q(Cr * 255.0f + 128.0f);
    } else {
        y = q(16.0f + 219.0f * Y);
        cb = q(128.0f + 224.0f * Cb);
        cr = q(128.0f + 224.0f * Cr);
    }
}

/// Nível de cinza (código Y) que o padrão FrameGray usa para o frame `i`.
inline u8 frame_gray_code(u32 i) { return static_cast<u8>(32 + (i * 7) % 190); }

class SyntheticFrame final : public DecodedFrame {
public:
    std::vector<u8> y, uv;
};

class SyntheticDecoder final : public VideoDecoderBackend {
public:
    explicit SyntheticDecoder(const SyntheticConfig& c) : cfg_(c) {
        info_.codedWidth = c.width;
        info_.codedHeight = c.height;
        info_.fps = c.fps;
        info_.durationUs = static_cast<i64>(std::llround(static_cast<f64>(c.frameCount) * 1e6 / c.fps));
        info_.color = c.color;
        info_.rotation = c.rotation;
        std::snprintf(info_.codec, sizeof(info_.codec), "%s", "video/sintetico");
        std::snprintf(info_.decoderName, sizeof(info_.decoderName), "%s", "sintetico");
    }

    const VideoStreamInfo& info() const noexcept override { return info_; }

    Status seek_to_keyframe(i64 targetUs) noexcept override {
        const u32 target = frame_of(targetUs);
        pos_ = (target / cfg_.gop) * cfg_.gop;
        seeks.fetch_add(1);
        return OkStatus;
    }

    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& eos) noexcept override {
        eos = false;
        if (pos_ >= cfg_.frameCount) { eos = true; outPtsUs = pts_of(cfg_.frameCount - 1); return OkStatus; }
        if (cfg_.decodeCostUs) std::this_thread::sleep_for(std::chrono::microseconds(cfg_.decodeCostUs));
        const u32 index = pos_++;
        decoded.fetch_add(1);
        outPtsUs = pts_of(index);
        eos = pos_ >= cfg_.frameCount;
        if (outPtsUs < deliverFromUs) { discarded.fetch_add(1); return OkStatus; }
        out = FrameRef::adopt(make_frame(index));
        delivered.fetch_add(1);
        return OkStatus;
    }

    u32 max_live_frames() const noexcept override { return 12; }
    i64 keyframe_interval_us() const noexcept override {
        return static_cast<i64>(std::llround(static_cast<f64>(cfg_.gop) * 1e6 / cfg_.fps));
    }

    [[nodiscard]] i64 pts_of(u32 index) const {
        return static_cast<i64>(std::llround(static_cast<f64>(index) * 1e6 / cfg_.fps));
    }
    [[nodiscard]] u32 frame_of(i64 us) const {
        const f64 f = static_cast<f64>(us) * cfg_.fps / 1e6 + 0.5;
        const u32 i = f <= 0.0 ? 0u : static_cast<u32>(f);
        return i >= cfg_.frameCount ? cfg_.frameCount - 1 : i;
    }

    std::atomic<u32> seeks{0};
    std::atomic<u32> decoded{0};
    std::atomic<u32> discarded{0};
    std::atomic<u32> delivered{0};

private:
    DecodedFrame* make_frame(u32 index) {
        auto* f = new SyntheticFrame();
        const u32 w = cfg_.width, h = cfg_.height;
        f->ptsUs = pts_of(index);
        f->width = w;
        f->height = h;
        f->visibleWidth = w;
        f->visibleHeight = h;
        f->rotation = cfg_.rotation;
        f->format = PixelFormat::NV12;
        f->color = cfg_.color;
        f->bufferId = index;
        f->y.assign(static_cast<usize>(w) * h, 0);
        f->uv.assign(static_cast<usize>((w + 1) / 2) * ((h + 1) / 2) * 2, 128);
        for (u32 yy = 0; yy < h; ++yy) {
            for (u32 xx = 0; xx < w; ++xx) {
                u8 Y = 0, Cb = 128, Cr = 128;
                if (cfg_.pattern == SyntheticPattern::FrameGray) {
                    Y = frame_gray_code(index);
                } else {
                    const bool right = xx >= w / 2, bottom = yy >= h / 2;
                    const f32 r = (!right && !bottom) || (right && bottom) ? 1.0f : 0.0f;
                    const f32 g = (right && !bottom) || (right && bottom) ? 1.0f : 0.0f;
                    const f32 b = (!right && bottom) || (right && bottom) ? 1.0f : 0.0f;
                    rgb_to_ycbcr8(r, g, b, cfg_.color, Y, Cb, Cr);
                }
                f->y[static_cast<usize>(yy) * w + xx] = Y;
                if ((xx % 2 == 0) && (yy % 2 == 0)) {
                    const usize ci = (static_cast<usize>(yy / 2) * ((w + 1) / 2) + xx / 2) * 2;
                    f->uv[ci] = Cb;
                    f->uv[ci + 1] = Cr;
                }
            }
        }
        f->planes[0] = f->y.data();
        f->planes[1] = f->uv.data();
        f->strides[0] = w;
        f->strides[1] = ((w + 1) / 2) * 2;
        f->planeCount = 2;
        return f;
    }

    SyntheticConfig cfg_;
    VideoStreamInfo info_;
    u32 pos_ = 0;
};

/// Fábrica para o MediaManager/Engine: todo asset abre um vídeo sintético com
/// a configuração dada. Guarda o último decoder criado para o teste inspecionar.
class SyntheticFactory final : public VideoSourceFactory {
public:
    explicit SyntheticFactory(const SyntheticConfig& c) : cfg_(c) {}

    bool probe(const char*, MediaProbe& out) override {
        SyntheticDecoder d(cfg_);
        out.video = d.info();
        out.video.color.fromStream = true;
        out.hasVideo = true;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        auto d = std::make_unique<SyntheticDecoder>(cfg_);
        last = d.get();
        ++opened;
        return d;
    }

    SyntheticDecoder* last = nullptr;
    u32 opened = 0;

private:
    SyntheticConfig cfg_;
};

} // namespace aurea::test
