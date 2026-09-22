// =============================================================================
//  Aurea / vector / VectorData.hpp
//
//  O dado da camada vetorial (forma com shapeType == kShapeVector), no molde
//  da shape layer do After Effects: grupos; cada grupo tem caminhos bezier
//  (livres ou paramétricos) e os operadores que valem para eles — Mesclar
//  (booleanas), Aparar (trim), Preencher, Contorno e Repetidor — mais o
//  transform do grupo.
//
//  Só dado (POD + vetores). As contas (achatar, recortar, contornar,
//  triangular) moram em vector/Vector.hpp; os valores animáveis moram na
//  TrackSet da camada como TrackProperty::VectorParam (effectIndex = grupo,
//  effectParamIndex = VectorParam); a forma do caminho animada (morph) mora
//  no próprio caminho (`keys`), porque um keyframe de forma é um caminho
//  inteiro, não um número.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <string>
#include <vector>

namespace aurea {

/// shapeType da ShapeData para a camada vetorial (0..10 = formas SDF).
inline constexpr u32 kShapeVector = 11;

/// Vértice bezier: posição e tangentes RELATIVAS à posição (como a máscara).
struct BezierVertex {
    Vec2 p{0.0f, 0.0f};
    Vec2 in{0.0f, 0.0f};
    Vec2 out{0.0f, 0.0f};
    friend bool operator==(const BezierVertex&, const BezierVertex&) noexcept = default;
};

struct BezierPath {
    std::vector<BezierVertex> v;
    bool closed = true;
    friend bool operator==(const BezierPath&, const BezierPath&) noexcept = default;
};

/// Keyframe de forma (morph): o caminho inteiro num quadro (tempo LOCAL da
/// camada). Interpolação: 0 linear, 1 suave (ease in-out), 2 congelar.
struct PathKey {
    i64        frame = 0;
    BezierPath path;
    u8         ease = 1;
    friend bool operator==(const PathKey&, const PathKey&) noexcept = default;
};

enum class VectorPathKind : u8 { Free = 0, Rect, Ellipse, Polygon, Star };

struct VectorPath {
    VectorPathKind kind = VectorPathKind::Free;
    BezierPath path;                     ///< Free
    /// Paramétricos (centro no espaço do grupo).
    Vec2 center{0.0f, 0.0f};
    Vec2 size{200.0f, 200.0f};           ///< retângulo / elipse
    f32  roundness = 0.0f;               ///< retângulo: raio dos cantos (px)
    f32  points = 5.0f;                  ///< polígono / estrela
    f32  outerRadius = 100.0f, innerRadius = 50.0f;
    f32  outerRoundness = 0.0f, innerRoundness = 0.0f;   ///< %
    f32  rotation = 0.0f;                ///< graus
    bool reversed = false;               ///< direção invertida (furo em não-zero)
    std::vector<PathKey> keys;           ///< morph (só Free); vazio = parado
    friend bool operator==(const VectorPath&, const VectorPath&) noexcept = default;
};

struct VectorStop {
    f32  pos = 0.0f;                     ///< 0..1
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};  ///< sRGB, alfa reto
    friend bool operator==(const VectorStop&, const VectorStop&) noexcept = default;
};

/// Tinta: cor sólida ou degradê (0 sólido, 1 linear, 2 radial). Os pontos do
/// degradê ficam no espaço do grupo (radial: centro = start, raio = |end − start|).
struct VectorPaint {
    u8   type = 0;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    Vec2 start{-100.0f, 0.0f}, end{100.0f, 0.0f};
    std::vector<VectorStop> stops;
    f32  opacity = 100.0f;               ///< %
    friend bool operator==(const VectorPaint&, const VectorPaint&) noexcept = default;
};

struct VectorFill {
    bool enabled = true;
    VectorPaint paint;
    u8   rule = 0;                       ///< 0 não-zero, 1 par-ímpar
    friend bool operator==(const VectorFill&, const VectorFill&) noexcept = default;
};

struct VectorStroke {
    bool enabled = false;
    VectorPaint paint;
    f32  width = 6.0f;                   ///< px
    u8   cap = 0;                        ///< 0 reta, 1 redonda, 2 quadrada
    u8   join = 0;                       ///< 0 miter, 1 redonda, 2 chanfro
    f32  miterLimit = 4.0f;
    std::vector<f32> dashes;             ///< traço, vão, traço, vão… (px); vazio = contínuo
    f32  dashOffset = 0.0f;
    friend bool operator==(const VectorStroke&, const VectorStroke&) noexcept = default;
};

struct VectorTrim {
    bool enabled = false;
    f32  start = 0.0f, end = 100.0f;     ///< %
    f32  offset = 0.0f;                  ///< % do comprimento (gira o começo)
    u8   mode = 0;                       ///< 0 cada caminho, 1 todos em sequência
    friend bool operator==(const VectorTrim&, const VectorTrim&) noexcept = default;
};

struct VectorRepeater {
    bool enabled = false;
    f32  copies = 3.0f;
    f32  offset = 0.0f;                  ///< desloca o índice das cópias
    Vec2 anchor{0.0f, 0.0f};
    Vec2 position{120.0f, 0.0f};         ///< por cópia
    f32  scale = 100.0f;                 ///< % por cópia (acumula)
    f32  rotation = 0.0f;                ///< graus por cópia
    f32  startOpacity = 100.0f, endOpacity = 100.0f;   ///< %
    u8   above = 0;                      ///< 0 cópias novas embaixo, 1 por cima
    friend bool operator==(const VectorRepeater&, const VectorRepeater&) noexcept = default;
};

/// Mesclar: 0 nenhum (os caminhos juntos pela regra do preenchimento),
/// 1 unir, 2 subtrair (o primeiro menos os outros), 3 interseção, 4 excluir.
struct VectorGroup {
    std::string name = "Grupo";
    bool visible = true;
    std::vector<VectorPath> paths;
    u8   merge = 0;
    VectorFill     fill;
    VectorStroke   stroke;
    VectorTrim     trim;
    VectorRepeater repeater;
    Vec2 position{0.0f, 0.0f}, anchor{0.0f, 0.0f};
    Vec2 scale{100.0f, 100.0f};          ///< %
    f32  rotation = 0.0f;                ///< graus
    f32  opacity = 100.0f;               ///< %
    friend bool operator==(const VectorGroup&, const VectorGroup&) noexcept = default;
};

struct VectorData {
    std::vector<VectorGroup> groups;
    friend bool operator==(const VectorData&, const VectorData&) noexcept = default;
};

/// Parâmetros animáveis de um grupo (TrackProperty::VectorParam). Os números
/// estão gravados em projetos: novos entram no fim.
enum VectorParam : u32 {
    kVecTrimStart = 0, kVecTrimEnd, kVecTrimOffset,
    kVecStrokeWidth, kVecDashOffset, kVecFillOpacity, kVecStrokeOpacity,
    kVecRepCopies, kVecRepOffset, kVecRepPosX, kVecRepPosY, kVecRepRotation, kVecRepScale,
    kVecRepStartOpacity, kVecRepEndOpacity,
    kVecPosX, kVecPosY, kVecRotation, kVecScale, kVecOpacity,
    kVecParamCount
};

} // namespace aurea
