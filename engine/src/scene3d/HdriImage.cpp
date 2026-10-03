// =============================================================================
//  Aurea / scene3d / HdriImage.cpp
//
//  Leitor de panoramas do ambiente 3D (Environment.hpp: decode_hdri_detailed).
//
//  - Radiance (.hdr/.hdri/.pic): leitor próprio, linha a linha. Aceita os dois
//    cabeçalhos ("#?RADIANCE", "#?RGBE" e qualquer "#?…"), CRLF, FORMAT RGBE
//    ou XYZE, RLE "novo" (por componente), RLE antigo (1,1,1,n) e linhas
//    planas, e as oito orientações (±Y ±X e as transpostas ±X ±Y).
//  - OpenEXR (.exr): tinyexr (third_party/tinyexr), canais R/G/B (ou Y),
//    linhas ou tiles, meia precisão ou float.
//  - JPG/PNG: stb_image (a implementação está em GltfImporter.cpp); curva
//    sRGB exata → linear.
//  - .zip: o primeiro .hdr/.exr (ou imagem) de dentro, "stored" ou deflate.
//
//  Panorama maior que kHdriMaxPixels é REDUZIDO por média de caixa (fator
//  inteiro) — o Radiance já entra reduzido, sem a imagem cheia na memória.
//  Valores não finitos ou negativos viram 0 (o half do cubo estouraria).
// =============================================================================
#include "aurea/scene3d/Environment.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#define STBI_NO_STDIO
#include "stb_image.h"
#include "tinyexr.h"

