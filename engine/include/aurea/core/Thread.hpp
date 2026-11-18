// =============================================================================
//  Aurea / core / Thread.hpp
//
//  Nome e prioridade de thread, por plataforma.
//
//  A prioridade é a ferramenta que garante "miniatura não disputa com preview":
//  a thread de render e a de decode do preview sobem para a faixa de display;
//  a de miniatura desce para segundo plano. O agendador do sistema faz o resto.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

namespace aurea {

enum class ThreadPriority : u8 {
    Background = 0,   ///< miniaturas, indexação, waveform
    Normal,
    Decode,           ///< decode do preview e do export
    Display,          ///< a thread de render do preview
    Audio,            ///< mixer de áudio (um buraco no som se ouve; um frame atrasado, quase não)
    /// Render, encoder e áudio do EXPORT (18/11/2026): o aparelho inteiro para
    /// exportar. Android nice -8 (URGENT_DISPLAY), iOS QoS userInitiated,
    /// Windows acima do normal. Só as threads do export usam.
    Export,
};

/// Aplica à thread atual. Falha silenciosa é aceitável (sem permissão o
/// sistema mantém a prioridade padrão) — mas nunca derruba o motor.
void set_current_thread_priority(ThreadPriority p) noexcept;

/// Nome visível no systrace/Perfetto e no depurador. Até 15 caracteres.
void set_current_thread_name(const char* name) noexcept;

/// Dica de desempenho do sistema para a thread atual (Android 13+:
/// APerformanceHint — o escalonador sobe o clock da CPU para a thread caber no
/// tempo alvo; carregado em tempo de execução, sem exigir API 33 no build).
/// Nas outras plataformas não faz nada. Uma sessão por thread do export.
class PerformanceHintSession {
public:
    PerformanceHintSession() noexcept = default;
    ~PerformanceHintSession();
    PerformanceHintSession(const PerformanceHintSession&) = delete;
    PerformanceHintSession& operator=(const PerformanceHintSession&) = delete;
    /// Abre para a thread ATUAL com o tempo alvo por unidade de trabalho (ns).
    /// false = o sistema não oferece (API < 33, sem serviço): nada muda.
    bool open_for_current_thread(i64 targetNs) noexcept;
    /// Duração real de uma unidade (um quadro). Ignorado sem sessão.
    void report(i64 actualNs) noexcept;
    void close() noexcept;
    [[nodiscard]] bool active() const noexcept { return session_ != nullptr; }
private:
    void* session_ = nullptr;
};

} // namespace aurea
