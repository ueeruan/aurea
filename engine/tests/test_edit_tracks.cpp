// =============================================================================
//  Faixas da Edição: V1/V2/A1…, e os comandos de montagem do fluxo Premiere.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/core/Time.hpp"

#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

struct TrackRig {
    Engine e;
    TrackRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~TrackRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 add(LayerKind kind, const char* name, i64 start, i64 end) {
        const u64 id = comp()->add_layer(kind, name).pack();
        L(id)->start = FrameIndex{start};
        L(id)->end = FrameIndex{end};
        return id;
    }
    /// Vídeo de `sourceFrames` quadros, ligado a uma mídia de verdade do projeto.
    u64 video(const char* name, i64 start, i64 end, i64 sourceFrames, bool sound = true) {
        Asset a;
        a.kind = AssetKind::Video;
        a.name = name;
        a.duration = FrameIndex{sourceFrames};
        a.timebaseFps = 30.0;
        a.audio.channels = sound ? 2 : 0;
        const AssetId asset = e.project()->add_asset(std::move(a));
        const u64 id = add(LayerKind::Video, name, start, end);
        L(id)->source = asset;
        return id;
    }
    void enter() {
        AUREA_CHECK(e.set_project_ui_mode(1));
        (void)e.query_edit_timeline();
    }
    u32 track(u32 index) { return comp()->edit_tracks()[index].id; }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    bool edit(TrackEditOp op, u64 layer, i64 frame, u32 track = 0) { return e.edit_track_clip(static_cast<u32>(op), layer, frame, track); }
};

} // namespace

AUREA_TEST(EditTracks, ExistingProjectBecomesVideoAndAudioTracks) {
    TrackRig r;
    const u64 video = r.video("V", 0, 60, 300);
    const u64 text = r.add(LayerKind::Text, "T", 0, 30);
    const u64 music = r.add(LayerKind::Audio, "M", 0, 90);
    r.enter();
    const auto& tracks = r.comp()->edit_tracks();
    AUREA_CHECK_EQ(tracks.size(), 6u);
    AUREA_CHECK(!tracks[0].audio && !tracks[2].audio && tracks[3].audio && tracks[5].audio);
    AUREA_CHECK_EQ(r.L(video)->trackId, r.track(0));   // V1
    AUREA_CHECK_EQ(r.L(text)->trackId, r.track(1));    // V2
    AUREA_CHECK_EQ(r.L(music)->trackId, r.track(4));   // A2: a A1 fica com o áudio ligado
    // V2 desenha por cima de V1.
    AUREA_CHECK(r.L(text)->zOrder > r.L(video)->zOrder);
    const std::vector<i64> q = r.e.query_edit_timeline();
    AUREA_CHECK_EQ(q[0], 6);
    AUREA_CHECK_EQ(q[1], 1);   // só o vídeo tem áudio ligado
    AUREA_CHECK_EQ(q[4 + 12], static_cast<i64>(video));
    AUREA_CHECK_EQ(q[4 + 13], static_cast<i64>(r.track(3)));
}

AUREA_TEST(EditTracks, ClipsThatOverlapNeverShareATrack) {
    TrackRig r;
    const u64 a = r.add(LayerKind::Text, "A", 0, 60);
    const u64 b = r.add(LayerKind::Text, "B", 30, 90);
    const u64 c = r.add(LayerKind::Text, "C", 40, 100);
    const u64 d = r.add(LayerKind::Text, "D", 50, 110);
    r.enter();
    AUREA_CHECK(r.L(a)->trackId != r.L(b)->trackId);
    AUREA_CHECK(r.L(c)->trackId != r.L(b)->trackId && r.L(c)->trackId != r.L(a)->trackId);
    AUREA_CHECK(r.comp()->edit_track(r.L(d)->trackId) != nullptr);   // abriu faixa nova
    // Duplicar cai em cima do original: a cópia vai para outra faixa.
    const LayerId copy = r.comp()->duplicate_layer(LayerId::unpack(a), FrameIndex{0});
    (void)r.e.query_edit_timeline();
    AUREA_CHECK(r.comp()->layer(copy)->trackId != r.L(a)->trackId);
}

