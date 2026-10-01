#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include <array>
#include <filesystem>

using namespace aurea;

namespace {
struct Keys2132 {
    Engine e;
    LayerId id;
    Keys2132() {
        EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.new_project(320, 240, 30, nullptr).ok());
        id = comp()->add_layer(LayerKind::Null, "keyframe drag");
        layer()->start = FrameIndex{90}; layer()->end = FrameIndex{210}; layer()->offset = FrameIndex{30};
    }
    ~Keys2132() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* layer() { return comp()->layer(id); }
    void command(CommandType type) { Command c; c.type = type; AUREA_CHECK(e.apply_command(c).ok()); }
    void scale() {
        for (auto property : {TrackProperty::ScaleX, TrackProperty::ScaleY}) {
            auto& track = layer()->tracks.get_or_create(property);
            const f32 ratio = property == TrackProperty::ScaleX ? 1.f : 2.f;
            track.set(FrameIndex{30}, .5f * ratio); track.set(FrameIndex{60}, 2.f * ratio); track.set(FrameIndex{90}, ratio);
        }
    }
};
}

AUREA_TEST(Regression2132, DragCommitsEveryAxisAcrossNeighborsAndSurvivesUndoAndSave) {
    Keys2132 r; r.scale();
    std::array<i64, 8> refs{3, -1, 0, 60, 4, -1, 0, 60};
    const auto depth = r.e.read_status().undoDepth;
    r.command(CommandType::UndoBeginGroup);
    i64 accepted = 60;
    // Finger crosses an occupied frame, reverses direction, then releases.
    for (i64 target : {65, 80, 90, 105, 99, 75, 71}) {
        refs[3] = refs[7] = accepted;
        const auto moved = r.e.edit_keyframe_selection(r.id.pack(), refs.data(), 2, target - accepted, false);
        AUREA_CHECK_EQ(moved, target == 90 ? 0u : 2u);
        if (moved) accepted = target;
        for (auto p : {TrackProperty::ScaleX, TrackProperty::ScaleY}) {
            const auto* track = r.layer()->tracks.find(p);
            AUREA_CHECK_EQ(track->keys.size(), usize{3});
            AUREA_CHECK(track->find_exact(FrameIndex{accepted}) != kInvalidIndex);
        }
    }
    r.command(CommandType::UndoEndGroup);
    AUREA_CHECK_EQ(accepted, i64{71});
    AUREA_CHECK_EQ(r.e.read_status().undoDepth, depth + 1);
    // The key at local 71 really is at composition frame 131 after trim/move.
    AUREA_CHECK_EQ(r.layer()->local_time(FrameIndex{131}).value, accepted);
    r.command(CommandType::Undo);
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::ScaleX)->find_exact(FrameIndex{60}) != kInvalidIndex);
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::ScaleY)->find_exact(FrameIndex{60}) != kInvalidIndex);
    r.command(CommandType::Redo);
    const char* path = "build/reference/keyframes-2132/drag.aurea";
    std::filesystem::create_directories("build/reference/keyframes-2132");
    AUREA_CHECK(r.e.save_project(path).ok());
    AUREA_CHECK(r.e.load_project(path).ok());
    for (auto p : {TrackProperty::ScaleX, TrackProperty::ScaleY}) {
        const auto* track = r.layer()->tracks.find(p);
        AUREA_CHECK(track->find_exact(FrameIndex{71}) != kInvalidIndex);
        AUREA_CHECK(track->find_exact(FrameIndex{60}) == kInvalidIndex);
    }
    // Applying the same easing after moving preserves the pre-existing 1:2 ratio
    // throughout every frame, including bounce/overshoot and negative values.
    for (auto interp : {Interpolation::Bezier, Interpolation::Bounce, Interpolation::Elastic}) {
        for (auto p : {TrackProperty::ScaleX, TrackProperty::ScaleY}) {
            auto* track = r.layer()->tracks.find(p);
            for (auto& key : track->keys) track->set_interpolation(key.time, interp, .34f, 1.56f, .64f, 1.f, 1);
        }
        for (i64 frame = 0; frame <= 120; ++frame) {
            const auto x = r.layer()->tracks.find(TrackProperty::ScaleX)->sample(FrameIndex{frame});
            const auto y = r.layer()->tracks.find(TrackProperty::ScaleY)->sample(FrameIndex{frame});
            AUREA_CHECK_NEAR(y, 2 * x, 1e-5f);
        }
    }
}

