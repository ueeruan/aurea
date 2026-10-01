// =============================================================================
//  Modo Edição (timeline magnética): aparar empurra/puxa, excluir fecha o
//  buraco, remover espaços vazios, aparar o projeto — tudo com desfazer.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/render/MaskRaster.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aurea;

namespace {

struct EditRig {
    Engine e;
    u64 a = 0, b = 0, c = 0;
    EditRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        // Três clipes em fila: A [0,30) B [30,60) C [60,90).
        a = *e.add_null(false);
        b = *e.add_null(false);
        c = *e.add_null(false);
        range(a, 0, 30);
        range(b, 30, 60);
        range(c, 60, 90);
    }
    ~EditRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    const Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void range(u64 id, i64 s, i64 en, i64 offset = -1) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        cmd.layer_range.layer = LayerId::unpack(id);
        cmd.layer_range.start = FrameIndex{s};
        cmd.layer_range.end = FrameIndex{en};
        cmd.layer_range.offset = FrameIndex{offset < 0 ? 0 : offset};
        cmd.layer_range.setOffset = offset >= 0;
        AUREA_CHECK(e.apply_command(cmd).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    bool at(u64 id, i64 s, i64 en) { const Layer* l = L(id); return l && l->start.value == s && l->end.value == en; }
};

} // namespace

namespace {
void clip_source(EditRig& r, u64 id, f32 speed = 1, bool reverse = false) {
    Asset a; a.kind = AssetKind::Video; a.duration = FrameIndex{300}; a.timebaseFps = 30;
    const AssetId source = r.e.project()->add_asset(std::move(a));
    Layer* l = r.comp()->layer(LayerId::unpack(id));
    l->kind = LayerKind::Video; l->source = source; l->speed = speed; l->reversed = reverse;
    l->offset = FrameIndex{60};
    auto& x = l->tracks.get_or_create(TrackProperty::PositionX);
    x.set(FrameIndex{0}, 0); x.set(FrameIndex{200}, 500);
}
}

AUREA_TEST(MaskAnimation, ScalarsKeepStableMaskIdsUndoAndReopen) {
    EditRig r;
    const f32 pts[] = {10,10,0,0,0,0, 80,10,0,0,0,0, 80,80,0,0,0,0, 10,80,0,0,0,0};
    const i32 first = r.e.add_mask(r.a, pts, 4, true);
    const i32 second = r.e.add_mask(r.a, pts, 4, true);
    AUREA_CHECK(first >= 0 && second > first);
    for (u32 p = 0; p < 3; ++p) AUREA_CHECK(r.e.toggle_mask_param_key(r.a, static_cast<u32>(second), p));
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{20}, 30);
    AUREA_CHECK_EQ(r.e.submit_commands(&seek, 1, nullptr, 0), 1u);
    AUREA_CHECK(r.e.set_mask_param(r.a, second, 0, 40));
    AUREA_CHECK(r.e.set_mask_param(r.a, second, 1, -20));
    AUREA_CHECK(r.e.set_mask_param(r.a, second, 2, .2f));
    Layer* layer = r.comp()->layer(LayerId::unpack(r.a));
    for (u32 p = 0; p < 3; ++p) {
        Track* tr = layer->tracks.find(TrackProperty::MaskParam, second, p);
        AUREA_CHECK(tr && tr->keys.size() == 2);
        if (tr) tr->keys[0].interp = Interpolation::Linear;
    }
    const Vec3 mid = mask::evaluate_props(*layer, layer->masks[1], 10);
    AUREA_CHECK_NEAR(mid.x, 20, .001); AUREA_CHECK_NEAR(mid.y, -10, .001); AUREA_CHECK_NEAR(mid.z, .6, .001);
    AUREA_CHECK(r.e.remove_mask(r.a, first));
    AUREA_CHECK(r.L(r.a)->masks[0].id == static_cast<u32>(second));
    AUREA_CHECK(r.L(r.a)->tracks.find(TrackProperty::MaskParam, second, 2) != nullptr);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_mask_scalars.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok()); AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const auto* reopened = r.L(r.a);
    const Vec3 loaded = mask::evaluate_props(*reopened, reopened->masks[0], 10);
    AUREA_CHECK_NEAR(loaded.x, mid.x, .001); AUREA_CHECK_NEAR(loaded.y, mid.y, .001); AUREA_CHECK_NEAR(loaded.z, mid.z, .001);
    AUREA_CHECK(r.e.remove_mask(r.a, second));
    AUREA_CHECK(r.L(r.a)->tracks.find(TrackProperty::MaskParam, second, 2) == nullptr);
    r.undo();
    AUREA_CHECK(r.L(r.a)->masks.size() == 1);
    AUREA_CHECK(r.L(r.a)->tracks.find(TrackProperty::MaskParam, second, 2) != nullptr);
    AUREA_CHECK(!r.e.set_mask_param(r.a, second, 3, 10));
    AUREA_CHECK(!r.e.toggle_mask_param_key(r.a, 999, 0));
    std::remove(path.c_str());
}

AUREA_TEST(MaskAnimation, TrimmedLocalTimeClipboardAndPathKeysUseTheActualPlayhead) {
    EditRig r; r.range(r.a, 10, 70, 5);
    const f32 pts[] = {10,10,0,0,0,0, 80,10,0,0,0,0, 80,80,0,0,0,0, 10,80,0,0,0,0};
    const i32 mask = r.e.add_mask(r.a, pts, 4, true);
    AUREA_CHECK_EQ(r.e.add_mask(r.b, pts, 4, true), mask);
    auto seek = [&](i64 frame) {
        Command cmd; cmd.type = CommandType::PlaybackSeek; cmd.seek.time = tick_at(FrameIndex{frame}, 30);
        AUREA_CHECK_EQ(r.e.submit_commands(&cmd, 1, nullptr, 0), 1u);
    };
    seek(12); AUREA_CHECK(r.e.toggle_mask_param_key(r.a, mask, 0));
    AUREA_CHECK(r.e.toggle_mask_path_key(r.a, mask));
    seek(22); AUREA_CHECK(r.e.set_mask_param(r.a, mask, 0, 20));
    AUREA_CHECK(r.e.toggle_mask_path_key(r.a, mask));
    const auto* track = r.L(r.a)->tracks.find(TrackProperty::MaskParam, mask, 0);
    AUREA_CHECK(track && track->keys.size() == 2);
    if (!track || track->keys.size() != 2) return;
    AUREA_CHECK_EQ(track->keys[0].time.value, 7); AUREA_CHECK_EQ(track->keys[1].time.value, 17);
    AUREA_CHECK_EQ(r.L(r.a)->masks[0].pathKeys[0].frame, 7);
    AUREA_CHECK_EQ(r.L(r.a)->masks[0].pathKeys[1].frame, 17);
    const i64 refs[] = {43, mask, 0, 7, 43, mask, 0, 17};
    AUREA_CHECK_EQ(r.e.copy_keyframe_selection(r.a, refs, 2), 2u);
    const u64 targets[] = {r.b, r.c};
    AUREA_CHECK_EQ(r.e.paste_keyframes(targets, 2, 50), 2u);
    const auto* copied = r.L(r.b)->tracks.find(TrackProperty::MaskParam, mask, 0);
    AUREA_CHECK(copied && copied->keys.size() == 2);
    if (copied && copied->keys.size() == 2) {
        AUREA_CHECK_EQ(copied->keys[0].time.value, 20); AUREA_CHECK_EQ(copied->keys[1].time.value, 30);
        AUREA_CHECK_NEAR(copied->keys[1].value, 20, .001);
    }
    AUREA_CHECK(r.L(r.c)->tracks.find(TrackProperty::MaskParam, mask, 0) == nullptr);
    r.undo(); AUREA_CHECK(r.L(r.b)->tracks.find(TrackProperty::MaskParam, mask, 0) == nullptr);
}

AUREA_TEST(TimeArrangement, AlignsAndSequencesWithOneUndo) {
    for (auto mode : {LayerTimeArrangement::AlignStarts, LayerTimeArrangement::AlignEnds, LayerTimeArrangement::Sequence}) {
        EditRig r;
        const u64 ids[] = {r.c, r.a, r.b};
        const u32 depth = r.e.history().depth();
        auto result = r.e.arrange_layer_times(ids, 3, mode);
        AUREA_CHECK(result.ok());
        AUREA_CHECK_EQ(r.e.history().depth(), depth + 1);
        if (mode == LayerTimeArrangement::AlignStarts) {
            AUREA_CHECK(r.at(r.a, 0, 30) && r.at(r.b, 0, 30) && r.at(r.c, 0, 30));
        } else if (mode == LayerTimeArrangement::AlignEnds) {
            AUREA_CHECK(r.at(r.a, 60, 90) && r.at(r.b, 60, 90) && r.at(r.c, 60, 90));
        } else {
            AUREA_CHECK(r.at(r.c, 60, 90) && r.at(r.a, 90, 120) && r.at(r.b, 120, 150));
        }
        r.undo();
        AUREA_CHECK(r.at(r.a, 0, 30) && r.at(r.b, 30, 60) && r.at(r.c, 60, 90));
        Command redo; redo.type = CommandType::Redo;
        AUREA_CHECK(r.e.apply_command(redo).ok());
        AUREA_CHECK_EQ(r.e.history().depth(), depth + 1);
    }
}

