// =============================================================================
//  Batidas: grade de cliques sintéticos com tempo e fase conhecidos.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/audio/Beats.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace aurea;

namespace {

/// Bumbo sintético (seno 60 Hz decaindo + estalo de ruído) a cada período,
/// sobre um tom contínuo e ruído baixo (a batida não pode depender de silêncio).
std::vector<f32> kick_track(f64 bpm, f64 first, f64 seconds, u32 seed) {
    const usize n = static_cast<usize>(seconds * 48000.0);
    std::vector<f32> x(n, 0.0f);
    u32 s = seed;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return static_cast<f32>(s >> 8) / 16777216.0f * 2.0f - 1.0f; };
    for (usize i = 0; i < n; ++i) x[i] = 0.15f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<f32>(i) / 48000.0f) + 0.01f * rnd();
    const f64 period = 60.0 / bpm;
    for (f64 t = first; t < seconds; t += period) {
        const usize at = static_cast<usize>(t * 48000.0);
        for (usize k = 0; k < 9600 && at + k < n; ++k) {
            const f32 tt = static_cast<f32>(k) / 48000.0f;
            x[at + k] += 0.8f * std::exp(-tt * 18.0f) * std::sin(2.0f * 3.14159265f * 60.0f * tt)
                       + (k < 480 ? 0.5f * rnd() * (1.0f - static_cast<f32>(k) / 480.0f) : 0.0f);
        }
    }
    return x;
}

void check_grid(f64 bpm, f64 first) {
    const auto x = kick_track(bpm, first, 20.0, 7u);
    const audio::BeatResult r = audio::detect_beats(x.data(), x.size());
    const f64 period = 60.0 / bpm;
    f64 worst = 0.0, sum = 0.0;
    u32 matched = 0;
    for (f64 b : r.beats) {
        const f64 k = std::round((b - first) / period);
        const f64 err = b - (first + k * period);
        worst = std::max(worst, std::fabs(err));
        sum += err;
        ++matched;
    }
    const u32 expected = static_cast<u32>((20.0 - first) / period) + 1;
    std::printf("    %.0f BPM: detectado %.2f, %u batidas (esperadas %u), erro medio %+.1f ms, pior %.1f ms\n",
                bpm, r.bpm, matched, expected, matched ? 1000.0 * sum / matched : 0.0, 1000.0 * worst);
    AUREA_CHECK(std::fabs(r.bpm - bpm) < 1.0);
    AUREA_CHECK(matched + 2 >= expected && matched <= expected + 1);
    AUREA_CHECK(worst < 0.020);
}

} // namespace

AUREA_TEST(Beats, FindsTempoAndPhase120) { check_grid(120.0, 0.37); }
AUREA_TEST(Beats, FindsTempoAndPhase90) { check_grid(90.0, 0.81); }
AUREA_TEST(Beats, FindsTempoAndPhase150) { check_grid(150.0, 0.12); }

AUREA_TEST(Beats, SilenceGivesNothing) {
    std::vector<f32> x(48000 * 10, 0.0f);
    const audio::BeatResult r = audio::detect_beats(x.data(), x.size());
    AUREA_CHECK(r.beats.empty() || r.bpm == 0.0 || r.beats.size() < 3);
}

AUREA_TEST(Beats, ToneBurstsEverySecond) {
    // O arquivo de teste do aparelho: tom de 440 Hz a 0,15 e rajadas de 0,1 s
    // a 0,6 a cada 1 s (60 BPM), 6 s.
    const usize n = 48000 * 6;
    std::vector<f32> x(n);
    for (usize i = 0; i < n; ++i) {
        const f64 t = static_cast<f64>(i) / 48000.0;
        const f64 amp = std::fmod(t, 1.0) < 0.1 ? 0.6 : 0.15;
        x[i] = static_cast<f32>(amp * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
    }
    const audio::BeatResult r = audio::detect_beats(x.data(), x.size());
    std::printf("    %.2f BPM, %zu batidas:", r.bpm, r.beats.size());
    for (f64 b : r.beats) std::printf(" %.3f", b);
    std::printf("\n");
    AUREA_CHECK(std::fabs(r.bpm - 60.0) < 1.5);
    // A rajada em t=0 não tem "antes" para comparar: 5 ou 6 batidas.
    AUREA_CHECK(r.beats.size() >= 5 && r.beats.size() <= 6);
    // O fim abrupto de cada rajada também é um transiente (espalha energia no
    // espectro); só a 1ª rajada, sem "antes", deixa o fim dela como ataque.
    for (f64 b : r.beats) if (b > 0.5) AUREA_CHECK(std::fabs(b - std::round(b)) < 0.02);
}
