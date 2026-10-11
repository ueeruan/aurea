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
