// =============================================================================
//  Aurea / export / ImageEncode.cpp — PNG, GIF e ZIP do export como imagem.
//  Escrito para o motor (ver ImageEncode.hpp); nenhum código de terceiros.
// =============================================================================
#include "aurea/export/ImageEncode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <queue>

namespace aurea {

// =============================================================================
// Plano e estimativa
// =============================================================================
ImageExportPlan plan_image_export(const ImageExportSettings& s, u32 compW, u32 compH, f64 compFps,
                                  i64 durationFrames, bool transparentBackground) noexcept {
    ImageExportPlan p;
    if (compW == 0 || compH == 0) return p;
    p.alpha = transparentBackground;
    f64 k = 1.0;
    if (s.format == ImageExportFormat::Gif) {
        const u32 maxW = std::clamp<u32>(s.maxWidth ? s.maxWidth : kGifDefaultWidth, 16u, kGifMaxWidth);
        k = std::min(1.0, static_cast<f64>(maxW) / static_cast<f64>(compW));
    } else if (s.shortSide > 0) {
        k = static_cast<f64>(s.shortSide) / static_cast<f64>(std::min(compW, compH));
    }
    p.width = std::max<u32>(1u, static_cast<u32>(std::lround(compW * k)));
    p.height = std::max<u32>(1u, static_cast<u32>(std::lround(compH * k)));
    const f64 cfps = compFps > 0.0 ? compFps : 30.0;
    if (s.format == ImageExportFormat::Png) {
        p.fps = cfps;
        p.frames = 1;
        return p;
    }
    if (s.format == ImageExportFormat::Gif) {
        p.fps = std::clamp(s.fps > 0.0 ? s.fps : kGifDefaultFps, kGifMinFps, kGifMaxFps);
        p.fps = std::min(p.fps, std::max(kGifMinFps, cfps));   // nunca mais quadros que a composição tem
    } else {
        p.fps = s.fps > 0.0 ? s.fps : cfps;
    }
    const f64 seconds = static_cast<f64>(std::max<i64>(1, durationFrames)) / cfps;
    p.frames = std::max<u32>(1u, static_cast<u32>(std::ceil(seconds * p.fps - 1e-6)));
    return p;
}

u64 estimate_image_export_bytes(ImageExportFormat format, u32 width, u32 height, u32 frames, bool alpha) noexcept {
    const f64 px = static_cast<f64>(width) * static_cast<f64>(height);
    // Médias medidas nos quadros de teste do motor (gráfico + foto): o PNG de
    // um quadro renderizado fica perto de 1,6 byte/pixel em RGB (2,0 com
    // alfa); o GIF com dithering, ~0,55 byte/pixel por quadro (paleta local
    // e cabeçalhos inclusos). É estimativa: arte chapada sai bem menor.
    switch (format) {
        case ImageExportFormat::Png:
            return static_cast<u64>(px * (alpha ? 2.0 : 1.6)) + 1024u;
        case ImageExportFormat::PngSequence:
            return static_cast<u64>(px * (alpha ? 2.0 : 1.6) * std::max<u32>(1u, frames)) +
                   static_cast<u64>(std::max<u32>(1u, frames)) * 120u + 1024u;
        case ImageExportFormat::Gif:
            return static_cast<u64>(px * 0.55 * std::max<u32>(1u, frames)) +
                   static_cast<u64>(std::max<u32>(1u, frames)) * 800u + 1024u;
    }
    return 0;
}

// =============================================================================
// Conversão RGBA16F linear pré-multiplicado → RGBA8 sRGB de alfa reto
// =============================================================================
namespace {
f32 half_to_float(u16 h) noexcept {
    const u32 sign = (h & 0x8000u) << 16, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
    u32 bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            u32 e = 113, m = mant;
            while (!(m & 0x400u)) { m <<= 1; --e; }
            bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 112) << 23) | (mant << 13);
    }
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}

u8 encode_srgb(f32 v) noexcept {
    if (!(v > 0.0f)) return 0;   // NaN e negativos
    if (v >= 1.0f) return 255;
    const f32 e = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(std::lround(std::clamp(e, 0.0f, 1.0f) * 255.0f));
}

/// Tabelas de 64 K entradas pelos bits do half: o caso comum (alfa 1) vira
/// uma consulta por canal, sem pow.
struct HalfTables {
    std::vector<f32> toFloat;
    std::vector<u8> toSrgb;
    HalfTables() : toFloat(65536), toSrgb(65536) {
        for (u32 i = 0; i < 65536; ++i) {
            toFloat[i] = half_to_float(static_cast<u16>(i));
            toSrgb[i] = encode_srgb(toFloat[i]);
        }
    }
};
const HalfTables& half_tables() {
    static const HalfTables t;
    return t;
}
} // namespace

void linear_half_to_srgb8(const u16* half, usize pixels, u8* out) noexcept {
    const HalfTables& t = half_tables();
    constexpr u16 kOne = 0x3C00u;
    for (usize i = 0; i < pixels; ++i) {
        const u16* p = half + i * 4;
        u8* o = out + i * 4;
        if (p[3] == kOne) {
            o[0] = t.toSrgb[p[0]];
            o[1] = t.toSrgb[p[1]];
            o[2] = t.toSrgb[p[2]];
            o[3] = 255;
            continue;
        }
        const f32 a = t.toFloat[p[3]];
        if (!(a > 1e-5f)) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
        const f32 inv = 1.0f / a;   // o trabalho é pré-multiplicado
        o[0] = encode_srgb(t.toFloat[p[0]] * inv);
        o[1] = encode_srgb(t.toFloat[p[1]] * inv);
        o[2] = encode_srgb(t.toFloat[p[2]] * inv);
        o[3] = static_cast<u8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
    }
}

// =============================================================================
// CRC-32 (PNG e ZIP usam o mesmo polinômio)
// =============================================================================
namespace {
const std::array<u32, 256>& crc_table() {
    static const std::array<u32, 256> t = [] {
        std::array<u32, 256> r{};
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            r[n] = c;
        }
        return r;
    }();
    return t;
}
} // namespace