namespace aurea::scene3d {
namespace {

// Tetos de ENTRADA (memória transitória): o arquivo inteiro já está na RAM.
constexpr u64 kMaxSide = 65536;                 ///< lado de qualquer formato
constexpr u64 kLdrMaxPixels = 8192ull * 4096;   ///< jpg/png 8 bits (stb carrega inteiro: ~100 MB)
constexpr u64 kLdr16MaxPixels = 4096ull * 4096; ///< png 16 bits
constexpr u64 kExrMaxBytes = 160ull << 20;      ///< canais decodificados do EXR (tinyexr carrega inteiro)
constexpr usize kZipMaxEntry = 512ull << 20;

f32 clean(f32 v) noexcept { return std::isfinite(v) && v > 0.0f ? std::min(v, 65504.0f) : 0.0f; }

/// Acumula a imagem de entrada (w×h, em qualquer ordem de pixels) numa saída
/// reduzida por um fator inteiro `k` — o menor que cabe em kHdriMaxPixels.
/// As colunas/linhas que sobram da divisão entram no último texel.
class Downscale {
public:
    bool init(u32 w, u32 h) {
        w_ = w; h_ = h; k_ = 1;
        while (static_cast<u64>(w / k_) * (h / k_) > kHdriMaxPixels || w / k_ > 16384) ++k_;
        ow_ = std::max(1u, w / k_);
        oh_ = std::max(1u, h / k_);
        out_ = std::make_shared<HdriPixels>();
        out_->width = ow_; out_->height = oh_;
        out_->rgb.assign(static_cast<usize>(ow_) * oh_ * 3, 0.0f);
        return true;
    }
    void add(u32 x, u32 y, f32 r, f32 g, f32 b) noexcept {
        const u32 ox = std::min(x / k_, ow_ - 1), oy = std::min(y / k_, oh_ - 1);
        f32* o = out_->rgb.data() + (static_cast<usize>(oy) * ow_ + ox) * 3;
        o[0] += clean(r); o[1] += clean(g); o[2] += clean(b);
    }
    std::shared_ptr<HdriPixels> finish() {
        if (k_ > 1) {
            for (u32 oy = 0; oy < oh_; ++oy) {
                const u32 cy = oy == oh_ - 1 ? h_ - oy * k_ : k_;
                for (u32 ox = 0; ox < ow_; ++ox) {
                    const u32 cx = ox == ow_ - 1 ? w_ - ox * k_ : k_;
                    const f32 inv = 1.0f / static_cast<f32>(cx * cy);
                    f32* o = out_->rgb.data() + (static_cast<usize>(oy) * ow_ + ox) * 3;
                    o[0] *= inv; o[1] *= inv; o[2] *= inv;
                }
            }
        }
        return std::move(out_);
    }
    u32 factor() const noexcept { return k_; }

private:
    u32 w_ = 0, h_ = 0, k_ = 1, ow_ = 0, oh_ = 0;
    std::shared_ptr<HdriPixels> out_;
};

HdriDecode fail(HdriStatus s) { return HdriDecode{nullptr, s}; }
HdriDecode done(std::shared_ptr<HdriPixels> p) { return HdriDecode{std::move(p), HdriStatus::Ok}; }

// --- Radiance -----------------------------------------------------------------

struct Reader {
    const u8* p;
    const u8* end;
    bool line(std::string& out) {
        out.clear();
        while (p < end && *p != '\n') {
            if (out.size() > 4096) return false;
            out.push_back(static_cast<char>(*p++));
        }
        if (p >= end) return false;
        ++p;
        if (!out.empty() && out.back() == '\r') out.pop_back();
        return true;
    }
};

/// Uma linha de varredura RGBE (`n` pixels) em `scan` (4 bytes por pixel).
bool read_scanline(Reader& r, u32 n, std::vector<u8>& scan) {
    scan.resize(static_cast<usize>(n) * 4);
    auto flat = [&](usize first) {
        // Plano / RLE antigo: (1,1,1,c) repete o pixel anterior c << shift vezes.
        u32 shift = 0;
        for (usize i = first; i < n;) {
            if (r.end - r.p < 4) return false;
            const u8* q = r.p; r.p += 4;
            if (q[0] == 1 && q[1] == 1 && q[2] == 1) {
                if (i == 0) return false;
                const usize count = static_cast<usize>(q[3]) << shift;
                if (count > n - i) return false;
                for (usize k = 0; k < count; ++k, ++i) std::memcpy(&scan[i * 4], &scan[(i - 1) * 4], 4);
                shift += 8;
                if (shift > 24) return false;
            } else {
                std::memcpy(&scan[i * 4], q, 4);
                ++i; shift = 0;
            }
        }
        return true;
    };
    if (n < 8 || n > 0x7fff) return flat(0);
    if (r.end - r.p < 4) return false;
    const u8* h = r.p;
    if (h[0] != 2 || h[1] != 2 || (h[2] & 0x80)) return flat(0);
    if (((static_cast<u32>(h[2]) << 8) | h[3]) != n) return false;
    r.p += 4;
    // RLE novo: cada componente em sequência; c > 128 = repetição, senão literal.
    for (u32 c = 0; c < 4; ++c) {
        for (u32 i = 0; i < n;) {
            if (r.p >= r.end) return false;
            u32 count = *r.p++;
            if (count > 128) {
                count -= 128;
                if (count > n - i || r.p >= r.end) return false;
                const u8 v = *r.p++;
                for (u32 k = 0; k < count; ++k) scan[(i++) * 4 + c] = v;
            } else {
                if (count == 0 || count > n - i || static_cast<usize>(r.end - r.p) < count) return false;
                for (u32 k = 0; k < count; ++k) scan[(i++) * 4 + c] = *r.p++;
            }
        }
    }
    return true;
}

HdriDecode decode_radiance(const u8* bytes, usize size) {
    Reader r{bytes, bytes + size};
    std::string line;
    if (!r.line(line) || line.rfind("#?", 0) != 0) return fail(HdriStatus::UnsupportedFormat);
    bool xyz = false;
    for (;;) {
        if (!r.line(line)) return fail(HdriStatus::Corrupt);
        if (line.empty()) break;
        if (line.rfind("FORMAT=", 0) == 0) {
            std::string f = line.substr(7);
            while (!f.empty() && std::isspace(static_cast<unsigned char>(f.back()))) f.pop_back();
            if (f == "32-bit_rle_xyze") xyz = true;
            else if (f != "32-bit_rle_rgbe") return fail(HdriStatus::UnsupportedFormat);
        }
    }
    // Resolução: "-Y H +X W" é o comum; vale qualquer sinal e a forma transposta.
    if (!r.line(line)) return fail(HdriStatus::Corrupt);
    char s1 = 0, a1 = 0, s2 = 0, a2 = 0;
    long n1 = 0, n2 = 0;
    if (std::sscanf(line.c_str(), " %c%c %ld %c%c %ld", &s1, &a1, &n1, &s2, &a2, &n2) != 6 ||
        (s1 != '-' && s1 != '+') || (s2 != '-' && s2 != '+') || n1 <= 0 || n2 <= 0 ||
        !((a1 == 'Y' && a2 == 'X') || (a1 == 'X' && a2 == 'Y')))
        return fail(HdriStatus::Corrupt);
    if (static_cast<u64>(n1) > kMaxSide || static_cast<u64>(n2) > kMaxSide) return fail(HdriStatus::TooLarge);
    const bool rowsAreY = a1 == 'Y';
    const u32 width = static_cast<u32>(rowsAreY ? n2 : n1), height = static_cast<u32>(rowsAreY ? n1 : n2);
    // Sinais: −Y = de cima para baixo (a imagem cresce para baixo); +X = da
    // esquerda para a direita.
    const bool yDown = (rowsAreY ? s1 : s2) == '-';
    const bool xRight = (rowsAreY ? s2 : s1) == '+';
    Downscale out;
    out.init(width, height);
    const u32 scans = static_cast<u32>(n1), length = static_cast<u32>(n2);
    std::vector<u8> scan;
    for (u32 s = 0; s < scans; ++s) {
        if (!read_scanline(r, length, scan)) return fail(HdriStatus::Corrupt);
        for (u32 i = 0; i < length; ++i) {
            const u8* px = &scan[static_cast<usize>(i) * 4];
            // Coordenadas do arquivo → imagem com a linha 0 em cima.
            u32 x = rowsAreY ? i : s, y = rowsAreY ? s : i;
            if (!xRight) x = width - 1 - x;
            if (!yDown) y = height - 1 - y;
            if (px[3] == 0) { out.add(x, y, 0, 0, 0); continue; }
            const f32 f = std::ldexp(1.0f, static_cast<int>(px[3]) - (128 + 8));
            f32 a = px[0] * f, b = px[1] * f, c = px[2] * f;
            if (xyz) {
                // CIE XYZ → sRGB/Rec.709 linear (D65), a mesma base do resto do motor.
                const f32 R = 3.2404542f * a - 1.5371385f * b - 0.4985314f * c;
                const f32 G = -0.9692660f * a + 1.8760108f * b + 0.0415560f * c;
                const f32 B = 0.0556434f * a - 0.2040259f * b + 1.0572252f * c;
                a = R; b = G; c = B;
            }
            out.add(x, y, a, b, c);
        }
    }
    return done(out.finish());
}

// --- OpenEXR ------------------------------------------------------------------

f32 exr_value(const EXRImage& img, const EXRHeader& h, int channel, usize index, const unsigned char* const* planes) {
    const unsigned char* plane = planes[channel];
    switch (h.pixel_types[channel]) {
        case TINYEXR_PIXELTYPE_FLOAT: return reinterpret_cast<const float*>(plane)[index];
        case TINYEXR_PIXELTYPE_HALF: {
            const u16 v = reinterpret_cast<const u16*>(plane)[index];
            const u32 sign = (v >> 15) & 1u, exp = (v >> 10) & 31u, man = v & 1023u;
            f32 f;
            if (exp == 0) f = std::ldexp(static_cast<f32>(man), -24);
            else if (exp == 31) f = man ? NAN : INFINITY;
            else f = std::ldexp(static_cast<f32>(man | 1024u), static_cast<int>(exp) - 25);
            return sign ? -f : f;
        }
        case TINYEXR_PIXELTYPE_UINT: return static_cast<f32>(reinterpret_cast<const u32*>(plane)[index]);
        default: (void)img; return 0.0f;
    }
}

int exr_channel(const EXRHeader& h, char want) {
    // "R" ou "<camada>.R" (a primeira camada que tiver o canal).
    for (int i = 0; i < h.num_channels; ++i) {
        const char* n = h.channels[i].name;
        const usize len = std::strlen(n);
        if (len == 0) continue;
        const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(n[len - 1])));
        if (last == want && (len == 1 || n[len - 2] == '.')) return i;
    }
    return -1;
}

