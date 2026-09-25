#include "aurea/media/MediaManager.hpp"
#include "aurea/core/Log.hpp"

#include <cstring>
#include <filesystem>
#include <chrono>

namespace aurea {

std::string VideoSourceFactory::cache_identity(const char* sourcePath) {
    if (!sourcePath || !*sourcePath) return {};
    if (std::strncmp(sourcePath, "file://", 7) == 0) sourcePath += 7;
    std::error_code error;
    const auto path = std::filesystem::u8path(sourcePath);
    const auto size = std::filesystem::file_size(path, error);
    if (error) return {};
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) return {};
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
    return std::to_string(size) + ':' + std::to_string(static_cast<i64>(ns));
}

MediaManager::~MediaManager() {
    proxies_.stop();
    close_all();
    {
        std::lock_guard<std::mutex> lock(retireMutex_);
        retireStop_ = true;
    }
    retireWake_.notify_all();
    if (retireThread_.joinable()) retireThread_.join();
}

void MediaManager::retire_locked(Entry&& entry) {
    if (entry.source) {
        entry.source->set_ready_callback(nullptr, nullptr);
        entry.source->suspend();
    }
    {
        std::lock_guard<std::mutex> lock(retireMutex_);
        retired_.push_back(std::move(entry));
        if (!retireThread_.joinable()) retireThread_ = std::thread([this] { retire_main(); });
    }
    retireWake_.notify_all();
}

void MediaManager::retire_main() {
    for (;;) {
        Entry closing;
        {
            std::unique_lock<std::mutex> lock(retireMutex_);
            retireWake_.wait(lock, [this] { return retireStop_ || !retired_.empty(); });
            if (retired_.empty() && retireStop_) return;
            closing = std::move(retired_.front());
            retired_.pop_front();
            retiring_ = true;
        }
        // Joins decode/open and releases platform buffers outside both locks.
        closing = Entry{};
        void (*ready)(void*) = nullptr;
        void* context = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready = readyFn_; context = readyCtx_;
        }
        {
            std::lock_guard<std::mutex> lock(retireMutex_);
            retiring_ = false;
            retireNotifying_ = true;
        }
        if (ready) ready(context);
        {
            std::lock_guard<std::mutex> lock(retireMutex_);
            retireNotifying_ = false;
        }
        retireWake_.notify_all();
    }
}

void MediaManager::drain_retired() {
    std::unique_lock<std::mutex> lock(retireMutex_);
    retireWake_.wait(lock, [this] { return retired_.empty() && !retiring_ && !retireNotifying_; });
}

void MediaManager::set_factory(VideoSourceFactory* factory) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    factory_ = factory;
}

void MediaManager::set_memory(MemoryManager* memory) noexcept {
    drain_retired();
    std::lock_guard<std::mutex> lock(mutex_);
    memory_ = memory;
    for (Entry& e : entries_) if (e.source) e.source->cache().attach(memory);
}

