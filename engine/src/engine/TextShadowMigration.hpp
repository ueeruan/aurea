// =============================================================================
//  Aurea / engine / TextShadowMigration.hpp  (interno do motor)
//
//  Funções do AUREA, decisão D2 do dono (13/11/2026): o texto tinha DUAS
//  sombras. Fica só a mais nova, a decoração "Sombra" de Borda e sombra
//  (efeito `aurea.stylize.drop_shadow` com papel kEffectRoleShadow, da 0.3.2
//  Omega, commit 86a18410 de 26/10/2026). A sombra do estilo do texto
//  (TextData::shadow, Fase 7A, commit 2e07406a de 22/09/2026) sai da interface.
//
//  Projeto antigo, preset de texto salvo com a sombra antiga: a camada de texto
//  ganha a decoração com a mesma cor, opacidade, deslocamento e suavidade, e
//  TextData::shadow desliga. Faixas de legenda (Layer::captions) ficam de fora:
//  o estilo da legenda é refeito pelo motor e usa a sombra do texto por dentro.
//  Implementado em EngineOmegaAppearance.cpp.
// =============================================================================
#pragma once

namespace aurea {
struct Layer;
class EffectRegistry;

/// Converte a sombra antiga do texto em decoração. Devolve true quando a
/// camada mudou. Quem chama segura o modelo. Se a camada já tem a decoração
/// de sombra, a sombra antiga entra como efeito "Sombra projetada" comum no
/// início da pilha, para nada perder o visual. Com a pilha cheia (ou sem o
/// efeito no registro) nada muda e a sombra antiga continua desenhando.
bool migrate_text_shadow_to_decoration(Layer& layer, const EffectRegistry& registry) noexcept;

} // namespace aurea
