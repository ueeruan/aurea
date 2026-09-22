// Testes da timeline e da composição.
#include "TestFramework.hpp"

#include "aurea/timeline/Timeline.hpp"
#include "aurea/timeline/Composition.hpp"

using namespace aurea;

namespace {
Composition make_comp(u32 w = 1920, u32 h = 1080, f64 fps = 60.0) {
    Composition c("teste");
    c.set_size(w, h);
    c.set_fps(fps);
    c.set_duration(FrameIndex{600});
    return c;
}
} // namespace

AUREA_TEST(Composition, AddLayerAppendsToOrder) {
    Composition c = make_comp();
    const LayerId a = c.add_layer(LayerKind::Video, "A");
    const LayerId b = c.add_layer(LayerKind::Image, "B");
    AUREA_CHECK_EQ(c.layers().count(), static_cast<u32>(2));
    AUREA_CHECK_EQ(c.order().size(), static_cast<u32>(2));
    AUREA_CHECK(c.order().at(0) == a);
    AUREA_CHECK(c.order().at(1) == b);
    // zOrder é derivado da ordem vertical, não uma cópia independente que pode
    // desatualizar.
    AUREA_CHECK_EQ(c.layer(a)->zOrder, static_cast<u32>(0));
    AUREA_CHECK_EQ(c.layer(b)->zOrder, static_cast<u32>(1));
}

AUREA_TEST(Composition, AddLayerUsesDefaultName) {
    Composition c = make_comp();
    const LayerId id = c.add_layer(LayerKind::Text, "");
    AUREA_CHECK(c.layer(id) != nullptr);
    AUREA_CHECK(!c.layer(id)->name.empty());
}

AUREA_TEST(Composition, RemoveLayerRebuildsOrder) {
    Composition c = make_comp();
    const LayerId a = c.add_layer(LayerKind::Video, "A");
    const LayerId b = c.add_layer(LayerKind::Video, "B");
    const LayerId d = c.add_layer(LayerKind::Video, "C");

    AUREA_CHECK(c.remove_layer(b));
    AUREA_CHECK_EQ(c.order().size(), static_cast<u32>(2));
    AUREA_CHECK(!c.layers().contains(b));
    AUREA_CHECK_EQ(c.layer(a)->zOrder, static_cast<u32>(0));
    AUREA_CHECK_EQ(c.layer(d)->zOrder, static_cast<u32>(1));
}

AUREA_TEST(Composition, RemoveLayerDetachesChildren) {
    // Um filho apontando para uma camada morta faria a avaliação de transform
    // subir para uma camada que não existe mais.
    Composition c = make_comp();
    const LayerId parent = c.add_layer(LayerKind::Null, "Pai");
    const LayerId child = c.add_layer(LayerKind::Video, "Filho");
    c.layer(child)->parent = parent;

    AUREA_CHECK(c.remove_layer(parent));
    AUREA_CHECK(!c.layer(child)->parent.valid());
}

AUREA_TEST(Composition, ReorderMovesVertically) {
    Composition c = make_comp();
    const LayerId a = c.add_layer(LayerKind::Video, "A");
    const LayerId b = c.add_layer(LayerKind::Video, "B");
    const LayerId d = c.add_layer(LayerKind::Video, "C");

    AUREA_CHECK(c.reorder_layer(d, 0));
    AUREA_CHECK(c.order().at(0) == d);
    AUREA_CHECK(c.order().at(1) == a);
    AUREA_CHECK(c.order().at(2) == b);
    // drawIndex acompanha: o renderer desenha pela ordem resolvida, não pelo
    // zOrder que a UI mostra.
    AUREA_CHECK_EQ(c.layer(d)->drawIndex, static_cast<u32>(0));
}

AUREA_TEST(Composition, DuplicatePlacesCopyAbove) {
    Composition c = make_comp();
    c.add_layer(LayerKind::Video, "A");
    const LayerId b = c.add_layer(LayerKind::Video, "B");

    const LayerId copy = c.duplicate_layer(b, FrameIndex{0});
    AUREA_CHECK(copy.valid());
    AUREA_CHECK_EQ(c.layers().count(), static_cast<u32>(3));
    // A cópia fica logo acima do original: é o que o usuário espera ao duplicar
    // no lugar, sem precisar arrastar.
    AUREA_CHECK(c.order().index_of(copy) > c.order().index_of(b));
    AUREA_CHECK(c.layer(copy)->name.find("copia") != std::string::npos);
}

