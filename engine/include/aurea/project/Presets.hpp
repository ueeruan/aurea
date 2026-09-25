// =============================================================================
//  Aurea / project / Presets.hpp
//
//  Presets (Fase 7F): o que a pessoa montou numa camada vira um arquivo JSON
//  pequeno, legível e versionado — e volta para QUALQUER camada, de qualquer
//  projeto. O motor só escreve e lê o texto; onde o arquivo mora (pasta do
//  app, pacote embutido, nuvem) é problema de quem chama.
//
//  Por que JSON e não o binário do .aurea: preset é para durar entre versões
//  e ser trocado entre pessoas. Efeito vai pela CHAVE estável
//  ("aurea.blur.gaussian") e parâmetro pelo ID ("radius"), não por índice —
//  um efeito que ganhe parâmetro novo continua abrindo o preset antigo.
//
//  FORMATO (versão 1). Um objeto por arquivo:
//
//    {
//      "aurea_preset": 1,        versão; o leitor recusa versão maior que a dele
//      "kind": "effects" | "text" | "animation" | "caption" | "curve",
//      "name": "Brilho suave",
//      "fps": 30,                quadros/s de onde vieram os tempos (effects,
//                                text, animation); ao aplicar numa composição
//                                de outro fps, os tempos são reescalados
//      ... o bloco do tipo (abaixo)
//    }
//
//  Keyframe = array [t, valor, interp, bx1, by1, bx2, by2, tanIn, tanOut,
//  easing] — t em quadros RELATIVOS (ver cada tipo), interp = Interpolation
//  (0 Hold, 1 Linear, 2 Bezier, 3 EaseIn, 4 EaseOut, 5 EaseInOut, 6 Custom).
//  Só [t, valor] é obrigatório; o resto cai no padrão do Keyframe.
//  Trilha = {"static": v, "keys": [keyframe…]} mais o endereço do tipo.
//
//  "effects" — pilha de efeitos com os keyframes (tempo relativo ao INÍCIO da
//  camada). Aplicar ACRESCENTA no fim da pilha, com ids novos.
//    "effects": [{ "key": "aurea.blur.gaussian", "enabled": true,
//                  "params": [{"id": "radius", "index": 0, "v": [x,y,z,w],
//                              "ref": 0, "src": 0}],
//                  "curves": [[[x,y, x,y…] ×4 canais]…],
//                  "gradients": [[[pos, r,g,b,a]…]…],
//                  "tracks": [{"param": "radius", "index": 0, "comp": 0,
//                              "static": v, "keys": […]}] }]
//    Referência a camada/textura não viaja (é do projeto): vira 0.
//
//  "text" — estilo do texto e/ou Text Animator (tempo relativo ao início da
//  camada). Aplicar troca o estilo (o CONTEÚDO fica) e troca os animadores.
//    "style": { "fontFamily", "fontWeight", "fontItalic", "size", "color":[4],
//               "strokeWidth", "strokeColor":[4], "alignment", "lineHeight",
//               "tracking", "boxMode", "autoSize", "box":[x,y,w,h],
//               "background", "backgroundColor":[4], "backgroundPadding",
//               "backgroundRadius", "shadow", "shadowColor":[4],
//               "shadowOffset":[2], "shadowBlur" }
//    "animators": [{ "name", "enabled", "selector": {basedOn, type, shape,
//                    randomOrder, seed, start, end, offset, amount, easeHigh,
//                    easeLow, wiggleRate}, "props", "position":[3],
//                    "scale":[2], "rotation":[3], "opacity", "tracking",
//                    "blur", "skew", "strokeWidth", "charOffset",
//                    "fill":[4], "stroke":[4] }]
//    "tracks": [{"animator": i, "param": TextAnimParam, "static", "keys"}]
//    Fonte importada (arquivo do projeto) não viaja: só família/peso/itálico.
//
//  "animation" — keyframes do transform. Tempo relativo ao PRIMEIRO keyframe
//  (aplicar começa no cabeçote, ou no início da camada se o cabeçote estiver
//  fora); "span" = duração original, para esticar/encolher até outra duração.
//    "span": 30,
//    "tracks": [{"prop": "positionX", "relative": true, "static", "keys"}]
//    prop ∈ positionX/Y/Z, scaleX/Y/Z, rotationX/Y/Z, anchorX/Y/Z, opacity,
//    skewX/Y (escala e opacidade em fração: 1 = 100%). "relative": valores são
//    DESLOCAMENTOS somados ao valor que a camada de destino tem no instante
//    (salvar grava posição e âncora assim, com o primeiro keyframe em 0) — um
//    "entrar pela esquerda" funciona em qualquer lugar da tela. Aplicar troca
//    as trilhas envolvidas.
//
//  "caption" — opções da legenda automática (text::CaptionOptions):
//    "caption": { mode, maxWords, maxChars, maxLines, style, highlight,
//                 uppercase, breakOnPause, pauseSec, posY, sizeFrac,
//                 "highlightColor":[4], removeFillers }
//
//  "curve" — um easing: "curve": {"interp": 2, "x1", "y1", "x2", "y2"}.
//    Aplicado pelo caminho de sempre (KeyframeSetBezier), keyframe a keyframe.
//
//  Leitura DEFENSIVA: arquivo malformado, versão futura, tipo errado ou número
//  fora da faixa → false, sem tocar em nada (a camada só muda depois que o
//  preset inteiro foi lido e validado).
// =============================================================================
#pragma once

