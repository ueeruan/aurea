// =============================================================================
//  Aurea / text / TextAnimator.cpp
// =============================================================================
#include "aurea/text/TextAnimator.hpp"

#include "aurea/timeline/Layer.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include <array>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace aurea::text {

void declare_animator_effect_params(ParameterRegistry& p) {
    constexpr u16 pct = kParamAnimatable | kParamPercent;
    p.add_float("start", "Início", 0, -100, 200, pct, "%");
    p.add_float("end", "Fim", 100, -100, 200, pct, "%");
    p.add_float("offset", "Deslocamento do seletor", 0, -200, 200, pct, "%");
    p.add_float("amount", "Intensidade", 100, -100, 100, pct, "%");
    static const char* units[] = {"Letra", "Palavra", "Linha"};
    p.add_enum("unit", "Unidade", units, 3, 0);
    static const char* shapes[] = {"Quadrada", "Rampa crescente", "Rampa decrescente", "Triângulo", "Redonda", "Suave"};
    p.add_enum("shape", "Forma do seletor", shapes, 6, 0);
    p.add_float("ease_high", "Suavizar máximo", 0, 0, 100, pct, "%");
    p.add_float("ease_low", "Suavizar mínimo", 0, 0, 100, pct, "%");
    p.add_bool("random_order", "Ordem aleatória", false);
    p.add_int("seed", "Semente", 1, 0, 9999);
    static const char* selectors[] = {"Alcance", "Oscilação aleatória"};
    p.add_enum("selector", "Seletor", selectors, 2, 0);
    p.add_float("wiggle_rate", "Frequência da oscilação", 2, 0, 30, kParamAnimatable, "Hz");
    p.add_point3("position", "Posição", {}, -2000, 2000);
    p.add_point2("scale", "Escala", {100, 100}, 0, 1000, pct);
    p.add_point3("rotation", "Rotação", {}, -1800, 1800);
    p.add_float("opacity", "Opacidade", 100, 0, 100, pct, "%");
    p.add_float("blur", "Desfoque", 0, 0, 100, kParamAnimatable | kParamPixels, "px");
    p.add_float("tracking", "Espaçamento entre letras", 0, -200, 200, kParamAnimatable | kParamPixels, "px");
    p.add_angle("skew", "Inclinação", 0, -80, 80);
    p.add_float("stroke_width", "Largura do contorno", 0, 0, 60, kParamAnimatable | kParamPixels, "px");
    p.add_bool("fill_enabled", "Substituir cor do preenchimento", false);
    p.add_color("fill_color", "Cor do preenchimento", {1, 1, 1, 1});
    p.add_bool("stroke_enabled", "Substituir cor do contorno", false);
    p.add_color("stroke_color", "Cor do contorno", {0, 0, 0, 1});
}

