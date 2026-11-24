// =============================================================================
//  0.5.3 (Fase B da auditoria docs/architecture/EXPORT_053_AUDIT.md), regras
//  puras do export que valem em todo aparelho:
//   - P0-7: o orçamento de memória do export vem do aparelho (memória total e
//     processo de 32 bits, nunca da marca) e desce um degrau por morte;
//   - P0-5: o tempo do pacote do encoder × os quadros pendentes (rearredondado
//     é aceito; quadro posterior = o encoder descartou);
//   - P2-1: a memória da escada de encoders entre aberturas do app.
//  Incluído por test_export_attempts.cpp.
// =============================================================================
#include "aurea/export/ExportMemory.hpp"
#include "aurea/export/EncoderPacketTiming.hpp"
#include "aurea/export/FFmpegExportSink.hpp"
#include "aurea/render/GPUBackend.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
constexpr u64 kMiB = 1ull << 20, kGiB = 1ull << 30;
}

AUREA_TEST(Export053, DeviceMemoryCapComesFromTotalRamAnd32BitProcessNeverFromTheBrand) {
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(0, 64), 0ull);              // não medido: sem teto a mais
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(2 * kGiB, 64), 384 * kMiB);
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(3 * kGiB, 64), 384 * kMiB);
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(3700 * kMiB, 64), 512 * kMiB);   // "4 GB" como o Android informa
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(4 * kGiB, 64), 512 * kMiB);
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(5600 * kMiB, 64), 768 * kMiB);   // "6 GB"
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(8 * kGiB, 64), 1536 * kMiB);
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(12 * kGiB, 64), 2048 * kMiB);
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(16 * kGiB, 64), 2048 * kMiB);
    // Processo de 32 bits: no máximo 512 MB, mesmo num aparelho de 8 GB.
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(8 * kGiB, 32), 512 * kMiB);
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(3 * kGiB, 32), 384 * kMiB);
    AUREA_CHECK_EQ(export_device_memory_cap_bytes(0, 32), 512 * kMiB);
}

AUREA_TEST(Export053, MemoryStepLowersTheExportBudgetWithAFloorAndNeverRaisesIt) {
    // Degrau 0: o menor entre o do processo e o teto do aparelho.
    AUREA_CHECK_EQ(export_memory_budget_bytes(1000 * kMiB, 512 * kMiB, 0), 512 * kMiB);
    AUREA_CHECK_EQ(export_memory_budget_bytes(300 * kMiB, 512 * kMiB, 0), 300 * kMiB);
    AUREA_CHECK_EQ(export_memory_budget_bytes(1000 * kMiB, 0, 0), 1000 * kMiB);
    // Cada morte: 75%, 50%, 35% — e o degrau acima de 3 fica no 3.
    u64 previous = export_memory_budget_bytes(1024 * kMiB, 1024 * kMiB, 0);
    for (u32 step = 1; step <= kExportMemoryStepMax; ++step) {
        const u64 now = export_memory_budget_bytes(1024 * kMiB, 1024 * kMiB, step);
        AUREA_CHECK(now < previous);
        previous = now;
    }
    AUREA_CHECK_EQ(export_memory_budget_bytes(1024 * kMiB, 1024 * kMiB, 99),
                   export_memory_budget_bytes(1024 * kMiB, 1024 * kMiB, kExportMemoryStepMax));
    const u64 half = export_memory_budget_bytes(1000 * kMiB, 0, 2);
    AUREA_CHECK(half >= 499 * kMiB && half <= 500 * kMiB);
    // Piso: um quadro 4K NV12 ainda cabe na fila (orçamento / 8 ≥ 12,4 MB).
    AUREA_CHECK_EQ(export_memory_budget_bytes(200 * kMiB, 0, 3), kExportMemoryFloorBytes);
    AUREA_CHECK(kExportMemoryFloorBytes / 8 >= 3840ull * 2160ull * 3 / 2);
    // O piso nunca sobe acima do que o processo tinha.
    AUREA_CHECK_EQ(export_memory_budget_bytes(100 * kMiB, 0, 3), 100 * kMiB);
    AUREA_CHECK_EQ(export_memory_budget_bytes(0, 512 * kMiB, 1), 0ull);
}

