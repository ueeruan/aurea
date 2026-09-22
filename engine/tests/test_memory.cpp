// =============================================================================
//  Fase 8B — memória, caches, pressão do sistema, jobs e vazamentos.
//
//  Dois tipos de teste aqui:
//   - CONTRATO (Memory8B.*, Jobs8B.*): o comportamento que a fase exige
//     (orçamento compartilhado, trim na ordem do spec sem tocar no projeto,
//     worker ocioso dormindo, fundo sem atrasar o quadro, sem starvation);
//   - MEDIÇÃO (Perf8B.*): imprimem os números reais do relatório
//     (docs/performance/PHASE_8_REPORT.md §8B). O mesmo arquivo compila
//     contra o motor de ANTES da fase (sem AUREA_MEMORY_API_8B) para medir o
//     "antes" com o mesmo código de medida.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/media/VideoSource.hpp"
#include "aurea/memory/MemoryManager.hpp"

#if defined(AUREA_TEST_VULKAN)
#include "VulkanBackend.hpp"
#include "aurea/export/ExportSink.hpp"
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <dirent.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

// -----------------------------------------------------------------------------
// Números do processo (o que o SO vê, não o que o motor acha que usa).
// -----------------------------------------------------------------------------
struct ProcSample {
    f64 cpuMs = 0.0;        ///< usuário + kernel, desde o início do processo
    u64 privateBytes = 0;   ///< memória privada comprometida
    u64 workingSet = 0;     ///< RSS
    u32 threads = 0;
    u32 handles = 0;        ///< handles (Windows) / descritores (POSIX)
};

ProcSample sample_process() {
    ProcSample s;
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
        const auto ft = [](const FILETIME& f) { return (static_cast<u64>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        s.cpuMs = static_cast<f64>(ft(k) + ft(u)) / 10'000.0;
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        s.privateBytes = pmc.PrivateUsage;
        s.workingSet = pmc.WorkingSetSize;
    }
    DWORD h = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &h)) s.handles = h;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        const DWORD pid = GetCurrentProcessId();
        if (Thread32First(snap, &te)) {
            do { if (te.th32OwnerProcessID == pid) ++s.threads; } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
#else
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        s.cpuMs = static_cast<f64>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000.0
                + static_cast<f64>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000.0;
    }
    if (std::FILE* f = std::fopen("/proc/self/statm", "r")) {
        unsigned long size = 0, rss = 0;
        if (std::fscanf(f, "%lu %lu", &size, &rss) == 2) {
            s.workingSet = static_cast<u64>(rss) * static_cast<u64>(sysconf(_SC_PAGESIZE));
            s.privateBytes = s.workingSet;
        }
        std::fclose(f);
    }
    auto count_dir = [](const char* p) {
        u32 n = 0;
        if (DIR* d = opendir(p)) {
            while (dirent* de = readdir(d)) if (de->d_name[0] != '.') ++n;
            closedir(d);
        }
        return n;
    };
    s.threads = count_dir("/proc/self/task");
    s.handles = count_dir("/proc/self/fd");
#endif
    return s;
}

f64 now_ms() {
    return std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void busy_ms(f64 ms) {
    const f64 end = now_ms() + ms;
    volatile u64 x = 0;
    while (now_ms() < end) x = x + 1;
}

f64 percentile(std::vector<f64> v, f64 p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const usize i = std::min(v.size() - 1, static_cast<usize>(p * static_cast<f64>(v.size() - 1) + 0.5));
    return v[i];
}

} // namespace

// =============================================================================
// JobSystem
// =============================================================================
AUREA_TEST(Perf8B, IdleJobPoolBurnsNoCpu) {
    // Pool subido e SEM trabalho, 1 s. Antes da Fase 8 cada worker girava em
    // yield() — um núcleo inteiro por worker com o app parado (§38 bateria).
    JobSystem jobs;
    AUREA_CHECK(jobs.start(4).ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));   // os workers chegam ao laço
    const ProcSample a = sample_process();
    const f64 t0 = now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const ProcSample b = sample_process();
    const f64 wall = now_ms() - t0;
    const f64 cpu = b.cpuMs - a.cpuMs;
    std::printf("\n    [8B] pool ocioso: %u workers, CPU %.0f ms em %.0f ms de parede (%.0f%% de um nucleo) ",
                jobs.worker_count(), cpu, wall, 100.0 * cpu / wall);
    jobs.stop();
#if defined(AUREA_MEMORY_API_8B)
    // Dormindo, o custo é o giro curto antes de dormir: muito menos que 1 núcleo.
    AUREA_CHECK_MSG(cpu < 0.10 * wall, "worker ocioso nao pode girar");
#endif
}

AUREA_TEST(Perf8B, IdleEngineBurnsNoCpu) {
    // O motor inteiro subido (headless, workers automáticos) e parado.
    Engine e;
    EngineConfig ec;
    ec.workerCount = 0;   // o que o aparelho recomenda
    ec.memoryBudgetBytes = 256ull << 20;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "ocioso").ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const ProcSample a = sample_process();
    const f64 t0 = now_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const ProcSample b = sample_process();
    const f64 wall = now_ms() - t0;
    const f64 cpu = b.cpuMs - a.cpuMs;
    std::printf("\n    [8B] motor ocioso: %u workers, %u threads no processo, CPU %.0f ms em %.0f ms (%.0f%% de um nucleo) ",
                e.jobs().worker_count(), b.threads, cpu, wall, 100.0 * cpu / wall);
    e.shutdown();