namespace {

u32 hash3(u32 a, u32 b, u32 c) {
    u32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
f32 rnd_signed(u32 a, u32 b, u32 c) { return static_cast<f32>(hash3(a, b, c) & 0xFFFFFFu) / 8388607.5f - 1.0f; }

/// Ruído de valor 1D por unidade (suave no tempo, determinístico por semente).
f32 wiggle(u32 seed, u32 unit, f64 t) {
    const f64 k = std::floor(t);
    const f32 u = static_cast<f32>(t - k);
    const f32 a = rnd_signed(seed, unit, static_cast<u32>(static_cast<i64>(k)));
    const f32 b = rnd_signed(seed, unit, static_cast<u32>(static_cast<i64>(k) + 1));
    const f32 s = u * u * (3.0f - 2.0f * u);
    return a + (b - a) * s;
}

f32 shape_value(u8 shape, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (shape) {
        case 1: return t;                                                   // rampa sobe
        case 2: return 1.0f - t;                                            // rampa desce
        case 3: return 1.0f - std::fabs(2.0f * t - 1.0f);                   // triângulo
        case 4: { const f32 x = 2.0f * t - 1.0f; return std::sqrt(std::max(0.0f, 1.0f - x * x)); }   // redondo
        case 5: return 0.5f - 0.5f * std::cos(t * 6.28318530718f);          // suave (sino)
        default: return 1.0f;
    }
}

/// Ordem embaralhada determinística das unidades (Fisher-Yates pela semente).
std::vector<u32> shuffled(u32 count, u32 seed) {
    std::vector<u32> p(count);
    for (u32 i = 0; i < count; ++i) p[i] = i;
    for (u32 i = count; i > 1; --i) std::swap(p[i - 1], p[hash3(seed, i, 7u) % i]);
    return p;
}

u32 unit_of(const TextSelector& s, const GlyphUnits& u) { return s.basedOn == 1 ? u.wordIndex : s.basedOn == 2 ? u.lineIndex : u.charIndex; }

} // namespace

f32 anim_param(const TrackSet& tracks, u32 animator, u32 param, f64 local, f32 fallback) noexcept {
    const Track* tr = tracks.find(TrackProperty::TextAnimParam, animator, param);
    if (!tr || !tr->driven()) return fallback;
    return tr->value_or_f(local, fallback);
}

bool has_animators(const TextData& t) noexcept {
    for (const TextAnimator& a : t.animators) if (a.enabled) return true;
    return false;
}

bool has_rig_animators(const TextData& t) noexcept {
    for (const TextAnimator& a : t.animators) if (a.enabled && a.rig != 0) return true;
    return false;
}

bool has_content_animators(const TextData& t) noexcept {
    for (const TextAnimator& a : t.animators) if (a.enabled && (a.props & (kTextPropCharOffset | kTextPropReveal | kTextPropRandomize | kTextPropSpacing))) return true;
    return false;
}

namespace {

/// Faixa e oscilação no formato dos projetos até v58.
f32 legacy_range(const TextSelector& s, u32 ai, u32 si, const TrackSet& tracks, f64 local, f64 timeSec, u32 unit, u32 count) noexcept {
    const auto P = [&](u32 k, f32 fb) { return anim_param(tracks, ai, selector_key(si, k), local, fb); };
    const f32 amount = P(kSelAmount, s.amount) / 100.0f;
    if (s.type == kTextSelectorWiggleLegacy) {
        const f32 rate = std::max(0.0f, P(kWiggleRate, s.wiggleRate));
        return std::clamp(wiggle(s.seed, unit, timeSec * rate), -1.0f, 1.0f) * amount;
    }
    const f32 n = static_cast<f32>(std::max<u32>(1, count));
    const f32 off = P(kSelOffset, s.offset);
    f32 st = P(kSelStart, s.start) + off;
    f32 en = P(kSelEnd, s.end) + off;
    if (st > en) std::swap(st, en);
    const f32 u0 = static_cast<f32>(unit) / n * 100.0f, u1 = static_cast<f32>(unit + 1) / n * 100.0f;
    f32 w = 0.0f;
    if (s.shape == 0) {
        // Quadrado: fração da unidade dentro do intervalo (revelação suave).
        const f32 ov = std::max(0.0f, std::min(u1, en) - std::max(u0, st));
        w = ov / (u1 - u0);
    } else {
        const f32 c = (u0 + u1) * 0.5f;
        if (en > st && c >= st && c <= en) w = shape_value(s.shape, (c - st) / (en - st));
    }
    const f32 eh = P(kSelEaseHigh, s.easeHigh), el = P(kSelEaseLow, s.easeLow);
    if (eh > 0.0f || el > 0.0f) {
        // Ease alto/baixo: mais tempo perto de 1 (alto) ou de 0 (baixo).
        const f32 high = 1.0f - (1.0f - w) * (1.0f - w), low = w * w;
        w = w + std::clamp(eh / 100.0f, 0.0f, 1.0f) * (high - w) * 0.5f + std::clamp(el / 100.0f, 0.0f, 1.0f) * (low - w) * 0.5f;
    }
    return std::clamp(w, 0.0f, 1.0f) * amount;
}

} // namespace

f32 selector_weight(const TextAnimator& a, u32 ai, const TrackSet& tracks, f64 local, f64 timeSec, u32 unit, u32 count) noexcept {
    expr::TextScope glyph(unit + 1, count);
    return legacy_range(a.selector, ai, 0, tracks, local, timeSec, unit, count);
}

namespace {

bool selector_active(const TextSelector& s) noexcept { return s.enabled; }

/// O peso de cada glifo (−1..1 por eixo): os seletores do animador, combinados na ordem.
void selector_weights(const TextAnimator& a, u32 ai, const TrackSet& tracks, f64 local, f64 timeSec, const std::vector<GlyphUnits>& units,
                      u32 chars, u32 words, u32 lines, std::vector<Vec3>& out) {
    out.assign(units.size(), Vec3{0, 0, 0});
    // Letras sem espaços: a posição do glifo entre os que têm tinta.
    std::vector<u32> visible;
    u32 inked = 0;
    const auto ensure_visible = [&] {
        if (!visible.empty() || units.empty()) return;
        std::vector<u32> seen;
        seen.reserve(units.size());
        for (const GlyphUnits& u : units) seen.push_back(u.charIndex);
        std::sort(seen.begin(), seen.end());
        seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
        inked = static_cast<u32>(seen.size());
        visible.reserve(units.size());
        for (const GlyphUnits& u : units) visible.push_back(static_cast<u32>(std::lower_bound(seen.begin(), seen.end(), u.charIndex) - seen.begin()));
    };
    bool first = true;
    const u32 total = 1u + std::min<u32>(static_cast<u32>(a.moreSelectors.size()), kMaxTextSelectors - 1u);
    for (u32 si = 0; si < total; ++si) {
        const TextSelector& s = si == 0 ? a.selector : a.moreSelectors[si - 1];
        if (!selector_active(s)) continue;
        const auto P = [&](u32 k, f32 fb) { return anim_param(tracks, ai, selector_key(si, k), local, fb); };
        if (s.basedOn == 3) ensure_visible();
        const u32 count = std::max<u32>(1, s.basedOn == 1 ? words : s.basedOn == 2 ? lines : s.basedOn == 3 ? inked : chars);
        const std::vector<u32> perm = s.randomOrder ? shuffled(count, s.seed) : std::vector<u32>{};
        const u32 mode = s.mode;
        // O primeiro seletor parte de "nada" (somar) ou de "tudo" (subtrair, interseção, mínimo).
        const f32 start = first && (mode == kTextModeSubtract || mode == kTextModeIntersect || mode == kTextModeMin) ? 1.0f : 0.0f;
        for (usize g = 0; g < units.size(); ++g) {
            const GlyphUnits& gu = units[g];
            const u32 raw = std::min(s.basedOn == 3 ? visible[g] : unit_of(s, gu), count - 1);
            const u32 unit = perm.empty() ? raw : perm[raw];
            Vec3 above = out[g];
            if (first) above = Vec3{start, start, start};
            Vec3 value{0, 0, 0};
            switch (s.type) {
                case kTextSelectorRangeLegacy: case kTextSelectorWiggleLegacy: {
                    expr::TextScope glyph(unit + 1, count);
                    const f32 w = legacy_range(s, ai, si, tracks, local, timeSec, unit, count);
                    value = Vec3{w, w, w};
                    break;
                }
                case kTextSelectorRange: {
                    expr::TextScope glyph(raw + 1, count);
                    const f32 w = pro_range_weight(P(kSelStart, s.start), P(kSelEnd, s.end), P(kSelOffset, s.offset), s.units, s.shape,
                                                   P(kAnimSmoothness, s.smoothness) / 100.0f, P(kSelEaseHigh, s.easeHigh) / 100.0f,
                                                   P(kSelEaseLow, s.easeLow) / 100.0f, unit, count, a.aeModel != 0) * P(kSelAmount, s.amount) / 100.0f;
                    value = Vec3{w, w, w};
                    break;
                }
                case kTextSelectorWiggly: {
                    const f32 rate = P(kWiggleRate, s.wiggleRate), corr = P(kAnimCorrelation, s.correlation) / 100.0f;
                    const f32 tp = P(kAnimTemporalPhase, s.temporalPhase), sp = P(kAnimSpatialPhase, s.spatialPhase);
                    const f32 lo = P(kAnimWigglyMin, s.wiggleMin) / 100.0f, hi = P(kAnimWigglyMax, s.wiggleMax) / 100.0f;
                    for (u32 d = 0; d < 3; ++d)
                        (&value.x)[d] = pro_wiggle(s.seed, unit, s.lockDimensions ? 0u : d, timeSec, rate, corr, tp, sp, lo, hi);
                    break;
                }
                case kTextSelectorExpression: {
                    // textIndex é da unidade na ordem do texto; selectorValue, o resultado de cima em %.
                    const Vec3 selected{above.x * 100.0f, above.y * 100.0f, above.z * 100.0f};
                    expr::TextScope glyph(raw + 1, count, &selected);
                    value = Vec3{P(kAnimExprX, s.exprAmount.x) / 100.0f, P(kAnimExprY, s.exprAmount.y) / 100.0f, P(kAnimExprZ, s.exprAmount.z) / 100.0f};
                    break;
                }
                default: break;
            }
            Vec3 result;
            for (u32 d = 0; d < 3; ++d)
                (&result.x)[d] = std::clamp(mode == kTextModeReplace ? (&value.x)[d] : combine_selector((&above.x)[d], (&value.x)[d], mode, a.aeModel != 0), -1.0f, 1.0f);
            out[g] = result;
        }
        first = false;
    }
}

Vec3 anim_rgb_to_hsv(Vec3 c) {
    const f32 mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z}), d = mx - mn;
    f32 h = 0;
    if (d > 1e-6f) {
        if (mx == c.x) h = std::fmod((c.y - c.z) / d, 6.0f);
        else if (mx == c.y) h = (c.z - c.x) / d + 2.0f;
        else h = (c.x - c.y) / d + 4.0f;
        h /= 6.0f;
        if (h < 0) h += 1.0f;
    }
    return {h, mx > 0 ? d / mx : 0.0f, mx};
}

