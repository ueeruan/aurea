// =============================================================================
//  Headbop: grade de batidas (áudio sintético, toques, BPM manual) e o motor de
//  movimento (curva do preset Bop contra as medidas da referência, presets,
//  subdivisão, fase, determinismo, robustez e desempenho).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/audio/Beats.hpp"
#include "aurea/effects/HeadbopMotion.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace aurea;
namespace hb = aurea::headbop;

namespace {

/// Bumbo sintético (seno 60 Hz decaindo + estalo) sobre tom contínuo e ruído
/// baixo. `accent(k)` escala a batida k (para testar a força do ataque).
template <typename Accent>
std::vector<f32> kick_track(f64 bpm, f64 first, f64 seconds, u32 seed, Accent accent) {
    const usize n = static_cast<usize>(seconds * 48000.0);
    std::vector<f32> x(n, 0.0f);
    u32 s = seed;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return static_cast<f32>(s >> 8) / 16777216.0f * 2.0f - 1.0f; };
    for (usize i = 0; i < n; ++i) x[i] = 0.15f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<f32>(i) / 48000.0f) + 0.01f * rnd();
    const f64 period = 60.0 / bpm;
    int k = 0;
    for (f64 t = first; t < seconds; t += period, ++k) {
        const f32 g = accent(k);
        const usize at = static_cast<usize>(t * 48000.0);
        for (usize j = 0; j < 9600 && at + j < n; ++j) {
            const f32 tt = static_cast<f32>(j) / 48000.0f;
            x[at + j] += g * (0.8f * std::exp(-tt * 18.0f) * std::sin(2.0f * 3.14159265f * 60.0f * tt)
                       + (j < 480 ? 0.5f * rnd() * (1.0f - static_cast<f32>(j) / 480.0f) : 0.0f));
        }
    }
    return x;
}
std::vector<f32> kick_track(f64 bpm, f64 first, f64 seconds, u32 seed) {
    return kick_track(bpm, first, seconds, seed, [](int) { return 1.0f; });
}

/// Cliques curtos (1 ms de ruído) — o caso "metrônomo".
std::vector<f32> click_track(f64 bpm, f64 first, f64 seconds) {
    const usize n = static_cast<usize>(seconds * 48000.0);
    std::vector<f32> x(n, 0.0f);
    u32 s = 99u;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return static_cast<f32>(s >> 8) / 16777216.0f * 2.0f - 1.0f; };
    for (usize i = 0; i < n; ++i) x[i] = 0.003f * rnd();
    for (f64 t = first; t < seconds; t += 60.0 / bpm) {
        const usize at = static_cast<usize>(t * 48000.0);
        for (usize j = 0; j < 48 && at + j < n; ++j) x[at + j] += 0.9f * rnd();
    }
    return x;
}

void check_analysis(const char* what, const std::vector<f32>& x, f64 bpm, f64 first, f64 seconds) {
    const hb::BeatGrid g = hb::analyze_audio(x.data(), x.size());
    const f64 period = 60.0 / bpm;
    f64 worst = 0.0;
    for (f64 b : g.beats) {
        const f64 k = std::round((b - first) / period);
        worst = std::max(worst, std::fabs(b - (first + k * period)));
    }
    // Fase da grade: deve cair numa batida real (módulo o período).
    f64 phErr = std::fmod(std::fabs(g.phaseSec - first), period);
    phErr = std::min(phErr, period - phErr);
    const u32 expected = static_cast<u32>((seconds - first) / period) + 1;
    std::printf("    %s %.1f BPM: detectado %.2f, %zu batidas (esperadas %u), pior %.1f ms, fase %.1f ms\n",
                what, bpm, g.bpm, g.beats.size(), expected, 1000.0 * worst, 1000.0 * phErr);
    AUREA_CHECK(std::fabs(g.bpm - bpm) <= 1.0);
    AUREA_CHECK(g.beats.size() + 2 >= expected && g.beats.size() <= expected + 1);
    AUREA_CHECK(worst <= 0.020);
    AUREA_CHECK(phErr <= 0.020);
    AUREA_CHECK_EQ(g.strength.size(), g.beats.size());
}

hb::Params bop() {
    hb::Params p;
    p.variation = 0;   // amplitude uniforme para medir a forma
    return p;
}

constexpr f64 kRefBpm = 117.5;
constexpr f64 kRefP = 60.0 / kRefBpm;

/// Instante do máximo de `get(pose)` em [t0, t1), passo fino.
template <typename Get>
f64 argmax(const hb::Params& p, const hb::BeatGrid& g, f64 t0, f64 t1, Get get, f64* value = nullptr) {
    f64 bestT = t0, best = -1e30;
    for (f64 t = t0; t < t1; t += 0.0005) {
        const f64 v = get(hb::evaluate(p, g, t));
        if (v > best) { best = v; bestT = t; }
    }
    if (value) *value = best;
    return bestT;
}

bool same(const hb::Pose& a, const hb::Pose& b) { return std::memcmp(&a, &b, sizeof(hb::Pose)) == 0; }

