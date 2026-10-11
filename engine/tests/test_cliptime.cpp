// =============================================================================
//  Tempo do clipe: velocidade, reverso, congelar quadro — uma só função de
//  tempo da fonte (Layer::source_frame) para vídeo, miniatura e áudio.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/render/Renderer.hpp"

#include <chrono>
#include <cmath>
#include <thread>
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
        // Animation time stays frame-aligned; the remap retains 2x source time.
        AUREA_CHECK_EQ(second->offset.value, 50);
        AUREA_CHECK(second->timeRemapEnabled);
        AUREA_CHECK_NEAR(second->source_frame(FrameIndex{50}), s.L()->source_frame(FrameIndex{49}) + 2.0, 1e-9);
    }
}

AUREA_TEST(ClipTime, TrimAndSlipKeepAudioOnTheVideoSourceClock) {
    for (bool reverse : {false, true}) {
        TimeRig r(cfg_with_audio()); r.speed(2); r.reverse(reverse);
        AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 0, 5));
        AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 1, 140));
        AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 2, reverse ? -3 : 3));
        auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
        AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(1));
        if (snap->clips.empty()) continue;
        audio::AudioBlockCache cache(&r.factory, 16ull << 20, false);
        cache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * 48000});
        Fetch f(cache); std::vector<f32> out(64);
        audio::mix(*snap, 20 * 1600, 32, f, out.data());
        for (u32 i = 0; i < 32; ++i) {
            const f64 seconds = r.L()->source_frame_f(20.0 + i / 1600.0) / 30.0;
            for (u32 channel = 0; channel < 2; ++channel)
                AUREA_CHECK_NEAR(out[i * 2 + channel], synthetic_audio_value(cfg_with_audio(), channel, seconds), 0.002);
        }
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


// =============================================================================
//  Marcas e batidas
// =============================================================================
AUREA_TEST(ClipTime, DetectBeatsPlacesMarkersOnTheTimelineGrid) {
    SyntheticConfig cfg = cfg_with_audio();
    cfg.audioBpm = 120.0;
    cfg.audioBeatStart = 0.5;
    TimeRig r(cfg);
    f64 bpm = 0.0;
    auto n = r.e.detect_beats(r.layer.pack(), &bpm);
    AUREA_CHECK(n.ok());
    std::vector<i64> m(3 * 64);
    const u32 total = r.e.query_markers(m.data(), 64);
    // 120 BPM a 30 fps = uma batida a cada 15 quadros, a 1ª no quadro 15.
    u32 off = 0;
    for (u32 i = 0; i < total && i < 64; ++i) {
        const i64 f = m[i * 3];
        const i64 near = 15 + ((f - 15 + 7) / 15) * 15;
        if (std::llabs(f - near) > 1) ++off;
        AUREA_CHECK_EQ(m[i * 3 + 2], static_cast<i64>(kMarkerBeat));
    }
    std::printf("    %.2f BPM, %u marcas, %u fora da grade\n", bpm, total, off);
    AUREA_CHECK(std::fabs(bpm - 120.0) < 1.0);
    AUREA_CHECK(total >= 18 && total <= 20);
    AUREA_CHECK_EQ(off, 0u);
    // Clipe 2x: as mesmas batidas caem na METADE do tempo da timeline.
    r.speed(2.0f);
    n = r.e.detect_beats(r.layer.pack(), &bpm);
    AUREA_CHECK(n.ok());
    const u32 total2 = r.e.query_markers(m.data(), 64);
    // Marcas de batida antigas fora do novo trecho (frames >= 150) continuam
    // (são da composição); as de dentro foram trocadas.
    u32 inside = 0, onGrid = 0;
    for (u32 i = 0; i < total2 && i < 64; ++i) {
        const i64 f = m[i * 3];
        if (f >= 150) continue;
        ++inside;
        const i64 rel = f - 7;   // 0,5 s de fonte a 2x = 7,5 quadros
        if (std::llabs(rel - ((rel + 3) / 7.5 > 0 ? static_cast<i64>(std::llround(rel / 7.5) * 7.5) : 0)) <= 1) ++onGrid;
    }
    std::printf("    a 2x: %u marcas no trecho, %u na grade de 7,5 quadros\n", inside, onGrid);
    AUREA_CHECK(inside >= 18 && inside <= 20);
    AUREA_CHECK(onGrid + 1 >= inside);
}

AUREA_TEST(ClipTime, DetectBeatsOn44kAudioLayer) {
    // Como no aparelho: camada de ÁUDIO (import_audio) de fonte a 44,1 kHz.
    SyntheticConfig cfg = cfg_with_audio();
    cfg.audioRate = 44100;
    cfg.audioBpm = 60.0;
    cfg.audioBeatStart = 0.25;
    cfg.audioSeconds = 6.0;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    ec.mediaFactory = &factory;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "som.wav";
    vi.displayName = "som";
    auto id = e.import_audio(vi);
    AUREA_CHECK(id.ok());
    f64 bpm = 0.0;
    auto n = id.ok() ? e.detect_beats(*id, &bpm) : Result<u32>{Status{Errc::InvalidState, ""}};
    std::printf("    44,1 kHz: %s, %u batidas, %.2f BPM\n", n.ok() ? "ok" : "erro", n.ok() ? *n : 0u, bpm);
    AUREA_CHECK(n.ok() && *n >= 5);
    e.shutdown();
}

AUREA_TEST(ClipTime, MarkerDragPreservesOccupiedTargetsAndUndo) {
    TimeRig r(cfg_with_audio());
    AUREA_CHECK(r.e.toggle_marker(30));
    AUREA_CHECK(r.e.toggle_marker(90));
    AUREA_CHECK(!r.e.move_marker(90, 30));
    AUREA_CHECK(r.e.move_marker(90, 90));
    std::vector<i64> markers(3 * 8);
    AUREA_CHECK_EQ(r.e.query_markers(markers.data(), 8), 2u);
    AUREA_CHECK_EQ(markers[0], 30);
    AUREA_CHECK_EQ(markers[3], 90);
    // Collision and stationary drag must not consume an undo step.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.e.query_markers(markers.data(), 8), 1u);
    AUREA_CHECK_EQ(markers[0], 30);
}

AUREA_TEST(ClipTime, MarkerEditingIsAtomicUndoableAndPortable) {
    TimeRig r(cfg_with_audio());
    const std::string label = "corte \xF0\x9F\x8E\xAC";
    AUREA_CHECK(r.e.edit_marker(-1, 30, 0xFF112233u, label));
    AUREA_CHECK(r.e.edit_marker(-1, 60, 0xFF445566u, "second"));
    AUREA_CHECK(!r.e.edit_marker(30, 60, 0xFFFFFFFFu, "collision"));
    AUREA_CHECK(!r.e.edit_marker(30, r.comp()->duration().value, 0xFFFFFFFFu, "outside"));
    AUREA_CHECK(!r.e.edit_marker(30, 40, 0xFFFFFFFFu, std::string(1025, 'x')));
    AUREA_CHECK_EQ(r.e.marker_label(30), label);
    AUREA_CHECK(r.e.edit_marker(30, 45, 0xFF778899u, "renamed"));
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.e.marker_label(30), label);
    AUREA_CHECK_EQ(r.e.marker_label(45), std::string{});
    Command redo; redo.type = CommandType::Redo;
    AUREA_CHECK(r.e.apply_command(redo).ok());
    AUREA_CHECK_EQ(r.e.marker_label(45), std::string("renamed"));
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_marker_edit.aurea";
    AUREA_CHECK(r.e.edit_marker(45, 45, 0xFF778899u, label));
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(r.e.marker_label(45), label);
    std::vector<i64> markers(24);
    AUREA_CHECK_EQ(r.e.query_markers(markers.data(), 8), 2u);
    AUREA_CHECK_EQ(markers[1], static_cast<i64>(0xFF778899u));
    AUREA_CHECK(r.e.delete_marker(45));
    AUREA_CHECK(!r.e.delete_marker(45));
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.e.marker_label(45), label);
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
    r.comp()->put_marker(Marker{FrameIndex{75}, 0xFF112233u, kMarkerBeat, "beat"});
    AUREA_CHECK(r.e.edit_marker(75, 80, 0xFF445566u, "beat moved"));
    const auto& all = r.comp()->markers();
    AUREA_CHECK_EQ(all.back().kind, kMarkerBeat);
}

