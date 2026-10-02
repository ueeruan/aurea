// Relatos beta de 02/10: "trava muito com deep glow, partículas e 3D" e
// "fecha sozinho: só pus 1 motion track, 1 texto 3D, 1 partículas e 1 texto".
//
//  - BetaHeavy.TrackText3DParticlesTextSurvivePlayScrubEditExport: a MESMA
//    combinação (vídeo com rastreio de ponto aplicado, texto 3D, partículas,
//    texto) tocada por uma thread de render enquanto a "UI" edita, consulta,
//    escolhe na cena 3D, refaz o texto 3D e desfaz/refaz; depois exporta.
//  - Perf8C.BenchBetaHeavyScene (AUREA_BENCH=1): custo por quadro do Deep
//    Glow, das partículas e do 3D, em resolução cheia e na meia do preview.
#include "TestFramework.hpp"
#include "MockBackend.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/scene3d/Text3D.hpp"

#if defined(AUREA_TEST_VULKAN)
#include "VulkanBackend.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

class NullSink final : public ExportSink {
public:
    explicit NullSink(std::atomic<u32>* frames) : frames_(frames) {}
    Status open(const char*, const VideoStreamConfig&, const AudioStreamConfig*) noexcept override { return OkStatus; }
    Status write_video(const u8*, u32, const u8*, u32, i64) noexcept override { frames_->fetch_add(1); return OkStatus; }
    Status write_audio(const i16*, u32, i64) noexcept override { return OkStatus; }
    Status finish() noexcept override { return OkStatus; }
    void abort() noexcept override {}
    EncoderInfo encoder_info() const noexcept override { return EncoderInfo{}; }
private:
    std::atomic<u32>* frames_;
};
std::unique_ptr<ExportSink> make_null_sink(void* user) { return std::make_unique<NullSink>(static_cast<std::atomic<u32>*>(user)); }

bool vulkan_ok() {
#if defined(AUREA_TEST_VULKAN)
    static const bool ok = [] {
        vk::Backend b;
        BackendConfig c;
        c.enableValidation = false;
        const bool r = b.initialize(c).ok();
        b.shutdown();
        return r;
    }();
    return ok;
#else
    return false;
#endif
}

GPUBackend* make_backend() {
#if defined(AUREA_TEST_VULKAN)
    if (vulkan_ok()) return new vk::Backend();
#endif
    return new MockBackend();
}

struct BetaRig {
    SyntheticFactory factory;
    std::atomic<u32> exported{0};
    Engine e;
    u64 video = 0;
    bool ok = false;
    explicit BetaRig(const SyntheticConfig& cfg, u32 compW = 1080, u32 compH = 1920) : factory(cfg) {
        EngineConfig ec;
        ec.backend = make_backend();
        ec.backendConfig.enableValidation = false;
        ec.mediaFactory = &factory;
        ec.exportSinkFactory = &make_null_sink;
        ec.exportSinkContext = &exported;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        if (!e.initialize(ec).ok() || !e.new_project(compW, compH, 30.0, "beta").ok()) return;
        VideoImport vi;
        vi.sourcePath = "sintetico";
        vi.displayName = "sintetico";
        auto v = e.import_video(vi);
        if (!v.ok()) return;
        video = *v;
        ok = true;
    }
    ~BetaRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    void seek(i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{frame}, 30.0);
        (void)e.submit_commands(&c, 1);
    }
    bool add_effect(u64 layer, const char* key) {
        Command add;
        add.type = CommandType::EffectAdd;
        add.effect_add.layer = LayerId::unpack(layer);
        add.effect_add.effectType = effect_type_id(key);
        add.effect_add.index = kInvalidIndex;
        return e.apply_command(add).ok();
    }
};

SyntheticConfig beta_video() {
    SyntheticConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.frameCount = 90;
    cfg.pattern = SyntheticPattern::MovingSquare;
    return cfg;
}

f64 ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

