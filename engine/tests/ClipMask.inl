// =============================================================================
//  Aurea / tests / ClipMask.inl  (incluído no fim de test_engine.cpp)
//
//  Máscara de recorte por camada (`Layer::clipToBelow`): elegibilidade, o
//  comando "Criar/Liberar máscara de recorte" (um passo de desfazer), as
//  regras que mantêm as cadeias coerentes depois das edições estruturais e a
//  gravação no projeto (v48).
// =============================================================================

#include "aurea/project/Serialization.hpp"

namespace {

/// Camadas de baixo para cima, por tipo; devolve os ids na mesma ordem.
std::vector<LayerId> clip_stack(Composition& c, std::initializer_list<LayerKind> kinds) {
    std::vector<LayerId> ids;
    for (LayerKind k : kinds) ids.push_back(c.add_layer(k, ""));
    return ids;
}

bool clipped(const Composition& c, LayerId id) {
    const Layer* l = c.layer(id);
    return l && l->clipToBelow;
}

void set_clip(Composition& c, LayerId id, bool on) { c.layer(id)->clipToBelow = on; }

} // namespace

AUREA_TEST(ClipMask, EligibilityFollowsVisualKinds) {
    Composition c("t");
    const auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Image, LayerKind::Audio, LayerKind::Text,
                                    LayerKind::Null, LayerKind::Video, LayerKind::Camera, LayerKind::Shape});
    // Fundo da pilha: nunca.
    AUREA_CHECK(!can_create_clip(c, 0));
    // Imagem sobre forma: sim.
    AUREA_CHECK(can_create_clip(c, 1));
    // Áudio não é recortado; o texto acima do áudio não tem base.
    AUREA_CHECK(!can_create_clip(c, 2));
    AUREA_CHECK(!can_create_clip(c, 3));
    // Null não é base; câmera não é recortada; forma sobre câmera: sem base.
    AUREA_CHECK(!can_create_clip(c, 5));
    AUREA_CHECK(!can_create_clip(c, 6));
    AUREA_CHECK(!can_create_clip(c, 7));
    // Camada de ajuste: nem recortada nem base.
    c.layer(ids[1])->adjustment = true;
    AUREA_CHECK(!can_create_clip(c, 1));
    c.layer(ids[1])->adjustment = false;
    // Já recortada: "Criar" não vale (o menu mostra "Liberar"); a de cima
    // dela entra na mesma cadeia.
    set_clip(c, ids[1], true);
    AUREA_CHECK(!can_create_clip(c, 1));
    AUREA_CHECK_EQ(clip_base_index(c, 1), 0);
    AUREA_CHECK_EQ(clip_base_index(c, 0), -1);
}

AUREA_TEST(ClipMask, ValidationReleasesBottomAndIneligibleBases) {
    Composition c("t");
    auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Image, LayerKind::Image});
    // Recortada no fundo: vira base; a de cima continua recortada por ela.
    set_clip(c, ids[0], true);
    set_clip(c, ids[2], true);
    AUREA_CHECK_EQ(enforce_clip_chains(c, nullptr), 1u);
    AUREA_CHECK(!clipped(c, ids[0]));
    AUREA_CHECK(clipped(c, ids[2]));
    // Base que não pode ser base (virou ajuste): a cadeia acima solta inteira.
    Composition d("t");
    ids = clip_stack(d, {LayerKind::Shape, LayerKind::Shape, LayerKind::Image, LayerKind::Image});
    set_clip(d, ids[2], true);
    set_clip(d, ids[3], true);
    d.layer(ids[1])->adjustment = true;
    (void)enforce_clip_chains(d, nullptr);
    AUREA_CHECK(!clipped(d, ids[2]));
    AUREA_CHECK(!clipped(d, ids[3]));
    // Áudio com a flag (arquivo mexido à mão): solta, e a de cima também.
    Composition e("t");
    ids = clip_stack(e, {LayerKind::Shape, LayerKind::Audio, LayerKind::Image});
    set_clip(e, ids[1], true);
    set_clip(e, ids[2], true);
    (void)enforce_clip_chains(e, nullptr);
    AUREA_CHECK(!clipped(e, ids[1]));
    AUREA_CHECK(!clipped(e, ids[2]));
}

