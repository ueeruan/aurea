// =============================================================================
//  Aurea / text / FontUnpack.cpp
//
//  Mais formatos de fonte para o texto 2D (0.1.8). O motor desenha sfnt
//  (TrueType/OpenType); na IMPORTAÇÃO os outros formatos viram arquivos sfnt
//  comuns ao lado do original, e daí em diante é uma fonte como outra
//  qualquer — projeto, pacote, export e iPhone/Android leem o mesmo arquivo:
//    * WOFF (1.0): cada tabela é zlib; sai um .ttf (ou .otf se for CFF).
//    * TTC / OTC (coleção): cada face vira um arquivo próprio
//      (`nome-1.ttf`, `nome-2.ttf`…), com as tabelas que ela usa.
//  TTF/OTF passam direto. WOFF2 (Brotli + transformações) não é aceito.
// =============================================================================
#include "aurea/text/FontManager.hpp"

#include "aurea/core/Log.hpp"

#include "stb_image.h"   // stbi_zlib_decode_buffer (zlib com cabeçalho, como no WOFF)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace aurea::text {
namespace {

u32 rd32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); }
u16 rd16(const u8* p) { return static_cast<u16>((p[0] << 8) | p[1]); }
void wr32(std::vector<u8>& v, usize at, u32 x) { v[at] = u8(x >> 24); v[at + 1] = u8(x >> 16); v[at + 2] = u8(x >> 8); v[at + 3] = u8(x); }
void wr16(std::vector<u8>& v, usize at, u16 x) { v[at] = u8(x >> 8); v[at + 1] = u8(x); }

