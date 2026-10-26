// =============================================================================
//  Omega 0.3.2, área 04 (painel da camada): fileira de tempo contextual (P0-3),
//  retempo e velocidade em todos os tipos (P1-7), o botão Áudio (P0-2).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <cmath>

using namespace aurea;

namespace {

struct PanelRig {
    Engine e;
    u64 a = 0, b = 0;
    PanelRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        a = *e.add_null(false);
        b = *e.add_null(false);
        range(a, 30, 90);
        range(b, 200, 260);
        // `a` anima a posição do quadro local 0 ao 60 (começo e fim do trecho).
        auto& x = L(a)->tracks.get_or_create(TrackProperty::PositionX);
        x.set(FrameIndex{0}, 0.0f);
        x.set(FrameIndex{30}, 250.0f);
        x.set(FrameIndex{60}, 500.0f);
    }
    ~PanelRig() { e.shutdown(); }
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
    /// Vira um clipe de vídeo de 600 quadros, fonte a partir do quadro 60.
    void make_video(u64 id) {
        Asset asset;
        asset.kind = AssetKind::Video;
        asset.duration = FrameIndex{600};
        asset.timebaseFps = 30;
        const AssetId source = e.project()->add_asset(std::move(asset));
        Layer* l = L(id);
        l->kind = LayerKind::Video;
        l->source = source;
        l->speed = 1.0f;
        l->offset = FrameIndex{60};
    }
};

} // namespace

AUREA_TEST(OmegaLayerPanel, TimeRowFollowsThePlayhead) {
    PanelRig r;
    // Antes do início: par Mover/Expandir (e o retempo de esticar o início).
    const u32 before = r.e.query_layer_panel(r.a, 10);
    AUREA_CHECK(before & Engine::kLayerPanelExtendStart);
    AUREA_CHECK(before & Engine::kLayerPanelMoveStart);
    AUREA_CHECK(before & Engine::kLayerPanelRetimeExtendStart);
    AUREA_CHECK(!(before & Engine::kLayerPanelSplit));
    // Dentro: o trio e o retempo de encurtar as duas pontas.
    const u32 inside = r.e.query_layer_panel(r.a, 60);
    AUREA_CHECK_EQ(inside & 7u, 7u);
    AUREA_CHECK(inside & Engine::kLayerPanelRetimeShortenStart);
    AUREA_CHECK(inside & Engine::kLayerPanelRetimeShortenEnd);
    AUREA_CHECK(!(inside & Engine::kLayerPanelMoveStart));
    // Na borda o trio continua, sem o retempo de dentro.
    const u32 edge = r.e.query_layer_panel(r.a, 30);
    AUREA_CHECK_EQ(edge & 7u, 7u);
    AUREA_CHECK(!(edge & Engine::kLayerPanelRetimeShortenStart));
    // Depois do fim: o par do outro lado.
    const u32 after = r.e.query_layer_panel(r.a, 120);
    AUREA_CHECK(after & Engine::kLayerPanelExtendEnd);
    AUREA_CHECK(after & Engine::kLayerPanelMoveEnd);
    AUREA_CHECK(after & Engine::kLayerPanelRetimeExtendEnd);
    // Os cinco bits de baixo são os mesmos de `query_clip_time_actions`.
    for (const i64 f : {i64{0}, i64{30}, i64{60}, i64{90}, i64{150}})
        AUREA_CHECK_EQ(r.e.query_layer_panel(r.a, f) & 31u, r.e.query_clip_time_actions(r.a, f));
}

AUREA_TEST(OmegaLayerPanel, LockedLayerLosesMoveAndRetime) {
    PanelRig r;
    r.L(r.a)->locked = true;
    const u32 bits = r.e.query_layer_panel(r.a, 10);
    AUREA_CHECK(bits & Engine::kLayerPanelLocked);
    AUREA_CHECK(!(bits & Engine::kLayerPanelMoveStart));
    AUREA_CHECK(!(bits & Engine::kLayerPanelRetimeExtendStart));
    AUREA_CHECK(!r.e.move_layer_to_playhead(r.a, 10, false));
    AUREA_CHECK(!r.e.retime_layer_to_playhead(r.a, 0, 10));
    AUREA_CHECK(!r.e.set_layer_speed_baked(r.a, 2.0f));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(30));
}

AUREA_TEST(OmegaLayerPanel, MoveToStartOrEndAtThePlayhead) {
    PanelRig r;
    // Cabeçote antes: a camada passa a COMEÇAR nele, duração e chaves juntas.
    AUREA_CHECK(r.e.move_layer_to_playhead(r.a, 10, false));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(10));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(70));
    AUREA_CHECK_EQ(r.key(r.a, 2), static_cast<i64>(60));
    // Cabeçote depois: a camada passa a TERMINAR nele (fim exclusivo).
    AUREA_CHECK(r.e.move_layer_to_playhead(r.a, 150, true));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(90));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(150));
    // Um passo de desfazer por toque.
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(10));
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(30));
    // Terminar antes do zero: para no zero.
    AUREA_CHECK(r.e.move_layer_to_playhead(r.a, 20, true));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(0));
}

