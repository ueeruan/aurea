// =============================================================================
//  Aurea / text / TextAnimatorPro.cpp
//
//  0.4.6 · Text Animator Pro. O animador completo de texto, especificado a
//  partir do animador do After Effects (identificadores levantados com o REA
//  em build/reference/; nenhum código, shader ou asset do AE aqui):
//
//  - Seletor de intervalo: Início/Fim/Deslocamento em % ou ÍNDICE; baseado em
//    letras, letras sem espaço, palavras ou linhas; forma (quadrada com
//    SUAVIDADE, rampas, triângulo, redonda, suave); suavizar alto/baixo;
//    ordem aleatória; intensidade; MODO de combinação.
//  - Seletor de oscilação (opcional, combinado pelo próprio modo): intensidade
//    mín./máx., oscilações por segundo, CORRELAÇÃO entre as unidades, fase
//    temporal e espacial, travar dimensões, semente.
//  - Propriedades: ponto de âncora, posição 3D, escala, inclinação + eixo,
//    rotação 3D, opacidade, cor/matiz/saturação/brilho/opacidade do
//    preenchimento, cor e largura do contorno, espaçamento entre letras e
//    entre LINHAS, desfoque; agrupamento (letra/palavra/linha/tudo) com
//    alinhamento.
// =============================================================================
#include "aurea/text/TextAnimator.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aurea::text {

void declare_animator_pro_params(ParameterRegistry& p) {
    constexpr u16 pct = kParamAnimatable | kParamPercent;
    // --- seletor de intervalo
    static const char* units[] = {"Porcentagem", "Índice"};
    p.add_enum("units", "Unidades", units, 2, 0);
    static const char* based[] = {"Letras", "Letras sem espaços", "Palavras", "Linhas"};
    p.add_enum("based_on", "Baseado em", based, 4, 0);
    p.add_float("start", "Início", 0, -1000, 1000, kParamAnimatable);
    p.add_float("end", "Fim", 100, -1000, 1000, kParamAnimatable);
    p.add_float("offset", "Deslocamento", 0, -1000, 1000, kParamAnimatable);
    static const char* modes[] = {"Somar", "Subtrair", "Interseção", "Mínimo", "Máximo", "Diferença"};
    p.add_enum("mode", "Modo", modes, 6, 0);
    p.add_float("amount", "Intensidade", 100, -100, 100, pct, "%");
    static const char* shapes[] = {"Quadrada", "Rampa crescente", "Rampa decrescente", "Triângulo", "Redonda", "Suave"};
    p.add_enum("shape", "Forma", shapes, 6, 0);
    p.add_float("smoothness", "Suavidade", 100, 0, 100, pct, "%");
    p.add_float("ease_high", "Suavizar alto", 0, -100, 100, pct, "%");
    p.add_float("ease_low", "Suavizar baixo", 0, -100, 100, pct, "%");
    p.add_bool("random_order", "Ordem aleatória", false);
    p.add_int("seed", "Semente aleatória", 0, 0, 9999);
    // --- seletor de oscilação
    p.add_bool("wiggly", "Seletor de oscilação", false);
    p.add_enum("wiggly_mode", "Modo da oscilação", modes, 6, 2);
    p.add_float("wiggly_max", "Intensidade máxima", 100, -100, 100, pct, "%");
    p.add_float("wiggly_min", "Intensidade mínima", -100, -100, 100, pct, "%");
    p.add_float("wiggly_rate", "Oscilações por segundo", 2, 0, 30, kParamAnimatable, "/s");
    p.add_float("correlation", "Correlação", 50, 0, 100, pct, "%");
    p.add_angle("temporal_phase", "Fase temporal", 0, -3600, 3600);
    p.add_angle("spatial_phase", "Fase espacial", 0, -3600, 3600);
    p.add_bool("lock_dimensions", "Travar dimensões", false);
    p.add_int("wiggly_seed", "Semente da oscilação", 0, 0, 9999);
    // --- propriedades
    p.add_point2("anchor", "Ponto de âncora", {}, -2000, 2000);
    p.add_point3("position", "Posição", {}, -4000, 4000);
    p.add_point2("scale", "Escala", {100, 100}, -1000, 1000, pct);
    p.add_angle("skew", "Inclinação", 0, -80, 80);
    p.add_angle("skew_axis", "Eixo da inclinação", 0, -360, 360);
    p.add_point3("rotation", "Rotação", {}, -3600, 3600);
    p.add_float("opacity", "Opacidade", 100, 0, 100, pct, "%");
    p.add_bool("fill_enabled", "Cor do preenchimento", false);
    p.add_color("fill_color", "Cor", {1, 0.8f, 0.2f, 1});
    p.add_angle("fill_hue", "Matiz do preenchimento", 0, -3600, 3600);
    p.add_float("fill_saturation", "Saturação do preenchimento", 0, -100, 100, pct, "%");
    p.add_float("fill_brightness", "Brilho do preenchimento", 0, -100, 100, pct, "%");
    p.add_float("fill_opacity", "Opacidade do preenchimento", 100, 0, 100, pct, "%");
    p.add_bool("stroke_enabled", "Cor do contorno", false);
    p.add_color("stroke_color", "Cor do contorno", {0, 0, 0, 1});
    p.add_float("stroke_width", "Largura do contorno", 0, 0, 60, kParamAnimatable | kParamPixels, "px");
    p.add_float("tracking", "Espaçamento entre letras", 0, -400, 400, kParamAnimatable | kParamPixels, "px");
    p.add_point2("line_spacing", "Espaçamento entre linhas", {}, -2000, 2000);
    p.add_float("blur", "Desfoque", 0, 0, 200, kParamAnimatable | kParamPixels, "px");
    static const char* grouping[] = {"Letra", "Palavra", "Linha", "Tudo"};
    p.add_enum("grouping", "Agrupamento da âncora", grouping, 4, 0);
    p.add_point2("group_align", "Alinhamento do agrupamento", {}, -100, 100, pct);
    // --- seletor de expressão (no fim: os índices salvos não mudam)
    p.add_bool("expr_selector", "Seletor de expressão", false);
    p.add_point3("expr_amount", "Intensidade da expressão", {100, 100, 100}, -1000, 1000);
}

