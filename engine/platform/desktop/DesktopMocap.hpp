// =============================================================================
//  Aurea / platform / desktop / DesktopMocap.hpp
//
//  A sessão de análise do Motion Capture do Aurea PC, sem JNI (a ponte em
//  DesktopMocap.cpp só traduz tipos; aurea_pc_mocap_bench usa direto).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <string>

namespace aurea::desktop_mocap {

struct Session;

inline constexpr u32 kInfoFloats = 6;    ///< [erro (0 ok, 1 modelos, 2 vídeo), duração s, Vulkan 0/1, largura, altura, fps]
inline constexpr u32 kStateFloats = 6;   ///< [progresso, largura/altura, fim, ms de rede/quadro, vezes do detector, quadros com pessoa]

/// Como analisar. `defaults(reduced)` é o que o app usa; o bench liga e
/// desliga cada peça para medir (e `legacy()` refaz o pipeline de 12/11).
struct Options {
    f64 fps = 30.0;          ///< amostras por segundo do vídeo
    u32 maxSide = 0;         ///< lado maior da imagem da análise (0 = resolução do vídeo)
    bool legacyImage = false;///< imagem reduzida por vizinho mais próximo (o de 12/11)
    bool heavy = true;       ///< pose_landmark_heavy (cai no full sem o arquivo)
    bool antialias = true;   ///< recorte pela pirâmide (sem serrilhado)
    bool refine = true;      ///< refino pelo heatmap (só o heavy)
    bool world = true;       ///< manda os pontos de mundo (quadro companheiro)
    bool videoTracker = true;///< mocap::VideoPoseTracker (ROI filtrada, detector de novo, lados)
    bool filterRoi = true;
    bool fixSwaps = true;
    bool borderRedetect = true;
    u32 recheckEvery = 15;

    [[nodiscard]] static Options defaults(bool reduced) noexcept;
    [[nodiscard]] static Options legacy(bool reduced) noexcept;
};

/// Carrega os modelos de `modelDir` e abre o vídeo (UTF-8). Nulo = falhou (`info[0]` diz o quê).
Session* open(const std::string& modelDir, const std::string& videoPath, bool reduced, f32 info[kInfoFloats]);
Session* open(const std::string& modelDir, const std::string& videoPath, const Options& options, f32 info[kInfoFloats]);
/// Até `maxFrames` quadros (rig::kMocapFrameFloats cada) em `out`. −1 = erro do decoder.
/// Com Options::world cada quadro analisado ocupa DOIS (o de imagem e o
/// companheiro de mundo, rig::kMocapWorldFlag): `maxFrames` conta os dois.
i32 next(Session* session, f32* out, i32 maxFrames, f32 state[kStateFloats]);
void close(Session* session);

/// Para o bench: a imagem do último quadro analisado e quantas trocas de lado o rastreio desfez.
struct FrameView {
    const u8* rgba = nullptr;
    u32 width = 0, height = 0;
};
[[nodiscard]] FrameView last_image(const Session* session);
[[nodiscard]] u32 swaps_fixed(const Session* session);
[[nodiscard]] f64 image_ms(const Session* session);   ///< conversão do quadro + pirâmide, total

} // namespace aurea::desktop_mocap
