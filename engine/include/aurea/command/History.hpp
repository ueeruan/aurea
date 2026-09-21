// =============================================================================
//  Aurea / command / History.hpp
//
//  Desfazer/refazer por SNAPSHOT da composição.
//
//  POR QUE SNAPSHOT E NÃO COMANDO INVERSO: um editor tem dezenas de comandos
//  (trim, split, efeito, parâmetro, keyframe, reordenar, duplicar...), e cada
//  inverso escrito à mão é um lugar a mais para errar — um split desfeito que
//  esquece o deslocamento do conteúdo corrompe o projeto em silêncio. A cópia
//  da composição (layers, efeitos, trilhas, ordem) é pequena (KB) e o
//  resultado é exato por construção: desfazer devolve os MESMOS ids, então a
//  seleção da UI e os caches do renderer continuam válidos.
//
//  Uma AÇÃO = um snapshot "antes". Um gesto da UI (arrastar, slider) abre um
//  grupo: só a primeira mudança dentro dele captura, e o gesto inteiro desfaz
//  de uma vez.
// =============================================================================
#pragma once

#include "aurea/core/Handle.hpp"
#include "aurea/timeline/Composition.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aurea {

class Timeline;

class History {
public:
    /// Ações guardadas. Mais antigas saem primeiro.
    static constexpr u32 kMaxEntries = 200;

    void clear() noexcept;

    void begin_group(const char* label) noexcept;
    void end_group() noexcept;
    [[nodiscard]] bool in_group() const noexcept { return groupDepth_ > 0; }

    /// Chamado ANTES de um comando que altera `comp`. Captura o estado se for
    /// a primeira mudança da ação (fora de grupo: toda mudança é uma ação).
    void before_mutation(const Composition& comp, CompositionId id, const char* label);

    /// Restaura o estado anterior/posterior da última ação. false = nada a fazer.
    [[nodiscard]] bool undo(Timeline& timeline);
    [[nodiscard]] bool redo(Timeline& timeline);

    [[nodiscard]] bool can_undo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool can_redo() const noexcept { return cursor_ < entries_.size(); }
    [[nodiscard]] u32 depth() const noexcept { return static_cast<u32>(entries_.size()); }
    [[nodiscard]] const char* undo_label() const noexcept {
        return can_undo() ? entries_[cursor_ - 1].label.c_str() : "";
    }

private:
    struct Entry {
        std::string label;
        CompositionId comp{};
        std::unique_ptr<Composition> before;
        std::unique_ptr<Composition> after;   ///< preenchido no primeiro desfazer
    };

    std::vector<Entry> entries_;
    usize cursor_ = 0;                ///< [0, cursor) aplicadas; [cursor, fim) refazíveis
    u32 groupDepth_ = 0;
    bool groupCaptured_ = false;
    std::string groupLabel_;
};

} // namespace aurea