bool read_all(const std::string& path, std::vector<u8>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    // Fonte maior que 64 MB não é fonte de texto de vídeo.
    if (size <= 12 || size > (64l << 20)) { std::fclose(f); return false; }
    out.resize(static_cast<usize>(size));
    const usize got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

bool write_all(const std::string& path, const std::vector<u8>& data) {
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    std::error_code ec;
    if (ok) std::filesystem::rename(tmp, path, ec);
    if (!ok || ec) { std::filesystem::remove(tmp, ec); return false; }
    return true;
}

struct Table { u32 tag, checksum; std::vector<u8> bytes; };

/// Monta um sfnt (cabeçalho, diretório em ordem de tag, tabelas alinhadas em 4).
std::vector<u8> build_sfnt(u32 flavor, std::vector<Table> tables) {
    std::sort(tables.begin(), tables.end(), [](const Table& a, const Table& b) { return a.tag < b.tag; });
    const u16 n = static_cast<u16>(tables.size());
    u16 entrySelector = 0;
    while ((2u << entrySelector) <= n) ++entrySelector;
    const u16 searchRange = static_cast<u16>((1u << entrySelector) * 16u);
    std::vector<u8> out(12 + 16 * static_cast<usize>(n), 0);
    wr32(out, 0, flavor);
    wr16(out, 4, n);
    wr16(out, 6, searchRange);
    wr16(out, 8, entrySelector);
    wr16(out, 10, static_cast<u16>(n * 16u - searchRange));
    for (u16 i = 0; i < n; ++i) {
        const usize at = out.size();
        out.insert(out.end(), tables[i].bytes.begin(), tables[i].bytes.end());
        while (out.size() % 4) out.push_back(0);
        const usize rec = 12 + 16 * static_cast<usize>(i);
        wr32(out, rec, tables[i].tag);
        wr32(out, rec + 4, tables[i].checksum);
        wr32(out, rec + 8, static_cast<u32>(at));
        wr32(out, rec + 12, static_cast<u32>(tables[i].bytes.size()));
    }
    return out;
}

bool woff_to_sfnt(const std::vector<u8>& in, std::vector<u8>& out, u32& flavor) {
    if (in.size() < 44 || rd32(in.data()) != 0x774F4646u) return false;   // 'wOFF'
    flavor = rd32(&in[4]);
    const u16 numTables = rd16(&in[12]);
    if (numTables == 0 || 44u + 20u * numTables > in.size()) return false;
    std::vector<Table> tables;
    tables.reserve(numTables);
    for (u16 i = 0; i < numTables; ++i) {
        const u8* r = &in[44 + 20u * i];
        const u32 tag = rd32(r), offset = rd32(r + 4), compLength = rd32(r + 8), origLength = rd32(r + 12), checksum = rd32(r + 16);
        if (static_cast<u64>(offset) + compLength > in.size() || origLength > (64u << 20) || compLength > origLength) return false;
        Table t{tag, checksum, {}};
        if (compLength == origLength) {
            t.bytes.assign(in.begin() + offset, in.begin() + offset + compLength);
        } else {
            t.bytes.resize(origLength);
            const int got = stbi_zlib_decode_buffer(reinterpret_cast<char*>(t.bytes.data()), static_cast<int>(origLength),
                                                    reinterpret_cast<const char*>(&in[offset]), static_cast<int>(compLength));
            if (got != static_cast<int>(origLength)) return false;
        }
        tables.push_back(std::move(t));
    }
    out = build_sfnt(flavor, std::move(tables));
    return true;
}

bool collection_face(const std::vector<u8>& in, u32 face, std::vector<u8>& out, u32& flavor) {
    if (in.size() < 16 || rd32(in.data()) != 0x74746366u) return false;   // 'ttcf'
    const u32 faces = rd32(&in[8]);
    if (face >= faces || 12u + 4u * (face + 1u) > in.size()) return false;
    const u32 base = rd32(&in[12 + 4u * face]);
    if (static_cast<u64>(base) + 12 > in.size()) return false;
    flavor = rd32(&in[base]);
    const u16 numTables = rd16(&in[base + 4]);
    if (static_cast<u64>(base) + 12 + 16ull * numTables > in.size()) return false;
    std::vector<Table> tables;
    for (u16 i = 0; i < numTables; ++i) {
        const u8* r = &in[base + 12 + 16u * i];
        const u32 tag = rd32(r), checksum = rd32(r + 4), offset = rd32(r + 8), length = rd32(r + 12);
        if (static_cast<u64>(offset) + length > in.size()) return false;
        tables.push_back(Table{tag, checksum, std::vector<u8>(in.begin() + offset, in.begin() + offset + length)});
    }
    out = build_sfnt(flavor, std::move(tables));
    return true;
}

std::string sibling(const std::string& path, const std::string& suffix, u32 flavor) {
    const std::filesystem::path p(path);
    const char* ext = flavor == 0x4F54544Fu ? ".otf" : ".ttf";   // 'OTTO' = CFF
    return (p.parent_path() / (p.stem().string() + suffix + ext)).string();
}

} // namespace

std::vector<std::string> unpack_font_file(const std::string& path) {
    std::vector<u8> data;
    if (!read_all(path, data)) return {};
    const u32 magic = rd32(data.data());
    if (magic == 0x774F4632u) {   // 'wOF2'
        AUREA_LOG_WARN("fonte: WOFF2 nao suportado (%s)", path.c_str());
        return {};
    }
    if (magic == 0x774F4646u) {   // WOFF
        std::vector<u8> sfnt;
        u32 flavor = 0;
        if (!woff_to_sfnt(data, sfnt, flavor)) return {};
        const std::string out = sibling(path, "", flavor);
        if (!write_all(out, sfnt)) return {};
        return {out};
    }
    if (magic == 0x74746366u) {   // TTC / OTC
        const u32 faces = std::min<u32>(rd32(&data[8]), 32);
        std::vector<std::string> outs;
        for (u32 i = 0; i < faces; ++i) {
            std::vector<u8> sfnt;
            u32 flavor = 0;
            if (!collection_face(data, i, sfnt, flavor)) continue;
            const std::string out = sibling(path, "-" + std::to_string(i + 1), flavor);
            if (write_all(out, sfnt)) outs.push_back(out);
        }
        return outs;
    }
    return {path};
}

} // namespace aurea::text