AUREA_TEST(TimeArrangement, DistributesStartsByTimeAndRoundsToFrames) {
    EditRig r;
    r.range(r.a, 10, 20); r.range(r.b, 12, 37); r.range(r.c, 71, 78);
    const u64 ids[] = {r.c, r.b, r.a};
    const auto result = r.e.arrange_layer_times(ids, 3, LayerTimeArrangement::DistributeStarts);
    AUREA_CHECK(result.ok() && *result == 1);
    AUREA_CHECK(r.at(r.a, 10, 20) && r.at(r.b, 41, 66) && r.at(r.c, 71, 78));
    const u32 depth = r.e.history().depth();
    const auto noop = r.e.arrange_layer_times(ids, 3, LayerTimeArrangement::DistributeStarts);
    AUREA_CHECK(noop.ok() && *noop == 0);
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
    r.undo(); AUREA_CHECK(r.at(r.b, 12, 37));
}

AUREA_TEST(TimeArrangement, EqualGapsKeepOuterLayersAndRejectOverlapAtomically) {
    EditRig r;
    r.range(r.a, 10, 20); r.range(r.b, 12, 37); r.range(r.c, 71, 78);
    const u64 ids[] = {r.c, r.a, r.b};
    const auto result = r.e.arrange_layer_times(ids, 3, LayerTimeArrangement::DistributeGaps);
    AUREA_CHECK(result.ok() && *result == 1);
    AUREA_CHECK(r.at(r.a, 10, 20) && r.at(r.b, 33, 58) && r.at(r.c, 71, 78));
    r.undo();
    r.range(r.a, 10, 60);
    const u32 depth = r.e.history().depth();
    AUREA_CHECK(!r.e.arrange_layer_times(ids, 3, LayerTimeArrangement::DistributeGaps).ok());
    AUREA_CHECK(r.at(r.a, 10, 60) && r.at(r.b, 12, 37) && r.at(r.c, 71, 78));
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
}

AUREA_TEST(TimeArrangement, LockedDuplicateAndMissingIdsNeverMoveNeighbours) {
    EditRig r;
    r.comp()->set_edit_mode(true);
    auto* locked = r.comp()->layer(LayerId::unpack(r.b));
    locked->locked = true; locked->magneticTrack = true;
    r.comp()->layer(LayerId::unpack(r.a))->magneticTrack = true;
    const u64 ids[] = {r.a, r.a, 0, r.b, r.c};
    const auto result = r.e.arrange_layer_times(ids, 5, LayerTimeArrangement::AlignStarts);
    AUREA_CHECK(result.ok() && *result == 1);
    AUREA_CHECK(r.at(r.a, 0, 30) && r.at(r.b, 30, 60) && r.at(r.c, 0, 30));
    AUREA_CHECK(r.L(r.b)->locked && r.L(r.b)->magneticTrack && r.L(r.a)->magneticTrack);
    r.undo();
    const u64 pair[] = {r.a, r.c};
    AUREA_CHECK(r.e.arrange_layer_times(pair, 2, LayerTimeArrangement::StartsAtPlayhead, 20).ok());
    AUREA_CHECK(r.at(r.b, 30, 60));
}

AUREA_TEST(TimeArrangement, MovesReversedRemappedMediaAndLocalAnimationTogether) {
    for (bool reverse : {false, true}) for (bool remap : {false, true}) {
        EditRig r; clip_source(r, r.b, 2.0f, reverse);
        if (remap) AUREA_CHECK(r.e.set_time_remap(r.b, true));
        const Layer before = *r.L(r.b);
        const u64 ids[] = {r.b};
        AUREA_CHECK(r.e.arrange_layer_times(ids, 1, LayerTimeArrangement::StartsAtPlayhead, 200).ok());
        const Layer* after = r.L(r.b);
        AUREA_CHECK(r.at(r.b, 200, 230));
        AUREA_CHECK_EQ(after->offset.value, before.offset.value);
        AUREA_CHECK_EQ(after->tracks.at(0).keys.size(), before.tracks.at(0).keys.size());
        for (i64 i = 0; i < 30; ++i) {
            AUREA_CHECK_NEAR(after->source_frame(FrameIndex{200 + i}), before.source_frame(FrameIndex{30 + i}), .0001);
            AUREA_CHECK_EQ(after->local_time(FrameIndex{200 + i}).value, before.local_time(FrameIndex{30 + i}).value);
        }
        r.undo(); AUREA_CHECK(r.at(r.b, 30, 60));
    }
}

AUREA_TEST(TimeArrangement, EndAtPlayheadAndBoundsAreAtomic) {
    EditRig r;
    r.range(r.b, 30, 80);
    const u64 ids[] = {r.a, r.b};
    AUREA_CHECK(r.e.arrange_layer_times(ids, 2, LayerTimeArrangement::EndsAtPlayhead, 100).ok());
    AUREA_CHECK(r.at(r.a, 70, 100) && r.at(r.b, 50, 100));
    r.undo();
    const u32 depth = r.e.history().depth();
    // A could fit, B cannot. Neither may move.
    AUREA_CHECK(!r.e.arrange_layer_times(ids, 2, LayerTimeArrangement::EndsAtPlayhead, 40).ok());
    AUREA_CHECK(!r.e.arrange_layer_times(ids, 2, LayerTimeArrangement::StartsAtPlayhead, (i64{1} << 31) - 20).ok());
    AUREA_CHECK(!r.e.arrange_layer_times(ids, 2, static_cast<LayerTimeArrangement>(100)).ok());
    AUREA_CHECK(!r.e.arrange_layer_times(nullptr, 2, LayerTimeArrangement::Sequence).ok());
    AUREA_CHECK(r.at(r.a, 0, 30) && r.at(r.b, 30, 80));
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
}

AUREA_TEST(TimeArrangement, ExtendsProjectAndUndoRestoresDuration) {
    EditRig r;
    const i64 duration = r.comp()->duration().value;
    const u64 ids[] = {r.a, r.b, r.c};
    AUREA_CHECK(r.e.arrange_layer_times(ids, 3, LayerTimeArrangement::StartsAtPlayhead, duration + 50).ok());
    AUREA_CHECK_EQ(r.comp()->duration().value, duration + 80);
    r.undo();
    AUREA_CHECK_EQ(r.comp()->duration().value, duration);
    AUREA_CHECK(r.at(r.a, 0, 30) && r.at(r.b, 30, 60) && r.at(r.c, 60, 90));
}

AUREA_TEST(ClipEdit, TrimPreservesSourceAndAnimationAcrossSpeedsAndReverse) {
    for (bool reverse : {false, true}) for (float speed : {0.0f, .5f, 1.0f, 2.0f}) for (u32 op : {0u, 1u}) {
        EditRig r; clip_source(r, r.b, speed, reverse);
        const Layer original = *r.L(r.b);
        AUREA_CHECK(r.e.edit_clip_time(r.b, op, op == 0 ? 37 : 53));
        const Layer* edited = r.L(r.b);
        for (i64 frame = edited->start.value; frame < edited->end.value; ++frame) {
            AUREA_CHECK_NEAR(edited->source_frame_f(frame + .25), original.source_frame_f(frame + .25), .001);
            AUREA_CHECK_EQ(edited->local_time(FrameIndex{frame}).value, original.local_time(FrameIndex{frame}).value);
        }
        r.undo();
        AUREA_CHECK(r.at(r.b, 30, 60)); AUREA_CHECK(!r.L(r.b)->timeRemapEnabled);
    }
}

AUREA_TEST(ClipEdit, RippleTrimKeepsTheContentAtItsNewPosition) {
    EditRig r; clip_source(r, r.b, 2, true); r.e.set_edit_mode(true);
    const Layer original = *r.L(r.b);
    AUREA_CHECK(r.e.edit_clip_time(r.b, 0, 38));
    AUREA_CHECK(r.at(r.b, 30, 52)); AUREA_CHECK(r.at(r.c, 52, 82));
    for (i64 f = 30; f < 52; ++f) {
        AUREA_CHECK_NEAR(r.L(r.b)->source_frame_f(f), original.source_frame_f(f + 8), .001);
        AUREA_CHECK_EQ(r.L(r.b)->local_time(FrameIndex{f}).value, original.local_time(FrameIndex{f+8}).value);
    }
}

AUREA_TEST(ClipEdit, ExtendingAfterTrimDoesNotFreezeAtTheOldBoundary) {
    for(bool reverse : {false,true}) {
        EditRig r; clip_source(r,r.b,2,reverse); const Layer original=*r.L(r.b);
        AUREA_CHECK(r.e.edit_clip_time(r.b,0,35));
        AUREA_CHECK(r.e.edit_clip_time(r.b,1,70));
        AUREA_CHECK_NEAR(r.L(r.b)->source_frame_f(67.25),original.source_frame_f(67.25),.001);
        AUREA_CHECK(r.e.edit_clip_time(r.b,0,25));
        AUREA_CHECK_NEAR(r.L(r.b)->source_frame_f(26.25),original.source_frame_f(26.25),.001);
    }
}

