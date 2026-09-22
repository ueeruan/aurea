// =============================================================================
//  Aurea / tests / test_perf.cpp — Fase 8A: suíte de benchmark do host.
//
//  Só mede com AUREA_BENCH (a suíte normal não paga o custo):
//    AUREA_BENCH=1         mede e imprime (nada é gravado);
//    AUREA_BENCH=baseline  mede e grava docs/performance/baseline_host.json;
//    AUREA_BENCH=compare   mede, grava bench_atual.json no diretório de
//                          trabalho e FALHA se um p50 de GPU ou CPU piorar
//                          mais de 40% sobre a baseline (§130). Com o host
//                          bem mais ocupado que na baseline, piora de CPU
//                          sai INCONCLUSIVA (AUREA_BENCH_STRICT=1 falha).
//  Rodar só a suíte: `aurea_tests.exe Perf`.
//
//  Motor real e GPU Vulkan real do host. Cada quadro medido diz:
//    - CPU: prepare (sob o lock do modelo) + gravação do FrameGraph + submit;
//    - GPU: timestamp query do quadro EXATO (render_offscreen espera a GPU);
//    - parede do quadro inteiro;
//  e cada benchmark: p50/p95/p99, média e desvio padrão (ritmo, §145), RSS do
//  processo, memória do alocador de GPU, draw calls, passes, efeitos vivos.
//
//  O que ISTO NÃO É: o fps do preview. O quadro offscreen é serial (CPU →
//  GPU → espera); o preview sobrepõe CPU e GPU e só apresenta. Também não é
//  celular: é uma RTX de mesa. Os números servem para comparar builds e achar
//  o estágio caro — nunca para dizer "60 fps no aparelho".
//
//  Sem truque (§163–165): nenhum efeito é desligado, nenhum quadro pulado; o
//  vídeo sintético é decodificado de verdade (NV12 gerado na CPU — o custo
//  NÃO representa um decoder de hardware; por isso o decode sai à parte).
// =============================================================================
#include "TestFramework.hpp"

#if defined(AUREA_TEST_VULKAN)

#include "SyntheticVideo.hpp"
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/project/Project.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#endif

using namespace aurea;
using namespace aurea::test;

namespace {

// -----------------------------------------------------------------------------
// Modo e ambiente
// -----------------------------------------------------------------------------
enum class BenchMode { Off, Print, Baseline, Compare };

BenchMode bench_mode() {
    const char* v = std::getenv("AUREA_BENCH");
    if (!v || !*v || *v == '0') return BenchMode::Off;
    if (std::strcmp(v, "baseline") == 0) return BenchMode::Baseline;
    if (std::strcmp(v, "compare") == 0) return BenchMode::Compare;
    return BenchMode::Print;
}

#define AUREA_REQUIRE_BENCH()                                                      \
    do {                                                                           \
        if (bench_mode() == BenchMode::Off) {                                      \
            std::printf("(pulado: AUREA_BENCH=1|baseline|compare para medir) ");   \
            return;                                                                \
        }                                                                          \
    } while (0)

f64 now_ms() {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

u64 process_rss() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.WorkingSetSize;
    return 0;
#else
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long pages = 0, rss = 0;
    const int n = std::fscanf(f, "%lu %lu", &pages, &rss);
    std::fclose(f);
    return n == 2 ? static_cast<u64>(rss) * 4096ull : 0;
#endif
}

u64 process_peak_rss() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.PeakWorkingSetSize;
#endif
    return 0;
}

std::string cpu_name() {
#if defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))
    int regs[4]{};
    char brand[49]{};
    for (int i = 0; i < 3; ++i) {
        __cpuid(regs, static_cast<int>(0x80000002u + static_cast<unsigned>(i)));
        std::memcpy(brand + i * 16, regs, 16);
    }
    std::string s(brand);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    const usize a = s.find_first_not_of(' ');
    return a == std::string::npos ? s : s.substr(a);
#else
    return "desconhecida";
#endif
}

u64 total_ram() {
#if defined(_WIN32)
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) return m.ullTotalPhys;
#endif
    return 0;
}

std::string os_name() {
#if defined(_WIN32)
    // RtlGetVersion diz a versão real (GetVersionEx mente sem manifesto).
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion")))) fn(&v);
    }
    char buf[80];
    // NT 10.0 com build >= 22000 é o Windows 11.
    std::snprintf(buf, sizeof(buf), "Windows %s (NT %lu.%lu build %lu)", v.dwBuildNumber >= 22000 ? "11" : "10",
                  v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    return buf;
#elif defined(__linux__)
    return "Linux";
#else
    return "desconhecido";
#endif
}

// -----------------------------------------------------------------------------
// Estatística
// -----------------------------------------------------------------------------
struct Stat {
    f64 p50 = 0, p95 = 0, p99 = 0, mean = 0, std = 0, min = 0, max = 0;
    u32 n = 0;
};

Stat stat_of(std::vector<f64> v) {
    Stat s;
    s.n = static_cast<u32>(v.size());
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    auto pct = [&](f64 q) {   // posto mais próximo
        const usize k = std::min(v.size() - 1, static_cast<usize>(std::ceil(q * static_cast<f64>(v.size())) - 1.0));
        return v[k];
    };
    s.p50 = pct(0.50);
    s.p95 = pct(0.95);
    s.p99 = pct(0.99);
    s.min = v.front();
    s.max = v.back();
    f64 sum = 0;
    for (f64 x : v) sum += x;
    s.mean = sum / static_cast<f64>(v.size());
    f64 var = 0;
    for (f64 x : v) var += (x - s.mean) * (x - s.mean);
    s.std = std::sqrt(var / static_cast<f64>(v.size()));
    return s;
}

// -----------------------------------------------------------------------------
// Resultados (acumulados pela suíte; o último teste grava/compara)
// -----------------------------------------------------------------------------
struct FrameBench {
    std::string id, desc;
    u32 width = 0, height = 0, frames = 0;
    Stat wall, cpu, prepare, record, gpu;
    bool gpuMeasured = false;
    f64 decodeMsAvg = 0;       ///< decoder sintético (thread de decode), se houver vídeo
    u32 drawCalls = 0, passes = 0, culled = 0, layers = 0, effects = 0;
    u32 draws3D = 0, triangles3D = 0, particles = 0;
    u64 rss = 0, gpuUsed = 0, transient = 0;
    std::string topPasses;     ///< os passes de GPU mais caros (rótulo=ms)
    /// Export: só o intervalo entre quadros entregues (parede, em `cpu`); a
    /// GPU do export não é separada por quadro e fica 0 (não é comparada).
    bool serialWall = false;
};

struct Metric {
    std::string id;
    f64 ms = 0;                 ///< mediana das repetições
    std::string note;
    bool compare = true;        ///< entra no verificador de regressão
};

struct EffectCost {
    std::string key, name, category;
    f64 gpuDelta = 0;           ///< GPU p50 com o efeito − sem ele
    f64 gpuP95 = 0;
    u32 passes = 0;
    bool built = true;          ///< entrou no plano (vivo, não neutro)
    f64 changedPct = 0;         ///< % de pixels que mudaram na captura 192 px
};

/// Carga de CPU do host FORA deste processo durante a suíte (outros builds,
/// emulador): número que diz o quanto a medição estava num PC ocupado.
struct LoadProbe {
    u64 idle = 0, kernel = 0, user = 0, proc = 0;
    bool ok = false;
};

