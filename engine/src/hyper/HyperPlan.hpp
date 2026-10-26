// =============================================================================
//  Aurea / hyper / HyperPlan.hpp (privado do motor)
//
//  O PLANO de um job Hyper: que caminho cada etapa usa de fato, em quantas
//  passagens pelo export, com que tamanho/fps/duração em cada uma, e a
//  estimativa. Funções puras (sem GPU, sem arquivo): os testes de CPU cobrem.
//
//  Passagens. O render (Turbo, efeitos de camada) vem ANTES do grafo (redes)
//  dentro de uma passagem. Uma etapa Turbo que precisa vir DEPOIS de uma rede
//  abre uma passagem nova, que lê o intermediário da anterior:
//      [Clean Turbo + Stable] → [Clean rede] → [Upscale] → [Face rede] → [Motion]
//  Exemplos: tudo Turbo = 1 passagem; Upscale Lite + 60 fps = 2 (o upscale
//  neural só processa os quadros ORIGINAIS; o Motion interpola depois).
//  O Stable é a transformação da camada (keyframes) e vai sempre na 1ª.
// =============================================================================
#pragma once

#include "aurea/hyper/HyperStage.hpp"

#include <array>
#include <string>
#include <vector>

namespace aurea::hyper {

/// O que a sonda sabe da fonte.
struct SourceInfo {
    u32 width = 0, height = 0;   ///< exibição (rotação aplicada)
    f64 fps = 30.0;
    i64 durationUs = 0;
    bool hasAudio = false;
    bool interlaced = false;     ///< detectado (deinterlace automático)
};

/// Os limites do aparelho que valem para a saída (os do export).
struct OutputCaps {
    u32 capLong = 0, capShort = 0;   ///< 0 = sem teto (encoder)
    u32 maxTexture = 0;              ///< lado máximo de textura da GPU
    f64 maxFps = 240.0;              ///< teto do encoder (MediaCodecInfo / VT); 0 = 240
};

/// Uma passagem pelo export.
struct PassPlan {
    // Render (Turbo, efeitos de camada aurea.hyper.* no projeto temporário).
    bool renderClean = false;        ///< denoise/deblock/deinterlace Turbo
    bool renderDenoise = false;      ///< o denoise Turbo (falso com a rede de Clean)
    bool renderStable = false;       ///< keyframes de estabilização na camada
    bool renderUpscale = false;      ///< Hyper Upscale Turbo
    bool renderMotion = false;       ///< Hyper Motion (interpolação de quadros)
    // Grafo (Lite/Max).
    std::vector<const StageDescriptor*> graph;
    // Tamanhos: o render produz renderW×renderH (= a composição temporária);
    // o grafo leva a outW×outH (já limitado pelo aparelho).
    u32 sourceW = 0, sourceH = 0;    ///< a fonte DESTA passagem
    f64 sourceFps = 30.0;
    u32 renderW = 0, renderH = 0;
    u32 outW = 0, outH = 0;
    f64 fps = 30.0;                  ///< da composição e da saída
    u32 slowMotion = 1;              ///< velocidade da camada = 1/slowMotion
    u64 frames = 0;                  ///< quadros de saída
    bool muteAudio = false;
    bool final = false;              ///< escreve no arquivo pedido
    /// Motion Lite/Max no grafo desta passagem: o render repete o quadro de
    /// origem (camada com velocidade 1/slowMotion, sem Hyper Motion).
    [[nodiscard]] bool graph_motion() const noexcept {
        for (const StageDescriptor* d : graph) if (d && d->kind == StageKind::Motion) return true;
        return false;
    }
};

struct JobPlan {
    Recipe recipe{};
    std::vector<PassPlan> passes;
    std::array<bool, kStepCount> present{};
    std::array<Path, kStepCount> paths{};
    u32 outW = 0, outH = 0;
    f64 outFps = 0.0;
    u64 outFrames = 0;
    bool muteAudio = false;
    bool resolutionCapped = false;
    bool fpsCapped = false;
    std::string note;                ///< o que caiu para outro caminho e por quê
};

/// O caminho efetivo pedido (Auto = o do perfil).
[[nodiscard]] Path effective_path(const Recipe& r, const DeviceProfile& p) noexcept;

/// Saída limitada pelo aparelho (proporção mantida, pares). A mesma regra do
/// export (start_export_checked): lado maior × lado menor e textura.
[[nodiscard]] bool fit_output(u32 wantW, u32 wantH, const OutputCaps& caps, u32& w, u32& h) noexcept;

/// O plano. `registry` = as etapas Lite/Max conhecidas. Erro = fonte inválida.
[[nodiscard]] Result<JobPlan> plan_job(const Recipe& recipe, const DeviceProfile& profile, const SourceInfo& source,
                                       const OutputCaps& caps, const StageRegistry& registry);

/// Estimativa (segundos por caminho, memória, tamanho). Pura: usa o perfil.
[[nodiscard]] Estimate estimate_job(const Recipe& recipe, const DeviceProfile& profile, const SourceInfo& source,
                                    const OutputCaps& caps, const StageRegistry& registry);

/// Quadros de saída de `seconds` a `fps` (a regra do export: ceil(s·fps − 1e-6)).
[[nodiscard]] u64 frames_for(f64 seconds, f64 fps) noexcept;

// --- Medidor: o arquivo do perfil -------------------------------------------
/// JSON pequeno e estável (uma chave por linha). `stamp` = build do motor +
/// GPU + driver: diferente = medir de novo.
[[nodiscard]] std::string profile_to_json(const DeviceProfile& p, const std::string& stamp);
[[nodiscard]] bool profile_from_json(const std::string& text, DeviceProfile& p, std::string& stamp);
/// O caminho padrão que o perfil sugere (regra do medidor).
[[nodiscard]] Path suggest_path(const DeviceProfile& p) noexcept;

} // namespace aurea::hyper
