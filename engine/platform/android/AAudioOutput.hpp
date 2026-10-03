// =============================================================================
//  Aurea / platform / android / AAudioOutput.hpp
//
//  Saída de som por AAudio (API 26+): stream float estéreo 48 kHz, baixa
//  latência, callback de tempo real. O relógio vem de AAudioStream_getTimestamp
//  — o quadro que ESTÁ saindo no alto-falante agora, não o que foi escrito.
//
//  Troca de rota (fone desplugado, Bluetooth conectou): o AAudio desconecta o
//  stream e avisa no callback de erro. O stream é reaberto numa thread à parte
//  (não se pode fechar stream dentro do callback) e volta a tocar se tocava;
//  os contadores de quadros continuam de onde estavam para o relógio não saltar.
//
//  Stream que morre CALADO: o callback de erro só chega com o stream rodando.
//  Desconectado com o play parado (rota trocou na pausa, audioserver
//  reiniciou), o requestStart seguinte devolve DISCONNECTED/INVALID_STATE —
//  antes isso deixava o som mudo até reiniciar o app. Agora start/stop olham o
//  estado e reabrem o stream; e o motor (AudioEngine) ainda vigia o callback e
//  fecha/reabre a saída se ele parar com o play pedido.
// =============================================================================
#pragma once

#include "aurea/audio/Audio.hpp"

#include <atomic>
#include <mutex>

struct AAudioStreamStruct;

namespace aurea::android {

class AAudioOutput final : public audio::AudioOutput {
public:
    AAudioOutput() = default;
    ~AAudioOutput() override;

    Status open(audio::AudioRenderFn fn, void* ctx) noexcept override;
    Status start() noexcept override;
    void stop() noexcept override;
    void close() noexcept override;
    bool presented(u64 nowNs, i64& frames) noexcept override;
    u32 latency_frames() const noexcept override;

    // Callbacks do AAudio (públicos por serem chamados de funções C).
    void on_data(f32* out, i32 frames) noexcept;
    void on_error() noexcept;

private:
    Status open_stream_locked() noexcept;
    void close_stream_locked() noexcept;
    /// Fecha e abre de novo (sob mutex_); com `start`, já pede o start.
    i32 reopen_locked(bool start) noexcept;
    void reopen() noexcept;

    std::mutex mutex_;
    AAudioStreamStruct* stream_ = nullptr;
    audio::AudioRenderFn fn_ = nullptr;
    void* ctx_ = nullptr;
    std::atomic<bool> wantPlaying_{false};
    std::atomic<i64> delivered_{0};     ///< quadros entregues desde o open (todos os streams)
    std::atomic<i64> streamBase_{0};    ///< delivered_ quando o stream atual abriu
    std::atomic<bool> reopening_{false};
    bool closed_ = true;
    /// Buffer do stream atual (quadros). Atômico: o render lê sem o mutex, e o
    /// stream pode ser fechado/reaberto pela thread do mixer a qualquer hora.
    std::atomic<u32> latency_{0};                ///< close() chamado: a thread de reabertura não reabre
    u32 sampleRate_ = audio::kMixRate;
};

} // namespace aurea::android
