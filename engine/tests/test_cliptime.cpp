// =============================================================================
//  Tempo do clipe: velocidade, reverso, congelar quadro — uma só função de
//  tempo da fonte (Layer::source_frame) para vídeo, miniatura e áudio.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/audio/Audio.hpp"

#include <cmath>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

struct TimeRig {
    SyntheticFactory factory;
    Engine e;
    LayerId layer{};
    explicit TimeRig(SyntheticConfig cfg) : factory(cfg) {
        EngineConfig ec;
        ec.workerCount = 2;
        ec.memoryBudgetBytes = 64ull << 20;
        ec.disableAutosave = true;
        ec.mediaFactory = &factory;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr).ok());
        VideoImport vi;
        vi.sourcePath = "s";
        vi.displayName = "clipe";
        auto id = e.import_video(vi);
        AUREA_CHECK(id.ok());
        if (id.ok()) layer = LayerId::unpack(*id);
    }
    ~TimeRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L() { return comp()->layer(layer); }
    void speed(f32 s) {
        Command c;
        c.type = CommandType::LayerSetSpeed;
        c.audio_gain.layer = layer;
        c.audio_gain.gain = s;
        AUREA_CHECK(e.apply_command(c).ok());
    }
    void reverse(bool r) {
        Command c;
        c.type = CommandType::LayerSetReversed;
        c.audio_flag.layer = layer;
        c.audio_flag.flag = r;
        AUREA_CHECK(e.apply_command(c).ok());
    }
};

SyntheticConfig cfg_with_audio() {
    SyntheticConfig c;
    c.frameCount = 300;       // 10 s a 30 fps
    c.audioRate = 48000;
    c.audioChannels = 2;
    c.audioSeconds = 10.0;
    return c;
}

class Fetch final : public audio::BlockSource {
public:
    explicit Fetch(audio::AudioBlockCache& c) : c_(c) {}
    const audio::AudioBlock* block(u64 a, i64 b) override {
        auto p = c_.fetch(a, b);
        if (!p) return nullptr;
        held_.push_back(p);
        return p.get();
    }
private:
    audio::AudioBlockCache& c_;
    std::vector<std::shared_ptr<const audio::AudioBlock>> held_;
};

} // namespace

AUREA_TEST(ClipTime, SpeedKeepsTheSourceSpanAndDrivesVideoAndAudio) {
    TimeRig r(cfg_with_audio());
    AUREA_CHECK_EQ(r.L()->end.value, 300);
    r.speed(2.0f);
    AUREA_CHECK_EQ(r.L()->end.value, 150);                  // metade do tempo
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{10}), 20.0, 1e-9);
    r.speed(0.5f);
    AUREA_CHECK_EQ(r.L()->end.value, 600);                  // câmera lenta: o dobro
    AUREA_CHECK(r.comp()->duration().value >= 600);         // a composição acompanha

    // Áudio a 2×: na amostra 0,5 s da timeline toca 1,0 s da fonte.
    r.speed(2.0f);
    auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(1));
    if (snap->clips.empty()) return;
    AUREA_CHECK_NEAR(snap->clips[0].rate, 2.0, 1e-9);
    audio::AudioBlockCache cache(&r.factory, 16ull << 20, false);
    cache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * 48000});
    Fetch f(cache);
    std::vector<f32> out(2);
    audio::mix(*snap, 24000, 1, f, out.data());
    AUREA_CHECK_NEAR(out[0], synthetic_audio_value(cfg_with_audio(), 0, 1.0), 1e-5);
}

AUREA_TEST(ClipTime, ReverseAndSplitRespectTheSourceTime) {
    TimeRig r(cfg_with_audio());
    r.reverse(true);
    // Reverso: o primeiro quadro mostra o último do trecho.
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{0}), 299.0, 1e-9);
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{299}), 0.0, 1e-9);
    // O áudio toca ao contrário (taxa −1) a partir do fim.
    auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
    if (!snap->clips.empty()) AUREA_CHECK_NEAR(snap->clips[0].rate, -1.0, 1e-9);

    // Corte de um clipe a 2× (não reverso): a segunda metade começa na fonte onde a primeira parou.
    TimeRig s(cfg_with_audio());
    s.speed(2.0f);
    Command c;
    c.type = CommandType::LayerSplit;
    c.layer_split.layer = s.layer;
    c.layer_split.at = FrameIndex{50};
    AUREA_CHECK(s.e.apply_command(c).ok());
    const Layer* second = nullptr;
    s.comp()->layers().for_each([&](LayerId id, const Layer& l) { if (id != s.layer) second = &l; });
    AUREA_CHECK(second != nullptr);
    if (second) {
        AUREA_CHECK_EQ(second->offset.value, 100);
        AUREA_CHECK_NEAR(second->source_frame(FrameIndex{50}), s.L()->source_frame(FrameIndex{49}) + 2.0, 1e-9);
    }
}

AUREA_TEST(ClipTime, FreezeFrameHoldsTheExactSourceFrameAndPushesTheRest) {
    TimeRig r(cfg_with_audio());
    r.speed(2.0f);                       // 150 frames na timeline
    const f64 srcAt60 = r.L()->source_frame(FrameIndex{60});
    auto hold = r.e.freeze_frame(r.layer.pack(), 60, 90);
    AUREA_CHECK(hold.ok());
    if (!hold.ok()) return;
    const Layer* h = r.comp()->layer(LayerId::unpack(*hold));
    AUREA_CHECK(h && h->speed == 0.0f && h->start.value == 60 && h->end.value == 150);
    if (h) {
        AUREA_CHECK_NEAR(h->source_frame(FrameIndex{60}), srcAt60, 1e-9);
        AUREA_CHECK_NEAR(h->source_frame(FrameIndex{149}), srcAt60, 1e-9);   // parado
    }
    // A primeira parte termina no cabeçote; o resto andou 90 quadros e segue de onde parou.
    AUREA_CHECK_EQ(r.L()->end.value, 60);
    const Layer* after = nullptr;
    r.comp()->layers().for_each([&](LayerId id, const Layer& l) {
        if (id != r.layer && id != LayerId::unpack(*hold)) after = &l;
    });
    AUREA_CHECK(after != nullptr);
    if (after) {
        AUREA_CHECK_EQ(after->start.value, 150);
        AUREA_CHECK_NEAR(after->source_frame(FrameIndex{150}), srcAt60, 1e-9);
    }
    // Quadro congelado não tem som.
    auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
    for (auto& c : snap->clips) AUREA_CHECK(!(c.start >= audio::frame_to_sample(60, 30.0) && c.end <= audio::frame_to_sample(150, 30.0)));
    // Um passo de desfazer volta tudo.
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(u).ok());
    AUREA_CHECK_EQ(r.comp()->layers().count(), 1u);
}

AUREA_TEST(ClipTime, SpeedAndReverseSurviveSaveAndReopen) {
    {
        TimeRig r(cfg_with_audio());
        r.speed(0.25f);
        r.reverse(true);
        AUREA_CHECK(r.e.save_project("aurea_teste_tempo.aurea").ok());
    }
    TimeRig r2(cfg_with_audio());
    AUREA_CHECK(r2.e.load_project("aurea_teste_tempo.aurea").ok());
    const Layer* l = nullptr;
    r2.comp()->layers().for_each([&](LayerId, const Layer& x) { l = &x; });
    AUREA_CHECK(l && std::fabs(l->speed - 0.25f) < 1e-6f && l->reversed && l->end.value == 1200);
}
