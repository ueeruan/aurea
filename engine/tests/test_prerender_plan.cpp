// Pre Render & Pre Export (1.0.2): as contas puras do painel — qualidade,
// "cabe ~Xs", livro de edições por trecho e o trecho efetivo.
#include "TestFramework.hpp"
#include "aurea/render/PreRenderPlan.hpp"

using namespace aurea;
using namespace aurea::prerender;

namespace {
constexpr u64 GiB = 1ull << 30;
Budget phone_budget(u64 free = 10 * GiB) {
    Budget b;
    b.capBytes = 4 * GiB;
    b.reserveBytes = 1 * GiB;
    b.freeDiskBytes = free;
    return b;
}
} // namespace

AUREA_TEST(PreRenderPlan, QualityDividesTheCompositionIntoEvenSides) {
    auto s = scaled_size(1920, 1080, 1);
    AUREA_CHECK_EQ(s.width, 1920u); AUREA_CHECK_EQ(s.height, 1080u);
    s = scaled_size(1920, 1080, 2);
    AUREA_CHECK_EQ(s.width, 960u); AUREA_CHECK_EQ(s.height, 540u);
    s = scaled_size(1920, 1080, 4);
    AUREA_CHECK_EQ(s.width, 480u); AUREA_CHECK_EQ(s.height, 270u);
    s = scaled_size(1920, 1080, 8);
    AUREA_CHECK_EQ(s.width, 240u); AUREA_CHECK_EQ(s.height, 136u);   // 135 → par
    s = scaled_size(1080, 1920, 8);
    AUREA_CHECK_EQ(s.width, 136u); AUREA_CHECK_EQ(s.height, 240u);
    s = scaled_size(40, 40, 8);
    AUREA_CHECK_EQ(s.width, 16u); AUREA_CHECK_EQ(s.height, 16u);     // nunca menor que 16
    // Divisor inválido vira 1/2 (o padrão do painel).
    s = scaled_size(1920, 1080, 3);
    AUREA_CHECK_EQ(s.width, 960u);
    AUREA_CHECK(!valid_divisor(0) && !valid_divisor(3) && !valid_divisor(16));
    AUREA_CHECK(valid_divisor(1) && valid_divisor(2) && valid_divisor(4) && valid_divisor(8));
    s = scaled_size(0, 1080, 1);
    AUREA_CHECK_EQ(s.width, 0u);
}

AUREA_TEST(PreRenderPlan, AvailableBytesFollowsTheCacheCapAndHalfTheFreeSpace) {
    // 10 GiB livres, 1 GiB de reserva: metade de 9 = 4,5 → o teto de 4 GiB vence.
    AUREA_CHECK_EQ(available_bytes(phone_budget()), 4 * GiB);
    // 5 GiB livres: (5 - 1) / 2 = 2 GiB.
    AUREA_CHECK_EQ(available_bytes(phone_budget(5 * GiB)), 2 * GiB);
    // O cache anterior sai antes do novo: o espaço dele conta como livre.
    Budget reclaim = phone_budget(3 * GiB);
    reclaim.reclaimableBytes = 2 * GiB;
    AUREA_CHECK_EQ(available_bytes(reclaim), 2 * GiB);
    // Abaixo da reserva: nada cabe.
    AUREA_CHECK_EQ(available_bytes(phone_budget(512ull << 20)), 0ull);
    // Sem medida do disco: só o teto.
    Budget unknown = phone_budget(0);
    unknown.freeDiskKnown = false;
    AUREA_CHECK_EQ(available_bytes(unknown), 4 * GiB);
    AUREA_CHECK_NEAR(fits_seconds(1000, 0), 0.0, 1e-12);
    AUREA_CHECK_NEAR(fits_seconds(1000, 100), 10.0, 1e-12);
}