bool finite_pose(const hb::Pose& p) {
    const float v[] = {p.frameDy, p.frameZoom, p.torsoDx, p.torsoDy, p.torsoRoll, p.headDx,
                       p.headDy, p.headPitch, p.headRoll, p.headYaw, p.extremityLag, p.frameDx};
    for (float f : v) if (!std::isfinite(f)) return false;
    return true;
}

f64 max_abs(const hb::Params& p, const hb::BeatGrid& g, f64 t0, f64 t1, float hb::Pose::*m) {
    f64 r = 0.0;
    for (f64 t = t0; t < t1; t += 0.004) r = std::max(r, static_cast<f64>(std::fabs(hb::evaluate(p, g, t).*m)));
    return r;
}

} // namespace

// =============================================================================
//  Grade de batidas
// =============================================================================

AUREA_TEST(HeadbopBeats, KicksAtKnownTempos) {
    check_analysis("bumbo", kick_track(90.0, 0.81, 20.0, 7u), 90.0, 0.81, 20.0);
    check_analysis("bumbo", kick_track(117.5, 0.25, 20.0, 3u), 117.5, 0.25, 20.0);
    check_analysis("bumbo", kick_track(128.0, 0.40, 20.0, 5u), 128.0, 0.40, 20.0);
    check_analysis("bumbo", kick_track(140.0, 0.10, 20.0, 9u), 140.0, 0.10, 20.0);
}

AUREA_TEST(HeadbopBeats, ClicksAtKnownTempos) {
    check_analysis("clique", click_track(117.5, 0.33, 20.0), 117.5, 0.33, 20.0);
    check_analysis("clique", click_track(128.0, 0.05, 20.0), 128.0, 0.05, 20.0);
}

AUREA_TEST(HeadbopBeats, OldApiUnchanged) {
    const auto x = kick_track(120.0, 0.37, 20.0, 7u);
    const audio::BeatResult r = audio::detect_beats(x.data(), x.size());
    const audio::BeatAnalysis a = audio::detect_beats_ex(x.data(), x.size());
    AUREA_CHECK(r.bpm == a.bpm);
    AUREA_CHECK(r.beats == a.beats);
}

AUREA_TEST(HeadbopBeats, StrengthFollowsAccents) {
    // Acento forte a cada 4 batidas: força maior nas acentuadas.
    const f64 bpm = 120.0, first = 0.5;
    const auto x = kick_track(bpm, first, 20.0, 11u, [](int k) { return k % 4 == 0 ? 1.0f : 0.35f; });
    const hb::BeatGrid g = hb::analyze_audio(x.data(), x.size());
    AUREA_CHECK_EQ(g.strength.size(), g.beats.size());
    f64 loud = 0, soft = 0;
    int nl = 0, ns = 0;
    for (usize i = 0; i < g.beats.size(); ++i) {
        const long k = std::lround((g.beats[i] - first) / 0.5);
        if (k % 4 == 0) { loud += g.strength[i]; ++nl; } else { soft += g.strength[i]; ++ns; }
    }
    loud /= std::max(nl, 1);
    soft /= std::max(ns, 1);
    std::printf("    forca media: acentuadas %.2f, fracas %.2f\n", loud, soft);
    AUREA_CHECK(nl > 5 && ns > 15);
    AUREA_CHECK(loud > soft * 1.5);
}

AUREA_TEST(HeadbopBeats, SilenceAndShortAudio) {
    std::vector<f32> x(48000 * 10, 0.0f);
    const hb::BeatGrid g = hb::analyze_audio(x.data(), x.size());
    AUREA_CHECK(g.beats.size() < 3);
    const hb::BeatGrid e = hb::analyze_audio(nullptr, 0);
    AUREA_CHECK(e.beats.empty() && e.bpm == 0.0);
    const hb::Pose p = hb::evaluate(hb::Params{}, e, 1.0);
    AUREA_CHECK(finite_pose(p));
}

AUREA_TEST(HeadbopBeats, TapsGiveTempoAndPhase) {
    // 120 BPM com jitter de ±12 ms, um toque esquecido e um toque duplo.
    std::mt19937 rng(42);
    std::uniform_real_distribution<f64> jit(-0.012, 0.012);
    std::vector<f64> taps;
    for (int k = 0; k < 12; ++k) {
        if (k == 5) continue;
        taps.push_back(1.3 + k * 0.5 + jit(rng));
    }
    taps.push_back(taps[2] + 0.01);
    std::shuffle(taps.begin(), taps.end(), rng);
    const hb::BeatGrid g = hb::grid_from_taps(taps, 30.0);
    f64 phErr = std::fmod(std::fabs(g.phaseSec - 1.3), 0.5);
    phErr = std::min(phErr, 0.5 - phErr);
    std::printf("    toques: %.2f BPM, fase %.3f s (erro %.1f ms), %zu batidas\n", g.bpm, g.phaseSec, 1000 * phErr, g.beats.size());
    AUREA_CHECK(std::fabs(g.bpm - 120.0) <= 1.0);
    AUREA_CHECK(phErr <= 0.020);
    AUREA_CHECK(g.beats.size() >= 59 && g.beats.size() <= 61);
    AUREA_CHECK(hb::grid_from_taps({}, 10.0).bpm == 0.0);
    AUREA_CHECK(hb::grid_from_taps({1.0}, 10.0).bpm == 0.0);
    AUREA_CHECK(hb::grid_from_taps({1.0, std::numeric_limits<f64>::quiet_NaN()}, 10.0).bpm == 0.0);
}

