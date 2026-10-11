#include "TestFramework.hpp"
#include "aurea/export/AureaExportEngineV2.hpp"
#include "aurea/export/ExportRules.hpp"
#include <thread>
#include <vector>
#include <chrono>
using namespace aurea;
using namespace aurea::test;

AUREA_TEST(ExportV2, PictureDimensionsAreIndependentOfCodecStorageAlignment) {
    for (const auto size : {ExportFrameSize{854, 480}, ExportFrameSize{1000, 700}, ExportFrameSize{1080, 1350}}) {
        const auto actual = export_frame_size_v2(size.width, size.height, 0);
        AUREA_CHECK_EQ(actual.width, size.width);
        AUREA_CHECK_EQ(actual.height, size.height);
    }
    AUREA_CHECK_EQ(export_frame_size_v2(1920, 1080, 480).width, 854u);
    AUREA_CHECK_EQ(export_frame_size_v2(1080, 1920, 480).height, 854u);
    AUREA_CHECK_EQ(export_frame_size_v2(853, 480, 0).width, 0u);
}

AUREA_TEST(ExportV2, BoundedQueuePreservesOrderAndDoesNotCompleteBeforeValidation) {
    std::atomic<bool> cancel{false}; AureaExportEngineV2 session;
    AUREA_CHECK(session.prepare(2, 600, &cancel).ok());
    std::vector<u32> consumed;
    std::atomic<bool> healthy{true};
    std::thread encoder([&] {
        for (u32 frame = 0; frame < 600; ++frame) {
            Result<u32> slot = Status{Errc::Timeout};
            do { slot = session.take(); } while (slot.code() == Errc::Timeout);
            if (!slot.ok()) { healthy = false; return; }
            consumed.push_back(frame);
            if (!session.encoded(*slot, frame).ok()) { healthy = false; return; }
        }
    });
    for (u32 frame = 0; frame < 600; ++frame) {
        Result<u32> slot = Status{Errc::Timeout};
        do { slot = session.acquire(frame % 7 == 0 ? 1 : 2); } while (slot.code() == Errc::Timeout);
        AUREA_CHECK(slot.ok());
        if (!slot.ok()) { cancel = true; break; }
        AUREA_CHECK(session.publish(*slot, frame).ok());
    }
    session.end_render(OkStatus); encoder.join();
    AUREA_CHECK(healthy.load()); AUREA_CHECK_EQ(consumed.size(), 600u);
    AUREA_CHECK_EQ(session.completed_frames(), 600u);
    AUREA_CHECK(session.drained()); AUREA_CHECK(session.begin_finalizing().ok());
    AUREA_CHECK(session.phase() == ExportPhase::Finalizing);
    AUREA_CHECK(session.begin_validating().ok());
    AUREA_CHECK(session.phase() == ExportPhase::Validating);
    session.validated(Errc::DecodeFailed);
    AUREA_CHECK(session.phase() == ExportPhase::Failed);
}
AUREA_TEST(ExportV2, SlotCannotBeReusedUntilEncoderCompletesAndCancellationWakesWaiters) {
    AureaExportEngineV2 session; std::atomic<bool> cancel{false};
    AUREA_CHECK(session.prepare(1, 2, &cancel).ok());
    const auto slot = session.acquire(1); AUREA_CHECK(slot.ok());
    if (!slot.ok()) return;
    AUREA_CHECK(session.publish(*slot, 0).ok());
    AUREA_CHECK(!session.publish(*slot, 0).ok());
    AUREA_CHECK(!session.begin_finalizing().ok());
    const auto taken = session.take(); AUREA_CHECK(taken.ok());
    AUREA_CHECK(session.acquire(1).code() == Errc::Timeout);
    cancel = true;
    const auto before = std::chrono::steady_clock::now();
    AUREA_CHECK(session.acquire(1).code() == Errc::Cancelled);
    AUREA_CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(250));
    session.end_render(Errc::Cancelled);
    AUREA_CHECK(session.phase() == ExportPhase::Cancelled);
}
AUREA_TEST(ExportV2, ExactFrameOwnershipAndValidationAreRequiredForSuccess) {
    AureaExportEngineV2 session;
    AUREA_CHECK(session.prepare(1, 1, nullptr).ok());
    auto slot = session.acquire(1); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(!session.publish(*slot, 1).ok());
    AUREA_CHECK(session.publish(*slot, 0).ok());
    slot = session.take(); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(!session.encoded(*slot, 1).ok());
    AUREA_CHECK(session.encoded(*slot, 0).ok());
    AUREA_CHECK(!session.encoded(*slot, 0).ok());
    session.end_render(OkStatus);
    AUREA_CHECK(session.begin_finalizing().ok());
    AUREA_CHECK(session.begin_validating().ok());
    session.validated(OkStatus);
    AUREA_CHECK(session.phase() == ExportPhase::Completed);
}