AUREA_TEST(BetaHeavy, TrackText3DParticlesTextSurvivePlayScrubEditExport) {
    BetaRig r(beta_video());
    AUREA_CHECK(r.ok); if (!r.ok) return;
    // 1) Motion track de ponto no vídeo, aplicado num texto (alvo) e num Nulo.
    r.seek(5);
    const f32 seed[2] = {static_cast<f32>(moving_square_x(5)), 18.0f};
    AUREA_CHECK(r.e.start_motion_track(r.video, 0, 0, false, seed, 1));
    for (int i = 0; i < 6000 && r.e.motion_track_status().state == 1; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto text = r.e.add_text("seguindo");
    AUREA_CHECK(text.ok()); if (!text.ok()) return;
    if (r.e.motion_track_status().state == 2) {
        AUREA_CHECK(r.e.apply_motion_track(*text, 1).ok());
        AUREA_CHECK(r.e.apply_motion_track(r.video, 0).ok());
    }
    // 2) Texto 3D, partículas e um texto comum; "mexi no Z".
    scene3d::Text3DSpec spec;
    spec.content = "Shawnwesley";
    const auto text3d = r.e.add_text3d(spec);
    AUREA_CHECK(text3d.ok()); if (!text3d.ok()) return;
    const auto particles = r.e.add_particles(0);
    AUREA_CHECK(particles.ok()); if (!particles.ok()) return;
    const auto plain = r.e.add_text("texto");
    AUREA_CHECK(plain.ok()); if (!plain.ok()) return;
    AUREA_CHECK(r.add_effect(*text3d, effect_keys::kDeepGlow));
    r.comp()->layer(LayerId::unpack(*text3d))->transform.position.z = -300.0f;

    // 3) Render (thread do preview) × UI (edita, consulta, escolhe, desfaz).
    TextureDesc d;
    d.width = 540;
    d.height = 960;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *r.e.gpu()->create_texture(d);
    std::atomic<bool> done{false};
    std::atomic<u32> rendered{0}, failed{0};
    std::thread render([&] {
        for (u32 f = 0; f < 240 && !done; ++f) {
            r.seek(static_cast<i64>((f * 7) % 90));   // toca e "arrasta" pra trás e pra frente
            if (r.e.render_offscreen(target, 540, 960, true).ok()) rendered.fetch_add(1); else failed.fetch_add(1);
        }
        done = true;
    });
    u32 step = 0;
    const u64 ids[] = {r.video, *text, *text3d, *particles, *plain};
    while (!done) {
        ++step;
        (void)r.e.read_status();
        for (u64 id : ids) {
            bridge::LayerDetailPOD pod{};
            (void)r.e.query_layer_detail(id, pod);
            f32 g[8]{};
            (void)r.e.query_gizmo(id, 50.0f, g);
        }
        r.e.set_scene_editor(step % 40 < 20, static_cast<f32>(step % 360), 15.0f, 3.0f);
        (void)r.e.scene_pick(540.0f, 960.0f, 40.0f);
        f32 guides[1280]{};
        (void)r.e.query_scene_guides(guides, 256);
        (void)r.e.set_particle_param(*particles, static_cast<u32>(ParticleParam::Rate), 20.0f + static_cast<f32>(step % 50));
        Command mv;
        mv.type = CommandType::LayerSetPosition;
        mv.position.layer = LayerId::unpack(*text3d);
        mv.position.x = 400.0f + static_cast<f32>(step % 200);
        mv.position.y = 900.0f;
        (void)r.e.submit_commands(&mv, 1);
        if (step % 9 == 0) {
            spec.content = step % 18 ? "Shawn" : "Shawnwesley";
            (void)r.e.set_text3d(*text3d, spec);
        }
        if (step % 13 == 0) {
            Command u;
            u.type = step % 26 ? CommandType::Undo : CommandType::Redo;
            (void)r.e.apply_command(u);
        }
        if (step % 31 == 0 && r.e.motion_track_status().state != 1) {
            (void)r.e.start_motion_track(r.video, 0, 0, false, seed, 1);
        }
        if (step % 37 == 0) r.e.cancel_motion_track();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    render.join();
    r.e.cancel_motion_track();
    r.e.set_scene_editor(false, 0, 0, 3);
    r.e.gpu()->destroy_texture(target);
    std::printf("    %u quadros, %u falhas, %u passos da UI\n", rendered.load(), failed.load(), step);
    AUREA_CHECK(rendered.load() > 200);

    // 4) Exporta tudo junto (o export roda na própria thread).
    ExportSettings s;
    s.height = 480;
    s.fps = 30;
    AUREA_CHECK(r.e.start_export(s, "nao-usado.mp4").ok());
    for (int i = 0; i < 120000 && !r.e.export_progress().finished; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto p = r.e.export_progress();
    AUREA_CHECK(p.finished);
    AUREA_CHECK(p.result == Errc::Ok);
    AUREA_CHECK(r.exported.load() > 0);
}

// =============================================================================
// Medição (AUREA_BENCH=1): o quadro da cena pesada do relato.
// =============================================================================
AUREA_TEST(Perf8C, BenchBetaHeavyScene) {
    if (!std::getenv("AUREA_BENCH") || *std::getenv("AUREA_BENCH") != '1') { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    if (!vulkan_ok()) { std::printf("    (sem GPU Vulkan)\n"); return; }
    struct Case { const char* name; bool glow, particles, text3d; };
    const Case cases[] = {{"so imagem", false, false, false}, {"deep glow", true, false, false},
                          {"particulas", false, true, false}, {"texto 3D", false, false, true},
                          {"tudo", true, true, true}};
    std::printf("\n    cena 1080x1920 (imagem cheia) | alvo | CPU prepare ms | GPU ms | parede ms (mediana de 20) | passes\n");
    std::vector<u8> photo(1080u * 1920u * 4u);
    for (u32 y = 0; y < 1920; ++y) for (u32 x = 0; x < 1080; ++x) {
        u8* p = &photo[(y * 1080u + x) * 4u];
        const bool light = ((x / 90) + (y / 90)) % 5 == 0;   // janelas claras: o glow tem o que acender
        p[0] = static_cast<u8>(light ? 250 : x * 120 / 1080); p[1] = static_cast<u8>(light ? 240 : y * 120 / 1920); p[2] = light ? 230 : 60; p[3] = 255;
    }
    for (const Case& c : cases) {
        BetaRig r(beta_video());
        AUREA_CHECK(r.ok); if (!r.ok) return;
        r.comp()->layer(LayerId::unpack(r.video))->visible = false;
        r.e.set_offscreen_timers(true);
        const auto image = r.e.import_image(photo.data(), 1080, 1920, "foto");
        AUREA_CHECK(image.ok()); if (!image.ok()) return;
        if (c.glow) AUREA_CHECK(r.add_effect(*image, effect_keys::kDeepGlow));
        if (c.particles) { AUREA_CHECK(r.e.add_particles(0).ok()); AUREA_CHECK(r.e.add_particles(1).ok()); }
        if (c.text3d) {
            scene3d::Text3DSpec spec;
            spec.content = "Shawnwesley";
            AUREA_CHECK(r.e.add_text3d(spec).ok());
        }
        for (const u32 h : {1920u, 960u}) {
            const u32 w = h * 1080 / 1920;
            TextureDesc d;
            d.width = w; d.height = h;
            d.format = SurfaceFormat::RGBA16F;
            d.renderTarget = true;
            d.transferSrc = true;
            const TextureHandle target = *r.e.gpu()->create_texture(d);
            std::vector<f64> prep, gpu, wall;
            for (u32 f = 0; f < 30; ++f) {
                r.seek(static_cast<i64>(f));
                const auto t0 = std::chrono::steady_clock::now();
                (void)r.e.render_offscreen(target, w, h, true);
                const f64 ms = ms_since(t0);
                if (f < 10) continue;
                const auto m = r.e.last_offscreen_measure();
                prep.push_back(m.prepareMs);
                gpu.push_back(m.gpuMs);
                wall.push_back(ms);
            }
            r.e.gpu()->destroy_texture(target);
            const auto last = r.e.last_offscreen_measure();
            if (c.glow && !c.particles && h == 1920) {
                GpuTiming passes[128]{};
                const u32 n = r.e.last_offscreen_gpu_passes(passes, 128);
                f64 glowMs = 0;
                for (u32 i = 0; i < n; ++i) std::printf("      passe %-22s %.3f ms\n", passes[i].label ? passes[i].label : "?", passes[i].ms);
                for (u32 i = 0; i < n; ++i) glowMs += passes[i].ms;
                std::printf("      soma dos passes %.3f ms\n", glowMs);
            }
            auto med = [](std::vector<f64> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
            std::printf("    %-11s | %4ux%-4u | %7.2f | %7.2f | %7.2f | %u\n", c.name, w, h, med(prep), med(gpu), med(wall), last.passesExecuted);
        }
    }
}
