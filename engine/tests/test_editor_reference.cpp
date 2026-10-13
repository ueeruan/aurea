#include "TestFramework.hpp"
#include "aurea/Engine.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;

namespace {
struct EditorReferenceRig {
    Engine engine;
    EditorReferenceRig() {
        EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
        AUREA_CHECK(engine.initialize(config).ok());
        AUREA_CHECK(engine.new_project(64, 64, 30, nullptr).ok());
    }
    ~EditorReferenceRig() { engine.shutdown(); }
    Composition* comp() { return engine.project()->timeline().composition(engine.project()->timeline().current()); }
    u64 add(const char* name, LayerKind kind = LayerKind::Shape) {
        return comp()->add_layer(kind, name).pack();
    }
    Layer* layer(u64 id) { return comp()->layer(LayerId::unpack(id)); }
};

const Layer* named(const Composition& comp, const char* name) {
    for (u32 i = 0; i < comp.order().size(); ++i) {
        const Layer* layer = comp.layer(comp.order().at(i));
        if (layer && layer->name == name) return layer;
    }
    return nullptr;
}
}

AUREA_TEST(EditorReference, PrecomposeRemapsInternalMattesWithoutRequiringAParent) {
    EditorReferenceRig rig;
    const u64 outside = rig.add("outside"), content = rig.add("content");
    (void)rig.add("gap");
    const u64 source = rig.add("matte");
    rig.layer(content)->matteSource = LayerId::unpack(source);
    rig.layer(content)->matteMode = MatteMode::LumaInverted;
    const u64 selected[]{source, content, content};
    const auto result = rig.engine.precompose(selected, 3);
    AUREA_CHECK(result.ok()); if (!result.ok()) return;
    AUREA_CHECK_EQ(rig.comp()->order().size(), 3u);
    AUREA_CHECK(rig.layer(outside));
    const auto* group = rig.layer(*result);
    const auto* child = rig.engine.project()->timeline().composition(group->nested.composition);
    AUREA_CHECK(child); if (!child) return;
    AUREA_CHECK_EQ(child->order().size(), 2u);
    const auto* target = named(*child, "content");
    AUREA_CHECK(target); if (!target) return;
    AUREA_CHECK(target->matteSource.pack() != source);
    const auto* matte = child->layer(target->matteSource);
    AUREA_CHECK(matte && matte->name == "matte");
    AUREA_CHECK_EQ(target->matteMode, MatteMode::LumaInverted);
    AUREA_CHECK(rig.engine.ungroup_precomp(*result).ok());
    const Layer* ungrouped = named(*rig.comp(), "content");
    AUREA_CHECK(ungrouped); if (!ungrouped) return;
    const Layer* ungroupedMatte = rig.comp()->layer(ungrouped->matteSource);
    AUREA_CHECK(ungroupedMatte && ungroupedMatte->name == "matte");
    AUREA_CHECK_EQ(ungrouped->matteMode, MatteMode::LumaInverted);
}

