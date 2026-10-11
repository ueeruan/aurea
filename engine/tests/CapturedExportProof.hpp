#pragma once
#include "aurea/export/ExportSink.hpp"
#include "aurea/project/FileIO.hpp"
#include <cmath>
#include <string>

namespace aurea::test {
// The capture sinks test compositor pixels and ordering. This proof validates
// their uncompressed stream; native MP4 tests use an independent real decoder.
class CapturedExportProof {
public:
    Status open(const char* path, const VideoStreamConfig& video,
                const AudioStreamConfig* audio) {
        path_ = path; video_ = video; hasAudio_ = audio != nullptr;
        if (audio) audio_ = *audio;
        pictures_ = samples_ = 0; valid_ = true; finished_ = false;
        return OkStatus;
    }
    void picture(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 pts) {
        valid_ &= y && uv && yStride >= video_.width && uvStride >= video_.width &&
                  std::abs(pts - static_cast<i64>(std::llround(pictures_ * 1e6 / video_.fps))) <= 1;
        ++pictures_;
    }
    void pcm(const i16* data, u32 frames, i64 pts) {
        valid_ &= hasAudio_ && data && audio_.sampleRate &&
            std::abs(pts - static_cast<i64>(samples_ * 1'000'000 / audio_.sampleRate)) <= 1;
        samples_ += frames;
    }
    Status finish() {
        if (!valid_ || pictures_ == 0) return Errc::CorruptData;
        const char marker[] = "validated uncompressed compositor capture";
        const auto status = fileio::write_atomic(path_, marker, sizeof(marker));
        finished_ = status.ok();
        return status;
    }
    Status validate(const ExportOutputValidation& expected) const {
        if (!finished_ || !valid_ || pictures_ != expected.frames ||
            video_.width != expected.width || video_.height != expected.height ||
            hasAudio_ != expected.audio || !fileio::exists(path_)) return Errc::CorruptData;
        if (hasAudio_) {
            const i64 wanted = static_cast<i64>(std::llround(expected.frames * audio_.sampleRate / expected.fps));
            if (std::abs(static_cast<i64>(samples_) - wanted) > 2) return Errc::CorruptData;
        }
        return OkStatus;
    }
private:
    std::string path_;
    VideoStreamConfig video_{};
    AudioStreamConfig audio_{};
    u64 pictures_ = 0, samples_ = 0;
    bool hasAudio_ = false, valid_ = true, finished_ = false;
};
}