AUREA_TEST(HeadbopBeats, ManualBpmGrid) {
    const hb::BeatGrid g = hb::grid_from_bpm(117.5, 0.2, 10.0);
    AUREA_CHECK(std::fabs(g.beats.front() - 0.2) < 1e-9);
    AUREA_CHECK(std::fabs(g.beats[1] - g.beats[0] - kRefP) < 1e-9);
    AUREA_CHECK(g.beats.back() <= 10.0 + 1e-9 && g.beats.back() + kRefP > 10.0);
    AUREA_CHECK(g.strength.size() == g.beats.size());
    AUREA_CHECK(hb::grid_from_bpm(0.0, 0.0, 10.0).beats.empty());
    AUREA_CHECK(hb::grid_from_bpm(std::numeric_limits<f64>::infinity(), 0.0, 10.0).beats.empty());
    // manualBpm sobrepõe a grade: mesmo resultado que uma grade implícita.
    hb::Params p = bop();
    p.manualBpm = 117.5;
    hb::BeatGrid implicitGrid;
    implicitGrid.bpm = 117.5;
    const hb::BeatGrid other = hb::grid_from_bpm(90.0, 0.0, 30.0);
    hb::Params q = bop();
    for (f64 t = 2.0; t < 6.0; t += 0.0371) {
        const hb::Pose a = hb::evaluate(p, other, t), b = hb::evaluate(q, implicitGrid, t);
        AUREA_CHECK(std::fabs(a.frameDy - b.frameDy) < 1e-6f && std::fabs(a.headPitch - b.headPitch) < 1e-4f);
    }
}

AUREA_TEST(HeadbopBeats, CacheAndBlob) {
    auto& cache = hb::AnalysisCache::shared();
    cache.clear();
    const auto x = kick_track(128.0, 0.4, 12.0, 5u);
    const hb::BeatGrid a = cache.get_or_analyze(x.data(), x.size());
    AUREA_CHECK_EQ(cache.size(), static_cast<usize>(1));
    const auto t0 = std::chrono::steady_clock::now();
    const hb::BeatGrid b = cache.get_or_analyze(x.data(), x.size());
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    cache: segunda consulta %.2f ms (so o hash)\n", ms);
    AUREA_CHECK(a.beats == b.beats && a.strength == b.strength && a.bpm == b.bpm);
    AUREA_CHECK_EQ(cache.size(), static_cast<usize>(1));
    const u64 h = hb::content_hash(x.data(), x.size());
    auto y = x;
    y[1000] += 1e-3f;
    AUREA_CHECK(hb::content_hash(y.data(), y.size()) != h);

    const std::string blob = hb::serialize(a, h);
    hb::BeatGrid r;
    u64 rh = 0;
    AUREA_CHECK(hb::deserialize(blob, r, &rh));
    AUREA_CHECK(rh == h && r.beats == a.beats && r.strength == a.strength && r.bpm == a.bpm && r.phaseSec == a.phaseSec);
    hb::BeatGrid untouched;
    untouched.bpm = 7.0;
    AUREA_CHECK(!hb::deserialize(blob.substr(0, blob.size() - 1), untouched));
    AUREA_CHECK(!hb::deserialize(std::string("XXXX") + blob.substr(4), untouched));
    AUREA_CHECK(!hb::deserialize(std::string(), untouched));
    AUREA_CHECK(untouched.bpm == 7.0);
    cache.clear();
}

// =============================================================================
//  Motor de movimento
// =============================================================================

