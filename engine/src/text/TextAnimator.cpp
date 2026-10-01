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
std::array<ParamValue, aeCount> animator_values(const Layer& layer, const EffectInstance& fx, f64 time) {
    static const ParameterRegistry specs = [] { ParameterRegistry p; declare_animator_effect_params(p); return p; }();
    std::array<ParamValue, aeCount> values{};
    const auto frame = static_cast<i64>(std::floor(time));
    const f32 blend = static_cast<f32>(time - frame);
    for (u32 p = 0; p < aeCount; ++p) {
        values[p] = evaluate_param(layer.tracks, fx, p, specs.at(p), FrameIndex{frame});
        if (blend > 0 && specs.at(p).animatable()) {
            const auto next = evaluate_param(layer.tracks, fx, p, specs.at(p), FrameIndex{frame + 1});
            for (u32 c = 0; c < component_count(specs.at(p).type); ++c) values[p].v[c] += (next.v[c] - values[p].v[c]) * blend;
        }
    }
    return values;
}
}

bool has_animator_effect(const Layer& layer) noexcept {
    for (const auto& fx : layer.effects) if (fx.enabled && fx.type == effect_type_id(kAnimatorEffect)) return true;
    return false;
}

f32 animator_effect_padding(const Layer& layer, f64 time, f32 size, u32 chars) {
    f32 pad = 0;
    for (const auto& fx : layer.effects) if (fx.enabled && fx.type == effect_type_id(kAnimatorEffect)) {
        const auto v = animator_values(layer, fx, time);
        pad += std::max({std::fabs(v[aePosition].v[0]), std::fabs(v[aePosition].v[1]), std::fabs(v[aePosition].v[2])});
        pad += size * std::max(0.f, std::max(v[aeScale].v[0], v[aeScale].v[1]) / 100 - 1) + v[aeBlur].v[0] * 2 + v[aeStrokeWidth].v[0];
        if (v[aeRotation].v[0] || v[aeRotation].v[1] || v[aeRotation].v[2] || v[aeSkew].v[0]) pad += size;
        pad += std::fabs(v[aeTracking].v[0]) * std::min(chars, 200u);
    }
    return std::min(pad, 4000.f);
}

void evaluate_animator_effects(const Layer& layer, f64 time, f64 fps, const std::vector<GlyphUnits>& units,
                              u32 chars, u32 words, u32 lines, std::vector<GlyphAnim>& out) {
    if (out.size() != units.size()) out.resize(units.size());
    for (const auto& fx : layer.effects) if (fx.enabled && fx.type == effect_type_id(kAnimatorEffect)) {
        const auto v = animator_values(layer, fx, time);
        TextData t; t.size = layer.text.size;
        TextAnimator a;
        a.selector.start = v[aeStart].v[0]; a.selector.end = v[aeEnd].v[0]; a.selector.offset = v[aeOffset].v[0];
        a.selector.amount = v[aeAmount].v[0]; a.selector.basedOn = v[aeUnit].as_enum(); a.selector.shape = v[aeShape].as_enum();
        a.selector.easeHigh = v[aeEaseHigh].v[0]; a.selector.easeLow = v[aeEaseLow].v[0];
        a.selector.randomOrder = v[aeRandom].as_bool(); a.selector.seed = static_cast<u32>(v[aeSeed].v[0]);
        a.selector.type = v[aeSelector].as_enum(); a.selector.wiggleRate = v[aeWiggleRate].v[0];
        a.props = kTextPropPosition | kTextPropScale | kTextPropRotation | kTextPropOpacity | kTextPropBlur |
                  kTextPropTracking | kTextPropSkew | kTextPropStrokeWidth;
        a.position = v[aePosition].as_vec3(); a.scale = v[aeScale].as_vec2(); a.rotation = v[aeRotation].as_vec3();
        a.opacity = v[aeOpacity].v[0]; a.blur = v[aeBlur].v[0]; a.tracking = v[aeTracking].v[0];
        a.skew = v[aeSkew].v[0]; a.strokeWidth = v[aeStrokeWidth].v[0];
        a.fill = v[aeFill].as_color(); a.stroke = v[aeStroke].as_color();
        if (v[aeFillOn].as_bool()) a.props |= kTextPropFill;
        if (v[aeStrokeOn].as_bool()) a.props |= kTextPropStroke;
        t.animators.push_back(a);
        std::vector<GlyphAnim> addition;
        evaluate_text_animators(t, TrackSet{}, time, fps, units, chars, words, lines, addition);
        for (usize i = 0; i < out.size(); ++i) {
            auto& o = out[i]; const auto& b = addition[i];
            o.translate = o.translate + b.translate; o.rotation = o.rotation + b.rotation;
            o.scale.x *= b.scale.x; o.scale.y *= b.scale.y; o.opacity *= b.opacity;
            o.blur += b.blur; o.trackingShift += b.trackingShift; o.strokeAdd += b.strokeAdd;
            o.skewTransform = o.skewTransform * b.skewTransform;
            if (b.fill.w > 0) {
                const f32 w = b.fill.w, prev = o.fill.w * (1-w), total = w + prev;
                o.fill = {(b.fill.x*w+o.fill.x*prev)/total, (b.fill.y*w+o.fill.y*prev)/total, (b.fill.z*w+o.fill.z*prev)/total, total};
                o.fillOpacity += (std::clamp(a.fill.w, 0.f, 1.f) - o.fillOpacity) * w;
            }
            if (b.stroke.w > 0) o.stroke = b.stroke;
        }
    }
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
    const f64 f = std::floor(local);
    const f32 a = tr->value_or(FrameIndex{static_cast<i64>(f)}, fallback);
    const f32 k = static_cast<f32>(local - f);
    if (k <= 0.0f) return a;
    return a + (tr->value_or(FrameIndex{static_cast<i64>(f) + 1}, fallback) - a) * k;
}