AUREA_TEST(Export053, DriverHeadroomUsesOneDeviceLocalHeapAndSaturatesUnderPressure) {
    GpuMemoryBudget snapshot;
    snapshot.heapCount = 4;
    snapshot.heaps[0] = {128 * kMiB, 80 * kMiB, true};
    snapshot.heaps[1] = {64 * kMiB, 24 * kMiB, true};
    snapshot.heaps[2] = {kGiB, 0, false};
    snapshot.heaps[3] = {0, kGiB, true};
    AUREA_CHECK_EQ(snapshot.largest_device_local_headroom().value_or(0), 48 * kMiB);

    snapshot.heaps[0].usageBytes = 140 * kMiB;
    snapshot.heaps[1].usageBytes = 64 * kMiB;
    AUREA_CHECK_EQ(snapshot.heaps[0].headroom_bytes(), 0ull);
    const auto exhausted = snapshot.largest_device_local_headroom();
    AUREA_CHECK(exhausted.has_value());
    AUREA_CHECK_EQ(exhausted.value_or(kGiB), 0ull);

    snapshot.heaps[0].budgetBytes = snapshot.heaps[1].budgetBytes = 0;
    AUREA_CHECK(!snapshot.largest_device_local_headroom().has_value());
    AUREA_CHECK(!GpuMemoryBudget{}.largest_device_local_headroom().has_value());
}

AUREA_TEST(Export053, DriverBudgetLimitsConcurrencyAndPreservesTraditionalFallback) {
    constexpr u64 enough4k = 3840ull * 2160 * 16;
    AUREA_CHECK(export_gpu_can_overlap_frames(std::nullopt, 3840, 2160));
    AUREA_CHECK(export_gpu_can_overlap_frames(enough4k, 3840, 2160));
    AUREA_CHECK(!export_gpu_can_overlap_frames(enough4k - 1, 3840, 2160));
    AUREA_CHECK(!export_gpu_can_overlap_frames(0, 1920, 1080));
    AUREA_CHECK(!export_gpu_can_overlap_frames(~0ull, ~0u, ~0u));

    AUREA_CHECK_EQ(export_exposure_batch_budget_bytes(32 * kMiB, std::nullopt), 32 * kMiB);
    AUREA_CHECK_EQ(export_exposure_batch_budget_bytes(32 * kMiB, 256 * kMiB), 32 * kMiB);
    AUREA_CHECK_EQ(export_exposure_batch_budget_bytes(32 * kMiB, 40 * kMiB), 10 * kMiB);
    AUREA_CHECK_EQ(export_exposure_batch_budget_bytes(32 * kMiB, 0), 1ull);
    AUREA_CHECK_EQ(export_exposure_batch_budget_bytes(0, 0), 0ull);
}

