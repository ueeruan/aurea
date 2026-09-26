// Testes da timeline e da composição.
#include "TestFramework.hpp"

#include "aurea/timeline/Timeline.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/playback/Playback.hpp"
#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/effects/EffectRegistry.hpp"

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

AUREA_TEST(Composition, ResizeFitsAnimatedHierarchyWithoutScalingChildrenTwice) {
    Composition c = make_comp(1280, 720, 30);
    const auto root = c.add_layer(LayerKind::Null, "root");
    const auto child = c.add_layer(LayerKind::Video, "video");
    auto* p = c.layer(root);
    p->transform.position = Vec3{640, 360, 0};
    auto& x = p->tracks.get_or_create(TrackProperty::PositionX);
    x.set(FrameIndex{0}, 640); x.set(FrameIndex{30}, 960);
    x.keys[0].tangentOut = 40;
    p->tracks.get_or_create(TrackProperty::ScaleX).set(FrameIndex{0}, 1);
    auto* v = c.layer(child); v->parent = root;
    v->transform.position = Vec3{120, 80, 0};
    v->transform.scale = Vec3{0.5f, 0.5f, 1};
    const auto snapshot = c.clone();
    c.resize_content(640, 360);
    AUREA_CHECK_EQ(c.width(), 640u);
    AUREA_CHECK_NEAR(p->transform.position.x, 320, 0.001f);
    AUREA_CHECK_NEAR(p->transform.scale.x, 0.5f, 0.001f);
    AUREA_CHECK_NEAR(p->tracks.find(TrackProperty::PositionX)->sample(FrameIndex{15}), 400, 0.001f);
    AUREA_CHECK_NEAR(p->tracks.find(TrackProperty::PositionX)->keys[0].tangentOut, 20, 0.001f);
    AUREA_CHECK_NEAR(p->tracks.find(TrackProperty::ScaleX)->sample(FrameIndex{0}), 0.5f, 0.001f);
    AUREA_CHECK_NEAR(v->transform.position.x, 120, 0.001f);
    AUREA_CHECK_NEAR(v->transform.scale.x, 0.5f, 0.001f);
    c.resize_content(1280, 720);
    AUREA_CHECK_NEAR(p->transform.position.x, 640, 0.001f);
    AUREA_CHECK_NEAR(p->transform.scale.x, 1, 0.001f);
    c.resize_content(720, 1280);
    AUREA_CHECK_NEAR(p->transform.position.x, 360, 0.001f);
    AUREA_CHECK_NEAR(p->transform.position.y, 640, 0.001f);
    AUREA_CHECK_NEAR(p->transform.scale.x, p->transform.scale.y, 0.001f);
    c.restore_from(*snapshot);
    AUREA_CHECK_EQ(c.width(), 1280u);
    AUREA_CHECK_NEAR(c.layer(root)->tracks.find(TrackProperty::PositionX)->sample(FrameIndex{15}), 800, 0.001f);
    AUREA_CHECK_EQ(c.layer(child)->parent, root);
}

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

