// =============================================================================
//  Aurea / text / Captions.hpp
//
//  Legendas automáticas (Fase 7C). O motor não sabe de onde vêm as palavras:
//  quem transcreve (Whisper local, SRT importado, outro provedor) entrega
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

// Times are integer frames local to one dedicated caption layer. Stable ids
// survive text edits, moves and undo; a segment never creates another layer.
struct CaptionToken { std::string text; i64 start = 0, end = 0; };
struct CaptionSegment {
    u64 id = 0;
    i64 start = 0, end = 0;
    std::string text;
    std::vector<CaptionToken> words;
};
[[nodiscard]] const CaptionSegment* active_caption(const std::vector<CaptionSegment>& segments, i64 frame) noexcept;
[[nodiscard]] bool valid_caption_track(const std::vector<CaptionSegment>& segments) noexcept;
// Matching words retain their original timestamps; inserted/replaced words
// use only the gap between their preserved neighbours.
void edit_caption_text(CaptionSegment& segment, const std::string& text);

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

inline constexpr u32 kCaptionStyleCount = 8;
/// 0 Clássico, 1 Caixa, 2 Destaque, 3 Neon, 4 Karaokê, 5 Pop.
[[nodiscard]] const char* caption_style_name(u32 id) noexcept;

/// Tempo mínimo na tela, duração máxima de um bloco e vão que some (0.2.5).
inline constexpr f64 kCaptionMinDisplaySec = 0.8;
inline constexpr f64 kCaptionMaxBlockSec = 4.0;
inline constexpr f64 kCaptionGapCloseSec = 0.3;

/// Agrupa as palavras. Limites rígidos: palavras, caracteres por linha,
/// linhas, pausa (`breakOnPause`) e 4 s por bloco. Dentro deles escolhe os
/// cortes de menor custo na frase inteira: fim de frase, vírgula/oração e
/// pausa são bons; terminar em artigo/preposição, bloco de uma palavra só e
/// bloco curto demais custam. Linhas equilibradas; o fim de cada bloco cobre
/// vãos < 0,3 s e o mínimo de 0,8 s na tela, sem invadir o seguinte.
[[nodiscard]] std::vector<CaptionGroup> group_captions(const std::vector<CaptionWord>& words, const CaptionOptions& opt);

/// Largura na tela em colunas: ideograma/emoji = 2, marca combinante = 0.
[[nodiscard]] u32 caption_width(const std::string& s) noexcept;
/// Junta duas palavras como a legenda mostra: sem espaço entre ideogramas e
/// kana (chinês/japonês não separam palavras) nem antes de pontuação.
[[nodiscard]] std::string caption_join(const std::string& left, const std::string& right);
/// Dois pedaços de escrita sem espaço (ideograma/kana completos dos dois
/// lados): o Whisper não marca a fronteira com espaço, e cada token vira palavra.
[[nodiscard]] bool caption_script_boundary(const std::string& left, const std::string& right) noexcept;
/// Só pontuação de fechamento ("," "?" "。" "»"…): gruda na palavra anterior.
[[nodiscard]] bool caption_closing_punctuation(const std::string& s) noexcept;
/// Limpeza antes de agrupar: espaços, pontuação solta grudada na vizinha,
/// tempos inválidos e maiúscula depois de fim de frase. Idempotente.
[[nodiscard]] std::vector<CaptionWord> normalize_caption_words(const std::vector<CaptionWord>& words);

/// CAIXA ALTA para exibição (ASCII + latino suplementar, igual ao agrupamento).
/// Aplicar na renderização deixa a opção "caixa alta" viva: um preset que a
/// liga/desliga aparece na hora, sem perder o texto original guardado.
[[nodiscard]] std::string upper_text(const std::string& s);

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
void apply_caption_animation(const CaptionOptions& opt, TextData& t, TrackSet& tracks,
                             const std::vector<i64>& wordFrames, i64 endFrame);

} // namespace aurea::text
