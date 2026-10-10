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

struct StartupState {
    // These must outlive the sink, including its destructor's abort callback.
    std::atomic<bool> cancel{false};
    std::atomic<u64> beat{0};
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false, abandoned = false;
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

// A single monitor per opened session, sleeping while no sink call is active.
// The sink ALWAYS sees state-owned atomics. Borrowed Engine pointers are read
// under this mutex only between a proxy call's binding and unbinding.
class OpenedSink final : public ExportSink {
public:
    explicit OpenedSink(std::shared_ptr<StartupState> state, u64 pollNs)
        : state_(std::move(state)), pollNs_(pollNs), monitor_([this] { monitor(); }) {}

    ~OpenedSink() override {
        {
            std::lock_guard lock(bindMutex_);
            stopping_ = true;
            desiredCancel_ = boundCancel_ = nullptr;
            desiredBeat_ = boundBeat_ = nullptr;
        }
        bindingsChanged_.notify_all();
        if (monitor_.joinable()) monitor_.join();
        abort_staged(*state_);
    }

    void set_cancel_flag(const std::atomic<bool>* flag) noexcept override {
        std::lock_guard lock(bindMutex_); desiredCancel_ = flag;
    }
    void set_heartbeat(std::atomic<u64>* beat) noexcept override {
        std::lock_guard lock(bindMutex_); desiredBeat_ = beat;
    }
    Status open(const char*, const VideoStreamConfig&, const AudioStreamConfig*) noexcept override {
        return Status{Errc::InvalidState, "encoder ja foi aberto pelo gate de exportacao"};
    }
    Status write_video(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts) noexcept override {
        Binding binding(*this);
        return state_->sink->write_video(y, ys, uv, uvs, pts);
    }
    Status write_video_timed(const u8* y, u32 ys, const u8* uv, u32 uvs, i64 pts, i64 duration) noexcept override {
        Binding binding(*this);
        return state_->sink->write_video_timed(y, ys, uv, uvs, pts, duration);
    }
    Status write_audio(const i16* pcm, u32 frames, i64 pts) noexcept override {
        Binding binding(*this);
        return state_->sink->write_audio(pcm, frames, pts);
    }
    Status finish() noexcept override {
        Binding binding(*this);
        if (finalized_) return Status{Errc::InvalidState, "export ja foi finalizado"};
        if (const Status status = state_->sink->finish(); !status.ok()) return status;
        finalized_ = true;
        if (state_->video.validateBeforePublish) return OkStatus;
        // The shared commit replaces an existing target atomically on Windows
        // and POSIX, preserving its bytes if publication fails.
        if (const Status status = fileio::commit_file(state_->staged, state_->target); !status.ok()) return status;
        published_ = true;
        return OkStatus;
    }
    Status validate_output(const ExportOutputValidation& expected) noexcept override {
        Binding binding(*this);
        if (!finalized_ || published_ || !state_->video.validateBeforePublish) return Errc::InvalidState;
        if (const Status status = state_->sink->validate_output(expected); !status.ok()) return status;
        if (state_->cancel.load(std::memory_order_acquire)) return Errc::Cancelled;
        if (const Status status = fileio::commit_file(state_->staged, state_->target); !status.ok()) return status;
        published_ = true;
        return OkStatus;
    }
    void abort() noexcept override {
        Binding binding(*this);
        abort_staged(*state_);
    }
    EncoderInfo encoder_info() const noexcept override { return state_->sink->encoder_info(); }

private:
    class Binding {
    public:
        explicit Binding(OpenedSink& sink) : sink_(sink) {
            std::lock_guard lock(sink_.bindMutex_);
            sink_.boundCancel_ = sink_.desiredCancel_;
            sink_.boundBeat_ = sink_.desiredBeat_;
            sink_.state_->cancel.store(sink_.boundCancel_ && sink_.boundCancel_->load(std::memory_order_acquire),
                                      std::memory_order_release);
            sink_.state_->beat.store(monotonic_ns(), std::memory_order_release);
            sink_.active_ = true;
            sink_.sync_bindings();
            sink_.bindingsChanged_.notify_all();
        }
        ~Binding() {
            std::lock_guard lock(sink_.bindMutex_);
            sink_.sync_bindings();
            sink_.active_ = false;
            sink_.boundCancel_ = nullptr;
            sink_.boundBeat_ = nullptr;
            sink_.bindingsChanged_.notify_all();
        }
    private:
        OpenedSink& sink_;
    };
    void sync_bindings() noexcept {
        if (boundCancel_ && boundCancel_->load(std::memory_order_acquire))
            state_->cancel.store(true, std::memory_order_release);
        if (boundBeat_) boundBeat_->store(state_->beat.load(std::memory_order_acquire), std::memory_order_release);
    }
    void monitor() noexcept {
        std::unique_lock lock(bindMutex_);
        while (!stopping_) {
            bindingsChanged_.wait(lock, [this] { return stopping_ || active_; });
            while (!stopping_ && active_) {
                sync_bindings();
                bindingsChanged_.wait_for(lock, std::chrono::nanoseconds(pollNs_),
                    [this] { return stopping_ || !active_; });
            }
        }
    }

    std::shared_ptr<StartupState> state_;
    u64 pollNs_;
    std::mutex bindMutex_;
    std::condition_variable bindingsChanged_;
    const std::atomic<bool>* desiredCancel_ = nullptr;
    std::atomic<u64>* desiredBeat_ = nullptr;
    const std::atomic<bool>* boundCancel_ = nullptr;
    std::atomic<u64>* boundBeat_ = nullptr;
    bool active_ = false, stopping_ = false, published_ = false, finalized_ = false;
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
            if (state->abandoned) {
                lock.unlock();
                abort_staged(*state);
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
                result.code = cancelled ? Errc::Cancelled : Errc::Timeout;
                copy_detail(result.detail, sizeof(result.detail), Status{result.code,
                    cancelled ? "abertura do encoder cancelada" : "encoder parou durante a abertura"});
                lock.unlock(); worker.detach();
                return result;
            }
            state->changed.wait_for(lock, std::chrono::nanoseconds(limits.pollNs));
        }
        lock.unlock(); worker.join();
        if (cancellation && cancellation->load(std::memory_order_acquire)) {
            abort_staged(*state);
            result.code = Errc::Cancelled;
            copy_detail(result.detail, sizeof(result.detail), Status{result.code, "abertura do encoder cancelada"});
            return result;
        }
        result.code = state->code;
        std::snprintf(result.detail, sizeof(result.detail), "%s", state->detail);
        if (result.code != Errc::Ok) {
            abort_staged(*state);
            return result;
        }
        result.sink = std::make_unique<OpenedSink>(state, limits.pollNs);
        result.sink->set_cancel_flag(cancellation);
        result.sink->set_heartbeat(heartbeat);
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
        worker.detach();
    } else if (state && state->sink) {
        abort_staged(*state);
    }
    copy_detail(result.detail, sizeof(result.detail), Status{result.code, "nao consegui iniciar o worker do encoder"});
    return result;
}

} // namespace aurea
