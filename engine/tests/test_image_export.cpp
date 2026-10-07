// =============================================================================
//  Export como imagem: PNG (com alfa), sequência PNG num .zip e GIF animado.
//
//  Os codificadores são do motor (export/ImageEncode.cpp); a conferência usa
//  decodificadores INDEPENDENTES: o PNG e o zlib pelo stb_image (o mesmo que o
//  import de texturas usa) e o GIF por um leitor de LZW escrito aqui, seguindo
//  a especificação GIF89a — um erro do codificador não se esconde atrás do
//  próprio codificador.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/export/ImageEncode.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"

#define STBI_NO_STDIO
#include "../third_party/stb/stb_image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;

namespace {

std::vector<u8> read_file(const std::string& path) {
    std::vector<u8> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<usize>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

/// PNG → RGBA8 pelo stb_image. `channels` = canais do arquivo (3 ou 4).
bool decode_png(const std::vector<u8>& png, std::vector<u8>& rgba, int& w, int& h, int& channels) {
    u8* px = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &channels, 4);
    if (!px) return false;
    rgba.assign(px, px + static_cast<usize>(w) * h * 4);
    stbi_image_free(px);
    return true;
}

// --- Leitor de GIF89a (para o teste) ------------------------------------------
struct GifFrame {
    u32 delayCs = 0;
    u32 disposal = 0;
    bool transparent = false;
    std::vector<u8> rgba;
};
struct GifFile {
    u32 width = 0, height = 0;
    bool loopForever = false;
    bool trailer = false;
    std::vector<GifFrame> frames;
};

bool lzw_decode(const std::vector<u8>& data, u32 minCode, usize expected, std::vector<u8>& out) {
    const u32 clear = 1u << minCode, eoi = clear + 1;
    std::vector<std::vector<u8>> dict;
    auto reset = [&] {
        dict.clear();
        for (u32 i = 0; i < clear; ++i) dict.push_back({static_cast<u8>(i)});
        dict.push_back({});
        dict.push_back({});
    };
    reset();
    u32 codeSize = minCode + 1;
    i32 prev = -1;
    u64 bitPos = 0;
    const u64 totalBits = static_cast<u64>(data.size()) * 8;
    while (bitPos + codeSize <= totalBits) {
        u32 code = 0;
        for (u32 b = 0; b < codeSize; ++b, ++bitPos)
            code |= static_cast<u32>((data[bitPos >> 3] >> (bitPos & 7)) & 1u) << b;
        if (code == clear) { reset(); codeSize = minCode + 1; prev = -1; continue; }
        if (code == eoi) return out.size() == expected;
        std::vector<u8> entry;
        if (prev < 0) {
            if (code >= dict.size()) return false;
            entry = dict[code];
        } else if (code < dict.size()) {
            entry = dict[code];
            if (dict.size() < 4096) { auto s = dict[prev]; s.push_back(entry[0]); dict.push_back(std::move(s)); }
        } else if (code == dict.size()) {
            entry = dict[prev];
            entry.push_back(dict[prev][0]);
            if (dict.size() < 4096) dict.push_back(entry);
        } else {
            return false;
        }
        out.insert(out.end(), entry.begin(), entry.end());
        if (prev >= 0 && dict.size() == (1u << codeSize) && codeSize < 12) ++codeSize;
        if (prev < 0 && dict.size() == (1u << codeSize) && codeSize < 12) ++codeSize;
        prev = static_cast<i32>(code);
    }
    return false;   // sem EOI
}

bool parse_gif(const std::vector<u8>& g, GifFile& out) {
    if (g.size() < 13 || std::memcmp(g.data(), "GIF89a", 6) != 0) return false;
    out.width = g[6] | (g[7] << 8);
    out.height = g[8] | (g[9] << 8);
    usize p = 13;
    if (g[10] & 0x80) p += 3u * (1u << ((g[10] & 7) + 1));
    GifFrame pendingCtl;
    auto skip_blocks = [&]() { while (p < g.size() && g[p] != 0) p += g[p] + 1u; ++p; };
    while (p < g.size()) {
        const u8 b = g[p++];
        if (b == 0x3B) { out.trailer = true; return p == g.size(); }
        if (b == 0x21) {
            const u8 label = g[p++];
            if (label == 0xFF) {
                const u8 n = g[p];
                if (n == 11 && std::memcmp(&g[p + 1], "NETSCAPE2.0", 11) == 0 && g[p + 12] == 3 && g[p + 13] == 1)
                    out.loopForever = (g[p + 14] | (g[p + 15] << 8)) == 0;
                p += n + 1u;
                skip_blocks();
            } else if (label == 0xF9) {
                if (g[p] != 4) return false;
                pendingCtl.disposal = (g[p + 1] >> 2) & 7u;
                pendingCtl.transparent = (g[p + 1] & 1u) != 0;
                pendingCtl.delayCs = g[p + 2] | (g[p + 3] << 8);
                const u8 ti = g[p + 4];
                pendingCtl.rgba.assign(1, ti);   // índice transparente guardado de passagem
                p += 5;
                skip_blocks();
            } else {
                skip_blocks();
            }
            continue;
        }
        if (b != 0x2C) return false;
        const u32 w = g[p + 4] | (g[p + 5] << 8), h = g[p + 6] | (g[p + 7] << 8);
        const u8 packed = g[p + 8];
        p += 9;
        if (!(packed & 0x80)) return false;   // o codificador sempre manda tabela local
        const u32 entries = 1u << ((packed & 7) + 1);
        const usize table = p;
        p += 3u * entries;
        const u32 minCode = g[p++];
        std::vector<u8> data;
        while (p < g.size() && g[p] != 0) { data.insert(data.end(), g.begin() + p + 1, g.begin() + p + 1 + g[p]); p += g[p] + 1u; }
        ++p;
        std::vector<u8> idx;
        if (!lzw_decode(data, minCode, static_cast<usize>(w) * h, idx)) return false;
        GifFrame f = pendingCtl;
        const u32 ti = pendingCtl.rgba.empty() ? 0xFFFFu : pendingCtl.rgba[0];
        f.rgba.resize(static_cast<usize>(w) * h * 4);
        for (usize i = 0; i < idx.size(); ++i) {
            if (idx[i] >= entries) return false;
            const u8* c = &g[table + idx[i] * 3u];
            f.rgba[i * 4] = c[0]; f.rgba[i * 4 + 1] = c[1]; f.rgba[i * 4 + 2] = c[2];
            f.rgba[i * 4 + 3] = (f.transparent && idx[i] == ti) ? 0 : 255;
        }
        out.frames.push_back(std::move(f));
        pendingCtl = GifFrame{};
    }
    return false;
}

// --- Leitor de ZIP "stored" (diretório central) -------------------------------
struct ZipItem { std::string name; std::vector<u8> data; u32 crc = 0; };
u32 le32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32>(p[3]) << 24); }
u32 le16(const u8* p) { return p[0] | (p[1] << 8); }
bool parse_zip(const std::vector<u8>& z, std::vector<ZipItem>& items) {
    if (z.size() < 22) return false;
    const usize eocd = z.size() - 22;
    if (le32(&z[eocd]) != 0x06054b50u) return false;
    const u32 count = le16(&z[eocd + 10]);
    usize cd = le32(&z[eocd + 16]);
    for (u32 i = 0; i < count; ++i) {
        if (le32(&z[cd]) != 0x02014b50u || le16(&z[cd + 10]) != 0) return false;
        ZipItem it;
        it.crc = le32(&z[cd + 16]);
        const u32 size = le32(&z[cd + 20]);
        const u32 nameLen = le16(&z[cd + 28]), extra = le16(&z[cd + 30]), comment = le16(&z[cd + 32]);
        const u32 local = le32(&z[cd + 42]);
        it.name.assign(reinterpret_cast<const char*>(&z[cd + 46]), nameLen);
        if (le32(&z[local]) != 0x04034b50u) return false;
        const usize data = local + 30 + le16(&z[local + 26]) + le16(&z[local + 28]);
        it.data.assign(z.begin() + data, z.begin() + data + size);
        items.push_back(std::move(it));
        cd += 46u + nameLen + extra + comment;
    }
    return true;
}

