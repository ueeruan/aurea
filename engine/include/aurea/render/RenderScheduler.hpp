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

/// Reduções TEMPORÁRIAS do preview (AUTO 2.0, Fase 8C §6–11). Cada campo é
/// uma fração do custo que o sistema dono aplica no PREVIEW — 1 = o que o
/// projeto pede. O export ignora tudo isto (`RenderSettings::finalQuality`).
/// O motor só expõe os botões; quem gira cada um é o sistema (partículas,
/// flow, 3D, blur), cada um na sua frente.
struct PreviewQuality {
    f32 motionBlurSamples = 1.0f;   ///< fração de `MotionBlurSettings::previewSamples`
    f32 flowResolution    = 1.0f;   ///< resolução do optical flow
    f32 ssao              = 1.0f;   ///< 0 = desligado no preview
    f32 shadowResolution  = 1.0f;   ///< lado do mapa de sombra
    f32 particles         = 1.0f;   ///< fração do máximo de partículas
    f32 blurSamples       = 1.0f;   ///< amostras/raio efetivo de blurs caros
    f32 lodBias           = 0.0f;   ///< níveis de LOD/mip mais grossos (0 = nenhum)

    /// O menor dos botões: para quem ainda usa o escalar único (`heavyScale`).
    [[nodiscard]] f32 heavy() const noexcept;
    /// Nível da escada de reduções: 0 completo, 1 metade, 2 um quarto.
    [[nodiscard]] static PreviewQuality level(u32 heavyLevel) noexcept;
    /// Cada botão no menor dos dois (restrição térmica × decisão do AUTO).
    [[nodiscard]] PreviewQuality min(const PreviewQuality& o) const noexcept;
};

/// O que está segurando o quadro, pela medição (a telemetria mostra).
enum class PreviewBottleneck : u8 { None = 0, Gpu, Cpu, Decode, Memory, Thermal };

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

    // --- AUTO 2.0 ------------------------------------------------------------
    /// Degrau de reduções temporárias (0..2) e os botões resultantes (já com
    /// o piso térmico/de aparelho aplicado).
    u32 heavyLevel = 0;
    PreviewQuality quality{};
    PreviewBottleneck bottleneck = PreviewBottleneck::None;
    /// Espera extra para voltar a subir: dobra a cada subida que não se
    /// sustentou (é o que prova que o controlador não oscila).
    u32 upBackoff = 1;
    u32 failedUpSteps = 0;
    f32 memoryPressure = 0.0f;
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

    /// Pressão de memória do processo (0..1, MemoryManager::pressure()).
    f32  memoryPressure = 0.0f;

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

    /// Botões de redução em vigor (AUTO) — com escala manual, só o piso
    /// térmico/de aparelho.
    [[nodiscard]] const PreviewQuality& quality() const noexcept { return state_.quality; }
    /// Piso imposto de fora (perfil de aparelho fraco, Fase 8H): o AUTO nunca
    /// sobe acima dele. `minDenominator` 1/2/4/8; `heavyLevel` 0..2.
    void set_quality_floor(u32 minDenominator, u32 heavyLevel) noexcept;
    /// Razão de custo de GPU medida entre um degrau de resolução e o de baixo
    /// (índice = degrau de cima: 0 FULL→1/2, 1 1/2→1/4, 2 1/4→1/8).
    [[nodiscard]] f32 measured_ratio(u32 step) const noexcept { return step < 3 ? ratio_[step] : 0.0f; }

    /// Limiares (fração do orçamento). Públicos para os testes lerem.
    static constexpr f32 kDownThreshold = 0.85f;   ///< média acima = sobrecarga
    static constexpr f32 kUpThreshold   = 0.65f;   ///< previsão abaixo = pode subir
    static constexpr u32 kDownStreak    = 3;
    static constexpr u32 kUpFrames      = 60;      ///< × upBackoff
    static constexpr u32 kDownCooldown  = 30;
    static constexpr u32 kUpCooldown    = 60;
    static constexpr u32 kFailWindow    = 180;     ///< subida desfeita antes disto = falhou
    static constexpr u32 kMaxBackoff    = 32;

private:
    void step_down() noexcept;
    void step_up() noexcept;
    void apply_resolution(u32 denominator) noexcept;
    void apply_quality(const ThermalState& thermal) noexcept;
    [[nodiscard]] u32 floor_denominator(const ThermalState& thermal) const noexcept;
    [[nodiscard]] u32 floor_heavy(const ThermalState& thermal) const noexcept;
    void note_change(bool up) noexcept;
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

    // AUTO 2.0
    u64 tick_ = 0;                  ///< quadros medidos
    u64 lastUpTick_ = 0;
    bool lastWasUp_ = false;
    u32 lastDropped_ = 0;
    u32 lastPasses_ = 0;
    u32 lastLayers_ = 0;
    f32 ratio_[3] = {0.0f, 0.0f, 0.0f};   ///< medida (0 = ainda não)
    f32 gpuBeforeDown_ = 0.0f;       ///< GPU média antes da última descida de resolução
    u32 ratioPending_ = kInvalidIndex;  ///< degrau cuja razão falta medir
    u32 floorDen_ = 1;               ///< piso de aparelho (8H)
    u32 floorHeavy_ = 0;
};

} // namespace aurea
