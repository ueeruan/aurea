// =============================================================================
//  Aurea / text / TextTexture.hpp
//
//  0.5.2 (timeline v69): PREENCHIMENTO COM TEXTURA do texto. Em vez da cor
//  lisa, as letras levam uma textura procedural (ouro, cromo, madeira,
//  mármore, fogo, galáxia...) calculada na GPU por common/texture_lib.glsl —
//  sem imagem no pacote, nítida em qualquer tamanho — ou uma imagem da
//  galeria (AssetId do projeto, como a textura do material 3D).
//
//  O desenho é do text/glyph.frag: cada glifo marcado lê os parâmetros de um
//  registro no mesmo buffer dos glifos (Renderer.cpp). A textura é medida no
//  px da camada antes do transform de cada letra, então o Text Animator move
//  a letra e ela leva o pedaço de textura dela; a prévia e o export calculam
//  o mesmo valor no mesmo ponto.
//
//  Os números de `kind` são contrato de arquivo e batem com os ids da
//  biblioteca GLSL (1..18). Só acrescentar no fim.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {

/// Ids da biblioteca de texturas procedurais (common/texture_lib.glsl). O
/// texto usa 1..18; o efeito Textura usa também os de sobreposição (19..26).
enum class ProceduralTexture : u8 {
    None = 0,
    Gold = 1, Silver, BrushedMetal, Wood, Marble, Stone, Concrete, Glitter, Holographic, Neon,
    Fire, Water, Galaxy, Paper, Denim, Leather, Rust, Ice,
    CrumpledPaper, FilmGrain, DustScratches, Canvas, Grunge, Halftone, Noise, FrostedGlass,
};
inline constexpr u8 kProceduralTextureLast = 26;

/// Texturas do texto: 1..18 procedurais (a ordem da grade no painel) e a
/// imagem da galeria.
inline constexpr u8 kTextTextureProcedural = 18;
inline constexpr u8 kTextTextureImage = 19;
/// Quantos valores `Engine::set_text_texture` / `query_text_texture` trocam.
inline constexpr u32 kTextTextureValues = 17;

struct TextTexture {
    u8   kind = 0;              ///< 0 cor lisa (desligado); 1..18 procedural; 19 imagem da galeria
    u8   mapping = 0;           ///< 0 o bloco todo, 1 cada letra
    bool stroke = false;        ///< o contorno também leva a textura
    f32  scale = 1.0f;          ///< 1 = a textura na medida do tamanho da fonte
    f32  rotation = 0.0f;       ///< graus
    Vec2 offset{0.0f, 0.0f};    ///< deslocamento, em unidades de textura (1 = um tamanho de fonte)
    f32  contrast = 0.0f;       ///< −1..1
    f32  brightness = 0.0f;     ///< −1..1
    Vec4 tint{1.0f, 1.0f, 1.0f, 0.0f};   ///< cor de tela (sRGB); alfa = quanto da tinta entra
    f32  speed = 1.0f;          ///< velocidade da animação (0 = parada; fogo, água, glitter...)
    f32  relief = 0.5f;         ///< relevo (bisel + luz) 0..1
    f32  lightAngle = 135.0f;   ///< direção da luz do relevo (graus, 0 = da direita, 90 = de cima)
    u32  seed = 0;              ///< variação da textura
    u64  image = 0;             ///< AssetId empacotado (kind == kTextTextureImage)

    /// Liga de verdade? Imagem sem asset desenha a cor lisa.
    [[nodiscard]] bool active() const noexcept {
        return (kind >= 1 && kind <= kTextTextureProcedural) || (kind == kTextTextureImage && image != 0);
    }
    [[nodiscard]] bool operator==(const TextTexture&) const noexcept = default;
};

/// Valores presos às faixas válidas (arquivo mexido à mão, NaN de expressão).
[[nodiscard]] inline TextTexture sanitized(TextTexture t) noexcept {
    const auto fin = [](f32 v, f32 lo, f32 hi, f32 fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
    if (t.kind > kTextTextureImage) t.kind = 0;
    t.mapping = t.mapping == 1 ? u8{1} : u8{0};
    t.scale = fin(t.scale, 0.05f, 20.0f, 1.0f);
    t.rotation = fin(t.rotation, -36000.0f, 36000.0f, 0.0f);
    t.offset = Vec2{fin(t.offset.x, -1000.0f, 1000.0f, 0.0f), fin(t.offset.y, -1000.0f, 1000.0f, 0.0f)};
    t.contrast = fin(t.contrast, -1.0f, 1.0f, 0.0f);
    t.brightness = fin(t.brightness, -1.0f, 1.0f, 0.0f);
    t.tint = Vec4{fin(t.tint.x, 0.0f, 1.0f, 1.0f), fin(t.tint.y, 0.0f, 1.0f, 1.0f), fin(t.tint.z, 0.0f, 1.0f, 1.0f),
                  fin(t.tint.w, 0.0f, 1.0f, 0.0f)};
    t.speed = fin(t.speed, -10.0f, 10.0f, 1.0f);
    t.relief = fin(t.relief, 0.0f, 1.0f, 0.5f);
    t.lightAngle = fin(t.lightAngle, -36000.0f, 36000.0f, 135.0f);
    t.seed = std::min<u32>(t.seed, 9999u);
    if (t.kind != kTextTextureImage) t.image = 0;
    return t;
}

/// Fase da animação no instante `seconds` (tempo local da camada): presa a
/// uma hora para o ruído não perder precisão em float no shader.
[[nodiscard]] inline f32 text_texture_phase(const TextTexture& t, f64 seconds) noexcept {
    if (!std::isfinite(seconds) || t.speed == 0.0f) return 0.0f;
    const f64 phase = std::fmod(seconds * static_cast<f64>(t.speed), 3600.0);
    return static_cast<f32>(phase);
}

} // namespace aurea
