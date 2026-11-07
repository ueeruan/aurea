#pragma once

// =============================================================================
//  Aurea / ai / RotoClick.hpp
//
//  Seleção por clique do Roto: a pessoa toca no objeto e a rede devolve o
//  recorte daquele quadro (EfficientTAM-Ti 512, só o encoder e o decoder; ver
//  assets/rotobrush/NOTICE-EfficientTAM.txt). Os quadros seguintes continuam
//  pela propagação do RotoMatte: o rastreamento completo do modelo precisa de
//  ~800 MiB e fica de fora.
//
//  As redes sobem só durante a inferência. O que fica é o resultado do encoder
//  do último quadro (1 MiB): outro clique no mesmo quadro só roda o decoder.
//  Tudo em CPU, em worker.
// =============================================================================

#include "aurea/core/Types.hpp"

#include <mutex>
#include <vector>

namespace aurea::ai {

class RotoClick {
public:
    /// O lado da entrada da rede: o quadro inteiro vira um quadrado, como no treino.
    static constexpr u32 kInput = 512;
    /// Cliques por quadro que entram na rede (os últimos).
    static constexpr u32 kMaxPoints = 16;

    struct Point {
        f32 x = 0.0f, y = 0.0f;   ///< 0..1 do quadro
        bool positive = true;     ///< objeto (true) ou fundo (false)
    };

    /// `rgb` = kInput² × 3. `imageKey` identifica o quadro: repetido, o encoder
    /// não roda de novo. Saída size², 0..1. false = sem objeto, sem memória ou
    /// sem o modelo neste build: quem chama fica com o caminho dos traços.
    [[nodiscard]] bool run(u64 imageKey, const u8* rgb, const Point* points, u32 count, u32 size, std::vector<f32>& out);
    /// Solta o resultado do encoder (pressão de memória). Não espera a rede.
    void clear() noexcept;

private:
    [[nodiscard]] bool encode(const u8* rgb);
    [[nodiscard]] bool decode(const Point* points, u32 count, u32 size, std::vector<f32>& out);

    std::mutex mutex_;
    u64 featuresKey_ = 0;
    std::vector<f32> features_;   ///< 256 × 32 × 32
};

} // namespace aurea::ai
