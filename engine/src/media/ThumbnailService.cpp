// =============================================================================
//  Aurea / media / ThumbnailService.cpp
// =============================================================================
#include "aurea/media/ThumbnailService.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {

namespace {

inline u8 to_u8(f32 v) noexcept {
    return static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/// Amostra de um plano (8 ou 16 bits com 10 bits no topo), normalizada 0..1.
inline f32 sample(const u8* plane, u32 stride, u32 x, u32 y, bool tenBit, u32 pixelStride, u32 component) noexcept {
    if (tenBit) {
        const u8* p = plane + static_cast<usize>(y) * stride + (static_cast<usize>(x) * pixelStride + component) * 2;
        const u16 v = static_cast<u16>(p[0] | (p[1] << 8));
        return static_cast<f32>(v >> 6) / 1023.0f;
    }
    return static_cast<f32>(plane[static_cast<usize>(y) * stride + static_cast<usize>(x) * pixelStride + component]) / 255.0f;
}

} // namespace

bool frame_to_thumbnail(const DecodedFrame& f, u32 height, ThumbnailService::Image& out) {
    if (f.planeCount < 2 || !f.planes[0] || !f.planes[1] || f.width == 0 || f.height == 0 || height == 0) return false;
    const bool threePlane = f.format == PixelFormat::YUV420P;
    if (threePlane && !f.planes[2]) return false;
    const bool tenBit = f.format == PixelFormat::P010;
    const bool nv21 = f.format == PixelFormat::NV21;

    const u32 vw = f.visibleWidth ? f.visibleWidth : f.width;
    const u32 vh = f.visibleHeight ? f.visibleHeight : f.height;
    const bool sideways = f.rotation == 90 || f.rotation == 270;
    const u32 dispW = sideways ? vh : vw;
    const u32 dispH = sideways ? vw : vh;
    out.height = height;
    out.width = std::max<u32>(1, static_cast<u32>(std::lround(static_cast<f64>(height) * dispW / dispH)));
    out.rgba.assign(static_cast<usize>(out.width) * out.height * 4, 255);

    f32 kr = 0.2126f, kb = 0.0722f;
    f.color.coefficients(kr, kb);
    const f32 kg = 1.0f - kr - kb;
    const bool full = f.color.fullRange;

    for (u32 oy = 0; oy < out.height; ++oy) {
        for (u32 ox = 0; ox < out.width; ++ox) {
            // Centro do pixel de saída em coordenadas de exibição (0..1).
            const f32 u = (static_cast<f32>(ox) + 0.5f) / static_cast<f32>(out.width);
            const f32 v = (static_cast<f32>(oy) + 0.5f) / static_cast<f32>(out.height);
            // Exibição → quadro codificado (desfaz a rotação do container).
            f32 su = u, sv = v;
            switch (f.rotation) {
                case 90:  su = v;        sv = 1.0f - u; break;
                case 180: su = 1.0f - u; sv = 1.0f - v; break;
                case 270: su = 1.0f - v; sv = u;        break;
                default: break;
            }
            const u32 sx = std::min(f.width - 1, f.cropLeft + static_cast<u32>(su * static_cast<f32>(vw)));
            const u32 sy = std::min(f.height - 1, f.cropTop + static_cast<u32>(sv * static_cast<f32>(vh)));
            const f32 y = sample(f.planes[0], f.strides[0], sx, sy, tenBit, 1, 0);
            f32 cb, cr;
            const u32 cx = sx / 2, cy = sy / 2;
            if (threePlane) {
                cb = sample(f.planes[1], f.strides[1], cx, cy, false, 1, 0);
                cr = sample(f.planes[2], f.strides[2], cx, cy, false, 1, 0);
            } else {
                const f32 c0 = sample(f.planes[1], f.strides[1], cx, cy, tenBit, 2, 0);
                const f32 c1 = sample(f.planes[1], f.strides[1], cx, cy, tenBit, 2, 1);
                cb = nv21 ? c1 : c0;
                cr = nv21 ? c0 : c1;
            }
            f32 yy = y, pb = cb - 0.5f, pr = cr - 0.5f;
            if (!full) {
                yy = (y - 16.0f / 255.0f) * (255.0f / 219.0f);
                pb *= 255.0f / 224.0f;
                pr *= 255.0f / 224.0f;
            }
            const f32 r = yy + 2.0f * (1.0f - kr) * pr;
            const f32 b = yy + 2.0f * (1.0f - kb) * pb;
            const f32 g = (yy - kr * r - kb * b) / kg;
            u8* px = out.rgba.data() + (static_cast<usize>(oy) * out.width + ox) * 4;
            px[0] = to_u8(r);
            px[1] = to_u8(g);
            px[2] = to_u8(b);
            px[3] = 255;
        }
    }
    return true;
}

ThumbnailService::~ThumbnailService() { stop(); }

void ThumbnailService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
}