AUREA_TEST(ClipEdit, SlipKeepsBoundsAndAnimationAndSupportsUndo) {
    EditRig r; clip_source(r, r.b, .5f, true);
    const Layer original = *r.L(r.b);
    AUREA_CHECK(r.e.edit_clip_time(r.b, 2, 12)); AUREA_CHECK(r.at(r.b, 30, 60));
    for (i64 f = 30; f < 60; ++f) {
        AUREA_CHECK_NEAR(r.L(r.b)->source_frame_f(f+.125), original.source_frame_f(f+.125)+12, .001);
        AUREA_CHECK_EQ(r.L(r.b)->local_time(FrameIndex{f}).value, original.local_time(FrameIndex{f}).value);
    }
    r.undo(); AUREA_CHECK(!r.L(r.b)->timeRemapEnabled);
    AUREA_CHECK(r.e.edit_clip_time(r.b, 2, -12));
    AUREA_CHECK_NEAR(r.L(r.b)->source_frame_f(45), original.source_frame_f(45)-12, .001);
}

AUREA_TEST(ClipEdit, RollAndSlideAreAtomicAndLeaveOtherClipsAlone) {
    for (u32 operation : {3u, 4u, 5u}) {
        EditRig r; for (u64 id : {r.a,r.b,r.c}) clip_source(r,id,2,true);
        r.e.set_edit_mode(true); // explicit roll/slide overrides ripple mode
        const Layer b = *r.L(r.b), a = *r.L(r.a), c = *r.L(r.c);
        AUREA_CHECK(r.e.edit_clip_time(r.b, operation, 5, r.a, r.c));
        AUREA_CHECK_EQ(r.L(r.a)->end.value, operation == 4 ? 30 : 35);
        AUREA_CHECK_EQ(r.L(r.c)->start.value, operation == 3 ? 60 : 65);
        AUREA_CHECK_NEAR(r.L(r.b)->source_frame_f(45), b.source_frame_f(operation == 5 ? 40 : 45), .001);
        AUREA_CHECK_NEAR(r.L(r.a)->source_frame_f(20), a.source_frame_f(20), .001);
        AUREA_CHECK_NEAR(r.L(r.c)->source_frame_f(75), c.source_frame_f(75), .001);
        r.undo(); AUREA_CHECK(r.at(r.a,0,30)); AUREA_CHECK(r.at(r.b,30,60)); AUREA_CHECK(r.at(r.c,60,90));
    }
}

AUREA_TEST(ClipEdit, RejectsMissingNeighboursLocksAndSourceOverrunWithoutUndoEntry) {
    EditRig r; clip_source(r, r.b); AUREA_CHECK(r.e.toggle_marker(12));
    AUREA_CHECK(!r.e.edit_clip_time(r.b, 5, 10));
    AUREA_CHECK(!r.e.edit_clip_time(r.b, 4, 40, r.a, r.c));
    AUREA_CHECK(!r.e.edit_clip_time(r.b, 2, -100));
    AUREA_CHECK(!r.e.edit_clip_time(r.b, 1, 1000));
    r.comp()->layer(LayerId::unpack(r.c))->locked = true;
    AUREA_CHECK(!r.e.edit_clip_time(r.b, 4, 1, r.a, r.c));
    r.e.set_edit_mode(true);
    AUREA_CHECK(!r.e.edit_clip_time(r.b, 1, 55));
    AUREA_CHECK(r.at(r.b,30,60)); AUREA_CHECK(r.at(r.c,60,90));
    r.undo(); AUREA_CHECK(!r.e.edit_mode());
    r.undo(); i64 marks[2]{}; AUREA_CHECK_EQ(r.e.query_markers(marks,2),0u);
}

AUREA_TEST(ClipEdit, SplitKeepsAnimatedClockAndRampedOrReverseSource) {
    for (bool remap : {false,true}) for (bool reverse : {false,true}) {
        EditRig r; clip_source(r,r.b,2,reverse);
        Layer* originalLayer = r.comp()->layer(LayerId::unpack(r.b));
        if(remap) { originalLayer->timeRemapEnabled=true; originalLayer->timeRemap.set(FrameIndex{60},80); originalLayer->timeRemap.set(FrameIndex{90},110,Interpolation::Linear); }
        const Layer original=*originalLayer;
        Command split; split.type=CommandType::LayerSplit; split.layer_split.layer=LayerId::unpack(r.b); split.layer_split.at=FrameIndex{43};
        AUREA_CHECK(r.e.apply_command(split).ok());
        const Layer* second=nullptr;
        r.comp()->layers().for_each([&](LayerId id,const Layer& l){ if(id.pack()!=r.a&&id.pack()!=r.b&&id.pack()!=r.c) second=&l; });
        AUREA_CHECK(second!=nullptr); if(!second)continue;
        for(i64 frame=30;frame<60;++frame) {
            const Layer* l=frame<43?r.L(r.b):second;
            AUREA_CHECK_NEAR(l->source_frame_f(frame+.125),original.source_frame_f(frame+.125),.001);
            AUREA_CHECK_EQ(l->local_time(FrameIndex{frame}).value,original.local_time(FrameIndex{frame}).value);
        }
    }
}

AUREA_TEST(Edit, CompositionModeTrimIsFree) {
    EditRig r;
    r.range(r.a, 0, 20);
    AUREA_CHECK(r.at(r.a, 0, 20));
    AUREA_CHECK(r.at(r.b, 30, 60));   // ninguém anda
    AUREA_CHECK(r.at(r.c, 60, 90));
}

AUREA_TEST(Edit, EditModeTrimEndRipples) {
    EditRig r;
    r.e.set_edit_mode(true);
    AUREA_CHECK(r.e.toggle_marker(75));
    r.range(r.a, 0, 20);                // encurta 10: B e C recuam 10
    AUREA_CHECK(r.at(r.a, 0, 20));
    AUREA_CHECK(r.at(r.b, 20, 50));
    AUREA_CHECK(r.at(r.c, 50, 80));
    i64 m[3] = {};
    AUREA_CHECK_EQ(r.e.query_markers(m, 1), 1u);
    AUREA_CHECK_EQ(m[0], 65);           // a marca anda junto
    r.range(r.b, 20, 70);               // alonga B 20: C avança
    AUREA_CHECK(r.at(r.c, 70, 100));
    r.undo();
    AUREA_CHECK(r.at(r.b, 20, 50));
    AUREA_CHECK(r.at(r.c, 50, 80));
}

AUREA_TEST(Edit, EditModeTrimStartKeepsThePlaceAndPullsTheRest) {
    EditRig r;
    r.e.set_edit_mode(true);
    // Aparar 12 do começo de B (conteúdo anda 12 no offset).
    r.range(r.b, 42, 60, 12);
    AUREA_CHECK(r.at(r.b, 30, 48));     // continua encostado em A
    AUREA_CHECK_EQ(r.L(r.b)->offset.value, 12);
    AUREA_CHECK(r.at(r.c, 48, 78));
    AUREA_CHECK(r.at(r.a, 0, 30));
    // Mover (início e fim juntos) não empurra ninguém.
    r.range(r.b, 100, 118, 12);
    AUREA_CHECK(r.at(r.b, 100, 118));
    AUREA_CHECK(r.at(r.c, 48, 78));
}

// "Puxar para o cabeçote" (o botão da fileira rápida, Android e iOS): o clipe
// INTEIRO anda até o cabeçote. A duração não muda e o conteúdo anda junto — o
// offset interno fica, que é o mesmo que o arrasto do corpo do clipe faz. É este
// payload que o `moveToPlayhead` emite (início e fim, sem `setOffset`).
AUREA_TEST(Edit, PullToPlayheadKeepsDurationAndContentOffset) {
    EditRig r;
    r.e.set_edit_mode(true);
    r.range(r.b, 42, 60, 12);                          // aparou 12 do começo
    AUREA_CHECK(r.at(r.b, 30, 48));
    AUREA_CHECK_EQ(r.L(r.b)->offset.value, 12);
    const i64 dur = r.L(r.b)->end.value - r.L(r.b)->start.value;
    const i64 head = 100;
    r.range(r.b, head, head + dur);                    // sem setOffset
    AUREA_CHECK(r.at(r.b, head, head + dur));
    AUREA_CHECK_EQ(r.L(r.b)->offset.value, 12);        // conteúdo andou junto
    AUREA_CHECK(r.at(r.a, 0, 30));                     // e ninguém mais andou
    AUREA_CHECK(r.at(r.c, 48, 78));
    r.undo();
    AUREA_CHECK(r.at(r.b, 30, 48));
}

AUREA_TEST(Edit, RippleDeleteClosesOnlyTheHoleItMade) {
    EditRig r;
    // D cobre parte do trecho de B: onde D está não há buraco.
    const u64 d = *r.e.add_null(false);
    r.range(d, 40, 50);
    AUREA_CHECK(r.e.toggle_marker(80));
    AUREA_CHECK(r.e.ripple_delete(&r.b, 1));
    // B ocupava [30,60); D segura [40,50): buracos [30,40) e [50,60) = 20.
    AUREA_CHECK(r.L(r.b) == nullptr);
    AUREA_CHECK(r.at(r.a, 0, 30));
    AUREA_CHECK(r.at(d, 30, 40));
    AUREA_CHECK(r.at(r.c, 40, 70));
    i64 m[3] = {};
    AUREA_CHECK_EQ(r.e.query_markers(m, 1), 1u);
    AUREA_CHECK_EQ(m[0], 60);
    r.undo();
    AUREA_CHECK(r.L(r.b) != nullptr);
    AUREA_CHECK(r.at(r.c, 60, 90));
    AUREA_CHECK(r.at(d, 40, 50));
}