Vec3 anim_hsv_to_rgb(Vec3 c) {
    const f32 h = (c.x - std::floor(c.x)) * 6.0f, s = std::clamp(c.y, 0.0f, 1.0f), v = std::clamp(c.z, 0.0f, 1.0f);
    const f32 f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (static_cast<int>(h) % 6) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

} // namespace

void evaluate_text_animators(const TextData& t, const TrackSet& tracks, f64 local, f64 fps, const std::vector<GlyphUnits>& units, u32 chars,
                             u32 words, u32 lines, std::vector<GlyphAnim>& out) {
    out.assign(units.size(), GlyphAnim{});
    if (!has_animators(t)) return;
    const f64 timeSec = local / (fps > 0.0 ? fps : 30.0);
    std::vector<Vec3> weights;
    for (u32 ai = 0; ai < t.animators.size(); ++ai) {
        const TextAnimator& a = t.animators[ai];
        // O modelo do Motion avalia em evaluate_text_rigs; o conteúdo, antes do layout.
        if (!a.enabled || a.rig != 0) continue;
        selector_weights(a, ai, tracks, local, timeSec, units, chars, words, lines, weights);
        auto P = [&](u32 p, f32 fb) { return anim_param(tracks, ai, p, local, fb); };
        const Vec3 pos{P(kPosX, a.position.x), P(kPosY, a.position.y), P(kPosZ, a.position.z)};
        const Vec2 scl{P(kScaleX, a.scale.x), P(kScaleY, a.scale.y)};
        const Vec3 rot{P(kRotX, a.rotation.x), P(kRotY, a.rotation.y), P(kRotZ, a.rotation.z)};
        const f32 op = P(kOpacity, a.opacity), blur = P(kBlur, a.blur), skew = P(kSkew, a.skew);
        const f32 trk = P(kTracking, a.tracking) * (P(kTrackingEm, 0) > 0.5f ? t.size / 1000.0f : 1.0f);
        const f32 skewAxis = P(kSkewAxis, 0) * kDeg2Rad;
        const u32 anchorGrouping = a.anchorGrouping ? std::min<u32>(a.anchorGrouping, 3u) : static_cast<u32>(std::clamp(P(kAnchorGrouping, 0), 0.0f, 2.0f));
        const f32 sw = P(kStrokeWidth, a.strokeWidth);
        const Vec3 fill{P(kFillR, a.fill.x), P(kFillG, a.fill.y), P(kFillB, a.fill.z)};
        const Vec3 stroke{P(kStrokeR, a.stroke.x), P(kStrokeG, a.stroke.y), P(kStrokeB, a.stroke.z)};
        const Vec2 anchor{P(kAnimAnchorX, a.anchor.x), P(kAnimAnchorY, a.anchor.y)};
        const Vec2 lineGap{P(kAnimLineSpacingX, a.lineSpacing.x), P(kAnimLineSpacingY, a.lineSpacing.y)};
        const Vec2 groupAlign{P(kAnimGroupAlignX, a.groupAlign.x) / 100.0f, P(kAnimGroupAlignY, a.groupAlign.y) / 100.0f};
        const f32 hue = P(kAnimFillHue, a.fillHue), sat = P(kAnimFillSaturation, a.fillSaturation) / 100.0f, bri = P(kAnimFillBrightness, a.fillBrightness) / 100.0f;
        const f32 fillOpacity = P(kAnimFillOpacity, a.fillOpacity) / 100.0f;
        for (usize g = 0; g < units.size(); ++g) {
            const Vec3 w3 = weights[g];
            const f32 w = w3.x;
            if (w == 0.0f && w3.y == 0.0f && w3.z == 0.0f) continue;
            GlyphAnim& o = out[g];
            o.anchorGrouping = anchorGrouping;
            o.aeModel = a.aeModel != 0;
            o.groupAlign = groupAlign;
            if (a.props & kTextPropAnchor) o.anchor = o.anchor + Vec2{anchor.x * w3.x, anchor.y * w3.y};
            if (a.props & kTextPropPosition) o.translate = o.translate + Vec3{pos.x * w3.x, pos.y * w3.y, pos.z * w3.z};
            if (a.props & kTextPropLineSpacing) o.translate = o.translate + Vec3{lineGap.x * w * static_cast<f32>(units[g].lineIndex), lineGap.y * w * static_cast<f32>(units[g].lineIndex), 0};
            if (a.props & kTextPropScale) {
                // Escala negativa espelha a letra, como no After Effects.
                o.scale.x *= 1.0f + w3.x * (scl.x / 100.0f - 1.0f);
                o.scale.y *= 1.0f + w3.y * (scl.y / 100.0f - 1.0f);
            }
            if (a.props & kTextPropRotation) o.rotation = o.rotation + Vec3{rot.x * w3.x, rot.y * w3.y, rot.z * w};
            if (a.props & kTextPropOpacity) o.opacity *= std::clamp(1.0f + w * (op / 100.0f - 1.0f), 0.0f, 1.0f);
            if (a.props & kTextPropBlur) o.blur += std::max(0.0f, w * blur);
            if (a.props & kTextPropSkew) {
                Mat4 shear;
                shear.col[1].x = -std::tan(std::clamp(w * skew, -80.0f, 80.0f) * kDeg2Rad);
                const Mat4 axis = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, skewAxis));
                const Mat4 inverseAxis = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, -skewAxis));
                o.skewTransform = o.skewTransform * axis * shear * inverseAxis;
                o.skew += w * skew;
            }
            if (a.props & kTextPropStrokeWidth) o.strokeAdd += w * sw;
            const f32 mixW = std::clamp(std::fabs(w), 0.0f, 1.0f);
            if (a.props & (kTextPropFill | kTextPropFillHsb)) {
                Vec3 c = a.props & kTextPropFill ? fill : Vec3{t.color.x, t.color.y, t.color.z};
                if (a.props & kTextPropFillHsb) {
                    Vec3 h = anim_rgb_to_hsv(c);
                    h.x += hue / 360.0f;
                    h.y = std::clamp(h.y + sat, 0.0f, 1.0f);
                    h.z = std::clamp(h.z + bri, 0.0f, 1.0f);
                    c = anim_hsv_to_rgb(h);
                }
                o.fill = Vec4{c.x, c.y, c.z, mixW};
            }
            if ((a.props & kTextPropFillOpacity) && fillOpacity < 1.0f) o.fillOpacity *= std::clamp(1.0f + mixW * (fillOpacity - 1.0f), 0.0f, 1.0f);
            if (a.props & kTextPropStroke) o.stroke = Vec4{stroke.x, stroke.y, stroke.z, mixW};
        }
        if (a.props & kTextPropTracking) {
            // Tracking empurra os glifos seguintes da mesma linha (ordem visual).
            std::vector<f32> acc(std::max<u32>(1, lines) + 1, 0.0f);
            for (usize g = 0; g < units.size(); ++g) {
                const u32 li = std::min<u32>(units[g].lineIndex, static_cast<u32>(acc.size() - 1));
                out[g].trackingShift += acc[li];
                acc[li] += weights[g].x * trk;
            }
        }
    }
}