bool has_animators(const TextData& t) noexcept {
    for (const TextAnimator& a : t.animators) if (a.enabled) return true;
    return false;
}

f32 selector_weight(const TextAnimator& a, u32 ai, const TrackSet& tracks, f64 local, f64 timeSec, u32 unit, u32 count) noexcept {
    expr::TextScope glyph(unit + 1, count);
    const TextSelector& s = a.selector;
    const f32 amount = anim_param(tracks, ai, kSelAmount, local, s.amount) / 100.0f;
    if (s.type == 1) {
        const f32 rate = std::max(0.0f, anim_param(tracks, ai, kWiggleRate, local, s.wiggleRate));
        return std::clamp(wiggle(s.seed, unit, timeSec * rate), -1.0f, 1.0f) * amount;
    }
    const f32 n = static_cast<f32>(std::max<u32>(1, count));
    const f32 off = anim_param(tracks, ai, kSelOffset, local, s.offset);
    f32 st = anim_param(tracks, ai, kSelStart, local, s.start) + off;
    f32 en = anim_param(tracks, ai, kSelEnd, local, s.end) + off;
    if (st > en) std::swap(st, en);
    const f32 u0 = static_cast<f32>(unit) / n * 100.0f, u1 = static_cast<f32>(unit + 1) / n * 100.0f;
    f32 w = 0.0f;
    if (s.shape == 0) {
        // Quadrado: fração da unidade dentro do intervalo (revelação suave).
        const f32 ov = std::max(0.0f, std::min(u1, en) - std::max(u0, st));
        w = ov / (u1 - u0);
    } else {
        const f32 c = (u0 + u1) * 0.5f;
        if (en > st && ((c >= st && c <= en) || (s.type == 2 && (s.shape == 1 || s.shape == 2))))
            w = shape_value(s.shape, (c - st) / (en - st));
    }
    const f32 eh = anim_param(tracks, ai, kSelEaseHigh, local, s.easeHigh), el = anim_param(tracks, ai, kSelEaseLow, local, s.easeLow);
    if (eh > 0.0f || el > 0.0f) {
        // Ease alto/baixo: mais tempo perto de 1 (alto) ou de 0 (baixo).
        const f32 smooth = w * w * (3.0f - 2.0f * w);
        const f32 high = 1.0f - (1.0f - w) * (1.0f - w), low = w * w;
        w = w + std::clamp(eh / 100.0f, 0.0f, 1.0f) * (high - w) * 0.5f + std::clamp(el / 100.0f, 0.0f, 1.0f) * (low - w) * 0.5f
          + 0.0f * smooth;
    }
    return std::clamp(w, 0.0f, 1.0f) * amount;
}

