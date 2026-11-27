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

#include <optional>
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

    /// 0.4.6: aparência própria por cima do estilo. Cada bit de `look` troca
    /// uma parte do estilo pelo valor daqui; sem o bit, vale a do estilo.
    u32  look = 0;
    std::string fontFamily;   ///< kCaptionLookFont (vazio = fonte do aparelho)
    u16  fontWeight = 700;
    bool fontItalic = false;
    std::string fontPath;     ///< fonte importada (arquivo); vazio = do aparelho
    Vec4 fill{1.0f, 1.0f, 1.0f, 1.0f};          ///< kCaptionLookFill
    Vec4 stroke{0.0f, 0.0f, 0.0f, 1.0f};        ///< kCaptionLookStroke
    f32  strokeFrac = 0.08f;                    ///< contorno, fração do tamanho (0 = sem)
    bool box = false;                           ///< kCaptionLookBox: fundo ligado
    Vec4 boxColor{0.0f, 0.0f, 0.0f, 0.7f};
    bool shadow = false;                        ///< kCaptionLookShadow: sombra ligada

    /// 0.5.3 (legendas no jeito do Premiere): a janela "Criar legendas".
    /// Os padrões repetem o que o motor fazia antes (projeto antigo igual).
    f32  minDurationSec = 0.8f;   ///< tempo mínimo na tela (era kCaptionMinDisplaySec)
    u32  gapFrames = 0;           ///< quadros vazios entre duas legendas (0 = encostadas)
    bool breakOnSentence = false; ///< o fim de frase sempre fecha a legenda
    /// kCaptionLookLayout: alinhamento e entrelinha.
    u32  align = 1;               ///< 0 esquerda, 1 centro, 2 direita
    f32  lineSpacing = 1.15f;     ///< altura da linha / tamanho do texto
    /// kCaptionLookBox: margem e raio do fundo, fração do tamanho do texto.
    f32  boxPadding = 0.3f;
    f32  boxRadius = 0.25f;
    /// kCaptionLookShadow: cor e desfoque (fração do tamanho) da sombra.
    Vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.6f};
    f32  shadowBlur = 0.12f;
};

inline constexpr u32 kCaptionLookFont = 1u << 0;
inline constexpr u32 kCaptionLookFill = 1u << 1;
inline constexpr u32 kCaptionLookStroke = 1u << 2;
inline constexpr u32 kCaptionLookBox = 1u << 3;
inline constexpr u32 kCaptionLookShadow = 1u << 4;
inline constexpr u32 kCaptionLookLayout = 1u << 5;   ///< 0.5.3: alinhamento e entrelinha
inline constexpr u32 kCaptionLookAll = (1u << 6) - 1;

// Times are integer frames local to one dedicated caption layer. Stable ids
// survive text edits, moves and undo; a segment never creates another layer.
struct CaptionToken { std::string text; i64 start = 0, end = 0; };
struct CaptionSegment {
    u64 id = 0;
    i64 start = 0, end = 0;
    std::string text;
    std::vector<CaptionToken> words;
    /// 0.4.6: estilo só deste bloco (o "Estilo" dos blocos marcados). Vale a
    /// parte de aparência — estilo, destaque, caixa alta e `look`; agrupamento,
    /// posição e tamanho continuam os da faixa. Vazio = o da faixa.
    std::optional<CaptionOptions> look;
};
[[nodiscard]] const CaptionSegment* active_caption(const std::vector<CaptionSegment>& segments, i64 frame) noexcept;
[[nodiscard]] bool valid_caption_track(const std::vector<CaptionSegment>& segments) noexcept;
/// Deixa a faixa válida sem perder o resto do projeto: acerta a ordem, tira palavras
/// e blocos que não dá para mostrar e troca ids repetidos. Só a leitura usa isto.
void repair_caption_track(std::vector<CaptionSegment>& segments);
// Matching words retain their original timestamps; inserted/replaced words
// use only the gap between their preserved neighbours.
void edit_caption_text(CaptionSegment& segment, const std::string& text);

/// Leva só a aparência de `from` para `to` (estilo, destaque, caixa alta e
/// `look` com os valores dele); o resto de `to` fica.
void copy_caption_appearance(const CaptionOptions& from, CaptionOptions& to);
/// As opções que valem para o bloco: as da faixa com a aparência do bloco.
[[nodiscard]] CaptionOptions caption_block_options(const CaptionOptions& track, const CaptionSegment& segment);

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

