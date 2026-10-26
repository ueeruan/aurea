// =============================================================================
//  Aurea / effects / Parameter.hpp
//
//  Parâmetros genéricos de efeito.
//
//  Um efeito DECLARA seus parâmetros (tipo, faixa, padrão) e LÊ valores já
//  resolvidos no instante do frame. Ele nunca sabe de onde o valor veio:
//
//      constante  → o número guardado no slot
//      keyframes  → tracks da layer (um por componente)
//      expressão  → o avaliador de expressões (gancho pronto, ver `ParamSource`)
//
//  É isso que deixa acrescentar keyframe ou expressão a QUALQUER parâmetro sem
//  reescrever efeito nenhum: a resolução mora em `evaluate_param`, uma função
//  só, e o efeito recebe `ParamValue`.
// =============================================================================
#pragma once

#include "aurea/core/Handle.hpp"
#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/effects/MeshWarp.hpp"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <vector>

namespace aurea {

class TrackSet;

enum class ParamType : u8 {
    Float = 0,
    Int,
    Bool,
    Color,             ///< RGBA linear
    Point2D,
    Point3D,
    Angle,             ///< graus
    Enum,
    Curve,             ///< curva de tom (mestra + R, G, B)
    Gradient,
    LayerReference,    ///< outra layer da composição (máscara de luminância...)
    TextureReference,  ///< asset de imagem (LUT, textura de deslocamento...)
};

/// Componentes numéricos animáveis de um tipo. Curva, gradiente e referências
/// não são animados componente a componente (0).
[[nodiscard]] constexpr u32 component_count(ParamType t) noexcept {
    switch (t) {
        case ParamType::Float:
        case ParamType::Int:
        case ParamType::Bool:
        case ParamType::Angle:
        case ParamType::Enum:    return 1;
        case ParamType::Point2D: return 2;
        case ParamType::Point3D: return 3;
        case ParamType::Color:   return 4;
        default:                 return 0;
    }
}

/// Valor resolvido. Numéricos em `v`; referências e índices de dado em `ref`.
struct ParamValue {
    f32 v[4]{0.0f, 0.0f, 0.0f, 0.0f};
    u64 ref = 0;

    [[nodiscard]] static ParamValue scalar(f32 x) noexcept { ParamValue p; p.v[0] = x; return p; }
    [[nodiscard]] static ParamValue vec2(f32 x, f32 y) noexcept { ParamValue p; p.v[0] = x; p.v[1] = y; return p; }
    [[nodiscard]] static ParamValue vec3(f32 x, f32 y, f32 z) noexcept {
        ParamValue p; p.v[0] = x; p.v[1] = y; p.v[2] = z; return p;
    }
    [[nodiscard]] static ParamValue color(f32 r, f32 g, f32 b, f32 a) noexcept {
        ParamValue p; p.v[0] = r; p.v[1] = g; p.v[2] = b; p.v[3] = a; return p;
    }
    [[nodiscard]] static ParamValue boolean(bool b) noexcept { return scalar(b ? 1.0f : 0.0f); }

    [[nodiscard]] f32  as_float() const noexcept { return v[0]; }
    [[nodiscard]] i32  as_int() const noexcept { return static_cast<i32>(v[0] + (v[0] >= 0 ? 0.5f : -0.5f)); }
    [[nodiscard]] bool as_bool() const noexcept { return v[0] >= 0.5f; }
    [[nodiscard]] u32  as_enum() const noexcept { return v[0] <= 0.0f ? 0u : static_cast<u32>(v[0] + 0.5f); }
    [[nodiscard]] Vec2 as_vec2() const noexcept { return Vec2{v[0], v[1]}; }
    [[nodiscard]] Vec3 as_vec3() const noexcept { return Vec3{v[0], v[1], v[2]}; }
    [[nodiscard]] Vec4 as_color() const noexcept { return Vec4{v[0], v[1], v[2], v[3]}; }

