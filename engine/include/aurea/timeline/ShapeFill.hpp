// =============================================================================
//  Aurea / timeline / ShapeFill.hpp
//
//  Omega 0.3.2, área 05 (aparência), P1-4: o PREENCHIMENTO da forma avaliado
//  no instante — cor sólida animável e degradê (linear, radial, angular).
//
//  As trilhas usam TrackProperty::ShapeParam (effectIndex 0) com índices numa
//  faixa PRÓPRIA, longe dos 1..14 de ShapeSetParam (shape::kParamTotal): quem
//  só conhece os parâmetros da geometria (query_shape_params, o palco, o
//  "congelar a última chave") ignora estes, e um app antigo também (lê a
//  trilha e não a usa). Append-only: os números são contrato de arquivo.
//
//  O renderer (Renderer.cpp, caso Shape) e a consulta da UI
//  (Engine::query_shape_fill) avaliam pelo MESMO `evaluate_shape_fill`.
// =============================================================================
#pragma once

#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::shape_fill {

// Índices de ShapeParam (effectIndex 0). Cor em sRGB reto, 0..1.
inline constexpr u32 kFillR = 32, kFillG = 33, kFillB = 34, kFillA = 35;
inline constexpr u32 kGrad0R = 36;          ///< 36..39 cor do início
inline constexpr u32 kGrad1R = 40;          ///< 40..43 cor do fim
inline constexpr u32 kGradStartX = 44, kGradStartY = 45;
inline constexpr u32 kGradEndX = 46, kGradEndY = 47;
inline constexpr u32 kFirstParam = 32, kEndParam = 48;

/// Grupos animáveis (bits de `Eval::animated`/`Eval::keyHere` e o `which` de
/// Engine::set_shape_fill_value): 0 cor sólida, 1 cor do início, 2 cor do fim,
/// 3 ponta inicial, 4 ponta final.
inline constexpr u32 kGroupColor = 0, kGroupGrad0 = 1, kGroupGrad1 = 2, kGroupStart = 3, kGroupEnd = 4;
inline constexpr u32 kGroupCount = 5;

/// Primeiro índice e número de componentes de cada grupo.
[[nodiscard]] constexpr u32 group_first(u32 group) noexcept {
    return group == kGroupColor ? kFillR : group == kGroupGrad0 ? kGrad0R : group == kGroupGrad1 ? kGrad1R
         : group == kGroupStart ? kGradStartX : kGradEndX;
}
[[nodiscard]] constexpr u32 group_size(u32 group) noexcept { return group <= kGroupGrad1 ? 4u : 2u; }

/// Faixa de cada componente (cor 0..1; pontas −2..3 da caixa: dá para levar a
/// ponta para fora da forma, como na referência).
[[nodiscard]] inline f32 clamp_component(u32 param, f32 v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return param < kGradStartX ? std::clamp(v, 0.0f, 1.0f) : std::clamp(v, -2.0f, 3.0f);
}

/// O valor PARADO (o campo da forma) de um componente.
[[nodiscard]] inline f32 static_component(const ShapeData& s, u32 param) noexcept {
    switch (param) {
        case kFillR: return s.fillColor.x; case kFillG: return s.fillColor.y;
        case kFillB: return s.fillColor.z; case kFillA: return s.fillColor.w;
        case kGrad0R: return s.gradColor0.x; case kGrad0R + 1: return s.gradColor0.y;
        case kGrad0R + 2: return s.gradColor0.z; case kGrad0R + 3: return s.gradColor0.w;
        case kGrad1R: return s.gradColor1.x; case kGrad1R + 1: return s.gradColor1.y;
        case kGrad1R + 2: return s.gradColor1.z; case kGrad1R + 3: return s.gradColor1.w;
        case kGradStartX: return s.gradStart.x; case kGradStartY: return s.gradStart.y;
        case kGradEndX: return s.gradEnd.x; case kGradEndY: return s.gradEnd.y;
        default: return 0.0f;
    }
}

