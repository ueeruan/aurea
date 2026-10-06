// Runs on the macOS CI host without submitting invalid work to a real GPU.
// Fake command-buffer states exercise the exact retirement helper used by Metal.
#import <Foundation/Foundation.h>
#include "MetalFrameCompletion.hpp"
#include <cstdio>

@interface TestCommandBufferState : NSObject
@property(nonatomic) MTLCommandBufferStatus status;
@end
@implementation TestCommandBufferState
@end

using namespace aurea;
using namespace aurea::mtl;

struct TestFrame {
    id<MTLCommandBuffer> cmd = nil;
    u64 frameNumber = 0;
    bool submitted = true;
    u32 releases = 0;
};

static id<MTLCommandBuffer> buffer(MTLCommandBufferStatus status) {
    TestCommandBufferState* state = [TestCommandBufferState new];
    state.status = status;
    return (id<MTLCommandBuffer>)state;
}

static int checks = 0;
#define REQUIRE(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "Metal completion check failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main() {
    @autoreleasepool {
        TestFrame frames[5];
        for (u32 i = 0; i < 5; ++i) {
            frames[i].cmd = buffer(MTLCommandBufferStatusScheduled);
            frames[i].frameNumber = i + 1;
        }
        auto release = [](TestFrame& frame) { ++frame.releases; };
        auto wait = [&](Status result) {
            return finish_frame_wait(frames[2], frames, 5, &frames[3], result, release);
        };
        REQUIRE(wait(Status{Errc::Timeout}).code() == Errc::Timeout);
        for (const auto& frame : frames) REQUIRE(frame.submitted && frame.releases == 0);
        // InvalidState is not evidence of GPU completion either.
        REQUIRE(wait(Status{Errc::InvalidState}).code() == Errc::InvalidState);
        REQUIRE(frames[2].submitted && frames[2].releases == 0);

        ((TestCommandBufferState*)frames[0].cmd).status = MTLCommandBufferStatusCompleted;
        ((TestCommandBufferState*)frames[2].cmd).status = MTLCommandBufferStatusError;
        ((TestCommandBufferState*)frames[3].cmd).status = MTLCommandBufferStatusCompleted;
        ((TestCommandBufferState*)frames[4].cmd).status = MTLCommandBufferStatusCompleted;
        REQUIRE(wait(Status{Errc::OutOfDeviceMemory}).code() == Errc::OutOfDeviceMemory);
        REQUIRE(!frames[0].submitted && frames[0].releases == 1);
        REQUIRE(frames[1].submitted && frames[1].releases == 0); // older, but still pending
        REQUIRE(!frames[2].submitted && frames[2].releases == 1);
        REQUIRE(frames[3].submitted && frames[3].releases == 0); // current frame
        REQUIRE(frames[4].submitted && frames[4].releases == 0); // newer submission
        REQUIRE(wait(Status{Errc::OutOfDeviceMemory}).ok()); // report failure once
        REQUIRE(frames[2].releases == 1); // never release twice

        frames[2].submitted = true; frames[2].releases = 0;
        REQUIRE(wait(Status{Errc::InvalidState}).code() == Errc::InvalidState);
        REQUIRE(!frames[2].submitted && frames[2].releases == 1); // Error without NSError
        frames[2].submitted = true; frames[2].releases = 0;
        ((TestCommandBufferState*)frames[2].cmd).status = MTLCommandBufferStatusCompleted;
        REQUIRE(wait(OkStatus).ok());
        REQUIRE(!frames[2].submitted && frames[2].releases == 1);
        frames[2].submitted = true; frames[2].releases = 0; frames[2].cmd = nil;
        REQUIRE(wait(Status{Errc::InvalidState}).code() == Errc::InvalidState);
        REQUIRE(frames[2].submitted && frames[2].releases == 0); // no actual completion proof
    }
    std::printf("Metal frame completion: %d checks passed; no GPU fault injected\n", checks);
    return 0;
}