AUREA_TEST(Edit, RemoveGapsAndTrimProject) {
    EditRig r;
    r.range(r.a, 10, 30);   // buraco inicial [0,10)
    r.range(r.c, 80, 110);  // buraco [60,80)
    AUREA_CHECK_EQ(r.e.remove_gaps(), 30);
    AUREA_CHECK(r.at(r.a, 0, 20));
    AUREA_CHECK(r.at(r.b, 20, 50));
    AUREA_CHECK(r.at(r.c, 50, 80));
    AUREA_CHECK_EQ(r.e.remove_gaps(), 0);      // nada mais a fechar
    r.undo();
    AUREA_CHECK(r.at(r.c, 80, 110));
    AUREA_CHECK(r.at(r.a, 10, 30));
    // Aparar o projeto em 70: C (começa em 80) sai; B fica; duração 70.
    AUREA_CHECK(r.e.trim_composition(70));
    AUREA_CHECK_EQ(r.comp()->duration().value, 70);
    AUREA_CHECK(r.L(r.c) == nullptr);
    AUREA_CHECK(r.at(r.b, 30, 60));
}

AUREA_TEST(Edit, EditModeSurvivesSaveAndReopen) {
    EditRig r;
    r.e.set_edit_mode(true);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_edicao.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK(r.e.edit_mode());
    std::remove(path.c_str());
}

AUREA_TEST(Edit, LockedClipsRejectDestructiveEditsWithoutAddingUndo) {
    EditRig r;
    r.comp()->layer(LayerId::unpack(r.b))->locked = true;
    r.e.set_edit_mode(true);
    for (const auto type : {CommandType::LayerDelete, CommandType::LayerSplit, CommandType::LayerSetTimeRange}) {
        Command cmd;
        cmd.type = type;
        cmd.layer_ref.layer = LayerId::unpack(r.b);
        if (type == CommandType::LayerSplit) cmd.layer_split.at = FrameIndex{45};
        if (type == CommandType::LayerSetTimeRange) {
            cmd.layer_range.start = FrameIndex{30};
            cmd.layer_range.end = FrameIndex{40};
        }
        AUREA_CHECK(!r.e.apply_command(cmd).ok());
    }
    AUREA_CHECK(!r.e.ripple_delete(&r.b, 1));
    AUREA_CHECK(r.at(r.b, 30, 60));
    r.undo(); // Rejected edits did not hide the actual previous operation.
    AUREA_CHECK(!r.e.edit_mode());
    AUREA_CHECK(r.at(r.b, 30, 60));
}

AUREA_TEST(Edit, MagneticTrimKeepsLockedLayerAnchored) {
    EditRig r;
    r.comp()->layer(LayerId::unpack(r.c))->locked = true;
    r.e.set_edit_mode(true);
    r.range(r.a, 0, 20);
    AUREA_CHECK(r.at(r.b, 20, 50));
    AUREA_CHECK(r.at(r.c, 60, 90));
    r.undo();
    AUREA_CHECK(r.at(r.b, 30, 60));
    AUREA_CHECK(r.at(r.c, 60, 90));
}

AUREA_TEST(Edit, RippleDeletePreservesLocksAndDoesNotRepeatedlyCollapseAnchoredGaps) {
    EditRig r;
    r.comp()->layer(LayerId::unpack(r.c))->locked = true;
    const u64 ids[] = {r.b, r.c};
    AUREA_CHECK(r.e.ripple_delete(ids, 2));
    AUREA_CHECK(r.L(r.b) == nullptr);
    AUREA_CHECK(r.at(r.c, 60, 90));
    AUREA_CHECK_EQ(r.e.remove_gaps(), 0);
    AUREA_CHECK_EQ(r.e.remove_gaps(), 0);
    r.undo();
    AUREA_CHECK(r.at(r.b, 30, 60));
    AUREA_CHECK(r.at(r.c, 60, 90));
}

// =============================================================================
//  Copiar e colar
// =============================================================================
AUREA_TEST(Clipboard, PasteLayersKeepsSpacingParentAndUndo) {
    EditRig r;
    // B filho de A; copia os dois e cola em 100.
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(r.b);
    pc.layer_parent.parent = LayerId::unpack(r.a);
    AUREA_CHECK(r.e.apply_command(pc).ok());
    const u64 ids[2] = {r.a, r.b};
    AUREA_CHECK_EQ(r.e.copy_layers(ids, 2), 2u);
    AUREA_CHECK_EQ(r.e.clipboard_state() & 1u, 1u);
    const u32 before = r.comp()->layers().count();
    AUREA_CHECK_EQ(r.e.paste_layers(100), 2u);
    AUREA_CHECK_EQ(r.comp()->layers().count(), before + 2);
    AUREA_CHECK_EQ(r.e.selection_count(), 2u);
    u64 sel[2] = {};
    r.e.get_selection(sel, 2);
    const Layer* na = nullptr;
    const Layer* nb = nullptr;
    for (u64 id : sel) {
        const Layer* l = r.L(id);
        if (l && l->start.value == 100) na = l;
        if (l && l->start.value == 130) nb = l;
    }
    AUREA_CHECK(na && nb);
    if (na && nb) {
        AUREA_CHECK_EQ(na->end.value, 130);
        AUREA_CHECK_EQ(nb->end.value, 160);
        // O filho colado segue o pai COLADO, não o original.
        const Layer* p = r.comp()->layer(nb->parent);
        AUREA_CHECK(p == na);
    }
    r.undo();
    AUREA_CHECK_EQ(r.comp()->layers().count(), before);
}

AUREA_TEST(Clipboard, PasteIntoAnotherProjectWorksForLayersWithoutMedia) {
    EditRig r;
    const u64 shape = *r.e.add_shape(0);
    const u64 ids[2] = {shape, r.a};
    AUREA_CHECK_EQ(r.e.copy_layers(ids, 2), 2u);
    // Projeto novo: a forma e o nulo (sem mídia) entram.
    AUREA_CHECK(r.e.new_project(320, 180, 30.0, nullptr).ok());
    AUREA_CHECK_EQ(r.e.paste_layers(0), 2u);
}

AUREA_TEST(Clipboard, StyleAndEffectsAndKeyframes) {
    EditRig r;
    const u64 s1 = *r.e.add_shape(0);
    const u64 s2 = *r.e.add_shape(3);
    Layer* a = r.comp()->layer(LayerId::unpack(s1));
    a->shape.fillColor = Vec4{1, 0, 0, 1};
    a->blendMode = BlendMode::Multiply;
    Command fx;
    fx.type = CommandType::EffectAdd;
    fx.effect_add.layer = LayerId::unpack(s1);
    fx.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    fx.effect_add.index = kInvalidIndex;
    AUREA_CHECK(r.e.apply_command(fx).ok());
    a = r.comp()->layer(LayerId::unpack(s1));
    AUREA_CHECK_EQ(a->effects.size(), usize{1});
    // Keyframe no parâmetro 0 do efeito e na opacidade, no frame 10.
    const u32 fxId = a->effects[0].id;
    a->tracks.get_or_create(TrackProperty::EffectParam, fxId, 0).set(a->local_time(FrameIndex{10}), 7.0f);
    a->tracks.get_or_create(TrackProperty::Opacity).set(a->local_time(FrameIndex{10}), 0.5f);
    const u32 shapeType2 = r.L(s2)->shape.shapeType;

    // Estilo: cor, mesclagem e efeitos; a geometria do destino fica.
    AUREA_CHECK(r.e.copy_style(s1));
    AUREA_CHECK_EQ(r.e.paste_style(&s2, 1), 1u);
    const Layer* b = r.L(s2);
    AUREA_CHECK(b->shape.fillColor.x == 1.0f && b->shape.fillColor.y == 0.0f);
    AUREA_CHECK(b->blendMode == BlendMode::Multiply);
    AUREA_CHECK_EQ(b->effects.size(), usize{1});
    AUREA_CHECK_EQ(b->shape.shapeType, shapeType2);

    // Efeitos: colar ACRESCENTA com id novo e leva o keyframe junto.
    AUREA_CHECK_EQ(r.e.copy_effects(s1), 1u);
    AUREA_CHECK_EQ(r.e.paste_effects(&s2, 1), 1u);
    b = r.L(s2);
    AUREA_CHECK_EQ(b->effects.size(), usize{2});
    AUREA_CHECK(b->effects[0].id != b->effects[1].id);
    const Track* t = b->tracks.find(TrackProperty::EffectParam, b->effects[1].id, 0);
    AUREA_CHECK(t && t->keys.size() == 1 && t->keys[0].value == 7.0f);

    // Keyframes do instante 10 → colados em 40 na outra camada.
    AUREA_CHECK_EQ(r.e.copy_keyframes(s1, 10), 2u);
    AUREA_CHECK_EQ(r.e.paste_keyframes(&s2, 1, 40), 2u);
    b = r.L(s2);
    const Track* op = b->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(op && op->find_exact(b->local_time(FrameIndex{40})) != kInvalidIndex);
    r.undo();
    b = r.L(s2);
    op = b->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(!op || op->find_exact(b->local_time(FrameIndex{40})) == kInvalidIndex);
}