std::vector<u8> test_image(u32 w, u32 h, bool alpha) {
    std::vector<u8> px(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px[(static_cast<usize>(y) * w + x) * 4];
            p[0] = static_cast<u8>(x * 255 / std::max<u32>(1, w - 1));
            p[1] = static_cast<u8>(y * 255 / std::max<u32>(1, h - 1));
            p[2] = static_cast<u8>((x / 8 + y / 8) % 2 ? 200 : 40);   // xadrez: repetição para o LZ77
            p[3] = alpha ? static_cast<u8>((x * 7 + y * 3) & 0xFF) : 255;
        }
    }
    return px;
}

std::string tmp_path(const char* name) {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "aurea-image-export";
    std::filesystem::create_directories(dir, ec);
    return (dir / name).string();
}

} // namespace

// =============================================================================
// Codificadores (sem GPU)
// =============================================================================
AUREA_TEST(ImageEncode, ZlibRoundTripsThroughAnIndependentInflater) {
    std::vector<u8> src;
    for (u32 i = 0; i < 300000; ++i) src.push_back(static_cast<u8>((i % 251) ^ ((i / 1000) * 37)));
    for (u32 i = 0; i < 70000; ++i) src.push_back(static_cast<u8>(std::rand() & 0xFF));   // trecho incompressível
    src.insert(src.end(), 5000, 7);                                                      // corrida longa
    std::vector<u8> z;
    zlib_compress(src.data(), src.size(), z);
    std::vector<u8> back(src.size() + 16);
    const int n = stbi_zlib_decode_buffer(reinterpret_cast<char*>(back.data()), static_cast<int>(back.size()),
                                          reinterpret_cast<const char*>(z.data()), static_cast<int>(z.size()));
    AUREA_CHECK_EQ(n, static_cast<int>(src.size()));
    AUREA_CHECK(std::memcmp(back.data(), src.data(), src.size()) == 0);
    AUREA_CHECK(z.size() < src.size() * 6 / 10);   // comprime de verdade
    // Vazio e 1 byte também são zlib válidos.
    for (usize len : {usize(0), usize(1), usize(2), usize(3)}) {
        std::vector<u8> tiny(len, 42), zz;
        zlib_compress(tiny.data(), tiny.size(), zz);
        char out[8] = {};
        AUREA_CHECK_EQ(stbi_zlib_decode_buffer(out, 8, reinterpret_cast<const char*>(zz.data()), static_cast<int>(zz.size())),
                       static_cast<int>(len));
    }
}

