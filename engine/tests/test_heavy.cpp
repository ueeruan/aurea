// =============================================================================
//  Fase 8E — sistemas pesados: contadores (testes normais) e benchmarks
//  (AUREA_BENCH=1). Os benchmarks imprimem as tabelas do relatório
//  docs/performance/PHASE_8_REPORT.md §8E; os testes normais travam o que o
//  relatório promete (atlas sem upload em regime, vetor/máscara sem refazer,
//  instancing, knob de sombra, export intocado pelo preview).
//
//  Golden/diff dos efeitos: AUREA_HEAVY_DUMP=<pasta> grava a saída de EXPORT
//  (qualidade cheia) de cada efeito do catálogo; AUREA_HEAVY_REF=<pasta>
//  compara com uma gravação anterior (diferença máxima e média, 8 bits sRGB).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/tracking/PointTracker.hpp"

// Contadores e botões da 8E (o mesmo arquivo compila na base anterior para o A/B).
#if __has_include("aurea/render/HeavyQuality.hpp")
#define AUREA_HEAVY_V2 1
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;

namespace {
bool heavy_bench() { const char* v = std::getenv("AUREA_BENCH"); return v && *v == '1'; }
f64 ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
f64 median(std::vector<f64> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
} // namespace

// -----------------------------------------------------------------------------
// Tracking (§83): precisão × tempo por resolução de análise. CPU pura.
// Imagem texturizada sintética movida por deslocamentos subpixel conhecidos;
// o erro é medido em pixels da resolução CHEIA (1080p).
// -----------------------------------------------------------------------------
namespace {
f32 tex_value(f32 x, f32 y) {
    // Soma de senos em várias escalas + manchas: textura rica e sem período curto.
    return 0.5f + 0.18f * std::sin(x * 0.071f + y * 0.023f) + 0.14f * std::sin(x * 0.019f - y * 0.083f)
         + 0.1f * std::sin((x + y) * 0.137f) + 0.08f * std::cos(x * 0.29f) * std::sin(y * 0.31f);
}
tracking::Gray make_frame(u32 w, u32 h, f32 scale, Vec2 shift) {
    tracking::Gray g;
    g.width = w;
    g.height = h;
    g.px.resize(static_cast<usize>(w) * h);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            // Amostra a cena "cheia" (1920×1080) com caixa 2×2 para não serrilhar.
            f32 s = 0;
            for (int k = 0; k < 4; ++k) {
                const f32 fx = (static_cast<f32>(x) + 0.25f + 0.5f * static_cast<f32>(k & 1)) / scale - shift.x;
                const f32 fy = (static_cast<f32>(y) + 0.25f + 0.5f * static_cast<f32>(k >> 1)) / scale - shift.y;
                s += tex_value(fx, fy);
            }
            g.px[static_cast<usize>(y) * w + x] = s * 0.25f;
        }
    }
    return g;
}
} // namespace

AUREA_TEST(Heavy, BenchTrackingPrecisionVersusResolution) {
    if (!heavy_bench()) { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    // 30 quadros, movimento de (2,3; −1,7) px/quadro em 1080p, 12 pontos.
    const Vec2 step{2.3f, -1.7f};
    std::printf("    | Análise | Erro médio (px 1080p) | Erro máx | Tempo por quadro (12 pontos) |\n");
    for (u32 h : {1080u, 720u, 360u, 240u}) {
        const f32 s = static_cast<f32>(h) / 1080.0f;
        const u32 w = static_cast<u32>(std::lround(1920.0f * s));
        std::vector<Vec2> pts;
        for (int i = 0; i < 12; ++i) pts.push_back(Vec2{400.0f + 100.0f * static_cast<f32>(i % 4), 300.0f + 150.0f * static_cast<f32>(i / 4)});
        std::vector<Vec2> pos;
        for (Vec2 p : pts) pos.push_back(Vec2{p.x * s, p.y * s});
        tracking::Gray prev = make_frame(w, h, s, Vec2{0, 0});
        f64 err = 0, errMax = 0, tms = 0;
        u32 n = 0;
        for (int f = 1; f <= 30; ++f) {
            const Vec2 shift{step.x * static_cast<f32>(f), step.y * static_cast<f32>(f)};
            tracking::Gray cur = make_frame(w, h, s, shift);
            const auto t0 = std::chrono::steady_clock::now();
            for (Vec2& p : pos) p = tracking::track_step(prev, cur, p).pos;
            tms += ms_since(t0);
            for (usize k = 0; k < pos.size(); ++k) {
                const f32 ex = pos[k].x / s - (pts[k].x + shift.x), ey = pos[k].y / s - (pts[k].y + shift.y);
                const f64 e = std::sqrt(static_cast<f64>(ex * ex + ey * ey));
                err += e;
                errMax = std::max(errMax, e);
                ++n;
            }
            prev = std::move(cur);
        }
        // Conversão RGBA → cinza do quadro na resolução de análise (o custo
        // que cresce com a resolução; o passo do NCC não depende dela).
        std::vector<u8> rgba(static_cast<usize>(w) * h * 4, 128);
        const auto tg = std::chrono::steady_clock::now();
        for (int k = 0; k < 5; ++k) (void)tracking::to_gray(rgba.data(), w, h);
        const f64 gray = ms_since(tg) / 5.0;
        std::printf("    | %up | %.3f | %.3f | %.3f ms | cinza %.3f ms |\n", h, err / n, errMax, tms / 30.0, gray);
    }
}

#if defined(AUREA_HEAVY_V2)
#include "aurea/vector/Vector.hpp"
// §66: caminho com morph fora do intervalo dos keys é o do key da ponta — o
// hash (a chave do cache da malha) não muda e a camada não retriangula.
AUREA_TEST(Heavy, VectorMorphHashIsStableOutsideTheKeyRange) {
    VectorGroup g;
    VectorPath p;
    PathKey a, b;
    a.frame = 10;
    a.path = vector::make_rect(Vec2{0, 0}, Vec2{10, 10}, 0.0f);
    b.frame = 20;
    b.path = vector::make_ellipse(Vec2{5, 5}, Vec2{20, 20});
    p.keys = {a, b};
    g.paths.push_back(p);
    const std::vector<VectorGroup> gs{g};
    AUREA_CHECK_EQ(vector::content_hash(gs, 0.0), vector::content_hash(gs, 9.0));
    AUREA_CHECK_EQ(vector::content_hash(gs, 20.0), vector::content_hash(gs, 300.0));
    AUREA_CHECK(vector::content_hash(gs, 12.0) != vector::content_hash(gs, 15.0));
}
#endif

#if defined(AUREA_TEST_VULKAN)

#include "ImageIO.hpp"
#include "SyntheticVideo.hpp"
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/scene3d/Importer.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/vector/Vector.hpp"

#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>

using namespace aurea::test;

namespace {

f32 h2f(u16 h) {
    const u32 sign = (h & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1F, mant = h & 0x3FF, bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) { mant <<= 1; --exp; }
            bits = sign | (exp << 23) | ((mant & 0x3FF) << 13);
        }
    } else if (exp == 31) bits = sign | 0x7F800000u | (mant << 13);
    else bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}