AUREA_TEST(EditorReference, GroupMaskUsesStackOrderAndOneUndoForMaskAndExclude) {
    for (bool exclude : {false, true}) {
        EditorReferenceRig rig;
        const u64 outside = rig.add("outside"), bottom = rig.add("bottom");
        const u64 middle = rig.add("middle"), top = rig.add("source");
        rig.layer(bottom)->start = FrameIndex{10}; rig.layer(bottom)->end = FrameIndex{90};
        rig.layer(bottom)->offset = FrameIndex{23};
        rig.layer(bottom)->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{35}, 60);
        rig.layer(middle)->start = FrameIndex{15}; rig.layer(middle)->end = FrameIndex{80};
        rig.layer(top)->start = FrameIndex{20}; rig.layer(top)->end = FrameIndex{70};
        rig.layer(top)->blendMode = BlendMode::Multiply;
        const u64 selected[]{top, bottom, middle, bottom}; // Deliberately not display order.
        const u32 history = rig.engine.history().depth();
        const auto result = rig.engine.group_and_mask(selected, 4, exclude, "masked group");
        AUREA_CHECK(result.ok()); if (!result.ok()) continue;
        AUREA_CHECK_EQ(rig.engine.history().depth(), history + 1);
        AUREA_CHECK_EQ(rig.comp()->order().size(), 2u);
        AUREA_CHECK_EQ(rig.comp()->order().at(0).pack(), outside);
        AUREA_CHECK_EQ(rig.comp()->order().at(1).pack(), *result);
        const Layer* group = rig.layer(*result);
        AUREA_CHECK_EQ(group->start.value, i64{10}); AUREA_CHECK_EQ(group->end.value, i64{90});
        AUREA_CHECK_EQ(group->offset.value, i64{10});
        const CompositionId childId = group->nested.composition;
        const auto* child = rig.engine.project()->timeline().composition(childId);
        AUREA_CHECK(child); if (!child) continue;
        AUREA_CHECK_EQ(child->layer(child->order().at(0))->name, std::string("bottom"));
        AUREA_CHECK_EQ(child->layer(child->order().at(1))->name, std::string("middle"));
        AUREA_CHECK_EQ(child->layer(child->order().at(2))->name, std::string("source"));
        AUREA_CHECK_EQ(named(*child, "source")->blendMode, exclude ? BlendMode::Exclude : BlendMode::Mask);
        const Layer* kept = named(*child, "bottom");
        AUREA_CHECK_EQ(kept->start.value, i64{10}); AUREA_CHECK_EQ(kept->offset.value, i64{23});
        AUREA_CHECK_EQ(kept->tracks.find(TrackProperty::PositionX)->keys[0].time.value, i64{35});
        AUREA_CHECK(!kept->matteSource.valid()); // Composite masking, not per-layer alpha.
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(rig.engine.apply_command(undo).ok());
        AUREA_CHECK_EQ(rig.comp()->order().size(), 4u);
        AUREA_CHECK_EQ(rig.layer(top)->blendMode, BlendMode::Multiply);
        Command redo; redo.type = CommandType::Redo;
        AUREA_CHECK(rig.engine.apply_command(redo).ok());
        AUREA_CHECK(rig.layer(*result));
        child = rig.engine.project()->timeline().composition(childId);
        AUREA_CHECK_EQ(named(*child, "source")->blendMode, exclude ? BlendMode::Exclude : BlendMode::Mask);
        const char* path = exclude ? "editor-group-exclude.aurea" : "editor-group-mask.aurea";
        AUREA_CHECK(rig.engine.save_project(path).ok());
        AUREA_CHECK(rig.engine.load_project(path).ok());
        const Layer* reopened = named(*rig.comp(), "masked group");
        AUREA_CHECK(reopened); if (reopened) {
            child = rig.engine.project()->timeline().composition(reopened->nested.composition);
            AUREA_CHECK(child && named(*child, "source"));
            if (child && named(*child, "source"))
                AUREA_CHECK_EQ(named(*child, "source")->blendMode, exclude ? BlendMode::Exclude : BlendMode::Mask);
        }
        std::remove(path);
    }
}

AUREA_TEST(EditorReference, GroupMaskRejectsMissingLockedNonvisualAndSingleSelectionsAtomically) {
    EditorReferenceRig rig;
    const u64 a = rig.add("a"), b = rig.add("b"), control = rig.add("control", LayerKind::Null);
    const u32 history = rig.engine.history().depth();
    const u64 missing[]{a, 0xffffffffffffffffull}, single[]{a, a}, nonvisual[]{a, control}, valid[]{b, a};
    for (const u64* ids : {missing, single, nonvisual}) {
        AUREA_CHECK(!rig.engine.group_and_mask(ids, 2, false).ok());
        AUREA_CHECK_EQ(rig.engine.history().depth(), history);
        AUREA_CHECK_EQ(rig.comp()->order().size(), 3u);
    }
    rig.layer(b)->locked = true;
    AUREA_CHECK(!rig.engine.group_and_mask(valid, 2, true).ok());
    AUREA_CHECK_EQ(rig.engine.history().depth(), history);
    AUREA_CHECK_EQ(rig.comp()->order().size(), 3u);
    AUREA_CHECK_EQ(rig.layer(b)->blendMode, BlendMode::Normal);
}