AUREA_TEST(ClipTime, BeatTapMarksLiveWithoutTogglingOrPausing) {
    TimeRig r(cfg_with_audio());
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{45}, 30.0);
    AUREA_CHECK(r.e.apply_command(seek).ok());
    AUREA_CHECK_EQ(r.e.mark_beat_live(), 45);
    // Dois toques no mesmo quadro: o 2º não apaga (toggle apagaria).
    AUREA_CHECK_EQ(r.e.mark_beat_live(), -1);
    std::vector<i64> m(3 * 8);
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 1u);
    AUREA_CHECK_EQ(m[0], 45);
    // Tocando: marca pelo relógio de mídia (não pelo último quadro
    // desenhado, que aqui nem avança — ninguém chama update) e segue tocando.
    seek.seek.time = tick_at(FrameIndex{90}, 30.0);
    AUREA_CHECK(r.e.apply_command(seek).ok());
    Command play; play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(r.e.apply_command(play).ok());
    AUREA_CHECK(r.e.playback().playing());
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const i64 live = r.e.mark_beat_live();
    AUREA_CHECK(live >= 92 && live <= 120);   // ≥ 3 quadros de 30 fps depois do play
    AUREA_CHECK(r.e.playback().playing());
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 2u);
    Command pause; pause.type = CommandType::PlaybackPause;
    AUREA_CHECK(r.e.apply_command(pause).ok());
    Command u; u.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(u).ok());   // cada toque é um passo
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 1u);
    AUREA_CHECK_EQ(m[0], 45);
}

AUREA_TEST(ClipTime, MarkersToggleUndoSaveAndRetime) {
    TimeRig r(cfg_with_audio());
    AUREA_CHECK(r.e.toggle_marker(30));
    AUREA_CHECK(r.e.toggle_marker(90));
    std::vector<i64> m(3 * 8);
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 2u);
    AUREA_CHECK(!r.e.toggle_marker(90));    // tocar de novo tira
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 1u);
    Command u;
    u.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(u).ok());   // volta a de 90
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 2u);
    AUREA_CHECK(r.e.move_marker(90, 120));
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 2u);
    AUREA_CHECK_EQ(m[3], 120);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_marcas.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 2u);
    AUREA_CHECK_EQ(m[0], 30);
    AUREA_CHECK_EQ(m[3], 120);
    // 30 → 60 fps: a marca fica no mesmo segundo.
    r.comp()->retime(60.0);
    AUREA_CHECK_EQ(r.e.query_markers(m.data(), 8), 2u);
    AUREA_CHECK_EQ(m[0], 60);
    AUREA_CHECK_EQ(m[3], 240);
    std::remove(path.c_str());
}

// =============================================================================
//  Remapeamento de tempo e rampas de velocidade
// =============================================================================
AUREA_TEST(ClipTime, TimeRemapOnIsIdenticalThenRampsFollowTheCurve) {
    TimeRig r(cfg_with_audio());
    r.speed(2.0f);   // 150 quadros mostrando 300 da fonte
    const u64 id = r.layer.pack();
    std::vector<f64> before;
    for (i64 f = 0; f < 150; f += 7) before.push_back(r.L()->source_frame(FrameIndex{f}));
    AUREA_CHECK(r.e.set_time_remap(id, true));
    f64 worst = 0;
    usize k = 0;
    for (i64 f = 0; f < 150; f += 7) worst = std::max(worst, std::fabs(r.L()->source_frame(FrameIndex{f}) - before[k++]));
    AUREA_CHECK(worst < 0.01);

    // Herói: rápido (1,5× a média) → lento (0,25×) → rápido; mesmas pontas.
    AUREA_CHECK(r.e.apply_speed_ramp(id, 2));
    const Layer* l = r.L();
    const f64 s0 = l->source_frame(FrameIndex{0}), s1 = l->source_frame(FrameIndex{150});
    const f64 avg = (s1 - s0) / 150.0;
    const f64 midSpeed = l->source_frame(FrameIndex{80}) - l->source_frame(FrameIndex{79});
    std::printf("    rampa heroi: %.1f..%.1f, media %.3f, meio %.3f (%.2fx)\n", s0, s1, avg, midSpeed, midSpeed / avg);
    AUREA_CHECK(std::fabs(s0 - 0.0) < 0.5 && std::fabs(s1 - 300.0) < 0.5);
    AUREA_CHECK(std::fabs(midSpeed / avg - 0.25) < 0.05);
    bool monotonic = true;
    for (i64 f = 1; f <= 150; ++f) if (l->source_frame(FrameIndex{f}) < l->source_frame(FrameIndex{f - 1}) - 1e-6) monotonic = false;
    AUREA_CHECK(monotonic);

    // Áudio segue a MESMA curva: no quadro 80 toca o instante da fonte dele.
    auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK_EQ(snap->clips.size(), usize{1});
    if (snap->clips.empty()) return;
    AUREA_CHECK(!snap->clips[0].srcByFrame.empty());
    audio::AudioBlockCache cache(&r.factory, 16ull << 20, false);
    cache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * 48000});
    Fetch f(cache);
    std::vector<f32> out(2);
    const i64 at = audio::frame_to_sample(80, 30.0);
    audio::mix(*snap, at, 1, f, out.data());
    const f64 srcSec = l->source_frame(FrameIndex{80}) / 30.0;
    std::printf("    audio no quadro 80: %.5f (esperado %.5f)\n", out[0], synthetic_audio_value(cfg_with_audio(), 0, srcSec));
    AUREA_CHECK_NEAR(out[0], synthetic_audio_value(cfg_with_audio(), 0, srcSec), 2e-3);

    // Salvar e reabrir mantém a curva.
    const f64 expect80 = l->source_frame(FrameIndex{80});
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_remap.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* back = nullptr;
    r.comp()->layers().for_each([&](LayerId, const Layer& x) { back = &x; });
    AUREA_CHECK(back && back->timeRemapEnabled);
    AUREA_CHECK(back && std::fabs(back->source_frame(FrameIndex{80}) - expect80) < 0.01);
    std::remove(path.c_str());
}

// =============================================================================
//  Rastreio de ponto e estabilização
// =============================================================================
namespace {
SyntheticConfig moving_square_cfg() {
    SyntheticConfig c;
    c.width = 160;
    c.height = 90;
    c.frameCount = 90;
    c.pattern = SyntheticPattern::MovingSquare;
    return c;
}
} // namespace

AUREA_TEST(ClipTime, PointTrackerFollowsTheSquareAndStabilizeHoldsIt) {
    TimeRig r(moving_square_cfg());
    u32 tracked = 0;
    auto nid = r.e.track_point(r.layer.pack(), 30.0f, 30.0f, false, &tracked);
    AUREA_CHECK(nid.ok());
    if (!nid.ok()) return;
    const Layer* n = r.comp()->layer(LayerId::unpack(*nid));
    const Layer* v = r.L();
    AUREA_CHECK(n != nullptr);
    f64 worst = 0;
    for (i64 f = 0; f < static_cast<i64>(tracked); f += 5) {
        const Vec4 want = layer_world_matrix(*r.comp(), *v, FrameIndex{f})
                        * Vec4{static_cast<f32>(moving_square_x(f)), static_cast<f32>(moving_square_y(f)), 0, 1};
        const f32 gx = n->tracks.find(TrackProperty::PositionX)->sample(n->local_time(FrameIndex{f}));
        const f32 gy = n->tracks.find(TrackProperty::PositionY)->sample(n->local_time(FrameIndex{f}));
        worst = std::max<f64>(worst, std::hypot(gx - want.x, gy - want.y));
    }
    // Escala camada → composição (a composição do teste é menor que o vídeo).
    const Mat4 m0 = layer_world_matrix(*r.comp(), *v, FrameIndex{0});
    const f64 k = std::hypot(m0.col[0].x, m0.col[0].y);
    std::printf("    rastreio: %u quadros, pior erro %.3f px da camada\n", tracked, worst / k);
    AUREA_CHECK(tracked >= 85);
    AUREA_CHECK(worst / k < 0.5);

    // Estabilizar: o quadrado fica parado na tela.
    auto sid = r.e.track_point(r.layer.pack(), 30.0f, 30.0f, true, &tracked);
    AUREA_CHECK(sid.ok());
    v = r.L();
    const Vec4 w0 = layer_world_matrix(*r.comp(), *v, FrameIndex{0}) * Vec4{30, 30, 0, 1};
    f64 drift = 0;
    for (i64 f = 0; f < static_cast<i64>(tracked); f += 5) {
        const Vec4 w = layer_world_matrix(*r.comp(), *v, FrameIndex{f})
                     * Vec4{static_cast<f32>(moving_square_x(f)), static_cast<f32>(moving_square_y(f)), 0, 1};
        drift = std::max<f64>(drift, std::hypot(w.x - w0.x, w.y - w0.y));
    }
    std::printf("    estabilizado: deriva maxima %.3f px da camada\n", drift / k);
    AUREA_CHECK(drift / k < 0.5);
}

