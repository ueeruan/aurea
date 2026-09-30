#include "aurea/text/TextTransform.hpp"
#include "aurea/timeline/Layer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

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
}

namespace {
using Values = std::array<ParamValue, kTransformParamCount>;
Values sample(const Layer& layer, const EffectInstance& effect, const ParameterRegistry& specs, f64 time) {
    Values values;
    const f64 floor = std::floor(time);
    const FrameIndex frame{static_cast<i64>(floor)};
    const f32 mix = static_cast<f32>(time - floor);
    for (u32 i = 0; i < kTransformParamCount; ++i) {
        values[i] = evaluate_param(layer.tracks, effect, i, specs.at(i), frame);
        if (mix > 0 && specs.at(i).animatable()) {
            const auto next = evaluate_param(layer.tracks, effect, i, specs.at(i), FrameIndex{frame.value + 1});
            for (u32 c = 0; c < component_count(specs.at(i).type); ++c)
                values[i].v[c] += (next.v[c] - values[i].v[c]) * mix;
        }
    }
    return values;
}
u32 random(u32& state) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
f32 influence(const Values& v, u32 rank, u32 count) {
    const f32 begin = v[kRangeStart].v[0] + v[kPhase].v[0];
    const f32 end = v[kRangeEnd].v[0] + v[kPhase].v[0];
    const f32 lo = std::min(begin, end), hi = std::max(begin, end);
    if (hi <= lo || count == 0) return 0;
    const f32 cell = 100.f / count;
    const f32 center = (rank + .5f) * cell;
    const f32 spread = cell * (1.f + v[kOverlap].v[0] * .01f);
    f32 weight = std::clamp((std::min(center + spread / 2, hi) - std::max(center - spread / 2, lo)) / spread, 0.f, 1.f);
    const f32 position = std::clamp((center - lo) / (hi - lo), 0.f, 1.f);
    if (v[kShape].as_enum() == 1) weight *= .5f - .5f * std::cos(position * 6.28318530718f);
    if (v[kShape].as_enum() == 2) weight *= 1.f - std::fabs(2.f * position - 1.f);
    const f32 in = v[kEaseIn].v[0] * .01f, out = v[kEaseOut].v[0] * .01f;
    weight = std::pow(weight, std::exp2(2.f * in));
    return 1.f - std::pow(1.f - weight, std::exp2(2.f * out));
}
}

bool has_transform_effect(const Layer& layer) noexcept {
    if (layer.kind != LayerKind::Text) return false;
    for (const auto& effect : layer.effects) if (effect.enabled && effect.type == effect_type_id(kTransformEffect)) return true;
    return false;
}

f32 transform_padding(const Layer& layer, const EffectRegistry& registry, f64 time, f32 extent) {
    const auto* specs = registry.params(effect_type_id(kTransformEffect));
    if (!specs) return 0;
    f32 extra = 0;
    for (const auto& effect : layer.effects) if (effect.enabled && effect.type == effect_type_id(kTransformEffect)) {
        const auto v = sample(layer, effect, *specs, time);
        extra += std::max(std::fabs(v[kOffset].v[0]), std::fabs(v[kOffset].v[1]));
        extra += extent * (std::max(0.f, v[kScale].v[0] * .01f) + std::max(0.f, v[kStretch].v[0] * .01f));
        if (v[kAngle].v[0] != 0) extra += extent;
    }
    return std::min(extra, 4000.f);
}