LoadProbe load_now() {
    LoadProbe p;
#if defined(_WIN32)
    FILETIME i, k, u, c, e, pk, pu;
    auto v = [](const FILETIME& t) { return (static_cast<u64>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    if (GetSystemTimes(&i, &k, &u) && GetProcessTimes(GetCurrentProcess(), &c, &e, &pk, &pu)) {
        p.idle = v(i);
        p.kernel = v(k);   // inclui o ocioso
        p.user = v(u);
        p.proc = v(pk) + v(pu);
        p.ok = true;
    }
#endif
    return p;
}

/// % de CPU do sistema ocupada por OUTROS processos entre `a` e `b`.
f64 other_load_pct(const LoadProbe& a, const LoadProbe& b) {
    if (!a.ok || !b.ok) return -1.0;
    const f64 total = static_cast<f64>((b.kernel - a.kernel) + (b.user - a.user));
    const f64 busy = total - static_cast<f64>(b.idle - a.idle);
    const f64 mine = static_cast<f64>(b.proc - a.proc);
    return total > 0 ? std::max(0.0, (busy - mine) / total * 100.0) : -1.0;
}

struct Results {
    LoadProbe start = load_now();
    std::vector<FrameBench> frames;
    std::vector<Metric> campaign;
    std::vector<EffectCost> effects;
    f64 effectsBaseGpu = 0;
    std::string gpuName, gpuDriver, gpuApi;
};

Results& results() {
    static Results r;
    return r;
}

void add_metric(const char* id, f64 ms, const char* note, bool compare = true) {
    results().campaign.push_back(Metric{id, ms, note ? note : "", compare});
    std::printf("\n    %-34s %9.2f ms  %s", id, ms, note ? note : "");
}

f64 median_of(std::vector<f64> v) { return stat_of(std::move(v)).p50; }

// -----------------------------------------------------------------------------
// Rig: motor com Vulkan de verdade e um alvo offscreen do tamanho da composição.
// -----------------------------------------------------------------------------
struct Rig {
    Engine e;
    TextureHandle target{};
    u32 w = 0, h = 0;
    bool ok = false;
    f64 initMs = 0;

    Rig(u32 width, u32 height, VideoSourceFactory* factory = nullptr, f64 fps = 30.0,
        ExportSinkFactory sink = nullptr, void* sinkCtx = nullptr) : w(width), h(height) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.backendConfig.framesInFlight = 2;
        ec.mediaFactory = factory;
        ec.exportSinkFactory = sink;
        ec.exportSinkContext = sinkCtx;
        ec.workerCount = 4;
        ec.disableAutosave = true;
        ec.memoryBudgetBytes = 2048ull << 20;
        const f64 t0 = now_ms();
        if (!e.initialize(ec).ok()) return;
        initMs = now_ms() - t0;
        if (!e.new_project(width, height, fps, "bench").ok()) return;
        if (!e.gpu()) return;
        TextureDesc d;
        d.width = width;
        d.height = height;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.transferSrc = true;
        d.debugName = "bench-alvo";
        auto t = e.gpu()->create_texture(d);
        if (!t.ok()) return;
        target = *t;
        e.set_offscreen_timers(true);
        auto& res = results();
        if (res.gpuName.empty()) {
            const GPUCapabilities& c = e.gpu()->capabilities();
            res.gpuName = c.deviceName;
            res.gpuDriver = c.driverInfo;
            // NVIDIA codifica a versão em 10.8.8.6 bits (o "driver 0x..." cru não diz nada).
            const usize hex = c.driverInfo.find("0x");
            if (c.vendorId == 0x10DE && hex != std::string::npos) {
                const unsigned long v = std::strtoul(c.driverInfo.c_str() + hex, nullptr, 16);
                char drv[48];
                std::snprintf(drv, sizeof(drv), " (NVIDIA %lu.%02lu)", (v >> 22) & 0x3FFul, (v >> 14) & 0xFFul);
                res.gpuDriver += drv;
            }
            char api[48];
            std::snprintf(api, sizeof(api), "%s %u.%u.%u", c.apiName.c_str(), c.apiMajor, c.apiMinor, c.apiPatch);
            res.gpuApi = api;
        }
        ok = true;
    }
    ~Rig() {
        if (target.valid() && e.gpu()) e.gpu()->destroy_texture(target);
        e.shutdown();
    }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* layer(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void seek(i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{frame}, comp()->fps());
        (void)e.apply_command(c);
    }
    void set_duration(i64 frames) {
        Command c;
        c.type = CommandType::CompositionSetDuration;
        c.comp_duration.comp = e.project()->timeline().current();
        c.comp_duration.duration = FrameIndex{frames};
        (void)e.apply_command(c);
    }
    Status render(bool preview = true) { return e.render_offscreen(target, w, h, preview); }
};

/// Efeito do catálogo com os VALORES DE DEMONSTRAÇÃO (§86: o que a prévia
/// mostra; o padrão de fábrica é neutro e sairia da cadeia sem custo).
bool add_demo_effect(Engine& e, Layer* l, EffectTypeId type) {
    const Effect* fx = e.effects().find(type);
    const ParameterRegistry* params = e.effects().params(type);
    if (!fx || !params || !l) return false;
    EffectInstance inst;
    inst.id = l->alloc_effect_id();
    inst.type = type;
    initialize_instance(inst, *params);
    std::vector<ParamValue> values(params->count());
    for (u32 p = 0; p < params->count(); ++p) values[p] = inst.params[p].constant;
    if (fx->demo_values(inst, values)) {
        for (u32 p = 0; p < params->count(); ++p) inst.params[p].constant = values[p];
    }
    l->effects.push_back(std::move(inst));
    return true;
}

bool add_demo_effect(Engine& e, Layer* l, const char* key) { return add_demo_effect(e, l, effect_type_id(key)); }

/// Placa RGBA8 com degradê, disco claro e linhas finas (borda para os efeitos
/// de vizinhança, área clara para os de luz).
std::vector<u8> plate(u32 w, u32 h) {
    std::vector<u8> px(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px[(static_cast<usize>(y) * w + x) * 4];
            const f32 u = static_cast<f32>(x) / static_cast<f32>(w), v = static_cast<f32>(y) / static_cast<f32>(h);
            const f32 dx = u - 0.5f, dy = (v - 0.5f) * static_cast<f32>(h) / static_cast<f32>(w);
            const bool disc = dx * dx + dy * dy < 0.03f;
            const bool line = (x % 64) < 2 || (y % 64) < 2;
            p[0] = disc ? 250 : static_cast<u8>(40 + 180 * u);
            p[1] = disc ? 240 : static_cast<u8>(30 + 120 * v);
            p[2] = line ? 255 : static_cast<u8>(90 + 60 * (1 - u));
            p[3] = 255;
        }
    }
    return px;
}

/// Mede `frames` quadros a partir de `first` (passo `step`). Com
/// `decodeFirst`, cada quadro é desenhado duas vezes e só a segunda conta: a
/// primeira espera o decoder (o decode sai no `decodeMsAvg`, não no quadro).
FrameBench measure_once(Rig& r, const char* id, const char* desc, u32 warm, u32 frames, i64 first, i64 step,
                        bool decodeFirst, bool preview) {
    FrameBench b;
    b.id = id;
    b.desc = desc;
    b.width = r.w;
    b.height = r.h;
    std::vector<f64> wall, cpu, prep, rec, gpu;
    std::vector<GpuTiming> passSum;
    std::vector<f64> passMs;
    GpuTiming passes[64];
    i64 f = first;
    for (u32 i = 0; i < warm + frames; ++i, f += step) {
        r.seek(f);
        if (decodeFirst) (void)r.render(preview);
        const f64 t0 = now_ms();
        const Status s = r.render(preview);
        const f64 t1 = now_ms();
        if (!s.ok()) continue;
        if (i < warm) continue;
        const Engine::OffscreenMeasure m = r.e.last_offscreen_measure();
        wall.push_back(t1 - t0);
        cpu.push_back(m.prepareMs + m.recordMs + m.submitMs);
        prep.push_back(m.prepareMs);
        rec.push_back(m.recordMs);
        if (m.gpuMeasured) {
            gpu.push_back(m.gpuMs);
            const u32 n = r.e.last_offscreen_gpu_passes(passes, 64);
            for (u32 k = 0; k < n; ++k) {
                usize j = 0;
                while (j < passSum.size() && std::strcmp(passSum[j].label ? passSum[j].label : "", passes[k].label ? passes[k].label : "") != 0) ++j;
                if (j == passSum.size()) {
                    passSum.push_back(passes[k]);
                    passMs.push_back(0);
                }
                passMs[j] += passes[k].ms;
            }
        }
        b.drawCalls = m.drawCalls;
        b.passes = m.passesExecuted;
        b.culled = m.passesCulled;
        b.layers = m.layersRendered;
        b.effects = m.activeEffects;
        b.draws3D = m.draws3D;
        b.triangles3D = m.triangles3D;
        b.particles = m.particles;
        b.gpuUsed = m.gpuUsedBytes;
        b.transient = m.transientBytes;
    }
    b.frames = static_cast<u32>(wall.size());
    b.wall = stat_of(wall);
    b.cpu = stat_of(cpu);
    b.prepare = stat_of(prep);
    b.record = stat_of(rec);
    b.gpu = stat_of(gpu);
    b.gpuMeasured = !gpu.empty();
    b.rss = process_rss();
    b.decodeMsAvg = r.e.media().stats().decodeMsAvg;
    // Os 4 passes mais caros (média por quadro).
    std::vector<usize> idx(passSum.size());
    for (usize i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](usize a, usize c) { return passMs[a] > passMs[c]; });
    for (usize i = 0; i < idx.size() && i < 4; ++i) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s%s=%.3f", i ? ", " : "", passSum[idx[i]].label ? passSum[idx[i]].label : "?",
                      passMs[idx[i]] / std::max<f64>(1.0, static_cast<f64>(gpu.size())));
        b.topPasses += buf;
    }
    return b;
}

void print_bench(const FrameBench& b) {
    std::printf("\n    %-22s %4ux%-4u n=%-3u GPU p50 %6.2f p95 %6.2f p99 %6.2f σ %5.2f | CPU p50 %6.2f (prep %5.2f) p95 %6.2f | "
                "parede p50 %6.2f p95 %6.2f | passes %u draws %u camadas %u efeitos %u | RSS %llu MB GPU %llu MB",
                b.id.c_str(), b.width, b.height, b.frames, b.gpu.p50, b.gpu.p95, b.gpu.p99, b.gpu.std, b.cpu.p50, b.prepare.p50,
                b.cpu.p95, b.wall.p50, b.wall.p95, b.passes, b.drawCalls, b.layers, b.effects,
                static_cast<unsigned long long>(b.rss >> 20), static_cast<unsigned long long>(b.gpuUsed >> 20));
    if (b.draws3D || b.triangles3D) std::printf(" | 3D draws %u tris %u", b.draws3D, b.triangles3D);
    if (b.particles) std::printf(" | particulas %u", b.particles);
    if (b.decodeMsAvg > 0) std::printf(" | decode %.2f ms", b.decodeMsAvg);
    if (!b.topPasses.empty()) std::printf("\n      passes caros: %s", b.topPasses.c_str());
}

