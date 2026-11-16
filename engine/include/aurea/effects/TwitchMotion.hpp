#pragma once
#include "aurea/effects/Effect.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace aurea::twitch_motion {
enum Parameter : u32 {
    Amount,
    Speed,
    BlurEnabled,
    ColorEnabled,
    LightEnabled,
    ScaleEnabled,
    SlideEnabled,
    TimeEnabled,
    EaseIn,
    EaseOut,
    Border,
    ShowOnly,
    Seed,
    RandomMinimum,
    BlurAmount,
    BlurRate,
    BlurTint,
    BlurHoldout,
    BlurSharpness,
    BlurBoost,
    BlurOpacity,
    BlurBlend,
    BlurAspect,
    LensBlur,
    BlurSeed,
    ColorAmount,
    ColorRate,
    Colorize,
    ColorRandom,
    ColorSeed,
    LightAmount,
    LightRate,
    LightBehavior,
    LightSeed,
    ScaleAmount,
    ScaleRate,
    ScaleOrigin,
    ScaleOriginRandom,
    ScaleMotionBlur,
    ScaleSeed,
    SlideAmount,
    SlideRate,
    SlideDirection,
    SlideSpread,
    SlideTendency,
    SlideRgb,
    SlideMotionBlur,
    SlideSeed,
    TimeAmount,
    TimeRate,
    TimeDirection,
    TimeSeed,
    Count
};
// Jump ahead in the 32-bit generator without process-global rand state or time-linear work.
inline f64 random(u32 seed, i64 cell) noexcept {
    u64 count = static_cast<u64>(std::abs(std::max<i64>(-4, cell))) + 6;
    u32 multiplier = 214013u, increment = 2531011u, combinedMultiplier = 1, combinedIncrement = 0;
    while (count) {
        if (count & 1u) {
            combinedIncrement = combinedIncrement * multiplier + increment;
            combinedMultiplier *= multiplier;
        }
        increment *= multiplier + 1u;
        multiplier *= multiplier;
        count >>= 1u;
    }
    const u32 state = combinedMultiplier * seed + combinedIncrement;
    return static_cast<f64>((state >> 16u) & 32767u) / 32767.0;
}
inline std::pair<f64, f64> event_interval(f64 time, i32 period, u32 seed) noexcept {
    const i64 cell = static_cast<i64>(time / period);
    const auto event = [&](i64 index) { return (index + random(seed, index)) * period; };
    const f64 center = event(cell);
    return center <= time ? std::pair{center, event(cell + 1)} : std::pair{event(cell - 1), center};
}
inline f64 pulse(f64 time, i32 period, u32 seed, f64 in, f64 out) noexcept {
    const auto [previous, next] = event_interval(time, period, seed);
    return std::max(0.0, std::max(1.0 - (time - previous) / (out + 1.0), 1.0 - (next - time) / (in + 1.0)));
}
inline i32 period(const EffectEval &e, u32 rate) noexcept {
    const f64 frequency = std::max(0.f, e.f(Speed)) * std::max(0.f, e.f(rate));
    if (!(frequency > 0.0))
        return 0;
    return static_cast<i32>(std::clamp(e.framesPerSecond / frequency, 1.0, 2147483647.0));
}
inline f64 noise(f64 time, i32 period, u32 seed, f64 minimum) noexcept {
    i64 cell = static_cast<i64>(time / period);
    if (time < 0)
        --cell;
    const f64 value = random(seed, cell);
    return minimum + (1.0 - minimum) * value;
}
inline f64 amplitude(const EffectEval &e, u32 amount, u32 rate, u32 uniqueSeed, u32 stream,
                     f64 time) noexcept {
    const i32 interval = period(e, rate);
    if (!interval)
        return 0;
    const u32 seed = static_cast<u32>(e.f(Seed) + e.f(uniqueSeed));
    return std::max(0.f, e.f(Amount)) * .01 * std::max(0.f, e.f(amount)) *
           pulse(time, interval, seed, e.f(EaseIn), e.f(EaseOut)) *
           noise(time, interval, seed + stream, e.f(RandomMinimum) * .01);
}
inline f64 source_offset(const EffectEval &e) noexcept {
    if (!e.b(TimeEnabled))
        return 0;
    const f64 time = e.time_frames() + .5;
    const i32 interval = period(e, TimeRate);
    if (!interval)
        return 0;
    f64 amount = amplitude(e, TimeAmount, TimeRate, TimeSeed, 10000, time);
    const u32 seed = static_cast<u32>(e.f(Seed) + e.f(TimeSeed));
    if (e.e(TimeDirection) == 2 || (e.e(TimeDirection) == 0 && noise(time, interval, seed + 10001, 0) < .5))
        amount = -amount;
    return static_cast<i64>(amount * e.framesPerSecond * .01);
}
} // namespace aurea::twitch_motion