AUREA_TEST(ClipTime, PointTrackerStartsAtThePlayhead) {
    // O toque é no quadro do cabeçote: o rastreio começa ali (e não no 1º quadro).
    TimeRig r(moving_square_cfg());
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{40}, 30.0);
    AUREA_CHECK(r.e.apply_command(seek).ok());
    u32 tracked = 0;
    auto nid = r.e.track_point(r.layer.pack(), static_cast<f32>(moving_square_x(40)), static_cast<f32>(moving_square_y(40)), false, &tracked);
    AUREA_CHECK(nid.ok());
    if (!nid.ok()) return;
    const Layer* n = r.comp()->layer(LayerId::unpack(*nid));
    const Layer* v = r.L();
    f64 worst = 0;
    for (i64 f = 40; f < 40 + static_cast<i64>(tracked); f += 5) {
        const Vec4 want = layer_world_matrix(*r.comp(), *v, FrameIndex{f})
                        * Vec4{static_cast<f32>(moving_square_x(f)), static_cast<f32>(moving_square_y(f)), 0, 1};
        const f32 gx = n->tracks.find(TrackProperty::PositionX)->sample(n->local_time(FrameIndex{f}));
        const f32 gy = n->tracks.find(TrackProperty::PositionY)->sample(n->local_time(FrameIndex{f}));
        worst = std::max<f64>(worst, std::hypot(gx - want.x, gy - want.y));
    }
    const Mat4 m0 = layer_world_matrix(*r.comp(), *v, FrameIndex{0});
    const f64 k = std::hypot(m0.col[0].x, m0.col[0].y);
    std::printf("    rastreio a partir do cabecote (40): %u quadros, pior erro %.3f px\n", tracked, worst / k);
    AUREA_CHECK(tracked >= 45 && tracked <= 50);
    AUREA_CHECK(worst / k < 0.5);
}

AUREA_TEST(ClipTime, MaskTrackerMovesThePathWithTheSquare) {
    // Máscara em volta do quadrado (px da camada): o rastreio grava um key de
    // caminho por quadro e o centro do caminho acompanha o quadrado.
    TimeRig r(moving_square_cfg());
    const f32 pts[4 * 6] = {22, 22, 0, 0, 0, 0,  38, 22, 0, 0, 0, 0,  38, 38, 0, 0, 0, 0,  22, 38, 0, 0, 0, 0};
    const i32 mid = r.e.add_mask(r.layer.pack(), pts, 4, true);
    AUREA_CHECK(mid >= 0);
    for (u32 mode : {0u, 1u}) {
        auto res = r.e.track_mask(r.layer.pack(), static_cast<u32>(mid), mode);
        AUREA_CHECK(res.ok());
        if (!res.ok()) return;
        const Layer* v = r.L();
        const Mask& m = v->masks[0];
        AUREA_CHECK_EQ(m.pathKeys.size(), static_cast<usize>(*res));
        f64 worst = 0, worstSize = 0;
        std::vector<MaskPoint> shape;
        for (i64 f = 0; f < static_cast<i64>(*res); f += 3) {
            mask::evaluate_path(m, static_cast<f64>(v->local_time(FrameIndex{f}).value), shape);
            f32 cx = 0, cy = 0;
            for (const MaskPoint& p : shape) { cx += p.position.x * 0.25f; cy += p.position.y * 0.25f; }
            worst = std::max<f64>(worst, std::hypot(cx - static_cast<f32>(moving_square_x(f)), cy - static_cast<f32>(moving_square_y(f))));
            worstSize = std::max<f64>(worstSize, std::fabs((shape[1].position.x - shape[0].position.x) - 16.0f));
        }
        std::printf("    mascara rastreada (modo %u): %u quadros, pior erro do centro %.3f px, tamanho +-%.3f px\n", mode, *res, worst, worstSize);
        AUREA_CHECK(*res >= 85);
        AUREA_CHECK(worst < 0.75);
        AUREA_CHECK(worstSize < 0.75);
    }
}

AUREA_TEST(ClipTime, VectorShutterUsesRemappedSourceTravel) {
    TimeRig r(cfg_with_audio());
    auto* l = r.L();
    AUREA_CHECK_NEAR(l->source_shutter_travel(50.0, 0.5), 0.5, 1e-9);
    l->speed = 2.0f;
    AUREA_CHECK_NEAR(l->source_shutter_travel(50.0, 0.5), 1.0, 1e-9);
    l->reversed = true;
    AUREA_CHECK_NEAR(l->source_shutter_travel(50.0, 0.5), 1.0, 1e-9);
    AUREA_CHECK(r.e.set_time_remap(r.layer.pack(), true));
    AUREA_CHECK(r.e.edit_time_remap_key(r.layer.pack(), 0, 0, 100.0f, static_cast<i32>(Interpolation::Linear)) == 0);
    AUREA_CHECK(r.e.edit_time_remap_key(r.layer.pack(), 1, 100, 0.0f, static_cast<i32>(Interpolation::Linear)) == 1);
    AUREA_CHECK_NEAR(l->source_shutter_travel(50.0, 0.5), 0.5, 1e-5);
    AUREA_CHECK(r.e.edit_time_remap_key(r.layer.pack(), 1, 100, 100.0f, static_cast<i32>(Interpolation::Linear)) == 1);
    AUREA_CHECK_NEAR(l->source_shutter_travel(50.0, 0.5), 0.0, 1e-9);
    AUREA_CHECK_NEAR(l->source_shutter_travel(50.0, 0.0), 0.0, 1e-9);
}

AUREA_TEST(ClipTime, RemapEffectKeysUseCanonicalCurveAndTimelineQuery) {
    TimeRig r(cfg_with_audio());
    Command add; add.type = CommandType::EffectAdd;
    add.effect_add.layer = r.layer;
    add.effect_add.effectType = effect_type_id(effect_keys::kTimeRemap);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(r.e.apply_command(add).ok());
    const u32 effect = r.L()->effects.back().id;
    Command key; key.type = CommandType::KeyframeInsert;
    key.keyframe.track = TrackRef{r.layer, TrackProperty::EffectParam, effect, 0};
    key.keyframe.time = FrameIndex{60}; key.keyframe.value = 4.0f;
    AUREA_CHECK(r.e.apply_command(key).ok());
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{60}), 120.0, 0.001);
    AUREA_CHECK(r.L()->tracks.find(TrackProperty::EffectParam, effect, 0) == nullptr);
    key.type = CommandType::KeyframeSetValue; key.keyframe.value = 3.0f;
    AUREA_CHECK(r.e.apply_command(key).ok());
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{60}), 90.0, 0.001);
    Command move; move.type = CommandType::KeyframeMove;
    move.keyframe_move.track = key.keyframe.track;
    move.keyframe_move.fromTime = FrameIndex{60}; move.keyframe_move.toTime = FrameIndex{70};
    AUREA_CHECK(r.e.apply_command(move).ok());
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{70}), 90.0, 0.001);
    key.keyframe.track.property = TrackProperty::TimeRemap;
    key.keyframe.time = FrameIndex{70}; key.keyframe.value = 135;
    AUREA_CHECK(r.e.apply_command(key).ok());
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{70}), 135.0, 0.001);
    bridge::KeyframeRow rows[16]{};
    const u32 count = r.e.query_keyframes(r.layer.pack(), rows, 16);
    AUREA_CHECK_EQ(count, 3u);
    for (u32 i = 0; i < count; ++i) AUREA_CHECK_EQ(rows[i].property, static_cast<u32>(TrackProperty::TimeRemap));
    u32 layers = 0;
    AUREA_CHECK_EQ(r.e.query_all_keyframes(nullptr, 0, nullptr, 0, &layers), 3u);
    f32 samples[3]{};
    AUREA_CHECK_EQ(r.e.query_track_curve(r.layer.pack(), static_cast<u32>(TrackProperty::EffectParam), effect, 0, 0, 70, samples, 3), 3u);
    AUREA_CHECK_NEAR(samples[2], 4.5f, 0.001f);
    Command enabled; enabled.type = CommandType::EffectSetEnabled;
    enabled.effect_enabled.layer = r.layer; enabled.effect_enabled.effect = EffectId{effect, 0}; enabled.effect_enabled.enabled = false;
    AUREA_CHECK(r.e.apply_command(enabled).ok());
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{70}), 70.0, 0.001);
    enabled.effect_enabled.enabled = true;
    AUREA_CHECK(r.e.apply_command(enabled).ok());
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{70}), 135.0, 0.001);
    const char* path = "remap_effect_canonical_test.aurea";
    AUREA_CHECK(r.e.save_project(path).ok());
    AUREA_CHECK(r.e.load_project(path).ok());
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{70}), 135.0, 0.001);
    std::remove(path);
    key.type = CommandType::KeyframeDelete;
    AUREA_CHECK(r.e.apply_command(key).ok());
    AUREA_CHECK_EQ(r.L()->timeRemap.keys.size(), usize{2});
}

