// =============================================================================
//  Aurea / tracking / PointTracker.hpp
//
//  Rastreio de ponto por correlação cruzada normalizada (NCC), quadro a
//  quadro: o bloco em volta do ponto no quadro anterior é procurado numa
//  janela do quadro atual; o pico da NCC ganha refino subpixel (parábola em x
//  e em y). NCC não liga para brilho/contraste globais (exposição mudando não
//  arrasta o ponto). Pontuação < limiar = ponto perdido.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <vector>
#include <cmath>

namespace aurea { class DecodedFrame; }

namespace aurea::tracking {

struct Gray {
    u32 width = 0, height = 0;
    std::vector<f32> px;   ///< luma 0..1
    [[nodiscard]] f32 at(i32 x, i32 y) const noexcept {
        x = x < 0 ? 0 : (x >= static_cast<i32>(width) ? static_cast<i32>(width) - 1 : x);
        y = y < 0 ? 0 : (y >= static_cast<i32>(height) ? static_cast<i32>(height) - 1 : y);
        return px[static_cast<usize>(y) * width + static_cast<usize>(x)];
    }
};

/// RGBA8 (sRGB) → luma.
[[nodiscard]] Gray to_gray(const u8* rgba, u32 width, u32 height);

/// Quadro decodificado → luma (Y' 0..1) de análise com `height` linhas (nunca
/// acima da exibição), já na orientação de EXIBIÇÃO (metadado de rotação),
/// pela média da área de cada pixel (sem o serrilhado do vizinho mais
/// próximo; só a luma, sem converter cor). Mesma largura que
/// `frame_to_thumbnail` (round(altura·W/H)). NV12, NV21, I420, P010 e RGBA8;
/// falso se o quadro não tem planos na CPU.
[[nodiscard]] bool frame_to_gray(const DecodedFrame& frame, u32 height, Gray& out);

struct TrackStep {
    Vec2 pos{0, 0};
    f32  score = 0.0f;   ///< NCC do pico (−1..1)
};

/// Procura, em `cur`, o bloco (2·half+1)² de `prev` centrado em `from`, numa
/// janela de ±`radius` px. Devolve a posição subpixel e a pontuação.
/// `centerWeighted`: NCC com peso gaussiano (σ = half/2) — o miolo do bloco
/// manda; o fundo nos cantos (que muda quando o detalhe anda) pesa pouco.
[[nodiscard]] TrackStep track_step(const Gray& prev, const Gray& cur, Vec2 from, i32 half = 8, i32 radius = 24,
                                   Vec2 searchCenter = {NAN, NAN}, bool centerWeighted = false);

/// Meia-largura do bloco para o detalhe tocado em `c`: 3·σ da escala
/// característica (máximo do LoG normalizado, σ²·|∇²(G_σ ∗ I)|), limitada a
/// [minHalf, maxHalf]. Um objeto pequeno sobre fundo com textura (um anel com
/// uma cruz, um rosto, uma logo) cabe inteiro no bloco; um ponto numa textura
/// uniforme fica no mínimo. Com o bloco padrão só no miolo, o fundo que anda
/// diferente dominava a correlação e o rastreio seguia o fundo.
[[nodiscard]] i32 feature_half_at(const Gray& g, Vec2 c, i32 minHalf, i32 maxHalf) noexcept;

/// Rastreio de UM detalhe contra o bloco do quadro onde ele foi escolhido
/// (sem a deriva do passo a passo), com peso por pixel: gaussiano no centro e,
/// aprendido quadro a quadro, menor onde o conteúdo não acompanha o detalhe
/// (o fundo que passa por trás). Busca em dois níveis (metade da resolução na
/// janela inteira, refino ±2 px) e subpixel pela parábola. Quando a aparência
/// muda demais (giro, escala), segue o passo a passo e renova o bloco.
class TemplateTracker {
public:
    void start(const Gray& g, Vec2 seed, i32 half);
    /// Procura em `cur` perto de `predicted` (±radius px). Com pontuação ≥
    /// `accept`, a posição é aceita (pesos e quadro anterior atualizados).
    TrackStep track(const Gray& cur, Vec2 predicted, i32 radius, f32 accept);
    [[nodiscard]] i32 half() const noexcept { return half_; }

private:
    struct Level { i32 half = 0; std::vector<f32> t, w; };
    void build(const Gray& g, Vec2 seed);
    void refresh_weights();
    Level l0_, l1_;
    std::vector<f32> prior_, rho_;
    Gray last_;
    Vec2 lastPos_{0, 0};
    i32 half_ = 0;
};

} // namespace aurea::tracking
