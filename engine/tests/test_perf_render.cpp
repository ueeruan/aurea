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
    f64 prepareMs = 0, recordMs = 0, gpuMs = 0, convMs = 0, fxMs = 0, compMs = 0;
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
    std::vector<f64> prep, rec, gpu, conv, fxv, compv, intervals;
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
            if (tm.gpuMeasured) {
                gpu.push_back(tm.gpuTotalMs);
                conv.push_back(tm.gpuColorConvMs);
                fxv.push_back(tm.gpuEffectsMs);
                compv.push_back(tm.gpuCompositeMs + tm.gpuOutputMs);
            }
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
    m.convMs = median(conv);
    m.fxMs = median(fxv);
    m.compMs = median(compv);
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
    std::printf("\n    GPU %s: res camadas fx | prepare | gravacao | GPU ms (fonte/efeitos/composicao) | passes | intervalo media/desvio/p99 ms\n",
                backend.capabilities().deviceName.c_str());
    for (const u32 w : {1920u, 3840u}) {
        for (const u32 n : {1u, 10u, 50u}) {
            for (const bool fx : {false, true}) {
                BenchScene s(w, w * 9 / 16);
                s.populate(effects, n, fx);
                const FrameMeasure m = run_frames(renderer, backend, s, 60);
                std::printf("    %4u %2u %s | %6.2f | %6.2f | %6.2f (%5.2f/%5.2f/%5.2f) | %4u | %6.2f / %5.2f / %6.2f\n", w, n,
                            fx ? "fx" : "--", m.prepareMs, m.recordMs, m.gpuMs, m.convMs, m.fxMs, m.compMs, m.passes,
                            m.intervalMeanMs, m.intervalStdMs, m.intervalP99Ms);
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
        for (u32 f = 0; f < 600; ++f) {
            Command seek;
            seek.type = CommandType::PlaybackSeek;
            seek.seek.time = tick_at(FrameIndex{static_cast<i64>(f % 600)}, 30.0);
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
    for (usize i = 0; i < statusWait.size(); ++i) {
        if (statusWait[i] > 1.0) std::printf("    [leitura %zu: %.3f ms]\n", i, statusWait[i]);
    }
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

// =============================================================================
// Preview AUTO 2.0 — séries de tempo sintéticas (determinístico).
// =============================================================================
namespace {

/// Custo sintético de um quadro em cada degrau de resolução (GPU) e CPU fixa.
struct CostModel {
    f32 gpu[4] = {0, 0, 0, 0};   ///< FULL, 1/2, 1/4, 1/8
    f32 cpu = 1.0f;
    f32 decode = 2.0f;
    bool gpuTimers = true;
    f32 heavyGpuFactor[3] = {1.0f, 1.0f, 1.0f};   ///< efeito das reduções sobre a GPU
    f32 heavyCpuFactor[3] = {1.0f, 1.0f, 1.0f};
};

u32 den_slot(u32 den) { return den >= 8 ? 3 : den >= 4 ? 2 : den >= 2 ? 1 : 0; }

FrameStats model_frame(const CostModel& m, u32 den, u32 heavy, u32 index, f32 jitter = 0.0f) {
    FrameStats s;
    s.frameIndex = index;
    const f32 g = m.gpu[den_slot(den)] * m.heavyGpuFactor[heavy] * (1.0f + jitter);
    s.gpuMs = m.gpuTimers ? g : 0.0f;
    s.cpuMs = m.gpuTimers ? m.cpu * m.heavyCpuFactor[heavy] : std::max(m.cpu * m.heavyCpuFactor[heavy], g);
    s.decodeMs = m.decode;
    s.passesExecuted = 40;
    s.layersRendered = 10;
    return s;
}

struct Run {
    std::vector<u32> dens;
    u32 changes = 0;
    u32 upProbes = 0;
};

/// Alimenta o controlador com `frames` quadros do modelo e grava a escala.
Run drive(AdaptiveResolutionController& c, const CostModel& m, u32 frames, u32 seed = 0, f32 noise = 0.0f,
          ThermalState thermal = ThermalState{}) {
    Run r;
    u32 rng = seed * 2654435761u + 1u;
    u32 prevDen = c.current_denominator();
    for (u32 i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const f32 j = noise * ((static_cast<f32>(rng >> 8) / 16777216.0f) * 2.0f - 1.0f);
        (void)c.update(model_frame(m, c.current_denominator(), c.state().heavyLevel, i + 1, j), thermal);
        const u32 d = c.current_denominator();
        if (d < prevDen) ++r.upProbes;
        prevDen = d;
        r.dens.push_back(d);
    }
    r.changes = c.change_count();
    return r;
}

/// O controlador da Fase 7 (limiares 80%/55%, 3 quadros para descer, 90
/// para subir, cooldown 30/60, sem previsão), reproduzido aqui só para
/// medir o ANTES na mesma série.
u32 legacy_changes(const CostModel& m, u32 frames) {
    u32 den = 1, over = 0, under = 0, cool = 0, changes = 0;
    f32 avg = 0.0f;
    for (u32 i = 0; i < frames; ++i) {
        const FrameStats s = model_frame(m, den, 0, i + 1);
        const f32 f = s.gpuMs > 0.0f ? std::max(s.cpuMs, s.gpuMs) : s.cpuMs;
        avg = avg <= 0.0f ? f : avg + (f - avg) * 0.15f;
        if (cool) --cool;
        const f32 budget = 1000.0f / 60.0f;
        if (avg > budget * 0.8f) { ++over; under = 0; }
        else if (avg < budget * 0.55f) { ++under; over = 0; }
        else { over = under = 0; }
        if (cool) continue;
        if (over >= 3 && den < 8) { den *= 2; cool = 30; over = under = 0; ++changes; }
        else if (under >= 90 && den > 1) { den /= 2; cool = 60; over = under = 0; ++changes; }
    }
    return changes;
}

} // namespace

AUREA_TEST(Perf8C, AutoSettlesOnceWhenFullIsTooExpensive) {
    // FULL custa 1,5× o orçamento de 60 fps; 1/2 cabe folgado. O degrau de
    // cima PREVISTO com a razão medida (≈2,8×) nunca cabe: uma troca só.
    DeviceCapabilities caps;
    AdaptiveResolutionController c(caps);
    c.configure(1920, 1080, 60.0f);
    CostModel m;
    m.gpu[0] = 25.0f; m.gpu[1] = 9.0f; m.gpu[2] = 3.5f; m.gpu[3] = 1.6f;
    m.cpu = 1.5f;
    const Run r = drive(c, m, 3000);
    const u32 legacy = legacy_changes(m, 3000);
    std::printf("    trocas em 3000 quadros: AUTO 2.0 %u, controlador antigo %u; razao medida FULL->1/2 %.2f\n",
                r.changes, legacy, c.measured_ratio(0));
    AUREA_CHECK_EQ(r.changes, 1u);
    AUREA_CHECK_EQ(c.current_denominator(), 2u);
    AUREA_CHECK(legacy > 10);   // o antigo oscilava FULL <-> 1/2 nesta série
    AUREA_CHECK_NEAR(c.measured_ratio(0), 25.0f / 9.0f, 0.1f);
}

AUREA_TEST(Perf8C, AutoBackoffBoundsFailedProbes) {
    // Sem timestamps de GPU (só CPU medida) e a previsão padrão otimista: cada
    // subida falha. A espera dobra a cada falha — as tentativas caem
    // geometricamente, e a sequência de escalas é estável e repetível.
    DeviceCapabilities caps;
    CostModel m;
    m.gpuTimers = false;
    m.gpu[0] = 15.5f; m.gpu[1] = 3.0f; m.gpu[2] = 1.5f; m.gpu[3] = 1.0f;
    m.cpu = 1.0f;
    AdaptiveResolutionController a(caps), b(caps);
    a.configure(1920, 1080, 60.0f);
    b.configure(1920, 1080, 60.0f);
    const Run ra = drive(a, m, 6000, 7, 0.10f);
    const Run rb = drive(b, m, 6000, 7, 0.10f);
    AUREA_CHECK(ra.dens == rb.dens);   // determinístico: mesma série, mesmas escalas
    // Intervalos entre tentativas: nunca encolhem.
    std::vector<u32> probeAt;
    for (u32 i = 1; i < ra.dens.size(); ++i) if (ra.dens[i] < ra.dens[i - 1]) probeAt.push_back(i);
    bool growing = true;
    for (usize i = 2; i < probeAt.size(); ++i) growing &= (probeAt[i] - probeAt[i - 1]) >= (probeAt[i - 1] - probeAt[i - 2]);
    const u32 legacy = legacy_changes(m, 6000);
    std::printf("    6000 quadros com ruido 10%%: %zu subidas (backoff %u), %u trocas; antigo %u trocas\n", probeAt.size(),
                a.state().upBackoff, ra.changes, legacy);
    AUREA_CHECK(growing);
    AUREA_CHECK(probeAt.size() <= 6);
    AUREA_CHECK(a.state().upBackoff >= 8);
    AUREA_CHECK(ra.changes < legacy);
}

AUREA_TEST(Perf8C, AutoNoisyBorderlineLoadDoesNotFlap) {
    // Carga no limite (≈80% do orçamento com ±25% de ruído): nenhuma troca
    // para cima e para baixo em sequência — no máximo uma descida.
    DeviceCapabilities caps;
    AdaptiveResolutionController c(caps);
    c.configure(1920, 1080, 60.0f);
    CostModel m;
    m.gpu[0] = 13.3f; m.gpu[1] = 5.0f; m.gpu[2] = 2.0f; m.gpu[3] = 1.0f;
    const Run r = drive(c, m, 5000, 3, 0.25f);
    std::printf("    limite com ruido: %u trocas, escala final 1/%u\n", r.changes, c.current_denominator());
    AUREA_CHECK(r.changes <= 1);
}

AUREA_TEST(Perf8C, AutoCpuBoundCutsReductionsBeforeResolution) {
    DeviceCapabilities caps;
    AdaptiveResolutionController c(caps);
    c.configure(1920, 1080, 60.0f);
    CostModel m;
    m.gpu[0] = 4.0f; m.gpu[1] = 2.0f; m.gpu[2] = 1.0f; m.gpu[3] = 0.5f;
    m.cpu = 20.0f;   // prepare pesado (desfoque de movimento, texto...)
    m.heavyCpuFactor[1] = 0.6f;
    m.heavyCpuFactor[2] = 0.4f;
    (void)drive(c, m, 400);
    std::printf("    CPU: degrau de reducoes %u, escala 1/%u, desfoque %.2f\n", c.state().heavyLevel, c.current_denominator(),
                c.quality().motionBlurSamples);
    AUREA_CHECK_EQ(c.current_denominator(), 1u);   // resolução intacta
    AUREA_CHECK(c.state().heavyLevel >= 1);
    AUREA_CHECK(c.quality().motionBlurSamples < 1.0f);
}

AUREA_TEST(Perf8C, AutoDecodeBoundKeepsResolution) {
    DeviceCapabilities caps;
    AdaptiveResolutionController c(caps);
    c.configure(1920, 1080, 30.0f);
    CostModel m;
    m.gpu[0] = 6.0f; m.gpu[1] = 3.0f; m.gpu[2] = 1.5f; m.gpu[3] = 1.0f;
    m.decode = 45.0f;   // o decoder não dá conta de 30 fps
    (void)drive(c, m, 600);
    AUREA_CHECK_EQ(c.current_denominator(), 1u);
    AUREA_CHECK(c.state().bottleneck == PreviewBottleneck::Decode);
}

AUREA_TEST(Perf8C, AutoRecoversWhenTheSceneGetsLighter) {
    DeviceCapabilities caps;
    AdaptiveResolutionController c(caps);
    c.configure(1920, 1080, 60.0f);
    CostModel heavy;
    heavy.gpu[0] = 30.0f; heavy.gpu[1] = 10.0f; heavy.gpu[2] = 4.0f; heavy.gpu[3] = 2.0f;
    (void)drive(c, heavy, 600);
    const u32 downDen = c.current_denominator();
    CostModel light;   // camadas apagadas
    light.gpu[0] = 6.0f; light.gpu[1] = 2.0f; light.gpu[2] = 1.0f; light.gpu[3] = 0.6f;
    (void)drive(c, light, 1500);
    std::printf("    pesada 1/%u -> leve 1/%u, %u trocas\n", downDen, c.current_denominator(), c.change_count());
    AUREA_CHECK(downDen >= 2);
    AUREA_CHECK_EQ(c.current_denominator(), 1u);
    AUREA_CHECK(c.change_count() <= 4);
}

AUREA_TEST(Perf8C, AutoMemoryAndThermalFloors) {
    DeviceCapabilities caps;
    CostModel m;
    m.gpu[0] = 3.0f; m.gpu[1] = 1.5f; m.gpu[2] = 1.0f; m.gpu[3] = 0.5f;
    {
        AdaptiveResolutionController c(caps);
        c.configure(1920, 1080, 60.0f);
        for (u32 i = 0; i < 100; ++i) {
            FrameStats s = model_frame(m, c.current_denominator(), 0, i + 1);
            s.memoryPressure = 0.95f;
            (void)c.update(s, ThermalState{});
        }
        AUREA_CHECK(c.current_denominator() >= 2);   // memória no limite: alvos menores
    }
    {
        AdaptiveResolutionController c(caps);
        c.configure(1920, 1080, 60.0f);
        ThermalState hot;
        hot.level = ThermalState::Level::Serious;
        (void)drive(c, m, 200, 0, 0.0f, hot);
        AUREA_CHECK_EQ(c.current_denominator(), 1u);            // quente: resolução fica
        AUREA_CHECK_NEAR(c.quality().particles, 0.5f, 1e-6f);   // reduções pela metade
        ThermalState critical;
        critical.level = ThermalState::Level::Critical;
        (void)drive(c, m, 5, 0, 0.0f, critical);
        AUREA_CHECK(c.current_denominator() >= 4);
        AUREA_CHECK_NEAR(c.quality().particles, 0.25f, 1e-6f);
        AUREA_CHECK_EQ(c.quality().ssao, 0.0f);
    }
    {
        // Piso de aparelho (8H): o AUTO nunca sobe acima dele.
        AdaptiveResolutionController c(caps);
        c.configure(1920, 1080, 60.0f);
        c.set_quality_floor(4, 1);
        (void)drive(c, m, 600);
        AUREA_CHECK_EQ(c.current_denominator(), 4u);
        AUREA_CHECK_NEAR(c.quality().blurSamples, 0.5f, 1e-6f);
    }
}

// =============================================================================
// Relógio: derruba quadro, nunca desacelera o vídeo (§9).
// =============================================================================
AUREA_TEST(Perf8C, SlowFramesDropNeverSlowTheClock) {
    PlaybackController pc;
    pc.configure(30.0, FrameIndex{1000});
    FrameScheduler sched;
    u64 now = 1'000'000'000ull;
    pc.play(now);
    // Cada quadro leva 100 ms para renderizar (3 quadros de 30 fps).
    FrameIndex last{0};
    for (u32 i = 0; i < 30; ++i) {
        now += 100'000'000ull;
        last = pc.update(now);
        sched.presented(last, pc.playing());
    }
    // 3 s de parede = quadro 90 da timeline, com 2 quadros derrubados por apresentação.
    AUREA_CHECK_EQ(last.value, static_cast<i64>(90));
    AUREA_CHECK_EQ(sched.dropped_total(), 29u * 2u);
    // Com o relógio de áudio como mestre, o vídeo segue o áudio (e não o render).
    struct FakeAudio final : MasterClock {
        i64 ns = 0;
        bool available() const noexcept override { return true; }
        i64 position_ns() const noexcept override { return ns; }
    } audio;
    PlaybackController pa;
    pa.configure(30.0, FrameIndex{1000});
    pa.clock().set_master(&audio);
    pa.play(now);
    audio.ns = 2'000'000'000;   // o som tocou 2 s, o render esteve travado
    AUREA_CHECK_EQ(pa.update(now + 5'000'000ull).value, static_cast<i64>(60));
}

// =============================================================================
// Export não muda com o preview (§8).
// =============================================================================
AUREA_TEST(Perf8C, ExportIgnoresPreviewReductions) {
    CpuRig rig;
    AUREA_CHECK(rig.ok);
    BenchScene s(1920, 1080);
    const LayerId p = s.comp->add_layer(LayerKind::ParticleSystem, "p");
    Layer* l = s.comp->layer(p);
    l->end = FrameIndex{600};
    l->particles.maxParticles = 1000;
    l->particles.rate = 4000.0f;
    l->particles.lifetime = 1.0f;
    RenderSettings preview;
    preview.quality = PreviewQuality::level(2);
    preview.heavyScale = 0.25f;
    RenderSettings exportRs = preview;
    exportRs.finalQuality = true;
    FrameSnapshot a, b;
    rig.renderer.prepare(*s.comp, s.project, FrameIndex{10}, nullptr, &BenchScene::lookup, &s, preview, 1, 0, DecodeMode::Still, 1.0f, a);
    rig.renderer.prepare(*s.comp, s.project, FrameIndex{10}, nullptr, &BenchScene::lookup, &s, exportRs, 2, 0, DecodeMode::Still, 1.0f, b);
    AUREA_CHECK_EQ(a.layers.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(b.layers.size(), static_cast<usize>(1));
    if (a.layers.size() == 1 && b.layers.size() == 1) {
        AUREA_CHECK_EQ(b.layers[0].source.particleSlots, 1000u);   // export: o que o projeto pede
        AUREA_CHECK_EQ(a.layers[0].source.particleSlots, 250u);    // preview reduzido
    }
    const PreviewQuality q = Renderer::effective_quality(exportRs);
    AUREA_CHECK(q.motionBlurSamples == 1.0f && q.flowResolution == 1.0f && q.ssao == 1.0f && q.shadowResolution == 1.0f
                && q.particles == 1.0f && q.blurSamples == 1.0f && q.lodBias == 0.0f);
}

// =============================================================================
// Pular trabalho (§19–22): contagem de decodes e passes.
// =============================================================================
AUREA_TEST(Perf8C, HiddenTransparentAndOffscreenLayersCostNothing) {
    CpuRig rig;
    AUREA_CHECK(rig.ok);
    BenchScene s(1280, 720);
    SyntheticConfig cfg;
    cfg.width = 160;
    cfg.height = 90;
    cfg.frameCount = 300;
    const LayerId visible = s.video(cfg, 640, 360);
    const LayerId hidden = s.video(cfg, 640, 360);
    const LayerId transparent = s.video(cfg, 640, 360);
    const LayerId offscreen = s.video(cfg, -2000, 360);
    const LayerId offBlur = s.video(cfg, -2000, 360);   // blur pode alcançar a tela: não é podada
    s.comp->layer(hidden)->visible = false;
    s.comp->layer(transparent)->transform.opacity = 0.0f;
    s.add_effect(rig.effects, offscreen, effect_keys::kExposure).params[0].constant.v[0] = 1.0f;   // cor não expande
    s.add_effect(rig.effects, offBlur, effect_keys::kGaussianBlur).params[0].constant.v[0] = 4.0f;
    (void)visible;
    RenderSettings rs;
    FrameSnapshot snap;
    for (u32 f = 0; f < 3; ++f) {
        rig.renderer.prepare(*s.comp, s.project, FrameIndex{f}, &s.media, &BenchScene::lookup, &s, rs, 10 + f, 1,
                             DecodeMode::Playback, 1.0f, snap);
        for (RenderLayer& l : snap.layers) l.source.frame.reset();
    }
    std::printf("    5 videos: %u decoders abertos, %u podada(s) fora da tela\n", s.factory->opened, snap.culledLayers);
    AUREA_CHECK_EQ(s.factory->opened, 2u);   // a visível e a com blur perto da borda
    AUREA_CHECK_EQ(snap.culledLayers, 1u);
}

AUREA_TEST(Perf8C, DisabledIdentityAndFusedEffectsPassCount) {
    CpuRig rig;
    AUREA_CHECK(rig.ok);
    auto passes = [&](auto&& setup) {
        BenchScene s(640, 360);
        const AssetId a = s.image_asset(160, 90);
        const LayerId id = s.image(a, 320, 180, 1.0f);
        setup(s, id);
        const FrameMeasure m = run_frames(rig.renderer, *rig.backend, s, 6, false, rig.backend);
        rig.renderer.release_project_resources();
        return m.passes;
    };
    const u32 base = passes([](BenchScene&, LayerId) {});
    const u32 neutral = passes([&](BenchScene& s, LayerId id) {
        s.add_effect(rig.effects, id, effect_keys::kGaussianBlur).params[0].constant.v[0] = 0.0f;   // raio 0
        s.add_effect(rig.effects, id, effect_keys::kExposure);                                       // exposição 0
        s.add_effect(rig.effects, id, effect_keys::kGlow).enabled = false;                           // desligado
    });
    const u32 fused = passes([&](BenchScene& s, LayerId id) {
        s.add_effect(rig.effects, id, effect_keys::kExposure).params[0].constant.v[0] = 0.5f;
        EffectInstance& bc = s.add_effect(rig.effects, id, effect_keys::kBrightnessContrast);
        bc.params[1].constant.v[0] = 25.0f;
        s.add_effect(rig.effects, id, effect_keys::kSaturation).params[0].constant.v[0] = -30.0f;
        s.add_effect(rig.effects, id, effect_keys::kTint).params[2].constant.v[0] = 40.0f;
        s.add_effect(rig.effects, id, effect_keys::kColorMatrix).params[0].constant.v[0] = 0.8f;
        s.add_effect(rig.effects, id, effect_keys::kLevels).params[2].constant.v[0] = 1.2f;
    });
    std::printf("    passes: camada %u; +blur 0/exposicao 0/glow desligado %u; +6 efeitos de cor %u (sem fusao seriam %u)\n",
                base, neutral, fused, base + 6);
    AUREA_CHECK_EQ(neutral, base);
    AUREA_CHECK_EQ(fused, base + 1);
}

// =============================================================================
// FrameGraph em regime (§24–28): nada criado, texturas reaproveitadas, zero
// alocação no caminho quente.
// =============================================================================
AUREA_TEST(Perf8C, SteadyPlaybackOf50LayersCreatesAndAllocatesNothing) {
    CpuRig rig;
    AUREA_CHECK(rig.ok);
    BenchScene s(1920, 1080);
    s.populate(rig.effects, 50, true);
    const u32 before = rig.backend->texturesCreated;
    const FrameMeasure warm = run_frames(rig.renderer, *rig.backend, s, 8, false, rig.backend);
    const u32 afterWarm = rig.backend->texturesCreated;
    const FrameMeasure m = run_frames(rig.renderer, *rig.backend, s, 40, false, rig.backend);
    (void)warm;
    // run_frames cria e destrói o próprio alvo de saída (1 criação por chamada).
    std::printf("    50 camadas+fx: %u passes, %u transitorias em %u fisicas (%u reaproveitadas), texturas criadas no aquecimento %u, "
                "em regime %u (alvo do teste: 1), alocacoes/quadro %.1f\n",
                m.passes, m.transient, m.physical, m.aliased, afterWarm - before, rig.backend->texturesCreated - afterWarm,
                m.allocsPerFrame);
    AUREA_CHECK_EQ(m.created, 0u);
    AUREA_CHECK_EQ(rig.backend->texturesCreated - afterWarm, 1u);
    AUREA_CHECK(m.aliased > 0);
    AUREA_CHECK(m.physical < m.transient);
    AUREA_CHECK_EQ(m.allocsPerFrame, 0.0);
}

// =============================================================================
// Render sob demanda (§39): parado e sem mudança, nenhum quadro.
// =============================================================================
AUREA_TEST(Perf8C, PausedWithoutChangesPresentsNothing) {
    auto* mock = new MockBackend();
    Engine e;
    EngineConfig ec;
    ec.backend = mock;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "ocioso").ok());
    int dummyWindow = 0;
    AUREA_CHECK(e.attach_surface(&dummyWindow, 640, 360).ok());
    AUREA_CHECK(e.render_frame(true).ok());
    const u32 first = mock->presents;
    for (u32 i = 0; i < 20; ++i) (void)e.render_frame(true);
    AUREA_CHECK_EQ(mock->presents, first);   // nada mudou: nada apresentado
    // A thread de render ociosa: no máximo UM quadro (o refino, quando o AUTO
    // começou reduzido) e depois nada.
    e.start_render_thread();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    const u32 afterIdle = mock->presents;
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    e.stop_render_thread();
    std::printf("    parado: %u apresentacao(oes) em 21 chamadas + 2 s de thread ociosa (refino: %u)\n",
                mock->presents, afterIdle - first);
    AUREA_CHECK(afterIdle <= first + 1);
    AUREA_CHECK_EQ(mock->presents, afterIdle);   // o refino não se repete
    const u32 base = mock->presents;
    // Uma mudança (seek) desenha exatamente um quadro.
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{5}, 30.0);
    AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
    (void)e.render_frame(true);
    (void)e.render_frame(true);
    AUREA_CHECK_EQ(mock->presents, base + 1);
    e.shutdown();
}

// =============================================================================
// Vulkan do host (§140–142): validação limpa, regime sem criação, e a imagem
// em cache dá EXATAMENTE os pixels da conversão feita no quadro.
// =============================================================================
#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(Perf8C, VulkanCachedImageIsPixelExactAndValidationClean) {
    vk::Backend backend;
    BackendConfig cfg;
    cfg.enableValidation = true;
    cfg.framesInFlight = 2;
    if (!backend.initialize(cfg).ok()) { std::printf("    (sem GPU Vulkan: pulado)\n"); return; }
    const u32 errors0 = vk::Backend::validation_errors();
    EffectRegistry effects;
    register_builtin_effects(effects);
    Renderer renderer;
    AUREA_CHECK(renderer.initialize(backend, effects).ok());

    BenchScene s(320, 180);
    const AssetId a = s.image_asset(160, 90);
    s.image(a, 100, 90, 1.3f, 12.0f);
    s.image(a, 220, 90, 1.3f, -7.0f);   // a mesma imagem, mesma densidade: uma conversão
    TextureDesc d;
    d.width = 320;
    d.height = 180;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.sampled = true;
    d.transferSrc = true;
    const TextureHandle target = *backend.create_texture(d);
    auto frame = [&](u64 n) {
        RenderSettings rs;
        rs.dither = false;
        FrameSnapshot snap;
        renderer.prepare(*s.comp, s.project, FrameIndex{0}, nullptr, &BenchScene::lookup, &s, rs, n, 0, DecodeMode::Still, 1.0f, snap);
        OffscreenTarget off{target, 320, 180};
        FrameStats st;
        RenderTimings tm;
        AUREA_CHECK(renderer.render(snap, rs, &off, st, tm).ok());
        backend.wait_idle();
        std::vector<u16> half(320u * 180u * 4u);
        AUREA_CHECK(backend.read_texture(target, half.data(), 320 * 8).ok());
        return half;
    };
    const std::vector<u16> built = frame(1);
    u32 hits = 0, builds = 0;
    renderer.image_cache_stats(hits, builds);
    const u32 builds1 = builds;
    const std::vector<u16> cached = frame(2);
    renderer.image_cache_stats(hits, builds);
    u32 diff = 0;
    for (usize i = 0; i < built.size(); ++i) diff += built[i] != cached[i] ? 1u : 0u;
    std::printf("    conversoes %u (2 camadas, 1 imagem), acertos %u, texels diferentes %u; validacao %s, erros %u\n", builds1, hits,
                diff, backend.capabilities().validationEnabled ? "LIGADA" : "ausente no host", vk::Backend::validation_errors() - errors0);
    AUREA_CHECK_EQ(builds1, 1u);
    AUREA_CHECK_EQ(builds, 1u);   // o segundo quadro não converteu de novo
    AUREA_CHECK(hits >= 3);
    AUREA_CHECK_EQ(diff, 0u);
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors0, 0u);
    backend.destroy_texture(target);
    renderer.shutdown();
    backend.shutdown();
}
#endif

