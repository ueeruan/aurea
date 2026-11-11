// =============================================================================
//  Aurea / engine / EngineAeAnimation.hpp  (interno do motor)
//
//  AE animation: o que o despacho de comandos do Engine.cpp chama para o
//  texto-fonte (trilha virtual TrackProperty::TextSource). Implementado em
//  EngineAeAnimation.cpp.
// =============================================================================
#pragma once

#include "aurea/command/Command.hpp"
#include "aurea/core/Result.hpp"

namespace aurea {
struct Layer;

namespace ae_anim {

/// Comandos de keyframe na trilha virtual do texto-fonte (inserir, apagar,
/// mover; os keys são sempre congelados). Quem chama segura o modelo e a
/// transação de desfazer da composição.
Status text_source_key_command(Layer& layer, const Command& command, FrameIndex playhead) noexcept;

/// Antes de uma edição de texto (conteúdo, tamanho, cor, fonte, trechos): com
/// keys, o texto de base passa a ser o do key que aparece no cabeçote — a
/// edição parte do que a pessoa vê.
void begin_text_edit(Layer& layer, FrameIndex playhead) noexcept;
/// Depois da edição: com keys, o key do cabeçote leva o resultado (cria um
/// se não havia ali), como o cronômetro ligado do AE.
void end_text_edit(Layer& layer, FrameIndex playhead) noexcept;

} // namespace ae_anim
} // namespace aurea
