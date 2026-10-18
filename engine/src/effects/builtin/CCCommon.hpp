// =============================================================================
//  Peças comuns do pacote CC (CCEffects.cpp, CCStylizeEffects.cpp e
//  CCTransitions.cpp).
//
//  O pacote reproduz o COMPORTAMENTO dos efeitos "CC" dos editores de
//  movimento (Kaleida, Sphere, Cylinder, Twister...) com implementação
//  própria: cada efeito é um ou dois passes de tela cheia, com laço fixo e
//  poucas amostras — feitos para celular fraco.
//
//  Todos os shaders `cc_*.frag` usam o MESMO bloco de uniforms (`Uniforms`,
//  espelho de `shaders/effects/cc_common.glsl`) e trabalham em px da camada:
//  o texel de saída vira ponto da camada e volta para o uv da entrada. Assim a
//  prévia em 1/4 e o export em 4K desenham o mesmo lugar, e uma entrada que um
//  efeito anterior alargou (brilho, desfoque) não desloca nada.
// =============================================================================
#pragma once

#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin::cc {

inline constexpr u16 kAnim = kParamAnimatable;
inline constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
inline constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;
inline constexpr u16 kAnimRel = kParamAnimatable | kParamRelative;
inline constexpr f32 kDeg = 0.01745329252f;

/// Bloco de uniforms dos shaders `cc_*.frag` (std140, só vec4).
struct Uniforms {
    Vec4 dst;    ///< região de SAÍDA (px da camada): x, y, largura, altura
    Vec4 src;    ///< região da ENTRADA (px da camada)
    Vec4 box;    ///< caixa natural da camada (px): x, y, largura, altura
    Vec4 info;   ///< x = px da camada por texel de saída, y = tempo (s), z = texels por px, w = livre
    Vec4 p0, p1, p2, p3, p4, p5;
    Vec4 color;
};
static_assert(sizeof(Uniforms) == 176, "layout std140 de cc_common.glsl");

[[nodiscard]] inline f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
[[nodiscard]] inline f32 clampf(f32 v, f32 lo, f32 hi, f32 fallback) noexcept {
    return std::clamp(finite_or(v, fallback), lo, hi);
}

/// Caixa da CAMADA (px): (0, 0, largura, altura) do posicionamento; sem ele,
/// a região da entrada.
[[nodiscard]] Rect layer_box(const EffectEval& e, const LayerImage& in) noexcept;
/// Ponto relativo (0..1 da camada, como o centro dos outros efeitos) em px.
[[nodiscard]] Vec2 layer_point(const EffectEval& e, const LayerImage& in, u32 param) noexcept;
/// Menor lado da camada (px), nunca abaixo de 1.
[[nodiscard]] f32 min_side(const Rect& box) noexcept;
/// Tempo do quadro em segundos (preview, scrubbing e export iguais).
[[nodiscard]] f32 seconds(const EffectEval& e) noexcept;
/// Cor sRGB de alfa reto (a do seletor) → linear pré-multiplicada.
[[nodiscard]] Vec4 premultiplied_linear(Vec4 c) noexcept;

/// Região de saída de um efeito que desenha FORA da entrada (esfera grande,
/// ladrilhos, raios): `want` recortada ao quadro visível. Sem recorte (cena 3D,
/// sem posicionamento) ela fica na entrada mais max(¼ do lado maior, 512 px),
/// como os raios de luz — uma camada 4K não vira uma textura de 40K.
[[nodiscard]] Rect grown_region(const EffectEval& e, const Rect& input, const Rect& want, f32 margin) noexcept;

/// Um passe de `frag` da entrada para `region`, na densidade da entrada.
/// Preenche dst/src/box/info de `u`; o efeito preenche p0..p5 e color.
/// `extra` (opcional) vai no slot 1 (o mapa suavizado do Vidro e das Bolhas).
[[nodiscard]] Status run_pass(EffectBuildContext& ctx, const EffectEval& e, ShaderId frag, const LayerImage& in,
                              const Rect& region, Uniforms& u, const char* name, LayerImage& out,
                              CommonSampler sampler = CommonSampler::LinearBorder,
                              const LayerImage* extra = nullptr);

/// Luz e sombreamento (Esfera, Cilindro, Bolhas, Vidro): 9 parâmetros seguidos
/// — intensidade, cor, direção, altura da luz, ambiente, difusa, especular,
/// aspereza e metal. O shader lê p4 (direção da luz + intensidade), p5
/// (ambiente, difusa, especular, expoente) e color (cor da luz + metal).
struct LightDefaults {
    f32 intensity = 100.0f;
    f32 direction = -45.0f;
    f32 height = 50.0f;
    f32 ambient = 40.0f;
    f32 diffuse = 70.0f;
    f32 specular = 40.0f;
    f32 roughness = 15.0f;
    f32 metal = 100.0f;
};
inline constexpr u32 kLightParams = 9;
void declare_light(ParameterRegistry& p, const LightDefaults& d);
void light_uniforms(const EffectEval& e, u32 first, Uniforms& u) noexcept;

/// Rotação 3D (graus, ordem X → Y → Z, como o giro das camadas 3D) nas três
/// linhas da INVERSA (vista → objeto), em xyz de p0..p2. As mesmas linhas
/// levam a normal do objeto para a vista (a inversa de uma rotação é a
/// transposta).
void rotation_rows(f32 xDeg, f32 yDeg, f32 zDeg, Uniforms& u) noexcept;

} // namespace aurea::builtin::cc
