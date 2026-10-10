#include "TestFramework.hpp"
#include "aurea/export/ExportSinkStartup.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/FileIO.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

using namespace aurea;
namespace {
struct Control {
    std::mutex mutex;
    std::condition_variable changed;
    bool release = false, blockOpen = false, progressOpen = false, cancelWrite = false;
    bool failOpen = false, failFinish = false;
    std::atomic<bool> entered{false}, inOpen{false};
    std::atomic<u32> destroyed{0}, aborts{0}, concurrentAborts{0};
    u32 delayMs = 0;
    std::string staged, payload = "complete";
    const std::atomic<bool>* cancelAddress = nullptr;
};
class ControlledSink final : public ExportSink {
public:
    explicit ControlledSink(std::shared_ptr<Control> c) : control_(std::move(c)) {}
    ~ControlledSink() override { abort(); ++control_->destroyed; control_->changed.notify_all(); }
    void set_cancel_flag(const std::atomic<bool>* c) noexcept override { cancel_ = c; }
    void set_heartbeat(std::atomic<u64>* b) noexcept override { beat_ = b; }
    Status open(const char* path, const VideoStreamConfig&, const AudioStreamConfig*) noexcept override {
        control_->inOpen = true;
        {
            std::lock_guard lock(control_->mutex);
            control_->staged = path;
            control_->cancelAddress = cancel_;
        }
        { std::ofstream partial(path); partial << "partial"; }
        control_->entered = true; control_->changed.notify_all();
        if (control_->blockOpen) {
            std::unique_lock lock(control_->mutex);
            control_->changed.wait(lock, [&] { return control_->release; });
        }
        for (u32 elapsed = 0; elapsed < control_->delayMs; elapsed += 5) {
            if (control_->progressOpen && beat_) beat_->store(monotonic_ns());
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // This load is after the original caller may have destroyed its flags.
        if (cancel_) (void)cancel_->load();
        control_->inOpen = false;
        if (control_->failOpen) {
            std::snprintf(detail_, sizeof(detail_), "precise open failure");
            return Status{Errc::UnsupportedCodec, detail_};
        }
        return OkStatus;
    }
    Status write_video(const u8*, u32, const u8*, u32, i64) noexcept override {
        if (!control_->cancelWrite) return OkStatus;
        const u64 deadline = monotonic_ns() + 500'000'000;
        while (monotonic_ns() < deadline) {
            if (beat_) beat_->store(monotonic_ns());
            if (cancel_ && cancel_->load()) return Errc::Cancelled;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return Errc::Timeout;
    }
    Status write_audio(const i16*, u32, i64) noexcept override { return OkStatus; }
    Status finish() noexcept override {
        if (control_->failFinish) return Status{Errc::EncodeFailed, "precise finish failure"};
        std::ofstream complete(control_->staged, std::ios::trunc); complete << control_->payload;
        return complete.good() ? OkStatus : Status{Errc::IoError};
    }
    void abort() noexcept override {
        ++control_->aborts;
        if (control_->inOpen) ++control_->concurrentAborts;
        if (!control_->staged.empty()) {
            std::error_code error;
            std::filesystem::remove(control_->staged, error);
        }
        std::snprintf(detail_, sizeof(detail_), "destroyed diagnostic");
    }
private:
    std::shared_ptr<Control> control_;
    const std::atomic<bool>* cancel_ = nullptr;
    std::atomic<u64>* beat_ = nullptr;
    char detail_[96]{};
};
struct Files {
    std::filesystem::path dir = std::filesystem::current_path() / ("test-startup-" + std::to_string(monotonic_ns()));
    Files() { std::filesystem::create_directory(dir); }
    ~Files() { std::error_code error; std::filesystem::remove_all(dir, error); }
    std::string target() const { return (dir / "video.mp4").string(); }
};
ExportSinkStartupLimits limits(u64 hardMs = 1000) { return {50'000'000, hardMs * 1'000'000, 1'000'000}; }
ExportSinkStartupResult run(std::shared_ptr<Control> c, const std::string& path,
    const std::atomic<bool>* cancel = nullptr, std::atomic<u64>* beat = nullptr, ExportSinkStartupLimits l = limits()) {
    return open_export_sink_startup(std::make_unique<ControlledSink>(c), path.c_str(), VideoStreamConfig{}, nullptr, cancel, beat, l);
}
bool wait_destroyed(const std::shared_ptr<Control>& c) {
    std::unique_lock lock(c->mutex);
    return c->changed.wait_for(lock, std::chrono::seconds(2), [&] { return c->destroyed.load() != 0; });
}
void release(const std::shared_ptr<Control>& c) {
    { std::lock_guard lock(c->mutex); c->release = true; }
    c->changed.notify_all();
}
std::string contents(const std::string& path) {
    std::ifstream file(path); return std::string(std::istreambuf_iterator<char>(file), {});
}
}

AUREA_TEST(ExportSinkStartup, OpensStageAndPublishesOnlySuccessfulFinish) {
    Files files; auto c = std::make_shared<Control>();
    auto opened = run(c, files.target());
    AUREA_CHECK(opened.status().ok()); AUREA_CHECK(opened.sink != nullptr);
    AUREA_CHECK(c->staged != files.target());
    AUREA_CHECK(!std::filesystem::exists(files.target()));
    AUREA_CHECK(opened.sink->write_video(nullptr, 0, nullptr, 0, 0).ok());
    AUREA_CHECK(opened.sink->finish().ok());
    AUREA_CHECK_EQ(contents(files.target()), std::string("complete"));
    AUREA_CHECK(!std::filesystem::exists(c->staged));
    AUREA_CHECK(!opened.sink->finish().ok());
    opened.sink.reset();
    AUREA_CHECK_EQ(contents(files.target()), std::string("complete"));
}

AUREA_TEST(ExportSinkStartup, FailureDiagnosticsSurviveSinkDestructionAndResultMove) {
    Files files; auto c = std::make_shared<Control>(); c->failOpen = true;
    auto result = run(c, files.target());
    auto moved = std::move(result);
    AUREA_CHECK(moved.code == Errc::UnsupportedCodec);
    AUREA_CHECK_EQ(moved.status().detail(), std::string_view("precise open failure"));
    AUREA_CHECK(!moved.sink);
    AUREA_CHECK_EQ(c->destroyed.load(), 1u);
    AUREA_CHECK(!std::filesystem::exists(c->staged));
}

AUREA_TEST(ExportSinkStartup, HungOpenOwnsFlagsAndLateAbortCannotEraseRetryOutput) {
    Files files; auto blocked = std::make_shared<Control>(); blocked->blockOpen = true;
    auto externalCancel = std::make_unique<std::atomic<bool>>(false);
    auto externalBeat = std::make_unique<std::atomic<u64>>(0);
    const auto* oldAddress = externalCancel.get();
    auto result = run(blocked, files.target(), externalCancel.get(), externalBeat.get());
    AUREA_CHECK(result.code == Errc::Timeout); AUREA_CHECK(!result.sink);
    AUREA_CHECK(blocked->cancelAddress != oldAddress);
    AUREA_CHECK_EQ(blocked->aborts.load(), 0u);
    externalCancel.reset(); externalBeat.reset();
    auto retry = std::make_shared<Control>(); retry->payload = "successful retry";
    auto second = run(retry, files.target());
    AUREA_CHECK(second.status().ok()); AUREA_CHECK(second.sink->finish().ok());
    release(blocked); AUREA_CHECK(wait_destroyed(blocked));
    AUREA_CHECK_EQ(blocked->concurrentAborts.load(), 0u);
    AUREA_CHECK(!std::filesystem::exists(blocked->staged));
    AUREA_CHECK_EQ(contents(files.target()), std::string("successful retry"));
}

AUREA_TEST(ExportSinkStartup, HeartbeatExtendsStallDeadlineButNotAbsoluteDeadline) {
    Files files; auto healthy = std::make_shared<Control>(); healthy->progressOpen = true; healthy->delayMs = 150;
    auto success = run(healthy, files.target());
    AUREA_CHECK(success.status().ok()); success.sink.reset();
    auto endless = std::make_shared<Control>(); endless->progressOpen = true; endless->delayMs = 150;
    auto capped = run(endless, files.target(), nullptr, nullptr, limits(70));
    AUREA_CHECK(capped.code == Errc::Timeout);
    AUREA_CHECK(wait_destroyed(endless));
    AUREA_CHECK_EQ(endless->concurrentAborts.load(), 0u);
}

AUREA_TEST(ExportSinkStartup, CancelReturnsWithoutAbortingConcurrentOpen) {
    Files files; auto c = std::make_shared<Control>(); c->blockOpen = true;
    std::atomic<bool> cancel{false};
    std::thread user([&] {
        while (!c->entered) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancel = true;
    });
    auto result = run(c, files.target(), &cancel); user.join();
    AUREA_CHECK(result.code == Errc::Cancelled); AUREA_CHECK(!result.sink);
    AUREA_CHECK_EQ(c->aborts.load(), 0u);
    release(c); AUREA_CHECK(wait_destroyed(c));
    AUREA_CHECK_EQ(c->concurrentAborts.load(), 0u);
}

AUREA_TEST(ExportSinkStartup, ProxyBindsCancellationOnlyDuringCallsAndKeepsFinishErrors) {
    Files files; auto c = std::make_shared<Control>(); c->cancelWrite = true; c->failFinish = true;
    auto cancel = std::make_unique<std::atomic<bool>>(false);
    auto beat = std::make_unique<std::atomic<u64>>(0);
    auto result = run(c, files.target(), cancel.get(), beat.get());
    AUREA_CHECK(result.status().ok());
    std::thread user([&] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); *cancel = true; });
    AUREA_CHECK(result.sink->write_video(nullptr, 0, nullptr, 0, 0).code() == Errc::Cancelled);
    user.join(); AUREA_CHECK(beat->load() != 0);
    const auto failed = result.sink->finish();
    AUREA_CHECK(failed.code() == Errc::EncodeFailed);
    AUREA_CHECK_EQ(failed.detail(), std::string_view("precise finish failure"));
    AUREA_CHECK(!std::filesystem::exists(files.target()));
    cancel.reset(); beat.reset(); // idle proxy must never dereference these again
    result.sink.reset();
    AUREA_CHECK(!std::filesystem::exists(c->staged));
}