AUREA_TEST(Regression2132, RemapAndEffectSelectionAreAtomicAndAliasesCannotMoveTwice) {
    Keys2132 r; r.scale();
    auto* l = r.layer();
    EffectInstance remap; remap.id = l->alloc_effect_id(); remap.type = effect_type_id(effect_keys::kTimeRemap);
    l->effects.push_back(remap);
    l->timeRemapEnabled = true; l->timeRemap.property = TrackProperty::TimeRemap;
    l->timeRemap.set(FrameIndex{30}, 0); l->timeRemap.set(FrameIndex{60}, 20); l->timeRemap.set(FrameIndex{90}, 0);
    std::array<i64, 12> refs{static_cast<i64>(TrackProperty::TimeRemap), -1, 0, 60,
        static_cast<i64>(TrackProperty::EffectParam), remap.id, 0, 60, 3, -1, 0, 60};
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.id.pack(), refs.data(), 3, 10, false), 2u);
    AUREA_CHECK_EQ(l->timeRemap.keys[1].time.value, i64{70});
    AUREA_CHECK_NEAR(l->timeRemap.keys[1].value, 20, 0);
    refs[3] = refs[7] = refs[11] = 70;
    // Collision on one axis refuses the entire edit, including the remap curve.
    l->tracks.find(TrackProperty::ScaleX)->set(FrameIndex{80}, 3);
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.id.pack(), refs.data(), 3, 10, false), 0u);
    AUREA_CHECK_EQ(l->timeRemap.keys[1].time.value, i64{70});
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.id.pack(), refs.data(), 3, -11, false), 2u);
    AUREA_CHECK_EQ(l->timeRemap.keys[1].time.value, i64{59});
    refs[3] = refs[7] = refs[11] = 59;
    AUREA_CHECK_EQ(r.e.copy_keyframe_selection(r.id.pack(), refs.data(), 2), 1u);
    const u64 id = r.id.pack();
    AUREA_CHECK_EQ(r.e.paste_keyframes(&id, 1, 140), 1u);
    AUREA_CHECK(l->timeRemap.find_exact(FrameIndex{80}) != kInvalidIndex);
    AUREA_CHECK(l->tracks.find(TrackProperty::TimeRemap) == nullptr);
}

AUREA_TEST(Regression2132, PastedTextAnimatorPreparesExtrudedLetters) {
    Keys2132 r;
    scene3d::Text3DSpec spec; spec.content = "AB";
    const auto source = r.e.add_text3d(spec), target = r.e.add_text3d(spec);
    AUREA_CHECK(source.ok() && target.ok()); if (!source.ok() || !target.ok()) return;
    Command add; add.type = CommandType::EffectAdd;
    add.effect_add = {LayerId::unpack(*source), effect_type_id(text::kAnimatorEffect), kInvalidIndex};
    AUREA_CHECK(r.e.apply_command(add).ok());
    AUREA_CHECK(r.e.copy_effects(*source) > 0);
    const u64 to = *target;
    AUREA_CHECK_EQ(r.e.paste_effects(&to, 1), 1u);
    auto* layer = r.comp()->layer(LayerId::unpack(to));
    auto asset = r.e.model_asset(layer->model.scene.pack());
    AUREA_CHECK(asset && asset->textGlyphLayout);
    if (!asset || !asset->textGlyphLayout) return;
    Command opacity; opacity.type = CommandType::EffectSetParam;
    opacity.effect_param = {LayerId::unpack(to), EffectId{layer->effects.back().id, 0}, text::aeOpacity, 0};
    AUREA_CHECK(r.e.apply_command(opacity).ok());
    auto pose = asset->rest_world_matrices(); std::vector<f32> alpha;
    scene3d::apply_text3d_animators(*asset, *layer, 0, 30, pose, alpha);
    AUREA_CHECK(!alpha.empty());
    for (auto a : alpha) AUREA_CHECK_NEAR(a, 0, 1e-6f);
    // Editing text with a panel snapshot taken before EffectAdd keeps the
    // geometry required by the effect, including an effect temporarily off.
    spec.content = "ABC"; spec.separateGlyphs = false;
    AUREA_CHECK(r.e.set_text3d(to, spec).ok());
    layer = r.comp()->layer(LayerId::unpack(to));
    asset = r.e.model_asset(layer->model.scene.pack());
    AUREA_CHECK(asset && asset->textGlyphLayout);
    const auto preset = r.e.save_preset(to, presets::PresetKind::Effects, "Animator");
    const auto presetTarget = r.e.add_text3d(spec);
    AUREA_CHECK(presetTarget.ok()); if (!presetTarget.ok()) return;
    AUREA_CHECK(r.e.apply_preset(*presetTarget, preset));
    auto* presetLayer = r.comp()->layer(LayerId::unpack(*presetTarget));
    AUREA_CHECK(r.e.model_asset(presetLayer->model.scene.pack())->textGlyphLayout);
    // Reproduce a project produced by the old paste path: effects/keys stored
    // beside the unseparated source recipe. Reopening repairs that recipe.
    const auto merged = r.e.add_text3d(spec);
    AUREA_CHECK(merged.ok()); if (!merged.ok()) return;
    layer = r.comp()->layer(LayerId::unpack(to));
    layer->model.scene = r.comp()->layer(LayerId::unpack(*merged))->model.scene;
    const char* path = "build/reference/keyframes-2132/legacy-text.aurea";
    std::filesystem::create_directories("build/reference/keyframes-2132");
    AUREA_CHECK(r.e.save_project(path).ok());
    AUREA_CHECK(r.e.load_project(path).ok());
    layer = r.comp()->layer(LayerId::unpack(to));
    asset = r.e.model_asset(layer->model.scene.pack());
    AUREA_CHECK(asset && asset->textGlyphLayout);
    scene3d::Text3DSpec reopened;
    AUREA_CHECK(r.e.query_text3d(to, reopened)); AUREA_CHECK(reopened.separateGlyphs);
}