    friend bool operator==(const ParamValue&, const ParamValue&) noexcept = default;
};

enum ParamFlags : u16 {
    kParamNone        = 0,
    kParamAnimatable  = 1u << 0,
    /// Comprimento em pixels da layer (raio, deslocamento). O motor escala pela
    /// resolução de trabalho — é o que faz o preview em 1/4 parecer o export.
    kParamPixels      = 1u << 1,
    /// Mostrado em % (o valor guardado continua na unidade do efeito).
    kParamPercent     = 1u << 2,
    /// Fração do tamanho da layer (0..1), como o centro do Motion Tile.
    kParamRelative    = 1u << 3,
    kParamHidden      = 1u << 4,
    /// Slot ausente numa instância SALVA (projeto, preset) vale 0, não o
    /// padrão declarado. É o "algoritmo" dos efeitos que ganharam um desenho
    /// novo: a instância nova nasce com o padrão (1); a antiga, que não tinha
    /// o slot, continua no desenho de antes.
    kParamLegacyZero  = 1u << 5,
    /// Duração ou contagem em QUADROS da composição (atraso, intervalo). Ao
    /// trocar o fps mantendo os segundos (`Composition::retime`), o valor é
    /// multiplicado pela razão — senão 3 quadros a 30 viram metade do tempo a 60.
    kParamFrames      = 1u << 6,
    /// Taxa POR QUADRO (px/quadro). No retime, dividida pela razão.
    kParamPerFrame    = 1u << 7,
    /// 0.3.1: referência de camada que só aceita câmera / nulo / luz (nenhum
    /// destes = qualquer camada). As telas filtram a lista; o motor recusa o resto.
    kParamRefCamera   = 1u << 8,
    kParamRefNull     = 1u << 9,
    kParamRefLight    = 1u << 10,
};
inline constexpr u16 kParamRefKindMask = kParamRefCamera | kParamRefNull | kParamRefLight;

/// Marca de "faixa digitada não declarada": vale a faixa do slider.
inline constexpr f32 kParamSameAsSlider = std::numeric_limits<f32>::quiet_NaN();

// Omega efeitos (0.3.2, área 07, P0-1): APRESENTAÇÃO do parâmetro no painel.
// Só como a linha aparece e responde (controle, passo da régua, origem do
// slider, estilo da escolha, habilitação cruzada). Nada disso entra no projeto
// salvo: índice, valor e trilhas continuam os de sempre.

/// O controle da linha. `Auto` = regra do motor (`resolve_control`).
enum class ParamControl : u8 {
    Auto = 0,
    Slider,        ///< trilho com polegar, valor absoluto na posição do dedo
    Ruler,         ///< régua de riscos: 1 risco = 1 `step`
    Point,         ///< Point2D: régua + caixas X/Y
    Xyz,           ///< Point3D: régua + caixas X/Y/Z
    Orientation,   ///< Point3D em graus (α/β/γ): régua + trackball no preview
    HueDisc,       ///< Point2D matiz/força num disco (agrupa até 4)
};

/// Como uma escolha (Enum) aparece. `Auto` = regra do motor (`resolve_enum_style`).
enum class EnumStyle : u8 { Auto = 0, Segmented, Dropdown };

/// Bits de `EffectParamRow::rowFlags` (ponte).
inline constexpr u32 kParamRowEnabled = 1u << 0;   ///< editável agora (habilitação cruzada)
inline constexpr u32 kParamRowHueDisc = 1u << 1;   ///< desenhar como disco de matiz agrupável

struct ParamSpec {
    const char* id = "";          ///< chave estável (projeto, UI)
    const char* label = "";       ///< nome na interface
    ParamType   type = ParamType::Float;
    u16         flags = kParamAnimatable;
    ParamValue  defaultValue{};
    /// Faixa do SLIDER (a régua): o curso confortável do dedo.
    f32         minValue = 0.0f;
    f32         maxValue = 1.0f;
    /// Faixa DIGITADA (teclado numérico) — o contrato do efeito. É ela que
    /// `evaluate_param` impõe; o slider é só um atalho dentro dela. Sempre
    /// contém a faixa do slider. NaN (padrão) = igual à do slider; o registro
    /// grava o valor explícito ao declarar (`ParameterRegistry::add`).
    f32         hardMin = kParamSameAsSlider;
    f32         hardMax = kParamSameAsSlider;
    const char* unit = nullptr;   ///< "px", "%", "°"
    u32         enumCount = 0;
    const char* const* enumLabels = nullptr;
    /// SEÇÃO do painel a que o parâmetro pertence: ordinal 1..N na tabela de
    /// seções do efeito (`ParameterRegistry::section`), 0 = fora de seção.
    /// Só apresentação: o índice do parâmetro (a chave no projeto) não muda.
    u16         section = 0;
    /// DICA só de leitura (ⓘ + texto) mostrada logo ACIMA da linha do
    /// parâmetro; nula = sem dica. Texto do motor (pt-BR) — a UI traduz pela
    /// identidade (chave do efeito + índice), como o rótulo.
    const char* tip = nullptr;
    // Omega efeitos (P0-1): apresentação; padrões compatíveis (= regra do motor).
    /// Controle da linha (Auto = `resolve_control`).
    ParamControl control = ParamControl::Auto;
    /// Estilo da escolha (Auto = `resolve_enum_style`).
    EnumStyle   enumStyle = EnumStyle::Auto;
    /// Valor por risco da régua (NaN = `resolve_step`).
    f32         step = kParamSameAsSlider;
    /// Origem do preenchimento do slider (NaN = `resolve_origin`).
    f32         origin = kParamSameAsSlider;
    /// Habilitação cruzada: o parâmetro só fica editável quando o Bool
    /// `enabledBy` está ligado (bit 0 de `enabledMask`; bit 1 = desligado) ou
    /// quando o Enum `enabledBy` está numa opção cujo bit está na máscara.
    /// kInvalidIndex = sempre habilitado (salvo o interruptor da seção).
    u32         enabledBy = kInvalidIndex;
    u32         enabledMask = 0;