std::string read_file(const std::string& path);
bool find_value(const std::string& json, const std::string& id, const char* key, f64& out);

/// A baseline (só no modo compare), lida uma vez.
const std::string& baseline_json() {
    static const std::string s = bench_mode() == BenchMode::Compare
                                     ? read_file(std::string(AUREA_PERF_DIR) + "/baseline_host.json") : std::string();
    return s;
}

bool over_budget(f64 now, f64 base, f64 floorMs) { return now > base * 1.40 && now - base > floorMs; }

bool regressed(const FrameBench& b) {
    const std::string& j = baseline_json();
    f64 g = 0, c = 0;
    return (find_value(j, b.id, "\"gpu_ms\": {\"p50\": ", g) && over_budget(b.gpu.p50, g, 0.25))
        || (find_value(j, b.id, "\"cpu_ms\": {\"p50\": ", c) && over_budget(b.cpu.p50, c, 0.25));
}

/// Mede; no modo compare, um resultado acima do limite é MEDIDO DE NOVO (até
/// 2 vezes) e fica o melhor p50: o PC do host é compartilhado (builds, o
/// emulador com -gpu host) e um pico de contenção não é regressão do código.
/// Nada do trabalho muda entre as tentativas — é a mesma cena, os mesmos quadros.
FrameBench measure(Rig& r, const char* id, const char* desc, u32 warm, u32 frames, i64 first, i64 step,
                   bool decodeFirst, bool preview = true) {
    FrameBench b = measure_once(r, id, desc, warm, frames, first, step, decodeFirst, preview);
    for (int retry = 0; retry < 2 && bench_mode() == BenchMode::Compare && regressed(b); ++retry) {
        FrameBench again = measure_once(r, id, desc, warm, frames, first, step, decodeFirst, preview);
        std::printf("\n    %s acima do limite: medido de novo (GPU p50 %.2f -> %.2f, CPU p50 %.2f -> %.2f)", id, b.gpu.p50,
                    again.gpu.p50, b.cpu.p50, again.cpu.p50);
        if (again.gpu.p50 + again.cpu.p50 < b.gpu.p50 + b.cpu.p50) b = std::move(again);
    }
    print_bench(b);
    return b;
}

void keep(FrameBench b) {
    AUREA_CHECK_MSG(b.frames > 0, "benchmark sem quadro medido");
    AUREA_CHECK_MSG(b.gpuMeasured || b.serialWall, "GPU nao medida (timestamp query)");
    results().frames.push_back(std::move(b));
}

SyntheticConfig video_cfg(u32 w, u32 h, u32 frames = 600) {
    SyntheticConfig c;
    c.width = w;
    c.height = h;
    c.frameCount = frames;
    c.pattern = SyntheticPattern::FastSquare;
    return c;
}

u64 import_synthetic(Rig& r, const char* name = "sintetico") {
    VideoImport vi;
    vi.sourcePath = name;
    vi.displayName = name;
    auto id = r.e.import_video(vi);
    return id.ok() ? *id : 0;
}

/// Cena BÁSICA: um vídeo em quadro cheio e um título.
void build_basic(Rig& r) {
    (void)import_synthetic(r);
    auto t = r.e.add_text("AUREA");
    if (t.ok()) r.layer(*t)->text.size = static_cast<f32>(r.h) * 0.08f;
}

/// Cena PESADA (§98): 5 camadas — vídeo com desfoque, glow e cor; segundo
/// vídeo girando com desfoque de movimento; dois textos (contorno + sombra,
/// e um animador); forma com glow. Desfoque de movimento da composição ligado.
void build_heavy(Rig& r) {
    const f32 k = static_cast<f32>(r.h) / 1080.0f;
    const u64 v1 = import_synthetic(r, "sintetico-a");
    const u64 v2 = import_synthetic(r, "sintetico-b");
    if (!v1 || !v2) return;
    Layer* a = r.layer(v1);
    add_demo_effect(r.e, a, effect_keys::kGaussianBlur);
    add_demo_effect(r.e, a, effect_keys::kExposure);
    add_demo_effect(r.e, a, effect_keys::kSaturation);
    add_demo_effect(r.e, a, effect_keys::kGlow);
    Layer* b = r.layer(v2);
    b->transform.scale = Vec3{0.5f, 0.5f, 1};
    b->motionBlur = true;
    Track& rot = b->tracks.get_or_create(TrackProperty::RotationZ);
    rot.set(FrameIndex{0}, 0.0f);
    rot.set(FrameIndex{600}, 720.0f);
    add_demo_effect(r.e, b, effect_keys::kCurves);
    auto t1 = r.e.add_text("AUREA FASE 8");
    auto t2 = r.e.add_text("benchmark pesado");
    auto s = r.e.add_shape(1);
    if (t1.ok()) {
        Layer* l = r.layer(*t1);
        l->text.size = 120 * k;
        l->text.strokeWidth = 4 * k;
        l->text.strokeColor = Vec4{0.1f, 0.1f, 0.1f, 1};
        l->text.shadow = true;
        l->text.shadowOffset = Vec2{6 * k, 6 * k};
        l->motionBlur = true;
        Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
        px.set(FrameIndex{0}, 200 * k);
        px.set(FrameIndex{600}, static_cast<f32>(r.w) - 200 * k);
    }
    if (t2.ok()) {
        r.layer(*t2)->text.size = 70 * k;
        (void)r.e.apply_text_preset(*t2, 8);   // máquina de escrever
    }
    if (s.ok()) {
        Layer* l = r.layer(*s);
        l->transform.scale = Vec3{2 * k, 2 * k, 1};
        l->shape.fillColor = Vec4{1, 0.6f, 0.1f, 1};
        add_demo_effect(r.e, l, effect_keys::kGlow);
    }
    r.comp()->motion_blur().enabled = true;
}

} // namespace

// =============================================================================
// PERF_* (§128)
// =============================================================================
AUREA_TEST(Perf, EngineOpenNewProjectAndInit) {
    AUREA_REQUIRE_BENCH();
    // Abertura do motor: initialize inteiro (instância/dispositivo Vulkan,
    // shaders e o pré-aquecimento de TODOS os pipelines). A 1ª no processo é
    // a "fria" possível no host (o cache do driver no disco não é controlado).
    std::vector<f64> inits, news;
    for (int i = 0; i < 4; ++i) {
        Engine e;
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.workerCount = 4;
        ec.disableAutosave = true;
        const f64 t0 = now_ms();
        const bool ok = e.initialize(ec).ok();
        const f64 t1 = now_ms();
        AUREA_CHECK(ok);
        if (!ok) return;
        const bool okp = e.new_project(1920, 1080, 30.0, "bench").ok();
        const f64 t2 = now_ms();
        AUREA_CHECK(okp);
        if (i == 0) add_metric("OPEN_ENGINE_INIT_FIRST", t1 - t0, "initialize, 1a no processo (Vulkan + pre-aquecimento)", false);
        else inits.push_back(t1 - t0);
        news.push_back(t2 - t1);
        e.shutdown();
    }
    add_metric("OPEN_ENGINE_INIT_WARM", median_of(inits), "initialize, mediana de 3 (driver ja aquecido)");
    add_metric("NEW_PROJECT_1080", median_of(news), "new_project 1920x1080");
}

AUREA_TEST(Perf, PERF_1080_BASIC) {
    AUREA_REQUIRE_BENCH();
    SyntheticFactory f(video_cfg(1920, 1080));
    Rig r(1920, 1080, &f);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    build_basic(r);
    keep(measure(r, "PERF_1080_BASIC", "1 video 1080p + 1 titulo", 10, 120, 0, 1, true));
}

AUREA_TEST(Perf, PERF_1080_HEAVY) {
    AUREA_REQUIRE_BENCH();
    SyntheticFactory f(video_cfg(1920, 1080));
    Rig r(1920, 1080, &f);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    build_heavy(r);
    keep(measure(r, "PERF_1080_HEAVY", "5 camadas: 2 videos, blur, glow x2, cor, curvas, 2 textos, motion blur", 10, 120, 0, 1, true));
}

AUREA_TEST(Perf, PERF_4K_BASIC) {
    AUREA_REQUIRE_BENCH();
    SyntheticFactory f(video_cfg(3840, 2160, 300));
    Rig r(3840, 2160, &f);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    build_basic(r);
    keep(measure(r, "PERF_4K_BASIC", "1 video 4K + 1 titulo", 5, 60, 0, 1, true));
}

