// =============================================================================
//  Aurea / text / Captions.hpp
//
//  Legendas automáticas (Fase 7C). O motor não sabe de onde vêm as palavras:
//  quem transcreve (Groq Whisper, SRT importado, outro provedor) entrega
//  palavras com tempo, e aqui elas viram camadas de texto — agrupadas ou uma
//  por palavra, com quebra de linha, área segura, estilos e destaque da
//  palavra falada (animador de texto, keyframes = dados).
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <string>
#include <vector>

namespace aurea {
struct TextData;
class TrackSet;
}

namespace aurea::text {

/// Uma palavra falada, em segundos da MÍDIA (não da timeline).
struct CaptionWord {
    std::string text;
    f64 start = 0.0;
    f64 end = 0.0;
};

struct CaptionOptions {
    u32  mode = 0;            ///< 0 agrupadas, 1 uma camada por palavra
    u32  maxWords = 4;        ///< por legenda (agrupadas)
    u32  maxChars = 18;       ///< por linha (quebra antes de passar)
    u32  maxLines = 2;
    u32  style = 0;           ///< kCaptionStyleCount estilos (dados)
    bool highlight = true;    ///< destaca a palavra falada
    bool uppercase = false;
    bool breakOnPause = true; ///< pausa maior que `pauseSec` fecha a legenda
    f32  pauseSec = 0.6f;
    f32  posY = 0.78f;        ///< centro da legenda, fração da altura (área segura: 0.1..0.9)
    f32  sizeFrac = 0.065f;   ///< tamanho do texto, fração do lado menor
    Vec4 highlightColor{1.0f, 0.83f, 0.0f, 1.0f};   ///< sRGB
};

/// Uma legenda pronta: texto com as quebras, tempo e as palavras dela.
struct CaptionGroup {
    std::string text;
    f64 start = 0.0, end = 0.0;
    u32 first = 0, count = 0;   ///< palavras [first, first+count)
};

inline constexpr u32 kCaptionStyleCount = 6;
/// 0 Clássico, 1 Caixa, 2 Destaque, 3 Neon, 4 Karaokê, 5 Pop.
[[nodiscard]] const char* caption_style_name(u32 id) noexcept;

/// Agrupa as palavras (limites de palavras, caracteres, linhas e pausas).
[[nodiscard]] std::vector<CaptionGroup> group_captions(const std::vector<CaptionWord>& words, const CaptionOptions& opt);

/// Vício de linguagem ("hum", "ahn", "tipo", "né", "uh", "um"…)?
[[nodiscard]] bool is_filler_word(const std::string& word);
/// Tira os vícios de linguagem (a legenda não mostra; o tempo continua).
[[nodiscard]] std::vector<CaptionWord> remove_filler_words(const std::vector<CaptionWord>& words);

/// SRT → palavras (o tempo de cada bloco é dividido pelo tamanho das palavras).
[[nodiscard]] std::vector<CaptionWord> parse_srt(const std::string& srt);

/// Estilo na camada de texto. `wordFrames` = início de cada palavra da
/// legenda em quadros LOCAIS da camada (destaque/pop seguem a fala).
void apply_caption_style(const CaptionOptions& opt, u32 compShortSide, TextData& t, TrackSet& tracks,
                         const std::vector<i64>& wordFrames, i64 endFrame);

} // namespace aurea::text