AUREA_TEST(ImageEncode, PngRoundTripKeepsEveryPixelAndAlpha) {
    for (bool alpha : {true, false}) {
        const u32 w = 173, h = 61;   // largura ímpar: nenhum filtro depende de alinhamento
        const std::vector<u8> src = test_image(w, h, alpha);
        std::vector<u8> png;
        AUREA_CHECK(encode_png(src.data(), w, h, alpha, png).ok());
        AUREA_CHECK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P');
        std::vector<u8> back;
        int bw = 0, bh = 0, ch = 0;
        AUREA_CHECK(decode_png(png, back, bw, bh, ch));
        AUREA_CHECK_EQ(bw, static_cast<int>(w));
        AUREA_CHECK_EQ(bh, static_cast<int>(h));
        AUREA_CHECK_EQ(ch, alpha ? 4 : 3);
        AUREA_CHECK(back == src);
        // Bem menor que o bruto (filtro + Huffman dinâmico).
        AUREA_CHECK(png.size() < static_cast<usize>(w) * h * (alpha ? 4 : 3));
    }
}

AUREA_TEST(ImageEncode, GifHasLoopFramesDelaysAndExactFlatColors) {
    const u32 w = 40, h = 24;
    GifWriter gif;
    AUREA_CHECK(gif.open_memory(w, h, true, GifDither::FloydSteinberg).ok());
    std::vector<std::vector<u8>> frames;
    for (u32 f = 0; f < 4; ++f) {
        std::vector<u8> px(static_cast<usize>(w) * h * 4);
        for (u32 i = 0; i < w * h; ++i) {
            const u32 x = i % w;
            u8* p = &px[i * 4];
            // Arte chapada: 3 cores + transparente; a faixa vermelha anda.
            const bool band = x >= f * 8 && x < f * 8 + 8;
            p[0] = band ? 230 : 20; p[1] = band ? 30 : 120; p[2] = band ? 40 : 220;
            p[3] = x >= 36 ? 0 : 255;
        }
        frames.push_back(px);
    }
    frames.insert(frames.begin() + 2, frames[1]);   // quadro repetido: vira atraso somado
    for (u32 i = 0; i < frames.size(); ++i) AUREA_CHECK(gif.add_frame(frames[i].data(), gif_frame_delay_cs(i, 30.0)).ok());
    AUREA_CHECK(gif.finish().ok());
    GifFile g;
    AUREA_CHECK(parse_gif(gif.bytes(), g));
    AUREA_CHECK(g.trailer);
    AUREA_CHECK(g.loopForever);
    AUREA_CHECK_EQ(g.width, w);
    AUREA_CHECK_EQ(g.height, h);
    AUREA_CHECK_EQ(g.frames.size(), static_cast<usize>(4));
    u32 total = 0;
    for (const GifFrame& f : g.frames) { total += f.delayCs; AUREA_CHECK(f.transparent); AUREA_CHECK_EQ(f.disposal, 2u); }
    AUREA_CHECK_EQ(total, static_cast<u32>(17));   // 5 quadros a 30 fps = 16,7 cs
    AUREA_CHECK_EQ(g.frames[1].delayCs, gif_frame_delay_cs(1, 30.0) + gif_frame_delay_cs(2, 30.0));
    // Poucas cores: paleta exata, sem dithering — os pixels voltam iguais.
    const std::vector<u8>* src[4] = {&frames[0], &frames[1], &frames[3], &frames[4]};
    for (u32 f = 0; f < 4; ++f) {
        bool same = true;
        for (u32 i = 0; i < w * h; ++i) {
            const u8* a = &(*src[f])[i * 4];
            const u8* b = &g.frames[f].rgba[i * 4];
            if (a[3] == 0) { same = same && b[3] == 0; continue; }
            same = same && b[3] == 255 && a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
        }
        AUREA_CHECK(same);
    }
    AUREA_CHECK(gif.bytes().size() < 4096);
}