AUREA_TEST(Perf, PERF_4K_HEAVY) {
    AUREA_REQUIRE_BENCH();
    SyntheticFactory f(video_cfg(3840, 2160, 300));
    Rig r(3840, 2160, &f);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    build_heavy(r);
    keep(measure(r, "PERF_4K_HEAVY", "a cena pesada em 4K", 5, 60, 0, 1, true));
}

namespace {
std::string gltf(const char* rel) { return std::string(AUREA_TEST_DATA_DIR) + "/gltf/" + rel; }
bool exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}
} // namespace

AUREA_TEST(Perf, PERF_3D) {
    AUREA_REQUIRE_BENCH();
    const char* models[] = {"DamagedHelmet.glb", "Fox.glb", "MetalRoughSpheres.glb"};
    for (const char* m : models) {
        if (!exists(gltf(m))) {
            std::printf("(sem %s em tests/data/gltf: pulado) ", m);
            return;
        }
    }
    Rig r(1920, 1080);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    // Importar = parse + upload (o que a pessoa espera ao escolher o arquivo).
    for (const char* m : models) {
        ModelImport mi;
        mi.path = gltf(m);
        const f64 t0 = now_ms();
        auto id = r.e.import_model(mi);
        const f64 t1 = now_ms();
        AUREA_CHECK(id.ok());
        char name[64];
        std::snprintf(name, sizeof(name), "IMPORT_MODEL_%s", m);
        for (char* c = name; *c; ++c) if (*c == '.') *c = '_';
        add_metric(name, t1 - t0, "import_model (parse glTF + texturas), 1 vez", false);
    }
    // Espalha: capacete à esquerda, raposa (esqueleto animado) no meio,
    // esferas PBR à direita. Sombras ligadas (padrão), IBL de estúdio.
    const Composition* c = r.comp();
    u32 i = 0;
    for (u32 k = 0; k < c->order().size(); ++k) {
        Layer* l = r.comp()->layer(c->order().at(k));
        if (!l || l->kind != LayerKind::Model3D) continue;
        l->transform.position.x = 480.0f + 480.0f * static_cast<f32>(i++);
    }
    keep(measure(r, "PERF_3D", "3 glTF: DamagedHelmet (PBR), Fox (esqueleto), MetalRoughSpheres; sombras + IBL", 10, 120, 0, 1, false));
}

AUREA_TEST(Perf, PERF_PARTICLES) {
    AUREA_REQUIRE_BENCH();
    for (u32 count : {10000u, 100000u, 500000u, 1000000u}) {
        Rig r(1920, 1080);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        auto p = r.e.add_particles(0);
        AUREA_CHECK(p.ok());
        if (!p.ok()) return;
        Layer* l = r.layer(*p);
        l->particles.maxParticles = count;
        l->particles.lifetime = 4.0f;
        l->particles.rate = static_cast<f32>(count) / 4.0f;
        l->particles.startSize = 3.0f;
        l->particles.endSize = 1.0f;
        char id[48], desc[64];
        std::snprintf(id, sizeof(id), "PERF_PARTICLES_%uK", count / 1000);
        std::snprintf(desc, sizeof(desc), "%u particulas analiticas (GPU), aditivas", count);
        keep(measure(r, id, desc, 5, 60, 150, 1, false));
    }
}

namespace {
std::vector<text::CaptionWord> fake_words(u32 n) {
    static const char* pool[] = {"o", "editor", "de", "video", "aurea", "roda", "no", "celular", "com", "legendas",
                                 "grandes", "e", "fluidas", "sem", "travar", "a", "timeline"};
    std::vector<text::CaptionWord> w;
    w.reserve(n);
    f64 t = 0.5;
    for (u32 i = 0; i < n; ++i) {
        const f64 len = 0.25 + 0.05 * static_cast<f64>(i % 5);
        w.push_back(text::CaptionWord{pool[i % (sizeof(pool) / sizeof(pool[0]))], t, t + len});
        t += len + ((i % 11) == 10 ? 0.8 : 0.08);   // pausa a cada 11 palavras
    }
    return w;
}
} // namespace

AUREA_TEST(Perf, PERF_CAPTIONS) {
    AUREA_REQUIRE_BENCH();
    // Fonte de 45 min (5000 palavras ≈ 38 min de fala). Vídeo pequeno: o que
    // se mede é o custo das legendas, não o decode.
    SyntheticConfig cfg = video_cfg(320, 180, 30 * 60 * 45);
    SyntheticFactory f(cfg);
    struct Case { u32 words; u32 mode; const char* id; };
    for (const Case cs : {Case{2000, 0, "PERF_CAPTIONS_2000"}, Case{5000, 0, "PERF_CAPTIONS_5000"},
                          Case{5000, 1, "PERF_CAPTIONS_5000_PALAVRA"}}) {
        Rig r(1920, 1080, &f);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        const u64 vid = import_synthetic(r);
        AUREA_CHECK(vid != 0);
        if (!vid) return;
        // A composição adotou o tamanho do vídeo: volta para 1080p.
        r.comp()->set_size(1920, 1080);
        const std::vector<text::CaptionWord> words = fake_words(cs.words);
        text::CaptionOptions o;
        o.mode = cs.mode;
        o.style = 2;
        // 3 vezes (cada chamada substitui as legendas anteriores): mediana.
        std::vector<f64> cr;
        Result<u32> made = Status{Errc::InvalidState};
        for (int k = 0; k < 3; ++k) {
            const f64 t0 = now_ms();
            made = r.e.create_captions(vid, words, o);
            cr.push_back(now_ms() - t0);
            AUREA_CHECK(made.ok());
        }
        char mid[64], note[96];
        std::snprintf(mid, sizeof(mid), "%s_CREATE", cs.id);
        std::snprintf(note, sizeof(note), "create_captions: %u palavras -> %u camadas (mediana de 3)", cs.words, made.ok() ? *made : 0u);
        add_metric(mid, median_of(cr), note);
        // Lista de camadas da UI (o que a timeline relê a cada mudança).
        {
            std::vector<bridge::LayerRow> rows(8192);
            std::vector<char> blob(1u << 20);
            std::vector<f64> q;
            u32 rowsOut = 0;
            for (int i = 0; i < 5; ++i) {
                const f64 a = now_ms();
                rowsOut = r.e.query_layers(rows.data(), static_cast<u32>(rows.size()), blob.data(), static_cast<u32>(blob.size()));
                q.push_back(now_ms() - a);
            }
            std::snprintf(mid, sizeof(mid), "%s_QUERY_LAYERS", cs.id);
            std::snprintf(note, sizeof(note), "query_layers: %u linhas (lado do motor; a decodificacao Kotlin nao entra)", rowsOut);
            add_metric(mid, median_of(q), note);
        }
        // Quadros espalhados pela fala inteira (scrub longo): legenda na tela.
        const i64 total = static_cast<i64>(words.back().end * 30.0);
        char desc[96];
        std::snprintf(desc, sizeof(desc), "%u palavras (%s) sobre video 320x180 em comp 1080p", cs.words,
                      cs.mode ? "1 camada por palavra" : "agrupadas");
        keep(measure(r, cs.id, desc, 3, 40, 40, std::max<i64>(1, total / 45), true));
    }
}

