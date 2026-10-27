// =============================================================================
//  Aurea / rig / AutoRig.hpp
//
//  AutoRig 3D (0.3.6 BETA): humanoide em T-pose → 17 ossos + skin + pesos.
//
//  1. Detecção (detect_tpose): os vértices em repouso, no espaço do modelo
//     (Y para cima). Caixa, perfil de largura por fatias de altura (o "T" dos
//     braços é a faixa mais larga), o eixo lateral (X ou Z) e a frente (os
//     dedos dos pés apontam para a frente), a virilha (onde as pernas se
//     juntam), o pescoço (a fatia mais estreita entre ombros e topo), e os
//     eixos dos braços e das pernas pelos centróides das seções. Recusa com
//     motivo (AutoRigError) quando não parece um humanoide em T-pose.
//  2. Pesos (build_rigged_asset): distância de cada vértice ao SEGMENTO de
//     cada osso com queda (inverso da distância⁴), lado do corpo respeitado,
//     no máximo 4 influências, normalizados, suavizados pelos vizinhos da
//     malha (2 passadas) e normalizados de novo.
//  3. O asset ganha os nós "aurea:<osso>" (só translação: a T-pose é o
//     repouso), uma skin por nó de malha e os fluxos JOINTS/WEIGHTS — o mesmo
//     caminho de skin dos rigs importados no renderer (GLES/Vulkan/Metal).
//
//  Determinístico: as mesmas juntas dão os mesmos pesos. O projeto guarda só
//  a receita (encode_rig_spec, no Asset) e reconstrói ao abrir.
// =============================================================================
#pragma once

#include "aurea/rig/Humanoid.hpp"

#include <atomic>
#include <string>

namespace aurea::scene3d {
struct SceneAsset;
}

namespace aurea::rig {

/// Por que o AutoRig não rodou. O número é contrato com Kotlin/Swift.
enum class AutoRigError : i32 {
    None = 0,
    NoGeometry = 1,      ///< sem vértices suficientes
    AlreadyRigged = 2,   ///< o modelo já tem skin (use o rig dele)
    NotHumanoid = 3,     ///< não tem tronco/braços/pernas/cabeça reconhecíveis
    NotTPose = 4,        ///< braços não abertos na horizontal
    Cancelled = 5,
    TooHeavy = 6,        ///< passa da memória do aparelho
    NotImported = 7,     ///< texto/forma 3D: não é personagem
    Busy = 8,            ///< outro AutoRig em andamento
    Failed = 9,          ///< erro interno (camada sumiu, modelo não carregado)
};

[[nodiscard]] const char* to_string(AutoRigError e) noexcept;

/// Juntas canônicas em espaço do modelo (repouso). Sempre as 17.
struct AutoRigJoints {
    std::array<Vec3, kHumanoidBoneCount> position{};
    /// Pontas dos ossos-folha (cabeça, mãos, pés): só para os pesos.
    Vec3 headTop{0, 0, 0}, leftHandTip{0, 0, 0}, rightHandTip{0, 0, 0}, leftToe{0, 0, 0}, rightToe{0, 0, 0};
};

/// Progresso 0..1 e cancelamento cooperativo (os dois opcionais).
struct AutoRigControl {
    std::atomic<f32>* progress = nullptr;
    const std::atomic<bool>* cancel = nullptr;
    [[nodiscard]] bool cancelled() const noexcept { return cancel && cancel->load(std::memory_order_relaxed); }
    void report(f32 p) const noexcept { if (progress) progress->store(p, std::memory_order_relaxed); }
};

/// Detecta o humanoide em T-pose e posiciona as 17 juntas.
[[nodiscard]] AutoRigError detect_tpose(const scene3d::SceneAsset& asset, AutoRigJoints& out,
                                        const AutoRigControl& control = {});

/// Pontas dos ossos-folha a partir das juntas (quando a receita só tem as 17).
void derive_tips(AutoRigJoints& joints) noexcept;

/// Acrescenta os ossos, as skins e os pesos ao asset (que não pode ter skin).
[[nodiscard]] AutoRigError build_rigged_asset(scene3d::SceneAsset& asset, const AutoRigJoints& joints,
                                              const AutoRigControl& control = {});

/// Receita guardada no Asset do projeto (Asset::ModelInfo::rig).
///   "arig1 a <17×3 floats>"           AutoRig (juntas em espaço do modelo)
///   "arig1 m <17 ints>"               rig importado com mapa corrigido à mão
[[nodiscard]] std::string encode_autorig_spec(const AutoRigJoints& joints);
[[nodiscard]] std::string encode_map_spec(const HumanoidMap& map);
/// Decodifica. true = válida; `isAuto` diz qual dos dois.
bool decode_rig_spec(const std::string& spec, bool& isAuto, AutoRigJoints& joints, HumanoidMap& map) noexcept;

/// Mapa humanoide de um asset com a receita do projeto (a do Asset; vazia =
/// só pelos nomes).
[[nodiscard]] HumanoidMap humanoid_map(const scene3d::SceneAsset& asset, const std::string& spec);

/// Aplica a receita a um asset recém-importado do arquivo (abrir projeto,
/// reimportar texturas). Receita de mapa não muda a geometria.
[[nodiscard]] AutoRigError apply_rig_spec(scene3d::SceneAsset& asset, const std::string& spec);

/// O asset já tem skin (rig importado)?
[[nodiscard]] bool has_skin(const scene3d::SceneAsset& asset) noexcept;

} // namespace aurea::rig
