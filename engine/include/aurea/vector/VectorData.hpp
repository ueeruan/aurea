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
/// Documento v4: a curva do trecho que SAI do key, a mesma dos keyframes
/// comuns (`aurea::Interpolation` + alças + força, avaliada por
/// `keyframe_ease`). `kPathCurveLegacy` = sem curva própria: vale `ease`.
inline constexpr u8 kPathCurveLegacy = 0xFF;
struct PathKey {
    i64        frame = 0;
    BezierPath path;
    u8         ease = 1;
    u8         curve = kPathCurveLegacy;
    u8         easePower = 1;
    f32        bx1 = 0.33f, by1 = 0.0f, bx2 = 0.67f, by2 = 1.0f;
    /// 0.5.0 (keyframe por ponto, como no Alight Motion): 1 = este key anima o
    /// ponto; vazio = todos. Os outros pontos passam por este instante como se
    /// o key não existisse. Mesmo tamanho de `path.v` quando preenchido.
    std::vector<u8> points;
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
    /// Omega conteúdo (área 06): FURO. O contorno recorta o preenchimento do
    /// grupo (o "preencher ↔ furo" do menu de contornos); o contorno continua
    /// sendo desenhado. Documento v3.
    bool exclude = false;
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
    /// Afinar (como no app antigo): % do comprimento em que a largura sai de
    /// zero no começo / volta a zero no fim; `taperEase` (0..100 %) arredonda
    /// a rampa (0 = reta, 100 = bojuda). 0/0 = largura constante.
    f32  taperStart = 0.0f, taperEnd = 0.0f, taperEase = 0.0f;
    /// Omega conteúdo (área 06): perfil de largura do pincel — fator (0..1) da
    /// largura em amostras IGUALMENTE espaçadas ao longo do comprimento (a
    /// pressão do dedo). Vazio = largura constante. Documento v3.
    std::vector<f32> widthProfile;
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
    /// Documento v6 (escala X/Y do After Effects): `scale` é a escala X e
    /// `scaleY` a Y. `scaleLinked` = Y acompanha X (o repetidor de sempre:
    /// documentos antigos e trilhas antigas de kVecRepScale continuam iguais).
    f32  scaleY = 100.0f;                ///< % por cópia (acumula), só com scaleLinked == 0
    u8   scaleLinked = 1;
    friend bool operator==(const VectorRepeater&, const VectorRepeater&) noexcept = default;
};

/// Operadores de caminho do grupo (shape layer do After Effects, ADBE Vector
/// Filter *). Valem em ORDEM, sobre os caminhos do grupo, depois de montar os
/// caminhos e antes de Mesclar/Aparar. Os números estão gravados em projetos:
/// novos entram no fim.
enum class VectorModifierType : u8 {
    OffsetPaths = 0,     ///< Deslocar caminhos (Clipper do motor)
    PuckerBloat,         ///< Franzir e inflar
    Roughen,             ///< Aspereza (parada; Frequência temporal > 0 anima)
    WigglePaths,         ///< Ondular caminhos (o mesmo código do Roughen, animado)
    RoundCorners,        ///< Arredondar cantos
    Twist,               ///< Torcer
    ZigZag,              ///< Zigue-zague
    WiggleTransform,     ///< Ondular transform (afim aleatória sobre os caminhos)
    Count
};

/// Parâmetros por operador (VectorModifier::params). Índices por tipo —
/// gravados em projetos e nas trilhas: nunca renumerar.
inline constexpr u32 kVecModParams = 16;
/// Semente aleatória: o mesmo índice em todos os tipos que sorteiam.
inline constexpr u32 kVecModSeed = 15;
// Deslocar caminhos
inline constexpr u32 kVecModOffsetAmount = 0, kVecModOffsetJoin = 1, kVecModOffsetMiter = 2,
                     kVecModOffsetCopies = 3, kVecModOffsetCopyOffset = 4;
