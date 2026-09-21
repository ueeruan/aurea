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
};

struct ParamSpec {
    const char* id = "";          ///< chave estável (projeto, UI)
    const char* label = "";       ///< nome na interface
    ParamType   type = ParamType::Float;
    u16         flags = kParamAnimatable;
    ParamValue  defaultValue{};
    f32         minValue = 0.0f;
    f32         maxValue = 1.0f;
    const char* unit = nullptr;   ///< "px", "%", "°"
    u32         enumCount = 0;
    const char* const* enumLabels = nullptr;

    [[nodiscard]] bool animatable() const noexcept {
        return (flags & kParamAnimatable) != 0 && component_count(type) > 0;
    }
};

/// Onde um efeito declara os parâmetros. Construído uma vez por tipo de efeito.
class ParameterRegistry {
public:
    u32 add(const ParamSpec& spec);

    u32 add_float(const char* id, const char* label, f32 def, f32 min, f32 max,
                  u16 flags = kParamAnimatable, const char* unit = nullptr);
    u32 add_int(const char* id, const char* label, i32 def, i32 min, i32 max);
    u32 add_bool(const char* id, const char* label, bool def);
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
    u32 add_texture_ref(const char* id, const char* label);

    [[nodiscard]] u32 count() const noexcept { return static_cast<u32>(specs_.size()); }
    [[nodiscard]] const ParamSpec& at(u32 i) const noexcept { return specs_[i]; }
    [[nodiscard]] u32 find(std::string_view id) const noexcept;

private:
    std::vector<ParamSpec> specs_;
};

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
    std::vector<ParamSlot>    params;
    std::vector<CurveData>    curves;      ///< indexado por ParamValue::ref
    std::vector<GradientData> gradients;
    MaskId       mask{};
};

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

/// Cria os slots de uma instância nova com os padrões da declaração.
void initialize_instance(EffectInstance& instance, const ParameterRegistry& params);

} // namespace aurea
