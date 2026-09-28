// =============================================================================
//  Aurea / audio / AudioEffects.hpp
//
//  Os efeitos de ÁUDIO da camada (Reverso, Atraso, Flange e chorus, Passa-
//  alta/baixa, Mixer estéreo, Modulador, EQ paramétrico, Reverb, Tom).
//
//  O que o usuário vê é um efeito comum na pilha da camada (registro de
//  efeitos, categoria "Áudio", keyframes e expressões como qualquer outro).
//  O que o mixer vê é uma CADEIA de estágios por clipe (`FxStage`), montada no
//  snapshot a partir desses efeitos, com os parâmetros já resolvidos no tempo
//  (constante, ou um valor por quadro da camada quando animado).
//
//  A cadeia roda como um FLUXO contínuo por clipe (ver `MixState`): o estado
//  (linhas de atraso, filtros, reverberação) vive entre as chamadas do mixer.
//  Um salto no tempo (seek, play noutro ponto, edição) recomeça o fluxo com
//  PRÉ-ROLAGEM: o trecho `preroll` antes do ponto é processado e descartado,
//  e o som que sai é o mesmo de ter tocado desde antes (até o resto do
//  estado mais velho que a pré-rolagem, que já decaiu abaixo do audível).
//  Parâmetros são lidos em sub-blocos alinhados ao tempo ABSOLUTO (32
//  amostras) e as fases (Tom, LFOs) são a INTEGRAL da frequência desde o
//  início da camada: o resultado não depende do tamanho do bloco que o mixer
//  pede nem de onde o play começou (preview == export).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aurea::audio {

/// Chaves estáveis dos efeitos de áudio (o id no projeto é o hash delas).
namespace fx_keys {
    inline constexpr const char* kBackwards    = "aurea.audio.backwards";
    inline constexpr const char* kDelay        = "aurea.audio.delay";
    inline constexpr const char* kFlangeChorus = "aurea.audio.flange_chorus";
    inline constexpr const char* kHighLowPass  = "aurea.audio.high_low_pass";
    inline constexpr const char* kStereoMixer  = "aurea.audio.stereo_mixer";
    inline constexpr const char* kModulator    = "aurea.audio.modulator";
    inline constexpr const char* kParametricEq = "aurea.audio.parametric_eq";
    inline constexpr const char* kReverb       = "aurea.audio.room_reverb";
    inline constexpr const char* kTone         = "aurea.audio.tone";
}

/// Tipos de estágio da cadeia. `Envelope` é interno: o volume, os fades e o
/// balanço do clipe (entra depois dos efeitos da própria camada e antes dos
/// efeitos de uma pré-composição que a contém).
enum class FxKind : u8 {
    Envelope = 0,
    Backwards,
    Delay,
    FlangeChorus,
    HighLowPass,
    StereoMixer,
    Modulator,
    ParametricEq,
    Reverb,
    Tone,
};

// Índices dos parâmetros — o MESMO contrato da declaração do efeito
// (effects/builtin/AudioEffects.cpp) e do DSP.
namespace fxp {
    enum Backwards : u32 { kSwapChannels = 0, kBackwardsCount };
    enum Delay : u32 { kDelayTime = 0, kDelayAmount, kDelayFeedback, kDelayDry, kDelayWet, kDelayCount };
    enum FlangeChorus : u32 { kVoiceSeparation = 0, kVoices, kFlangeRate, kFlangeDepth, kVoicePhase, kFlangeInvert,
                              kStereoVoices, kFlangeDry, kFlangeWet, kFlangeCount };
    enum HighLow : u32 { kFilterType = 0, kCutoff, kFilterDry, kFilterWet, kFilterCount };
    enum StereoMixer : u32 { kLeftLevel = 0, kRightLevel, kLeftPan, kRightPan, kMixerInvert, kMixerCount };
    enum Modulator : u32 { kModType = 0, kModRate, kModDepth, kModAmplitude, kModCount };
    /// EQ: banda b (0..2) começa em `b * kEqBandStride`.
    enum ParametricEq : u32 { kEqEnable = 0, kEqFrequency, kEqBandwidth, kEqGain, kEqBandStride, kEqCount = 12 };
    enum Reverb : u32 { kReverbTime = 0, kDiffusion, kDecay, kBrightness, kReverbDry, kReverbWet, kReverbCount };
    enum Tone : u32 { kWaveform = 0, kFreq1, kFreq2, kFreq3, kFreq4, kFreq5, kToneLevel, kToneCount };
}

