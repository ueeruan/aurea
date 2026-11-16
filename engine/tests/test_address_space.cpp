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
#include "aurea/core/Time.hpp"

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

AUREA_TEST(AddressSpace32, SixtyFourBitPreviewCapEvictsOnlyIdleSources) {
    // 0.5.3: a prévia do motor também tem teto em 64 bits (set_source_cap). Cortes
    // rápidos não acumulam decoders; layers visíveis juntas acima do teto ficam.
    SyntheticFactory factory(SyntheticConfig{});
    MediaManager m;
    m.set_pointer_bits(64);
    m.set_source_cap(4);
    m.set_factory(&factory);
    Asset asset;
    u64 frame = 1;
    for (u32 layer = 1; layer <= 10; ++layer, frame += 20) {
        AUREA_CHECK(open_layer(m, layer, asset, frame) != nullptr);
        AUREA_CHECK(m.live_sources() <= 4u);
    }
    AUREA_CHECK_EQ(m.live_sources(), 4u);
    // Seis layers visíveis no mesmo quadro: nenhuma cede o lugar a outra.
    m.close_all();
    for (u32 layer = 1; layer <= 6; ++layer) AUREA_CHECK(open_layer(m, layer, asset, 1000) != nullptr);
    const u32 opened = factory.opened.load();
    for (u64 f = 1001; f < 1005; ++f)
        for (u32 layer = 1; layer <= 6; ++layer)
            AUREA_CHECK(m.source_for(LayerId{layer, 1}, AssetId{0, 1}, asset, f) != nullptr);
    AUREA_CHECK_EQ(factory.opened.load(), opened);
    AUREA_CHECK_EQ(m.live_sources(), 6u);
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

// =============================================================================
//  0.5.4 — "Coloquei uns 3 vídeos e alguns não apareciam" (iPhone; diminuir a
//  qualidade da prévia ajudou um pouco). O aparelho tem poucas sessões de
//  decoder e, logo depois de importar, o proxy de edição, a miniatura e as
//  camadas disputam todas. Antes: o open recusado tentava 5 vezes contadas em
//  QUADROS (parado o contador não anda) e o decode recusado tentava 2 vezes; a
//  camada ficava transparente para sempre, mesmo depois de o proxy e a
//  miniatura devolverem as sessões. Estes testes simulam a prévia PARADA: o
//  render só roda quando algo o acorda (o callback de pronto das fontes, o
//  fechamento de um decoder) ou quando o MediaManager agenda uma nova tentativa
//  (next_retry_ns, que o Engine::render_frame transforma em quadro agendado).
// =============================================================================
namespace {
/// Sessões de decoder do "aparelho". `used` inclui as do proxy/miniatura.
struct DecoderSlots {
    std::mutex mutex;
    i32 total = 0, used = 0;
    bool acquire() { std::lock_guard<std::mutex> l(mutex); if (used >= total) return false; ++used; return true; }
    void release() { std::lock_guard<std::mutex> l(mutex); if (used > 0) --used; }
};

/// Decoder que ocupa uma sessão. Preguiçoso (iOS/AVAssetReader): o objeto abre
/// sem sessão e a pede no primeiro seek; sem sessão livre o seek falha e ESTE
/// decoder fica morto para sempre (o reader de uma sessão derrubada não volta):
/// só uma fonte nova pode decodificar.
class SlotDecoder final : public VideoDecoderBackend {
public:
    SlotDecoder(const SyntheticConfig& c, DecoderSlots& slots, bool holding) : inner_(c), slots_(slots), holding_(holding) {}
    ~SlotDecoder() override { if (holding_) slots_.release(); }
    const VideoStreamInfo& info() const noexcept override { return inner_.info(); }
    Status seek_to_keyframe(i64 us) noexcept override {
        if (!holding_ && !dead_) {
            holding_ = slots_.acquire();
            dead_ = !holding_;
        }
        if (dead_) return Status{Errc::DecodeFailed, "decoder ocupado (sintetico)"};
        return inner_.seek_to_keyframe(us);
    }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        if (!holding_) return Status{Errc::DecodeFailed, "decoder ocupado (sintetico)"};
        return inner_.next_frame(from, out, pts, eos);
    }
    u32 max_live_frames() const noexcept override { return inner_.max_live_frames(); }
    i64 keyframe_interval_us() const noexcept override { return inner_.keyframe_interval_us(); }
private:
    SyntheticDecoder inner_;
    DecoderSlots& slots_;
    bool holding_ = false, dead_ = false;
};

class SlotFactory final : public VideoSourceFactory {
public:
    SlotFactory(const SyntheticConfig& c, DecoderSlots& slots, bool lazy) : cfg_(c), slots_(slots), lazy_(lazy) {}
    bool probe(const char*, MediaProbe& out) override { SyntheticDecoder d(cfg_); out.video = d.info(); out.hasVideo = true; return true; }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        ++opens;
        if (lazy_) return std::make_unique<SlotDecoder>(cfg_, slots_, false);
        // Android: AMediaCodec_createDecoderByType/configure recusado → nulo.
        if (!slots_.acquire()) { ++refused; return nullptr; }
        return std::make_unique<SlotDecoder>(cfg_, slots_, true);
    }
    std::atomic<u32> opens{0}, refused{0};
private:
    SyntheticConfig cfg_;
    DecoderSlots& slots_;
    bool lazy_;
};