AUREA_TEST(EditorReference, GroupMaskConsumesQueuedReorderBeforeChoosingTheSource) {
    EditorReferenceRig rig;
    const u64 a = rig.add("a"), b = rig.add("b");
    (void)rig.add("outside");
    Command reorder; reorder.type = CommandType::LayerReorder;
    reorder.layer_reorder.layer = LayerId::unpack(a); reorder.layer_reorder.newIndex = 2;
    AUREA_CHECK_EQ(rig.engine.submit_commands(&reorder, 1), 1u);
    const u64 selected[]{a, b};
    const auto result = rig.engine.group_and_mask(selected, 2, false);
    AUREA_CHECK(result.ok()); if (!result.ok()) return;
    const auto* child = rig.engine.project()->timeline().composition(rig.layer(*result)->nested.composition);
    AUREA_CHECK(child); if (!child) return;
    AUREA_CHECK_EQ(named(*child, "a")->blendMode, BlendMode::Mask);
    AUREA_CHECK_EQ(named(*child, "b")->blendMode, BlendMode::Normal);
}

AUREA_TEST(EditorReference, UngroupMaskRejectsExtraVisualTargetsOnlyDuringTheMasksActiveInterval) {
    for (bool exclude : {false, true}) for (u32 scenario = 0; scenario < 6; ++scenario) {
        EditorReferenceRig rig;
        u64 outside = 0;
        if (scenario != 5 && scenario != 2) outside = rig.add("outside", scenario == 3 ? LayerKind::Null : LayerKind::Shape);
        const u64 content = rig.add("content"), source = rig.add("source");
        rig.layer(content)->start = FrameIndex{10}; rig.layer(content)->end = FrameIndex{30};
        rig.layer(source)->start = FrameIndex{15}; rig.layer(source)->end = FrameIndex{25};
        const u64 ids[]{source, content};
        const auto grouped = rig.engine.group_and_mask(ids, 2, exclude);
        AUREA_CHECK(grouped.ok()); if (!grouped.ok()) continue;
        if (scenario == 2) outside = rig.add("outside"); // Above the group: unaffected.
        if (outside) {
            rig.layer(outside)->start = FrameIndex{scenario == 1 ? 25 : 24};
            rig.layer(outside)->end = FrameIndex{28};
            rig.layer(outside)->visible = scenario != 4;
        }
        const u32 before = rig.engine.history().depth();
        std::string why;
        const auto ungrouped = rig.engine.ungroup_precomp(*grouped, &why);
        if (scenario == 0) {
            AUREA_CHECK(!ungrouped.ok());
            AUREA_CHECK_EQ(why, std::string("a mascara do grupo afetaria camadas externas"));
            AUREA_CHECK_EQ(rig.engine.history().depth(), before);
            AUREA_CHECK(rig.layer(*grouped));
        } else {
            AUREA_CHECK(ungrouped.ok());
            AUREA_CHECK(why.empty());
            AUREA_CHECK(!rig.layer(*grouped));
        }
    }
}

