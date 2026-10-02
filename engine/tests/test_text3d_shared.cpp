#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/TextTransform.hpp"
#include <filesystem>

using namespace aurea;
namespace {
struct SharedTextRig {
    Engine engine;
    SharedTextRig() {
        EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
        AUREA_CHECK(engine.initialize(config).ok());
        AUREA_CHECK(engine.new_project(320, 240, 30, nullptr).ok());
    }
    ~SharedTextRig() { engine.shutdown(); }
    Composition* comp() { return engine.project()->timeline().composition(engine.project()->timeline().current()); }
    Layer* layer(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 mesh(const char* content = "AB CD") {
        scene3d::Text3DSpec spec; spec.content = content;
        auto result = engine.add_text3d(spec); AUREA_CHECK(result.ok());
        return result.ok() ? *result : 0;
    }
    Status add(u64 id) {
        Command c; c.type = CommandType::EffectAdd;
        c.effect_add = {LayerId::unpack(id), effect_type_id(text::kTransformEffect), kInvalidIndex};
        return engine.apply_command(c);
    }
};
struct GlyphPose {
    std::vector<Mat4> matrices;
    std::vector<f32> opacity;
    std::vector<Vec4> fill;
};
GlyphPose sample(SharedTextRig& rig, u64 id, f64 frame) {
    const Layer& layer = *rig.layer(id);
    const auto asset = rig.engine.model_asset(layer.model.scene.pack());
    AUREA_CHECK(asset && asset->textGlyphLayout);
    GlyphPose p;
    if (!asset) return p;
    p.matrices = asset->rest_world_matrices();
    scene3d::apply_text3d_animators(*asset, layer, frame, 30, p.matrices, p.opacity, &p.fill);
    return p;
}
bool different(const GlyphPose& a, const GlyphPose& b) {
    if (a.matrices.size() != b.matrices.size() || a.opacity.size() != b.opacity.size()) return true;
    for (usize i = 0; i < a.matrices.size(); ++i) {
        for (u32 col = 0; col < 4; ++col) {
            const Vec4 x = a.matrices[i].col[col], y = b.matrices[i].col[col];
            if (std::fabs(x.x-y.x)+std::fabs(x.y-y.y)+std::fabs(x.z-y.z)+std::fabs(x.w-y.w) > 1e-5f) return true;
        }
        if (i < a.opacity.size() && std::fabs(a.opacity[i]-b.opacity[i]) > 1e-5f) return true;
        if (i < a.fill.size() && i < b.fill.size()) {
            const Vec4 x = a.fill[i], y = b.fill[i];
            if (std::fabs(x.x-y.x)+std::fabs(x.y-y.y)+std::fabs(x.z-y.z)+std::fabs(x.w-y.w) > 1e-5f) return true;
        }
    }
    return false;
}
}

AUREA_TEST(SharedText3D, TransformSelectsWordsColorsAndSamplesEffectCurves) {
    SharedTextRig r; const u64 id = r.mesh(); AUREA_CHECK(r.add(id).ok());
    Layer& l = *r.layer(id); auto& effect = l.effects.back();
    auto set = [&](u32 p, f32 v) { effect.params[p].constant = ParamValue::scalar(v); };
    set(text::kComponent, 1); set(text::kRangeEnd, 50);
    effect.params[text::kOffset].constant = ParamValue::vec2(100, 50);
    set(text::kAlpha, -50); set(text::kOverrideFill, 1);
    effect.params[text::kFillColor].constant = ParamValue::color(1, 0, 0, .5f);
    auto pose = sample(r, id, 0);
    for (usize i = 0; i < 4; ++i) {
        AUREA_CHECK_NEAR(pose.matrices[i].col[3].x, i < 2 ? 1.f : 0.f, 1e-5);
        AUREA_CHECK_NEAR(pose.matrices[i].col[3].y, i < 2 ? -.5f : 0.f, 1e-5);
        AUREA_CHECK_NEAR(pose.opacity[i], i < 2 ? .25f : 1.f, 1e-5);
        AUREA_CHECK_NEAR(pose.fill[i].w, i < 2 ? 1.f : 0.f, 1e-5);
    }
    auto& track = l.tracks.get_or_create(TrackProperty::EffectParam, effect.id, param_track_key(text::kOffset, 0));
    track.set(FrameIndex{0}, 0, Interpolation::Linear); track.set(FrameIndex{30}, 100, Interpolation::Linear);
    const auto half = sample(r, id, 15.5);
    AUREA_CHECK_NEAR(half.matrices[0].col[3].x, 15.5f/30, 1e-5);
    sample(r, id, 29); sample(r, id, 0);
    AUREA_CHECK(!different(half, sample(r, id, 15.5)));
    set(text::kPhase, 50);
    pose = sample(r, id, 15.5);
    AUREA_CHECK_NEAR(pose.opacity[0], 1.f, 1e-5);
    AUREA_CHECK_NEAR(pose.opacity[3], .25f, 1e-5);
}

AUREA_TEST(SharedText3D, TransformScalePreservesDepthAndComponentPivot) {
    SharedTextRig r; const u64 id = r.mesh("AB"); AUREA_CHECK(r.add(id).ok());
    Layer& l = *r.layer(id); auto& effect = l.effects.back();
    effect.params[text::kScale].constant = ParamValue::scalar(100);
    effect.params[text::kAnchor].constant = ParamValue::scalar(1);
    const auto asset = r.engine.model_asset(l.model.scene.pack());
    const auto pose = sample(r, id, 0);
    for (usize i = 0; i < pose.matrices.size(); ++i) {
        const Vec3 center = asset->meshes[i].bounds.center();
        const Vec3 moved = pose.matrices[i].transform_point(center);
        AUREA_CHECK_NEAR(moved.x, center.x, 1e-5); AUREA_CHECK_NEAR(moved.y, center.y, 1e-5);
        AUREA_CHECK_NEAR(moved.z, center.z, 1e-5);
        AUREA_CHECK_NEAR(pose.matrices[i].col[0].x, 2, 1e-5);
        AUREA_CHECK_NEAR(pose.matrices[i].col[1].y, 2, 1e-5);
        AUREA_CHECK_NEAR(pose.matrices[i].col[2].z, 2, 1e-5);
    }
}

AUREA_TEST(SharedText3D, EveryNormalTextPresetChangesExtrudedGlyphs) {
    SharedTextRig r; const u64 id = r.mesh("AUREA 3D");
    for (u32 preset = 0; preset < text::kTextPresetCount; ++preset) {
        AUREA_CHECK(r.engine.apply_text_preset(id, preset));
        const auto first = sample(r, id, 0);
        bool changed = false;
        for (f64 frame : {3.0, 9.5, 17.0, 30.0, 60.0, 90.0}) {
            const auto next = sample(r, id, frame);
            changed |= different(first, next);
            for (f32 alpha : next.opacity) AUREA_CHECK(std::isfinite(alpha) && alpha >= 0 && alpha <= 1);
        }
        if (!changed) std::printf("unchanged preset: %u (%s)\n", preset, text::text_preset_name(preset));
        AUREA_CHECK(changed);
    }
}

AUREA_TEST(SharedText3D, PresetsSupportEditsUndoAndProjectRoundTrip) {
    SharedTextRig r; const u64 id = r.mesh("Olá 3D");
    AUREA_CHECK(r.engine.apply_text_preset(id, 2));
    AUREA_CHECK_EQ(r.engine.query_text_animators(id, nullptr, 0), 1u);
    AUREA_CHECK(r.engine.set_text_anim_param(id, 0, text::kPosX, 240));
    AUREA_CHECK(r.engine.toggle_text_anim_key(id, 0, text::kPosX));
    AUREA_CHECK(r.add(id).ok());
    r.layer(id)->effects.back().params[text::kAngle].constant = ParamValue::scalar(25);
    const auto before = sample(r, id, 12.5);
    const std::string path = "build/reference/text3d-shared/project.aurea";
    std::filesystem::create_directories("build/reference/text3d-shared");
    AUREA_CHECK(r.engine.save_project(path.c_str()).ok());
    AUREA_CHECK(r.engine.load_project(path.c_str()).ok());
    AUREA_CHECK(!different(before, sample(r, id, 12.5)));
    scene3d::Text3DSpec spec; AUREA_CHECK(r.engine.query_text3d(id, spec));
    spec.separateGlyphs = false; spec.content = "Novo texto";
    AUREA_CHECK(r.engine.set_text3d(id, spec).ok());
    AUREA_CHECK(r.engine.query_text3d(id, spec) && spec.separateGlyphs);
    AUREA_CHECK_EQ(r.layer(id)->text.content, std::string("Novo texto"));
    Command undo; undo.type = CommandType::Undo; AUREA_CHECK(r.engine.apply_command(undo).ok());
    AUREA_CHECK(!different(before, sample(r, id, 12.5)));
}

AUREA_TEST(SharedText3D, SavedPresetsAndCopiedEffectsWorkAcross2DAnd3D) {
    SharedTextRig r;
    auto normal = r.engine.add_text("AB CD"); AUREA_CHECK(normal.ok()); if (!normal.ok()) return;
    AUREA_CHECK(r.engine.apply_text_preset(*normal, 6));
    const auto preset = r.engine.save_preset(*normal, presets::PresetKind::Text, "Text animation");
    const u64 id = r.mesh();
    const f32 unitScale = r.layer(id)->model.unitScale;
    AUREA_CHECK(r.engine.apply_preset(id, preset));
    AUREA_CHECK_EQ(r.engine.query_text_animators(id, nullptr, 0), 1u);
    AUREA_CHECK_NEAR(r.layer(id)->model.unitScale, unitScale, 1e-6);
    AUREA_CHECK(different(sample(r, id, 0), sample(r, id, 20)));
    const auto meshPreset = r.engine.save_preset(id, presets::PresetKind::Text, "3D animation");
    AUREA_CHECK(!meshPreset.empty()); AUREA_CHECK(r.engine.apply_preset(*normal, meshPreset));
    AUREA_CHECK(r.add(*normal).ok()); AUREA_CHECK(r.engine.copy_effects(*normal) > 0);
    const u64 target = r.mesh(); AUREA_CHECK_EQ(r.engine.paste_effects(&target, 1), 1u);
    r.layer(target)->effects.back().params[text::kAlpha].constant = ParamValue::scalar(-100);
    for (f32 alpha : sample(r, target, 0).opacity) AUREA_CHECK_NEAR(alpha, 0, 1e-6);
    const auto effectPreset = r.engine.save_preset(target, presets::PresetKind::Effects, "Text Transform");
    const u64 other = r.mesh(); AUREA_CHECK(r.engine.apply_preset(other, effectPreset));
    for (f32 alpha : sample(r, other, 0).opacity) AUREA_CHECK_NEAR(alpha, 0, 1e-6);
    const auto shape = r.engine.add_shape3d(0); AUREA_CHECK(shape.ok()); if (!shape.ok()) return;
    AUREA_CHECK(!r.add(*shape).ok()); AUREA_CHECK(!r.engine.apply_text_preset(*shape, 0));
    AUREA_CHECK(!r.engine.apply_preset(*shape, preset)); AUREA_CHECK(!r.engine.apply_preset(*shape, effectPreset));
    r.layer(id)->locked = true;
    AUREA_CHECK(!r.engine.apply_text_preset(id, 0)); AUREA_CHECK(!r.engine.set_text_anim_param(id, 0, text::kPosX, 10));
}