HdriDecode decode_exr(const u8* bytes, usize size) {
    EXRVersion version;
    if (ParseEXRVersionFromMemory(&version, bytes, size) != TINYEXR_SUCCESS) return fail(HdriStatus::Corrupt);
    if (version.multipart || version.non_image) return fail(HdriStatus::UnsupportedFormat);
    EXRHeader header;
    InitEXRHeader(&header);
    const char* err = nullptr;
    if (ParseEXRHeaderFromMemory(&header, &version, bytes, size, &err) != TINYEXR_SUCCESS) {
        if (err) FreeEXRErrorMessage(err);
        return fail(HdriStatus::Corrupt);
    }
    struct HeaderGuard { EXRHeader* h; ~HeaderGuard() { FreeEXRHeader(h); } } headerGuard{&header};
    const i64 w = static_cast<i64>(header.data_window.max_x) - header.data_window.min_x + 1;
    const i64 h = static_cast<i64>(header.data_window.max_y) - header.data_window.min_y + 1;
    if (w <= 0 || h <= 0) return fail(HdriStatus::Corrupt);
    if (static_cast<u64>(w) > kMaxSide || static_cast<u64>(h) > kMaxSide) return fail(HdriStatus::TooLarge);
    const int cr = exr_channel(header, 'R'), cg = exr_channel(header, 'G'), cb = exr_channel(header, 'B');
    const int cy = exr_channel(header, 'Y');
    if ((cr < 0 || cg < 0 || cb < 0) && cy < 0) return fail(HdriStatus::UnsupportedFormat);
    // tinyexr decodifica TODOS os canais, cada um no tipo dele: o custo real.
    u64 bytesPerPixel = 0;
    for (int i = 0; i < header.num_channels; ++i)
        bytesPerPixel += header.pixel_types[i] == TINYEXR_PIXELTYPE_HALF ? 2u : 4u;
    if (static_cast<u64>(w) * static_cast<u64>(h) * bytesPerPixel > kExrMaxBytes) return fail(HdriStatus::TooLarge);
    EXRImage image;
    InitEXRImage(&image);
    if (LoadEXRImageFromMemory(&image, &header, bytes, size, &err) != TINYEXR_SUCCESS) {
        if (err) FreeEXRErrorMessage(err);
        return fail(HdriStatus::Corrupt);
    }
    struct ImageGuard { EXRImage* i; ~ImageGuard() { FreeEXRImage(i); } } imageGuard{&image};
    Downscale out;
    out.init(static_cast<u32>(w), static_cast<u32>(h));
    auto put = [&](const unsigned char* const* planes, usize index, u32 x, u32 y) {
        if (cr >= 0 && cg >= 0 && cb >= 0)
            out.add(x, y, exr_value(image, header, cr, index, planes), exr_value(image, header, cg, index, planes),
                    exr_value(image, header, cb, index, planes));
        else {
            const f32 v = exr_value(image, header, cy, index, planes);
            out.add(x, y, v, v, v);
        }
    };
    if (image.images) {
        for (u32 y = 0; y < static_cast<u32>(h); ++y)
            for (u32 x = 0; x < static_cast<u32>(w); ++x)
                put(image.images, static_cast<usize>(y) * static_cast<usize>(w) + x, x, y);
    } else if (image.tiles && image.num_tiles > 0) {
        for (int t = 0; t < image.num_tiles; ++t) {
            const EXRTile& tile = image.tiles[t];
            if (tile.level_x != 0 || tile.level_y != 0 || !tile.images) continue;
            for (int ty = 0; ty < tile.height; ++ty)
                for (int tx = 0; tx < tile.width; ++tx) {
                    const i64 x = static_cast<i64>(tile.offset_x) * header.tile_size_x + tx;
                    const i64 y = static_cast<i64>(tile.offset_y) * header.tile_size_y + ty;
                    if (x < 0 || y < 0 || x >= w || y >= h) continue;
                    put(tile.images, static_cast<usize>(ty) * static_cast<usize>(header.tile_size_x) + static_cast<usize>(tx),
                        static_cast<u32>(x), static_cast<u32>(y));
                }
        }
    } else {
        return fail(HdriStatus::Corrupt);
    }
    return done(out.finish());
}