/// SRT (ou WebVTT) → palavras (o tempo de cada bloco é dividido pelo tamanho das palavras).
[[nodiscard]] std::vector<CaptionWord> parse_srt(const std::string& srt);

/// Estilo na camada de texto. `wordFrames` = início de cada palavra da
/// legenda em quadros LOCAIS da camada (destaque/pop seguem a fala).
void apply_caption_style(const CaptionOptions& opt, u32 compShortSide, TextData& t, TrackSet& tracks,
                         const std::vector<i64>& wordFrames, i64 endFrame);
void apply_caption_animation(const CaptionOptions& opt, TextData& t, TrackSet& tracks,
                             const std::vector<i64>& wordFrames, i64 endFrame);

/// 0.4.6: troca de opções numa faixa já pronta. Mudou o estilo: refaz tudo.
/// Senão mexe só no que mudou (tamanho em escala, cada parte de `look`), para
/// não apagar a aparência que veio de um preset da comunidade ou do painel Texto.
void update_caption_look(const CaptionOptions& before, const CaptionOptions& after, u32 compShortSide,
                         TextData& t, TrackSet& tracks);
/// O texto do bloco com estilo próprio: o texto da faixa (`t`, com o que o
/// painel Texto mudou) e por cima as partes em que o bloco difere da faixa.
/// Os animadores do usuário ficam (as trilhas deles apontam pelo índice).
void apply_caption_block_look(const CaptionOptions& track, const CaptionOptions& block, u32 compShortSide, TextData& t);

/// Resolve the editable text and word-timing keyframes on top of the track's base text and animation.
void resolve_caption_block(const CaptionOptions& track, const CaptionSegment& segment, u32 shortSide,
                           TextData& t, TrackSet& tracks);

/// Modelos prontos da legenda (escolhidos antes de gerar). `id` é estável:
/// a interface traduz o nome por ele. As opções só trazem estilo e aparência;
/// agrupamento, posição e tamanho continuam os do usuário.
struct CaptionTemplate {
    const char* id;
    CaptionOptions options;
};
[[nodiscard]] const std::vector<CaptionTemplate>& caption_templates();
/// Leva estilo e aparência do modelo para `opt` (o resto fica).
void apply_caption_template(const CaptionTemplate& tpl, CaptionOptions& opt);

// ---- 0.5.3: edição no jeito do Premiere (regras puras, quadros locais da faixa) ----

/// Deixa `gapFrames` quadros vazios entre blocos seguidos: o fim recua, nunca
/// abaixo de 1 quadro na tela. 0 = nada muda.
void apply_caption_gap(std::vector<CaptionSegment>& segments, u32 gapFrames);
/// Divide no cursor do texto: `left` fica com as `leftWords` primeiras palavras e
/// `right` (mesmo estilo, id a acertar) com o resto, no início da primeira delas.
[[nodiscard]] bool split_caption_at_word(CaptionSegment& left, CaptionSegment& right, usize leftWords);
/// Entrada/saída do bloco `index` andam `deltaIn`/`deltaOut` quadros sem invadir os
/// vizinhos e sem ficar com menos de 1 quadro; as palavras ficam dentro.
void nudge_caption(std::vector<CaptionSegment>& segments, usize index, i64 deltaIn, i64 deltaOut);
/// Troca `find` por `replacement` em `text` (sem diferenciar maiúsculas de
/// ASCII/Latin-1 quando `matchCase` é falso). Devolve quantas trocas fez.
[[nodiscard]] u32 replace_caption_text(std::string& text, const std::string& find, const std::string& replacement, bool matchCase);
/// Bloco novo [start, end) com `text`; as palavras repartem o tempo por igual.
[[nodiscard]] CaptionSegment make_caption_segment(u64 id, i64 start, i64 end, const std::string& text);
/// O arquivo SRT (ou WebVTT) dos blocos. `shift` leva o quadro local da faixa ao
/// quadro da timeline; caixa alta como a tela mostra.
[[nodiscard]] std::string export_captions(const std::vector<CaptionSegment>& segments, f64 fps, i64 shift, bool vtt, bool uppercase);

} // namespace aurea::text
