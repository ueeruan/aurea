#include "aurea/audio/Beats.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace aurea::audio {

namespace {

constexpr u32 kWin = 1024;
constexpr u32 kHop = 480;              // 100 quadros/s a 48 kHz
constexpr f64 kFrameRate = 48000.0 / kHop;
constexpr u32 kMaxBin = 342;           // ~16 kHz

void fft(std::vector<std::complex<f32>>& a) noexcept {
    const usize n = a.size();
    for (usize i = 1, j = 0; i < n; ++i) {
        usize bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (usize len = 2; len <= n; len <<= 1) {
        const f32 ang = -2.0f * 3.14159265358979f / static_cast<f32>(len);
        const std::complex<f32> wl(std::cos(ang), std::sin(ang));
        for (usize i = 0; i < n; i += len) {
            std::complex<f32> w(1.0f, 0.0f);
            for (usize k = 0; k < len / 2; ++k) {
                const std::complex<f32> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/// Envelope de ataques (um valor por quadro de 10 ms).
std::vector<f32> onset_envelope(const f32* x, usize n) {
    std::vector<f32> env;
    if (n < kWin) return env;
    const usize frames = (n - kWin) / kHop + 1;
    env.assign(frames, 0.0f);
    std::vector<f32> hann(kWin);
    for (u32 i = 0; i < kWin; ++i) hann[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * static_cast<f32>(i) / kWin);
    std::vector<std::complex<f32>> buf(kWin);
    std::vector<f32> prev(kMaxBin, 0.0f), cur(kMaxBin);
    for (usize f = 0; f < frames; ++f) {
        const f32* p = x + f * kHop;
        for (u32 i = 0; i < kWin; ++i) buf[i] = std::complex<f32>(p[i] * hann[i], 0.0f);
        fft(buf);
        f32 flux = 0.0f;
        for (u32 b = 1; b < kMaxBin; ++b) {
            cur[b] = std::log1p(100.0f * std::abs(buf[b]));
            if (f > 0) flux += std::max(0.0f, cur[b] - prev[b]);
        }
        std::swap(prev, cur);
        env[f] = flux;
    }
    // Menos a média local (0,5 s), meia-onda, desvio unitário.
    const usize half = 25;
    std::vector<f64> pre(frames + 1, 0.0);
    for (usize i = 0; i < frames; ++i) pre[i + 1] = pre[i] + env[i];
    std::vector<f32> out(frames);
    f64 sq = 0.0;
    for (usize i = 0; i < frames; ++i) {
        const usize a = i > half ? i - half : 0, b = std::min(frames, i + half + 1);
        const f32 mean = static_cast<f32>((pre[b] - pre[a]) / static_cast<f64>(b - a));
        out[i] = std::max(0.0f, env[i] - mean);
        sq += static_cast<f64>(out[i]) * out[i];
    }
    const f32 sd = static_cast<f32>(std::sqrt(sq / static_cast<f64>(frames)));
    if (sd > 1e-9f) for (f32& v : out) v /= sd;
    return out;
}

} // namespace

BeatResult detect_beats(const f32* mono, usize count) noexcept {
    BeatResult r;
    if (!mono || count < 48000 * 3) return r;
    const std::vector<f32> o = onset_envelope(mono, count);
    const usize n = o.size();
    if (n < 300) return r;

    // Tempo: autocorrelação ponderada (60–200 BPM).
    const i32 minLag = static_cast<i32>(std::floor(kFrameRate * 60.0 / 200.0));
    const i32 maxLag = static_cast<i32>(std::ceil(kFrameRate * 60.0 / 60.0));
    std::vector<f64> ac(static_cast<usize>(maxLag + 2), 0.0);
    for (i32 lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        f64 s = 0.0;
        for (usize i = static_cast<usize>(lag); i < n; ++i) s += static_cast<f64>(o[i]) * o[i - static_cast<usize>(lag)];
        ac[static_cast<usize>(lag)] = s / static_cast<f64>(n - static_cast<usize>(lag));
    }
    const f64 center = kFrameRate * 60.0 / 120.0;
    i32 best = -1;
    f64 bestScore = 0.0;
    for (i32 lag = minLag; lag <= maxLag; ++lag) {
        const f64 oct = std::log2(static_cast<f64>(lag) / center);
        const f64 score = ac[static_cast<usize>(lag)] * std::exp(-0.5 * oct * oct / (0.9 * 0.9));
        if (score > bestScore) { bestScore = score; best = lag; }
    }
    if (best < 0 || bestScore <= 0.0) return r;
    // Refinamento parabólico do período.
    f64 period = best;
    {
        const f64 y0 = ac[static_cast<usize>(best - 1)], y1 = ac[static_cast<usize>(best)], y2 = ac[static_cast<usize>(best + 1)];
        const f64 den = y0 - 2.0 * y1 + y2;
        if (std::fabs(den) > 1e-12) period += std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
    }

    // Programação dinâmica.
    constexpr f64 kTightness = 100.0;
    std::vector<f64> score(n);
    std::vector<i64> from(n, -1);
    const i64 lo = static_cast<i64>(std::round(period * 0.5)), hi = static_cast<i64>(std::round(period * 2.0));
    for (usize t = 0; t < n; ++t) {
        f64 bestPrev = 0.0;
        i64 arg = -1;
        for (i64 d = lo; d <= hi; ++d) {
            const i64 p = static_cast<i64>(t) - d;
            if (p < 0) break;
            const f64 dev = std::log(static_cast<f64>(d) / period);
            const f64 s = score[static_cast<usize>(p)] - kTightness * dev * dev;
            if (arg < 0 || s > bestPrev) { bestPrev = s; arg = p; }
        }
        score[t] = o[t] + (arg >= 0 ? std::max(0.0, bestPrev) : 0.0);
        from[t] = (arg >= 0 && bestPrev > 0.0) ? arg : -1;
    }
    // Última batida: o melhor ponto no último período.
    usize end = n - 1;
    f64 top = -1.0;
    for (usize t = n - 1 - std::min<usize>(n - 1, static_cast<usize>(period)); t < n; ++t) {
        if (score[t] > top) { top = score[t]; end = t; }
    }
    std::vector<usize> idx;
    for (i64 t = static_cast<i64>(end); t >= 0; t = from[static_cast<usize>(t)]) idx.push_back(static_cast<usize>(t));
    std::reverse(idx.begin(), idx.end());
    // Pontas sem ataque: a DP completa a grade para dentro dos buracos (bom no
    // meio da música), mas no começo/fim ela inventa batida onde não há nada
    // para ouvir. Corta as das pontas abaixo de 20% da força mediana.
    if (idx.size() >= 3) {
        std::vector<f32> strength;
        strength.reserve(idx.size());
        for (usize t : idx) strength.push_back(o[t]);
        std::nth_element(strength.begin(), strength.begin() + strength.size() / 2, strength.end());
        const f32 floor = 0.2f * strength[strength.size() / 2];
        usize a = 0, b = idx.size();
        while (a < b && o[idx[a]] < floor) ++a;
        while (b > a && o[idx[b - 1]] < floor) --b;
        idx = std::vector<usize>(idx.begin() + static_cast<std::ptrdiff_t>(a), idx.begin() + static_cast<std::ptrdiff_t>(b));
    }
    // Tempo do quadro: o fluxo acende quando o ataque entra na metade
    // posterior da janela (medido com cliques sintéticos: ~meia janela).
    const f64 offset = static_cast<f64>(kWin) * 0.5 / 48000.0;
    r.beats.reserve(idx.size());
    for (usize t : idx) r.beats.push_back(static_cast<f64>(t) / kFrameRate + offset);
    r.bpm = 60.0 * kFrameRate / period;
    return r;
}

} // namespace aurea::audio