#if defined(AUREA_MEMORY_API_8B)
    AUREA_CHECK_MSG(cpu < 0.10 * wall, "motor parado nao pode gastar CPU");
#endif
}

namespace {
struct LatencyProbe {
    f64 submitMs = 0.0;
    std::atomic<f64>* out = nullptr;
};
std::atomic<int> g_bgDone{0};
void bg_task(void*, JobContext&) { busy_ms(30.0); g_bgDone.fetch_add(1); }
void probe_task(void* ud, JobContext&) {
    auto* p = static_cast<LatencyProbe*>(ud);
    p->out->store(now_ms() - p->submitMs);
}
} // namespace

AUREA_TEST(Perf8B, BackgroundDoesNotDelayHighPriority) {
    // 12 tarefas LOW de 30 ms ocupando o pool; no meio delas, 40 tarefas HIGH
    // (o "quadro atual"). Mede quanto cada HIGH esperou para COMEÇAR.
    JobSystem jobs;
    AUREA_CHECK(jobs.start(4).ok());
    g_bgDone.store(0);
    for (int i = 0; i < 12; ++i) AUREA_CHECK(jobs.submit(JobPriority::Low, bg_task, nullptr).valid());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::vector<LatencyProbe> probes(40);
    std::vector<std::atomic<f64>> lat(40);
    for (auto& l : lat) l.store(-1.0);
    for (int i = 0; i < 40; ++i) {
        probes[i].out = &lat[i];
        probes[i].submitMs = now_ms();
        AUREA_CHECK(jobs.submit(JobPriority::High, probe_task, &probes[i]).valid());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const f64 deadline = now_ms() + 5000.0;
    while (g_bgDone.load() < 12 && now_ms() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    for (int i = 0; i < 40; ++i) {
        while (lat[i].load() < 0.0 && now_ms() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::vector<f64> v;
    for (auto& l : lat) v.push_back(l.load());
    const f64 p50 = percentile(v, 0.5), p95 = percentile(v, 0.95), mx = percentile(v, 1.0);
    std::printf("\n    [8B] HIGH com 12 LOW de 30 ms no pool (4 workers): espera p50 %.2f ms, p95 %.2f ms, max %.2f ms ",
                p50, p95, mx);
    jobs.stop();
#if defined(AUREA_MEMORY_API_8B)
    // Um worker fica sempre livre para o trabalho de cima.
    AUREA_CHECK_MSG(p95 < 5.0, "fundo atrasou o trabalho do quadro");
    AUREA_CHECK_EQ(g_bgDone.load(), 12);
#endif
}

namespace {
std::atomic<bool> g_stream{false};
std::atomic<int> g_hiRan{0};
std::atomic<f64> g_lowAt{-1.0};
void short_high(void*, JobContext&) { busy_ms(0.05); g_hiRan.fetch_add(1); }
void low_marker(void* ud, JobContext&) { g_lowAt.store(now_ms() - *static_cast<f64*>(ud)); }
} // namespace

AUREA_TEST(Perf8B, LowPriorityIsNotStarved) {
    // Fila HIGH sempre cheia por 400 ms (2 workers) e UMA tarefa LOW enviada
    // no começo. Quanto ela espera? Sem envelhecimento, até a rajada acabar.
    JobSystem jobs;
    AUREA_CHECK(jobs.start(2).ok());
    g_hiRan.store(0);
    g_lowAt.store(-1.0);
    for (int i = 0; i < 200; ++i) (void)jobs.submit(JobPriority::High, short_high, nullptr);
    f64 t0 = now_ms();
    AUREA_CHECK(jobs.submit(JobPriority::Low, low_marker, &t0).valid());
    const f64 end = t0 + 400.0;
    while (now_ms() < end) {
        // Mantém a fila de cima com trabalho esperando.
        while (jobs.queue_depth(JobPriority::High) < 64) (void)jobs.submit(JobPriority::High, short_high, nullptr);
        std::this_thread::yield();
    }
    const f64 deadline = now_ms() + 3000.0;
    while (g_lowAt.load() < 0.0 && now_ms() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::printf("\n    [8B] LOW sob rajada HIGH continua de 400 ms: comecou apos %.1f ms (%d HIGH rodaram) ",
                g_lowAt.load(), g_hiRan.load());
    jobs.stop();
#if defined(AUREA_MEMORY_API_8B)
    AUREA_CHECK_MSG(g_lowAt.load() >= 0.0 && g_lowAt.load() < 50.0, "LOW passou fome");
#endif
}

#if defined(AUREA_MEMORY_API_8B)
namespace {
std::atomic<int> g_prioOrder[8];
std::atomic<int> g_prioSeq{0};
void mark_prio(void* ud, JobContext&) { g_prioOrder[reinterpret_cast<uintptr_t>(ud)].store(g_prioSeq.fetch_add(1)); }
void sleepy(void*, JobContext&) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
} // namespace

AUREA_TEST(Jobs8B, FivePrioritiesRunInOrderAndThermalShrinksThePool) {
    JobSystem jobs;
    AUREA_CHECK(jobs.start(1).ok());
    // Worker único ocupado enquanto as cinco entram fora de ordem.
    (void)jobs.submit(JobPriority::High, sleepy, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    g_prioSeq.store(0);
    const JobPriority order[] = {JobPriority::Background, JobPriority::Low, JobPriority::Normal, JobPriority::High,
                                 JobPriority::Realtime};
    for (JobPriority p : order) {
        AUREA_CHECK(jobs.submit(p, mark_prio, reinterpret_cast<void*>(static_cast<uintptr_t>(p))).valid());
    }
    const f64 deadline = now_ms() + 3000.0;
    while (g_prioSeq.load() < 5 && now_ms() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK_EQ(g_prioOrder[0].load(), 0);   // Realtime primeiro
    AUREA_CHECK_EQ(g_prioOrder[1].load(), 1);
    AUREA_CHECK_EQ(g_prioOrder[2].load(), 2);
    AUREA_CHECK_EQ(g_prioOrder[3].load(), 3);
    AUREA_CHECK_EQ(g_prioOrder[4].load(), 4);   // Background por último
    jobs.stop();

    JobSystem pool;
    AUREA_CHECK(pool.start(4).ok());
    AUREA_CHECK_EQ(pool.background_limit(), 3u);
    pool.apply_thermal(1);   // morno
    AUREA_CHECK_EQ(pool.active_workers(), 4u);
    AUREA_CHECK_EQ(pool.background_limit(), 2u);
    pool.apply_thermal(2);   // quente
    AUREA_CHECK_EQ(pool.background_limit(), 1u);
    pool.apply_thermal(3);   // crítico
    AUREA_CHECK_EQ(pool.active_workers(), 2u);
    AUREA_CHECK_EQ(pool.background_limit(), 1u);
    // Com metade do pool desligado, tudo ainda roda.
    g_bgDone.store(0);
    for (int i = 0; i < 6; ++i) (void)pool.submit(JobPriority::Low, bg_task, nullptr);
    const f64 dl = now_ms() + 3000.0;
    while (g_bgDone.load() < 6 && now_ms() < dl) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    AUREA_CHECK_EQ(g_bgDone.load(), 6);
    pool.apply_thermal(0);
    AUREA_CHECK_EQ(pool.active_workers(), 4u);
    AUREA_CHECK_EQ(pool.background_limit(), 3u);
    const ProcSample before = sample_process();
    pool.stop();
    const ProcSample after = sample_process();
    // stop() junta as threads: elas saem do processo de verdade.
    AUREA_CHECK(after.threads + 4 <= before.threads);
}
#endif

// =============================================================================
// Cache de quadros decodificados por modo (§16)
// =============================================================================
namespace {
struct ModeResult {
    u32 shown = 0;
    u32 onTime = 0;       ///< o quadro EXATO estava no cache no prazo do quadro
    u32 seeks = 0;
    u32 decoded = 0;      ///< entregues + descartados pelo decoder
};

/// Roda `targets` em tempo real (um a cada `periodMs`), pedindo no começo do
/// período e conferindo no fim se o quadro exato chegou.
ModeResult run_mode(const SyntheticConfig& cfg, DecodeMode mode, const std::vector<i64>& frames, f64 periodMs) {
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    ModeResult r;
    i64 prev = frames.empty() ? 0 : frames.front();
    for (i64 f : frames) {
        const i64 us = raw->pts_of(f);
        const i32 dir = f > prev ? 1 : (f < prev ? -1 : 0);
        prev = f;
        const f64 t0 = now_ms();
        src.request({us, mode, mode == DecodeMode::Playback && dir == 0 ? 1 : dir, 1.0f});
        while (now_ms() - t0 < periodMs) std::this_thread::sleep_for(std::chrono::microseconds(500));
        bool exact = false;
        (void)src.frame_for(us, &exact);
        ++r.shown;
        if (exact) ++r.onTime;
    }
    src.stop();
    r.seeks = raw->seeks.load();
    r.decoded = raw->delivered.load() + raw->discarded.load();
    return r;
}

void print_mode(const char* name, const ModeResult& r) {
    std::printf("\n    [8B] %-26s no prazo %3u/%3u (%5.1f%%)  seeks %3u  decodes/quadro %.2f ", name, r.onTime, r.shown,
                100.0 * r.onTime / std::max<u32>(1, r.shown), r.seeks,
                static_cast<f64>(r.decoded) / std::max<u32>(1, r.shown));
}
} // namespace

AUREA_TEST(Perf8B, DecodedFrameCacheByMode) {
    // Vídeo sintético 30 fps, GOP 30, 4 ms por quadro decodificado (decode de
    // hardware de 1080p num aparelho médio fica entre 2 e 6 ms). Quadros
    // mostrados a 30 fps em tempo real.
    SyntheticConfig cfg;
    cfg.frameCount = 300;
    cfg.gop = 30;
    cfg.decodeCostUs = 4000;
    const f64 period = 1000.0 / 30.0;

    std::vector<i64> fwd, rev, scrubBack, scrubRegion;
    for (i64 f = 40; f < 100; ++f) fwd.push_back(f);
    for (i64 f = 220; f > 160; --f) rev.push_back(f);
    for (i64 f = 280; f > 220; --f) scrubBack.push_back(f);
    // Vai e volta numa região (o dedo procurando um corte).
    for (int k = 0; k < 3; ++k) {
        for (i64 f = 130; f <= 150; f += 2) scrubRegion.push_back(f);
        for (i64 f = 148; f > 130; f -= 2) scrubRegion.push_back(f);
    }

    const ModeResult a = run_mode(cfg, DecodeMode::Playback, fwd, period);
    const ModeResult b = run_mode(cfg, DecodeMode::Playback, rev, period);
    const ModeResult c = run_mode(cfg, DecodeMode::Scrub, scrubBack, period);
    const ModeResult d = run_mode(cfg, DecodeMode::Scrub, scrubRegion, period);
    print_mode("playback para a frente", a);
    print_mode("playback reverso", b);
    print_mode("scrub para tras", c);
    print_mode("scrub vai-e-volta", d);
    AUREA_CHECK(a.onTime >= a.shown * 9 / 10);
#if defined(AUREA_MEMORY_API_8B)
    // Reverso: janela para trás — os seeks caem para uma fração dos quadros.
    AUREA_CHECK(b.seeks * 3 <= b.shown);
    AUREA_CHECK(c.seeks * 3 <= c.shown);
#endif
}

AUREA_TEST(Perf8B, ThumbnailCacheBehindTheUiCache) {
    // A UI (Kotlin) guarda os bitmaps num LRU próprio; o motor tem outro atrás
    // dele. Quanto o do motor acerta num uso típico? Rolagem da timeline de
    // 60 s para a direita e de volta, 12 miniaturas visíveis, e depois uma
    // troca de zoom (altura nova) — com a UI guardando até 24 MB.
    SyntheticConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.frameCount = 1800;   // 60 s
    SyntheticFactory factory(cfg);
    ThumbnailService ts;
    ts.set_factory(&factory);
    ts.start();
    Asset asset;
    asset.sourcePath = "sintetico";
    struct Front {
        std::vector<std::pair<i64, u64>> lru;   // (bucket<<8 | altura, bytes)
        u64 bytes = 0, cap = 24ull << 20;
        bool get(i64 k) {
            for (usize i = 0; i < lru.size(); ++i) {
                if (lru[i].first != k) continue;
                auto e = lru[i];
                lru.erase(lru.begin() + static_cast<std::ptrdiff_t>(i));
                lru.push_back(e);
                return true;
            }
            return false;
        }
        void put(i64 k, u64 b) {
            lru.emplace_back(k, b);
            bytes += b;
            while (bytes > cap && !lru.empty()) { bytes -= lru.front().second; lru.erase(lru.begin()); }
        }
    } front;
    u64 frontHits = 0, engineHits = 0, decodes = 0;
    auto show = [&](i64 bucket, u32 height) {
        const i64 key = (bucket << 8) | height;
        if (front.get(key)) { ++frontHits; return; }
        ThumbnailService::Image img;
        const i64 us = bucket * 250'000;
        if (ts.video(1, asset, us, height, img)) { ++engineHits; front.put(key, img.rgba.size()); return; }
        ++decodes;
        const f64 dl = now_ms() + 2000.0;
        while (!ts.video(1, asset, us, height, img) && now_ms() < dl) std::this_thread::sleep_for(std::chrono::microseconds(200));
        front.put(key, img.rgba.size());
    };
    const i64 buckets = 240;
    for (i64 first = 0; first + 12 <= buckets; first += 2) for (i64 b = first; b < first + 12; ++b) show(b, 48);
    for (i64 first = buckets - 12; first >= 0; first -= 2) for (i64 b = first; b < first + 12; ++b) show(b, 48);
    for (i64 b = 100; b < 112; ++b) show(b, 64);   // zoom: altura nova
    for (i64 b = 100; b < 112; ++b) show(b, 48);   // volta
    ts.stop();
    const u64 total = frontHits + engineHits + decodes;
#if defined(AUREA_MEMORY_API_8B)
    const f64 engineKB = static_cast<f64>(ts.cached_bytes()) / 1024.0;
#else
    const f64 engineKB = ts.cached() * (85.0 * 48 * 4) / 1024.0;   // antes não contava bytes: 85×48 RGBA
#endif
    std::printf("\n    [8B] miniaturas: %llu pedidos da UI; cache da UI %llu (%.1f%%), cache do motor %llu (%.1f%%), decodes %llu, motor guardando %u (%.0f KB) ",
                static_cast<unsigned long long>(total), static_cast<unsigned long long>(frontHits), 100.0 * frontHits / total,
                static_cast<unsigned long long>(engineHits), 100.0 * engineHits / total,
                static_cast<unsigned long long>(decodes), ts.cached(), engineKB);
    AUREA_CHECK(decodes > 0);
}

#if defined(AUREA_MEMORY_API_8B)
// =============================================================================
// Orçamento, LRU, métricas e trim (contratos)
// =============================================================================
AUREA_TEST(Memory8B, DecodedFramesShareOneBudget) {
    // Duas fontes, orçamento da categoria para ~4 quadros: a soma nunca passa
    // do teto (mais o quadro que acabou de entrar), e cada uma guarda o dela.
    SyntheticConfig cfg;
    MemoryManager mm;
    auto probe = std::make_unique<SyntheticDecoder>(cfg);
    const u64 frameBytes = [&] {
        FrameRef f;
        i64 pts = 0;
        bool eos = false;
        (void)probe->seek_to_keyframe(0);
        (void)probe->next_frame(-1, f, pts, eos);
        return f ? f->approx_bytes() : 0;
    }();
    AUREA_CHECK(frameBytes > 0);
    mm.set_budget(MemoryClass::DecodedFrames, static_cast<usize>(frameBytes * 4));
    DecodedFrameCache a, b;
    DecodedFrameCache::Config c;
    c.maxFrames = 8;
    a.configure(c);
    b.configure(c);
    a.attach(&mm);
    b.attach(&mm);
    auto feed = [&](DecodedFrameCache& cache, i64 first) {
        SyntheticDecoder d(cfg);
        (void)d.seek_to_keyframe(d.pts_of(first));
        cache.set_focus(d.pts_of(first), 1);
        for (int i = 0; i < 6; ++i) {
            FrameRef f;
            i64 pts = 0;
            bool eos = false;
            (void)d.next_frame(-1, f, pts, eos);
            if (f) (void)cache.insert(std::move(f));
            AUREA_CHECK(mm.used(MemoryClass::DecodedFrames) <= frameBytes * 5);
        }
    };
    feed(a, 0);
    feed(b, 60);
    AUREA_CHECK(a.stats().frames >= 1);
    AUREA_CHECK(b.stats().frames >= 1);
    AUREA_CHECK_EQ(mm.used(MemoryClass::DecodedFrames), static_cast<usize>(a.stats().bytes + b.stats().bytes));
    // Trim: fica só o quadro do playhead em cada uma.
    const MemoryManager::TrimReport rep = mm.trim(TrimStage::UnusedDecodedFrames);
    AUREA_CHECK_EQ(a.stats().frames, 1u);
    AUREA_CHECK_EQ(b.stats().frames, 1u);
    AUREA_CHECK(rep.freed[static_cast<u8>(TrimStage::UnusedDecodedFrames)] > 0);
    AUREA_CHECK_EQ(mm.used(MemoryClass::DecodedFrames), static_cast<usize>(frameBytes * 2));
    CacheMetrics m[4];
    AUREA_CHECK_EQ(mm.collect_metrics(m, 4), 2u);
    AUREA_CHECK(m[0].evictions > 0 || m[1].evictions > 0);
    a.clear();
    AUREA_CHECK(a.stats().version == 1u);
    b.attach(nullptr);
    AUREA_CHECK_EQ(mm.used(MemoryClass::DecodedFrames), static_cast<usize>(0));
    AUREA_CHECK_EQ(mm.reclaimable_count(), 1u);
}

namespace {
/// Cache falso de um estágio, para conferir a ORDEM do trim.
struct StageCache : IMemoryReclaimable {
    MemoryClass cls;
    usize bytes = 1000;
    std::vector<int>* log = nullptr;
    int id = 0;
    explicit StageCache(MemoryClass c) : cls(c) {}
    MemoryClass memory_class() const noexcept override { return cls; }
    usize reclaim(usize) noexcept override {
        if (log) log->push_back(id);
        const usize b = bytes;
        bytes = 0;
        return b;
    }
    const char* debug_name() const noexcept override { return "estagio"; }
};
} // namespace

AUREA_TEST(Memory8B, TrimFollowsTheSpecOrderAndNeverTouchesTheProject) {
    MemoryManager mm;
    std::vector<int> log;
    // Registrados FORA de ordem de propósito.
    StageCache geo(MemoryClass::GpuGeometry), tex(MemoryClass::GpuTextures), render(MemoryClass::RenderedFrames),
        frames(MemoryClass::DecodedFrames), wave(MemoryClass::Waveforms), thumbs(MemoryClass::Thumbnails),
        tmp(MemoryClass::Assets);
    StageCache* all[] = {&geo, &tex, &render, &frames, &wave, &thumbs, &tmp};
    const int ids[] = {6, 5, 4, 3, 2, 1, 7};
    for (int i = 0; i < 7; ++i) {
        all[i]->log = &log;
        all[i]->id = ids[i];
        mm.commit(all[i]->cls, 1000);
        AUREA_CHECK(mm.register_reclaimable(all[i]).ok());
    }
    mm.commit(MemoryClass::Persistent, 5000);

    // Mapa do Android → estágio.
    AUREA_CHECK(trim_stage_for_os_level(5) == TrimStage::OldWaveforms);
    AUREA_CHECK(trim_stage_for_os_level(10) == TrimStage::UnusedDecodedFrames);
    AUREA_CHECK(trim_stage_for_os_level(15) == TrimStage::OldRenderCache);
    AUREA_CHECK(trim_stage_for_os_level(20) == TrimStage::UnusedDecodedFrames);
    AUREA_CHECK(trim_stage_for_os_level(40) == TrimStage::OldRenderCache);
    AUREA_CHECK(trim_stage_for_os_level(60) == TrimStage::Unused3DAssets);
    AUREA_CHECK(trim_stage_for_os_level(80) == TrimStage::Temporaries);

    // RUNNING_LOW: só os três primeiros, na ordem.
    (void)mm.trim(trim_stage_for_os_level(10));
    AUREA_CHECK_EQ(log.size(), static_cast<usize>(3));
    if (log.size() == 3) AUREA_CHECK(log[0] == 1 && log[1] == 2 && log[2] == 3);
    // COMPLETE: todos os estágios de novo, na ordem; só os quatro de baixo
    // ainda tinham o que soltar.
    log.clear();
    const MemoryManager::TrimReport rep = mm.trim(TrimStage::Temporaries);
    AUREA_CHECK_EQ(log.size(), static_cast<usize>(7));
    for (usize i = 0; i < log.size(); ++i) AUREA_CHECK_EQ(log[i], static_cast<int>(i + 1));
    AUREA_CHECK_EQ(rep.total, static_cast<u64>(4000));
    AUREA_CHECK_EQ(rep.freed[1], static_cast<u64>(0));
    AUREA_CHECK_EQ(rep.freed[7], static_cast<u64>(1000));
    // O projeto nunca entra.
    AUREA_CHECK_EQ(mm.used(MemoryClass::Persistent), static_cast<usize>(5000));
    for (StageCache* c : all) mm.unregister_reclaimable(c);
}

AUREA_TEST(Memory8B, ReserveReclaimsOnlyItsOwnClass) {
    // Antes: estourar o orçamento de quadros pedia despejo a TODAS as
    // categorias (soltava miniatura sem abrir espaço nenhum para quadro).
    MemoryManager mm;
    mm.set_budget(MemoryClass::DecodedFrames, 1000);
    StageCache thumbs(MemoryClass::Thumbnails), frames(MemoryClass::DecodedFrames);
    mm.commit(MemoryClass::Thumbnails, 1000);
    mm.commit(MemoryClass::DecodedFrames, 1000);
    AUREA_CHECK(mm.register_reclaimable(&thumbs).ok());
    AUREA_CHECK(mm.register_reclaimable(&frames).ok());
    auto r = mm.try_reserve(MemoryClass::DecodedFrames, 500);
    AUREA_CHECK(r.valid());
    AUREA_CHECK_EQ(thumbs.bytes, static_cast<usize>(1000));   // intacta
    AUREA_CHECK_EQ(frames.bytes, static_cast<usize>(0));
    mm.unregister_reclaimable(&thumbs);
    mm.unregister_reclaimable(&frames);
}

AUREA_TEST(Memory8B, BudgetTableSplitsTheDeviceBudget) {
    MemoryManager mm;
    mm.apply_budget_table(1000ull << 20);
    u64 sum = 0;
    for (u8 i = 0; i < static_cast<u8>(MemoryClass::_Count); ++i) sum += mm.budget(static_cast<MemoryClass>(i));
    AUREA_CHECK(sum <= (1000ull << 20));
    AUREA_CHECK(sum >= (999ull << 20));
    AUREA_CHECK_EQ(mm.budget(MemoryClass::DecodedFrames), static_cast<usize>((1000ull << 20) / 1000 * 265));
}

AUREA_TEST(Memory8B, WaveformCacheHasBudgetLruAndOldOnlyTrim) {
    SyntheticConfig cfg;
    cfg.audioRate = 48000;
    cfg.audioSeconds = 4.0;
    SyntheticFactory factory(cfg);
    MemoryManager mm;
    audio::WaveformCache wc(&factory);
    wc.attach(&mm);
    const i64 samples = static_cast<i64>(cfg.audioSeconds * 48000.0);
    for (u64 k = 1; k <= 3; ++k) wc.request(k, audio::AudioAssetRef{"sintetico", samples});
    const f64 deadline = now_ms() + 10000.0;
    while (now_ms() < deadline && (wc.progress(1) < 1.0f || wc.progress(2) < 1.0f || wc.progress(3) < 1.0f)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    AUREA_CHECK(wc.progress(3) >= 1.0f);
    AUREA_CHECK_EQ(mm.used(MemoryClass::Waveforms), static_cast<usize>(wc.bytes()));
    AUREA_CHECK(wc.bytes() > 0);
    // Tudo consultado agora (na tela): o trim de "antiga" não tira nada.
    u8 buf[16];
    AUREA_CHECK(wc.query(1, 0.0, 480.0, 16, buf));
    (void)mm.trim(TrimStage::OldWaveforms);
    AUREA_CHECK_EQ(wc.entry_count(), 3u);
    // Orçamento menor que as três: cai a menos consultada, nunca a da tela.
    CacheMetrics m[2];
    AUREA_CHECK_EQ(mm.collect_metrics(m, 2), 1u);
    AUREA_CHECK(m[0].hits >= 1);
    wc.clear();
    AUREA_CHECK_EQ(wc.entry_count(), 0u);
    AUREA_CHECK_EQ(mm.used(MemoryClass::Waveforms), static_cast<usize>(0));
    wc.attach(nullptr);
}

AUREA_TEST(Memory8B, ThumbnailServiceIsBoundedInBytes) {
    MemoryManager mm;
    ThumbnailService ts;
    mm.set_budget(MemoryClass::Thumbnails, 64 * 1024);   // ~15 miniaturas de 48 px de 16:9
    ts.attach(&mm);
    std::vector<u8> rgba(64 * 36 * 4, 128);
    ThumbnailService::Image img;
    for (u64 k = 1; k <= 60; ++k) AUREA_CHECK(ts.image(k, rgba.data(), 64, 36, 48, img));
    AUREA_CHECK(ts.cached_bytes() <= 64 * 1024);
    AUREA_CHECK_EQ(mm.used(MemoryClass::Thumbnails), static_cast<usize>(ts.cached_bytes()));
    AUREA_CHECK(ts.cached() < 60u);
    // A mais recente ficou (LRU), a primeira saiu.
    AUREA_CHECK(ts.image(60, nullptr, 0, 0, 48, img));
    AUREA_CHECK(!ts.image(1, nullptr, 0, 0, 48, img));
    CacheMetrics m;
    AUREA_CHECK(ts.metrics(m));
    AUREA_CHECK(m.evictions > 0 && m.hits >= 1);
    const MemoryManager::TrimReport rep = mm.trim(TrimStage::OffscreenThumbnails);
    AUREA_CHECK(rep.total > 0);
    AUREA_CHECK_EQ(ts.cached(), 0u);
    AUREA_CHECK_EQ(mm.used(MemoryClass::Thumbnails), static_cast<usize>(0));
    ts.attach(nullptr);
}
#endif   // AUREA_MEMORY_API_8B

// =============================================================================
// Motor inteiro com GPU: pressão do sistema e ciclo longo (vazamentos)
// =============================================================================
#if defined(AUREA_TEST_VULKAN)
namespace {

struct NullSink final : ExportSink {
    std::atomic<u32>* frames = nullptr;
    Status open(const char*, const VideoStreamConfig&, const AudioStreamConfig*) noexcept override { return OkStatus; }
    Status write_video(const u8*, u32, const u8*, u32, i64) noexcept override { frames->fetch_add(1); return OkStatus; }
    Status write_audio(const i16*, u32, i64) noexcept override { return OkStatus; }
    Status finish() noexcept override { return OkStatus; }
    void abort() noexcept override {}
};
std::atomic<u32> g_exported{0};
std::unique_ptr<ExportSink> make_null_sink(void*) {
    auto s = std::make_unique<NullSink>();
    s->frames = &g_exported;
    return s;
}

struct CycleRig {
    SyntheticFactory factory;
    Engine e;
    bool ok = false;
    explicit CycleRig(const SyntheticConfig& cfg) : factory(cfg) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.backendConfig.framesInFlight = 2;
        ec.mediaFactory = &factory;
        ec.exportSinkFactory = &make_null_sink;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ec.memoryBudgetBytes = 256ull << 20;
        ok = e.initialize(ec).ok() && e.gpu() != nullptr;
    }
    ~CycleRig() { e.shutdown(); }
};

void seek_to(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    (void)e.submit_commands(&c, 1);
}

/// Uma sessão de edição completa sobre o projeto atual: vídeo com efeito,
/// texto, forma, imagem, scrub, miniaturas, waveform, export curto.
void edit_session(Engine& e, TextureHandle target, u32 w, u32 h, u32 cycle) {
    VideoImport vi;
    vi.sourcePath = "sintetico";
    vi.displayName = "clipe";
    auto layer = e.import_video(vi);
    AUREA_CHECK(layer.ok());
    if (layer.ok()) {
        Command add;
        add.type = CommandType::EffectAdd;
        add.effect_add.layer = LayerId::unpack(*layer);
        add.effect_add.effectType = effect_type_id(effect_keys::kExposure);
        add.effect_add.index = kInvalidIndex;
        (void)e.submit_commands(&add, 1);
    }
    (void)e.add_text("ciclo");
    (void)e.add_shape(0);
    std::vector<u8> rgba(96 * 64 * 4, static_cast<u8>(40 + cycle));
    (void)e.import_image(rgba.data(), 96, 64, "imagem");
    for (i64 f = 0; f < 60; f += 3) {
        seek_to(e, f);
        (void)e.render_offscreen(target, w, h, true);
    }
    Command batch[10];
    batch[0].type = CommandType::PlaybackScrubBegin;
    for (u32 i = 0; i < 8; ++i) {
        batch[1 + i].type = CommandType::PlaybackScrub;
        batch[1 + i].seek.time = tick_at(FrameIndex{static_cast<i64>(80 - i * 4)}, 30.0);
    }
    batch[9].type = CommandType::PlaybackScrubEnd;
    (void)e.submit_commands(batch, 10);
    (void)e.render_offscreen(target, w, h, true);
    if (layer.ok()) {
        std::vector<u8> px(256 * 256 * 4);
        u32 tw = 0;
        for (i32 f = 0; f < 90; f += 8) (void)e.query_thumbnail(*layer, f, 32, px.data(), static_cast<u32>(px.size()), &tw);
        u8 wave[64];
        (void)e.query_waveform(*layer, 0.0, 2.0, 64, wave);
    }
    ExportSettings s;
    s.width = w;
    s.height = h;
    s.fps = 0.0;
    Command dur;
    dur.type = CommandType::CompositionSetDuration;
    dur.comp_duration.comp = e.project()->timeline().current();
    dur.comp_duration.duration = FrameIndex{12};
    (void)e.apply_command(dur);
    if (e.start_export(s, "nao-usado.mp4").ok()) {
        const f64 dl = now_ms() + 20000.0;
        while (!e.export_progress().finished && now_ms() < dl) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

AUREA_TEST(Perf8B, LongSessionMemoryStabilizes) {
    // §102–104, §155–157: abrir → editar → renderizar → exportar → salvar →
    // reabrir → fechar → outro projeto, N vezes. Memória privada, threads,
    // handles e objetos vivos no backend têm de estabilizar.
    SyntheticConfig cfg;
    cfg.width = 160;
    cfg.height = 90;
    cfg.frameCount = 150;
    cfg.audioRate = 48000;
    cfg.audioSeconds = 5.0;
    CycleRig rig(cfg);
    if (!rig.ok) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    Engine& e = rig.e;
    const u32 w = 160, h = 90;
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    const char* path = "aurea_teste_ciclo_8b.aurea";

    constexpr u32 kCycles = 12;
    ProcSample ps[kCycles];
    GpuMemoryStats gs[kCycles];
    u64 engineUsed[kCycles]{};
    std::printf("\n    [8B] ciclo  privada_MB  rss_MB  threads  handles  tex_gpu  buf_gpu  gpu_MB  motor_MB");
    for (u32 c = 0; c < kCycles; ++c) {
        AUREA_CHECK(e.new_project(w, h, 30.0, "ciclo").ok());
        edit_session(e, target, w, h, c);
        AUREA_CHECK(e.save_project(path).ok());
        AUREA_CHECK(e.load_project(path).ok());
        for (i64 f = 0; f < 30; f += 5) {
            seek_to(e, f);
            (void)e.render_offscreen(target, w, h, true);
        }
        // "Fechar": projeto vazio novo (é o que o app faz ao voltar à Home).
        AUREA_CHECK(e.new_project(w, h, 30.0, "vazio").ok());
        (void)e.render_offscreen(target, w, h, true);
        e.gpu()->wait_idle();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ps[c] = sample_process();
        gs[c] = e.gpu()->memory_stats();
        engineUsed[c] = e.memory().total_used();
        std::printf("\n    [8B] %5u  %10.1f  %6.1f  %7u  %7u  %7u  %7u  %6.1f  %8.2f", c + 1, ps[c].privateBytes / 1048576.0,
                    ps[c].workingSet / 1048576.0, ps[c].threads, ps[c].handles, gs[c].textureCount, gs[c].bufferCount,
                    gs[c].usedBytes / 1048576.0, engineUsed[c] / 1048576.0);
    }
    std::printf("\n    ");
    std::remove(path);
    e.gpu()->destroy_texture(target);
    // Estabilizou: do 4º ao último ciclo (depois do aquecimento de pipelines e
    // caches de fonte), nada cresce além do ruído do alocador.
    const u32 a = 3, b = kCycles - 1;
    const f64 growMB = (static_cast<f64>(ps[b].privateBytes) - static_cast<f64>(ps[a].privateBytes)) / 1048576.0;
    std::printf("[8B] crescimento ciclo %u->%u: privada %+.1f MB, threads %+d, handles %+d, texturas %+d, buffers %+d ",
                a + 1, b + 1, growMB, static_cast<int>(ps[b].threads) - static_cast<int>(ps[a].threads),
                static_cast<int>(ps[b].handles) - static_cast<int>(ps[a].handles),
                static_cast<int>(gs[b].textureCount) - static_cast<int>(gs[a].textureCount),
                static_cast<int>(gs[b].bufferCount) - static_cast<int>(gs[a].bufferCount));
#if defined(AUREA_MEMORY_API_8B)
    AUREA_CHECK(ps[b].threads <= ps[a].threads + 1);
    AUREA_CHECK(ps[b].handles <= ps[a].handles + 16);
    AUREA_CHECK(gs[b].textureCount <= gs[a].textureCount);
    AUREA_CHECK(gs[b].bufferCount <= gs[a].bufferCount);
    AUREA_CHECK_MSG(growMB < 16.0, "memoria privada cresce a cada ciclo");
#endif
}

#if defined(AUREA_MEMORY_API_8B)
AUREA_TEST(Perf8B, OsTrimLevelsReleaseMemoryAndKeepTheProject) {
    // Cada nível do Android, simulado sobre o mesmo projeto aquecido: quanto
    // sai em cada estágio, e o projeto (camadas, histórico, sujo) intacto.
    SyntheticConfig cfg;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.frameCount = 150;
    cfg.audioRate = 48000;
    cfg.audioSeconds = 5.0;
    CycleRig rig(cfg);
    if (!rig.ok) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    Engine& e = rig.e;
    const u32 w = 1280, h = 720;
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = true;
    d.transferSrc = true;
    const TextureHandle target = *e.gpu()->create_texture(d);
    AUREA_CHECK(e.new_project(w, h, 30.0, "pressao").ok());
    VideoImport vi;
    vi.sourcePath = "sintetico";
    vi.displayName = "clipe";
    auto v1 = e.import_video(vi);
    auto v2 = e.import_video(vi);
    AUREA_CHECK(v1.ok() && v2.ok());
    (void)e.add_text("pressao");

    auto warm = [&]() {
        for (i64 f = 0; f < 45; f += 3) {
            seek_to(e, f);
            (void)e.render_offscreen(target, w, h, true);
        }
        std::vector<u8> px(256 * 256 * 4);
        u32 tw = 0;
        for (int pass = 0; pass < 40; ++pass) {
            for (i32 f = 0; f < 150; f += 8) (void)e.query_thumbnail(*v1, f, 32, px.data(), static_cast<u32>(px.size()), &tw);
            u8 wave[64];
            (void)e.query_waveform(*v1, 0.0, 2.0, 64, wave);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Waveform e miniaturas "saem da tela": 2,1 s sem consulta.
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        seek_to(e, 40);
        (void)e.render_offscreen(target, w, h, true);
        e.gpu()->wait_idle();
    };

    bridge::EngineStatusPOD before{};
    e.fill_status(before);
    const u64 undoBefore = before.undoDepth;
    const i32 levels[] = {5, 10, 15, 20, 40, 60, 80};
    std::printf("\n    [8B] nivel  estagio                   miniat_KB  wave_KB  quadros_KB  render_GPU_KB  total_KB  motor_antes_MB  motor_depois_MB");
    for (i32 lv : levels) {
        warm();
        const u64 usedBefore = e.memory().total_used();
        const MemoryManager::TrimReport r = e.trim_memory(lv);
        const u64 usedAfter = e.memory().total_used();
        std::printf("\n    [8B] %5d  %-24s  %9.1f  %7.1f  %10.1f  %13.1f  %8.1f  %14.2f  %15.2f", lv, to_string(r.upTo),
                    r.freed[1] / 1024.0, r.freed[2] / 1024.0, r.freed[3] / 1024.0, r.freed[4] / 1024.0, r.total / 1024.0,
                    usedBefore / 1048576.0, usedAfter / 1048576.0);
        // Projeto intacto e o próximo quadro sai.
        bridge::EngineStatusPOD st{};
        e.fill_status(st);
        AUREA_CHECK_EQ(st.layerCount, before.layerCount);
        AUREA_CHECK_EQ(static_cast<u64>(st.undoDepth), undoBefore);
        AUREA_CHECK(e.render_offscreen(target, w, h, true).ok());
    }
    std::printf("\n    ");
    e.gpu()->destroy_texture(target);
}
#endif
#endif   // AUREA_TEST_VULKAN
