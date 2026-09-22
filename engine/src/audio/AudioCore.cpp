// =============================================================================
//  Aurea / audio / AudioCore.cpp — tempo, reamostragem, canais, PCM.
// =============================================================================
#include "aurea/audio/Audio.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::audio {

i64 frame_to_sample(i64 frame, f64 fps) noexcept {
    if (!(fps > 0.0)) fps = 30.0;
    const f64 rounded = std::round(fps);
    if (std::fabs(fps - rounded) < 1e-6) {
        const i64 f = static_cast<i64>(rounded);
        // Floor também para negativos (frame antes do zero da mídia).
        const i64 n = frame * static_cast<i64>(kMixRate);
        return n >= 0 ? n / f : -((-n + f - 1) / f);
    }
    // NTSC: 23,976 / 29,97 / 59,94 = N·1000/1001. A conta em inteiros não
    // acumula deriva: o frame 107892 de um 29,97 cai na amostra exata.
    const f64 ntsc = fps * 1001.0 / 1000.0;
    if (std::fabs(ntsc - std::round(ntsc)) < 1e-3) {
        const i64 num = static_cast<i64>(std::round(ntsc)) * 1000;
        const i64 n = frame * static_cast<i64>(kMixRate) * 1001;
        return n >= 0 ? n / num : -((-n + num - 1) / num);
    }
    return static_cast<i64>(std::floor(static_cast<f64>(frame) * kMixRate / fps));
}

// -----------------------------------------------------------------------------
// Reamostragem: sinc janelado (Kaiser β = 9), tabela de 512 fases com
// interpolação linear entre fases. Descendo a taxa (96 → 48 kHz) o corte
// acompanha (anti-aliasing) e a janela alonga na mesma proporção; subindo, o
// corte fica em 95% do Nyquist da fonte.
// -----------------------------------------------------------------------------
namespace {

constexpr i32 kPhases = 512;

f64 bessel_i0(f64 x) noexcept {
    f64 sum = 1.0, term = 1.0;
    const f64 q = x * x / 4.0;
    for (i32 k = 1; k < 60; ++k) {
        term *= q / (static_cast<f64>(k) * k);
        sum += term;
        if (term < sum * 1e-12) break;
    }
    return sum;
}

struct Kernel {
    f64 step = -1.0;
    i32 half = 0;
    i32 taps = 0;
    std::vector<f32> table;   ///< (kPhases + 1) × taps
};

const Kernel& kernel_for(f64 step) {
    thread_local Kernel k;
    if (k.step == step) return k;
    k.step = step;
    k.half = resample_half_width(step);
    k.taps = 2 * k.half;
    k.table.assign(static_cast<usize>(kPhases + 1) * k.taps, 0.0f);
    const f64 fc = step > 1.0 ? 0.95 / step : 0.95;
    constexpr f64 beta = 9.0;
    const f64 i0b = bessel_i0(beta);
    for (i32 p = 0; p <= kPhases; ++p) {
        const f64 frac = static_cast<f64>(p) / kPhases;
        f64 sum = 0.0;
        f32* row = k.table.data() + static_cast<usize>(p) * k.taps;
        for (i32 t = 0; t < k.taps; ++t) {
            const f64 x = static_cast<f64>(t - (k.half - 1)) - frac;
            const f64 r = x / k.half;
            const f64 w = std::fabs(r) >= 1.0 ? 0.0 : bessel_i0(beta * std::sqrt(1.0 - r * r)) / i0b;
            const f64 px = 3.14159265358979323846 * fc * x;
            const f64 sn = std::fabs(px) < 1e-12 ? 1.0 : std::sin(px) / px;
            const f64 h = fc * sn * w;
            row[t] = static_cast<f32>(h);
            sum += h;
        }
        // Ganho DC exatamente 1 em toda fase: sem "zumbido" de fase.
        if (sum != 0.0) {
            for (i32 t = 0; t < k.taps; ++t) row[t] = static_cast<f32>(row[t] / sum);
        }
    }
    return k;
}

} // namespace