// Preview parado com o AUTO reduzido (aparelho fraco começa em 1/4): depois de
// 250 ms sem nada acontecer sai UM quadro na melhor resolução permitida.
AUREA_TEST(Perf8C, PausedReducedPreviewRefinesOnce) {
    auto* mock = new MockBackend();
    Engine e;
    EngineConfig ec;
    ec.backend = mock;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, "refino").ok());
    int dummyWindow = 0;
    AUREA_CHECK(e.attach_surface(&dummyWindow, 1920, 1080).ok());
    // Quadros pesados medidos: o AUTO desce a resolução.
    for (u32 i = 0; i < 6; ++i) {
        FrameStats heavy;
        heavy.cpuMs = 120.0f;
        heavy.gpuMs = 120.0f;
        heavy.passesExecuted = 10;
        heavy.layersRendered = 1;
        e.debug_feed_frame_stats(heavy);
    }
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{3}, 30.0);
    AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
    const u32 den = e.read_telemetry().previewDenominator;
    AUREA_CHECK(e.render_frame(true).ok());
    const u32 first = mock->presents;
    e.start_render_thread();
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    e.stop_render_thread();
    std::printf("    AUTO em 1/%u: %u quadro(s) de refino parado\n", den, mock->presents - first);
    if (den > 1) AUREA_CHECK_EQ(mock->presents, first + 1);
    else AUREA_CHECK_EQ(mock->presents, first);
    AUREA_CHECK(den > 1);
    AUREA_CHECK_EQ(e.read_telemetry().previewDenominator, den);   // o refino não mexe no AUTO
    e.shutdown();
}