AUREA_TEST(Composition, DuplicateIsDeepCopy) {
    Composition c = make_comp();
    const LayerId orig = c.add_layer(LayerKind::Video, "A");
    c.layer(orig)->tracks.get_or_create(TrackProperty::Opacity)
        .set(FrameIndex{0}, 0.5f);
    c.layer(orig)->effects.push_back(EffectInstance{});

    const LayerId copy = c.duplicate_layer(orig, FrameIndex{0});
    c.layer(copy)->tracks.get_or_create(TrackProperty::Opacity)
        .set(FrameIndex{0}, 0.9f);

    // Mexer na cópia não pode mexer no original. Sem cópia profunda dos tracks,
    // animar a duplicata animaria as duas.
    AUREA_CHECK_NEAR(c.layer(orig)->tracks.sample_or(TrackProperty::Opacity,
                                                     FrameIndex{0}, 0.0f), 0.5f, 1e-6);
}

AUREA_TEST(Composition, CollectActiveFiltersByTime) {
    Composition c = make_comp();
    const LayerId a = c.add_layer(LayerKind::Video, "A");
    const LayerId b = c.add_layer(LayerKind::Video, "B");
    c.layer(a)->start = FrameIndex{0};
    c.layer(a)->end = FrameIndex{100};
    c.layer(b)->start = FrameIndex{200};
    c.layer(b)->end = FrameIndex{300};

    std::vector<LayerId> active;
    AUREA_CHECK_EQ(c.collect_active(FrameIndex{50}, active), static_cast<u32>(1));
    AUREA_CHECK(active[0] == a);
    AUREA_CHECK_EQ(c.collect_active(FrameIndex{150}, active), static_cast<u32>(0));
    AUREA_CHECK_EQ(c.collect_active(FrameIndex{250}, active), static_cast<u32>(1));
}

AUREA_TEST(Composition, CollectActiveSkipsHidden) {
    Composition c = make_comp();
    const LayerId a = c.add_layer(LayerKind::Video, "A");
    c.layer(a)->visible = false;
    std::vector<LayerId> active;
    AUREA_CHECK_EQ(c.collect_active(FrameIndex{10}, active), static_cast<u32>(0));
}

AUREA_TEST(Composition, CollectActiveRespectsEndBoundary) {
    // O fim é EXCLUSIVO. Se fosse inclusivo, dois cortes adjacentes mostrariam
    // um frame repetido na junção — o tipo de erro que só aparece no export.
    Composition c = make_comp();
    const LayerId a = c.add_layer(LayerKind::Video, "A");
    c.layer(a)->start = FrameIndex{0};
    c.layer(a)->end = FrameIndex{10};
    std::vector<LayerId> active;
    AUREA_CHECK_EQ(c.collect_active(FrameIndex{9}, active), static_cast<u32>(1));
    AUREA_CHECK_EQ(c.collect_active(FrameIndex{10}, active), static_cast<u32>(0));
}

AUREA_TEST(Composition, CannotNestItself) {
    Composition c = make_comp();
    AUREA_CHECK(!c.can_nest(c));
}

AUREA_TEST(Composition, CannotNestBeyondMaxDepth) {
    Composition a = make_comp();
    Composition b = make_comp();
    a.set_nesting_depth(kMaxNestingDepth);
    AUREA_CHECK(!a.can_nest(b));
}

AUREA_TEST(Timeline, CreateCompositionSetsRootAndCurrent) {
    Timeline t;
    const CompositionId id = t.create_composition("Principal", 1920, 1080, 60.0);
    AUREA_CHECK(id.valid());
    AUREA_CHECK(t.root() == id);
    AUREA_CHECK(t.current() == id);
    AUREA_CHECK(t.composition(id) != nullptr);
    AUREA_CHECK_NEAR(t.composition(id)->fps(), 60.0, 1e-9);
}

AUREA_TEST(Timeline, RemoveCompositionRefusesRoot) {
    Timeline t;
    const CompositionId id = t.create_composition("Principal", 1920, 1080, 60.0);
    AUREA_CHECK(!t.remove_composition(id));
    AUREA_CHECK(t.composition(id) != nullptr);
}

AUREA_TEST(Timeline, SetCurrentRejectsUnknownId) {
    Timeline t;
    t.create_composition("Principal", 1920, 1080, 60.0);
    CompositionId bogus{};
    bogus.index = 999;
    bogus.generation = 2;
    AUREA_CHECK(!t.set_current(bogus));
}

