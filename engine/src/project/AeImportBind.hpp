// =============================================================================
//  Aurea / project / AeImportBind.hpp   (interno do importador do AE)
//
//  A PONTE entre uma propriedade do After Effects e o modelo do Aurea, usada
//  pelo leitor de .ffx e pelo DOM do .jsx: o mesmo endereço, a mesma
//  conversão de unidade e o mesmo jeito de escrever valor parado, keyframes
//  (com o ease temporal convertido em bézier) e expressão.
//
//  Unidades: o lado AE de uma Binding está nas unidades do SCRIPTING (o que
//  `property.value` devolve: escala e opacidade da camada em %, pontos de
//  efeito em px da camada, cores RGBA 0..1). O .ffx guarda algumas
//  diferentes (escala/opacidade em fração, pontos de efeito relativos ao
//  tamanho da camada); `fileMul` leva do arquivo para o scripting.
// =============================================================================
#pragma once

#include "aurea/animation/Curve.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/project/AeImport.hpp"
#include "aurea/timeline/Layer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aurea {
class EffectRegistry;
class ParameterRegistry;
}

namespace aurea::ae::detail {

/// Onde mora o valor de um componente.
enum class Home : u8 {
    Track,      ///< trilha (prop, ei, pi) e o campo/constante que vale sem keyframe
    TrackOnly,  ///< trilha sem campo (parâmetro só de trilha do animador): parado vira 1 key
    Field,      ///< campo sem trilha (enum do seletor, opção do texto no caminho)
};

enum class Field : u8 {
    None, SelBasedOn, SelShape, SelRandom, SelSeed, PathReverse, PathPerpendicular, PathForceAlign,
    PathMask, Grouping,
};

enum class Conv : u8 {
    Linear,        ///< ae · mul + add
    SrgbToLinear,  ///< cor de efeito (o Aurea guarda linear)
    Bool,          ///< ≠ 0 → 1
    Round,         ///< inteiro
    Enum,          ///< valor de menu do AE (1..) → índice do Aurea pelo mapa (−1 = não existe)
    Reciprocal,    ///< mul / ae (blocos → tamanho da célula)
};

inline constexpr u8 kConstSrc = 0xFF;   ///< componente fixo: o valor é `add`

struct Channel {
    Home home = Home::Track;
    TrackProperty prop = TrackProperty::_Count;
    u32 ei = kInvalidIndex;
    u32 pi = 0;
    Field field = Field::None;
    u8 src = 0;
    Conv conv = Conv::Linear;
    f64 mul = 1.0, add = 0.0;
    f64 lo = -1.0e9, hi = 1.0e9;
    const i8* map = nullptr;
    u8 mapN = 0;
};

struct Binding {
    std::string label;             ///< para o relatório
    std::vector<Channel> ch;
    u32 dims = 1;                  ///< componentes do lado AE
    bool spatial = false;          ///< ease único pelo caminho (posição, âncora, pontos)
    f64 fileMul[4]{1, 1, 1, 1};    ///< arquivo .ffx → unidade do scripting
    u32 animator = kInvalidIndex;  ///< animador de texto (marca as propriedades)
    u32 textProps = 0;             ///< TextAnimProp ligadas quando escrito
    std::string unsupported;       ///< sem equivalente: o motivo (ch vazio)
    std::string approx;            ///< aproximação a anotar quando escrito
    [[nodiscard]] bool ok() const noexcept { return !ch.empty(); }
};

/// Contexto de escrita numa camada.
struct WriteCtx {
    Layer* layer = nullptr;
    f64 fps = 30.0;
    ImportReport* report = nullptr;
    const EffectRegistry* registry = nullptr;
    /// Tempo do AE → quadro LOCAL da camada. `compTime` (jsx): t é o tempo da
    /// composição. Senão (ffx): t0 cai em `localAnchor`.
    bool compTime = false;
    f64 t0 = 0.0;
    i64 localAnchor = 0;
    u32 line = 0;                  ///< linha do script (relatório)
    // Pendências que só se resolvem no fim (máscara do caminho, agrupamento).
    i32 pendingPathMask = -1;      ///< índice 1.. da máscara do AE (0 = nenhuma)
    i32 pendingGrouping = -1;      ///< 0 letra, 1 palavra, 2 linha
    u32 keyframes = 0;
    u32 writes = 0;

