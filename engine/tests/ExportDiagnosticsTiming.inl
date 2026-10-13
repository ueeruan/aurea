#include "aurea/export/ExportDiagnostics.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace export_diagnostics_timing_test {
unsigned long long value(const std::string& json, const char* token) {
    const char* found = std::strstr(json.c_str(), token);
    if (!found) return ~0ull;
    return std::strtoull(found + std::strlen(token), nullptr, 10);
}
}

AUREA_TEST(ExportDiagnosticsTiming, ActiveWaitAdvancesWithoutBlockingTheReader) {
    using namespace aurea;
    ExportDiagnostics diagnostics;
    diagnostics.stage(ExportDiagnostics::Lane::Producer, ExportDiagnostics::Stage::Readback);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto before = diagnostics.json();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto after = diagnostics.json();
    using export_diagnostics_timing_test::value;
    AUREA_CHECK(value(after, "\"elapsedMs\":") >= value(before, "\"elapsedMs\":") + 80);
    AUREA_CHECK(value(after, "\"producerWaitMs\":") >= value(before, "\"producerWaitMs\":") + 80);
    AUREA_CHECK(after.find("\"hasResult\":false") != std::string::npos);
    AUREA_CHECK(after.find("\"producerStage\":\"readback\"") != std::string::npos);
}

AUREA_TEST(ExportDiagnosticsTiming, TerminalResultFreezesTheMeasuredSessionAndStageWaits) {
    using namespace aurea;
    using export_diagnostics_timing_test::value;
    using Stage = ExportDiagnostics::Stage;
    for (const auto terminal : {Stage::Complete, Stage::Failed, Stage::Cancelled}) {
        ExportDiagnostics diagnostics;
        diagnostics.stage(ExportDiagnostics::Lane::Producer, Stage::Readback);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        diagnostics.result(terminal == Stage::Complete ? 0 : 26);
        diagnostics.stage(ExportDiagnostics::Lane::Session, terminal);
        const auto before = diagnostics.json();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // A late native callback must not rewrite the session that has already
        // terminated. This is distinct from the active lane above.
        diagnostics.event(ExportDiagnostics::Counter::Submitted);
        diagnostics.stage(ExportDiagnostics::Lane::Producer, Stage::VideoInput);
        diagnostics.codec("late-codec", "late-backend", 21);
        diagnostics.format({1920, 1080, 12000000, 1, 0, 30.0, 21});
        diagnostics.result(99);
        const auto after = diagnostics.json();
        std::printf("terminal=%u elapsed=%llu->%llu readback_wait=%llu->%llu ",
            static_cast<unsigned>(terminal), value(before, "\"elapsedMs\":"),
            value(after, "\"elapsedMs\":"), value(before, "\"producerWaitMs\":"),
            value(after, "\"producerWaitMs\":"));
        AUREA_CHECK_EQ(value(after, "\"elapsedMs\":"), value(before, "\"elapsedMs\":"));
        AUREA_CHECK_EQ(value(after, "\"producerWaitMs\":"), value(before, "\"producerWaitMs\":"));
        AUREA_CHECK_EQ(value(after, "\"sessionWaitMs\":"), value(before, "\"sessionWaitMs\":"));
        AUREA_CHECK(after.find("\"hasResult\":true") != std::string::npos);
        AUREA_CHECK_EQ(after, before);
    }
}