u8 enc8(f32 c) {
    c = std::fmax(0.0f, std::fmin(1.0f, c));
    const f32 e = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(std::lround(e * 255.0f));
}

struct HGpu {
    vk::Backend backend;
    EffectRegistry effects;
    Renderer renderer;
    bool ok = false;
    std::unordered_map<u64, TextureHandle> targets;
    HGpu() {
        BackendConfig cfg;
        cfg.enableValidation = false;   // medição: sem a camada de validação no caminho
        cfg.framesInFlight = 2;
        if (!backend.initialize(cfg).ok()) return;
        register_builtin_effects(effects);
        ok = renderer.initialize(backend, effects).ok();
    }
    ~HGpu() {
        backend.wait_idle();
        for (auto& [k, t] : targets) backend.destroy_texture(t);
        renderer.shutdown();
        backend.shutdown();
    }
    TextureHandle target(u32 w, u32 h) {
        const u64 key = (static_cast<u64>(w) << 32) | h;
        if (auto it = targets.find(key); it != targets.end()) return it->second;
        TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.sampled = true;
        d.transferSrc = true;
        auto t = backend.create_texture(d);
        targets[key] = *t;
        return *t;
    }
};
HGpu& hgpu() {
    static HGpu g;
    return g;
}
#define HEAVY_REQUIRE_GPU()                                   \
    do {                                                      \
        if (!hgpu().ok) {                                     \
            std::printf("(sem GPU Vulkan: pulado) ");         \
            return;                                           \
        }                                                     \
    } while (0)

/// Custo de GPU de um quadro, por grupo de passes (rótulos estáticos).
struct PassCost {
    f64 total = 0, effects = 0, source = 0, composite = 0, output = 0;
    std::vector<std::pair<std::string, f64>> passes;
};
PassCost read_cost(GPUBackend& b) {
    GpuTiming t[256];
    f32 total = 0;
    const u32 n = b.read_gpu_timings(t, 256, &total);
    PassCost c;
    c.total = total;
    for (u32 i = 0; i < n; ++i) {
        const char* s = t[i].label ? t[i].label : "?";
        auto starts = [s](const char* p) { return std::strncmp(s, p, std::strlen(p)) == 0; };
        if (starts("cor-do-video") || starts("imagem") || starts("solido") || starts("layer-")) c.source += t[i].ms;
        else if (starts("composicao")) c.composite += t[i].ms;
        else if (starts("saida") || starts("export-")) c.output += t[i].ms;
        else c.effects += t[i].ms;
        bool found = false;
        for (auto& [name, ms] : c.passes) if (name == s) { ms += t[i].ms; found = true; }
        if (!found) c.passes.emplace_back(s, t[i].ms);
    }
    return c;
}

ImagePixels plate(u32 w, u32 h) {
    ImagePixels px;
    px.width = w;
    px.height = h;
    px.rgba.resize(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
            p[0] = static_cast<u8>(x * 255 / (w - 1));
            p[1] = static_cast<u8>(y * 255 / (h - 1));
            p[2] = 64;
            p[3] = 255;
            const f32 dx = static_cast<f32>(x) - w * 0.5f, dy = static_cast<f32>(y) - h * 0.5f;
            if (dx * dx + dy * dy < (h * 0.25f) * (h * 0.25f)) p[0] = p[1] = p[2] = 255;
            if (x % 32 == 0 || y % 32 == 0) p[0] = p[1] = p[2] = 20;
        }
    }
    return px;
}