AUREA_TEST(Clipboard, SingleEffectPreservesOnlyItsAnimationAndSkipsLockedTargets) {
    EditRig r;
    for (const char* type : {effect_keys::kGlow,effect_keys::kGaussianBlur}) {
        Command fx;fx.type=CommandType::EffectAdd;fx.effect_add.layer=LayerId::unpack(r.a);
        fx.effect_add.effectType=effect_type_id(type);fx.effect_add.index=kInvalidIndex;
        AUREA_CHECK(r.e.apply_command(fx).ok());
    }
    auto* source=r.comp()->layer(LayerId::unpack(r.a));
    const u32 glow=source->effects[0].id,blur=source->effects[1].id;
    source->tracks.get_or_create(TrackProperty::EffectParam,glow,0).set(FrameIndex{0},40);
    source->tracks.get_or_create(TrackProperty::EffectParam,blur,0).set(FrameIndex{12},25);
    AUREA_CHECK_EQ(r.e.copy_effects(r.a,blur),1u);
    AUREA_CHECK_EQ(r.e.copy_effects(r.a,9999),0u); // Invalid selection does not erase the clipboard.
    auto* locked=r.comp()->layer(LayerId::unpack(r.c));locked->locked=true;
    const u64 targets[]={r.c,r.b};
    AUREA_CHECK_EQ(r.e.paste_effects(targets,2),1u);
    AUREA_CHECK(r.L(r.c)->effects.empty());
    const auto* result=r.L(r.b);
    AUREA_CHECK_EQ(result->effects.size(),usize{1});
    AUREA_CHECK_EQ(result->effects[0].type,effect_type_id(effect_keys::kGaussianBlur));
    const Track* track=result->tracks.find(TrackProperty::EffectParam,result->effects[0].id,0);
    AUREA_CHECK(track && track->keys.size()==1 && track->keys[0].time.value==12 && track->keys[0].value==25);
    r.undo();AUREA_CHECK(r.L(r.b)->effects.empty());
}

AUREA_TEST(Clipboard, PastedEffectsKeepKeyTimesRelativeToTheLayerStart) {
    EditRig r;
    // A aparada (entra na fonte em 12), B em 100, C sem aparo: keys guardadas
    // em tempo local, então o início de cada camada é o `offset` dela.
    r.range(r.a, 30, 90, 12);
    r.range(r.b, 120, 180, 100);
    r.range(r.c, 200, 260, 0);
    for (const char* type : {effect_keys::kGlow, effect_keys::kGaussianBlur}) {
        Command fx; fx.type = CommandType::EffectAdd; fx.effect_add.layer = LayerId::unpack(r.a);
        fx.effect_add.effectType = effect_type_id(type); fx.effect_add.index = kInvalidIndex;
        AUREA_CHECK(r.e.apply_command(fx).ok());
    }
    Layer* src = r.comp()->layer(LayerId::unpack(r.a));
    const u32 glow = src->effects[0].id, blur = src->effects[1].id;
    // Blur anima nos quadros 5 e 25 da camada A (timeline 35 e 55).
    Track& bt = src->tracks.get_or_create(TrackProperty::EffectParam, blur, 0);
    bt.set(src->local_time(FrameIndex{35}), 2.0f);
    bt.set(src->local_time(FrameIndex{55}), 20.0f);
    bt.keys[0].easePower = 3;
    src->tracks.get_or_create(TrackProperty::EffectParam, glow, 0).set(src->local_time(FrameIndex{40}), 9.0f);
    src->tracks.get_or_create(TrackProperty::Opacity).set(src->local_time(FrameIndex{35}), 0.25f);
    auto keyAt = [&](u64 id, u32 fxIndex, u32 k) -> i64 {
        const Layer* l = r.L(id);
        const Track* t = l ? l->tracks.find(TrackProperty::EffectParam, l->effects[fxIndex].id, 0) : nullptr;
        return t && k < t->keys.size() ? l->timeline_time(t->keys[k].time).value - l->start.value : -1;
    };

    // Todos os efeitos: mesma distância do início de B e de C.
    AUREA_CHECK_EQ(r.e.copy_effects(r.a), 2u);
    const u64 both[] = {r.b, r.c};
    AUREA_CHECK_EQ(r.e.paste_effects(both, 2), 2u);
    for (u64 id : both) {
        AUREA_CHECK_EQ(r.L(id)->effects.size(), usize{2});
        AUREA_CHECK_EQ(keyAt(id, 0, 0), 10);
        AUREA_CHECK_EQ(keyAt(id, 1, 0), 5);
        AUREA_CHECK_EQ(keyAt(id, 1, 1), 25);
        const Track* t = r.L(id)->tracks.find(TrackProperty::EffectParam, r.L(id)->effects[1].id, 0);
        AUREA_CHECK(t && t->keys[0].easePower == 3 && t->keys[1].value == 20.0f);
    }
    // A origem não mudou.
    AUREA_CHECK_EQ(keyAt(r.a, 1, 0), 5);
    r.undo();
    AUREA_CHECK(r.L(r.b)->effects.empty() && r.L(r.c)->effects.empty());
    AUREA_CHECK_EQ(r.L(r.b)->tracks.size(), 0u);

    // Um efeito só ("Copiar este efeito").
    AUREA_CHECK_EQ(r.e.copy_effects(r.a, blur), 1u);
    AUREA_CHECK_EQ(r.e.paste_effects(&r.b, 1), 1u);
    AUREA_CHECK_EQ(r.L(r.b)->effects.size(), usize{1});
    AUREA_CHECK_EQ(keyAt(r.b, 0, 0), 5);
    AUREA_CHECK_EQ(keyAt(r.b, 0, 1), 25);
    // Colar de novo na própria origem não desloca.
    AUREA_CHECK_EQ(r.e.paste_effects(&r.a, 1), 1u);
    AUREA_CHECK_EQ(keyAt(r.a, 2, 0), 5);
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->effects.size(), usize{2});
    r.undo();
    AUREA_CHECK(r.L(r.b)->effects.empty());

    // Colar estilo leva efeitos e opacidade com a mesma regra.
    AUREA_CHECK(r.e.copy_style(r.a));
    AUREA_CHECK_EQ(r.e.paste_style(&r.c, 1), 1u);
    AUREA_CHECK_EQ(keyAt(r.c, 1, 0), 5);
    const Layer* c = r.L(r.c);
    const Track* op = c->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(op && op->keys.size() == 1 && c->timeline_time(op->keys[0].time).value == c->start.value + 5);
    r.undo();
    AUREA_CHECK(r.L(r.c)->effects.empty());
}

AUREA_TEST(Clipboard, ShiftedTrimmedLayersUseCompositionTime) {
    EditRig r;
    r.range(r.a, 30, 90, 12);
    r.range(r.b, 120, 180, 7);
    Layer* source = r.comp()->layer(LayerId::unpack(r.a));
    source->tracks.get_or_create(TrackProperty::Opacity).set(
        source->local_time(FrameIndex{45}), 0.35f);
    AUREA_CHECK_EQ(r.e.copy_keyframes(r.a, 45), 1u);
    AUREA_CHECK_EQ(r.e.paste_keyframes(&r.b, 1, 140), 1u);
    const Layer* target = r.L(r.b);
    const Track* track = target->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(track && track->keys.size() == 1);
    if (track && track->keys.size() == 1) {
        AUREA_CHECK_EQ(track->keys[0].time.value, target->local_time(FrameIndex{140}).value);
        AUREA_CHECK_EQ(track->keys[0].value, 0.35f);
    }
    r.undo();
    track = r.L(r.b)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(!track || track->keys.empty());
    AUREA_CHECK(r.at(r.a, 30, 90) && r.at(r.b, 120, 180));
}

AUREA_TEST(Clipboard, SelectedKeysMoveAtomicallyCopySpacingAndDeleteWithUndo) {
    EditRig r;
    auto* layer = r.comp()->layer(LayerId::unpack(r.a));
    for (i64 frame : {0, 10, 20}) {
        layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{frame}, static_cast<f32>(frame));
        layer->tracks.get_or_create(TrackProperty::PositionY).set(FrameIndex{frame}, 100.0f + frame);
    }
    const i64 refs[] = {0, -1, 0, 0, 0, -1, 0, 10, 1, -1, 0, 10};
    // X at 10 would collide with unselected X at 20: nothing may move.
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.a, refs, 3, 10, false), 0u);
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionX)->keys[0].time.value, 0);
    AUREA_CHECK_EQ(r.e.copy_keyframe_selection(r.a, refs, 3), 3u);
    AUREA_CHECK_EQ(r.e.paste_keyframes(&r.b, 1, 35), 3u);
    const auto* x = r.L(r.b)->tracks.find(TrackProperty::PositionX);
    const auto* y = r.L(r.b)->tracks.find(TrackProperty::PositionY);
    AUREA_CHECK(x && x->keys.size() == 2 && y && y->keys.size() == 1);
    if (x && x->keys.size() == 2 && y && y->keys.size() == 1) {
        AUREA_CHECK_EQ(x->keys[0].time.value, 5);
        AUREA_CHECK_EQ(x->keys[1].time.value, 15);
        AUREA_CHECK_EQ(y->keys[0].time.value, 15);
    }
    r.undo();
    AUREA_CHECK_EQ(r.L(r.b)->tracks.size(), 0u);
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.a, refs, 3, 5, false), 3u);
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionX)->keys[0].time.value, 5);
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionY)->keys[0].time.value, 0);
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionX)->keys[0].time.value, 0);
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.a, refs, 3, 0, true), 3u);
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionX)->keys.size(), usize{1});
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->tracks.find(TrackProperty::PositionX)->keys.size(), usize{3});
}

