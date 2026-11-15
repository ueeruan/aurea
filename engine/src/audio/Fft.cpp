#include "aurea/audio/Fft.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::audio {
void fft_complex(std::complex<f32>* samples, u32 count, bool inverse) noexcept {
    if (!samples || count < 2 || (count & (count - 1))) return;
    for (u32 i = 1, j = 0; i < count; ++i) {
        u32 bit = count >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(samples[i], samples[j]);
    }
    constexpr f64 pi = 3.14159265358979323846;
    for (u32 length = 2; length <= count; length <<= 1) {
        const f64 angle = (inverse ? 2.0 : -2.0) * pi / length;
        const std::complex<f32> step(static_cast<f32>(std::cos(angle)), static_cast<f32>(std::sin(angle)));
        for (u32 first = 0; first < count; first += length) {
            std::complex<f32> phase(1.f, 0.f);
            for (u32 k = 0; k < length / 2; ++k) {
                const auto a = samples[first + k];
                const auto b = samples[first + k + length / 2] * phase;
                samples[first + k] = a + b;
                samples[first + k + length / 2] = a - b;
                phase *= step;
            }
        }
        if (length == count) break;
    }
    if (inverse) for (u32 i = 0; i < count; ++i) samples[i] /= static_cast<f32>(count);
}
}