    [[nodiscard]] i64 local_frame(f64 t) const noexcept;
};

// --- escrita -------------------------------------------------------------------
/// Valor parado (`v` nas unidades do scripting; `fromFile` aplica fileMul).
void write_static(WriteCtx& w, const Binding& b, const std::vector<f64>& v, bool fromFile);
/// Keyframes (substituem os da trilha). Devolve quantos keys do Aurea saíram.
u32 write_keys(WriteCtx& w, const Binding& b, const std::vector<Key>& keys, bool fromFile);
/// Expressão (já reescrita) em cada componente. Falso = erro de sintaxe
/// (fica gravada mesmo assim, como no Engine::set_expression).
bool write_expression(WriteCtx& w, const Binding& b, const std::string& source);
/// Valor atual (unidades do scripting) no quadro local `frame`.
[[nodiscard]] std::vector<f64> read_value(const Layer& l, const Binding& b, i64 frame);
/// Tira os keyframes / a expressão dos componentes (o valor parado fica).
void clear_keys(WriteCtx& w, const Binding& b);
void clear_expression(WriteCtx& w, const Binding& b);
/// Liga/desliga a expressão já gravada nos componentes.
void set_expression_enabled(WriteCtx& w, const Binding& b, bool enabled);

// --- endereços -------------------------------------------------------------------
/// Transform da camada ("ADBE Position", "ADBE Scale"...). `ffx` = unidades do arquivo.
[[nodiscard]] Binding bind_transform(const Layer& l, std::string_view match);
/// Propriedade do animador de texto `a` ("ADBE Text Position 3D"...).
[[nodiscard]] Binding bind_text_property(const Layer& l, u32 a, std::string_view match);
/// Propriedade de seletor do animador `a`. `kind`: 0 intervalo, 1 wiggly, 2 expressão.
[[nodiscard]] Binding bind_text_selector(const Layer& l, u32 a, u32 kind, std::string_view match, u32 indexUnits);
/// Opções do texto no caminho e "Mais opções".
[[nodiscard]] Binding bind_text_path(const Layer& l, std::string_view match);
[[nodiscard]] Binding bind_text_more(const Layer& l, std::string_view match);
/// Propriedade de máscara (id estável da máscara do Aurea).
[[nodiscard]] Binding bind_mask(const Layer& l, u32 maskId, std::string_view match);

// --- tabela de efeitos (AeImportTable.cpp) --------------------------------------
enum class PConv : u8 {
    Same,          ///< ae · k + b
    Bool,          ///< (ae · k + b) ≠ 0
    Enum,          ///< menu do AE (1..) → índice pelo mapa
    Color,         ///< sRGB → linear
    PointRel,      ///< px da camada → fração do tamanho (ponto do Aurea relativo)
    PointPx,       ///< px da camada
    PointCenter,   ///< px − centro da camada (deslocamentos)
    PointPercent,  ///< px → % do tamanho (centro_x/centro_y do Aurea), componente `comp`
    Seconds,       ///< segundos → quadros (ae · k · fps)
    BlocksW,       ///< blocos → tamanho (largura da camada / ae)
    BlocksH,       ///< (altura da camada / ae)
    PxPercentMax,  ///< px → % do maior lado da camada (suavidade das transições)
    PxPercentMin,  ///< px → % do menor lado
    None,          ///< sem equivalente (relatado se ≠ padrão ou animado)
};

struct PRule {
    u16 ae = 0;                 ///< número do parâmetro (o NNNN do matchName)
    const char* aeName = "";    ///< nome em inglês (scripting: property("Blurriness"))
    const char* id = nullptr;   ///< id do parâmetro do Aurea
    PConv conv = PConv::Same;
    f64 k = 1.0, b = 0.0;
    u8 comp = 0;                ///< componente do AE usado (ponto → dois escalares)
    u8 tcomp = 0xFF;            ///< componente do parâmetro do Aurea (0xFF = natural)
    const i8* map = nullptr;
    u8 mapN = 0;
    f64 def = 0.0;              ///< None: padrão do AE (não relata se igual)
    bool hasDef = false;
    bool silent = false;        ///< None sem efeito visual aqui: nunca relata
};

struct FixedValue { const char* id; f32 v[4]; };

struct PostIn;   ///< valores do AE de um efeito (para os acertos)
using PostFn = void (*)(EffectInstance& fx, const ParameterRegistry& specs, const PostIn& in, ImportReport& rep);

struct ERule {
    const char* ae = "";        ///< matchName do efeito
    const char* aeName = "";    ///< nome em inglês
    const char* key = nullptr;  ///< chave do Aurea (nulo = sem equivalente)
    const PRule* params = nullptr;
    u32 paramCount = 0;
    const char* reason = nullptr;   ///< sem equivalente: por quê; com: aproximação
    const FixedValue* fixed = nullptr;
    u32 fixedCount = 0;
    PostFn post = nullptr;
};

/// Valores parados do AE (unidades do scripting) por número de parâmetro.
struct PostIn {
    struct V { u16 ae; std::vector<f64> v; bool animated; };
    std::vector<V> values;
    f64 layerW = 0, layerH = 0, fps = 30;
    [[nodiscard]] const V* find(u16 ae) const noexcept {
        for (const V& x : values) if (x.ae == ae) return &x;
        return nullptr;
    }
    [[nodiscard]] f64 get(u16 ae, u8 comp, f64 def) const noexcept {
        const V* x = find(ae);
        return x && comp < x->v.size() ? x->v[comp] : def;
    }
};

[[nodiscard]] const ERule* find_effect_rule(std::string_view aeMatch) noexcept;
/// Regra pelo nome exibido em inglês ("Gaussian Blur") — scripting.
[[nodiscard]] const ERule* find_effect_rule_by_name(std::string_view name) noexcept;
/// O parâmetro do AE pela posição na interface (1..), pelo nome em inglês ou
/// pelo matchName ("ADBE Gaussian Blur 2-0001"). 0 = não achou.
[[nodiscard]] u16 effect_param_number(const ERule& r, std::string_view nameOrMatch) noexcept;
[[nodiscard]] u16 effect_param_by_position(const ERule& r, u32 position) noexcept;
/// Endereço do parâmetro `ae` do efeito `effectId` (a Binding reúne as regras
/// que leem esse parâmetro). `dims` = componentes do valor no AE.
[[nodiscard]] Binding bind_effect_param(const ERule& r, u32 effectId, u16 ae, u32 dims, const EffectRegistry& reg,
                                        f64 layerW, f64 layerH, f64 fps);
/// Id do parâmetro do Aurea para o parâmetro `ae` (expressões); nulo = nenhum.
[[nodiscard]] const char* aurea_param_for(const ERule& r, u16 ae) noexcept;
[[nodiscard]] u32 rule_count() noexcept;
[[nodiscard]] const ERule& rule_at(u32 i) noexcept;

// --- utilidades ------------------------------------------------------------------
/// Tamanho da camada em px (fonte, caixa do texto, forma; senão a composição).
void layer_size(const Layer& l, f64 compW, f64 compH, f64& w, f64& h) noexcept;
/// Unidades do texto para o "Based On" do AE (1 caracteres, 2 sem espaços,
/// 3 palavras, 4 linhas).
[[nodiscard]] u32 text_units(const std::string& s, u32 basedOn) noexcept;
/// Reescreve effect("Nome")(param) das expressões para os efeitos do Aurea.
struct EffectAlias {
    std::string name;        ///< nome da instância, nome em inglês ou matchName (sem \t)
    std::string aureaRef;    ///< "aurea.control.slider" ou "aurea.control.slider 2"
    const ERule* rule = nullptr;
};
[[nodiscard]] std::string rewrite_expression(std::string_view src, const std::vector<EffectAlias>& aliases);
[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept;

} // namespace aurea::ae::detail
