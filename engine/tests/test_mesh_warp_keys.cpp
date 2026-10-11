#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/effects/MeshWarp.hpp"
#include <filesystem>

using namespace aurea;
namespace {
struct MeshKeyFixture {
    Engine engine;
    u64 layer = 0;
    u32 effect = 0;
    MeshKeyFixture() {
        EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
        AUREA_CHECK(engine.initialize(config).ok());
        AUREA_CHECK(engine.new_project(320, 180, 30, nullptr).ok());
        std::vector<u8> pixels(32 * 24 * 4, 255);
        const auto imported = engine.import_image(pixels.data(), 32, 24, "mesh");
        AUREA_CHECK(imported.ok());
        if (!imported.ok()) return;
        layer = *imported;
        Command add; add.type = CommandType::EffectAdd;
        add.effect_add = {LayerId::unpack(layer), effect_type_id(kMeshWarpKey), kInvalidIndex};
        AUREA_CHECK(engine.apply_command(add).ok());
        bridge::LayerEffectRow row{}; char names[2048]{};
        AUREA_CHECK_EQ(engine.query_layer_effects(layer, &row, 1, names, sizeof(names)), 1u);
        effect = row.effectId;
    }
    ~MeshKeyFixture() { engine.shutdown(); }
    void seek(i64 frame) {
        Command command; command.type = CommandType::PlaybackSeek;
        command.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(engine.apply_command(command).ok());
    }
    Layer* model() {
        auto& timeline = engine.project()->timeline();
        return timeline.composition(timeline.current())->layer(LayerId::unpack(layer));
    }
    std::vector<f32> mesh() {
        std::vector<f32> values(engine.query_mesh_warp(layer, effect, nullptr, 0));
        AUREA_CHECK_EQ(engine.query_mesh_warp(layer, effect, values.data(), static_cast<u32>(values.size())), values.size());
        return values;
    }
    Command command(CommandType type, i64 time) {
        Command out; out.type = type;
        out.keyframe.track = {LayerId::unpack(layer), TrackProperty::MeshWarp, effect, 0};
        out.keyframe.time = FrameIndex{time};
        return out;
    }
};
}

AUREA_TEST(MeshWarpKeys, ExplicitDiamondCapturesTheVisibleMeshAndPublishesOrdinaryTimelineRows) {
    MeshKeyFixture f;
    AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, f.effect, 9, 0, .3f, .32f, false, false));
    const auto before = f.mesh();
    AUREA_CHECK(f.engine.mesh_warp_toggle_key(f.layer, f.effect));
    const auto after = f.mesh();
    for (usize i = Engine::kMeshWarpHeaderFloats; i < before.size(); ++i)
        AUREA_CHECK_NEAR(after[i], before[i], 0);
    AUREA_CHECK_NEAR(after[2], 1, 0); AUREA_CHECK_NEAR(after[3], 1, 0);
    bridge::KeyframeRow keys[4]{};
    AUREA_CHECK_EQ(f.engine.query_keyframes(f.layer, keys, 4), 1u);
    AUREA_CHECK_EQ(keys[0].property, 44u); AUREA_CHECK_EQ(keys[0].effectIndex, f.effect);
    AUREA_CHECK_EQ(keys[0].paramIndex, 0u); AUREA_CHECK_EQ(keys[0].time, 0);
    AUREA_CHECK_EQ(keys[0].interpolation, static_cast<u32>(Interpolation::Linear));
    bridge::LayerRow layer{}; char names[2048]{};
    AUREA_CHECK_EQ(f.engine.query_layers(&layer, 1, names, sizeof(names)), 1u);
    AUREA_CHECK_EQ(layer.keyframeCount, 1u);
    bridge::KeyframeIndexRow index{}; u32 layers = 0;
    AUREA_CHECK_EQ(f.engine.query_all_keyframes(nullptr, 0, nullptr, 0, &layers), 1u);
    AUREA_CHECK_EQ(layers, 1u);
    AUREA_CHECK_EQ(f.engine.query_all_keyframes(&index, 1, keys, 4, &layers), 1u);
    AUREA_CHECK_EQ(index.layerId, f.layer); AUREA_CHECK_EQ(index.count, 1u);
    AUREA_CHECK_EQ(f.model()->tracks.size(), 0u); // no synthetic serialized scalar tracks
}

