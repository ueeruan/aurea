// =============================================================================
//  Fase 8C — render / preview: medição e regressão.
//
//  Tudo aqui mede o MESMO código do app (Renderer, FrameGraph, EffectGraph,
//  Engine). Dois tipos de teste:
//
//   - VERIFICAÇÃO (sempre roda): contagens exatas — passes, decodes, texturas
//     criadas, alocações no caminho quente — e o comportamento do preview
//     AUTO com séries de tempos sintéticas (determinístico).
//   - MEDIÇÃO (AUREA_BENCH=1): tempo de prepare/gravação na CPU (backend
//     falso: só o custo do motor, sem driver) e tempo de GPU com timestamps
//     no Vulkan do host, em 1080p/4K com 1, 10 e 50 camadas. Números do HOST
//     (GPU de mesa), nunca "de celular": o relatório diz isso.
//
//  O contador de alocação troca o `operator new` global deste executável de
//  teste: conta só na thread que ligou a contagem (a do render no teste).
// =============================================================================
#include "TestFramework.hpp"
#include "MockBackend.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/playback/Playback.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/Renderer.hpp"

#if defined(AUREA_TEST_VULKAN)
#include "VulkanBackend.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------------
// Contador de alocação (só a thread que ligou conta).
// -----------------------------------------------------------------------------
namespace {
std::atomic<aurea::u64> gAllocCount{0};
thread_local bool tCountAllocs = false;
inline void* counted_malloc(std::size_t n) noexcept {
    if (tCountAllocs) gAllocCount.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n ? n : 1);
}
} // namespace