bool is_animator_effect(u32 type) noexcept {
    return type == effect_type_id(kAnimatorEffect) || type == effect_type_id(kAnimatorProEffect);
}

f32 combine_selector(f32 above, f32 value, u32 mode, bool aeMath) noexcept {
    switch (mode) {
        // Medido: valor negativo também inverte o que vem de cima.
        case 1: return aeMath ? above * ((value < 0.0f ? -1.0f : 1.0f) - value) : above - value;   // subtrair
        case 2: return above * value;                    // interseção
        case 3: return std::min(above, value);           // mínimo
        case 4: return std::max(above, value);           // máximo
        case 5: return std::fabs(above - value);         // diferença
        default: return above + value;                   // somar
    }
}

namespace {
u32 mix(u32 a, u32 b, u32 c) {
    u32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
f32 noise_value(u32 a, u32 b, i64 k) { return static_cast<f32>(mix(a, b, static_cast<u32>(k)) & 0xFFFFFFu) / 8388607.5f - 1.0f; }
/// Ruído 1D suave (valor, interpolado com smoothstep), −1..1.
f32 smooth_noise(u32 seed, u32 lane, f64 t) {
    const f64 k = std::floor(t);
    const f32 u = static_cast<f32>(t - k), s = u * u * (3.0f - 2.0f * u);
    const f32 a = noise_value(seed, lane, static_cast<i64>(k)), b = noise_value(seed, lane, static_cast<i64>(k) + 1);
    return a + (b - a) * s;
}
f32 shape_at(u32 shape, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (shape) {
        case 1: return t;
        case 2: return 1.0f - t;
        case 3: return 1.0f - std::fabs(2.0f * t - 1.0f);
        case 4: { const f32 x = 2.0f * t - 1.0f; return std::sqrt(std::max(0.0f, 1.0f - x * x)); }
        case 5: return 0.5f - 0.5f * std::cos(t * 6.28318530718f);
        default: return 1.0f;
    }
}
/// Suavizar alto/baixo: >0 segura perto do extremo, <0 acelera nele.
f32 ease(f32 w, f32 high, f32 low) {
    if (high != 0.0f) { const f32 k = std::clamp(high, -1.0f, 1.0f); const f32 e = 1.0f - (1.0f - w) * (1.0f - w); w += k * ((k > 0 ? e : w * w) - w); }
    if (low != 0.0f) { const f32 k = std::clamp(low, -1.0f, 1.0f); const f32 e = w * w; w += k * ((k > 0 ? e : 1.0f - (1.0f - w) * (1.0f - w)) - w); }
    return std::clamp(w, 0.0f, 1.0f);
}
f32 bezier_axis(f32 u, f32 a, f32 b) {
    const f32 v = 1.0f - u;
    return u * (3.0f * v * (v * a + u * b) + u * u);
}
/// y(x) da cúbica de (0,0) a (1,1). x é monótono; a bisseção aguenta tangente vertical.
f32 bezier_y_at_x(f32 x, f32 x1, f32 y1, f32 x2, f32 y2) {
    f32 lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 28; ++i) {
        const f32 u = 0.5f * (lo + hi);
        (bezier_axis(u, x1, x2) < x ? lo : hi) = u;
    }
    return bezier_axis(0.5f * (lo + hi), y1, y2);
}
/// As formas e o ease medidos no After Effects (docs/parity/text-animator-universal.md).
f32 ae_shape_at(u32 shape, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (shape == 4) return std::sqrt(std::max(0.0f, std::sin(t * 3.14159265359f)));
    if (shape == 5) return bezier_y_at_x(1.0f - std::fabs(2.0f * t - 1.0f), 0.5f, 0.0f, 0.5f, 1.0f);
    return shape_at(shape, t);
}
f32 ae_ease(f32 w, f32 high, f32 low) {
    if (high == 0.0f && low == 0.0f) return std::clamp(w, 0.0f, 1.0f);
    const f32 L = std::clamp(low, -1.0f, 1.0f), H = std::clamp(high, -1.0f, 1.0f);
    return std::clamp(bezier_y_at_x(std::clamp(w, 0.0f, 1.0f), L >= 0.0f ? L : 0.0f, L >= 0.0f ? 0.0f : -L,
                                    H >= 0.0f ? 1.0f - H : 1.0f, H >= 0.0f ? 1.0f : 1.0f + H), 0.0f, 1.0f);
}
} // namespace