AUREA_TEST(MeshWarpKeys, MarkDragInterpolateMoveAndRemovePreserveTheMesh) {
    MeshKeyFixture f;
    AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, f.effect, 9, 0, .2f, .2f, false, false));
    AUREA_CHECK(f.engine.mesh_warp_toggle_key(f.layer, f.effect));
    f.seek(30);
    AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, f.effect, 9, 0, .8f, .8f, false, false));
    f.seek(15);
    constexpr usize vertex = Engine::kMeshWarpHeaderFloats + 9 * kMeshWarpVertexFloats;
    AUREA_CHECK_NEAR(f.mesh()[vertex], .5f, 1e-6f);
    Command hold; hold.type = CommandType::KeyframeSetInterpolation;
    hold.keyframe_interp.track = {LayerId::unpack(f.layer), TrackProperty::MeshWarp, f.effect, 0};
    hold.keyframe_interp.time = FrameIndex{0}; hold.keyframe_interp.interp = Interpolation::Hold;
    AUREA_CHECK(f.engine.apply_command(hold).ok());
    AUREA_CHECK_NEAR(f.mesh()[vertex], .2f, 1e-6f);
    Command move; move.type = CommandType::KeyframeMove;
    move.keyframe_move.track = hold.keyframe_interp.track;
    move.keyframe_move.fromTime = FrameIndex{30}; move.keyframe_move.toTime = FrameIndex{0};
    AUREA_CHECK(!f.engine.apply_command(move).ok()); // collision never overwrites another key
    move.keyframe_move.toTime = FrameIndex{45};
    AUREA_CHECK(f.engine.apply_command(move).ok());
    AUREA_CHECK(f.engine.apply_command(f.command(CommandType::KeyframeDelete, 0)).ok());
    const auto last = f.mesh();
    AUREA_CHECK(f.engine.apply_command(f.command(CommandType::KeyframeDelete, 45)).ok());
    const auto frozen = f.mesh();
    for (usize i = Engine::kMeshWarpHeaderFloats; i < last.size(); ++i) AUREA_CHECK_NEAR(last[i], frozen[i], 0);
    AUREA_CHECK_NEAR(frozen[3], 0, 0);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(f.engine.apply_command(undo).ok());
    bridge::KeyframeRow row{}; AUREA_CHECK_EQ(f.engine.query_keyframes(f.layer, &row, 1), 1u);
    AUREA_CHECK_EQ(row.time, 45);
}

AUREA_TEST(MeshWarpKeys, OffsetKeysRemainLocalAndHiddenEndpointsAreReportedWithoutTruncatingTheBatch) {
    MeshKeyFixture f;
    Layer* model = f.model(); model->start = FrameIndex{10}; model->end = FrameIndex{40}; model->offset = FrameIndex{4};
    f.seek(15); AUREA_CHECK(f.engine.mesh_warp_toggle_key(f.layer, f.effect));
    auto extra = f.command(CommandType::KeyframeInsert, 100);
    AUREA_CHECK(f.engine.apply_command(extra).ok());
    bridge::KeyframeRow keys[2]{};
    AUREA_CHECK_EQ(f.engine.query_keyframes(f.layer, keys, 2), 2u);
    AUREA_CHECK_EQ(keys[0].time, 9); AUREA_CHECK_EQ(keys[0].timelineFlags, 0u);
    AUREA_CHECK((keys[1].timelineFlags & bridge::kKeyframeTimelineHidden) != 0);
    bridge::KeyframeIndexRow index{}; index.count = 1234; u32 layers = 0;
    AUREA_CHECK_EQ(f.engine.query_all_keyframes(&index, 1, keys, 1, &layers), 2u);
    AUREA_CHECK_EQ(index.count, 1234u); // retry contract: insufficient capacity writes nothing
}

