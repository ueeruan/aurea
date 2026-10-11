#include "TestFramework.hpp"
#include "aurea/render/RenderScheduler.hpp"

#include <cmath>
#include <limits>

using namespace aurea;
using namespace aurea::test;

AUREA_TEST(ExportScheduler, FractionalFrameTimesAreAbsoluteAndDoNotDrift) {
    AureaRenderScheduler scheduler;
    AUREA_CHECK(scheduler.configure(108001, 30000.0 / 1001.0, 24000.0 / 1001.0, 4).ok());
    // More than an hour, including nonsequential access: neither rendering
    // latency nor recovery from an earlier frame may influence the clock.
    for (const u32 index : {108000u, 1u, 90000u, 0u, 53999u, 54000u, 2u, 3u, 4u, 5u}) {
        const auto plan = scheduler.frame(index);
        AUREA_CHECK(plan.ok());
        if (!plan.ok()) continue;
        const i64 expectedPts = (static_cast<i64>(index) * 1001 * 1000000 + 15000) / 30000;
        AUREA_CHECK_EQ(plan->ptsUs, expectedPts);
        AUREA_CHECK_EQ(plan->compositionFrame.value, static_cast<i64>(index) * 4 / 5);
    }
    AUREA_CHECK(!scheduler.frame(108001).ok());
}

AUREA_TEST(ExportScheduler, PressureBoundsAdmissionAndRecoveryHasHysteresis) {
    AureaRenderScheduler scheduler;
    AUREA_CHECK(scheduler.configure(100, 60, 30, 4).ok());
    constexpr u64 second = 1'000'000'000ull;
    AUREA_CHECK_EQ(scheduler.admission_limit(.4f, false, false, second), 4u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.88f, false, false, 2 * second), 2u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.7f, false, false, 3 * second), 2u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.5f, false, false, 4 * second), 2u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.5f, false, false, 5 * second), 2u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.5f, false, false, 6 * second), 4u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.4f, true, false, 7 * second), 1u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.4f, false, false, 8 * second), 1u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.8f, false, false, 9 * second), 1u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.4f, false, false, 10 * second), 1u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.4f, false, false, 12 * second), 4u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.4f, false, true, 13 * second), 1u);
    AUREA_CHECK_EQ(scheduler.admission_limit(.4f, false, false, 14 * second), 4u);
    AUREA_CHECK_EQ(scheduler.admission_limit(std::numeric_limits<f32>::quiet_NaN(), false, false, 15 * second), 1u);
    // An admission change cannot alter a selected sample time.
    const auto plan = scheduler.frame(59);
    AUREA_CHECK(plan.ok());
    if (plan.ok()) { AUREA_CHECK_EQ(plan->ptsUs, 983333); AUREA_CHECK_EQ(plan->compositionFrame.value, 29); }
}

AUREA_TEST(ExportScheduler, InvalidAndUnrepresentableClocksAreRejected) {
    AureaRenderScheduler scheduler;
    AUREA_CHECK(!scheduler.configure(0, 30, 30, 1).ok());
    AUREA_CHECK(!scheduler.configure(1, 30, 30, 0).ok());
    AUREA_CHECK(!scheduler.configure(1, -1, 30, 1).ok());
    AUREA_CHECK(!scheduler.configure(1, 30, std::numeric_limits<f64>::infinity(), 1).ok());
    AUREA_CHECK(!scheduler.configure(1, std::numeric_limits<f64>::quiet_NaN(), 30, 1).ok());
    AUREA_CHECK(!scheduler.configure(1, 1e-20, 30, 1).ok());
    AUREA_CHECK(!scheduler.configure(1, 30, 30, 1, static_cast<ExportExecutionProfile>(3)).ok());
    AUREA_CHECK(!scheduler.frame(0).ok());
}

AUREA_TEST(ExportScheduler, ProfilesChangeResourcesAndKeepTheSameTimeline) {
    AureaRenderScheduler fast, balanced, quality;
    AUREA_CHECK(fast.configure(61, 60, 30, 4, ExportExecutionProfile::Fast).ok());
    AUREA_CHECK(balanced.configure(61, 60, 30, 4, ExportExecutionProfile::Balanced).ok());
    AUREA_CHECK(quality.configure(61, 60, 30, 4, ExportExecutionProfile::HighQuality).ok());
    AUREA_CHECK_EQ(quality.capacity(), 1u);
    constexpr u64 second = 1'000'000'000ull;
    AUREA_CHECK_EQ(fast.admission_limit(.96f, false, false, second), 1u);
    AUREA_CHECK_EQ(balanced.admission_limit(.96f, false, false, second), 1u);
    AUREA_CHECK_EQ(fast.admission_limit(.67f, false, false, 2 * second), 1u);
    AUREA_CHECK_EQ(fast.admission_limit(.67f, false, false, 3 * second), 4u);
    AUREA_CHECK_EQ(balanced.admission_limit(.67f, false, false, 3 * second), 1u);
    AUREA_CHECK_EQ(quality.admission_limit(0, false, false, 3 * second), 1u);
    for (u32 i = 0; i <= 60; ++i) {
        const auto a = fast.frame(i), b = balanced.frame(i), c = quality.frame(i);
        AUREA_CHECK(a.ok() && b.ok() && c.ok());
        if (!a.ok() || !b.ok() || !c.ok()) continue;
        AUREA_CHECK_EQ(a->ptsUs, b->ptsUs); AUREA_CHECK_EQ(a->ptsUs, c->ptsUs);
        AUREA_CHECK_EQ(a->compositionFrame.value, b->compositionFrame.value);
        AUREA_CHECK_EQ(a->compositionFrame.value, c->compositionFrame.value);
    }
}