f32 pro_range_weight(f32 start, f32 end, f32 offset, u32 units, u32 shape, f32 smoothness,
                     f32 easeHigh, f32 easeLow, u32 unit, u32 count, bool aeMath) noexcept {
    const f32 n = static_cast<f32>(std::max<u32>(1, count));
    // Índice: as unidades viram % do total (Início 2 = depois da 2a unidade).
    const f32 k = units == 1 ? 100.0f / n : 1.0f;
    f32 st = (start + offset) * k, en = (end + offset) * k;
    if (st > en) std::swap(st, en);
    const f32 u0 = static_cast<f32>(unit) / n * 100.0f, u1 = static_cast<f32>(unit + 1) / n * 100.0f;
    f32 w = 0.0f;
    if (shape == 0) {
        // Quadrada: a fração da unidade dentro do intervalo. Suavidade 0 = corte
        // seco no meio da unidade; 100 % = a entrada gradual pela fração.
        const f32 cover = std::max(0.0f, std::min(u1, en) - std::max(u0, st)) / (u1 - u0);
        const f32 s = std::clamp(smoothness, 0.0f, 1.0f);
        w = s <= 1e-4f ? (cover >= 0.5f ? 1.0f : 0.0f) : std::clamp((cover - 0.5f) / s + 0.5f, 0.0f, 1.0f);
    } else if (en > st) {
        const f32 c = (u0 + u1) * 0.5f;
        // Rampas continuam saturadas fora do intervalo (antes do início = 0 na
        // crescente, depois do fim = 1), como no animador de referência.
        if (shape == 1) w = c <= st ? 0.0f : c >= en ? 1.0f : shape_at(1, (c - st) / (en - st));
        else if (shape == 2) w = c <= st ? 1.0f : c >= en ? 0.0f : shape_at(2, (c - st) / (en - st));
        else if (c >= st && c <= en) w = aeMath ? ae_shape_at(shape, (c - st) / (en - st)) : shape_at(shape, (c - st) / (en - st));
    }
    return aeMath ? ae_ease(w, easeHigh, easeLow) : ease(w, easeHigh, easeLow);
}

f32 pro_wiggle(u32 seed, u32 unit, u32 dimension, f64 timeSec, f32 rate, f32 correlation,
               f32 temporalPhase, f32 spatialPhase, f32 minAmount, f32 maxAmount) noexcept {
    // Fase temporal: 360° = uma oscilação adiantada. Fase espacial: cada
    // unidade anda essa fração a mais (onda que corre pelo texto).
    const f64 base = timeSec * std::max(0.0f, rate) + static_cast<f64>(temporalPhase) / 360.0;
    const f64 lane = base + static_cast<f64>(spatialPhase) / 360.0 * unit;
    const f32 own = smooth_noise(seed * 31u + dimension, unit + 1u, lane);
    const f32 shared = smooth_noise(seed * 31u + dimension, 0u, base);
    const f32 c = std::clamp(correlation, 0.0f, 1.0f);
    const f32 n = std::clamp(own * (1.0f - c) + shared * c, -1.0f, 1.0f);
    const f32 lo = std::min(minAmount, maxAmount), hi = std::max(minAmount, maxAmount);
    return lo + (n * 0.5f + 0.5f) * (hi - lo);
}

} // namespace aurea::text