AUREA_TEST(EditorReference, KeyNavigationUsesCompositionFramesAndVisibleClipEndpoints) {
    EditorReferenceRig rig;
    const u64 id = rig.add("trimmed");
    Layer* layer = rig.layer(id);
    layer->start = FrameIndex{100}; layer->end = FrameIndex{160}; layer->offset = FrameIndex{20};
    layer->speed = 3; layer->reversed = true; // Source playback does not alter local key timing.
    Track& track = layer->tracks.get_or_create(TrackProperty::PositionX);
    for (i64 frame : {10, 20, 35, 60, 80, 90}) (void)track.set(FrameIndex{frame}, static_cast<f32>(frame));
    Engine::KeyframeNavigation nav;
    const u32 property = static_cast<u32>(TrackProperty::PositionX);
    auto query = [&](i64 frame) { return rig.engine.query_keyframe_navigation(id, property, kInvalidIndex, 0, frame, nav); };
    AUREA_CHECK(query(130));
    AUREA_CHECK_EQ(nav.visibleCount, 4u); AUREA_CHECK_EQ(nav.flags, 14u);
    AUREA_CHECK_EQ(nav.localFrame, i64{50}); AUREA_CHECK_EQ(nav.previousFrame, i64{115});
    AUREA_CHECK_EQ(nav.nextFrame, i64{140}); AUREA_CHECK_EQ(nav.segmentStart, i64{35});
    AUREA_CHECK_EQ(nav.segmentEnd, i64{60});
    AUREA_CHECK(query(100)); AUREA_CHECK_EQ(nav.flags, 13u); AUREA_CHECK_EQ(nav.nextFrame, i64{115});
    AUREA_CHECK_EQ(nav.segmentStart, i64{20});
    AUREA_CHECK(query(160)); AUREA_CHECK_EQ(nav.flags, 11u); AUREA_CHECK_EQ(nav.previousFrame, i64{140});
    AUREA_CHECK_EQ(nav.segmentStart, i64{60}); AUREA_CHECK_EQ(nav.segmentEnd, i64{80});
    AUREA_CHECK(query(115)); AUREA_CHECK_EQ(nav.flags, 15u);
    AUREA_CHECK_EQ(nav.segmentStart, i64{35}); AUREA_CHECK_EQ(nav.segmentEnd, i64{60});
    AUREA_CHECK(query(95)); AUREA_CHECK_EQ(nav.flags, 4u); AUREA_CHECK_EQ(nav.nextFrame, i64{100});
    AUREA_CHECK(query(180)); AUREA_CHECK_EQ(nav.flags, 2u); AUREA_CHECK_EQ(nav.previousFrame, i64{160});
    AUREA_CHECK(rig.engine.query_keyframe_navigation(id, static_cast<u32>(TrackProperty::Opacity), kInvalidIndex, 0, 130, nav));
    AUREA_CHECK_EQ(nav.visibleCount, 0u); AUREA_CHECK_EQ(nav.flags, 0u);
    AUREA_CHECK(!rig.engine.query_keyframe_navigation(0xffffffffffffffffull, property, kInvalidIndex, 0, 130, nav));
    AUREA_CHECK_EQ(nav.flags, 0u);
    (void)layer->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{50}, .5f);
    AUREA_CHECK(rig.engine.query_keyframe_navigation(id, static_cast<u32>(TrackProperty::Opacity), kInvalidIndex, 0, 130, nav));
    AUREA_CHECK_EQ(nav.visibleCount, 1u); AUREA_CHECK_EQ(nav.flags, 1u); // No easing interval for one key.
    layer->timeRemap.set(FrameIndex{20}, 0); layer->timeRemap.set(FrameIndex{80}, 100);
    AUREA_CHECK(rig.engine.query_keyframe_navigation(id, static_cast<u32>(TrackProperty::TimeRemap), kInvalidIndex, 0, 130, nav));
    AUREA_CHECK_EQ(nav.flags, 14u); AUREA_CHECK_EQ(nav.previousFrame, i64{100}); AUREA_CHECK_EQ(nav.nextFrame, i64{160});
}

AUREA_TEST(EditorReference, KeyNavigationConsumesQueuedInsertionsAndValidatesProperty) {
    EditorReferenceRig rig;
    const u64 id = rig.add("keys");
    Command insert; insert.type = CommandType::KeyframeInsert;
    insert.keyframe.track = TrackRef{LayerId::unpack(id), TrackProperty::Opacity, kInvalidIndex, 0};
    insert.keyframe.time = FrameIndex{20}; insert.keyframe.value = .4f;
    AUREA_CHECK_EQ(rig.engine.submit_commands(&insert, 1), 1u);
    Engine::KeyframeNavigation nav;
    AUREA_CHECK(rig.engine.query_keyframe_navigation(id, static_cast<u32>(TrackProperty::Opacity), kInvalidIndex, 0, 20, nav));
    AUREA_CHECK_EQ(nav.visibleCount, 1u); AUREA_CHECK_EQ(nav.flags, 1u);
    AUREA_CHECK(!rig.engine.query_keyframe_navigation(id, static_cast<u32>(TrackProperty::_Count), kInvalidIndex, 0, 20, nav));
    AUREA_CHECK_EQ(nav.visibleCount, 0u); AUREA_CHECK_EQ(nav.flags, 0u);
}

