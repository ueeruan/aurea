// =============================================================================
//  Espaço de endereçamento de 32 bits (APK armeabi-v7a).
//
//  Beta 08/10: "lento e fecha só cortando clipes" num MediaTek Helio de 3 GB
//  com o APK de 32 bits; outro aparelho de 3 GB (64 bits) roda bem. As regras
//  de AddressSpace.hpp são funções puras da largura do ponteiro: aqui o host
//  (64 bits) chama com 32 e confere o ramo do aparelho — e confere que com 64
//  nada muda.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/platform/AddressSpace.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"
#include "aurea/render/PreviewCachePolicy.hpp"

#include <chrono>
#include <thread>

using namespace aurea;
using namespace aurea::test;

namespace {
constexpr u64 MiB = 1ull << 20, GiB = 1ull << 30;

bool wait_for(auto&& predicate, u32 timeoutMs = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

// Abre a fonte da layer e espera o open de fundo terminar.
VideoSource* open_layer(MediaManager& m, u32 layer, const Asset& asset, u64 frame) {
    VideoSource* s = nullptr;
    (void)wait_for([&] { s = m.source_for(LayerId{layer, 1}, AssetId{0, 1}, asset, frame); return s != nullptr; });
    return s;
}
} // namespace

AUREA_TEST(AddressSpace32, BuildFlagMatchesPointerWidth) {
    AUREA_CHECK_EQ(kPointerBits, static_cast<u32>(sizeof(void*) * 8));
    AUREA_CHECK(kAddressSpace32 == (sizeof(void*) == 4));
    AUREA_CHECK(address_space::narrow(32));
    AUREA_CHECK(!address_space::narrow(64));
}

AUREA_TEST(AddressSpace32, ProcessBudgetCappedOnlyOn32Bit) {
    // 3 GB de RAM, 1/4 disponível como orçamento = 768 MB: em 32 bits cai para 256.
    AUREA_CHECK_EQ(address_space::process_budget(768 * MiB, 32), 256 * MiB);
    AUREA_CHECK_EQ(address_space::process_budget(768 * MiB, 64), 768 * MiB);
    AUREA_CHECK_EQ(address_space::process_budget(128 * MiB, 32), 128 * MiB);   // menor que o teto: fica
    AUREA_CHECK_EQ(address_space::process_budget(4 * GiB, 64), 4 * GiB);
    // DeviceCapabilities delega para a regra central.
    AUREA_CHECK_EQ(DeviceCapabilities::process_budget_limit(768 * MiB, 32), 256 * MiB);
    AUREA_CHECK_EQ(DeviceCapabilities::process_budget_limit(768 * MiB, 64), 768 * MiB);
    AUREA_CHECK_EQ(DeviceCapabilities::process_budget_limit(768 * MiB), address_space::process_budget(768 * MiB, kPointerBits));
    // Prévia guardada num aparelho de 3 GB, classe LOW: 32 MiB nos dois builds,
    // e nunca acima de 1/4 do orçamento de 32 bits.
    const u64 b32 = address_space::process_budget(768 * MiB, 32);
    AUREA_CHECK_EQ(preview_cache_budget(3 * GiB, b32, 0.f, true), 32 * MiB);
    AUREA_CHECK(preview_cache_budget(6 * GiB, b32) <= b32 / 4);
    AUREA_CHECK_EQ(preview_cache_budget(6 * GiB, address_space::process_budget(1536 * MiB, 64)), 320 * MiB);
}

AUREA_TEST(AddressSpace32, PolicyKnobsUnchangedOn64Bit) {
    // GPU: em 64 bits tudo que é host-visible continua mapeado.
    AUREA_CHECK(address_space::map_gpu_memory_eagerly(64));
    AUREA_CHECK(!address_space::map_gpu_memory_eagerly(32));
    // Decoders vivos: sem teto em 64 bits.
    AUREA_CHECK_EQ(address_space::max_video_sources(64), 0u);
    AUREA_CHECK_EQ(address_space::max_video_sources(32), address_space::kNarrowMaxVideoSources);
    // Prazo de ocioso: a prévia (180) encolhe em 32 bits; o export (2) nunca.
    AUREA_CHECK_EQ(address_space::source_idle_frames(180, 64), 180u);
    AUREA_CHECK_EQ(address_space::source_idle_frames(180, 32), address_space::kNarrowSourceIdleFrames);
    AUREA_CHECK_EQ(address_space::source_idle_frames(2, 32), 2u);
    AUREA_CHECK_EQ(address_space::source_idle_frames(1, 32), 1u);
    // Rede de IA: 30 s em 64 bits, 5 s em 32.
    AUREA_CHECK_EQ(address_space::ai_idle_release_ms(30'000, 64), 30'000u);
    AUREA_CHECK_EQ(address_space::ai_idle_release_ms(30'000, 32), address_space::kNarrowAiIdleReleaseMs);
    // off_t de 32 bits: acima de 2 GB não cabe; 64 bits cabe tudo.
    AUREA_CHECK(address_space::file_offset_fits(0x7FFFFFFFull, 32));
    AUREA_CHECK(!address_space::file_offset_fits(0x80000000ull, 32));
    AUREA_CHECK(!address_space::file_offset_fits(5 * GiB, 32));
    AUREA_CHECK(address_space::file_offset_fits(5 * GiB, 64));
}

AUREA_TEST(AddressSpace32, CuttingClipsKeepsDecoderCountBounded) {
    // Cada corte é uma layer nova com decoder próprio. Em 32 bits, a fonte
    // ociosa mais antiga cede o lugar: nunca mais que o teto abertas.
    SyntheticFactory factory(SyntheticConfig{});
    MediaManager m;
    m.set_pointer_bits(32);
    m.set_factory(&factory);
    Asset asset;
    const u32 cap = address_space::max_video_sources(32);
    u64 frame = 1;
    for (u32 layer = 1; layer <= cap; ++layer, frame += 20)
        AUREA_CHECK(open_layer(m, layer, asset, frame) != nullptr);
    AUREA_CHECK_EQ(m.live_sources(), cap);
    // Mais cortes, cada um longe dos anteriores na timeline.
    for (u32 layer = cap + 1; layer <= cap + 4; ++layer, frame += 20) {
        AUREA_CHECK(open_layer(m, layer, asset, frame) != nullptr);
        AUREA_CHECK(m.live_sources() <= cap);
    }
    AUREA_CHECK_EQ(m.live_sources(), cap);
    // O prazo de ocioso da prévia também encolhe: 60 quadros, não 180.
    m.collect(frame + address_space::kNarrowSourceIdleFrames + 1);
    AUREA_CHECK_EQ(m.live_sources(), 0u);
    m.close_all();
}

AUREA_TEST(AddressSpace32, LayersShownTogetherNeverEvictEachOther) {
    // Mais layers visíveis juntas que o teto: nenhuma usada há pouco cede o
    // lugar (seria um seek por quadro). O teto só vale para fontes ociosas.
    SyntheticFactory factory(SyntheticConfig{});
    MediaManager m;
    m.set_pointer_bits(32);
    m.set_factory(&factory);
    Asset asset;
    const u32 n = address_space::max_video_sources(32) + 2;
    for (u32 layer = 1; layer <= n; ++layer) AUREA_CHECK(open_layer(m, layer, asset, 10) != nullptr);
    AUREA_CHECK_EQ(m.live_sources(), n);
    const u32 opened = factory.opened.load();
    for (u64 f = 11; f < 15; ++f)
        for (u32 layer = 1; layer <= n; ++layer)
            AUREA_CHECK(m.source_for(LayerId{layer, 1}, AssetId{0, 1}, asset, f) != nullptr);
    AUREA_CHECK_EQ(factory.opened.load(), opened);
    m.close_all();
}

AUREA_TEST(AddressSpace32, SixtyFourBitKeepsEveryDecoderAndTheLongIdle) {
    SyntheticFactory factory(SyntheticConfig{});
    MediaManager m;
    m.set_pointer_bits(64);
    m.set_factory(&factory);
    Asset asset;
    u64 frame = 1;
    for (u32 layer = 1; layer <= 8; ++layer, frame += 20) AUREA_CHECK(open_layer(m, layer, asset, frame) != nullptr);
    AUREA_CHECK_EQ(m.live_sources(), 8u);
    // 61 quadros depois da última: em 64 bits o prazo continua 180.
    const u64 last = frame - 20;
    m.collect(last + 61);
    AUREA_CHECK(m.live_sources() >= 1u);
    m.close_all();
}
