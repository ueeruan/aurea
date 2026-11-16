#pragma once
// Glitchify reference (CSpice Glitchify 1.0.0, behaviour reconstructed with REA
// in build/reference/effects-20261115/batch-c). The legacy aurea.glitch.glitchify
// stays registered with its 60 slots; this key carries the native 65-control
// contract in native order. Shared C++ rules here drive both mobile UIs.

#include "aurea/effects/EffectRegistry.hpp"

#include <array>

namespace aurea::glitchify_reference {
inline constexpr const char* kKey = "aurea.glitch.glitchify_reference";

enum ParamIndex : u32 {
    kAmount, kSpeed, kSeed,
    // Transition
    kCompletion, kSoftness, kTransitionChannel,
    // Transform Glitch
    kTransformSpeed, kPosition, kPositionWiggle, kScale, kScaleWiggle, kCropTopLeft, kCropBottomRight,
    kCropWiggle, kTransformOpacity, kTransformMode, kCompositeOver,
    // Channel Glitch
    kChannelEnabled, kSplitChannel, kSplitHorizontal, kSplitVertical, kSplitSpeed,
    kChannelScale, kScaleChannel, kScaleVertical, kScaleOffset, kScaleSpeed, kChannelMode,
    // Color Glitch
    kColorEnabled, kColorOffset, kColorAmount, kColorOpacity, kColorMode,
    // Image Glitch
    kImageEnabled, kPixelStreak, kVerticalSort, kSliceAmount, kSlicePosition, kSliceOpacity, kSliceMode,
    kBlockAmount, kBlockSpeed, kBlockWidth, kBlockHeight, kBlockDirection, kImageMode,
    // Compression Glitch
    kCompressionEnabled, kCompressionMultiplier, kDitherAmount, kJpegAmount,
    // Advanced
    kShowNoise, kNoiseGain, kNoiseLacunarity, kNoiseOctaves, kRepeatEdges,
    kSigmoidSteps, kScaleSteps, kFillGaps, kUseNoise, kNoiseCutoff,
    kSliceQuality, kSliceShift, kSliceSteps, kGrouping, kWiggleSteps,
    kCount
};

/// The 39 functional AE transfer modes in menu order (separators removed).
inline constexpr u32 kTransferModeCount = 39;
inline constexpr u32 kTransferDivide = 28;
inline constexpr u32 kTransferOverlay = 17;

/// The noise engine's permutation: 256 entries shuffled by the MSVC CRT
/// `rand()` sequence seeded with Random Seed (behaviour of the native
/// constructor; the generator is the documented CRT LCG, not plugin code).
[[nodiscard]] std::array<u8, 256> permutation(u32 seed) noexcept;

/// Noise-engine sample (native 0x180067A50): octave sum of 3D simplex noise at
/// (y, x, z) / height (frequency × lacunarity, amplitude × gain per octave),
/// returned as sum · 16 + 0.5 (about 0..1).
[[nodiscard]] f32 sample(const std::array<u8, 256>& perm, f32 x, f32 y, f32 z, f32 height, u32 octaves,
                         f32 lacunarity, f32 gain) noexcept;
/// The 12-sample quantized wiggle (native 0x1800683C0): samples at (salt,
/// salt + 30·i, z), (Σ − 6)/6, truncated to `steps`.
[[nodiscard]] f32 wiggle(const std::array<u8, 256>& perm, i32 salt, f32 z, f32 steps, f32 height, u32 octaves,
                         f32 lacunarity, f32 gain) noexcept;

void register_effect(EffectRegistry& registry);

} // namespace aurea::glitchify_reference