AUREA_TEST(EditorReference, TransportUsesOnlyMarkersWithoutAnActiveLayer) {
    EditorReferenceRig rig;
    const u64 id = rig.add("animated but not selected");
    (void)rig.layer(id)->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{20}, .5f);
    rig.comp()->put_marker(Marker{FrameIndex{10}});
    rig.comp()->put_marker(Marker{FrameIndex{40}});
    i64 target = -1;
    AUREA_CHECK(rig.engine.query_timeline_navigation(0, 10, 1, target));
    AUREA_CHECK_EQ(target, i64{40}); // The unselected key at 20 is ignored.
    AUREA_CHECK(rig.engine.query_timeline_navigation(0, 40, -1, target));
    AUREA_CHECK_EQ(target, i64{10});
    AUREA_CHECK(!rig.engine.query_timeline_navigation(0, 40, 1, target));
    AUREA_CHECK_EQ(target, i64{40}); // No wrap or frame-step fallback.
    AUREA_CHECK(!rig.engine.query_timeline_navigation(0, 10, -1, target));
    AUREA_CHECK_EQ(target, i64{10});
    AUREA_CHECK(!rig.engine.query_timeline_navigation(0, 25, 0, target));
    AUREA_CHECK_EQ(target, i64{25});
    AUREA_CHECK(rig.engine.query_timeline_navigation(0xffffffffffffffffull, 10, 1, target));
    AUREA_CHECK_EQ(target, i64{40}); // A stale UI selection still navigates markers.
    AUREA_CHECK(rig.comp()->remove_marker_at(FrameIndex{10}));
    AUREA_CHECK(rig.comp()->remove_marker_at(FrameIndex{40}));
    AUREA_CHECK(!rig.engine.query_timeline_navigation(0, 10, 1, target));
    AUREA_CHECK_EQ(target, i64{10});
    AUREA_CHECK(rig.engine.query_timeline_navigation(id, 10, 1, target));
    AUREA_CHECK_EQ(target, i64{20});
}

AUREA_TEST(EditorReference, TransportMergesActiveLayerKeysAndMarkersByDistance) {
    EditorReferenceRig rig;
    const u64 id = rig.add("trimmed active"), other = rig.add("other");
    Layer* layer = rig.layer(id);
    layer->start = FrameIndex{100}; layer->end = FrameIndex{160}; layer->offset = FrameIndex{20};
    layer->speed = 3; layer->reversed = true;
    Track& track = layer->tracks.get_or_create(TrackProperty::PositionX);
    for (i64 frame : {10, 20, 35, 60, 80, 90}) (void)track.set(FrameIndex{frame}, static_cast<f32>(frame));
    (void)layer->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{35}, .5f); // Same-frame keys never stall.
    (void)rig.layer(other)->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{124}, .5f);
    for (i64 frame : {90, 125, 150}) rig.comp()->put_marker(Marker{FrameIndex{frame}});
    auto expect = [&](i64 now, i32 direction, i64 expected) {
        i64 target = -1;
        AUREA_CHECK(rig.engine.query_timeline_navigation(id, now, direction, target));
        AUREA_CHECK_EQ(target, expected);
    };
    expect(130, -1, 125); expect(130, 1, 140); // A marker and key can each be nearer.
    expect(120, -1, 115); expect(120, 1, 125); // Other layer's key at 124 is ignored.
    expect(115, 1, 125); expect(125, -1, 115); // Strict neighbors skip coincident events.
    expect(99, 1, 100); expect(159, 1, 160); // Both authored clip endpoints remain anchors.
    expect(200, -1, 160); // Hidden key at 170 must not participate after trimming.
    expect(100, -1, 90); // Hidden key at 90 and marker at 90 are distinct sources.
    i64 target = -1;
    AUREA_CHECK(!rig.engine.query_timeline_navigation(id, 160, 1, target));
    AUREA_CHECK_EQ(target, i64{160});
    (void)layer->tracks.get_or_create(TrackProperty::EffectParam, 7, 0).set(FrameIndex{46}, .5f);
    Mask mask; mask.id = 0; mask.pathKeys.push_back(MaskPathKey{47, 1, {}});
    layer->masks.push_back(std::move(mask));
    (void)layer->timeRemap.set(FrameIndex{48}, 3);
    expect(125, 1, 126); expect(126, 1, 127); expect(127, 1, 128);
    expect(128, -1, 127); expect(127, -1, 126); expect(126, -1, 125);
}