AUREA_TEST(PreRenderPlan, FitsDurationGrowsAsQualityDrops) {
    const Budget b = phone_budget();
    const Fit full = plan_fit(1920, 1080, 30.0, 1, Mode::PreRender, b, 600);
    const Fit half = plan_fit(1920, 1080, 30.0, 2, Mode::PreRender, b, 600);
    const Fit quarter = plan_fit(1920, 1080, 30.0, 4, Mode::PreRender, b, 600);
    const Fit eighth = plan_fit(1920, 1080, 30.0, 8, Mode::PreRender, b, 600);
    // Inteira: 1920 × 1080 × 5 B × 30 qps ≈ 311 MB/s → ~13,8 s em 4 GiB.
    AUREA_CHECK_EQ(full.bytesPerSecond, (1920ull * 1080 * 5 + kCacheFrameOverheadBytes) * 30);
    AUREA_CHECK_NEAR(full.fitsSeconds, 4.0 * GiB / static_cast<f64>(full.bytesPerSecond), 1e-9);
    AUREA_CHECK(full.fitsSeconds > 13.0 && full.fitsSeconds < 15.0);
    AUREA_CHECK(full.fitsSeconds < half.fitsSeconds && half.fitsSeconds < quarter.fitsSeconds
                && quarter.fitsSeconds < eighth.fitsSeconds);
    // Cada degrau tem 1/4 dos pixels: cabe ~4× mais.
    AUREA_CHECK_NEAR(half.fitsSeconds / full.fitsSeconds, 4.0, 0.05);
    AUREA_CHECK_NEAR(quarter.fitsSeconds / half.fitsSeconds, 4.0, 0.05);
    // 20 s do trecho: a Inteira (~13,8 s) não cabe, 1/2 cabe.
    AUREA_CHECK_NEAR(full.rangeSeconds, 20.0, 1e-9);
    AUREA_CHECK(!full.fits);
    AUREA_CHECK(half.fits && quarter.fits && eighth.fits);
    AUREA_CHECK_EQ(half.width, 960u);
    AUREA_CHECK_EQ(half.height, 540u);
    // A medida do aparelho (bytes por pixel da última sessão) vale mais que a estimativa.
    const Fit measured = plan_fit(1920, 1080, 30.0, 2, Mode::PreRender, b, 300, 2.5);
    AUREA_CHECK(measured.fitsSeconds > half.fitsSeconds * 1.9);
    // O dobro de quadros por segundo: metade do tempo.
    const Fit sixty = plan_fit(1920, 1080, 60.0, 2, Mode::PreRender, b, 300);
    AUREA_CHECK_NEAR(sixty.fitsSeconds * 2.0, half.fitsSeconds, 1e-6);
    // Sem espaço: nada cabe.
    const Fit none = plan_fit(1920, 1080, 30.0, 8, Mode::PreRender, phone_budget(0), 30);
    AUREA_CHECK_NEAR(none.fitsSeconds, 0.0, 1e-12);
    AUREA_CHECK(!none.fits);
}

AUREA_TEST(PreRenderPlan, PreExportVideoFitsFarMoreThanCachedFrames) {
    const Budget b = phone_budget();
    const Fit frames = plan_fit(1920, 1080, 30.0, 1, Mode::PreRender, b, 300);
    const Fit video = plan_fit(1920, 1080, 30.0, 1, Mode::PreExport, b, 300);
    AUREA_CHECK(video.bytesPerSecond > 0);
    AUREA_CHECK(video.bytesPerSecond < frames.bytesPerSecond / 50);   // H.264 vs RGBA16F
    AUREA_CHECK(video.fits);
    // Menor qualidade, vídeo menor (a taxa automática acompanha os pixels).
    const Fit small = plan_fit(1920, 1080, 30.0, 4, Mode::PreExport, b, 300);
    AUREA_CHECK(small.bytesPerSecond < video.bytesPerSecond);
    AUREA_CHECK(small.fitsSeconds > video.fitsSeconds);
}

AUREA_TEST(PreRenderPlan, LedgerInvalidatesOnlyTheTouchedSegments) {
    EditLedger ledger;
    const auto snap = ledger.capture(0, 64);
    AUREA_CHECK_EQ(snap.segments.size(), usize{8});
    AUREA_CHECK(ledger.all_clean(snap));
    ledger.touch(20, 30);   // trechos [16,24) e [24,32)
    AUREA_CHECK_EQ(ledger.dirty_segments(snap), 2u);
    for (i64 f = 0; f < 16; ++f) AUREA_CHECK(ledger.frame_clean(snap, f));
    for (i64 f = 16; f < 32; ++f) AUREA_CHECK(!ledger.frame_clean(snap, f));
    for (i64 f = 32; f < 64; ++f) AUREA_CHECK(ledger.frame_clean(snap, f));
    AUREA_CHECK(!ledger.frame_clean(snap, 64));   // fora do retrato
    AUREA_CHECK(!ledger.frame_clean(snap, -1));
    auto dirty = ledger.dirty_ranges(snap);
    AUREA_CHECK_EQ(dirty.size(), usize{1});
    if (dirty.size() == 1) { AUREA_CHECK_EQ(dirty[0].first, 16); AUREA_CHECK_EQ(dirty[0].second, 32); }
    AUREA_CHECK(ledger.any_clean(snap));
    // Edição fora do trecho do retrato: nada muda nele.
    ledger.touch(200, 260);
    AUREA_CHECK_EQ(ledger.dirty_segments(snap), 2u);
    // Uma edição separada abre outro trecho sujo.
    ledger.touch(50, 51);
    dirty = ledger.dirty_ranges(snap);
    AUREA_CHECK_EQ(dirty.size(), usize{2});
    if (dirty.size() == 2) { AUREA_CHECK_EQ(dirty[1].first, 48); AUREA_CHECK_EQ(dirty[1].second, 56); }
    // Um retrato novo parte limpo.
    const auto fresh = ledger.capture(0, 64);
    AUREA_CHECK(ledger.all_clean(fresh));
    // Qualquer quadro pode mudar: nada vale mais.
    ledger.touch_all();
    AUREA_CHECK(!ledger.any_clean(snap));
    AUREA_CHECK(!ledger.any_clean(fresh));
    AUREA_CHECK_EQ(ledger.dirty_segments(fresh), 8u);
    // Vazios e invertidos não sujam nada.
    const auto after = ledger.capture(0, 64);
    ledger.touch(10, 10);
    ledger.touch(30, 5);
    AUREA_CHECK(ledger.all_clean(after));
    // Começo negativo vira 0.
    ledger.touch(-40, 3);
    AUREA_CHECK(!ledger.frame_clean(after, 0));
    AUREA_CHECK(ledger.frame_clean(after, 8));
}

