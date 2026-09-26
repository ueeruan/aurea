// =============================================================================
//  Modo Edição (timeline magnética): aparar empurra/puxa, excluir fecha o
//  buraco, remover espaços vazios, aparar o projeto — tudo com desfazer.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"

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
