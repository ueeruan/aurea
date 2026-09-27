#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aurea::shake {
// Evaluated in seconds, without mutable RNG state. Scrubbing, reverse and
// export sample the same continuous camera path at the same timestamp.
inline float random(std::uint32_t seed, std::int64_t cell, std::uint32_t axis) noexcept {
    std::uint64_t x = std::uint64_t(seed) * 0x9E3779B97F4A7C15ull;
    x ^= std::uint64_t(cell) * 0xBF58476D1CE4E5B9ull;
    x ^= std::uint64_t(axis + 1) * 0x94D049BB133111EBull;
    x ^= x >> 31; x *= 0xD6E8FEB86659FD93ull; x ^= x >> 29;
    return float(x & 0xFFFFFFull) / 8388607.5f - 1.f;
}
inline float noise(double time, std::uint32_t seed, std::uint32_t axis, float smoothness) noexcept {
    if (!std::isfinite(time)) return 0;
    time = std::clamp(time, -1e12, 1e12);
    const auto cell = static_cast<std::int64_t>(std::floor(time));
    const float f = float(time - double(cell));
    const float eased = f*f*f*(f*(f*6.f-15.f)+10.f);
    const float weight = f + (eased-f)*std::clamp(smoothness, 0.f, 1.f);
    const float a = random(seed, cell, axis), b = random(seed, cell+1, axis);
    return a + (b-a)*weight;
}
struct Settings {
    float amplitudeX = 20, amplitudeY = 20, frequency = 8, rotation = 0, zoom = 0;
    float amount = 1, smoothness = 1, phase = 0, wave = 0;
    std::uint32_t seed = 1, style = 0;
    bool separate = true;
};
struct Pose { float x = 0, y = 0, rotation = 0, scale = 1; };
inline Pose sample(const Settings& s, double seconds) noexcept {
    if (!std::isfinite(seconds) || s.amount <= 0 || s.frequency <= 0) return {};
    const double t = seconds * double(s.frequency) + double(s.phase);
    auto axis = [&](std::uint32_t index) {
        float value = noise(t, s.seed, index, s.smoothness);
        if (s.style == 1) { // Twitchy: continuous, short bursts separated by rests.
            const float gate = std::max(0.f, noise(t * .25, s.seed + 73, 9, 1.f));
            value *= std::min(1.f, gate * 3.f);
        } else if (s.style == 2) { // Jumpy: fast transition followed by a slow drift.
            const double cell = std::floor(t);
            const double f = t-cell;
            const double jump = std::min(1.0, f / .12);
            value = noise(cell + jump, s.seed, index, 1.f) * .85f
                  + noise(t*.35, s.seed+37, index, 1.f) * .15f;
        }
        const float wave = std::sin(float(std::remainder(t, 1.0) * 6.283185307179586 + index * 1.618));
        return (value * (1.f-s.wave) + wave*s.wave) * s.amount;
    };
    const float nx = axis(0), ny = s.separate ? axis(1) : nx;
    return {nx*s.amplitudeX, ny*s.amplitudeY, axis(2)*s.rotation,
            std::exp2(std::clamp(axis(3)*s.zoom*.01f, -2.f, 2.f))};
}
} // namespace aurea::shake