u32 crc32_update(u32 crc, const u8* data, usize size) noexcept {
    const auto& t = crc_table();
    u32 c = crc ^ 0xFFFFFFFFu;
    for (usize i = 0; i < size; ++i) c = t[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// =============================================================================
// Deflate (RFC 1951): LZ77 com cadeia de hash + Huffman dinâmico por bloco
// =============================================================================
namespace {

class BitWriter {
public:
    explicit BitWriter(std::vector<u8>& out) : out_(out) {}
    void put(u32 bits, u32 count) {   // LSB primeiro
        acc_ |= static_cast<u64>(bits) << n_;
        n_ += count;
        while (n_ >= 8) {
            out_.push_back(static_cast<u8>(acc_));
            acc_ >>= 8;
            n_ -= 8;
        }
    }
    void flush() {
        if (n_ > 0) out_.push_back(static_cast<u8>(acc_));
        acc_ = 0;
        n_ = 0;
    }
private:
    std::vector<u8>& out_;
    u64 acc_ = 0;
    u32 n_ = 0;
};

constexpr u16 kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                              35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr u8 kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr u16 kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                               1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr u8 kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr u8 kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct CodeTables {
    std::array<u8, 259> lenCode{};      ///< comprimento → índice (0..28)
    std::array<u8, 32769> distCode{};   ///< distância → índice (0..29)
    CodeTables() {
        for (u32 c = 0; c < 29; ++c) {
            const u32 end = c == 28 ? 259 : kLenBase[c + 1];
            for (u32 l = kLenBase[c]; l < end && l < 259; ++l) lenCode[l] = static_cast<u8>(c);
        }
        lenCode[258] = 28;
        for (u32 c = 0; c < 30; ++c) {
            const u32 end = c == 29 ? 32769 : kDistBase[c + 1];
            for (u32 d = kDistBase[c]; d < end; ++d) distCode[d] = static_cast<u8>(c);
        }
    }
};
const CodeTables& code_tables() {
    static const CodeTables t;
    return t;
}

/// Comprimentos de Huffman com teto `maxLen`. Passou do teto: achata as
/// frequências e refaz (sempre dá código completo — o que todo leitor aceita).
void huffman_lengths(const u32* freq, u32 n, u32 maxLen, u8* lens) {
    std::vector<u32> f(freq, freq + n);
    // Pelo menos dois símbolos: um código de 1 símbolo é incompleto e alguns
    // leitores recusam.
    u32 used = 0;
    for (u32 i = 0; i < n; ++i) used += f[i] ? 1u : 0u;
    for (u32 i = 0; used < 2 && i < n; ++i) {
        if (!f[i]) { f[i] = 1; ++used; }
    }
    for (;;) {
        struct Node { u64 w; i32 left, right; };
        std::vector<Node> nodes;
        nodes.reserve(2 * n);
        using Item = std::pair<u64, i32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        std::vector<i32> leafOf(n, -1);
        for (u32 i = 0; i < n; ++i) {
            if (!f[i]) continue;
            leafOf[i] = static_cast<i32>(nodes.size());
            nodes.push_back({f[i], -1, -1});
            pq.push({f[i], leafOf[i]});
        }
        while (pq.size() > 1) {
            const Item a = pq.top(); pq.pop();
            const Item b = pq.top(); pq.pop();
            nodes.push_back({a.first + b.first, a.second, b.second});
            pq.push({a.first + b.first, static_cast<i32>(nodes.size() - 1)});
        }
        std::vector<u32> depth(nodes.size(), 0);
        for (i32 k = static_cast<i32>(nodes.size()) - 1; k >= 0; --k) {
            if (nodes[k].left >= 0) {
                depth[nodes[k].left] = depth[k] + 1;
                depth[nodes[k].right] = depth[k] + 1;
            }
        }
        u32 maxSeen = 0;
        for (u32 i = 0; i < n; ++i) {
            lens[i] = leafOf[i] >= 0 ? static_cast<u8>(std::min<u32>(depth[leafOf[i]], 255)) : 0;
            maxSeen = std::max<u32>(maxSeen, lens[i]);
        }
        if (maxSeen <= maxLen) return;
        for (u32 i = 0; i < n; ++i) if (f[i]) f[i] = (f[i] >> 1) | 1u;
    }
}

/// Códigos canônicos (RFC 1951 §3.2.2), já com os bits invertidos para o
/// escritor LSB-primeiro.
void canonical_codes(const u8* lens, u32 n, u16* codes) {
    u32 blCount[16] = {};
    for (u32 i = 0; i < n; ++i) blCount[lens[i]]++;
    blCount[0] = 0;
    u32 next[16] = {};
    u32 code = 0;
    for (u32 b = 1; b < 16; ++b) {
        code = (code + blCount[b - 1]) << 1;
        next[b] = code;
    }
    for (u32 i = 0; i < n; ++i) {
        const u32 len = lens[i];
        if (!len) { codes[i] = 0; continue; }
        u32 c = next[len]++, r = 0;
        for (u32 k = 0; k < len; ++k) { r = (r << 1) | (c & 1u); c >>= 1; }
        codes[i] = static_cast<u16>(r);
    }
}

struct Sym { u16 litlen; u16 dist; };   ///< dist 0 = literal

void write_block(BitWriter& bw, const std::vector<Sym>& syms, bool last) {
    const CodeTables& ct = code_tables();
    u32 lf[286] = {}, df[30] = {};
    for (const Sym& s : syms) {
        if (s.dist == 0) lf[s.litlen]++;
        else {
            lf[257 + ct.lenCode[s.litlen]]++;
            df[ct.distCode[s.dist]]++;
        }
    }
    lf[256] = 1;
    u8 ll[286], dl[30];
    huffman_lengths(lf, 286, 15, ll);
    huffman_lengths(df, 30, 15, dl);
    u32 hlit = 286, hdist = 30;
    while (hlit > 257 && ll[hlit - 1] == 0) --hlit;
    while (hdist > 1 && dl[hdist - 1] == 0) --hdist;

    // Comprimentos dos dois alfabetos em sequência, com RLE (16/17/18).
    std::vector<u8> all(ll, ll + hlit);
    all.insert(all.end(), dl, dl + hdist);
    struct Rle { u8 sym, extra; };
    std::vector<Rle> rle;
    for (usize i = 0; i < all.size();) {
        const u8 v = all[i];
        usize run = 1;
        while (i + run < all.size() && all[i + run] == v) ++run;
        usize left = run;
        if (v == 0) {
            while (left >= 11) { const usize k = std::min<usize>(left, 138); rle.push_back({18, static_cast<u8>(k - 11)}); left -= k; }
            if (left >= 3) { rle.push_back({17, static_cast<u8>(left - 3)}); left = 0; }
        } else {
            rle.push_back({v, 0});
            --left;
            while (left >= 3) { const usize k = std::min<usize>(left, 6); rle.push_back({16, static_cast<u8>(k - 3)}); left -= k; }
        }
        while (left > 0) { rle.push_back({v, 0}); --left; }
        i += run;
    }
    u32 cf[19] = {};
    for (const Rle& r : rle) cf[r.sym]++;
    u8 cl[19];
    huffman_lengths(cf, 19, 7, cl);
    u32 hclen = 19;
    while (hclen > 4 && cl[kClOrder[hclen - 1]] == 0) --hclen;
    u16 cc[19], lc[286], dc[30];
    canonical_codes(cl, 19, cc);
    canonical_codes(ll, 286, lc);
    canonical_codes(dl, 30, dc);

    bw.put(last ? 1u : 0u, 1);
    bw.put(2u, 2);   // Huffman dinâmico
    bw.put(hlit - 257, 5);
    bw.put(hdist - 1, 5);
    bw.put(hclen - 4, 4);
    for (u32 i = 0; i < hclen; ++i) bw.put(cl[kClOrder[i]], 3);
    for (const Rle& r : rle) {
        bw.put(cc[r.sym], cl[r.sym]);
        if (r.sym == 16) bw.put(r.extra, 2);
        else if (r.sym == 17) bw.put(r.extra, 3);
        else if (r.sym == 18) bw.put(r.extra, 7);
    }
    for (const Sym& s : syms) {
        if (s.dist == 0) { bw.put(lc[s.litlen], ll[s.litlen]); continue; }
        const u32 c = ct.lenCode[s.litlen];
        bw.put(lc[257 + c], ll[257 + c]);
        if (kLenExtra[c]) bw.put(s.litlen - kLenBase[c], kLenExtra[c]);
        const u32 d = ct.distCode[s.dist];
        bw.put(dc[d], dl[d]);
        if (kDistExtra[d]) bw.put(s.dist - kDistBase[d], kDistExtra[d]);
    }
    bw.put(lc[256], ll[256]);
}

constexpr u32 kWindow = 32768;
constexpr u32 kHashBits = 15;
constexpr u32 kMinMatch = 3;
constexpr u32 kMaxMatch = 258;

void deflate(const u8* data, usize size, std::vector<u8>& out, u32 maxChain, bool lazy) {
    BitWriter bw(out);
    if (size == 0) {
        // Bloco fixo vazio: só o fim de bloco.
        bw.put(1, 1);
        bw.put(1, 2);
        bw.put(0, 7);
        bw.flush();
        return;
    }
    std::vector<i32> head(1u << kHashBits, -1);
    std::vector<i32> prev(kWindow, -1);
    auto hash3 = [&](usize p) -> u32 {
        const u32 v = static_cast<u32>(data[p]) | (static_cast<u32>(data[p + 1]) << 8) | (static_cast<u32>(data[p + 2]) << 16);
        return (v * 2654435761u) >> (32 - kHashBits);
    };
    auto insert = [&](usize p) {
        if (p + kMinMatch > size) return;
        const u32 h = hash3(p);
        prev[p & (kWindow - 1)] = head[h];
        head[h] = static_cast<i32>(p);
    };
    auto longest = [&](usize p, u32& bestDist) -> u32 {
        if (p + kMinMatch > size) return 0;
        const u32 maxLen = static_cast<u32>(std::min<usize>(kMaxMatch, size - p));
        u32 best = kMinMatch - 1;
        i32 cand = head[hash3(p)];
        u32 chain = maxChain;
        while (cand >= 0 && chain-- > 0) {
            const usize c = static_cast<usize>(cand);
            if (p - c > kWindow - 1 || c >= p) break;
            if (data[c + best] == data[p + best] && data[c] == data[p]) {
                u32 l = 0;
                while (l < maxLen && data[c + l] == data[p + l]) ++l;
                if (l > best) {
                    best = l;
                    bestDist = static_cast<u32>(p - c);
                    if (l >= maxLen) break;
                }
            }
            const i32 nx = prev[c & (kWindow - 1)];
            if (nx >= cand) break;
            cand = nx;
        }
        return best >= kMinMatch ? best : 0;
    };

    std::vector<Sym> syms;
    constexpr usize kBlockSyms = 1u << 16;
    syms.reserve(kBlockSyms + 2);
    usize p = 0;
    while (p < size) {
        u32 dist = 0;
        u32 len = longest(p, dist);
        if (len && lazy && len < 32 && p + 1 < size) {
            u32 d2 = 0;
            insert(p);
            const u32 l2 = longest(p + 1, d2);
            if (l2 > len) {
                syms.push_back({data[p], 0});
                ++p;
                len = l2;
                dist = d2;
                // O literal (p − 1) já entrou no hash; o casamento começa em p.
                syms.push_back({static_cast<u16>(len), static_cast<u16>(dist)});
                for (u32 k = 0; k < len; ++k) insert(p + k);
                p += len;
                if (syms.size() >= kBlockSyms) { write_block(bw, syms, false); syms.clear(); }
                continue;
            }
            syms.push_back({static_cast<u16>(len), static_cast<u16>(dist)});
            for (u32 k = 1; k < len; ++k) insert(p + k);
            p += len;
        } else if (len) {
            syms.push_back({static_cast<u16>(len), static_cast<u16>(dist)});
            for (u32 k = 0; k < len; ++k) insert(p + k);
            p += len;
        } else {
            syms.push_back({data[p], 0});
            insert(p);
            ++p;
        }
        if (syms.size() >= kBlockSyms) { write_block(bw, syms, false); syms.clear(); }
    }
    write_block(bw, syms, true);
    bw.flush();
}

u32 adler32(const u8* d, usize n) {
    u32 a = 1, b = 0;
    while (n > 0) {
        const usize k = std::min<usize>(n, 5552);
        for (usize i = 0; i < k; ++i) { a += d[i]; b += a; }
        a %= 65521u;
        b %= 65521u;
        d += k;
        n -= k;
    }
    return (b << 16) | a;
}

void put_be32(std::vector<u8>& o, u32 v) {
    o.push_back(static_cast<u8>(v >> 24));
    o.push_back(static_cast<u8>(v >> 16));
    o.push_back(static_cast<u8>(v >> 8));
    o.push_back(static_cast<u8>(v));
}

void zlib_compress_level(const u8* data, usize size, std::vector<u8>& out, u32 maxChain, bool lazy) {
    out.push_back(0x78);
    out.push_back(0x9C);   // janela de 32 KB, nível padrão (FCHECK: 0x789C % 31 == 0)
    deflate(data, size, out, maxChain, lazy);
    put_be32(out, adler32(data, size));
}

} // namespace

void zlib_compress(const u8* data, usize size, std::vector<u8>& out) noexcept {
    zlib_compress_level(data, size, out, 32, true);
}

// =============================================================================
// PNG
// =============================================================================
namespace {
void png_chunk(std::vector<u8>& out, const char type[4], const u8* data, usize n) {
    put_be32(out, static_cast<u32>(n));
    const usize start = out.size();
    out.insert(out.end(), type, type + 4);
    if (n) out.insert(out.end(), data, data + n);
    put_be32(out, crc32_update(0, out.data() + start, n + 4));
}

u8 paeth(i32 a, i32 b, i32 c) {
    const i32 p = a + b - c;
    const i32 pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<u8>(a);
    return static_cast<u8>(pb <= pc ? b : c);
}
} // namespace

Status encode_png(const u8* rgba, u32 width, u32 height, bool withAlpha, std::vector<u8>& out) noexcept {
    if (!rgba || width == 0 || height == 0) return Status{Errc::InvalidArgument, "imagem vazia"};
    if (width > 32768 || height > 32768) return Status{Errc::InvalidArgument, "imagem grande demais para PNG"};
    const u32 bpp = withAlpha ? 4u : 3u;
    const usize rowBytes = static_cast<usize>(width) * bpp;
    std::vector<u8> raw(static_cast<usize>(height) * (rowBytes + 1));
    std::vector<u8> cur(rowBytes), prevRow(rowBytes, 0), cand(rowBytes), best(rowBytes);
    for (u32 y = 0; y < height; ++y) {
        const u8* src = rgba + static_cast<usize>(y) * width * 4;
        if (withAlpha) std::memcpy(cur.data(), src, rowBytes);
        else for (u32 x = 0; x < width; ++x) { cur[x * 3] = src[x * 4]; cur[x * 3 + 1] = src[x * 4 + 1]; cur[x * 3 + 2] = src[x * 4 + 2]; }
        // Filtro da linha: o de menor soma absoluta (heurística da própria
        // especificação do PNG).
        u64 bestScore = ~0ull;
        u8 bestType = 0;
        for (u8 type = 0; type < 5; ++type) {
            u64 score = 0;
            for (usize i = 0; i < rowBytes; ++i) {
                const i32 a = i >= bpp ? cur[i - bpp] : 0;
                const i32 b = prevRow[i];
                const i32 c = i >= bpp ? prevRow[i - bpp] : 0;
                u8 v = cur[i];
                switch (type) {
                    case 1: v = static_cast<u8>(v - a); break;
                    case 2: v = static_cast<u8>(v - b); break;
                    case 3: v = static_cast<u8>(v - ((a + b) >> 1)); break;
                    case 4: v = static_cast<u8>(v - paeth(a, b, c)); break;
                    default: break;
                }
                cand[i] = v;
                score += static_cast<u64>(v < 128 ? v : 256 - v);
            }
            if (score < bestScore) { bestScore = score; bestType = type; best.swap(cand); }
        }
        u8* dst = raw.data() + static_cast<usize>(y) * (rowBytes + 1);
        dst[0] = bestType;
        std::memcpy(dst + 1, best.data(), rowBytes);
        prevRow.swap(cur);
    }

    out.clear();
    static constexpr u8 kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    out.insert(out.end(), kSig, kSig + 8);
    u8 ihdr[13];
    ihdr[0] = static_cast<u8>(width >> 24); ihdr[1] = static_cast<u8>(width >> 16);
    ihdr[2] = static_cast<u8>(width >> 8);  ihdr[3] = static_cast<u8>(width);
    ihdr[4] = static_cast<u8>(height >> 24); ihdr[5] = static_cast<u8>(height >> 16);
    ihdr[6] = static_cast<u8>(height >> 8);  ihdr[7] = static_cast<u8>(height);
    ihdr[8] = 8;                          // 8 bits por canal
    ihdr[9] = withAlpha ? 6 : 2;          // RGBA ou RGB
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(out, "IHDR", ihdr, 13);
    // sRGB (intenção perceptual): o leitor não reinterpreta as cores.
    const u8 srgb = 0;
    png_chunk(out, "sRGB", &srgb, 1);
    std::vector<u8> z;
    z.reserve(raw.size() / 2 + 1024);
    // Imagem grande: cadeia mais curta (o export não pode levar minutos num 4K).
    const bool big = static_cast<u64>(width) * height > 4'000'000ull;
    zlib_compress_level(raw.data(), raw.size(), z, big ? 12u : 32u, !big);
    // IDAT em pedaços de 1 MB (leitores antigos limitam o tamanho do chunk).
    constexpr usize kChunk = 1u << 20;
    for (usize off = 0; off < z.size(); off += kChunk) png_chunk(out, "IDAT", z.data() + off, std::min(kChunk, z.size() - off));
    png_chunk(out, "IEND", nullptr, 0);
    return OkStatus;
}

// =============================================================================
// GIF
// =============================================================================
u32 gif_frame_delay_cs(u32 i, f64 fps) noexcept {
    if (!(fps > 0.0)) fps = kGifDefaultFps;
    const i64 a = std::llround(static_cast<f64>(i) * 100.0 / fps);
    const i64 b = std::llround(static_cast<f64>(i + 1) * 100.0 / fps);
    return static_cast<u32>(std::max<i64>(2, b - a));
}

namespace {

struct Palette {
    std::array<u8, 768> rgb{};
    u32 count = 0;
    bool exact = false;   ///< cada cor do quadro tem a sua entrada (sem dithering)
};

/// Median cut no histograma de 15 bits (5 por canal), média real dos pixels
/// de cada caixa. Poucas cores (arte chapada): uma entrada por cor.
void build_palette(const u8* rgba, usize n, u32 maxColors, Palette& pal, std::vector<u32>& binOfColor) {
    struct Bin { u32 count = 0; u64 r = 0, g = 0, b = 0; };
    std::vector<Bin> hist(32768);
    for (usize i = 0; i < n; ++i) {
        const u8* p = rgba + i * 4;
        if (p[3] < 128) continue;
        const u32 key = (static_cast<u32>(p[0] >> 3) << 10) | (static_cast<u32>(p[1] >> 3) << 5) | (p[2] >> 3);
        Bin& b = hist[key];
        b.count++;
        b.r += p[0];
        b.g += p[1];
        b.b += p[2];
    }
    std::vector<u32> bins;
    for (u32 k = 0; k < 32768; ++k) if (hist[k].count) bins.push_back(k);
    binOfColor.assign(32768, 0xFFFFFFFFu);
    pal.count = 0;
    pal.exact = false;
    if (bins.empty()) {
        pal.count = 1;
        return;
    }
    auto mean_into = [&](const u32* first, const u32* last, u32 index) {
        u64 c = 0, r = 0, g = 0, b = 0;
        for (const u32* it = first; it != last; ++it) {
            const Bin& h = hist[*it];
            c += h.count; r += h.r; g += h.g; b += h.b;
            binOfColor[*it] = index;
        }
        c = std::max<u64>(1, c);
        pal.rgb[index * 3 + 0] = static_cast<u8>((r + c / 2) / c);
        pal.rgb[index * 3 + 1] = static_cast<u8>((g + c / 2) / c);
        pal.rgb[index * 3 + 2] = static_cast<u8>((b + c / 2) / c);
    };
    if (bins.size() <= maxColors) {
        for (u32 i = 0; i < bins.size(); ++i) mean_into(&bins[i], &bins[i] + 1, i);
        pal.count = static_cast<u32>(bins.size());
        pal.exact = true;
        return;
    }
    struct Box { u32 begin, end; u64 pop; u32 range; u32 axis; };
    auto chan = [](u32 key, u32 axis) { return (key >> (10 - axis * 5)) & 31u; };
    auto measure = [&](Box& bx) {
        u32 lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
        bx.pop = 0;
        for (u32 i = bx.begin; i < bx.end; ++i) {
            const u32 k = bins[i];
            bx.pop += hist[k].count;
            for (u32 a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], chan(k, a)); hi[a] = std::max(hi[a], chan(k, a)); }
        }
        // Verde pesa mais (o olho distingue mais tons de verde).
        const u32 rr = (hi[0] - lo[0]) * 3, gg = (hi[1] - lo[1]) * 4, bb = (hi[2] - lo[2]) * 2;
        bx.axis = gg >= rr && gg >= bb ? 1u : (rr >= bb ? 0u : 2u);
        bx.range = std::max({rr, gg, bb});
    };
    std::vector<Box> boxes;
    boxes.push_back({0, static_cast<u32>(bins.size()), 0, 0, 0});
    measure(boxes[0]);
    while (boxes.size() < maxColors) {
        // A caixa com mais erro: população × extensão.
        i32 pick = -1;
        f64 bestScore = 0.0;
        for (u32 i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2 || boxes[i].range == 0) continue;
            const f64 s = std::sqrt(static_cast<f64>(boxes[i].pop)) * boxes[i].range;
            if (s > bestScore) { bestScore = s; pick = static_cast<i32>(i); }
        }
        if (pick < 0) break;
        Box bx = boxes[pick];
        const u32 axis = bx.axis;
        std::sort(bins.begin() + bx.begin, bins.begin() + bx.end,
                  [&](u32 a, u32 b) { return chan(a, axis) < chan(b, axis); });
        u64 half = bx.pop / 2, acc = 0;
        u32 cut = bx.begin;
        while (cut < bx.end - 1 && acc + hist[bins[cut]].count <= half) acc += hist[bins[cut++]].count;
        if (cut == bx.begin) cut = bx.begin + 1;
        Box a{bx.begin, cut, 0, 0, 0}, b{cut, bx.end, 0, 0, 0};
        measure(a);
        measure(b);
        boxes[pick] = a;
        boxes.push_back(b);
    }
    for (u32 i = 0; i < boxes.size(); ++i) mean_into(bins.data() + boxes[i].begin, bins.data() + boxes[i].end, i);
    pal.count = static_cast<u32>(boxes.size());
}

