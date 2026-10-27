// =============================================================================
//  Omega 0.3.2, área 03 (timeline): alças de velocidade (P1-8).
//  Esticar a camada muda a velocidade e mantém o MESMO trecho da fonte; os
//  keyframes andam junto no tempo local; um passo de desfazer volta tudo.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <cmath>

using namespace aurea;

namespace {

struct StretchRig {
    Engine e;
    u64 a = 0, b = 0;
    StretchRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        a = *e.add_null(false);
        b = *e.add_null(false);
        range(a, 0, 60);
        range(b, 60, 90);
        // A camada `a` vira um clipe de vídeo: fonte a partir do quadro 60, 1×,
        // com um keyframe de posição no começo e outro no fim do trecho.
        Asset asset;
        asset.kind = AssetKind::Video;
        asset.duration = FrameIndex{600};
        asset.timebaseFps = 30;
        const AssetId source = e.project()->add_asset(std::move(asset));
        Layer* l = L(a);
        l->kind = LayerKind::Video;
        l->source = source;
        l->speed = 1.0f;
        l->offset = FrameIndex{60};
        auto& x = l->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{60}, 0.0f);
        x.set(FrameIndex{120}, 500.0f);
    }
    ~StretchRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void range(u64 id, i64 s, i64 en) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        cmd.layer_range.layer = LayerId::unpack(id);
        cmd.layer_range.start = FrameIndex{s};
        cmd.layer_range.end = FrameIndex{en};
        cmd.layer_range.offset = FrameIndex{0};
        cmd.layer_range.setOffset = false;
        AUREA_CHECK(e.apply_command(cmd).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    i64 key(u64 id, usize index) {
        const Track* t = L(id)->tracks.find(TrackProperty::PositionX);
        return t && index < t->keys.size() ? t->keys[index].time.value : -1;
    }
};

} // namespace

AUREA_TEST(OmegaTimeline, StretchingTheEndHalvesTheSpeedAndKeepsTheSource) {
    StretchRig r;
    const f64 lastSource = r.L(r.a)->source_frame(FrameIndex{60});   // a fonte no fim do trecho
    AUREA_CHECK(r.e.stretch_layer_span(r.a, 0, 120));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(0));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(120));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 0.5f, 1e-6f);
    // O mesmo trecho da fonte: começa no 60 e chega ao mesmo ponto no fim.
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{0}), 60.0, 1e-9);
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{120}), lastSource, 1e-9);
    // Os keyframes andam no tempo local, a partir do ponto de entrada.
    AUREA_CHECK_EQ(r.key(r.a, 0), static_cast<i64>(60));
    AUREA_CHECK_EQ(r.key(r.a, 1), static_cast<i64>(180));
    // Um passo de desfazer volta tudo.
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(60));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 1.0f, 1e-6f);
    AUREA_CHECK_EQ(r.key(r.a, 1), static_cast<i64>(120));
}

AUREA_TEST(OmegaTimeline, StretchingTheStartKeepsTheEnd) {
    StretchRig r;
    r.range(r.a, 30, 90);
    r.range(r.b, 90, 120);
    AUREA_CHECK(r.e.stretch_layer_span(r.a, 0, 90));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(0));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(90));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 60.0f / 90.0f, 1e-5f);
}

AUREA_TEST(OmegaTimeline, SpeedStaysInsideTheRangeAndTheDurationStopsAtTheLimit) {
    StretchRig r;
    // 60 quadros em 1 pediriam 60×: para em 16× (a duração para no limite).
    AUREA_CHECK(r.e.stretch_layer_span(r.a, 0, 1));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(4));
    AUREA_CHECK(r.L(r.a)->speed <= 16.0f + 1e-4f);
    AUREA_CHECK(r.L(r.a)->speed >= 14.9f);
}

AUREA_TEST(OmegaTimeline, LockedLayerIsRefused) {
    StretchRig r;
    r.L(r.a)->locked = true;
    AUREA_CHECK(!r.e.stretch_layer_span(r.a, 0, 120));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(60));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 1.0f, 1e-6f);
}

AUREA_TEST(OmegaTimeline, MagneticLineRipplesTheNeighbours) {
    StretchRig r;
    r.L(r.a)->magneticTrack = true;
    r.L(r.a)->trackId = 7;
    r.L(r.b)->magneticTrack = true;
    r.L(r.b)->trackId = 7;
    AUREA_CHECK(r.e.stretch_layer_span(r.a, 0, 120));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(120));
    AUREA_CHECK_EQ(r.L(r.b)->start.value, static_cast<i64>(120));
    AUREA_CHECK_EQ(r.L(r.b)->end.value, static_cast<i64>(150));
    // Linha magnética: o começo fica, mesmo puxando a alça de início.
    AUREA_CHECK(r.e.stretch_layer_span(r.a, 60, 120));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(0));
}

AUREA_TEST(OmegaTimeline, KeyedSpeedKeepsItsIntegral) {
    StretchRig r;
    // Velocidade animada constante em 2×: dobrar a duração dá 1× e o tempo das chaves dobra.
    auto& speed = r.L(r.a)->tracks.get_or_create(TrackProperty::Speed);
    speed.set(FrameIndex{60}, 2.0f);
    speed.set(FrameIndex{120}, 2.0f);
    const f64 before = r.L(r.a)->source_frame(FrameIndex{60});
    AUREA_CHECK(r.e.stretch_layer_span(r.a, 0, 120));
    const Track* t = r.L(r.a)->tracks.find(TrackProperty::Speed);
    AUREA_CHECK(t != nullptr);
    if (!t || t->keys.size() < 2) return;
    AUREA_CHECK_NEAR(t->keys[0].value, 1.0f, 1e-6f);
    AUREA_CHECK_EQ(t->keys[1].time.value, static_cast<i64>(180));
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{120}), before, 1e-6);
}