AUREA_TEST(ClipTime, RemapSmoothFreezeAndReversePresetsChangeSourceTime) {
    TimeRig r(cfg_with_audio());
    Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = r.layer;
    add.effect_add.effectType = effect_type_id(effect_keys::kTimeRemap); add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(r.e.apply_command(add).ok());
    Command param; param.type = CommandType::EffectSetParam; param.effect_param.layer = r.layer;
    param.effect_param.effect = EffectId{r.L()->effects.back().id, 0};
    param.effect_param.paramIndex = 1; param.effect_param.value = 1;
    AUREA_CHECK(r.e.apply_command(param).ok());
    AUREA_CHECK(r.L()->timeRemap.keys[0].interp == Interpolation::EaseInOut);
    AUREA_CHECK(r.L()->source_frame(FrameIndex{30}) < 25.0);
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{60}, 30.0);
    AUREA_CHECK(r.e.apply_command(seek).ok());
    const f64 frozen = r.L()->source_frame(FrameIndex{60});
    AUREA_CHECK(r.e.apply_speed_ramp(r.layer.pack(), 5));
    for (i64 frame : {0LL, 280LL, 30LL, 150LL}) AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{frame}), frozen, 0.001);
    AUREA_CHECK(r.e.apply_speed_ramp(r.layer.pack(), 6));
    AUREA_CHECK(r.L()->source_frame(FrameIndex{10}) > r.L()->source_frame(FrameIndex{100}));
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{150}), 150.0, 0.001);
}

// O Remapear tempo simplificado (um "Tempo" + facilidade por chave + atalhos)
// não muda o dado: um projeto salvo com o efeito antigo — parâmetros
// {Tempo, Interpolação 0/1/2} e a curva com Bézier do gráfico, Segurar e
// Suave — tem que abrir mostrando exatamente os mesmos quadros da fonte.
AUREA_TEST(ClipTime, RemapOldProjectKeepsFrameTimesAndSimpleControls) {
    TimeRig r(cfg_with_audio());
    Layer* l = r.L();
    EffectInstance fx;
    fx.id = l->alloc_effect_id();
    fx.type = effect_type_id(effect_keys::kTimeRemap);
    fx.params.resize(2);
    fx.params[0].constant.v[0] = 2.5f;   // o "Tempo" guardado pelo efeito antigo
    fx.params[1].constant.v[0] = 2.0f;   // "Segurar" no enum antigo de 3 itens
    l->effects.push_back(fx);
    Track& t = l->timeRemap;
    t.property = TrackProperty::TimeRemap;
    t.set(FrameIndex{0}, 0.0f, Interpolation::Bezier);
    t.keys[0].bx1 = 0.2f; t.keys[0].by1 = 0.6f; t.keys[0].bx2 = 0.7f; t.keys[0].by2 = 0.9f;
    t.set(FrameIndex{40}, 90.0f, Interpolation::Hold);
    t.set(FrameIndex{80}, 90.0f, Interpolation::EaseInOut);
    t.set(FrameIndex{120}, 30.0f, Interpolation::Linear);
    t.set(FrameIndex{150}, 150.0f, Interpolation::Linear);
    l->timeRemapEnabled = true;
    std::vector<f64> before;
    for (i64 f = 0; f <= 150; ++f) before.push_back(l->source_frame_f(static_cast<f64>(f) + 0.25));
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_remap_antigo.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    std::remove(path.c_str());
    r.comp()->layers().for_each([&](LayerId id, const Layer&) { r.layer = id; });
    l = r.L();
    AUREA_CHECK(l && l->timeRemapEnabled);
    if (!l) return;
    f64 worst = 0;
    for (i64 f = 0; f <= 150; ++f)
        worst = std::max(worst, std::fabs(l->source_frame_f(static_cast<f64>(f) + 0.25) - before[static_cast<usize>(f)]));
    std::printf("    remap antigo reaberto: pior diferenca %.6f quadros\n", worst);
    AUREA_CHECK(worst < 1e-4);

    // A leitura da facilidade: os números antigos continuam dizendo o mesmo.
    const u32 effect = l->effects.back().id;
    auto seek = [&](i64 frame) {
        Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(r.e.apply_command(c).ok());
    };
    auto ease_here = [&]() {
        bridge::EffectParamRow rows[4]{}; char blob[512]{};
        AUREA_CHECK(r.e.query_effect_params(r.layer.pack(), effect, rows, 4, blob, sizeof blob) >= 2u);
        return rows[1].value[0];
    };
    seek(40); AUREA_CHECK_NEAR(ease_here(), 2.0f, 1e-6f);   // Segurar
    seek(80); AUREA_CHECK_NEAR(ease_here(), 1.0f, 1e-6f);   // Suave
    seek(0);  AUREA_CHECK_NEAR(ease_here(), 1.0f, 1e-6f);   // Bézier do gráfico lê "Suave"

    // Facilidades novas por chave (3 = começa devagar, 4 = termina devagar)
    // sem tocar no gráfico, e o arrasto do Tempo mantém a escolhida.
    Command param; param.type = CommandType::EffectSetParam; param.effect_param.layer = r.layer;
    param.effect_param.effect = EffectId{effect, 0};
    seek(120);
    param.effect_param.paramIndex = 1; param.effect_param.value = 3.0f;
    AUREA_CHECK(r.e.apply_command(param).ok());
    AUREA_CHECK(l->timeRemap.keys[l->timeRemap.find_exact(FrameIndex{120})].interp == Interpolation::EaseIn);
    AUREA_CHECK_NEAR(ease_here(), 3.0f, 1e-6f);
    AUREA_CHECK(l->source_frame(FrameIndex{135}) < 30.0 + 60.0);   // começa devagar: abaixo da reta
    param.effect_param.paramIndex = 0; param.effect_param.value = 2.0f;   // 2 s da fonte
    AUREA_CHECK(r.e.apply_command(param).ok());
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{120}), 60.0, 1e-3);
    AUREA_CHECK(l->timeRemap.keys[l->timeRemap.find_exact(FrameIndex{120})].interp == Interpolation::EaseIn);
    param.effect_param.value = 999.0f;   // o dedo passou do fim: preso ao último quadro
    AUREA_CHECK(r.e.apply_command(param).ok());
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{120}), 299.0, 1e-3);
    param.effect_param.paramIndex = 1; param.effect_param.value = 4.0f;
    AUREA_CHECK(r.e.apply_command(param).ok());
    AUREA_CHECK(l->timeRemap.keys[l->timeRemap.find_exact(FrameIndex{120})].interp == Interpolation::EaseOut);

    // Congelar aqui: 1 s parado no quadro do cabeçote, depois segue a 1×; o
    // que vem antes do cabeçote não muda.
    AUREA_CHECK(r.e.apply_speed_ramp(r.layer.pack(), 0));
    seek(60);
    const f64 at60 = l->source_frame(FrameIndex{60});
    const f64 at20 = l->source_frame(FrameIndex{20});
    AUREA_CHECK(r.e.apply_speed_ramp(r.layer.pack(), 7));
    for (i64 f : {60LL, 75LL, 90LL}) AUREA_CHECK_NEAR(l->source_frame(FrameIndex{f}), at60, 1e-3);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{100}), at60 + 10.0, 1e-3);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{20}), at20, 1e-3);
    seek(300);   // no fim do clipe não há o que segurar
    AUREA_CHECK(!r.e.apply_speed_ramp(r.layer.pack(), 7));
}

AUREA_TEST(ClipTime, MoveVideoToTwoSecondPlayheadPreservesSourceAndUndo) {
    TimeRig r(cfg_with_audio());
    const FrameIndex oldEnd = r.L()->end;
    Command seek; seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{60}, 30.0);
    AUREA_CHECK(r.e.apply_command(seek).ok());
    Command move; move.type = CommandType::LayerSetTimeRange;
    move.layer_range.layer = r.layer;
    move.layer_range.start = FrameIndex{60};
    move.layer_range.end = FrameIndex{60 + oldEnd.value};
    move.layer_range.setOffset = 0;
    // Same asynchronous command queue used by the iOS dock bridge.
    AUREA_CHECK_EQ(r.e.submit_commands(&move, 1, nullptr, 0), 1u);
    AUREA_CHECK(r.e.render_frame().ok());
    AUREA_CHECK_EQ(r.L()->start.value, 60LL);
    AUREA_CHECK_EQ(r.L()->end.value - r.L()->start.value, oldEnd.value);
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{60}), 0.0, 0.001);
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{90}), 30.0, 0.001);
    for (i64 frame : {90LL, 60LL, 120LL, 61LL}) {
        seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(r.e.apply_command(seek).ok());
        AUREA_CHECK(r.e.render_frame().ok());
        AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{frame}), static_cast<f64>(frame - 60), 0.001);
    }
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.L()->start.value, 0LL);
    AUREA_CHECK_EQ(r.L()->end.value, oldEnd.value);
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{60}), 60.0, 0.001);
}

