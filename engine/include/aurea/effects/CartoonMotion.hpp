#pragma once
#include "aurea/effects/Effect.hpp"
#include "aurea/timeline/Layer.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::cartoon_motion {
enum Parameter : u32 {
    MotionSource,
    MotionLayer,
    Shutter,
    FixedSamples,
    MotionThreshold,
    TimeThreshold,
    ScaleSamples,
    RotationSamples,
    Fill,
    SolidColor,
    StartColor,
    EndColor,
    StartOpacity,
    EndOpacity,
    Easing,
    MasterOpacity,
    Original,
    Left,
    Top,
    Right,
    Bottom
};
inline f32 ease(f32 x, u32 mode) noexcept {
    x = std::clamp(x, 0.f, 1.f);
    if (x == 0.f || x == 1.f)
        return x;
    if (mode == 0)
        return std::exp2(10.f * (x - 1.f));
    if (mode == 1)
        return 1.f - std::exp2(-10.f * x);
    if (mode == 2)
        return x < .5f ? .5f * std::exp2(10.f * (2.f * x - 1.f))
                       : .5f * (2.f - std::exp2(-10.f * (2.f * x - 1.f)));
    return x;
}
inline f32 max_delta(Vec3 a, Vec3 b) noexcept {
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}
inline f32 distance(const EffectMotionState &a, const EffectMotionState &b, f32 scale,
                    f32 rotation) noexcept {
    return std::max({max_delta(a.position, b.position), max_delta(a.anchor, b.anchor),
                     100.f * std::max(0.f, scale) * max_delta(a.scale, b.scale),
                     std::max(0.f, rotation) * max_delta(a.rotation, b.rotation)});
}
// AE's time subdivision is 1024 ticks per frame. The present is a separate composite.
inline std::vector<EffectTemporalSample> samples(const EffectEval &e) {
    std::vector<EffectTemporalSample> result;
    const f64 present = e.time_frames() + (e.layer ? e.layer->start.value - e.layer->offset.value : 0);
    const f64 shutter = std::clamp(static_cast<f64>(e.f(Shutter)), 0.0, 100000.0) / 360.0;
    if (!(shutter > 0.0))
        return result;
    if (e.e(MotionSource) == 2) {
        const u32 count = static_cast<u32>(std::clamp(e.f(FixedSamples), 0.f, 10000.f));
        result.reserve(count);
        for (u32 i = 0; i < count; ++i)
            result.push_back({present - shutter + shutter * i / count});
        return result;
    }
    if (!e.resources || !e.layer)
        return result;
    const u64 guide = e.value(MotionLayer).ref;
    const bool world = e.e(MotionSource) == 1;
    const f64 step = std::clamp(static_cast<f64>(e.f(TimeThreshold)), 1.0, 1024.0) / 1024.0;
    f64 accepted = present - shutter;
    EffectMotionState previous;
    if (!e.resources->motion_state(e.layer, guide, accepted, world, previous))
        return result;
    for (f64 probe = accepted + step; probe <= present; probe += step) {
        EffectMotionState current;
        if (!e.resources->motion_state(e.layer, guide, probe, world, current))
            continue;
        if (distance(previous, current, e.f(ScaleSamples), e.f(RotationSamples)) >= e.f(MotionThreshold)) {
            result.push_back({accepted});
            accepted = probe;
            previous = current;
        }
    }
    return result;
}
} // namespace aurea::cartoon_motion