AUREA_TEST(Timeline, SeekClampsNegative) {
    Timeline t;
    t.create_composition("Principal", 1920, 1080, 60.0);
    t.seek(FrameIndex{-50});
    AUREA_CHECK_EQ(t.playhead().value, static_cast<i64>(0));
}

AUREA_TEST(Timeline, SnapPointsFindNeighbors) {
    Timeline t;
    const CompositionId cid = t.create_composition("Principal", 1920, 1080, 60.0);
    Composition* c = t.composition(cid);
    const LayerId a = c->add_layer(LayerKind::Video, "A");
    c->layer(a)->start = FrameIndex{100};
    c->layer(a)->end = FrameIndex{200};

    // O ímã precisa achar as bordas: é o que faz alinhar corte com corte sem
    // precisar de zoom alto.
    AUREA_CHECK_EQ(t.next_snap_point(FrameIndex{50}, cid).value, static_cast<i64>(100));
    AUREA_CHECK_EQ(t.prev_snap_point(FrameIndex{150}, cid).value, static_cast<i64>(100));
    AUREA_CHECK_EQ(t.prev_snap_point(FrameIndex{500}, cid).value, static_cast<i64>(200));
}

AUREA_TEST(Timeline, SnapPointsIncludeKeyframes) {
    Timeline t;
    const CompositionId cid = t.create_composition("Principal", 1920, 1080, 60.0);
    Composition* c = t.composition(cid);
    const LayerId a = c->add_layer(LayerKind::Video, "A");
    c->layer(a)->start = FrameIndex{0};
    c->layer(a)->end = FrameIndex{1000};
    c->layer(a)->tracks.get_or_create(TrackProperty::Opacity).set(FrameIndex{300}, 1.0f);

    AUREA_CHECK_EQ(t.next_snap_point(FrameIndex{100}, cid).value, static_cast<i64>(300));
}

AUREA_TEST(Timeline, TotalDurationIsMaxAcrossCompositions) {
    Timeline t;
    const CompositionId a = t.create_composition("A", 1920, 1080, 60.0);
    t.composition(a)->set_duration(FrameIndex{600});
    const CompositionId b = t.create_composition("B", 1920, 1080, 60.0);
    t.composition(b)->set_duration(FrameIndex{1800});

    AUREA_CHECK_EQ(t.total_duration().value, static_cast<i64>(1800));
}

AUREA_TEST(Timeline, ResolveNestedTimeMapsThroughLayer) {
    Timeline t;
    const CompositionId outer = t.create_composition("Externa", 1920, 1080, 60.0);
    const CompositionId inner = t.create_composition("Interna", 1920, 1080, 60.0);
    t.composition(inner)->set_duration(FrameIndex{300});

    Composition* o = t.composition(outer);
    const LayerId layer = o->add_layer(LayerKind::Composition, "Pre-comp");
    o->layer(layer)->nested.composition = inner;
    o->layer(layer)->start = FrameIndex{100};
    o->layer(layer)->end = FrameIndex{400};

    CompositionId outComp{};
    FrameIndex outTime{};
    AUREA_CHECK(t.resolve_nested_time(outer, layer, FrameIndex{150}, outComp, outTime).ok());
    AUREA_CHECK(outComp == inner);
    // 150 - 100 = 50 dentro da pre-comp.
    AUREA_CHECK_EQ(outTime.value, static_cast<i64>(50));
}

AUREA_TEST(Timeline, NestedTimeClampsBeyondInnerDuration) {
    // Estender a camada além do material da pre-comp congela no último frame em
    // vez de sumir. Sumir seria um bug visível; congelar é o que o usuário
    // espera ter acontecido.
    Timeline t;
    const CompositionId outer = t.create_composition("Externa", 1920, 1080, 60.0);
    const CompositionId inner = t.create_composition("Interna", 1920, 1080, 60.0);
    t.composition(inner)->set_duration(FrameIndex{100});

    Composition* o = t.composition(outer);
    const LayerId layer = o->add_layer(LayerKind::Composition, "Pre-comp");
    o->layer(layer)->nested.composition = inner;

    CompositionId outComp{};
    FrameIndex outTime{};
    AUREA_CHECK(t.resolve_nested_time(outer, layer, FrameIndex{5000}, outComp, outTime).ok());
    AUREA_CHECK_EQ(outTime.value, static_cast<i64>(99));
}

