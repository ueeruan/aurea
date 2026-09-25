#include "aurea/media/PreviewProxy.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace aurea {
namespace {
namespace fs = std::filesystem;
constexpr u64 kMaxFrames = 2'000'000;
std::string fingerprint(const Asset& a, u32 target, const std::string& identity = {}) {
    std::string input = a.sourcePath + ':' + std::to_string(a.contentHash) + ':' + std::to_string(a.fileSizeBytes) + ':' + std::to_string(target);
    input += ':' + identity;
    u64 h = 1469598103934665603ull;
    for (unsigned char c : input) { h ^= c; h *= 1099511628211ull; }
    std::ostringstream out; out << std::hex << h;
    return out.str();
}

// Source coordinates at the output rectangle's edges, before container rotation.
void source_rect(const DecodedFrame& f, f64& x0, f64& y0, f64& x1, f64& y1) {
    const f64 a = x0, b = y0, c = x1, d = y1;
    if (f.rotation == 90) { x0 = b; x1 = d; y0 = 1 - c; y1 = 1 - a; }
    else if (f.rotation == 180) { x0 = 1 - c; x1 = 1 - a; y0 = 1 - d; y1 = 1 - b; }
    else if (f.rotation == 270) { x0 = 1 - d; x1 = 1 - b; y0 = a; y1 = c; }
    const u32 w = f.visibleWidth ? f.visibleWidth : f.width, h = f.visibleHeight ? f.visibleHeight : f.height;
    x0 = f.cropLeft + x0 * w; x1 = f.cropLeft + x1 * w;
    y0 = f.cropTop + y0 * h; y1 = f.cropTop + y1 * h;
}
u8 area(const DecodedFrame& f, u32 channel, f64 x0, f64 y0, f64 x1, f64 y1) {
    const bool ten = f.format == PixelFormat::P010, planar = f.format == PixelFormat::YUV420P;
    const u32 plane = channel == 0 ? 0 : planar ? channel : 1;
    const u32 component = channel == 0 || planar ? 0 : (f.format == PixelFormat::NV21 ? 2 - channel : channel - 1);
    const u32 step = (channel == 0 || planar ? 1 : 2) * (ten ? 2 : 1);
    if (channel) { x0 *= .5; x1 *= .5; y0 *= .5; y1 *= .5; }
    f64 sum = 0, weight = 0;
    for (u32 y = static_cast<u32>(y0); y < static_cast<u32>(std::ceil(y1)); ++y) {
        const f64 wy = std::min(y1, static_cast<f64>(y + 1)) - std::max(y0, static_cast<f64>(y));
        for (u32 x = static_cast<u32>(x0); x < static_cast<u32>(std::ceil(x1)); ++x) {
            const f64 w = wy * (std::min(x1, static_cast<f64>(x + 1)) - std::max(x0, static_cast<f64>(x)));
            const u8* p = f.planes[plane] + static_cast<usize>(y) * f.strides[plane] + static_cast<usize>(x) * step + component * (ten ? 2 : 1);
            const f64 value = ten ? static_cast<f64>((p[0] | (p[1] << 8)) >> 6) * (f.color.fullRange ? 255.0 / 1023.0 : .25) : p[0];
            sum += value * w; weight += w;
        }
    }
    return static_cast<u8>(std::clamp(std::lround(sum / std::max(weight, 1e-20)), 0l, 255l));
}

class ProxyDecoder final : public VideoDecoderBackend {
    std::unique_ptr<VideoDecoderBackend> decoder_;
    std::shared_ptr<const PreviewProxy> proxy_;
    VideoStreamInfo info_;
public:
    ProxyDecoder(std::unique_ptr<VideoDecoderBackend> d, std::shared_ptr<const PreviewProxy> p)
        : decoder_(std::move(d)), proxy_(std::move(p)), info_(decoder_->info()) {
        info_.durationUs = proxy_->original.durationUs; info_.fps = proxy_->original.fps;
        info_.rotation = 0; info_.color = proxy_->original.color; info_.color.bitDepth = 8;
        info_.preciseFrameTiming = true;
    }
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 us) noexcept override { return decoder_->seek_to_keyframe(std::max<i64>(0, us - proxy_->times.front())); }
    Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
        const auto status = decoder_->next_frame(from < 0 ? from : from - proxy_->times.front(), out, pts, eos);
        pts += proxy_->times.front();
        if (!status.ok() || !out) return status;
        auto it = std::lower_bound(proxy_->times.begin(), proxy_->times.end(), pts);
        if (it == proxy_->times.end() || (it != proxy_->times.begin() && pts - *(it - 1) < *it - pts)) --it;
        const usize index = static_cast<usize>(it - proxy_->times.begin());
        out->ptsUs = pts = *it; out->durationUs = proxy_->durations[index];
        const u8 decodedDepth = out->color.bitDepth;
        out->color = info_.color; out->color.bitDepth = decodedDepth;
        if (out->format == PixelFormat::RGBA8 || out->format == PixelFormat::RGBA16F) out->color.fullRange = true;
        out->rotation = 0;
        return status;
    }
    u32 max_live_frames() const noexcept override { return decoder_->max_live_frames(); }
    i64 keyframe_interval_us() const noexcept override { return decoder_->keyframe_interval_us(); }
    void suspend() noexcept override { decoder_->suspend(); }
    Status resume() noexcept override { return decoder_->resume(); }
};