/// Ponteiro para o campo parado de um componente (nulo fora da faixa).
[[nodiscard]] inline f32* static_field(ShapeData& s, u32 param) noexcept {
    switch (param) {
        case kFillR: return &s.fillColor.x; case kFillG: return &s.fillColor.y;
        case kFillB: return &s.fillColor.z; case kFillA: return &s.fillColor.w;
        case kGrad0R: return &s.gradColor0.x; case kGrad0R + 1: return &s.gradColor0.y;
        case kGrad0R + 2: return &s.gradColor0.z; case kGrad0R + 3: return &s.gradColor0.w;
        case kGrad1R: return &s.gradColor1.x; case kGrad1R + 1: return &s.gradColor1.y;
        case kGrad1R + 2: return &s.gradColor1.z; case kGrad1R + 3: return &s.gradColor1.w;
        case kGradStartX: return &s.gradStart.x; case kGradStartY: return &s.gradStart.y;
        case kGradEndX: return &s.gradEnd.x; case kGradEndY: return &s.gradEnd.y;
        default: return nullptr;
    }
}

struct Eval {
    u8   kind = 0;          ///< 0 cor sólida, 1 degradê
    u8   type = 0;          ///< 0 linear, 1 radial, 2 angular
    bool filled = true;
    Vec4 color{};           ///< sRGB reto
    Vec4 color0{}, color1{};
    Vec2 start{}, end{};
    u32  animated = 0;      ///< bit = grupo com keyframe
    u32  keyHere = 0;       ///< bit = grupo com keyframe exatamente em `local`
};

/// Avalia o preenchimento no instante LOCAL da camada (quadro fracionário).
[[nodiscard]] inline Eval evaluate(const Layer& l, f64 local) noexcept {
    const ShapeData& s = l.shape;
    Eval e;
    e.kind = s.fillKind <= 1 ? s.fillKind : 0;
    e.type = s.gradType <= 2 ? s.gradType : 0;
    e.filled = s.filled;
    f32 v[kEndParam - kFirstParam];
    const FrameIndex exact{static_cast<i64>(std::floor(local))};
    const bool integral = std::floor(local) == local;
    for (u32 p = kFirstParam; p < kEndParam; ++p) {
        f32 value = static_component(s, p);
        if (const Track* tr = l.tracks.find(TrackProperty::ShapeParam, 0, p); tr && tr->driven()) {
            value = clamp_component(p, tr->value_or_f(local, value));
            for (u32 g = 0; g < kGroupCount; ++g) {
                if (p < group_first(g) || p >= group_first(g) + group_size(g)) continue;
                if (!tr->keys.empty()) e.animated |= 1u << g;
                if (integral && tr->find_exact(exact) != kInvalidIndex) e.keyHere |= 1u << g;
            }
        }
        v[p - kFirstParam] = value;
    }
    auto at = [&](u32 p) { return v[p - kFirstParam]; };
    e.color = Vec4{at(kFillR), at(kFillG), at(kFillB), at(kFillA)};
    e.color0 = Vec4{at(kGrad0R), at(kGrad0R + 1), at(kGrad0R + 2), at(kGrad0R + 3)};
    e.color1 = Vec4{at(kGrad1R), at(kGrad1R + 1), at(kGrad1R + 2), at(kGrad1R + 3)};
    e.start = Vec2{at(kGradStartX), at(kGradStartY)};
    e.end = Vec2{at(kGradEndX), at(kGradEndY)};
    return e;
}

/// Há o que pintar no preenchimento?
[[nodiscard]] inline bool paints(const Eval& e) noexcept {
    if (!e.filled) return false;
    return e.kind == 1 ? (e.color0.w > 0.0f || e.color1.w > 0.0f) : e.color.w > 0.0f;
}

} // namespace aurea::shape_fill
