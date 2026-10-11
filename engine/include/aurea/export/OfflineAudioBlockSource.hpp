#pragma once

#include "aurea/audio/Audio.hpp"
#include <array>

namespace aurea {

/// Every real PCM block stays alive until a mix chunk ends, even when visual
/// audio effects evict it from the shared cache. Exclusive to offline export.
class OfflineAudioBlockSource final : public audio::BlockSource {
public:
    static constexpr u64 kDefaultBudget = 16ull << 20;
    OfflineAudioBlockSource(audio::AudioBlockCache& cache, const std::atomic<bool>& cancel,
                            std::atomic<u64>* heartbeat = nullptr,
                            u64 budget = kDefaultBudget) noexcept;
    void begin_chunk() noexcept;
    const audio::AudioBlock* block(u64 asset, i64 index) override;
    [[nodiscard]] Status status() const noexcept { return status_; }
    [[nodiscard]] u64 leased_bytes() const noexcept { return bytes_; }
    [[nodiscard]] u64 peak_leased_bytes() const noexcept { return peak_; }
private:
    struct Held {
        u64 asset = 0;
        i64 index = 0;
        std::shared_ptr<const audio::AudioBlock> value;
    };
    audio::AudioBlockCache& cache_;
    const std::atomic<bool>& cancel_;
    std::atomic<u64>* heartbeat_;
    const u64 budget_;
    std::array<Held, 512> held_{};
    usize count_ = 0;
    u64 bytes_ = 0, peak_ = 0;
    Status status_;
};

} // namespace aurea
