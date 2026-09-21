// =============================================================================
//  Aurea / core / Time.hpp
//
//  Relógio do motor.
//
//  Uma regra que vale para todo o Aurea: DURANTE PLAYBACK, QUEM MANDA É O ÁUDIO.
//  O master clock é a posição do mixer de áudio; o renderer pergunta "que
//  instante é agora?" e desenha o frame correspondente. Áudio não pode esperar
//  por frame — se o vídeo travar, o áudio continua e o vídeo pula. O contrário
//  produz drift e estalo.
//
//  Durante SCRUBBING (usuário arrastando o playhead) não há áudio: quem manda é
//  a UI, e o tempo é "pinned" — o renderer desenha exatamente o frame pedido,
//  sem se preocupar em manter cadência.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

namespace aurea {

enum class ClockSource : u8 {
    Audio,   ///< playback: posição do mixer
    Pinned,  ///< scrubbing / seek: instante imposto pela UI
    Free,    ///< editor parado: tempo não avança
};

/// Relógio monotônico do sistema, em nanossegundos. Não depende de data/hora
/// e não anda para trás.
[[nodiscard]] u64 monotonic_ns() noexcept;
/// Frequência do relógio monotônico, para converter ciclos em tempo.
[[nodiscard]] u64 monotonic_frequency() noexcept;

/// Relógio de parede em milissegundos desde a época Unix. Só para autosave e
/// nomes de arquivo — nunca para sincronizar mídia.
[[nodiscard]] u64 wall_clock_ms() noexcept;

/// Relógio dentro de uma composição. Mantém a taxa de quadros e a origem.
///
/// O ponto sutil: `fps` é um número racional na prática (29.97, 23.976). Guardar
/// como `f64` e converter com arredondamento estável evita que 1 hora de
/// timeline acumule 1 frame de erro.
class TimelineClock {
public:
    TimelineClock() = default;
    explicit TimelineClock(f64 fps) noexcept : fps_(fps) {}

    void set_fps(f64 fps) noexcept { fps_ = fps > 0.0 ? fps : 30.0; }
    [[nodiscard]] f64 fps() const noexcept { return fps_; }

    /// Cadência do clock em nanossegundos, para o scheduler do preview.
    [[nodiscard]] TickNs frame_duration() const noexcept { return aurea::frame_duration(fps_); }

    /// Converte posição em frame para tempo.
    [[nodiscard]] TickNs to_time(FrameIndex f) const noexcept { return tick_at(f, fps_); }
    /// Converte tempo para o frame que o contém (piso).
    [[nodiscard]] FrameIndex to_frame(TickNs t) const noexcept { return frame_at(t, fps_); }

    // --- Estado de reprodução ------------------------------------------------

    void play() noexcept { playing_ = true; source_ = ClockSource::Audio; }
    void pause() noexcept { playing_ = false; source_ = ClockSource::Free; }

    /// Trava o tempo num instante (scrubbing). Ignora o clock de áudio enquanto
    /// pinado — é o que permite arrastar o playhead sem o áudio puxar de volta.
    void pin(TickNs t) noexcept { pinned_ = t; source_ = ClockSource::Pinned; }
    void unpin() noexcept { source_ = playing_ ? ClockSource::Audio : ClockSource::Free; }

    /// Instante atual. `audio_time` só é consultado quando o áudio manda.
    [[nodiscard]] TickNs now(TickNs audio_time) const noexcept {
        switch (source_) {
            case ClockSource::Audio:  return audio_time;
            case ClockSource::Pinned: return pinned_;
            case ClockSource::Free:   break;
        }
        return pinned_;
    }

    [[nodiscard]] FrameIndex now_frame(TickNs audio_time) const noexcept {
        return to_frame(now(audio_time));
    }

    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] ClockSource source() const noexcept { return source_; }

private:
    f64          fps_     = 30.0;
    TickNs       pinned_{0};
    bool         playing_ = false;
    ClockSource  source_  = ClockSource::Free;
};

} // namespace aurea