/// Cor mais próxima (distância ponderada), com cache por cor de 6 bits/canal.
class Nearest {
public:
    void reset(const Palette& p) {
        pal_ = &p;
        cache_.assign(1u << 18, -1);
    }
    u8 find(i32 r, i32 g, i32 b) {
        const u32 key = (static_cast<u32>(r >> 2) << 12) | (static_cast<u32>(g >> 2) << 6) | static_cast<u32>(b >> 2);
        i16& c = cache_[key];
        if (c >= 0) return static_cast<u8>(c);
        const i32 cr = (r & ~3) | 2, cg = (g & ~3) | 2, cb = (b & ~3) | 2;
        i32 best = 0;
        i64 bestD = INT64_MAX;
        for (u32 i = 0; i < pal_->count; ++i) {
            const i32 dr = cr - pal_->rgb[i * 3], dg = cg - pal_->rgb[i * 3 + 1], db = cb - pal_->rgb[i * 3 + 2];
            const i64 d = 3ll * dr * dr + 4ll * dg * dg + 2ll * db * db;
            if (d < bestD) { bestD = d; best = static_cast<i32>(i); }
        }
        c = static_cast<i16>(best);
        return static_cast<u8>(best);
    }
private:
    const Palette* pal_ = nullptr;
    std::vector<i16> cache_;
};