    [[nodiscard]] bool animatable() const noexcept {
        return (flags & kParamAnimatable) != 0 && component_count(type) > 0;
    }
    /// Limites digitados resolvidos (nunca mais estreitos que o slider).
    [[nodiscard]] f32 typed_min() const noexcept {
        return !std::isnan(hardMin) && hardMin < minValue ? hardMin : minValue;
    }
    [[nodiscard]] f32 typed_max() const noexcept {
        return !std::isnan(hardMax) && hardMax > maxValue ? hardMax : maxValue;
    }
};

/// Uma SEÇÃO do painel de controles: título recolhível que agrupa parâmetros
/// já declarados (os índices continuam os de sempre — seção é só como o painel
/// mostra, nunca muda o projeto salvo). `toggle` é o parâmetro Bool que LIGA a
/// seção (mostrado como interruptor no cabeçalho, não como linha); kInvalidIndex
/// = seção sem interruptor. `open` é como a seção nasce (aberta/recolhida).
struct ParamSection {
    const char* id = "";        ///< chave estável ("glow"): estado e tradução
    const char* label = "";     ///< título (texto do motor, pt-BR)
    u32         toggle = kInvalidIndex;
    bool        open = true;
    /// Omega efeitos (P0-1): "edite primeiro". Seção declarada recolhida que
    /// NASCE aberta numa instância nova (todos os valores no padrão). O estado
    /// inicial vai para `sectionsFlipped` na criação (`initialize_instance`),
    /// sem mudar o formato; depois disso a pessoa manda.
    bool        editFirst = false;
};

/// Máximo de seções por efeito: o estado aberto/recolhido de cada uma é
/// gravado em 7 bits do byte `expanded` da instância (ver `EffectInstance`).
inline constexpr u32 kMaxParamSections = 7;

/// Onde um efeito declara os parâmetros. Construído uma vez por tipo de efeito.
class ParameterRegistry {
public:
    u32 add(const ParamSpec& spec);