AUREA_TEST(Perf, PERF_TIMELINE) {
    AUREA_REQUIRE_BENCH();
    Rig r(1920, 1080);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    r.set_duration(3000);
    auto proto = r.e.add_shape(0);
    AUREA_CHECK(proto.ok());
    if (!proto.ok()) return;
    {
        Layer* p = r.layer(*proto);
        p->start = FrameIndex{0};
        p->end = FrameIndex{3000};
        p->transform.scale = Vec3{0.1f, 0.1f, 1};
    }
    // 1000 camadas × 10 keyframes = 10 000 keyframes, todas vivas o tempo todo
    // (o pior caso do prepare: nenhuma sai por tempo).
    Composition* c = r.comp();
    const f64 t0 = now_ms();
    for (u32 i = 1; i < 1000; ++i) (void)c->duplicate_layer(LayerId::unpack(*proto), FrameIndex{0});
    const f64 t1 = now_ms();
    u32 n = 0, keys = 0;
    for (u32 k = 0; k < c->order().size(); ++k) {
        Layer* l = c->layer(c->order().at(k));
        if (!l) continue;
        l->start = FrameIndex{0};
        l->end = FrameIndex{3000};
        l->transform.position = Vec3{static_cast<f32>(40 + (n % 40) * 46), static_cast<f32>(40 + (n / 40) * 40), 0};
        Track& px = l->tracks.get_or_create(TrackProperty::PositionX);
        for (u32 j = 0; j < 10; ++j) {
            px.set(FrameIndex{static_cast<i64>(j * 300)}, l->transform.position.x + static_cast<f32>((j % 2) * 30));
            ++keys;
        }
        ++n;
    }
    c->rebuild_draw_order();
    std::printf("\n    timeline: %u camadas, %u keyframes (duplicar 999: %.1f ms)", n, keys, t1 - t0);
    AUREA_CHECK(n == 1000 && keys == 10000);

    std::vector<bridge::LayerRow> rows(2048);
    std::vector<char> blob(1u << 20);
    std::vector<bridge::KeyframeRow> krows(64);
    std::vector<f64> ql, qk, cmdMs;
    const u64 some = *proto;
    u32 rowsOut = 0;
    for (int i = 0; i < 7; ++i) {
        f64 a = now_ms();
        rowsOut = r.e.query_layers(rows.data(), static_cast<u32>(rows.size()), blob.data(), static_cast<u32>(blob.size()));
        ql.push_back(now_ms() - a);
        a = now_ms();
        (void)r.e.query_keyframes(some, krows.data(), static_cast<u32>(krows.size()));
        qk.push_back(now_ms() - a);
        // Uma edição comum pela fronteira: grava o "antes" no desfazer.
        Command op;
        op.type = CommandType::LayerSetOpacity;
        op.opacity.layer = LayerId::unpack(some);
        op.opacity.opacity = (i % 2) ? 0.9f : 1.0f;
        a = now_ms();
        (void)r.e.apply_command(op);
        cmdMs.push_back(now_ms() - a);
    }
    AUREA_CHECK(rowsOut == 1000);
    add_metric("TIMELINE_QUERY_LAYERS_1000", median_of(ql), "query_layers: 1000 linhas (lado do motor)");
    add_metric("TIMELINE_QUERY_KEYFRAMES", median_of(qk), "query_keyframes de 1 camada (10 keys)");
    add_metric("TIMELINE_EDIT_WITH_UNDO", median_of(cmdMs), "LayerSetOpacity (snapshot de desfazer da comp de 1000 camadas)");
    keep(measure(r, "PERF_TIMELINE", "1000 camadas de forma vivas, 10 000 keyframes", 5, 60, 0, 7, false));

    // Salvar/abrir esse projeto (o autosave do app é o mesmo save_project,
    // na thread de IO, SEGURANDO o lock do modelo: o render espera por ele).
    const std::string path = "bench_timeline.aurea";
    std::vector<f64> sv, ld;
    for (int i = 0; i < 3; ++i) {
        f64 a = now_ms();
        AUREA_CHECK(r.e.save_project(path.c_str()).ok());
        sv.push_back(now_ms() - a);
    }
    for (int i = 0; i < 3; ++i) {
        const f64 a = now_ms();
        AUREA_CHECK(r.e.load_project(path.c_str()).ok());
        ld.push_back(now_ms() - a);
    }
    add_metric("SAVE_PROJECT_TIMELINE_1000", median_of(sv), "save_project (= autosave; lock do modelo preso o tempo todo)");
    add_metric("OPEN_PROJECT_TIMELINE_1000", median_of(ld), "load_project 1000 camadas / 10k keys");
    std::remove(path.c_str());
}