void evaluate_text_animators(const TextData& t, const TrackSet& tracks, f64 local, f64 fps, const std::vector<GlyphUnits>& units, u32 chars,
                             u32 words, u32 lines, std::vector<GlyphAnim>& out) {
    out.assign(units.size(), GlyphAnim{});
    if (!has_animators(t)) return;
    const f64 timeSec = local / (fps > 0.0 ? fps : 30.0);
    for (u32 ai = 0; ai < t.animators.size(); ++ai) {
        const TextAnimator& a = t.animators[ai];
        if (!a.enabled) continue;
        const TextSelector& s = a.selector;
        const u32 count = s.basedOn == 1 ? words : s.basedOn == 2 ? lines : chars;
        const std::vector<u32> perm = s.randomOrder ? shuffled(std::max<u32>(1, count), s.seed) : std::vector<u32>{};
        auto P = [&](u32 p, f32 fb) { return anim_param(tracks, ai, p, local, fb); };
        const Vec3 pos{P(kPosX, a.position.x), P(kPosY, a.position.y), P(kPosZ, a.position.z)};
        const Vec2 scl{P(kScaleX, a.scale.x), P(kScaleY, a.scale.y)};
        const Vec3 rot{P(kRotX, a.rotation.x), P(kRotY, a.rotation.y), P(kRotZ, a.rotation.z)};
        const f32 op = P(kOpacity, a.opacity), blur = P(kBlur, a.blur), skew = P(kSkew, a.skew);
        const f32 trk = P(kTracking, a.tracking) * (P(kTrackingEm, 0) > 0.5f ? t.size / 1000.0f : 1.0f);
        const f32 skewAxis = P(kSkewAxis, 0) * kDeg2Rad;
        const u32 anchorGrouping = static_cast<u32>(std::clamp(P(kAnchorGrouping, 0), 0.0f, 2.0f));
        const f32 sw = P(kStrokeWidth, a.strokeWidth);
        const Vec3 fill{P(kFillR, a.fill.x), P(kFillG, a.fill.y), P(kFillB, a.fill.z)};
        const Vec3 stroke{P(kStrokeR, a.stroke.x), P(kStrokeG, a.stroke.y), P(kStrokeB, a.stroke.z)};
        std::vector<f32> weight(units.size(), 0.0f);
        for (usize g = 0; g < units.size(); ++g) {
            u32 unit = unit_of(s, units[g]);
            if (!perm.empty() && unit < perm.size()) unit = perm[unit];
            weight[g] = selector_weight(a, ai, tracks, local, timeSec, unit, count);
        }
        for (usize g = 0; g < units.size(); ++g) {
            const f32 w = weight[g];
            if (w == 0.0f) continue;
            GlyphAnim& o = out[g];
            o.anchorGrouping = anchorGrouping;
            if (a.props & kTextPropPosition) o.translate = o.translate + pos * w;
            if (a.props & kTextPropScale) {
                o.scale.x *= std::max(0.0f, 1.0f + w * (scl.x / 100.0f - 1.0f));
                o.scale.y *= std::max(0.0f, 1.0f + w * (scl.y / 100.0f - 1.0f));
            }
            if (a.props & kTextPropRotation) o.rotation = o.rotation + rot * w;
            if (a.props & kTextPropOpacity) o.opacity *= std::max(0.0f, 1.0f + w * (op / 100.0f - 1.0f));
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
            if (a.props & kTextPropFill) o.fill = Vec4{fill.x, fill.y, fill.z, std::clamp(std::fabs(w), 0.0f, 1.0f)};
            if (a.props & kTextPropStroke) o.stroke = Vec4{stroke.x, stroke.y, stroke.z, std::clamp(std::fabs(w), 0.0f, 1.0f)};
        }
        if (a.props & kTextPropTracking) {
            // Tracking empurra os glifos seguintes da mesma linha (ordem visual).
            std::vector<f32> acc(std::max<u32>(1, lines) + 1, 0.0f);
            for (usize g = 0; g < units.size(); ++g) {
                const u32 li = std::min<u32>(units[g].lineIndex, static_cast<u32>(acc.size() - 1));
                out[g].trackingShift += acc[li];
                acc[li] += weight[g] * trk;
            }
        }
    }
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
        "Preset Salto por palavra", "Preset Movimento suave", "Onda de contorno", "Digitação reversa", "Palavras em sequência", "Cintilação", "Legenda suave", "Legenda luminosa"};
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

#include "JuanTextPresets.inc"
#include "PackTextPresets.inc"

bool apply_text_preset(u32 id, TextData& t, TrackSet& tr, i64 s, i64 d, f64 fps) {
    if (id >= kTextPresetCount) return false;
    d = std::max<i64>(2, d);
    tr.remove_if([](const Track& x) { return x.property == TrackProperty::TextAnimParam; });
    t.animators.clear();
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