#include "aurea/animation/Curve.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/text/Captions.hpp"
#include "aurea/timeline/Layer.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurea {

class EffectRegistry;

// -----------------------------------------------------------------------------
// JSON mínimo (sem dependência): árvore de valores, leitor e escritor.
// -----------------------------------------------------------------------------
namespace json {

struct Value {
    enum class Type : u8 { Null = 0, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    f64  number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    [[nodiscard]] bool is_object() const noexcept { return type == Type::Object; }
    [[nodiscard]] bool is_array() const noexcept { return type == Type::Array; }
    [[nodiscard]] bool is_number() const noexcept { return type == Type::Number; }
    [[nodiscard]] bool is_string() const noexcept { return type == Type::String; }
    /// Membro do objeto (nulo se não for objeto ou não houver a chave).
    [[nodiscard]] const Value* get(std::string_view key) const noexcept;
};

/// Limites do leitor: texto até 4 MB, profundidade 64. Falso = malformado.
[[nodiscard]] bool parse(std::string_view text, Value& out, std::string* error = nullptr);

/// Escritor compacto. Números f32 saem com o MENOR texto que volta ao mesmo
/// f32 (ida e volta exata); strings com escape JSON.
class Writer {
public:
    Writer& begin_object();
    Writer& end_object();
    Writer& begin_array();
    Writer& end_array();
    Writer& key(std::string_view k);
    Writer& value(std::string_view s);
    Writer& value(const char* s) { return value(std::string_view(s ? s : "")); }
    Writer& value(f64 n);
    Writer& value(f32 n);
    Writer& value(i64 n);
    Writer& value(u32 n) { return value(static_cast<i64>(n)); }
    Writer& value(i32 n) { return value(static_cast<i64>(n)); }
    Writer& value(bool b);
    [[nodiscard]] const std::string& str() const noexcept { return out_; }

private:
    void comma();
    std::string out_;
    std::vector<bool> first_;   ///< por nível: ainda não escreveu elemento
    bool afterKey_ = false;
};

} // namespace json

// -----------------------------------------------------------------------------
// Presets
// -----------------------------------------------------------------------------
namespace presets {

inline constexpr u32 kPresetVersion = 1;

enum class PresetKind : u8 { Effects = 0, Text, Animation, Caption, Curve, Invalid = 255 };

[[nodiscard]] const char* kind_name(PresetKind k) noexcept;
[[nodiscard]] PresetKind kind_from_name(std::string_view s) noexcept;

/// O que salvar de uma camada de texto (kind = Text).
enum TextPresetParts : u32 { kTextStyle = 1u << 0, kTextAnimators = 1u << 1, kTextAll = 3u };

/// Trilha de animação do transform (kind = Animation).
struct AnimTrack {
    TrackProperty property = TrackProperty::PositionX;
    bool relative = false;   ///< valores = deslocamento somado ao valor da camada de destino
    Track track;             ///< tempos relativos ao primeiro keyframe do preset
};

/// Preset lido (ou montado de uma camada), pronto para aplicar.
struct Preset {
    PresetKind  kind = PresetKind::Invalid;
    std::string name;
    f64         fps = 30.0;