void ThumbnailService::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    decoders_.clear();
}

void ThumbnailService::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    pending_.clear();
    failedAssets_.clear();
    index_.clear();
    lru_.clear();
    generation_.fetch_add(1, std::memory_order_acq_rel);
}

u32 ThumbnailService::cached() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<u32>(lru_.size());
}

void ThumbnailService::insert_locked(const Key& k, Image img) {
    if (auto it = index_.find(k); it != index_.end()) {
        lru_.erase(it->second);
        index_.erase(it);
    }
    lru_.emplace_front(k, std::move(img));
    index_[k] = lru_.begin();
    while (lru_.size() > kMaxCached) {
        index_.erase(lru_.back().first);
        lru_.pop_back();
    }
}

bool ThumbnailService::video(u64 assetKey, const Asset& asset, i64 timeUs, u32 height, Image& out) {
    const Key k{assetKey, std::max<i64>(0, timeUs) / kBucketUs, height};
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = index_.find(k); it != index_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second);
        out = it->second->second;
        return true;
    }
    if (!pending_.contains(k) && !failedAssets_.contains(assetKey) && factory_ && running_) {
        pending_[k] = true;
        queue_.push_front(Request{k, asset});
        while (queue_.size() > kMaxQueued) {
            pending_.erase(queue_.back().key);
            queue_.pop_back();
        }
        wake_.notify_one();
    }
    return false;
}

bool ThumbnailService::image(u64 assetKey, const u8* rgba, u32 width, u32 height, u32 thumbHeight, Image& out) {
    const Key k{assetKey, -1, thumbHeight};
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = index_.find(k); it != index_.end()) {
        out = it->second->second;
        return true;
    }
    if (!rgba || width == 0 || height == 0 || thumbHeight == 0) return false;
    Image img;
    img.height = thumbHeight;
    img.width = std::max<u32>(1, static_cast<u32>(std::lround(static_cast<f64>(thumbHeight) * width / height)));
    img.rgba.resize(static_cast<usize>(img.width) * img.height * 4);
    for (u32 y = 0; y < img.height; ++y) {
        const u32 sy = std::min(height - 1, static_cast<u32>((y + 0.5) * height / img.height));
        for (u32 x = 0; x < img.width; ++x) {
            const u32 sx = std::min(width - 1, static_cast<u32>((x + 0.5) * width / img.width));
            const u8* s = rgba + (static_cast<usize>(sy) * width + sx) * 4;
            u8* d = img.rgba.data() + (static_cast<usize>(y) * img.width + x) * 4;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
        }
    }
    insert_locked(k, img);
    out = std::move(img);
    return true;
}

bool ThumbnailService::decode(const Request& r, Image& out) {
    // Um decoder por asset, reaproveitado (abrir um codec custa dezenas de ms).
    auto it = std::find_if(decoders_.begin(), decoders_.end(), [&](const Decoder& d) { return d.asset == r.key.asset; });
    if (it == decoders_.end()) {
        auto backend = factory_->open_video(r.asset, MediaPriority::Thumbnail);
        if (!backend) {
            std::lock_guard<std::mutex> lock(mutex_);
            failedAssets_[r.key.asset] = true;
            return false;
        }
        if (decoders_.size() >= 2) decoders_.erase(decoders_.begin());
        decoders_.push_back(Decoder{r.key.asset, std::move(backend)});
        it = decoders_.end() - 1;
    }
    VideoDecoderBackend& dec = *it->backend;
    const i64 target = r.key.bucket * kBucketUs;
    if (!dec.seek_to_keyframe(target).ok()) return false;
    // O primeiro frame depois do seek (o quadro-chave): aproximado de
    // propósito. Decodificar até o frame exato custaria um GOP inteiro.
    for (int i = 0; i < 4; ++i) {
        FrameRef frame;
        i64 pts = 0;
        bool eos = false;
        if (!dec.next_frame(-1, frame, pts, eos).ok()) return false;
        if (frame) return frame_to_thumbnail(*frame.get(), r.key.height, out);
        if (eos) return false;
    }
    return false;
}

void ThumbnailService::thread_main() noexcept {
    set_current_thread_name("aurea-thumbs");
    set_current_thread_priority(ThreadPriority::Background);
    for (;;) {
        Request req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return !running_ || !queue_.empty(); });
            if (!running_) break;
            req = std::move(queue_.front());
            queue_.pop_front();
        }
        Image img;
        const bool ok = decode(req, img);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.erase(req.key);
            if (ok) insert_locked(req.key, std::move(img));
        }
        if (ok) generation_.fetch_add(1, std::memory_order_acq_rel);
    }
}

} // namespace aurea
