#include "aurea/text/TextTransform.hpp"
#include "aurea/timeline/Layer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace aurea::text {
void declare_transform_params(ParameterRegistry& p) {
    constexpr u16 percent = kParamAnimatable | kParamPercent;
    p.add_float("start", "Start", 0, 0, 100, percent, "%");
    p.add_float("end", "End", 100, 0, 100, percent, "%");
    p.add_float("phase", "Phase", 0, -100, 200, percent, "%");
    static const char* components[] = {"Letter", "Word", "Line"};
    p.add_enum("component", "Component", components, 3, 0);
    static const char* anchors[] = {"Layer", "Component"};
    p.add_enum("anchor", "Anchor", anchors, 2, 0);
    p.add_point2("offset", "Offset", {}, -2000, 2000, kParamAnimatable | kParamPixels); p.typed_range(-20000, 20000);
    p.add_angle("angle", "Angle", 0, -1800, 1800);
    p.add_float("scale", "Scale", 0, -100, 1000, percent, "%");
    p.add_float("stretch", "Stretch", 0, -100, 1000, percent, "%");
    p.add_float("alpha", "Opacity change", 0, -100, 100, percent, "%");
    p.add_bool("override_fill", "Override fill color", false);
    p.add_color("fill_color", "Fill color", Vec4{0, 1, 0, 1});
    p.add_float("ease_in", "Ease in", 0, -100, 100, percent, "%");
    p.add_float("ease_out", "Ease out", 0, -100, 100, percent, "%");
    p.add_float("overlap", "Overlap", 0, 0, 1000, percent, "%");
    static const char* shapes[] = {"Square", "Smooth", "Triangle"};
    p.add_enum("shape", "Shape", shapes, 3, 0);
    p.add_bool("random_order", "Random order", false);
    p.add_float("seed", "Seed", 0, 0, 5, kParamAnimatable);
    // As 5 seções do Motion, todas fechadas: abre-se só o grupo que vai usar.
    p.add_section_range("range", "Range", 0, 2, kInvalidIndex, false);
    p.add_section_range("transform", "Transform", 3, 9, kInvalidIndex, false);
    p.add_section_range("visual", "Visual attributes", 10, 11, kInvalidIndex, false);
    p.add_section_range("interpolation", "Interpolation and easing", 12, 15, kInvalidIndex, false);
    p.add_section_range("random", "Random order", 16, 17, 16, false);
}

namespace {
u32 random(u32& state) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
}

f32 transform_influence(f32 start, f32 end, f32 phase, f32 overlap, u32 shape, f32 easeIn, f32 easeOut, u32 rank, u32 count) noexcept {
    if (count == 0) return 0;
    // Cada unidade ocupa uma janela de largura `reach` em `slots` janelas: a
    // primeira só entra por inteiro depois de `reach`, e a última sai antes do fim.
    const f32 reach = 1.f + std::max(0.f, overlap);
    const f32 slots = static_cast<f32>(count) + 2.f * reach - 1.f;
    const f32 width = reach / slots;
    const f32 at = (static_cast<f32>(rank) + reach) / slots + phase;
    const f32 middle = at + width * .5f;
    f32 weight = 0;
    if (shape == 0) {
        const f32 rise = std::clamp((at - start) / width, 0.f, 1.f);
        const f32 fall = std::clamp((end - at) / width, 0.f, 1.f);
        // Faixa invertida (início depois do fim): o efeito vale FORA dela.
        weight = start < end ? std::min(rise, fall) : 1.f - std::max(rise, fall);
    } else if (end > start && middle >= start && middle <= end) {
        const f32 u = (middle - start) / (end - start);
        if (shape == 1) {
            const f32 x = (u - .5f) * 6.28318530718f;
            weight = std::exp(-x * x * .5f);
        } else {
            weight = u < .5f ? u * 2.f : (1.f - u) * 2.f;
        }
    }
    if (easeIn != 0 || easeOut != 0) {
        weight = cubic_bezier(std::max(easeIn * .5f, 0.f), std::max(-easeIn * .5f, 0.f),
                              1.f - std::max(easeOut * .5f, 0.f), 1.f - std::max(-easeOut * .5f, 0.f), weight);
    }
    return std::clamp(weight, 0.f, 1.f);
}

f32 rig_padding(const Layer& layer, f64 time, f32 extent) {
    f32 extra = 0;
    for (u32 ai = 0; ai < layer.text.animators.size(); ++ai) {
        const TextAnimator& a = layer.text.animators[ai];
        if (!a.enabled || a.rig == 0) continue;
        extra += std::max(std::fabs(anim_param(layer.tracks, ai, kPosX, time, a.position.x)), std::fabs(anim_param(layer.tracks, ai, kPosY, time, a.position.y)));
        extra += extent * (std::max(0.f, anim_param(layer.tracks, ai, kScaleX, time, a.scale.x) * .01f) + std::max(0.f, anim_param(layer.tracks, ai, kScaleY, time, a.scale.y) * .01f));
        if (anim_param(layer.tracks, ai, kRotZ, time, a.rotation.z) != 0) extra += extent;
    }
    return std::min(extra, 4000.f);
}

