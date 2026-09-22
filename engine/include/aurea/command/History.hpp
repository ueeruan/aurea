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
    /// Ações guardadas (§125: 1000). Mais antigas saem primeiro — pela
    /// contagem ou pelo orçamento de bytes, o que vier antes.
    static constexpr u32 kMaxEntries = 1000;
    /// Orçamento padrão dos snapshots (§126). Uma composição de 500 camadas
    /// com milhares de keyframes pesa MBs por snapshot; 1000 deles sem teto
    /// seriam GBs — o OOM killer do Android antes do undo.
    static constexpr u64 kDefaultBudgetBytes = 64ull * 1024 * 1024;

    /// Orçamento de memória dos snapshots. A última ação sempre fica
    /// desfazível, mesmo sozinha acima do teto.
    void set_budget_bytes(u64 bytes) noexcept;
    [[nodiscard]] u64 budget_bytes() const noexcept { return budgetBytes_; }
    /// Bytes estimados guardados agora (antes + depois de todas as entradas).
    [[nodiscard]] u64 bytes() const noexcept { return bytes_; }
    /// Estimativa do tamanho em memória de uma composição (o que um snapshot
    /// custa). Percorre as camadas; não aloca.
    [[nodiscard]] static u64 estimate_bytes(const Composition& comp) noexcept;

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
        u64 beforeBytes = 0;
        u64 afterBytes = 0;
    };

    /// Tira as entradas mais antigas até caber no orçamento (fica ≥ 1).
    void enforce_budget() noexcept;
    void erase_range(usize first, usize last) noexcept;

    std::vector<Entry> entries_;
    u64 budgetBytes_ = kDefaultBudgetBytes;
    u64 bytes_ = 0;
    usize cursor_ = 0;                ///< [0, cursor) aplicadas; [cursor, fim) refazíveis
    u32 groupDepth_ = 0;
    bool groupCaptured_ = false;
    std::string groupLabel_;
};

} // namespace aurea
