// =============================================================================
//  Aurea / command / UndoStack.hpp
//
//  Histórico por comando, não por snapshot.
//
//  A regra: NUNCA guardar o projeto inteiro a cada ação. Um projeto de 40
//  layers com máscaras, efeitos e keyframes passa de dezenas de MB. Salvar isso
//  a cada arrasto de dedo estoura memória em segundos e faz o editor engasgar.
//
//  O que é guardado, então:
//     - comandos pequenos  → o comando inverso, POD, no próprio registro;
//     - comandos grandes   → o payload inverso num blob, referenciado por
//                            (offset, tamanho). O blob é uma arena: cresce no
//                            fim e é compactado quando o histórico enche.
//
//  Agrupamento: um arrasto de dedo gera dezenas de LayerSetTransform. Todos
//  entram num grupo e o usuário vê UM "desfazer". Sem isso, desfazer um gesto
//  vira apertar desfazer 60 vezes.
// =============================================================================
#pragma once

#include "aurea/command/Command.hpp"
#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea {

/// Um passo do histórico.
struct UndoRecord {
    /// Comando que desfaz este passo. Aplicar `inverse` reverte.
    Command      inverse{};

    /// Payload extra do inverso (layer recriada em LayerDelete, caminho de
    /// asset, conteúdo de texto longo). Vazio = o inverso é só o comando.
    u32          payloadOffset = kInvalidIndex;
    u32          payloadSize   = 0;

    /// Índice do grupo a que pertence. Registros do mesmo grupo desfazem
    /// juntos (um gesto = um passo).
    u32          groupId = 0;

    /// Rótulo do passo, para a UI mostrar "Desfazer: mover camada" em vez de
    /// "Desfazer". Guardado no blob como string: offset e tamanho.
    u32          labelOffset = kInvalidIndex;
    u32          labelSize   = 0;

    /// Marca de compressão: registros consecutivos com o mesmo alvo e a mesma
    /// propriedade podem ser fundidos (arrastar X gera N LayerSetPositionX;
    /// só o PRIMEIRO valor importa, os intermediários não).
    u64          mergeKey = 0;
    bool         mergeable = false;
};

class UndoStack {
public:
    static constexpr u32 kDefaultMaxRecords = 512;
    static constexpr u32 kDefaultMaxBlob    = 24u * 1024 * 1024;   // 24 MiB
    static constexpr u32 kMaxGroups         = 64;

    UndoStack() = default;

    /// Zera o histórico (projeto fechado / carregado).
    void clear() noexcept;

    /// Registra o inverso de uma ação já aplicada. Chamado pelo motor logo
    /// depois de aplicar um comando.
    ///
    /// `mergeKey != 0` habilita fusão: se o topo do histórico tem a mesma
    /// mergeKey, e ambos são do mesmo grupo, o registro novo é descartado e o
    /// antigo mantém o valor original — que é o comportamento correto para
    /// "desfazer o arrasto inteiro".
    [[nodiscard]] Status record(const Command& inverse,
                                const void* payload, u32 payloadSize,
                                const char* label, u32 labelLen,
                                u64 mergeKey);

    /// Abre um grupo. Tudo que for registrado até `end_group` desfaz junto.
    void begin_group(const char* label, u32 labelLen) noexcept;
    void end_group() noexcept;
    [[nodiscard]] bool in_group() const noexcept { return groupDepth_ > 0; }

    /// Tira o inverso do topo e o devolve. Quem chama aplica e então chama
    /// `commit_undo` com o comando direto, que vira o novo inverso (refazer).
    [[nodiscard]] bool pop_undo(Command& outInverse, const void*& outPayload,
                                u32& outPayloadSize) noexcept;

    [[nodiscard]] bool pop_redo(Command& outForward, const void*& outPayload,
                                u32& outPayloadSize) noexcept;

    void commit_undo(const Command& forward, const void* payload, u32 payloadSize) noexcept;
    void commit_redo(const Command& inverse, const void* payload, u32 payloadSize) noexcept;

    [[nodiscard]] bool can_undo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool can_redo() const noexcept { return cursor_ < records_.size(); }

    /// Rótulo do passo que seria desfeito/refeito, para a UI. Vazio se não há.
    [[nodiscard]] const char* undo_label() const noexcept;
    [[nodiscard]] const char* redo_label() const noexcept;

    [[nodiscard]] u32 depth() const noexcept { return static_cast<u32>(records_.size()); }
    [[nodiscard]] u32 cursor() const noexcept { return cursor_; }

    /// Quanto do orçamento de blob está em uso.
    [[nodiscard]] usize blob_used() const noexcept { return blob_.size(); }
    [[nodiscard]] usize blob_capacity() const noexcept { return maxBlob_; }
    void set_max_blob(usize bytes) noexcept { maxBlob_ = bytes; }

private:
    /// Grava bytes no blob e devolve o offset. Devolve kInvalidIndex se não
    /// couber — e nesse caso o registro é gravado sem payload (o inverso vira
    /// parcial), o que é melhor do que estourar memória.
    [[nodiscard]] u32 blob_write(const void* data, u32 size) noexcept;
    [[nodiscard]] const void* blob_read(u32 offset, u32 size) const noexcept;

    /// Descarta o registro mais antigo e compacta o blob quando o histórico
    /// passa do teto. Um passo antigo some, mas o histórico nunca quebra.
    void evict_oldest() noexcept;

    std::vector<UndoRecord> records_;
    std::vector<u8>         blob_;
    std::vector<u32>        groupStack_;      ///< índices de início de grupo
    std::vector<u8>         labelBlob_;

    u32  cursor_      = 0;      ///< posição de desfazer (records_ antes disso)
    u32  groupDepth_  = 0;
    u32  nextGroupId_ = 1;
    u32  groupId_     = 0;      ///< id do grupo aberto; 0 = nenhum
    u32  groupStart_  = 0;
    usize maxBlob_    = kDefaultMaxBlob;
};

} // namespace aurea