void evaluate_text_rigs(const Layer& layer, f64 time, const TextLayout& layout, std::vector<GlyphAnim>& styles,
                        std::vector<Mat4>& matrices, const Vec3* layerPivot) {
    matrices.assign(layout.quads.size(), Mat4::identity());
    if (styles.size() != layout.quads.size()) styles.resize(layout.quads.size());
    for (u32 ai = 0; ai < layer.text.animators.size(); ++ai) {
        const TextAnimator& a = layer.text.animators[ai];
        if (!a.enabled || a.rig == 0) continue;
        const TextSelector& s = a.selector;
        const auto P = [&](u32 key, f32 fallback) { return anim_param(layer.tracks, ai, key, time, fallback); };
        const u32 component = s.basedOn;
        const auto unit = [component](const GlyphQuad& q) { return component == 1 ? q.wordIndex : component == 2 ? q.lineIndex : q.charIndex; };
        // Só contam as unidades com tinta: espaço e quebra de linha não ocupam janela.
        u32 limit = 0;
        for (const auto& q : layout.quads) limit = std::max(limit, unit(q) + 1);
        if (!limit) continue;
        std::vector<u32> dense(limit, kInvalidIndex);
        u32 count = 0;
        for (const auto& q : layout.quads) if (dense[unit(q)] == kInvalidIndex) dense[unit(q)] = count++;
        std::vector<u32> ranks(count);
        std::iota(ranks.begin(), ranks.end(), 0);
        if (s.randomOrder) {
            u32 seed = s.seed + 0x9e3779b9u;
            for (u32 i = count; i > 1; --i) std::swap(ranks[i - 1], ranks[random(seed) % i]);
        }
        std::vector<Vec4> bounds(count, Vec4{1e30f, 1e30f, -1e30f, -1e30f});
        for (const auto& q : layout.quads) {
            auto& b = bounds[dense[unit(q)]];
            b.x = std::min(b.x, q.x0); b.y = std::min(b.y, q.y0); b.z = std::max(b.z, q.x1); b.w = std::max(b.w, q.y1);
        }
        const f32 start = P(kSelStart, s.start) * .01f, end = P(kSelEnd, s.end) * .01f, phase = P(kSelOffset, s.offset) * .01f;
        const f32 overlap = P(kAnimOverlap, s.overlap) * .01f, easeIn = P(kSelEaseHigh, s.easeHigh) * .01f, easeOut = P(kSelEaseLow, s.easeLow) * .01f;
        const f32 alpha = P(kOpacity, a.opacity) * .01f, scaleAmount = P(kScaleX, a.scale.x) * .01f, stretchAmount = P(kScaleY, a.scale.y) * .01f;
        const Vec2 offset{P(kPosX, a.position.x), P(kPosY, a.position.y)};
        const f32 angle = P(kRotZ, a.rotation.z) * kDeg2Rad;
        const Vec4 color{P(kFillR, a.fill.x), P(kFillG, a.fill.y), P(kFillB, a.fill.z), P(kAnimFillA, a.fill.w)};
        for (usize g = 0; g < layout.quads.size(); ++g) {
            const u32 index = dense[unit(layout.quads[g])];
            const f32 weight = transform_influence(start, end, phase, overlap, s.shape, easeIn, easeOut, ranks[index], count);
            // Opacidade: negativa apaga o que está na faixa; positiva apaga o que está FORA dela.
            styles[g].opacity *= std::clamp(alpha < 0 ? 1.f + alpha * weight : 1.f - (1.f - weight) * alpha, 0.f, 1.f);
            if (weight == 0) continue;
            const auto b = bounds[index];
            const f32 margin = layer.text.strokeWidth > 0 ? layer.text.strokeWidth + 2 : 2;
            Vec3 layerAnchor = layer.transform.anchor;
            for (u32 axis = 0; axis < 3; ++axis) {
                if (const auto* track = layer.tracks.find(static_cast<TrackProperty>(static_cast<u16>(TrackProperty::AnchorX) + axis))) {
                    f32& base = axis == 0 ? layerAnchor.x : axis == 1 ? layerAnchor.y : layerAnchor.z;
                    base = track->value_or_f(time, base);
                }
            }
            layerAnchor.x += layout.pad - margin;
            layerAnchor.y += layout.pad - margin;
            if (layerPivot) layerAnchor = *layerPivot;
            const Vec3 pivot = a.rigAnchor == 1 ? Vec3{(b.x + b.z) / 2, (b.y + b.w) / 2, layerPivot ? layerPivot->z : 0} : layerAnchor;
            const f32 scale = 1.f + scaleAmount * weight;
            const Vec3 stretch{scale, scale + stretchAmount * weight, layerPivot ? scale : 1};
            const auto transform = Mat4::translation(pivot + Vec3{offset.x * weight, offset.y * weight, 0})
                * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, angle * weight))
                * Mat4::scale(stretch) * Mat4::translation(-pivot);
            matrices[g] = transform * matrices[g];
            auto& style = styles[g];
            if (a.props & kTextPropFill) {
                const f32 w = std::clamp(weight, 0.f, 1.f), previous = style.fill.w * (1.f - w), total = w + previous;
                style.fillOpacity += (std::clamp(color.w, 0.f, 1.f) - style.fillOpacity) * w;
                style.fill = total > 0 ? Vec4{(color.x * w + style.fill.x * previous) / total, (color.y * w + style.fill.y * previous) / total,
                                              (color.z * w + style.fill.z * previous) / total, total} : Vec4{};
            }
        }
    }
}

