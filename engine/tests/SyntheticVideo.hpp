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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace aurea::test {

enum class SyntheticPattern : u8 { Quadrants = 0, FrameGray, MovingSquare, FastSquare, Scene3D };

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
    /// Trilha de áudio sintética (0 = sem áudio): senoide por canal, canal c
    /// em audioFreq·(c+1) Hz, amplitude 0,5.
    u32 audioRate = 0;
    u32 audioChannels = 2;
    f64 audioFreq = 440.0;
    f64 audioSeconds = 10.0;
    /// > 0: soma um bumbo (60 Hz decaindo) a cada batida, a partir de
    /// `audioBeatStart` s — para testar a detecção de batidas.
    f64 audioBpm = 0.0;
    f64 audioBeatStart = 0.0;
};

/// Centro do quadrado do padrão MovingSquare no quadro `i`.
inline i32 moving_square_x(i64 i) { return 30 + static_cast<i32>(i); }
/// Cena 3D (rastreio de câmera): pontos fixos no mundo filmados por uma câmera
/// que anda e gira (FOV vertical 55°). Mundo → câmera, convenção do Aurea
/// (X direita, Y baixo, Z frente).
struct Scene3DTruth {
    f64 R[9];   // mundo → câmera
    f64 C[3];   // centro
};
inline constexpr f64 kScene3DFov = 55.0;
inline Scene3DTruth scene3d_camera(u32 i, u32 n) {
    const f64 pi = 3.14159265358979323846;
    const f64 u = static_cast<f64>(i) / static_cast<f64>(n > 1 ? n - 1 : 1);
    const f64 ry = (-8.0 + 16.0 * u) * pi / 180.0, rx = 3.0 * std::sin(u * pi) * pi / 180.0, rz = 1.5 * u * pi / 180.0;
    const f64 cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry), cz = std::cos(rz), sz = std::sin(rz);
    const f64 Rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx}, Ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy}, Rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    f64 T[9], W[9];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { T[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) T[r * 3 + c] += Ry[r * 3 + k] * Rx[k * 3 + c]; }
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { W[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) W[r * 3 + c] += Rz[r * 3 + k] * T[k * 3 + c]; }
    Scene3DTruth t{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) t.R[r * 3 + c] = W[c * 3 + r];
    t.C[0] = -1.2 + 2.4 * u;
    t.C[1] = -0.3 * u;
    t.C[2] = 0.8 * u;
    return t;
}
inline const std::vector<std::array<f64, 3>>& scene3d_points() {
    static const std::vector<std::array<f64, 3>> pts = [] {
        std::vector<std::array<f64, 3>> v;
        u32 seed = 12345u;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<f64>(seed >> 8) / static_cast<f64>(1u << 24); };
        for (int i = 0; i < 220; ++i) v.push_back({-5.0 + 10.0 * rnd(), -3.0 + 6.0 * rnd(), 5.0 + 7.0 * rnd()});
        return v;
    }();
    return pts;
}
/// Projeção verdadeira (px) do ponto no quadro i de n; falso se atrás.
inline bool scene3d_project(const std::array<f64, 3>& X, u32 i, u32 n, u32 w, u32 h, f64& u, f64& v) {
    const Scene3DTruth c = scene3d_camera(i, n);
    const f64 f = 0.5 * h / std::tan(0.5 * kScene3DFov * 3.14159265358979323846 / 180.0);
    const f64 d[3] = {X[0] - c.C[0], X[1] - c.C[1], X[2] - c.C[2]};
    const f64 x = c.R[0] * d[0] + c.R[1] * d[1] + c.R[2] * d[2], y = c.R[3] * d[0] + c.R[4] * d[1] + c.R[5] * d[2];
    const f64 z = c.R[6] * d[0] + c.R[7] * d[1] + c.R[8] * d[2];
    if (z < 0.5) return false;
    u = f * x / z + 0.5 * w;
    v = f * y / z + 0.5 * h;
    return true;
}