void evaluate_transform_effects(const Layer& layer, const EffectRegistry& registry, f64 time,
                                const TextLayout& layout, std::vector<GlyphAnim>& styles,
                                std::vector<Mat4>& matrices) {
    matrices.assign(layout.quads.size(), Mat4::identity());
    if (styles.size() != layout.quads.size()) styles.resize(layout.quads.size());
    const auto* specs = registry.params(effect_type_id(kTransformEffect));
    if (!specs) return;
    for (const auto& effect : layer.effects) if (effect.enabled && effect.type == effect_type_id(kTransformEffect)) {
        const auto v = sample(layer, effect, *specs, time);
        const u32 component = v[kComponent].as_enum();
        const u32 count = component == 1 ? layout.words : component == 2 ? layout.lines : layout.chars;
        if (!count) continue;
        std::vector<u32> ranks(count); std::iota(ranks.begin(), ranks.end(), 0);
        if (v[kRandomOrder].as_bool()) {
            u32 seed = static_cast<u32>(std::lround(v[kSeed].v[0] * 10000)) + 0x9e3779b9u;
            for (u32 i = count; i > 1; --i) std::swap(ranks[i - 1], ranks[random(seed) % i]);
        }
        const auto unit = [component](const GlyphQuad& q) { return component == 1 ? q.wordIndex : component == 2 ? q.lineIndex : q.charIndex; };
        std::vector<Vec4> bounds(count, Vec4{1e30f, 1e30f, -1e30f, -1e30f});
        for (const auto& q : layout.quads) if (unit(q) < count) {
            auto& b = bounds[unit(q)]; b.x = std::min(b.x, q.x0); b.y = std::min(b.y, q.y0); b.z = std::max(b.z, q.x1); b.w = std::max(b.w, q.y1);
        }
        for (usize g = 0; g < layout.quads.size(); ++g) {
            const u32 index = unit(layout.quads[g]); if (index >= count) continue;
            const f32 weight = influence(v, ranks[index], count); if (weight == 0) continue;
            const auto b = bounds[index];
            const f32 margin = layer.text.strokeWidth > 0 ? layer.text.strokeWidth + 2 : 2;
            Vec3 layerAnchor = layer.transform.anchor;
            for (u32 axis=0; axis<3; ++axis) {
                if (const auto* track = layer.tracks.find(static_cast<TrackProperty>(static_cast<u16>(TrackProperty::AnchorX)+axis))) {
                    const float base = axis==0 ? layerAnchor.x : axis==1 ? layerAnchor.y : layerAnchor.z;
                    const float value = track->value_or(FrameIndex{static_cast<i64>(std::floor(time))},base);
                    if (axis==0) layerAnchor.x=value; else if(axis==1) layerAnchor.y=value; else layerAnchor.z=value;
                }
            }
            layerAnchor.x += layout.pad-margin; layerAnchor.y += layout.pad-margin;
            const Vec3 pivot = v[kAnchor].as_enum() == 1 ? Vec3{(b.x + b.z) / 2, (b.y + b.w) / 2, 0} : layerAnchor;
            const f32 scale = std::max(0.f, 1.f + v[kScale].v[0] * .01f * weight);
            const Vec3 stretch{scale, scale * std::max(0.f, 1.f + v[kStretch].v[0] * .01f * weight), 1};
            const auto offset = v[kOffset].as_vec2();
            const auto transform = Mat4::translation(pivot + Vec3{offset.x * weight, offset.y * weight, 0})
                * Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, v[kAngle].v[0] * kDeg2Rad * weight))
                * Mat4::scale(stretch) * Mat4::translation(-pivot);
            matrices[g] = transform * matrices[g];
            auto& style = styles[g];
            style.opacity *= std::clamp(1.f + v[kAlpha].v[0] * .01f * weight, 0.f, 1.f);
            if (v[kOverrideFill].as_bool()) {
                const Vec4 color = v[kFillColor].as_color();
                const f32 a = std::clamp(weight, 0.f, 1.f), previous = style.fill.w * (1.f - a), total = a + previous;
                // fill.w is the selector's color-mix weight, not color alpha.
                // Keep alpha independent so its slider/keys affect the rendered fill.
                style.fillOpacity += (std::clamp(color.w, 0.f, 1.f) - style.fillOpacity) * a;
                style.fill = total > 0 ? Vec4{(color.x * a + style.fill.x * previous) / total,
                    (color.y * a + style.fill.y * previous) / total, (color.z * a + style.fill.z * previous) / total, total} : Vec4{};
            }
        }
    }
}
}
