#include "aurea/media/MediaManager.hpp"
#include "aurea/core/Log.hpp"

#include <cstring>

namespace aurea {

MediaManager::~MediaManager() { close_all(); }

void MediaManager::set_factory(VideoSourceFactory* factory) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    factory_ = factory;
}

VideoSource* MediaManager::source_for(LayerId layer, AssetId assetId, const Asset& asset,
                                      u64 frameNumber) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Entry& e : entries_) {
        if (e.layer == layer && e.asset == assetId) {
            e.lastUsedFrame = frameNumber;
            return e.failed ? nullptr : e.source.get();
        }
    }
    if (!factory_) return nullptr;

    Entry e;
    e.layer = layer;
    e.asset = assetId;
    e.lastUsedFrame = frameNumber;
    std::unique_ptr<VideoDecoderBackend> backend = factory_->open_video(asset, MediaPriority::Preview);
    if (!backend) {
        // Guarda a falha: sem isso, o motor tentaria abrir o arquivo de novo a
        // cada frame, e cada tentativa custa uma sondagem do container.
        AUREA_LOG_ERROR("nao foi possivel abrir o video '%s'", asset.name.c_str());
        e.failed = true;
        entries_.push_back(std::move(e));
        return nullptr;
    }
    e.source = std::make_unique<VideoSource>(std::move(backend), MediaPriority::Preview);
    e.source->set_ready_callback(readyFn_, readyCtx_);
    e.source->start();
    if (suspended_) e.source->suspend();
    VideoSource* raw = e.source.get();
    entries_.push_back(std::move(e));
    return raw;
}

void MediaManager::collect(u64 frameNumber, u32 idleFrames) {
    std::vector<std::unique_ptr<VideoSource>> closing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (usize i = 0; i < entries_.size();) {
            if (frameNumber > entries_[i].lastUsedFrame + idleFrames) {
                closing.push_back(std::move(entries_[i].source));
                entries_[i] = std::move(entries_.back());
                entries_.pop_back();
                continue;
            }
            ++i;
        }
    }
    // Fecha FORA do lock: parar a thread de decode espera ela terminar o frame
    // em curso, e ninguém mais pode ficar travado por isso.
    closing.clear();
}

void MediaManager::close_layer(LayerId layer) {
    std::vector<std::unique_ptr<VideoSource>> closing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (usize i = 0; i < entries_.size();) {
            if (entries_[i].layer == layer) {
                closing.push_back(std::move(entries_[i].source));
                entries_[i] = std::move(entries_.back());
                entries_.pop_back();
                continue;
            }
            ++i;
        }
    }
}

void MediaManager::close_all() {
    std::vector<Entry> closing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing.swap(entries_);
    }
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