// A timeline arrasta a seleção de keyframes (várias propriedades) com o
// `UndoBeginGroup` na FILA e a edição direta: o grupo precisa valer antes da
// 1ª mutação, senão o 1º passo do arrasto vira um desfazer à parte.
AUREA_TEST(Clipboard, QueuedUndoGroupWrapsDirectKeySelectionDragAcrossProperties) {
    EditRig r;
    auto* layer = r.comp()->layer(LayerId::unpack(r.a));
    for (i64 frame : {0, 20}) {
        layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{frame}, static_cast<f32>(frame));
        layer->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{frame + 5}, 1.0f);
    }
    const i64 opacity = static_cast<i64>(TrackProperty::Opacity);
    Command begin; begin.type = CommandType::UndoBeginGroup;
    AUREA_CHECK_EQ(r.e.submit_commands(&begin, 1, nullptr, 0), 1u);
    i64 refs[] = {0, -1, 0, 20, opacity, -1, 0, 25};
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.a, refs, 2, 3, false), 2u);
    refs[3] = 23; refs[7] = 28;
    AUREA_CHECK_EQ(r.e.edit_keyframe_selection(r.a, refs, 2, 2, false), 2u);
    Command end; end.type = CommandType::UndoEndGroup;
    AUREA_CHECK_EQ(r.e.submit_commands(&end, 1, nullptr, 0), 1u);
    AUREA_CHECK(r.e.render_frame().ok());
    const auto* x = r.L(r.a)->tracks.find(TrackProperty::PositionX);
    const auto* o = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(x && x->keys.size() == 2 && o && o->keys.size() == 2);
    if (x && o && x->keys.size() == 2 && o->keys.size() == 2) {
        AUREA_CHECK_EQ(x->keys[0].time.value, 0);
        AUREA_CHECK_EQ(x->keys[1].time.value, 25);
        AUREA_CHECK_EQ(o->keys[0].time.value, 5);
        AUREA_CHECK_EQ(o->keys[1].time.value, 30);
    }
    r.undo();
    x = r.L(r.a)->tracks.find(TrackProperty::PositionX);
    o = r.L(r.a)->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(x && o && x->keys.size() == 2 && o->keys.size() == 2);
    if (x && o && x->keys.size() == 2 && o->keys.size() == 2) {
        AUREA_CHECK_EQ(x->keys[1].time.value, 20);
        AUREA_CHECK_EQ(o->keys[1].time.value, 25);
    }
}

AUREA_TEST(Clipboard, RepeatedEffectsKeepSeparateKeysAndLockedTargetsStayUntouched) {
    EditRig r;
    for (u64 id : {r.a, r.b}) {
        for (int index = 0; index < 2; ++index) {
            Command command;
            command.type = CommandType::EffectAdd;
            command.effect_add.layer = LayerId::unpack(id);
            command.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
            command.effect_add.index = kInvalidIndex;
            AUREA_CHECK(r.e.apply_command(command).ok());
        }
    }
    Layer* source = r.comp()->layer(LayerId::unpack(r.a));
    for (u32 index = 0; index < 2; ++index) {
        source->tracks.get_or_create(TrackProperty::EffectParam, source->effects[index].id, 0)
            .set(source->local_time(FrameIndex{10}), 3.0f + index * 10.0f);
    }
    AUREA_CHECK_EQ(r.e.copy_keyframes(r.a, 10), 2u);
    r.comp()->layer(LayerId::unpack(r.b))->locked = true;
    AUREA_CHECK_EQ(r.e.paste_keyframes(&r.b, 1, 40), 0u);
    AUREA_CHECK_EQ(r.L(r.b)->tracks.size(), 0u);
    r.comp()->layer(LayerId::unpack(r.b))->locked = false;
    AUREA_CHECK_EQ(r.e.paste_keyframes(&r.b, 1, 40), 2u);
    const Layer* target = r.L(r.b);
    for (u32 index = 0; index < 2; ++index) {
        const Track* track = target->tracks.find(TrackProperty::EffectParam, target->effects[index].id, 0);
        AUREA_CHECK(track && track->keys.size() == 1);
        if (track && track->keys.size() == 1)
            AUREA_CHECK_EQ(track->keys[0].value, 3.0f + index * 10.0f);
    }
    r.undo();
    AUREA_CHECK_EQ(r.L(r.b)->tracks.size(), 0u);
}

// =============================================================================
//  Pré-composição
// =============================================================================
AUREA_TEST(Precomp, MovesLayersKeepsTimesAndSurvivesReopen) {
    EditRig r;
    // B filho de A; pré-compõe A e B.
    Command pc;
    pc.type = CommandType::LayerSetParent;
    pc.layer_parent.layer = LayerId::unpack(r.b);
    pc.layer_parent.parent = LayerId::unpack(r.a);
    AUREA_CHECK(r.e.apply_command(pc).ok());
    r.comp()->layer(LayerId::unpack(r.a))->name = "A";
    r.comp()->layer(LayerId::unpack(r.b))->name = "B";
    const CompositionId mainId = r.e.project()->timeline().current();
    const u64 ids[2] = {r.a, r.b};
    auto pre = r.e.precompose(ids, 2, "Grupo");
    AUREA_CHECK(pre.ok());
    const Layer* p = r.L(*pre);
    AUREA_CHECK(p && p->kind == LayerKind::Composition);
    AUREA_CHECK(p && p->start.value == 0 && p->end.value == 60 && p->offset.value == 0);
    AUREA_CHECK_EQ(r.comp()->layers().count(), 2u);   // C + a pré-composição
    const Composition* child = p ? r.e.project()->timeline().composition(p->nested.composition) : nullptr;
    AUREA_CHECK(child && child->layers().count() == 2u);
    const Layer* ca = nullptr;
    const Layer* cb = nullptr;
    if (child) child->layers().for_each([&](LayerId, const Layer& l) { if (l.name == "A") ca = &l; if (l.name == "B") cb = &l; });
    AUREA_CHECK(ca && cb && cb->start.value == 30 && cb->end.value == 60);
    AUREA_CHECK(ca && cb && child->layer(cb->parent) == ca);
    // Salvar e reabrir: a principal continua sendo a principal e a camada
    // continua apontando para a MESMA pré-composição.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_precomp.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Timeline& tl = r.e.project()->timeline();
    const Composition* mainC = tl.composition(tl.current());
    AUREA_CHECK(mainC && mainC->layers().count() == 2u);
    AUREA_CHECK(tl.current() == tl.root());
    const Layer* reP = nullptr;
    if (mainC) mainC->layers().for_each([&](LayerId, const Layer& l) { if (l.kind == LayerKind::Composition) reP = &l; });
    AUREA_CHECK(reP != nullptr);
    const Composition* reChild = reP ? tl.composition(reP->nested.composition) : nullptr;
    AUREA_CHECK(reChild && reChild->layers().count() == 2u && reChild != mainC);
    (void)mainId;
    std::remove(path.c_str());
}

// =============================================================================
//  Gizmo 3D
// =============================================================================
AUREA_TEST(Gizmo, AxesProjectAndMoveInWorldEvenWithAParent) {
    EditRig r;
    // Camada 2D comum: sem gizmo 3D.
    f32 g[8] = {};
    AUREA_CHECK(!r.e.query_gizmo(r.a, 100.0f, g));
    // Nulo girado em X vive no espaço 3D: gizmo com X para a direita e Y para baixo.
    const u64 n = *r.e.add_null(false);
    Layer* nl = r.comp()->layer(LayerId::unpack(n));
    nl->transform.rotation = Vec3{30, 0, 0};
    AUREA_CHECK(r.e.query_gizmo(n, 50.0f, g));
    std::printf("    gizmo: origem (%.1f, %.1f) X (%.1f, %.1f) Y (%.1f, %.1f) Z (%.1f, %.1f)\n",
                g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7]);
    AUREA_CHECK(g[2] > g[0] + 20.0f && std::fabs(g[3] - g[1]) < 1.0f);   // X → direita
    AUREA_CHECK(g[5] > g[1] + 20.0f && std::fabs(g[4] - g[0]) < 1.0f);   // Y → baixo
    // Z para dentro da tela: encolhe em perspectiva, perto da origem.
    AUREA_CHECK(std::hypot(g[6] - g[0], g[7] - g[1]) < 10.0f);
    // Filho do nulo (escala 2 no pai): 10 unidades no mundo = 5 no espaço do pai.
    nl->transform.rotation = Vec3{0, 0, 0};
    nl->transform.scale = Vec3{2, 2, 1};   // Z relativo a X: profundidade também 2
    nl->threeD = true;
    Layer* bl = r.comp()->layer(LayerId::unpack(r.b));
    bl->parent = LayerId::unpack(n);
    const f32 x0 = bl->transform.position.x;
    f32 out[3] = {};
    AUREA_CHECK(r.e.gizmo_move_local(r.b, 0, 10.0f, out));
    AUREA_CHECK(std::fabs(out[0] - (x0 + 5.0f)) < 1e-3f);
    AUREA_CHECK(r.e.gizmo_move_local(r.b, 2, 8.0f, out));
    AUREA_CHECK(std::fabs(out[2] - (bl->transform.position.z + 4.0f)) < 1e-3f);
}

