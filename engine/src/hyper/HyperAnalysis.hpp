// =============================================================================
//  Aurea / hyper / HyperAnalysis.hpp (privado do motor)
//
//  Análises de CPU do Hyper:
//    * entrelaçado — a métrica de "pente" (o mesmo cálculo do shader
//      hyper/deint_score.frag): numa imagem entrelaçada com movimento, a linha
//      difere mais das vizinhas do OUTRO campo (y±1) que das do mesmo campo
//      (y±2). Razão > 1,6 com energia mínima = entrelaçado.
//    * estabilização — ~200 pontos em ¼ da resolução rastreados quadro a
//      quadro (tracking::track_step, NCC subpixel), semelhança robusta por
//      quadro (translação, giro, escala), trajetória suavizada (gaussiana) e o
//      zoom mínimo que esconde as bordas.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/tracking/PointTracker.hpp"

#include <atomic>
#include <vector>

namespace aurea::hyper {

// --- Entrelaçado ---------------------------------------------------------------
struct CombScore {
    f64 inter = 0.0;     ///< média |Y − (Y↑+Y↓)/2| (campo oposto), em códigos 0..255
    f64 intra = 0.0;     ///< média |Y − (Y↑↑+Y↓↓)/2| (mesmo campo)
    [[nodiscard]] f64 ratio() const noexcept { return inter / (intra + 0.25); }
    [[nodiscard]] bool interlaced() const noexcept { return inter >= 2.0 && ratio() >= kRatio; }
    static constexpr f64 kRatio = 1.6;
};
/// Luma 8 bits `width`×`height` (linhas com `stride`).
[[nodiscard]] CombScore comb_score(const u8* luma, u32 width, u32 height, u32 stride) noexcept;

// --- Estabilização -------------------------------------------------------------
/// Movimento do conteúdo de um quadro para o seguinte (px da imagem analisada):
/// p' = s·R(θ)·(p − c) + c + (dx, dy), c = centro.
struct Motion2D {
    f64 dx = 0.0, dy = 0.0, angle = 0.0, scale = 1.0;
    u32 inliers = 0;
    bool valid = false;
};
/// ~`points` pontos (grade com o detalhe mais forte por célula) de `prev`
/// rastreados em `cur`; ajuste robusto (mínimos quadrados com rejeição).
[[nodiscard]] Motion2D estimate_motion(const tracking::Gray& prev, const tracking::Gray& cur, u32 points = 200);

/// Correção de um quadro, em px da FONTE (largura × altura da fonte), graus e
/// fator: o conteúdo do quadro k é deslocado/girado/escalado por isto.
struct StabCorrection {
    f32 dx = 0.0f, dy = 0.0f, degrees = 0.0f, scale = 1.0f;
};
struct StabPlan {
    std::vector<StabCorrection> frames;   ///< um por quadro da fonte
    f32 zoom = 1.0f;                      ///< recorte: ampliação que esconde as bordas (1 = sem recorte)
    u32 width = 0, height = 0;            ///< da fonte
};
/// Trajetória suavizada. `steps[k]` = movimento do quadro k−1 para k (o 0 é
/// ignorado). `strength` 0..1 (σ da gaussiana de 3 a 45 quadros). `crop`
/// calcula o zoom (teto 1,3; acima, a correção é reduzida para caber).
/// `analysisScale` = px da fonte por px analisado.
[[nodiscard]] StabPlan smooth_trajectory(const std::vector<Motion2D>& steps, f32 strength, u32 width, u32 height,
                                         f64 analysisScale, bool crop);

} // namespace aurea::hyper
