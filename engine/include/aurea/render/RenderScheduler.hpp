// =============================================================================
//  Aurea / render / RenderScheduler.hpp
//
//  Preview adaptativo, prefetch e medição.
//
//  A tese: é melhor reduzir a resolução do preview do que deixar travar. Um
//  editor que mostra 1/2 da resolução a 60 fps é utilizável; um que mostra
//  resolução cheia a 12 fps não é. O usuário sente fluidez, não pixels — e a
//  resolução cheia está a um toque de distância quando ele pausa.
//
//  Como a decisão é tomada, em ordem de preferência:
//
//    1. SOBRA TEMPO?          não faz nada.
//    2. FRAME PASSOU DO BUDGET?  reduz a resolução do preview um degrau.
//    3. AINDA PASSA?          reduz efeitos caros para a versão de preview.
//    4. AINDA PASSA?          reduz mais um degrau.
//    5. AQUECEU?              reduz mais cedo, antes de o aparelho travar.
//
//  Nunca, em nenhuma hipótese, o EXPORT degrada. O export usa sempre a
//  configuração final, com a resolução e as amostras escolhidas. Preview e
//  export compartilham a mesma timeline, o mesmo compositor, os mesmos shaders
//  e a mesma avaliação de animação — o que muda é só o quanto o preview
//  degrada. O resultado final é o mesmo.
//
//  O orçamento é por CADÊNCIA REAL DO DISPLAY, não 60 fixo: um aparelho de
//  120 Hz tem 8.33 ms e um de 60 Hz tem 16.67 ms. Assumir 60 num display de
//  120 desperdiça metade da margem.
// =============================================================================
#pragma once

#include "aurea/platform/DeviceCapabilities.hpp"
#include "aurea/render/GPUBackend.hpp"
#include "aurea/core/Time.hpp"

#include <vector>

namespace aurea {

/// Orçamento de tempo de um frame, em milissegundos, derivado da taxa real.
struct FrameBudget {
    f32 targetFps = 60.0f;
    f32 totalMs = 16.67f;

    /// Divisão do orçamento. Os números vêm de medição em aparelho, não de
    /// estimativa: decode e composição dominam; a UI fica com o resto.
    f32 decodeMs     = 0.0f;   ///< preenchido a partir de DeviceCapabilities
    f32 renderMs     = 0.0f;
    f32 presentMs    = 0.0f;
    f32 reserveMs    = 0.0f;   ///< margem para o sistema

    /// Fração do orçamento acima da qual o plano é considerado "no limite".
    /// Não se espera bater 100%: um frame que usa 98% do orçamento já está
    /// perdido, porque a variação normal o empurra para fora.
    static constexpr f32 kComfortThreshold = 0.80f;

    [[nodiscard]] bool fits(f32 measuredMs) const noexcept {
        return measuredMs < totalMs * kComfortThreshold;
    }
    [[nodiscard]] f32 headroom(f32 measuredMs) const noexcept {
        return totalMs - measuredMs;
    }
};

/// Estado do preview adaptativo. Uma instância por sessão de reprodução.
struct AdaptiveState {
    PreviewScale current = PreviewScale::Auto;
    u32  numerator   = 1;
    u32  denominator = 1;

    /// Média móvel do tempo de frame. Não é o último frame: um pico isolado
    /// (o primeiro frame depois de um seek, sempre caro) não deve derrubar a
    /// resolução.
    f32 averageFrameMs = 0.0f;
    f32 averageGpuMs   = 0.0f;
    f32 averageCpuMs   = 0.0f;
    f32 decodeMs       = 0.0f;

    /// Frames consecutivos acima do orçamento. Só se degrada depois de alguns:
    /// um frame ruim isolado é ruído, três seguidos é padrão.
    u32 overBudgetStreak = 0;
    /// Frames consecutivos bem abaixo. Só se sobe depois de um número maior,
    /// para não oscilar entre duas resoluções a cada segundo.
    u32 underBudgetStreak = 0;

    /// Bloqueia degradação por N frames depois de uma mudança. Sem isto, o
    /// editor entra em oscilação: sobe, o frame estoura, desce, sobra, sobe...
    u32 cooldownFrames = 0;

    /// Efeitos caros em modo de preview. Preenchido a cada frame.
    u32 effectsInPreviewMode = 0;

