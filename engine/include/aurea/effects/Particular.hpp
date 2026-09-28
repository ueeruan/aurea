// =============================================================================
//  Aurea / effects / Particular.hpp
//
//  O Particular: o sistema de partículas do app antigo, refeito nativo no
//  motor como o efeito `effect_keys::kParticular` (ParticularEffect.cpp).
//
//  Ele substituiu o motor das camadas de partículas: uma camada nova de
//  partículas é uma `LayerKind::ParticleSystem` marcada com
//  `ParticleEmitter::Particular`, cuja imagem é transparente e cujo primeiro
//  efeito é o Particular. As camadas antigas (qualquer outro emissor) abrem e
//  desenham como sempre — nenhum projeto perde camada.
//
//  Os presets são os do app antigo. Os números da API (`add_particles`,
//  `apply_particle_preset`) começam em `kPresetBase` para não colidir com os
//  presets das camadas antigas (0..18), que continuam valendo.
// =============================================================================
#pragma once

#include "aurea/effects/Parameter.hpp"

namespace aurea::particular {

/// Presets: 0 Padrão, 1 Chuva, 2 Neve, 3 Fogo, 4 Faíscas, 5 Fogos de
/// artifício, 6 Poeira, 7 Bokeh.
inline constexpr u32 kPresetCount = 8;
/// Número do preset 0 na API do motor/bridges (preset `kPresetBase + i`).
inline constexpr u32 kPresetBase = 20;

/// Índices dos parâmetros do efeito (contrato com a UI e com os projetos:
/// só se acrescenta no fim).
enum Param : u32 {
    kRate = 0, kPreRoll, kPositionX, kPositionY, kPositionZ,
    kEmitterW, kEmitterH, kEmitterD, kEmitterSphere,
    kVelocity, kVelocityRandom, kDirectionTilt, kDirectionSpin, kSpread, kOutwards,
    kGravity, kWindX, kWindY, kWindZ, kAirDrag, kTurbulence, kTurbulenceSpeed,
    kLife, kLifeRandom, kSize, kSizeRandom, kSizeEnd, kOpacity, kFadeIn, kFadeOut,
    kFeather, kStretch, kColor, kColorEnd, kColorRandom, kAddMode, kShowSource, kSeed,
    kParamCount,
};

/// Escreve o preset `preset` (0..kPresetCount-1) na instância: volta todos os
/// parâmetros ao padrão da declaração e aplica os do preset. A semente é
/// preservada. false = preset fora da faixa ou instância incompatível.
[[nodiscard]] bool apply_preset(EffectInstance& instance, const ParameterRegistry& params, u32 preset) noexcept;

} // namespace aurea::particular
