// =============================================================================
//  Aurea / playback / Playback.hpp
//
//  PlaybackClock + PlaybackController + FrameScheduler.
//
//  O RELÓGIO MESTRE É TROCÁVEL. Hoje a referência é o relógio monotônico do
//  sistema; quando o mixer de áudio existir, ele implementa `MasterClock` e
//  passa a ditar o tempo — o vídeo segue o áudio, nunca o contrário (o ouvido
//  percebe 20 ms de salto; o olho não percebe um frame repetido). Nada fora
//  deste arquivo muda quando isso acontecer.
//
//  O CONTROLLER é a única fonte da verdade sobre "em que instante estamos":
//  play, pause, seek, scrub (com direção e velocidade do dedo), passo de frame,
//  loop e velocidade. O resto do motor pergunta a ele.
//
//  O FRAME SCHEDULER compara o frame que o relógio pede com o último mostrado:
//  pulou frame da composição → conta como perdido. É esse número que o painel
//  DEV mostra e que alimenta o preview adaptativo.
// =============================================================================
#pragma once

#include "aurea/core/Time.hpp"
#include "aurea/core/Types.hpp"

namespace aurea {

/// Fonte de tempo mestre. O áudio vai implementar isto.
class MasterClock {
public:
    virtual ~MasterClock() = default;
    /// O relógio está entregando tempo confiável agora? (áudio ainda não
    /// começou a tocar → não; o controller cai no relógio do sistema.)
    [[nodiscard]] virtual bool available() const noexcept = 0;
    /// Posição de mídia em ns.
    [[nodiscard]] virtual i64 position_ns() const noexcept = 0;
};

class PlaybackClock {
public:
    /// `nowNs` é o relógio monotônico. Recebido de fora para os testes
    /// poderem simular o tempo.
    void start(i64 mediaNs, u64 nowNs, f32 speed) noexcept;
    void stop(u64 nowNs) noexcept;
    void seek(i64 mediaNs, u64 nowNs) noexcept;
    void set_speed(f32 speed, u64 nowNs) noexcept;

    [[nodiscard]] i64 media_ns(u64 nowNs) const noexcept;
    [[nodiscard]] bool running() const noexcept { return running_; }

    void set_master(const MasterClock* master) noexcept { master_ = master; }
    [[nodiscard]] bool using_master() const noexcept { return master_ && master_->available(); }

private:
    const MasterClock* master_ = nullptr;
    i64  anchorMediaNs_ = 0;
    u64  anchorNowNs_ = 0;
    f32  speed_ = 1.0f;
    bool running_ = false;
};

enum class PlaybackMode : u8 { Paused = 0, Playing, Scrubbing };

class PlaybackController {
public:
    void configure(f64 fps, FrameIndex duration) noexcept;

    void play(u64 nowNs) noexcept;
    void pause(u64 nowNs) noexcept;
    void toggle(u64 nowNs) noexcept;
    void seek(FrameIndex frame, u64 nowNs) noexcept;

    void begin_scrub(u64 nowNs) noexcept;
    void scrub(FrameIndex frame, u64 nowNs) noexcept;
    void end_scrub(u64 nowNs) noexcept;

    /// Avança/recua N frames (pausado).
    void step(i32 frames, u64 nowNs) noexcept;

    void set_loop(bool loop) noexcept { loop_ = loop; }
    void set_speed(f32 speed, u64 nowNs) noexcept;

    /// Avança o relógio. Chamado uma vez por frame de display. Tocando, faz o
    /// loop ou para no fim.
    FrameIndex update(u64 nowNs) noexcept;

    [[nodiscard]] PlaybackMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool playing() const noexcept { return mode_ == PlaybackMode::Playing; }
    [[nodiscard]] FrameIndex current() const noexcept { return current_; }
    [[nodiscard]] i64 current_ns() const noexcept { return currentNs_; }
    [[nodiscard]] FrameIndex duration() const noexcept { return duration_; }
    [[nodiscard]] f64 fps() const noexcept { return fps_; }
    [[nodiscard]] f32 speed() const noexcept { return speed_; }
    [[nodiscard]] bool loop() const noexcept { return loop_; }
    /// +1 para a frente, -1 para trás, 0 parado. No scrub, é a direção do dedo.
    [[nodiscard]] i32 direction() const noexcept { return direction_; }
    /// Frames por segundo que o dedo está varrendo (scrub). O decode usa para
    /// decidir quanto vale a pena adiantar.
    [[nodiscard]] f32 scrub_velocity() const noexcept { return scrubVelocity_; }
    /// Muda a cada descontinuidade (seek, salto de scrub, loop).
    [[nodiscard]] u64 generation() const noexcept { return generation_; }

    [[nodiscard]] PlaybackClock& clock() noexcept { return clock_; }

private:
    [[nodiscard]] FrameIndex clamp_frame(FrameIndex f) const noexcept;
    [[nodiscard]] i64 frame_to_ns(FrameIndex f) const noexcept;
    [[nodiscard]] FrameIndex ns_to_frame(i64 ns) const noexcept;

    PlaybackClock clock_;
    PlaybackMode mode_ = PlaybackMode::Paused;
    f64 fps_ = 30.0;
    FrameIndex duration_{1};
    FrameIndex current_{0};
    i64 currentNs_ = 0;
    f32 speed_ = 1.0f;
    bool loop_ = false;
    i32 direction_ = 0;
    f32 scrubVelocity_ = 0.0f;
    u64 lastScrubNs_ = 0;
    FrameIndex lastScrubFrame_{0};
    bool wasPlayingBeforeScrub_ = false;
    u64 generation_ = 1;
};

/// Conta frames perdidos e decide se há o que desenhar.
class FrameScheduler {
public:
    /// Registra o frame que foi efetivamente mostrado.
    void presented(FrameIndex frame, bool playing) noexcept;

    [[nodiscard]] u32 dropped_total() const noexcept { return dropped_; }
    [[nodiscard]] u32 dropped_recent() const noexcept { return droppedWindow_; }
    /// Chamado a cada segundo pelo painel: zera a janela recente.
    void roll_window() noexcept { droppedWindow_ = 0; }
    void reset() noexcept { dropped_ = 0; droppedWindow_ = 0; hasLast_ = false; }

private:
    FrameIndex last_{0};
    bool hasLast_ = false;
    u32 dropped_ = 0;
    u32 droppedWindow_ = 0;
};

} // namespace aurea