AUREA_TEST(ExportSinkStartup, PublishFailureNeverReportsSuccessOrDeletesAnExistingTarget) {
    Files files; std::filesystem::create_directory(files.target());
    auto c = std::make_shared<Control>(); auto result = run(c, files.target());
    AUREA_CHECK(result.status().ok());
    AUREA_CHECK(result.sink->finish().code() == Errc::IoError);
    result.sink.reset();
    AUREA_CHECK(std::filesystem::is_directory(files.target()));
    AUREA_CHECK(!std::filesystem::exists(c->staged));
}

AUREA_TEST(ExportSinkStartup, ExistingOutputChangesOnlyAfterSuccessfulPublication) {
    Files files;
    { std::ofstream previous(files.target()); previous << "previous complete export"; }
    auto c = std::make_shared<Control>(); auto result = run(c, files.target());
    AUREA_CHECK(result.status().ok());
    AUREA_CHECK_EQ(contents(files.target()), std::string("previous complete export"));
    AUREA_CHECK(result.sink->finish().ok());
    AUREA_CHECK_EQ(contents(files.target()), std::string("complete"));
    result.sink.reset();
    AUREA_CHECK_EQ(contents(files.target()), std::string("complete"));
}

AUREA_TEST(ExportSinkStartup, FailedAtomicCommitPreservesPreviousOutput) {
    Files files;
    { std::ofstream previous(files.target()); previous << "previous complete export"; }
    auto c = std::make_shared<Control>(); auto result = run(c, files.target());
    AUREA_CHECK(result.status().ok());
    fileio::FaultInjection fault; fault.kind = fileio::Fault::RenameFails; fault.pathContains = files.target();
    fileio::set_fault_injection(fault);
    const Status failed = result.sink->finish();
    const u32 injected = fileio::injected_failures(); fileio::clear_fault_injection();
    AUREA_CHECK(failed.code() == Errc::IoError);
    AUREA_CHECK_EQ(injected, 1u);
    AUREA_CHECK_EQ(contents(files.target()), std::string("previous complete export"));
    result.sink.reset();
    AUREA_CHECK_EQ(contents(files.target()), std::string("previous complete export"));
    AUREA_CHECK(!std::filesystem::exists(c->staged));
}
