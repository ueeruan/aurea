#pragma once

// =============================================================================
//  Aurea / ai / RotoMatte.hpp
//
//  O fluxo do Roto Brush (referência de comportamento: Roto Brush e Refine
//  Edge do After Effects) sobre a probabilidade do Rotobrush IA:
//
//   - Traços: a pessoa pinta OBJETO (verde) e FUNDO (vermelho) num quadro da
//     fonte. Os traços moram na instância do efeito (um parâmetro de curva
//     oculto, só acrescentado: projetos antigos não mudam) e são restrições
//     duras: o pintado como fundo SAI mesmo que a rede diga objeto.
//   - Segmentação de um quadro-base: distância geodésica (custo = espaço +
//     diferença de cor ao quadrado) a partir das sementes de objeto e de
//     fundo — traços custam 0, a rede confiante custa mais. Quem chega mais
//     barato vence: a borda para nas arestas da imagem.
//   - Propagação: o recorte do quadro anterior é levado pelo fluxo óptico
//     (blocos, pirâmide 80/160/320) e erodido vira semente; a faixa da borda
//     é decidida de novo pelas arestas do quadro novo e pela rede.
//   - Quadros com traço viram base nova; os seguintes dependem dela.
//
//  Tudo em CPU, na grade da rede (320×320 da fonte inteira), em worker.
// =============================================================================

#include "aurea/effects/Parameter.hpp"

#include <memory>
#include <vector>

namespace aurea::ai {

/// Um traço, nas coordenadas da CAMADA (0..1), num quadro da FONTE.
struct RotoStroke {
    i64 frame = 0;
    bool background = false;
    f32 radius = 0.03f;          ///< fração da MENOR dimensão da camada
    std::vector<Vec2> points;    ///< 0..1 da camada
};
using RotoStrokes = std::vector<RotoStroke>;

/// O parâmetro de curva oculto do Rotobrush que guarda os traços.
inline constexpr u32 kRotoStrokesParam = 11;
inline constexpr u32 kRotoMaxPoints = 4000;     ///< teto do canal de curva (4096 na leitura)
inline constexpr u32 kRotoMaxStrokePoints = 256;

/// Traços ⇄ curva (canal 0: quadro e raio com sinal; 1: contagem; 2: pontos;
/// 3: marca de formato). Curva sem a marca = sem traços.
[[nodiscard]] bool roto_decode(const CurveData& curve, RotoStrokes& out);
void roto_encode(const RotoStrokes& strokes, CurveData& curve);
/// Os traços da instância (nada quando o parâmetro não existe/está vazio).
[[nodiscard]] bool roto_instance_strokes(const EffectInstance& instance, RotoStrokes& out);

/// Quadros-base (com traço), em ordem.
[[nodiscard]] std::vector<i64> roto_bases(const RotoStrokes& strokes);
/// A dependência do recorte do quadro `frame`: muda quando muda qualquer
/// traço que o decide (as bases ≤ frame, ou a primeira base para trás).
/// 0 = sem traços.
[[nodiscard]] u64 roto_dependency(const RotoStrokes& strokes, i64 frame);

/// Rótulos na grade size²: 0 nada, 1 objeto, 2 fundo (o último traço manda).
void roto_rasterize(const RotoStrokes& strokes, i64 frame, u32 size, f32 layerW, f32 layerH,
                    std::vector<u8>& labels);

/// Segmentação de um quadro. `prob`/`rgb` da rede (size², size²×3); `labels`
/// opcional (traços deste quadro); `prior` opcional (recorte levado do quadro
/// vizinho). Saída 0..1 por pixel.
void roto_segment(const f32* prob, const u8* rgb, const u8* labels, const f32* prior, u32 size,
                  std::vector<f32>& out);

/// Fluxo óptico de trás: para cada pixel do quadro `cur`, o deslocamento até
/// onde ele estava em `prev` (pixels da grade).
void roto_flow(const u8* prevRgb, const u8* curRgb, u32 size, std::vector<Vec2>& flow);
/// `prev` levado ao quadro novo pelo fluxo (bilinear).
void roto_warp(const std::vector<f32>& prev, const std::vector<Vec2>& flow, u32 size, std::vector<f32>& out);
/// Um passo da propagação: recorte anterior + quadros → recorte novo.
void roto_propagate(const std::vector<f32>& prevMatte, const u8* prevRgb, const u8* curRgb, const f32* prob,
                    const u8* labels, u32 size, std::vector<f32>& out);
/// Reduzir trepidação: média ponderada com os vizinhos onde a cor não mudou.
void roto_reduce_chatter(const std::vector<f32>& cur, const std::vector<f32>* prev, const std::vector<f32>* next,
                         f32 amount, std::vector<f32>& out);

/// Recorte compacto (RLE de 8 bits): o cache cabe centenas de quadros.
void roto_pack(const std::vector<f32>& matte, std::vector<u8>& out);
[[nodiscard]] bool roto_unpack(const std::vector<u8>& packed, usize count, std::vector<f32>& out);

} // namespace aurea::ai