/// Quadrado rápido (optical flow): 8 px por quadro em x, dando a volta.
inline i32 fast_square_x(i64 i) { return 20 + static_cast<i32>((i * 8) % 56); }
inline i32 moving_square_y(i64 i) { return 30 + static_cast<i32>(i / 2); }

/// Valor exato da senoide sintética no instante `t` (s), canal `c`.
inline f32 synthetic_audio_value(const SyntheticConfig& cfg, u32 c, f64 t) {
    constexpr f64 kPi = 3.14159265358979323846;
    if (cfg.audioBpm > 0.0) {
        const f64 period = 60.0 / cfg.audioBpm;
        const f64 since = t - cfg.audioBeatStart;
        const f64 tt = since >= 0.0 ? std::fmod(since, period) : 1e9;
        const f64 kick = tt < 0.2 ? 0.8 * std::exp(-tt * 18.0) * std::sin(2.0 * kPi * 60.0 * tt) : 0.0;
        return static_cast<f32>(0.1 * std::sin(2.0 * kPi * cfg.audioFreq * (c + 1) * t) + kick);
    }
    return static_cast<f32>(0.5 * std::sin(2.0 * kPi * cfg.audioFreq * (c + 1) * t));
}

/// "Decoder" de áudio: entrega quadros de 1024 amostras (como AAC), com pts
/// exato; o seek cai no início do quadro de 1024 anterior ao alvo.
class SyntheticAudioDecoder final : public audio::AudioDecoderBackend {
public:
    explicit SyntheticAudioDecoder(const SyntheticConfig& c) : cfg_(c) {
        info_.sampleRate = c.audioRate;
        info_.channels = c.audioChannels;
        info_.durationUs = static_cast<i64>(c.audioSeconds * 1e6);
        total_ = static_cast<i64>(c.audioSeconds * c.audioRate);
    }
    const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    Status seek(i64 us) noexcept override {
        const i64 f = us * cfg_.audioRate / 1'000'000;
        pos_ = (f / 1024) * 1024;
        ++seeks;
        return OkStatus;
    }
    Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept override {
        out.clear();
        eos = false;
        if (pos_ >= total_) {
            eos = true;
            return OkStatus;
        }
        const i64 n = std::min<i64>(1024, total_ - pos_);
        out.resize(static_cast<usize>(n) * cfg_.audioChannels);
        for (i64 i = 0; i < n; ++i) {
            const f64 t = static_cast<f64>(pos_ + i) / cfg_.audioRate;
            for (u32 c = 0; c < cfg_.audioChannels; ++c) out[static_cast<usize>(i) * cfg_.audioChannels + c] = synthetic_audio_value(cfg_, c, t);
        }
        ptsUs = pos_ * 1'000'000 / cfg_.audioRate;
        pos_ += n;
        ++chunks;
        return OkStatus;
    }
    u32 seeks = 0;
    u32 chunks = 0;

private:
    SyntheticConfig cfg_;
    audio::AudioStreamInfo info_{};
    i64 total_ = 0;
    i64 pos_ = 0;
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
        f->strides[0] = w;
        f->strides[1] = ((w + 1) / 2) * 2;
        f->planeCount = 2;
        if (cfg_.pattern == SyntheticPattern::FrameGray) {
            // Cinza uniforme: os mesmos bytes do laço abaixo, sem o custo por
            // pixel (em 4K o laço levava ~40 ms e virava o gargalo de qualquer
            // benchmark — um decoder de hardware entrega bem mais rápido).
            f->y.assign(static_cast<usize>(w) * h, frame_gray_code(index));
            f->uv.assign(static_cast<usize>((w + 1) / 2) * ((h + 1) / 2) * 2, 128);
            f->planes[0] = f->y.data();
            f->planes[1] = f->uv.data();
            return f;
        }
        f->y.assign(static_cast<usize>(w) * h, 0);
        f->uv.assign(static_cast<usize>((w + 1) / 2) * ((h + 1) / 2) * 2, 128);
        // Cena 3D: manchas gaussianas somadas num buffer antes (220 pontos).
        std::vector<f32> scene;
        if (cfg_.pattern == SyntheticPattern::Scene3D) {
            scene.assign(static_cast<usize>(w) * h, 0.0f);
            const auto& pts = scene3d_points();
            for (usize k = 0; k < pts.size(); ++k) {
                f64 u, v;
                if (!scene3d_project(pts[k], index, cfg_.frameCount, w, h, u, v)) continue;
                const f32 amp = 0.35f + 0.55f * static_cast<f32>((k * 37) % 100) / 100.0f;
                for (int dy = -6; dy <= 6; ++dy)
                    for (int dx = -6; dx <= 6; ++dx) {
                        const i32 X = static_cast<i32>(std::floor(u)) + dx, Yy = static_cast<i32>(std::floor(v)) + dy;
                        if (X < 0 || Yy < 0 || X >= static_cast<i32>(w) || Yy >= static_cast<i32>(h)) continue;
                        const f64 ddx = X - u, ddy = Yy - v;
                        scene[static_cast<usize>(Yy) * w + static_cast<usize>(X)] += amp * static_cast<f32>(std::exp(-(ddx * ddx + ddy * ddy) / (2.0 * 1.8 * 1.8)));
                    }
            }
        }
        for (u32 yy = 0; yy < h; ++yy) {
            for (u32 xx = 0; xx < w; ++xx) {
                u8 Y = 0, Cb = 128, Cr = 128;
                if (cfg_.pattern == SyntheticPattern::Scene3D) {
                    Y = static_cast<u8>(std::clamp(30.0f + 200.0f * scene[static_cast<usize>(yy) * w + xx], 16.0f, 235.0f));
                } else if (cfg_.pattern == SyntheticPattern::FrameGray) {
                    Y = frame_gray_code(index);
                } else if (cfg_.pattern == SyntheticPattern::FastSquare) {
                    // Fundo em degradê + quadrado xadrez 21×21 (casas de 7 px)
                    // centrado em fast_square_x, meio da altura.
                    const i32 dx = static_cast<i32>(xx) - fast_square_x(index), dy = static_cast<i32>(yy) - static_cast<i32>(h / 2);
                    Y = static_cast<u8>(100 + (xx * 30) / std::max<u32>(1, w));
                    if (std::abs(dx) <= 10 && std::abs(dy) <= 10) Y = (((dx + 10) / 7 + (dy + 10) / 7) % 2) ? 235 : 20;
                } else if (cfg_.pattern == SyntheticPattern::MovingSquare) {
                    // Fundo em degradê suave + quadrado xadrez 13×13 andando
                    // (1 px por quadro em x, meio em y): alvo de rastreio.
                    const i32 cx = moving_square_x(index), cy = moving_square_y(index);
                    const i32 dx = static_cast<i32>(xx) - cx, dy = static_cast<i32>(yy) - cy;
                    Y = static_cast<u8>(60 + (xx * 40) / std::max<u32>(1, w) + (yy * 30) / std::max<u32>(1, h));
                    if (std::abs(dx) <= 6 && std::abs(dy) <= 6) Y = (((dx + 6) / 3 + (dy + 6) / 3) % 2) ? 235 : 20;
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
        out.hasVideo = cfg_.width > 0;
        if (cfg_.audioRate > 0) {
            out.hasAudio = true;
            out.audioSampleRate = cfg_.audioRate;
            out.audioChannels = cfg_.audioChannels;
            out.audioDurationUs = static_cast<i64>(cfg_.audioSeconds * 1e6);
        }
        return true;
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override {
        if (cfg_.audioRate == 0) return nullptr;
        ++audioOpened;
        return std::make_unique<SyntheticAudioDecoder>(cfg_);
    }
    u32 audioOpened = 0;
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        auto d = std::make_unique<SyntheticDecoder>(cfg_);
        last = d.get();
        ++opened;
        return d;
    }

    std::atomic<SyntheticDecoder*> last{nullptr};
    std::atomic<u32> opened{0};

private:
    SyntheticConfig cfg_;
};

} // namespace aurea::test