AUREA_TEST(EditTracks, DragOverwritesWhatIsUnderneathAndUndoBringsItBack) {
    TrackRig r;
    const u64 a = r.video("A", 0, 60, 300);
    const u64 b = r.video("B", 60, 120, 300);
    r.enter();
    AUREA_CHECK(r.edit(TrackEditOp::Move, b, 30));
    AUREA_CHECK_EQ(r.L(a)->end.value, 30);
    AUREA_CHECK_EQ(r.L(b)->start.value, 30);
    AUREA_CHECK_EQ(r.L(b)->end.value, 90);
    AUREA_CHECK_EQ(r.comp()->duration().value, 90);
    r.undo();
    AUREA_CHECK_EQ(r.L(a)->end.value, 60);
    AUREA_CHECK_EQ(r.L(b)->start.value, 60);
    // No meio de um clipe maior: ele vira dois pedaços.
    const u64 small = r.add(LayerKind::Text, "T", 300, 310);
    (void)r.e.query_edit_timeline();
    const u32 layersBefore = r.comp()->layers().count();
    AUREA_CHECK(r.edit(TrackEditOp::Move, small, 20, r.track(0)));
    AUREA_CHECK_EQ(r.comp()->layers().count(), layersBefore + 1);
    AUREA_CHECK_EQ(r.L(a)->end.value, 20);
    AUREA_CHECK_EQ(r.L(small)->trackId, r.track(0));
}

AUREA_TEST(EditTracks, AudioStaysOnAudioTracksAndVideoOnVideoTracks) {
    TrackRig r;
    const u64 video = r.video("V", 0, 60, 300);
    const u64 music = r.add(LayerKind::Audio, "M", 100, 160);
    r.enter();
    AUREA_CHECK(!r.edit(TrackEditOp::Move, video, 0, r.track(3)));
    AUREA_CHECK(!r.edit(TrackEditOp::Move, music, 100, r.track(0)));
    // Em cima do áudio ligado de um vídeo, as duas ondas tocariam juntas.
    AUREA_CHECK(!r.edit(TrackEditOp::Move, music, 10, r.track(3)));
    AUREA_CHECK(r.edit(TrackEditOp::MoveLinkedAudio, video, 0, r.track(5)));
    AUREA_CHECK(r.edit(TrackEditOp::Move, music, 10, r.track(3)));
    AUREA_CHECK_EQ(r.L(music)->trackId, r.track(3));
}

AUREA_TEST(EditTracks, TrimStopsAtTheNeighbourAndAtTheEndOfTheMedia) {
    TrackRig r;
    const u64 a = r.video("A", 0, 60, 90);
    const u64 b = r.video("B", 80, 120, 300);
    r.enter();
    AUREA_CHECK(r.edit(TrackEditOp::TrimOut, a, 200));
    AUREA_CHECK_EQ(r.L(a)->end.value, 80);       // vizinho
    AUREA_CHECK(!r.edit(TrackEditOp::TrimIn, b, 10));   // já encostado no vizinho
    AUREA_CHECK_EQ(r.L(b)->start.value, 80);
    (void)b;
    const u64 c = r.video("C", 200, 260, 90);
    (void)r.e.query_edit_timeline();
    AUREA_CHECK(r.L(c)->trackId != 0);
    r.L(c)->trackId = r.track(0);
    AUREA_CHECK(r.edit(TrackEditOp::TrimOut, c, 900));
    AUREA_CHECK_EQ(r.L(c)->end.value, 290);      // 90 quadros de mídia, e não além
    AUREA_CHECK(r.edit(TrackEditOp::TrimIn, c, 215));
    AUREA_CHECK_EQ(r.L(c)->offset.value, 15);
    AUREA_CHECK_EQ(r.L(c)->start.value, 215);
    AUREA_CHECK(r.edit(TrackEditOp::TrimIn, c, 0));
    AUREA_CHECK_EQ(r.L(c)->start.value, 200);    // volta só até o quadro zero da mídia
    AUREA_CHECK_EQ(r.L(c)->offset.value, 0);
}