// Franzir e inflar
inline constexpr u32 kVecModPuckerAmount = 0;
// Aspereza / Ondular caminhos
inline constexpr u32 kVecModRoughSize = 0, kVecModRoughDetail = 1, kVecModRoughPoints = 2, kVecModRoughFreq = 3,
                     kVecModRoughCorrelation = 4, kVecModRoughTemporalPhase = 5, kVecModRoughSpatialPhase = 6;
// Arredondar cantos
inline constexpr u32 kVecModRoundRadius = 0;
// Torcer
inline constexpr u32 kVecModTwistAngle = 0, kVecModTwistCenterX = 1, kVecModTwistCenterY = 2;
// Zigue-zague
inline constexpr u32 kVecModZigSize = 0, kVecModZigRidges = 1, kVecModZigPoints = 2;
// Ondular transform
inline constexpr u32 kVecModWigAnchorX = 0, kVecModWigAnchorY = 1, kVecModWigPosX = 2, kVecModWigPosY = 3,
                     kVecModWigScaleX = 4, kVecModWigScaleY = 5, kVecModWigRotation = 6, kVecModWigFreq = 7,
                     kVecModWigCorrelation = 8, kVecModWigTemporalPhase = 9, kVecModWigSpatialPhase = 10;

/// Um operador de caminho. `params` por tipo (ver os kVecMod* acima);
/// animáveis pela trilha VectorParam com o id `vector_modifier_param`.
struct VectorModifier {
    VectorModifierType type = VectorModifierType::OffsetPaths;
    bool enabled = true;
    f32  params[kVecModParams]{};
    /// Tempo da camada em segundos no instante avaliado (NÃO é gravado):
    /// `evaluate_group` preenche, os operadores animados (Ondular) leem.
    f64  time = 0.0;
    friend bool operator==(const VectorModifier&, const VectorModifier&) noexcept = default;
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
    /// Omega conteúdo (área 06): composição do grupo sobre os grupos de BAIXO
    /// (os anteriores na lista). 0 normal; 1 apagar (a borracha do desenho):
    /// a forma do grupo é tirada da geometria dos grupos anteriores — destino
    /// × (1 − α), com α = 1. Documento v3.
    u8   composite = 0;
    /// Documento v6: operadores de caminho (Deslocar, Franzir e inflar,
    /// Aspereza, Ondular, Arredondar cantos, Torcer, Zigue-zague, Ondular
    /// transform), em ordem, entre os caminhos e o Mesclar/Aparar.
    std::vector<VectorModifier> modifiers;
    friend bool operator==(const VectorGroup&, const VectorGroup&) noexcept = default;
};

/// Composição do grupo (VectorGroup::composite).
inline constexpr u8 kVectorCompositeNormal = 0;
inline constexpr u8 kVectorCompositeErase = 1;

struct VectorData {
    std::vector<VectorGroup> groups;
    /// Omega conteúdo (área 06): camada de DESENHO (traços de caneta, pincel,
    /// preencher e borracha). Só diz à interface qual editor abrir; o desenho
    /// continua editável como vetor. Documento v3.
    bool drawing = false;
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
    kVecRepScaleY,       ///< v6: escala Y do repetidor (só com scaleLinked == 0)
    kVecParamCount
};

/// Parâmetros dos operadores de caminho na trilha VectorParam: um bloco de
/// kVecModParams ids por operador, depois de todos os VectorParam comuns
/// (effectIndex = grupo, effectParamIndex = este id).
inline constexpr u32 kVecModParamBase = 0x10000;
inline constexpr u32 kVecMaxModifiers = 256;
[[nodiscard]] constexpr u32 vector_modifier_param(u32 modifier, u32 param) noexcept {
    return kVecModParamBase + modifier * kVecModParams + param;
}
[[nodiscard]] constexpr bool is_vector_modifier_param(u32 id) noexcept {
    return id >= kVecModParamBase && id < kVecModParamBase + kVecMaxModifiers * kVecModParams;
}

} // namespace aurea