AUREA_TEST(Composition, RippleKeepsMarkersOrderedAcrossUnchangedMarkers) {
    Composition c = make_comp(1920, 1080, 30.0);
    c.put_marker(Marker{FrameIndex{10}, 0xFFFFFFFFu, kMarkerManual, "earlier"});
    c.put_marker(Marker{FrameIndex{25}, 0xFF00FFFFu, kMarkerManual, "unchanged"});
    c.put_marker(Marker{FrameIndex{30}, 0xFFFF00FFu, kMarkerManual, "shifted"});
    c.put_marker(Marker{FrameIndex{45}, 0xFFFFFFFFu, kMarkerManual, "collision"});
    c.shift_from(FrameIndex{30}, -20);
    AUREA_CHECK_EQ(c.markers().size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(c.markers()[0].frame.value, 10);
    AUREA_CHECK_EQ(c.markers()[1].frame.value, 25);
    AUREA_CHECK_EQ(c.markers()[0].label, std::string("earlier"));
    AUREA_CHECK_EQ(c.markers()[1].label, std::string("unchanged"));
    // Insertion after ripple relies on the same sorted invariant as snapping.
    c.put_marker(Marker{FrameIndex{20}, 0xFFFFFFFFu, kMarkerManual, "inserted"});
    AUREA_CHECK_EQ(c.markers().size(), static_cast<usize>(3));
    AUREA_CHECK_EQ(c.markers()[1].frame.value, 20);
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

AUREA_TEST(Composition, RetimeKeepsLastMarkerInsideDuration) {
    Composition c = make_comp(1920, 1080, 60.0);
    c.set_duration(FrameIndex{60});
    c.put_marker(Marker{FrameIndex{59}, 0xFF00FFFFu, kMarkerManual, "last frame"});
    c.retime(30.0);
    AUREA_CHECK_EQ(c.duration().value, 30);
    AUREA_CHECK_EQ(c.markers().size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(c.markers()[0].frame.value, 29);
    AUREA_CHECK_EQ(c.markers()[0].label, std::string("last frame"));
    AUREA_CHECK_EQ(c.markers()[0].color, 0xFF00FFFFu);
}

// -----------------------------------------------------------------------------
// O cursor passa do fim
// -----------------------------------------------------------------------------

AUREA_TEST(Timeline, PlayheadGoesPastTheEndOfTheComposition) {
    // A duração diz até onde o CONTEÚDO roda, não até onde a timeline existe.
    // Prender o cursor ao último quadro travava a timeline inteira no fim do
    // projeto: o scrub, o passo e o zoom param todos no mesmo clamp.
    PlaybackController p;
    p.configure(30.0, FrameIndex{294});   // 9 s 24 q — o projeto do relato
    AUREA_CHECK_EQ(p.current().value, static_cast<i64>(0));

    p.seek(FrameIndex{600}, 1000);
    std::printf("\n    duracao 294: cursor pedido 600 -> %lld\n", static_cast<long long>(p.current().value));
    AUREA_CHECK_EQ(p.current().value, static_cast<i64>(600));

    p.begin_scrub(2000);
    p.scrub(FrameIndex{400}, 3000);
    AUREA_CHECK_EQ(p.current().value, static_cast<i64>(400));
    p.end_scrub(4000);

    // O passo também anda para depois do fim…
    p.seek(FrameIndex{292}, 5000);
    p.step(10, 6000);
    AUREA_CHECK_EQ(p.current().value, static_cast<i64>(302));

    // …e o cursor nunca vai para antes do zero.
    p.seek(FrameIndex{-50}, 7000);
    AUREA_CHECK_EQ(p.current().value, static_cast<i64>(0));

    // TOCAR continua parando no fim da composição: é o fim do conteúdo.
    p.seek(FrameIndex{290}, 8000);
    p.play(8000);
    const FrameIndex fim = p.update(8000 + static_cast<u64>(tick_at(FrameIndex{10}, 30.0).value));
    std::printf("    tocando alem do fim: parou em %lld (ultimo quadro 293)\n", static_cast<long long>(fim.value));
    AUREA_CHECK_EQ(fim.value, static_cast<i64>(293));
}

// -----------------------------------------------------------------------------
// Remapear tempo como EFEITO
// -----------------------------------------------------------------------------

AUREA_TEST(Timeline, TimeRemapIsAnEffectOverTheLayerCurve) {
    // "Remapear tempo" entra no navegador de efeitos, ao lado do Posterizar
    // tempo, mas NÃO guarda um segundo remapeamento: o parâmetro "Tempo" É a
    // curva da camada (a mesma que o gráfico do painel de velocidade edita).
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());

    std::vector<u8> px(64 * 64 * 4, 255);
    const Result<u64> vid = e.import_image(px.data(), 64, 64, "quadro.png", nullptr);
    AUREA_CHECK(vid.ok());
    if (!vid.ok()) return;
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*vid));
    l->end = FrameIndex{300};

    AUREA_CHECK(!l->timeRemapEnabled);
    AUREA_CHECK(l->timeRemap.keys.empty());

    // Pôr o efeito liga a curva (a rampa equivalente ao tempo de agora).
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*vid);
    add.effect_add.effectType = effect_type_id(effect_keys::kTimeRemap);
    AUREA_CHECK(e.apply_command(add).ok());
    AUREA_CHECK(l->timeRemapEnabled);
    AUREA_CHECK(l->timeRemap.keys.size() == 2);
    const u32 effectId = l->effects.back().id;

    // "Tempo" em segundos da fonte: gravar no cabeçote grava na curva.
    Command seg;
    seg.type = CommandType::PlaybackSeek;
    seg.seek.time = tick_at(FrameIndex{60}, 30.0);
    AUREA_CHECK(e.apply_command(seg).ok());

    Command set;
    set.type = CommandType::EffectSetParam;
    set.effect_param.layer = LayerId::unpack(*vid);
    set.effect_param.effect = EffectId{effectId, 0};
    set.effect_param.paramIndex = 0;
    set.effect_param.value = 1.0f;   // 1 segundo da fonte = 30 quadros
    AUREA_CHECK(e.apply_command(set).ok());
    const i64 local60 = 60;
    const u32 at = l->timeRemap.find_exact(FrameIndex{local60});
    AUREA_CHECK(at != kInvalidIndex);
    std::printf("\n    curva: %zu chaves; no quadro 60 a fonte esta em %.1f quadros (pedido 30)\n",
                l->timeRemap.keys.size(), static_cast<double>(l->timeRemap.keys[at].value));
    AUREA_CHECK(std::fabs(l->timeRemap.keys[at].value - 30.0f) < 0.01f);

    // Ler o parâmetro devolve o MESMO valor, em segundos, e marcado animado.
    bridge::EffectParamRow rows[8]{};
    char blob[512]{};
    const u32 n = e.query_effect_params(*vid, effectId, rows, 8, blob, sizeof(blob));
    AUREA_CHECK(n >= 2);
    AUREA_CHECK(rows[0].index == 0);
    std::printf("    parametro Tempo no quadro 60: %.2f s (animado %u); interpolacao %.0f\n",
                static_cast<double>(rows[0].value[0]), rows[0].animated, static_cast<double>(rows[1].value[0]));
    AUREA_CHECK(std::fabs(rows[0].value[0] - 1.0f) < 0.02f);
    AUREA_CHECK(rows[0].animated == 1u);

    // Segurar o quadro: modo 2 do AE vira Hold na chave do cabeçote.
    Command hold;
    hold.type = CommandType::EffectSetParam;
    hold.effect_param.layer = LayerId::unpack(*vid);
    hold.effect_param.effect = EffectId{effectId, 0};
    hold.effect_param.paramIndex = 1;
    hold.effect_param.value = 2.0f;
    AUREA_CHECK(e.apply_command(hold).ok());
    AUREA_CHECK(l->timeRemap.keys[l->timeRemap.find_exact(FrameIndex{local60})].interp == Interpolation::Hold);
    AUREA_CHECK(e.query_effect_params(*vid, effectId, rows, 8, blob, sizeof(blob)) >= 2);
    AUREA_CHECK(std::fabs(rows[1].value[0] - 2.0f) < 0.01f);

    // Salvar e reabrir: a curva volta com o efeito.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_remap_efeito.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    Composition* comp2 = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* l2 = comp2->layer(LayerId::unpack(*vid));
    AUREA_CHECK(l2 != nullptr);
    if (l2) {
        AUREA_CHECK(l2->timeRemapEnabled);
        AUREA_CHECK(l2->timeRemap.find_exact(FrameIndex{local60}) != kInvalidIndex);
        AUREA_CHECK(std::fabs(l2->timeRemap.keys[l2->timeRemap.find_exact(FrameIndex{local60})].value - 30.0f) < 0.01f);
    }

    // Tirar o efeito desliga a curva — mas a curva FICA guardada.
    Command del;
    del.type = CommandType::EffectRemove;
    del.effect_ref.layer = LayerId::unpack(*vid);
    del.effect_ref.effect = EffectId{effectId, 0};
    AUREA_CHECK(e.apply_command(del).ok());
    AUREA_CHECK(l2 && !l2->timeRemapEnabled);
    AUREA_CHECK(l2 && !l2->timeRemap.keys.empty());
    std::remove(path.c_str());
}