namespace {
f32* selector_param(TextSelector& s, u32 k) noexcept {
    switch (k) {
        case kSelStart: return &s.start;
        case kSelEnd: return &s.end;
        case kSelOffset: return &s.offset;
        case kSelAmount: return &s.amount;
        case kSelEaseHigh: return &s.easeHigh;
        case kSelEaseLow: return &s.easeLow;
        case kWiggleRate: return &s.wiggleRate;
        case kAnimSmoothness: return &s.smoothness;
        case kAnimWigglyMin: return &s.wiggleMin;
        case kAnimWigglyMax: return &s.wiggleMax;
        case kAnimCorrelation: return &s.correlation;
        case kAnimTemporalPhase: return &s.temporalPhase;
        case kAnimSpatialPhase: return &s.spatialPhase;
        case kAnimExprX: return &s.exprAmount.x;
        case kAnimExprY: return &s.exprAmount.y;
        case kAnimExprZ: return &s.exprAmount.z;
        case kAnimOverlap: return &s.overlap;
        default: return nullptr;
    }
}
} // namespace

f32* animator_param_ref(TextAnimator& a, u32 key) noexcept {
    const u32 block = key / kSelectorKeyBlock, k = key % kSelectorKeyBlock;
    if (block > 0) return block <= a.moreSelectors.size() ? selector_param(a.moreSelectors[block - 1], k) : nullptr;
    if (f32* selector = selector_param(a.selector, k)) return selector;
    switch (k) {
        case kPosX: return &a.position.x;
        case kPosY: return &a.position.y;
        case kPosZ: return &a.position.z;
        case kScaleX: return &a.scale.x;
        case kScaleY: return &a.scale.y;
        case kRotX: return &a.rotation.x;
        case kRotY: return &a.rotation.y;
        case kRotZ: return &a.rotation.z;
        case kOpacity: return &a.opacity;
        case kTracking: return &a.tracking;
        case kBlur: return &a.blur;
        case kSkew: return &a.skew;
        case kStrokeWidth: return &a.strokeWidth;
        case kCharOffset: return &a.charOffset;
        case kFillR: return &a.fill.x;
        case kFillG: return &a.fill.y;
        case kFillB: return &a.fill.z;
        case kAnimFillA: return &a.fill.w;
        case kStrokeR: return &a.stroke.x;
        case kStrokeG: return &a.stroke.y;
        case kStrokeB: return &a.stroke.z;
        case kAnimAnchorX: return &a.anchor.x;
        case kAnimAnchorY: return &a.anchor.y;
        case kAnimLineSpacingX: return &a.lineSpacing.x;
        case kAnimLineSpacingY: return &a.lineSpacing.y;
        case kAnimGroupAlignX: return &a.groupAlign.x;
        case kAnimGroupAlignY: return &a.groupAlign.y;
        case kAnimFillHue: return &a.fillHue;
        case kAnimFillSaturation: return &a.fillSaturation;
        case kAnimFillBrightness: return &a.fillBrightness;
        case kAnimFillOpacity: return &a.fillOpacity;
        case kAnimRandAmount: return &a.randAmount;
        case kAnimRandEvolution: return &a.randEvolution;
        case kAnimRandSeed: return &a.randSeed;
        case kAnimLetterSpacing: return &a.letterSpacing;
        case kAnimLineSpacingEm: return &a.lineSpacingEm;
        default: return nullptr;
    }
}

const f32* animator_param_ref(const TextAnimator& a, u32 key) noexcept { return animator_param_ref(const_cast<TextAnimator&>(a), key); }

