// =============================================================================
//  Aurea / audio / Spectrum.cpp — espectro em faixas de uma janela de som.
//
//  Os bins do FFT são lineares (23,4 Hz cada, a 48 kHz e 2048 amostras); as
//  faixas são logarítmicas. Uma faixa grave (30–35 Hz) é mais estreita que um
//  bin: aí a magnitude é interpolada entre os dois bins vizinhos do centro da
//  faixa. Uma faixa aguda cobre dezenas de bins: fica o MAIOR deles — é o que
//  faz uma nota aparecer na faixa dela com a altura dela, em vez de diluída na
//  média de bins vazios.
// =============================================================================
#include "aurea/audio/Spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace aurea::audio {

namespace {

constexpr f64 kPi = 3.14159265358979323846;
constexpr f32 kMixRateHz = 48000.0f;

/// FFT radix-2 no lugar (tamanho potência de dois).
void fft_in_place(std::complex<f32>* a, u32 n) noexcept {
    for (u32 i = 1, j = 0; i < n; ++i) {
        u32 bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (u32 len = 2; len <= n; len <<= 1) {
        const f64 ang = -2.0 * kPi / static_cast<f64>(len);
        const std::complex<f32> wl(static_cast<f32>(std::cos(ang)), static_cast<f32>(std::sin(ang)));
        for (u32 i = 0; i < n; i += len) {
            std::complex<f32> w(1.0f, 0.0f);
            for (u32 k = 0; k < len / 2; ++k) {
                const std::complex<f32> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/// Amplitude linear (0..1+) → régua das barras: dB comprimidos em 0..1.
f32 to_bar(f32 amplitude) noexcept {
    if (!(amplitude > 0.0f)) return 0.0f;
    const f32 db = 20.0f * std::log10(amplitude);
    return std::clamp(1.0f + db / kSpectrumRangeDb, 0.0f, 1.0f);
}

} // namespace

f32 spectrum_band_center_hz(u32 band, u32 bands) noexcept {
    const u32 n = std::clamp(bands, 1u, kSpectrumMaxBands);
    const f32 t = (static_cast<f32>(std::min(band, n - 1)) + 0.5f) / static_cast<f32>(n);
    return kSpectrumLowHz * std::pow(kSpectrumHighHz / kSpectrumLowHz, t);
}

void analyze_spectrum(const f32* mono, u32 count, u32 bands, f32 gain, f32* outBands, f32* outLevel) noexcept {
    const u32 n = std::clamp(bands, 1u, kSpectrumMaxBands);
    if (outBands) std::fill(outBands, outBands + n, 0.0f);
    if (outLevel) *outLevel = 0.0f;
    if (!mono || count == 0) return;
    // Ganho 0 é silêncio pedido; só um valor inválido cai no 0 dB.
    const f32 g = std::isfinite(gain) && gain >= 0.0f ? gain : 1.0f;
    if (g <= 0.0f) return;

    // Janela de Hann sobre o que há (o resto é silêncio) e o RMS do trecho.
    std::vector<std::complex<f32>> buf(kSpectrumWindow);
    f64 energy = 0.0;
    for (u32 i = 0; i < kSpectrumWindow; ++i) {
        const f32 x = i < count && std::isfinite(mono[i]) ? mono[i] * g : 0.0f;
        const f32 w = 0.5f - 0.5f * static_cast<f32>(std::cos(2.0 * kPi * i / kSpectrumWindow));
        buf[i] = std::complex<f32>(x * w, 0.0f);
        energy += static_cast<f64>(x) * x;
    }
    if (outLevel) *outLevel = to_bar(static_cast<f32>(std::sqrt(energy / kSpectrumWindow)) * 1.41421356f);

    fft_in_place(buf.data(), kSpectrumWindow);

    // |X[k]| normalizado: um seno em escala cheia com janela de Hann soma
    // N/4 no bin dele.
    const u32 half = kSpectrumWindow / 2;
    std::vector<f32> mag(half + 1);
    const f32 norm = 4.0f / static_cast<f32>(kSpectrumWindow);
    for (u32 k = 0; k <= half; ++k) mag[k] = std::abs(buf[k]) * norm;

    const f32 binHz = kMixRateHz / static_cast<f32>(kSpectrumWindow);
    auto at_hz = [&](f32 hz) {
        const f32 b = std::clamp(hz / binHz, 0.0f, static_cast<f32>(half));
        const u32 i = static_cast<u32>(b);
        const f32 f = b - static_cast<f32>(i);
        return i + 1 <= half ? mag[i] + (mag[i + 1] - mag[i]) * f : mag[i];
    };
    if (!outBands) return;
    const f32 ratio = kSpectrumHighHz / kSpectrumLowHz;
    for (u32 band = 0; band < n; ++band) {
        const f32 lo = kSpectrumLowHz * std::pow(ratio, static_cast<f32>(band) / static_cast<f32>(n));
        const f32 hi = kSpectrumLowHz * std::pow(ratio, static_cast<f32>(band + 1) / static_cast<f32>(n));
        f32 amp = at_hz(spectrum_band_center_hz(band, n));
        // Faixa mais larga que um bin: o maior bin inteiro dentro dela.
        const u32 k0 = static_cast<u32>(std::ceil(lo / binHz));
        const u32 k1 = std::min(half, static_cast<u32>(std::floor(hi / binHz)));
        for (u32 k = k0; k <= k1 && k <= half; ++k) amp = std::max(amp, mag[k]);
        outBands[band] = to_bar(amp);
    }
}

} // namespace aurea::audio
