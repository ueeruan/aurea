// =============================================================================
//  Aurea / text / TextAnimatorMigrate.cpp
//
//  Os efeitos antigos de texto viram animadores do modelo único. Valores,
//  keyframes e expressões vão junto; o efeito sai da camada.
// =============================================================================
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextTransform.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace aurea::text {

namespace {

const ParameterRegistry& registry_of(void (*declare)(ParameterRegistry&), ParameterRegistry& slot, bool& ready) {
    if (!ready) { declare(slot); ready = true; }
    return slot;
}

struct Mapping { u32 param, component, key; };

/// Leva a trilha do efeito para a trilha do animador.
void move_tracks(Layer& layer, u32 effectId, u32 animator, const std::vector<Mapping>& map, bool colorToSrgb = false, u32 colorFirst = 0, u32 colorLast = 0) {
    for (const Mapping& m : map) {
        Track* source = layer.tracks.find(TrackProperty::EffectParam, effectId, m.param * 4 + m.component);
        if (!source) continue;
        Track moved = *source;
        moved.property = TrackProperty::TextAnimParam;
        moved.effectIndex = animator;
        moved.effectParamIndex = m.key;
        if (colorToSrgb && m.key >= colorFirst && m.key <= colorLast) {
            moved.staticValue = Color::linear_to_srgb(moved.staticValue);
            for (Keyframe& k : moved.keys) k.value = Color::linear_to_srgb(k.value);
        }
        layer.tracks.get_or_create(TrackProperty::TextAnimParam, animator, m.key) = std::move(moved);
    }
}

Vec4 srgb(Vec4 c) { return Vec4{Color::linear_to_srgb(c.x), Color::linear_to_srgb(c.y), Color::linear_to_srgb(c.z), c.w}; }

bool has_any_track(const Layer& layer, u32 effectId, u32 param) {
    for (u32 c = 0; c < 4; ++c) if (layer.tracks.find(TrackProperty::EffectParam, effectId, param * 4 + c)) return true;
    return false;
}

template <usize N>
std::array<ParamValue, N> constants(const EffectInstance& fx, const ParameterRegistry& specs) {
    static const TrackSet none;
    std::array<ParamValue, N> v{};
    for (u32 p = 0; p < N; ++p) v[p] = evaluate_param_f(none, fx, p, specs.at(p), 0.0);
    return v;
}

TextAnimator from_transform(Layer& layer, const EffectInstance& fx, u32 index) {
    static ParameterRegistry slot; static bool ready = false;
    const auto v = constants<kTransformParamCount>(fx, registry_of(declare_transform_params, slot, ready));
    TextAnimator a;
    a.name = "Text Transform";
    a.rig = 1;
    a.rigAnchor = static_cast<u8>(v[kAnchor].as_enum());
    a.props = kTextPropPosition | kTextPropScale | kTextPropRotation | kTextPropOpacity;
    if (v[kOverrideFill].as_bool()) a.props |= kTextPropFill;
    TextSelector& s = a.selector;
    s.type = kTextSelectorSweep;
    s.basedOn = static_cast<u8>(v[kComponent].as_enum());
    s.shape = static_cast<u8>(v[kShape].as_enum());
    s.randomOrder = v[kRandomOrder].as_bool();
    s.seed = static_cast<u32>(std::lround(v[kSeed].v[0] * 10000));
    s.start = v[kRangeStart].v[0]; s.end = v[kRangeEnd].v[0]; s.offset = v[kPhase].v[0];
    s.easeHigh = v[kEaseIn].v[0]; s.easeLow = v[kEaseOut].v[0]; s.overlap = v[kOverlap].v[0];
    a.position = Vec3{v[kOffset].v[0], v[kOffset].v[1], 0};
    a.rotation = Vec3{0, 0, v[kAngle].v[0]};
    a.scale = Vec2{v[kScale].v[0], v[kStretch].v[0]};
    a.opacity = v[kAlpha].v[0];
    a.fill = v[kFillColor].as_color();
    move_tracks(layer, fx.id, index, {
        {kRangeStart, 0, kSelStart}, {kRangeEnd, 0, kSelEnd}, {kPhase, 0, kSelOffset}, {kOffset, 0, kPosX}, {kOffset, 1, kPosY},
        {kAngle, 0, kRotZ}, {kScale, 0, kScaleX}, {kStretch, 0, kScaleY}, {kAlpha, 0, kOpacity},
        {kFillColor, 0, kFillR}, {kFillColor, 1, kFillG}, {kFillColor, 2, kFillB}, {kFillColor, 3, kAnimFillA},
        {kEaseIn, 0, kSelEaseHigh}, {kEaseOut, 0, kSelEaseLow}, {kOverlap, 0, kAnimOverlap},
    });
    return a;
}

TextAnimator from_progress(Layer& layer, const EffectInstance& fx, u32 index) {
    static ParameterRegistry slot; static bool ready = false;
    const auto v = constants<kProgressParamCount>(fx, registry_of(declare_progress_params, slot, ready));
    TextAnimator a;
    a.name = "Text Progress";
    a.props = kTextPropReveal;
    a.selector.start = v[kProgStart].v[0]; a.selector.end = v[kProgEnd].v[0];
    a.cursor = static_cast<u8>(v[kProgCursor].as_enum());
    a.cursorBlink = v[kProgBlink].as_bool();
    move_tracks(layer, fx.id, index, {{kProgStart, 0, kSelStart}, {kProgEnd, 0, kSelEnd}});
    return a;
}

TextAnimator from_randomizer(Layer& layer, const EffectInstance& fx, u32 index) {
    static ParameterRegistry slot; static bool ready = false;
    const auto v = constants<kRandomizerParamCount>(fx, registry_of(declare_randomizer_params, slot, ready));
    TextAnimator a;
    a.name = "Text Randomizer";
    a.props = kTextPropRandomize;
    a.randAmount = v[kRandAmount].v[0]; a.randEvolution = v[kRandEvolution].v[0]; a.randSeed = v[kRandSeed].v[0];
    a.selector.start = v[kRandStart].v[0]; a.selector.end = v[kRandEnd].v[0];
    a.charset = static_cast<u8>(v[kRandCharset].as_enum());
    a.preserveSpace = v[kRandPreserveSpace].as_bool();
    move_tracks(layer, fx.id, index, {{kRandAmount, 0, kAnimRandAmount}, {kRandEvolution, 0, kAnimRandEvolution}, {kRandSeed, 0, kAnimRandSeed},
                                      {kRandStart, 0, kSelStart}, {kRandEnd, 0, kSelEnd}});
    return a;
}

TextAnimator from_spacing(Layer& layer, const EffectInstance& fx, u32 index) {
    static ParameterRegistry slot; static bool ready = false;
    const auto v = constants<kSpacingParamCount>(fx, registry_of(declare_spacing_params, slot, ready));
    TextAnimator a;
    a.name = "Text Spacing";
    a.props = kTextPropSpacing;
    a.letterSpacing = v[kSpaceLetter].v[0]; a.lineSpacingEm = v[kSpaceLine].v[0];
    move_tracks(layer, fx.id, index, {{kSpaceLetter, 0, kAnimLetterSpacing}, {kSpaceLine, 0, kAnimLineSpacingEm}});
    return a;
}

TextAnimator from_animator_effect(Layer& layer, const EffectInstance& fx, u32 index) {
    static ParameterRegistry slot; static bool ready = false;
    const auto v = constants<aeCount>(fx, registry_of(declare_animator_effect_params, slot, ready));
    TextAnimator a;
    a.name = "Text Animator";
    TextSelector& s = a.selector;
    s.start = v[aeStart].v[0]; s.end = v[aeEnd].v[0]; s.offset = v[aeOffset].v[0]; s.amount = v[aeAmount].v[0];
    s.basedOn = static_cast<u8>(v[aeUnit].as_enum()); s.shape = static_cast<u8>(v[aeShape].as_enum());
    s.easeHigh = v[aeEaseHigh].v[0]; s.easeLow = v[aeEaseLow].v[0];
    s.randomOrder = v[aeRandom].as_bool(); s.seed = static_cast<u32>(v[aeSeed].v[0]);
    s.type = static_cast<u8>(v[aeSelector].as_enum()); s.wiggleRate = v[aeWiggleRate].v[0];
    a.props = kTextPropPosition | kTextPropScale | kTextPropRotation | kTextPropOpacity | kTextPropBlur | kTextPropTracking | kTextPropSkew | kTextPropStrokeWidth;
    a.position = v[aePosition].as_vec3(); a.scale = v[aeScale].as_vec2(); a.rotation = v[aeRotation].as_vec3();
    a.opacity = v[aeOpacity].v[0]; a.blur = v[aeBlur].v[0]; a.tracking = v[aeTracking].v[0];
    a.skew = v[aeSkew].v[0]; a.strokeWidth = v[aeStrokeWidth].v[0];
    a.fill = v[aeFill].as_color(); a.stroke = v[aeStroke].as_color();
    if (v[aeFillOn].as_bool()) { a.props |= kTextPropFill | kTextPropFillOpacity; a.fillOpacity = std::clamp(a.fill.w, 0.f, 1.f) * 100.f; }
    if (v[aeStrokeOn].as_bool()) a.props |= kTextPropStroke;
    move_tracks(layer, fx.id, index, {
        {aeStart, 0, kSelStart}, {aeEnd, 0, kSelEnd}, {aeOffset, 0, kSelOffset}, {aeAmount, 0, kSelAmount}, {aeEaseHigh, 0, kSelEaseHigh}, {aeEaseLow, 0, kSelEaseLow},
        {aeWiggleRate, 0, kWiggleRate}, {aePosition, 0, kPosX}, {aePosition, 1, kPosY}, {aePosition, 2, kPosZ}, {aeScale, 0, kScaleX}, {aeScale, 1, kScaleY},
        {aeRotation, 0, kRotX}, {aeRotation, 1, kRotY}, {aeRotation, 2, kRotZ}, {aeOpacity, 0, kOpacity}, {aeBlur, 0, kBlur}, {aeTracking, 0, kTracking},
        {aeSkew, 0, kSkew}, {aeStrokeWidth, 0, kStrokeWidth},
        {aeFill, 0, kFillR}, {aeFill, 1, kFillG}, {aeFill, 2, kFillB}, {aeStroke, 0, kStrokeR}, {aeStroke, 1, kStrokeG}, {aeStroke, 2, kStrokeB},
    });
    return a;
}

TextAnimator from_animator_pro(Layer& layer, const EffectInstance& fx, u32 index) {
    static ParameterRegistry slot; static bool ready = false;
    const auto v = constants<apCount>(fx, registry_of(declare_animator_pro_params, slot, ready));
    TextAnimator a;
    a.name = "Text Animator Pro";
    static constexpr u8 basedOn[4] = {0, 3, 1, 2};
    const u8 unit = basedOn[std::min<u32>(v[apBasedOn].as_enum(), 3u)];
    TextSelector& s = a.selector;
    s.type = kTextSelectorRange;
    s.basedOn = unit; s.units = static_cast<u8>(v[apUnits].as_enum()); s.mode = static_cast<u8>(v[apMode].as_enum());
    s.shape = static_cast<u8>(v[apShape].as_enum()); s.randomOrder = v[apRandom].as_bool(); s.seed = static_cast<u32>(v[apSeed].v[0]) + 1u;
    s.start = v[apStart].v[0]; s.end = v[apEnd].v[0]; s.offset = v[apOffset].v[0]; s.amount = v[apAmount].v[0];
    s.smoothness = v[apSmoothness].v[0]; s.easeHigh = v[apEaseHigh].v[0]; s.easeLow = v[apEaseLow].v[0];
    std::vector<Mapping> map = {
        {apStart, 0, kSelStart}, {apEnd, 0, kSelEnd}, {apOffset, 0, kSelOffset}, {apAmount, 0, kSelAmount}, {apSmoothness, 0, kAnimSmoothness},
        {apEaseHigh, 0, kSelEaseHigh}, {apEaseLow, 0, kSelEaseLow},
        {apAnchor, 0, kAnimAnchorX}, {apAnchor, 1, kAnimAnchorY}, {apPosition, 0, kPosX}, {apPosition, 1, kPosY}, {apPosition, 2, kPosZ},
        {apScale, 0, kScaleX}, {apScale, 1, kScaleY}, {apSkew, 0, kSkew}, {apSkewAxis, 0, kSkewAxis},
        {apRotation, 0, kRotX}, {apRotation, 1, kRotY}, {apRotation, 2, kRotZ}, {apOpacity, 0, kOpacity},
        {apFill, 0, kFillR}, {apFill, 1, kFillG}, {apFill, 2, kFillB}, {apFillHue, 0, kAnimFillHue}, {apFillSaturation, 0, kAnimFillSaturation},
        {apFillBrightness, 0, kAnimFillBrightness}, {apFillOpacity, 0, kAnimFillOpacity},
        {apStroke, 0, kStrokeR}, {apStroke, 1, kStrokeG}, {apStroke, 2, kStrokeB}, {apStrokeWidth, 0, kStrokeWidth},
        {apTracking, 0, kTracking}, {apLineSpacing, 0, kAnimLineSpacingX}, {apLineSpacing, 1, kAnimLineSpacingY}, {apBlur, 0, kBlur},
    };
    u32 next = 1;
    if (v[apExprOn].as_bool()) {
        TextSelector e;
        e.type = kTextSelectorExpression;
        e.basedOn = unit;
        e.mode = kTextModeReplace;
        e.exprAmount = v[apExprAmount].as_vec3();
        a.moreSelectors.push_back(e);
        for (u32 c = 0; c < 3; ++c) map.push_back({apExprAmount, c, selector_key(next, kAnimExprX + c)});
        ++next;
    }
    if (v[apWiggly].as_bool()) {
        TextSelector w;
        w.type = kTextSelectorWiggly;
        w.basedOn = unit;
        w.mode = static_cast<u8>(v[apWigglyMode].as_enum());
        w.wiggleMax = v[apWigglyMax].v[0]; w.wiggleMin = v[apWigglyMin].v[0]; w.wiggleRate = v[apWigglyRate].v[0];
        w.correlation = v[apCorrelation].v[0]; w.temporalPhase = v[apTemporalPhase].v[0]; w.spatialPhase = v[apSpatialPhase].v[0];
        w.lockDimensions = v[apLockDims].as_bool();
        w.seed = static_cast<u32>(v[apWigglySeed].v[0]);
        a.moreSelectors.push_back(w);
        for (const Mapping& m : std::initializer_list<Mapping>{{apWigglyMax, 0, kAnimWigglyMax}, {apWigglyMin, 0, kAnimWigglyMin}, {apWigglyRate, 0, kWiggleRate},
                                                                {apCorrelation, 0, kAnimCorrelation}, {apTemporalPhase, 0, kAnimTemporalPhase}, {apSpatialPhase, 0, kAnimSpatialPhase}})
            map.push_back({m.param, m.component, selector_key(next, m.key)});
    }
    a.props = kTextPropAnchor | kTextPropPosition | kTextPropScale | kTextPropRotation | kTextPropOpacity | kTextPropBlur | kTextPropTracking |
              kTextPropSkew | kTextPropStrokeWidth | kTextPropLineSpacing | kTextPropFillOpacity;
    if (v[apFillOn].as_bool()) a.props |= kTextPropFill;
    if (v[apStrokeOn].as_bool()) a.props |= kTextPropStroke;
    const f32 hue = v[apFillHue].v[0], sat = v[apFillSaturation].v[0], bri = v[apFillBrightness].v[0];
    if (hue != 0 || sat != 0 || bri != 0 || has_any_track(layer, fx.id, apFillHue) || has_any_track(layer, fx.id, apFillSaturation) || has_any_track(layer, fx.id, apFillBrightness))
        a.props |= kTextPropFillHsb;
    a.anchor = v[apAnchor].as_vec2(); a.position = v[apPosition].as_vec3(); a.scale = v[apScale].as_vec2();
    a.skew = v[apSkew].v[0]; a.rotation = v[apRotation].as_vec3(); a.opacity = v[apOpacity].v[0];
    a.fill = srgb(v[apFill].as_color()); a.stroke = srgb(v[apStroke].as_color());
    a.fillHue = hue; a.fillSaturation = sat; a.fillBrightness = bri; a.fillOpacity = v[apFillOpacity].v[0];
    a.strokeWidth = v[apStrokeWidth].v[0]; a.tracking = v[apTracking].v[0]; a.lineSpacing = v[apLineSpacing].as_vec2(); a.blur = v[apBlur].v[0];
    a.anchorGrouping = static_cast<u8>(v[apGrouping].as_enum());
    a.groupAlign = v[apGroupAlign].as_vec2();
    move_tracks(layer, fx.id, index, map, true, kFillR, kStrokeB);
    if (const f32 axis = v[apSkewAxis].v[0]; axis != 0) {
        Track& t = layer.tracks.get_or_create(TrackProperty::TextAnimParam, index, kSkewAxis);
        if (!t.driven()) t.staticValue = axis;
    }
    return a;
}

} // namespace

TextAnimator make_transform_animator() {
    TextAnimator a;
    a.name = "Text Transform";
    a.rig = 1;
    a.props = kTextPropPosition | kTextPropScale | kTextPropRotation | kTextPropOpacity;
    a.selector.type = kTextSelectorSweep;
    a.scale = Vec2{0, 0};   // no modelo do Motion a escala soma
    a.opacity = 0;          // e a opacidade é o quanto apaga
    return a;
}

void migrate_text_effects(Layer& layer) {
    if (layer.kind != LayerKind::Text && layer.kind != LayerKind::Model3D) return;
    if (layer.effects.empty()) return;
    const u32 transform = effect_type_id(kTransformEffect), progress = effect_type_id(kProgressEffect), randomizer = effect_type_id(kRandomizerEffect);
    const u32 spacing = effect_type_id(kSpacingEffect), animator = effect_type_id(kAnimatorEffect), pro = effect_type_id(kAnimatorProEffect);
    std::vector<u32> retired;
    for (const EffectInstance& fx : layer.effects) {
        const u32 t = fx.type;
        if (t != transform && t != progress && t != randomizer && t != spacing && t != animator && t != pro) continue;
        if (layer.text.animators.size() >= 64) continue;
        const u32 index = static_cast<u32>(layer.text.animators.size());
        TextAnimator a = t == transform ? from_transform(layer, fx, index) : t == progress ? from_progress(layer, fx, index)
                       : t == randomizer ? from_randomizer(layer, fx, index) : t == spacing ? from_spacing(layer, fx, index)
                       : t == animator ? from_animator_effect(layer, fx, index) : from_animator_pro(layer, fx, index);
        a.enabled = fx.enabled;
        a.aeModel = 0;
        layer.text.animators.push_back(std::move(a));
        retired.push_back(fx.id);
    }
    if (retired.empty()) return;
    layer.tracks.remove_if([&](const Track& t) {
        return t.property == TrackProperty::EffectParam && std::find(retired.begin(), retired.end(), t.effectIndex) != retired.end();
    });
    layer.effects.erase(std::remove_if(layer.effects.begin(), layer.effects.end(), [&](const EffectInstance& e) {
        return std::find(retired.begin(), retired.end(), e.id) != retired.end();
    }), layer.effects.end());
}

} // namespace aurea::text
