#include "aurea/export/ExportSinkStartup.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/FileIO.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace aurea {
namespace {

// A stalled native open/close still owns its codec and atomics. The permit
// survives success through OpenedSink and abandonment through the detached
// worker; another attempt must not accumulate unbounded native sessions.
class NativeSessionPermit final {
public:
    bool acquire() noexcept {
        auto& count = live_sessions();
        u32 observed = count.load(std::memory_order_acquire);
        while (observed < 2) {
            if (count.compare_exchange_weak(observed, observed + 1,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                acquired_ = true;
                return true;
            }
        }
        return false;
    }
    ~NativeSessionPermit() {
        if (acquired_) live_sessions().fetch_sub(1, std::memory_order_acq_rel);
    }
private:
    static std::atomic<u32>& live_sessions() noexcept {
        static std::atomic<u32> count{0};
        return count;
    }
    bool acquired_ = false;
};

struct StartupState {
    // FIRST member is destroyed LAST: release admission only after the native
    // sink destructor, borrowed-flag owners and synchronization objects finish.
    NativeSessionPermit permit;
    // These must outlive the sink, including its destructor's abort callback.
    std::atomic<bool> cancel{false};
    std::atomic<u64> beat{0};
    std::mutex mutex;
    std::condition_variable changed;
    bool openReturned = false, accepted = false, done = false, abandoned = false;
    Errc code = Errc::Ok;
    char detail[256]{};
    std::string target, staged;
    VideoStreamConfig video;
    AudioStreamConfig audio;
    bool hasAudio = false;
    std::unique_ptr<ExportSink> sink;
};

void copy_detail(char* out, usize capacity, Status status) noexcept {
    const auto text = status.detail().empty() ? status.message() : status.detail();
    std::snprintf(out, capacity, "%.*s", static_cast<int>(std::min(text.size(), capacity - 1)), text.data());
}

void remove_staged(const StartupState& state) noexcept {
    try {
        std::error_code error;
        std::filesystem::remove(std::filesystem::u8path(state.staged), error);
    } catch (...) {} // platform abort has already attempted cleanup
}

void abort_staged(StartupState& state) noexcept {
    state.cancel.store(true, std::memory_order_release);
    // iOS may retain its writer host after a bounded abort returns. Detach its
    // borrowed atomics before destroying this state; native cleanup has no
    // need to report progress to a caller that has already returned.
    state.sink->set_cancel_flag(nullptr);
    state.sink->set_heartbeat(nullptr);
    state.sink->abort();
    remove_staged(state);
}

struct ProxyBindings {
    std::mutex mutex;
    std::condition_variable changed;
    const std::atomic<bool>* desiredCancel = nullptr;
    std::atomic<u64>* desiredBeat = nullptr;
    const std::atomic<bool>* boundCancel = nullptr;
    std::atomic<u64>* boundBeat = nullptr;
    bool active = false, stopping = false, published = false, retired = false;
};

// A single monitor per opened session, sleeping while no sink call is active.
// It also owns final native retirement. It captures shared state, never the
// proxy address: a delayed stop/delete remains safe after the caller leaves.
// Cleanup reuses this existing thread and never allocates a new native worker
// while the device is already under memory pressure.
class OpenedSink final : public ExportSink {
public:
    explicit OpenedSink(std::shared_ptr<StartupState> state, u64 pollNs)
        : state_(std::move(state)), bindings_(std::make_shared<ProxyBindings>()),
          monitor_([state = state_, bindings = bindings_, pollNs] { monitor(state, bindings, pollNs); }) {}

    ~OpenedSink() override { retire(); }

