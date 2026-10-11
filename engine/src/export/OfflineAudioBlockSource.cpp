#include "aurea/export/OfflineAudioBlockSource.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <new>

namespace aurea {

OfflineAudioBlockSource::OfflineAudioBlockSource(audio::AudioBlockCache& cache,
    const std::atomic<bool>& cancel, std::atomic<u64>* heartbeat, u64 budget) noexcept
    : cache_(cache), cancel_(cancel), heartbeat_(heartbeat), budget_(budget) {}

void OfflineAudioBlockSource::begin_chunk() noexcept {
    for (usize index = 0; index < count_; ++index) held_[index].value.reset();
    count_ = 0;
    bytes_ = 0;
    status_ = OkStatus;
}

const audio::AudioBlock* OfflineAudioBlockSource::block(u64 asset, i64 index) {
    if (cancel_.load(std::memory_order_acquire)) { status_ = Errc::Cancelled; return nullptr; }
    if (!status_.ok()) return nullptr;
    for (usize position = 0; position < count_; ++position) {
        const auto& held = held_[position];
        if (held.asset == asset && held.index == index) return held.value.get();
    }
    if (count_ == held_.size()) {
        status_ = Status{Errc::BudgetExceeded, "fontes de audio excedem o teto de leases offline"};
        return nullptr;
    }
    try {
        auto value = cache_.fetch(asset, index, &cancel_);
        if (!value) {
            status_ = cancel_.load(std::memory_order_acquire) ? Status{Errc::Cancelled}
                : Status{Errc::DecodeFailed, "nao foi possivel decodificar o audio solicitado"};
            return nullptr;
        }
        if (heartbeat_) heartbeat_->store(monotonic_ns(), std::memory_order_release);
        const u64 bytes = static_cast<u64>(value->pcm.capacity()) * sizeof(f32);
        if (bytes > budget_ - bytes_) {
            status_ = Status{Errc::BudgetExceeded, "fontes de audio do trecho excedem o teto de memoria offline"};
            return nullptr;
        }
        bytes_ += bytes;
        peak_ = std::max(peak_, bytes_);
        held_[count_++] = Held{asset, index, std::move(value)};
        return held_[count_ - 1].value.get();
    } catch (const std::bad_alloc&) {
        status_ = Status{Errc::OutOfMemory, "memoria dos blocos de audio offline"};
    } catch (...) {
        status_ = Status{Errc::DecodeFailed, "falha ao decodificar bloco de audio offline"};
    }
    return nullptr;
}

} // namespace aurea