AUREA_TEST(HeadbopMotion, BopMatchesReference) {
    // Vídeo de referência: 117.9 BPM, 30 fps (medido por casamento de modelo).
    constexpr f64 bpm = 117.9, P = 60.0 / bpm;
    const hb::BeatGrid g = hb::grid_from_bpm(bpm, 0.0, 60.0);
    const hb::Params p = bop();
    const f64 b0 = 20.0 * P;   // regime estabelecido

    // Quadro: diagonal, desce 5.5% e vai 2.4% à direita em ~4 quadros, volta em ~10.
    f64 lo = 1e9, hi = -1e9, tLo = 0, tHi = 0, dxMax = -1e9, tDx = 0;
    for (f64 t = b0 - 0.05 * P; t < b0 + 0.95 * P; t += 0.0002) {
        const hb::Pose q = hb::evaluate(p, g, t);
        if (q.frameDy < lo) { lo = q.frameDy; tLo = t; }
        if (q.frameDy > hi) { hi = q.frameDy; tHi = t; }
        if (q.frameDx > dxMax) { dxMax = q.frameDx; tDx = t; }
    }
    const f64 drop = hi - lo, down = (tHi - tLo) / P;
    const hb::Pose bottom = hb::evaluate(p, g, tHi);
    const f64 ratio = bottom.frameDx / bottom.frameDy;
    // Cabeça: inclinação (roll) num sentido só, 2–3 quadros depois do fundo do quadro.
    f64 rollMax = 0;
    const f64 tR = argmax(p, g, b0, b0 + P, [](const hb::Pose& q) { return q.headRoll; }, &rollMax);
    const f64 rollLagFrames = (tR - tHi) * 30.0;
    f64 rollMin = 1e9, rollMinAll = 1e9;
    for (f64 t = b0; t < b0 + P; t += 0.001) rollMin = std::min(rollMin, (f64)hb::evaluate(p, g, t).headRoll);
    for (f64 t = 0.0; t < 40.0; t += 0.003) rollMinAll = std::min(rollMinAll, (f64)hb::evaluate(p, g, t).headRoll);
    f64 pitchMax = 0, headDrop = 0, torsoMax = 0;
    argmax(p, g, b0, b0 + P, [](const hb::Pose& q) { return q.headPitch; }, &pitchMax);
    argmax(p, g, b0, b0 + P, [](const hb::Pose& q) { return q.torsoDy + q.headDy; }, &headDrop);
    const f64 tT = argmax(p, g, b0, b0 + P, [](const hb::Pose& q) { return q.torsoDy; }, &torsoMax);
    // Extremidades: ~12% da batida depois do corpo.
    const f64 tE = argmax(p, g, b0, b0 + P, [](const hb::Pose& q) { return q.extremityLag; });
    const f64 extLag = (tE - tT) / P;
    // Tronco: balanço ±2.3% relativo ao quadro, período ~1.6 batida, roll ±1°.
    const f64 swayMax = max_abs(p, g, b0, b0 + 10 * 1.6 * P, &hb::Pose::torsoDx);
    const f64 torsoRollMax = max_abs(p, g, b0, b0 + 10 * 1.6 * P, &hb::Pose::torsoRoll);
    std::vector<f64> zc;
    f64 prev = hb::evaluate(p, g, b0).torsoDx;
    for (f64 t = b0 + 0.001; t < b0 + 20 * P; t += 0.001) {
        const f64 v = hb::evaluate(p, g, t).torsoDx;
        if ((prev < 0) != (v < 0)) zc.push_back(t);
        prev = v;
    }
    const f64 swayPeriod = zc.size() >= 3 ? 2.0 * (zc.back() - zc.front()) / static_cast<f64>(zc.size() - 1) / P : 0.0;
    // Cabeça contra o tronco no lateral.
    f64 sxy = 0, sxx = 0, syy = 0, worldMax = 0;
    for (f64 t = b0; t < b0 + 10 * P; t += 0.004) {
        const hb::Pose q = hb::evaluate(p, g, t);
        sxy += q.torsoDx * q.headDx; sxx += q.torsoDx * q.torsoDx; syy += q.headDx * q.headDx;
        worldMax = std::max(worldMax, static_cast<f64>(std::fabs(q.torsoDx + q.headDx)));
    }
    const f64 corr = sxy / std::sqrt(sxx * syy);

    std::printf("    quadro: desce %.2f%% (alvo 5.5) e %.2f%% a direita (alvo 2.4), dx/dy %.3f (alvo 0.44), desce em %.3f batida = %.1f quadros (alvo 0.27 / ~4), pico dx-dy %.1f ms\n",
                100 * drop, 100 * dxMax, ratio, down, down * P * 30.0, 1000.0 * (tDx - tHi));
    std::printf("    cabeca: roll %.1f graus (alvo 12-16), %.2f quadros depois do fundo do quadro (alvo 2-3), entre batidas %.2f graus (alvo 0-2), minimo geral %.2f\n",
                rollMax, rollLagFrames, rollMin, rollMinAll);
    std::printf("    cabeca: aceno %.1f graus (alvo 4-6), queda extra vs quadro %.2f%% (alvo 3-6), tronco %.2f%% (menor)\n",
                pitchMax, 100 * headDrop, 100 * torsoMax);
    std::printf("    tronco: balanco %.2f%% (alvo 2.3), periodo %.2f batidas (alvo ~1.6), roll %.2f graus (alvo 1); extremidades %.3f batida (alvo 0.12); corr cabeca x tronco %.3f\n",
                100 * swayMax, swayPeriod, torsoRollMax, extLag, corr);

    AUREA_CHECK(std::fabs(drop - 0.055) <= 0.005);
    AUREA_CHECK(std::fabs(dxMax - 0.024) <= 0.003);
    AUREA_CHECK(std::fabs(ratio - 0.44) <= 0.03);
    AUREA_CHECK(std::fabs(tDx - tHi) < 0.002);
    AUREA_CHECK(std::fabs(down - 0.27) <= 0.05);
    AUREA_CHECK(std::fabs(tLo - b0) / P < 0.05 || std::fabs(tLo - b0 - P) / P < 0.05);  // no topo na batida
    AUREA_CHECK(rollMax >= 12.0 && rollMax <= 16.0);
    AUREA_CHECK(rollLagFrames >= 1.5 && rollLagFrames <= 3.5);
    AUREA_CHECK(rollMin >= 0.0 && rollMin <= 2.5);
    AUREA_CHECK(rollMinAll >= -0.5);
    AUREA_CHECK(pitchMax >= 4.0 && pitchMax <= 6.0);
    AUREA_CHECK(headDrop >= 0.03 && headDrop <= 0.06);
    AUREA_CHECK(torsoMax < headDrop);
    AUREA_CHECK(std::fabs(swayMax - 0.023) <= 0.003);
    AUREA_CHECK(std::fabs(swayPeriod - 1.6) <= 0.1);
    AUREA_CHECK(std::fabs(torsoRollMax - 1.0) <= 0.15);
    AUREA_CHECK(std::fabs(extLag - 0.12) <= 0.03);
    AUREA_CHECK(corr < -0.9);
    AUREA_CHECK(worldMax < 0.35 * swayMax);
    // Sentido configurável: tiltSign −1 espelha a inclinação; joltAngle muda a diagonal.
    hb::Params m = p;
    m.tiltSign = -1;
    AUREA_CHECK(std::fabs(hb::evaluate(m, g, tR).headRoll + rollMax) < 1e-3);
    m = p;
    m.joltAngle = -23.6f;   // de volta à vertical
    AUREA_CHECK(std::fabs(hb::evaluate(m, g, tHi).frameDx) < 1e-6f);
}

