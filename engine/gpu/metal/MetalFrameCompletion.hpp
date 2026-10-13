#pragma once

#import <Metal/Metal.h>
#include "aurea/core/Result.hpp"

namespace aurea::mtl {

// Submit even discarded frames: recorded uploads and deferred ownership still
// need a real GPU completion. Only a complete picture enters presentation.
inline void commit_presentable_frame(id<MTLCommandBuffer> command, id<MTLDrawable> drawable,
                                     bool present, id<MTLDrawable> __strong& unpresented) noexcept {
    if (drawable) {
        if (present) [command presentDrawable:drawable];
        else unpresented = drawable;
    }
    [command commit];
}

[[nodiscard]] inline bool command_buffer_terminal(id<MTLCommandBuffer> command) noexcept {
    if (!command) return false;
    const MTLCommandBufferStatus state = command.status;
    return state == MTLCommandBufferStatusCompleted || state == MTLCommandBufferStatusError;
}

// Export cleanup separates safe completion from the original command error.
// A terminal Error owns no outstanding GPU work, but remains a failed export;
// a generic error without actual completion never permits resource retirement.
[[nodiscard]] inline Status export_command_completion(id<MTLCommandBuffer> command,
        Status reported, Status& firstFailure) noexcept {
    if (reported.code() == Errc::DeviceLost) return reported;
    if (!command_buffer_terminal(command))
        return reported.ok() ? Status{Errc::Timeout} : reported;
    if (!reported.ok() && firstFailure.ok()) firstFailure = reported;
    return OkStatus;
}

// Observe every submitted command before retiring any slot. Slots are recycled
// out of frame-number order; retiring a recent frame first can consume an older
// terminal Error before its original status is recorded by export cleanup.
template<class Frame, class Wait>
[[nodiscard]] Status observe_export_frames(Frame* frames, u32 count, Frame* current,
                                           Status& firstFailure, Wait&& wait) noexcept {
    if (current) return Status{Errc::Timeout};
    for (u32 i = 0; i < count; ++i) {
        const Frame& frame = frames[i];
        if (!frame.submitted) continue;
        const Status ready = export_command_completion(frame.cmd, wait(frame), firstFailure);
        if (!ready.ok()) return ready;
    }
    return OkStatus;
}

// Shared by the backend and the native fault-injection regression. Completion
// with an error is still completion, but a generic error alone proves nothing
// about ownership: inspect each actual command buffer before retiring it.
template<class Frame, class Retire>
[[nodiscard]] Status finish_frame_wait(Frame& waited, Frame* frames, u32 count,
                                       Frame* current, Status result, Retire&& retire) noexcept {
    if (!waited.submitted) return OkStatus;
    if (!result.ok() && !command_buffer_terminal(waited.cmd)) return result;
    const u64 completedThrough = waited.frameNumber;
    for (u32 i = 0; i < count; ++i) {
        Frame& frame = frames[i];
        if (&frame == current || !frame.submitted || frame.frameNumber > completedThrough
                || !command_buffer_terminal(frame.cmd)) continue;
        retire(frame);
        frame.submitted = false;
    }
    // Preserve the original failure for this caller. A later wait observes the
    // retired frame as complete instead of returning its terminal error forever.
    return result;
}

} // namespace aurea::mtl