AUREA_TEST(ClipMask, ReorderRulesKeepChainsCoherent) {
    // Pilha: A (normal), B (base), C1, C2 (recortadas), Z (normal).
    auto build = [](Composition& c) {
        auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Shape, LayerKind::Image, LayerKind::Text, LayerKind::Shape});
        set_clip(c, ids[2], true);
        set_clip(c, ids[3], true);
        return ids;
    };
    auto move = [](Composition& c, LayerId id, u32 to) {
        ClipChainEdit edit(&c);
        edit.moved(id);   // como o comando de reordenar
        AUREA_CHECK(c.reorder_layer(id, to));
    };
    {   // Recortada levada para longe das outras (o topo): perde o recorte.
        Composition c("t");
        const auto ids = build(c);
        move(c, ids[2], 4);
        AUREA_CHECK(!clipped(c, ids[2]));
        AUREA_CHECK(clipped(c, ids[3]));   // C2 continua na cadeia de B
        AUREA_CHECK_EQ(clip_base_index(c, static_cast<u32>(c.z_index_of(ids[3]))), c.z_index_of(ids[1]));
    }
    {   // Recortada levada para o fundo da pilha: perde o recorte.
        Composition c("t");
        const auto ids = build(c);
        move(c, ids[3], 0);
        AUREA_CHECK(!clipped(c, ids[3]));
        AUREA_CHECK(clipped(c, ids[2]));
    }
    {   // Reordenar DENTRO da cadeia: as duas continuam recortadas.
        Composition c("t");
        const auto ids = build(c);
        move(c, ids[3], 2);
        AUREA_CHECK(clipped(c, ids[2]) && clipped(c, ids[3]));
    }
    {   // Normal levada para o MEIO da cadeia: vira parte dela.
        Composition c("t");
        const auto ids = build(c);
        move(c, ids[4], 3);   // entre C1 e C2
        AUREA_CHECK(clipped(c, ids[4]));
        AUREA_CHECK(clipped(c, ids[2]) && clipped(c, ids[3]));
        Composition d("t");
        const auto jd = build(d);
        move(d, jd[0], 1);    // A entre B e C1: entra na cadeia de B
        AUREA_CHECK(clipped(d, jd[0]));
        AUREA_CHECK_EQ(clip_base_index(d, 1), 0);
    }
    {   // Normal levada para logo ACIMA da cadeia: continua normal.
        Composition c("t");
        const auto ids = build(c);
        move(c, ids[0], 4);
        AUREA_CHECK(!clipped(c, ids[0]));
        AUREA_CHECK(clipped(c, ids[2]) && clipped(c, ids[3]));
    }
    {   // A base andou para longe: a cadeia que ficou solta.
        Composition c("t");
        const auto ids = build(c);
        move(c, ids[1], 4);
        AUREA_CHECK(!clipped(c, ids[2]) && !clipped(c, ids[3]));
        AUREA_CHECK(!clipped(c, ids[1]));
    }
    {   // A base sobe um degrau (A C1 B C2): C1 solta, C2 continua com B.
        Composition c("t");
        const auto ids = build(c);
        move(c, ids[1], 2);
        AUREA_CHECK(!clipped(c, ids[2]));
        AUREA_CHECK(!clipped(c, ids[1]));
        AUREA_CHECK(clipped(c, ids[3]));
        AUREA_CHECK_EQ(clip_base_index(c, static_cast<u32>(c.z_index_of(ids[3]))), c.z_index_of(ids[1]));
    }
    {   // Recortada levada para o lado de outra cadeia intacta: entra nela.
        Composition c("t");
        auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Image, LayerKind::Shape, LayerKind::Image,
                                  LayerKind::Image, LayerKind::Shape});
        set_clip(c, ids[1], true);              // cadeia X: 0 ← 1
        set_clip(c, ids[3], true);              // cadeia Y: 2 ← 3, 4
        set_clip(c, ids[4], true);
        move(c, ids[1], 3);                     // 1 entre 3 e 4
        AUREA_CHECK(clipped(c, ids[1]));
        AUREA_CHECK_EQ(clip_base_index(c, static_cast<u32>(c.z_index_of(ids[1]))), c.z_index_of(ids[2]));
    }
}