// =============================================================================
// Efeitos de conteúdo (0.5.0)
// =============================================================================
void declare_progress_params(ParameterRegistry& p) {
    constexpr u16 percent = kParamAnimatable | kParamPercent;
    p.add_float("start", "Start", 0, 0, 100, percent, "%");
    p.add_float("end", "End", 100, 0, 100, percent, "%");
    static const char* cursors[] = {"None", "_", "\xE2\x96\x88", "\xE2\x96\x8C", "\xE2\x96\x81", "\xE2\x96\x8F", "\xE2\x96\x95", "\xE2\x96\xAF", "\xE2\x96\x8E"};
    p.add_enum("cursor", "Cursor", cursors, kProgressCursorCount, 0);
    p.add_bool("blink", "Piscar cursor", false);
}

void declare_randomizer_params(ParameterRegistry& p) {
    constexpr u16 percent = kParamAnimatable | kParamPercent;
    p.add_float("amount", "Amount", 100, 0, 100, percent, "%");
    p.add_float("evolution", "Evolução", 0, -1000, 1000, kParamAnimatable);
    p.add_float("seed", "Seed", 0, 0, 1000, kParamAnimatable);
    p.add_float("start", "Start", 0, 0, 100, percent, "%");
    p.add_float("end", "End", 100, 0, 100, percent, "%");
    static const char* sets[] = {"Maiúsculas", "Minúsculas", "Maiúsculas e minúsculas", "Dígitos", "Dígitos e letras", "Texto da camada", "Asterisco", "Marcador",
                                 "Sublinhado", "Em branco", "Losango", "Traço", "Símbolos", "Dígitos, letras e símbolos", "Estendido"};
    p.add_enum("charset", "Caracteres", sets, kRandomizerCharsetCount, 0);
    p.add_bool("preserve_space", "Manter espaços", true);
}

void declare_spacing_params(ParameterRegistry& p) {
    constexpr u16 percent = kParamAnimatable | kParamPercent;
    p.add_float("letter", "Espaço entre letras", 0, -1000, 1000, percent, "%");
    p.add_float("line", "Espaço entre linhas", 100, 0, 1000, percent, "%");
}