AUREA_TEST(PreRenderPlan, LedgerSnapshotsNotAlignedToSegmentsClipDirtyRanges) {
    EditLedger ledger;
    const auto snap = ledger.capture(5, 21);   // trechos 0, 1 e 2
    AUREA_CHECK_EQ(snap.segments.size(), usize{3});
    ledger.touch(0, 1);                        // o trecho 0 inteiro
    AUREA_CHECK(!ledger.frame_clean(snap, 5));
    AUREA_CHECK(ledger.frame_clean(snap, 8));
    const auto dirty = ledger.dirty_ranges(snap);
    AUREA_CHECK_EQ(dirty.size(), usize{1});
    if (dirty.size() == 1) { AUREA_CHECK_EQ(dirty[0].first, 5); AUREA_CHECK_EQ(dirty[0].second, 8); }
    ledger.touch(20, 21);
    const auto tail = ledger.dirty_ranges(snap);
    AUREA_CHECK_EQ(tail.size(), usize{2});
    if (tail.size() == 2) { AUREA_CHECK_EQ(tail[1].first, 16); AUREA_CHECK_EQ(tail[1].second, 21); }
    // reset esquece tudo: retratos antigos não valem mais.
    ledger.reset();
    AUREA_CHECK(!ledger.any_clean(snap));
    AUREA_CHECK(ledger.capture(0, 0).empty());
}

AUREA_TEST(PreRenderPlan, EffectiveRangeIsChosenThenWorkAreaThenContent) {
    // Escolhido com Início/Fim.
    auto r = resolve_range(30, 90, true, 10, 20, 120, 300);
    AUREA_CHECK(r.explicitRange); AUREA_CHECK_EQ(r.in, 30); AUREA_CHECK_EQ(r.out, 90);
    // Sem escolha: a área de trabalho.
    r = resolve_range(-1, -1, true, 10, 20, 120, 300);
    AUREA_CHECK(!r.explicitRange); AUREA_CHECK_EQ(r.in, 10); AUREA_CHECK_EQ(r.out, 20);
    // Sem área: do começo ao fim do conteúdo.
    r = resolve_range(-1, -1, false, 0, 0, 120, 300);
    AUREA_CHECK_EQ(r.in, 0); AUREA_CHECK_EQ(r.out, 120);
    // Fim além da composição é preso nela.
    r = resolve_range(250, 900, false, 0, 0, 120, 300);
    AUREA_CHECK_EQ(r.in, 250); AUREA_CHECK_EQ(r.out, 300);
    // Início no cabeçote: o fim que ainda está depois fica.
    auto p = range_with_start(40, 10, 90, 120, 300);
    AUREA_CHECK_EQ(p.first, 40); AUREA_CHECK_EQ(p.second, 90);
    // ... senão vai ao fim do conteúdo.
    p = range_with_start(100, 10, 90, 120, 300);
    AUREA_CHECK_EQ(p.first, 100); AUREA_CHECK_EQ(p.second, 120);
    p = range_with_start(150, -1, -1, 120, 300);
    AUREA_CHECK_EQ(p.first, 150); AUREA_CHECK_EQ(p.second, 300);
    // Fim no cabeçote: o quadro do cabeçote entra; início depois dele volta a 0.
    p = range_with_end(59, 10, 90, 300);
    AUREA_CHECK_EQ(p.first, 10); AUREA_CHECK_EQ(p.second, 60);
    p = range_with_end(5, 10, 90, 300);
    AUREA_CHECK_EQ(p.first, 0); AUREA_CHECK_EQ(p.second, 6);
    p = range_with_end(999, -1, -1, 300);
    AUREA_CHECK_EQ(p.first, 0); AUREA_CHECK_EQ(p.second, 300);
}
