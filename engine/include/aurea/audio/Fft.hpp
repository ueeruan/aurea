#pragma once

#include "aurea/core/Types.hpp"
#include <complex>

namespace aurea::audio {
// In-place radix-2 transform. The inverse includes normalization.
// Callers admit a power-of-two length before entering audio processing.
void fft_complex(std::complex<f32>* samples, u32 count, bool inverse = false) noexcept;
}
