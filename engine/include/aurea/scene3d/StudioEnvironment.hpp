// =============================================================================
//  Aurea / scene3d / StudioEnvironment.hpp
//
//  Ambientes de estúdio PROCEDURAIS: um HDRI equiretangular (RGB float linear,
//  Y para cima, linhas de cima para baixo — o mesmo formato do decode_hdri)
//  gerado na CPU, de forma determinística. Entra no MESMO caminho de um HDRI
//  importado (SceneEnvironment::hdri → build_environment_from_equirect): o IBL,
//  o fundo visível e o cache por chave não sabem que ele é gerado.
//
//  Presets (ids gravados no projeto — NÃO renumerar):
//    1 estudio_escuro  — quase preto, 4 faixas longas de softbox (teto e
//                        laterais) e um recorte atrás: reflexos longos e
//                        nítidos na pintura do carro, faixa dinâmica alta.
//    2 estudio_produto — ciclorama cinza médio, softbox grande e macio no
//                        alto e rebatedores laterais largos (foto de produto).
//    3 ceu_sol         — céu azul (gradiente zênite → horizonte com névoa),
//                        disco do sol com intensidade HDR física e o
//                        rebatimento do chão.
// =============================================================================
#pragma once

#include "aurea/scene3d/Environment.hpp"

#include <memory>

namespace aurea::scene3d {

enum class StudioPreset : u32 { None = 0, DarkStudio = 1, ProductStudio = 2, SkySun = 3 };
inline constexpr u32 kStudioPresetCount = 4;   ///< inclui o 0 (nenhum)

/// Chave do ambiente na GPU (SceneEnvironment::hdriKey) de um preset. Fora do
/// espaço de AssetId::pack (geração 0x53545544 = "STUD"): não colide com HDRI
/// importado e dois objetos no mesmo preset dividem o upload.
inline constexpr u64 kStudioKeyBase = 0x5354554400000000ull;
[[nodiscard]] constexpr u64 studio_environment_key(u32 preset) noexcept { return kStudioKeyBase | preset; }
/// A chave é de um preset? Devolve o preset (0 = não é).
[[nodiscard]] constexpr u32 studio_preset_of_key(u64 key) noexcept {
    return (key & 0xFFFFFFFF00000000ull) == kStudioKeyBase && (key & 0xFFFFFFFFull) < kStudioPresetCount
         ? static_cast<u32>(key & 0xFFFFFFFFull) : 0u;
}

/// Nome curto do preset ("estudio_escuro", ...); nulo fora da faixa.
[[nodiscard]] const char* studio_preset_name(u32 preset) noexcept;
/// Id pelo nome (aceita também o número em texto); 0 = desconhecido.
[[nodiscard]] u32 studio_preset_from_name(const char* name) noexcept;

/// Gera o panorama `width`×(width/2). Determinístico (os mesmos bits em
/// qualquer aparelho). Nulo para preset inválido. 2048 de largura: o IBL
/// limita o cubo à fonte (max(128, pow2ceil(W/4))) — 2048 dá especular 512²
/// e fundo 512² (25 MB de float; um HDRI de 4K daria 1024² e 100 MB).
[[nodiscard]] std::shared_ptr<HdriPixels> generate_studio_hdri(u32 preset, u32 width = 2048) noexcept;
/// O mesmo panorama, gerado uma vez por preset e compartilhado (thread-safe).
[[nodiscard]] std::shared_ptr<const HdriPixels> studio_hdri(u32 preset) noexcept;

} // namespace aurea::scene3d