/// A prévia parada do motor, reduzida ao que importa aqui.
struct PausedPreview {
    std::atomic<u32> wakes{0};
    u32 seen = 0;
    u64 frame = 1;
    u32 renders = 0;
    static void on_ready(void* self) { static_cast<PausedPreview*>(self)->wakes.fetch_add(1); }
    bool due(const MediaManager& m) {
        if (wakes.load() != seen) return true;
        const u64 retry = m.next_retry_ns();
        return retry && monotonic_ns() >= retry;
    }
    /// Um quadro: cada camada visível pede o quadro 0 (parado, pedido igual a
    /// cada quadro — a fonte o coalesce, como no motor). Devolve quantas têm
    /// o quadro exato.
    u32 render(MediaManager& m, const Asset& asset, u32 layers) {
        seen = wakes.load();
        ++frame; ++renders;
        u32 exact = 0;
        for (u32 layer = 1; layer <= layers; ++layer) {
            VideoSource* src = m.source_for(LayerId{layer, 1}, AssetId{0, 1}, asset, frame);
            if (!src) continue;
            src->set_epoch(1);
            bool ok = false;
            (void)src->frame_for(0, &ok);
            if (ok) { ++exact; continue; }
            DecodeRequest req; req.targetUs = 0; req.mode = DecodeMode::Still;
            src->request(req);
        }
        return exact;
    }
};

/// Roda a prévia parada até `untilNs`; devolve o maior número de camadas com quadro visto.
u32 run_paused(PausedPreview& p, MediaManager& m, const Asset& asset, u32 layers, u64 untilNs, bool stopWhenAll) {
    u32 best = p.render(m, asset, layers);
    while (monotonic_ns() < untilNs && !(stopWhenAll && best == layers)) {
        if (p.due(m)) best = std::max(best, p.render(m, asset, layers));
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return best;
}
} // namespace