AUREA_TEST(Gizmo, LocalAxesFollowRotationAndRespectParentScale) {
    EditRig r;
    Layer* child = r.comp()->layer(LayerId::unpack(r.b));
    Layer* parent = r.comp()->layer(LayerId::unpack(r.a));
    child->threeD = true;
    child->transform.rotation = Vec3{0, 0, 90};
    f32 world[8]{}, local[8]{};
    AUREA_CHECK(r.e.query_gizmo(r.b, 25, world));
    AUREA_CHECK(r.e.query_gizmo(r.b, 25, local, true));
    AUREA_CHECK(world[2] > world[0]);
    AUREA_CHECK(std::fabs(local[2] - local[0]) < 0.01f);
    AUREA_CHECK(local[3] > local[1]);
    parent->threeD = true;
    parent->transform.scale = Vec3{2, 2, 2};
    child->parent = LayerId::unpack(r.a);
    const Vec3 original = child->transform.position;
    f32 moved[3]{};
    AUREA_CHECK(r.e.gizmo_move_local(r.b, 3, 10, moved));
    AUREA_CHECK(std::fabs(moved[0] - original.x) < 0.01f);
    AUREA_CHECK(std::fabs(moved[1] - original.y - 5) < 0.01f);
    AUREA_CHECK(std::fabs(moved[2] - original.z) < 0.01f);
    AUREA_CHECK(!r.e.gizmo_move_local(r.b, 6, 10, moved));
}

AUREA_TEST(Gizmo, AnimatedAnchorKeepsGizmoAtEvaluatedPivot) {
    EditRig r;
    Layer* child = r.comp()->layer(LayerId::unpack(r.b));
    child->threeD = true;
    child->transform.rotation = Vec3{20,15,30};
    f32 before[8]{},after[8]{};
    AUREA_CHECK(r.e.query_gizmo(r.b,50,before));
    child->tracks.get_or_create(TrackProperty::AnchorX).set(FrameIndex{0},130);
    child->tracks.get_or_create(TrackProperty::AnchorY).set(FrameIndex{0},75);
    child->tracks.get_or_create(TrackProperty::AnchorZ).set(FrameIndex{0},20);
    AUREA_CHECK(r.e.query_gizmo(r.b,50,after));
    for(u32 i=0;i<8;++i)AUREA_CHECK_NEAR(before[i],after[i],.01f);
}


// "Vincular a novo nulo": um toque cria o nulo no centro das camadas e liga
// todas a ele sem ninguém sair do lugar na tela; um desfazer volta tudo.
AUREA_TEST(Parenting, SelectionParentsToNewNullWithoutMovingAndUndoesOnce) {
    EditRig r;
    Layer* la = r.comp()->layer(LayerId::unpack(r.a));
    Layer* lb = r.comp()->layer(LayerId::unpack(r.b));
    la->transform.position = Vec3{100.0f, 50.0f, 0.0f};
    lb->transform.position = Vec3{220.0f, 130.0f, 0.0f};
    lb->transform.rotation = Vec3{0.0f, 0.0f, 30.0f};
    lb->transform.scale = Vec3{2.0f, 2.0f, 1.0f};
    const FrameIndex t{0};
    const Mat4 wa = layer_world_matrix(*r.comp(), *la, t), wb = layer_world_matrix(*r.comp(), *lb, t);
    const u32 before = r.comp()->layers().count();
    const u64 ids[2] = {r.a, r.b};
    auto made = r.e.parent_to_new_null(ids, 2);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const Layer* n = r.L(*made);
    AUREA_CHECK(n && n->kind == LayerKind::Null && !n->threeD);
    AUREA_CHECK_EQ(r.comp()->layers().count(), before + 1);
    AUREA_CHECK(r.L(r.a)->parent == LayerId::unpack(*made));
    AUREA_CHECK(r.L(r.b)->parent == LayerId::unpack(*made));
    // O nulo nasce no centro dos pontos de ancoragem no mundo e cobre os filhos no tempo.
    const Vec4 oa = wa * Vec4{la->transform.anchor.x, la->transform.anchor.y, 0, 1};
    const Vec4 ob = wb * Vec4{lb->transform.anchor.x, lb->transform.anchor.y, 0, 1};
    AUREA_CHECK_NEAR(n->transform.position.x, (oa.x + ob.x) * 0.5f, 0.01f);
    AUREA_CHECK_NEAR(n->transform.position.y, (oa.y + ob.y) * 0.5f, 0.01f);
    AUREA_CHECK(n->start.value == 0 && n->end.value >= 60);
    // Compensação: nada mudou na tela.
    const Mat4 wa2 = layer_world_matrix(*r.comp(), *r.L(r.a), t), wb2 = layer_world_matrix(*r.comp(), *r.L(r.b), t);
    for (int c = 0; c < 4; ++c) {
        AUREA_CHECK_NEAR(wa2.col[c].x, wa.col[c].x, 0.01f); AUREA_CHECK_NEAR(wa2.col[c].y, wa.col[c].y, 0.01f);
        AUREA_CHECK_NEAR(wb2.col[c].x, wb.col[c].x, 0.01f); AUREA_CHECK_NEAR(wb2.col[c].y, wb.col[c].y, 0.01f);
    }
    r.undo();   // UM passo: some o nulo e os vínculos
    AUREA_CHECK_EQ(r.comp()->layers().count(), before);
    AUREA_CHECK(!r.L(r.a)->parent.valid() && !r.L(r.b)->parent.valid());
}

// =============================================================================
//  Escalonar: cascata de N quadros na ordem escolhida (camadas ou só animação)
// =============================================================================
AUREA_TEST(Stagger, LayersCascadeInGivenOrderSkipLockedAndUndoOnce) {
    EditRig r;
    r.range(r.a, 0, 30); r.range(r.b, 0, 30); r.range(r.c, 0, 30);
    const u64 ids[3] = {r.c, r.a, r.b};              // ordem da pessoa, não a de criação
    auto moved = r.e.stagger_layers(ids, 3, 4, false);
    AUREA_CHECK(moved.ok() && *moved == 2);
    AUREA_CHECK(r.at(r.c, 0, 30) && r.at(r.a, 4, 34) && r.at(r.b, 8, 38));
    r.undo();                                         // UM passo
    AUREA_CHECK(r.at(r.a, 0, 30) && r.at(r.b, 0, 30) && r.at(r.c, 0, 30));
    // Bloqueada não anda nem ocupa um degrau.
    r.comp()->layer(LayerId::unpack(r.a))->locked = true;
    moved = r.e.stagger_layers(ids, 3, 5, false);
    AUREA_CHECK(moved.ok() && *moved == 1);
    AUREA_CHECK(r.at(r.a, 0, 30) && r.at(r.c, 0, 30) && r.at(r.b, 5, 35));
    r.comp()->layer(LayerId::unpack(r.a))->locked = false;
    // Passo negativo que passaria do zero é recusado sem entrar no histórico.
    const u32 depth = r.e.history().depth();
    AUREA_CHECK(!r.e.stagger_layers(ids, 3, -3, false).ok());
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
    // Cascata além do fim estica o projeto.
    const u64 far[3] = {r.a, r.b, r.c};
    AUREA_CHECK(r.e.stagger_layers(far, 3, 200, false).ok());
    AUREA_CHECK(r.comp()->duration().value >= 430);
}

AUREA_TEST(Stagger, KeysOnlyShiftsEveryAnimationButKeepsTheBars) {
    EditRig r;
    r.range(r.a, 0, 90); r.range(r.b, 0, 90);
    Layer* b = r.comp()->layer(LayerId::unpack(r.b));
    Track& x = b->tracks.get_or_create(TrackProperty::PositionX);
    (void)x.set(FrameIndex{0}, 0.0f); (void)x.set(FrameIndex{20}, 100.0f);
    Track& o = b->tracks.get_or_create(TrackProperty::Opacity);
    (void)o.set(FrameIndex{5}, 0.0f); (void)o.set(FrameIndex{15}, 1.0f);
    Mask m; m.id = b->alloc_mask_id();
    MaskPathKey mk; mk.frame = 10; m.pathKeys.push_back(mk);
    b->masks.push_back(m);
    const u64 ids[2] = {r.a, r.b};
    auto moved = r.e.stagger_layers(ids, 2, 6, true);
    AUREA_CHECK(moved.ok() && *moved == 1);
    b = r.comp()->layer(LayerId::unpack(r.b));
    AUREA_CHECK(r.at(r.b, 0, 90));                    // a barra ficou
    const Track* nx = b->tracks.find(TrackProperty::PositionX);
    const Track* no = b->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(nx && nx->keys.size() == 2 && nx->keys[0].time.value == 6 && nx->keys[1].time.value == 26);
    AUREA_CHECK(no && no->keys[0].time.value == 11 && no->keys[1].time.value == 21);
    AUREA_CHECK(b->masks.size() == 1 && b->masks[0].pathKeys[0].frame == 16);
    AUREA_CHECK_NEAR(nx->sample_keys(FrameIndex{16}), 50.0f, 1e-3f);
    r.undo();
    b = r.comp()->layer(LayerId::unpack(r.b));
    AUREA_CHECK(b->tracks.find(TrackProperty::PositionX)->keys[0].time.value == 0);
    AUREA_CHECK(b->masks[0].pathKeys[0].frame == 10);
    // Uma camada só não escalona.
    AUREA_CHECK(!r.e.stagger_layers(ids, 1, 6, true).ok());
}

