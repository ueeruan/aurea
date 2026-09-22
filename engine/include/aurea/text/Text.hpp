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

private:
    Font() = default;
    std::unique_ptr<Impl> impl_;
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

/// Chave de cache: muda quando qualquer coisa que altera os pixels muda.
[[nodiscard]] u64 raster_key(const TextData& t, f32 scale) noexcept;

} // namespace text
} // namespace aurea