    /// Times lógicos. Renderizar o frame N enquanto o N-1 ainda está na GPU
    /// esconde a latência sem travar a UI — mas só funciona com N estável.
    u32 framesInFlight = 0;
    u32 maxFramesInFlight = 2;
};

/// Métricas de um frame, preenchidas pelo renderer. É a ENTRADA da decisão
/// adaptativa e do painel de telemetria.
struct FrameStats {
    u32  frameIndex = 0;

    f32 cpuMs      = 0.0f;   ///< tempo de CPU do frame inteiro
    f32 gpuMs      = 0.0f;   ///< medido por timestamp query; 0 = não medido
    f32 decodeMs   = 0.0f;
    f32 composeMs  = 0.0f;
    f32 uploadMs   = 0.0f;
    f32 presentMs  = 0.0f;

    u32  droppedFrames = 0;
    u32  layersRendered = 0;
    u32  passesExecuted = 0;
    u32  passesCulled   = 0;
    u32  drawCalls      = 0;
    u32  triangles      = 0;
    u32  particles      = 0;

    /// Texturas comprimidas/descomprimidas alocadas neste frame.
    u64  gpuMemoryBytes = 0;
    u64  cpuMemoryBytes = 0;

    /// Cache: quantas leituras foram atendidas sem recompor.
    u32  frameCacheHits = 0;
    u32  frameCacheMisses = 0;

    u32  previewWidth  = 0;
    u32  previewHeight = 0;

    bool valid() const noexcept { return frameIndex != 0; }
};

// O cache de frames decodificados e o prefetch vivem em media/ (VideoSource,
// DecodedFrameCache): é lá que a decisão de o que decodificar tem os dados
// para ser tomada — o pts real, o keyframe, o estado do decoder.

/// Recalcula a escala do preview conforme o orçamento medido.
class AdaptiveResolutionController {
public:
    explicit AdaptiveResolutionController(const DeviceCapabilities& caps) noexcept;

    /// Configura para uma composição. Recalcula as opções de escala válidas.
    void configure(u32 compWidth, u32 compHeight, f32 targetFps) noexcept;

    /// Alimenta o controlador com o frame medido. Devolve o novo estado.
    [[nodiscard]] const AdaptiveState& update(const FrameStats& stats,
                                              const ThermalState& thermal) noexcept;

    [[nodiscard]] const AdaptiveState& state() const noexcept { return state_; }
    [[nodiscard]] const FrameBudget& budget() const noexcept { return budget_; }

    /// Opções que a UI mostra no seletor AUTO / FULL / 1/2 / 1/4 / 1/8.
    [[nodiscard]] const std::vector<PreviewScaleOption>& options() const noexcept { return options_; }

    /// Trava o usuário numa escala específica. AUTO volta ao controle
    /// automático. Uma escolha manual NUNCA é sobreposta pelo automático —
    /// o usuário mandou, e ele vê o resultado.
    void set_user_scale(PreviewScale scale) noexcept;

    [[nodiscard]] bool auto_mode() const noexcept;
    [[nodiscard]] u32 current_numerator() const noexcept { return state_.numerator; }
    [[nodiscard]] u32 current_denominator() const noexcept { return state_.denominator; }

    /// Resolução efetiva de renderização para a composição atual.
    [[nodiscard]] u32 render_width() const noexcept;
    [[nodiscard]] u32 render_height() const noexcept;

    /// Quantas vezes o controlador mudou de escala desde o início. Se cresce
    /// muito, o limiar está mal ajustado para este aparelho.
    [[nodiscard]] u32 change_count() const noexcept { return changeCount_; }

private:
    void step_down() noexcept;
    void step_up() noexcept;
    void rebuild_options() noexcept;
    void recompute_budget() noexcept;

    const DeviceCapabilities& caps_;
    FrameBudget          budget_{};
    AdaptiveState        state_{};
    PreviewScale         userScale_ = PreviewScale::Auto;
    u32                  compWidth_ = 1920;
    u32                  compHeight_ = 1080;
    u32                  maxWidth_ = 1920;
    u32                  maxHeight_ = 1080;
    std::vector<PreviewScaleOption> options_;
    u32                  changeCount_ = 0;
};

} // namespace aurea
