// =============================================================================
//  Tempo do clipe: velocidade, reverso, congelar quadro — uma só função de
//  tempo da fonte (Layer::source_frame) para vídeo, miniatura e áudio.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/render/Renderer.hpp"

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
