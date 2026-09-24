// =============================================================================
//  Aurea / text / Text.hpp
//
//  Texto da camada de texto: fonte (stb_truetype), medida e rasterização.
//
//  Preenchimento pela cobertura EXATA do stb (antisserrilhado analítico);
//  contorno pelo campo de distância do glifo (espessura uniforme, cantos
//  redondos). Várias linhas, alinhamento, entrelinha e espaçamento entre
//  letras. A imagem sai em RGBA8 sRGB com alfa reto — o mesmo formato de uma
//  imagem importada, então o resto do pipeline (efeitos, composição, export)
//  não sabe que é texto.
//
//  A rasterização é feita numa escala (px de textura por px da layer)
//  escolhida pelo renderer conforme o zoom na tela: o texto continua nítido
//  ampliado, sem recalcular a cada quadro (a escala anda em potências de 2).
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aurea {

struct TextData;

namespace text {

/// Fonte carregada (arquivo TTF/OTF inteiro na memória).
class Font {
public:
    [[nodiscard]] static std::shared_ptr<const Font> load(const std::string& path);
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    struct Impl;
    [[nodiscard]] const Impl& impl() const noexcept { return *impl_; }
    /// Bytes do arquivo em RAM (a fonte fica inteira na memória enquanto viva).
    [[nodiscard]] usize memory_bytes() const noexcept;
    /// Identidade ESTÁVEL dos BYTES da fonte (FNV-1a, calculada uma vez na
    /// carga). É o que se usa como chave de cache de geometria: o endereço do
    /// objeto não serve — o alocador reaproveita o endereço de uma fonte
    /// liberada para outra fonte, e o cache devolveria a malha da fonte errada.
    [[nodiscard]] u64 content_id() const noexcept { return contentId_; }

private:
    Font() = default;
    std::unique_ptr<Impl> impl_;
    u64 contentId_ = 0;
};

/// Fonte padrão do aparelho (Roboto no Android, Segoe/Arial no Windows,
/// Helvetica no iOS). Carregada uma vez; nula se nenhum arquivo existir.
[[nodiscard]] std::shared_ptr<const Font> default_font();
/// Caminho explícito (EngineConfig); vazio = procurar os candidatos.
void set_default_font_path(const std::string& path);

/// Caixa do texto em px da layer (sem contorno).
struct TextExtent {
    f32 width = 0.0f;
    f32 height = 0.0f;
};
[[nodiscard]] TextExtent measure(const Font& font, const TextData& t);

struct TextRaster {
    std::vector<u8> rgba;    ///< sRGB, alfa reto
    u32 width = 0, height = 0;
    f32 layerWidth = 0.0f;   ///< tamanho em px da layer (contorno incluído)
    f32 layerHeight = 0.0f;
    f32 padding = 0.0f;      ///< margem do contorno, em px da layer
};

/// Rasteriza em `scale` px de textura por px da layer.
[[nodiscard]] bool rasterize(const Font& font, const TextData& t, f32 scale, TextRaster& out);

/// Contornos vetoriais do texto (curvas achatadas em segmentos), em px da
/// layer com Y para BAIXO e a origem no canto de cima da caixa — o mesmo
/// layout (linhas, alinhamento, entrelinha, espaçamento) da rasterização.
/// Cada contorno é fechado implicitamente (o último ponto liga no primeiro).
/// É a base do texto 3D extrudado.
[[nodiscard]] bool outline(const Font& font, const TextData& t, std::vector<std::vector<Vec2>>& contours, i32 glyphIndex = -1);

/// Um glifo depois do shaping (HarfBuzz): índice na fonte, caractere de
/// origem (cluster, na linha), posição na linha de base (px da layer) e se
/// veio de uma fonte de reserva (fallback).
struct ShapedGlyph {
    u32 glyph = 0;
    u32 cluster = 0;
    u32 line = 0;
    f32 x = 0.0f, y = 0.0f;
    bool fallback = false;
};
/// Glifos do texto em ordem visual (kerning, ligaduras, árabe contextual, RTL).
u32 shaped_glyphs(const Font& font, const TextData& t, std::vector<ShapedGlyph>& out);

// -----------------------------------------------------------------------------
// Texto na GPU: atlas de glifos SDF (cada glifo rasterizado UMA vez, a 64 px,
// com 16 px de distância em volta) e o texto como quads — nada de rasterizar o
// bloco de novo a cada quadro, e cada glifo pode ter o seu transform/cor
// (Text Animator).
// -----------------------------------------------------------------------------
inline constexpr u32 kGlyphAtlasSize = 2048;
inline constexpr f32 kGlyphBasePx = 64.0f;
inline constexpr f32 kGlyphSpread = 16.0f;       ///< px da base, em volta do glifo
inline constexpr f32 kGlyphDistScale = 8.0f;     ///< valor (0..255) por px da base

struct GlyphQuad {
    f32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;   ///< px da layer (margem incluída)
    f32 u0 = 0, v0 = 0, u1 = 0, v1 = 0;   ///< no atlas
    f32 k = 1;                            ///< px da layer por px da base (distância do SDF)
    f32 penX = 0, baseline = 0;           ///< origem do glifo (pivô dos transforms por letra)
    f32 advance = 0;                      ///< largura do glifo na linha (px da layer)
    u32 charIndex = 0;                    ///< caractere de origem, em ordem lógica no texto inteiro
    u32 wordIndex = 0;
    u32 lineIndex = 0;
    Vec4 color{1, 1, 1, 1};               ///< sRGB: a do texto ou a do trecho (rich text)
};

struct TextLayout {
    std::vector<GlyphQuad> quads;         ///< em ordem visual
    f32 width = 0, height = 0;            ///< caixa da layer com a margem
    f32 pad = 0;
    f32 contentWidth = 0, contentHeight = 0;   ///< sem a margem
    u32 chars = 0, words = 0, lines = 0;
};

/// Monta os quads do texto (shaping + atlas). `pad` = margem em px da layer
/// (contorno). Falso se não há glifo visível.
bool layout_quads(const Font& font, const TextData& t, f32 pad, TextLayout& out);

/// Atlas (R8, kGlyphAtlasSize²): pixels, geração (muda quando é refeito) e se
/// mudou desde a última leitura (o renderer sobe a textura).
const u8* glyph_atlas(u64& generation, bool& dirty);
void glyph_atlas_clean() noexcept;
/// SDFs gerados e atlas refeitos (acumulados no processo) — 8E: em regime de
/// playback nenhum dos dois cresce.
void glyph_atlas_stats(u32& rasterized, u32& resets) noexcept;
/// Fontes de reserva carregadas (sob demanda, uma a uma) e os bytes delas.
void fallback_font_stats(u32& loaded, u64& bytes) noexcept;

/// Chave de cache: muda quando qualquer coisa que altera os pixels muda.
[[nodiscard]] u64 raster_key(const TextData& t, f32 scale) noexcept;

} // namespace text
} // namespace aurea
