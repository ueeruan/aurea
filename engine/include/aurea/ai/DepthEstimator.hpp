#pragma once

#include "aurea/core/Result.hpp"
#include <atomic>
#include <memory>

namespace aurea::ai {

/// Profundidade monocular por IA: MiDaS v2.1 small, na NOSSA conversão para
/// ncnn (não existe release ncnn oficial — origem, receita e hashes em
/// engine/assets/ai/README.md).
///
/// A rede roda sempre em 256×256 (a imagem é reduzida por média de área, sem
/// manter a proporção, como no treino) e devolve DISPARIDADE relativa: maior =
/// mais perto, sempre ≥ 0, sem escala nem deslocamento definidos. Quem usa
/// normaliza (ver `depth_percentiles`).
///
/// Uma instância pertence a UM worker: a rede nunca é chamada em paralelo.
class DepthEstimator {
public:
    enum class Backend { Auto, Cpu, Vulkan };
    static constexpr u32 kSize = 256;
    static constexpr u32 kPixels = kSize * kSize;

    DepthEstimator() noexcept;
    ~DepthEstimator();
    DepthEstimator(const DepthEstimator&) = delete;
    DepthEstimator& operator=(const DepthEstimator&) = delete;

    /// Carrega o modelo embutido. Auto = Vulkan quando há GPU de verdade (não
    /// software), senão CPU; na Apple é sempre CPU (sem ncnn Vulkan).
    [[nodiscard]] Status load(Backend backend = Backend::Auto);
    /// Solta a rede (pesos em fp32 na RAM/GPU: ~66 MB).
    void unload() noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] Backend backend() const noexcept;

    /// `rgb`: RGB8 (channels 3) ou RGBA8 (channels 4, alfa ignorado), de
    /// qualquer tamanho. `out`: kPixels floats, linha a linha (y para baixo).
    [[nodiscard]] Status run(const u8* pixels, u32 width, u32 height, u32 stride, u32 channels,
                             const std::atomic<bool>& cancel, f32* out);

    /// Duração da última inferência (só a rede, sem a redução), em ms.
    [[nodiscard]] f32 last_inference_ms() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Redução por média de área de uma imagem RGB8/RGBA8 para RGB8 kSize×kSize
/// (768 bytes por linha). Ampliar também funciona (vizinho mais próximo).
void depth_input_rgb(const u8* pixels, u32 width, u32 height, u32 stride, u32 channels, u8* out) noexcept;

/// Percentis 2º e 98º da disparidade — os limites que prendem o mapa (o
/// mínimo/máximo global faz o mapa "respirar" num vídeo).
void depth_percentiles(const f32* disparity, u32 count, f32& p2, f32& p98) noexcept;

} // namespace aurea::ai
