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

f32 combine_selector(f32 above, f32 value, u32 mode) noexcept {
    switch (mode) {
        case 1: return above - value;                    // subtrair
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
std::vector<u32> shuffled(u32 count, u32 seed) {
    std::vector<u32> p(count);
    for (u32 i = 0; i < count; ++i) p[i] = i;
    for (u32 i = count; i > 1; --i) std::swap(p[i - 1], p[mix(seed, i, 11u) % i]);
    return p;
}
Vec3 rgb_to_hsv(Vec3 c) {
    const f32 mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z}), d = mx - mn;
    f32 h = 0;
    if (d > 1e-6f) {
        if (mx == c.x) h = std::fmod((c.y - c.z) / d, 6.0f);
        else if (mx == c.y) h = (c.z - c.x) / d + 2.0f;
        else h = (c.x - c.y) / d + 4.0f;
        h /= 6.0f; if (h < 0) h += 1.0f;
    }
    return {h, mx > 0 ? d / mx : 0.0f, mx};
}
Vec3 hsv_to_rgb(Vec3 c) {
    const f32 h = (c.x - std::floor(c.x)) * 6.0f, s = std::clamp(c.y, 0.0f, 1.0f), v = std::clamp(c.z, 0.0f, 1.0f);
    const f32 f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (static_cast<int>(h) % 6) {
        case 0: return {v, t, p}; case 1: return {q, v, p}; case 2: return {p, v, t};
        case 3: return {p, q, v}; case 4: return {t, p, v}; default: return {v, p, q};
    }
}
} // namespace

f32 pro_range_weight(f32 start, f32 end, f32 offset, u32 units, u32 shape, f32 smoothness,
                     f32 easeHigh, f32 easeLow, u32 unit, u32 count) noexcept {
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
        else if (c >= st && c <= en) w = shape_at(shape, (c - st) / (en - st));
    }
    return ease(w, easeHigh, easeLow);
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

namespace {
const ParameterRegistry& pro_specs() {
    static const ParameterRegistry specs = [] { ParameterRegistry p; declare_animator_pro_params(p); return p; }();
    return specs;
}
} // namespace

