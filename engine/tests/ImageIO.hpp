// =============================================================================
//  PNG mínimo para os golden frames: RGBA8, deflate "stored" (sem compressão).
//
//  Sem dependência externa de propósito: o golden é um arquivo que qualquer
//  visualizador abre (dá para OLHAR o que o teste espera), e o leitor só
//  precisa entender os PNGs que este mesmo escritor produz.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace aurea::test {

struct Image8 {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;
    [[nodiscard]] const u8* at(u32 x, u32 y) const { return &rgba[(static_cast<usize>(y) * width + x) * 4]; }
};

namespace png_detail {

inline u32 crc(const u8* p, usize n, u32 c = 0xFFFFFFFFu) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; ++i) {
            u32 v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        ready = true;
    }
    for (usize i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

inline void put32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x >> 24));
    v.push_back(static_cast<u8>(x >> 16));
    v.push_back(static_cast<u8>(x >> 8));
    v.push_back(static_cast<u8>(x));
}

inline u32 get32(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) | p[3];
}

inline void chunk(std::vector<u8>& out, const char* type, const std::vector<u8>& data) {
    put32(out, static_cast<u32>(data.size()));
    std::vector<u8> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

} // namespace png_detail

inline bool write_png(const std::string& path, const Image8& img) {
    using namespace png_detail;
    std::vector<u8> raw;
    raw.reserve((static_cast<usize>(img.width) * 4 + 1) * img.height);
    for (u32 y = 0; y < img.height; ++y) {
        raw.push_back(0);   // filtro "nenhum"
        raw.insert(raw.end(), img.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<usize>(y) * img.width * 4),
                   img.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<usize>(y + 1) * img.width * 4));
    }
    std::vector<u8> z = {0x78, 0x01};
    usize pos = 0;
    while (pos < raw.size() || raw.empty()) {
        const usize n = std::min<usize>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<u8>(n & 0xFF));
        z.push_back(static_cast<u8>(n >> 8));
        z.push_back(static_cast<u8>(~n & 0xFF));
        z.push_back(static_cast<u8>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        if (raw.empty()) break;
    }
    u32 a = 1, b = 0;
    for (u8 c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32(z, (b << 16) | a);

    std::vector<u8> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<u8> ihdr;
    put32(ihdr, img.width);
    put32(ihdr, img.height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(file, "IHDR", ihdr);
    chunk(file, "IDAT", z);
    chunk(file, "IEND", {});
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    std::fclose(f);
    return ok;
}

inline bool read_png(const std::string& path, Image8& out) {
    using namespace png_detail;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<u8> file;
    u8 buf[4096];
    usize n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    if (file.size() < 8) return false;
    std::vector<u8> idat;
    usize p = 8;
    while (p + 8 <= file.size()) {
        const u32 len = get32(&file[p]);
        const char* type = reinterpret_cast<const char*>(&file[p + 4]);
        if (p + 12 + len > file.size()) return false;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            out.width = get32(&file[p + 8]);
            out.height = get32(&file[p + 12]);
            if (file[p + 16] != 8 || file[p + 17] != 6) return false;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), file.begin() + static_cast<std::ptrdiff_t>(p + 8),
                        file.begin() + static_cast<std::ptrdiff_t>(p + 8 + len));
        }
        p += 12 + len;
    }
    // Só blocos "stored" (os que o escritor gera).
    std::vector<u8> raw;
    usize q = 2;
    for (;;) {
        if (q + 5 > idat.size()) return false;
        const u8 hdr = idat[q];
        if ((hdr & 0x6) != 0) return false;
        const usize len = idat[q + 1] | (static_cast<usize>(idat[q + 2]) << 8);
        q += 5;
        raw.insert(raw.end(), idat.begin() + static_cast<std::ptrdiff_t>(q),
                   idat.begin() + static_cast<std::ptrdiff_t>(q + len));
        q += len;
        if (hdr & 1) break;
    }
    out.rgba.resize(static_cast<usize>(out.width) * out.height * 4);
    for (u32 y = 0; y < out.height; ++y) {
        const usize row = static_cast<usize>(y) * (static_cast<usize>(out.width) * 4 + 1);
        if (row >= raw.size() || raw[row] != 0) return false;
        std::memcpy(&out.rgba[static_cast<usize>(y) * out.width * 4], &raw[row + 1], static_cast<usize>(out.width) * 4);
    }
    return true;
}

} // namespace aurea::test