AUREA_TEST(EditorReference, TransportReachesProjectMarkersBeyondClipsWithoutChangingRegularSeek) {
    EditorReferenceRig rig;
    const u64 id = rig.add("short");
    rig.comp()->set_duration(FrameIndex{300});
    rig.layer(id)->end = FrameIndex{60};
    (void)rig.layer(id)->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{20}, .5f);
    AUREA_CHECK(rig.engine.edit_marker(-1, 150, 0xFFF7C34Fu, "after clip")); // A legal composition marker.
    rig.comp()->put_marker(Marker{FrameIndex{350}}); // Malformed out-of-composition marker is ignored.
    rig.engine.set_content_bounded_playback(true);
    AUREA_CHECK_EQ(rig.engine.query_navigation_end(), i64{60});
    for (u64 selected : {u64{0}, id}) {
        i64 target = -1;
        AUREA_CHECK(rig.engine.query_timeline_navigation(selected, 60, 1, target));
        AUREA_CHECK_EQ(target, i64{150});
        AUREA_CHECK(rig.engine.seek_timeline_event(target));
        AUREA_CHECK_EQ(rig.engine.read_status().playhead.value, i64{150});
        AUREA_CHECK_EQ(rig.engine.project()->timeline().playhead().value, i64{150});
        for (u32 tick = 0; tick < 3; ++tick) {
            AUREA_CHECK(rig.engine.render_frame(tick != 0).ok());
            AUREA_CHECK_EQ(rig.engine.read_status().playhead.value, i64{150});
            AUREA_CHECK_EQ(rig.engine.project()->timeline().playhead().value, i64{150});
        }
        AUREA_CHECK(!rig.engine.query_timeline_navigation(selected, 150, 1, target));
        Command ordinary; ordinary.type = CommandType::PlaybackSeek;
        ordinary.seek.time = tick_at(FrameIndex{150}, 30);
        AUREA_CHECK(rig.engine.apply_command(ordinary).ok());
        AUREA_CHECK_EQ(rig.engine.read_status().playhead.value, i64{60}); // Restore the ordinary seek bound.
    }
    AUREA_CHECK(!rig.engine.seek_timeline_event(-1));
    AUREA_CHECK(!rig.engine.seek_timeline_event(300));
    AUREA_CHECK(!rig.engine.seek_timeline_event(160)); // Beyond content without an authored marker.
    AUREA_CHECK(rig.engine.seek_timeline_event(150));
    AUREA_CHECK(!rig.engine.seek_timeline_event(160));
    AUREA_CHECK_EQ(rig.engine.read_status().playhead.value, i64{150}); // Invalid seeks cannot discard a valid cursor.
    AUREA_CHECK(rig.engine.delete_marker(150));
    AUREA_CHECK(rig.engine.render_frame().ok());
    AUREA_CHECK_EQ(rig.engine.read_status().playhead.value, i64{60}); // Deleted markers lose the exception.
    AUREA_CHECK(rig.engine.edit_marker(-1, 150, 0xFFF7C34Fu, "after clip"));
    AUREA_CHECK(rig.engine.seek_timeline_event(150));
    Command play; play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(rig.engine.apply_command(play).ok());
    AUREA_CHECK(rig.engine.read_status().playing);
    AUREA_CHECK(rig.engine.read_status().playhead.value < 60); // Existing content-bounded restart.
}