// =============================================================================
//  Velocidade com keyframes: a fonte anda a INTEGRAL da velocidade. Sem
//  keyframe, a conta antiga (byte a byte); com, vídeo, áudio, forma de onda,
//  corte e arquivo seguem a mesma integral.
// =============================================================================
namespace {
void speed_key(TimeRig& r, i64 local, f32 v) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe.track.layer = r.layer;
    c.keyframe.track.property = TrackProperty::Speed;
    c.keyframe.time = FrameIndex{local};
    c.keyframe.value = v;
    AUREA_CHECK(r.e.apply_command(c).ok());
}
} // namespace

AUREA_TEST(ClipTime, SpeedWithoutKeysIsByteIdenticalToConstantSpeed) {
    TimeRig r(cfg_with_audio());
    r.speed(2.0f);
    std::vector<f64> before;
    for (i64 f = 0; f < 150; ++f) before.push_back(r.L()->source_frame_f(static_cast<f64>(f) + 0.25));
    // Trilha vazia (criada, sem keyframe): nada muda, nem no último bit.
    (void)r.L()->tracks.get_or_create(TrackProperty::Speed);
    AUREA_CHECK(r.L()->speed_track() == nullptr);
    bool same = true;
    for (i64 f = 0; f < 150; ++f)
        same = same && r.L()->source_frame_f(static_cast<f64>(f) + 0.25) == before[static_cast<usize>(f)];
    AUREA_CHECK(same);
    // Um keyframe só, no valor parado: a mesma reta (a menos do arredondamento).
    speed_key(r, 0, 2.0f);
    f64 worst = 0;
    for (i64 f = 0; f < 150; ++f)
        worst = std::max(worst, std::fabs(r.L()->source_frame_f(static_cast<f64>(f) + 0.25) - before[static_cast<usize>(f)]));
    AUREA_CHECK(worst < 1e-9);
}

AUREA_TEST(ClipTime, SpeedKeyframesIntegrateSourceTimeAndAudioFollows) {
    TimeRig r(cfg_with_audio());
    // 1× no quadro 0 → 3× no quadro 20 (linear), 3× depois:
    // fonte(u) = u + u²/20 até 20 (= 40), depois 40 + 3·(u − 20).
    speed_key(r, 0, 1.0f);
    speed_key(r, 20, 3.0f);
    const Layer* l = r.L();
    AUREA_CHECK(l->speed_track() != nullptr);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{0}), 0.0, 1e-9);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{10}), 15.0, 1e-9);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{20}), 40.0, 1e-9);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{30}), 70.0, 1e-9);
    AUREA_CHECK_NEAR(l->source_frame_f(10.5), 10.5 + 10.5 * 10.5 / 20.0, 1e-6);
    AUREA_CHECK_NEAR(l->speed_at(FrameIndex{10}), 2.0, 1e-6);
    // Facilidade (não linear): a integral continua monótona e contínua.
    Command ease;
    ease.type = CommandType::KeyframeSetInterpolation;
    ease.keyframe_interp.track.layer = r.layer;
    ease.keyframe_interp.track.property = TrackProperty::Speed;
    ease.keyframe_interp.time = FrameIndex{0};
    ease.keyframe_interp.interp = Interpolation::EaseInOut;
    ease.keyframe_interp.bx1 = 0.42f; ease.keyframe_interp.by1 = 0.0f;
    ease.keyframe_interp.bx2 = 0.58f; ease.keyframe_interp.by2 = 1.0f;
    AUREA_CHECK(r.e.apply_command(ease).ok());
    bool monotonic = true;
    for (i64 f = 1; f < 60; ++f) monotonic = monotonic && l->source_frame(FrameIndex{f}) > l->source_frame(FrameIndex{f - 1});
    AUREA_CHECK(monotonic);
    // EaseInOut simétrico: a área do trecho é a mesma da reta (média 2×).
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{20}), 40.0, 0.05);
    ease.keyframe_interp.interp = Interpolation::Linear;
    AUREA_CHECK(r.e.apply_command(ease).ok());

    // Áudio pelo caminho quadro a quadro, no instante da fonte da integral.
    auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK_EQ(snap->clips.size(), usize{1});
    if (snap->clips.empty()) return;
    AUREA_CHECK(!snap->clips[0].srcByFrame.empty());
    audio::AudioBlockCache cache(&r.factory, 16ull << 20, false);
    cache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * 48000});
    Fetch f(cache);
    std::vector<f32> out(2);
    audio::mix(*snap, audio::frame_to_sample(30, 30.0), 1, f, out.data());
    AUREA_CHECK_NEAR(out[0], synthetic_audio_value(cfg_with_audio(), 0, 70.0 / 30.0), 2e-3);

    // Detalhe da camada: a velocidade no cabeçote e o bit "animada".
    Command seek; seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(r.e.apply_command(seek).ok());
    bridge::LayerDetailPOD d{};
    AUREA_CHECK(r.e.query_layer_detail(r.layer.pack(), d));
    AUREA_CHECK_NEAR(d.speed, 2.0f, 1e-4);
    AUREA_CHECK((d.timeFlags & 64u) != 0);

    // Salvar e reabrir mantém a integral.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_speedkeys.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* back = nullptr;
    r.comp()->layers().for_each([&](LayerId, const Layer& x) { back = &x; });
    AUREA_CHECK(back && back->speed_track() != nullptr);
    AUREA_CHECK(back && std::fabs(back->source_frame(FrameIndex{30}) - 70.0) < 1e-6);
    std::remove(path.c_str());
}

AUREA_TEST(ClipTime, SplitAndReverseKeepTheIntegratedSpeedTime) {
    TimeRig r(cfg_with_audio());
    speed_key(r, 0, 1.0f);
    speed_key(r, 20, 3.0f);
    std::vector<f64> before;
    for (i64 f = 0; f < 60; ++f) before.push_back(r.L()->source_frame(FrameIndex{f}));
    // Corte no quadro 25: as duas metades mostram a MESMA fonte de antes.
    Command split; split.type = CommandType::LayerSplit;
    split.layer_split.layer = r.layer;
    split.layer_split.at = FrameIndex{25};
    AUREA_CHECK(r.e.apply_command(split).ok());
    f64 worst = 0;
    r.comp()->layers().for_each([&](LayerId, const Layer& x) {
        for (i64 f = std::max<i64>(0, x.start.value); f < std::min<i64>(60, x.end.value); ++f)
            worst = std::max(worst, std::fabs(x.source_frame(FrameIndex{f}) - before[static_cast<usize>(f)]));
    });
    std::printf("    corte com velocidade animada: erro maximo %.6f quadro\n", worst);
    AUREA_CHECK(worst < 0.01);

    // Reverso com keyframes: o último quadro mostra a entrada, sempre recuando.
    TimeRig q(cfg_with_audio());
    speed_key(q, 0, 1.0f);
    speed_key(q, 20, 3.0f);
    q.reverse(true);
    const Layer* l = q.L();
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{l->end.value - 1}), 0.0, 1e-9);
    bool down = true;
    for (i64 f = 1; f < l->end.value; ++f) down = down && l->source_frame(FrameIndex{f}) < l->source_frame(FrameIndex{f - 1});
    AUREA_CHECK(down);
}

// =============================================================================
//  Remapear tempo "de lista" (três linhas: manter o tom, o momento da fonte no
//  cabeçote com chave, interpolação) + Ao contrário. O dado é o de sempre:
//  a curva Layer::timeRemap, com o easing padrão (editor de curva normal).
// =============================================================================
namespace {
struct RemapPanel {
    TimeRig& r;
    u32 effect = 0;
    explicit RemapPanel(TimeRig& rig) : r(rig) {
        Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = r.layer;
        add.effect_add.effectType = effect_type_id(effect_keys::kTimeRemap); add.effect_add.index = kInvalidIndex;
        AUREA_CHECK(r.e.apply_command(add).ok());
        effect = r.L()->effects.back().id;
    }
    void seek(i64 frame) {
        Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(r.e.apply_command(c).ok());
    }
    /// O que o painel faz ao arrastar/digitar o valor: o momento da fonte (s)
    /// no cabeçote — grava a chave ali.
    void at(i64 frame, f32 seconds) {
        seek(frame);
        Command p; p.type = CommandType::EffectSetParam; p.effect_param.layer = r.layer;
        p.effect_param.effect = EffectId{effect, 0}; p.effect_param.paramIndex = 0; p.effect_param.value = seconds;
        AUREA_CHECK(r.e.apply_command(p).ok());
    }
    u32 flags() {
        bridge::LayerDetailPOD d{};
        AUREA_CHECK(r.e.query_layer_detail(r.layer.pack(), d));
        return d.timeFlags;
    }
};

/// Cruzamentos por zero do canal esquerdo em [start, start + n) da timeline.
u32 zero_crossings(const audio::AudioMixSnapshot& snap, audio::BlockSource& f, i64 start, u32 n, f32* peak = nullptr) {
    std::vector<f32> out(static_cast<usize>(n) * 2);
    audio::mix(snap, start, n, f, out.data());
    u32 z = 0;
    f32 top = 0.0f;
    for (u32 i = 0; i < n; ++i) {
        top = std::max(top, std::fabs(out[i * 2]));
        if (i > 0 && (out[(i - 1) * 2] < 0.0f) != (out[i * 2] < 0.0f)) ++z;
    }
    if (peak) *peak = top;
    return z;
}
} // namespace