AUREA_TEST(Export053, PacketTimeRetimedIsTheSameFrameAndALaterOneIsADrop) {
    using namespace export_timing;
    const i64 tolerance = retime_tolerance_us(30.0);
    AUREA_CHECK_EQ(tolerance, 16'665);                       // meio quadro a 30 qps, menos 1 µs
    AUREA_CHECK_EQ(retime_tolerance_us(0.0), 0);
    const std::array<i64, 4> pending{0, 33'333, 66'667, 100'000};
    const auto at = [&](u32 i) { return pending[i]; };
    const auto match = [&](i64 us, i64 tol = -1) { return match_packet(at, 4u, us, tol < 0 ? tolerance : tol); };
    AUREA_CHECK(match(0).kind == PacketMatch::Exact);
    // Rearredondado (milissegundos, grade do encoder): o mesmo quadro.
    AUREA_CHECK(match(1'000).kind == PacketMatch::Retimed);
    AUREA_CHECK(match(-1'000).kind == PacketMatch::Retimed);
    // Quadro posterior: o encoder descartou o(s) anterior(es).
    AUREA_CHECK(match(33'333).kind == PacketMatch::Dropped);
    AUREA_CHECK_EQ(match(33'333).skipped, 1u);
    AUREA_CHECK(match(66'000).kind == PacketMatch::Dropped);
    AUREA_CHECK_EQ(match(66'000).skipped, 2u);
    // Mais perto do quadro seguinte que do mais antigo: nunca vira "o mesmo".
    AUREA_CHECK(match(16'700).kind == PacketMatch::Dropped);
    // Nenhum quadro com esse tempo (duplicado de um já entregue, lixo).
    AUREA_CHECK(match(-40'000).kind == PacketMatch::Unknown);
    AUREA_CHECK(match(5'000'000).kind == PacketMatch::Unknown);
    // Taxa variável (tolerância 0): só o tempo exato.
    AUREA_CHECK(match(1'000, 0).kind == PacketMatch::Unknown);
    AUREA_CHECK(match(33'333, 0).kind == PacketMatch::Dropped);
    AUREA_CHECK(match_packet(at, 0u, 0, tolerance).kind == PacketMatch::Unknown);
}

AUREA_TEST(Export053, EncoderLadderMemorySurvivesReopenAndForgetsOnAnotherDeviceOrAppVersion) {
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "aurea-export053-rungs";
    std::filesystem::create_directories(dir, ec);
    const std::string path = (dir / "export-rungs.txt").string();
    std::filesystem::remove(path, ec);
    const char* surface = "surface:c2.vendor.avc.encoder|avc|1080";
    configure_export_rung_memory(path.c_str(), "aparelho-A|sistema-1|2220");
    AUREA_CHECK_EQ(export_rung_memory_count('F', surface, 0), 0u);
    export_rung_memory_note('F', surface);
    AUREA_CHECK_EQ(export_rung_memory_count('F', surface, 3600), 1u);
    export_rung_memory_note('F', surface);
    AUREA_CHECK_EQ(export_rung_memory_count('F', surface, 3600), 2u);
    export_rung_memory_note('C', "cbr:c2.vendor.avc.encoder|avc");
    // Outro processo (reabre o arquivo): o que foi aprendido continua.
    configure_export_rung_memory(path.c_str(), "aparelho-A|sistema-1|2220");
    AUREA_CHECK_EQ(export_rung_memory_count('F', surface, 3600), 2u);
    AUREA_CHECK_EQ(export_rung_memory_count('C', "cbr:c2.vendor.avc.encoder|avc", 3600), 1u);
    char text[1024]{};
    (void)export_rung_memory_text(text, sizeof(text));
    AUREA_CHECK(std::strstr(text, "F 2 surface:c2.vendor.avc.encoder|avc|1080") != nullptr);
    // O degrau que deu certo apaga as falhas dele.
    export_rung_memory_note('G', surface);
    AUREA_CHECK_EQ(export_rung_memory_count('F', surface, 0), 0u);
    AUREA_CHECK_EQ(export_rung_memory_count('G', surface, 0), 1u);
    // Tabulação/quebra na chave não estraga o arquivo.
    export_rung_memory_note('N', "hints:bad\tname\nx|avc");
    configure_export_rung_memory(path.c_str(), "aparelho-A|sistema-1|2220");
    AUREA_CHECK_EQ(export_rung_memory_count('N', "hints:bad name x|avc", 0), 1u);
    AUREA_CHECK_EQ(export_rung_memory_count('C', "cbr:c2.vendor.avc.encoder|avc", 0), 1u);
    // Outro aparelho, sistema ou versão do app: começa do zero.
    configure_export_rung_memory(path.c_str(), "aparelho-A|sistema-2|2220");
    AUREA_CHECK_EQ(export_rung_memory_count('C', "cbr:c2.vendor.avc.encoder|avc", 0), 0u);
    AUREA_CHECK_EQ(export_rung_memory_count('G', surface, 0), 0u);
    // Tabela cheia: a nota mais antiga sai, a nova entra.
    for (int i = 0; i < 60; ++i) export_rung_memory_note('F', ("buffer:codec-" + std::to_string(i) + "|avc|720").c_str());
    AUREA_CHECK_EQ(export_rung_memory_count('F', "buffer:codec-59|avc|720", 0), 1u);
    // Tipo desconhecido ou chave vazia: ignorado.
    export_rung_memory_note('X', "qualquer");
    export_rung_memory_note('F', "");
    AUREA_CHECK_EQ(export_rung_memory_count('X', "qualquer", 0), 0u);
    // Sem caminho: só memória do processo, vazia (não contamina os outros testes).
    configure_export_rung_memory("", "");
    AUREA_CHECK_EQ(export_rung_memory_count('F', "buffer:codec-59|avc|720", 0), 0u);
    std::filesystem::remove_all(dir, ec);
}