AUREA_TEST(Layer, LocalAndTimelineTimeRoundTrip) {
    Layer l;
    l.start = FrameIndex{100};
    l.end = FrameIndex{200};
    l.offset = FrameIndex{25};

    const FrameIndex local = l.local_time(FrameIndex{150});
    AUREA_CHECK_EQ(local.value, static_cast<i64>(75));
    AUREA_CHECK_EQ(l.timeline_time(local).value, static_cast<i64>(150));
}

AUREA_TEST(Layer, EffectIdLookupIsByLocalId) {
    Layer l;
    EffectInstance a;
    a.id = l.alloc_effect_id();
    EffectInstance b;
    b.id = l.alloc_effect_id();
    l.effects.push_back(a);
    l.effects.push_back(b);

    EffectId ref{};
    ref.index = b.id;
    AUREA_CHECK_EQ(l.effect_index(ref), static_cast<u32>(1));
    AUREA_CHECK(l.find_effect(ref) != nullptr);

    // A posição e o id são coisas diferentes: reordenar muda o índice, não o id.
    // É por isso que a UI guarda o id — ela não pode falar de "o terceiro
    // efeito" sem errar depois de um arrasto.
    std::swap(l.effects[0], l.effects[1]);
    AUREA_CHECK_EQ(l.effect_index(ref), static_cast<u32>(0));
}

AUREA_TEST(Layer, MaskIdLookupIsByLocalId) {
    Layer l;
    Mask m;
    m.id = l.alloc_mask_id();
    m.name = "M1";
    l.masks.push_back(m);

    MaskId ref{};
    ref.index = m.id;
    AUREA_CHECK(l.find_mask(ref) != nullptr);
    AUREA_CHECK_EQ(l.find_mask(ref)->name, std::string("M1"));
}

AUREA_TEST(Layer, ContainsTimeIsHalfOpen) {
    Layer l;
    l.start = FrameIndex{10};
    l.end = FrameIndex{20};
    AUREA_CHECK(!l.contains_time(FrameIndex{9}));
    AUREA_CHECK(l.contains_time(FrameIndex{10}));
    AUREA_CHECK(l.contains_time(FrameIndex{19}));
    AUREA_CHECK(!l.contains_time(FrameIndex{20}));
}

AUREA_TEST(Composition, RetimeKeepsSecondsNotFrames) {
    Composition c = make_comp(1920, 1080, 30.0);
    c.set_duration(FrameIndex{300});                 // 10 s
    const LayerId id = c.add_layer(LayerKind::Video, "V");
    Layer* l = c.layer(id);
    l->start = FrameIndex{30};                       // 1 s
    l->end = FrameIndex{150};                        // 5 s
    l->offset = FrameIndex{15};                      // 0,5 s
    Track& t = l->tracks.get_or_create(TrackProperty::Opacity);
    t.keys.push_back(Keyframe{FrameIndex{0}, 0.0f});
    t.keys.push_back(Keyframe{FrameIndex{60}, 1.0f});
    const u64 rev = c.revision();

    c.retime(60.0);
    l = c.layer(id);
    AUREA_CHECK_EQ(c.fps(), 60.0);
    AUREA_CHECK_EQ(c.duration().value, static_cast<i64>(600));
    AUREA_CHECK_EQ(l->start.value, static_cast<i64>(60));
    AUREA_CHECK_EQ(l->end.value, static_cast<i64>(300));
    AUREA_CHECK_EQ(l->offset.value, static_cast<i64>(30));
    const Track* tr = l->tracks.find(TrackProperty::Opacity);
    AUREA_CHECK(tr != nullptr);
    AUREA_CHECK_EQ(tr->keys.size(), static_cast<size_t>(2));
    AUREA_CHECK_EQ(tr->keys[1].time.value, static_cast<i64>(120));
    AUREA_CHECK(c.revision() > rev);

    // Para baixo, keys que colapsam no mesmo frame viram um só.
    Track& dense = c.layer(id)->tracks.get_or_create(TrackProperty::RotationZ);
    dense.keys.push_back(Keyframe{FrameIndex{0}, 0.0f});
    dense.keys.push_back(Keyframe{FrameIndex{1}, 1.0f});
    dense.keys.push_back(Keyframe{FrameIndex{10}, 2.0f});
    c.retime(6.0);
    const Track* d = c.layer(id)->tracks.find(TrackProperty::RotationZ);
    AUREA_CHECK_EQ(d->keys.size(), static_cast<size_t>(2));
    AUREA_CHECK_EQ(d->keys[1].time.value, static_cast<i64>(1));
    AUREA_CHECK(c.layer(id)->end.value > c.layer(id)->start.value);
}
