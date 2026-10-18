// =============================================================================
//  INTERFACE DO PROJETO (0.2.5): Composição (padrão) e Edição. A escolha fica
//  no projeto; ligar a Edição liga o ímã e escolhe a trilha principal.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;

namespace {

struct UiRig {
    Engine e;
    UiRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~UiRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 add(LayerKind kind, const char* name, i64 start, i64 end) {
        const u64 id = comp()->add_layer(kind, name).pack();
        L(id)->start = FrameIndex{start};
        L(id)->end = FrameIndex{end};
        return id;
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
};

std::string temp_path(const char* name) {
    const char* dir = std::getenv("TEMP");
    return std::string(dir ? dir : ".") + "/" + name;
}

} // namespace

AUREA_TEST(UiMode, NewProjectOpensInCompositionAndRejectsUnknownModes) {
    UiRig r;
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 0u);
    AUREA_CHECK(!r.e.set_project_ui_mode(2));
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 0u);
    AUREA_CHECK(!r.e.edit_mode());
}

AUREA_TEST(UiMode, EditTurnsTheMagnetOnAndAdoptsTheDeepestMediaLine) {
    UiRig r;
    const u64 video = r.add(LayerKind::Video, "V", 0, 60);
    const u64 text = r.add(LayerKind::Text, "T", 0, 30);
    AUREA_CHECK(!r.L(video)->magneticTrack);
    AUREA_CHECK_EQ(r.e.main_track_anchor(), video);
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 1u);
    AUREA_CHECK(r.e.edit_mode());
    AUREA_CHECK(r.L(video)->magneticTrack);
    AUREA_CHECK(!r.L(text)->magneticTrack);   // texto em outra linha não vira trilha principal
    // Um passo de desfazer devolve o ímã e a linha soltos; a interface fica.
    r.undo();
    AUREA_CHECK(!r.e.edit_mode());
    AUREA_CHECK(!r.L(video)->magneticTrack);
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 1u);
    // Voltar à Composição não desliga o ímã de quem já tem.
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    AUREA_CHECK(r.e.set_project_ui_mode(0));
    AUREA_CHECK(r.e.edit_mode());
    AUREA_CHECK(r.L(video)->magneticTrack);
}

AUREA_TEST(UiMode, ExistingMagneticChoiceIsKept) {
    UiRig r;
    const u64 low = r.add(LayerKind::Video, "Baixo", 0, 30);
    const u64 high = r.add(LayerKind::Video, "Alto", 0, 30);
    r.L(high)->magneticTrack = true;
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    AUREA_CHECK(!r.L(low)->magneticTrack);
    AUREA_CHECK(r.L(high)->magneticTrack);
}

AUREA_TEST(UiMode, ModeSurvivesSaveAndLoad) {
    UiRig r;
    (void)r.add(LayerKind::Image, "I", 0, 30);
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    const std::string path = temp_path("aurea_test_ui_mode.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.new_project(320, 180, 30.0, nullptr).ok());
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 0u);
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 1u);
    AUREA_CHECK(r.e.edit_mode());
    AUREA_CHECK(r.e.set_project_ui_mode(0));
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(r.e.project_ui_mode(), 0u);
    std::remove(path.c_str());
}

AUREA_TEST(UiMode, ToOverlayLeavesTheMainTrackPackedAndUndoesOnce) {
    UiRig r;
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    // Três cortes na trilha principal (modo Edição: cada um entra no fim dela).
    const u64 a = r.comp()->add_layer(LayerKind::Video, "A").pack();
    r.L(a)->start = FrameIndex{0}; r.L(a)->end = FrameIndex{30};
    const u64 b = r.comp()->add_layer(LayerKind::Video, "B").pack();
    r.L(b)->start = FrameIndex{30}; r.L(b)->end = FrameIndex{60}; r.L(b)->trackId = r.L(a)->trackId; r.L(b)->magneticTrack = true;
    const u64 c = r.comp()->add_layer(LayerKind::Video, "C").pack();
    r.L(c)->start = FrameIndex{60}; r.L(c)->end = FrameIndex{90}; r.L(c)->trackId = r.L(a)->trackId; r.L(c)->magneticTrack = true;
    r.L(a)->magneticTrack = true;
    const u32 main = r.L(a)->trackId;
    AUREA_CHECK(r.e.move_clip_to_overlay(b, 15));
    AUREA_CHECK(r.L(b)->trackId != main);
    AUREA_CHECK(!r.L(b)->magneticTrack);
    AUREA_CHECK_EQ(r.L(b)->start.value, 15); AUREA_CHECK_EQ(r.L(b)->end.value, 45);
    AUREA_CHECK_EQ(r.L(c)->start.value, 30);   // o buraco de B fechou
    AUREA_CHECK(r.comp()->z_index_of(LayerId::unpack(b)) > r.comp()->z_index_of(LayerId::unpack(c)));
    AUREA_CHECK_EQ(r.e.main_track_anchor(), a);
    r.undo();
    AUREA_CHECK_EQ(r.L(b)->trackId, main);
    AUREA_CHECK_EQ(r.L(b)->start.value, 30);
    AUREA_CHECK_EQ(r.L(c)->start.value, 60);
}

