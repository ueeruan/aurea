// =============================================================================
//  Aurea / export / ExportMemory.hpp
//
//  0.5.3 (P0-7, decisão do dono): o orçamento de memória do EXPORT vem do
//  aparelho — memória total e processo de 32 bits —, nunca de uma lista de
//  fabricantes. A 1.2 tinha um teto de 480 MB para Xiaomi/Redmi/POCO e 1 GB nos
//  demais (o monitor de GPU do HyperOS matou um Redmi 12 com ~629 MB); o porte
//  da escada perdeu o teto no Android. Aqui ele volta sem marca: conservador até
//  6 GB, igual ao do iOS (1 GiB) acima disso.
//
//  E o DEGRAU DE MEMÓRIA: o sistema matou o app no meio do export (a tela sabe
//  pela marca "em curso" que ficou no disco) — o próximo export, inclusive o que
//  continua o checkpoint, sai com o orçamento um degrau abaixo, em vez de repetir
//  igual a tentativa que morreu. Volta a zero no primeiro export que termina.
//
//  O orçamento só decide caches, fila e quantos quadros ficam em voo: menos
//  orçamento = mais lento, nunca outra imagem. Funções puras (testes do host).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <algorithm>

namespace aurea {

/// O degrau mais apertado (a tela guarda 0..3).
inline constexpr u32 kExportMemoryStepMax = 3;
/// Nenhum degrau desce abaixo disto: um quadro 4K NV12 (12,4 MB) ainda cabe na
/// fila do export (orçamento / 8) e o render tem o mínimo de sempre.
inline constexpr u64 kExportMemoryFloorBytes = 128ull << 20;

/// Teto do orçamento do export pelo aparelho. `totalBytes` 0 = não medido (sem
/// teto a mais: vale o orçamento medido de sempre); `pointerBits` < 64 = processo
/// de 32 bits (espaço de endereços: no máximo 512 MB).
[[nodiscard]] constexpr u64 export_device_memory_cap_bytes(u64 totalBytes, u32 pointerBits) noexcept {
    constexpr u64 MB = 1ull << 20;
    u64 cap = 0;
    if (totalBytes) {
        if (totalBytes <= 3200 * MB) cap = 384 * MB;        // até 3 GB
        else if (totalBytes <= 4608 * MB) cap = 512 * MB;   // 4 GB
        else if (totalBytes <= 7168 * MB) cap = 768 * MB;   // 6 GB
        // 18/11/2026: o aparelho grande usa a memória que tem (antes 1 GB em
        // todo aparelho de 8 GB ou mais: o 4K de um topo de linha rodava com a
        // fila de um intermediário). Ainda limitado pelo orçamento medido do
        // processo (1/4 do disponível) e pelos degraus depois de morte.
        else if (totalBytes <= 10240 * MB) cap = 1536 * MB; // 8 GB
        else cap = 2048 * MB;                               // 12 GB ou mais
    }
    if (pointerBits < 64) cap = cap ? std::min<u64>(cap, 512 * MB) : 512 * MB;
    return cap;
}

/// Fração do orçamento em cada degrau (em milésimos): 100%, 75%, 50%, 35%.
[[nodiscard]] constexpr u64 export_memory_step_permille(u32 step) noexcept {
    switch (step > kExportMemoryStepMax ? kExportMemoryStepMax : step) {
        case 0: return 1000;
        case 1: return 750;
        case 2: return 500;
        default: return 350;
    }
}

/// O orçamento do export: o do processo, no teto do aparelho, no degrau pedido,
/// nunca abaixo do piso (nem acima do que o processo tinha).
[[nodiscard]] constexpr u64 export_memory_budget_bytes(u64 processBudget, u64 deviceCap, u32 step) noexcept {
    if (processBudget == 0) return 0;
    u64 budget = deviceCap ? std::min(processBudget, deviceCap) : processBudget;
    budget = budget * export_memory_step_permille(step) / 1000;   // u64: sem estouro até 18 PB
    return std::min(processBudget, std::max(budget, kExportMemoryFloorBytes));
}

} // namespace aurea