/// Um parâmetro de estágio: constante, ou um valor por quadro da composição
/// da camada dona (a partir de `FxStage::frame0`), interpolado linearmente.
/// `integral[i]` = ∫ valor dt (segundos) desde `FxStage::origin` até o
/// início do quadro i — só nos parâmetros que são FREQUÊNCIA de oscilador.
struct FxParam {
    f32 value = 0.0f;
    std::vector<f32> byFrame;
    std::vector<f64> integral;
};

struct FxStage {
    FxKind kind = FxKind::Envelope;
    std::vector<FxParam> params;
    /// Régua do tempo dos parâmetros: amostra da raiz − envShift = amostra da
    /// composição da camada dona; quadro = amostra × fps / 48000.
    i64 envShift = 0;
    i64 frame0 = 0;
    f64 fps = 30.0;
    /// Amostra (raiz) onde as fases começam: o início da camada dona.
    i64 origin = 0;
    /// Maior valor que um parâmetro de tempo assume (dimensiona as linhas).
    f32 maxDelayMs = 0.0f;

    /// Valor do parâmetro `i` na amostra `t` da raiz.
    [[nodiscard]] f32 at(u32 i, i64 t) const noexcept;
    /// ∫ do parâmetro `i` (Hz → voltas) de `origin` até `t`.
    [[nodiscard]] f64 turns(u32 i, i64 t) const noexcept;
};

/// Pré-rolagem que a cadeia precisa (amostras), já com teto.
[[nodiscard]] i64 chain_preroll(const std::vector<FxStage>& chain) noexcept;

/// Teto da pré-rolagem: 8 s. Reverb/atraso com realimentação acima disso
/// continuam soando; o que sobra do estado mais velho que 8 s fica fora.
inline constexpr i64 kMaxPreroll = 8 * 48000;

// -----------------------------------------------------------------------------
// Resposta do EQ paramétrico (o gráfico do painel e os testes usam a MESMA
// conta do filtro que toca).
// -----------------------------------------------------------------------------
struct Biquad {
    f64 b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};
/// Pico/vale (RBJ) em `hz`, largura de banda em % da frequência central
/// (Q = 100 / largura), ganho em dB, a 48 kHz.
[[nodiscard]] Biquad eq_band(f64 hz, f64 bandwidthPercent, f64 gainDb) noexcept;
/// Passa-alta (`highPass`) ou passa-baixa de 2ª ordem (Butterworth) em `hz`.
[[nodiscard]] Biquad pass_filter(bool highPass, f64 hz) noexcept;
/// |H| em dB do filtro na frequência `hz`.
[[nodiscard]] f64 biquad_response_db(const Biquad& b, f64 hz) noexcept;

// -----------------------------------------------------------------------------
// Análise para os efeitos visuais (Forma de onda / Espectro de áudio)
// -----------------------------------------------------------------------------
/// Magnitudes LINEARES de `bands` faixas entre `startHz` e `endHz` (espaçadas
/// linearmente, graves à esquerda). Um seno
/// em escala cheia dá ~1 na faixa dele. `averaging`: média de janelas de 1024
/// ao longo do trecho em vez de um FFT só. Devolve a faixa mais forte.
u32 analyze_linear_bands(const f32* mono, u32 count, f32 startHz, f32 endHz, u32 bands, bool averaging,
                         f32* out) noexcept;

} // namespace aurea::audio