namespace {
std::vector<std::string> split_code_points(const std::string& s) {
    std::vector<std::string> out;
    out.reserve(s.size());
    for (usize i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        usize n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        n = std::min(n, s.size() - i);
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

u32 mix3(u32 a, u32 b, u32 c) noexcept {
    u32 h = 0x811C9DC5u;
    for (u32 v : {a, b, c}) { h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2); h *= 0x01000193u; h ^= h >> 13; }
    return h;
}

/// 0..1, suave em `x` e fixo por `index`: o mesmo caractere muda aos poucos.
f32 smooth_random(u32 index, u32 channel, f32 x) noexcept {
    const f32 base = std::floor(x);
    const f32 t = x - base, u = t * t * (3.f - 2.f * t);
    const u32 k = static_cast<u32>(static_cast<i32>(base));
    const f32 a = static_cast<f32>(mix3(index, channel, k) & 0xFFFFFFu) / 16777215.f;
    const f32 b = static_cast<f32>(mix3(index, channel, k + 1) & 0xFFFFFFu) / 16777215.f;
    return a + (b - a) * u;
}

const char* charset_of(u32 id) noexcept {
    switch (id) {
        case 0: return "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        case 1: return "abcdefghijklmnopqrstuvwxyz";
        case 2: return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        case 3: return "0123456789";
        case 4: return "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        case 6: return "*";
        case 7: return "\xE2\x80\xA2";
        case 8: return "_";
        case 9: return " ";
        case 10: return "\xE2\x97\x8A";
        case 11: return "-";
        case 12: return "+=-!@#$%^*()[]{}:;/,.<>~`";
        case 13: return "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+=-!@#$%^*()[]{}:;/,.<>~`";
        case 14: return "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+=-!@#$%^*()[]{}:;/,.<>~`\xCE\xA9\xC2\xB5\xCE\xB4\xCE\xB3\xCE\xB2\xCF\x80\xCE\xBB";
        default: return "";
    }
}

const char* cursor_of(u32 id) noexcept {
    static const char* cursors[kProgressCursorCount] = {"", "_", "\xE2\x96\x88", "\xE2\x96\x8C", "\xE2\x96\x81", "\xE2\x96\x8F", "\xE2\x96\x95", "\xE2\x96\xAF", "\xE2\x96\x8E"};
    return id < kProgressCursorCount ? cursors[id] : "";
}

usize cut_at(f32 fraction, usize size) noexcept {
    return std::min(size, static_cast<usize>(std::lround(std::clamp(fraction, 0.f, 1.f) * static_cast<f32>(size))));
}
} // namespace

std::string progress_text(const std::string& content, f32 start, f32 end, u32 cursor, bool showCursor) {
    const auto cps = split_code_points(content);
    const usize a = cut_at(start, cps.size()), b = cut_at(end, cps.size());
    std::string out;
    for (usize i = a; i < b; ++i) out += cps[i];
    if (showCursor) out += cursor_of(cursor);
    return out;
}

std::string randomize_text(const std::string& content, f32 amount, f32 evolution, f32 seed, f32 start, f32 end, u32 charset, bool preserveSpace) {
    const auto cps = split_code_points(content);
    const usize a = cut_at(start, cps.size()), b = cut_at(end, cps.size());
    const auto pool = charset == 5 ? cps : split_code_points(charset_of(charset));
    std::string out;
    for (usize i = 0; i < cps.size(); ++i) {
        const bool inRange = i >= a && i < b;
        const u32 order = static_cast<u32>(i >= a ? i - a : i);
        const bool keep = !inRange || pool.empty() || smooth_random(order, 1, seed) >= amount || (preserveSpace && cps[i] == " ");
        // Quebra de linha nunca vira letra: mudaria o número de linhas do layout.
        if (keep || cps[i] == "\n") { out += cps[i]; continue; }
        const f32 pick = smooth_random(order, 2, evolution + .5f) * 92.1734f * static_cast<f32>(pool.size());
        out += pool[std::min(pool.size() - 1, static_cast<usize>(std::fmod(pick, static_cast<f32>(pool.size()))))];
    }
    return out;
}

bool is_content_effect(u32 type) noexcept {
    return type == effect_type_id(kProgressEffect) || type == effect_type_id(kRandomizerEffect) || type == effect_type_id(kSpacingEffect);
}

void apply_text_content(const TextData& t, const TrackSet& tracks, f64 local, f64 fps, TextData& out) {
    out.content = apply_char_offset(t, tracks, local, fps);
    for (u32 ai = 0; ai < t.animators.size(); ++ai) {
        const TextAnimator& a = t.animators[ai];
        if (!a.enabled) continue;
        const auto P = [&](u32 key, f32 fallback, f64 when) { return anim_param(tracks, ai, key, when, fallback); };
        if (a.props & kTextPropReveal) {
            const f32 s = P(kSelStart, a.selector.start, local) * .01f, en = P(kSelEnd, a.selector.end, local) * .01f;
            bool show = true;
            if (a.cursorBlink) {
                // Pisca só com o texto parado: enquanto digita o cursor fica aceso.
                const usize n = split_code_points(out.content).size();
                const f64 prev = local - 1.0;
                const bool still = cut_at(s, n) == cut_at(P(kSelStart, a.selector.start, prev) * .01f, n) && cut_at(en, n) == cut_at(P(kSelEnd, a.selector.end, prev) * .01f, n);
                const f64 sec = local / (fps > 0 ? fps : 30.0);
                if (still && std::fmod(sec * 2.0, 2.0) > 1.0) show = false;
            }
            out.content = progress_text(out.content, s, en, a.cursor, show);
        }
        if (a.props & kTextPropRandomize) {
            out.content = randomize_text(out.content, P(kAnimRandAmount, a.randAmount, local) * .01f, P(kAnimRandEvolution, a.randEvolution, local),
                                         P(kAnimRandSeed, a.randSeed, local), P(kSelStart, a.selector.start, local) * .01f, P(kSelEnd, a.selector.end, local) * .01f,
                                         a.charset, a.preserveSpace);
        }
        if (a.props & kTextPropSpacing) {
            out.tracking += P(kAnimLetterSpacing, a.letterSpacing, local) * 10.f;   // 1 % do em = 10 milésimos
            out.lineHeight *= std::max(0.f, P(kAnimLineSpacingEm, a.lineSpacingEm, local) * .01f);
        }
    }
}

}
