// =============================================================================
//  Peças internas do mixer que o fluxo dos efeitos de áudio (AudioFx.cpp)
//  divide com o caminho de sempre (AudioMixer.cpp). Não é cabeçalho público.
// =============================================================================
#pragma once

#include "aurea/audio/Audio.hpp"

#include <cmath>

namespace aurea::audio::detail {

/// Ganho do clipe na amostra `t` da raiz: ganho × volume (animado) × fades.
[[nodiscard]] f32 clip_envelope(const AudioClip& c, i64 t) noexcept;
/// Amostra (fracionária) da fonte que toca em `t` (velocidade/remapeamento).
[[nodiscard]] f64 clip_source_pos(const AudioClip& c, i64 t) noexcept;

/// Manter o tom (AudioClip::keepPitch): grãos de `kPitchGrain` amostras com
/// janela de Hann e 50% de sobreposição (as duas janelas somam exatamente 1).
/// Cada grão nasce na posição da fonte do seu CENTRO (a mesma função de tempo
/// do vídeo) e dali toca a 1× — para trás se a fonte anda para trás. Sem estado:
/// a mesma amostra sai igual em qualquer salto ou tamanho de bloco. A 1× a soma
/// reconstrói a fonte exatamente; fonte parada (quadro congelado) = silêncio.
/// `read(idx, l, r)` lê uma amostra inteira da fonte (falso = sem som ali).
inline constexpr i64 kPitchGrain = 2048;
template <class Read>
inline void keep_pitch_at(const AudioClip& c, i64 t, Read&& read, f32& l, f32& r) noexcept {
    l = r = 0.0f;
    constexpr i64 kHop = kPitchGrain / 2;
    const i64 k = t >= 0 ? t / kHop : -((-t + kHop - 1) / kHop);
    for (i64 g = k - 1; g <= k; ++g) {
        const i64 center = g * kHop + kHop;
        const f64 u = static_cast<f64>(t - g * kHop) / static_cast<f64>(kPitchGrain);
        const f32 w = static_cast<f32>(0.5 - 0.5 * std::cos(6.283185307179586 * u));
        const f64 speed = (clip_source_pos(c, center + kHop) - clip_source_pos(c, center - kHop)) / static_cast<f64>(2 * kHop);
        if (!std::isfinite(speed) || std::fabs(speed) < 0.02) continue;
        const f64 pos = clip_source_pos(c, center) + (speed > 0.0 ? 1.0 : -1.0) * static_cast<f64>(t - center);
        if (!(pos >= 0.0) || pos >= static_cast<f64>(c.sourceLength)) continue;
        const i64 i0 = static_cast<i64>(pos);
        const f32 fr = static_cast<f32>(pos - static_cast<f64>(i0));
        f32 l0 = 0, r0 = 0, l1 = 0, r1 = 0;
        if (!read(i0, l0, r0)) continue;
        if (!read(i0 + 1, l1, r1)) { l1 = l0; r1 = r0; }
        l += (l0 + (l1 - l0) * fr) * w;
        r += (r0 + (r1 - r0) * fr) * w;
    }
}

/// O trecho [s0, s1) de um clipe COM cadeia de efeitos, somado em `out`
/// (quadro 0 de `out` = amostra `start`). `missing` conta blocos que faltaram.
void mix_fx_clip(const AudioClip& c, i64 s0, i64 s1, i64 start, BlockSource& blocks, f32* out, MixState* state,
                 u64 epoch, u32& missing) noexcept;

} // namespace aurea::audio::detail