    u32 add_float(const char* id, const char* label, f32 def, f32 min, f32 max,
                  u16 flags = kParamAnimatable, const char* unit = nullptr);
    u32 add_int(const char* id, const char* label, i32 def, i32 min, i32 max, u16 flags = kParamAnimatable);
    u32 add_bool(const char* id, const char* label, bool def, u16 flags = kParamAnimatable);
    u32 add_color(const char* id, const char* label, Vec4 def);
    u32 add_point2(const char* id, const char* label, Vec2 def, f32 min, f32 max,
                   u16 flags = kParamAnimatable);
    u32 add_point3(const char* id, const char* label, Vec3 def, f32 min, f32 max);
    u32 add_angle(const char* id, const char* label, f32 defDegrees,
                  f32 min = -36000.0f, f32 max = 36000.0f);
    u32 add_enum(const char* id, const char* label, const char* const* labels, u32 count, u32 def);
    u32 add_curve(const char* id, const char* label);
    u32 add_gradient(const char* id, const char* label);
    u32 add_layer_ref(const char* id, const char* label);
    /// `accept` = kParamRef* (0 = qualquer camada).
    u32 add_layer_ref(const char* id, const char* label, u16 accept);
    u32 add_texture_ref(const char* id, const char* label);

    /// Alarga a faixa DIGITADA do parâmetro `index` (o slider não muda). Serve
    /// para a edição extrema (AMV): digitar um raio de 2000 px com a régua
    /// indo só até 500. Só números contínuos (Float, Angle, Point2D/3D): contagem,
    /// iteração, amostras, qualidade, enum e bool ficam com a faixa do slider —
    /// é o que limita memória e custo por pixel. Regra dos tetos: ~5-10x o
    /// curso do slider, ou o limite físico/da implementação (pirâmide do blur,
    /// meia precisão, laço fixo do shader) quando ele vem antes — anotado no
    /// efeito. Nunca estreita: o resultado sempre contém a faixa do slider.
    /// Devolve `index` para encadear.
    u32 set_typed_range(u32 index, f32 hardMin, f32 hardMax) noexcept;
    /// Atalho: alarga a faixa digitada do ÚLTIMO parâmetro declarado.
    u32 typed_range(f32 hardMin, f32 hardMax) noexcept {
        return specs_.empty() ? kInvalidIndex : set_typed_range(count() - 1, hardMin, hardMax);
    }

    /// Abre uma SEÇÃO com os parâmetros `members`, JÁ declarados (efeito antigo
    /// ganha seções sem mexer em índice nenhum; a seção mostra os membros na
    /// ordem dos índices). `toggle` (opcional) é o Bool que liga a seção: ele
    /// sai das linhas e vira o interruptor do cabeçalho. Membro inexistente ou
    /// já em outra seção, `toggle` que não é Bool ou mais de
    /// `kMaxParamSections` seções: recusado (kInvalidIndex, nada muda).
    /// Devolve o ordinal da seção (1..N).
    u32 add_section(const char* id, const char* label, std::initializer_list<u32> members,
                    u32 toggle = kInvalidIndex, bool open = true) noexcept;
    /// Atalho: seção com os parâmetros [first, last].
    u32 add_section_range(const char* id, const char* label, u32 first, u32 last,
                          u32 toggle = kInvalidIndex, bool open = true) noexcept;
    /// Põe uma DICA (ⓘ, só leitura) acima do parâmetro `index`. Devolve `index`.
    u32 set_tip(u32 index, const char* text) noexcept;

