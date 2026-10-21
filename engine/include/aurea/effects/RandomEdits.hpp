// =============================================================================
//  Aurea / effects / RandomEdits.hpp
//
//  "Edições aleatórias" (Tempo, 0.2.7): a camada é cortada em trechos de N
//  quadros e os trechos são remontados numa ordem embaralhada — cada trecho
//  aparece uma vez — repetível pela semente. É o comportamento do RandomEdits
//  do Sapphire (Edit Frame Length, Seed); o nome e a implementação são nossos.
//
//  A ordem é uma permutação sem tabela: uma rede de Feistel de 4 rodadas num
//  domínio de 2^k, com "cycle walking" até cair dentro de [0, n). Mesma conta
//  no renderer (Renderer::prepare) e na prévia do navegador de efeitos.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <algorithm>
#include <cstring>

namespace aurea::random_edits {

/// Posição do trecho `index` na montagem embaralhada de `count` trechos.
[[nodiscard]] inline u32 permute(u32 index, u32 count, u32 seed) noexcept {
    if (count <= 1 || index >= count) return index < count ? index : 0;
    u32 bits = 2;
    while (bits < 32 && (1u << bits) < count) ++bits;
    if (bits & 1u) ++bits;
    const u32 half = bits / 2, mask = (1u << half) - 1u;
    u32 x = index;
    do {
        u32 l = x >> half, r = x & mask;
        for (u32 round = 0; round < 4; ++round) {
            u32 f = r * 0x9E3779B1u ^ (seed + round * 0x85EBCA77u);
            f ^= f >> 15; f *= 0x2C1B3C6Du; f ^= f >> 12; f *= 0x297A2D39u; f ^= f >> 15;
            const u32 next = (l ^ f) & mask;
            l = r; r = next;
        }
        x = (l << half) | r;
    } while (x >= count);
    return x;
}

/// Semente do parâmetro (float) → bits estáveis.
[[nodiscard]] inline u32 seed_bits(f32 seed) noexcept {
    u32 bits = 0;
    std::memcpy(&bits, &seed, sizeof(bits));
    bits ^= bits >> 16; bits *= 0x7FEB352Du; bits ^= bits >> 15; bits *= 0x846CA68Bu; bits ^= bits >> 16;
    return bits;
}

/// O quadro (desde o início da camada) que aparece no quadro `frame` de uma
/// camada de `duration` quadros, com trechos de `length` quadros.
[[nodiscard]] inline i64 frame(i64 frame, i64 duration, i64 length, f32 seed) noexcept {
    if (length < 1 || duration <= length || frame < 0 || frame >= duration) return frame;
    const i64 segments = (duration + length - 1) / length;
    if (segments > 0x7FFFFFFF) return frame;
    const i64 segment = frame / length, offset = frame % length;
    const i64 source = permute(static_cast<u32>(segment), static_cast<u32>(segments), seed_bits(seed));
    return std::min(source * length + offset, duration - 1);
}

} // namespace aurea::random_edits