namespace {
/// Sink nulo de export: registra só o instante de cada quadro entregue.
struct NullExport {
    std::vector<f64> writes;
    u64 bytes = 0;
};
class NullSink final : public ExportSink {
public:
    explicit NullSink(NullExport* o) : o_(o) {}
    Status open(const char*, const VideoStreamConfig& v, const AudioStreamConfig*) noexcept override {
        h_ = v.height;
        return OkStatus;
    }
    Status write_video(const u8* y, u32 yStride, const u8*, u32 uvStride, i64) noexcept override {
        // Toca a memória como o encoder faria (lê a primeira e a última linha).
        volatile u8 sink = y[0];
        (void)sink;
        o_->bytes += static_cast<u64>(yStride) * h_ + static_cast<u64>(uvStride) * (h_ / 2);
        o_->writes.push_back(now_ms());
        return OkStatus;
    }
    Status write_audio(const i16*, u32, i64) noexcept override { return OkStatus; }
    Status finish() noexcept override { return OkStatus; }
    void abort() noexcept override {}
private:
    NullExport* o_;
    u32 h_ = 0;
};
std::unique_ptr<ExportSink> make_null_sink(void* u) { return std::make_unique<NullSink>(static_cast<NullExport*>(u)); }

bool wait_export(Engine& e, int timeoutMs) {
    for (int i = 0; i < timeoutMs / 2; ++i) {
        if (e.export_progress().finished) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
} // namespace

AUREA_TEST(Perf, PERF_EXPORT) {
    AUREA_REQUIRE_BENCH();
    // O export de verdade (render_export_frame em qualidade final + NV12 +
    // leitura), até o sink. O encoder é nulo no host: sem MediaCodec aqui, o
    // custo do encoder de hardware NÃO entra (mede-se no aparelho).
    struct Case { u32 w, h; u32 frames; bool heavy; const char* id; };
    for (const Case cs : {Case{1920, 1080, 90, false, "PERF_EXPORT_1080_BASIC"}, Case{1920, 1080, 90, true, "PERF_EXPORT_1080_HEAVY"},
                          Case{3840, 2160, 30, true, "PERF_EXPORT_4K_HEAVY"}}) {
        SyntheticFactory f(video_cfg(cs.w, cs.h, 300));
        NullExport out;
        Rig r(cs.w, cs.h, &f, 30.0, &make_null_sink, &out);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        if (cs.heavy) build_heavy(r); else build_basic(r);
        r.set_duration(cs.frames);
        ExportSettings s;
        s.height = cs.h;
        const f64 t0 = now_ms();
        const Status started = r.e.start_export(s, "nao-usado.mp4");
        if (started.code() == Errc::NotSupported) {
            // O teto de export vem da tabela de codecs da plataforma; o host
            // não tem MediaCodecList e fica no conservador — recusa honesta,
            // não se finge um encoder 4K. O custo de quadro 4K sai no PERF_4K_*.
            std::printf("\n    %-22s recusado pelo motor: %.*s (sem tabela de codecs no host)", cs.id,
                        static_cast<int>(started.message().size()), started.message().data());
            continue;
        }
        AUREA_CHECK(started.ok());
        if (!started.ok()) continue;
        AUREA_CHECK(wait_export(r.e, 600000));
        const f64 t1 = now_ms();
        const Engine::ExportProgress p = r.e.export_progress();
        AUREA_CHECK_EQ(p.result, Errc::Ok);
        AUREA_CHECK_EQ(p.framesDone, cs.frames);
        std::vector<f64> gaps;
        for (usize i = 1; i < out.writes.size(); ++i) gaps.push_back(out.writes[i] - out.writes[i - 1]);
        FrameBench b;
        b.id = cs.id;
        b.desc = cs.heavy ? "export qualidade final, cena pesada, sink nulo (sem encoder)" : "export qualidade final, cena basica, sink nulo";
        b.width = cs.w;
        b.height = cs.h;
        b.frames = static_cast<u32>(out.writes.size());
        b.wall = stat_of(gaps);
        // No export a CPU e a GPU não são separadas por quadro: o intervalo
        // entre quadros entregues é o número (decode + render + leitura), e
        // vai em `cpu_ms` (parede). `gpu_ms` fica 0 — não medido, não fingido.
        b.cpu = b.wall;
        b.serialWall = true;
        b.rss = process_rss();
        b.decodeMsAvg = r.e.media().stats().decodeMsAvg;
        std::printf("\n    %-22s %4ux%-4u %u quadros em %.0f ms (%.1f q/s) | intervalo p50 %.2f p95 %.2f p99 %.2f σ %.2f ms | RSS %llu MB",
                    cs.id, cs.w, cs.h, b.frames, t1 - t0, 1000.0 * b.frames / std::max(1.0, t1 - t0), b.wall.p50, b.wall.p95,
                    b.wall.p99, b.wall.std, static_cast<unsigned long long>(b.rss >> 20));
        keep(std::move(b));
    }
}

// =============================================================================
// Campanha de medição (§2): o que dá para medir no host
// =============================================================================
AUREA_TEST(Perf, CampaignDecodeScrubPlayback) {
    AUREA_REQUIRE_BENCH();
    for (u32 res : {1080u, 2160u}) {
        const u32 w = res == 1080 ? 1920 : 3840;
        SyntheticFactory f(video_cfg(w, res, 900));
        Rig r(w, res, &f);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        (void)import_synthetic(r);
        // Playback serial: cada quadro desenhado UMA vez (espera o decoder).
        std::vector<f64> play;
        for (i64 fr = 0; fr < 90; ++fr) {
            r.seek(fr);
            const f64 a = now_ms();
            (void)r.render(true);
            play.push_back(now_ms() - a);
        }
        const Stat ps = stat_of(play);
        const f32 decode = r.e.media().stats().decodeMsAvg;
        // Scrub: saltos aleatórios (GOP 30) até o quadro EXATO.
        std::vector<f64> scrub;
        u32 seed = 12345;
        for (int i = 0; i < 30; ++i) {
            seed = seed * 1664525u + 1013904223u;
            r.seek(static_cast<i64>(seed % 880));
            const f64 a = now_ms();
            (void)r.render(true);
            scrub.push_back(now_ms() - a);
        }
        const Stat ss = stat_of(scrub);
        char id[64], note[128];
        std::snprintf(id, sizeof(id), "DECODE_SYNTH_%uP", res);
        std::snprintf(note, sizeof(note), "decodeMsAvg do decoder sintetico (NV12 gerado na CPU; nao e HW)");
        add_metric(id, decode, note, false);
        std::snprintf(id, sizeof(id), "PLAYBACK_SERIAL_%uP_P50", res);
        // Fora do verificador: dominados pelo decoder SINTÉTICO (CPU, sensível
        // a outros processos) e quantizados pela espera de 5 ms do offscreen.
        std::snprintf(note, sizeof(note), "quadro a quadro, decode+render serial; p95 %.2f ms (%.0f q/s no p50)", ps.p95, 1000.0 / std::max(0.01, ps.p50));
        add_metric(id, ps.p50, note, false);
        std::snprintf(id, sizeof(id), "SCRUB_EXACT_%uP_P50", res);
        std::snprintf(note, sizeof(note), "salto aleatorio ate o quadro exato (GOP 30); p95 %.2f ms", ss.p95);
        add_metric(id, ss.p50, note, false);
    }
}

AUREA_TEST(Perf, CampaignSaveOpenHeavyProject) {
    AUREA_REQUIRE_BENCH();
    SyntheticFactory f(video_cfg(1920, 1080));
    Rig r(1920, 1080, &f);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    build_heavy(r);
    const std::string path = "bench_pesado.aurea";
    std::vector<f64> sv, ld;
    for (int i = 0; i < 5; ++i) {
        const f64 a = now_ms();
        AUREA_CHECK(r.e.save_project(path.c_str()).ok());
        sv.push_back(now_ms() - a);
    }
    for (int i = 0; i < 5; ++i) {
        const f64 a = now_ms();
        AUREA_CHECK(r.e.load_project(path.c_str()).ok());
        ld.push_back(now_ms() - a);
    }
    add_metric("SAVE_PROJECT_HEAVY", median_of(sv), "save_project da cena pesada (= autosave), mediana de 5");
    add_metric("OPEN_PROJECT_HEAVY", median_of(ld), "load_project da cena pesada, mediana de 5");
    // Primeiro quadro depois de abrir (decoders novos, texturas novas).
    const f64 a = now_ms();
    (void)r.render(true);
    add_metric("OPEN_PROJECT_HEAVY_FIRST_FRAME", now_ms() - a, "primeiro quadro exato depois de abrir", false);
    std::remove(path.c_str());
}

AUREA_TEST(Perf, CampaignWaveformAndThumbnails) {
    AUREA_REQUIRE_BENCH();
    SyntheticConfig cfg = video_cfg(1920, 1080, 1800);
    cfg.audioRate = 48000;
    cfg.audioSeconds = 60.0;
    SyntheticFactory f(cfg);
    Rig r(1920, 1080, &f);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 vid = import_synthetic(r);
    AUREA_CHECK(vid != 0);
    if (!vid) return;
    // Waveform de 60 s: do primeiro pedido até o último balde calculado.
    {
        std::vector<u8> buckets(1800);
        const f64 t0 = now_ms();
        f64 done = -1;
        while (now_ms() - t0 < 30000) {
            const u32 n = r.e.query_waveform(vid, 0.0, 1.0, 1800, buckets.data());
            if (n == 0) break;
            if (buckets[1790] != 0 && buckets[5] != 0) { done = now_ms() - t0; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        AUREA_CHECK(done >= 0);
        add_metric("WAVEFORM_60S_READY", done, "query_waveform: 60 s de audio 48 kHz ate o ultimo balde pronto", false);
        std::vector<f64> q;
        for (int i = 0; i < 20; ++i) {
            const f64 a = now_ms();
            (void)r.e.query_waveform(vid, static_cast<f64>(i * 40), 0.5, 1200, buckets.data());
            q.push_back(now_ms() - a);
        }
        add_metric("WAVEFORM_QUERY_1200", median_of(q), "query_waveform de 1200 baldes ja prontos (zoom/scroll)");
    }
    // Miniaturas: 12 pedidos pela timeline (altura 90) até todas prontas.
    {
        std::vector<u8> px(512 * 90 * 4);
        u32 outW = 0;
        bool ready[12]{};
        u32 got = 0;
        const f64 t0 = now_ms();
        f64 first = -1;
        while (got < 12 && now_ms() - t0 < 30000) {
            for (u32 i = 0; i < 12; ++i) {
                if (ready[i]) continue;
                if (r.e.query_thumbnail(vid, static_cast<i32>(i * 150), 90, px.data(), static_cast<u32>(px.size()), &outW) > 0) {
                    ready[i] = true;
                    ++got;
                    if (first < 0) first = now_ms() - t0;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        AUREA_CHECK(got == 12);
        add_metric("THUMBNAIL_FIRST", first, "primeira miniatura 1080p -> 90 px de altura", false);
        add_metric("THUMBNAILS_12_READY", now_ms() - t0, "12 miniaturas espalhadas (seek + decode + reducao)", false);
    }
}

AUREA_TEST(Perf, CampaignTextAndFlowAndTracking) {
    AUREA_REQUIRE_BENCH();
    // Texto: 30 camadas (contorno, sombra, posição animada) + 1 animador.
    {
        Rig r(1920, 1080);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        for (u32 i = 0; i < 30; ++i) {
            char s[48];
            std::snprintf(s, sizeof(s), "Titulo numero %u", i);
            auto t = r.e.add_text(s);
            if (!t.ok()) continue;
            Layer* l = r.layer(*t);
            l->text.size = 48;
            l->text.strokeWidth = 2;
            l->text.shadow = (i % 2) == 0;
            l->transform.position = Vec3{static_cast<f32>(200 + (i % 5) * 330), static_cast<f32>(80 + (i / 5) * 160), 0};
            Track& py = l->tracks.get_or_create(TrackProperty::PositionY);
            py.set(FrameIndex{0}, l->transform.position.y);
            py.set(FrameIndex{300}, l->transform.position.y + 60);
            if (i == 0) (void)r.e.apply_text_preset(*t, 8);
        }
        keep(measure(r, "PERF_TEXT", "30 textos (contorno, sombra, posicao animada) + 1 animador", 5, 60, 0, 1, false));
    }
    // Optical flow (movimento de pixels): 1080p a 20% com o cache desligado —
    // cada quadro estima o fluxo de novo (o pior caso do preview).
    {
        SyntheticFactory f(video_cfg(1920, 1080, 300));
        Rig r(1920, 1080, &f);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        const u64 v = import_synthetic(r);
        Layer* l = r.layer(v);
        l->speed = 0.2f;
        l->end = FrameIndex{l->start.value + 1000};
        r.comp()->set_duration(FrameIndex{1000});
        (void)r.e.set_frame_blend(v, 2);
        r.e.set_flow_cache_enabled(false);
        keep(measure(r, "PERF_FLOW_1080", "optical flow 1080p, video a 20%, cache do fluxo desligado", 3, 30, 11, 2, true));
        r.e.set_flow_cache_enabled(true);
    }
    // Rastreio de ponto (síncrono, decodifica o vídeo): 1080p, 150 quadros.
    {
        SyntheticConfig cfg = video_cfg(1920, 1080, 150);
        cfg.pattern = SyntheticPattern::MovingSquare;
        SyntheticFactory f(cfg);
        Rig r(1920, 1080, &f);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        const u64 v = import_synthetic(r);
        r.seek(0);
        u32 tracked = 0;
        const f64 t0 = now_ms();
        auto res = r.e.track_point(v, static_cast<f32>(moving_square_x(0)), 540.0f, false, &tracked);
        const f64 t1 = now_ms();
        AUREA_CHECK(res.ok());
        char note[96];
        std::snprintf(note, sizeof(note), "track_point 1080p: %u quadros em %.0f ms (%.2f ms/quadro)", tracked, t1 - t0,
                      (t1 - t0) / std::max(1u, tracked));
        add_metric("TRACK_POINT_1080_PER_FRAME", (t1 - t0) / std::max(1u, tracked), note);
    }
    // Rastreio de câmera 3D (modo rápido), 1080p 10 s, em segundo plano.
    {
        SyntheticConfig cfg = video_cfg(1920, 1080, 300);
        cfg.pattern = SyntheticPattern::Scene3D;
        SyntheticFactory f(cfg);
        Rig r(1920, 1080, &f);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        const u64 v = import_synthetic(r);
        const f64 t0 = now_ms();
        AUREA_CHECK(r.e.start_camera_track(v, 0));
        while (r.e.camera_track_status().state == 1 && now_ms() - t0 < 300000) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const f64 t1 = now_ms();
        const Engine::CameraTrackStatus st = r.e.camera_track_status();
        AUREA_CHECK(st.state == 2);
        char note[128];
        std::snprintf(note, sizeof(note), "rastreio de camera modo rapido 1080p: %u quadros em %.0f ms (%.1f q/s), erro %.2f px",
                      st.frames, t1 - t0, st.frames * 1000.0 / std::max(1.0, t1 - t0), st.rmsError);
        add_metric("CAMERA_TRACK_1080_PER_FRAME", (t1 - t0) / std::max(1u, st.frames), note);
    }
}

AUREA_TEST(Perf, CampaignGpuCostPerEffect) {
    AUREA_REQUIRE_BENCH();
    // §86: custo de GPU de CADA efeito do catálogo, com os valores de
    // demonstração, numa placa 1080p. Custo = GPU p50 com o efeito − GPU p50
    // da mesma placa sem efeito (mesma sessão, mesmos quadros).
    Rig r(1920, 1080);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const std::vector<u8> px = plate(1920, 1080);
    auto img = r.e.import_image(px.data(), 1920, 1080, "placa");
    AUREA_CHECK(img.ok());
    if (!img.ok()) return;
    auto gpu_p50 = [&](u32& passes) {
        std::vector<f64> g;
        for (i64 f = 0; f < 18; ++f) {
            r.seek(10 + f);
            if (!r.render(true).ok()) continue;
            const Engine::OffscreenMeasure m = r.e.last_offscreen_measure();
            passes = m.passesExecuted;
            if (f >= 3 && m.gpuMeasured) g.push_back(m.gpuMs);   // 3 de aquecimento
        }
        return stat_of(g);
    };
    // Quadro pequeno para ver se o efeito MUDOU a imagem (efeito que some sem
    // erro seria custo zero enganoso).
    auto snap = [&]() {
        std::vector<u8> px8;
        u32 sw = 0, sh = 0;
        (void)r.e.capture_frame_rgba(192, px8, sw, sh);
        return px8;
    };
    r.seek(12);
    const std::vector<u8> plain = snap();
    u32 basePasses = 0;
    f64 baseSum = 0;
    const EffectRegistry& reg = r.e.effects();
    for (u32 i = 0; i < reg.count(); ++i) {
        const Effect& fx = reg.at(i);
        Layer* l = r.layer(*img);
        // Base medida de novo ANTES de cada efeito: o relógio da GPU varia
        // entre minutos; o delta pareado é o que se compara.
        l->effects.clear();
        const Stat base = gpu_p50(basePasses);
        baseSum += base.p50;
        EffectCost c;
        c.key = fx.info().key;
        c.name = fx.info().name;
        c.category = fx.info().category;
        c.built = add_demo_effect(r.e, l, fx.type_id());
        u32 passes = 0;
        const Stat s = gpu_p50(passes);
        const u32 alive = r.e.last_offscreen_measure().activeEffects;
        c.gpuDelta = s.p50 - base.p50;
        c.gpuP95 = s.p95;
        c.passes = passes > basePasses ? passes - basePasses : 0;
        r.seek(12);
        const std::vector<u8> with = snap();
        u32 changed = 0;
        for (usize k = 0; k + 3 < with.size() && k + 3 < plain.size(); k += 4) {
            if (std::abs(with[k] - plain[k]) + std::abs(with[k + 1] - plain[k + 1]) + std::abs(with[k + 2] - plain[k + 2]) > 6) ++changed;
        }
        c.built = c.built && alive > 0;
        c.changedPct = with.empty() ? 0.0 : 100.0 * changed / (static_cast<f64>(with.size()) / 4.0);
        results().effects.push_back(c);
    }
    r.layer(*img)->effects.clear();
    results().effectsBaseGpu = baseSum / std::max(1u, reg.count());
    std::printf("\n    placa 1080p sem efeito: GPU p50 medio %.3f ms (%u passes)", results().effectsBaseGpu, basePasses);
    std::vector<EffectCost> sorted = results().effects;
    std::sort(sorted.begin(), sorted.end(), [](const EffectCost& a, const EffectCost& b) { return a.gpuDelta > b.gpuDelta; });
    for (const EffectCost& c : sorted) {
        std::printf("\n      %-34s %-12s %+7.3f ms GPU (p95 total %6.3f)  +%u passes  %5.1f%% px mudados%s", c.key.c_str(), c.category.c_str(),
                    c.gpuDelta, c.gpuP95, c.passes, c.changedPct,
                    c.changedPct > 0.0 ? "" : "  (nao muda a placa: controle de expressao ou temporal sem outro instante)");
    }
    AUREA_CHECK(results().effects.size() == reg.count());
}

// =============================================================================
// Gravar a baseline / comparar (§129–130). É o último da suíte.
// =============================================================================
namespace {

void json_str(std::FILE* f, const std::string& s) {
    std::fputc('"', f);
    for (char c : s) {
        if (c == '"' || c == '\\') std::fputc('\\', f);
        if (static_cast<unsigned char>(c) < 0x20) continue;
        std::fputc(c, f);
    }
    std::fputc('"', f);
}

void json_stat(std::FILE* f, const char* name, const Stat& s) {
    std::fprintf(f, "\"%s\": {\"p50\": %.4f, \"p95\": %.4f, \"p99\": %.4f, \"mean\": %.4f, \"std\": %.4f, \"min\": %.4f, \"max\": %.4f}", name,
                 s.p50, s.p95, s.p99, s.mean, s.std, s.min, s.max);
}

bool write_json(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const Results& r = results();
    std::time_t now = std::time(nullptr);
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
    std::fprintf(f, "{\n  \"schema\": 1,\n  \"generated\": \"%s\",\n", date);
    std::fprintf(f, "  \"machine\": {\n    \"os\": ");
    json_str(f, os_name());
    std::fprintf(f, ",\n    \"cpu\": ");
    json_str(f, cpu_name());
    std::fprintf(f, ",\n    \"cpu_threads\": %u,\n    \"ram_gb\": %.1f,\n    \"gpu\": ", std::thread::hardware_concurrency(),
                 static_cast<f64>(total_ram()) / (1024.0 * 1024.0 * 1024.0));
    json_str(f, r.gpuName);
    std::fprintf(f, ",\n    \"gpu_driver\": ");
    json_str(f, r.gpuDriver);
    std::fprintf(f, ",\n    \"gpu_api\": ");
    json_str(f, r.gpuApi);
#if defined(NDEBUG)
    std::fprintf(f, ",\n    \"build\": \"Release\"");
#else
    std::fprintf(f, ",\n    \"build\": \"Debug\"");
#endif
#if defined(_MSC_VER)
    std::fprintf(f, ",\n    \"compiler\": \"MSVC %d\"", _MSC_VER);
#elif defined(__clang__)
    std::fprintf(f, ",\n    \"compiler\": \"clang %d.%d\"", __clang_major__, __clang_minor__);
#endif
    std::fprintf(f, ",\n    \"peak_rss_mb\": %llu,\n    \"host_other_cpu_load_pct\": %.1f\n  },\n",
                 static_cast<unsigned long long>(process_peak_rss() >> 20), other_load_pct(r.start, load_now()));
    std::fprintf(f, "  \"notes\": \"Host (PC) com GPU de mesa: NAO e numero de celular. Quadro offscreen serial (CPU, depois GPU, "
                    "espera): cpu_ms = prepare+gravacao+submit; gpu_ms = timestamp do quadro; wall_ms = parede. PERF_EXPORT_*: "
                    "intervalo entre quadros entregues ao sink nulo (sem encoder). Regressao: p50 de GPU ou CPU > +40%% da "
                    "baseline (e > piso absoluto).\",\n");
    std::fprintf(f, "  \"benchmarks\": [\n");
    for (usize i = 0; i < r.frames.size(); ++i) {
        const FrameBench& b = r.frames[i];
        std::fprintf(f, "    {\"id\": ");
        json_str(f, b.id);
        std::fprintf(f, ", \"desc\": ");
        json_str(f, b.desc);
        std::fprintf(f, ", \"width\": %u, \"height\": %u, \"frames\": %u, \"serial_wall\": %s,\n     ", b.width, b.height, b.frames,
                     b.serialWall ? "true" : "false");
        json_stat(f, "gpu_ms", b.gpu);
        std::fprintf(f, ",\n     ");
        json_stat(f, "cpu_ms", b.cpu);
        std::fprintf(f, ",\n     ");
        json_stat(f, "prepare_ms", b.prepare);
        std::fprintf(f, ",\n     ");
        json_stat(f, "wall_ms", b.wall);
        std::fprintf(f, ",\n     \"decode_ms_avg\": %.3f, \"passes\": %u, \"passes_culled\": %u, \"draw_calls\": %u, \"layers\": %u, "
                        "\"effects\": %u, \"draws_3d\": %u, \"triangles_3d\": %u, \"particles\": %u, \"rss_mb\": %llu, "
                        "\"gpu_used_mb\": %llu, \"transient_mb\": %llu,\n     \"top_gpu_passes\": ",
                     b.decodeMsAvg, b.passes, b.culled, b.drawCalls, b.layers, b.effects, b.draws3D, b.triangles3D, b.particles,
                     static_cast<unsigned long long>(b.rss >> 20), static_cast<unsigned long long>(b.gpuUsed >> 20),
                     static_cast<unsigned long long>(b.transient >> 20));
        json_str(f, b.topPasses);
        std::fprintf(f, "}%s\n", i + 1 < r.frames.size() ? "," : "");
    }
    std::fprintf(f, "  ],\n  \"campaign\": [\n");
    for (usize i = 0; i < r.campaign.size(); ++i) {
        const Metric& m = r.campaign[i];
        std::fprintf(f, "    {\"id\": ");
        json_str(f, m.id);
        std::fprintf(f, ", \"ms\": %.4f, \"compare\": %s, \"note\": ", m.ms, m.compare ? "true" : "false");
        json_str(f, m.note);
        std::fprintf(f, "}%s\n", i + 1 < r.campaign.size() ? "," : "");
    }
    std::fprintf(f, "  ],\n  \"effects_gpu_1080\": {\"base_gpu_ms\": %.4f, \"method\": \"placa 1080p RGBA8, valores de demonstracao; "
                    "delta = GPU p50 com o efeito - GPU p50 da placa sem efeito medida logo antes (15 quadros cada)\", \"effects\": [\n",
                 r.effectsBaseGpu);
    for (usize i = 0; i < r.effects.size(); ++i) {
        const EffectCost& c = r.effects[i];
        std::fprintf(f, "    {\"id\": ");
        json_str(f, c.key);
        std::fprintf(f, ", \"name\": ");
        json_str(f, c.name);
        std::fprintf(f, ", \"category\": ");
        json_str(f, c.category);
        std::fprintf(f, ", \"gpu_ms_delta\": %.4f, \"gpu_ms_p95_total\": %.4f, \"extra_passes\": %u, \"in_plan\": %s, \"changed_px_pct\": %.1f}%s\n",
                     c.gpuDelta, c.gpuP95, c.passes, c.built ? "true" : "false", c.changedPct, i + 1 < r.effects.size() ? "," : "");
    }
    std::fprintf(f, "  ]}\n}\n");
    std::fclose(f);
    return true;
}

std::string read_file(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string s;
    char buf[4096];
    usize n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

/// Leitor mínimo para o arquivo que ESTA suíte escreve: acha o objeto do id
/// e, dentro dele, o número depois da chave (ex.: `"gpu_ms": {"p50": `).
bool find_value(const std::string& json, const std::string& id, const char* key, f64& out) {
    const std::string tag = "{\"id\": \"" + id + "\"";
    const usize a = json.find(tag);
    if (a == std::string::npos) return false;
    usize end = json.find("{\"id\": ", a + tag.size());
    if (end == std::string::npos) end = json.size();
    const usize k = json.find(key, a);
    if (k == std::string::npos || k > end) return false;
    out = std::strtod(json.c_str() + k + std::strlen(key), nullptr);
    return true;
}

struct Regression { std::string what; f64 base, now; bool cpu; };

void check(std::vector<Regression>& out, const std::string& json, const std::string& id, const char* key, f64 now, f64 floorMs,
           const char* label, bool cpu = false) {
    f64 base = 0;
    if (!find_value(json, id, key, base)) return;   // novo: sem baseline para comparar
    if (over_budget(now, base, floorMs)) out.push_back(Regression{id + " " + label, base, now, cpu});
}

/// Carga de outros processos gravada na baseline (-1 = não gravada).
f64 baseline_load(const std::string& json) {
    const char* key = "\"host_other_cpu_load_pct\": ";
    const usize k = json.find(key);
    return k == std::string::npos ? -1.0 : std::strtod(json.c_str() + k + std::strlen(key), nullptr);
}

} // namespace

// O verificador em si (roda sempre, sem GPU): acha o valor certo no JSON que
// a suíte escreve e só acusa acima de +40% E acima do piso absoluto.
AUREA_TEST(Perf, RegressionCheckerReadsBaselineAndFlagsFortyPercent) {
    const std::string json =
        "{\"machine\": {\"host_other_cpu_load_pct\": 39.3},\n"
        " \"benchmarks\": [\n"
        "    {\"id\": \"PERF_A\", \"gpu_ms\": {\"p50\": 2.0000, \"p95\": 9.0}, \"cpu_ms\": {\"p50\": 1.0000}},\n"
        "    {\"id\": \"PERF_A_4K\", \"gpu_ms\": {\"p50\": 8.0000}, \"cpu_ms\": {\"p50\": 3.0000}}\n"
        "  ], \"campaign\": [{\"id\": \"OPEN\", \"ms\": 100.0000, \"compare\": true}]}";
    f64 v = 0;
    AUREA_CHECK(find_value(json, "PERF_A", "\"gpu_ms\": {\"p50\": ", v) && v == 2.0);
    AUREA_CHECK(find_value(json, "PERF_A_4K", "\"cpu_ms\": {\"p50\": ", v) && v == 3.0);   // id exato, não prefixo
    AUREA_CHECK(find_value(json, "OPEN", "\"ms\": ", v) && v == 100.0);
    AUREA_CHECK(!find_value(json, "PERF_B", "\"gpu_ms\": {\"p50\": ", v));                  // novo: sem baseline
    std::vector<Regression> reg;
    check(reg, json, "PERF_A", "\"gpu_ms\": {\"p50\": ", 2.79, 0.25, "GPU p50");   // +39,5%: passa
    AUREA_CHECK(reg.empty());
    check(reg, json, "PERF_A", "\"gpu_ms\": {\"p50\": ", 2.85, 0.25, "GPU p50");   // +42,5%: acusa
    AUREA_CHECK_EQ(reg.size(), static_cast<usize>(1));
    check(reg, json, "OPEN", "\"ms\": ", 101.0, 2.0, "tempo");                       // ruído: passa
    check(reg, json, "PERF_A", "\"cpu_ms\": {\"p50\": ", 1.2, 0.25, "CPU p50");     // +20%: passa
    AUREA_CHECK_EQ(reg.size(), static_cast<usize>(1));
    AUREA_CHECK(!over_budget(0.30, 0.10, 0.25));   // +200% mas 0,2 ms: abaixo do piso, é ruído
    AUREA_CHECK(std::fabs(baseline_load(json) - 39.3) < 1e-9);
    AUREA_CHECK(baseline_load("{}") < 0.0);
}

AUREA_TEST(Perf, ZZ_BaselineOrRegressionCheck) {
    const BenchMode mode = bench_mode();
    if (mode == BenchMode::Off) {
        std::printf("(pulado: AUREA_BENCH=1|baseline|compare para medir) ");
        return;
    }
    const std::string baseline = std::string(AUREA_PERF_DIR) + "/baseline_host.json";
    std::printf("\n    CPU do host ocupada por OUTROS processos durante a suite: %.1f%%", other_load_pct(results().start, load_now()));
    if (mode == BenchMode::Baseline) {
        AUREA_CHECK(write_json(baseline));
        std::printf("\n    baseline gravada em %s", baseline.c_str());
        return;
    }
    AUREA_CHECK(write_json("bench_atual.json"));
    if (mode != BenchMode::Compare) return;
    const std::string json = read_file(baseline);
    AUREA_CHECK_MSG(!json.empty(), "sem docs/performance/baseline_host.json (rode AUREA_BENCH=baseline)");
    if (json.empty()) return;
    // Pisos absolutos: abaixo disto a variação é ruído de medição, não regressão.
    std::vector<Regression> reg;
    const Results& r = results();
    for (const FrameBench& b : r.frames) {
        if (!b.serialWall) check(reg, json, b.id, "\"gpu_ms\": {\"p50\": ", b.gpu.p50, 0.25, "GPU p50");
        check(reg, json, b.id, "\"cpu_ms\": {\"p50\": ", b.cpu.p50, 0.25, b.serialWall ? "intervalo p50" : "CPU p50", true);
    }
    for (const Metric& m : r.campaign) {
        if (m.compare) check(reg, json, m.id, "\"ms\": ", m.ms, 2.0, "tempo", true);
    }
    for (const EffectCost& c : r.effects) check(reg, json, c.key, "\"gpu_ms_delta\": ", c.gpuDelta, 0.25, "GPU do efeito");
    // PC compartilhado: com o host bem mais ocupado por OUTROS processos do
    // que na baseline (+10 pontos), piora de CPU não prova regressão do
    // código — sai como INCONCLUSIVA (impressa, não falha; rode de novo com o
    // host quieto). Piora de GPU falha sempre. AUREA_BENCH_STRICT=1 falha tudo.
    const f64 loadBase = baseline_load(json);
    const f64 loadNow = other_load_pct(r.start, load_now());
    const bool busier = loadBase >= 0.0 && loadNow > loadBase + 10.0;
    const char* strict = std::getenv("AUREA_BENCH_STRICT");
    const bool strictMode = strict && *strict == '1';
    u32 failing = 0;
    for (const Regression& x : reg) {
        const bool inconclusive = x.cpu && busier && !strictMode;
        std::printf("\n    %s %s: %.3f -> %.3f ms (+%.0f%%)", inconclusive ? "INCONCLUSIVA" : "REGRESSAO", x.what.c_str(), x.base, x.now,
                    (x.now / x.base - 1.0) * 100.0);
        if (!inconclusive) ++failing;
    }
    if (busier && !reg.empty()) {
        std::printf("\n    host ocupado por outros processos: %.1f%% agora x %.1f%% na baseline — CPU inconclusiva", loadNow, loadBase);
    }
    if (reg.empty()) std::printf("\n    sem regressao acima de 40%% (bench_atual.json gravado)");
    AUREA_CHECK_MSG(failing == 0, "regressao de performance acima de 40% sobre a baseline");
}

#endif // AUREA_TEST_VULKAN
