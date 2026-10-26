// =============================================================================
//  Aurea / effects / builtin / HyperFx.hpp (privado do motor)
//
//  Os passes Turbo do AureaHyper montados no FrameGraph, compartilhados entre
//  os efeitos de camada (HyperEffects.cpp) e o renderer (a interpolação do
//  Hyper Motion acontece na FONTE, onde o renderer escolhe os quadros):
//
//    * campo de movimento por bloco 16x16 (busca hierárquica, me_*.frag);
//    * quadro intermediário pelos dois sentidos com oclusão (mi_interp.frag);
//    * as cadeias de Upscale (FSR 1 / realce de linhas) e Clean.
// =============================================================================
#pragma once

#include "aurea/effects/Effect.hpp"

namespace aurea::hyper::fx {

inline constexpr u32 kBlock = 16;   ///< px da resolução cheia por bloco do campo

/// Tamanho do campo de blocos para uma imagem w x h.
void field_size(u32 w, u32 h, u32& fw, u32& fh) noexcept;

/// Luma perceptual dos dois quadros no mesmo texel (R = A, G = B).
[[nodiscard]] FGTexture luma_pair(EffectBuildContext& ctx, FGTexture a, FGTexture b, u32 w, u32 h,
                                  bool aPerceptual, bool bPerceptual, PassStage stage) noexcept;

/// Campo A -> B (backward = false) ou B -> A (true) a partir da luma do par
/// (a pirâmide é montada aqui). Saída fw x fh: RG = vetor (px), B = custo.
[[nodiscard]] FGTexture motion_field(EffectBuildContext& ctx, FGTexture luma, u32 w, u32 h, bool backward,
                                     PassStage stage) noexcept;

/// Os dois sentidos empacotados em `target` (fw x fh, RGBA: RG = A->B, BA =
/// B->A) — é o que o renderer guarda em cache por par de quadros.
[[nodiscard]] bool bidirectional_field(EffectBuildContext& ctx, FGTexture a, FGTexture b, u32 w, u32 h,
                                       FGTexture target, PassStage stage) noexcept;

/// O quadro em `t` (0..1) entre A e B. `luma` = luma_pair(A, B) (pode ser
/// inválida: é montada aqui). Mesma densidade/espaço das entradas.
[[nodiscard]] FGTexture interpolate(EffectBuildContext& ctx, FGTexture a, FGTexture b, FGTexture packedField,
                                    FGTexture luma, u32 w, u32 h, f32 t, PassStage stage) noexcept;

} // namespace aurea::hyper::fx
