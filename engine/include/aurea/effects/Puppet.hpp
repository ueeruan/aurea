#pragma once
// =============================================================================
//  Aurea / effects / Puppet.hpp
//
//  Fantoche (Puppet Pin do AE): pinos sobre a camada; arrastar um pino
//  deforma a malha triangulada da camada "o mais rígido possível" (ARAP de
//  Igarashi, Moscovich e Hughes 2005 — dois passos lineares: similaridade,
//  depois ajuste de escala). Tudo em px da camada.
//
//  Os pinos moram nos parâmetros (ocultos) do efeito `aurea.distort.puppet`:
//  por pino, liga/desliga, a posição de REPOUSO (onde foi posto na imagem
//  sem deformar) e a posição ATUAL (animável: keyframes no playhead).
// =============================================================================
#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <vector>

namespace aurea::puppet {

inline constexpr const char* kPuppetKey = "aurea.distort.puppet";
inline constexpr u32 kMaxPins = 16;

/// Índices dos parâmetros do efeito. Os pinos começam em `kFirstPin`, três
/// parâmetros por pino (ordem gravada no projeto: só cresce no fim).
enum Param : u32 {
    kTriangles = 0,   ///< densidade da malha (alvo de triângulos)
    kExpansion = 1,   ///< margem da malha além da camada (px)
    kRigidity = 2,    ///< 0..100: 100 = a camada inteira só gira/anda
    kFirstPin = 3,
};
[[nodiscard]] constexpr u32 pin_on(u32 i) noexcept { return kFirstPin + i * 3; }
[[nodiscard]] constexpr u32 pin_rest(u32 i) noexcept { return kFirstPin + i * 3 + 1; }
[[nodiscard]] constexpr u32 pin_pos(u32 i) noexcept { return kFirstPin + i * 3 + 2; }
/// CONTORNO da camada (a malha segue o que é opaco): grade kOutline×kOutline
/// sobre a caixa da camada, uma linha por parâmetro (24 bits num float —
/// inteiro exato). Gravado quando o primeiro pino entra (alfa da imagem ou o
/// recorte do Rotobrush). Tudo zero = sem contorno (a caixa inteira).
inline constexpr u32 kOutline = 24;
inline constexpr u32 kOutlineFirst = kFirstPin + kMaxPins * 3;
inline constexpr u32 kParamCount = kOutlineFirst + kOutline;

struct Pin {
    Vec2 rest{};
    Vec2 pos{};
};

/// Malha triangulada em px da camada (repouso).
struct Mesh {
    std::vector<Vec2> rest;
    std::vector<u32> tris;   ///< 3 índices por triângulo
};

/// Grade triangulada sobre (−exp, −exp)–(w+exp, h+exp) com ~`triangles`
/// triângulos. `occupancy` (opcional, `ow`×`oh` células 0/1 sobre a camada)
/// tira os triângulos sem nada opaco por perto — a malha segue o contorno.
void build_mesh(f32 width, f32 height, u32 triangles, f32 expansion, Mesh& out,
                const u8* occupancy = nullptr, u32 ow = 0, u32 oh = 0);

/// Vértices deformados (mesma ordem de `mesh.rest`). Sem pino: cópia do
/// repouso. `rigidity` 0..1 mistura o ARAP com o melhor movimento rígido.
void deform(const Mesh& mesh, const std::vector<Pin>& pins, f32 rigidity, std::vector<Vec2>& out);

/// Linhas do contorno (kOutline floats) → células 0/1 (kOutline²). false = vazio.
bool outline_cells(const f32* rows, std::vector<u8>& cells);
/// Cobertura (0..1, `w`×`h`, linha a linha) → linhas do contorno. `threshold`
/// = cobertura mínima de algum pixel da célula para ela contar.
void outline_rows(const f32* coverage, u32 w, u32 h, f32 threshold, f32* rows);

/// Ponto da imagem em repouso sob `p` (px da camada) na malha deformada
/// `deformed`; false = fora da malha (devolve o próprio ponto).
bool rest_point(const Mesh& mesh, const std::vector<Vec2>& deformed, Vec2 p, Vec2& rest) noexcept;

} // namespace aurea::puppet