    void set_cancel_flag(const std::atomic<bool>* flag) noexcept override {
        std::lock_guard lock(bindings_->mutex);
        if (!bindings_->stopping) bindings_->desiredCancel = flag;
    }
    void set_heartbeat(std::atomic<u64>* beat) noexcept override {
        std::lock_guard lock(bindings_->mutex);
        if (!bindings_->stopping) bindings_->desiredBeat = beat;
    }
    void set_diagnostics(std::shared_ptr<ExportDiagnostics> diagnostics) noexcept override {
        if (!retirementRequested_) state_->sink->set_diagnostics(std::move(diagnostics));
    }
    Status open(const char*, const VideoStreamConfig&, const AudioStreamConfig*) noexcept override {
        return Status{Errc::InvalidState, "encoder ja foi aberto pelo gate de exportacao"};
    }
    Status write_video(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts) noexcept override {
        if (retirementRequested_) return Errc::InvalidState;
        Binding binding(*this);
        return state_->sink->write_video(y, ys, uv, uvs, pts);
    }
    Status write_video_timed(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts, i64 duration) noexcept override {
        if (retirementRequested_) return Errc::InvalidState;
        Binding binding(*this);
        return state_->sink->write_video_timed(y, ys, uv, uvs, pts, duration);
    }
    Status write_audio(const i16* pcm, u32 frames, i64 pts) noexcept override {
        if (retirementRequested_) return Errc::InvalidState;
        Binding binding(*this);
        return state_->sink->write_audio(pcm, frames, pts);
    }
    Status finish() noexcept override {
        if (retirementRequested_) return Errc::InvalidState;
        Binding binding(*this);
        if (finalized_) return Status{Errc::InvalidState, "export ja foi finalizado"};
        if (const Status status = state_->sink->finish(); !status.ok()) return status;
        finalized_ = true;
        if (state_->video.validateBeforePublish) return OkStatus;
        if (cancelled_before_publication()) return Errc::Cancelled;
        // The shared commit replaces an existing target atomically on Windows
        // and POSIX, preserving its bytes if publication fails.
        if (const Status status = fileio::commit_file(state_->staged, state_->target); !status.ok()) return status;
        published_ = true;
        return OkStatus;
    }
    Status validate_output(const ExportOutputValidation& expected) noexcept override {
        if (retirementRequested_) return Errc::InvalidState;
        Binding binding(*this);
        if (!finalized_ || published_ || !state_->video.validateBeforePublish) return Errc::InvalidState;
        if (const Status status = state_->sink->validate_output(expected); !status.ok()) return status;
        if (cancelled_before_publication()) return Errc::Cancelled;
        if (const Status status = fileio::commit_file(state_->staged, state_->target); !status.ok()) return status;
        published_ = true;
        return OkStatus;
    }
    void abort() noexcept override { retire(); }
    EncoderInfo encoder_info() const noexcept override {
        return retirementRequested_ ? EncoderInfo{} : state_->sink->encoder_info();
    }

private:
    class Binding {
    public:
        explicit Binding(OpenedSink& sink) : sink_(sink) {
            auto& bindings = *sink_.bindings_;
            std::lock_guard lock(bindings.mutex);
            bindings.boundCancel = bindings.desiredCancel;
            bindings.boundBeat = bindings.desiredBeat;
            sink_.state_->cancel.store(bindings.boundCancel && bindings.boundCancel->load(std::memory_order_acquire),
                                      std::memory_order_release);
            sink_.state_->beat.store(monotonic_ns(), std::memory_order_release);
            bindings.active = true;
            sync_bindings(*sink_.state_, bindings);
            bindings.changed.notify_all();
        }
        ~Binding() {
            auto& bindings = *sink_.bindings_;
            std::lock_guard lock(bindings.mutex);
            sync_bindings(*sink_.state_, bindings);
            bindings.active = false;
            bindings.boundCancel = nullptr;
            bindings.boundBeat = nullptr;
            bindings.changed.notify_all();
        }
    private:
        OpenedSink& sink_;
    };
    static void sync_bindings(StartupState& state, ProxyBindings& bindings) noexcept {
        if (bindings.boundCancel && bindings.boundCancel->load(std::memory_order_acquire))
            state.cancel.store(true, std::memory_order_release);
        if (bindings.boundBeat) bindings.boundBeat->store(state.beat.load(std::memory_order_acquire), std::memory_order_release);
    }
    bool cancelled_before_publication() noexcept {
        // Native finish/validation may return immediately after the Engine
        // watchdog cancelled this session, before the polling monitor wakes.
        // The borrowed flag is valid throughout Binding; refresh it now,
        // while it is still bound, rather than after committing the file.
        std::lock_guard lock(bindings_->mutex);
        sync_bindings(*state_, *bindings_);
        return state_->cancel.load(std::memory_order_acquire);
    }
    static void monitor(const std::shared_ptr<StartupState>& state,
        const std::shared_ptr<ProxyBindings>& bindings, u64 pollNs) noexcept {
        std::unique_lock lock(bindings->mutex);
        while (!bindings->stopping) {
            bindings->changed.wait(lock, [&] { return bindings->stopping || bindings->active; });
            while (!bindings->stopping && bindings->active) {
                sync_bindings(*state, *bindings);
                bindings->changed.wait_for(lock, std::chrono::nanoseconds(pollNs),
                    [&] { return bindings->stopping || !bindings->active; });
            }
        }
        const bool published = bindings->published;
        lock.unlock();
        if (!published) abort_staged(*state);
        else {
            state->sink->set_cancel_flag(nullptr);
            state->sink->set_heartbeat(nullptr);
        }
        state->sink.reset(); // native destructor stays on the retained worker
        lock.lock(); bindings->retired = true;
        lock.unlock(); bindings->changed.notify_all();
    }
    void retire() noexcept {
        if (retirementRequested_) return;
        retirementRequested_ = true;
        state_->cancel.store(true, std::memory_order_release);
        std::unique_lock lock(bindings_->mutex);
        bindings_->stopping = true;
        bindings_->published = published_;
        bindings_->desiredCancel = bindings_->boundCancel = nullptr;
        bindings_->desiredBeat = bindings_->boundBeat = nullptr;
        bindings_->changed.notify_all();
        // Normal close completes here. A stuck native stop/delete cannot hold
        // startup, render/lifecycle locks or final progress beyond this wait.
        // The admission permit remains owned until native destruction ends.
        const bool retired = bindings_->changed.wait_for(lock, std::chrono::milliseconds(100),
            [&] { return bindings_->retired; });
        lock.unlock();
        if (retired) monitor_.join();
        else monitor_.detach();
    }

