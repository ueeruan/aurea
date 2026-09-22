// =============================================================================
//  Fase 8H — aparelhos fracos, temperatura e bateria.
//
//  Tudo aqui roda no host e é determinístico: a classificação e a política são
//  funções puras das capacidades MEDIDAS, então um "aparelho de entrada" é só
//  um PlatformInfo sintético — não precisa do aparelho na mão.
// =============================================================================
#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #include <windows.h>
#else
    #include <ctime>
#endif

using namespace aurea;

namespace {

/// CPU (usuário + sistema) consumida pelo PROCESSO até agora, em ns.
u64 process_cpu_ns() {
#if defined(_WIN32)
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    auto ns = [](const FILETIME& f) {
        return ((static_cast<u64>(f.dwHighDateTime) << 32) | f.dwLowDateTime) * 100ull;
    };
    return ns(k) + ns(u);
#else
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return static_cast<u64>(ts.tv_sec) * 1'000'000'000ull + static_cast<u64>(ts.tv_nsec);
#endif
}

/// Fração de UM núcleo que o processo gastou parado por `ms`.
f64 idle_cpu_fraction(u32 ms) {
    const u64 c0 = process_cpu_ns();
    const u64 t0 = monotonic_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    const u64 c1 = process_cpu_ns();
    const u64 t1 = monotonic_ns();
    return static_cast<f64>(c1 - c0) / static_cast<f64>(t1 - t0);
}

} // namespace

// -----------------------------------------------------------------------------
// Bateria (§38): nada acorda sem trabalho
// -----------------------------------------------------------------------------
AUREA_TEST(Battery, IdleJobSystemDoesNotSpin) {
    JobSystem jobs;
    AUREA_CHECK(jobs.start(4).ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // workers sobem
    const f64 cpu = idle_cpu_fraction(1000);
    std::printf("    pool de 4 workers parado: %.1f%% de um nucleo\n", cpu * 100.0);
    jobs.stop();
    AUREA_CHECK(cpu < 0.05);
}

AUREA_TEST(Battery, IdleEngineDoesNotSpin) {
    // O motor inteiro parado, como no aparelho antes da superfície chegar (ou
    // com o app em segundo plano): pool, thread de render, mixer, miniaturas,
    // waveform. Nenhum deles tem o que fazer.
    Engine e;
    EngineConfig ec;
    ec.workerCount = 4;
    ec.memoryBudgetBytes = 64ull * 1024 * 1024;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, "parado").ok());
    e.start_render_thread();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const u64 r0 = e.render_wakeups();
    const u64 j0 = e.job_idle_wakeups();
    const f64 cpu = idle_cpu_fraction(1000);
    const u64 renderWakes = e.render_wakeups() - r0;
    const u64 jobWakes = e.job_idle_wakeups() - j0;
    std::printf("    motor parado (4 workers + render, sem superficie): %.1f%% de um nucleo; "
                "acordadas/s: render %llu, workers %llu\n",
                cpu * 100.0, static_cast<unsigned long long>(renderWakes), static_cast<unsigned long long>(jobWakes));
    e.shutdown();
    AUREA_CHECK(cpu < 0.05);
    AUREA_CHECK_EQ(renderWakes, static_cast<u64>(0));
    AUREA_CHECK_EQ(jobWakes, static_cast<u64>(0));
}

AUREA_TEST(Battery, ParkedWorkersWakeForWork) {
    // Dormir não pode custar tarefa perdida: depois de todos os workers
    // dormirem, cada submissão ainda roda — inclusive uma rajada.
    JobSystem jobs;
    AUREA_CHECK(jobs.start(3).ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    AUREA_CHECK(jobs.idle_parks() >= 3);
    static std::atomic<u32> ran{0};
    ran = 0;
    auto bump = [](void*, JobContext&) { ran.fetch_add(1); };
    for (int round = 0; round < 20; ++round) {
        for (int i = 0; i < 50; ++i) AUREA_CHECK(jobs.submit(JobPriority::Normal, bump, nullptr).valid());
        const u64 deadline = monotonic_ns() + 2'000'000'000ull;
        while (ran.load() < static_cast<u32>((round + 1) * 50) && monotonic_ns() < deadline) std::this_thread::yield();
        AUREA_CHECK_EQ(ran.load(), static_cast<u32>((round + 1) * 50));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));   // deixa dormir de novo
    }
    jobs.stop();
}