AUREA_TEST(DecoderRecovery054, RefusedOpensRecoverWhilePausedOnceTheDeviceFreesSessions) {
    // Android: o codec é recusado na abertura. 4 sessões, 2 tomadas pelo proxy
    // e pela miniatura; 4 camadas visíveis (teto da prévia em 2: o teto nunca
    // segura camada visível). Parado, por mais tempo que as 5 tentativas antigas.
    DecoderSlots slots; slots.total = 4; slots.used = 2;
    SlotFactory factory(SyntheticConfig{}, slots, false);
    PausedPreview preview;
    MediaManager m;
    m.set_pointer_bits(64);
    m.set_source_cap(2);
    m.set_persistent_retry(true);
    m.set_ready_callback(&PausedPreview::on_ready, &preview);
    m.set_factory(&factory);
    Asset asset;
    const u32 layers = 4;
    const u32 before = run_paused(preview, m, asset, layers, monotonic_ns() + 1'500'000'000ull, false);
    AUREA_CHECK_EQ(before, 2u);                 // só cabem duas
    AUREA_CHECK(factory.refused.load() >= 2u);  // as outras foram recusadas e seguem tentando
    const u32 rendersWhileBusy = preview.renders;
    // O proxy e a miniatura terminam: as sessões voltam.
    slots.release(); slots.release();
    const u64 released = monotonic_ns();
    const u32 after = run_paused(preview, m, asset, layers, released + 4'000'000'000ull, true);
    const f64 recoverMs = static_cast<f64>(monotonic_ns() - released) * 1e-6;
    std::printf("recusadas=%u opens=%u renders=%u(+%u) volta=%.0f ms ", factory.refused.load(), factory.opens.load(),
                rendersWhileBusy, preview.renders - rendersWhileBusy, recoverMs);
    AUREA_CHECK_EQ(after, layers);              // todas as camadas visíveis com o quadro
    AUREA_CHECK(recoverMs < 3000.0);            // no máximo uma espera de 2 s + o open
    AUREA_CHECK(rendersWhileBusy < 200u);       // parado não vira laço de render
    m.close_all();
}

AUREA_TEST(DecoderRecovery054, BusyDecodersAreReplacedAndEveryVisibleLayerGetsItsFrame) {
    // iOS: o AVAssetReader abre, mas o decode falha com o decoder ocupado e o
    // reader daquela sessão não volta (media services reset). 3 sessões, 2
    // tomadas; 3 camadas visíveis. A fonte que só falha é trocada por uma nova
    // (MediaManager) e a nova pega a sessão quando ela volta.
    DecoderSlots slots; slots.total = 3; slots.used = 2;
    SlotFactory factory(SyntheticConfig{}, slots, true);
    PausedPreview preview;
    MediaManager m;
    m.set_pointer_bits(64);
    m.set_persistent_retry(true);
    m.set_ready_callback(&PausedPreview::on_ready, &preview);
    m.set_factory(&factory);
    Asset asset;
    const u32 layers = 3;
    const u32 before = run_paused(preview, m, asset, layers, monotonic_ns() + 1'500'000'000ull, false);
    AUREA_CHECK_EQ(before, 1u);
    const u32 opensWhileBusy = factory.opens.load();
    AUREA_CHECK(opensWhileBusy > layers);       // decoders mortos foram trocados
    slots.release(); slots.release();
    const u64 released = monotonic_ns();
    const u32 after = run_paused(preview, m, asset, layers, released + 5'000'000'000ull, true);
    const f64 recoverMs = static_cast<f64>(monotonic_ns() - released) * 1e-6;
    std::printf("opens=%u (%u ocupado) renders=%u volta=%.0f ms ", factory.opens.load(), opensWhileBusy,
                preview.renders, recoverMs);
    AUREA_CHECK_EQ(after, layers);
    AUREA_CHECK(recoverMs < 4500.0);
    m.close_all();
}

AUREA_TEST(DecoderRecovery054, ExportManagerKeepsTheBoundedRetries) {
    // Sem set_persistent_retry (o MediaManager do export): as regras antigas —
    // 5 opens contados em quadros, nada agendado pelo relógio.
    DecoderSlots slots; slots.total = 0;
    SlotFactory factory(SyntheticConfig{}, slots, false);
    MediaManager m;
    m.set_pointer_bits(64);
    m.set_factory(&factory);
    Asset asset;
    for (u64 f = 1; f < 400; ++f) {
        (void)m.source_for(LayerId{1, 1}, AssetId{0, 1}, asset, f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK_EQ(m.next_retry_ns(), 0ull);
    AUREA_CHECK(factory.opens.load() <= 5u);
    m.close_all();
}
