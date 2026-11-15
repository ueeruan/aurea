// =============================================================================
//  Lote X do AE (2026-11-12): os assistentes que criam camadas.
//  1 · Convert Audio to Keyframes (`Engine::audio_to_keyframes`).
//  O AE é só a especificação de comportamento.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;

namespace {

/// Senoide de amplitude 0,5 nos dois canais (RMS 0,3536 → valor 35,36).
struct ToneFactory final : VideoSourceFactory {
    test::SyntheticFactory source;
    explicit ToneFactory(f64 seconds) : source([&] {
        test::SyntheticConfig c;
        c.audioRate = 48000;
        c.audioSeconds = seconds;
        return c;
    }()) {}
    bool probe(const char* path, MediaProbe& out) override { return source.probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override {
        return source.open_video(asset, priority);
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char* path) override { return source.open_audio(path); }
};

struct AssistRig {
    ToneFactory factory;
    Engine e;
    u64 audio = 0;
    explicit AssistRig(f64 seconds = 2.0, bool withAudio = true) : factory(seconds) {
        EngineConfig config;
        config.workerCount = 1;
        config.memoryBudgetBytes = 64ull << 20;
        config.disableAutosave = true;
        config.mediaFactory = &factory;
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.new_project(64, 36, 30, nullptr).ok());
        if (withAudio) {
            VideoImport request;
            request.sourcePath = "synthetic-tone";
            const auto imported = e.import_audio(request);
            AUREA_CHECK(imported.ok());
            if (imported.ok()) audio = *imported;
        }
    }
    ~AssistRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    bool undo() {
        Command u;
        u.type = CommandType::Undo;
        return e.apply_command(u).ok();
    }
    /// A trilha do controle deslizante `index` (0 esquerdo, 1 direito, 2 ambos).
    const Track* slider(u64 layer, u32 index) {
        const Layer* l = L(layer);
        if (!l || index >= l->effects.size()) return nullptr;
        return l->tracks.find(TrackProperty::EffectParam, l->effects[index].id, param_track_key(0, 0));
    }
};

constexpr f32 kToneRms = 35.3553f;   // 0,5 / sqrt(2) × 100

} // namespace

AUREA_TEST(AeAssist, AudioToKeyframesMakesANullWithThreeSlidersAndOneKeyPerFrame) {
    AssistRig r;
    const i64 duration = r.comp()->duration().value;
    AUREA_CHECK(duration >= 30);
    const u32 depth = r.e.history().depth();
    const u32 layersBefore = r.comp()->order().size();
    const auto made = r.e.audio_to_keyframes(0, "Audio Amplitude");
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const Layer* n = r.L(*made);
    AUREA_CHECK(n != nullptr);
    if (!n) return;
    AUREA_CHECK(n->kind == LayerKind::Null);
    AUREA_CHECK(n->name == "Audio Amplitude");
    AUREA_CHECK_EQ(n->start.value, 0);
    AUREA_CHECK_EQ(n->end.value, duration);
    AUREA_CHECK_EQ(n->effects.size(), 3u);
    const EffectTypeId slider = effect_type_id(effect_keys::kSliderControl);
    for (const EffectInstance& fx : n->effects) AUREA_CHECK_EQ(fx.type, slider);
    // Um passo de desfazer, uma camada a mais.
    AUREA_CHECK_EQ(r.e.history().depth(), depth + 1);
    AUREA_CHECK_EQ(r.comp()->order().size(), layersBefore + 1);
    // Um keyframe por quadro, e o valor é o RMS × 100 da senoide de 0,5.
    for (u32 c = 0; c < 3; ++c) {
        const Track* t = r.slider(*made, c);
        AUREA_CHECK(t != nullptr);
        if (!t) return;
        AUREA_CHECK_EQ(static_cast<i64>(t->keys.size()), duration);
        for (usize i = 0; i < t->keys.size(); ++i) AUREA_CHECK_EQ(t->keys[i].time.value, static_cast<i64>(i));
        // Longe das bordas (rampa de entrada/saída do mixer): o tom inteiro.
        for (i64 f = 3; f < std::min<i64>(duration, 55) - 3; ++f) AUREA_CHECK_NEAR(t->keys[static_cast<usize>(f)].value, kToneRms, 0.6f);
        AUREA_CHECK_NEAR(t->sample(FrameIndex{10}), kToneRms, 0.6f);
    }
    // Desfazer tira o nulo de uma vez; o som continua lá.
    AUREA_CHECK(r.undo());
    AUREA_CHECK(r.L(*made) == nullptr);
    AUREA_CHECK_EQ(r.comp()->order().size(), layersBefore);
    AUREA_CHECK(r.L(r.audio) != nullptr);
    std::printf("nulo + 3 controles, %lld keys por trilha, RMS do tom = %.2f; ", static_cast<long long>(duration), kToneRms);
}