AUREA_TEST(ClipTime, RemapPanelKeysMapTimelineToSourceFreezeAndEase) {
    TimeRig r(cfg_with_audio());   // 300 quadros a 30 fps, 1×
    RemapPanel p(r);
    p.at(0, 0.0f);
    p.at(60, 4.0f);     // no quadro 60 da timeline: 4 s (120) da fonte
    p.at(90, 4.0f);     // 60..90 parado (congelado)
    p.at(150, 2.0f);    // e volta para 2 s: toca ao contrário
    const Layer* l = r.L();
    AUREA_CHECK(l->timeRemapEnabled);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{60}), 120.0, 1e-6);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{30}), 60.0, 1e-6);     // entre chaves: interpolado
    AUREA_CHECK_NEAR(l->source_frame_f(30.5), 61.0, 1e-6);
    for (i64 f = 60; f <= 90; ++f) AUREA_CHECK_NEAR(l->source_frame(FrameIndex{f}), 120.0, 1e-6);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{120}), 90.0, 1e-6);    // descendo de 120 para 60
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{150}), 60.0, 1e-6);
    // O valor que a linha mostra (segundos da fonte no cabeçote).
    p.seek(120);
    bridge::EffectParamRow rows[4]{}; char blob[512]{};
    AUREA_CHECK(r.e.query_effect_params(r.layer.pack(), p.effect, rows, 4, blob, sizeof blob) >= 1u);
    AUREA_CHECK_NEAR(rows[0].value[0], 3.0f, 1e-5f);
    // Easing pelo editor de curva NORMAL: o mesmo comando de qualquer trilha.
    Command ease; ease.type = CommandType::KeyframeSetInterpolation;
    ease.keyframe_interp.track = TrackRef{r.layer, TrackProperty::TimeRemap, kInvalidIndex, 0};
    ease.keyframe_interp.time = FrameIndex{0};
    ease.keyframe_interp.interp = Interpolation::Bezier;
    ease.keyframe_interp.bx1 = 0.42f; ease.keyframe_interp.by1 = 0.0f;
    ease.keyframe_interp.bx2 = 1.0f; ease.keyframe_interp.by2 = 1.0f;
    AUREA_CHECK(r.e.apply_command(ease).ok());
    AUREA_CHECK(l->timeRemap.keys[0].interp == Interpolation::Bezier);
    const f64 want = 120.0 * keyframe_ease(l->timeRemap.keys[0], 0.5f);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{30}), want, 1e-4);
    AUREA_CHECK(l->source_frame(FrameIndex{30}) < 55.0);                // começa devagar
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{60}), 120.0, 1e-6);    // as chaves não mexem
    // Arrastar o valor numa chave que já existe mantém o easing escolhido.
    p.at(0, 0.5f);
    AUREA_CHECK(l->timeRemap.keys[0].interp == Interpolation::Bezier);
    AUREA_CHECK_NEAR(l->source_frame(FrameIndex{0}), 15.0, 1e-5);
}

AUREA_TEST(ClipTime, ThreeRemapKeysGoForwardThenBackwardWithCurvesAndReload) {
    TimeRig r(cfg_with_audio());
    RemapPanel p(r);
    p.at(0, 0);
    p.at(150, 5);
    p.at(300, 0);
    AUREA_CHECK_EQ(r.L()->timeRemap.keys.size(), usize{3});
    for (i64 frame : {0LL, 30LL, 75LL, 149LL, 150LL, 180LL, 225LL, 299LL, 300LL}) {
        const f64 want = frame <= 150 ? frame : 300 - frame;
        AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{frame}), want, .001);
    }
    Command ease; ease.type = CommandType::KeyframeSetBezier;
    ease.keyframe_interp.track = TrackRef{r.layer, TrackProperty::TimeRemap, kInvalidIndex, 0};
    ease.keyframe_interp.time = FrameIndex{150};
    ease.keyframe_interp.interp = Interpolation::Bezier;
    ease.keyframe_interp.bx1 = .42f; ease.keyframe_interp.by1 = 0;
    ease.keyframe_interp.bx2 = 1; ease.keyframe_interp.by2 = 1;
    AUREA_CHECK(r.e.apply_command(ease).ok());
    std::vector<f64> before;
    for (i64 frame = 0; frame < 300; ++frame) {
        const f64 value = r.L()->source_frame(FrameIndex{frame});
        if (frame > 150) AUREA_CHECK(value <= before.back());
        before.push_back(value);
    }
    AUREA_CHECK_NEAR(before[75], 75., .001);
    AUREA_CHECK(before[225] > 75.); // easing slows the return, never changes its sign
    const char* path = "remap_three_keys_test.aurea";
    AUREA_CHECK(r.e.save_project(path).ok());
    AUREA_CHECK(r.e.load_project(path).ok());
    for (i64 frame = 0; frame < 300; ++frame)
        AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{frame}), before[frame], .001);
    AUREA_CHECK(r.e.reverse_time_remap(r.layer.pack()));
    for (i64 frame = 0; frame < 300; ++frame)
        AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{frame}), before[299 - frame], .02);
    std::remove(path);
}

AUREA_TEST(ClipTime, RemapReverseMirrorsTheCurveExactlyAndUndoes) {
    TimeRig r(cfg_with_audio());
    RemapPanel p(r);
    p.at(0, 0.0f);
    p.at(60, 4.0f);
    p.at(90, 4.0f);
    Command ease; ease.type = CommandType::KeyframeSetInterpolation;
    ease.keyframe_interp.track = TrackRef{r.layer, TrackProperty::TimeRemap, kInvalidIndex, 0};
    ease.keyframe_interp.time = FrameIndex{0};
    ease.keyframe_interp.interp = Interpolation::Bezier;
    ease.keyframe_interp.bx1 = 0.3f; ease.keyframe_interp.by1 = 0.1f;
    ease.keyframe_interp.bx2 = 0.9f; ease.keyframe_interp.by2 = 0.6f;
    AUREA_CHECK(r.e.apply_command(ease).ok());
    ease.keyframe_interp.time = FrameIndex{90};
    ease.keyframe_interp.interp = Interpolation::EaseIn;
    AUREA_CHECK(r.e.apply_command(ease).ok());
    const i64 last = r.L()->end.value - 1;
    std::vector<f64> before;
    for (i64 f = 0; f <= last; ++f) before.push_back(r.L()->source_frame(FrameIndex{f}));
    AUREA_CHECK((p.flags() & 256u) == 0);
    AUREA_CHECK(r.e.reverse_time_remap(r.layer.pack()));
    f64 worst = 0;
    for (i64 f = 0; f <= last; ++f)
        worst = std::max(worst, std::fabs(r.L()->source_frame(FrameIndex{f}) - before[static_cast<usize>(last - f)]));
    std::printf("    ao contrario: pior diferenca %.6f quadros\n", worst);
    AUREA_CHECK(worst < 0.02);
    AUREA_CHECK((p.flags() & 256u) != 0);
    // De novo: volta exatamente ao que era.
    AUREA_CHECK(r.e.reverse_time_remap(r.layer.pack()));
    worst = 0;
    for (i64 f = 0; f <= last; ++f)
        worst = std::max(worst, std::fabs(r.L()->source_frame(FrameIndex{f}) - before[static_cast<usize>(f)]));
    AUREA_CHECK(worst < 1e-3);
    AUREA_CHECK((p.flags() & 256u) == 0);
    // Um passo de desfazer por toque.
    AUREA_CHECK(r.e.reverse_time_remap(r.layer.pack()));
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    worst = 0;
    for (i64 f = 0; f <= last; ++f)
        worst = std::max(worst, std::fabs(r.L()->source_frame(FrameIndex{f}) - before[static_cast<usize>(f)]));
    AUREA_CHECK(worst < 1e-3);

    // Remapeamento desligado: Ao contrário liga (curva do tempo de agora) e inverte.
    TimeRig plain(cfg_with_audio());
    AUREA_CHECK(plain.e.reverse_time_remap(plain.layer.pack()));
    AUREA_CHECK(plain.L()->timeRemapEnabled);
    AUREA_CHECK_NEAR(plain.L()->source_frame(FrameIndex{0}), 299.0, 1e-6);
    AUREA_CHECK_NEAR(plain.L()->source_frame(FrameIndex{299}), 0.0, 1e-6);
    AUREA_CHECK_NEAR(plain.L()->source_frame(FrameIndex{100}), 199.0, 1e-6);
}