AUREA_TEST(HeadbopMotion, PerBeatStrengthVaries) {
    // Força do ataque: referência 15°, 8°, 13°, 13° por batida.
    hb::BeatGrid g = hb::grid_from_bpm(117.9, 0.0, 30.0);
    const float pattern[] = {1.15f, 0.33f, 0.86f, 0.86f};
    for (usize i = 0; i < g.strength.size(); ++i) g.strength[i] = pattern[i % 4];
    hb::Params p = bop();
    const f64 beatP = 60.0 / 117.9;
    f64 peaks[4];
    for (int k = 0; k < 4; ++k) {
        const f64 b = (20 + k) * beatP;
        argmax(p, g, b, b + beatP, [](const hb::Pose& q) { return q.headRoll; }, &peaks[k]);
    }
    std::printf("    inclinacao por batida (forca do ataque): %.1f %.1f %.1f %.1f graus (referencia 15 8 13 13)\n", peaks[0], peaks[1], peaks[2], peaks[3]);
    const f64 ref[] = {15.0, 8.0, 13.0, 13.0};
    for (int k = 0; k < 4; ++k) AUREA_CHECK(std::fabs(peaks[k] - ref[k]) <= 2.0);

    // Ruído de semente fixa: variação 0 → batidas iguais; variação 1 → espalha.
    const hb::BeatGrid flat = hb::grid_from_bpm(117.9, 0.0, 30.0);
    p.variation = 1;
    f64 mn = 1e9, mx = -1e9;
    for (int k = 0; k < 16; ++k) {
        f64 v = 0;
        const f64 b = (20 + k) * beatP;
        argmax(p, flat, b, b + beatP, [](const hb::Pose& q) { return q.headRoll; }, &v);
        mn = std::min(mn, v); mx = std::max(mx, v);
    }
    std::printf("    variacao 1: inclinacao entre %.1f e %.1f graus\n", mn, mx);
    AUREA_CHECK(mx - mn > 3.0);
    AUREA_CHECK(mn > 5.0 && mx < 24.0);
    // Outra semente → outra sequência, mesma semente → a mesma.
    hb::Params p2 = p;
    p2.seed = 77;
    AUREA_CHECK(!same(hb::evaluate(p, flat, 11.11), hb::evaluate(p2, flat, 11.11)));
    AUREA_CHECK(same(hb::evaluate(p, flat, 11.11), hb::evaluate(p, flat, 11.11)));
}