/// Composição direta no Renderer (sem Engine): imagens em memória.
struct HScene {
    Project project;
    Composition* comp = nullptr;
    std::unordered_map<u64, ImagePixels> images;
    std::unique_ptr<SyntheticFactory> factory;
    MediaManager media;
    HScene(u32 w, u32 h) {
        auto p = Project::create_new(w, h, 30.0, "heavy");
        project = std::move(*p);
        comp = project.timeline().composition(project.timeline().root());
        comp->set_background(Color{0, 0, 0, 1});
        comp->set_duration(FrameIndex{600});
        hgpu().renderer.release_project_resources();
    }
    static const ImagePixels* lookup(void* self, AssetId id) {
        auto* s = static_cast<HScene*>(self);
        auto it = s->images.find(id.pack());
        return it == s->images.end() ? nullptr : &it->second;
    }
    LayerId image(ImagePixels px) {
        Asset a;
        a.kind = AssetKind::Image;
        const AssetId aid = project.add_asset(std::move(a));
        const u32 w = px.width, h = px.height;
        images[aid.pack()] = std::move(px);
        const LayerId id = comp->add_layer(LayerKind::Image, "img");
        Layer* l = comp->layer(id);
        l->source = aid;
        l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0};
        l->transform.position = Vec3{comp->width() * 0.5f, comp->height() * 0.5f, 0};
        return id;
    }
    LayerId video(const SyntheticConfig& cfg) {
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
        l->transform.position = Vec3{comp->width() * 0.5f, comp->height() * 0.5f, 0};
        return id;
    }
    /// Um quadro fora da tela; devolve o tempo de CPU (prepare + gravação).
    f64 frame(FrameIndex t, const RenderSettings& rs, u32 w, u32 h, RenderTimings* outT = nullptr) {
        HGpu& g = hgpu();
        static u64 frameNo = 0;
        FrameSnapshot snap;
        const auto t0 = std::chrono::steady_clock::now();
        f64 prep = 0;
        for (int attempt = 0; attempt < 1500; ++attempt) {
            const auto ta = std::chrono::steady_clock::now();
            g.renderer.prepare(*comp, project, t, factory ? &media : nullptr, &HScene::lookup, this, rs, ++frameNo, 0, DecodeMode::Still,
                               1.0f, snap);
            prep = ms_since(ta);
            if (snap.missingVideoFrames == 0 && snap.staleVideoFrames == 0) break;
            for (RenderLayer& rl : snap.layers) rl.source.frame.reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        (void)t0;
        OffscreenTarget off{g.target(w, h), w, h};
        FrameStats stats;
        RenderTimings timings;
        (void)g.renderer.render(snap, rs, &off, stats, timings);
        if (outT) *outT = timings;
        return prep + timings.cpuRecordMs;
    }
    /// Lê o alvo (RGBA16F linear) em sRGB 8 bits.
    Image8 read(u32 w, u32 h) {
        HGpu& g = hgpu();
        g.backend.wait_idle();
        std::vector<u16> half(static_cast<usize>(w) * h * 4);
        Image8 img;
        img.width = w;
        img.height = h;
        img.rgba.resize(half.size());
        if (!g.backend.read_texture(g.target(w, h), half.data(), w * 8).ok()) return img;
        for (usize i = 0; i < half.size(); i += 4) {
            for (int c = 0; c < 3; ++c) img.rgba[i + c] = enc8(h2f(half[i + c]));
            img.rgba[i + 3] = 255;
        }
        return img;
    }
};

/// Mede N quadros iguais: a leitura de GPU é a de 2 quadros atrás (anel de
/// 2 em voo) — o mesmo conteúdo, então a mediana é o custo em regime.
struct Measured {
    f64 gpuTotal = 0, gpuEffects = 0, cpu = 0;
    PassCost last;
    std::vector<PassCost> all;
    /// Mediana, entre os quadros medidos, da soma dos passes com este nome.
    [[nodiscard]] f64 pass(const char* name) const {
        std::vector<f64> v;
        for (const PassCost& c : all) {
            f64 t = 0;
            for (auto& [n, ms] : c.passes) if (n == name) t += ms;
            v.push_back(t);
        }
        return median(v);
    }
};
Measured measure(HScene& s, const RenderSettings& rs, u32 w, u32 h, u32 frames = 8, FrameIndex t = FrameIndex{30}) {
    std::vector<f64> tot, fx, cpu;
    Measured m;
    for (u32 i = 0; i < frames + 3; ++i) {
        const f64 c = s.frame(t, rs, w, h);
        if (i < 3) continue;
        const PassCost pc = read_cost(hgpu().backend);
        tot.push_back(pc.total);
        fx.push_back(pc.effects);
        cpu.push_back(c);
        m.last = pc;
        m.all.push_back(pc);
    }
    hgpu().backend.wait_idle();
    m.gpuTotal = median(tot);
    m.gpuEffects = median(fx);
    m.cpu = median(cpu);
    return m;
}

} // namespace

