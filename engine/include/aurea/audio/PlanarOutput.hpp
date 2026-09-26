#pragma once
#include "aurea/audio/Audio.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace aurea::audio {
// AVAudioFormat standard float buffers are planar. Keep the platform adapter
// bounded and allocation-free, even when the device requests a large quantum.
inline bool render_planar_stereo(AudioRenderFn fn, void* ctx, f32* left, usize leftBytes,
                                 f32* right, usize rightBytes, u32 frames) noexcept {
    const usize required = static_cast<usize>(frames) * sizeof(f32);
    if (!left || !right || leftBytes < required || rightBytes < required) {
        if (left) std::memset(left, 0, leftBytes);
        if (right) std::memset(right, 0, rightBytes);
        return false;
    }
    if (!fn) {
        std::memset(left, 0, required); std::memset(right, 0, required);
        return false;
    }
    constexpr u32 quantum = 256;
    std::array<f32, quantum * kMixChannels> stereo{};
    for (u32 offset = 0; offset < frames;) {
        const u32 count = std::min(quantum, frames - offset);
        fn(ctx, stereo.data(), count);
        for (u32 i = 0; i < count; ++i) {
            left[offset + i] = stereo[i * 2];
            right[offset + i] = stereo[i * 2 + 1];
        }
        offset += count;
    }
    return true;
}
} // namespace aurea::audio
