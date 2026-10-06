// =============================================================================
//  Aurea / scene3d / MaterialPreview.hpp
//
//  MINIATURA DE MATERIAL: a "bola de material" que as listas do painel 3D
//  mostram ao lado de cada nome — materiais prontos do texto 3D, materiais
//  do modelo importado e partes da forma 3D. Antes as listas eram só nomes
//  ("Material 1", "Material 2"...) ou uma bolinha de cor chapada; agora cada
//  item mostra o material de verdade: cor, metal, rugosidade, especular,
//  emissão e a IMAGEM do mapa de cor (a textura aplicada aparece na bola).
//
//  É a mesma receita do PBR da cena (fatores lineares do glTF, F0 de
//  dielétrico 0,04 × especular, metal tinge o reflexo), num estúdio fixo
//  calculado na CPU: sem GPU, sem esperar o render do quadro, determinístico
//  (o mesmo material dá os mesmos bytes nas duas plataformas). Uma bola de
//  96 px custa bem menos de 1 ms; ainda assim as UIs pedem fora da thread
//  principal e o motor guarda as prontas num cache por receita (a lista pede
//  de novo a cada edição e só o material que mudou é recalculado).
// =============================================================================
#pragma once

#include "aurea/scene3d/SceneAsset.hpp"

#include <vector>

namespace aurea::scene3d {

/// O que a bola mostra: os fatores do PBR (lineares) e o mapa de cor.
struct MaterialBall {
    Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};   ///< linear; o alfa vale em Blend/Mask
    f32  metallic = 0.0f;
    f32  roughness = 0.5f;
    f32  specular = 1.0f;                     ///< KHR_materials_specular (dielétrico)
    Vec3 emissive{0.0f, 0.0f, 0.0f};          ///< linear, já multiplicada pela força
    AlphaMode alphaMode = AlphaMode::Opaque;
    f32  alphaCutoff = 0.5f;
    bool unlit = false;                       ///< cor de exibição (sem luz, sem tone map)
    /// Mapa de cor em RGBA8 sRGB (nulo = só a cor). NÃO é copiado: tem de
    /// viver até o fim da chamada (o motor segura o asset dono dos pixels).
    const u8* image = nullptr;
    u32 imageWidth = 0, imageHeight = 0;
};

inline constexpr u32 kMaterialPreviewMinSize = 16;
inline constexpr u32 kMaterialPreviewMaxSize = 256;

/// A bola do material: `size` × `size` em RGBA8 sRGB, alfa RETO, fundo
/// transparente e borda suavizada. Vazio = tamanho fora da faixa.
[[nodiscard]] std::vector<u8> render_material_ball(const MaterialBall& m, u32 size);

/// O mesmo, passando por um cache por receita (fatores + amostra do mapa +
/// tamanho), limitado em bytes. Thread-safe.
[[nodiscard]] std::vector<u8> material_ball_cached(const MaterialBall& m, u32 size);

/// Fatores de um material do asset e o mapa de cor dele (quando a imagem já
/// tem pixels). Os ponteiros apontam para dentro de `asset`.
[[nodiscard]] MaterialBall material_ball_of(const SceneAsset& asset, const Material& m) noexcept;

/// A bola de um material pronto do texto 3D (0..6, a ordem de
/// apply_text3d_material_preset): o material da FRENTE da receita.
[[nodiscard]] bool text3d_preset_ball(u32 preset, MaterialBall& out) noexcept;

} // namespace aurea::scene3d