bool write_manifest(const PreviewProxy& p, const std::string& path) {
    std::ofstream out(fs::u8path(path), std::ios::trunc);
    out << "AUREA_PROXY_1\n" << p.width << ' ' << p.height << ' ' << std::setprecision(17) << p.original.fps << ' ' << p.original.durationUs << '\n';
    const auto& c = p.original.color;
    out << static_cast<int>(c.matrix) << ' ' << c.fullRange << ' ' << static_cast<int>(c.primaries) << ' ' << static_cast<int>(c.transfer) << '\n';
    out << p.times.size() << '\n';
    for (usize i = 0; i < p.times.size(); ++i) out << p.times[i] << ' ' << p.durations[i] << '\n';
    out.close(); return out.good();
}
std::shared_ptr<PreviewProxy> read_manifest(const std::string& path) {
    std::ifstream in(fs::u8path(path + ".pts"));
    std::string magic; in >> magic;
    if (magic != "AUREA_PROXY_1") return {};
    auto p = std::make_shared<PreviewProxy>(); p->path = path;
    int matrix, full, primaries, transfer; u64 count;
    in >> p->width >> p->height >> p->original.fps >> p->original.durationUs >> matrix >> full >> primaries >> transfer >> count;
    if (!in || p->width < 2 || p->height < 2 || p->width > 8192 || p->height > 8192 ||
        !std::isfinite(p->original.fps) || p->original.fps <= 0 || count == 0 || count > kMaxFrames ||
        matrix < 0 || matrix > 2 || primaries < 0 || primaries > 3 || transfer < 0 || transfer > 3) return {};
    p->original.color.matrix = static_cast<YCbCrMatrix>(matrix); p->original.color.fullRange = full != 0;
    p->original.color.primaries = static_cast<ColorPrimaries>(primaries); p->original.color.transfer = static_cast<TransferFunction>(transfer);
    p->original.color.bitDepth = 8;
    for (u64 i = 0; i < count; ++i) {
        i64 time, duration; in >> time >> duration;
        if (!in || duration <= 0 || (i && time <= p->times.back())) return {};
        p->times.push_back(time); p->durations.push_back(duration);
    }
    return p;
}
} // namespace

bool proxy_frame_nv12(const DecodedFrame& f, u32 width, u32 height, std::vector<u8>& pixels) {
    const bool planar = f.format == PixelFormat::YUV420P, ten = f.format == PixelFormat::P010;
    if (f.format != PixelFormat::NV12 && f.format != PixelFormat::NV21 && !planar && !ten) return false;
    if (!width || !height || (width & 1) || (height & 1) || width > 8192 || height > 8192 ||
        !f.width || !f.height || f.planeCount < (planar ? 3u : 2u) || !f.planes[0] || !f.planes[1] || (planar && !f.planes[2])) return false;
    const u32 vw = f.visibleWidth ? f.visibleWidth : f.width, vh = f.visibleHeight ? f.visibleHeight : f.height;
    if (static_cast<u64>(f.cropLeft) + vw > f.width || static_cast<u64>(f.cropTop) + vh > f.height ||
        f.strides[0] < static_cast<u64>(f.width) * (ten ? 2 : 1) ||
        f.strides[1] < static_cast<u64>((f.width + 1) / 2) * (planar ? 1 : ten ? 4 : 2) ||
        (planar && f.strides[2] < (f.width + 1) / 2)) return false;
    pixels.resize(static_cast<usize>(width) * height * 3 / 2);
    for (u32 channel = 0; channel < 3; ++channel) {
        const u32 w = channel ? width / 2 : width, h = channel ? height / 2 : height;
        for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
            f64 x0 = static_cast<f64>(x) / w, x1 = static_cast<f64>(x + 1) / w;
            f64 y0 = static_cast<f64>(y) / h, y1 = static_cast<f64>(y + 1) / h;
            source_rect(f, x0, y0, x1, y1);
            const usize index = channel ? static_cast<usize>(width) * height + (static_cast<usize>(y) * w + x) * 2 + channel - 1
                                        : static_cast<usize>(y) * w + x;
            pixels[index] = area(f, channel, x0, y0, x1, y1);
        }
    }
    return true;
}

