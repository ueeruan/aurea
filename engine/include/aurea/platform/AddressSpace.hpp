// =============================================================================
//  Aurea / platform / AddressSpace.hpp
//
//  Política do espaço de endereçamento de 32 bits (APK armeabi-v7a).
//
//  Um processo de 32 bits enxerga ~3 GB de endereços, já fragmentados pela VM,
//  pelas .so, pelos drivers e pelos codecs. A RAM física do aparelho não é o
//  limite: um aparelho de 3 GB com o APK de 64 bits roda, o mesmo aparelho com
//  o APK de 32 bits fica sem ENDEREÇOS (mmap/vkMapMemory/malloc grande falham)
//  muito antes de ficar sem memória. Beta 08/10: "lento e fecha só cortando
//  clipes" num Helio de 3 GB com o APK de 32 bits.
//
//  Toda decisão daqui é uma função pura que recebe a largura do ponteiro: o
//  teste do host (64 bits) chama com 32 e confere o ramo do aparelho. No build
//  de 64 bits todas devolvem o comportamento de sempre — nada muda no export.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

namespace aurea {

/// Largura do ponteiro deste build: 32 no armeabi-v7a, 64 no resto.
inline constexpr u32 kPointerBits = static_cast<u32>(sizeof(void*) * 8);
/// Build com espaço de endereçamento de 32 bits.
inline constexpr bool kAddressSpace32 = kPointerBits <= 32;

namespace address_space {

[[nodiscard]] constexpr bool narrow(u32 pointerBits) noexcept { return pointerBits <= 32; }

/// Teto do orçamento do MemoryManager num processo de 32 bits. O resto dos
/// endereços fica para a VM/Compose, codecs, driver da GPU e fragmentação.
inline constexpr u64 kNarrowProcessBudget = 256ull << 20;

[[nodiscard]] constexpr u64 process_budget(u64 budget, u32 pointerBits = kPointerBits) noexcept {
    return narrow(pointerBits) && budget > kNarrowProcessBudget ? kNarrowProcessBudget : budget;
}

/// Memória da GPU que o driver declara visível à CPU (Mali/PowerVR: TODA ela)
/// só é mapeada no processo quando alguém usa o ponteiro (buffers, staging).
/// Em 64 bits o mapa persistente de tudo continua (caminho de sempre); em 32
/// bits cada textura mapeada gastava endereços do tamanho dela (blocos de 16 MB
/// e alocações dedicadas de alvos de render/quadros de vídeo).
[[nodiscard]] constexpr bool map_gpu_memory_eagerly(u32 pointerBits = kPointerBits) noexcept {
    return !narrow(pointerBits);
}

/// Decoders de vídeo (MediaCodec + AImageReader + extractor + cache de
/// quadros) vivos ao mesmo tempo. Uma fonte é POR LAYER: cada corte cria uma
/// layer nova e um decoder novo. 0 = sem teto (64 bits).
inline constexpr u32 kNarrowMaxVideoSources = 4;
[[nodiscard]] constexpr u32 max_video_sources(u32 pointerBits = kPointerBits) noexcept {
    return narrow(pointerBits) ? kNarrowMaxVideoSources : 0u;
}
/// Quadros sem uso para uma fonte poder ceder o lugar a outra. Uma layer
/// visível é tocada a cada quadro; o prazo impede trocar decoders entre
/// layers que aparecem juntas (um seek por quadro).
inline constexpr u32 kSourceEvictIdleFrames = 8;

/// Quadros sem uso até o `collect` padrão fechar a fonte (a prévia; o export
/// passa o próprio prazo e não muda). 64 bits: o pedido, como sempre.
inline constexpr u32 kNarrowSourceIdleFrames = 60;
[[nodiscard]] constexpr u32 source_idle_frames(u32 requested, u32 pointerBits = kPointerBits) noexcept {
    return narrow(pointerBits) && requested > kNarrowSourceIdleFrames ? kNarrowSourceIdleFrames : requested;
}

/// Tempo ocioso até a rede de IA (profundidade/Rotobrush, ~66 MB em fp32)
/// sair da memória. Em 32 bits sai logo: voltar custa um load, ficar custa
/// endereços que o próximo corte precisa.
inline constexpr u32 kNarrowAiIdleReleaseMs = 5'000;
[[nodiscard]] constexpr u32 ai_idle_release_ms(u32 requested, u32 pointerBits = kPointerBits) noexcept {
    return narrow(pointerBits) && requested > kNarrowAiIdleReleaseMs ? kNarrowAiIdleReleaseMs : requested;
}

/// Deslocamento de arquivo representável pelo `off_t` deste build (32 bits
/// no bionic sem _FILE_OFFSET_BITS=64): acima disto o fseeko truncaria.
[[nodiscard]] constexpr bool file_offset_fits(u64 offset, u32 offsetBits) noexcept {
    return offsetBits >= 64 || offset <= ((1ull << (offsetBits - 1)) - 1);
}

} // namespace address_space
} // namespace aurea
