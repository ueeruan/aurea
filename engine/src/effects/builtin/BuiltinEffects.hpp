// =============================================================================
//  Peças internas dos efeitos embutidos. Não é cabeçalho público: o resto do
//  motor fala com efeito só pela API de Effect.hpp.
// =============================================================================
#pragma once

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/MotionTile.hpp"

namespace aurea::builtin {

void register_color_effects(EffectRegistry& r);
void register_blur_effects(EffectRegistry& r);
void register_glow_effect(EffectRegistry& r);
void register_transform_effect(EffectRegistry& r);
void register_motion_tile_effect(EffectRegistry& r);
void register_keying_effects(EffectRegistry& r);

/// Pedido de gaussiano, em pixels de LAYER. O construtor converte para texels
/// pela densidade da entrada e reduz a imagem enquanto o sigma passar de 8
/// texels — o custo por pixel fica limitado, qualquer que seja o raio.
struct BlurRequest {
    f32  sigmaX = 0.0f;
    f32  sigmaY = 0.0f;
    bool repeatEdges = false;
    Rect outRegion{};
    const char* label = "blur";
};

[[nodiscard]] Status build_gaussian(EffectBuildContext& ctx, const LayerImage& input,
                                    const BlurRequest& request, LayerImage& out);

/// Região de saída de um efeito que espalha luz: a entrada expandida pela
/// extensão do kernel, recortada ao que o quadro mostra (mais a margem que os
/// efeitos seguintes leem). Sem o recorte, um blur numa layer em 1000% de
/// escala alocaria uma textura do tamanho da layer inteira ampliada.
[[nodiscard]] Rect spread_region(const Rect& input, f32 extendX, f32 extendY,
                                 const LayerPlacement* placement, f32 margin) noexcept;

} // namespace aurea::builtin