    // --- Omega efeitos (P0-1): apresentação, efeito a efeito -------------------
    // Todos devolvem `index` (kInvalidIndex se não existe) para encadear e nunca
    // mudam índice, valor nem faixa: só como a linha aparece.
    /// Controle da linha. Recusa combinação sem sentido (Point num Float...).
    u32 set_control(u32 index, ParamControl control) noexcept {
        if (index >= specs_.size()) return kInvalidIndex;
        ParamSpec& s = specs_[index];
        const bool scalar = s.type == ParamType::Float || s.type == ParamType::Int || s.type == ParamType::Angle;
        const bool ok = control == ParamControl::Auto ||
            ((control == ParamControl::Slider || control == ParamControl::Ruler) && scalar) ||
            ((control == ParamControl::Point || control == ParamControl::HueDisc) && s.type == ParamType::Point2D) ||
            ((control == ParamControl::Xyz || control == ParamControl::Orientation) && s.type == ParamType::Point3D);
        if (ok) s.control = control;
        return index;
    }
    /// Valor por risco da régua (> 0 e finito; senão volta à regra).
    u32 set_step(u32 index, f32 step) noexcept {
        if (index >= specs_.size()) return kInvalidIndex;
        specs_[index].step = std::isfinite(step) && step > 0.0f ? step : kParamSameAsSlider;
        return index;
    }
    /// Origem do preenchimento do slider (finita; senão volta à regra).
    u32 set_origin(u32 index, f32 origin) noexcept {
        if (index >= specs_.size()) return kInvalidIndex;
        specs_[index].origin = std::isfinite(origin) ? origin : kParamSameAsSlider;
        return index;
    }
    /// Estilo da escolha (só Enum).
    u32 set_enum_style(u32 index, EnumStyle style) noexcept {
        if (index >= specs_.size()) return kInvalidIndex;
        if (specs_[index].type == ParamType::Enum) specs_[index].enumStyle = style;
        return index;
    }
    /// Habilitação cruzada: `index` só fica editável com o Bool `controller`
    /// ligado (mask 1; 2 = desligado) ou com o Enum `controller` numa opção do
    /// `mask` (bit k = opção k). Controlador que não é Bool/Enum, o próprio
    /// parâmetro ou inexistente: recusado (nada muda).
    u32 set_enabled_by(u32 index, u32 controller, u32 mask) noexcept {
        if (index >= specs_.size()) return kInvalidIndex;
        if (controller >= specs_.size() || controller == index || mask == 0) return index;
        const ParamType t = specs_[controller].type;
        if (t != ParamType::Bool && t != ParamType::Enum) return index;
        specs_[index].enabledBy = controller;
        specs_[index].enabledMask = mask;
        return index;
    }
    /// Marca a seção `ordinal` (1-based) como "edite primeiro".
    u32 set_section_edit_first(u32 ordinal, bool editFirst = true) noexcept {
        if (ordinal < 1 || ordinal > sections_.size()) return kInvalidIndex;
        sections_[ordinal - 1].editFirst = editFirst;
        return ordinal;
    }

    [[nodiscard]] u32 count() const noexcept { return static_cast<u32>(specs_.size()); }
    [[nodiscard]] const ParamSpec& at(u32 i) const noexcept { return specs_[i]; }
    [[nodiscard]] u32 find(std::string_view id) const noexcept;
    /// Seções declaradas (ordinal 1..section_count()).
    [[nodiscard]] u32 section_count() const noexcept { return static_cast<u32>(sections_.size()); }
    /// Seção de ordinal `ordinal` (1-based); nula fora da faixa.
    [[nodiscard]] const ParamSection* section(u32 ordinal) const noexcept {
        return ordinal >= 1 && ordinal <= sections_.size() ? &sections_[ordinal - 1] : nullptr;
    }

private:
    u32 add_section_of(const char* id, const char* label, const u32* members, u32 count, u32 toggle,
                       bool open) noexcept;

