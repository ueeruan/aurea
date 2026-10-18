#include "aurea/export/ExportDiagnostics.hpp"
#include <fstream>

AUREA_TEST(ExportDiagnosticsProbe, JsonRecordsCountersAndFirstFrameWithoutPaths) {
    auto evidence = std::make_shared<aurea::ExportDiagnostics>();
    aurea::ExportDiagnostics::Format format;
    format.width = 64; format.height = 36; format.fps = 30; format.bitrate = 800000;
    evidence->format(format);
    evidence->codec("test.encoder", "controlled.fixture", 21, 64, 36);
    evidence->stage(aurea::ExportDiagnostics::Lane::Session, aurea::ExportDiagnostics::Stage::CodecOpen);
    evidence->stage(aurea::ExportDiagnostics::Lane::Producer, aurea::ExportDiagnostics::Stage::Readback);
    evidence->event(aurea::ExportDiagnostics::Counter::Rendered);
    evidence->event(aurea::ExportDiagnostics::Counter::Submitted);
    evidence->event(aurea::ExportDiagnostics::Counter::VideoPackets);
    evidence->event(aurea::ExportDiagnostics::Counter::AudioPackets);
    evidence->event(aurea::ExportDiagnostics::Counter::InputAgain);
    evidence->event(aurea::ExportDiagnostics::Counter::OutputAgain);
    const auto json = evidence->json();
    AUREA_CHECK(json.find("\"submitted\":1") != std::string::npos);
    AUREA_CHECK(json.find("\"videoPackets\":1") != std::string::npos);
    AUREA_CHECK(json.find("\"audioPackets\":1") != std::string::npos);
    AUREA_CHECK(json.find("\"firstInputMs\":-1") == std::string::npos);
    AUREA_CHECK(json.find("\"firstPacketMs\":-1") == std::string::npos);
    AUREA_CHECK(json.find("\"sessionToken\":\"") != std::string::npos);
    AUREA_CHECK(json.find("\"negotiatedColor\":null") != std::string::npos);
    AUREA_CHECK(json.find("path") == std::string::npos);
    AUREA_CHECK(json.find(".mp4") == std::string::npos);
    { std::ofstream output("diagnostics-header-counters.json"); output << json; }
}

#if defined(AUREA_TEST_VULKAN) || defined(AUREA_TEST_METAL)
AUREA_TEST(ExportDiagnosticsProbe, ConcurrentRejectedAdmissionCannotFinalizeAnotherCallersRecord) {
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 72; cfg.frameCount = 3;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    std::atomic<int> mode{0};
    std::atomic<bool> firstFrameWait{false}, admissionEntered{false}, releaseAdmission{false};
    std::atomic<bool> allowNewWrite{false}, priorSinkDestroyed{false}, secondStarted{false};
    backend->beforeWaitFrame = [&](u64 frame, u64 timeout) -> Status {
        if (!frame) return OkStatus;
        if (mode.load() == 0) {
            firstFrameWait.store(true);
            if (timeout) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return Errc::Timeout;
        }
        if (mode.load() == 1) {
            admissionEntered.store(true);
            const auto safety = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!releaseAdmission.load() && std::chrono::steady_clock::now() < safety)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return OkStatus;
    };
    Rig r(cfg, 30, 3, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) { mode = 2; return; }
    struct ReleaseOnExit { std::atomic<int>& mode; ~ReleaseOnExit() { mode = 2; } } end{mode};
    r.comp()->layer(r.video_layer())->visible = false;
    r.cap.sinkDestroyed = &priorSinkDestroyed;
    ExportSettings settings; settings.height = 72; settings.fps = 30;
    AUREA_CHECK(r.e.start_export(settings, "diagnostics-concurrency-prior.mp4").ok());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!firstFrameWait.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(firstFrameWait.load());
    r.e.cancel_export();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!r.e.export_progress().finished && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(r.e.export_progress().finished);
    while (!priorSinkDestroyed.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(priorSinkDestroyed.load());
    mode = 1;
    r.cap.allowWrite = &allowNewWrite;
    r.cap.sinkDestroyed = nullptr;
    Status first, second;
    std::thread primary([&] { first = r.e.start_export(settings, "diagnostics-concurrency-primary.mp4"); });
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!admissionEntered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(admissionEntered.load());
    std::thread rejected([&] {
        secondStarted.store(true);
        second = r.e.start_export(settings, "diagnostics-concurrency-rejected.mp4");
    });
    while (!secondStarted.load()) std::this_thread::yield();
    // The primary is held inside the pre-context fence hook. The second can
    // read the previous diagnostics but cannot pass the lifecycle mutex yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    mode = 2; releaseAdmission = true;
    primary.join(); rejected.join();
    AUREA_CHECK(first.ok()); AUREA_CHECK(second.code() == Errc::InvalidState);
    const auto progress = r.e.export_progress();
    AUREA_CHECK(progress.running && !progress.finished);
    const auto json = r.e.export_diagnostics();
    std::printf(" primary running=%d rejected=%d diagnostic=%s ", progress.running, second.raw(), json.c_str());
    AUREA_CHECK(json.find("\"hasResult\":false") != std::string::npos);
    AUREA_CHECK(json.find("\"sessionStage\":\"failed\"") == std::string::npos);
    { std::ofstream output("diagnostics-concurrent-rejected-admission.json"); output << json; }
    allowNewWrite = true;
    r.e.cancel_export();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!r.e.export_progress().finished && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(r.e.export_progress().finished);
}

AUREA_TEST(ExportDiagnosticsProbe, ReadingEngineDiagnosticRemainsAvailableDuringBlockedCodecOpen) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    std::atomic<bool> entered{false}, releaseOpen{false}, destroyed{false};
    r.cap.sinkDestroyed = &destroyed;
    r.cap.beforeOpen = [&]() -> Status {
        entered.store(true, std::memory_order_release);
        const auto safetyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!releaseOpen.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < safetyDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return OkStatus;
    };
    ExportSettings settings; settings.height = 36; settings.fps = 30;
    Status startup;
    std::thread starter([&] { startup = r.e.start_export(settings, "diagnostics-private-fixture.mp4"); });
    const auto enteredDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < enteredDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(entered.load());
    const u64 started = monotonic_ns();
    std::string json;
    for (u32 i = 0; i < 100; ++i) json = r.e.export_diagnostics();
    const u64 elapsed = monotonic_ns() - started;
    std::printf(" 100 independent diagnostic reads while native open blocked: %.3fms ",
        static_cast<double>(elapsed) / 1'000'000.0);
    AUREA_CHECK(elapsed < 500'000'000ull);
    AUREA_CHECK(json.find("\"sessionStage\":\"codec_open\"") != std::string::npos);
    AUREA_CHECK(json.find("\"hasResult\":false") != std::string::npos);
    AUREA_CHECK(json.find("diagnostics-private-fixture") == std::string::npos);
    AUREA_CHECK(json.find("path") == std::string::npos);
    { std::ofstream output("diagnostics-engine-open-blocked.json"); output << json; }
    r.e.cancel_export();
    starter.join();
    releaseOpen.store(true, std::memory_order_release);
    const auto cleanupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!destroyed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < cleanupDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(destroyed.load()); AUREA_CHECK(startup.code() == Errc::Cancelled);
    const auto completed = r.e.export_diagnostics();
    AUREA_CHECK(completed.find("\"sessionStage\":\"cancelled\"") != std::string::npos);
    AUREA_CHECK(completed.find("\"result\":25") != std::string::npos);
    { std::ofstream output("diagnostics-engine-open-cancelled.json"); output << completed; }
}
#endif
