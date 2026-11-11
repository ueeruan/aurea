// =============================================================================
//  Aurea / timeline / LayerWorld.hpp
//
//  AS MATRIZES DA CAMADA e a cadeia de pais, fora do renderer (AE animation,
//  gap_system item 6). Antes moravam no Renderer.cpp; agora o render, as
//  expressões (toComp/fromComp/toWorld/fromWorld) e as consultas da UI usam a
//  MESMA conta, sem precisar de um Renderer. O código foi movido sem mudar
//  nenhum número: projeto antigo desenha igual.
//
//  Também é aqui que entram o Auto-Orientar da camada (ao longo do caminho /
//  para a câmera) e a câmera de dois nós (mira no Ponto de Interesse).
//
//  Duas coisas continuam no renderer e chegam por gancho (registrado por ele
//  na inicialização do programa): o quadro do osso de um modelo 3D ("Prender a
//  um osso") e a projeção da câmera ativa (composição ← mundo). Sem gancho, o
//  osso é a identidade (a camada segue o modelo inteiro, como fora do render)
//  e a projeção é a identidade.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

namespace aurea {

class Composition;
struct Layer;
struct Track;

namespace world {

/// Valor da trilha no instante local fracionário (curva, hold, expressão).
[[nodiscard]] f32 sample_frac(const Track& tr, f64 t, f32 fallback) noexcept;
/// Matriz 2D da camada (pai ← camada) num tempo local fracionário, com os
/// animadores de camada e o Auto-Orientar ao longo do caminho.
[[nodiscard]] Mat4 layer_matrix_frac(const Layer& l, f64 local, f64 fps) noexcept;
/// Matriz 3D da camada (pai ← camada). `cameraInParent` (opcional) = a câmera
/// ativa no espaço do pai: com Auto-Orientar "para a câmera" a camada fica de
/// frente para ela.
[[nodiscard]] Mat4 layer_matrix_3d_frac(const Layer& l, f64 local, f64 fps, const Vec3* cameraInParent = nullptr) noexcept;

[[nodiscard]] bool under_layering_camera(const Composition& comp, const Layer& l, FrameIndex time) noexcept;
/// A camada (ou um pai) vive no espaço 3D no instante? (a regra do render)
[[nodiscard]] bool wants_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept;
[[nodiscard]] Mat4 parenting_basis(const Layer& child, Mat4 parent, f64 local, f32* continuousZ) noexcept;

/// Mundo da camada com a cadeia de pais; cada pai no instante `parentTime`.
[[nodiscard]] Mat4 world_chain_frac(const Composition& comp, const Layer& l, f64 time, bool threeD, f64 parentTime) noexcept;
[[nodiscard]] Mat4 world_2d_frac(const Composition& comp, const Layer& l, f64 time) noexcept;
[[nodiscard]] Mat4 world_3d_frac(const Composition& comp, const Layer& l, f64 time) noexcept;
/// Composição/mundo ← camada como o render a põe: 3D quando `wants_3d`.
[[nodiscard]] Mat4 layer_world_frac(const Composition& comp, const Layer& l, f64 time) noexcept;

/// Cache do mundo dos PAIS durante um quadro (Renderer::prepare, prévia e
/// export). Aninhável: vale o de fora. Fora dele não há cache.
class WorldMatrixCacheScope {
public:
    WorldMatrixCacheScope() noexcept;
    ~WorldMatrixCacheScope();
    WorldMatrixCacheScope(const WorldMatrixCacheScope&) = delete;
    WorldMatrixCacheScope& operator=(const WorldMatrixCacheScope&) = delete;
private:
    void* owned_ = nullptr;
};

/// Posição da câmera ativa no MUNDO no instante (a de cima, visível, ativa,
/// no tempo; sem ela, a câmera padrão). É a câmera que o Auto-Orientar "para
/// a câmera" segue.
[[nodiscard]] Vec3 active_camera_position(const Composition& comp, f64 time) noexcept;

/// Composição (px, dividir por w) ← mundo 3D, pela câmera ativa (gancho do
/// renderer: a MESMA projeção do desenho).
[[nodiscard]] Mat4 comp_from_world(const Composition& comp, f64 time) noexcept;
/// Inversa geral 4×4 (falso = singular; `out` = identidade).
bool invert(const Mat4& m, Mat4& out) noexcept;

// --- Ganchos do renderer -------------------------------------------------------
using BoneFrameFn = Mat4 (*)(const Composition& comp, const Layer& parent, i32 node, f64 time) noexcept;
using CompProjectionFn = Mat4 (*)(const Composition& comp, f64 time) noexcept;
void set_bone_frame_provider(BoneFrameFn fn) noexcept;
void set_comp_projection_provider(CompProjectionFn fn) noexcept;

} // namespace world
} // namespace aurea
