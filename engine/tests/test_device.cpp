// =============================================================================
//  Fase 8H — aparelhos fracos, temperatura e bateria.
//
//  Tudo aqui roda no host e é determinístico: a classificação e a política são
//  funções puras das capacidades MEDIDAS, então um "aparelho de entrada" é só
//  um PlatformInfo sintético — não precisa do aparelho na mão.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"
#include "VulkanBackend.hpp"
#include "aurea/Engine.hpp"
#include "aurea/media/ThumbnailService.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/jobs/JobSystem.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOMINMAX   // sem as macros min/max: std::min/std::max continuam funções
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

AUREA_TEST(Battery, ThumbnailDecodersCloseWhenIdle) {
    // §38: decoder parado não fica aberto. Depois da rajada, a fila vazia por
    // kDecoderIdleMs fecha os decoders; o próximo pedido reabre.
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory(cfg);
    ThumbnailService svc;
    svc.set_factory(&factory);
    svc.start();
    Asset a;
    a.kind = AssetKind::Video;
    ThumbnailService::Image img;
    const u32 gen0 = svc.generation();
    AUREA_CHECK(!svc.video(7, a, 1'000'000, 24, img));
    const u64 t0 = monotonic_ns();
    while (svc.generation() == gen0 && monotonic_ns() - t0 < 2'000'000'000ull) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK_EQ(svc.open_decoders(), 1u);
    std::this_thread::sleep_for(std::chrono::milliseconds(ThumbnailService::kDecoderIdleMs + 300));
    AUREA_CHECK_EQ(svc.open_decoders(), 0u);
    // Pedido novo depois do fechamento: reabre e entrega.
    const u32 gen1 = svc.generation();
    AUREA_CHECK(!svc.video(7, a, 3'000'000, 24, img));
    const u64 t1 = monotonic_ns();
    while (svc.generation() == gen1 && monotonic_ns() - t1 < 2'000'000'000ull) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(svc.video(7, a, 3'000'000, 24, img));
    AUREA_CHECK_EQ(factory.opened, 2u);
    svc.stop();
}

AUREA_TEST(Battery, ThumbnailPacingSlowsBackgroundWork) {
    // WARM/HOT: a pausa entre miniaturas é real (a política não é só número).
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory(cfg);
    ThumbnailService svc;
    svc.set_factory(&factory);
    svc.set_pacing_ms(device_policy(DeviceTier::Mid, ThermalTier::Hot).backgroundPauseMs);
    svc.start();
    Asset a;
    a.kind = AssetKind::Video;
    ThumbnailService::Image img;
    const u64 t0 = monotonic_ns();
    for (i64 k = 0; k < 3; ++k) (void)svc.video(9, a, k * 1'000'000, 24, img);
    while (svc.generation() < 3 && monotonic_ns() - t0 < 5'000'000'000ull) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const f64 ms = static_cast<f64>(monotonic_ns() - t0) / 1e6;
    std::printf("    3 miniaturas com pausa HOT (%u ms): %.0f ms\n", svc.pacing_ms(), ms);
    AUREA_CHECK_EQ(svc.generation(), 3u);
    AUREA_CHECK(ms >= 2.0 * svc.pacing_ms());   // duas pausas entre três
    svc.stop();
}

// -----------------------------------------------------------------------------
// Classe do aparelho (§110–111): perfis sintéticos
// -----------------------------------------------------------------------------
namespace {

constexpr u64 kMiB = 1024ull * 1024ull;
constexpr u32 kAvc1 = 0x61766331u, kHvc1 = 0x68766331u, kAv01 = 0x61763031u;

CodecCapability codec(u32 w, u32 h, bool hw) {
    CodecCapability c;
    c.supported = true;
    c.hardwareAccelerated = hw;
    c.maxWidth = w;
    c.maxHeight = h;
    c.concurrentInstances = 2;
    return c;
}

void add_decoder(PlatformInfo& info, u32 tag, const CodecCapability& c) {
    info.decoders[info.decoderCount] = c;
    info.set_decoder_tag(tag, info.decoderCount);
    ++info.decoderCount;
}

void add_encoder(PlatformInfo& info, u32 tag, const CodecCapability& c) {
    info.encoders[info.encoderCount] = c;
    info.set_encoder_tag(tag, info.encoderCount);
    ++info.encoderCount;
}

struct Profile {
    PlatformInfo info;
    GpuCapabilities gpu;
};

/// Entrada: 3 GB (o Android reporta ~2,8), 8 × 1,8 GHz, Mali-T830 (Vulkan 1.0,
/// 256 invocações, sem fp16), H.264 1080p de hardware e SEM HEVC.
Profile low_profile() {
    Profile p;
    p.info.totalCores = 8;
    p.info.performanceCores = 4;
    p.info.efficiencyCores = 4;
    p.info.maxFrequencyKhz = 1'800'000;
    p.info.totalMemoryBytes = 2800 * kMiB;
    p.info.availableMemoryBytes = 900 * kMiB;
    add_decoder(p.info, kAvc1, codec(1920, 1088, true));
    add_encoder(p.info, kAvc1, codec(1920, 1088, true));
    p.gpu.deviceName = "Mali-T830";
    p.gpu.vulkan = true;
    p.gpu.apiVersionMajor = 1;
    p.gpu.apiVersionMinor = 0;
    p.gpu.maxTextureSize = 8192;
    p.gpu.maxComputeWorkgroupSize = 256;
    p.gpu.supportsCompute = true;
    return p;
}

/// Médio: 6 GB, 2,3 GHz, Adreno 618 (Vulkan 1.1, 1024, fp16), HEVC 4K.
Profile mid_profile() {
    Profile p;
    p.info.totalCores = 8;
    p.info.performanceCores = 2;
    p.info.efficiencyCores = 6;
    p.info.maxFrequencyKhz = 2'300'000;
    p.info.totalMemoryBytes = 5600 * kMiB;
    p.info.availableMemoryBytes = 2500 * kMiB;
    add_decoder(p.info, kAvc1, codec(4096, 2176, true));
    add_decoder(p.info, kHvc1, codec(4096, 2176, true));
    add_encoder(p.info, kAvc1, codec(3840, 2160, true));
    add_encoder(p.info, kHvc1, codec(3840, 2160, true));
    p.gpu.deviceName = "Adreno (TM) 618";
    p.gpu.vulkan = true;
    p.gpu.apiVersionMajor = 1;
    p.gpu.apiVersionMinor = 1;
    p.gpu.maxTextureSize = 16384;
    p.gpu.maxComputeWorkgroupSize = 1024;
    p.gpu.supportsCompute = true;
    p.gpu.supportsFloat16 = true;
    p.gpu.supportsFloat16Storage = true;
    return p;
}

/// Alto: 8 GB, 2,84 GHz, Adreno 660 (Vulkan 1.1), HEVC 4K.
Profile high_profile() {
    Profile p = mid_profile();
    p.info.maxFrequencyKhz = 2'840'000;
    p.info.performanceCores = 4;
    p.info.efficiencyCores = 4;
    p.info.totalMemoryBytes = 7400 * kMiB;
    p.info.availableMemoryBytes = 3500 * kMiB;
    p.gpu.deviceName = "Adreno (TM) 660";
    return p;
}

/// Topo: 12 GB, 3,2 GHz, Adreno 740 (Vulkan 1.3), HEVC e AV1 4K.
Profile ultra_profile() {
    Profile p = high_profile();
    p.info.maxFrequencyKhz = 3'200'000;
    p.info.totalMemoryBytes = 11200 * kMiB;
    p.info.availableMemoryBytes = 6000 * kMiB;
    add_decoder(p.info, kAv01, codec(4096, 2176, true));
    p.gpu.deviceName = "Adreno (TM) 740";
    p.gpu.apiVersionMinor = 3;
    return p;
}

/// O caminho do motor: plataforma → detect → GPU real.
DeviceCapabilities caps_for(const Profile& p) {
    DeviceCapabilities caps;
    caps.apply_platform_info(p.info);
    caps.detect();
    (void)caps.apply_gpu(p.gpu);
    return caps;
}

} // namespace

AUREA_TEST(DeviceClass, SyntheticProfilesLandInTheirTier) {
    const DeviceCapabilities low = caps_for(low_profile());
    const DeviceCapabilities mid = caps_for(mid_profile());
    const DeviceCapabilities high = caps_for(high_profile());
    const DeviceCapabilities ultra = caps_for(ultra_profile());
    for (const DeviceCapabilities* c : {&low, &mid, &high, &ultra}) {
        const DeviceClass& k = c->device_class();
        std::printf("    %-16s -> %-5s (mem %s gpu %s cpu %s codec %s, limite %u)\n", c->gpu().deviceName.c_str(),
                    device_tier_name(k.tier), device_tier_name(k.memory), device_tier_name(k.gpu),
                    device_tier_name(k.cpu), device_tier_name(k.codecs), static_cast<unsigned>(k.limit));
    }
    AUREA_CHECK_EQ(low.tier(), DeviceTier::Low);
    AUREA_CHECK_EQ(low.device_class().limit, DeviceLimit::Memory);   // empate: memória é o mais grave
    AUREA_CHECK_EQ(low.device_class().gpu, DeviceTier::Low);
    AUREA_CHECK_EQ(low.device_class().codecs, DeviceTier::Mid);        // H.264 1080p de hardware, sem HEVC
    AUREA_CHECK_EQ(mid.tier(), DeviceTier::Mid);
    AUREA_CHECK_EQ(high.tier(), DeviceTier::High);
    AUREA_CHECK_EQ(ultra.tier(), DeviceTier::Ultra);
    AUREA_CHECK_EQ(ultra.device_class().limit, DeviceLimit::None);
}

AUREA_TEST(DeviceClass, MissingCodecIsAbsentNotCodecZero) {
    // Regressão: o mapa tag→índice nascia com zeros e um aparelho sem HEVC
    // ficava com "HEVC de hardware" = o decoder H.264 do slot 0.
    const DeviceCapabilities low = caps_for(low_profile());
    AUREA_CHECK(low.decoder_h264().supported);
    AUREA_CHECK(!low.decoder_hevc().supported);
    AUREA_CHECK(!low.decoder_av1().supported);
    AUREA_CHECK(!low.encoder_hevc().supported);
}

AUREA_TEST(DeviceClass, HostIsClassifiedFromMeasurement) {
    // O host de verdade, com a GPU que o Vulkan responder: a classe sai dos
    // limites medidos (sem tabela de codecs, o eixo de codec fica neutro).
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.workerCount = 2;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    const DeviceClass& k = e.caps().device_class();
    const DeviceClassInput in = e.caps().class_input();
    std::printf("    host: %s -> %s (mem %s gpu %s cpu %s codec %s, limite %u; Vulkan %u.%u, wg %u, tex %u, fp16 %d/%d, "
                "RAM %llu/%llu MB)\n",
                e.caps().gpu().deviceName.empty() ? "sem GPU" : e.caps().gpu().deviceName.c_str(),
                device_tier_name(k.tier), device_tier_name(k.memory), device_tier_name(k.gpu), device_tier_name(k.cpu),
                device_tier_name(k.codecs), static_cast<unsigned>(k.limit), in.gpuApiMajor, in.gpuApiMinor,
                in.gpuMaxWorkgroup, in.gpuMaxTexture, in.gpuFp16 ? 1 : 0, in.gpuFp16Storage ? 1 : 0,
                static_cast<unsigned long long>(in.availableMemoryBytes / kMiB),
                static_cast<unsigned long long>(in.totalMemoryBytes / kMiB));
    // Sem codecs medidos, o motivo nunca é "codec".
    AUREA_CHECK(k.limit != DeviceLimit::Codecs);
    // Os núcleos são os do host, não o padrão de 1 da struct (regressão:
    // detect_cpu tomava o padrão por medição e todo aparelho saía com 1).
    AUREA_CHECK_EQ(e.caps().cpu().totalCores, std::thread::hardware_concurrency());
    AUREA_CHECK(k.cpu != DeviceTier::Low || std::thread::hardware_concurrency() < 4);
    e.shutdown();
}

AUREA_TEST(DeviceClass, TheWeakestAxisDecides) {
    // GPU e CPU de topo com 3 GB de RAM é um aparelho de 3 GB.
    Profile p = ultra_profile();
    p.info.totalMemoryBytes = 2800 * kMiB;
    p.info.availableMemoryBytes = 1200 * kMiB;
    AUREA_CHECK_EQ(caps_for(p).tier(), DeviceTier::Low);
    AUREA_CHECK_EQ(caps_for(p).device_class().limit, DeviceLimit::Memory);

    // Tudo de topo, sem HEVC de hardware: metade dos vídeos de celular em
    // software — no máximo MID, e o motivo é o codec.
    Profile q = ultra_profile();
    q.info.decoders[1].hardwareAccelerated = false;
    AUREA_CHECK_EQ(caps_for(q).tier(), DeviceTier::Mid);
    AUREA_CHECK_EQ(caps_for(q).device_class().limit, DeviceLimit::Codecs);

    // Pressão de memória agora (400 MB livres num aparelho de 8 GB) desce uma faixa.
    Profile r = high_profile();
    r.info.availableMemoryBytes = 400 * kMiB;
    AUREA_CHECK_EQ(caps_for(r).tier(), DeviceTier::Mid);
    AUREA_CHECK_EQ(caps_for(r).device_class().limit, DeviceLimit::MemoryPressure);

    // CPU com menos de 4 núcleos é entrada, qualquer que seja o clock.
    Profile s = ultra_profile();
    s.info.totalCores = 2;
    s.info.performanceCores = 2;
    s.info.efficiencyCores = 0;
    AUREA_CHECK_EQ(caps_for(s).tier(), DeviceTier::Low);
    AUREA_CHECK_EQ(caps_for(s).device_class().limit, DeviceLimit::Cpu);
}

AUREA_TEST(DeviceClass, UnmeasuredStaysMidNeverGuessed) {
    // Sem medição nenhuma: MID por GPU desconhecida — nem o plano de entrada
    // (esconderia o que o aparelho faz) nem o de topo (derrubaria o preview).
    const DeviceClass none = classify_device(DeviceClassInput{});
    AUREA_CHECK_EQ(none.tier, DeviceTier::Mid);
    AUREA_CHECK_EQ(none.limit, DeviceLimit::GpuUnknown);
    // DeviceCapabilities antes de detect(): o mesmo, e o preview AUTO começa cheio.
    DeviceCapabilities caps;
    AUREA_CHECK_EQ(caps.tier(), DeviceTier::Mid);
    AUREA_CHECK_EQ(caps.recommended_initial_scale(1920, 1080), PreviewScale::Auto);
    // A GPU conhecida, mas a plataforma não mandou codecs (host): o eixo de
    // codec é neutro, não "entrada".
    DeviceClassInput in;
    in.gpuKnown = true;
    in.gpuApiMajor = 1;
    in.gpuApiMinor = 3;
    in.gpuCompute = true;
    in.gpuFp16 = true;
    in.gpuFp16Storage = true;
    in.gpuMaxTexture = 32768;
    in.gpuMaxWorkgroup = 1024;
    AUREA_CHECK_EQ(classify_device(in).tier, DeviceTier::Ultra);
}

AUREA_TEST(DeviceClass, LowProfileAppliesTheLowPlan) {
    const DeviceCapabilities low = caps_for(low_profile());
    const DevicePolicy p = low.policy();
    // §107: preview 1/4, custo das operações caras pela metade, sombra e
    // partículas menores, proxy acima de 720p, cache de decode menor, prévias
    // de efeito menores.
    AUREA_CHECK_EQ(low.recommended_initial_scale(1920, 1080), PreviewScale::Quarter);
    AUREA_CHECK_EQ(low.recommended_initial_scale(1080, 1920), PreviewScale::Quarter);
    AUREA_CHECK_EQ(low.recommended_initial_scale(640, 360), PreviewScale::Half);
    AUREA_CHECK_NEAR(p.heavyScale, 0.5f, 1e-6f);
    AUREA_CHECK(p.shadowMapSize < device_policy(DeviceTier::Mid, ThermalTier::Normal).shadowMapSize);
    AUREA_CHECK(p.particleScale < 1.0f);
    AUREA_CHECK_EQ(p.preferProxyAboveShortSide, 720u);
    AUREA_CHECK(p.decodedFramesBudgetPercent < 24u);
    AUREA_CHECK(p.effectPreviewMaxSide < 320u);
    // Export: teto do CODIFICADOR (1920×1088 → 1080p) e o motivo registrado.
    AUREA_CHECK_EQ(low.max_export_width(), 1920u);
    AUREA_CHECK_EQ(low.max_export_height(), 1080u);
    AUREA_CHECK_EQ(low.export_limit(), ExportLimit::Encoder);
    // O plano MID é o de sempre: nada muda para quem já funcionava.
    const DevicePolicy mid = device_policy(DeviceTier::Mid, ThermalTier::Normal);
    AUREA_CHECK_EQ(mid.previewInitialDenominator, 1u);
    AUREA_CHECK_NEAR(mid.heavyScale, 1.0f, 1e-6f);
    AUREA_CHECK_EQ(mid.decodedFramesBudgetPercent, 24u);
    AUREA_CHECK_EQ(mid.shadowMapSize, 2048u);
    AUREA_CHECK_EQ(mid.effectPreviewMaxSide, 320u);
}

AUREA_TEST(DeviceClass, ExportCeilingFollowsTheEncoderNotTheDecoder) {
    // Decoder 4K, codificador 1080p (comum em aparelho de entrada): antes o
    // teto saía do decoder e o app oferecia 4K que falhava no meio do export.
    Profile p = mid_profile();
    p.info.encoders[0] = codec(1920, 1080, true);   // H.264
    const DeviceCapabilities caps = caps_for(p);
    AUREA_CHECK_EQ(caps.max_export_width(), 1920u);
    AUREA_CHECK_EQ(caps.max_export_height(), 1080u);
    AUREA_CHECK_EQ(caps.export_limit(), ExportLimit::Encoder);
    // Codificador 4K: teto 4K, sem limite a explicar.
    const DeviceCapabilities full = caps_for(mid_profile());
    AUREA_CHECK_EQ(full.max_export_width(), 3840u);
    AUREA_CHECK_EQ(full.max_export_height(), 2160u);
    AUREA_CHECK_EQ(full.export_limit(), ExportLimit::None);
    // Codificador 4K, mas RAM que não segura os quadros 4K: 1080p pela memória.
    Profile m = mid_profile();
    m.info.availableMemoryBytes = 700 * kMiB;
    const DeviceCapabilities tight = caps_for(m);
    AUREA_CHECK_EQ(tight.max_export_height(), 1080u);
    AUREA_CHECK_EQ(tight.export_limit(), ExportLimit::Memory);
}

AUREA_TEST(DeviceClass, ReportCarriesClassReasonAndCodecs) {
    i64 r[kDeviceReportSlots];
    write_device_report(caps_for(low_profile()), r);
    AUREA_CHECK_EQ(r[14], static_cast<i64>(DeviceTier::Low));
    AUREA_CHECK_EQ(r[15], static_cast<i64>(DeviceLimit::Memory));
    AUREA_CHECK_EQ(r[21], static_cast<i64>(ExportLimit::Encoder));
    AUREA_CHECK((r[20] & kReportCodecsKnown) != 0);
    AUREA_CHECK((r[20] & kReportHwDecodeH264) != 0);
    AUREA_CHECK((r[20] & kReportHwDecodeHevc) == 0);
    AUREA_CHECK((r[20] & kReportEncodeHevc) == 0);
    AUREA_CHECK_EQ(r[22], i64{0});
    AUREA_CHECK_EQ(r[24], i64{50});   // heavyScale 0,5
    AUREA_CHECK_EQ(r[25], i64{4});    // preview começa em 1/4
    AUREA_CHECK_EQ(r[31], i64{1800}); // MHz
    write_device_report(caps_for(ultra_profile()), r);
    AUREA_CHECK_EQ(r[14], static_cast<i64>(DeviceTier::Ultra));
    AUREA_CHECK((r[20] & kReportEncodeHevc) != 0);
    AUREA_CHECK_EQ(r[22], i64{3840});
    AUREA_CHECK_EQ(r[23], i64{2160});
}

// -----------------------------------------------------------------------------
// Temperatura (§35–37)
// -----------------------------------------------------------------------------
AUREA_TEST(Thermal, AndroidStatusMapsToFourTiers) {
    const ThermalTier expected[7] = {ThermalTier::Normal, ThermalTier::Warm, ThermalTier::Hot, ThermalTier::Hot,
                                     ThermalTier::Critical, ThermalTier::Critical, ThermalTier::Critical};
    for (i32 s = 0; s <= 6; ++s) {
        AUREA_CHECK_EQ(thermal_tier_from_android(s), expected[s]);
        // O caminho do JNI (status → ThermalState → faixa) dá a MESMA faixa.
        AUREA_CHECK_EQ(thermal_state_from_android(s).tier(), expected[s]);
    }
    AUREA_CHECK_EQ(thermal_tier_from_android(-1), ThermalTier::Normal);
    AUREA_CHECK_EQ(thermal_tier_from_android(99), ThermalTier::Critical);
    // Sem nível informado (host, iOS antigo): normal.
    AUREA_CHECK_EQ(ThermalState{}.tier(), ThermalTier::Normal);
}

AUREA_TEST(Thermal, PolicyActionsPerTier) {
    for (u8 t = 0; t < 4; ++t) {
        const DeviceTier tier = static_cast<DeviceTier>(t);
        const DevicePolicy n = device_policy(tier, ThermalTier::Normal);
        const DevicePolicy w = device_policy(tier, ThermalTier::Warm);
        const DevicePolicy h = device_policy(tier, ThermalTier::Hot);
        const DevicePolicy c = device_policy(tier, ThermalTier::Critical);
        // WARM: só o trabalho de fundo; o preview não muda.
        AUREA_CHECK(w.backgroundPauseMs > n.backgroundPauseMs);
        AUREA_CHECK_NEAR(w.heavyScale, n.heavyScale, 1e-6f);
        AUREA_CHECK_EQ(w.shadowMapSize, n.shadowMapSize);
        AUREA_CHECK_EQ(w.minPreviewDenominator, n.minPreviewDenominator);
        // HOT: preview, partículas, sombra, flow e amostras de blur (heavyScale) menores.
        AUREA_CHECK(h.heavyScale < w.heavyScale);
        AUREA_CHECK(h.particleScale < w.particleScale);
        AUREA_CHECK(h.shadowMapSize < w.shadowMapSize);
        AUREA_CHECK(h.minPreviewDenominator >= 2u);
        AUREA_CHECK(h.backgroundPauseMs > w.backgroundPauseMs);
        // CRITICAL: o mínimo útil — 1/4, mistura no lugar do flow, sombra 512.
        AUREA_CHECK(c.heavyScale <= 0.25f);
        AUREA_CHECK(c.heavyScale >= 0.125f);   // nunca zero: o editor continua mostrando
        AUREA_CHECK(!c.previewOpticalFlow);
        AUREA_CHECK_EQ(c.minPreviewDenominator, 4u);
        AUREA_CHECK_EQ(c.shadowMapSize, 512u);
        // §37: o export perde paralelismo, NUNCA qualidade.
        for (const DevicePolicy* p : {&n, &w, &h, &c}) {
            AUREA_CHECK_NEAR(p->exportRenderScale, 1.0f, 0.0f);
            AUREA_CHECK_NEAR(p->exportHeavyScale, 1.0f, 0.0f);
            AUREA_CHECK(p->exportPipelineDepth >= 1u);
        }
        AUREA_CHECK(h.exportPipelineDepth <= n.exportPipelineDepth);
        AUREA_CHECK_EQ(c.exportPipelineDepth, 1u);
    }
}

AUREA_TEST(Thermal, EngineAppliesTierAndThermalToTheLiveKnobs) {
    // Motor inteiro (sem GPU) com a sondagem de um aparelho de entrada.
    Engine e;
    EngineConfig ec;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    const Profile low = low_profile();
    ec.platformInfo = low.info;
    ec.hasPlatformInfo = true;
    AUREA_CHECK(e.initialize(ec).ok());
    // Sem GPU medida a classe já é LOW pela memória (a GPU desconhecida só
    // seguraria em MID).
    AUREA_CHECK_EQ(e.caps().tier(), DeviceTier::Low);
    AUREA_CHECK_NEAR(e.preview_heavy_scale(), 0.5f, 1e-6f);
    const u64 budget = e.caps().memory_budget_bytes();
    AUREA_CHECK_EQ(e.memory().budget(MemoryClass::DecodedFrames), budget * 16 / 100);

    e.set_thermal(static_cast<u32>(thermal_state_from_android(1).level), thermal_state_from_android(1).throttling);
    AUREA_CHECK_EQ(e.caps().thermal_tier(), ThermalTier::Warm);
    AUREA_CHECK_NEAR(e.preview_heavy_scale(), 0.5f, 1e-6f);        // WARM: preview igual
    const ThermalState crit = thermal_state_from_android(4);
    e.set_thermal(static_cast<u32>(crit.level), crit.throttling);
    AUREA_CHECK_EQ(e.caps().thermal_tier(), ThermalTier::Critical);
    AUREA_CHECK(e.preview_heavy_scale() <= 0.25f);
    AUREA_CHECK(e.preview_heavy_scale() > 0.0f);
    e.set_thermal(0, false);
    AUREA_CHECK_NEAR(e.preview_heavy_scale(), 0.5f, 1e-6f);
    e.shutdown();
}