// =============================================================================
//  0.3.6, área keys: keyframe no mesmo quadro e corte/aparar "no cabeçote".
// =============================================================================
namespace {

void seek036(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}

Status key036(Engine& e, u64 layer, TrackProperty p, i64 local, f32 value, u32 flags = 0) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe.track.layer = LayerId::unpack(layer);
    c.keyframe.track.property = p;
    c.keyframe.track.effectIndex = kInvalidIndex;
    c.keyframe.track.effectParamIndex = 0;
    c.keyframe.time = FrameIndex{local};
    c.keyframe.value = value;
    c.keyframe.onlyIfChanged = flags;
    return e.apply_command(c);
}

usize keyCount036(Layer* l, TrackProperty p) {
    const Track* t = l ? l->tracks.find(p) : nullptr;
    return t ? t->keys.size() : 0;
}

} // namespace

AUREA_TEST(OmegaTimeline, KeysAddingTheSameFrameTwiceKeepsOneKeyPerTrack) {
    StretchRig r;
    seek036(r.e, 20);                       // local = 20 − 0 + 60 = 80
    // ◆ do Mover: X e Y no quadro do cabeçote (uma chave por trilha).
    AUREA_CHECK(key036(r.e, r.a, TrackProperty::PositionX, 80, 10.0f).ok());
    AUREA_CHECK(key036(r.e, r.a, TrackProperty::PositionY, 80, 20.0f).ok());
    AUREA_CHECK_EQ(keyCount036(r.L(r.a), TrackProperty::PositionX), usize{3});   // 60, 80, 120
    AUREA_CHECK_EQ(keyCount036(r.L(r.a), TrackProperty::PositionY), usize{1});
    // O mesmo toque de novo e o gesto com Auto-Key no quadro que a prévia mostra
    // (o mesmo 80) REGRAVAM a chave: nada de uma segunda no mesmo instante.
    AUREA_CHECK(key036(r.e, r.a, TrackProperty::PositionX, 80, 10.0f).ok());
    AUREA_CHECK(key036(r.e, r.a, TrackProperty::PositionX, 0, 15.0f, kAutoKeyOnlyIfChanged | kAutoKeyAtPlayhead).ok());
    AUREA_CHECK_EQ(keyCount036(r.L(r.a), TrackProperty::PositionX), usize{3});
    const Track* x = r.L(r.a)->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(x != nullptr);
    if (!x) return;
    const u32 at = x->find_exact(FrameIndex{80});
    AUREA_CHECK(at != kInvalidIndex);
    if (at != kInvalidIndex) AUREA_CHECK_NEAR(x->keys[at].value, 15.0f, 1e-6f);
    // As chaves seguem em ordem e sem tempo repetido.
    for (usize i = 1; i < x->keys.size(); ++i) AUREA_CHECK(x->keys[i - 1].time.value < x->keys[i].time.value);
}

AUREA_TEST(OmegaTimeline, KeysSplitAtPlayheadUsesTheShownFrame) {
    StretchRig r;
    const f64 at25 = r.L(r.a)->source_frame(FrameIndex{25});
    const f64 at24 = r.L(r.a)->source_frame(FrameIndex{24});
    seek036(r.e, 25);
    Command split;
    split.type = CommandType::LayerSplit;
    split.layer_split.layer = LayerId::unpack(r.a);
    split.layer_split.at = FrameIndex{kEditAtPlayhead};
    AUREA_CHECK(r.e.apply_command(split).ok());
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(25));
    LayerId second;
    const AssetId source = r.L(r.a)->source;
    r.comp()->layers().for_each([&](LayerId id, const Layer& l) {
        if (id != LayerId::unpack(r.a) && l.source == source && l.start.value == 25) second = id;
    });
    AUREA_CHECK(second.valid());
    if (!second.valid()) return;
    // Os dois lados mostram a MESMA fonte de antes, quadro a quadro, no corte.
    AUREA_CHECK_NEAR(r.comp()->layer(second)->source_frame(FrameIndex{25}), at25, 1e-6);
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{24}), at24, 1e-6);
    AUREA_CHECK_EQ(r.comp()->layer(second)->end.value, static_cast<i64>(60));
    // Cabeçote fora do clipe: o corte "no cabeçote" é recusado (nada muda).
    seek036(r.e, 40);
    AUREA_CHECK(!r.e.apply_command(split).ok());
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(25));
}

AUREA_TEST(OmegaTimeline, KeysTrimAtPlayheadKeepsTheSourceWithSpeedAndOffset) {
    StretchRig r;
    r.L(r.a)->speed = 2.0f;                 // fonte = 60 + 2·t (deslocamento 60, 2×)
    const f64 before = r.L(r.a)->source_frame(FrameIndex{30});
    seek036(r.e, 10);
    AUREA_CHECK(r.e.edit_clip_time(r.a, 0, kEditAtPlayhead));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(10));
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{30}), before, 1e-3);
    seek036(r.e, 40);
    AUREA_CHECK(r.e.edit_clip_time(r.a, 1, kEditAtPlayhead));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(40));
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{30}), before, 1e-3);
    // Fora do clipe (antes do começo): recusado, sem virar um clipe de 1 quadro.
    seek036(r.e, 5);
    AUREA_CHECK(!r.e.edit_clip_time(r.a, 0, kEditAtPlayhead));
    AUREA_CHECK(!r.e.edit_clip_time(r.a, 1, kEditAtPlayhead));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(10));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(40));
}
