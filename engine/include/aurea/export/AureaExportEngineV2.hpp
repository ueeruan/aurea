#pragma once
#include "aurea/core/Result.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace aurea {
enum class ExportPhase : u32 {
    Preparing = 1, Rendering, Encoding, Finalizing, Validating,
    Completed, Failed, Cancelled, Recovering
};
inline constexpr u32 kExportPhaseShift = 8, kExportPhaseMask = 15u << kExportPhaseShift;

// Independent offline-session coordinator. Slots remain renderer-owned until
// their GPU fence and CPU invalidation complete; ownership transfers exactly
// once to the encoder and back only after video AND audio accept that frame.
// Native API calls remain on the platform worker, never under this mutex.
class AureaExportEngineV2 final {
public:
    Status prepare(u32 slots, u32 frames, const std::atomic<bool>* cancellation) noexcept;
    Result<u32> acquire(u32 admission) noexcept;
    Status publish(u32 slot, u32 frame) noexcept;
    Result<u32> take() noexcept;
    Status encoded(u32 slot, u32 frame) noexcept;
    void end_render(Status result) noexcept;
    Status begin_finalizing() noexcept;
    Status begin_validating() noexcept;
    void validated(Status result) noexcept;
    void fail(Status result) noexcept;
    [[nodiscard]] ExportPhase phase() const noexcept { return phase_.load(); }
    [[nodiscard]] u32 completed_frames() const noexcept { return completed_.load(); }
    [[nodiscard]] bool drained() const noexcept;
    [[nodiscard]] bool cancelled() const noexcept;
private:
    enum class Owner : u8 { Free, Renderer, Ready, Encoder };
    void transition(ExportPhase phase) noexcept;
    std::array<Owner, 4> owners_{};
    std::array<u32, 4> queue_{}, frame_{};
    u32 capacity_ = 0, total_ = 0, ready_ = 0, head_ = 0, published_ = 0;
    bool ended_ = false;
    Errc failure_ = Errc::Ok;
    const std::atomic<bool>* cancellation_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<ExportPhase> phase_{ExportPhase::Preparing};
    std::atomic<u32> completed_{0};
};
} // namespace aurea
