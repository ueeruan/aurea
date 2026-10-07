// =============================================================================
//  Aurea / effects / MeshWarp.hpp
//
//  Malha de deformação (aurea.distort.mesh_warp): a grade (linhas+1)×(colunas+1)
//  de vértices, cada um com 4 alças de bezier (esquerda, direita, cima, baixo).
//  Cada célula é um retalho de Coons cujas bordas são as curvas cúbicas entre
//  dois vértices vizinhos; com as alças a 1/3 da aresta (a malha de fábrica) o
//  retalho é bilinear — o desenho sai idêntico à camada.
//
//  Coordenadas NORMALIZADAS ao tamanho da camada (0..1 = a camada inteira):
//  trocar a resolução ou o tamanho da fonte não estraga a malha. Posição de
//  vértice e alças relativas à posição dele, em `kMeshWarpVertexFloats` floats:
//      [0,1] posição   [2,3] alça esquerda   [4,5] alça direita
//      [6,7] alça cima [8,9] alça baixo
//
//  Animação: `keys` como o caminho das máscaras (MaskPathKey) — vazio = a
//  malha parada (`values`); senão os keys mandam (0 segura, 1 linear, 2 suave).
//  A malha só vale para a grade em que foi feita (`rows`×`cols`): trocar
//  Linhas/Colunas a redefine, como no After Effects.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea {

inline constexpr const char* kMeshWarpKey = "aurea.distort.mesh_warp";
inline constexpr u32 kMeshWarpMaxDivisions = 31;
inline constexpr u32 kMeshWarpVertexFloats = 10;
/// Teto de keys por malha (projeto estragado não aloca sem fim).
inline constexpr u32 kMeshWarpMaxKeys = 4096;

struct MeshWarpKey {
    i64 frame = 0;           ///< tempo LOCAL da camada
    u8  interp = 1;          ///< 0 segura, 1 linear, 2 suave
    std::vector<f32> values; ///< (rows+1)(cols+1) × kMeshWarpVertexFloats
};

struct MeshWarpData {
    u32 rows = 0;
    u32 cols = 0;
    std::vector<f32> values;
    std::vector<MeshWarpKey> keys;
};

namespace mesh_warp {

[[nodiscard]] constexpr u32 vertex_count(u32 rows, u32 cols) noexcept { return (rows + 1) * (cols + 1); }
[[nodiscard]] constexpr u32 value_count(u32 rows, u32 cols) noexcept {
    return vertex_count(rows, cols) * kMeshWarpVertexFloats;
}

/// A malha de fábrica (grade regular, alças a 1/3 da aresta).
void identity(u32 rows, u32 cols, std::vector<f32>& out);
/// `values` é a malha de fábrica (tolerância 1e-6)?
[[nodiscard]] bool is_identity(const std::vector<f32>& values, u32 rows, u32 cols) noexcept;
/// A malha guardada vale para esta grade? (senão: redefinida)
[[nodiscard]] bool matches(const MeshWarpData& d, u32 rows, u32 cols) noexcept;
/// A malha no instante `frame` (tempo local, fracionário). Sem dado ou de outra
/// grade = a de fábrica. Devolve false quando o resultado é a de fábrica.
bool evaluate(const MeshWarpData* d, u32 rows, u32 cols, f64 frame, std::vector<f32>& out);
/// Índice do key exatamente em `frame`, ou −1.
[[nodiscard]] i32 key_at(const MeshWarpData& d, i64 frame) noexcept;

/// Subdivisões de cada retalho pela Qualidade (1..10), limitadas para a grade
/// inteira caber em `maxGrid` pontos por eixo.
[[nodiscard]] u32 subdivisions(u32 quality, u32 divisions, u32 maxGrid = 513) noexcept;
/// Grade tesselada (normalizada): (cols·subX+1) × (rows·subY+1) pontos, linha
/// a linha. O ponto (i, j) é a imagem do ponto-fonte (i/(gx−1), j/(gy−1)).
void tessellate(const std::vector<f32>& values, u32 rows, u32 cols, u32 subX, u32 subY,
                std::vector<Vec2>& out, u32& gx, u32& gy);
/// Um ponto do retalho (r, c) em (u, v) ∈ [0,1]² (normalizado).
[[nodiscard]] Vec2 patch_point(const f32* values, u32 cols, u32 r, u32 c, f32 u, f32 v) noexcept;

/// Move um vértice (as alças vão junto) ou uma alça (1..4) para `p`
/// (normalizado). Recusa índice fora da grade.
bool move_handle(std::vector<f32>& values, u32 rows, u32 cols, u32 vertex, u32 handle, Vec2 p) noexcept;

} // namespace mesh_warp
} // namespace aurea