void read_animator_fields(const TextAnimator& a, f32* v) noexcept {
    std::fill(v, v + kTextAnimatorFieldCount, 0.0f);
    v[0] = static_cast<f32>(a.props); v[1] = a.rig; v[2] = a.rigAnchor; v[3] = a.anchorGrouping; v[4] = a.cursor;
    v[5] = a.cursorBlink ? 1.f : 0.f; v[6] = a.charset; v[7] = a.preserveSpace ? 1.f : 0.f; v[8] = a.groupAlign.x; v[9] = a.groupAlign.y;
    const u32 extra = std::min<u32>(static_cast<u32>(a.moreSelectors.size()), kMaxTextSelectors - 1u);
    v[15] = static_cast<f32>(1u + extra);
    for (u32 si = 0; si <= extra; ++si) {
        const TextSelector& s = si == 0 ? a.selector : a.moreSelectors[si - 1];
        f32* o = v + 16u * (si + 1u);
        o[0] = s.enabled ? 1.f : 0.f; o[1] = s.type; o[2] = s.basedOn; o[3] = s.units; o[4] = s.mode; o[5] = s.shape;
        o[6] = s.randomOrder ? 1.f : 0.f; o[7] = static_cast<f32>(s.seed); o[8] = s.lockDimensions ? 1.f : 0.f;
    }
}

bool write_animator_field(TextAnimator& a, u32 field, f32 value) noexcept {
    if (!std::isfinite(value)) return false;
    if (field < 16) {
        switch (field) {
            case 0: a.props = static_cast<u32>(std::max(0.f, value)) & 0x3FFFFu; return true;
            case 2: a.rigAnchor = value > .5f ? 1 : 0; return true;
            case 3: a.anchorGrouping = static_cast<u8>(std::clamp(value, 0.f, 3.f)); return true;
            case 4: a.cursor = static_cast<u8>(std::clamp(value, 0.f, 8.f)); return true;
            case 5: a.cursorBlink = value > .5f; return true;
            case 6: a.charset = static_cast<u8>(std::clamp(value, 0.f, 14.f)); return true;
            case 7: a.preserveSpace = value > .5f; return true;
            case 8: a.groupAlign.x = std::clamp(value, -100.f, 100.f); return true;
            case 9: a.groupAlign.y = std::clamp(value, -100.f, 100.f); return true;
            default: return false;
        }
    }
    const u32 si = field / 16u - 1u, f = field % 16u;
    if (si > a.moreSelectors.size() || si >= kMaxTextSelectors) return false;
    TextSelector& s = si == 0 ? a.selector : a.moreSelectors[si - 1];
    switch (f) {
        case 0: s.enabled = value > .5f; return true;
        case 2: s.basedOn = static_cast<u8>(std::clamp(value, 0.f, 3.f)); return true;
        case 3: s.units = value > .5f ? 1 : 0; return true;
        case 4: s.mode = static_cast<u8>(std::clamp(value, 0.f, 5.f)); return true;
        case 5: s.shape = static_cast<u8>(std::clamp(value, 0.f, 5.f)); return true;
        case 6: s.randomOrder = value > .5f; return true;
        case 7: s.seed = static_cast<u32>(std::clamp(value, 0.f, 1e6f)); return true;
        case 8: s.lockDimensions = value > .5f; return true;
        default: return false;
    }
}

f32 clamp_animator_param(const TextAnimator& a, u32 key, f32 v) noexcept {
    const u32 block = key / kSelectorKeyBlock, k = key % kSelectorKeyBlock;
    const TextSelector* s = block == 0 ? &a.selector : block <= a.moreSelectors.size() ? &a.moreSelectors[block - 1] : nullptr;
    const bool indexUnits = s && s->units == 1;
    switch (k) {
        case kFillR: case kFillG: case kFillB: case kAnimFillA:
        case kStrokeR: case kStrokeG: case kStrokeB: return std::clamp(v, 0.0f, 1.0f);
        case kSelStart: case kSelEnd: return indexUnits ? std::clamp(v, -1000.0f, 1000.0f) : std::clamp(v, 0.0f, 100.0f);
        case kSelOffset: return std::clamp(v, -1000.0f, 1000.0f);
        case kSelAmount: case kSelEaseHigh: case kSelEaseLow: case kAnimWigglyMin: case kAnimWigglyMax:
        case kAnimGroupAlignX: case kAnimGroupAlignY: return std::clamp(v, -100.0f, 100.0f);
        case kAnimSmoothness: case kAnimCorrelation: case kAnimFillOpacity: return std::clamp(v, 0.0f, 100.0f);
        case kWiggleRate: return std::clamp(v, 0.0f, 60.0f);
        case kOpacity: return a.rig ? std::clamp(v, -100.0f, 100.0f) : std::clamp(v, 0.0f, 100.0f);
        case kScaleX: case kScaleY: return std::clamp(v, -2000.0f, 2000.0f);
        case kBlur: return std::clamp(v, 0.0f, 200.0f);
        case kSkew: return std::clamp(v, -80.0f, 80.0f);
        case kStrokeWidth: return std::clamp(v, -50.0f, 50.0f);
        default: return std::clamp(v, -100000.0f, 100000.0f);
    }
}

TextSelector make_text_selector(u32 type) noexcept {
    TextSelector s;
    s.type = static_cast<u8>(type);
    // Como no After Effects: oscilação e expressão entram em interseção com o de cima.
    if (type == kTextSelectorWiggly || type == kTextSelectorExpression) s.mode = kTextModeIntersect;
    return s;
}

std::string format_counter(f64 value, u32 decimals, bool grouping) {
    decimals = std::min(decimals, 6u);
    if (!std::isfinite(value)) value = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(decimals), std::fabs(value));
    std::string digits(buf);
    std::string frac;
    if (const auto dot = digits.find('.'); dot != std::string::npos) { frac = digits.substr(dot + 1); digits.resize(dot); }
    std::string out;
    // "-0" nunca aparece: o sinal só se o número mostrado não é zero.
    bool nonzero = false;
    for (char c : digits + frac) if (c != '0') nonzero = true;
    if (value < 0.0 && nonzero) out += '-';
    for (usize i = 0; i < digits.size(); ++i) {
        if (grouping && i > 0 && (digits.size() - i) % 3 == 0) out += '.';
        out += digits[i];
    }
    if (!frac.empty()) { out += ','; out += frac; }
    return out;
}

