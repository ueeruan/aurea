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

#include <atomic>
#include <cstdlib>
#include <deque>

namespace aurea::tracking {

class AnalysisFramePicker {
public:
    enum class Result { Ok, Cancelled, Failed };

    AnalysisFramePicker(VideoDecoderBackend& decoder, f64 fps, const std::atomic<bool>& cancel) noexcept
        : dec_(decoder), cancel_(cancel) {
        const f64 rate = fps > 0.0 ? fps : 30.0;
        half_ = static_cast<i64>(5e5 / rate);
        // Quadros que começaram até 2,5 durações nominais antes do alvo ainda
        // podem ser "o que cobre": não deixe o decoder descartá-los.
        early_ = static_cast<i64>(2.5e6 / rate);
    }

    /// Quadro mostrado em `wantUs` (instante da fonte).
    Result pick(i64 wantUs, FrameRef& out) noexcept {
        out = {};
        // Seek: no começo, ao voltar no tempo (rastreio para trás) e ao pular
        // mais que um GOP para a frente (velocidade/remapeamento de tempo).
        const bool backwards = !window_.empty() && wantUs + half_ < window_.front()->ptsUs;
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
            if (cancel_.load(std::memory_order_relaxed)) return Result::Cancelled;
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
    VideoDecoderBackend& dec_;
    const std::atomic<bool>& cancel_;
    std::deque<FrameRef> window_;
    i64 half_ = 16667, early_ = 83333;
    bool eos_ = false, started_ = false;
};

} // namespace aurea::tracking
