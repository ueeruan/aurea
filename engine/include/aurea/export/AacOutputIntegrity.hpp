#pragma once

#include "aurea/core/Types.hpp"

#include <limits>
#include <span>
#include <string_view>

namespace aurea {

struct AacLcFrameInfo {
    u32 sampleRate = 0;
    u32 channels = 0;
    u32 frameSamples = 0;
};

// Narrow ASC reader: plain AAC-LC, mono/stereo, standard GASpecificConfig.
// Unknown extensions (including SBR/PS), malformed data and core-coder
// dependencies deliberately return unknown, rather than assuming 1024 PCM.
// GASpecificConfig.frameLengthFlag selects 1024 or 960, independently of delay.
[[nodiscard]] inline AacLcFrameInfo aac_lc_frame_info(std::span<const u8> asc) noexcept {
    if (asc.size() != 2 && asc.size() != 5) return {};
    usize bit = 0;
    auto read = [&](u32 count, u32& value) {
        if (count > asc.size() * 8 - bit) return false;
        value = 0;
        for (u32 n = 0; n < count; ++n, ++bit)
            value = (value << 1) | ((asc[bit / 8] >> (7 - bit % 8)) & 1u);
        return true;
    };
    u32 objectType = 0, rateIndex = 0, rate = 0, channels = 0;
    u32 shortFrame = 0, coreCoder = 0, extension = 0;
    if (!read(5, objectType) || objectType != 2 || !read(4, rateIndex)) return {};
    if (rateIndex == 15) {
        if (!read(24, rate) || !rate) return {};
    } else {
        constexpr u32 rates[]{96000, 88200, 64000, 48000, 44100, 32000, 24000,
                              22050, 16000, 12000, 11025, 8000, 7350};
        if (rateIndex >= sizeof(rates) / sizeof(rates[0])) return {};
        rate = rates[rateIndex];
    }
    if (!read(4, channels) || (channels != 1 && channels != 2)
        || !read(1, shortFrame) || !read(1, coreCoder) || coreCoder
        || !read(1, extension) || extension || bit != asc.size() * 8) return {};
    return {rate, channels, shortFrame ? 960u : 1024u};
}

// MediaCodec generally permits multiple encoded audio AUs per buffer. Only
// the audited AOSP component with explicitly disabled output batching is
// eligible for the stronger packet-capacity check. Missing keys are unknown.
[[nodiscard]] inline bool aac_c2_single_au_contract(std::string_view component,
    bool maxBatchKnown, i32 maxBatch, bool thresholdKnown, i32 threshold) noexcept {
    return component == "c2.android.aac.encoder" && maxBatchKnown && thresholdKnown
        && maxBatch == 0 && threshold == 0;
}

class AacOutputIntegrity {
public:
    void set_format(std::span<const u8> asc, u32 pcmRate, u32 pcmChannels,
                    bool confirmedSingleAu) noexcept {
        frame_ = aac_lc_frame_info(asc);
        singleAu_ = confirmedSingleAu && frame_.sampleRate == pcmRate
            && frame_.channels == pcmChannels && frame_.frameSamples != 0;
    }

    // Call only after the platform accepted a PCM input buffer successfully.
    void accepted_pcm(u64 frames) noexcept {
        if (frames > std::numeric_limits<u64>::max() - acceptedFrames_) {
            acceptedFrames_ = std::numeric_limits<u64>::max();
            counterOverflow_ = true;
        } else acceptedFrames_ += frames;
    }

    // MediaCodec flag values: codec config=2, EOS=4, partial frame=8.
    // A nonempty data buffer that also carries EOS remains an audio output.
    // Config-only buffers and an empty EOS never represent PCM samples.
    void encoded_buffer(u64 payloadBytes, u32 flags, i64 ptsUs) noexcept {
        if (flags & 8u) unknownPacketization_ = true;
        if (!payloadBytes || (flags & 2u)) return;
        if (flags & ~(1u | 4u)) unknownPacketization_ = true;
        if (dataBuffers_ == std::numeric_limits<u64>::max()) counterOverflow_ = true;
        else ++dataBuffers_;
        if (havePts_ && singleAu_) {
            const u64 delta = static_cast<u64>(ptsUs) - static_cast<u64>(lastPts_);
            const u64 numerator = static_cast<u64>(frame_.frameSamples) * 1'000'000;
            const u64 lo = numerator / frame_.sampleRate;
            const u64 hi = (numerator + frame_.sampleRate - 1) / frame_.sampleRate;
            // Timestamp rounding may move a cadence by one microsecond.
            // Gaps, repeats or batch cadences disable the strong guard safely.
            if (ptsUs <= lastPts_ || delta < lo - 1
                || delta > hi + 1) unknownPacketization_ = true;
        }
        lastPts_ = ptsUs;
        havePts_ = true;
    }

    [[nodiscard]] bool capacity_known() const noexcept {
        return singleAu_ && !unknownPacketization_ && !counterOverflow_;
    }
    // True only for proven insufficient encoded capacity. False does not prove
    // complete audio: leading encoder delay can hide tail loss even when the
    // nominal AU capacity equals accepted PCM. Do not subtract guessed delay.
    [[nodiscard]] bool proves_insufficient() const noexcept {
        if (!acceptedFrames_) return false;
        if (!dataBuffers_) return true;
        if (!capacity_known()) return false;
        const u64 needed = acceptedFrames_ / frame_.frameSamples
            + (acceptedFrames_ % frame_.frameSamples != 0);
        return dataBuffers_ < needed;
    }
    [[nodiscard]] u64 accepted_frames() const noexcept { return acceptedFrames_; }
    [[nodiscard]] u64 data_buffers() const noexcept { return dataBuffers_; }
    [[nodiscard]] u32 frame_samples() const noexcept { return frame_.frameSamples; }

private:
    AacLcFrameInfo frame_{};
    u64 acceptedFrames_ = 0, dataBuffers_ = 0;
    i64 lastPts_ = 0;
    bool singleAu_ = false, unknownPacketization_ = false;
    bool counterOverflow_ = false, havePts_ = false;
};

} // namespace aurea
