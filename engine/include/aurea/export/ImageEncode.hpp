// =============================================================================
//  Aurea / export / ImageEncode.hpp
//
//  Export como IMAGEM: o quadro atual em PNG, a sequência de PNGs num .zip e o
//  GIF animado. Tudo escrito aqui, no motor (as duas interfaces só escolhem o
//  formato e publicam o arquivo pronto):
//
//    PNG   RGBA8 (alfa reto) ou RGB8; filtro por linha (o de menor soma
//          absoluta) + deflate próprio (LZ77 com cadeia de hash e Huffman
//          dinâmico) dentro de um zlib. Qualquer leitor de PNG abre.
//    GIF   GIF89a com laço infinito (NETSCAPE2.0), paleta local por quadro
//          (median cut no histograma de 15 bits), dithering Floyd–Steinberg
//          ou ordenado (Bayer 4×4), transparência de 1 bit quando o fundo da
//          composição é transparente e LZW de até 12 bits. Quadros idênticos
//          em sequência viram um quadro só com o atraso somado.
//    ZIP   "stored" (método 0): os PNGs já estão comprimidos; CRC-32 e
//          tamanhos no cabeçalho local, diretório central no fim. Sem ZIP64.
//
//  As estimativas de tamanho (`estimate_image_export_bytes`) são as MESMAS nas
//  duas telas: uma regra só, como o BitratePolicy do vídeo.
// =============================================================================
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

namespace aurea {

/// Formatos de imagem do export (o valor atravessa as bridges).
enum class ImageExportFormat : u32 {
    Png = 0,            ///< quadro do playhead, um PNG
    PngSequence = 1,    ///< um PNG por quadro do trecho, num .zip
    Gif = 2,            ///< GIF animado do trecho
};

enum class GifDither : u32 { None = 0, FloydSteinberg = 1, Ordered = 2 };

/// Pedido de export como imagem (a UI preenche; o motor decide o resto).
struct ImageExportSettings {
    ImageExportFormat format = ImageExportFormat::Png;
    /// Lado menor pedido (PNG e sequência). 0 = o da composição (resolução cheia).
    u32 shortSide = 0;
    /// GIF: largura máxima (480/720…). 0 = 480. Nunca amplia a composição.
    u32 maxWidth = 0;
    /// Sequência/GIF. 0 = o da composição (sequência) ou 15 (GIF).
    f64 fps = 0.0;
    /// Trecho: só até o fim do conteúdo (como o vídeo) ou a composição inteira.
    bool trimToContent = true;
    GifDither dither = GifDither::FloydSteinberg;
};

// --- Limites do GIF (as telas usam os mesmos) ------------------------------
inline constexpr u32 kGifDefaultWidth = 480;
inline constexpr u32 kGifMaxWidth = 1080;
inline constexpr f64 kGifDefaultFps = 15.0;
inline constexpr f64 kGifMinFps = 5.0;
inline constexpr f64 kGifMaxFps = 50.0;   ///< atraso mínimo de 2 cs
/// Quadros no máximo num GIF (≈ 60 s a 30 fps): passa disso, o motor recusa
/// com uma mensagem clara em vez de gerar um arquivo que nenhum app aceita.
inline constexpr u32 kGifMaxFrames = 1800;
/// Quadros no máximo numa sequência PNG.
inline constexpr u32 kSequenceMaxFrames = 18000;

/// Dimensões de saída de um export como imagem para a composição `compW`×`compH`.
struct ImageExportPlan {
    u32 width = 0, height = 0;
    f64 fps = 0.0;
    u32 frames = 0;
    bool alpha = false;
};
/// O plano (dimensões, fps e quadros) — a regra única das telas e do motor.
/// `durationFrames` = duração do trecho em quadros da composição.
[[nodiscard]] ImageExportPlan plan_image_export(const ImageExportSettings& s, u32 compW, u32 compH, f64 compFps,
                                                i64 durationFrames, bool transparentBackground) noexcept;
/// Tamanho estimado (bytes) do arquivo final.
[[nodiscard]] u64 estimate_image_export_bytes(ImageExportFormat format, u32 width, u32 height, u32 frames,
                                              bool alpha) noexcept;

// --- Conversão --------------------------------------------------------------
/// RGBA16F linear pré-multiplicado (o alvo do renderer) → RGBA8 sRGB de alfa
/// reto. `half` tem `width*height*4` meias-precisões.
void linear_half_to_srgb8(const u16* half, usize pixels, u8* outRgba) noexcept;

// --- PNG ----------------------------------------------------------------------
/// Codifica RGBA8 (`withAlpha`) ou descarta o alfa e grava RGB8.
[[nodiscard]] Status encode_png(const u8* rgba, u32 width, u32 height, bool withAlpha, std::vector<u8>& out) noexcept;
/// zlib (RFC 1950) com o deflate do motor. Exposto para os testes.
void zlib_compress(const u8* data, usize size, std::vector<u8>& out) noexcept;
[[nodiscard]] u32 crc32_update(u32 crc, const u8* data, usize size) noexcept;   ///< crc inicial 0

// --- GIF ----------------------------------------------------------------------
class GifWriter {
public:
    GifWriter() = default;
    ~GifWriter();
    GifWriter(const GifWriter&) = delete;
    GifWriter& operator=(const GifWriter&) = delete;

