#pragma once

#include "aurea/media/VideoSource.hpp"
#include "aurea/export/ExportSink.hpp"
#include "aurea/project/Asset.hpp"
#include <deque>
#include <unordered_map>

namespace aurea {
class VideoSourceFactory;

struct PreviewProxy {
    std::string path;
    VideoStreamInfo original;
    u32 width = 0, height = 0;
    // Original presentation times; the encoded proxy begins at zero.
    std::vector<i64> times, durations;
};

// CPU area resampling in the source's encoded color space. Crop and rotation
// are baked into the proxy; PQ/HLG stay PQ/HLG (preview quantization to 8 bits).
bool proxy_frame_nv12(const DecodedFrame& frame, u32 width, u32 height,
                      std::vector<u8>& pixels);
std::unique_ptr<VideoDecoderBackend> proxy_decoder(std::unique_ptr<VideoDecoderBackend> decoder,
                                                 std::shared_ptr<const PreviewProxy> proxy);

class PreviewProxyService {
public:
    ~PreviewProxyService();
    void configure(VideoSourceFactory* media, ExportSinkFactory sink, void* context,
                   std::string directory, u64 diskBudget = 512ull << 20,
                   std::string (*resolve)(const std::string&, void*) = nullptr, void* resolveContext = nullptr);
    void set_policy(u32 shortSide, u32 pacingMs = 0) noexcept;
    void set_paused(bool paused) noexcept;
    enum PauseReason : u32 { Background = 1, Thermal = 2, Export = 4, Manual = 8, Playback = 16 };
    void set_pause_reason(PauseReason reason, bool paused) noexcept;
    std::shared_ptr<const PreviewProxy> request(const Asset& asset);
    void clear() noexcept;
    void stop() noexcept;
private:
    struct Request { Asset asset; std::string key; u32 target = 0; u64 generation = 0; };
    void run();
    std::shared_ptr<PreviewProxy> generate(const Request& request);
    bool cancelled(u64 generation) const noexcept;
    bool make_room(u64 bytes, const std::string& preserve);
    VideoSourceFactory* media_ = nullptr;
    ExportSinkFactory sink_ = nullptr;
    void* context_ = nullptr;
    std::string (*resolve_)(const std::string&, void*) = nullptr;
    void* resolveContext_ = nullptr;
    std::string directory_;
    u64 diskBudget_ = 0;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::deque<Request> queue_;
    std::unordered_map<std::string, std::shared_ptr<PreviewProxy>> records_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> retryAt_;
    std::atomic<u64> generation_{0};
    std::atomic<bool> stopping_{false}, paused_{false};
    u32 pauseReasons_ = 0;
    std::atomic<u32> shortSide_{0}, pacingMs_{0};
};
} // namespace aurea