/// Aplica um Text Animator Pro (chamado por evaluate_animator_effects).
void evaluate_animator_pro(const Layer& layer, const EffectInstance& fx, f64 time, f64 fps, const std::vector<GlyphUnits>& units,
                           u32 chars, u32 words, u32 lines, std::vector<GlyphAnim>& out) {
    std::array<ParamValue, apCount> v{};
    for (u32 p = 0; p < apCount; ++p) v[p] = evaluate_param_f(layer.tracks, fx, p, pro_specs().at(p), time);
    const f64 timeSec = time / (fps > 0.0 ? fps : 30.0);
    const u32 basedOn = v[apBasedOn].as_enum();
    // Letras sem espaços: a posição do glifo entre os glifos visíveis (os
    // espaços não viram glifo); os outros contam pelo índice do layout.
    std::vector<u32> visible;
    if (basedOn == 1) {
        std::vector<u32> seen;
        for (const GlyphUnits& u : units) seen.push_back(u.charIndex);
        std::sort(seen.begin(), seen.end()); seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
        visible.reserve(units.size());
        for (const GlyphUnits& u : units)
            visible.push_back(static_cast<u32>(std::lower_bound(seen.begin(), seen.end(), u.charIndex) - seen.begin()));
        chars = static_cast<u32>(seen.size());
    }
    const u32 count = std::max<u32>(1, basedOn == 2 ? words : basedOn == 3 ? lines : chars);
    const std::vector<u32> perm = v[apRandom].as_bool() ? shuffled(count, static_cast<u32>(v[apSeed].v[0]) + 1u) : std::vector<u32>{};
    const u32 mode = v[apMode].as_enum(), shape = v[apShape].as_enum(), unitsMode = v[apUnits].as_enum();
    const f32 amount = v[apAmount].v[0] / 100.0f;
    const bool wiggly = v[apWiggly].as_bool();
    const bool exprSel = v[apExprOn].as_bool();
    const u32 wMode = v[apWigglyMode].as_enum();
    const bool lock = v[apLockDims].as_bool();
    // A primeira seleção parte de "nada" (somar) ou de "tudo" (subtrair/interseção/mínimo).
    const f32 above = (mode == 1 || mode == 2 || mode == 3) ? 1.0f : 0.0f;

    const Vec2 anchor = v[apAnchor].as_vec2();
    const Vec3 pos = v[apPosition].as_vec3(), rot = v[apRotation].as_vec3();
    const Vec2 scl = v[apScale].as_vec2(), lineSpacing = v[apLineSpacing].as_vec2(), align = v[apGroupAlign].as_vec2();
    const f32 skew = v[apSkew].v[0], skewAxis = v[apSkewAxis].v[0] * kDeg2Rad, opacity = v[apOpacity].v[0];
    const f32 blur = v[apBlur].v[0], strokeWidth = v[apStrokeWidth].v[0], tracking = v[apTracking].v[0];
    const bool fillOn = v[apFillOn].as_bool(), strokeOn = v[apStrokeOn].as_bool();
    // Cor de efeito é guardada em linear (como nos outros efeitos); o texto pinta em sRGB.
    auto srgb = [](Vec4 c) { return Vec4{Color::linear_to_srgb(c.x), Color::linear_to_srgb(c.y), Color::linear_to_srgb(c.z), c.w}; };
    const Vec4 fillColor = srgb(v[apFill].as_color()), strokeColor = srgb(v[apStroke].as_color());
    const f32 hue = v[apFillHue].v[0], sat = v[apFillSaturation].v[0] / 100.0f, bri = v[apFillBrightness].v[0] / 100.0f;
    const f32 fillOpacity = v[apFillOpacity].v[0] / 100.0f;
    const bool hsb = hue != 0.0f || sat != 0.0f || bri != 0.0f;
    const u32 grouping = std::min(v[apGrouping].as_enum(), 3u);

    if (out.size() != units.size()) out.resize(units.size());
    std::vector<f32> weightX(units.size(), 0.0f);
    for (usize g = 0; g < units.size(); ++g) {
        const GlyphUnits& gu = units[g];
        u32 unit = basedOn == 1 ? visible[g] : basedOn == 2 ? gu.wordIndex : basedOn == 3 ? gu.lineIndex : gu.charIndex;
        unit = std::min(unit, count - 1);
        if (!perm.empty()) unit = perm[unit];
        const f32 range = pro_range_weight(v[apStart].v[0], v[apEnd].v[0], v[apOffset].v[0], unitsMode, shape,
                                           v[apSmoothness].v[0] / 100.0f, v[apEaseHigh].v[0] / 100.0f, v[apEaseLow].v[0] / 100.0f,
                                           unit, count) * amount;
        // Seletor de expressão: a intensidade da expressão por unidade (índice 1..,
        // como textIndex) substitui a seleção de cima, eixo a eixo.
        Vec3 exprW{1, 1, 1};
        if (exprSel) {
            const u32 raw = basedOn == 1 ? visible[g] : basedOn == 2 ? gu.wordIndex : basedOn == 3 ? gu.lineIndex : gu.charIndex;
            expr::TextScope scope(std::min(raw, count - 1) + 1, count);
            const ParamValue e = evaluate_param_f(layer.tracks, fx, apExprAmount, pro_specs().at(apExprAmount), time);
            exprW = Vec3{e.v[0] / 100.0f, e.v[1] / 100.0f, e.v[2] / 100.0f};
        }
        Vec3 w3;
        for (u32 d = 0; d < 3; ++d) {
            f32 w = exprSel ? (&exprW.x)[d] : combine_selector(above, range, mode);
            if (wiggly) {
                const f32 wv = pro_wiggle(static_cast<u32>(v[apWigglySeed].v[0]), unit, lock ? 0u : d, timeSec, v[apWigglyRate].v[0],
                                          v[apCorrelation].v[0] / 100.0f, v[apTemporalPhase].v[0], v[apSpatialPhase].v[0],
                                          v[apWigglyMin].v[0] / 100.0f, v[apWigglyMax].v[0] / 100.0f);
                w = combine_selector(w, wv, wMode);
            }
            (&w3.x)[d] = std::clamp(w, -1.0f, 1.0f);
        }
        const f32 w = w3.x;
        weightX[g] = w;
        GlyphAnim& o = out[g];
        if (w == 0.0f && w3.y == 0.0f && w3.z == 0.0f) continue;
        o.anchorGrouping = grouping;
        o.groupAlign = Vec2{align.x / 100.0f, align.y / 100.0f};
        o.anchor = o.anchor + Vec2{anchor.x * w3.x, anchor.y * w3.y};
        o.translate = o.translate + Vec3{pos.x * w3.x, pos.y * w3.y, pos.z * w3.z}
                    + Vec3{lineSpacing.x * w * static_cast<f32>(gu.lineIndex), lineSpacing.y * w * static_cast<f32>(gu.lineIndex), 0};
        // Escala negativa espelha a letra (como no animador de referência).
        o.scale.x *= 1.0f + w3.x * (scl.x / 100.0f - 1.0f);
        o.scale.y *= 1.0f + w3.y * (scl.y / 100.0f - 1.0f);
        o.rotation = o.rotation + Vec3{rot.x * w3.x, rot.y * w3.y, rot.z * w};
        o.opacity *= std::clamp(1.0f + w * (opacity / 100.0f - 1.0f), 0.0f, 1.0f);
        o.blur += std::max(0.0f, w * blur);
        o.strokeAdd += w * strokeWidth;
        if (skew != 0.0f) {
            Mat4 shear;
            shear.col[1].x = -std::tan(std::clamp(w * skew, -80.0f, 80.0f) * kDeg2Rad);
            const Mat4 axis = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, skewAxis));
            const Mat4 inverseAxis = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, -skewAxis));
            o.skewTransform = o.skewTransform * axis * shear * inverseAxis;
            o.skew += w * skew;
        }
        const f32 mixW = std::clamp(std::fabs(w), 0.0f, 1.0f);
        if (fillOn || hsb) {
            // A cor de partida é a da camada (ou a do animador, se ligada); o
            // matiz/saturação/brilho deslocam essa cor.
            Vec3 c = fillOn ? Vec3{fillColor.x, fillColor.y, fillColor.z} : Vec3{layer.text.color.x, layer.text.color.y, layer.text.color.z};
            if (hsb) {
                Vec3 h = rgb_to_hsv(c);
                h.x += hue / 360.0f; h.y = std::clamp(h.y + sat, 0.0f, 1.0f); h.z = std::clamp(h.z + bri, 0.0f, 1.0f);
                c = hsv_to_rgb(h);
            }
            o.fill = Vec4{c.x, c.y, c.z, mixW};
        }
        if (fillOpacity < 1.0f) o.fillOpacity *= std::clamp(1.0f + mixW * (fillOpacity - 1.0f), 0.0f, 1.0f);
        if (strokeOn) o.stroke = Vec4{strokeColor.x, strokeColor.y, strokeColor.z, mixW};
    }
    if (tracking != 0.0f) {
        // Espaçamento: empurra os glifos seguintes da mesma linha.
        std::vector<f32> acc(std::max<u32>(1, lines) + 1, 0.0f);
        for (usize g = 0; g < units.size(); ++g) {
            const u32 li = std::min<u32>(units[g].lineIndex, static_cast<u32>(acc.size() - 1));
            out[g].trackingShift += acc[li];
            acc[li] += weightX[g] * tracking;
        }
    }
}

} // namespace aurea::text