    std::shared_ptr<StartupState> state_;
    std::shared_ptr<ProxyBindings> bindings_;
    bool retirementRequested_ = false, published_ = false, finalized_ = false;
    std::thread monitor_;
};

} // namespace

ExportSinkStartupResult open_export_sink_startup(std::unique_ptr<ExportSink> sink, const char* outputPath,
    const VideoStreamConfig& video, const AudioStreamConfig* audio,
    const std::atomic<bool>* cancellation, std::atomic<u64>* heartbeat, ExportSinkStartupLimits limits) noexcept {
    ExportSinkStartupResult result;
    if (!sink || !outputPath || !*outputPath || !limits.stallNs || !limits.hardNs || !limits.pollNs) {
        result.code = Errc::InvalidArgument;
        copy_detail(result.detail, sizeof(result.detail), Status{result.code, "configuracao invalida do gate de exportacao"});
        return result;
    }
    std::shared_ptr<StartupState> state;
    std::thread worker;
    try {
        state = std::make_shared<StartupState>();
        if (!state->permit.acquire()) {
            result.code = Errc::BudgetExceeded;
            copy_detail(result.detail, sizeof(result.detail), Status{result.code,
                "duas sessoes nativas de exportacao ainda abertas; aguarde os encoders encerrarem"});
            return result; // no worker, native open or staging file is created
        }
        state->sink = std::move(sink);
        state->target = outputPath;
        static std::atomic<u64> generation{0};
        const auto pid =
#if defined(_WIN32)
            _getpid();
#else
            getpid();
#endif
        state->staged = state->target + ".aurea-startup-" + std::to_string(pid) + "-" +
            std::to_string(monotonic_ns()) + "-" + std::to_string(++generation) + ".mp4";
        state->video = video;
        state->hasAudio = audio != nullptr;
        if (audio) state->audio = *audio;
        state->beat.store(monotonic_ns(), std::memory_order_release);
        state->sink->set_cancel_flag(&state->cancel);
        state->sink->set_heartbeat(&state->beat);
        const u64 started = monotonic_ns();
        worker = std::thread([state] {
            const Status status = state->sink->open(state->staged.c_str(), state->video, state->hasAudio ? &state->audio : nullptr);
            std::unique_lock lock(state->mutex);
            state->code = status.code();
            copy_detail(state->detail, sizeof(state->detail), status);
            state->openReturned = true;
            state->changed.notify_all();
            // A successful native open stays with this worker until the
            // caller can construct its proxy. A failure, cancellation or
            // constructor exception retires the codec HERE, under the same
            // startup deadline; neither abort nor the platform destructor
            // may become an unbounded call on the export starter.
            if (status.ok() && !state->abandoned) {
                state->changed.wait(lock, [&] { return state->accepted || state->abandoned; });
            }
            if (!state->accepted) {
                lock.unlock();
                abort_staged(*state);
                state->sink.reset(); // native delete/stop can stall too
                lock.lock();
            }
            state->done = true;
            lock.unlock(); state->changed.notify_all();
        });
        std::unique_lock lock(state->mutex);
        while (!state->done) {
            const u64 now = monotonic_ns();
            const u64 beat = state->beat.load(std::memory_order_acquire);
            if (heartbeat) heartbeat->store(beat, std::memory_order_release);
            const bool cancelled = cancellation && cancellation->load(std::memory_order_acquire);
            if (cancelled || now - started > limits.hardNs || (now >= beat && now - beat > limits.stallNs)) {
                state->abandoned = true;
                state->cancel.store(true, std::memory_order_release);
                // Native open may already have failed while stop/delete is
                // still blocked. Keep its actionable error and copied detail
                // rather than replacing them with a teardown timeout.
                if (!cancelled && state->openReturned && state->code != Errc::Ok) {
                    result.code = state->code;
                    std::snprintf(result.detail, sizeof(result.detail), "%s", state->detail);
                } else {
                    result.code = cancelled ? Errc::Cancelled : Errc::Timeout;
                    copy_detail(result.detail, sizeof(result.detail), Status{result.code,
                        cancelled ? "abertura do encoder cancelada" : "encoder parou durante a abertura"});
                }
                lock.unlock(); state->changed.notify_all(); worker.detach();
                return result;
            }
            if (state->openReturned && state->code == Errc::Ok) {
                result.sink = std::make_unique<OpenedSink>(state, limits.pollNs);
                result.sink->set_cancel_flag(cancellation);
                result.sink->set_heartbeat(heartbeat);
                state->accepted = true;
                lock.unlock(); state->changed.notify_all();
                worker.join(); // accepted worker no longer calls the platform
                return result;
            }
            state->changed.wait_for(lock, std::chrono::nanoseconds(limits.pollNs));
        }
        lock.unlock(); worker.join();
        if (cancellation && cancellation->load(std::memory_order_acquire)) {
            result.code = Errc::Cancelled;
            copy_detail(result.detail, sizeof(result.detail), Status{result.code, "abertura do encoder cancelada"});
            return result;
        }
        result.code = state->code;
        std::snprintf(result.detail, sizeof(result.detail), "%s", state->detail);
        return result;
    } catch (const std::bad_alloc&) {
        result.code = Errc::OutOfMemory;
    } catch (...) {
        result.code = Errc::IoError;
    }
    // Exceptions are confined to setup/monitor creation. A live open worker is
    // never destroyed or aborted by the caller while it accesses its sink.
    if (worker.joinable()) {
        { std::lock_guard lock(state->mutex); state->abandoned = true; }
        state->cancel.store(true, std::memory_order_release);
        state->changed.notify_all();
        worker.detach();
    } else if (state && state->sink) {
        abort_staged(*state);
    }
    copy_detail(result.detail, sizeof(result.detail), Status{result.code, "nao consegui iniciar o worker do encoder"});
    return result;
}

} // namespace aurea
