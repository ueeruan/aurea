// =============================================================================
//  Aurea / engine / EngineVector3D.hpp  (interno do motor)
//
//  Pontos do caminho vetorial numa camada 3D: a conta grupo → composição pela
//  câmera que o palco mostra (projeção com w) e a volta composição → grupo
//  pelo plano da camada (raio × plano). Usada por Engine::vector_path_at
//  (EngineVector.cpp) e pelas funções de EngineVector3D.cpp.
//
//  Os pontos continuam PLANOS (x, y no espaço do grupo): o caminho bezier não
//  tem profundidade por ponto, e o projeto salvo não muda.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/vector/Vector.hpp"

namespace aurea {
class Composition;
struct Layer;

namespace vector3d {

/// w mínimo para um ponto contar como "na frente da câmera" (px de profundidade).
inline constexpr f32 kNearW = 1e-3f;

/// A conta do palco para um grupo vetorial no instante.
struct StageFrame {
    Mat4 compFromGroup = Mat4::identity();   ///< px da composição (homogêneo) ← grupo
    Mat4 worldFromGroup = Mat4::identity();  ///< mundo 3D ← grupo (só com `perspective`)
    scene3d::SceneCamera camera{};           ///< a câmera do palco (só com `perspective`)
    u32 width = 1, height = 1;
    bool perspective = false;                ///< camada no espaço 3D
};

/// `editor` = a vista de navegação da Cena 3D quando ligada (nulo = câmera ativa).
[[nodiscard]] StageFrame stage_frame(const Composition& comp, const Layer& l, FrameIndex now,
                                     const vector::Affine2& group, const SceneEditorView* editor) noexcept;
/// Grupo → composição. Falso = atrás da câmera (`out` fica sem sentido).
bool project(const StageFrame& f, Vec2 g, Vec2& out) noexcept;
/// Composição → grupo pelo plano da camada. Falso = plano de lado ou atrás da câmera.
bool unproject(const StageFrame& f, f32 x, f32 y, Vec2& out) noexcept;

} // namespace vector3d
} // namespace aurea
