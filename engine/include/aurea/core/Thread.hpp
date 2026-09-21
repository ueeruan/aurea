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
};

/// Aplica à thread atual. Falha silenciosa é aceitável (sem permissão o
/// sistema mantém a prioridade padrão) — mas nunca derruba o motor.
void set_current_thread_priority(ThreadPriority p) noexcept;

/// Nome visível no systrace/Perfetto e no depurador. Até 15 caracteres.
void set_current_thread_name(const char* name) noexcept;

} // namespace aurea