AUREA_TEST(HeadbopMotion, DeterministicAnyOrder) {
    const hb::BeatGrid g = hb::grid_from_bpm(kRefBpm, 0.13, 40.0);
    hb::BeatGrid detected = g;
    for (usize i = 0; i < detected.beats.size(); ++i) {
        detected.beats[i] += 0.004 * std::sin(1.7 * static_cast<f64>(i));
        detected.strength[i] = 0.6f + 0.4f * static_cast<f32>(std::fabs(std::sin(0.9 * static_cast<f64>(i))));
    }
    hb::Params a;           // Bop com variação
    hb::Params b = a;
    b.preset = hb::Preset::Groove;
    b.smoothness = 1.4f; b.delay = 0.6f; b.subdivision = 0.5f; b.turn3d = 0.5f; b.beatZoom = 0.03f;
    std::vector<f64> ts;
    for (int f = 0; f < 900; ++f) ts.push_back(f / 30.0);
    std::vector<hb::Pose> seqA, seqB;
    for (f64 t : ts) { seqA.push_back(hb::evaluate(a, detected, t)); seqB.push_back(hb::evaluate(b, detected, t)); }
    std::vector<usize> order(ts.size());
    for (usize i = 0; i < order.size(); ++i) order[i] = i;
    std::mt19937 rng(7);
    std::shuffle(order.begin(), order.end(), rng);
    bool ok = true;
    for (usize i : order) {
        // Intercala parâmetros diferentes (exercita o memo das cadeias).
        ok = ok && same(hb::evaluate(b, detected, ts[i]), seqB[i]);
        ok = ok && same(hb::evaluate(a, detected, ts[i]), seqA[i]);
    }
    AUREA_CHECK(ok);
}

AUREA_TEST(HeadbopMotion, PresetChannelIsolation) {
    const hb::BeatGrid g = hb::grid_from_bpm(120.0, 0.0, 30.0);
    auto run = [&](hb::Preset pr) {
        hb::Params p;
        p.preset = pr;
        p.turn3d = 0;
        struct M { f64 frameDy, torsoDy, torsoDx, headPitch, headDx, headYaw, headRoll, ext, frameDx; } m{};
        for (f64 t = 5.0; t < 12.0; t += 0.005) {
            const hb::Pose q = hb::evaluate(p, g, t);
            m.frameDy = std::max(m.frameDy, (f64)std::fabs(q.frameDy));
            m.torsoDy = std::max(m.torsoDy, (f64)std::fabs(q.torsoDy));
            m.torsoDx = std::max(m.torsoDx, (f64)std::fabs(q.torsoDx));
            m.headPitch = std::max(m.headPitch, (f64)std::fabs(q.headPitch));
            m.headDx = std::max(m.headDx, (f64)std::fabs(q.headDx));
            m.headYaw = std::max(m.headYaw, (f64)std::fabs(q.headYaw));
            m.headRoll = std::max(m.headRoll, (f64)std::fabs(q.headRoll));
            m.ext = std::max(m.ext, (f64)std::fabs(q.extremityLag));
            m.frameDx = std::max(m.frameDx, (f64)std::fabs(q.frameDx));
        }
        return m;
    };
    const auto nod = run(hb::Preset::Nod), sway = run(hb::Preset::Sway), bounce = run(hb::Preset::Bounce),
               groove = run(hb::Preset::Groove), bopM = run(hb::Preset::Bop);
    // Nod: só a cabeça.
    AUREA_CHECK(nod.frameDy == 0 && nod.frameDx == 0 && nod.torsoDy == 0 && nod.torsoDx == 0 && nod.ext == 0 && nod.headDx == 0);
    AUREA_CHECK(nod.headRoll > 10.0 && nod.headPitch > 4.0);
    // Sway: só lateral.
    AUREA_CHECK(sway.frameDy == 0 && sway.frameDx == 0 && sway.torsoDy == 0 && sway.headPitch == 0);
    AUREA_CHECK(sway.torsoDx > 0.04 && sway.headDx > 0.03 && sway.headRoll > 3.0);
    // Bounce: só o solavanco.
    AUREA_CHECK(bounce.torsoDx == 0 && bounce.headPitch == 0 && bounce.headDx == 0 && bounce.headRoll == 0);
    AUREA_CHECK(bounce.frameDy > 0.05 && bounce.frameDx > 0.02 && bounce.torsoDy > 0.02 && bounce.ext > 0.04);
    // Groove: giro, e ciclo de 2 batidas (aceno alternando forte/fraco).
    AUREA_CHECK(groove.headYaw > 8.0 && bopM.headYaw == 0);
    hb::Params gp;
    gp.preset = hb::Preset::Groove;
    gp.variation = 0;
    f64 r0 = 0, r1 = 0;
    argmax(gp, g, 10.0, 10.5, [](const hb::Pose& q) { return q.headRoll; }, &r0);
    argmax(gp, g, 10.5, 11.0, [](const hb::Pose& q) { return -q.headRoll; }, &r1);
    AUREA_CHECK(r0 > 3.0 && r1 > 1.5);   // alterna o sentido no ciclo de 2
    // Escalas: jolt só no quadro, head só na cabeça, body só no tronco.
    hb::Params p;
    p.jolt = 0;
    hb::Pose q = hb::evaluate(p, g, 10.17), ref = hb::evaluate(hb::Params{}, g, 10.17);
    AUREA_CHECK(q.frameDy == 0 && q.frameDx == 0 && q.torsoDy == ref.torsoDy && q.headPitch == ref.headPitch);
    p = {}; p.head = 0;
    q = hb::evaluate(p, g, 10.17);
    AUREA_CHECK(q.headPitch == 0 && q.headRoll == 0 && q.headDx == 0 && q.frameDy == ref.frameDy && q.torsoDx == ref.torsoDx);
    p = {}; p.body = 0;
    q = hb::evaluate(p, g, 10.17);
    AUREA_CHECK(q.torsoDy == 0 && q.torsoDx == 0 && q.extremityLag == 0 && q.frameDy == ref.frameDy && q.headPitch == ref.headPitch);
    p = {}; p.intensity = 0.5f;
    q = hb::evaluate(p, g, 10.17);
    AUREA_CHECK(std::fabs(q.frameDy - 0.5f * ref.frameDy) < 1e-6f);
    // Turn 3D: acrescenta giro.
    p = {}; p.turn3d = 1;
    AUREA_CHECK(max_abs(p, g, 5.0, 10.0, &hb::Pose::headYaw) > 10.0);
    // Beat zoom: pulso logo depois da batida.
    p = {}; p.beatZoom = 0.04f; p.variation = 0;
    f64 z = 0;
    const f64 tz = argmax(p, g, 10.0, 10.5, [](const hb::Pose& r) { return r.frameZoom; }, &z);
    AUREA_CHECK(std::fabs(z - 0.04) < 0.004 && tz - 10.0 < 0.1);
    AUREA_CHECK(hb::evaluate(hb::Params{}, g, 10.03).frameZoom == 0.0f);
}

