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

// GPU readbacks are allocated once. This queue transfers leases, never pixels;
// one leased slot cannot be reused until both codec input and PCM are accepted.
class OfflineFrameQueue final {
public:
    /// `first` > 0: export retomado, os quadros anteriores já estão no registro.
    Status prepare(u32 slots, u32 frames, u64 bytesPerSlot, u64 byteLimit,
                   const std::atomic<bool>* cancellation, u32 first = 0) noexcept;
    /// Espera até `waitMs` por um slot livre (0 = só olha).
    Result<u32> acquire(u32 admission, u32 waitMs = 100) noexcept;
    Status publish(u32 slot, u32 frame) noexcept;
    Result<u32> take() noexcept;
    Status encoded(u32 slot, u32 frame) noexcept;
    void end_render(Status result) noexcept;
    Status begin_finalizing() noexcept;
    Status begin_validating() noexcept;
    void validated(Status result) noexcept;
    void fail(Status result) noexcept;
    bool drained() const noexcept;
    ExportPhase phase() const noexcept { return phase_.load(std::memory_order_acquire); }
    u64 reserved_bytes() const noexcept { return reservedBytes_; }
private:
    enum class Lease : u8 { Free, Rendering, Queued, Encoding };
    struct Entry { Lease lease = Lease::Free; u32 frame = 0; };
    bool cancelled() const noexcept;
    std::array<Entry, 4> entries_{};
    u32 capacity_ = 0, total_ = 0, nextPublish_ = 0, nextEncode_ = 0;
    u64 reservedBytes_ = 0;
    bool closed_ = false;
    Errc error_ = Errc::Ok;
    const std::atomic<bool>* cancel_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<ExportPhase> phase_{ExportPhase::Preparing};
};
} // namespace aurea