std::string expand_dynamic(const TextData& t, f64 local, f64 duration, f64 compFrame, f64 fps) {
    if (!t.dynamic || t.content.find('{') == std::string::npos) return t.content;
    if (!(fps > 0.0)) fps = 30.0;
    const f64 span = std::max(1.0, duration - 1.0);
    const f32 progress = static_cast<f32>(std::clamp(local / span, 0.0, 1.0));
    const Interpolation ease = t.counterEase <= static_cast<u8>(kLastInterpolation)
        ? static_cast<Interpolation>(t.counterEase) : Interpolation::Linear;
    const f32 k = apply_easing(ease, progress, 0.42f, 0.0f, 0.58f, 1.0f);
    const f64 value = t.counterFrom + (t.counterTo - t.counterFrom) * static_cast<f64>(k);
    const i64 frame = std::max<i64>(0, static_cast<i64>(std::floor(compFrame + 1e-6)));
    const i64 rate = std::max<i64>(1, static_cast<i64>(std::llround(fps)));
    const i64 seconds = frame / rate;
    std::string out;
    out.reserve(t.content.size() + 16);
    for (usize i = 0; i < t.content.size();) {
        auto take = [&](const char* token) {
            const usize n = std::char_traits<char>::length(token);
            if (t.content.compare(i, n, token) != 0) return false;
            i += n;
            return true;
        };
        char buf[48];
        if (take("{#}")) { out += format_counter(value, t.counterDecimals, t.counterGrouping); continue; }
        if (take("{tc}")) {
            std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld:%02lld", static_cast<long long>(seconds / 3600),
                          static_cast<long long>(seconds / 60 % 60), static_cast<long long>(seconds % 60),
                          static_cast<long long>(frame % rate));
            out += buf; continue;
        }
        if (take("{t}")) {
            std::snprintf(buf, sizeof(buf), "%lld:%02lld", static_cast<long long>(seconds / 60), static_cast<long long>(seconds % 60));
            out += buf; continue;
        }
        if (take("{f}")) { out += std::to_string(frame); continue; }
        out += t.content[i++];
    }
    return out;
}

std::string apply_char_offset(const TextData& t, const TrackSet& tracks, f64 local, f64 fps) {
    bool any = false;
    for (const TextAnimator& a : t.animators) any |= a.enabled && (a.props & kTextPropCharOffset);
    if (!any) return t.content;
    // Unidades lógicas direto do texto (o deslocamento vem antes do layout).
    std::vector<u32> cps;
    for (usize i = 0; i < t.content.size();) {
        const u8 c = static_cast<u8>(t.content[i]);
        u32 cp = 0xFFFD;
        usize n = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 0x6 && i + 1 < t.content.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<u8>(t.content[i + 1]) & 0x3Fu); n = 2; }
        else if ((c >> 4) == 0xE && i + 2 < t.content.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<u8>(t.content[i + 1]) & 0x3Fu) << 6) | (static_cast<u8>(t.content[i + 2]) & 0x3Fu);
            n = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < t.content.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<u8>(t.content[i + 1]) & 0x3Fu) << 12) | ((static_cast<u8>(t.content[i + 2]) & 0x3Fu) << 6)
               | (static_cast<u8>(t.content[i + 3]) & 0x3Fu);
            n = 4;
        }
        cps.push_back(cp);
        i += n;
    }
    std::vector<GlyphUnits> units(cps.size());
    u32 word = 0, line = 0;
    bool inWord = false;
    for (usize i = 0; i < cps.size(); ++i) {
        const bool space = cps[i] == ' ' || cps[i] == '\n' || cps[i] == '\t';
        if (cps[i] == '\n') ++line;
        if (!space && !inWord) inWord = true;
        if (space && inWord) { ++word; inWord = false; }
        units[i] = GlyphUnits{static_cast<u32>(i), word, line};
    }
    const f64 timeSec = local / (fps > 0.0 ? fps : 30.0);
    std::vector<f32> shift(cps.size(), 0.0f);
    for (u32 ai = 0; ai < t.animators.size(); ++ai) {
        const TextAnimator& a = t.animators[ai];
        if (!a.enabled || !(a.props & kTextPropCharOffset)) continue;
        const f32 amt = anim_param(tracks, ai, kCharOffset, local, a.charOffset);
        const u32 count = a.selector.basedOn == 1 ? word + 1 : a.selector.basedOn == 2 ? line + 1 : static_cast<u32>(cps.size());
        const auto perm = a.selector.randomOrder ? shuffled(std::max<u32>(1, count), a.selector.seed) : std::vector<u32>{};
        for (usize i = 0; i < cps.size(); ++i) {
            u32 unit = unit_of(a.selector, units[i]);
            if (unit < perm.size()) unit = perm[unit];
            shift[i] += amt * selector_weight(a, ai, tracks, local, timeSec, unit, count);
        }
    }
    auto roll = [](u32 cp, i32 d, u32 lo, u32 n) { return lo + static_cast<u32>(((static_cast<i32>(cp - lo) + d) % static_cast<i32>(n) + static_cast<i32>(n)) % static_cast<i32>(n)); };
    std::string out;
    for (usize i = 0; i < cps.size(); ++i) {
        u32 cp = cps[i];
        const i32 d = static_cast<i32>(std::lround(shift[i]));
        if (d != 0) {
            if (cp >= 'A' && cp <= 'Z') cp = roll(cp, d, 'A', 26);
            else if (cp >= 'a' && cp <= 'z') cp = roll(cp, d, 'a', 26);
            else if (cp >= '0' && cp <= '9') cp = roll(cp, d, '0', 10);
        }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    return out;
}

// =============================================================================
// Presets (dados: animadores + keyframes)
// =============================================================================
const char* text_preset_name(u32 id) noexcept {
    static const char* names[kTextPresetCount] = {"Pop", "Bounce", "Slide", "Scale", "Fade", "Blur Reveal", "Word Highlight", "Karaoke",
        "Typewriter", "Wave", "Elastic", "Preset Bounce", "Preset Entrada suave", "Preset Revelar",
        "Preset Deslizar", "Preset Entrada rápida", "Preset Salto elástico",
        "Preset Salto por palavra", "Preset Movimento suave", "Onda de contorno", "Digitação reversa", "Palavras em sequência", "Cintilação", "Legenda suave", "Legenda luminosa", "Text Explosion", "Salto suave"};
    return id < kTextPresetCount ? names[id] : "";
}