AUREA_TEST(ImageEncode, GifPhotoUsesAPaletteAndStaysClose) {
    const u32 w = 96, h = 64;
    const std::vector<u8> src = test_image(w, h, false);   // degradê de milhares de cores
    for (GifDither d : {GifDither::FloydSteinberg, GifDither::Ordered, GifDither::None}) {
        GifWriter gif;
        AUREA_CHECK(gif.open_memory(w, h, false, d).ok());
        AUREA_CHECK(gif.add_frame(src.data(), 7).ok());
        AUREA_CHECK(gif.finish().ok());
        GifFile g;
        AUREA_CHECK(parse_gif(gif.bytes(), g));
        AUREA_CHECK_EQ(g.frames.size(), static_cast<usize>(1));
        AUREA_CHECK_EQ(g.frames[0].delayCs, 7u);
        AUREA_CHECK_EQ(g.frames[0].disposal, 1u);
        // Erro médio por canal pequeno (256 cores para um degradê RGB).
        f64 err = 0;
        for (usize i = 0; i < static_cast<usize>(w) * h; ++i)
            for (u32 c = 0; c < 3; ++c) err += std::abs(static_cast<int>(src[i * 4 + c]) - g.frames[0].rgba[i * 4 + c]);
        err /= static_cast<f64>(w) * h * 3;
        std::printf("(dither %u: erro medio %.2f, %zu bytes) ", static_cast<u32>(d), err, gif.bytes().size());
        AUREA_CHECK(err < 10.0);
        // Limite de tamanho: nunca mais que 1,5 byte por pixel + cabeçalhos.
        AUREA_CHECK(gif.bytes().size() < static_cast<usize>(w) * h * 3 / 2 + 1024);
    }
}