constexpr u8 kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/// LZW do GIF (códigos de 9 a 12 bits, CLEAR quando a tabela enche), em
/// sub-blocos de até 255 bytes.
void lzw_encode(const u8* idx, usize n, std::vector<u8>& out) {
    constexpr u32 kMinCode = 8, kClear = 256, kEoi = 257, kMaxCodes = 4096;
    constexpr u32 kHash = 8192;
    std::vector<i32> keys(kHash, -1);
    std::vector<u16> vals(kHash, 0);
    std::vector<u8> bytes;
    bytes.reserve(n / 2 + 64);
    u64 acc = 0;
    u32 nbits = 0;
    u32 codeSize = kMinCode + 1;
    u32 nextCode = kEoi + 1;
    auto emit = [&](u32 code) {
        acc |= static_cast<u64>(code) << nbits;
        nbits += codeSize;
        while (nbits >= 8) { bytes.push_back(static_cast<u8>(acc)); acc >>= 8; nbits -= 8; }
    };
    auto clear_table = [&] { std::fill(keys.begin(), keys.end(), -1); nextCode = kEoi + 1; };
    emit(kClear);
    if (n > 0) {
        u32 prefix = idx[0];
        for (usize i = 1; i < n; ++i) {
            const u32 c = idx[i];
            const i32 key = static_cast<i32>((prefix << 8) | c);
            u32 h = (static_cast<u32>(key) * 2654435761u) >> (32 - 13);
            bool found = false;
            while (keys[h] >= 0) {
                if (keys[h] == key) { prefix = vals[h]; found = true; break; }
                h = (h + 1) & (kHash - 1);
            }
            if (found) continue;
            emit(prefix);
            // O decodificador cresce o código quando a tabela dele chega a
            // 2^n — ele anda um passo atrás do codificador.
            if (nextCode < kMaxCodes) {
                if (nextCode == (1u << codeSize) && codeSize < 12) ++codeSize;
                keys[h] = key;
                vals[h] = static_cast<u16>(nextCode++);
            } else {
                emit(kClear);
                clear_table();
                codeSize = kMinCode + 1;
            }
            prefix = c;
        }
        emit(prefix);
        if (nextCode == (1u << codeSize) && codeSize < 12) ++codeSize;
    }
    emit(kEoi);
    if (nbits > 0) bytes.push_back(static_cast<u8>(acc));
    out.push_back(static_cast<u8>(kMinCode));
    for (usize off = 0; off < bytes.size(); off += 255) {
        const usize k = std::min<usize>(255, bytes.size() - off);
        out.push_back(static_cast<u8>(k));
        out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(off),
                   bytes.begin() + static_cast<std::ptrdiff_t>(off + k));
    }
    out.push_back(0);
}

} // namespace

