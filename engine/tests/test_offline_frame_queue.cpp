#include "TestFramework.hpp"
#include "aurea/export/OfflineFrameQueue.hpp"
#include "aurea/export/ExportRules.hpp"
#include <array>
#include <chrono>
#include <limits>
#include <thread>
#include <vector>
using namespace aurea;

AUREA_TEST(OfflineFrameQueue, PictureDimensionsDoNotUseCodecStorageAlignment) {
    for (const auto size : {ExportFrameSize{854, 480}, ExportFrameSize{1000, 700}, ExportFrameSize{1080, 1350}}) {
        const auto actual = export_frame_size_v2(size.width, size.height, 0);
        AUREA_CHECK_EQ(actual.width, size.width); AUREA_CHECK_EQ(actual.height, size.height);
    }
    AUREA_CHECK_EQ(export_frame_size_v2(1920, 1080, 480).width, 854u);
    AUREA_CHECK_EQ(export_frame_size_v2(1080, 1920, 480).height, 854u);
    AUREA_CHECK_EQ(export_frame_size_v2(853, 480, 0).width, 0u);
}
AUREA_TEST(OfflineFrameQueue, ReservationsEnforceExactByteBudgetBeforeAdmission) {
    OfflineFrameQueue exact, insufficient, overflow, invalid;
    AUREA_CHECK(exact.prepare(4, 200, 4096, 16384, nullptr).ok());
    AUREA_CHECK_EQ(exact.reserved_bytes(), 16384ull);
    AUREA_CHECK(insufficient.prepare(4, 200, 4096, 16383, nullptr).code() == Errc::BudgetExceeded);
    AUREA_CHECK_EQ(insufficient.reserved_bytes(), 0ull);
    AUREA_CHECK(overflow.prepare(4, 200, std::numeric_limits<u64>::max(),
        std::numeric_limits<u64>::max(), nullptr).code() == Errc::BudgetExceeded);
    AUREA_CHECK_EQ(overflow.reserved_bytes(), 0ull);
    AUREA_CHECK(invalid.prepare(5, 200, 1, 100, nullptr).code() == Errc::InvalidArgument);
    AUREA_CHECK(exact.prepare(1, 1, 1, 1, nullptr).code() == Errc::InvalidArgument);
}
AUREA_TEST(OfflineFrameQueue, BoundedLeasesDeliverActualPictureIdentitiesInOrder) {
    std::atomic<bool> cancel{false}, healthy{true}; OfflineFrameQueue session;
    AUREA_CHECK(session.prepare(2, 600, 4096, 8192, &cancel).ok());
    std::array<u32, 4> picture{};
    std::vector<u32> consumed;
    std::thread encoder([&] {
        for (u32 frame = 0; frame < 600; ++frame) {
            Result<u32> slot = Status{Errc::Timeout};
            do { slot = session.take(); } while (slot.code() == Errc::Timeout);
            if (!slot.ok()) { healthy = false; return; }
            consumed.push_back(picture[*slot]);
            if (picture[*slot] != frame || !session.encoded(*slot, frame).ok()) {
                healthy = false; cancel = true; return;
            }
        }
    });
    for (u32 frame = 0; frame < 600; ++frame) {
        Result<u32> slot = Status{Errc::Timeout};
        do { slot = session.acquire(frame % 7 == 0 ? 1 : 2); } while (slot.code() == Errc::Timeout);
        AUREA_CHECK(slot.ok()); if (!slot.ok()) { cancel = true; break; }
        picture[*slot] = frame;
        AUREA_CHECK(session.publish(*slot, frame).ok());
    }
    session.end_render(OkStatus); encoder.join();
    AUREA_CHECK(healthy); AUREA_CHECK_EQ(consumed.size(), 600u);
    for (u32 n = 0; n < consumed.size(); ++n) AUREA_CHECK_EQ(consumed[n], n);
    AUREA_CHECK_EQ(session.reserved_bytes(), 8192ull);
    AUREA_CHECK(session.drained()); AUREA_CHECK(session.begin_finalizing().ok());
    AUREA_CHECK(session.phase() == ExportPhase::Finalizing);
    AUREA_CHECK(session.begin_validating().ok());
    session.validated(Errc::DecodeFailed);
    AUREA_CHECK(session.phase() == ExportPhase::Failed);
}
AUREA_TEST(OfflineFrameQueue, CodecOwnsSlotUntilAcknowledgedAndCancelEndsBackpressure) {
    OfflineFrameQueue session; std::atomic<bool> cancel{false};
    AUREA_CHECK(session.prepare(1, 2, 4, 4, &cancel).ok());
    const auto slot = session.acquire(1); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(session.publish(*slot, 0).ok());
    AUREA_CHECK(!session.publish(*slot, 0).ok());
    AUREA_CHECK(!session.begin_finalizing().ok());
    const auto taken = session.take(); AUREA_CHECK(taken.ok());
    AUREA_CHECK(session.acquire(1).code() == Errc::Timeout);
    std::atomic<Errc> observed{Errc::Ok};
    std::thread waiter([&] { observed = session.acquire(1).code(); });
    cancel = true;
    const auto before = std::chrono::steady_clock::now();
    waiter.join();
    AUREA_CHECK(observed.load() == Errc::Cancelled);
    AUREA_CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(250));
    session.end_render(Errc::Cancelled);
    AUREA_CHECK(session.phase() == ExportPhase::Cancelled);
}
AUREA_TEST(OfflineFrameQueue, RejectedOwnershipAndIncompleteRenderCannotReportSuccess) {
    OfflineFrameQueue session;
    AUREA_CHECK(session.prepare(1, 2, 4, 4, nullptr).ok());
    auto slot = session.acquire(1); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(!session.publish(*slot, 1).ok());
    AUREA_CHECK(session.publish(*slot, 0).ok());
    slot = session.take(); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(!session.encoded(*slot, 1).ok());
    AUREA_CHECK(session.encoded(*slot, 0).ok());
    AUREA_CHECK(!session.encoded(*slot, 0).ok());
    session.end_render(OkStatus);
    AUREA_CHECK(session.phase() == ExportPhase::Failed);
    AUREA_CHECK(!session.drained());
    AUREA_CHECK(!session.begin_finalizing().ok());
    session.validated(OkStatus);
    AUREA_CHECK(session.phase() == ExportPhase::Failed);
}
AUREA_TEST(OfflineFrameQueue, FinalPictureStillRequiresSuccessfulIndependentValidation) {
    OfflineFrameQueue session;
    AUREA_CHECK(session.prepare(1, 1, 4, 4, nullptr).ok());
    auto slot = session.acquire(1); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(session.publish(*slot, 0).ok());
    session.end_render(OkStatus);
    AUREA_CHECK(!session.drained()); AUREA_CHECK(!session.begin_finalizing().ok());
    slot = session.take(); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(session.encoded(*slot, 0).ok());
    AUREA_CHECK(session.drained()); AUREA_CHECK(session.begin_finalizing().ok());
    AUREA_CHECK(session.begin_validating().ok());
    AUREA_CHECK(session.phase() == ExportPhase::Validating);
    session.validated(OkStatus);
    AUREA_CHECK(session.phase() == ExportPhase::Completed);
}
AUREA_TEST(OfflineFrameQueue, CancelDuringValidationIsReportedToTheCaller) {
    // 0.3.2: cancelar enquanto a saída é validada virava fase Cancelled, mas o
    // motor seguia com o resultado OK e publicava o arquivo.
    std::atomic<bool> cancel{false};
    OfflineFrameQueue session;
    AUREA_CHECK(session.prepare(1, 1, 4, 4, &cancel).ok());
    auto slot = session.acquire(1); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(session.publish(*slot, 0).ok());
    session.end_render(OkStatus);
    slot = session.take(); AUREA_CHECK(slot.ok()); if (!slot.ok()) return;
    AUREA_CHECK(session.encoded(*slot, 0).ok());
    AUREA_CHECK(session.begin_finalizing().ok());
    AUREA_CHECK(session.begin_validating().ok());
    cancel.store(true);
    const Status result = session.validated(OkStatus);
    AUREA_CHECK(result.code() == Errc::Cancelled);
    AUREA_CHECK(session.phase() == ExportPhase::Cancelled);
}