    // effects: effectIndex das trilhas = POSIÇÃO do efeito em `effects`;
    // tempos relativos ao início da camada.
    std::vector<EffectInstance> effects;
    std::vector<Track>          effectTracks;

    // text
    u32                       textParts = 0;   ///< TextPresetParts presentes
    TextData                  style;           ///< só os campos de estilo valem
    std::vector<TextAnimator> animators;
    std::vector<Track>        textTracks;      ///< TextAnimParam (effectIndex = animador)

    // animation
    std::vector<AnimTrack> animTracks;
    i64                    span = 0;           ///< do primeiro ao último keyframe

    // caption
    text::CaptionOptions caption;
    bool                 removeFillers = true;

    // curve
    Interpolation curveInterp = Interpolation::Bezier;
    f32 x1 = 0.42f, y1 = 0.0f, x2 = 0.58f, y2 = 1.0f;
};

/// Preset → JSON. `registry` (opcional) dá as chaves/ids estáveis dos efeitos;
/// sem ele, os efeitos saem só com o id numérico do tipo e índices.
[[nodiscard]] std::string write(const Preset& p, const EffectRegistry* registry = nullptr);

/// JSON → preset. Falso (com `error`) em qualquer problema; `out` só é
/// preenchido quando tudo foi validado.
[[nodiscard]] bool parse(std::string_view text, Preset& out, const EffectRegistry* registry = nullptr,
                         std::string* error = nullptr);
/// O mesmo, de um objeto já lido (lista de presets num arquivo só).
[[nodiscard]] bool parse_value(const json::Value& root, Preset& out, const EffectRegistry* registry = nullptr,
                               std::string* error = nullptr);

/// Monta o preset `kind` (Effects, Text ou Animation) da camada. Falso quando
/// não há o que salvar (sem efeitos, camada não é texto, sem keyframe de
/// transform). `parts` = TextPresetParts (só Text).
[[nodiscard]] bool capture(const Layer& layer, PresetKind kind, std::string name, f64 fps, u32 parts,
                           const EffectRegistry* registry, Preset& out);

/// Preset de efeitos com UM efeito só: a instância `effectId` (o id estável
/// do efeito na camada, não a posição na pilha) com os keyframes dela — o
/// MESMO formato "effects", então aplica pelo caminho de sempre. Falso = a
/// camada não tem esse efeito.
[[nodiscard]] bool capture_effect(const Layer& layer, u32 effectId, std::string name, f64 fps,
                                  const EffectRegistry* registry, Preset& out);

/// Aplica o preset (Effects, Text ou Animation) na camada. `anchorLocal` =
/// instante LOCAL onde a animação começa (Animation; Effects e Text usam o
/// início da camada); `durationFrames` > 0 estica a animação até essa
/// duração. `fps` = da composição de destino. Falso = tipo não se aplica a
/// esta camada (nada muda).
bool apply(const Preset& p, Layer& layer, i64 anchorLocal, i64 durationFrames, f64 fps,
           const EffectRegistry* registry = nullptr);

/// O preset se aplica a esta camada? (mesma regra de `apply`, sem mudar nada)
[[nodiscard]] bool applicable(const Preset& p, const Layer& layer) noexcept;

/// Atalhos para os tipos que não moram na camada.
[[nodiscard]] std::string make_caption_preset(const std::string& name, const text::CaptionOptions& o, bool removeFillers);
[[nodiscard]] std::string make_curve_preset(const std::string& name, Interpolation interp, f32 x1, f32 y1, f32 x2, f32 y2);

} // namespace presets
} // namespace aurea