AUREA_TEST(UiMode, ToMainTrackAppendsAtTheEndMagnetic) {
    UiRig r;
    const u64 a = r.add(LayerKind::Video, "A", 0, 30);
    const u64 o = r.add(LayerKind::Image, "O", 5, 20);
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    AUREA_CHECK(!r.e.move_clip_to_main_track(a));   // a própria trilha principal
    AUREA_CHECK(r.e.move_clip_to_main_track(o));
    AUREA_CHECK_EQ(r.L(o)->trackId, r.L(a)->trackId);
    AUREA_CHECK(r.L(o)->magneticTrack);
    AUREA_CHECK_EQ(r.L(o)->start.value, 30); AUREA_CHECK_EQ(r.L(o)->end.value, 45);
    AUREA_CHECK(!r.e.move_clip_to_main_track(o));   // já mora nela
    const u64 t = r.add(LayerKind::Text, "T", 0, 10);
    AUREA_CHECK(!r.e.move_clip_to_main_track(t));   // texto não é trilha principal
}

AUREA_TEST(UiMode, PhotosJoinTheEndOfTheMainTrackForThreeSeconds) {
    UiRig r;
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    std::vector<u8> px(16 * 9 * 4, 200);
    const auto first = r.e.import_image(px.data(), 16, 9, "A", "test://a");
    AUREA_CHECK(first.ok());
    const auto second = r.e.import_image(px.data(), 16, 9, "B", "test://b");
    AUREA_CHECK(second.ok());
    if (!first.ok() || !second.ok()) return;
    const Layer* a = r.L(*first);
    const Layer* b = r.L(*second);
    AUREA_CHECK(a && b);
    if (!a || !b) return;
    AUREA_CHECK(a->magneticTrack && b->magneticTrack);
    AUREA_CHECK_EQ(a->start.value, 0);                   // a primeira foto abre a trilha no zero
    AUREA_CHECK_EQ(a->end.value - a->start.value, 90);
    AUREA_CHECK_EQ(a->trackId, b->trackId);
    AUREA_CHECK_EQ(b->start.value, a->end.value);   // encostada no fim, não no cabeçote
    AUREA_CHECK_EQ(b->end.value - b->start.value, 90);   // 3 s a 30 fps
}

AUREA_TEST(UiMode, CutTransitionSetsBothSidesInOneUndo) {
    UiRig r;
    const u64 a = r.add(LayerKind::Video, "A", 0, 30);
    const u64 b = r.add(LayerKind::Video, "B", 30, 40);
    AUREA_CHECK(r.e.set_cut_transition(a, b, 1, 15));
    AUREA_CHECK_EQ(r.L(a)->transitionOut, 1); AUREA_CHECK_EQ(r.L(a)->transitionOutFrames, 15u);
    AUREA_CHECK_EQ(r.L(b)->transitionIn, 1);  AUREA_CHECK_EQ(r.L(b)->transitionInFrames, 5u);   // metade de B
    AUREA_CHECK_EQ(r.e.cut_transition(b), 1u);
    AUREA_CHECK(!r.e.set_cut_transition(a, b, 9, 15));
    r.undo();
    AUREA_CHECK_EQ(r.L(a)->transitionOut, 0); AUREA_CHECK_EQ(r.L(b)->transitionIn, 0);
}

AUREA_TEST(UiMode, FirstMainClipToOverlayLeavesTheRestAsMainTrack) {
    UiRig r;
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    std::vector<u8> px(16 * 9 * 4, 120);
    const auto a = r.e.import_image(px.data(), 16, 9, "A", "test://a");
    const auto b = r.e.import_image(px.data(), 16, 9, "B", "test://b");
    AUREA_CHECK(a.ok() && b.ok());
    if (!a.ok() || !b.ok()) return;
    AUREA_CHECK_EQ(r.e.main_track_anchor(), *a);
    AUREA_CHECK(r.e.move_clip_to_overlay(*a));
    // O primeiro corte sobe para uma linha acima; o B vira a trilha principal e encosta no zero.
    AUREA_CHECK_EQ(r.e.main_track_anchor(), *b);
    AUREA_CHECK(r.L(*a)->trackId != r.L(*b)->trackId);
    AUREA_CHECK(r.comp()->z_index_of(LayerId::unpack(*a)) > r.comp()->z_index_of(LayerId::unpack(*b)));
    AUREA_CHECK_EQ(r.L(*b)->start.value, 0);
}

AUREA_TEST(UiMode, SplitKeepsBothPiecesOnTheMainTrackOnlyInEditing) {
    UiRig r;
    AUREA_CHECK(r.e.set_project_ui_mode(1));
    std::vector<u8> px(16 * 9 * 4, 90);
    const auto a = r.e.import_image(px.data(), 16, 9, "A", "test://a");
    AUREA_CHECK(a.ok());
    if (!a.ok()) return;
    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_ref.layer = LayerId::unpack(*a);
    split.layer_split.at = FrameIndex{45};
    AUREA_CHECK(r.e.apply_command(split).ok());
    u32 onMain = 0;
    r.comp()->layers().for_each([&](LayerId, const Layer& l) { if (l.trackId == r.L(*a)->trackId) ++onMain; });
    AUREA_CHECK_EQ(onMain, 2u);
    // Na Composição o pedaço novo continua ganhando a linha dele.
    AUREA_CHECK(r.e.set_project_ui_mode(0));
    split.layer_split.at = FrameIndex{20};
    AUREA_CHECK(r.e.apply_command(split).ok());
    u32 after = 0;
    r.comp()->layers().for_each([&](LayerId, const Layer& l) { if (l.trackId == r.L(*a)->trackId) ++after; });
    AUREA_CHECK_EQ(after, 2u);
}