std::unique_ptr<VideoDecoderBackend> proxy_decoder(std::unique_ptr<VideoDecoderBackend> decoder, std::shared_ptr<const PreviewProxy> proxy) {
    if (!decoder || !proxy || proxy->times.empty() || proxy->times.size() != proxy->durations.size()) return {};
    return std::make_unique<ProxyDecoder>(std::move(decoder), std::move(proxy));
}

PreviewProxyService::~PreviewProxyService() { stop(); }
void PreviewProxyService::configure(VideoSourceFactory* media, ExportSinkFactory sink, void* context, std::string directory, u64 budget,
                                    std::string (*resolve)(const std::string&, void*), void* resolveContext) {
    stop();
    media_ = media; sink_ = sink; context_ = context; directory_ = std::move(directory); diskBudget_ = budget;
    resolve_ = resolve; resolveContext_ = resolveContext;
    stopping_.store(false);
    paused_.store(false);
    pauseReasons_ = 0;
    if (media_ && sink_ && !directory_.empty()) worker_ = std::thread([this] { run(); });
}
void PreviewProxyService::set_policy(u32 shortSide, u32 pacing) noexcept { shortSide_.store(shortSide); pacingMs_.store(pacing); }
void PreviewProxyService::set_paused(bool paused) noexcept {
    set_pause_reason(Manual, paused);
}
void PreviewProxyService::set_pause_reason(PauseReason reason, bool paused) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paused) pauseReasons_ |= reason; else pauseReasons_ &= ~static_cast<u32>(reason);
        const bool blocked = pauseReasons_ != 0;
        if (paused_.exchange(blocked) != blocked && blocked) {
            ++generation_;
            // Completed files remain reusable; interrupted work is requested again.
            queue_.clear();
            std::erase_if(records_, [](const auto& item) { return !item.second; });
        }
    }
    wake_.notify_all();
}
void PreviewProxyService::clear() noexcept { std::lock_guard<std::mutex> lock(mutex_); ++generation_; queue_.clear(); records_.clear(); retryAt_.clear(); wake_.notify_all(); }
void PreviewProxyService::stop() noexcept { stopping_.store(true); wake_.notify_all(); if (worker_.joinable()) worker_.join(); clear(); }
bool PreviewProxyService::cancelled(u64 generation) const noexcept { return stopping_.load() || paused_.load() || generation != generation_.load(); }
std::shared_ptr<const PreviewProxy> PreviewProxyService::request(const Asset& asset) {
    const u32 target = shortSide_.load();
    if (!target || !worker_.joinable() || std::min(asset.video.width, asset.video.height) <= target) return {};
    const std::string key = fingerprint(asset, target);
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = records_.find(key); it != records_.end()) return it->second;
    if (auto it = retryAt_.find(key); it != retryAt_.end() && std::chrono::steady_clock::now() < it->second) return {};
    if (paused_.load()) return {};
    if (queue_.size() >= 16) return {};
    records_[key] = {};
    queue_.push_back(Request{asset, key, target, generation_.load()});
    wake_.notify_one(); return {};
}