AUREA_TEST(HeadbopMotion, SubdivisionAndPhase) {
    const hb::BeatGrid g = hb::grid_from_bpm(120.0, 0.0, 40.0);
    auto peaks = [&](float sub) {
        hb::Params p = bop();
        p.subdivision = sub;
        int n = 0;
        f64 a = hb::evaluate(p, g, 10.0).frameDy, b = hb::evaluate(p, g, 10.002).frameDy;
        for (f64 t = 10.004; t < 14.0; t += 0.002) {   // 8 batidas
            const f64 c = hb::evaluate(p, g, t).frameDy;
            if (b > a && b >= c && b > 0.02) ++n;
            a = b; b = c;
        }
        return n;
    };
    const int p05 = peaks(0.5f), p1 = peaks(1.0f), p2 = peaks(2.0f);
    std::printf("    acenos em 8 batidas: subdiv 0.5 → %d, 1 → %d, 2 → %d\n", p05, p1, p2);
    AUREA_CHECK_EQ(p1, 8);
    AUREA_CHECK_EQ(p2, 4);
    AUREA_CHECK_EQ(p05, 16);
    // Fase: deslocar phaseBeats = deslocar o tempo (grade implícita).
    hb::BeatGrid ig;
    ig.bpm = 120.0;
    hb::Params p = bop(), q = bop();
    q.phaseBeats = 0.25;
    f64 worst = 0;
    for (f64 t = 3.0; t < 8.0; t += 0.0137) {
        const hb::Pose a = hb::evaluate(q, ig, t), b = hb::evaluate(p, ig, t - 0.125);
        worst = std::max({worst, (f64)std::fabs(a.frameDy - b.frameDy), (f64)std::fabs(a.headPitch - b.headPitch) / 100.0,
                          (f64)std::fabs(a.torsoDx - b.torsoDx)});
    }
    AUREA_CHECK(worst < 1e-5);
    // Fase da grade (phaseSec) com BPM manual.
    hb::BeatGrid shifted;
    shifted.bpm = 120.0;
    shifted.phaseSec = 0.1;
    worst = 0;
    for (f64 t = 3.0; t < 8.0; t += 0.0137) {
        const hb::Pose a = hb::evaluate(p, shifted, t), b = hb::evaluate(p, ig, t - 0.1);
        worst = std::max(worst, (f64)std::fabs(a.frameDy - b.frameDy));
    }
    AUREA_CHECK(worst < 1e-5);
}

AUREA_TEST(HeadbopMotion, DelayAndSmoothness) {
    const hb::BeatGrid g = hb::grid_from_bpm(kRefBpm, 0.0, 40.0);
    const f64 b0 = 20 * kRefP;
    auto lag = [&](float delay) {
        hb::Params p = bop();
        p.delay = delay;
        const f64 tT = argmax(p, g, b0, b0 + kRefP, [](const hb::Pose& q) { return q.torsoDy; });
        const f64 tH = argmax(p, g, b0, b0 + kRefP, [](const hb::Pose& q) { return q.headPitch; });
        return (tH - tT) / kRefP;
    };
    const f64 l0 = lag(0.0f), l1 = lag(1.0f), l2 = lag(2.0f);
    std::printf("    atraso da cabeca: delay 0 → %.3f, 1 → %.3f, 2 → %.3f batida\n", l0, l1, l2);
    AUREA_CHECK(l0 < 0.02 && l2 > l1 * 1.5);
    auto overshoot = [&](float smooth) {
        hb::Params p = bop();
        p.smoothness = smooth;
        f64 mn = 1e9;
        for (f64 t = b0; t < b0 + kRefP; t += 0.001) mn = std::min(mn, (f64)hb::evaluate(p, g, t).headPitch);
        f64 mx = 0;
        argmax(p, g, b0, b0 + kRefP, [](const hb::Pose& q) { return q.headPitch; }, &mx);
        return mn / mx;   // quanto a cabeça "repica" (baixo = mais elástico)
    };
    const f64 soft = overshoot(0.5f), hard = overshoot(1.8f);
    std::printf("    min/max do aceno: suavidade 0.5 → %.3f, 1.8 → %.3f\n", soft, hard);
    AUREA_CHECK(soft != hard);
}