AUREA_TEST(AeAssist, AudioToKeyframesSeparatesLeftRightAndBoth) {
    AssistRig r;
    r.L(r.audio)->pan = 0.5f;   // o canal esquerdo cai à metade
    const auto made = r.e.audio_to_keyframes(0);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    AUREA_CHECK(r.L(*made)->name == "Amplitude de áudio");
    const Track* l = r.slider(*made, 0);
    const Track* rr = r.slider(*made, 1);
    const Track* b = r.slider(*made, 2);
    AUREA_CHECK(l && rr && b);
    if (!l || !rr || !b) return;
    const f32 lv = l->keys[12].value, rv = rr->keys[12].value, bv = b->keys[12].value;
    AUREA_CHECK_NEAR(lv, kToneRms * 0.5f, 0.6f);
    AUREA_CHECK_NEAR(rv, kToneRms, 0.6f);
    AUREA_CHECK_NEAR(bv, std::sqrt((lv * lv + rv * rv) * 0.5f), 0.05f);
    // Um segundo nulo ganha número, não o mesmo nome.
    const auto again = r.e.audio_to_keyframes(0);
    AUREA_CHECK(again.ok());
    if (again.ok()) AUREA_CHECK(r.L(*again)->name == "Amplitude de áudio 2");
    std::printf("E %.2f  D %.2f  ambos %.2f; ", lv, rv, bv);
}

AUREA_TEST(AeAssist, AudioToKeyframesFollowsTheWorkAreaAndGoesSilentAfterTheSound) {
    AssistRig r(1.0);
    // A composição passa do fim do som: depois dele, zero.
    r.comp()->set_duration(FrameIndex{90});
    r.L(r.audio)->end = FrameIndex{30};
    r.comp()->set_work_area(20, 50);
    const auto made = r.e.audio_to_keyframes(0);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const Layer* n = r.L(*made);
    AUREA_CHECK_EQ(n->start.value, 20);
    AUREA_CHECK_EQ(n->end.value, 50);
    const Track* both = r.slider(*made, 2);
    AUREA_CHECK(both != nullptr);
    if (!both) return;
    AUREA_CHECK_EQ(both->keys.size(), 30u);
    // Tempo da camada: o key 0 é o quadro 20 da composição (com som), o 15 é o 35 (sem).
    AUREA_CHECK_NEAR(both->keys[2].value, kToneRms, 0.6f);
    AUREA_CHECK_NEAR(both->keys[15].value, 0.0f, 1e-3f);
    AUREA_CHECK_NEAR(both->keys[29].value, 0.0f, 1e-3f);
    std::printf("area de trabalho 20..50 -> 30 keys, silencio depois do som; ");
}

AUREA_TEST(AeAssist, AudioToKeyframesOfOneLayerIgnoresTheOthersAndRefusesSilentOnes) {
    AssistRig r;
    VideoImport request;
    request.sourcePath = "synthetic-tone";
    const auto second = r.e.import_audio(request);
    AUREA_CHECK(second.ok());
    if (!second.ok()) return;
    r.L(*second)->gain = 0.5f;
    // Só a segunda camada (metade do volume), mesmo com a primeira tocando junto.
    const auto made = r.e.audio_to_keyframes(*second);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const Track* both = r.slider(*made, 2);
    AUREA_CHECK(both != nullptr);
    if (!both) return;
    AUREA_CHECK_NEAR(both->keys[10].value, kToneRms * 0.5f, 0.6f);
    // Uma camada sem som recusa e não deixa nada no histórico.
    const auto shape = r.e.add_shape(0);
    AUREA_CHECK(shape.ok());
    const u32 depth = r.e.history().depth();
    const u32 layers = r.comp()->order().size();
    const auto refused = r.e.audio_to_keyframes(shape.ok() ? *shape : 0);
    AUREA_CHECK(!refused.ok());
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
    AUREA_CHECK_EQ(r.comp()->order().size(), layers);
    AUREA_CHECK(!r.e.audio_to_keyframes(0xdeadbeefull).ok());
    std::printf("camada sozinha a 50%% = %.2f; camada muda recusada; ", both->keys[10].value);
}

AUREA_TEST(AeAssist, AudioToKeyframesRefusesACompositionWithoutSound) {
    AssistRig r(2.0, false);
    AUREA_CHECK(r.e.add_shape(0).ok());
    const u32 depth = r.e.history().depth();
    const auto refused = r.e.audio_to_keyframes(0);
    AUREA_CHECK(!refused.ok() && refused.status().code() == Errc::InvalidArgument);
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
    // Mudo na única camada com som: também não há o que converter.
    AssistRig muted;
    muted.L(muted.audio)->muted = true;
    AUREA_CHECK(!muted.e.audio_to_keyframes(0).ok());
}
