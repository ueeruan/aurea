// =============================================================================
//  Aurea / hyper / HyperExport.hpp (privado do motor)
//
//  O que o job Hyper entrega ao export (Engine::start_export_checked): o
//  projeto temporário já serializado (não é o projeto aberto) e as etapas do
//  grafo Hyper desta passagem. O export trata o resto como sempre: render,
//  checkpoint, encoder, validação e publicação atômica.
// =============================================================================
#pragma once

#include "aurea/hyper/HyperStage.hpp"
#include "aurea/timeline/Timeline.hpp"

#include <vector>

namespace aurea {

struct HyperExportOverride {
    std::vector<u8> projectBytes;          ///< ProjectSerializer::encode do projeto temporário
    CompositionId composition{};
    f64 compFps = 30.0;
    /// Etapas Lite/Max entre o readback e o encoder (vazio = só render).
    std::vector<const hyper::StageDescriptor*> graph;
    hyper::Recipe graphRecipe{};            ///< a receita das etapas (upscale = escala da rede)
    hyper::DeviceProfile profile{};
    /// Saída do grafo (o encoder recebe isto); a composição tem o tamanho do render.
    u32 outputWidth = 0, outputHeight = 0;
    /// Motion no grafo: a posição na fonte do quadro de saída i é
    /// i · motionSourceFps / (motionSlow · fps da saída). 0 = sem Motion no grafo.
    f64 motionSourceFps = 0.0;
    u32 motionSlow = 1;
};

} // namespace aurea