// -----------------------------------------------------------------------------
// Efeitos (§86–91): custo de GPU por efeito do catálogo, 1080p, com os valores
// de demonstração (os mesmos da prévia). Export (qualidade cheia) e preview
// adaptativo com a escala pesada em 0,5.
// -----------------------------------------------------------------------------
AUREA_TEST(Heavy, BenchEveryCatalogEffectAt1080p) {
    HEAVY_REQUIRE_GPU();
    const char* dump = std::getenv("AUREA_HEAVY_DUMP");
    const char* ref = std::getenv("AUREA_HEAVY_REF");
    const bool bench = heavy_bench();
    if (!bench && !dump && !ref) { std::printf("    (pulado: AUREA_BENCH=1 / AUREA_HEAVY_DUMP / AUREA_HEAVY_REF)\n"); return; }
    HGpu& g = hgpu();
    const u32 W = 1920, H = 1080;
    RenderSettings full;
    full.dither = false;
    full.finalQuality = true;
    RenderSettings prev = full;
    prev.finalQuality = false;
    prev.heavyScale = 0.5f;
    f64 base = 0;
    {
        HScene s(W, H);
        (void)s.image(plate(W, H));
        base = measure(s, full, W, H).gpuTotal;
    }
    if (bench) {
        std::printf("    base (imagem 1080p + composição + saída): %.3f ms GPU\n", base);
        std::printf("    | Efeito | Chave | GPU export (ms) | GPU preview 0,5 (ms) | Passes | CPU (ms) |\n");
    }
    u32 worst = 0;
    f64 worstMs = 0;
    for (u32 i = 0; i < g.effects.count(); ++i) {
        const Effect& fx = g.effects.at(i);
        const ParameterRegistry& params = g.effects.params_at(i);
        HScene s(W, H);
        const LayerId id = s.image(plate(W, H));
        Layer* l = s.comp->layer(id);
        EffectInstance e;
        e.id = l->alloc_effect_id();
        e.type = fx.type_id();
        initialize_instance(e, params);
        std::vector<ParamValue> values(params.count());
        for (u32 p = 0; p < params.count(); ++p) values[p] = e.params[p].constant;
        if (fx.demo_values(e, values)) for (u32 p = 0; p < params.count(); ++p) e.params[p].constant = values[p];
        l->effects.push_back(std::move(e));
        std::string fname = fx.info().key;
        for (char& c : fname) if (c == '.') c = '_';
        if (dump || ref) {
            // AUREA_HEAVY_ONLY=<chave>: só um efeito; AUREA_HEAVY_FULLRES=1: diff em 1080p.
            const char* only = std::getenv("AUREA_HEAVY_ONLY");
            if (only && !std::strstr(fx.info().key, only)) continue;
            const bool fullRes = std::getenv("AUREA_HEAVY_FULLRES") != nullptr;
            const u32 dw = fullRes ? W : 480, dh = fullRes ? H : 270;
            (void)s.frame(FrameIndex{30}, full, dw, dh);
            const Image8 img = s.read(dw, dh);
            if (dump) (void)write_png(std::string(dump) + "/" + fname + ".png", img);
            if (ref) {
                Image8 want;
                if (read_png(std::string(ref) + "/" + fname + ".png", want) && want.rgba.size() == img.rgba.size()) {
                    u32 mx = 0;
                    f64 sum = 0;
                    for (usize k = 0; k < img.rgba.size(); ++k) {
                        const u32 d = static_cast<u32>(std::abs(static_cast<int>(img.rgba[k]) - static_cast<int>(want.rgba[k])));
                        mx = std::max(mx, d);
                        sum += d;
                    }
                    const f64 mean = sum / static_cast<f64>(img.rgba.size());
                    if (mx > 0) std::printf("    diff export %-28s max %3u media %.4f\n", fx.info().key, mx, mean);
                    if (mx > 3) {
                        (void)write_png(std::string(ref) + "/" + fname + "_obtido.png", img);
                        u32 px = 0;
                        for (usize k = 0; k < img.rgba.size(); k += 4) {
                            u32 d = 0;
                            for (int c = 0; c < 3; ++c) d = std::max<u32>(d, static_cast<u32>(std::abs(img.rgba[k + c] - want.rgba[k + c])));
                            if (d > 3) {
                                if (px < 5) std::printf("      (%zu,%zu) obtido %u,%u,%u esperado %u,%u,%u\n", (k / 4) % img.width, (k / 4) / img.width,
                                                        img.rgba[k], img.rgba[k + 1], img.rgba[k + 2], want.rgba[k], want.rgba[k + 1], want.rgba[k + 2]);
                                ++px;
                            }
                        }
                        std::printf("      %u pixels acima de 3/255\n", px);
                    }
                    // Critério: o dos golden frames (máx. 3, média 0,35) OU, para
                    // efeito com curvas de nível duras (estouro do brilho), média
                    // ≤ 0,35 com no máximo 2% dos pixels acima de 3/255.
                    u32 over = 0;
                    for (usize k = 0; k < img.rgba.size(); ++k)
                        over += std::abs(static_cast<int>(img.rgba[k]) - static_cast<int>(want.rgba[k])) > 3 ? 1u : 0u;
                    const f64 frac = static_cast<f64>(over) / static_cast<f64>(img.rgba.size());
                    if (mx > 3) std::printf("      fração de canais acima de 3/255: %.3f%%\n", frac * 100.0);
                    AUREA_CHECK_MSG((mx <= 3 && mean <= 0.35) || (mean <= 0.35 && frac <= 0.02), fx.info().key);
                }
            }
        }
        if (!bench) continue;
        const Measured mf = measure(s, full, W, H);
        const Measured mp = measure(s, prev, W, H);
        u32 passes = 0;
        for (auto& p : mf.last.passes) {
            if (p.first.rfind("imagem", 0) == 0 || p.first.rfind("composicao", 0) == 0 || p.first.rfind("saida", 0) == 0) continue;
            ++passes;
        }
        // Custo do efeito = mediana da soma dos PASSES dele (sem a imagem, a
        // composição e a saída) — não "total − base", que sofre com o clock da
        // GPU subindo e descendo entre as medições.
        const f64 cost = mf.gpuEffects, costP = mp.gpuEffects;
        std::printf("    | %s | %s | %.3f | %.3f | %u | %.2f |\n", fx.info().name, fx.info().key, cost, costP, passes, mf.cpu);
        if (const char* v = std::getenv("AUREA_HEAVY_VERBOSE"); v && std::strstr(fx.info().key, v)) {
            for (auto& [name, ms] : mf.last.passes) std::printf("      passe %s %.3f\n", name.c_str(), ms);
        }
        if (cost > worstMs) { worstMs = cost; worst = i; }
    }
    if (bench) std::printf("    mais caro: %s (%.3f ms)\n", g.effects.at(worst).info().key, worstMs);
}

