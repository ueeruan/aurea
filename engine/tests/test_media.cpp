// =============================================================================
//  Testes de mídia: cache de frames decodificados, DecodeScheduler (seek
//  coalescing, prefetch, descarte de intermediários) e playback.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/core/Time.hpp"
#include "aurea/media/DecodedFrameCache.hpp"
#include "aurea/media/ThumbnailService.hpp"
#include "aurea/media/VideoSource.hpp"
#include "aurea/playback/Playback.hpp"

#include <chrono>
#include <thread>

using namespace aurea;
using namespace aurea::test;

namespace {

FrameRef frame_at(i64 ptsUs) {
    auto* f = new SyntheticFrame();
    f->ptsUs = ptsUs;
    f->width = 64;
    f->height = 36;
    f->format = PixelFormat::NV12;
    return FrameRef::adopt(f);
}

constexpr i64 kFrame = 33'333;

void wait_until(auto&& predicate, u32 timeoutMs = 3000) {
    const auto start = std::chrono::steady_clock::now();
    while (!predicate()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace

AUREA_TEST(VideoSource, TightCacheDoesNotDecodeAndSeekForever) {
    SyntheticConfig cfg;
    auto decoder = std::make_unique<SyntheticDecoder>(cfg);
    auto* raw = decoder.get();
    VideoSource source(std::move(decoder), MediaPriority::Preview);
    DecodedFrameCache::Config budget;
    budget.maxFrames = 1;
    source.cache().configure(budget);
    source.start();
    source.request({raw->pts_of(30), DecodeMode::Playback, 1, 1.f});
    AUREA_CHECK(source.wait_for(raw->pts_of(30), 3000));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    AUREA_CHECK(raw->seeks.load() <= 1);
    AUREA_CHECK(raw->delivered.load() <= 5);
    source.request({raw->pts_of(31), DecodeMode::Playback, 1, 1.f});
    AUREA_CHECK(source.wait_for(raw->pts_of(31), 3000));
    source.stop();
}

AUREA_TEST(DecodedFrameCache, KeepsDisplayFrameWhenSingleFrameExceedsBudget) {
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxBytes = 1;
    cache.configure(cfg);
    (void)cache.insert(frame_at(0));
    AUREA_CHECK(cache.contains(0, 0));
}

AUREA_TEST(MediaManager, CodecStartupDoesNotBlockRenderOrStatus) {
    struct SlowFactory : VideoSourceFactory {
        std::atomic<bool> release{false};
        std::atomic<bool> entered{false};
        bool probe(const char*, MediaProbe&) override { return false; }
        std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
            entered.store(true);
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return std::make_unique<SyntheticDecoder>(SyntheticConfig{});
        }
    } factory;
    MediaManager manager;
    manager.set_factory(&factory);
    Asset asset;
    const auto start = std::chrono::steady_clock::now();
    auto* source = manager.source_for(LayerId{0, 1}, AssetId{0, 1}, asset, 1);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    AUREA_CHECK(source == nullptr);
    AUREA_CHECK(elapsed < std::chrono::milliseconds(100));
    wait_until([&] { return factory.entered.load(); });
    (void)manager.stats(); // Must remain accessible while opening is blocked.
    factory.release.store(true);
    wait_until([&] { source = manager.source_for(LayerId{0, 1}, AssetId{0, 1}, asset, 2); return source != nullptr; });
    AUREA_CHECK(source != nullptr);
    manager.close_all();
}

// =============================================================================
// DecodedFrameCache
// =============================================================================
AUREA_TEST(DecodedFrameCache, ExactAndNearestLookups) {
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxFrames = 8;
    cache.configure(cfg);
    for (i64 i = 10; i < 15; ++i) (void)cache.insert(frame_at(i * kFrame));
    bool exact = false;
    FrameRef f = cache.find(12 * kFrame + 1000, kFrame / 2, &exact);
    AUREA_CHECK(f && exact && f->ptsUs == 12 * kFrame);
    // Sem o exato: o anterior mais próximo (o que um player mostraria).
    f = cache.find(20 * kFrame, kFrame / 2, &exact);
    AUREA_CHECK(f && !exact && f->ptsUs == 14 * kFrame);
    // Antes de tudo: o mais próximo de qualquer lado.
    f = cache.find(0, kFrame / 2, &exact);
    AUREA_CHECK(f && !exact && f->ptsUs == 10 * kFrame);
}

AUREA_TEST(DecodedFrameCache, PlayingForwardEvictsWhatIsBehind) {
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxFrames = 4;
    cache.configure(cfg);
    cache.set_focus(10 * kFrame, +1);
    for (i64 i = 6; i <= 13; ++i) (void)cache.insert(frame_at(i * kFrame));
    AUREA_CHECK_EQ(cache.stats().frames, static_cast<u32>(4));
    // Ficam o playhead e o que está logo à frente.
    AUREA_CHECK(cache.contains(10 * kFrame, kFrame / 2));
    AUREA_CHECK(cache.contains(11 * kFrame, kFrame / 2));
    AUREA_CHECK(!cache.contains(6 * kFrame, kFrame / 2));
}

AUREA_TEST(DecodedFrameCache, ScrubbingBackKeepsWhatIsBehind) {
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxFrames = 4;
    cache.configure(cfg);
    cache.set_focus(10 * kFrame, -1);
    for (i64 i = 7; i <= 14; ++i) (void)cache.insert(frame_at(i * kFrame));
    AUREA_CHECK(cache.contains(9 * kFrame, kFrame / 2));
    AUREA_CHECK(!cache.contains(14 * kFrame, kFrame / 2));
}

AUREA_TEST(DecodedFrameCache, ByteBudgetIsRespected) {
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxFrames = 100;
    cfg.maxBytes = frame_at(0)->approx_bytes() * 3;
    cache.configure(cfg);
    for (i64 i = 0; i < 10; ++i) (void)cache.insert(frame_at(i * kFrame));
    AUREA_CHECK(cache.stats().bytes <= cfg.maxBytes);
    AUREA_CHECK_EQ(cache.stats().frames, static_cast<u32>(3));
}

AUREA_TEST(DecodedFrameCache, FrameOutlivesEvictionWhileReferenced) {
    // O renderer segura o frame enquanto a GPU lê; o cache pode soltá-lo.
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxFrames = 1;
    cache.configure(cfg);
    (void)cache.insert(frame_at(0));
    bool exact = false;
    FrameRef held = cache.find(0, kFrame / 2, &exact);
    (void)cache.insert(frame_at(kFrame));
    AUREA_CHECK(held && held->ptsUs == 0);   // ainda válido
}

AUREA_TEST(DecodedFrameCache, ContiguousEndFollowsTheRun) {
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxFrames = 10;
    cache.configure(cfg);
    for (i64 i : {5, 6, 7, 9}) (void)cache.insert(frame_at(i * kFrame));
    AUREA_CHECK_EQ(cache.contiguous_end(5 * kFrame, kFrame), 7 * kFrame);
    AUREA_CHECK(cache.contiguous_end(8 * kFrame, kFrame) < 8 * kFrame);
}

// =============================================================================
// VideoSource (DecodeScheduler)
// =============================================================================
AUREA_TEST(VideoSource, StoppedSourceDoesNotReportMissingFrameAsReady) {
    VideoSource src(std::make_unique<SyntheticDecoder>(SyntheticConfig{}), MediaPriority::Preview);
    AUREA_CHECK(!src.wait_for(0, 10));
}

AUREA_TEST(VideoSource, StopWakesPendingWaitWithoutReportingSuccess) {
    VideoSource src(std::make_unique<SyntheticDecoder>(SyntheticConfig{}), MediaPriority::Preview);
    src.start();
    std::atomic<bool> entered{false}, done{false};
    bool ready = true;
    std::thread waiter([&] {
        entered.store(true);
        ready = src.wait_for(0, 1500);
        done.store(true);
    });
    wait_until([&] { return entered.load(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    src.stop();
    wait_until([&] { return done.load(); }, 200);
    const bool woke = done.load();
    waiter.join();
    AUREA_CHECK(woke);
    AUREA_CHECK(!ready);
}

AUREA_TEST(VideoSource, RestartRedecodesTheLastRequest) {
    VideoSource src(std::make_unique<SyntheticDecoder>(SyntheticConfig{}), MediaPriority::Preview);
    src.start();
    src.request({0, DecodeMode::Still, 0, 1.0f});
    AUREA_CHECK(src.wait_for(0, 1000));
    src.stop();
    src.start();
    src.request({0, DecodeMode::Still, 0, 1.0f});
    const bool ready = src.wait_for(0, 1000);
    src.stop();
    AUREA_CHECK(ready);
}

AUREA_TEST(VideoSource, SameRequestAfterCacheClearIsDecodedAgain) {
    VideoSource src(std::make_unique<SyntheticDecoder>(SyntheticConfig{}), MediaPriority::Preview);
    src.start();
    src.request({0, DecodeMode::Still, 0, 1.0f});
    AUREA_CHECK(src.wait_for(0, 1000));
    src.cache().clear();
    src.request({0, DecodeMode::Still, 0, 1.0f});
    const bool ready = src.wait_for(0, 1000);
    src.stop();
    AUREA_CHECK(ready);
}

AUREA_TEST(DecodedFrameCache, ReplacementReappliesByteBudget) {
    DecodedFrameCache cache;
    DecodedFrameCache::Config cfg;
    cfg.maxFrames = 10;
    cfg.maxBytes = frame_at(0)->approx_bytes() * 3;
    cache.configure(cfg);
    cache.set_focus(0, 0);
    (void)cache.insert(frame_at(0));
    (void)cache.insert(frame_at(kFrame));
    (void)cache.insert(frame_at(2 * kFrame));
    auto larger = frame_at(0);
    larger->width *= 2;
    (void)cache.insert(std::move(larger));
    AUREA_CHECK(cache.stats().bytes <= cfg.maxBytes);
    AUREA_CHECK(cache.contains(0, 0));
}

AUREA_TEST(VideoSource, StillRequestDeliversTheExactFrame) {
    SyntheticConfig cfg;
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    const i64 target = raw->pts_of(47);
    src.request({target, DecodeMode::Still, 0, 1.0f});
    AUREA_CHECK(src.wait_for(target, 3000));
    bool exact = false;
    FrameRef f = src.frame_for(target, &exact);
    AUREA_CHECK(f && exact && f->ptsUs == target);
    // Um seek (keyframe 30) e os intermediários 30..46 descartados sem render.
    AUREA_CHECK_EQ(raw->seeks.load(), static_cast<u32>(1));
    AUREA_CHECK_EQ(raw->discarded.load(), static_cast<u32>(17));
    AUREA_CHECK_EQ(raw->delivered.load(), static_cast<u32>(1));
    src.stop();
}

AUREA_TEST(VideoSource, ScrubRequestsAreCoalesced) {
    // O dedo manda 1.0s, 1.1s, 1.2s, 1.3s, 1.4s rápido: só o último importa.
    SyntheticConfig cfg;
    cfg.decodeCostUs = 3000;   // decode "lento" para os pedidos se acumularem
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    const i64 targets[] = {1'000'000, 1'100'000, 1'200'000, 1'300'000, 1'400'000};
    for (i64 t : targets) src.request({t, DecodeMode::Scrub, +1, 1.0f});
    const i64 last = raw->pts_of(raw->frame_of(1'400'000));
    AUREA_CHECK(src.wait_for(last, 4000));
    bool exact = false;
    FrameRef f = src.frame_for(1'400'000, &exact);
    AUREA_CHECK(f && exact);
    const VideoSource::Stats st = src.stats();
    // Nenhum dos alvos intermediários virou um seek próprio: todos estão à
    // frente e ao alcance do decoder, que só seguiu andando.
    AUREA_CHECK(raw->seeks.load() <= 2);
    AUREA_CHECK(st.coalesced + st.forwardRetargets >= 3);
    // Não se desperdiçou render entregando cada alvo velho.
    AUREA_CHECK(raw->delivered.load() <= 4);
    src.stop();
}

AUREA_TEST(VideoSource, JumpingBackCostsASeek) {
    SyntheticConfig cfg;
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    const i64 a = raw->pts_of(200);
    src.request({a, DecodeMode::Still, 0, 1.0f});
    AUREA_CHECK(src.wait_for(a, 3000));
    const i64 b = raw->pts_of(40);
    src.request({b, DecodeMode::Scrub, -1, 1.0f});
    AUREA_CHECK(src.wait_for(b, 3000));
    AUREA_CHECK_EQ(raw->seeks.load(), static_cast<u32>(2));
    src.stop();
}

AUREA_TEST(VideoSource, NearForwardTargetReusesTheDecoderPosition) {
    SyntheticConfig cfg;
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    const i64 a = raw->pts_of(10);
    src.request({a, DecodeMode::Still, 0, 1.0f});
    AUREA_CHECK(src.wait_for(a, 3000));
    const i64 b = raw->pts_of(20);   // à frente, dentro do GOP: anda, sem seek
    src.request({b, DecodeMode::Still, 0, 1.0f});
    AUREA_CHECK(src.wait_for(b, 3000));
    AUREA_CHECK_EQ(raw->seeks.load(), static_cast<u32>(1));
    src.stop();
}

AUREA_TEST(VideoSource, PlaybackPrefetchesCurrentNextAndNextPlusOne) {
    SyntheticConfig cfg;
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    const i64 t = raw->pts_of(60);
    src.request({t, DecodeMode::Playback, +1, 1.0f});
    wait_until([&] { return src.cache().contains(raw->pts_of(62), kFrame / 2); });
    AUREA_CHECK(src.cache().contains(raw->pts_of(60), kFrame / 2));
    AUREA_CHECK(src.cache().contains(raw->pts_of(61), kFrame / 2));
    AUREA_CHECK(src.cache().contains(raw->pts_of(62), kFrame / 2));
    // E para por aí: não decodifica o vídeo inteiro.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    AUREA_CHECK(raw->delivered.load() <= 5);
    // O playhead avança: o prefetch acompanha.
    src.request({raw->pts_of(61), DecodeMode::Playback, +1, 1.0f});
    wait_until([&] { return src.cache().contains(raw->pts_of(63), kFrame / 2); });
    AUREA_CHECK(src.cache().contains(raw->pts_of(63), kFrame / 2));
    AUREA_CHECK_EQ(raw->seeks.load(), static_cast<u32>(1));
    src.stop();
}

AUREA_TEST(VideoSource, RequestPastTheEndShowsTheLastFrame) {
    SyntheticConfig cfg;
    cfg.frameCount = 40;
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    src.request({raw->pts_of(39) + 10'000'000, DecodeMode::Still, 0, 1.0f});
    wait_until([&] { return src.cache().contains(raw->pts_of(39), kFrame / 2); });
    bool exact = false;
    FrameRef f = src.frame_for(raw->pts_of(39) + 10'000'000, &exact);
    AUREA_CHECK(f && f->ptsUs == raw->pts_of(39));
    src.stop();
}

AUREA_TEST(VideoSource, SuspendReleasesDecoderBuffers) {
    SyntheticConfig cfg;
    auto dec = std::make_unique<SyntheticDecoder>(cfg);
    SyntheticDecoder* raw = dec.get();
    VideoSource src(std::move(dec), MediaPriority::Preview);
    src.start();
    src.request({raw->pts_of(5), DecodeMode::Still, 0, 1.0f});
    AUREA_CHECK(src.wait_for(raw->pts_of(5), 3000));
    src.suspend();
    wait_until([&] { return src.cache().stats().frames == 0; });
    AUREA_CHECK_EQ(src.cache().stats().frames, static_cast<u32>(0));
    src.resume();
    AUREA_CHECK(src.wait_for(raw->pts_of(5), 3000));
    src.stop();
}

// =============================================================================
// Playback
// =============================================================================
AUREA_TEST(Playback, PlayAdvancesWithTheClockAndPauseHolds) {
    PlaybackController pc;
    pc.configure(30.0, FrameIndex{300});
    pc.play(0);
    AUREA_CHECK(pc.playing());
    AUREA_CHECK_EQ(pc.update(1'000'000'000ull).value, static_cast<i64>(30));
    pc.pause(1'000'000'000ull);
    AUREA_CHECK_EQ(pc.update(5'000'000'000ull).value, static_cast<i64>(30));
    pc.play(5'000'000'000ull);
    AUREA_CHECK_EQ(pc.update(5'500'000'000ull).value, static_cast<i64>(45));
}

AUREA_TEST(Playback, SpeedScalesTheClock) {
    PlaybackController pc;
    pc.configure(30.0, FrameIndex{1000});
    pc.set_speed(2.0f, 0);
    pc.play(0);
    AUREA_CHECK_EQ(pc.update(1'000'000'000ull).value, static_cast<i64>(60));
}

AUREA_TEST(Playback, StopsAtTheEndOrLoops) {
    PlaybackController pc;
    pc.configure(30.0, FrameIndex{30});
    pc.play(0);
    AUREA_CHECK_EQ(pc.update(2'000'000'000ull).value, static_cast<i64>(29));
    AUREA_CHECK(!pc.playing());
    pc.set_loop(true);
    pc.play(0);   // do fim, volta ao começo
    const u64 g = pc.generation();
    AUREA_CHECK_EQ(pc.update(1'500'000'000ull).value, static_cast<i64>(15));
    AUREA_CHECK(pc.playing());
    AUREA_CHECK(pc.generation() > g - 1);
}

AUREA_TEST(Playback, ScrubTracksDirectionAndResumesPlayback) {
    PlaybackController pc;
    pc.configure(30.0, FrameIndex{300});
    pc.play(0);
    pc.begin_scrub(100'000'000ull);
    AUREA_CHECK(pc.mode() == PlaybackMode::Scrubbing);
    pc.scrub(FrameIndex{50}, 110'000'000ull);
    AUREA_CHECK_EQ(pc.direction(), 1);
    pc.scrub(FrameIndex{40}, 120'000'000ull);
    AUREA_CHECK_EQ(pc.direction(), -1);
    AUREA_CHECK_EQ(pc.current().value, static_cast<i64>(40));
    pc.end_scrub(130'000'000ull);
    // Estava tocando antes do dedo: volta a tocar de onde o dedo soltou.
    AUREA_CHECK(pc.playing());
    AUREA_CHECK_EQ(pc.update(130'000'000ull + 1'000'000'000ull).value, static_cast<i64>(70));
}

AUREA_TEST(Playback, StepMovesExactlyOneFrameAndPauses) {
    PlaybackController pc;
    pc.configure(30.0, FrameIndex{300});
    pc.seek(FrameIndex{10}, 0);
    pc.step(1, 0);
    AUREA_CHECK_EQ(pc.current().value, static_cast<i64>(11));
    pc.step(-3, 0);
    AUREA_CHECK_EQ(pc.current().value, static_cast<i64>(8));
    pc.step(-100, 0);
    AUREA_CHECK_EQ(pc.current().value, static_cast<i64>(0));
}

AUREA_TEST(Playback, MasterClockDrivesTimeWhenAvailable) {
    struct FakeAudio final : MasterClock {
        bool on = false;
        i64 pos = 0;
        bool available() const noexcept override { return on; }
        i64 position_ns() const noexcept override { return pos; }
    } audio;
    PlaybackController pc;
    pc.configure(30.0, FrameIndex{300});
    pc.clock().set_master(&audio);
    pc.play(0);
    AUREA_CHECK_EQ(pc.update(1'000'000'000ull).value, static_cast<i64>(30));   // sem áudio: sistema
    audio.on = true;
    audio.pos = 2'000'000'000;
    AUREA_CHECK_EQ(pc.update(1'000'000'000ull).value, static_cast<i64>(60));   // o áudio manda
}

AUREA_TEST(FrameScheduler, CountsSkippedCompositionFrames) {
    FrameScheduler fs;
    fs.presented(FrameIndex{0}, true);
    fs.presented(FrameIndex{1}, true);
    fs.presented(FrameIndex{4}, true);   // 2 e 3 não apareceram
    fs.presented(FrameIndex{4}, true);   // repetir (display mais rápido que o vídeo) não é perda
    AUREA_CHECK_EQ(fs.dropped_total(), static_cast<u32>(2));
    fs.presented(FrameIndex{100}, false);   // seek parado não é perda
    AUREA_CHECK_EQ(fs.dropped_total(), static_cast<u32>(2));
}

// -----------------------------------------------------------------------------
// Miniaturas da timeline
// -----------------------------------------------------------------------------
AUREA_TEST(Thumbnail, ConvertsWithTheVideoMatrixAndKeepsAspect) {
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    SyntheticDecoder dec(cfg);
    AUREA_CHECK(dec.seek_to_keyframe(0).ok());
    FrameRef f;
    i64 pts = 0;
    bool eos = false;
    AUREA_CHECK(dec.next_frame(-1, f, pts, eos).ok());
    AUREA_CHECK(static_cast<bool>(f));

    ThumbnailService::Image img;
    AUREA_CHECK(frame_to_thumbnail(*f.get(), 18, img));
    AUREA_CHECK_EQ(img.height, 18u);
    AUREA_CHECK_EQ(img.width, 32u);   // 16:9
    auto px = [&](u32 x, u32 y, u32 c) { return static_cast<int>(img.rgba[(static_cast<usize>(y) * img.width + x) * 4 + c]); };
    // Quadrantes: vermelho, verde, azul, branco (BT.709 limitado).
    AUREA_CHECK(px(4, 4, 0) > 240 && px(4, 4, 1) < 16 && px(4, 4, 2) < 16);
    AUREA_CHECK(px(28, 4, 1) > 240 && px(28, 4, 0) < 16 && px(28, 4, 2) < 16);
    AUREA_CHECK(px(4, 14, 2) > 240 && px(4, 14, 0) < 16 && px(4, 14, 1) < 16);
    AUREA_CHECK(px(28, 14, 0) > 240 && px(28, 14, 1) > 240 && px(28, 14, 2) > 240);
}

AUREA_TEST(Thumbnail, RotatedVideoIsUpright) {
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.rotation = 90;
    SyntheticDecoder dec(cfg);
    AUREA_CHECK(dec.seek_to_keyframe(0).ok());
    FrameRef f;
    i64 pts = 0;
    bool eos = false;
    AUREA_CHECK(dec.next_frame(-1, f, pts, eos).ok());
    f->rotation = 90;
    ThumbnailService::Image img;
    AUREA_CHECK(frame_to_thumbnail(*f.get(), 32, img));
    AUREA_CHECK_EQ(img.width, 18u);   // retrato
    // Girado 90° no sentido horário: o canto superior DIREITO da exibição é o
    // superior ESQUERDO do quadro codificado (vermelho).
    const usize tr = (static_cast<usize>(2) * img.width + (img.width - 3)) * 4;
    AUREA_CHECK(img.rgba[tr] > 240 && img.rgba[tr + 1] < 16);
}

AUREA_TEST(Thumbnail, ServiceDecodesAsyncAndCaches) {
    SyntheticConfig cfg;
    SyntheticFactory factory(cfg);
    ThumbnailService svc;
    svc.set_factory(&factory);
    svc.start();
    Asset a;
    a.kind = AssetKind::Video;
    ThumbnailService::Image img;
    const u32 gen0 = svc.generation();
    AUREA_CHECK(!svc.video(7, a, 1'000'000, 24, img));   // primeiro pedido: fila
    const u64 t0 = monotonic_ns();
    while (svc.generation() == gen0 && monotonic_ns() - t0 < 2'000'000'000ull) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(svc.video(7, a, 1'000'000, 24, img));
    AUREA_CHECK_EQ(img.height, 24u);
    AUREA_CHECK(img.width > 0);
    // Mesmo balde de 250 ms: vem do cache, sem abrir decoder novo.
    AUREA_CHECK(svc.video(7, a, 1'100'000, 24, img));
    AUREA_CHECK_EQ(factory.opened.load(), 1u);
    svc.stop();
}
