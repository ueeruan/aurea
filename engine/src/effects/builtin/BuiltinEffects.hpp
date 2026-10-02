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
void register_expression_controls(EffectRegistry& r);
void register_echo_effect(EffectRegistry& r);
void register_distort_effects(EffectRegistry& r);
void register_stylize_effects(EffectRegistry& r);
void register_light_effects(EffectRegistry& r);
void register_glitch_effects(EffectRegistry& r);
void register_temporal_effects(EffectRegistry& r);
void register_transition_effects(EffectRegistry& r);
void register_optical_effects(EffectRegistry& r);
void register_sampled_blur_effects(EffectRegistry& r);
void register_pattern_effects(EffectRegistry& r);
void register_corner_pin_effect(EffectRegistry& r);
void register_media_lab_effects(EffectRegistry& r);
void register_studio_light_effects(EffectRegistry& r);
void register_tracery_effect(EffectRegistry& r);
// Pacote de paridade (registrados no fim: a ordem dos antigos não muda).
void register_motion_behavior_effects(EffectRegistry& r);
void register_shape_transition_effects(EffectRegistry& r);
void register_finishing_effects(EffectRegistry& r);
// Geradores (ruído fractal, degradês, espectro de áudio) e recorte
// (contorno da silhueta, refinar recorte). Também no fim.
void register_generate_effects(EffectRegistry& r);
void register_matte_effects(EffectRegistry& r);
// O VHS de Estilizar (o look de fita com OSD). Também no fim.
void register_vhs_look_effect(EffectRegistry& r);
// Mapa de profundidade (IA). O último de todos.
void register_depth_effects(EffectRegistry& r);
void register_rotobrush_effect(EffectRegistry& r);
void register_grid_layout_effects(EffectRegistry& r);
// Pacote de áudio (efeitos de som + Forma de onda, Espectro e Bolas). Depois de todos.
void register_audio_pack_effects(EffectRegistry& r);
// Particular (as partículas do app antigo). Depois do pacote de áudio.
void register_particular_effect(EffectRegistry& r);
// Tremor em trancos (do app antigo). Depois de todos.
void register_twitch_effect(EffectRegistry& r);
/// Shape 3D Layout (PatternEffects.cpp): o Text 3D Layout das formas 3D.
void register_shape3d_layout_effect(EffectRegistry& r);
/// Detectar movimento (MotionDetectEffect.cpp). Sempre no fim.
void register_motion_detect_effect(EffectRegistry& r);
/// Emulador CRT, Tremor dissolvente e Mapa de deslocamento (RetroDisplaceEffects.cpp).
void register_retro_displace_effects(EffectRegistry& r);

/// O bloco de uniforms dos efeitos novos, num layout só.
///
/// Um efeito novo não inventa layout: ele preenche este bloco e o shader lê
/// `p0..p3`, `color` e `texel` nos mesmos lugares. Uniformizar isto é o que
/// permite revisar dez efeitos de uma vez sem reler dez blocos diferentes — e
/// é o que impede o erro clássico de desalinhamento em std140.
struct EffectUniforms {
    Vec4 uvMap{};    ///< uv de entrada = uv da saída * xy + zw
    Vec4 texel{};    ///< x = 1/largura, y = 1/altura, z/w = texels por pixel da layer
    Vec4 p0{};
    Vec4 p1{};
    Vec4 p2{};
    Vec4 p3{};
    Vec4 color{};    ///< tinta/cor do efeito, linear
};
static_assert(sizeof(EffectUniforms) == 112, "layout std140 dos uniforms de efeito");

/// Um passe de tela cheia que NÃO muda a região: lê a entrada, escreve a saída
/// do mesmo tamanho e densidade. É o corpo de quase todo efeito — o que sobra
/// para o efeito é a conta e o preenchimento dos uniforms.
[[nodiscard]] Status single_pass(EffectBuildContext& ctx, ShaderId frag, const LayerImage& input,
                                 const EffectUniforms& u, const char* name, LayerImage& out);

/// Uniforms já com o mapa de uv e a escala de texel preenchidos para um passe
/// que mantém a região (o caso de [single_pass]).
[[nodiscard]] EffectUniforms base_uniforms(const LayerImage& input) noexcept;

/// Um passe de REAMOSTRAGEM AFIM: a saída lê a entrada pela inversa de `m`,
/// com a região da caixa transformada. É o corpo do Transformar e do Shake —
/// os dois são "mover a imagem no plano", só que um pelo usuário e o outro
/// por um sorteio determinístico.
[[nodiscard]] Status affine_pass(EffectBuildContext& ctx, const LayerImage& input, const Mat4& m,
                                 const LayerPlacement* placement, f32 opacity, const char* name,
                                 f32 margin, LayerImage& out);

/// Pedido de gaussiano, em pixels de LAYER. O construtor converte para texels
/// pela densidade da entrada e reduz a imagem enquanto o sigma passar de 8
/// texels — o custo por pixel fica limitado, qualquer que seja o raio.
struct BlurRequest {
    f32  sigmaX = 0.0f;
    f32  sigmaY = 0.0f;
    bool repeatEdges = false;
    Rect outRegion{};
    /// `outRegion` foi RECORTADA ao quadro visível (spread_region cortou): a
    /// redução e o passe horizontal trabalham numa área maior (saída + 3σ),
    /// senão a borda do recorte lê transparente e escurece.
    bool clippedRegion = false;
    const char* label = "blur";
    /// Reduz a imagem por 2 enquanto o sigma passar disto (texels). 8 é o
    /// padrão; halos largos e suaves (lóbulos do Deep Glow) aceitam menos —
    /// o borrão sai igual e cada passe custa uma fração.
    f32 maxSigmaTexels = 8.0f;
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
