// =============================================================================
//  Aurea / tracking / AnalysisDecode.hpp (privado)
//
//  Decodificação para as análises (rastreio de ponto, estabilizador, câmera
//  3D): um decoder PRÓPRIO andando para a frente, escolhendo para cada
//  instante pedido o MESMO quadro que o preview mostra (DecodedFrameCache::find):
//  o que cobre o instante (VFR com duração), senão o exato a meio quadro,
//  senão o anterior mais próximo. Antes o rastreio pegava "o primeiro com
//  pts ≥ alvo − ½ quadro", que em VFR analisava um quadro diferente do que
//  aparecia na tela (o Nulo saía deslocado do detalhe).
//
//  O cancelamento é olhado a cada quadro decodificado (andar até um alvo
//  longe de um keyframe em 4K leva segundos no aparelho).
// =============================================================================
#pragma once

#include "aurea/media/VideoSource.hpp"
#include "aurea/tracking/PointTracker.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <span>

namespace aurea::tracking {

class AnalysisFramePicker {
public:
    enum class Result { Ok, Cancelled, Failed };

    AnalysisFramePicker(VideoDecoderBackend& decoder, f64 fps, const std::atomic<bool>& cancel,
                        u64 deadlineNs = 0) noexcept
        : dec_(decoder), cancel_(cancel), deadlineNs_(deadlineNs) {
        const f64 rate = fps > 0.0 ? fps : 30.0;
        half_ = static_cast<i64>(5e5 / rate);
        // Quadros que começaram até 2,5 durações nominais antes do alvo ainda
        // podem ser "o que cobre": não deixe o decoder descartá-los.
        early_ = static_cast<i64>(2.5e6 / rate);
    }

    /// Quadro mostrado em `wantUs` (instante da fonte).
    Result pick(i64 wantUs, FrameRef& out) noexcept {
        out = {};
        if (expired()) return Result::Cancelled;
        // Seek: no começo, ao voltar no tempo (rastreio para trás) e ao pular
        // mais que um GOP para a frente (velocidade/remapeamento de tempo).
        // A target before the retained window can still be covered by the
        // discarded VFR frame. Seek even when the delta is below half a frame.
        const bool backwards = !window_.empty() && wantUs < window_.front()->ptsUs;
        const bool farAhead = !window_.empty() &&
            wantUs - window_.back()->ptsUs > std::max<i64>(dec_.keyframe_interval_us(), 1'000'000) + early_;
        if (!started_ || backwards || farAhead) {
            if (!dec_.seek_to_keyframe(std::max<i64>(0, wantUs - early_)).ok()) return Result::Failed;
            window_.clear();
            eos_ = false;
            started_ = true;
        }
        // Lê até passar do alvo (+ meio quadro) ou acabar o vídeo.
        for (int guard = 0; !eos_ && (window_.empty() || window_.back()->ptsUs <= wantUs + half_); ++guard) {
            if (expired()) return Result::Cancelled;
            if (guard > 20000) return Result::Failed;
            FrameRef f;
            i64 pts = 0;
            if (!dec_.next_frame(wantUs - early_, f, pts, eos_).ok()) { eos_ = true; break; }
            if (!f) continue;
            if (!window_.empty() && f->ptsUs <= window_.back()->ptsUs) continue;   // pts repetido/fora de ordem
            window_.push_back(std::move(f));
        }
        // Guarda só o último quadro que ainda pode servir (o anterior ao alvo).
        while (window_.size() >= 2 && window_[1]->ptsUs <= wantUs) window_.pop_front();
        if (window_.empty()) return Result::Failed;
        for (const FrameRef& f : window_)
            if (f->covers(wantUs)) { out = f; return Result::Ok; }
        for (const FrameRef& f : window_)
            if (f->durationUs == 0 && std::llabs(f->ptsUs - wantUs) <= half_) { out = f; return Result::Ok; }
        for (const FrameRef& f : window_)
            if (f->ptsUs <= wantUs) out = f;
        if (!out) out = window_.front();
        return Result::Ok;
    }

private:
    bool expired() const noexcept {
        return cancel_.load(std::memory_order_relaxed) || (deadlineNs_ && monotonic_ns() >= deadlineNs_);
    }

    VideoDecoderBackend& dec_;
    const std::atomic<bool>& cancel_;
    u64 deadlineNs_;
    std::deque<FrameRef> window_;
    i64 half_ = 16667, early_ = 83333;
    bool eos_ = false, started_ = false;
};

/// A short reverse run is decoded forward once, then consumed in reverse.
/// Keep only analysis luma in the cache, never a GOP of hardware surfaces.
class AnalysisGraySequence {
public:
    using Result = AnalysisFramePicker::Result;
    static constexpr usize kBudgetBytes = 12 * 1024 * 1024;

    AnalysisGraySequence(VideoDecoderBackend& decoder, f64 fps, const std::atomic<bool>& cancel,
                         std::span<const i64> times, u32 height, u32 sourceW, u32 sourceH) noexcept
        : picker_(decoder, fps, cancel), cancel_(cancel), times_(times), height_(height) {
        const u64 width = (static_cast<u64>(height) * sourceW + std::max(1u, sourceH) - 1) / std::max(1u, sourceH);
        const u64 bytes = (width + 1) * height * sizeof(f32);
        batch_ = static_cast<usize>(std::clamp<u64>(kBudgetBytes / std::max<u64>(1, bytes), 1, 24));
    }

    Result read(usize i, Gray& out) {
        if (cancel_.load(std::memory_order_relaxed)) return Result::Cancelled;
        if (i >= times_.size()) return Result::Failed;
        if (i == next_ && !cache_.empty()) {
            out = std::move(cache_.back());
            cache_.pop_back();
            ++next_;
            return Result::Ok;
        }
        cache_.clear();
        usize end = i;
        while (end + 1 < times_.size() && end + 1 - i < batch_ && times_[end + 1] <= times_[end]) ++end;
        if (end == i || times_[end] == times_[i]) return decode(i, out);

        usize usedBytes = 0;
        for (usize n = end + 1; n-- > i;) {
            Gray frame;
            const auto result = decode(n, frame);
            if (result == Result::Cancelled) { cache_.clear(); return result; }
            usedBytes += frame.px.capacity() * sizeof(f32);
            if (result != Result::Ok || usedBytes > kBudgetBytes) {
                // An unusual source or a failed prefetch must still let the
                // current frame try the ordinary selection path.
                cache_.clear();
                return decode(i, out);
            }
            cache_.push_back(std::move(frame));
        }
        out = std::move(cache_.back());
        cache_.pop_back();
        next_ = i + 1;
        return Result::Ok;
    }

private:
    Result decode(usize i, Gray& out) {
        FrameRef frame;
        const auto result = picker_.pick(times_[i], frame);
        if (result != Result::Ok) return result;
        return frame && frame_to_gray(*frame.get(), height_, out) ? Result::Ok : Result::Failed;
    }

    AnalysisFramePicker picker_;
    const std::atomic<bool>& cancel_;
    std::span<const i64> times_;
    u32 height_;
    usize batch_ = 1, next_ = 0;
    std::vector<Gray> cache_;
};

} // namespace aurea::tracking