AUREA_TEST(ClipTime, KeepPitchHoldsToneWhileTheSourceFollowsTheRemap) {
    TimeRig r(cfg_with_audio());   // seno de 440 Hz no canal esquerdo
    audio::AudioBlockCache cache(&r.factory, 32ull << 20, false);
    auto crossings = [&](i64 fromFrame, f32* peak = nullptr) {
        auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
        AUREA_CHECK_EQ(snap->clips.size(), usize{1});
        if (snap->clips.empty()) return 0u;
        cache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * 48000});
        Fetch f(cache);
        return zero_crossings(*snap, f, audio::frame_to_sample(fromFrame, 30.0), 24000, peak);   // 0,5 s
    };
    const u32 natural = crossings(30);
    std::printf("    cruzamentos em 0,5 s: 1x %u\n", natural);
    AUREA_CHECK(natural >= 430 && natural <= 450);
    // 2× sem manter o tom: o tom dobra (varispeed, como sempre).
    r.speed(2.0f);
    const u32 fast = crossings(30);
    AUREA_CHECK(fast >= 860 && fast <= 900);
    // 2× mantendo o tom: a mesma nota.
    AUREA_CHECK(r.e.set_keep_pitch(r.layer.pack(), true));
    const u32 kept = crossings(30);
    std::printf("    2x: varispeed %u, tom mantido %u\n", fast, kept);
    AUREA_CHECK(kept >= 420 && kept <= 475);
    // Câmera lenta pela curva (0,5×) mantendo o tom: nota igual, não grave.
    r.speed(1.0f);
    RemapPanel p(r);
    p.at(0, 0.0f);
    p.at(150, 2.5f);
    const u32 slow = crossings(30);
    AUREA_CHECK(slow >= 420 && slow <= 475);
    AUREA_CHECK(r.e.set_keep_pitch(r.layer.pack(), false));
    const u32 low = crossings(30);
    std::printf("    0,5x pela curva: tom mantido %u, varispeed %u\n", slow, low);
    AUREA_CHECK(low >= 200 && low <= 240);
    AUREA_CHECK(r.e.set_keep_pitch(r.layer.pack(), true));

    // A 1× (curva igual ao tempo) os grãos reconstroem a fonte exatamente, e
    // Ao contrário a −1× também (lê para trás na mesma posição).
    AUREA_CHECK(r.e.apply_speed_ramp(r.layer.pack(), 0));
    auto sample_at = [&](i64 frame) {
        auto snap = audio::build_snapshot(*r.comp(), *r.e.project(), nullptr, nullptr, nullptr);
        if (snap->clips.empty()) return 0.0f;
        AUREA_CHECK(snap->clips[0].keepPitch);
        cache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * 48000});
        Fetch f(cache);
        std::vector<f32> out(2);
        audio::mix(*snap, audio::frame_to_sample(frame, 30.0) + 123, 1, f, out.data());
        return out[0];
    };
    const f64 t1 = (r.L()->source_frame_f(40.0) * 1600.0 + 123.0) / 48000.0;
    AUREA_CHECK_NEAR(sample_at(40), synthetic_audio_value(cfg_with_audio(), 0, t1), 2e-3);
    AUREA_CHECK(r.e.reverse_time_remap(r.layer.pack()));
    const f64 t2 = (r.L()->source_frame_f(40.0) * 1600.0 - 123.0) / 48000.0;
    AUREA_CHECK_NEAR(sample_at(40), synthetic_audio_value(cfg_with_audio(), 0, t2), 2e-3);
    // Congelado mantendo o tom = silêncio (nada de zumbido de grão repetido).
    p.seek(60);
    AUREA_CHECK(r.e.apply_speed_ramp(r.layer.pack(), 5));
    f32 peak = 1.0f;
    (void)crossings(30, &peak);
    AUREA_CHECK(peak < 1e-6f);
}

AUREA_TEST(ClipTime, KeepPitchAndInterpolationSurviveSaveAndOldProjectsStayOff) {
    TimeRig r(cfg_with_audio());
    const u64 id = r.layer.pack();
    AUREA_CHECK(r.e.set_keep_pitch(id, true));
    AUREA_CHECK(r.e.set_frame_blend(id, 2));
    RemapPanel p(r);
    p.at(0, 1.0f);
    AUREA_CHECK(r.e.reverse_time_remap(id));
    AUREA_CHECK((p.flags() & 128u) != 0);
    const f64 at40 = r.L()->source_frame(FrameIndex{40});
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_remap_lista.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    std::remove(path.c_str());
    r.comp()->layers().for_each([&](LayerId lid, const Layer&) { r.layer = lid; });
    const Layer* l = r.L();
    AUREA_CHECK(l && l->keepPitch && l->frameBlend == 2 && l->timeRemapEnabled);
    AUREA_CHECK(l && std::fabs(l->source_frame(FrameIndex{40}) - at40) < 1e-4);
    AUREA_CHECK((p.flags() & (128u | 256u | 16u)) == (128u | 256u | 16u));
    // Desfazer o "manter o tom" é um passo.
    AUREA_CHECK(r.e.set_keep_pitch(r.layer.pack(), false));
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK(r.L()->keepPitch);

    // Projeto gravado antes do campo (seção Timeline < 39): tom acompanha.
    Project old;
    LoadReport report;
    const std::string fixture = std::string(AUREA_TEST_DATA_DIR) + "/manual-editing/manual-editing.aurea";
    AUREA_CHECK(ProjectSerializer::load(old, fixture, LoadOptions{}, &report).ok());
    AUREA_CHECK(report.timelineVersion < 39u);
    const Composition* c = old.timeline().composition(old.timeline().root());
    u32 layers = 0;
    if (c) c->layers().for_each([&](LayerId, const Layer& x) { ++layers; AUREA_CHECK(!x.keepPitch); });
    AUREA_CHECK(layers > 0u);
}

// Beta 2140: o Remapear tempo numa pré-composição. Apagar o efeito leva as
// chaves junto (antes a curva ficava guardada e os losangos continuavam na
// régua "sem informação"); pôr de novo começa limpo; apagar uma chave solta
// funciona na pré-composição como no vídeo.
AUREA_TEST(ClipTime, RemapEffectOnPrecompDeletesKeysAndStartsClean) {
    TimeRig r(cfg_with_audio());
    const u64 ids[1] = {r.layer.pack()};
    auto pre = r.e.precompose(ids, 1, "Grupo");
    AUREA_CHECK(pre.ok());
    if (!pre.ok()) return;
    const LayerId p = LayerId::unpack(*pre);
    auto P = [&]() { return r.comp()->layer(p); };
    AUREA_CHECK(P() && P()->kind == LayerKind::Composition);
    auto add_remap = [&]() -> u32 {
        Command add; add.type = CommandType::EffectAdd;
        add.effect_add.layer = p;
        add.effect_add.effectType = effect_type_id(effect_keys::kTimeRemap);
        add.effect_add.index = kInvalidIndex;
        AUREA_CHECK(r.e.apply_command(add).ok());
        return P()->effects.back().id;
    };
    u32 effect = add_remap();
    AUREA_CHECK(P()->timeRemapEnabled);
    AUREA_CHECK_EQ(P()->timeRemap.keys.size(), usize{2});
    Command key; key.type = CommandType::KeyframeInsert;
    key.keyframe.track = TrackRef{p, TrackProperty::EffectParam, effect, 0};
    key.keyframe.time = FrameIndex{30}; key.keyframe.value = 2.0f;
    AUREA_CHECK(r.e.apply_command(key).ok());
    AUREA_CHECK_EQ(P()->timeRemap.keys.size(), usize{3});
    AUREA_CHECK_NEAR(P()->source_frame(FrameIndex{30}), 60.0, 0.001);

    // Apagar a chave solta (pelo efeito e pela linha da régua).
    Command del = key; del.type = CommandType::KeyframeDelete;
    AUREA_CHECK(r.e.apply_command(del).ok());
    AUREA_CHECK_EQ(P()->timeRemap.keys.size(), usize{2});
    AUREA_CHECK(r.e.apply_command(key).ok());
    del.keyframe.track.property = TrackProperty::TimeRemap;
    AUREA_CHECK(r.e.apply_command(del).ok());
    AUREA_CHECK_EQ(P()->timeRemap.keys.size(), usize{2});
    AUREA_CHECK(r.e.apply_command(key).ok());
    AUREA_CHECK_EQ(P()->timeRemap.keys.size(), usize{3});

    // Apagar o efeito: a curva e as chaves vão junto; a régua fica vazia.
    Command rem; rem.type = CommandType::EffectRemove;
    rem.effect_ref.layer = p; rem.effect_ref.effect = EffectId{effect, 0};
    AUREA_CHECK(r.e.apply_command(rem).ok());
    AUREA_CHECK(!P()->timeRemapEnabled);
    AUREA_CHECK(P()->timeRemap.keys.empty());
    bridge::KeyframeRow rows[8]{};
    AUREA_CHECK_EQ(r.e.query_keyframes(p.pack(), rows, 8), 0u);
    AUREA_CHECK_NEAR(P()->source_frame(FrameIndex{30}), 30.0, 0.001);

    // Pôr de novo: a rampa de agora, sem a chave antiga.
    effect = add_remap();
    AUREA_CHECK_EQ(P()->timeRemap.keys.size(), usize{2});
    AUREA_CHECK(P()->timeRemap.find_exact(FrameIndex{30}) == kInvalidIndex);
    AUREA_CHECK_NEAR(P()->source_frame(FrameIndex{30}), 30.0, 0.001);

    // Desfazer a remoção devolve a curva com as chaves.
    rem.effect_ref.effect = EffectId{effect, 0};
    AUREA_CHECK(r.e.apply_command(rem).ok());
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(P()->timeRemap.keys.size(), usize{2});
    AUREA_CHECK(P()->timeRemapEnabled);
}