void* operator new(std::size_t n) {
    if (void* p = counted_malloc(n)) return p;
    std::abort();
}
void* operator new[](std::size_t n) {
    if (void* p = counted_malloc(n)) return p;
    std::abort();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return counted_malloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return counted_malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

using namespace aurea;
using namespace aurea::test;

namespace {

bool bench_enabled() {
    const char* v = std::getenv("AUREA_BENCH");
    return v && *v == '1';
}

struct AllocScope {
    u64 start = gAllocCount.load();
    AllocScope() { tCountAllocs = true; }
    ~AllocScope() { tCountAllocs = false; }
    [[nodiscard]] u64 count() const { return gAllocCount.load() - start; }
};

f64 ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

f64 median(std::vector<f64> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// -----------------------------------------------------------------------------
// Cena de medição (a mesma para CPU e GPU).
// -----------------------------------------------------------------------------
ImagePixels gradient(u32 w, u32 h) {
    ImagePixels px;
    px.width = w;
    px.height = h;
    px.rgba.resize(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
            p[0] = static_cast<u8>(x * 255 / std::max(1u, w - 1));
            p[1] = static_cast<u8>(y * 255 / std::max(1u, h - 1));
            p[2] = 90;
            p[3] = 255;
        }
    }
    return px;
}

struct BenchScene {
    Project project;
    Composition* comp = nullptr;
    std::unordered_map<u64, ImagePixels> images;
    std::unique_ptr<SyntheticFactory> factory;
    MediaManager media;
    AssetId sharedImage{};

    BenchScene(u32 w, u32 h, f64 fps = 30.0) {
        auto p = Project::create_new(w, h, fps, "8c");
        project = std::move(*p);
        comp = project.timeline().composition(project.timeline().root());
        comp->set_background(Color{0, 0, 0, 1});
        comp->set_duration(FrameIndex{600});
    }

    static const ImagePixels* lookup(void* self, AssetId id) {
        auto* s = static_cast<BenchScene*>(self);
        auto it = s->images.find(id.pack());
        return it == s->images.end() ? nullptr : &it->second;
    }

    AssetId image_asset(u32 w, u32 h) {
        Asset a;
        a.kind = AssetKind::Image;
        const AssetId aid = project.add_asset(std::move(a));
        images[aid.pack()] = gradient(w, h);
        return aid;
    }

    LayerId image(AssetId aid, f32 x, f32 y, f32 scale, f32 rot = 0.0f) {
        const ImagePixels& px = images[aid.pack()];
        const LayerId id = comp->add_layer(LayerKind::Image, "img");
        Layer* l = comp->layer(id);
        l->source = aid;
        l->end = FrameIndex{600};
        l->transform.anchor = Vec3{px.width * 0.5f, px.height * 0.5f, 0};
        l->transform.position = Vec3{x, y, 0};
        l->transform.scale = Vec3{scale, scale, 1};
        l->transform.rotation = Vec3{0, 0, rot};
        return id;
    }

    LayerId video(const SyntheticConfig& cfg, f32 x, f32 y) {
        if (!factory) {
            factory = std::make_unique<SyntheticFactory>(cfg);
            media.set_factory(factory.get());
        }
        Asset a;
        a.kind = AssetKind::Video;
        a.video.width = cfg.width;
        a.video.height = cfg.height;
        a.video.fps = cfg.fps;
        const AssetId aid = project.add_asset(std::move(a));
        const LayerId id = comp->add_layer(LayerKind::Video, "video");
        Layer* l = comp->layer(id);
        l->source = aid;
        l->end = FrameIndex{static_cast<i64>(cfg.frameCount)};
        l->transform.anchor = Vec3{cfg.width * 0.5f, cfg.height * 0.5f, 0};
        l->transform.position = Vec3{x, y, 0};
        return id;
    }

    EffectInstance& add_effect(const EffectRegistry& reg, LayerId id, const char* key) {
        Layer* l = comp->layer(id);
        EffectInstance e;
        e.id = l->alloc_effect_id();
        e.type = effect_type_id(key);
        initialize_instance(e, *reg.params(e.type));
        l->effects.push_back(std::move(e));
        return l->effects.back();
    }

    /// `layers` imagens espalhadas pela composição; com `fx`, cada uma leva a
    /// cadeia de cor (exposição, brilho/contraste, saturação, tingir) + blur, e
    /// uma em cada cinco um glow.
    void populate(const EffectRegistry& reg, u32 layers, bool fx) {
        const f32 cw = static_cast<f32>(comp->width()), ch = static_cast<f32>(comp->height());
        sharedImage = image_asset(480, 270);
        const u32 cols = std::max(1u, static_cast<u32>(std::ceil(std::sqrt(static_cast<f32>(layers)))));
        for (u32 i = 0; i < layers; ++i) {
            const f32 gx = (static_cast<f32>(i % cols) + 0.5f) / static_cast<f32>(cols);
            const f32 gy = (static_cast<f32>(i / cols) + 0.5f) / static_cast<f32>(cols);
            const f32 scale = layers == 1 ? cw / 480.0f : cw / 480.0f * 0.45f;
            const LayerId id = image(sharedImage, gx * cw, gy * ch, scale, static_cast<f32>(i % 7) * 4.0f);
            comp->layer(id)->transform.opacity = layers == 1 ? 1.0f : 0.85f;
            if (!fx) continue;
            add_effect(reg, id, effect_keys::kExposure).params[0].constant.v[0] = 0.3f;
            EffectInstance& bc = add_effect(reg, id, effect_keys::kBrightnessContrast);
            bc.params[0].constant.v[0] = 10.0f;
            bc.params[1].constant.v[0] = 20.0f;
            add_effect(reg, id, effect_keys::kSaturation).params[0].constant.v[0] = 25.0f;
            add_effect(reg, id, effect_keys::kTint).params[2].constant.v[0] = 30.0f;
            add_effect(reg, id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 6.0f;
            if (i % 5 == 0) add_effect(reg, id, effect_keys::kGlow);
        }
    }
};

struct FrameMeasure {
    f64 prepareMs = 0, recordMs = 0, gpuMs = 0;
    f64 intervalMeanMs = 0, intervalStdMs = 0, intervalP99Ms = 0;
    u32 passes = 0, culled = 0, created = 0, transient = 0, physical = 0, aliased = 0;
    f64 allocsPerFrame = 0;
    f64 allocsPrepare = 0;
    u32 layers = 0;
};

/// Laço de playback: prepara e grava `frames` quadros seguidos (tempo andando).
/// Mede a mediana da segunda metade (regime), as alocações na thread do
/// render e as texturas criadas.
FrameMeasure run_frames(Renderer& r, GPUBackend& backend, BenchScene& s, u32 frames, bool finalQuality = false,
                        MockBackend* mock = nullptr) {
    const u32 w = s.comp->width(), h = s.comp->height();
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.sampled = true;
    d.transferSrc = true;
    const TextureHandle target = *backend.create_texture(d);
    OffscreenTarget off{target, w, h};
    RenderSettings rs;
    rs.dither = false;
    rs.gpuTimers = true;
    rs.finalQuality = finalQuality;
    FrameSnapshot snap;
    std::vector<f64> prep, rec, gpu, intervals;
    FrameMeasure m;
    u64 allocs = 0, allocsPrep = 0;
    u32 counted = 0;
    static u64 frameNo = 1000000;
    auto last = std::chrono::steady_clock::now();
    for (u32 f = 0; f < frames; ++f) {
        const bool steady = f >= frames / 2;
        if (mock) { mock->events.clear(); mock->events.reserve(8192); }
        const auto t0 = std::chrono::steady_clock::now();
        u64 a0 = gAllocCount.load();
        if (steady) tCountAllocs = true;
        r.prepare(*s.comp, s.project, FrameIndex{static_cast<i64>(f)}, &s.media, &BenchScene::lookup, &s, rs, ++frameNo, 1,
                  DecodeMode::Playback, 1.0f, snap);
        const f64 tp = ms_since(t0);
        const u64 aPrep = gAllocCount.load() - a0;
        FrameStats st;
        RenderTimings tm;
        const auto t1 = std::chrono::steady_clock::now();
        (void)r.render(snap, rs, &off, st, tm);
        tCountAllocs = false;
        const auto t2 = std::chrono::steady_clock::now();
        if (steady) {
            allocs += gAllocCount.load() - a0;
            allocsPrep += aPrep;
            ++counted;
            prep.push_back(tp);
            rec.push_back(std::chrono::duration<f64, std::milli>(t2 - t1).count());
            if (tm.gpuMeasured) gpu.push_back(tm.gpuTotalMs);
            intervals.push_back(std::chrono::duration<f64, std::milli>(t2 - last).count());
            m.created += r.pool_stats().createdThisFrame;
        }
        last = t2;
        m.passes = r.graph_stats().passesExecuted;
        m.culled = r.graph_stats().passesCulled;
        m.transient = r.graph_stats().transientTextures;
        m.physical = r.graph_stats().physicalTextures;
        m.aliased = r.graph_stats().aliasedTextures;
        m.layers = st.layersRendered;
    }
    backend.wait_idle();
    backend.destroy_texture(target);
    m.prepareMs = median(prep);
    m.recordMs = median(rec);
    m.gpuMs = median(gpu);
    m.allocsPerFrame = counted ? static_cast<f64>(allocs) / counted : 0.0;
    m.allocsPrepare = counted ? static_cast<f64>(allocsPrep) / counted : 0.0;
    if (!intervals.empty()) {
        f64 sum = 0;
        for (f64 v : intervals) sum += v;
        m.intervalMeanMs = sum / static_cast<f64>(intervals.size());
        f64 var = 0;
        for (f64 v : intervals) var += (v - m.intervalMeanMs) * (v - m.intervalMeanMs);
        m.intervalStdMs = std::sqrt(var / static_cast<f64>(intervals.size()));
        std::vector<f64> sorted = intervals;
        std::sort(sorted.begin(), sorted.end());
        m.intervalP99Ms = sorted[std::min(sorted.size() - 1, static_cast<usize>(static_cast<f64>(sorted.size()) * 0.99))];
    }
    return m;
}

// -----------------------------------------------------------------------------
// Backend falso: custo do motor na CPU, sem driver.
// -----------------------------------------------------------------------------
struct CpuRig {
    MockBackend* backend = new MockBackend();
    EffectRegistry effects;
    Renderer renderer;
    bool ok = false;
    CpuRig() {
        register_builtin_effects(effects);
        ok = renderer.initialize(*backend, effects).ok();
    }
    ~CpuRig() {
        renderer.shutdown();
        delete backend;
    }
};

} // namespace

// =============================================================================
// Medição (AUREA_BENCH=1)
// =============================================================================
AUREA_TEST(Perf8C, BenchCpuPrepareAndRecord) {
    if (!bench_enabled()) { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    std::printf("\n    CPU (backend falso): res camadas fx | prepare ms | gravacao ms | passes | aloc/quadro (no prepare) | criadas\n");
    for (const u32 w : {1920u, 3840u}) {
        for (const u32 n : {1u, 10u, 50u, 200u}) {
            for (const bool fx : {false, true}) {
                CpuRig rig;
                AUREA_CHECK(rig.ok);
                BenchScene s(w, w * 9 / 16);
                s.populate(rig.effects, n, fx);
                const FrameMeasure m = run_frames(rig.renderer, *rig.backend, s, 60, false, rig.backend);
                std::printf("    %4u %3u %s | %7.3f | %7.3f | %4u | %5.1f (%4.1f) | %u\n", w, n, fx ? "fx" : "--", m.prepareMs,
                            m.recordMs, m.passes, m.allocsPerFrame, m.allocsPrepare, m.created);
            }
        }
    }
}

#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(Perf8C, BenchGpuFrameAndPacing) {
    if (!bench_enabled()) { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    vk::Backend backend;
    BackendConfig cfg;
    cfg.enableValidation = false;   // medição: sem a camada (que nem existe neste host)
    cfg.framesInFlight = 2;
    if (!backend.initialize(cfg).ok()) { std::printf("    (sem GPU Vulkan)\n"); return; }
    EffectRegistry effects;
    register_builtin_effects(effects);
    Renderer renderer;
    AUREA_CHECK(renderer.initialize(backend, effects).ok());
    std::printf("\n    GPU %s: res camadas fx | prepare | gravacao | GPU ms | passes | intervalo media/desvio/p99 ms\n",
                backend.capabilities().deviceName.c_str());
    for (const u32 w : {1920u, 3840u}) {
        for (const u32 n : {1u, 10u, 50u}) {
            for (const bool fx : {false, true}) {
                BenchScene s(w, w * 9 / 16);
                s.populate(effects, n, fx);
                const FrameMeasure m = run_frames(renderer, backend, s, 60);
                std::printf("    %4u %2u %s | %6.2f | %6.2f | %6.2f | %4u | %6.2f / %5.2f / %6.2f\n", w, n, fx ? "fx" : "--",
                            m.prepareMs, m.recordMs, m.gpuMs, m.passes, m.intervalMeanMs, m.intervalStdMs, m.intervalP99Ms);
                renderer.release_project_resources();
            }
        }
    }
    renderer.shutdown();
    backend.shutdown();
}
#endif

// =============================================================================
// Contenção do lock do modelo: a UI lê o estado a cada vsync enquanto o
// render prepara quadros pesados (50 camadas com efeitos).
// =============================================================================
AUREA_TEST(Perf8C, ModelLockWaitDuringHeavyPlayback) {
    Engine e;
    EngineConfig ec;
    ec.backend = new MockBackend();
    ec.workerCount = 2;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, "lock").ok());
    const ImagePixels px = gradient(480, 270);
    EffectRegistry reg;
    register_builtin_effects(reg);
    std::vector<u64> ids;
    for (u32 i = 0; i < 50; ++i) {
        auto id = e.import_image(px.rgba.data(), px.width, px.height, "img");
        AUREA_CHECK(id.ok());
        if (id.ok()) ids.push_back(*id);
    }
    {
        Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        c->set_duration(FrameIndex{600});
        for (u64 raw : ids) {
            Layer* l = c->layer(LayerId::unpack(raw));
            l->end = FrameIndex{600};
            for (const char* key : {effect_keys::kExposure, effect_keys::kSaturation, effect_keys::kGaussianBlur}) {
                EffectInstance inst;
                inst.id = l->alloc_effect_id();
                inst.type = effect_type_id(key);
                initialize_instance(inst, *reg.params(inst.type));
                inst.params[0].constant.v[0] = 5.0f;
                l->effects.push_back(std::move(inst));
            }
        }
    }
    TextureDesc d;
    d.width = 1920;
    d.height = 1080;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    const TextureHandle target = *e.gpu()->create_texture(d);

    std::atomic<bool> done{false};
    std::vector<f64> renderMs;
    std::thread render([&] {
        for (u32 f = 0; f < 120; ++f) {
            Command seek;
            seek.type = CommandType::PlaybackSeek;
            seek.seek.time = tick_at(FrameIndex{static_cast<i64>(f)}, 30.0);
            (void)e.submit_commands(&seek, 1);
            const auto t0 = std::chrono::steady_clock::now();
            (void)e.render_offscreen(target, 1920, 1080, true);
            renderMs.push_back(ms_since(t0));
        }
        done = true;
    });
    std::vector<f64> statusWait;
    while (!done) {
        const auto t0 = std::chrono::steady_clock::now();
        (void)e.read_status();
        statusWait.push_back(ms_since(t0));
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    render.join();
    std::sort(statusWait.begin(), statusWait.end());
    const f64 p50 = statusWait[statusWait.size() / 2];
    const f64 p99 = statusWait[std::min(statusWait.size() - 1, statusWait.size() * 99 / 100)];
    const f64 mx = statusWait.back();
    std::printf("    render %.2f ms/quadro (mediana, backend falso); read_status espera p50 %.3f p99 %.3f max %.3f ms (%zu leituras)\n",
                median(renderMs), p50, p99, mx, statusWait.size());
    // A leitura de estado da UI nunca pode esperar um quadro inteiro de GPU:
    // o lock do modelo cobre só o prepare.
    AUREA_CHECK(mx < 250.0);
    e.gpu()->destroy_texture(target);
    e.shutdown();
}
