// =============================================================================
//  Aurea / hyper / HyperStabilize.hpp (privado do motor)
//
//  Hyper Stable: a análise da fonte (decoder próprio, para a frente, ¼ da
//  resolução, HyperAnalysis.hpp) e a escrita do resultado como keyframes de
//  posição/rotação/escala na camada. O recorte é a escala extra (zoom) que
//  esconde as bordas; "gravar keyframes" (Recipe::stabilizeToKeyframes) grava
//  a mesma correção sem o zoom (a borda aparece, a pessoa ajusta no editor).
// =============================================================================
#pragma once

#include "HyperAnalysis.hpp"

#include "aurea/media/MediaManager.hpp"
#include "aurea/project/Asset.hpp"

namespace aurea { struct Layer; }

namespace aurea::hyper {

struct StabAnalysis {
    VideoSourceFactory* factory = nullptr;
    const Asset* asset = nullptr;         ///< vídeo (caminho já resolvido)
    f64 fps = 30.0;                       ///< da fonte
    u64 frames = 0;                       ///< quadros da fonte a analisar
    f32 strength = 0.5f;
    bool crop = true;
    const std::atomic<bool>* cancel = nullptr;
    void (*progress)(void*, f32) noexcept = nullptr;
    void* progressContext = nullptr;
};
/// Decodifica a fonte inteira (uma vez, para a frente) e devolve as correções.
[[nodiscard]] Status analyze_stabilization(const StabAnalysis& request, StabPlan& out);

/// Grava a correção como keyframes da camada (um por quadro da fonte, linear).
/// A camada já tem âncora no centro da fonte, posição no centro da composição
/// e escala `layerScale` (o ajuste à composição). `compFramesPerSourceFrame` =
/// quadros da timeline por quadro da fonte (câmera lenta ×N: N·fpsComp/fpsFonte).
/// `hold` = keyframes segurados (o render repete o quadro de origem para o
/// Motion Lite/Max: a correção é a do quadro de origem, não interpolada).
void apply_stabilization(Layer& layer, const StabPlan& plan, f32 layerScale, f64 compFramesPerSourceFrame,
                         bool hold = false);

} // namespace aurea::hyper