// -----------------------------------------------------------------------------
// Partículas (§76–79): 10K/100K/500K/1M, 1080p, GPU e CPU.
// -----------------------------------------------------------------------------
AUREA_TEST(Heavy, BenchParticles) {
    HEAVY_REQUIRE_GPU();
    if (!heavy_bench()) { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    std::printf("    | Partículas | Blend | GPU export (ms) | GPU preview 0,5 (ms) | CPU (ms) |\n");
    for (u32 blend : {1u, 0u}) {
        for (u32 count : {10000u, 100000u, 500000u, 1000000u}) {
            HScene s(1920, 1080);
            const LayerId id = s.comp->add_layer(LayerKind::ParticleSystem, "p");
            Layer* l = s.comp->layer(id);
            ParticleData& p = l->particles;
            p.maxParticles = count;
            p.lifetime = 4.0f;
            p.rate = static_cast<f32>(count) / 4.0f;
            p.startSize = 4.0f;
            p.endSize = 1.0f;
            p.speed = 300.0f;
            p.spread = 360.0f;
            p.gravity = Vec3{0, 0, 0};
            p.emitterSize = Vec2{1600.0f, 900.0f};
            p.blendMode = blend;
            RenderSettings full;
            full.finalQuality = true;
            RenderSettings prev;
            prev.heavyScale = 0.5f;
            const Measured mf = measure(s, full, 1920, 1080, 8, FrameIndex{180});
            const Measured mp = measure(s, prev, 1920, 1080, 8, FrameIndex{180});
            std::printf("    | %uk | %s | %.3f | %.3f | %.3f |\n", count / 1000, blend == 1 ? "aditivo" : "normal", mf.gpuTotal, mp.gpuTotal, mf.cpu);
        }
    }
}

// -----------------------------------------------------------------------------
// Optical flow (§80–82): custo por estágio, 1080p, preview × export.
// -----------------------------------------------------------------------------
AUREA_TEST(Heavy, BenchOpticalFlowStages) {
    HEAVY_REQUIRE_GPU();
    if (!heavy_bench()) { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    const char* stages[] = {"flow-luma", "flow-reduz", "flow-lk", "flow-deforma", "desfoque-vetorial"};
    std::printf("    | Modo | luma | pirâmide | LK (estimativa) | deformação (interp.) | desfoque vetorial | GPU total | CPU |\n");
    for (int pass = 0; pass < 4; ++pass) {
        HScene s(1920, 1080);
        SyntheticConfig cfg;
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.frameCount = 300;
        cfg.pattern = SyntheticPattern::FastSquare;
        const LayerId v = s.video(cfg);
        Layer* l = s.comp->layer(v);
        l->speed = 0.2f;
        l->end = FrameIndex{1000};
        l->frameBlend = 2;
        l->vectorBlur = 0.5f;
        hgpu().renderer.set_flow_cache_enabled(false);
        RenderSettings rs;
        rs.dither = false;
        const char* label = "export";
        if (pass == 0) rs.finalQuality = true;
        else if (pass == 1) label = "preview 1,0";
        else if (pass == 2) { label = "preview 0,5"; rs.heavyScale = 0.5f; }
        else { label = "preview 0,25"; rs.heavyScale = 0.25f; }
        const Measured m = measure(s, rs, 1920, 1080, 8, FrameIndex{101});
        f64 st[5] = {0, 0, 0, 0, 0};
        for (int k = 0; k < 5; ++k) st[k] = m.pass(stages[k]);
        std::printf("    | %s | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.2f |\n", label, st[0], st[1], st[2], st[3], st[4], m.gpuTotal, m.cpu);
        if (std::getenv("AUREA_HEAVY_VERBOSE")) for (auto& [name, ms] : m.last.passes) std::printf("      passe %s %.3f\n", name.c_str(), ms);
        hgpu().renderer.set_flow_cache_enabled(true);
    }
}

// -----------------------------------------------------------------------------
// 3D (§68–75): desenhos, sombra e instancing medidos. Capacete (DamagedHelmet,
// 5 texturas 2048²) 1× e 25× numa grade; export × preview crítico; instancing
// ligado × desligado (o "antes").
// -----------------------------------------------------------------------------
namespace {
struct Models {
    std::unordered_map<u64, std::shared_ptr<const scene3d::SceneAsset>> byId;
    static std::shared_ptr<const scene3d::SceneAsset> lookup(void* self, AssetId id) {
        auto* m = static_cast<Models*>(self);
        auto it = m->byId.find(id.pack());
        return it == m->byId.end() ? nullptr : it->second;
    }
};
} // namespace

AUREA_TEST(Heavy, BenchScene3D) {
    HEAVY_REQUIRE_GPU();
    if (!heavy_bench()) { std::printf("    (pulado: AUREA_BENCH=1 para medir)\n"); return; }
    const std::string path = std::string(AUREA_TEST_DATA_DIR) + "/gltf/DamagedHelmet.glb";
    scene3d::ImportOptions io;
    io.maxTextureSize = 2048;
    scene3d::ImportResult r = scene3d::import_scene_file(path, io);
    if (!r.ok()) { std::printf("    (modelo de amostra ausente)\n"); return; }
    std::shared_ptr<const scene3d::SceneAsset> helmet(std::move(r.asset));
    std::printf("    capacete: %u triângulos, texturas decodificadas na RAM %.1f MB (teto de import 2048)\n", helmet->stats.triangles,
                static_cast<f64>(helmet->stats.imageBytes) / 1048576.0);
    Models models;
    hgpu().renderer.set_model_lookup(&Models::lookup, &models);
    std::printf("    | Cena | Modo | Instancing | Desenhos | Sombra (desenhos) | Triângulos | Mapa | GPU sombra (ms) | GPU 3D cor (ms) | GPU total (ms) | CPU (ms) |\n");
    for (u32 count : {1u, 25u}) {
        HScene s(1920, 1080);
        Asset a;
        a.kind = AssetKind::Model3D;
        const AssetId aid = s.project.add_asset(std::move(a));
        models.byId[aid.pack()] = helmet;
        const scene3d::Aabb& b = helmet->bounds;
        const f32 maxExt = std::max({b.extent().x, b.extent().y, b.extent().z, 1e-3f});
        const u32 side = count == 1 ? 1u : 5u;
        for (u32 i = 0; i < count; ++i) {
            const LayerId id = s.comp->add_layer(LayerKind::Model3D, "capacete");
            Layer* l = s.comp->layer(id);
            l->model.scene = aid;
            l->model.pivot = b.center();
            l->model.unitScale = 0.55f * 1080.0f / maxExt / static_cast<f32>(side);
            l->transform.position = Vec3{1920.0f * (static_cast<f32>(i % side) + 0.5f) / static_cast<f32>(side),
                                         1080.0f * (static_cast<f32>(i / side) + 0.5f) / static_cast<f32>(side), 0.0f};
        }
        for (int mode = 0; mode < 3; ++mode) {
            RenderSettings rs;
            rs.dither = false;
            bool inst = true;
            const char* label = "export";
            if (mode == 0) rs.finalQuality = true;
            else if (mode == 1) { rs.finalQuality = true; inst = false; label = "export"; }
            else { rs.heavyScale = 0.25f; label = "preview crítico"; }
            if (count == 1 && mode == 1) continue;
#if defined(AUREA_HEAVY_V2)
            hgpu().renderer.set_scene_instancing(inst);
#else
            if (inst && count > 1 && mode == 0) continue;   // antes da 8E não havia instancing
            inst = false;
#endif
            const Measured m = measure(s, rs, 1920, 1080, 8, FrameIndex{0});
            const f64 sh = m.pass("3d-sombra"), pbr = m.pass("3d-pbr");
#if defined(AUREA_HEAVY_V2)
            const HeavyStats& st = hgpu().renderer.heavy_stats();
            std::printf("    | %u capacete(s) | %s | %s | %u | %u | %u | %u | %.3f | %.3f | %.3f | %.2f |\n", count, label, inst ? "sim" : "não",
                        st.lastSceneDrawCalls, st.lastSceneShadowDrawCalls, st.lastSceneTriangles, st.lastShadowMapSize, sh, pbr, m.gpuTotal, m.cpu);
#else
            const scene3d::SceneStats& st = hgpu().renderer.scene_stats();
            std::printf("    | %u capacete(s) | %s | %s | %u | ? | %u | 2048 | %.3f | %.3f | %.3f | %.2f |\n", count, label, inst ? "sim" : "não",
                        st.drawCalls, st.triangles, sh, pbr, m.gpuTotal, m.cpu);
#endif
        }
#if defined(AUREA_HEAVY_V2)
        hgpu().renderer.set_scene_instancing(true);
#endif
        std::printf("    GPU residente do 3D (%u capacete(s), um modelo por asset): %.1f MB\n", count,
                    static_cast<f64>(hgpu().renderer.scene_resident_bytes()) / 1048576.0);
    }
    hgpu().renderer.set_model_lookup(nullptr, nullptr);
}

#if defined(AUREA_HEAVY_V2)
// -----------------------------------------------------------------------------
// Testes normais (travam o que o relatório promete). Motor inteiro, alvo
// fora da tela, contadores do Renderer (HeavyStats).
// -----------------------------------------------------------------------------
namespace {
struct HRig {
    Engine e;
    TextureHandle target{};
    u32 w, h;
    bool ok = false;
    HRig(u32 width, u32 height) : w(width), h(height) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        if (!e.initialize(ec).ok() || !e.new_project(w, h, 30.0, nullptr).ok()) return;
        TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.sampled = true;
        d.transferSrc = true;
        auto t = e.gpu()->create_texture(d);
        if (!t.ok()) return;
        target = *t;
        ok = true;
    }
    ~HRig() {
        if (target.valid()) e.gpu()->destroy_texture(target);
        e.shutdown();
    }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    void seek(i64 f) {
        Command s;
        s.type = CommandType::PlaybackSeek;
        s.seek.time = tick_at(FrameIndex{f}, 30.0);
        (void)e.apply_command(s);
    }
    bool render(bool preview = false) { return e.render_offscreen(target, w, h, preview).ok(); }
    std::vector<u8> pixels() {
        std::vector<u16> half(static_cast<usize>(w) * h * 4);
        std::vector<u8> out(half.size());
        if (!e.gpu()->read_texture(target, half.data(), w * 8).ok()) return out;
        for (usize i = 0; i < half.size(); ++i) out[i] = enc8(h2f(half[i]));
        return out;
    }
    const HeavyStats& stats() { return e.renderer().heavy_stats(); }
};
std::string heavy_gltf(const char* rel) { return std::string(AUREA_TEST_DATA_DIR) + "/gltf/" + rel; }
bool heavy_file(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}
} // namespace

// §64: texto animado em playback — o atlas SDF não re-rasteriza nem sobe de novo.
AUREA_TEST(Heavy, AnimatedTextPlaybackNeverTouchesTheAtlasInSteadyState) {
    HEAVY_REQUIRE_GPU();
    HRig rig(640, 360);
    if (!rig.ok) return;
    auto tid = rig.e.add_text("Aurea texto animado 123");
    AUREA_CHECK(tid.ok());
    if (!tid.ok()) return;
    AUREA_CHECK(rig.e.apply_text_preset(*tid, 8));   // máquina de escrever (opacidade por letra)
    for (i64 f = 0; f < 45; ++f) { rig.seek(f); AUREA_CHECK(rig.render()); }   // 1ª passada: glifos entram
    rig.e.renderer().reset_heavy_stats();
    for (i64 f = 0; f < 45; ++f) { rig.seek(f); AUREA_CHECK(rig.render()); }   // regime
    const HeavyStats& st = rig.stats();
    std::printf("    regime (45 quadros): %u uploads do atlas, %u glifos rasterizados, %u atlas refeitos\n", st.glyphAtlasUploads,
                st.glyphsRasterized, st.glyphAtlasResets);
    AUREA_CHECK_EQ(st.glyphAtlasUploads, 0u);
    AUREA_CHECK_EQ(st.glyphsRasterized, 0u);
    AUREA_CHECK_EQ(st.glyphAtlasResets, 0u);
}

// §65: fonte de reserva carrega sob demanda — uma, a da escrita do caractere.
AUREA_TEST(Heavy, FallbackFontsLoadOneAtATime) {
    const auto font = text::default_font();
    if (!font) return;
    u32 before = 0, after = 0;
    u64 bytesBefore = 0, bytesAfter = 0;
    text::fallback_font_stats(before, bytesBefore);
    TextData t;
    // U+0627 (árabe): a fonte padrão do host não tem. Antes, o primeiro
    // caractere ausente carregava TODAS as candidatas; agora só a 1ª que tem
    // (no Android, a Noto Naskh; no Windows, a Segoe UI).
    t.content = "Aurea \xD8\xA7";
    text::TextLayout L;
    (void)text::layout_quads(*font, t, 2.0f, L);
    text::fallback_font_stats(after, bytesAfter);
    std::printf("    reservas carregadas: %u -> %u (%.1f MB em RAM)\n", before, after, static_cast<f64>(bytesAfter) / 1048576.0);
    AUREA_CHECK(after - before <= 1);   // antes: todas as candidatas de uma vez
}

// §66–67: vetor parado não retriangula; máscara parada não rasteriza de novo —
// mesmo com a camada andando (a chave é o conteúdo, não a posição).
AUREA_TEST(Heavy, StaticVectorAndMaskAreBuiltOnceWhileTheLayerMoves) {
    HEAVY_REQUIRE_GPU();
    HRig rig(320, 240);
    if (!rig.ok) return;
    auto vid = rig.e.add_vector_layer(1);
    AUREA_CHECK(vid.ok());
    auto sid = rig.e.add_shape(0);
    AUREA_CHECK(sid.ok());
    if (!vid.ok() || !sid.ok()) return;
    const f32 pts[4 * 6] = {10, 10, 0, 0, 0, 0, 90, 10, 0, 0, 0, 0, 90, 70, 0, 0, 0, 0, 10, 70, 0, 0, 0, 0};
    AUREA_CHECK(rig.e.add_mask(*sid, pts, 4, true) >= 0);
    AUREA_CHECK(rig.render());
    rig.e.renderer().reset_heavy_stats();
    for (int f = 0; f < 20; ++f) {
        rig.comp()->layer(LayerId::unpack(*vid))->transform.position.x += 3.0f;
        rig.comp()->layer(LayerId::unpack(*sid))->transform.position.y += 2.0f;
        AUREA_CHECK(rig.render());
    }
    const HeavyStats& st = rig.stats();
    std::printf("    20 quadros andando: vetor %u retriangulações / %u acertos; máscara %u rasterizações / %u acertos\n",
                st.vectorTessellations, st.vectorCacheHits, st.maskRasterizations, st.maskCacheHits);
    AUREA_CHECK_EQ(st.vectorTessellations, 0u);
    AUREA_CHECK(st.vectorCacheHits >= 20u);
    AUREA_CHECK_EQ(st.maskRasterizations, 0u);
    AUREA_CHECK(st.maskCacheHits >= 20u);
}

// §68–75: instâncias do mesmo modelo viram um desenho instanciado e o quadro
// é IGUAL ao de modelos separados; fora do frustum conta como recortado; o
// mapa de sombra cai no preview quente e fica cheio no export.
AUREA_TEST(Heavy, Scene3DInstancingCullingAndShadowKnob) {
    HEAVY_REQUIRE_GPU();
    const std::string path = heavy_gltf("Box.glb");
    if (!heavy_file(path)) { std::printf("    (modelo de amostra ausente)\n"); return; }
    const Vec3 spots[4] = {Vec3{160, 120, 0}, Vec3{480, 120, 0}, Vec3{160, 240, 0}, Vec3{480, 240, 0}};
    auto scene = [&](bool shared, HRig& rig, std::vector<LayerId>& ids) {
        ModelImport mi;
        mi.path = path;
        auto first = rig.e.import_model(mi);
        AUREA_CHECK(first.ok());
        if (!first.ok()) return;
        ids.push_back(LayerId::unpack(*first));
        for (int i = 1; i < 4; ++i) {
            if (shared) {
                ids.push_back(rig.comp()->duplicate_layer(ids[0], FrameIndex{0}));
            } else {
                auto more = rig.e.import_model(mi);
                AUREA_CHECK(more.ok());
                if (more.ok()) ids.push_back(LayerId::unpack(*more));
            }
        }
        for (usize i = 0; i < ids.size(); ++i) {
            Layer* l = rig.comp()->layer(ids[i]);
            l->transform.position = spots[i];
            l->transform.scale = l->transform.scale * 0.35f;
        }
    };
    HRig a(640, 360), b(640, 360);
    if (!a.ok || !b.ok) return;
    std::vector<LayerId> ia, ib;
    scene(true, a, ia);
    scene(false, b, ib);
    AUREA_CHECK(a.render() && a.render());
    AUREA_CHECK(b.render() && b.render());
    const HeavyStats sa = a.stats(), sb = b.stats();
    std::printf("    4 caixas: mesmo asset %u desenhos (%u instanciados), sombra %u; assets separados %u desenhos, sombra %u\n",
                sa.lastSceneDrawCalls, sa.lastSceneInstancedDraws, sa.lastSceneShadowDrawCalls, sb.lastSceneDrawCalls, sb.lastSceneShadowDrawCalls);
    AUREA_CHECK(sa.lastSceneInstancedDraws >= 1u);
    AUREA_CHECK(sa.lastSceneDrawCalls * 4 <= sb.lastSceneDrawCalls + 3);
    AUREA_CHECK(sa.lastSceneShadowDrawCalls < sb.lastSceneShadowDrawCalls);
    AUREA_CHECK_EQ(sa.lastSceneVisible, sb.lastSceneVisible);
    const std::vector<u8> pa = a.pixels(), pb = b.pixels();
    u32 mx = 0;
    for (usize i = 0; i < pa.size() && i < pb.size(); ++i) mx = std::max<u32>(mx, static_cast<u32>(std::abs(pa[i] - pb[i])));
    std::printf("    instanciado × separado: diferença máxima %u/255\n", mx);
    AUREA_CHECK(mx <= 2);
    // Frustum: uma caixa bem fora da tela não desenha (e conta).
    a.comp()->layer(ia[3])->transform.position = Vec3{-4000, -4000, 0};
    AUREA_CHECK(a.render() && a.render());
    std::printf("    uma caixa fora da tela: visíveis %u, recortadas %u\n", a.stats().lastSceneVisible, a.stats().lastSceneCulled);
    AUREA_CHECK(a.stats().lastSceneCulled >= 1u);
    AUREA_CHECK(a.stats().lastSceneVisible < sa.lastSceneVisible);
    // Sombra: export (e preview frio) 2048; preview crítico 512 e filtro barato.
    AUREA_CHECK(a.render(false));
    const u32 full = a.stats().lastShadowMapSize;
    a.e.set_thermal(3, true);
    AUREA_CHECK(a.render(true));
    const u32 hot = a.stats().lastShadowMapSize;
    AUREA_CHECK(a.render(false));
    const u32 exportAgain = a.stats().lastShadowMapSize;
    a.e.set_thermal(0, false);
    std::printf("    mapa de sombra: cheio %u, preview crítico %u, export durante o calor %u\n", full, hot, exportAgain);
    AUREA_CHECK_EQ(full, 2048u);
    AUREA_CHECK_EQ(hot, 512u);
    AUREA_CHECK_EQ(exportAgain, 2048u);
}

// §82: o cache do optical flow respeita o orçamento (LRU por camada).
AUREA_TEST(Heavy, FlowCacheStaysWithinItsBudget) {
    HEAVY_REQUIRE_GPU();
    HScene s(640, 360);
    SyntheticConfig cfg;
    cfg.width = 640;
    cfg.height = 360;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::FastSquare;
    for (int i = 0; i < 3; ++i) {
        Layer* l = s.comp->layer(s.video(cfg));
        l->speed = 0.2f;
        l->end = FrameIndex{1000};
        l->frameBlend = 2;
        l->transform.scale = Vec3{0.5f, 0.5f, 1.0f};
    }
    Renderer& r = hgpu().renderer;
    // Uma textura de flow deste vídeo: base 384 no lado maior (384×216 RGBA16F).
    const u64 one = 384ull * 216ull * 8ull;
    r.set_flow_cache_budget(one * 3);
    r.reset_heavy_stats();
    RenderSettings rs;
    rs.dither = false;
    for (i64 f = 101; f < 131; f += 3) (void)s.frame(FrameIndex{f}, rs, 640, 360);
    hgpu().backend.wait_idle();
    const HeavyStats st = r.heavy_stats();
    std::printf("    3 camadas com flow, orçamento %.2f MB: residente %.2f MB, %u calculados, %u despejos\n",
                static_cast<f64>(one * 3) / 1048576.0, static_cast<f64>(st.flowCacheBytes) / 1048576.0, st.flowComputed, st.flowCacheEvictions);
    AUREA_CHECK(st.flowCacheBytes <= one * 3);
    AUREA_CHECK(st.flowCacheEvictions > 0u);
    AUREA_CHECK(st.flowComputed > 0u);
    r.set_flow_cache_budget(48ull << 20);
    r.release_project_resources();
    AUREA_CHECK_EQ(r.heavy_stats().flowCacheBytes, 0ull);
}

// Sistemas pesados no preview adaptativo: a escada do HeavyQuality desce tudo
// junto e o export ignora a escala (SPEC §8).
AUREA_TEST(Heavy, QualityLadderAndExportIsAlwaysFull) {
    RenderSettings rs;
    rs.heavyScale = 0.25f;
    rs.previewDenominator = 2;
    const HeavyQuality q = resolve_heavy(rs);
    AUREA_CHECK(q.particles <= 0.25f && q.flow <= 0.25f && q.shadowMapSize == 512u && q.shadowFilter == 1u && q.effects <= 0.25f);
    AUREA_CHECK(!q.exportFrame);
    rs.finalQuality = true;
    const HeavyQuality e = resolve_heavy(rs);
    AUREA_CHECK(e.is_full() && e.exportFrame && e.shadowMapSize == 2048u);
    RenderSettings cool;
    cool.previewDenominator = 4;
    AUREA_CHECK_EQ(resolve_heavy(cool).shadowMapSize, 512u);   // preview 1/4: o mapa acompanha
    AUREA_CHECK_EQ(resolve_heavy(cool).particles, 1.0f);       // … sem tirar partícula de quem está frio
}

#endif // AUREA_HEAVY_V2

#endif // AUREA_TEST_VULKAN