GifWriter::~GifWriter() { abort(); }

Status GifWriter::write(const void* p, usize n) noexcept {
    total_ += n;
    if (toMemory_) {
        const u8* b = static_cast<const u8*>(p);
        mem_.insert(mem_.end(), b, b + n);
        return OkStatus;
    }
    if (!f_) return Status{Errc::InvalidState, "gif fechado"};
    if (std::fwrite(p, 1, n, f_) != n) return Status{Errc::StorageFull, "falha ao gravar o GIF (armazenamento cheio?)"};
    return OkStatus;
}

namespace {
void put_le16(std::vector<u8>& o, u32 v) {
    o.push_back(static_cast<u8>(v));
    o.push_back(static_cast<u8>(v >> 8));
}
} // namespace

Status GifWriter::open_memory(u32 width, u32 height, bool transparency, GifDither dither) noexcept {
    toMemory_ = true;
    mem_.clear();
    return open(nullptr, width, height, transparency, dither);
}

Status GifWriter::open(const char* path, u32 width, u32 height, bool transparency, GifDither dither) noexcept {
    if (width == 0 || height == 0 || width > 65535 || height > 65535) return Status{Errc::InvalidArgument, "tamanho de GIF invalido"};
    if (!toMemory_) {
        if (!path || !*path) return Errc::InvalidArgument;
        path_ = path;
        f_ = std::fopen(path, "wb");
        if (!f_) return Status{Errc::IoError, "nao foi possivel criar o GIF"};
    }
    w_ = width;
    h_ = height;
    transparency_ = transparency;
    dither_ = dither;
    written_ = 0;
    total_ = 0;
    hasPending_ = false;
    std::vector<u8> head;
    const char* sig = "GIF89a";
    head.insert(head.end(), sig, sig + 6);
    put_le16(head, width);
    put_le16(head, height);
    head.push_back(0x00);   // sem tabela global (cada quadro traz a sua)
    head.push_back(0x00);   // fundo
    head.push_back(0x00);   // proporção de pixel
    // NETSCAPE2.0: repetir para sempre.
    static constexpr u8 kLoop[19] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
                                     0x03, 0x01, 0x00, 0x00, 0x00};
    head.insert(head.end(), kLoop, kLoop + 19);
    return write(head.data(), head.size());
}

