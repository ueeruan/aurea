#include "aurea/playback/Playback.hpp"

#include <cmath>

namespace aurea {

// =============================================================================
// PlaybackClock
// =============================================================================
void PlaybackClock::start(i64 mediaNs, u64 nowNs, f32 speed) noexcept {
    anchorMediaNs_ = mediaNs;
    anchorNowNs_ = nowNs;
    speed_ = speed;
    running_ = true;
}

void PlaybackClock::stop(u64 nowNs) noexcept {
    anchorMediaNs_ = media_ns(nowNs);
    anchorNowNs_ = nowNs;
    running_ = false;
}

void PlaybackClock::seek(i64 mediaNs, u64 nowNs) noexcept {
    anchorMediaNs_ = mediaNs;
    anchorNowNs_ = nowNs;
}

void PlaybackClock::set_speed(f32 speed, u64 nowNs) noexcept {
    anchorMediaNs_ = media_ns(nowNs);
    anchorNowNs_ = nowNs;
    speed_ = speed;
}

i64 PlaybackClock::media_ns(u64 nowNs) const noexcept {
    if (!running_) return anchorMediaNs_;
    if (master_ && master_->available()) return master_->position_ns();
    const f64 elapsed = static_cast<f64>(static_cast<i64>(nowNs - anchorNowNs_)) * static_cast<f64>(speed_);
    return anchorMediaNs_ + static_cast<i64>(elapsed);
}

// =============================================================================
// PlaybackController
// =============================================================================
void PlaybackController::configure(f64 fps, FrameIndex duration) noexcept {
    fps_ = fps > 0.0 ? fps : 30.0;
    duration_ = FrameIndex{duration.value > 0 ? duration.value : 1};
    current_ = clamp_frame(current_);
    currentNs_ = frame_to_ns(current_);
}

i64 PlaybackController::frame_to_ns(FrameIndex f) const noexcept { return tick_at(f, fps_).value; }
FrameIndex PlaybackController::ns_to_frame(i64 ns) const noexcept { return frame_at(TickNs{ns}, fps_); }

FrameIndex PlaybackController::clamp_frame(FrameIndex f) const noexcept {
    if (f.value < 0) return FrameIndex{0};
    if (f.value > duration_.value - 1) return FrameIndex{duration_.value - 1};
    return f;
}

void PlaybackController::play(u64 nowNs) noexcept {
    if (mode_ == PlaybackMode::Playing) return;
    // Tocar a partir do último frame volta ao começo — é o que todo player faz.
    if (current_.value >= duration_.value - 1) {
        current_ = FrameIndex{0};
        ++generation_;
    }
    currentNs_ = frame_to_ns(current_);
    clock_.start(currentNs_, nowNs, speed_);
    mode_ = PlaybackMode::Playing;
    direction_ = speed_ >= 0.0f ? 1 : -1;
}

void PlaybackController::pause(u64 nowNs) noexcept {
    if (mode_ != PlaybackMode::Playing) return;
    clock_.stop(nowNs);
    // Pausa no frame INTEIRO mostrado, não no meio de um: o próximo play
    // recomeça exatamente do que está na tela.
    currentNs_ = frame_to_ns(current_);
    clock_.seek(currentNs_, nowNs);
    mode_ = PlaybackMode::Paused;
    direction_ = 0;
}

void PlaybackController::toggle(u64 nowNs) noexcept {
    if (mode_ == PlaybackMode::Playing) pause(nowNs);
    else play(nowNs);
}

void PlaybackController::seek(FrameIndex frame, u64 nowNs) noexcept {
    current_ = clamp_frame(frame);
    currentNs_ = frame_to_ns(current_);
    clock_.seek(currentNs_, nowNs);
    ++generation_;
}

void PlaybackController::begin_scrub(u64 nowNs) noexcept {
    wasPlayingBeforeScrub_ = mode_ == PlaybackMode::Playing;
    if (wasPlayingBeforeScrub_) clock_.stop(nowNs);
    mode_ = PlaybackMode::Scrubbing;
    lastScrubNs_ = nowNs;
    lastScrubFrame_ = current_;
    scrubVelocity_ = 0.0f;
    direction_ = 0;
}

void PlaybackController::scrub(FrameIndex frame, u64 nowNs) noexcept {
    if (mode_ != PlaybackMode::Scrubbing) begin_scrub(nowNs);
    const FrameIndex target = clamp_frame(frame);
    const i64 delta = target.value - lastScrubFrame_.value;
    if (delta != 0) {
        direction_ = delta > 0 ? 1 : -1;
        const f64 dt = static_cast<f64>(nowNs - lastScrubNs_) * 1e-9;
        if (dt > 1e-4) {
            const f32 v = static_cast<f32>(static_cast<f64>(delta) / dt);
            scrubVelocity_ = scrubVelocity_ == 0.0f ? v : scrubVelocity_ * 0.7f + v * 0.3f;
        }
        lastScrubNs_ = nowNs;
        lastScrubFrame_ = target;
        ++generation_;
    }
    current_ = target;
    currentNs_ = frame_to_ns(current_);
}

void PlaybackController::end_scrub(u64 nowNs) noexcept {
    if (mode_ != PlaybackMode::Scrubbing) return;
    mode_ = PlaybackMode::Paused;
    direction_ = 0;
    scrubVelocity_ = 0.0f;
    clock_.seek(currentNs_, nowNs);
    if (wasPlayingBeforeScrub_) play(nowNs);
}

void PlaybackController::step(i32 frames, u64 nowNs) noexcept {
    if (mode_ == PlaybackMode::Playing) pause(nowNs);
    seek(FrameIndex{current_.value + frames}, nowNs);
}

void PlaybackController::set_speed(f32 speed, u64 nowNs) noexcept {
    if (!(speed > 0.0f) || speed > 16.0f) return;
    speed_ = speed;
    clock_.set_speed(speed, nowNs);
}

FrameIndex PlaybackController::update(u64 nowNs) noexcept {
    if (mode_ != PlaybackMode::Playing) return current_;

    i64 ns = clock_.media_ns(nowNs);
    const i64 endNs = frame_to_ns(duration_);
    if (ns >= endNs) {
        if (loop_ && endNs > 0) {
            ns = ns % endNs;
            clock_.start(ns, nowNs, speed_);
            ++generation_;
        } else {
            current_ = FrameIndex{duration_.value - 1};
            currentNs_ = frame_to_ns(current_);
            clock_.stop(nowNs);
            mode_ = PlaybackMode::Paused;
            direction_ = 0;
            return current_;
        }
    }
    currentNs_ = ns;
    current_ = clamp_frame(ns_to_frame(ns));
    return current_;
}

// =============================================================================
// FrameScheduler
// =============================================================================
void FrameScheduler::presented(FrameIndex frame, bool playing) noexcept {
    if (playing && hasLast_ && frame.value > last_.value + 1) {
        const u32 skipped = static_cast<u32>(frame.value - last_.value - 1);
        dropped_ += skipped;
        droppedWindow_ += skipped;
    }
    last_ = frame;
    hasLast_ = true;
}

} // namespace aurea
