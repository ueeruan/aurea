// =============================================================================
//  Peças internas do mixer que o fluxo dos efeitos de áudio (AudioFx.cpp)
//  divide com o caminho de sempre (AudioMixer.cpp). Não é cabeçalho público.
// =============================================================================
#pragma once

#include "aurea/audio/Audio.hpp"

namespace aurea::audio::detail {

/// Ganho do clipe na amostra `t` da raiz: ganho × volume (animado) × fades.
[[nodiscard]] f32 clip_envelope(const AudioClip& c, i64 t) noexcept;
/// Amostra (fracionária) da fonte que toca em `t` (velocidade/remapeamento).
[[nodiscard]] f64 clip_source_pos(const AudioClip& c, i64 t) noexcept;

/// O trecho [s0, s1) de um clipe COM cadeia de efeitos, somado em `out`
/// (quadro 0 de `out` = amostra `start`). `missing` conta blocos que faltaram.
void mix_fx_clip(const AudioClip& c, i64 s0, i64 s1, i64 start, BlockSource& blocks, f32* out, MixState* state,
                 u64 epoch, u32& missing) noexcept;

} // namespace aurea::audio::detail