AUREA_TEST(ImageEncode, StoredZipHoldsEveryPngWithValidCrc) {
    const std::string path = tmp_path("seq-test.zip");
    StoredZipWriter zip;
    AUREA_CHECK(zip.open(path.c_str()).ok());
    constexpr u32 kFrames = 7;
    std::vector<std::vector<u8>> pngs;
    for (u32 i = 0; i < kFrames; ++i) {
        std::vector<u8> px = test_image(33, 17, i % 2 == 0);
        px[0] = static_cast<u8>(i * 30);
        std::vector<u8> png;
        AUREA_CHECK(encode_png(px.data(), 33, 17, i % 2 == 0, png).ok());
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%05u.png", i + 1);
        AUREA_CHECK(zip.add(name, png.data(), png.size()).ok());
        pngs.push_back(std::move(png));
    }
    AUREA_CHECK(zip.finish().ok());
    const std::vector<u8> z = read_file(path);
    std::vector<ZipItem> items;
    AUREA_CHECK(parse_zip(z, items));
    AUREA_CHECK_EQ(items.size(), static_cast<usize>(kFrames));
    for (u32 i = 0; i < items.size(); ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%05u.png", i + 1);
        AUREA_CHECK_EQ(items[i].name, std::string(name));
        AUREA_CHECK(items[i].data == pngs[i]);
        AUREA_CHECK_EQ(items[i].crc, crc32_update(0, items[i].data.data(), items[i].data.size()));
        std::vector<u8> rgba;
        int w = 0, h = 0, ch = 0;
        AUREA_CHECK(decode_png(items[i].data, rgba, w, h, ch));
        AUREA_CHECK_EQ(rgba[0], static_cast<u8>(i * 30));
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

AUREA_TEST(ImageEncode, PlanAndEstimateFollowOneRule) {
    ImageExportSettings s;
    // PNG: resolução cheia da composição, um quadro.
    ImageExportPlan p = plan_image_export(s, 1080, 1920, 30.0, 300, true);
    AUREA_CHECK_EQ(p.width, 1080u);
    AUREA_CHECK_EQ(p.height, 1920u);
    AUREA_CHECK_EQ(p.frames, 1u);
    AUREA_CHECK(p.alpha);
    // GIF: 480 de largura, 15 fps por padrão; 10 s → 150 quadros.
    s.format = ImageExportFormat::Gif;
    p = plan_image_export(s, 1920, 1080, 30.0, 300, false);
    AUREA_CHECK_EQ(p.width, 480u);
    AUREA_CHECK_EQ(p.height, 270u);
    AUREA_CHECK_EQ(p.frames, 150u);
    // Nunca amplia nem passa do fps da composição.
    s.maxWidth = 720;
    s.fps = 30.0;
    p = plan_image_export(s, 640, 360, 24.0, 48, false);
    AUREA_CHECK_EQ(p.width, 640u);
    AUREA_CHECK_NEAR(p.fps, 24.0, 1e-9);
    AUREA_CHECK_EQ(p.frames, 48u);
    // Sequência: fps da composição e lado menor pedido.
    s = ImageExportSettings{};
    s.format = ImageExportFormat::PngSequence;
    s.shortSide = 720;
    p = plan_image_export(s, 1920, 1080, 25.0, 50, false);
    AUREA_CHECK_EQ(p.width, 1280u);
    AUREA_CHECK_EQ(p.height, 720u);
    AUREA_CHECK_EQ(p.frames, 50u);
    // Estimativas crescem com quadros e alfa, e são plausíveis.
    const u64 gif = estimate_image_export_bytes(ImageExportFormat::Gif, 480, 270, 150, false);
    AUREA_CHECK(gif > 5'000'000ull && gif < 20'000'000ull);
    AUREA_CHECK(estimate_image_export_bytes(ImageExportFormat::Png, 1920, 1080, 1, true) >
                estimate_image_export_bytes(ImageExportFormat::Png, 1920, 1080, 1, false));
    AUREA_CHECK_EQ(gif_frame_delay_cs(0, 30.0) + gif_frame_delay_cs(1, 30.0) + gif_frame_delay_cs(2, 30.0), 10u);
}

// Beta: "sem fundo" nos Ajustes do projeto deixava tudo branco e o PNG nunca
// saía transparente. A escolha "Transparente" das telas chega como alfa 0; o
// motor guarda a flag, devolve alfa 0 e grava/lê a flag no arquivo.
AUREA_TEST(ImageEncode, TransparentBackgroundChoiceRoundTripsAndPlansAlpha) {
    auto created = Project::create_new(64, 36, 30.0, "fundo");
    AUREA_CHECK(created.ok());
    if (!created.ok()) return;
    Project p = std::move(*created);
    Composition* root = p.timeline().composition(p.timeline().root());
    AUREA_CHECK(root != nullptr);
    if (!root) return;
    AUREA_CHECK(!root->transparent_background());
    root->set_background_choice(Color{1, 1, 1, 0});
    AUREA_CHECK(root->transparent_background());
    AUREA_CHECK_EQ(root->background_choice().a, 0.0f);
    AUREA_CHECK_EQ(root->background_choice().r, 1.0f);   // a cor fica guardada
    const ImageExportPlan plan = plan_image_export(ImageExportSettings{}, 64, 36, 30.0, 30,
                                                   root->transparent_background());
    AUREA_CHECK(plan.alpha);

    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(p, SaveOptions{}, bytes).ok());
    Project back;
    AUREA_CHECK(ProjectSerializer::load_bytes(back, bytes.data(), bytes.size(), LoadOptions{}).ok());
    const Composition* r2 = back.timeline().composition(back.timeline().root());
    AUREA_CHECK(r2 != nullptr);
    if (r2) {
        AUREA_CHECK(r2->transparent_background());
        AUREA_CHECK_EQ(r2->background_choice().a, 0.0f);
        AUREA_CHECK_EQ(r2->background_choice().g, 1.0f);
    }

    // Uma cor opaca escolhida depois volta a valer (alfa 1, sem transparência).
    root->set_background_choice(Color{0.2f, 0.4f, 0.6f, 1});
    AUREA_CHECK(!root->transparent_background());
    AUREA_CHECK_EQ(root->background_choice().a, 1.0f);
    AUREA_CHECK_EQ(root->background().a, 1.0f);
}

// =============================================================================
// Motor inteiro (GPU): o quadro renderizado vira PNG/ZIP/GIF de verdade
// =============================================================================
#if defined(AUREA_TEST_VULKAN)

#include "SyntheticVideo.hpp"
#if defined(AUREA_TEST_GLES)
#include "GlesBackend.hpp"
namespace aurea { namespace vk = gles; }
#else
#include "VulkanBackend.hpp"
#endif
#include "aurea/Engine.hpp"
using namespace aurea::test;

#include <chrono>
#include <thread>

namespace {
bool image_gpu_ok() {
    static const bool ok = [] {
        vk::Backend b;
        BackendConfig c;
        c.enableValidation = false;
        const bool r = b.initialize(c).ok();
        b.shutdown();
        return r;
    }();
    return ok;
}

bool wait_export(Engine& e, int timeoutMs = 120000) {
    for (int i = 0; i < timeoutMs; ++i) {
        if (e.export_progress().finished) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

AUREA_TEST(ImageExport, CurrentFramePngKeepsTransparentBackground) {
    if (!image_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr).ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    comp->set_transparent_background(true);
    const LayerId id = comp->add_layer(LayerKind::Shape, "metade");
    Layer* l = comp->layer(id);
    l->shape.bounds = Rect{0, 0, 32, 36};
    l->shape.fillColor = Vec4{1, 0, 0, 1};
    l->transform.anchor = Vec3{16, 18, 0};
    l->transform.position = Vec3{16, 18, 0};

    ImageExportSettings s;
    s.format = ImageExportFormat::Png;
    const ImageExportPlan plan = e.query_image_export_plan(s);
    AUREA_CHECK_EQ(plan.width, 64u);
    AUREA_CHECK_EQ(plan.height, 36u);
    AUREA_CHECK(plan.alpha);
    const std::string path = tmp_path("quadro.png");
    AUREA_CHECK(e.start_image_export(s, path.c_str()).ok());
    AUREA_CHECK(wait_export(e));
    const Engine::ExportProgress p = e.export_progress();
    AUREA_CHECK(p.result == Errc::Ok);
    AUREA_CHECK_EQ(p.framesDone, 1u);
    std::vector<u8> rgba;
    int w = 0, h = 0, ch = 0;
    AUREA_CHECK(decode_png(read_file(path), rgba, w, h, ch));
    AUREA_CHECK_EQ(w, 64);
    AUREA_CHECK_EQ(h, 36);
    AUREA_CHECK_EQ(ch, 4);
    if (rgba.size() == 64u * 36u * 4u) {
        const u8* in = &rgba[(18 * 64 + 8) * 4];
        const u8* out = &rgba[(18 * 64 + 56) * 4];
        std::printf("(dentro %u,%u,%u,%u fora alfa %u) ", in[0], in[1], in[2], in[3], out[3]);
        AUREA_CHECK(in[0] > 240 && in[1] < 10 && in[2] < 10 && in[3] == 255);
        AUREA_CHECK_EQ(out[3], static_cast<u8>(0));
    }
    std::error_code ec2;
    std::filesystem::remove(path, ec2);
    e.shutdown();
}

// Beta: com "sem fundo" (fundo branco guardado, alfa 0) o quadro saía branco.
// O fundo escolhido como Transparente na folha "Novo projeto" (alfa 0) tem de
// sair com alfa 0 nos cantos e a forma semitransparente com a cor reta certa
// — sem o branco guardado vazando nas áreas transparentes.
AUREA_TEST(ImageExport, TransparentChoiceExportsStraightAlphaPng) {
    if (!image_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    const f32 transparentWhite[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr, transparentWhite).ok());
    {
        u64 id = 0; u32 w = 0, h = 0; f64 fps = 0; i64 dur = 0;
        f32 bg[4]{};
        AUREA_CHECK(e.query_composition(id, w, h, fps, dur, bg));
        AUREA_CHECK_EQ(bg[3], 0.0f);   // as telas leem "Transparente"
    }
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    AUREA_CHECK(comp->transparent_background());
    const LayerId id = comp->add_layer(LayerKind::Shape, "meia");
    Layer* l = comp->layer(id);
    l->shape.bounds = Rect{0, 0, 32, 36};
    l->shape.fillColor = Vec4{1, 0, 0, 1};
    l->transform.anchor = Vec3{16, 18, 0};
    l->transform.position = Vec3{16, 18, 0};
    l->transform.opacity = 0.5f;

    ImageExportSettings s;
    s.format = ImageExportFormat::Png;
    AUREA_CHECK(e.query_image_export_plan(s).alpha);
    const std::string path = tmp_path("quadro-transparente.png");
    AUREA_CHECK(e.start_image_export(s, path.c_str()).ok());
    AUREA_CHECK(wait_export(e));
    AUREA_CHECK(e.export_progress().result == Errc::Ok);
    std::vector<u8> rgba;
    int w = 0, h = 0, ch = 0;
    AUREA_CHECK(decode_png(read_file(path), rgba, w, h, ch));
    AUREA_CHECK_EQ(ch, 4);
    if (rgba.size() == 64u * 36u * 4u) {
        // A forma cobre a metade esquerda: os cantos da direita e o meio da
        // direita são fundo — alfa 0 e nada da cor branca guardada.
        for (const int px : {63, 35 * 64 + 63, 18 * 64 + 56}) {
            const u8* c = &rgba[static_cast<usize>(px) * 4];
            AUREA_CHECK_EQ(c[3], static_cast<u8>(0));
            AUREA_CHECK(c[0] == 0 && c[1] == 0 && c[2] == 0);
        }
        const u8* in = &rgba[(18 * 64 + 8) * 4];
        std::printf("(meia %u,%u,%u,%u) ", in[0], in[1], in[2], in[3]);
        AUREA_CHECK(in[3] >= 120 && in[3] <= 136);              // 50% de opacidade
        AUREA_CHECK(in[0] > 240 && in[1] < 10 && in[2] < 10);   // vermelho reto, sem branco somado
    }
    std::error_code ec2;
    std::filesystem::remove(path, ec2);
    e.shutdown();
}

AUREA_TEST(ImageExport, SequenceZipAndGifCarryEveryRenderedFrame) {
    if (!image_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 64;
    cfg.height = 36;
    cfg.frameCount = 300;
    cfg.pattern = SyntheticPattern::FrameGray;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.mediaFactory = &factory;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "sintetico";
    vi.displayName = "sintetico";
    AUREA_CHECK(e.import_video(vi).ok());
    Command d;
    d.type = CommandType::CompositionSetDuration;
    d.comp_duration.comp = e.project()->timeline().current();
    d.comp_duration.duration = FrameIndex{12};
    AUREA_CHECK(e.apply_command(d).ok());

    // Sequência: 12 quadros a 30 fps → 12 PNGs, cada um com o cinza do SEU quadro.
    ImageExportSettings s;
    s.format = ImageExportFormat::PngSequence;
    s.trimToContent = false;
    const std::string zipPath = tmp_path("seq.zip");
    AUREA_CHECK(e.start_image_export(s, zipPath.c_str()).ok());
    AUREA_CHECK(wait_export(e));
    AUREA_CHECK(e.export_progress().result == Errc::Ok);
    std::vector<ZipItem> items;
    AUREA_CHECK(parse_zip(read_file(zipPath), items));
    AUREA_CHECK_EQ(items.size(), static_cast<usize>(12));
    std::vector<u8> grays;
    for (const ZipItem& it : items) {
        std::vector<u8> rgba;
        int w = 0, h = 0, ch = 0;
        AUREA_CHECK(decode_png(it.data, rgba, w, h, ch));
        AUREA_CHECK_EQ(w, 64);
        AUREA_CHECK_EQ(ch, 3);   // fundo opaco: RGB, sem alfa
        if (!rgba.empty()) grays.push_back(rgba[(18 * 64 + 32) * 4 + 1]);
    }
    bool increasing = grays.size() == 12;
    for (usize i = 1; i < grays.size(); ++i) increasing = increasing && grays[i] > grays[i - 1];
    AUREA_CHECK(increasing);   // FrameGray: cinza sobe com o índice do quadro

    // GIF: 15 fps de um trecho de 12 quadros a 30 → 6 quadros, 32 px de largura.
    s.format = ImageExportFormat::Gif;
    s.maxWidth = 32;
    s.fps = 15.0;
    const std::string gifPath = tmp_path("anim.gif");
    AUREA_CHECK(e.start_image_export(s, gifPath.c_str()).ok());
    AUREA_CHECK(wait_export(e));
    AUREA_CHECK(e.export_progress().result == Errc::Ok);
    AUREA_CHECK_EQ(e.export_progress().framesTotal, 6u);
    const std::vector<u8> bytes = read_file(gifPath);
    GifFile g;
    AUREA_CHECK(parse_gif(bytes, g));
    AUREA_CHECK(g.loopForever);
    AUREA_CHECK_EQ(g.width, 32u);
    AUREA_CHECK_EQ(g.height, 18u);
    AUREA_CHECK_EQ(g.frames.size(), static_cast<usize>(6));
    u32 total = 0;
    for (const GifFrame& f : g.frames) total += f.delayCs;
    AUREA_CHECK_EQ(total, 40u);   // 6 quadros a 15 fps = 0,4 s
    AUREA_CHECK(bytes.size() <= estimate_image_export_bytes(ImageExportFormat::Gif, 32, 18, 6, false));

    // Cancelar no meio apaga o parcial.
    d.comp_duration.duration = FrameIndex{240};
    AUREA_CHECK(e.apply_command(d).ok());
    s.format = ImageExportFormat::PngSequence;
    const std::string cancelPath = tmp_path("cancel.zip");
    AUREA_CHECK(e.start_image_export(s, cancelPath.c_str()).ok());
    AUREA_CHECK(e.cancel_export().ok());
    AUREA_CHECK(wait_export(e));
    AUREA_CHECK(e.export_progress().result == Errc::Cancelled);
    AUREA_CHECK(!std::filesystem::exists(cancelPath));

    std::error_code ec2;
    std::filesystem::remove(zipPath, ec2);
    std::filesystem::remove(gifPath, ec2);
    e.shutdown();
}

#endif // AUREA_TEST_VULKAN
