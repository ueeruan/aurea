#include "aurea/export/OfflineFrameQueue.hpp"
#include <algorithm>
#include <chrono>

namespace aurea {
bool OfflineFrameQueue::cancelled() const noexcept { return cancel_ && cancel_->load(std::memory_order_acquire); }
Status OfflineFrameQueue::prepare(u32 slots, u32 frames, u64 bytes, u64 limit,
    const std::atomic<bool>* cancel) noexcept {
    std::lock_guard lock(mutex_);
    if (capacity_ || !slots || slots > entries_.size() || !frames || !bytes) return Errc::InvalidArgument;
    if (bytes > limit / slots) return Errc::BudgetExceeded;
    capacity_ = slots; total_ = frames; reservedBytes_ = bytes * slots; cancel_ = cancel;
    phase_.store(ExportPhase::Rendering);
    return OkStatus;
}
Result<u32> OfflineFrameQueue::acquire(u32 admission) noexcept {
    std::unique_lock lock(mutex_);
    if (!capacity_) return Status{Errc::InvalidState};
    auto find = [&]() -> u32 {
        u32 occupied = 0, free = capacity_;
        for (u32 i = 0; i < capacity_; ++i) {
            if (entries_[i].lease != Lease::Free) ++occupied; else free = i;
        }
        return occupied < std::clamp(admission, 1u, capacity_) ? free : capacity_;
    };
    changed_.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return find() != capacity_ || cancelled() || closed_ || error_ != Errc::Ok;
    });
    if (cancelled()) return Status{Errc::Cancelled};
    if (error_ != Errc::Ok) return Status{error_};
    if (closed_) return Status{Errc::InvalidState};
    const u32 slot = find();
    if (slot == capacity_) return Status{Errc::Timeout};
    entries_[slot].lease = Lease::Rendering;
    return slot;
}
Status OfflineFrameQueue::publish(u32 slot, u32 frame) noexcept {
    std::lock_guard lock(mutex_);
    if (cancelled()) return Status{Errc::Cancelled};
    if (error_ != Errc::Ok) return Status{error_};
    if (closed_ || slot >= capacity_ || entries_[slot].lease != Lease::Rendering
        || frame != nextPublish_ || frame >= total_) return Errc::InvalidState;
    entries_[slot] = {Lease::Queued, frame}; ++nextPublish_;
    changed_.notify_all(); return OkStatus;
}
Result<u32> OfflineFrameQueue::take() noexcept {
    std::unique_lock lock(mutex_);
    auto find = [&]() -> u32 {
        for (u32 i = 0; i < capacity_; ++i)
            if (entries_[i].lease == Lease::Queued && entries_[i].frame == nextEncode_) return i;
        return capacity_;
    };
    changed_.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return find() != capacity_ || closed_ || cancelled() || error_ != Errc::Ok;
    });
    if (cancelled()) return Status{Errc::Cancelled};
    if (error_ != Errc::Ok) return Status{error_};
    const u32 slot = find();
    if (slot == capacity_) return Status{closed_ ? Errc::InvalidState : Errc::Timeout};
    entries_[slot].lease = Lease::Encoding;
    phase_.store(ExportPhase::Encoding);
    return slot;
}
Status OfflineFrameQueue::encoded(u32 slot, u32 frame) noexcept {
    std::lock_guard lock(mutex_);
    if (slot >= capacity_ || entries_[slot].lease != Lease::Encoding || entries_[slot].frame != frame
        || frame != nextEncode_) return Errc::InvalidState;
    entries_[slot].lease = Lease::Free; ++nextEncode_;
    changed_.notify_all(); return OkStatus;
}
void OfflineFrameQueue::end_render(Status result) noexcept {
    std::lock_guard lock(mutex_); closed_ = true;
    if (result.ok() && nextPublish_ != total_) result = Errc::InvalidState;
    if (!result.ok()) {
        error_ = result.code();
        phase_.store(result.code() == Errc::Cancelled || cancelled() ? ExportPhase::Cancelled : ExportPhase::Failed);
    }
    changed_.notify_all();
}
bool OfflineFrameQueue::drained() const noexcept {
    std::lock_guard lock(mutex_);
    return closed_ && error_ == Errc::Ok && nextPublish_ == total_ && nextEncode_ == total_;
}
Status OfflineFrameQueue::begin_finalizing() noexcept {
    std::lock_guard lock(mutex_);
    if (cancelled()) return Errc::Cancelled;
    if (error_ != Errc::Ok) return error_;
    if (!closed_ || nextEncode_ != total_ || nextPublish_ != total_) return Errc::InvalidState;
    phase_.store(ExportPhase::Finalizing); return OkStatus;
}
Status OfflineFrameQueue::begin_validating() noexcept {
    if (cancelled()) return Errc::Cancelled;
    if (phase_.load() != ExportPhase::Finalizing) return Errc::InvalidState;
    phase_.store(ExportPhase::Validating); return OkStatus;
}
void OfflineFrameQueue::validated(Status result) noexcept {
    std::lock_guard lock(mutex_);
    if (result.ok() && phase_.load() != ExportPhase::Validating) result = Errc::InvalidState;
    if (result.ok() && cancelled()) result = Errc::Cancelled;
    error_ = result.code();
    phase_.store(result.ok() ? ExportPhase::Completed : result.code() == Errc::Cancelled ? ExportPhase::Cancelled : ExportPhase::Failed);
}
void OfflineFrameQueue::fail(Status result) noexcept { if (!result.ok()) end_render(result); }
} // namespace aurea