    std::vector<ParamSpec> specs_;
    std::vector<ParamSection> sections_;
};

/// A seção `ordinal` (1-based) da instância está ABERTA? O declarado
/// (`ParamSection::open`) trocado pelo bit da instância (`sectionsFlipped`).
[[nodiscard]] inline bool section_open(const ParamSection& s, u8 flipped, u32 ordinal) noexcept {
    const bool flip = ordinal >= 1 && ordinal <= kMaxParamSections && ((flipped >> (ordinal - 1)) & 1u) != 0;
    return s.open != flip;
}

// -----------------------------------------------------------------------------
// Dado de instância
// -----------------------------------------------------------------------------

/// Origem do valor de um parâmetro. `Keyframes` é derivado (existe track para
/// ele); `Expression` é escolhido pelo usuário e tem precedência.
enum class ParamSource : u8 { Constant = 0, Keyframes, Expression };

struct ParamSlot {
    ParamValue  constant{};
    /// Expressão do projeto (índice), quando `source == Expression`. O
    /// avaliador de expressões ainda não existe: com ele ausente, o valor
    /// constante vale — e `evaluate_param` sinaliza isso.
    u32         expression = kInvalidIndex;
    ParamSource source = ParamSource::Constant;
};

/// Curva de tom. Canal 0 = mestra (aplicada a R, G e B), 1..3 = R, G, B.
/// Pontos em [0,1] x [0,1], ordenados por x.
struct CurveData {
    struct Point { f32 x = 0.0f; f32 y = 0.0f; };
    std::vector<Point> channel[4];

