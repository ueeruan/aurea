// =============================================================================
//  Aurea / text / WhisperQuality.hpp
//
//  O que fica em volta do Whisper local (0.2.5) e não depende dele, para os
//  testes do motor exercitarem sem modelo:
//    - atividade de voz por energia: janelas cortadas em pausas e silêncio
//      puro nunca chega ao decodificador (é dele que saem as alucinações);
//    - mapa de tempo dos trechos de fala colados numa janela;
//    - filtros de alucinação (créditos de legenda, despedidas no silêncio,
//      laços de repetição, eco do prompt);
//    - acabamento do tempo das palavras pela energia.
// =============================================================================
#pragma once

#include "aurea/text/Captions.hpp"

#include <string>
#include <utility>
#include <vector>

namespace aurea::text {

/// Energia de um trecho mono em quadros de 10 ms. `voiced` já sem estalos
/// (< 60 ms) e sem os furos curtos (< 150 ms) dentro da fala; `regions` é a
/// fala com folga (200 ms antes, 300 ms depois), em segundos da mídia.
struct SpeechActivity {
    f64 start = 0.0;
    f64 frame = 0.01;
    std::vector<f32> db;
    std::vector<u8> voiced;
    std::vector<std::pair<f64, f64>> regions;
    f32 floorDb = -120.0f, thresholdDb = -120.0f;

    [[nodiscard]] f64 end() const noexcept { return start + frame * static_cast<f64>(voiced.size()); }
    /// Fração de quadros com voz em [from, to) (fora do trecho analisado = 1).
    [[nodiscard]] f64 voiced_ratio(f64 from, f64 to) const noexcept;
};

[[nodiscard]] SpeechActivity analyze_speech(const f32* mono, usize count, u32 rate, f64 start);

inline constexpr f64 kWhisperWindowSec = 28.0;   ///< o modelo vê 30 s; a margem evita o corte dele
inline constexpr f64 kWhisperGapSec = 0.5;       ///< silêncio entre dois trechos colados
inline constexpr f64 kWhisperMergeGapSec = 1.0;  ///< pausa até aqui fica dentro do trecho

/// Uma janela do Whisper: trechos de fala (segundos da mídia) colados com
/// `kWhisperGapSec` entre eles, até 28 s. `spans` vazio = nada a transcrever
/// (silêncio ou falta ler mais áudio); o laço segue de `next`.
struct WhisperWindow {
    std::vector<std::pair<f64, f64>> spans;
    f64 ownBegin = 0, ownEnd = 0;   ///< palavras com o meio aqui pertencem a esta janela
    f64 next = 0;                   ///< onde a próxima janela começa a ler
    f64 nextOwn = 0;                ///< onde começa a posse da próxima (corte sem pausa: 1 s de sobreposição)
};

/// `limit` = fim do áudio lido; `final` = não há áudio depois dele.
[[nodiscard]] WhisperWindow plan_whisper_window(const SpeechActivity& a, f64 cursor, f64 own, f64 limit, bool final);

/// Tempo do áudio colado → segundo da mídia (o vão entre trechos cai na borda mais próxima).
struct WhisperTimeMap {
    struct Piece { f64 packed0 = 0, packed1 = 0, source0 = 0; };
    std::vector<Piece> pieces;
    [[nodiscard]] f64 source(f64 packed) const noexcept;
};

/// Tamanho do modelo pelo cabeçalho ggml (camadas e vocabulário), não pelo nome do arquivo.
enum class WhisperModelSize : u8 { Unknown, Tiny, Base, Small, Medium, Large, LargeV3, Turbo };
struct WhisperModelInfo {
    WhisperModelSize size = WhisperModelSize::Unknown;
    bool english = false;   ///< modelo ".en" (vocabulário só de inglês)
};
[[nodiscard]] WhisperModelInfo whisper_model_info(i32 vocab, i32 audioLayers, i32 textLayers) noexcept;
[[nodiscard]] WhisperModelInfo read_whisper_model_info(const std::string& path) noexcept;

/// Núcleos menos um (a UI e o áudio continuam respondendo), entre 2 e 8.
[[nodiscard]] int whisper_thread_count(unsigned hardware) noexcept;

/// Frase típica de alucinação do Whisper (aprendida de legendas da internet).
/// Credit: crédito de legenda ("Legendas pela comunidade Amara.org") — sai
/// sempre; Closing: despedida ("Obrigado por assistir") — sai só quando o
/// áudio embaixo é fraco; NonSpeech: só símbolos ou anotação ("[Música]", "♪").
enum class WhisperPhrase : u8 { None, Credit, Closing, NonSpeech };
[[nodiscard]] WhisperPhrase classify_whisper_phrase(const std::string& text);

/// Tira laços de repetição (o mesmo n-grama 3–6 vezes seguidas, n = 1..6):
/// fica a primeira ocorrência (duas para palavra única). Devolve quantas saíram.
usize drop_repeated_ngrams(std::vector<CaptionWord>& words);
/// Tira do começo da janela o fim da transcrição anterior repetido (≥ 3 palavras) — eco do prompt.
usize drop_prompt_echo(std::vector<CaptionWord>& words, const std::vector<CaptionWord>& before);
/// O fim da fala para condicionar a próxima janela (curto: o prompt longo propaga erro).
[[nodiscard]] std::string whisper_prompt_tail(const std::vector<CaptionWord>& words, usize maxWords = 24, usize maxBytes = 200);
/// Início no ataque da voz, fim antes do silêncio, duração limitada pela mediana; sem sobreposição.
void refine_word_timing(std::vector<CaptionWord>& words, const SpeechActivity& a);

} // namespace aurea::text