AUREA_TEST(EditorReference, TransportConsumesQueuedKeyInsertions) {
    EditorReferenceRig rig;
    const u64 id = rig.add("keys");
    rig.comp()->put_marker(Marker{FrameIndex{40}});
    Command insert; insert.type = CommandType::KeyframeInsert;
    insert.keyframe.track = TrackRef{LayerId::unpack(id), TrackProperty::Opacity, kInvalidIndex, 0};
    insert.keyframe.time = FrameIndex{20}; insert.keyframe.value = .4f;
    AUREA_CHECK_EQ(rig.engine.submit_commands(&insert, 1), 1u);
    i64 target = -1;
    AUREA_CHECK(rig.engine.query_timeline_navigation(id, 10, 1, target));
    AUREA_CHECK_EQ(target, i64{20}); // Does not depend on the UI's stale keyframe cache.
    AUREA_CHECK(rig.engine.query_timeline_navigation(id, 20, 1, target));
    AUREA_CHECK_EQ(target, i64{40});
}

#if defined(AUREA_TEST_VULKAN)
#include "VulkanBackend.hpp"

AUREA_TEST(EditorReferenceGpu, GroupMaskCutsCompositeAlphaOnceForOverlappingContent) {
    vk::Backend probe; BackendConfig probeConfig; probeConfig.enableValidation = false;
    if (!probe.initialize(probeConfig).ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    probe.shutdown();
    for (bool exclude : {false, true}) {
        Engine engine; EngineConfig config; config.backend = new vk::Backend();
        config.backendConfig.enableValidation = false; config.disableAutosave = true; config.workerCount = 1;
        AUREA_CHECK(engine.initialize(config).ok());
        AUREA_CHECK(engine.new_project(32, 32, 30, nullptr).ok());
        auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
        comp->set_transparent_background(true);
        std::vector<u8> red(32 * 32 * 4), blue(red.size()), mask(red.size());
        for (usize pixel = 0; pixel < 32 * 32; ++pixel) {
            red[pixel * 4] = 255; red[pixel * 4 + 3] = 255;
            blue[pixel * 4 + 2] = 255; blue[pixel * 4 + 3] = 255;
            mask[pixel * 4] = mask[pixel * 4 + 1] = mask[pixel * 4 + 2] = 255;
            const usize x = pixel % 32;
            mask[pixel * 4 + 3] = x >= 8 && x < 24 ? 128 : 0;
        }
        const auto a = engine.import_image(red.data(), 32, 32, "bottom");
        const auto b = engine.import_image(blue.data(), 32, 32, "top content");
        const auto m = engine.import_image(mask.data(), 32, 32, "source");
        AUREA_CHECK(a.ok() && b.ok() && m.ok());
        if (a.ok() && b.ok() && m.ok()) {
            const u64 ids[]{*m, *a, *b};
            AUREA_CHECK(engine.group_and_mask(ids, 3, exclude).ok());
            std::vector<u8> rgba; u32 width = 0, height = 0;
            AUREA_CHECK(engine.capture_frame_rgba(32, rgba, width, height).ok());
            AUREA_CHECK_EQ(width, 32u); AUREA_CHECK_EQ(height, 32u);
            if (rgba.size() == 32 * 32 * 4) {
                const u8 center = rgba[(16 * 32 + 16) * 4 + 3];
                const u8 outside = rgba[(16 * 32 + 3) * 4 + 3];
                std::printf("    %s overlap alpha=%u outside=%u\n", exclude ? "exclude" : "mask", center, outside);
                AUREA_CHECK_NEAR(center, exclude ? 127 : 128, 2);
                AUREA_CHECK_NEAR(outside, exclude ? 255 : 0, 2);
            }
        }
        engine.shutdown();
    }
}
#endif