AUREA_TEST(ClipMask, DeletingTheBaseClipsByTheLayerThatStays) {
    {   // Base no meio: a cadeia passa a recortar pela camada que ficou embaixo.
        Composition c("t");
        auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Shape, LayerKind::Image, LayerKind::Image});
        set_clip(c, ids[2], true);
        set_clip(c, ids[3], true);
        {
            ClipChainEdit edit(&c);
            AUREA_CHECK(c.remove_layer(ids[1]));
        }
        AUREA_CHECK(clipped(c, ids[2]) && clipped(c, ids[3]));
        AUREA_CHECK_EQ(clip_base_index(c, 1), 0);
    }
    {   // Base no fundo: a primeira recortada vira a base das outras.
        Composition c("t");
        auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Image, LayerKind::Image});
        set_clip(c, ids[1], true);
        set_clip(c, ids[2], true);
        {
            ClipChainEdit edit(&c);
            AUREA_CHECK(c.remove_layer(ids[0]));
        }
        AUREA_CHECK(!clipped(c, ids[1]));
        AUREA_CHECK(clipped(c, ids[2]));
    }
}

AUREA_TEST(ClipMask, DuplicateAndPasteKeepOrDropTheChain) {
    {   // Duplicar a base: a cópia fica base (não vira recortada da original).
        Composition c("t");
        auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Image});
        set_clip(c, ids[1], true);
        LayerId copy;
        {
            ClipChainEdit edit(&c);
            copy = c.duplicate_layer(ids[0], FrameIndex{0});
            edit.alias(copy, ids[0]);
        }
        AUREA_CHECK(!clipped(c, copy));
        AUREA_CHECK(clipped(c, ids[1]));
        AUREA_CHECK_EQ(c.z_index_of(copy), 1);
    }
    {   // Duplicar uma recortada: a cópia entra na mesma cadeia.
        Composition c("t");
        auto ids = clip_stack(c, {LayerKind::Shape, LayerKind::Image});
        set_clip(c, ids[1], true);
        LayerId copy;
        {
            ClipChainEdit edit(&c);
            copy = c.duplicate_layer(ids[1], FrameIndex{0});
            edit.alias(copy, ids[1]);
        }
        AUREA_CHECK(clipped(c, copy));
        AUREA_CHECK_EQ(clip_base_index(c, static_cast<u32>(c.z_index_of(copy))), 0);
    }
    {   // Colar uma recortada sozinha sobre outra camada: perde o recorte.
        //  Colar base + recortada juntas: a cadeia continua.
        Composition c("t");
        (void)clip_stack(c, {LayerKind::Shape});
        LayerId lone, base, child;
        {
            ClipChainEdit edit(&c);
            lone = c.add_layer(LayerKind::Image, "");
            set_clip(c, lone, true);
        }
        AUREA_CHECK(!clipped(c, lone));
        {
            ClipChainEdit edit(&c);
            base = c.add_layer(LayerKind::Shape, "");
            child = c.add_layer(LayerKind::Image, "");
            set_clip(c, child, true);
        }
        AUREA_CHECK(clipped(c, child));
        AUREA_CHECK_EQ(clip_base_index(c, static_cast<u32>(c.z_index_of(child))), c.z_index_of(base));
    }
}