bool PreviewProxyService::make_room(u64 bytes, const std::string& preserve) {
    if (bytes > diskBudget_) return false;
    std::error_code ec;
    fs::create_directories(fs::u8path(directory_), ec);
    if (ec) return false;
    struct File { fs::path path; fs::file_time_type time; u64 bytes; };
    std::vector<File> files; u64 used = 0;
    for (fs::directory_iterator it(fs::u8path(directory_), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".mp4") continue;
        if (it->path().generic_string() == preserve) continue;
        u64 size = it->file_size(ec); if (ec) return false;
        const auto metadataBytes = fs::file_size(fs::u8path(it->path().generic_string() + ".pts"), ec);
        if (!ec) size += metadataBytes;
        ec.clear();
        used += size; files.push_back({it->path(), it->last_write_time(ec), size});
    }
    if (ec) return false;
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.time < b.time; });
    for (const File& file : files) {
        if (used + bytes <= diskBudget_) break;
        const std::string path = file.path.generic_string();
        if (path == preserve) continue;
        bool pinned = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& item : records_) if (item.second && item.second->path == path && item.second.use_count() > 1) pinned = true;
            if (!pinned) std::erase_if(records_, [&](const auto& item) { return item.second && item.second->path == path; });
        }
        if (pinned) continue;
        if (fs::remove(file.path, ec)) {
            used -= file.bytes;
            fs::remove(fs::u8path(path + ".pts"), ec); ec.clear();
        }
        ec.clear();
    }
    return used + bytes <= diskBudget_;
}

void PreviewProxyService::run() {
    set_current_thread_name("aurea-proxy"); set_current_thread_priority(ThreadPriority::Background);
    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_.load() || (!paused_.load() && !queue_.empty()); });
            if (stopping_.load()) return;
            request = std::move(queue_.front()); queue_.pop_front();
        }
        auto proxy = generate(request);
        std::lock_guard<std::mutex> lock(mutex_);
        if (request.generation == generation_.load()) {
            if (proxy) { records_[request.key] = std::move(proxy); retryAt_.erase(request.key); }
            else { records_.erase(request.key); retryAt_[request.key] = std::chrono::steady_clock::now() + std::chrono::seconds(30); }
        }
    }
}