    [[nodiscard]] static CurveData identity();
    [[nodiscard]] bool is_identity() const noexcept;
    /// Avalia um canal por spline monotônica (Fritsch–Carlson): passa por todos
    /// os pontos e NUNCA ultrapassa entre dois deles — uma curva que "faz onda"
    /// entre pontos produz banda de cor em degradê.
    [[nodiscard]] f32 evaluate(u32 channel, f32 x) const noexcept;
    [[nodiscard]] u64 hash() const noexcept;
};

struct GradientStop { f32 position = 0.0f; Vec4 color{}; };
struct GradientData { std::vector<GradientStop> stops; };

using EffectTypeId = u32;

/// FNV-1a 32 da chave estável do efeito ("aurea.blur.gaussian"). O id é
/// gravado no projeto: trocar o NOME exibido não quebra projeto antigo, trocar a
/// CHAVE quebra — por isso a chave nunca muda.
[[nodiscard]] constexpr EffectTypeId effect_type_id(std::string_view key) noexcept {
    u32 h = 2166136261u;
    for (char c : key) {
        h ^= static_cast<u8>(c);
        h *= 16777619u;
    }
    return h;
}

/// Uma instância de efeito numa layer.
struct EffectInstance {
    /// Id local à layer, estável: é a chave dos keyframes deste efeito (a
    /// posição no vetor muda ao reordenar; o id não).
    u32          id = kInvalidIndex;
    EffectTypeId type = 0;
    bool         enabled = true;
    bool         expanded = true;          ///< estado do painel (persistido)
    /// Seções do painel cujo estado (aberta/recolhida) foi TROCADO em relação
    /// ao declarado: bit (ordinal − 1). Persistido sem mudar o formato: vai
    /// nos bits 1..7 do mesmo byte de `expanded` — leitor antigo lê o byte
    /// como bool (≠ 0) e não usa; projeto antigo traz 0 (tudo como declarado).
    u8           sectionsFlipped = 0;
    // Omega aparência (P0-4): DECORAÇÃO da camada (Borda e sombra). 0 = efeito
    // comum (aparece na lista de Efeitos); os outros são efeitos existentes com
    // papel fixo, escondidos da lista e sempre DEPOIS dos comuns, na ordem
    // traço → bordas → sombra (kEffectRole*). Gravado na timeline v51.
    u8           role = 0;
    std::vector<ParamSlot>    params;
    std::vector<CurveData>    curves;      ///< indexado por ParamValue::ref
    std::vector<GradientData> gradients;
    MaskId       mask{};
    /// Malha de deformação (só aurea.distort.mesh_warp; vazio nos outros).
    std::vector<MeshWarpData> meshes;
};

// Omega aparência (P0-4): papéis de `EffectInstance::role`. A ordem numérica é
// a ordem de desenho depois dos efeitos comuns.
inline constexpr u8 kEffectRoleNone = 0;     ///< efeito comum
inline constexpr u8 kEffectRoleStroke = 1;   ///< traço (reservado: forma/texto usam o traço nativo)
inline constexpr u8 kEffectRoleBorder = 2;   ///< borda (aurea.stylize.stroke_outline)
inline constexpr u8 kEffectRoleShadow = 3;   ///< sombra (aurea.stylize.drop_shadow)
inline constexpr u8 kLastEffectRole = kEffectRoleShadow;

/// Chave do track de um componente de parâmetro. O track mora no `TrackSet`
/// da layer com `TrackProperty::EffectParam`, `effectIndex = id do efeito` e
/// `effectParamIndex` = isto.
[[nodiscard]] constexpr u32 param_track_key(u32 paramIndex, u32 component) noexcept {
    return paramIndex * 4u + component;
}

/// Resolve o valor de um parâmetro no instante `localTime` (tempo da layer).
/// `usedFallback` é marcado quando havia uma expressão e ela não pôde ser
/// avaliada (o valor constante foi usado no lugar).
[[nodiscard]] ParamValue evaluate_param(const TrackSet& tracks, const EffectInstance& effect,
                                        u32 paramIndex, const ParamSpec& spec,
                                        FrameIndex localTime, bool* usedFallback = nullptr) noexcept;
[[nodiscard]] ParamValue evaluate_param_f(const TrackSet& tracks, const EffectInstance& effect,
                                          u32 paramIndex, const ParamSpec& spec,
                                          f64 localTime, bool* usedFallback = nullptr) noexcept;

/// Cria os slots de uma instância nova com os padrões da declaração.
void initialize_instance(EffectInstance& instance, const ParameterRegistry& params);

// -----------------------------------------------------------------------------
// Omega efeitos (P0-1): regras de apresentação (EngineOmegaEffects.cpp). Puras,
// testadas no host; as duas interfaces leem o resultado pela ponte.
// -----------------------------------------------------------------------------

/// O controle que a linha usa. Declarado vence; senão: Angle → Ruler;
/// Point2D → Point; Point3D → Xyz; Float/Int com faixa de slider finita e faixa
/// digitada até 4× a do slider → Slider; o resto → Ruler. Tipos sem controle
/// numérico (Bool, Enum, cor...) devolvem Auto.
[[nodiscard]] ParamControl resolve_control(const ParamSpec& spec) noexcept;
/// Valor por risco da régua. Declarado vence; senão Int = 1, Angle = 1°,
/// Float/ponto = (max − min)/200 arredondado para 1·10ⁿ, 2·10ⁿ ou 5·10ⁿ.
[[nodiscard]] f32 resolve_step(const ParamSpec& spec) noexcept;
/// Origem do preenchimento do slider: declarada; senão 0 quando a faixa cruza o
/// zero (slider bipolar), senão o mínimo.
[[nodiscard]] f32 resolve_origin(const ParamSpec& spec) noexcept;
/// Estilo da escolha: declarado; senão Segmented com até 4 opções de até 10
/// caracteres (UTF-8 contado por letra), senão Dropdown.
[[nodiscard]] EnumStyle resolve_enum_style(const ParamSpec& spec) noexcept;
/// O parâmetro `index` está editável agora? Habilitação cruzada (`enabledBy`)
/// e interruptor da seção (os membros dependem dele), recursivo (o controlador
/// também precisa estar habilitado) e protegido contra ciclo. `tracks` nulo =
/// só os valores constantes.
[[nodiscard]] bool param_enabled(const EffectInstance& effect, const ParameterRegistry& params, u32 index,
                                 const TrackSet* tracks, FrameIndex localTime) noexcept;
/// Os parâmetros da instância estão todos no padrão da declaração (constante,
/// sem trilha com keyframe)? É o que decide a seção "edite primeiro".
[[nodiscard]] bool effect_at_defaults(const EffectInstance& effect, const ParameterRegistry& params,
                                      const TrackSet* tracks) noexcept;
/// Estado inicial das seções "edite primeiro" de uma instância nova: liga o
/// bit de troca das seções recolhidas marcadas `editFirst`.
void apply_edit_first_sections(EffectInstance& instance, const ParameterRegistry& params) noexcept;

} // namespace aurea