// =============================================================================
// LINHA MAGNÉTICA (2026-09-27): edição de vídeo POR FAIXA.
//
// Uma linha é o `zOrder`. O split já põe as duas metades na mesma linha (a
// cópia mantém o zOrder do original); estes testes provam que o ripple de uma
// linha NÃO arrasta quem está em outra — que é a diferença entre editar vídeo
// e compor camadas.
// =============================================================================
namespace {

/// Três trechos em fila na LINHA 0 e um overlay solto na LINHA 1.
struct MagneticRig {
    EditRig r;
    u64 overlay = 0;
    MagneticRig() {
        clip_source(r, r.a); clip_source(r, r.b); clip_source(r, r.c);
        // Mesma linha para os três: é o que o split produz.
        const u32 row = r.comp()->layer(LayerId::unpack(r.a))->trackId;
        r.comp()->layer(LayerId::unpack(r.b))->trackId = row;
        r.comp()->layer(LayerId::unpack(r.c))->trackId = row;
        overlay = *r.e.add_null(false);
        range(overlay, 40, 70);
    }
    void range(u64 id, i64 s, i64 en) { r.range(id, s, en); }
    void magnetic(bool on) {
        for (u64 id : {r.a, r.b, r.c}) (void)r.e.set_layer_magnetic_track(id, on);
    }
    i64 start_of(u64 id) { return r.L(id)->start.value; }
    i64 end_of(u64 id) { return r.L(id)->end.value; }
};

} // namespace

AUREA_TEST(MagneticTrack, DeleteClosesTheHoleOnlyOnItsOwnTrack) {
    MagneticRig m;
    m.magnetic(true);
    AUREA_CHECK(m.r.e.ripple_delete(&m.r.b, 1));
    // A linha fecha: o C encosta no fim do A.
    AUREA_CHECK_EQ(m.start_of(m.r.a), 0);
    AUREA_CHECK_EQ(m.start_of(m.r.c), 30);
    AUREA_CHECK_EQ(m.end_of(m.r.c), 60);
    // O overlay de OUTRA linha não se move: é composição, não montagem.
    AUREA_CHECK_EQ(m.start_of(m.overlay), 40);
    AUREA_CHECK_EQ(m.end_of(m.overlay), 70);
    m.r.undo();
    AUREA_CHECK_EQ(m.start_of(m.r.c), 60);
}

AUREA_TEST(MagneticTrack, DeletingWithoutMagneticLeavesTheHoleAlone) {
    MagneticRig m;
    m.magnetic(false);
    // Apagar de verdade (LayerDelete) nunca fechou buraco nenhum: quem fecha
    // é a ação "excluir e fechar o espaço" ou o ripple da linha magnética.
    Command del;
    del.type = CommandType::LayerDelete;
    del.layer_ref.layer = LayerId::unpack(m.r.b);
    AUREA_CHECK(m.r.e.apply_command(del).ok());
    AUREA_CHECK_EQ(m.start_of(m.r.c), 60);
    AUREA_CHECK_EQ(m.start_of(m.overlay), 40);

    // "Remover espaços vazios" continua sendo a ação de COMPOSIÇÃO de sempre:
    // sem nenhuma linha magnética, ela fecha a composição inteira.
    // O buraco é [30,40) — o vão entre o A e o overlay que segura [40,70).
    AUREA_CHECK_EQ(m.r.e.remove_gaps(), 10);
    AUREA_CHECK_EQ(m.start_of(m.overlay), 30);
    AUREA_CHECK_EQ(m.start_of(m.r.c), 50);
}

AUREA_TEST(MagneticTrack, RippleTrimPullsOnlyTheNeighboursOfTheSameRow) {
    MagneticRig m;
    m.magnetic(true);
    // Apara o começo do B em 8 frames: ele fica parado e o C vem junto.
    AUREA_CHECK(m.r.e.edit_clip_time(m.r.b, 0, 38));
    AUREA_CHECK_EQ(m.start_of(m.r.b), 30);
    AUREA_CHECK_EQ(m.end_of(m.r.b), 52);
    AUREA_CHECK_EQ(m.start_of(m.r.c), 52);
    AUREA_CHECK_EQ(m.start_of(m.overlay), 40);
}

AUREA_TEST(MagneticTrack, ReorderPutsTheCutWhereItWasDroppedAndKeepsTheRowPacked) {
    MagneticRig m;
    m.magnetic(true);
    // Arrasta o C para o começo da linha: ele vira o primeiro.
    AUREA_CHECK(m.r.e.reorder_clip(m.r.c, 0));
    AUREA_CHECK_EQ(m.start_of(m.r.c), 0);
    AUREA_CHECK_EQ(m.start_of(m.r.a), 30);
    AUREA_CHECK_EQ(m.start_of(m.r.b), 60);
    // Sem buraco: 0..30, 30..60, 60..90.
    AUREA_CHECK_EQ(m.end_of(m.r.b), 90);
    AUREA_CHECK_EQ(m.start_of(m.overlay), 40);
    m.r.undo();
    AUREA_CHECK_EQ(m.start_of(m.r.a), 0);
    AUREA_CHECK_EQ(m.start_of(m.r.c), 60);
}

AUREA_TEST(MagneticTrack, ReorderRefusesOnALooseClipAndOnALockedRow) {
    MagneticRig m;
    m.magnetic(false);
    AUREA_CHECK(!m.r.e.reorder_clip(m.r.c, 0));      // sem linha magnética
    AUREA_CHECK_EQ(m.start_of(m.r.c), 60);
    m.magnetic(true);
    m.r.comp()->layer(LayerId::unpack(m.r.a))->locked = true;
    AUREA_CHECK(!m.r.e.reorder_clip(m.r.c, 0));      // vizinho travado
    AUREA_CHECK_EQ(m.start_of(m.r.c), 60);
}

AUREA_TEST(MagneticTrack, NewClipLandsAtTheEndOfTheMagneticRow) {
    MagneticRig m;
    m.magnetic(true);
    m.r.e.set_edit_mode(true);                        // modo Edição: a linha manda
    // Um trecho NOVO com FONTE: nasce encostado no fim da linha magnética.
    const LayerId added = m.r.comp()->add_layer(LayerKind::Video, "novo");
    const Layer* l = m.r.comp()->layer(added);
    AUREA_CHECK(l != nullptr);
    AUREA_CHECK_EQ(l->trackId, m.r.comp()->layer(LayerId::unpack(m.r.a))->trackId);
    AUREA_CHECK_EQ(l->start.value, 90);               // encostado no fim do C
}

AUREA_TEST(MagneticTrack, QueryLayersCarriesIndependentSplitRowsToTheUi) {
    MagneticRig m;
    // Existing neighbours retain their shared row. Splitting creates an
    // independent layer row, as requested, and both native bridges receive it.
    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_ref.layer = LayerId::unpack(m.r.a);
    split.layer_split.at = FrameIndex{10};
    AUREA_CHECK(m.r.e.apply_command(split).ok());
    bridge::LayerRow rows[8]{};
    const u32 n = m.r.e.query_layers(rows, 8, nullptr, 0);
    AUREA_CHECK_EQ(n, 5u);
    const u32 row = m.r.comp()->layer(LayerId::unpack(m.r.a))->trackId;
    AUREA_CHECK(row != 0u);
    u32 sameRow = 0;
    for (u32 i = 0; i < n; ++i) {
        const Layer* l = m.r.comp()->layer(LayerId::unpack(rows[i].id));
        AUREA_CHECK(l != nullptr);
        AUREA_CHECK_EQ(rows[i].trackId, l->trackId);
        if (rows[i].trackId == row) ++sameRow;
    }
    AUREA_CHECK_EQ(sameRow, 3u);                      // first part of A, B and C
    u32 splitRows = 0;
    for (u32 i = 0; i < n; ++i) {
        const Layer* l = m.r.comp()->layer(LayerId::unpack(rows[i].id));
        if (l && l->start.value == 10 && l->end.value == 30) {
            AUREA_CHECK(rows[i].trackId != row);
            AUREA_CHECK(rows[i].trackId != m.r.L(m.overlay)->trackId);
            ++splitRows;
        }
    }
    AUREA_CHECK_EQ(splitRows, 1u);
    AUREA_CHECK(m.r.L(m.overlay)->trackId != row);    // o overlay é outra linha
}

AUREA_TEST(MagneticTrack, TheFlagSurvivesSaveAndLoad) {
    MagneticRig m;
    m.magnetic(true);
    const char* path = "aurea_test_magnetic.aurea";
    AUREA_CHECK(m.r.e.save_project(path).ok());
    AUREA_CHECK(m.r.e.load_project(path).ok());
    AUREA_CHECK(m.r.e.layer_magnetic_track(m.r.a));
    AUREA_CHECK(m.r.e.layer_magnetic_track(m.r.c));
    AUREA_CHECK(!m.r.e.layer_magnetic_track(m.overlay));
    std::remove(path);
}