std::shared_ptr<PreviewProxy> PreviewProxyService::generate(const Request& request) {
    Asset resolved = request.asset;
    if (resolve_) resolved.sourcePath = resolve_(resolved.sourcePath, resolveContext_);
    const std::string identity = media_->cache_identity(resolved.sourcePath.c_str());
    const std::string session = "session:" + std::to_string(static_cast<i64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string hash = fingerprint(resolved, request.target, identity.empty() ? session : identity);
    if (hash.empty() || cancelled(request.generation)) return {};
    const std::string path = (fs::u8path(directory_) / (hash + ".mp4")).generic_string();
    std::error_code ec;
    if (auto cached = read_manifest(path); cached && fs::file_size(fs::u8path(path), ec) > 0 && !ec) {
        MediaProbe probe;
        if (media_->probe(path.c_str(), probe) && probe.hasVideo && probe.video.display_width() == cached->width && probe.video.display_height() == cached->height) return cached;
    }
    ec.clear();
    auto decoder = media_->open_video(request.asset, MediaPriority::Thumbnail);
    if (!decoder || !decoder->seek_to_keyframe(0).ok() || cancelled(request.generation)) return {};
    auto result = std::make_shared<PreviewProxy>(); result->path = path; result->original = decoder->info();
    const std::string partial = path + ".partial.mp4";
    std::unique_ptr<ExportSink> sink;
    std::vector<u8> pixels, pendingPixels;
    u32 empty = 0;
    bool eos = false, success = false;
    while (!cancelled(request.generation) && result->times.size() < kMaxFrames) {
        FrameRef frame; i64 pts = 0;
        if (!decoder->next_frame(-1, frame, pts, eos).ok()) break;
        if (!frame) { if (eos) { success = !result->times.empty(); break; } if (++empty > 500) break; continue; }
        empty = 0;
        if (!result->times.empty() && pts <= result->times.back()) break;
        if (!sink) {
            result->original.color = frame->color;
            const u32 vw = frame->visibleWidth ? frame->visibleWidth : frame->width, vh = frame->visibleHeight ? frame->visibleHeight : frame->height;
            const bool rotated = frame->rotation == 90 || frame->rotation == 270;
            const u32 dw = rotated ? vh : vw, dh = rotated ? vw : vh;
            const f64 scale = std::min(1., static_cast<f64>(request.target) / std::min(dw, dh));
            result->width = std::max(2u, static_cast<u32>(dw * scale / 2) * 2);
            result->height = std::max(2u, static_cast<u32>(dh * scale / 2) * 2);
            if (result->width > 4096 || result->height > 4096 || !std::isfinite(result->original.fps) || result->original.fps <= 0) break;
            VideoStreamConfig config; config.width = result->width; config.height = result->height;
            config.fps = result->original.fps; config.keyframeIntervalFrames = std::max(1u, static_cast<u32>(config.fps));
            config.bitrateBps = std::clamp(static_cast<u32>(config.width * static_cast<f64>(config.height) * std::min(config.fps, 120.) * .16), 1'000'000u, 12'000'000u);
            const auto& color = frame->color;
            config.color.matrix = color.matrix == YCbCrMatrix::BT601 ? 6 : color.matrix == YCbCrMatrix::BT2020 ? 9 : 1;
            config.color.primaries = color.primaries == ColorPrimaries::BT601 ? 6 : color.primaries == ColorPrimaries::BT2020 ? 9 : color.primaries == ColorPrimaries::P3 ? 12 : 1;
            config.color.transfer = color.transfer == TransferFunction::PQ ? 16 : color.transfer == TransferFunction::HLG ? 18 : color.transfer == TransferFunction::Linear ? 8 : 1;
            config.color.fullRange = color.fullRange;
            const u64 expected = static_cast<u64>(std::max<i64>(0, result->original.durationUs) / 1e6 * config.bitrateBps / 8 * 1.25) + (1ull << 20);
            if (!make_room(expected, path)) break;
            sink = sink_(context_);
            if (!sink || !sink->open(partial.c_str(), config, nullptr).ok()) break;
        }
        if (frame->color != result->original.color || !proxy_frame_nv12(*frame.get(), result->width, result->height, pixels)) break;
        if (!result->times.empty()) {
            result->durations.back() = pts - result->times.back();
            if (!sink->write_video_timed(pendingPixels.data(), result->width,
                    pendingPixels.data() + static_cast<usize>(result->width) * result->height, result->width,
                    result->times.back() - result->times.front(), result->durations.back()).ok()) break;
        }
        result->times.push_back(pts); result->durations.push_back(frame->durationUs);
        frame.reset();
        pendingPixels.swap(pixels);
        if (result->times.size() % 120 == 0) {
            const u64 size = fs::file_size(fs::u8path(partial), ec);
            if (!ec && size > diskBudget_) break;
            ec.clear();
        }
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(std::max(1u, pacingMs_.load())), [&] { return cancelled(request.generation); });
    }
    if (success && sink && !cancelled(request.generation)) {
        if (result->durations.back() <= 0) result->durations.back() = result->original.durationUs > result->times.back()
            ? result->original.durationUs - result->times.back() : std::max<i64>(1, static_cast<i64>(1e6 / result->original.fps));
        result->original.durationUs = result->times.back() + result->durations.back();
        success = sink->write_video_timed(pendingPixels.data(), result->width,
            pendingPixels.data() + static_cast<usize>(result->width) * result->height, result->width,
            result->times.back() - result->times.front(), result->durations.back()).ok() && sink->finish().ok();
        const u64 size = fs::file_size(fs::u8path(partial), ec);
        success = success && !ec && size > 0 && write_manifest(*result, path + ".pts.tmp");
        if (success) {
            const u64 metadataBytes = fs::file_size(fs::u8path(path + ".pts.tmp"), ec);
            success = !ec && make_room(size + metadataBytes, partial);
        }
        if (success && !cancelled(request.generation) && (identity.empty() || media_->cache_identity(resolved.sourcePath.c_str()) == identity)) {
            fs::rename(fs::u8path(partial), fs::u8path(path), ec);
            if (!ec) fs::rename(fs::u8path(path + ".pts.tmp"), fs::u8path(path + ".pts"), ec);
            if (!ec) { AUREA_LOG_INFO("proxy pronto: %ux%u, %zu quadros", result->width, result->height, result->times.size()); return result; }
        }
    }
    if (sink) sink->abort();
    fs::remove(fs::u8path(partial), ec); fs::remove(fs::u8path(path + ".pts.tmp"), ec);
    return {};
}
} // namespace aurea
