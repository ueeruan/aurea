#pragma once

#include "aurea/audio/Audio.hpp"

namespace aurea {

/// Session-owned offline audio graph. Composition buses are rendered in their
/// own clocks, then read through their parent's exact source-time mapping.
/// Does not mutate the frozen project or the playback snapshots.
class OfflineAudioSnapshot final {
public:
    static constexpr u64 kDefaultBlockBudget = 16ull << 20;
    [[nodiscard]] static Result<std::unique_ptr<OfflineAudioSnapshot>> create(
        const Composition& root, const Project& project, audio::AudioBlockCache* cache,
        audio::AssetPathResolver resolver, void* resolverUser,
        u64 blockBudget = kDefaultBlockBudget,
        const std::atomic<bool>* cancel = nullptr) noexcept;
    ~OfflineAudioSnapshot();
    OfflineAudioSnapshot(const OfflineAudioSnapshot&) = delete;
    OfflineAudioSnapshot& operator=(const OfflineAudioSnapshot&) = delete;

    [[nodiscard]] const std::shared_ptr<audio::AudioMixSnapshot>& snapshot() const noexcept;
    /// BlockSource pointers remain alive throughout this call, including nested
    /// effects preroll. A missing block is an error; no partial PCM is accepted.
    [[nodiscard]] Status mix(i64 start, u32 frames, audio::BlockSource& source,
                             f32* stereo, audio::MixStats* stats = nullptr,
                             audio::MixState* state = nullptr,
                             const std::atomic<bool>* cancel = nullptr,
                             std::atomic<u64>* heartbeat = nullptr) noexcept;
    struct Stats {
        u64 blockBytes = 0;
        u64 peakBlockBytes = 0;
        u64 renderedBlocks = 0;
        u32 compositionInstances = 0;
    };
    [[nodiscard]] Stats stats() const noexcept;
private:
    struct Impl;
    explicit OfflineAudioSnapshot(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace aurea
