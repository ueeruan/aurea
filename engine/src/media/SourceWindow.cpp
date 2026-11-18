// =============================================================================
//  Aurea / media / SourceWindow.cpp — ver SourceWindow.hpp.
// =============================================================================
#include "aurea/media/SourceWindow.hpp"

#include "aurea/media/MediaManager.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace aurea {
namespace {

std::mutex& registry_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, i64>& registry() {
    static std::unordered_map<std::string, i64> windows;
    return windows;
}

/// Vídeo na régua do original. Só a thread de decode da fonte chama (como o
/// backend de dentro); `info_` é atualizado nas chamadas dela.
class WindowedVideoDecoder final : public VideoDecoderBackend {
public:
    WindowedVideoDecoder(std::unique_ptr<VideoDecoderBackend> inner, i64 startUs)
        : inner_(std::move(inner)), startUs_(startUs) {
        refresh();
    }

    const VideoStreamInfo& info() const noexcept override { return info_; }

    Status seek_to_keyframe(i64 targetUs) noexcept override {
        const i64 inner = targetUs - startUs_;
        // Antes da janela (ou no começo dela): o decoder volta ao 1º quadro do
        // trecho, e esse quadro sai segurando todo o trecho anterior.
        atStart_ = inner <= frame_us();
        const Status s = inner_->seek_to_keyframe(std::max<i64>(0, inner));
        refresh();
        return s;
    }

    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& endOfStream) noexcept override {
        // Sem estourar no deslocamento: o "entregue tudo" de quem chama é um
        // valor muito negativo (VideoSource usa min()/4).
        const i64 innerFrom = deliverFromUs <= std::numeric_limits<i64>::min() / 2 ? deliverFromUs
                                                                                    : deliverFromUs - startUs_;
        // Sentinela: um backend que não produziu nada nesta chamada (nem
        // entregue nem descartado) não mexe no pts — e o de quem chamou volta.
        constexpr i64 kUnset = std::numeric_limits<i64>::min();
        const i64 callerPts = outPtsUs;
        outPtsUs = kUnset;
        const Status s = inner_->next_frame(innerFrom, out, outPtsUs, endOfStream);
        refresh();
        const bool produced = s.ok() && (out || outPtsUs != kUnset);
        if (outPtsUs == kUnset) outPtsUs = callerPts;
        if (!s.ok()) return s;
        const bool first = atStart_ && produced;
        if (produced) {
            atStart_ = false;
            outPtsUs += startUs_;
        }
        if (out) {
            DecodedFrame* f = out.get();
            f->ptsUs += startUs_;
            if (first) {
                // O 1º quadro do trecho cobre também tudo antes dele.
                const i64 own = f->durationUs > 0 ? f->durationUs : frame_us();
                f->durationUs = f->ptsUs + own;
                f->ptsUs = 0;
            }
        }
        return s;
    }

    u32 max_live_frames() const noexcept override { return inner_->max_live_frames(); }
    i64 keyframe_interval_us() const noexcept override { return inner_->keyframe_interval_us(); }
    void suspend() noexcept override { inner_->suspend(); }
    Status resume() noexcept override {
        const Status s = inner_->resume();
        refresh();
        return s;
    }

private:
    void refresh() noexcept {
        info_ = inner_->info();
        if (info_.durationUs > 0) info_.durationUs += startUs_;
    }
    [[nodiscard]] i64 frame_us() const noexcept {
        const f64 fps = info_.fps > 0.0 && std::isfinite(info_.fps) ? info_.fps : 30.0;
        return std::max<i64>(1, static_cast<i64>(std::llround(1e6 / fps)));
    }

    std::unique_ptr<VideoDecoderBackend> inner_;
    i64 startUs_ = 0;
    VideoStreamInfo info_{};
    bool atStart_ = true;   ///< aberto agora: o decoder está no começo do trecho
};

class WindowedAudioDecoder final : public audio::AudioDecoderBackend {
public:
    WindowedAudioDecoder(std::unique_ptr<audio::AudioDecoderBackend> inner, i64 startUs)
        : inner_(std::move(inner)), startUs_(startUs) {
        info_ = inner_->info();
        if (info_.durationUs > 0) info_.durationUs += startUs_;
    }
    const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    Status seek(i64 us) noexcept override { return inner_->seek(std::max<i64>(0, us - startUs_)); }
    Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept override {
        const Status s = inner_->read(out, ptsUs, eos);
        if (s.ok()) ptsUs += startUs_;
        return s;
    }

private:
    std::unique_ptr<audio::AudioDecoderBackend> inner_;
    i64 startUs_ = 0;
    audio::AudioStreamInfo info_{};
};

} // namespace

void register_source_window(std::string_view path, i64 startUs) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lock(registry_mutex());
    if (startUs > 0) registry()[std::string(path)] = startUs;
    else registry().erase(std::string(path));
}

i64 source_window_us(std::string_view path) noexcept {
    if (path.empty()) return 0;
    std::lock_guard<std::mutex> lock(registry_mutex());
    if (registry().empty()) return 0;
    const auto it = registry().find(std::string(path));
    return it == registry().end() ? 0 : it->second;
}

std::unique_ptr<VideoDecoderBackend> window_video_decoder(std::unique_ptr<VideoDecoderBackend> inner, i64 startUs) {
    if (!inner || startUs <= 0) return inner;
    return std::make_unique<WindowedVideoDecoder>(std::move(inner), startUs);
}

std::unique_ptr<audio::AudioDecoderBackend> window_audio_decoder(std::unique_ptr<audio::AudioDecoderBackend> inner,
                                                                 i64 startUs) {
    if (!inner || startUs <= 0) return inner;
    return std::make_unique<WindowedAudioDecoder>(std::move(inner), startUs);
}

void window_probe(MediaProbe& probe, i64 startUs) noexcept {
    if (startUs <= 0) return;
    if (probe.hasVideo && probe.video.durationUs > 0) probe.video.durationUs += startUs;
    if (probe.hasAudio && probe.audioDurationUs > 0) probe.audioDurationUs += startUs;
}

} // namespace aurea
