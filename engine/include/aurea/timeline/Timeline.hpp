// =============================================================================
//  Aurea / timeline / Timeline.hpp
//
//  A timeline não é a UI da timeline.
//
//  Esta é a estrutura de dados: composições, e qual delas está aberta. Ela não
//  sabe quantos pixels tem a régua, não sabe o que é um dedo arrastando, não
//  conhece zoom nem scroll. A UI lê daqui para desenhar e escreve aqui por
//  comandos.
//
//  Essa separação é o que permite o MESMO objeto alimentar o preview e o
//  export. Se a timeline morasse na UI, o export teria que reconstruí-la — e
//  as duas versões divergiriam no primeiro caso especial.
// =============================================================================
#pragma once

#include "aurea/timeline/Composition.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Result.hpp"

#include <string>
#include <vector>
#include <functional>

namespace aurea {

class Timeline {
public:
    Timeline() = default;

    // --- Composições ----------------------------------------------------------

    [[nodiscard]] CompositionId create_composition(std::string name,
                                                   u32 width, u32 height, f64 fps);
    bool remove_composition(CompositionId id) noexcept;

    [[nodiscard]] Composition* composition(CompositionId id) noexcept {
        return compositions_.get(id);
    }
    [[nodiscard]] const Composition* composition(CompositionId id) const noexcept {
        return compositions_.get(id);
    }

    /// Composição raiz do projeto — a que o usuário abre primeiro.
    [[nodiscard]] CompositionId root() const noexcept { return root_; }
    void set_root(CompositionId id) noexcept { root_ = id; }

    /// Composição aberta no editor.
    [[nodiscard]] CompositionId current() const noexcept { return current_; }
    bool set_current(CompositionId id) noexcept;

    [[nodiscard]] u32 composition_count() const noexcept { return compositions_.count(); }

    template <typename Fn>
    void for_each_composition(Fn&& fn) { compositions_.for_each(std::forward<Fn>(fn)); }
    template <typename Fn>
    void for_each_composition(Fn&& fn) const { compositions_.for_each(std::forward<Fn>(fn)); }

    // --- Relógio --------------------------------------------------------------

    [[nodiscard]] TimelineClock& clock() noexcept { return clock_; }
    [[nodiscard]] const TimelineClock& clock() const noexcept { return clock_; }

    /// Playhead, em frames. É o instante que o preview desenha quando parado.
    [[nodiscard]] FrameIndex playhead() const noexcept { return playhead_; }

    /// O USUÁRIO posicionou o playhead.
    ///
    /// Trava o relógio no instante pedido. É o que faz o arrasto na timeline
    /// mandar: sem a trava, o clock de áudio continuaria devolvendo a posição
    /// antiga e o playhead voltaria sozinho para onde estava — o dedo puxa para
    /// um lado e a cabeça de reprodução vai para o outro.
    ///
    /// Durante o playback a trava é solta em seguida, porque aí quem manda é o
    /// áudio (que a camada de áudio já reposicionou junto).
    void seek(FrameIndex f) noexcept {
        if (f.value < 0) f = FrameIndex{0};
        playhead_ = f;
        clock_.pin(clock_.to_time(f));
        if (clock_.playing()) clock_.unpin();
        dirty_ = true;
    }

    /// O RELÓGIO posicionou o playhead (playback ou scrubbing contínuo).
    ///
    /// Não toca no clock: quem está devolvendo o instante é ele mesmo, e
    /// re-travar aqui produziria a cada frame uma trava e um destrave que não
    /// significam nada.
    void set_playhead(FrameIndex f) noexcept {
        if (f.value < 0) f = FrameIndex{0};
        playhead_ = f;
        dirty_ = true;
    }

    void play() noexcept  { clock_.play();  dirty_ = true; }
    void pause() noexcept { clock_.pause(); dirty_ = true; }

    [[nodiscard]] bool playing() const noexcept { return clock_.playing(); }

    /// Marca o frame atual como sujo: o renderer precisa recompor.
    void mark_dirty() noexcept { dirty_ = true; }
    [[nodiscard]] bool consume_dirty() noexcept {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

    void set_loop(bool loop) noexcept { loop_ = loop; }
    [[nodiscard]] bool loop() const noexcept { return loop_; }

    void set_speed(f32 s) noexcept { speed_ = s; }
    [[nodiscard]] f32 speed() const noexcept { return speed_; }

    // --- Duração total --------------------------------------------------------
    //
    //  Comprimento da timeline = maior `end` entre as composições de topo.
    //  Calculado, não guardado: um valor guardado desatualiza no primeiro
    //  arrasto de borda e a UI desenha uma régua que não bate com o conteúdo.
    [[nodiscard]] FrameIndex total_duration() const;

    /// Instante que a composição entrega para o compositor: o que o clock diz.
    [[nodiscard]] TickNs now_time(TickNs audioTime) const noexcept {
        return clock_.now(audioTime);
    }

    // --- Navegação ------------------------------------------------------------

    /// Próximo/anterior ponto de interesse (início ou fim de layer, keyframe).
    /// É o que faz o ímã da timeline: o playhead encosta nas bordas.
    [[nodiscard]] FrameIndex next_snap_point(FrameIndex from, CompositionId comp) const;
    [[nodiscard]] FrameIndex prev_snap_point(FrameIndex from, CompositionId comp) const;

    /// Resolve o tempo de uma layer de composição aninhada dentro do tempo da
    /// composição de topo. Usado pelo renderer ao entrar numa pre-comp.
    [[nodiscard]] Status resolve_nested_time(CompositionId parent, LayerId nested,
                                             FrameIndex parentTime,
                                             CompositionId& outComp,
                                             FrameIndex& outTime) const;

private:
    SlotTable<Composition, CompositionTag> compositions_;
    CompositionId root_{};
    CompositionId current_{};

    TimelineClock clock_{60.0};
    FrameIndex    playhead_{0};
    f32           speed_ = 1.0f;
    bool          loop_  = false;
    bool          dirty_ = true;
};

} // namespace aurea