Status GifWriter::add_frame(const u8* rgba, u32 delayCs) noexcept {
    if (!toMemory_ && !f_) return Status{Errc::InvalidState, "gif fechado"};
    if (!rgba) return Errc::InvalidArgument;
    const usize bytes = static_cast<usize>(w_) * h_ * 4;
    delayCs = std::max<u32>(2, delayCs);
    // Quadro igual ao anterior (trecho parado): soma o atraso, não grava outro.
    if (hasPending_ && pendingDelay_ + delayCs <= 65535 && std::memcmp(pending_.data(), rgba, bytes) == 0) {
        pendingDelay_ += delayCs;
        return OkStatus;
    }
    if (hasPending_) {
        if (const Status s = encode_pending(); !s.ok()) return s;
    }
    pending_.assign(rgba, rgba + bytes);
    pendingDelay_ = delayCs;
    hasPending_ = true;
    return OkStatus;
}

Status GifWriter::encode_pending() noexcept {
    const usize n = static_cast<usize>(w_) * h_;
    const u8* rgba = pending_.data();
    Palette pal;
    std::vector<u32> binOf;
    const u32 maxColors = transparency_ ? 255u : 256u;
    build_palette(rgba, n, maxColors, pal, binOf);
    const u32 transparentIndex = transparency_ ? pal.count : 0u;
    const u32 entries = transparency_ ? pal.count + 1 : pal.count;

    std::vector<u8> idx(n);
    const bool exact = pal.exact;
    if (exact || dither_ == GifDither::None) {
        Nearest near;
        if (!exact) near.reset(pal);
        for (usize i = 0; i < n; ++i) {
            const u8* p = rgba + i * 4;
            if (transparency_ && p[3] < 128) { idx[i] = static_cast<u8>(transparentIndex); continue; }
            if (exact) {
                const u32 key = (static_cast<u32>(p[0] >> 3) << 10) | (static_cast<u32>(p[1] >> 3) << 5) | (p[2] >> 3);
                idx[i] = static_cast<u8>(binOf[key]);
            } else {
                idx[i] = near.find(p[0], p[1], p[2]);
            }
        }
    } else if (dither_ == GifDither::Ordered) {
        Nearest near;
        near.reset(pal);
        for (u32 y = 0; y < h_; ++y) {
            for (u32 x = 0; x < w_; ++x) {
                const usize i = static_cast<usize>(y) * w_ + x;
                const u8* p = rgba + i * 4;
                if (transparency_ && p[3] < 128) { idx[i] = static_cast<u8>(transparentIndex); continue; }
                const i32 o = (static_cast<i32>(kBayer4[y & 3][x & 3]) * 2 - 15);   // −15…15
                idx[i] = near.find(std::clamp(p[0] + o, 0, 255), std::clamp(p[1] + o, 0, 255), std::clamp(p[2] + o, 0, 255));
            }
        }
    } else {
        // Floyd–Steinberg com 3/4 da força: reduz o "chuvisco" que anda entre
        // quadros de um GIF sem perder a suavidade dos degradês.
        Nearest near;
        near.reset(pal);
        std::vector<i32> err((static_cast<usize>(w_) + 2) * 2 * 3, 0);
        i32* rowA = err.data();
        i32* rowB = err.data() + (w_ + 2) * 3;
        for (u32 y = 0; y < h_; ++y) {
            std::fill(rowB, rowB + (w_ + 2) * 3, 0);
            for (u32 x = 0; x < w_; ++x) {
                const usize i = static_cast<usize>(y) * w_ + x;
                const u8* p = rgba + i * 4;
                if (transparency_ && p[3] < 128) { idx[i] = static_cast<u8>(transparentIndex); continue; }
                i32 c[3];
                for (u32 k = 0; k < 3; ++k) c[k] = std::clamp(p[k] + rowA[(x + 1) * 3 + k] / 16, 0, 255);
                const u8 q = near.find(c[0], c[1], c[2]);
                idx[i] = q;
                for (u32 k = 0; k < 3; ++k) {
                    const i32 e = (c[k] - pal.rgb[q * 3 + k]) * 3 / 4;
                    rowA[(x + 2) * 3 + k] += e * 7;
                    rowB[(x + 0) * 3 + k] += e * 3;
                    rowB[(x + 1) * 3 + k] += e * 5;
                    rowB[(x + 2) * 3 + k] += e * 1;
                }
            }
            std::swap(rowA, rowB);
        }
    }

    // Tabela local: potência de 2 que caiba as entradas usadas.
    u32 bits = 1;
    while ((1u << bits) < std::max<u32>(2, entries)) ++bits;
    scratch_.clear();
    scratch_.reserve(n / 2 + 1024);
    // Controle gráfico: atraso, descarte e transparência.
    scratch_.push_back(0x21);
    scratch_.push_back(0xF9);
    scratch_.push_back(0x04);
    // Com transparência, o quadro anterior sai antes do próximo (descarte 2):
    // senão o pixel transparente mostraria o quadro velho por baixo.
    const u8 disposal = transparency_ ? 2 : 1;
    scratch_.push_back(static_cast<u8>((disposal << 2) | (transparency_ ? 1 : 0)));
    put_le16(scratch_, pendingDelay_);
    scratch_.push_back(static_cast<u8>(transparentIndex));
    scratch_.push_back(0x00);
    // Descritor da imagem (quadro inteiro) com tabela local.
    scratch_.push_back(0x2C);
    put_le16(scratch_, 0);
    put_le16(scratch_, 0);
    put_le16(scratch_, w_);
    put_le16(scratch_, h_);
    scratch_.push_back(static_cast<u8>(0x80 | (bits - 1)));
    for (u32 i = 0; i < (1u << bits); ++i) {
        if (i < pal.count) {
            scratch_.push_back(pal.rgb[i * 3]);
            scratch_.push_back(pal.rgb[i * 3 + 1]);
            scratch_.push_back(pal.rgb[i * 3 + 2]);
        } else {
            scratch_.push_back(0); scratch_.push_back(0); scratch_.push_back(0);
        }
    }
    // O LZW do GIF usa no mínimo 2 bits; com 8 bits fixos, qualquer tabela serve.
    lzw_encode(idx.data(), n, scratch_);
    hasPending_ = false;
    ++written_;
    return write(scratch_.data(), scratch_.size());
}

