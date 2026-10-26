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