AUREA_TEST(EditTracks, RippleMovesWhatComesAfterAndNeverOverlaps) {
    TrackRig r;
    const u64 a = r.video("A", 0, 60, 300);
    const u64 b = r.video("B", 60, 120, 300);
    const u64 c = r.video("C", 120, 180, 300);
    const u64 over = r.add(LayerKind::Text, "T", 130, 150);
    const u64 span = r.add(LayerKind::Text, "S", 20, 200);
    r.enter();
    AUREA_CHECK(r.L(span)->trackId != r.L(over)->trackId);
    AUREA_CHECK(r.edit(TrackEditOp::RippleTrimOut, a, 40));
    AUREA_CHECK_EQ(r.L(a)->end.value, 40);
    AUREA_CHECK_EQ(r.L(b)->start.value, 40);
    AUREA_CHECK_EQ(r.L(c)->start.value, 100);
    AUREA_CHECK_EQ(r.L(over)->start.value, 110);  // faixa sincronizada anda junto
    AUREA_CHECK_EQ(r.L(span)->start.value, 20);   // quem cruza o corte fica
    AUREA_CHECK(r.edit(TrackEditOp::RippleDelete, b, 0));
    AUREA_CHECK(r.comp()->layer(LayerId::unpack(b)) == nullptr);
    AUREA_CHECK_EQ(r.L(c)->start.value, 40);
    AUREA_CHECK_EQ(r.L(c)->end.value, 100);
    r.undo();
    AUREA_CHECK(r.comp()->layer(LayerId::unpack(b)) != nullptr);
    AUREA_CHECK_EQ(r.L(c)->start.value, 100);
    // Faixa travada não anda.
    AUREA_CHECK(r.e.set_edit_track_flag(r.L(over)->trackId, 0, true));
    AUREA_CHECK(r.edit(TrackEditOp::RippleDelete, b, 0));
    AUREA_CHECK_EQ(r.L(over)->start.value, 110);
    AUREA_CHECK(!r.edit(TrackEditOp::TrimOut, over, 140));
}

AUREA_TEST(EditTracks, RollSlideAndSlipKeepTheSequenceLength) {
    TrackRig r;
    const u64 a = r.video("A", 0, 60, 300);
    const u64 b = r.video("B", 60, 120, 300);
    const u64 c = r.video("C", 120, 180, 300);
    r.L(b)->offset = FrameIndex{50};
    r.L(c)->offset = FrameIndex{50};
    r.enter();
    AUREA_CHECK(r.edit(TrackEditOp::Roll, a, 70));
    AUREA_CHECK_EQ(r.L(a)->end.value, 70);
    AUREA_CHECK_EQ(r.L(b)->start.value, 70);
    AUREA_CHECK_EQ(r.L(b)->offset.value, 60);
    AUREA_CHECK_EQ(r.comp()->duration().value, 180);
    AUREA_CHECK(r.edit(TrackEditOp::Slide, b, 10));
    AUREA_CHECK_EQ(r.L(a)->end.value, 80);
    AUREA_CHECK_EQ(r.L(b)->start.value, 80);
    AUREA_CHECK_EQ(r.L(b)->end.value, 130);
    AUREA_CHECK_EQ(r.L(c)->start.value, 130);
    AUREA_CHECK_EQ(r.L(c)->offset.value, 60);
    AUREA_CHECK_EQ(r.comp()->duration().value, 180);
    const i64 before = r.L(b)->offset.value;
    AUREA_CHECK(r.edit(TrackEditOp::Slip, b, -20));
    AUREA_CHECK_EQ(r.L(b)->offset.value, before + 20);
    AUREA_CHECK_EQ(r.L(b)->start.value, 80);
    AUREA_CHECK(r.edit(TrackEditOp::Slip, b, 100000));
    AUREA_CHECK_EQ(r.L(b)->offset.value, 0);      // parou no começo da mídia
}

AUREA_TEST(EditTracks, RazorCutsOneClipOrEveryTargetedTrack) {
    TrackRig r;
    const u64 a = r.video("A", 0, 100, 300);
    const u64 t = r.add(LayerKind::Text, "T", 10, 90);
    r.enter();
    const u32 n = r.comp()->layers().count();
    AUREA_CHECK(r.edit(TrackEditOp::Razor, a, 50));
    AUREA_CHECK_EQ(r.comp()->layers().count(), n + 1);
    AUREA_CHECK_EQ(r.L(a)->end.value, 50);
    u32 sameName = 0;
    r.comp()->layers().for_each([&](LayerId, const Layer& l) { sameName += l.name == "A"; });
    AUREA_CHECK_EQ(sameName, 2u);
    r.undo();
    AUREA_CHECK(r.e.set_edit_track_flag(r.L(t)->trackId, 3, false));
    AUREA_CHECK(r.edit(TrackEditOp::RazorAll, 0, 50));
    AUREA_CHECK_EQ(r.comp()->layers().count(), n + 1);   // V2 sem alvo ficou inteira
    AUREA_CHECK_EQ(r.L(t)->end.value, 90);
    AUREA_CHECK(r.e.set_edit_track_flag(r.L(t)->trackId, 3, true));
    AUREA_CHECK(r.edit(TrackEditOp::RazorAll, 0, 30));
    AUREA_CHECK_EQ(r.L(t)->end.value, 30);
    // Os pedaços ficam na mesma faixa.
    u32 onV1 = 0;
    r.comp()->layers().for_each([&](LayerId, const Layer& l) { onV1 += l.trackId == r.track(0); });
    AUREA_CHECK_EQ(onV1, 3u);
}