Status GifWriter::finish() noexcept {
    if (!toMemory_ && !f_) return Status{Errc::InvalidState, "gif fechado"};
    if (hasPending_) {
        if (const Status s = encode_pending(); !s.ok()) { abort(); return s; }
    }
    const u8 trailer = 0x3B;
    if (const Status s = write(&trailer, 1); !s.ok()) { abort(); return s; }
    if (f_) {
        const bool ok = std::fflush(f_) == 0;
        std::fclose(f_);
        f_ = nullptr;
        if (!ok) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::u8path(path_), ec);
            return Status{Errc::StorageFull, "falha ao gravar o GIF (armazenamento cheio?)"};
        }
    }
    pending_.clear();
    pending_.shrink_to_fit();
    return OkStatus;
}

void GifWriter::abort() noexcept {
    if (f_) {
        std::fclose(f_);
        f_ = nullptr;
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(path_), ec);
    }
    hasPending_ = false;
}

// =============================================================================
// ZIP "stored"
// =============================================================================
namespace {
void put_le32(std::vector<u8>& o, u32 v) {
    o.push_back(static_cast<u8>(v));
    o.push_back(static_cast<u8>(v >> 8));
    o.push_back(static_cast<u8>(v >> 16));
    o.push_back(static_cast<u8>(v >> 24));
}
void dos_time(u16& time, u16& date) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    const int year = std::max(1980, tm.tm_year + 1900);
    time = static_cast<u16>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    date = static_cast<u16>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
}
} // namespace