AUREA_TEST(HeadbopMotion, NoNaNOnDegenerateInput) {
    const f64 nan = std::numeric_limits<f64>::quiet_NaN(), inf = std::numeric_limits<f64>::infinity();
    std::vector<hb::BeatGrid> grids(6);
    grids[1].bpm = 120.0;                                  // implícita
    grids[2].beats = {1.0};                                // uma batida, sem BPM
    grids[3].beats = {1.0}; grids[3].bpm = 100.0;          // uma batida com BPM
    grids[4].beats = {1.0, 1.0, 1.0, 2.0, 2.0};            // espaçamento zero
    grids[4].strength = {1.0f, 0.0f};                      // tamanho errado
    grids[5] = hb::grid_from_bpm(120.0, 0.0, 10.0);
    grids[5].strength.assign(grids[5].beats.size(), static_cast<f32>(nan));
    hb::Params weird;
    weird.intensity = static_cast<f32>(inf); weird.smoothness = 0; weird.delay = static_cast<f32>(nan);
    weird.subdivision = 0; weird.phaseBeats = nan; weird.variation = -3; weird.preset = static_cast<hb::Preset>(200);
    std::vector<hb::Params> params = {hb::Params{}, weird};
    hb::Params zeroDelay;
    zeroDelay.delay = 0; zeroDelay.smoothness = 8;
    params.push_back(zeroDelay);
    hb::Params manual;
    manual.manualBpm = nan;
    params.push_back(manual);
    bool ok = true;
    for (const auto& g : grids)
        for (const auto& p : params)
            for (f64 t : {-5.0, 0.0, 0.5, 1.0, 1.5, 2.0, 3.7, 1e7, nan, inf, -inf}) ok = ok && finite_pose(hb::evaluate(p, g, t));
    AUREA_CHECK(ok);
    const hb::Pose z = hb::evaluate(hb::Params{}, grids[0], 3.0);
    AUREA_CHECK(z.frameDy == 0 && z.headPitch == 0 && z.torsoDx == 0);
    AUREA_CHECK(hb::preset_value(hb::Preset::Bop, -1) == 0 && hb::preset_value(hb::Preset::Count, 0) == 0);
    AUREA_CHECK(hb::preset_value(hb::Preset::Bop, hb::ChJolt) == 0.055f);
    // Antes da primeira batida e muito depois da última: repouso.
    const hb::Pose before = hb::evaluate(hb::Params{}, grids[5], -3.0), after = hb::evaluate(hb::Params{}, grids[5], 30.0);
    AUREA_CHECK(std::fabs(before.frameDy) < 1e-4f && std::fabs(after.frameDy) < 1e-4f && std::fabs(after.torsoDx) < 1e-4f);
    // PoseDriver padrão = evaluate().
    hb::ProceduralPoseDriver src;
    src.grid = grids[5];
    hb::PoseDriver& s = src;
    AUREA_CHECK(same(s.at(2.345), hb::evaluate(src.params, src.grid, 2.345)));
}

AUREA_TEST(HeadbopMotion, EvaluateIsFast) {
    // Grade explícita de 3 min (busca binária) e grade implícita.
    hb::BeatGrid g = hb::grid_from_bpm(kRefBpm, 0.21, 180.0);
    for (usize i = 0; i < g.strength.size(); ++i) g.strength[i] = 0.7f + 0.3f * static_cast<f32>(i % 3);
    hb::Params p;
    p.turn3d = 0.3f;
    p.beatZoom = 0.02f;
    volatile f32 sink = 0;
    for (int i = 0; i < 2000; ++i) sink = sink + hb::evaluate(p, g, i * 0.01).headPitch;   // aquece
    constexpr int kN = 200000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kN; ++i) sink = sink + hb::evaluate(p, g, 0.5 + i * 0.000871).headPitch;
    const f64 us = std::chrono::duration<f64, std::micro>(std::chrono::steady_clock::now() - t0).count() / kN;
    hb::Params q = p;
    q.preset = hb::Preset::Groove;
    q.subdivision = 0.5f;
    const auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < kN; ++i) sink = sink + hb::evaluate(q, g, 0.5 + i * 0.000871).headPitch;
    const f64 us2 = std::chrono::duration<f64, std::micro>(std::chrono::steady_clock::now() - t1).count() / kN;
    std::printf("    evaluate(): %.3f us (Bop), %.3f us (Groove, subdiv 0.5)\n", us, us2);
    AUREA_CHECK(us < 2.0);
    AUREA_CHECK(us2 < 2.0 * 2.2);   // subdivisão 0.5 = o dobro de eventos na janela
}