// --- JPG / PNG ----------------------------------------------------------------

HdriDecode decode_ldr(const u8* bytes, usize size, f32 ldrGain) {
    if (size > static_cast<usize>(INT32_MAX)) return fail(HdriStatus::TooLarge);
    const int n = static_cast<int>(size);
    int w = 0, h = 0, c = 0;
    if (!stbi_info_from_memory(bytes, n, &w, &h, &c) || w <= 0 || h <= 0) return fail(HdriStatus::UnsupportedFormat);
    const bool wide = stbi_is_16_bit_from_memory(bytes, n) != 0;
    if (static_cast<u64>(w) * static_cast<u64>(h) > (wide ? kLdr16MaxPixels : kLdrMaxPixels)) return fail(HdriStatus::TooLarge);
    if (stbi_is_hdr_from_memory(bytes, n)) return fail(HdriStatus::UnsupportedFormat);   // Radiance já foi tratado
    const f32 gain = std::isfinite(ldrGain) && ldrGain > 0.0f ? ldrGain : 1.0f;
    auto eotf = [gain](f32 e) {
        return (e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f)) * gain;
    };
    Downscale out;
    if (wide) {
        stbi_us* px = stbi_load_16_from_memory(bytes, n, &w, &h, &c, 3);
        if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return fail(HdriStatus::Corrupt); }
        out.init(static_cast<u32>(w), static_cast<u32>(h));
        for (u32 y = 0; y < static_cast<u32>(h); ++y)
            for (u32 x = 0; x < static_cast<u32>(w); ++x) {
                const stbi_us* p = px + (static_cast<usize>(y) * static_cast<usize>(w) + x) * 3;
                out.add(x, y, eotf(p[0] / 65535.0f), eotf(p[1] / 65535.0f), eotf(p[2] / 65535.0f));
            }
        stbi_image_free(px);
    } else {
        stbi_uc* px = stbi_load_from_memory(bytes, n, &w, &h, &c, 3);
        if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return fail(HdriStatus::Corrupt); }
        f32 lut[256];
        for (u32 i = 0; i < 256; ++i) lut[i] = eotf(static_cast<f32>(i) / 255.0f);
        out.init(static_cast<u32>(w), static_cast<u32>(h));
        for (u32 y = 0; y < static_cast<u32>(h); ++y)
            for (u32 x = 0; x < static_cast<u32>(w); ++x) {
                const stbi_uc* p = px + (static_cast<usize>(y) * static_cast<usize>(w) + x) * 3;
                out.add(x, y, lut[p[0]], lut[p[1]], lut[p[2]]);
            }
        stbi_image_free(px);
    }
    auto pixels = out.finish();
    pixels->ldr = true;
    return done(std::move(pixels));
}