StoredZipWriter::~StoredZipWriter() { abort(); }

Status StoredZipWriter::open(const char* path) noexcept {
    if (!path || !*path) return Errc::InvalidArgument;
    path_ = path;
    f_ = std::fopen(path, "wb");
    if (!f_) return Status{Errc::IoError, "nao foi possivel criar o .zip"};
    entries_.clear();
    offset_ = 0;
    return OkStatus;
}

Status StoredZipWriter::add(const std::string& name, const u8* data, usize size) noexcept {
    if (!f_) return Status{Errc::InvalidState, "zip fechado"};
    if (offset_ + size + 30 + name.size() > 0xFFFFFFF0ull || entries_.size() >= 0xFFFF)
        return Status{Errc::NotSupported, "sequencia maior que 4 GB"};
    Entry e;
    e.name = name;
    e.crc = crc32_update(0, data, size);
    e.size = static_cast<u32>(size);
    e.offset = static_cast<u32>(offset_);
    u16 t = 0, d = 0;
    dos_time(t, d);
    std::vector<u8> h;
    put_le32(h, 0x04034b50u);
    put_le16(h, 20);
    put_le16(h, 0x0800);   // nomes em UTF-8
    put_le16(h, 0);        // stored
    put_le16(h, t);
    put_le16(h, d);
    put_le32(h, e.crc);
    put_le32(h, e.size);
    put_le32(h, e.size);
    put_le16(h, static_cast<u32>(name.size()));
    put_le16(h, 0);
    h.insert(h.end(), name.begin(), name.end());
    if (std::fwrite(h.data(), 1, h.size(), f_) != h.size() || (size && std::fwrite(data, 1, size, f_) != size))
        return Status{Errc::StorageFull, "falha ao gravar o .zip (armazenamento cheio?)"};
    offset_ += h.size() + size;
    entries_.push_back(std::move(e));
    return OkStatus;
}

Status StoredZipWriter::finish() noexcept {
    if (!f_) return Status{Errc::InvalidState, "zip fechado"};
    u16 t = 0, d = 0;
    dos_time(t, d);
    std::vector<u8> cd;
    for (const Entry& e : entries_) {
        put_le32(cd, 0x02014b50u);
        put_le16(cd, 20);
        put_le16(cd, 20);
        put_le16(cd, 0x0800);
        put_le16(cd, 0);
        put_le16(cd, t);
        put_le16(cd, d);
        put_le32(cd, e.crc);
        put_le32(cd, e.size);
        put_le32(cd, e.size);
        put_le16(cd, static_cast<u32>(e.name.size()));
        put_le16(cd, 0);
        put_le16(cd, 0);
        put_le16(cd, 0);
        put_le16(cd, 0);
        put_le32(cd, 0);
        put_le32(cd, e.offset);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    const u64 cdOffset = offset_;
    put_le32(cd, 0x06054b50u);
    put_le16(cd, 0);
    put_le16(cd, 0);
    put_le16(cd, static_cast<u32>(entries_.size()));
    put_le16(cd, static_cast<u32>(entries_.size()));
    put_le32(cd, static_cast<u32>(cd.size() - 22));
    put_le32(cd, static_cast<u32>(cdOffset));
    put_le16(cd, 0);
    const bool ok = std::fwrite(cd.data(), 1, cd.size(), f_) == cd.size() && std::fflush(f_) == 0;
    std::fclose(f_);
    f_ = nullptr;
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(path_), ec);
        return Status{Errc::StorageFull, "falha ao gravar o .zip (armazenamento cheio?)"};
    }
    offset_ += cd.size();
    return OkStatus;
}

void StoredZipWriter::abort() noexcept {
    if (!f_) return;
    std::fclose(f_);
    f_ = nullptr;
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(path_), ec);
}

} // namespace aurea