AUREA_TEST(EditTracks, InsertOpensSpaceAndOverwriteDoesNot) {
    TrackRig r;
    const u64 a = r.video("A", 0, 100, 300);
    const u64 over = r.add(LayerKind::Text, "T", 60, 80);
    const u64 fresh = r.video("N", 300, 340, 300);
    r.enter();
    r.L(fresh)->trackId = r.track(0);
    AUREA_CHECK(r.edit(TrackEditOp::MoveInsert, fresh, 50));
    AUREA_CHECK_EQ(r.L(a)->end.value, 50);
    AUREA_CHECK_EQ(r.L(fresh)->start.value, 50);
    AUREA_CHECK_EQ(r.L(fresh)->end.value, 90);
    AUREA_CHECK_EQ(r.L(over)->start.value, 100);    // sincronizada
    AUREA_CHECK_EQ(r.comp()->duration().value, 140);
}

AUREA_TEST(EditTracks, MarkedRangeLiftsOrExtracts) {
    TrackRig r;
    const u64 a = r.video("A", 0, 100, 300);
    r.enter();
    AUREA_CHECK(!r.e.edit_marked_range(false));
    AUREA_CHECK(r.e.set_edit_mark(0, 20));
    AUREA_CHECK(r.e.set_edit_mark(1, 40));
    AUREA_CHECK(r.e.edit_marked_range(false));
    AUREA_CHECK_EQ(r.L(a)->end.value, 20);
    AUREA_CHECK_EQ(r.comp()->layers().count(), 2u);
    r.undo();
    AUREA_CHECK_EQ(r.L(a)->end.value, 100);
    AUREA_CHECK(r.e.edit_marked_range(true));
    AUREA_CHECK_EQ(r.comp()->duration().value, 80);
    AUREA_CHECK_EQ(r.comp()->mark_in(), -1);
    // Marcas cruzadas não ficam.
    AUREA_CHECK(r.e.set_edit_mark(1, 10));
    AUREA_CHECK(r.e.set_edit_mark(0, 30));
    AUREA_CHECK_EQ(r.comp()->mark_out(), -1);
    AUREA_CHECK_EQ(r.comp()->mark_in(), 30);
}

AUREA_TEST(EditTracks, TrackSwitchesMuteAndHideWithoutTouchingTheClip) {
    TrackRig r;
    const u64 video = r.video("V", 0, 60, 300);
    const u64 music = r.add(LayerKind::Audio, "M", 0, 90);
    r.enter();
    const u32 v1 = r.track(0), a1 = r.track(3), a2 = r.track(4);
    AUREA_CHECK(r.e.set_edit_track_flag(v1, 1, true));
    AUREA_CHECK(r.L(video)->trackHidden && r.L(video)->visible);
    AUREA_CHECK(r.e.set_edit_track_flag(v1, 1, false));
    AUREA_CHECK(!r.L(video)->trackHidden);
    AUREA_CHECK(r.e.set_edit_track_flag(a1, 1, true));
    AUREA_CHECK(r.L(video)->trackMuted && !r.L(music)->trackMuted && !r.L(video)->muted);
    AUREA_CHECK(r.e.set_edit_track_flag(a1, 1, false));
    AUREA_CHECK(r.e.set_edit_track_flag(a2, 2, true));   // solo
    AUREA_CHECK(r.L(video)->trackMuted && !r.L(music)->trackMuted);
    // Voltar à Composição não pode deixar nada mudo sem chave para desligar.
    AUREA_CHECK(r.e.set_project_ui_mode(0));
    AUREA_CHECK(!r.L(video)->trackMuted);
}

AUREA_TEST(EditTracks, TracksAndMarksSurviveSaveAndReopen) {
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_edit_tracks.aurea";
    u32 v3 = 0, a3 = 0;
    {
        TrackRig r;
        (void)r.video("V", 0, 60, 300);
        r.enter();
        v3 = r.track(2); a3 = r.track(5);
        AUREA_CHECK(r.e.set_edit_track_flag(v3, 0, true));
        AUREA_CHECK(r.e.set_edit_track_flag(a3, 1, true));
        AUREA_CHECK(r.e.set_edit_mark(0, 12));
        AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    }
    TrackRig r;
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 1u);
    AUREA_CHECK_EQ(r.comp()->edit_tracks().size(), 6u);
    AUREA_CHECK(r.comp()->edit_track(v3) && r.comp()->edit_track(v3)->locked);
    AUREA_CHECK(r.comp()->edit_track(a3) && r.comp()->edit_track(a3)->muted);
    AUREA_CHECK_EQ(r.comp()->mark_in(), 12);
}