// --- .zip ---------------------------------------------------------------------

u32 le16(const u8* p) noexcept { return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8); }
u32 le32(const u8* p) noexcept { return le16(p) | (le16(p + 2) << 16); }

bool ends_with(std::string_view s, std::string_view tail) {
    if (s.size() < tail.size()) return false;
    for (usize i = 0; i < tail.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - tail.size() + i])) != tail[i]) return false;
    return true;
}

HdriDecode decode_any(const u8* bytes, usize size, f32 ldrGain, bool allowZip);

HdriDecode decode_zip(const u8* bytes, usize size, f32 ldrGain) {
    // Fim do diretório central: nos últimos 64 KiB + 22 bytes.
    if (size < 22) return fail(HdriStatus::Corrupt);
    usize eocd = static_cast<usize>(-1);
    const usize stop = size > 65557 ? size - 65557 : 0;
    for (usize i = size - 22 + 1; i-- > stop;)
        if (le32(bytes + i) == 0x06054b50u) { eocd = i; break; }
    if (eocd == static_cast<usize>(-1)) return fail(HdriStatus::Corrupt);
    const u32 entries = le16(bytes + eocd + 10);
    usize dir = le32(bytes + eocd + 16);
    // Melhor candidato: .hdr/.exr antes de jpg/png; pastas e __MACOSX fora.
    int bestRank = 0;
    usize bestLocal = 0, bestComp = 0, bestRaw = 0;
    u32 bestMethod = 0, bestFlags = 0;
    for (u32 e = 0; e < entries; ++e) {
        if (dir + 46 > size || le32(bytes + dir) != 0x02014b50u) return fail(HdriStatus::Corrupt);
        const u8* c = bytes + dir;
        const u32 nameLen = le16(c + 28), extraLen = le16(c + 30), commentLen = le16(c + 32);
        if (dir + 46 + nameLen > size) return fail(HdriStatus::Corrupt);
        const std::string_view name(reinterpret_cast<const char*>(c + 46), nameLen);
        int rank = 0;
        if (name.find("__MACOSX") == std::string_view::npos && !name.empty() && name.back() != '/') {
            if (ends_with(name, ".hdr") || ends_with(name, ".hdri") || ends_with(name, ".pic") || ends_with(name, ".exr")) rank = 2;
            else if (ends_with(name, ".jpg") || ends_with(name, ".jpeg") || ends_with(name, ".png")) rank = 1;
        }
        if (rank > bestRank) {
            bestRank = rank; bestFlags = le16(c + 8); bestMethod = le16(c + 10);
            bestComp = le32(c + 20); bestRaw = le32(c + 24); bestLocal = le32(c + 42);
        }
        dir += 46 + nameLen + extraLen + commentLen;
    }
    if (bestRank == 0) return fail(HdriStatus::UnsupportedFormat);
    if ((bestFlags & 1u) || bestComp == 0xffffffffu || bestRaw == 0xffffffffu) return fail(HdriStatus::UnsupportedFormat);  // senha / zip64
    if (bestRaw > kZipMaxEntry) return fail(HdriStatus::TooLarge);
    if (bestLocal + 30 > size || le32(bytes + bestLocal) != 0x04034b50u) return fail(HdriStatus::Corrupt);
    const usize data = bestLocal + 30 + le16(bytes + bestLocal + 26) + le16(bytes + bestLocal + 28);
    if (data > size || bestComp > size - data) return fail(HdriStatus::Corrupt);
    if (bestMethod == 0) {
        if (bestComp != bestRaw) return fail(HdriStatus::Corrupt);
        return decode_any(bytes + data, bestRaw, ldrGain, false);
    }
    if (bestMethod != 8 || bestComp > static_cast<usize>(INT32_MAX) || bestRaw > static_cast<usize>(INT32_MAX))
        return fail(HdriStatus::UnsupportedFormat);
    std::vector<u8> raw(bestRaw);
    const int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(raw.data()), static_cast<int>(raw.size()),
                                                     reinterpret_cast<const char*>(bytes + data), static_cast<int>(bestComp));
    if (got != static_cast<int>(bestRaw)) return fail(HdriStatus::Corrupt);
    return decode_any(raw.data(), raw.size(), ldrGain, false);
}

HdriDecode decode_any(const u8* bytes, usize size, f32 ldrGain, bool allowZip) {
    if (!bytes || size < 4) return fail(HdriStatus::Corrupt);
    if (bytes[0] == '#' && bytes[1] == '?') return decode_radiance(bytes, size);
    if (bytes[0] == 0x76 && bytes[1] == 0x2f && bytes[2] == 0x31 && bytes[3] == 0x01) return decode_exr(bytes, size);
    if (bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 3 && bytes[3] == 4)
        return allowZip ? decode_zip(bytes, size, ldrGain) : fail(HdriStatus::UnsupportedFormat);
    return decode_ldr(bytes, size, ldrGain);
}

} // namespace

HdriDecode decode_hdri_detailed(const u8* bytes, usize size, f32 ldrGain) noexcept {
    return decode_any(bytes, size, ldrGain, true);
}

} // namespace aurea::scene3d