AUREA_TEST(MeshWarpKeys, ScalarEditsAndExpressionsCannotCreateASecondFakeMeshAnimation) {
    MeshKeyFixture f; AUREA_CHECK(f.engine.mesh_warp_toggle_key(f.layer, f.effect));
    auto value = f.command(CommandType::KeyframeSetValue, 0); value.keyframe.value = 7;
    AUREA_CHECK(!f.engine.apply_command(value).ok());
    AUREA_CHECK(!f.engine.set_expression(f.layer, 44, f.effect, 0, "time").ok());
    AUREA_CHECK_EQ(f.model()->tracks.size(), 0u);
    Command lock; lock.type = CommandType::LayerSetLocked; lock.layer_locked.layer = LayerId::unpack(f.layer); lock.layer_locked.locked = true;
    AUREA_CHECK(f.engine.apply_command(lock).ok());
    AUREA_CHECK(!f.engine.mesh_warp_toggle_key(f.layer, f.effect));
}


AUREA_TEST(MeshWarpKeys, MixedSelectionsAndClipboardPreserveGeometryAndRejectPartialMoves) {
    MeshKeyFixture f;
    AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, f.effect, 9, 0, .2f, .25f, true, false));
    f.seek(20); AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, f.effect, 9, 0, .8f, .75f, false, false));
    Command scalar; scalar.type = CommandType::KeyframeInsert;
    scalar.keyframe.track = {LayerId::unpack(f.layer), TrackProperty::Opacity, 0, 0};
    scalar.keyframe.time = FrameIndex{0}; scalar.keyframe.value = .3f;
    AUREA_CHECK(f.engine.apply_command(scalar).ok());
    scalar.keyframe.time = FrameIndex{20}; scalar.keyframe.value = .9f;
    AUREA_CHECK(f.engine.apply_command(scalar).ok());
    const i64 refs[] = {44, f.effect, 0, 0, static_cast<i64>(TrackProperty::Opacity), 0, 0, 0};
    AUREA_CHECK_EQ(f.engine.edit_keyframe_selection(f.layer, refs, 2, 20, false), 0u);
    AUREA_CHECK_EQ(f.model()->tracks.find(TrackProperty::Opacity, 0, 0)->keys[0].time.value, 0);
    AUREA_CHECK_EQ(mesh_warp::animation(f.model()->effects[0])->keys[0].frame, 0);
    AUREA_CHECK_EQ(f.engine.copy_keyframe_selection(f.layer, refs, 2), 2u);
    AUREA_CHECK((f.engine.clipboard_state() & 8u) != 0);
    AUREA_CHECK_EQ(f.engine.paste_keyframes(&f.layer, 1, 40), 2u);
    const auto* data = mesh_warp::animation(f.model()->effects[0]);
    AUREA_CHECK(data != nullptr); if (!data) return;
    AUREA_CHECK_EQ(data->keys.size(), 3u);
    AUREA_CHECK_EQ(data->keys.back().frame, 40);
    AUREA_CHECK_EQ(data->keys.back().values, data->keys.front().values);
    AUREA_CHECK_NEAR(f.model()->tracks.find(TrackProperty::Opacity, 0, 0)->keys.back().value, .3f, 0);
    const i64 moved[] = {44, f.effect, 0, 40, static_cast<i64>(TrackProperty::Opacity), 0, 0, 40};
    AUREA_CHECK_EQ(f.engine.edit_keyframe_selection(f.layer, moved, 2, 5, false), 2u);
    AUREA_CHECK_EQ(mesh_warp::animation(f.model()->effects[0])->keys.back().frame, 45);
    const i64 remove[] = {44, f.effect, 0, 45, static_cast<i64>(TrackProperty::Opacity), 0, 0, 45};
    AUREA_CHECK_EQ(f.engine.edit_keyframe_selection(f.layer, remove, 2, 0, true), 2u);
    AUREA_CHECK_EQ(mesh_warp::animation(f.model()->effects[0])->keys.size(), 2u);
    AUREA_CHECK_EQ(f.model()->tracks.find(TrackProperty::Opacity, 0, 0)->keys.size(), 2u);
}