namespace {
void key(TrackSet& tr, u32 a, u32 p, i64 f, f32 v, Interpolation in = Interpolation::EaseInOut) {
    tr.get_or_create(TrackProperty::TextAnimParam, a, p).set(FrameIndex{f}, v, in);
}
/// Revelação clássica: o início do seletor vai de 0 a 100 — o que já passou
/// fica normal, o resto recebe as propriedades do animador.
TextAnimator reveal(u8 basedOn, u8 shape, u32 props) {
    TextAnimator a;
    a.selector.basedOn = basedOn;
    a.selector.shape = shape;
    a.props = props;
    return a;
}
} // namespace

namespace {
/// Text Explosion: as letras chegam voando de uma explosão — cada uma com
/// direção, profundidade e giro PRÓPRIOS (seletores wiggly parados, um por
/// eixo, sementes diferentes), desfocadas, grandes e transparentes — e
/// montam o texto. Um "quantidade" de 100 → 0 em todos, com saída rápida.
bool apply_text_explosion(TextData& t, TrackSet& tr, i64 s, i64 d) {
    auto scatter = [&](u32 seed, Vec3 position, Vec3 rotation, const char* name) {
        TextAnimator a;
        a.name = name;
        a.selector.type = 1;          // wiggly
        a.selector.wiggleRate = 0;    // parado: um sorteio fixo por letra
        a.selector.seed = seed;
        a.props = kTextPropPosition | kTextPropRotation;
        a.position = position;
        a.rotation = rotation;
        t.animators.push_back(a);
        return static_cast<u32>(t.animators.size() - 1);
    };
    const u32 x = scatter(7, Vec3{900, 0, 0}, Vec3{0, 0, 540}, "Explosão X");
    const u32 y = scatter(23, Vec3{0, 650, 0}, Vec3{420, 0, 0}, "Explosão Y");
    const u32 z = scatter(41, Vec3{0, 0, -1200}, Vec3{0, 380, 0}, "Explosão Z");
    TextAnimator glow;
    glow.name = "Explosão brilho";
    glow.selector.shape = 0;
    glow.props = kTextPropOpacity | kTextPropBlur | kTextPropScale;
    glow.opacity = 0;
    glow.blur = 18;
    glow.scale = Vec2{240, 240};
    t.animators.push_back(glow);
    const u32 g = static_cast<u32>(t.animators.size() - 1);
    for (u32 a : {x, y, z, g}) {
        key(tr, a, kSelAmount, s, 100, Interpolation::EaseOut);
        key(tr, a, kSelAmount, s + d, 0, Interpolation::EaseOut);
    }
    return true;
}

/// 0.4.4 Salto suave: cada palavra cai de 50 px acima, aparecendo, e assenta
/// com um quique amortecido (seno que morre na exponencial), uma palavra
/// depois da outra. Um animador de posição (passa do lugar e volta) e um de
/// opacidade (só sobe), ambos por palavra com a quantidade por expressão —
/// como o seletor de expressão do After Effects.
bool apply_smooth_bounce(TextData& t, TrackSet& tr, i64 s, f64 fps) {
    const std::string at = std::to_string(static_cast<double>(s) / fps);
    auto expression = [&](u32 ai, const char* source) {
        const std::string program = "if (localTime < " + at + ") { 0; } else { var presetTime = localTime - " + at + "; " + source + " }";
        auto& track = tr.get_or_create(TrackProperty::TextAnimParam, ai, kSelAmount);
        track.expression = expr::compile(program);
        track.expressionEnabled = true;
    };
    TextAnimator fall = reveal(1, 0, kTextPropPosition);
    fall.name = "Salto suave queda";
    fall.position = Vec3{0, -50, 0};
    t.animators.push_back(fall);
    TextAnimator fade = reveal(1, 0, kTextPropOpacity);
    fade.name = "Salto suave entrada";
    fade.opacity = 0;
    t.animators.push_back(fade);
    expression(0, "delay = 0.08; dur = 0.12; freq = 3; decay = 6;\n"
                  "t = presetTime - (textIndex - 1) * delay;\n"
                  "if (t < 0) { 100 } else { if (t < dur) { linear(t, 0, dur, 100, 0) } else {\n"
                  "  w = freq * Math.PI * 2;\n"
                  "  (-100 / dur) * Math.sin((t - dur) * w) / Math.exp(decay * (t - dur)) / w } }");
    expression(1, "t = presetTime - (textIndex - 1) * 0.08;\n"
                  "if (t < 0) { 100 } else { linear(t, 0, 0.12, 100, 0) }");
    return true;
}
} // namespace

#include "JuanTextPresets.inc"
#include "PackTextPresets.inc"