AUREA_TEST(OmegaLayerPanel, RetimeKeepsTheSourceAndChangesTheSpeed) {
    PanelRig r;
    r.make_video(r.a);
    const f64 lastSource = r.L(r.a)->source_frame(FrameIndex{90});
    // 60 quadros esticados até o quadro 120 (o fim): 90 quadros = 0,666×.
    AUREA_CHECK(r.e.retime_layer_to_playhead(r.a, 3, 120));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(30));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(120));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 60.0f / 90.0f, 1e-5f);
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{30}), 60.0, 1e-9);
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{120}), lastSource, 1e-4); // velocidade em f32: ~2e-6 quadro de erro em 90 quadros
    // Encurtar o fim até o cabeçote de dentro.
    AUREA_CHECK(r.e.retime_layer_to_playhead(r.a, 2, 60));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(60));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 2.0f, 1e-4f);
    // Fora da regra: esticar o fim com o cabeçote dentro não faz nada.
    AUREA_CHECK(!r.e.retime_layer_to_playhead(r.a, 3, 40));
    AUREA_CHECK(!r.e.retime_layer_to_playhead(r.a, 0, 40));
}

AUREA_TEST(OmegaLayerPanel, SpeedOnALayerWithoutSourcePlaysItsAnimationFaster) {
    PanelRig r;
    // Um nulo a 2×: metade da duração e as chaves na metade do tempo local.
    AUREA_CHECK(r.e.set_layer_speed_baked(r.a, 2.0f));
    AUREA_CHECK_EQ(r.L(r.a)->start.value, static_cast<i64>(30));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(60));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 2.0f, 1e-6f);
    AUREA_CHECK_EQ(r.key(r.a, 0), static_cast<i64>(0));
    AUREA_CHECK_EQ(r.key(r.a, 1), static_cast<i64>(15));
    AUREA_CHECK_EQ(r.key(r.a, 2), static_cast<i64>(30));
    // O valor no fim do trecho é o mesmo de antes (a animação inteira cabe).
    const Track* t = r.L(r.a)->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(t != nullptr);
    if (t) AUREA_CHECK_NEAR(t->sample(FrameIndex{30}), 500.0f, 1e-3f);
    // Um passo de desfazer volta tudo.
    r.undo();
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(90));
    AUREA_CHECK_NEAR(r.L(r.a)->speed, 1.0f, 1e-6f);
    AUREA_CHECK_EQ(r.key(r.a, 2), static_cast<i64>(60));
}

AUREA_TEST(OmegaLayerPanel, ADragOfSpeedStepsDoesNotAccumulateRounding) {
    PanelRig r;
    // Um arrasto: muitos passos seguidos, sem outra edição no meio. Cada passo
    // parte da camada de antes do arrasto, então voltar a 1× devolve as chaves
    // exatamente onde estavam (e nenhuma se fundiu com a vizinha).
    for (f32 v = 1.0f; v <= 16.0f; v *= 1.37f) AUREA_CHECK(r.e.set_layer_speed_baked(r.a, v));
    AUREA_CHECK(r.e.set_layer_speed_baked(r.a, 16.0f));
    AUREA_CHECK_EQ(r.L(r.a)->end.value - r.L(r.a)->start.value, static_cast<i64>(4));
    AUREA_CHECK(r.e.set_layer_speed_baked(r.a, 0.7f));
    AUREA_CHECK(r.e.set_layer_speed_baked(r.a, 1.0f));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(90));
    AUREA_CHECK_EQ(r.key(r.a, 0), static_cast<i64>(0));
    AUREA_CHECK_EQ(r.key(r.a, 1), static_cast<i64>(30));
    AUREA_CHECK_EQ(r.key(r.a, 2), static_cast<i64>(60));
}

AUREA_TEST(OmegaLayerPanel, BakedSpeedOnVideoKeepsTheSourceRange) {
    PanelRig r;
    r.make_video(r.a);
    const f64 lastSource = r.L(r.a)->source_frame(FrameIndex{90});
    AUREA_CHECK(r.e.set_layer_speed_baked(r.a, 0.5f));
    AUREA_CHECK_EQ(r.L(r.a)->end.value, static_cast<i64>(150));
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{30}), 60.0, 1e-9);
    AUREA_CHECK_NEAR(r.L(r.a)->source_frame(FrameIndex{150}), lastSource, 1e-6);
    // Velocidade animada: recusa (o painel completo de velocidade cuida dela).
    auto& speed = r.L(r.a)->tracks.get_or_create(TrackProperty::Speed);
    speed.set(FrameIndex{60}, 1.0f);
    speed.set(FrameIndex{90}, 2.0f);
    AUREA_CHECK(!r.e.set_layer_speed_baked(r.a, 2.0f));
    AUREA_CHECK(!(r.e.query_layer_panel(r.a, 60) & Engine::kLayerPanelSpeedBakeable));
}

AUREA_TEST(OmegaLayerPanel, AudioButtonFollowsHasAnyAudio) {
    PanelRig r;
    // Nulo: sem áudio.
    AUREA_CHECK(!(r.e.query_layer_panel(r.a, 60) & Engine::kLayerPanelHasAudio));
    // Todo vídeo habilita o Áudio (a referência não olha a faixa de som).
    r.make_video(r.a);
    AUREA_CHECK(r.e.query_layer_panel(r.a, 60) & Engine::kLayerPanelHasAudio);
    // Imagem não.
    r.L(r.b)->kind = LayerKind::Image;
    AUREA_CHECK(!(r.e.query_layer_panel(r.b, 220) & Engine::kLayerPanelHasAudio));
    // Volume com mais de um keyframe: o bit próprio (o botão fica habilitado).
    auto& gain = r.L(r.b)->tracks.get_or_create(TrackProperty::AudioVolume);
    gain.set(FrameIndex{0}, 1.0f);
    gain.set(FrameIndex{10}, 0.5f);
    AUREA_CHECK(r.e.query_layer_panel(r.b, 220) & Engine::kLayerPanelGainKeys);
}