VideoSource* MediaManager::source_for(LayerId layer, AssetId assetId, const Asset& asset,
                                      u64 frameNumber, bool finalQuality) {
    auto proxy = finalQuality ? std::shared_ptr<const PreviewProxy>{} : proxies_.request(asset);
    Asset decodeAsset = asset;
    if (proxy) {
        decodeAsset.sourcePath = proxy->path;
        decodeAsset.video.width = proxy->width; decodeAsset.video.height = proxy->height;
        decodeAsset.profile.bitDepth = 8;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (usize i = 0; i < entries_.size();) {
        if (entries_[i].layer == layer && (entries_[i].asset != assetId || entries_[i].path != decodeAsset.sourcePath)) {
            retire_locked(std::move(entries_[i]));
            entries_[i] = std::move(entries_.back()); entries_.pop_back();
        } else ++i;
    }
    for (Entry& e : entries_) {
        if (e.layer == layer && e.asset == assetId) {
            e.lastUsedFrame = frameNumber;
            if (e.opening && e.opening->ready.load(std::memory_order_acquire)) {
                auto backend = std::move(e.opening->decoder);
                e.opening.reset();
                if (!backend) e.failed = true;
                else {
                    e.source = std::make_unique<VideoSource>(std::move(backend), MediaPriority::Preview);
                    e.source->cache().attach(memory_);
                    e.source->set_ready_callback(readyFn_, readyCtx_);
                    e.source->start();
                    if (suspended_) e.source->suspend();
                }
            }
            return e.failed ? nullptr : e.source.get();
        }
    }
    if (!factory_) return nullptr;

    {
        std::lock_guard<std::mutex> closing(retireMutex_);
        // Do not accumulate replacement codecs while an old platform session
        // is stuck closing. Completion wakes the renderer to retry admission.
        if (retiring_ || !retired_.empty()) return nullptr;
    }

    Entry e;
    e.layer = layer;
    e.asset = assetId;
    e.path = decodeAsset.sourcePath;
    e.lastUsedFrame = frameNumber;
    e.opening = std::make_unique<Opening>();
    auto* pending = e.opening.get();
    auto* factory = factory_;
    auto ready = readyFn_;
    auto* context = readyCtx_;
    pending->worker = std::thread([pending, factory, asset, decodeAsset, proxy, ready, context] {
        pending->decoder = factory->open_video(decodeAsset, MediaPriority::Preview);
        if (proxy) {
            pending->decoder = proxy_decoder(std::move(pending->decoder), proxy);
            if (!pending->decoder) pending->decoder = factory->open_video(asset, MediaPriority::Preview);
        }
        pending->ready.store(true, std::memory_order_release);
        if (ready) ready(context);
    });
    entries_.push_back(std::move(e));
    return nullptr;
}

void MediaManager::collect(u64 frameNumber, u32 idleFrames) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (usize i = 0; i < entries_.size();) {
            if (frameNumber > entries_[i].lastUsedFrame + idleFrames) {
                retire_locked(std::move(entries_[i]));
                entries_[i] = std::move(entries_.back());
                entries_.pop_back();
                continue;
            }
            ++i;
        }
    }
}

void MediaManager::close_layer(LayerId layer) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (usize i = 0; i < entries_.size();) {
            if (entries_[i].layer == layer) {
                retire_locked(std::move(entries_[i]));
                entries_[i] = std::move(entries_.back());
                entries_.pop_back();
                continue;
            }
            ++i;
        }
    }
}

void MediaManager::close_all() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (Entry& entry : entries_) retire_locked(std::move(entry));
        entries_.clear();
    }
    drain_retired();
}

void MediaManager::suspend_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    suspended_ = true;
    for (Entry& e : entries_) if (e.source) e.source->suspend();
}

void MediaManager::resume_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    suspended_ = false;
    for (Entry& e : entries_) if (e.source) e.source->resume();
}

void MediaManager::set_ready_callback(void (*fn)(void*), void* ctx) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    readyFn_ = fn;
    readyCtx_ = ctx;
    for (Entry& e : entries_) if (e.source) e.source->set_ready_callback(fn, ctx);
}

MediaManager::Stats MediaManager::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    f32 decodeSum = 0.0f;
    u32 decodeN = 0;
    for (const Entry& e : entries_) {
        if (!e.source) continue;
        const VideoSource::Stats vs = e.source->stats();
        ++s.sources;
        s.cachedFrames += vs.cache.frames;
        s.cachedBytes += vs.cache.bytes;
        s.seeks += vs.seeks;
        s.coalesced += vs.coalesced;
        s.discarded += vs.framesDiscarded;
        if (vs.decodeMsAvg > 0.0f) { decodeSum += vs.decodeMsAvg; ++decodeN; }
        if (vs.lastSeekMs > s.lastSeekMs) s.lastSeekMs = vs.lastSeekMs;
        const VideoStreamInfo& info = e.source->info();
        s.hardwareDecoder = s.hardwareDecoder || info.hardwareDecoder;
        if (!s.decoderName[0]) std::memcpy(s.decoderName, info.decoderName, sizeof(s.decoderName));
    }
    s.decodeMsAvg = decodeN ? decodeSum / static_cast<f32>(decodeN) : 0.0f;
    return s;
}

} // namespace aurea
