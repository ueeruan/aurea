#include "aurea/export/AureaExportEngineV2.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include <algorithm>
#include <chrono>

namespace aurea {
void AureaExportEngineV2::transition(ExportPhase next) noexcept {
    const auto previous = phase_.exchange(next);
    if (previous != next) AUREA_LOG_INFO("export-v2 timestamp_ns=%llu phase=%u frame=%u/%u",
        static_cast<unsigned long long>(monotonic_ns()), static_cast<u32>(next), completed_.load(), total_);
}
bool AureaExportEngineV2::cancelled() const noexcept {
    return cancellation_ && cancellation_->load(std::memory_order_acquire);
}
Status AureaExportEngineV2::prepare(u32 slots, u32 frames, const std::atomic<bool>* cancellation) noexcept {
    std::lock_guard lock(mutex_);
    if (capacity_ || !slots || slots > owners_.size() || !frames) return Status{Errc::InvalidArgument};
    capacity_ = slots; total_ = frames; cancellation_ = cancellation;
    owners_.fill(Owner::Free);
    transition(ExportPhase::Rendering);
    return OkStatus;
}
Result<u32> AureaExportEngineV2::acquire(u32 admission) noexcept {
    std::unique_lock lock(mutex_);
    auto available = [&]() {
        const auto free = std::count(owners_.begin(), owners_.begin() + capacity_, Owner::Free);
        return free > 0 && capacity_ - static_cast<u32>(free) < std::clamp(admission, 1u, capacity_);
    };
    if (!capacity_) return Status{Errc::InvalidState};
    changed_.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return cancelled() || failure_ != Errc::Ok || ended_ || available();
    });
    if (cancelled()) return Status{Errc::Cancelled};
    if (failure_ != Errc::Ok) return Status{failure_};
    if (ended_) return Status{Errc::InvalidState};
    if (!available()) return Status{Errc::Timeout}; // caller's native-worker watchdog decides if this is a stall
    for (u32 i = 0; i < capacity_; ++i) if (owners_[i] == Owner::Free) {
        owners_[i] = Owner::Renderer; return i;
    }
    return Status{Errc::InvalidState};
}
Status AureaExportEngineV2::publish(u32 slot, u32 frame) noexcept {
    std::lock_guard lock(mutex_);
    if (cancelled()) return Status{Errc::Cancelled};
    if (failure_ != Errc::Ok) return Status{failure_};
    if (ended_ || slot >= capacity_ || owners_[slot] != Owner::Renderer || frame != published_ || frame >= total_)
        return Status{Errc::InvalidState, "export-v2: quadro fora de ordem ou buffer sem propriedade"};
    frame_[slot] = frame; owners_[slot] = Owner::Ready;
    queue_[(head_ + ready_) % capacity_] = slot; ++ready_; ++published_;
    changed_.notify_all(); return OkStatus;
}
Result<u32> AureaExportEngineV2::take() noexcept {
    std::unique_lock lock(mutex_);
    changed_.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return ready_ || ended_ || cancelled() || failure_ != Errc::Ok;
    });
    if (cancelled()) return Status{Errc::Cancelled};
    if (failure_ != Errc::Ok) return Status{failure_};
    if (!ready_) return Status{ended_ ? Errc::InvalidState : Errc::Timeout};
    const u32 slot = queue_[head_]; head_ = (head_ + 1) % capacity_; --ready_;
    owners_[slot] = Owner::Encoder;
    transition(ExportPhase::Encoding);
    return slot;
}
Status AureaExportEngineV2::encoded(u32 slot, u32 frame) noexcept {
    std::lock_guard lock(mutex_);
    if (slot >= capacity_ || owners_[slot] != Owner::Encoder || frame_[slot] != frame || frame != completed_.load())
        return Status{Errc::InvalidState, "export-v2: conclusao duplicada ou fora de ordem"};
    owners_[slot] = Owner::Free; completed_.store(frame + 1);
    changed_.notify_all(); return OkStatus;
}
void AureaExportEngineV2::end_render(Status result) noexcept {
    std::lock_guard lock(mutex_); ended_ = true;
    if (!result.ok()) { failure_ = result.code(); transition(cancelled() || result.code() == Errc::Cancelled
        ? ExportPhase::Cancelled : ExportPhase::Failed); }
    changed_.notify_all();
}
bool AureaExportEngineV2::drained() const noexcept {
    std::lock_guard lock(mutex_);
    return ended_ && failure_ == Errc::Ok && completed_.load() == total_ && ready_ == 0;
}
Status AureaExportEngineV2::begin_finalizing() noexcept {
    std::lock_guard lock(mutex_);
    if (cancelled()) return Status{Errc::Cancelled};
    if (failure_ != Errc::Ok) return Status{failure_};
    if (!ended_ || completed_.load() != total_ || ready_) return Status{Errc::InvalidState};
    transition(ExportPhase::Finalizing); return OkStatus;
}
Status AureaExportEngineV2::begin_validating() noexcept {
    std::lock_guard lock(mutex_);
    if (cancelled()) return Status{Errc::Cancelled};
    if (phase_.load() != ExportPhase::Finalizing) return Status{Errc::InvalidState};
    transition(ExportPhase::Validating); return OkStatus;
}
void AureaExportEngineV2::validated(Status result) noexcept {
    std::lock_guard lock(mutex_);
    if (phase_.load() != ExportPhase::Validating && result.ok()) result = Errc::InvalidState;
    if (cancelled() && result.ok()) result = Errc::Cancelled;
    failure_ = result.code();
    transition(result.ok() ? ExportPhase::Completed : result.code() == Errc::Cancelled
        ? ExportPhase::Cancelled : ExportPhase::Failed);
    changed_.notify_all();
}
void AureaExportEngineV2::fail(Status result) noexcept { if (!result.ok()) end_render(result); }
} // namespace aurea
