#pragma once
// =============================================================================
//  Aurea / timeline / ShapeGeometry.hpp
//
//  Formas paramétricas 2D (camada de forma SDF): os tipos, os parâmetros de
//  cada forma e a MESMA conta de distância de shaders/shape/shape.frag, aqui na
//  CPU (partículas que nascem da forma, testes).
//
//  Regra de compatibilidade: os números de tipo e de parâmetro vão gravados no
//  projeto e nas trilhas (TrackProperty::ShapeParam). Nunca renumerar — forma
//  ou parâmetro novo entra NO FIM.
//
//  Parâmetros "do tipo": `depth`, `thickness` e `head` nascem negativos na
//  ShapeData e valem o padrão da forma (`default_param`) até alguém mexer.
//  Assim um projeto antigo (sem o campo) abre exatamente como antes e uma
//  forma trocada pelo ‹ › pega a proporção natural da forma nova.
// =============================================================================

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

namespace aurea {
struct ShapeData;
}

namespace aurea::shape {

/// ShapeData::shapeType. 11 é a camada vetorial (kShapeVector) e 2 o caminho
/// livre — nenhum dos dois é SDF.
enum Type : u32 {
    kRect = 0,
    kEllipse = 1,
    kPath = 2,
    kPolygon = 3,
    kStar = 4,
    kCross = 5,
    kRing = 6,
    kPie = 7,
    kFlower = 8,
    kArrow = 9,
    kRightTriangle = 10,
    kTrapezoid = 12,
    kParallelogram = 13,
    kGear = 14,
    kDoubleArrow = 15,
    // Formas novas (fim da lista).
    kLine = 16,
    kDiamond = 17,
    kHeart = 18,
    kSeal = 19,
    kArc = 20,
    kBubble = 21,
    kBolt = 22,
    kWave = 23,
    kBlob = 24,
};
/// Maior tipo SDF válido (o comando de trocar forma limita a ele).
inline constexpr u32 kLastSdfType = kBlob;

/// Índice do parâmetro: o mesmo de CommandType::ShapeSetParam e de
/// TrackProperty::ShapeParam.
enum Param : u32 {
    kParamType = 0,       ///< não animável (troca a forma)
    kParamCorner = 1,     ///< raio do canto (px): retângulo, linha, balão
    kParamCount = 2,      ///< lados / pontas / pétalas / dentes / saliências / ondas / lóbulos
    kParamInner = 3,      ///< raio interno, espessura da cruz/anel/seta dupla, topo, inclinação, cubo
    kParamStroke = 4,     ///< largura do contorno (px)
    kParamWidth = 5,      ///< largura da caixa (px)
    kParamHeight = 6,     ///< altura da caixa (px)
    kParamDepth = 7,      ///< profundidade: entalhe do coração, saliência do selo, dente, pétala, variação do blob
    kParamTip = 8,        ///< cintura do losango, posição da ponta do balão, inclinação do raio
    kParamThickness = 9,  ///< espessura: linha (da altura), arco (do menor lado), onda (da altura)
    kParamSweep = 10,     ///< abertura em graus: fatia e arco
    kParamHead = 11,      ///< comprimento da ponta da seta (fração da largura)
    kParamShaft = 12,     ///< espessura da haste da seta (fração da altura)
    kParamAmplitude = 13, ///< amplitude da onda (fração da folga)
    kParamSeed = 14,      ///< variante do blob (inteiro)
    kParamTotal = 15,
};

/// Faixa válida de cada parâmetro (o 0 devolve o valor como está).
[[nodiscard]] f32 clamp_param(u32 param, f32 value) noexcept;
/// Padrão do parâmetro naquela forma (o que um campo "do tipo" vale).
[[nodiscard]] f32 default_param(u32 type, u32 param) noexcept;
/// Campo da ShapeData que guarda o parâmetro (nulo para o tipo).
[[nodiscard]] f32* param_field(ShapeData& sh, u32 param) noexcept;
/// Valor efetivo: o campo, ou o padrão da forma quando o campo é "do tipo";
/// já dentro da faixa.
[[nodiscard]] f32 param_value(const ShapeData& sh, u32 param) noexcept;
/// Fases (radianos) dos três harmônicos do blob a partir da variante. Conta
/// inteira: o mesmo resultado em todo aparelho (o shader recebe as fases).
void blob_phases(f32 seed, f32 out[3]) noexcept;

/// Uniformes do shader além do tamanho/tipo e das cores — a CPU e a GPU usam
/// exatamente estes números.
struct SdfParams {
    Vec4 shape;   ///< canto (px), pontas, raio interno, preenchida (0/1)
    Vec4 extra;   ///< largura do contorno (px), profundidade, ponta, espessura
    Vec4 more;    ///< abertura (graus), ponta da seta, haste, amplitude
    Vec4 blob;    ///< fases do blob (3), 0
};
[[nodiscard]] SdfParams sdf_params(const ShapeData& sh, bool filled, f32 strokeWidth) noexcept;

/// Distância com sinal (px; negativa dentro) do ponto `q` (px, centro da
/// caixa na origem, y para baixo) até a forma `type` de meia caixa `half` —
/// a conta de shape.frag.
[[nodiscard]] f32 signed_distance(u32 type, const SdfParams& p, Vec2 q, Vec2 half) noexcept;
/// Atalho com a ShapeData (contorno pela largura da própria forma).
[[nodiscard]] f32 signed_distance(const ShapeData& sh, Vec2 q, Vec2 half) noexcept;

} // namespace aurea::shape