AUREA_TEST(ClipTime, DockActionsExtendOnlyTheEdgeFacingThePlayhead) {
    TimeRig r(cfg_with_audio());
    const u64 id = r.layer.pack();
    AUREA_CHECK(r.e.edit_clip_time(id, 0, 30));
    AUREA_CHECK(r.e.edit_clip_time(id, 1, 120));
    AUREA_CHECK_EQ(r.e.query_clip_time_actions(id, 0), u32{8});
    AUREA_CHECK_EQ(r.e.query_clip_time_actions(id, 29), u32{8});
    for (i64 frame : {30, 31, 60, 119, 120})
        AUREA_CHECK_EQ(r.e.query_clip_time_actions(id, frame), u32{7});
    AUREA_CHECK_EQ(r.e.query_clip_time_actions(id, 121), u32{16});
    AUREA_CHECK_EQ(r.e.query_clip_time_actions(id, -1), u32{0});
    AUREA_CHECK_EQ(r.e.query_clip_time_actions(0, 60), u32{0});
    AUREA_CHECK(!r.e.edit_clip_time(id, 8, 60));
    AUREA_CHECK(!r.e.edit_clip_time(id, 9, 60));
    // Extending to an absolute playhead must not turn into a magnetic ripple.
    AUREA_CHECK(r.e.set_layer_magnetic_track(id, true));
    const f64 source = r.L()->source_frame(FrameIndex{60});
    AUREA_CHECK(r.e.edit_clip_time(id, 8, 10));
    AUREA_CHECK_EQ(r.L()->start.value, 10);
    AUREA_CHECK_EQ(r.L()->end.value, 120);
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{60}), source, 1e-9);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.L()->start.value, 30);
    AUREA_CHECK(r.e.edit_clip_time(id, 9, 140));
    AUREA_CHECK_EQ(r.L()->start.value, 30);
    AUREA_CHECK_EQ(r.L()->end.value, 140);
    AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{60}), source, 1e-9);
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.L()->end.value, 120);
    r.L()->locked = true;
    AUREA_CHECK(!r.e.edit_clip_time(id, 8, 10));
    AUREA_CHECK(!r.e.edit_clip_time(id, 9, 140));
}

AUREA_TEST(ClipTime, AbsoluteMoveAcceptsZeroAndPreservesTrimmedSourceAndKeys) {
    TimeRig r(cfg_with_audio());
    AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 0, 30));
    AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 1, 120));
    const i64 offset = r.L()->offset.value;
    const f64 source = r.L()->source_frame(r.L()->start);
    Command key;
    key.type = CommandType::KeyframeInsert;
    key.keyframe.track.layer = r.layer;
    key.keyframe.track.property = TrackProperty::PositionX;
    key.keyframe.track.effectIndex = kInvalidIndex;
    key.keyframe.time = FrameIndex{15};
    key.keyframe.value = 71;
    AUREA_CHECK(r.e.apply_command(key).ok());
    AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 6, 0));
    AUREA_CHECK_EQ(r.L()->start.value, 0);
    AUREA_CHECK_EQ(r.L()->end.value, 90);
    AUREA_CHECK_EQ(r.L()->offset.value, offset);
    AUREA_CHECK_NEAR(r.L()->source_frame(r.L()->start), source, 1e-9);
    AUREA_CHECK_EQ(r.L()->tracks.find(TrackProperty::PositionX)->keys[0].time.value, 15);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK_EQ(r.L()->start.value, 30);
    AUREA_CHECK_EQ(r.L()->end.value, 120);
    AUREA_CHECK_EQ(r.L()->offset.value, offset);
    r.L()->locked = true;
    AUREA_CHECK(!r.e.edit_clip_time(r.layer.pack(), 6, 0));
    AUREA_CHECK_EQ(r.L()->start.value, 30);
}

// Beta 06–07/10: depois de dividir, as duas partes voltam a esticar até onde a
// mídia vai (vídeo/áudio) ou sem limite (foto). Nada no corte trava a borda.
AUREA_TEST(ClipTime, SplitPartsExtendAgainUpToTheSource) {
    for (int kind = 0; kind < 3; ++kind) {
        TimeRig r(cfg_with_audio());
        LayerId id = r.layer;
        if (kind == 1) {
            VideoImport ai; ai.sourcePath = "s"; ai.displayName = "som";
            auto a = r.e.import_audio(ai);
            AUREA_CHECK(a.ok()); if (!a.ok()) continue;
            id = LayerId::unpack(*a);
        } else if (kind == 2) {
            std::vector<u8> px(4 * 4 * 4, 255);
            auto p = r.e.import_image(px.data(), 4, 4, "foto");
            AUREA_CHECK(p.ok()); if (!p.ok()) continue;
            id = LayerId::unpack(*p);
        }
        Layer* l = r.comp()->layer(id);
        AUREA_CHECK(l != nullptr); if (!l) continue;
        const i64 s0 = l->start.value, e0 = l->end.value;
        AUREA_CHECK(e0 - s0 > 20);
        const i64 at = s0 + (e0 - s0) / 2;
        Command c; c.type = CommandType::LayerSplit; c.layer_split.layer = id; c.layer_split.at = FrameIndex{at};
        AUREA_CHECK(r.e.apply_command(c).ok());
        LayerId second{};
        r.comp()->layers().for_each([&](LayerId lid, const Layer& x) {
            if (lid != id && x.start.value == at && x.end.value == e0) second = lid;
        });
        AUREA_CHECK(second.valid());
        // Primeira parte: estica o fim de volta até o fim original da mídia.
        AUREA_CHECK(r.e.edit_clip_time(id.pack(), 1, e0));
        AUREA_CHECK_EQ(r.comp()->layer(id)->end.value, e0);
        // Segunda parte: puxa o começo de volta até o começo original.
        AUREA_CHECK(r.e.edit_clip_time(second.pack(), 0, s0));
        AUREA_CHECK_EQ(r.comp()->layer(second)->start.value, s0);
        if (kind == 2) {   // foto: sem fim de mídia
            AUREA_CHECK(r.e.edit_clip_time(id.pack(), 1, e0 + 500));
            AUREA_CHECK_EQ(r.comp()->layer(id)->end.value, e0 + 500);
        } else {           // vídeo/áudio: nunca além da fonte
            AUREA_CHECK(!r.e.edit_clip_time(id.pack(), 1, e0 + 500));
        }
    }
}

AUREA_TEST(ClipTime, ExtendButtonRestoresAvailableHandleAndUndo) {
    for (f32 speed : {1.f, 2.f}) {
        TimeRig r(cfg_with_audio());
        r.speed(speed);
        AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 1, 50));
        Track keyed; keyed.property = TrackProperty::PositionX;
        keyed.set(FrameIndex{15}, 12.f); r.L()->tracks.add(std::move(keyed));
        const f64 sampled = r.L()->source_frame(FrameIndex{30});
        AUREA_CHECK(r.e.edit_clip_time(r.layer.pack(), 7, 0));
        AUREA_CHECK_EQ(r.L()->end.value, speed == 1.f ? 300 : 150);
        AUREA_CHECK_NEAR(r.L()->source_frame(FrameIndex{30}), sampled, 1e-9);
        AUREA_CHECK_EQ(r.L()->tracks.find(TrackProperty::PositionX)->keys[0].time.value, 15);
        AUREA_CHECK(!r.e.edit_clip_time(r.layer.pack(), 7, 0));
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(r.e.apply_command(undo).ok());
        AUREA_CHECK_EQ(r.L()->end.value, 50);
        r.L()->locked = true;
        AUREA_CHECK(!r.e.edit_clip_time(r.layer.pack(), 7, 0));
    }
}
