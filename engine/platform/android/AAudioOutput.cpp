// =============================================================================
//  Aurea / platform / android / AAudioOutput.cpp
// =============================================================================
#include "AAudioOutput.hpp"

#include "aurea/core/Log.hpp"

#include <aaudio/AAudio.h>

#include <cstring>
#include <thread>
#include <time.h>

namespace aurea::android {
namespace {

aaudio_data_callback_result_t data_cb(AAudioStream*, void* user, void* audioData, int32_t numFrames) {
    static_cast<AAudioOutput*>(user)->on_data(static_cast<f32*>(audioData), numFrames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void error_cb(AAudioStream*, void* user, aaudio_result_t error) {
    AUREA_LOG_WARN("audio: stream desconectado (%s)", AAudio_convertResultToText(error));
    static_cast<AAudioOutput*>(user)->on_error();
}

} // namespace

AAudioOutput::~AAudioOutput() { close(); }

Status AAudioOutput::open(audio::AudioRenderFn fn, void* ctx) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    fn_ = fn;
    ctx_ = ctx;
    return open_stream_locked();
}

Status AAudioOutput::open_stream_locked() noexcept {
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) return Status{Errc::NotSupported, "AAudio indisponivel"};
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, static_cast<int32_t>(audio::kMixChannels));
    AAudioStreamBuilder_setSampleRate(b, static_cast<int32_t>(audio::kMixRate));
    AAudioStreamBuilder_setDataCallback(b, &data_cb, this);
    AAudioStreamBuilder_setErrorCallback(b, &error_cb, this);
    AAudioStream* s = nullptr;
    const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !s) {
        return Status{Errc::NotSupported, AAudio_convertResultToText(r)};
    }
    sampleRate_ = static_cast<u32>(AAudioStream_getSampleRate(s));
    if (sampleRate_ != audio::kMixRate || AAudioStream_getChannelCount(s) != static_cast<int32_t>(audio::kMixChannels)
        || AAudioStream_getFormat(s) != AAUDIO_FORMAT_PCM_FLOAT) {
        // Sem a conversão do sistema o som sairia na velocidade errada: melhor
        // mudo (o relógio do sistema conduz) do que tocando errado.
        AUREA_LOG_WARN("audio: stream %d Hz / %d canais / formato %d — recusado", AAudioStream_getSampleRate(s),
                       AAudioStream_getChannelCount(s), AAudioStream_getFormat(s));
        AAudioStream_close(s);
        return Status{Errc::NotSupported, "saida de audio sem 48 kHz estereo float"};
    }
    // Dois bursts: o mínimo que não estala na maioria dos aparelhos.
    const int32_t burst = AAudioStream_getFramesPerBurst(s);
    if (burst > 0) AAudioStream_setBufferSizeInFrames(s, burst * 2);
    streamBase_.store(delivered_.load());
    stream_ = reinterpret_cast<AAudioStreamStruct*>(s);
    AUREA_LOG_INFO("audio: AAudio %u Hz, burst %d, buffer %d quadros", sampleRate_, burst,
                   AAudioStream_getBufferSizeInFrames(s));
    return OkStatus;
}

void AAudioOutput::close_stream_locked() noexcept {
    if (!stream_) return;
    auto* s = reinterpret_cast<AAudioStream*>(stream_);
    AAudioStream_requestStop(s);
    AAudioStream_close(s);
    stream_ = nullptr;
}

Status AAudioOutput::start() noexcept {
    wantPlaying_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_) {
        if (const Status st = open_stream_locked(); !st.ok()) return st;
    }
    auto* s = reinterpret_cast<AAudioStream*>(stream_);
    const aaudio_result_t r = AAudioStream_requestStart(s);
    return r == AAUDIO_OK ? OkStatus : Status{Errc::IoError, AAudio_convertResultToText(r)};
}

void AAudioOutput::stop() noexcept {
    wantPlaying_.store(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_) return;
    auto* s = reinterpret_cast<AAudioStream*>(stream_);
    // Pausa + descarta o que estava no buffer: o próximo play não começa com
    // um resto do instante antigo.
    AAudioStream_requestPause(s);
    aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(s, AAUDIO_STREAM_STATE_PAUSING, &next, 100'000'000);
    AAudioStream_requestFlush(s);
}

void AAudioOutput::close() noexcept {
    wantPlaying_.store(false);
    std::lock_guard<std::mutex> lock(mutex_);
    close_stream_locked();
}

bool AAudioOutput::presented(u64 nowNs, i64& frames) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_) return false;
    int64_t pos = 0, t = 0;
    if (AAudioStream_getTimestamp(reinterpret_cast<AAudioStream*>(stream_), CLOCK_MONOTONIC, &pos, &t) != AAUDIO_OK) {
        return false;
    }
    const i64 elapsed = static_cast<i64>(nowNs) - static_cast<i64>(t);
    frames = streamBase_.load() + pos + elapsed * static_cast<i64>(sampleRate_) / 1'000'000'000;
    return true;
}

u32 AAudioOutput::latency_frames() const noexcept {
    auto* s = reinterpret_cast<AAudioStream*>(stream_);
    return s ? static_cast<u32>(AAudioStream_getBufferSizeInFrames(s)) : 0;
}

void AAudioOutput::on_data(f32* out, i32 frames) noexcept {
    if (fn_) fn_(ctx_, out, static_cast<u32>(frames));
    else std::memset(out, 0, static_cast<usize>(frames) * audio::kMixChannels * sizeof(f32));
    delivered_.fetch_add(frames, std::memory_order_relaxed);
}

void AAudioOutput::on_error() noexcept {
    // Nada de fechar dentro do callback: uma thread à parte reabre.
    if (reopening_.exchange(true)) return;
    std::thread([this] { reopen(); }).detach();
}

void AAudioOutput::reopen() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        close_stream_locked();
        const Status s = open_stream_locked();
        if (s.ok() && wantPlaying_.load()) AAudioStream_requestStart(reinterpret_cast<AAudioStream*>(stream_));
        if (!s.ok()) AUREA_LOG_WARN("audio: reabrir falhou: %s", s.message().data());
    }
    reopening_.store(false);
}

} // namespace aurea::android