bool apply_text_preset(u32 id, TextData& t, TrackSet& tr, i64 s, i64 d, f64 fps) {
    if (id >= kTextPresetCount) return false;
    d = std::max<i64>(2, d);
    tr.remove_if([](const Track& x) { return x.property == TrackProperty::TextAnimParam; });
    t.animators.clear();
    if (id == 26) return apply_smooth_bounce(t, tr, s, fps > 0 ? fps : 30.0);
    if (id == 25) return apply_text_explosion(t, tr, s, std::max<i64>(d * 6 / 5, 2));
    if (id >= 19) return apply_pack_text(id, t, tr, s, d);
    if (id >= 11) return apply_juan_text(id, t, tr, s, fps > 0 ? fps : 30.0);
    u32 words = 0;
    {
        bool in = false;
        for (char c : t.content) {
            const bool sp = c == ' ' || c == '\n' || c == '\t';
            if (!sp && !in) { in = true; ++words; }
            if (sp) in = false;
        }
        words = std::max<u32>(1, words);
    }
    auto push = [&](TextAnimator a, const char* name) { a.name = name; t.animators.push_back(a); return static_cast<u32>(t.animators.size() - 1); };
    switch (id) {
        case 0: {   // Pop: letras nascem do zero
            TextAnimator a = reveal(0, 1, kTextPropScale | kTextPropOpacity);
            a.scale = Vec2{0, 0};
            a.opacity = 0;
            const u32 i = push(a, "Pop");
            key(tr, i, kSelStart, s, 0);
            key(tr, i, kSelStart, s + d, 100);
            break;
        }
        case 1: {   // Bounce: um "pulo" que atravessa o texto + revelação
            TextAnimator up = reveal(0, 4, kTextPropPosition);
            up.position = Vec3{0, -40, 0};
            up.selector.end = 25;
            const u32 i = push(up, "Bounce pulo");
            key(tr, i, kSelOffset, s, -25, Interpolation::Linear);
            key(tr, i, kSelOffset, s + d, 100, Interpolation::Linear);
            TextAnimator op = reveal(0, 1, kTextPropOpacity | kTextPropPosition);
            op.opacity = 0;
            op.position = Vec3{0, 30, 0};
            const u32 j = push(op, "Bounce entrada");
            key(tr, j, kSelStart, s, 0);
            key(tr, j, kSelStart, s + d, 100);
            break;
        }
        case 2: {   // Slide
            TextAnimator a = reveal(0, 1, kTextPropPosition | kTextPropOpacity);
            a.position = Vec3{120, 0, 0};
            a.opacity = 0;
            const u32 i = push(a, "Slide");
            key(tr, i, kSelStart, s, 0);
            key(tr, i, kSelStart, s + d, 100);
            break;
        }
        case 3: {   // Scale (por palavra)
            TextAnimator a = reveal(1, 1, kTextPropScale | kTextPropOpacity);
            a.scale = Vec2{300, 300};
            a.opacity = 0;
            const u32 i = push(a, "Scale");
            key(tr, i, kSelStart, s, 0);
            key(tr, i, kSelStart, s + d, 100);
            break;
        }
        case 4: {   // Fade
            TextAnimator a = reveal(0, 1, kTextPropOpacity);
            a.opacity = 0;
            const u32 i = push(a, "Fade");
            key(tr, i, kSelStart, s, 0);
            key(tr, i, kSelStart, s + d, 100);
            break;
        }
        case 5: {   // Blur Reveal
            TextAnimator a = reveal(0, 1, kTextPropOpacity | kTextPropBlur);
            a.opacity = 0;
            a.blur = 14;
            const u32 i = push(a, "Blur Reveal");
            key(tr, i, kSelStart, s, 0);
            key(tr, i, kSelStart, s + d, 100);
            break;
        }
        case 6: {   // Word Highlight: janela de UMA palavra que anda
            TextAnimator a = reveal(1, 0, kTextPropFill | kTextPropScale);
            a.fill = Vec4{1.0f, 0.85f, 0.1f, 1};
            a.scale = Vec2{112, 112};
            a.selector.start = 0;
            a.selector.end = 100.0f / static_cast<f32>(words);
            const u32 i = push(a, "Word Highlight");
            key(tr, i, kSelOffset, s, 0, Interpolation::Hold);
            for (u32 w = 1; w < words; ++w) key(tr, i, kSelOffset, s + d * w / words, 100.0f * static_cast<f32>(w) / static_cast<f32>(words), Interpolation::Hold);
            break;
        }
        case 7: {   // Karaoke: o que já foi cantado muda de cor
            TextAnimator a = reveal(0, 0, kTextPropFill);
            a.fill = Vec4{0.2f, 0.85f, 1.0f, 1};
            a.selector.start = 0;
            a.selector.end = 0;
            const u32 i = push(a, "Karaoke");
            key(tr, i, kSelEnd, s, 0, Interpolation::Linear);
            key(tr, i, kSelEnd, s + d, 100, Interpolation::Linear);
            break;
        }
        case 8: {   // Typewriter: letra por letra, sem transição
            TextAnimator a = reveal(0, 0, kTextPropOpacity);
            a.opacity = 0;
            const u32 i = push(a, "Typewriter");
            // Use the same UTF-8 code-point units as text layout.
            u32 count = 0;
            for (usize byte = 0; byte < t.content.size(); ++count) {
                const u8 lead = static_cast<u8>(t.content[byte]);
                usize width = 1;
                if ((lead >> 5) == 0x6 && byte + 1 < t.content.size()) width = 2;
                else if ((lead >> 4) == 0xE && byte + 2 < t.content.size()) width = 3;
                else if ((lead >> 3) == 0x1E && byte + 3 < t.content.size()) width = 4;
                byte += width;
            }
            const u32 n = std::max<u32>(1, count);
            // Round up so grouped reveals never overwrite the hidden first frame.
            for (u32 c = 0; c <= n; ++c) key(tr, i, kSelStart, s + (d * c + n - 1) / n,
                100.0f * static_cast<f32>(c) / static_cast<f32>(n), Interpolation::Hold);
            break;
        }
        case 9: {   // Wave: ondinha que passa pelas letras, em ciclos de 1 s
            TextAnimator a = reveal(0, 4, kTextPropPosition);
            a.position = Vec3{0, -28, 0};
            a.selector.end = 30;
            const u32 i = push(a, "Wave");
            const i64 period = std::max<i64>(2, static_cast<i64>(std::lround(fps)));
            for (i64 c = s; c < s + std::max<i64>(d, period); c += period) {
                key(tr, i, kSelOffset, c, -30, Interpolation::Linear);
                key(tr, i, kSelOffset, c + period - 1, 100, Interpolation::Hold);
            }
            break;
        }
        case 10: {  // Elastic: nasce e passa do ponto (lombada logo atrás da revelação)
            TextAnimator a = reveal(0, 1, kTextPropScale | kTextPropOpacity);
            a.scale = Vec2{0, 0};
            a.opacity = 0;
            const u32 i = push(a, "Elastic entrada");
            key(tr, i, kSelStart, s, 0);
            key(tr, i, kSelStart, s + d, 100);
            TextAnimator b = reveal(0, 3, kTextPropScale);
            b.scale = Vec2{135, 135};
            b.selector.end = 20;
            const u32 j = push(b, "Elastic sobra");
            key(tr, j, kSelOffset, s, -20);
            key(tr, j, kSelOffset, s + d, 100);
            break;
        }
        default: return false;
    }
    return true;
}

} // namespace aurea::text