    /// Abre o arquivo e grava o cabeçalho e o laço infinito.
    [[nodiscard]] Status open(const char* path, u32 width, u32 height, bool transparency, GifDither dither) noexcept;
    /// Mesmo, para a memória (testes): o resultado fica em `bytes()`.
    [[nodiscard]] Status open_memory(u32 width, u32 height, bool transparency, GifDither dither) noexcept;
    /// Um quadro RGBA8 (alfa reto) de `width*height`, exibido por `delayCs` centésimos.
    [[nodiscard]] Status add_frame(const u8* rgba, u32 delayCs) noexcept;
    /// Grava o último quadro e o terminador. Fecha o arquivo.
    [[nodiscard]] Status finish() noexcept;
    /// Cancelamento: fecha e apaga o parcial.
    void abort() noexcept;

    [[nodiscard]] const std::vector<u8>& bytes() const noexcept { return mem_; }
    [[nodiscard]] u32 frames_written() const noexcept { return written_; }
    [[nodiscard]] u64 bytes_written() const noexcept { return total_; }

private:
    Status write(const void* p, usize n) noexcept;
    Status encode_pending() noexcept;

    std::FILE* f_ = nullptr;
    std::string path_;
    bool toMemory_ = false;
    std::vector<u8> mem_;
    u32 w_ = 0, h_ = 0;
    bool transparency_ = false;
    GifDither dither_ = GifDither::FloydSteinberg;
    std::vector<u8> pending_;     ///< último quadro recebido, ainda não gravado
    u32 pendingDelay_ = 0;
    bool hasPending_ = false;
    u32 written_ = 0;
    u64 total_ = 0;
    std::vector<u8> scratch_;
};

/// Atraso (centésimos) do quadro `i` a `fps`, sem deriva acumulada (30 fps → 3,3,4…).
[[nodiscard]] u32 gif_frame_delay_cs(u32 i, f64 fps) noexcept;

// --- ZIP ------------------------------------------------------------------------
/// ZIP "stored" gravado em fluxo (um PNG por vez, sem guardar a sequência).
class StoredZipWriter {
public:
    StoredZipWriter() = default;
    ~StoredZipWriter();
    StoredZipWriter(const StoredZipWriter&) = delete;
    StoredZipWriter& operator=(const StoredZipWriter&) = delete;

    [[nodiscard]] Status open(const char* path) noexcept;
    [[nodiscard]] Status add(const std::string& name, const u8* data, usize size) noexcept;
    [[nodiscard]] Status finish() noexcept;
    void abort() noexcept;
    [[nodiscard]] u64 bytes_written() const noexcept { return offset_; }

private:
    struct Entry { std::string name; u32 crc = 0; u32 size = 0; u32 offset = 0; };
    std::FILE* f_ = nullptr;
    std::string path_;
    std::vector<Entry> entries_;
    u64 offset_ = 0;
};

} // namespace aurea