AUREA_TEST(MeshWarpKeys, WholeAnimationClipboardMatchesEffectOrdinalAndSurvivesProjectRoundTrip) {
    MeshKeyFixture f;
    AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, f.effect, 9, 0, .2f, .25f, true, false));
    f.seek(20); AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, f.effect, 9, 0, .8f, .75f, false, false));
    Command second; second.type = CommandType::EffectAdd;
    second.effect_add = {LayerId::unpack(f.layer), effect_type_id(kMeshWarpKey), kInvalidIndex};
    AUREA_CHECK(f.engine.apply_command(second).ok());
    const u32 secondId = f.model()->effects[1].id;
    AUREA_CHECK(f.engine.mesh_warp_drag(f.layer, secondId, 9, 0, .35f, .4f, true, false));
    AUREA_CHECK_EQ(f.engine.copy_animation(f.layer), 3u);
    AUREA_CHECK_EQ(f.engine.paste_keyframes(&f.layer, 1, 60), 3u);
    const auto first = *mesh_warp::animation(f.model()->effects[0]);
    const auto last = *mesh_warp::animation(f.model()->effects[1]);
    AUREA_CHECK_EQ(first.keys.size(), 4u); AUREA_CHECK_EQ(last.keys.size(), 2u);
    AUREA_CHECK_EQ(first.keys[2].frame, 60); AUREA_CHECK_EQ(last.keys[1].frame, 80);
    AUREA_CHECK_EQ(first.keys[2].values, first.keys[0].values);
    AUREA_CHECK_EQ(last.keys[1].values, last.keys[0].values);
    const auto path = std::filesystem::absolute("build/reference/astra-mesh-warp-roundtrip.aurea");
    std::filesystem::create_directories(path.parent_path());
    AUREA_CHECK(f.engine.save_project(path.string().c_str()).ok());
    AUREA_CHECK(f.engine.load_project(path.string().c_str()).ok());
    const auto* firstRead = mesh_warp::animation(f.model()->effects[0]);
    const auto* lastRead = mesh_warp::animation(f.model()->effects[1]);
    AUREA_CHECK(firstRead && lastRead); if (!firstRead || !lastRead) return;
    AUREA_CHECK_EQ(firstRead->keys.size(), first.keys.size()); AUREA_CHECK_EQ(lastRead->keys.size(), last.keys.size());
    for (usize i = 0; i < first.keys.size(); ++i) {
        AUREA_CHECK_EQ(firstRead->keys[i].frame, first.keys[i].frame);
        AUREA_CHECK_EQ(firstRead->keys[i].values, first.keys[i].values);
    }
    AUREA_CHECK_EQ(f.model()->tracks.size(), 0u);
}

AUREA_TEST(MeshWarpKeys, IncompatibleGridPasteCannotPartiallyApplyMixedScalarAnimation) {
    MeshKeyFixture f;
    AUREA_CHECK(f.engine.mesh_warp_toggle_key(f.layer, f.effect));
    Command scalar; scalar.type = CommandType::KeyframeInsert;
    scalar.keyframe.track = {LayerId::unpack(f.layer), TrackProperty::Opacity, 0, 0};
    scalar.keyframe.time = FrameIndex{0}; scalar.keyframe.value = .25f;
    AUREA_CHECK(f.engine.apply_command(scalar).ok());
    AUREA_CHECK_EQ(f.engine.copy_keyframes(f.layer, 0), 2u);
    f.model()->effects[0].params[0].constant = ParamValue::scalar(3);
    AUREA_CHECK_EQ(f.engine.paste_keyframes(&f.layer, 1, 60), 0u);
    AUREA_CHECK_EQ(f.model()->tracks.find(TrackProperty::Opacity, 0, 0)->keys.size(), 1u);
    AUREA_CHECK_EQ(f.model()->effects[0].meshes[0].keys.size(), 1u);
    AUREA_CHECK_EQ(f.model()->effects[0].meshes[0].rows, 7u);
}
