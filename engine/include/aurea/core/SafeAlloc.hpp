#pragma once
// =============================================================================
//  SafeAlloc — alocação de buffers grandes sem lançar.
//
//  O motor compila sem exceções no celular (-fno-exceptions): um
//  std::vector::resize que falha ali vira std::terminate (o app fecha sem
//  aviso). Os caminhos que copiam uma FOTO inteira (reabrir projeto pela
//  plataforma, subir a textura da imagem) pedem dezenas de MB de uma vez; com
//  duas fotos grandes e o aparelho apertado, a falta de memória era um fecho.
//  Estas funções vivem num arquivo compilado COM exceções e devolvem false.
// =============================================================================
#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea {

/// Redimensiona `v` para `bytes`; false (e `v` intacto) se faltar memória.
[[nodiscard]] bool try_resize_bytes(std::vector<u8>& v, usize bytes) noexcept;

/// `dst` vira uma cópia de [src, src+bytes); false (e `dst` intacto) se faltar memória.
[[nodiscard]] bool try_assign_bytes(std::vector<u8>& dst, const u8* src, usize bytes) noexcept;

}  // namespace aurea
