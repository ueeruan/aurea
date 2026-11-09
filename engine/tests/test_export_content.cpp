// Conferência do conteúdo do arquivo final e regra de continuar um export.
#include "TestFramework.hpp"

#include "aurea/export/ExportCheckpoint.hpp"
#include "aurea/export/ExportContentCheck.hpp"

#include <vector>

using namespace aurea;
using namespace aurea::export_content;

namespace {
constexpr u32 kW = 320, kH = 180;

struct Nv12 {
    std::vector<u8> y = std::vector<u8>(kW * kH), uv = std::vector<u8>(kW * kH / 2);
    Nv12() {
        for (u32 r = 0; r < kH; ++r)
            for (u32 x = 0; x < kW; ++x) y[r * kW + x] = static_cast<u8>(40 + (x * 150) / kW + (r * 40) / kH);
        for (u32 r = 0; r < kH / 2; ++r)
            for (u32 x = 0; x < kW; x += 2) { uv[r * kW + x] = 110; uv[r * kW + x + 1] = 160; }
    }
    Fingerprint print() const {
        Planes p;
        p.y = y.data(); p.cb = uv.data(); p.cr = uv.data() + 1;
        p.yStride = p.cbStride = p.crStride = kW;
        p.chromaStep = 2;
        Fingerprint f;
        compute(p, kW, kH, f);
        return f;
    }
};
} // namespace

AUREA_TEST(ExportContent, SamePictureAndCompressionNoisePass) {
    const Nv12 source;
    Nv12 noisy;
    for (usize i = 0; i < noisy.y.size(); ++i) noisy.y[i] = static_cast<u8>(noisy.y[i] + (i % 2 ? 9 : -9));
    for (usize i = 0; i < noisy.uv.size(); ++i) noisy.uv[i] = static_cast<u8>(noisy.uv[i] + (i % 3 ? 5 : -5));
    AUREA_CHECK(!damaged(compare(source.print(), source.print())));
    AUREA_CHECK(!damaged(compare(source.print(), noisy.print())));
}

AUREA_TEST(ExportContent, ZeroedPlanesAreGreenAndFail) {
    const Nv12 source;
    Nv12 green;
    std::fill(green.y.begin(), green.y.end(), u8(0));
    std::fill(green.uv.begin(), green.uv.end(), u8(0));
    AUREA_CHECK(damaged(compare(source.print(), green.print())));
}

AUREA_TEST(ExportContent, BlackFrameFails) {
    const Nv12 source;
    Nv12 black;
    std::fill(black.y.begin(), black.y.end(), u8(16));
    std::fill(black.uv.begin(), black.uv.end(), u8(128));
    AUREA_CHECK(damaged(compare(source.print(), black.print())));
}

AUREA_TEST(ExportContent, SwappedChromaFails) {
    const Nv12 source;
    Nv12 swapped = source;
    for (usize i = 0; i + 1 < swapped.uv.size(); i += 2) std::swap(swapped.uv[i], swapped.uv[i + 1]);
    AUREA_CHECK(damaged(compare(source.print(), swapped.print())));
}

AUREA_TEST(ExportContent, PlanarDecodeMatchesNv12Source) {
    const Nv12 source;
    std::vector<u8> cb(kW * kH / 4), cr(kW * kH / 4);
    for (u32 r = 0; r < kH / 2; ++r)
        for (u32 x = 0; x < kW / 2; ++x) {
            cb[r * (kW / 2) + x] = source.uv[r * kW + x * 2];
            cr[r * (kW / 2) + x] = source.uv[r * kW + x * 2 + 1];
        }
    Planes p;
    p.y = source.y.data(); p.cb = cb.data(); p.cr = cr.data();
    p.yStride = kW; p.cbStride = p.crStride = kW / 2;
    Fingerprint planar;
    compute(p, kW, kH, planar);
    const Difference d = compare(source.print(), planar);
    AUREA_CHECK_NEAR(d.meanY, 0.0, 1e-9);
    AUREA_CHECK_NEAR(d.meanC, 0.0, 1e-9);
}

AUREA_TEST(ExportContent, AudioLostOnlyWhenSourceHadSound) {
    AUREA_CHECK(audio_lost(2000.0, 0.0));
    AUREA_CHECK(audio_lost(2000.0, 100.0));
    AUREA_CHECK(!audio_lost(2000.0, 1500.0));
    AUREA_CHECK(!audio_lost(0.0, 0.0));     // projeto mudo continua valendo
    AUREA_CHECK(!audio_lost(4.0, 0.0));
}

AUREA_TEST(ExportContent, CrashAtTheSameGopCannotResumeForever) {
    using namespace export_checkpoint;
    Manifest m;
    m.state = SessionState::Running;
    m.lastResumeFrame = 300;
    m.failureResumes = 0;
    AUREA_CHECK(!resume_allowed(m, 300));   // nenhum GOP novo desde a última vez
    AUREA_CHECK(resume_allowed(m, 360));
    m.failureResumes = kMaxFailureResumes;  // morreu de novo: passa do limite
    AUREA_CHECK(!resume_allowed(m, 360));
    m.state = SessionState::Paused;         // pausa do sistema não é falha
    AUREA_CHECK(resume_allowed(m, 300));
}
