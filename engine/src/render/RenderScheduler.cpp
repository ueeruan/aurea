#include "aurea/render/RenderScheduler.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Math.hpp"

#include <algorithm>

namespace aurea {

// -----------------------------------------------------------------------------
// AdaptiveResolutionController
// -----------------------------------------------------------------------------
AdaptiveResolutionController::AdaptiveResolutionController(
    const DeviceCapabilities& caps) noexcept
    : caps_(caps) {
    rebuild_options();
    recompute_budget();
}

void AdaptiveResolutionController::configure(u32 compWidth, u32 compHeight,
                                             f32 targetFps) noexcept {
    compWidth_ = compWidth ? compWidth : 1920;
    compHeight_ = compHeight ? compHeight : 1080;

    // Teto do aparelho. Uma composição maior que o teto do preview nunca é
    // renderizada em resolução cheia no preview — não adianta, a tela não
    // mostra, e o custo é real.
    maxWidth_ = caps_.max_preview_width();
    maxHeight_ = caps_.max_preview_height();

    // Taxa alvo limitada pelo que o display entrega. Pedir 60 num display de
    // 120 Hz desperdiça metade da margem; pedir 120 num de 60 é impossível.
    if (targetFps <= 0.0f) targetFps = 60.0f;
    budget_.targetFps = targetFps;
    budget_.totalMs = 1000.0f / targetFps;

    // Divisão do orçamento, com base em medição de aparelho: em 1080p o
    // decode de hardware fica em ~2 ms, a composição em ~60% do resto, e a
    // apresentação com vsync come um pedaço fixo. A reserva existe porque um
    // frame que usa 100% do orçamento já está perdido — a variação normal o
    // empurra para fora.
    budget_.decodeMs  = budget_.totalMs * 0.15f;
    budget_.presentMs = budget_.totalMs * 0.10f;
    budget_.reserveMs = budget_.totalMs * 0.20f;
    budget_.renderMs  = budget_.totalMs - budget_.decodeMs
                      - budget_.presentMs - budget_.reserveMs;

    rebuild_options();

    if (userScale_ == PreviewScale::Auto) {
        // Ponto de partida sugerido pelo aparelho, mas o controlador ajusta no
        // primeiro frame medido. Começar em FULL e descer mostra um engasgo
        // desnecessário ao abrir um projeto 4K.
        const PreviewScale suggested = caps_.recommended_initial_scale(compWidth, compHeight);
        switch (suggested) {
            case PreviewScale::Half:    state_.numerator = 1; state_.denominator = 2; break;
            case PreviewScale::Quarter: state_.numerator = 1; state_.denominator = 4; break;
            case PreviewScale::Eighth:  state_.numerator = 1; state_.denominator = 8; break;
            default:                    state_.numerator = 1; state_.denominator = 1; break;
        }
        state_.current = suggested;
    }
}

void AdaptiveResolutionController::rebuild_options() noexcept {
    options_.clear();

    auto make = [&](PreviewScale scale, u32 num, u32 den) {
        PreviewScaleOption o;
        o.scale = scale;
        o.numerator = num;
        o.denominator = den;

        u32 w = compWidth_ * num / den;
        u32 h = compHeight_ * num / den;
        // O teto do aparelho se aplica a TODAS as opções, inclusive FULL: a
        // UI não pode oferecer uma opção que o hardware não sustenta.
        if (w > maxWidth_) { h = h * maxWidth_ / (w ? w : 1); w = maxWidth_; }
        if (h > maxHeight_) { w = w * maxHeight_ / (h ? h : 1); h = maxHeight_; }
        // Resolução ímpar quebra o alinhamento de bloco de 2x2 dos codecs de
        // hardware na hora de codificar. Arredonda para baixo em múltiplo de 2.
        w &= ~1u;
        h &= ~1u;
        if (w == 0) w = 2;
        if (h == 0) h = 2;

        o.width = w;
        o.height = h;
        options_.push_back(o);
    };

    make(PreviewScale::Auto, 1, 1);
    make(PreviewScale::Full, 1, 1);
    make(PreviewScale::Half, 1, 2);
    make(PreviewScale::Quarter, 1, 4);
    make(PreviewScale::Eighth, 1, 8);
}

void AdaptiveResolutionController::recompute_budget() noexcept {
    // Nada a recalcular além do que `configure` faz. Existe como ponto único
    // caso o orçamento passe a depender do estado térmico.
}

void AdaptiveResolutionController::set_user_scale(PreviewScale scale) noexcept {
    userScale_ = scale;
    if (scale == PreviewScale::Auto) return;   // volta ao automático

    // Escolha manual nunca é sobreposta pelo automático. O usuário mandou; ele
    // vê o resultado, mesmo que o frame caia. A UI avisa se ficar lento.
    state_.current = scale;
    const u32 num = (scale == PreviewScale::Full)    ? 1 : 1;
    const u32 den = (scale == PreviewScale::Full)    ? 1 :
                    (scale == PreviewScale::Half)    ? 2 :
                    (scale == PreviewScale::Quarter) ? 4 : 8;
    state_.numerator = num;
    state_.denominator = den;
    state_.overBudgetStreak = 0;
    state_.underBudgetStreak = 0;
    state_.heavyLevel = 0;   // manual: só o piso térmico/de aparelho reduz
    state_.quality = PreviewQuality::level(floorHeavy_);
    ++changeCount_;
}

bool AdaptiveResolutionController::auto_mode() const noexcept {
    return userScale_ == PreviewScale::Auto;
}

u32 AdaptiveResolutionController::render_width() const noexcept {
    u32 w = compWidth_ * state_.numerator / state_.denominator;
    if (w > maxWidth_) w = maxWidth_;
    w &= ~1u;
    return w ? w : 2;
}

u32 AdaptiveResolutionController::render_height() const noexcept {
    u32 h = compHeight_ * state_.numerator / state_.denominator;
    if (h > maxHeight_) h = maxHeight_;
    h &= ~1u;
    return h ? h : 2;
}

// -----------------------------------------------------------------------------
// AUTO 2.0 (Fase 8C §6–11)
//
// Duas escadas independentes, escolhidas pelo GARGALO MEDIDO:
//   resolução  FULL → 1/2 → 1/4 → 1/8     (ajuda quando a GPU segura o quadro)
//   reduções   completo → metade → quarto  (amostras de desfoque, flow, SSAO,
//              sombra, partículas, blur, LOD — ajuda CPU e GPU)
// Descer é rápido (3 quadros acima de 85% do orçamento na média móvel). Subir
// exige a PREVISÃO do degrau de cima abaixo de 65% — com a razão de custo
// MEDIDA na última descida, não chutada — por 60 quadros × `upBackoff`. Uma
// subida desfeita em menos de 180 quadros dobra `upBackoff`: as tentativas
// caem geometricamente e o preview não fica piscando entre dois degraus.
// Decode lento não derruba resolução (resolução não acelera o decoder): o
// estado mostra o gargalo em vez de fingir que resolveu.
// -----------------------------------------------------------------------------
namespace {
u32 den_index(u32 den) noexcept { return den >= 8 ? 3u : den >= 4 ? 2u : den >= 2 ? 1u : 0u; }
PreviewScale scale_of(u32 den) noexcept {
    return den >= 8 ? PreviewScale::Eighth : den >= 4 ? PreviewScale::Quarter : den >= 2 ? PreviewScale::Half : PreviewScale::Full;
}
/// Razão de custo de um degrau quando ainda não foi medida: 3× (um quarto
/// dos pixels, mas os custos fixos não caem). Conservadora para SUBIR.
constexpr f32 kDefaultRatio = 3.0f;
} // namespace

f32 PreviewQuality::heavy() const noexcept {
    return std::min({motionBlurSamples, flowResolution, shadowResolution, particles, blurSamples});
}

PreviewQuality PreviewQuality::level(u32 heavyLevel) noexcept {
    PreviewQuality q;
    if (heavyLevel == 0) return q;
    const f32 k = heavyLevel == 1 ? 0.5f : 0.25f;
    q.motionBlurSamples = k;
    q.flowResolution = k;
    q.ssao = heavyLevel == 1 ? 0.5f : 0.0f;
    q.shadowResolution = k;
    q.particles = k;
    q.blurSamples = k;
    q.lodBias = static_cast<f32>(std::min(heavyLevel, 2u));
    return q;
}

PreviewQuality PreviewQuality::min(const PreviewQuality& o) const noexcept {
    PreviewQuality q;
    q.motionBlurSamples = std::min(motionBlurSamples, o.motionBlurSamples);
    q.flowResolution = std::min(flowResolution, o.flowResolution);
    q.ssao = std::min(ssao, o.ssao);
    q.shadowResolution = std::min(shadowResolution, o.shadowResolution);
    q.particles = std::min(particles, o.particles);
    q.blurSamples = std::min(blurSamples, o.blurSamples);
    q.lodBias = std::max(lodBias, o.lodBias);
    return q;
}

void AdaptiveResolutionController::set_quality_floor(u32 minDenominator, u32 heavyLevel) noexcept {
    floorDen_ = minDenominator >= 8 ? 8u : minDenominator >= 4 ? 4u : minDenominator >= 2 ? 2u : 1u;
    floorHeavy_ = std::min(heavyLevel, 2u);
}

u32 AdaptiveResolutionController::floor_denominator(const ThermalState& thermal) const noexcept {
    // Crítico: no mínimo 1/4, mesmo com o quadro no orçamento — o aparelho VAI
    // estrangular, e descer depois do estrangulamento já é engasgo.
    return std::max(floorDen_, thermal.severe() ? 4u : 1u);
}

u32 AdaptiveResolutionController::floor_heavy(const ThermalState& thermal) const noexcept {
    return std::max(floorHeavy_, thermal.severe() ? 2u : thermal.should_degrade() ? 1u : 0u);
}

void AdaptiveResolutionController::apply_resolution(u32 denominator) noexcept {
    state_.numerator = 1;
    state_.denominator = denominator;
    state_.current = scale_of(denominator);
}

void AdaptiveResolutionController::apply_quality(const ThermalState& thermal) noexcept {
    const u32 level = std::max(auto_mode() ? state_.heavyLevel : 0u, floor_heavy(thermal));
    state_.quality = PreviewQuality::level(level);
}

void AdaptiveResolutionController::note_change(bool up) noexcept {
    if (!up && lastWasUp_ && tick_ - lastUpTick_ <= kFailWindow) {
        // A subida não se sustentou: esperar o dobro antes da próxima.
        ++state_.failedUpSteps;
        state_.upBackoff = std::min(state_.upBackoff * 2, kMaxBackoff);
    }
    lastWasUp_ = up;
    if (up) lastUpTick_ = tick_;
    ++changeCount_;
    state_.cooldownFrames = up ? kUpCooldown : kDownCooldown;
    state_.overBudgetStreak = 0;
    state_.underBudgetStreak = 0;
}

void AdaptiveResolutionController::step_down() noexcept {
    // CPU segurando o quadro (medido: CPU > GPU com timestamps): menos
    // resolução não ajuda — primeiro as reduções. GPU, memória ou gargalo
    // desconhecido (sem timestamps): resolução primeiro, o degrau mais eficaz.
    const bool cpuBound = state_.bottleneck == PreviewBottleneck::Cpu;
    if (cpuBound && state_.heavyLevel < 2) {
        ++state_.heavyLevel;
    } else if (state_.denominator < 8) {
        gpuBeforeDown_ = state_.averageGpuMs;
        ratioPending_ = den_index(state_.denominator);
        apply_resolution(state_.denominator * 2);
    } else if (state_.heavyLevel < 2) {
        ++state_.heavyLevel;
    } else {
        return;   // tudo no mínimo: nada mais a reduzir (o gargalo fica no estado)
    }
    note_change(false);
}

void AdaptiveResolutionController::step_up() noexcept {
    // Desfaz na ordem inversa: reduções primeiro — a resolução, o que mais
    // muda a imagem, volta por último.
    if (state_.heavyLevel > floorHeavy_) {
        --state_.heavyLevel;
    } else if (state_.denominator > floorDen_) {
        ratioPending_ = kInvalidIndex;
        apply_resolution(state_.denominator / 2);
    } else {
        return;
    }
    note_change(true);
}

const AdaptiveState& AdaptiveResolutionController::update(const FrameStats& stats,
                                                          const ThermalState& thermal) noexcept {
    ++tick_;
    const f32 budget = budget_.totalMs;
    // Média móvel exponencial. O último frame sozinho não decide: o primeiro
    // frame depois de um seek é sempre caro (decodifica, aloca, compila), e
    // reagir a ele derrubaria a resolução sem motivo.
    constexpr f32 kAlpha = 0.15f;
    const f32 frameMs = stats.gpuMs > 0.0f ? std::max(stats.cpuMs, stats.gpuMs) : stats.cpuMs;

    // Complexidade mudou muito (camadas/passes): a média antiga não descreve a
    // cena nova — recomeça dela, e a espera de subida volta ao normal.
    const bool sceneChanged = lastPasses_ > 0
        && (stats.passesExecuted * 2 + 4 < lastPasses_ || stats.passesExecuted > lastPasses_ * 2 + 4
            || stats.layersRendered * 2 + 2 < lastLayers_ || stats.layersRendered > lastLayers_ * 2 + 2);
    lastPasses_ = stats.passesExecuted;
    lastLayers_ = stats.layersRendered;
    if (state_.averageFrameMs <= 0.0f || sceneChanged) {
        state_.averageFrameMs = frameMs;
        state_.averageGpuMs = stats.gpuMs;
        state_.averageCpuMs = stats.cpuMs;
        state_.decodeMs = stats.decodeMs;
        if (sceneChanged) {
            state_.overBudgetStreak = 0;
            state_.underBudgetStreak = 0;
            state_.upBackoff = 1;
            ratioPending_ = kInvalidIndex;
        }
    } else {
        state_.averageFrameMs = lerpf(state_.averageFrameMs, frameMs, kAlpha);
        state_.averageGpuMs = lerpf(state_.averageGpuMs, stats.gpuMs, kAlpha);
        state_.averageCpuMs = lerpf(state_.averageCpuMs, stats.cpuMs, kAlpha);
        state_.decodeMs = lerpf(state_.decodeMs, stats.decodeMs, kAlpha);
    }
    state_.framesInFlight = stats.frameIndex;
    state_.memoryPressure = stats.memoryPressure;
    const u32 drops = stats.droppedFrames >= lastDropped_ ? stats.droppedFrames - lastDropped_ : 0u;
    lastDropped_ = stats.droppedFrames;

    if (state_.cooldownFrames > 0) --state_.cooldownFrames;

    // Razão de custo do degrau que acabou de descer, com a média já assentada.
    if (ratioPending_ != kInvalidIndex && state_.cooldownFrames == 0) {
        if (gpuBeforeDown_ > 0.0f && state_.averageGpuMs > 0.0f) {
            ratio_[ratioPending_] = std::clamp(gpuBeforeDown_ / state_.averageGpuMs, 1.0f, 4.0f);
        }
        ratioPending_ = kInvalidIndex;
    }

    // Gargalo pela medição. Quadro perdido com carga média já alta também é
    // evidência (o pico que a média ainda não mostrou).
    const bool over = state_.averageFrameMs > budget * kDownThreshold || (drops > 0 && state_.averageFrameMs > budget * 0.6f);
    if (over) {
        const bool gpuKnown = state_.averageGpuMs > 0.0f;
        state_.bottleneck = gpuKnown && state_.averageCpuMs > state_.averageGpuMs ? PreviewBottleneck::Cpu : PreviewBottleneck::Gpu;
    } else if (state_.decodeMs > budget) {
        state_.bottleneck = PreviewBottleneck::Decode;
    } else if (state_.memoryPressure >= 0.9f) {
        state_.bottleneck = PreviewBottleneck::Memory;
    } else if (thermal.should_degrade()) {
        state_.bottleneck = PreviewBottleneck::Thermal;
    } else {
        state_.bottleneck = PreviewBottleneck::None;
    }

    if (over) { ++state_.overBudgetStreak; state_.underBudgetStreak = 0; }
    else state_.overBudgetStreak = 0;

    // Resolução travada pelo usuário: o automático para de mexer (os botões
    // de redução seguem só o piso térmico/de aparelho).
    if (userScale_ != PreviewScale::Auto) {
        apply_quality(thermal);
        return state_;
    }

    // Uma subida que ficou de pé por uma janela inteira: a espera volta a encurtar.
    if (lastWasUp_ && tick_ - lastUpTick_ > kFailWindow) {
        lastWasUp_ = false;
        state_.upBackoff = std::max(1u, state_.upBackoff / 2);
    }

    // Pisos (térmico e de aparelho) valem na hora, sem cooldown: só descem.
    const u32 floorDen = floor_denominator(thermal);
    if (state_.denominator < floorDen) {
        ratioPending_ = kInvalidIndex;   // descida imposta: não mede razão
        apply_resolution(floorDen);
        note_change(false);
        apply_quality(thermal);
        return state_;
    }

    if (state_.cooldownFrames > 0) {
        apply_quality(thermal);
        return state_;
    }

    // Memória no limite: alvos menores já (e nada de subir).
    if (state_.memoryPressure >= 0.9f && state_.denominator < 8) {
        state_.bottleneck = PreviewBottleneck::Memory;
        step_down();
        apply_quality(thermal);
        return state_;
    }

    // Padrão, não ruído: três quadros seguidos acima (na média móvel).
    if (state_.overBudgetStreak >= kDownStreak) {
        step_down();
        apply_quality(thermal);
        return state_;
    }

    // Subir: a PREVISÃO do degrau de cima cabe com folga?
    bool candidate = false;
    if (state_.heavyLevel > std::max(floorHeavy_, floor_heavy(thermal))) {
        // Reduções de volta: estimativa conservadora de +60% no quadro.
        candidate = state_.averageFrameMs * 1.6f < budget * kUpThreshold;
    } else if (state_.denominator > floorDen && !thermal.should_degrade() && state_.memoryPressure < 0.75f) {
        const u32 idx = den_index(state_.denominator) - 1;
        const f32 r = ratio_[idx] > 0.0f ? ratio_[idx] : kDefaultRatio;
        const f32 predicted = state_.averageGpuMs > 0.0f
            ? std::max(state_.averageGpuMs * r, state_.averageCpuMs * 1.1f)
            : state_.averageFrameMs * r;
        candidate = predicted < budget * kUpThreshold;
    }
    state_.underBudgetStreak = candidate ? state_.underBudgetStreak + 1 : 0;
    if (candidate && state_.underBudgetStreak >= kUpFrames * state_.upBackoff) step_up();

    apply_quality(thermal);
    return state_;
}

} // namespace aurea