AUREA_TEST(ClipMask, CommandCreatesReleasesAndUndoes) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    const auto b = e.add_shape(10);
    const auto c = e.add_shape(10);
    AUREA_CHECK(a.ok() && b.ok() && c.ok());
    // A de baixo não pode; as de cima podem (as linhas vêm da frente para trás).
    AUREA_CHECK(!e.set_layer_clip_to_below(*a, true));
    bridge::LayerRow rows[4];
    AUREA_CHECK_EQ(e.query_layers(rows, 4, nullptr, 0), 3u);
    AUREA_CHECK((rows[2].flags & bridge::kLayerRowFlagClipCandidate) == 0);   // a (fundo)
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagClipCandidate) != 0);   // c
    AUREA_CHECK(e.set_layer_clip_to_below(*b, true));
    AUREA_CHECK(e.set_layer_clip_to_below(*c, true));
    AUREA_CHECK_EQ(e.query_layers(rows, 4, nullptr, 0), 3u);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagClipToBelow) != 0);
    AUREA_CHECK((rows[1].flags & bridge::kLayerRowFlagClipToBelow) != 0);
    AUREA_CHECK((rows[0].flags & bridge::kLayerRowFlagClipCandidate) == 0);  // já recortada: "Liberar"
    bridge::LayerDetailPOD d;
    AUREA_CHECK(e.query_layer_detail(*c, d));
    AUREA_CHECK((d.flags & bridge::kLayerRowFlagClipToBelow) != 0);
    // Liberar b solta também c (a cadeia logo acima) — num passo só.
    AUREA_CHECK(e.set_layer_clip_to_below(*b, false));
    AUREA_CHECK(!layer_of(e, LayerId::unpack(*b))->clipToBelow);
    AUREA_CHECK(!layer_of(e, LayerId::unpack(*c))->clipToBelow);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(layer_of(e, LayerId::unpack(*b))->clipToBelow);
    AUREA_CHECK(layer_of(e, LayerId::unpack(*c))->clipToBelow);
    // Reordenar pelo comando: b (recortada) vai para o fundo e perde o recorte.
    Command reorder;
    reorder.type = CommandType::LayerReorder;
    reorder.layer_reorder.layer = LayerId::unpack(*b);
    reorder.layer_reorder.newIndex = 0;
    AUREA_CHECK(e.apply_command(reorder).ok());
    AUREA_CHECK(!layer_of(e, LayerId::unpack(*b))->clipToBelow);
    AUREA_CHECK(layer_of(e, LayerId::unpack(*c))->clipToBelow);   // c: ainda sobre a, que é base
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(layer_of(e, LayerId::unpack(*b))->clipToBelow);
    // Camada de ajuste não serve de base: a cadeia acima de a solta.
    AUREA_CHECK(e.set_layer_adjustment(*a, true));
    AUREA_CHECK(!layer_of(e, LayerId::unpack(*b))->clipToBelow);
    AUREA_CHECK(!layer_of(e, LayerId::unpack(*c))->clipToBelow);
    e.shutdown();
}

AUREA_TEST(ClipMask, SplittingTheBaseKeepsTheChain) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto base = e.add_shape(10);
    const auto child = e.add_shape(10);
    AUREA_CHECK(base.ok() && child.ok());
    AUREA_CHECK(e.set_layer_clip_to_below(*child, true));
    const Layer* bl = layer_of(e, LayerId::unpack(*base));
    const i64 mid = (bl->start.value + bl->end.value) / 2;
    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = LayerId::unpack(*base);
    split.layer_split.at = FrameIndex{mid};
    AUREA_CHECK(e.apply_command(split).ok());
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK_EQ(comp->order().size(), 3u);
    const Layer* second = comp->layer(comp->order().at(1));
    AUREA_CHECK(second && !second->clipToBelow);               // o 2º pedaço é base
    AUREA_CHECK(layer_of(e, LayerId::unpack(*child))->clipToBelow);
    // Apagar o 2º pedaço: a recortada passa a recortar pelo 1º.
    Command del;
    del.type = CommandType::LayerDelete;
    del.layer_ref.layer = comp->order().at(1);
    AUREA_CHECK(e.apply_command(del).ok());
    AUREA_CHECK(layer_of(e, LayerId::unpack(*child))->clipToBelow);
    e.shutdown();
}

AUREA_TEST(ClipMask, ProjectRoundTripKeepsTheFlag) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const auto base = e.add_shape(10);
    const auto child = e.add_shape(10);
    const auto other = e.add_shape(10);
    AUREA_CHECK(base.ok() && child.ok() && other.ok());
    AUREA_CHECK(e.set_layer_clip_to_below(*child, true));
    const char* file = "aurea_test_clip_mask.aurea";
    AUREA_CHECK(e.save_project(file).ok());
    Project loaded;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load(loaded, file, LoadOptions{}, &report).ok());
    AUREA_CHECK_EQ(report.timelineVersion, kTimelineSectionVersion);
    const Composition* c = loaded.timeline().composition(loaded.timeline().root());
    AUREA_CHECK(c && c->order().size() == 3u);
    AUREA_CHECK(!c->layer(c->order().at(0))->clipToBelow);
    AUREA_CHECK(c->layer(c->order().at(1))->clipToBelow);
    AUREA_CHECK(!c->layer(c->order().at(2))->clipToBelow);
    std::remove(file);
    e.shutdown();
}