i32 resample_half_width(f64 step) noexcept {
    const f64 s = std::clamp(step, 1.0, 4.0);
    return static_cast<i32>(std::ceil(kResampleTaps * s));
}

void resample_to_mix(const f32* src, i64 srcFrames, f64 srcPos0, f64 step, f32* out, u32 outFrames) noexcept {
    if (step == 1.0 && srcPos0 == std::floor(srcPos0)) {
        const i64 base = static_cast<i64>(srcPos0);
        for (u32 n = 0; n < outFrames; ++n) {
            const i64 i = base + n;
            const bool in = i >= 0 && i < srcFrames;
            out[2 * n] = in ? src[2 * i] : 0.0f;
            out[2 * n + 1] = in ? src[2 * i + 1] : 0.0f;
        }
        return;
    }
    const Kernel& k = kernel_for(step);
    const i32 taps = k.taps;
    for (u32 n = 0; n < outFrames; ++n) {
        const f64 pos = srcPos0 + static_cast<f64>(n) * step;
        const f64 fl = std::floor(pos);
        const i64 i0 = static_cast<i64>(fl);
        const f64 phase = (pos - fl) * kPhases;
        const i32 p = std::min(kPhases - 1, static_cast<i32>(phase));
        const f32 a = static_cast<f32>(phase - p);
        const f32* r0 = k.table.data() + static_cast<usize>(p) * taps;
        const f32* r1 = r0 + taps;
        const i64 first = i0 - (k.half - 1);
        f32 l = 0.0f, rr = 0.0f;
        if (first >= 0 && first + taps <= srcFrames) {
            const f32* sp = src + 2 * first;
            for (i32 t = 0; t < taps; ++t) {
                const f32 c = r0[t] + (r1[t] - r0[t]) * a;
                l += c * sp[2 * t];
                rr += c * sp[2 * t + 1];
            }
        } else {
            for (i32 t = 0; t < taps; ++t) {
                const i64 i = first + t;
                if (i < 0 || i >= srcFrames) continue;
                const f32 c = r0[t] + (r1[t] - r0[t]) * a;
                l += c * src[2 * i];
                rr += c * src[2 * i + 1];
            }
        }
        out[2 * n] = l;
        out[2 * n + 1] = rr;
    }
}

void to_stereo(const f32* f, u32 channels, f32& l, f32& r) noexcept {
    switch (channels) {
        case 0: l = r = 0.0f; return;
        case 1: l = r = f[0]; return;
        case 2: l = f[0]; r = f[1]; return;
        default: break;
    }
    // Ordem do WAVE/MediaCodec: FL FR FC LFE BL BR (SL SR). LFE fica de fora
    // (downmix ITU-R BS.775); centro e surround entram a −3 dB.
    constexpr f32 k = 0.70710678f;
    l = f[0];
    r = f[1];
    if (channels >= 3) {
        l += k * f[2];
        r += k * f[2];
    }
    if (channels >= 6) {
        l += k * f[4];
        r += k * f[5];
    }
    if (channels >= 8) {
        l += k * f[6];
        r += k * f[7];
    }
    // Normaliza para o pior caso não saturar (1 + 0,707·2 com 8 canais).
    const f32 norm = channels >= 8 ? 1.0f / (1.0f + 2.0f * k) : channels >= 6 ? 1.0f / (1.0f + 1.41421356f) : 1.0f / (1.0f + k);
    l *= norm;
    r *= norm;
}

void to_pcm16(const f32* in, usize samples, i16* out) noexcept {
    for (usize i = 0; i < samples; ++i) {
        const f32 v = std::clamp(in[i], -1.0f, 1.0f) * 32767.0f;
        out[i] = static_cast<i16>(std::lrint(v));
    }
}

} // namespace aurea::audio
