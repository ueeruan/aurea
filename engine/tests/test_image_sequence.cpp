// =============================================================================
//  Sequência de imagens (media/ImageSequence.hpp, engine/EngineImageSequence.cpp)
//
//  PNGs de verdade escritos pelo `encode_png` do motor: a cor de cada quadro
//  diz QUAL quadro é (vermelho = 20 + 9·k) e o alfa é um degradê horizontal.
//  PNG de 16 bits por um escritor mínimo daqui (zlib do motor); EXR com alfa
//  pelo SaveEXRToMemory do tinyexr. Conferem: padrão, contagem e buracos;
//  receita; decoder (quadro ausente segura o anterior); antecipação tocando;
//  import (duração, quadro k na composição de outro fps); alfa reto x
//  pré-multiplicado x ignorado sobre um fundo colorido; EXR linear; reabrir
//  o projeto; snapshot do export e pacote do projeto com todos os quadros;
//  .zip (inclusive o da sequência PNG que o próprio app exporta).
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/export/ExportRecovery.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "aurea/media/ImageSequence.hpp"
#include "aurea/media/VideoSource.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/ProjectPackage.hpp"
#include "aurea/project/Serialization.hpp"
#include "tinyexr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace aurea;

namespace {

namespace fs = std::filesystem;

std::string u8s(const fs::path& p) {
    const auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

std::string fresh_dir(const std::string& name) {
    const fs::path d = fs::temp_directory_path() / "aurea_sequencia_teste" / fs::u8path(name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return u8s(d);
}

std::string join(const std::string& dir, const std::string& name) { return u8s(fs::u8path(dir) / fs::u8path(name)); }

bool write_bytes(const std::string& path, const std::vector<u8>& bytes) {
    return fileio::write_atomic(path, bytes.data(), bytes.size(), fileio::AtomicWriteOptions{false, false}).ok();
}

constexpr u32 kW = 32, kH = 16;

u8 frame_red(u32 k) { return static_cast<u8>(20 + 9 * k); }

/// Quadro k: R = 20 + 9k, G = 200, B = 40; alfa = degradê horizontal (0..255)
/// ou opaco.
std::vector<u8> frame_pixels(u32 k, bool gradient, u32 w = kW, u32 h = kH) {
    std::vector<u8> rgba(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u8* p = rgba.data() + (static_cast<usize>(y) * w + x) * 4;
            p[0] = frame_red(k); p[1] = 200; p[2] = 40;
            p[3] = gradient ? static_cast<u8>(std::lround(255.0 * x / (w - 1))) : 255;
        }
    return rgba;
}

bool write_png(const std::string& path, const std::vector<u8>& rgba, u32 w, u32 h, bool alpha) {
    std::vector<u8> png;
    if (!encode_png(rgba.data(), w, h, alpha, png).ok()) return false;
    return write_bytes(path, png);
}

void put32be(std::vector<u8>& v, u32 x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<u8>(x >> s));
}

/// PNG RGBA de 16 bits (IHDR, IDAT, IEND), filtro 0, zlib do motor.
std::vector<u8> png16(const std::vector<u16>& rgba, u32 w, u32 h) {
    std::vector<u8> raw;
    raw.reserve((static_cast<usize>(w) * 8 + 1) * h);
    for (u32 y = 0; y < h; ++y) {
        raw.push_back(0);
        for (u32 i = 0; i < w * 4; ++i) {
            const u16 v = rgba[static_cast<usize>(y) * w * 4 + i];
            raw.push_back(static_cast<u8>(v >> 8));
            raw.push_back(static_cast<u8>(v & 0xff));
        }
    }
    std::vector<u8> z;
    zlib_compress(raw.data(), raw.size(), z);
    std::vector<u8> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    auto chunk = [&](const char* type, const std::vector<u8>& data) {
        put32be(out, static_cast<u32>(data.size()));
        std::vector<u8> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        put32be(out, crc32_update(0, body.data(), body.size()));
    };
    std::vector<u8> ihdr;
    put32be(ihdr, w); put32be(ihdr, h);
    ihdr.push_back(16); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return out;
}

std::vector<u8> exr_rgba(const std::vector<f32>& rgba, u32 w, u32 h) {
    unsigned char* mem = nullptr;
    const char* err = nullptr;
    const int n = SaveEXRToMemory(rgba.data(), static_cast<int>(w), static_cast<int>(h), 4, 1, &mem, &err);
    if (n <= 0 || !mem) { if (err) FreeEXRErrorMessage(err); return {}; }
    std::vector<u8> out(mem, mem + n);
    std::free(mem);
    return out;
}

f32 half_f(u16 v) {
    const u32 sign = (v >> 15) & 1u, exp = (v >> 10) & 31u, man = v & 1023u;
    f32 f;
    if (exp == 0) f = std::ldexp(static_cast<f32>(man), -24);
    else if (exp == 31) f = 65504.0f;
    else f = std::ldexp(static_cast<f32>(man | 1024u), static_cast<int>(exp) - 25);
    return sign ? -f : f;
}

f32 to_linear(f32 e) { return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f); }
f32 to_srgb(f32 v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

/// Grava os quadros `numbers` (render_0001.png …) e devolve os caminhos.
std::vector<std::string> write_sequence(const std::string& dir, const std::vector<u32>& numbers, bool gradient,
                                        const char* prefix = "render_") {
    std::vector<std::string> out;
    for (u32 n : numbers) {
        char name[64];
        std::snprintf(name, sizeof(name), "%s%04u.png", prefix, n);
        const std::string path = join(dir, name);
        if (write_png(path, frame_pixels(n, gradient), kW, kH, gradient)) out.push_back(path);
    }
    return out;
}

std::vector<u32> range(u32 a, u32 b) {
    std::vector<u32> v;
    for (u32 i = a; i <= b; ++i) v.push_back(i);
    return v;
}

/// Primeiro quadro entregue a partir de `targetUs` (decoder cru).
FrameRef decode_at(VideoDecoderBackend& b, i64 targetUs) {
    FrameRef frame;
    if (!b.seek_to_keyframe(targetUs).ok()) return frame;
    i64 pts = 0;
    bool eos = false;
    if (!b.next_frame(targetUs, frame, pts, eos).ok()) frame.reset();
    return frame;
}

u8 red_of(const DecodedFrame& f, u32 x = kW / 2, u32 y = kH / 2) {
    return f.planes[0][static_cast<usize>(y) * f.strides[0] + static_cast<usize>(x) * 4];
}

} // namespace

// =============================================================================
// Receita e detecção (sem GPU)
// =============================================================================

AUREA_TEST(ImageSequence, RecipeRoundTripsWithGapsAndHex) {
    ImageSequenceSpec s;
    s.directory = "docs:Media/sequências;x=1";
    s.prefix = "shot.v2_";
    s.suffix = "_beauty.exr";
    s.padding = 4;
    s.first = 1;
    s.last = 240;
    s.present = {{1, 120}, {122, 240}};   // o 121 falta
    s.fps = 23.976023976;
    s.alpha = SequenceAlphaMode::Premultiplied;
    s.color = SequenceColorMode::Linear;
    const std::string recipe = encode_image_sequence(s);
    AUREA_CHECK(is_image_sequence(recipe));
    std::printf("(%s) ", recipe.c_str());
    ImageSequenceSpec d;
    AUREA_CHECK(decode_image_sequence(recipe, d));
    AUREA_CHECK(d.directory == s.directory);
    AUREA_CHECK(d.prefix == s.prefix);
    AUREA_CHECK(d.suffix == s.suffix);
    AUREA_CHECK_EQ(d.padding, 4u);
    AUREA_CHECK_EQ(d.first, static_cast<i64>(1));
    AUREA_CHECK_EQ(d.last, static_cast<i64>(240));
    AUREA_CHECK_EQ(d.present.size(), static_cast<usize>(2));
    AUREA_CHECK_NEAR(d.fps, 23.976023976, 1e-9);
    AUREA_CHECK(d.alpha == SequenceAlphaMode::Premultiplied);
    AUREA_CHECK(d.color == SequenceColorMode::Linear);
    AUREA_CHECK_EQ(d.frame_count(), static_cast<i64>(240));
    AUREA_CHECK_EQ(d.present_count(), static_cast<u64>(239));
    AUREA_CHECK(!d.has(121));
    AUREA_CHECK_EQ(d.held_number(121), static_cast<i64>(120));   // ausente segura o anterior
    AUREA_CHECK_EQ(d.held_number(122), static_cast<i64>(122));
    AUREA_CHECK(d.file_name(7) == "shot.v2_0007_beauty.exr");
    AUREA_CHECK(d.file_name(12345) == "shot.v2_12345_beauty.exr");
    AUREA_CHECK(d.display_pattern() == "shot.v2_####_beauty.exr");
    // Lixo e versões futuras sem os campos obrigatórios não viram sequência.
    AUREA_CHECK(!decode_image_sequence("aurea-seq:v1;d=zz;", d));
    AUREA_CHECK(!decode_image_sequence("aurea-shape3d:v1;k=0;", d));
    AUREA_CHECK(!decode_image_sequence(std::string(kImageSequenceScheme) + "v1;d=41;s=2e706e67;a=5;b=2;r=24;", d));
}

AUREA_TEST(ImageSequence, DetectsPatternCountGapsAndPadding) {
    std::vector<std::string> files = {
        "/x/render_0001.png", "/x/render_0002.png", "/x/render_0003.png", "/x/render_0005.png",
        "/x/render_0006.png", "/x/render_0009.png", "/x/notas.txt", "/x/capa.png", "/x/._render_0004.png",
        "/x/outro_01.png", "/x/outro_02.png", "/y/render_0001.png"};
    ImageSequenceSpec s;
    u32 candidates = 0;
    AUREA_CHECK(detect_image_sequence(files, "", s, &candidates));
    AUREA_CHECK_EQ(candidates, 3u);   // render_ em /x, outro_ em /x, render_ em /y
    AUREA_CHECK(s.prefix == "render_");
    AUREA_CHECK(s.suffix == ".png");
    AUREA_CHECK_EQ(s.padding, 4u);
    AUREA_CHECK_EQ(s.first, static_cast<i64>(1));
    AUREA_CHECK_EQ(s.last, static_cast<i64>(9));
    AUREA_CHECK_EQ(s.frame_count(), static_cast<i64>(9));
    AUREA_CHECK_EQ(s.present_count(), static_cast<u64>(6));
    AUREA_CHECK(!s.has(4) && !s.has(7) && !s.has(8) && s.has(9));
    AUREA_CHECK_EQ(s.held_number(8), static_cast<i64>(6));
    // Um quadro escolhido decide o grupo.
    AUREA_CHECK(detect_image_sequence(files, "/x/outro_02.png", s));
    AUREA_CHECK(s.prefix == "outro_");
    AUREA_CHECK_EQ(s.padding, 2u);
    // Um quadro escolhido sem número não puxa outra sequência da pasta.
    AUREA_CHECK(!detect_image_sequence(files, "/x/capa.png", s));
    // Sem zeros à esquerda e larguras diferentes: largura livre.
    AUREA_CHECK(detect_image_sequence({"/z/f1.jpg", "/z/f2.jpg", "/z/f10.jpg", "/z/f11.jpg"}, "", s));
    AUREA_CHECK_EQ(s.padding, 0u);
    AUREA_CHECK_EQ(s.first, static_cast<i64>(1));
    AUREA_CHECK_EQ(s.last, static_cast<i64>(11));
    AUREA_CHECK(s.file_name(10) == "f10.jpg");
    // Número depois do qual há texto ("_beauty") e Blender passando de 9999.
    AUREA_CHECK(detect_image_sequence({"/b/a_0999_beauty.exr", "/b/a_9999_beauty.exr", "/b/a_10000_beauty.exr"}, "", s));
    AUREA_CHECK(s.prefix == "a_");
    AUREA_CHECK(s.suffix == "_beauty.exr");
    AUREA_CHECK_EQ(s.padding, 4u);
    AUREA_CHECK_EQ(s.first, static_cast<i64>(999));
    AUREA_CHECK_EQ(s.last, static_cast<i64>(10000));
    AUREA_CHECK(s.file_name(10000) == "a_10000_beauty.exr");
    AUREA_CHECK(!detect_image_sequence({"/q/sem_numero.png", "/q/video.mp4"}, "", s));
}

AUREA_TEST(ImageSequence, DecoderMapsFramesAndHoldsMissingOnes) {
    const std::string dir = fresh_dir("decoder");
    const auto files = write_sequence(dir, {1, 2, 3, 5, 6, 7, 8}, true);
    AUREA_CHECK_EQ(files.size(), static_cast<usize>(7));
    ImageSequenceSpec spec;
    AUREA_CHECK(detect_image_sequence(files, "", spec));
    spec.fps = 24.0;
    spec.alpha = SequenceAlphaMode::Straight;
    auto backend = open_image_sequence(spec, dir, MediaPriority::Preview, nullptr);
    AUREA_CHECK(backend != nullptr);
    if (!backend) return;
    const VideoStreamInfo& info = backend->info();
    AUREA_CHECK_EQ(info.codedWidth, kW);
    AUREA_CHECK_EQ(info.codedHeight, kH);
    AUREA_CHECK(info.preciseFrameTiming);
    AUREA_CHECK(!info.hardwareDecoder);
    AUREA_CHECK_EQ(info.durationUs, static_cast<i64>(std::llround(8 * 1e6 / 24.0)));
    for (u32 k = 0; k < 8; ++k) {
        const i64 pts = static_cast<i64>(std::llround(k * 1e6 / 24.0));
        // Meio do intervalo: o quadro que está na tela nesse instante.
        const FrameRef f = decode_at(*backend, pts + 20000);
        AUREA_CHECK(f);
        if (!f) continue;
        AUREA_CHECK_EQ(f->ptsUs, pts);
        AUREA_CHECK(f->covers(pts + 20000));
        AUREA_CHECK(f->format == PixelFormat::RGBA8);
        AUREA_CHECK(f->alpha == FrameAlpha::Straight);
        // Número k+1; o 4 não existe: segura o 3.
        const u32 number = k + 1 == 4 ? 3 : k + 1;
        AUREA_CHECK_EQ(red_of(*f.get()), frame_red(number));
        // Alfa reto do arquivo, sem mexer: degradê.
        AUREA_CHECK_EQ(f->planes[0][3], 0);
        AUREA_CHECK_EQ(f->planes[0][(kW - 1) * 4 + 3], 255);
    }
    // Sequencial até o fim: o último sai com fim de fluxo.
    AUREA_CHECK(backend->seek_to_keyframe(0).ok());
    u32 delivered = 0;
    bool eos = false;
    for (int guard = 0; guard < 20 && !eos; ++guard) {
        FrameRef f;
        i64 pts = 0;
        AUREA_CHECK(backend->next_frame(-1, f, pts, eos).ok());
        if (f) ++delivered;
    }
    AUREA_CHECK_EQ(delivered, 8u);
    AUREA_CHECK(eos);
    // Arquivo apagado depois do import: segura o anterior em vez de sumir
    // (decoder novo: nada da antecipação do anterior).
    backend.reset();
    std::error_code ec;
    fs::remove(fs::u8path(join(dir, "render_0006.png")), ec);
    backend = open_image_sequence(spec, dir, MediaPriority::Preview, nullptr);
    AUREA_CHECK(backend != nullptr);
    if (!backend) return;
    const FrameRef held = decode_at(*backend, static_cast<i64>(std::llround(5 * 1e6 / 24.0)) + 100);
    AUREA_CHECK(held);
    if (held) AUREA_CHECK_EQ(red_of(*held.get()), frame_red(5));
}

AUREA_TEST(ImageSequence, SixteenBitPngAndExrKeepPrecisionAndAlpha) {
    const std::string dir = fresh_dir("largos");
    // PNG 16 bits: valores que o 8 bits não representa.
    std::vector<u16> wide(static_cast<usize>(kW) * kH * 4);
    for (u32 i = 0; i < kW * kH; ++i) {
        wide[i * 4 + 0] = 1000; wide[i * 4 + 1] = 32768; wide[i * 4 + 2] = 65535;
        wide[i * 4 + 3] = static_cast<u16>((i % kW) * 65535 / (kW - 1));
    }
    const std::string p16 = join(dir, "wide_0001.png");
    AUREA_CHECK(write_bytes(p16, png16(wide, kW, kH)));
    SequenceFrameHeader header;
    AUREA_CHECK(read_sequence_frame_header(p16, header).ok());
    AUREA_CHECK_EQ(header.bitDepth, 16u);
    AUREA_CHECK(header.hasAlpha);
    SequencePixels px;
    AUREA_CHECK(decode_sequence_frame(p16, px).ok());
    AUREA_CHECK(px.format == PixelFormat::RGBA16F);
    AUREA_CHECK_EQ(px.width, kW);
    if (px.bytes.size() == static_cast<usize>(kW) * kH * 8) {
        const u16* h = reinterpret_cast<const u16*>(px.bytes.data());
        AUREA_CHECK_NEAR(half_f(h[0]), 1000.0 / 65535.0, 2e-5);
        AUREA_CHECK_NEAR(half_f(h[1]), 32768.0 / 65535.0, 5e-4);
        AUREA_CHECK_NEAR(half_f(h[2]), 1.0, 1e-6);
        AUREA_CHECK_NEAR(half_f(h[3]), 0.0, 1e-6);
        AUREA_CHECK_NEAR(half_f(h[(kW - 1) * 4 + 3]), 1.0, 1e-6);
    }
    // EXR meia precisão com alfa (linear, pré-multiplicado), valor acima de 1.
    std::vector<f32> lin(static_cast<usize>(kW) * kH * 4);
    for (u32 i = 0; i < kW * kH; ++i) {
        lin[i * 4 + 0] = 2.5f * 0.5f; lin[i * 4 + 1] = 0.25f * 0.5f; lin[i * 4 + 2] = 0.0f; lin[i * 4 + 3] = 0.5f;
    }
    const std::string pexr = join(dir, "beauty_0001.exr");
    AUREA_CHECK(write_bytes(pexr, exr_rgba(lin, kW, kH)));
    AUREA_CHECK(read_sequence_frame_header(pexr, header).ok());
    AUREA_CHECK(header.exr);
    AUREA_CHECK(header.hasAlpha);
    AUREA_CHECK_EQ(header.width, kW);
    AUREA_CHECK(decode_sequence_frame(pexr, px).ok());
    AUREA_CHECK(px.format == PixelFormat::RGBA16F);
    AUREA_CHECK(px.hasAlpha);
    if (px.bytes.size() == static_cast<usize>(kW) * kH * 8) {
        const u16* h = reinterpret_cast<const u16*>(px.bytes.data());
        AUREA_CHECK_NEAR(half_f(h[0]), 1.25, 1e-3);   // acima de 1: nada cortado
        AUREA_CHECK_NEAR(half_f(h[1]), 0.125, 1e-3);
        AUREA_CHECK_NEAR(half_f(h[3]), 0.5, 1e-3);    // alfa do EXR preservado
    }
    // Lado acima do teto: reduzido por fator inteiro, mesma proporção.
    const u32 bigW = kImageSequenceMaxSide + 512, bigH = 8;
    std::vector<u8> big(static_cast<usize>(bigW) * bigH * 4, 255);
    const std::string pbig = join(dir, "big_0001.png");
    AUREA_CHECK(write_png(pbig, big, bigW, bigH, true));
    AUREA_CHECK(decode_sequence_frame(pbig, px).ok());
    AUREA_CHECK(px.width <= kImageSequenceMaxSide);
    AUREA_CHECK_EQ(px.width, bigW / 2);
}

AUREA_TEST(ImageSequence, PlaybackDecodeAheadDeliversFreshFrames) {
    const std::string dir = fresh_dir("antecipa");
    const auto files = write_sequence(dir, range(1, 48), false);
    AUREA_CHECK_EQ(files.size(), static_cast<usize>(48));
    ImageSequenceSpec spec;
    AUREA_CHECK(detect_image_sequence(files, "", spec));
    spec.fps = 24.0;
    spec.alpha = SequenceAlphaMode::Ignore;
    const u64 aheadBefore = image_sequence_ahead_decodes();
    {
        // Dois quadros seguidos = tocando: os próximos arquivos já vão para as
        // threads de antecipação.
        auto direct = open_image_sequence(spec, dir, MediaPriority::Preview, nullptr);
        AUREA_CHECK(direct != nullptr);
        if (!direct) return;
        AUREA_CHECK(direct->seek_to_keyframe(0).ok());
        for (int i = 0; i < 2; ++i) {
            FrameRef f;
            i64 pts = 0;
            bool eos = false;
            AUREA_CHECK(direct->next_frame(-1, f, pts, eos).ok() && f);
        }
        for (int i = 0; i < 200 && image_sequence_ahead_decodes() < aheadBefore + 3; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        AUREA_CHECK(image_sequence_ahead_decodes() >= aheadBefore + 3);
        // O próximo pedido sai da antecipação, com os pixels certos.
        FrameRef f;
        i64 pts = 0;
        bool eos = false;
        AUREA_CHECK(direct->next_frame(-1, f, pts, eos).ok() && f);
        if (f) AUREA_CHECK_EQ(red_of(*f.get()), frame_red(3));
    }
    auto backend = open_image_sequence(spec, dir, MediaPriority::Preview, nullptr);
    AUREA_CHECK(backend != nullptr);
    if (!backend) return;
    VideoSource source(std::move(backend), MediaPriority::Preview);
    source.start();
    const auto pts = [](u32 k) { return static_cast<i64>(std::llround(k * 1e6 / 24.0)); };
    u32 fresh = 0;
    for (u32 k = 0; k < 40; ++k) {
        DecodeRequest r;
        r.targetUs = pts(k) + 1000;
        r.mode = DecodeMode::Playback;
        r.direction = 1;
        source.request(r);
        // "Pouco tempo": um quadro a 24 fps.
        if (!source.wait_for(r.targetUs, 42)) (void)source.wait_for(r.targetUs, 2000);
        bool exact = false;
        const FrameRef f = source.frame_for(r.targetUs, &exact, 1);
        if (!(f && exact)) {
            const VideoSource::Stats st = source.stats();
            std::printf("(quadro %u ausente: tem %lld, pedidos %llu, seeks %llu, entregues %llu, descartados %llu, cache %u, fim %d) ",
                        k, f ? static_cast<long long>(f->ptsUs) : -1ll, static_cast<unsigned long long>(st.requests),
                        static_cast<unsigned long long>(st.seeks), static_cast<unsigned long long>(st.framesDelivered),
                        static_cast<unsigned long long>(st.framesDiscarded), st.cache.frames, st.endOfStream ? 1 : 0);
        }
        AUREA_CHECK(f && exact);
        if (f && exact && f->ptsUs == pts(k) && red_of(*f.get()) == frame_red(k + 1)) ++fresh;
    }
    AUREA_CHECK_EQ(fresh, 40u);   // nunca o quadro de antes
    // Os próximos arquivos foram decodificados antes de serem pedidos.
    AUREA_CHECK(image_sequence_ahead_decodes() > aheadBefore);
    const VideoSource::Stats st = source.stats();
    AUREA_CHECK(st.framesDelivered >= 40u);
    source.stop();
}

AUREA_TEST(ImageSequence, FullHdPngPlaybackKeepsUpInRealTime) {
    // 1920×1080 com alfa, degradê com ruído (comprime como um render, não como
    // uma cor lisa). Dois conteúdos alternados: dá para ver se o quadro é o certo.
    constexpr u32 W = 1920, H = 1080;
    const std::string dir = fresh_dir("fullhd");
    std::vector<u8> png[2];
    for (u32 v = 0; v < 2; ++v) {
        std::vector<u8> rgba(static_cast<usize>(W) * H * 4);
        u32 seed = 12345 + v;
        for (u32 y = 0; y < H; ++y)
            for (u32 x = 0; x < W; ++x) {
                seed = seed * 1664525u + 1013904223u;
                u8* p = rgba.data() + (static_cast<usize>(y) * W + x) * 4;
                p[0] = static_cast<u8>(v ? 200 : 40);
                p[1] = static_cast<u8>((x * 255 / W + ((seed >> 24) & 7)) & 255);
                p[2] = static_cast<u8>((y * 255 / H + ((seed >> 16) & 7)) & 255);
                p[3] = static_cast<u8>(std::min<u32>(255, (x + y) * 255 / (W + H) + ((seed >> 8) & 3)));
            }
        AUREA_CHECK(encode_png(rgba.data(), W, H, true, png[v]).ok());
    }
    constexpr u32 kFrames = 60;
    std::vector<std::string> files;
    for (u32 n = 1; n <= kFrames; ++n) {
        char name[32];
        std::snprintf(name, sizeof(name), "hd_%04u.png", n);
        files.push_back(join(dir, name));
        AUREA_CHECK(write_bytes(files.back(), png[n % 2]));
    }
    // Um quadro numa thread só: o custo que a antecipação divide.
    SequencePixels px;
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(decode_sequence_frame(files[0], px).ok());
    const f64 singleMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ImageSequenceSpec spec;
    AUREA_CHECK(detect_image_sequence(files, "", spec));
    // Toca os 60 quadros em tempo real na taxa dada; devolve os que estavam
    // prontos (exatos e certos) no vsync de cada um.
    const auto play = [&](f64 fps, u32& onTime, u32& right, u64& ahead) {
        spec.fps = fps;
        auto backend = open_image_sequence(spec, dir, MediaPriority::Preview, nullptr);
        AUREA_CHECK(backend != nullptr);
        if (!backend) return;
        const u64 aheadBefore = image_sequence_ahead_decodes();
        VideoSource source(std::move(backend), MediaPriority::Preview);
        source.start();
        const auto pts = [&](u32 k) { return static_cast<i64>(std::llround(k * 1e6 / fps)); };
        // Aquece no primeiro quadro (o preview parado mostra o quadro antes do play).
        DecodeRequest still;
        still.mode = DecodeMode::Still;
        source.request(still);
        AUREA_CHECK(source.wait_for(0, 2000));
        onTime = right = 0;
        const auto start = std::chrono::steady_clock::now();
        for (u32 k = 0; k < kFrames; ++k) {
            DecodeRequest r;
            r.targetUs = pts(k);
            r.mode = DecodeMode::Playback;
            r.direction = 1;
            source.request(r);
            // O quadro k vai para a tela no vsync seguinte (k+1)/fps.
            std::this_thread::sleep_until(start + std::chrono::microseconds(pts(k + 1)));
            bool exact = false;
            const FrameRef f = source.frame_for(r.targetUs, &exact, 1);
            if (f && exact) {
                ++onTime;
                if (f->planes[0][0] == ((k + 1) % 2 ? 200 : 40)) ++right;
            }
        }
        ahead = image_sequence_ahead_decodes() - aheadBefore;
        // Atrasado, o decoder pula para o quadro da vez (não entrega os velhos).
        if (fps <= 30.0) AUREA_CHECK(source.stats().framesDelivered >= kFrames);
        source.stop();
    };
    // Vazão sustentada: cada quadro pedido assim que o anterior chega.
    f64 throughputFps = 0.0;
    {
        spec.fps = 60.0;
        auto backend = open_image_sequence(spec, dir, MediaPriority::Preview, nullptr);
        AUREA_CHECK(backend != nullptr);
        if (backend) {
            VideoSource source(std::move(backend), MediaPriority::Preview);
            source.start();
            const auto t = std::chrono::steady_clock::now();
            u32 got = 0;
            for (u32 k = 0; k < kFrames; ++k) {
                DecodeRequest r;
                r.targetUs = static_cast<i64>(std::llround(k * 1e6 / 60.0));
                r.mode = DecodeMode::Playback;
                r.direction = 1;
                source.request(r);
                got += source.wait_for(r.targetUs, 5000) ? 1u : 0u;
            }
            throughputFps = kFrames / std::chrono::duration<f64>(std::chrono::steady_clock::now() - t).count();
            AUREA_CHECK_EQ(got, kFrames);
            source.stop();
        }
    }
    u32 onTime30 = 0, right30 = 0, onTime60 = 0, right60 = 0;
    u64 ahead30 = 0, ahead60 = 0;
    play(30.0, onTime30, right30, ahead30);
    play(60.0, onTime60, right60, ahead60);
    std::printf("(1080p RGBA: %.1f ms por quadro numa thread, vazao %.0f quadros/s; a tempo %u/%u a 30 fps (%llu antecipados), "
                "%u/%u a 60 fps (%llu)) ",
                singleMs, throughputFps, onTime30, kFrames, static_cast<unsigned long long>(ahead30), onTime60, kFrames,
                static_cast<unsigned long long>(ahead60));
    AUREA_CHECK_EQ(right30, onTime30);           // a tempo e o quadro certo
    AUREA_CHECK_EQ(right60, onTime60);
    AUREA_CHECK(onTime30 * 4 >= kFrames * 3);    // ≥ 75% a 30 (máquina de teste ocupada tolera alguns)
}

AUREA_TEST(ImageSequence, ProbeFindsSiblingsFolderAndReportsGaps) {
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.mediaFactory = &factory;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    const std::string dir = fresh_dir("sonda");
    const auto files = write_sequence(dir, {1, 2, 3, 5, 6, 9, 10}, true);
    AUREA_CHECK(write_png(join(dir, "capa.png"), frame_pixels(0, false), kW, kH, false));
    for (const std::vector<std::string>& input : {std::vector<std::string>{files[2]}, std::vector<std::string>{dir}, files}) {
        const auto r = e.probe_image_sequence(input);
        AUREA_CHECK(r.ok());
        if (!r.ok()) continue;
        AUREA_CHECK(r->pattern == "render_####.png");
        AUREA_CHECK(r->format == "png");
        AUREA_CHECK_EQ(r->first, static_cast<i64>(1));
        AUREA_CHECK_EQ(r->last, static_cast<i64>(10));
        AUREA_CHECK_EQ(r->frameCount, 10u);
        AUREA_CHECK_EQ(r->presentCount, 7u);
        AUREA_CHECK_EQ(r->missingCount, 3u);
        AUREA_CHECK(r->missing == (std::vector<i64>{4, 7, 8}));
        AUREA_CHECK_EQ(r->width, kW);
        AUREA_CHECK_EQ(r->height, kH);
        AUREA_CHECK(r->hasAlpha);
        AUREA_CHECK_EQ(r->bitDepth, 8u);
        AUREA_CHECK(!r->linear);
        AUREA_CHECK_NEAR(r->suggestedFps, 24.0, 1e-9);
        AUREA_CHECK(r->suggestedAlpha == SequenceAlphaMode::Straight);
        AUREA_CHECK(r->suggestedColor == SequenceColorMode::Srgb);
        AUREA_CHECK(r->totalBytes > 0);
    }
    AUREA_CHECK(e.probe_image_sequence({join(dir, "nao_existe_0001.png")}).code() == Errc::MediaSourceMissing);
    AUREA_CHECK(e.probe_image_sequence({join(dir, "capa.png"), join(dir, "capa.png")}).code() == Errc::UnsupportedFormat);
    // Sem projeto aberto, importar recusa; não cria nada.
    ImageSequenceImport req;
    req.paths = files;
    AUREA_CHECK(e.import_image_sequence(req).code() == Errc::InvalidState);
    e.shutdown();
}

AUREA_TEST(ImageSequence, ExportSnapshotAndProjectPackageCarryEveryFrame) {
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory(cfg);
    const std::string root = fresh_dir("pacote");
    const std::string docs = join(root, "docs");
    fs::create_directories(fs::u8path(docs));
    const std::string frames = join(docs, "Media/sequences/blender");
    fs::create_directories(fs::u8path(frames));
    const auto files = write_sequence(frames, {1, 2, 3, 4, 6, 7, 8}, true);
    AUREA_CHECK_EQ(files.size(), static_cast<usize>(7));
    Engine e;
    EngineConfig ec;
    ec.mediaFactory = &factory;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    ec.documentsDirectory = docs;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(kW, kH, 30.0, "pacote").ok());
    ImageSequenceImport req;
    req.paths = files;
    req.fps = 24.0;
    const auto layer = e.import_image_sequence(req);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) return;
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* l = comp->layer(LayerId::unpack(*layer));
    AUREA_CHECK(l && l->kind == LayerKind::Video);
    const Asset* a = l ? e.project()->asset(l->source) : nullptr;
    AUREA_CHECK(a != nullptr);
    if (!a) return;
    ImageSequenceSpec spec;
    AUREA_CHECK(decode_image_sequence(a->sourcePath, spec));
    // Dentro de Documentos: gravado relativo (sobrevive à troca do contêiner).
    AUREA_CHECK(spec.directory == "docs:Media/sequences/blender");
    AUREA_CHECK_EQ(a->video.frameCount.value, static_cast<i64>(8));
    AUREA_CHECK_NEAR(a->video.fps, 24.0, 1e-9);
    AUREA_CHECK_EQ(l->end.value - l->start.value, static_cast<i64>(10));   // 8 quadros a 24 fps = 10 a 30 fps
    AUREA_CHECK_EQ(a->audio.channels, 0u);

    // Snapshot do export: cada quadro presente vira dependência; a receita
    // congelada aponta para a pasta absoluta.
    {
        std::unique_ptr<Project> frozen;
        export_recovery::Package package;
        const Status s = export_recovery::capture(*e.project(), e.project()->timeline().current(), ExportSettings{},
            [&](const std::string& p) {
                if (p.rfind("docs:", 0) == 0) return u8s(fs::u8path(docs) / fs::u8path(p.substr(5)));
                return p;
            }, frozen, package, true);
        AUREA_CHECK(s.ok());
        u32 found = 0;
        for (const auto& f : files) {
            const std::string want = u8s(fs::absolute(fs::u8path(f)).lexically_normal());
            for (const auto& d : package.dependencies) if (d.path == want) { ++found; break; }
        }
        AUREA_CHECK_EQ(found, 7u);
        if (s.ok()) AUREA_CHECK(export_recovery::fingerprint_sources(package).ok());
        bool frozenAbsolute = false;
        if (frozen) frozen->for_each_asset([&](AssetId, const Asset& asset) {
            ImageSequenceSpec f;
            if (decode_image_sequence(asset.sourcePath, f)) frozenAbsolute = fs::u8path(f.directory).is_absolute();
        });
        AUREA_CHECK(frozenAbsolute);
    }

    // Pacote do projeto: lista os 7 arquivos; ler o pacote põe a sequência numa
    // pasta própria com os nomes originais e religa a receita.
    const std::string project = join(root, "seq.aurea");
    AUREA_CHECK(e.save_project(project.c_str()).ok());
    std::vector<package::MediaRef> media;
    AUREA_CHECK(e.project_file_media(project.c_str(), media).ok());
    AUREA_CHECK_EQ(media.size(), static_cast<usize>(7));
    std::vector<package::MediaFile> include;
    for (const auto& m : media) {
        AUREA_CHECK(fileio::exists(m.resolved));
        include.push_back({m.stored, m.resolved, m.name});
    }
    const std::string pkg = join(root, "seq.aureaproj");
    package::ExportResult written;
    AUREA_CHECK(package::write_package(project, pkg, "seq", "teste", include, &written).ok());
    AUREA_CHECK_EQ(written.included, 7u);
    // Uma foto solta com o mesmo nome de um quadro, na pasta de mídia de destino:
    // a sequência não pode virar "render_0001-2.png".
    const std::string mediaDir = join(root, "importado");
    fs::create_directories(fs::u8path(mediaDir));
    AUREA_CHECK(write_png(join(mediaDir, "render_0001.png"), frame_pixels(30, false), kW, kH, false));
    package::ImportResult imported;
    const std::string reopened = join(root, "reaberto.aurea");
    AUREA_CHECK(package::read_package(pkg, reopened, mediaDir, imported).ok());
    AUREA_CHECK_EQ(imported.relinked, 7u);
    AUREA_CHECK_EQ(imported.missing, 0u);
    Project p;
    LoadOptions o;
    AUREA_CHECK(ProjectSerializer::load(p, reopened, o, nullptr, nullptr).ok());
    bool relinked = false;
    p.for_each_asset([&](AssetId, const Asset& asset) {
        ImageSequenceSpec s;
        if (!decode_image_sequence(asset.sourcePath, s)) return;
        relinked = fs::u8path(s.directory).parent_path() == fs::u8path(mediaDir);
        for (i64 n : s.present_numbers()) relinked = relinked && fileio::exists(join(s.directory, s.file_name(n)));
        relinked = relinked && s.present_count() == 7 && s.frame_count() == 8;
    });
    AUREA_CHECK(relinked);
    e.shutdown();
}

// 0.3.0, app com.aurea.aurea: a cópia congelada do export grava a PASTA dos
// quadros com o caminho absoluto do app (/data/user/0/com.aurea.aurea/files/
// projetos/…). O resolvedor de caminhos antigos só aceitava arquivo regular e
// devolvia "" para a pasta: o decoder da sequência nunca abria e o export
// parava no 1o quadro dela, igual em todo degrau da escada.
AUREA_TEST(ImageSequence, AndroidReleaseAbsoluteFolderResolves) {
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory(cfg);
    const std::string root = fresh_dir("pacote-android");
    const std::string docs = join(root, "files/projetos");
    const std::string frames = join(docs, "Media/sequences/seq-1");
    fs::create_directories(fs::u8path(frames));
    const auto files = write_sequence(frames, {1, 2, 3}, false);
    Engine e;
    EngineConfig ec;
    ec.mediaFactory = &factory;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    ec.documentsDirectory = docs;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(kW, kH, 30.0, "android").ok());
    ImageSequenceImport req;
    req.paths = files;
    const auto layer = e.import_image_sequence(req);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) return;
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* l = comp->layer(LayerId::unpack(*layer));
    Asset* a = l ? e.project()->asset(l->source) : nullptr;
    AUREA_CHECK(a != nullptr);
    if (!a) return;
    ImageSequenceSpec spec;
    AUREA_CHECK(decode_image_sequence(a->sourcePath, spec));
    const std::string want = u8s(fs::weakly_canonical(fs::u8path(frames)) / fs::u8path(spec.file_name(spec.first)));
    for (const char* legacy : {"/data/user/0/com.aurea.aurea/files/projetos/Media/sequences/seq-1",
                               "/data/data/com.aurea.aurea/files/projetos/Media/sequences/seq-1"}) {
        spec.directory = legacy;   // o que capture_snapshot grava no app de produção
        a->sourcePath = encode_image_sequence(spec);
        const std::string got = e.layer_source_path(*layer);
        AUREA_CHECK_MSG(!got.empty(), legacy);
        std::error_code err;
        AUREA_CHECK_MSG(fs::equivalent(fs::u8path(got), fs::u8path(want), err), legacy);
    }
    // Pasta que não existe continua ausente.
    spec.directory = "/data/user/0/com.aurea.aurea/files/projetos/Media/sequences/outra";
    a->sourcePath = encode_image_sequence(spec);
    AUREA_CHECK(e.layer_source_path(*layer).empty());
    e.shutdown();
}

// =============================================================================
// Na GPU: import, mapeamento de tempo, alfa, reabrir, .zip
// =============================================================================
#if defined(AUREA_TEST_VULKAN)
#if defined(AUREA_TEST_GLES)
#include "GlesBackend.hpp"
namespace aurea { namespace vk = gles; }
#else
#include "VulkanBackend.hpp"
#endif

namespace {

bool sequence_gpu_ok() {
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

struct Rig {
    test::SyntheticConfig cfg;
    test::SyntheticFactory factory{cfg};
    Engine e;
    TextureHandle target{};
    std::vector<u16> half;
    u32 w = kW, h = kH;

    bool start(const std::string& docs, const f32* background = nullptr, u32 width = kW, u32 height = kH, f64 fps = 30.0) {
        w = width; h = height;
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.mediaFactory = &factory;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ec.documentsDirectory = docs;
        if (!e.initialize(ec).ok() || !e.gpu()) return false;
        if (!e.new_project(w, h, fps, "seq", background).ok()) return false;
        TextureDesc d;
        d.width = w; d.height = h;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.transferSrc = true;
        auto t = e.gpu()->create_texture(d);
        if (!t.ok()) return false;
        target = *t;
        half.resize(static_cast<usize>(w) * h * 4);
        return true;
    }
    bool render_at(i64 frame) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{frame}, e.project()->timeline().composition(e.project()->timeline().current())->fps());
        if (e.submit_commands(&seek, 1) != 1) return false;
        if (!e.render_offscreen(target, w, h).ok()) return false;
        return e.gpu()->read_texture(target, half.data(), w * 8).ok();
    }
    /// Linear pré-multiplicado, como sai do alvo.
    std::array<f32, 4> at(u32 x, u32 y) const {
        std::array<f32, 4> v{};
        for (u32 c = 0; c < 4; ++c) v[c] = half_f(half[(static_cast<usize>(y) * w + x) * 4 + c]);
        return v;
    }
    /// Quadro da sequência que está na tela (pelo vermelho; fundo preto opaco).
    i32 shown() const {
        const f32 r = to_srgb(at(w / 2, h / 2)[0]) * 255.0f;
        return static_cast<i32>(std::lround((r - 20.0f) / 9.0f));
    }
    void stop() {
        if (target.valid() && e.gpu()) e.gpu()->destroy_texture(target);
        target = TextureHandle{};
        e.shutdown();
    }
};

} // namespace

AUREA_TEST(ImageSequence, ImportedClipShowsSourceFrameAtCompositionTime) {
    if (!sequence_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string dir = fresh_dir("tempo");
    const auto files = write_sequence(dir, range(1, 24), false);
    Rig rig;
    AUREA_CHECK(rig.start(""));
    ImageSequenceImport req;
    req.paths = files;
    req.fps = 24.0;   // sequência a 24 numa composição a 30
    const auto layer = rig.e.import_image_sequence(req);
    AUREA_CHECK(layer.ok());
    if (!layer.ok()) { rig.stop(); return; }
    const Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    const Layer* l = comp->layer(LayerId::unpack(*layer));
    AUREA_CHECK(l != nullptr);
    if (l) AUREA_CHECK_EQ(l->end.value - l->start.value, static_cast<i64>(30));   // 24 quadros / 24 fps = 1 s
    AUREA_CHECK_EQ(comp->duration().value, static_cast<i64>(30));
    for (i64 k = 0; k < 30; ++k) {
        AUREA_CHECK(rig.render_at(k));
        // Quadro da composição k → instante k/30 s → quadro floor(k·24/30) da fonte.
        const i32 want = static_cast<i32>(std::floor(static_cast<f64>(k) * 24.0 / 30.0 + 1e-9)) + 1;
        AUREA_CHECK_EQ(rig.shown(), want);
    }
    rig.stop();
}

AUREA_TEST(ImageSequence, AlphaStraightPremultipliedAndIgnoredCompositeCorrectly) {
    if (!sequence_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string dir = fresh_dir("alfa");
    // Cor (100, 60, 0) com alfa 128 em todos os pixels, e um degradê de alfa.
    std::vector<std::string> flat, ramp;
    for (u32 n = 1; n <= 3; ++n) {
        std::vector<u8> px(static_cast<usize>(kW) * kH * 4);
        for (usize i = 0; i < px.size(); i += 4) { px[i] = 100; px[i + 1] = 60; px[i + 2] = 0; px[i + 3] = 128; }
        char name[32];
        std::snprintf(name, sizeof(name), "flat_%04u.png", n);
        flat.push_back(join(dir, name));
        AUREA_CHECK(write_png(flat.back(), px, kW, kH, true));
        std::snprintf(name, sizeof(name), "ramp_%04u.png", n);
        ramp.push_back(join(dir, name));
        AUREA_CHECK(write_png(ramp.back(), frame_pixels(n, true), kW, kH, true));
    }
    const f32 blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    const f32 a = 128.0f / 255.0f;
    struct Case { SequenceAlphaMode mode; f32 r, g, b; };
    // Reto: cor linear × alfa + fundo × (1 − alfa).
    // Pré-multiplicado: a cor do arquivo já tem o alfa (100/128, 60/128 retos).
    // Ignorado: opaco, sem fundo.
    const Case cases[] = {
        {SequenceAlphaMode::Straight, to_linear(100 / 255.0f) * a, to_linear(60 / 255.0f) * a, 1.0f - a},
        {SequenceAlphaMode::Premultiplied, to_linear(100 / 128.0f) * a, to_linear(60 / 128.0f) * a, 1.0f - a},
        {SequenceAlphaMode::Ignore, to_linear(100 / 255.0f), to_linear(60 / 255.0f), 0.0f},
    };
    for (const Case& c : cases) {
        Rig rig;
        AUREA_CHECK(rig.start("", blue));
        ImageSequenceImport req;
        req.paths = flat;
        req.fps = 30.0;
        req.alpha = c.mode;
        AUREA_CHECK(rig.e.import_image_sequence(req).ok());
        AUREA_CHECK(rig.render_at(1));
        const auto v = rig.at(kW / 2, kH / 2);
        std::printf("(alfa %u: %.4f %.4f %.4f) ", static_cast<u32>(c.mode), v[0], v[1], v[2]);
        AUREA_CHECK_NEAR(v[0], c.r, 0.006);
        AUREA_CHECK_NEAR(v[1], c.g, 0.006);
        AUREA_CHECK_NEAR(v[2], c.b, 0.006);
        AUREA_CHECK_NEAR(v[3], 1.0, 1e-3);
        rig.stop();
    }
    // Degradê de alfa (Auto = reto): a mistura acompanha o alfa coluna a coluna.
    Rig rig;
    AUREA_CHECK(rig.start("", blue));
    ImageSequenceImport req;
    req.paths = ramp;
    req.fps = 30.0;
    AUREA_CHECK(rig.e.import_image_sequence(req).ok());
    AUREA_CHECK(rig.render_at(0));
    for (u32 x : {0u, 8u, 16u, 31u}) {
        const f32 alpha = std::round(255.0f * x / (kW - 1)) / 255.0f;
        const auto v = rig.at(x, kH / 2);
        AUREA_CHECK_NEAR(v[0], to_linear(frame_red(1) / 255.0f) * alpha, 0.006);
        AUREA_CHECK_NEAR(v[1], to_linear(200 / 255.0f) * alpha, 0.006);
        AUREA_CHECK_NEAR(v[2], to_linear(40 / 255.0f) * alpha + (1.0f - alpha), 0.006);
    }
    rig.stop();
}

AUREA_TEST(ImageSequence, LinearExrWithAlphaCompositesAsPremultiplied) {
    if (!sequence_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string dir = fresh_dir("exr");
    std::vector<std::string> files;
    for (u32 n = 1; n <= 2; ++n) {
        std::vector<f32> px(static_cast<usize>(kW) * kH * 4);
        for (usize i = 0; i < px.size(); i += 4) { px[i] = 0.6f * 0.5f; px[i + 1] = 0.1f * 0.5f; px[i + 2] = 0.0f; px[i + 3] = 0.5f; }
        char name[32];
        std::snprintf(name, sizeof(name), "beauty.%04u.exr", n);
        files.push_back(join(dir, name));
        AUREA_CHECK(write_bytes(files.back(), exr_rgba(px, kW, kH)));
    }
    const f32 blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    Rig rig;
    AUREA_CHECK(rig.start("", blue));
    const auto probe = rig.e.probe_image_sequence(files);
    AUREA_CHECK(probe.ok());
    if (probe.ok()) {
        AUREA_CHECK(probe->linear);
        AUREA_CHECK(probe->hasAlpha);
        AUREA_CHECK(probe->suggestedAlpha == SequenceAlphaMode::Premultiplied);
        AUREA_CHECK(probe->suggestedColor == SequenceColorMode::Linear);
        AUREA_CHECK(probe->pattern == "beauty.####.exr");
    }
    ImageSequenceImport req;
    req.paths = files;
    req.fps = 30.0;   // Auto: linear, pré-multiplicado
    AUREA_CHECK(rig.e.import_image_sequence(req).ok());
    AUREA_CHECK(rig.render_at(0));
    const auto v = rig.at(kW / 2, kH / 2);
    std::printf("(exr %.4f %.4f %.4f) ", v[0], v[1], v[2]);
    AUREA_CHECK_NEAR(v[0], 0.30, 0.004);   // sem curva sRGB, sem multiplicar de novo
    AUREA_CHECK_NEAR(v[1], 0.05, 0.004);
    AUREA_CHECK_NEAR(v[2], 0.50, 0.004);
    rig.stop();
}

AUREA_TEST(ImageSequence, MissingFrameHoldsPreviousAndReopenRendersTheSame) {
    if (!sequence_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string root = fresh_dir("reabrir");
    const std::string docs = join(root, "docs");
    const std::string frames = join(docs, "renders");
    fs::create_directories(fs::u8path(frames));
    const auto files = write_sequence(frames, {1, 2, 3, 5, 6}, false);
    std::vector<i32> before;
    const std::string project = join(root, "seq.aurea");
    {
        Rig rig;
        AUREA_CHECK(rig.start(docs, nullptr, kW, kH, 30.0));
        ImageSequenceImport req;
        req.paths = {files[0]};   // um quadro só: os irmãos vêm junto
        req.fps = 30.0;
        AUREA_CHECK(rig.e.import_image_sequence(req).ok());
        for (i64 k = 0; k < 6; ++k) {
            AUREA_CHECK(rig.render_at(k));
            before.push_back(rig.shown());
        }
        AUREA_CHECK(before == (std::vector<i32>{1, 2, 3, 3, 5, 6}));   // o 4 segura o 3
        AUREA_CHECK(rig.e.save_project(project.c_str()).ok());
        rig.stop();
    }
    // Outro motor, o mesmo projeto: os mesmos quadros.
    Rig rig;
    AUREA_CHECK(rig.start(docs, nullptr, kW, kH, 30.0));
    AUREA_CHECK(rig.e.load_project(project.c_str()).ok());
    std::vector<i32> after;
    for (i64 k = 0; k < 6; ++k) {
        AUREA_CHECK(rig.render_at(k));
        after.push_back(rig.shown());
    }
    AUREA_CHECK(after == before);
    rig.stop();
}

AUREA_TEST(ImageSequence, ZipOfFramesImportsIncludingTheAppsOwnExport) {
    if (!sequence_gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string root = fresh_dir("zip");
    const std::string docs = join(root, "docs");
    fs::create_directories(fs::u8path(docs));
    // O formato do export de sequência PNG do app: frame_00001.png, 1 em diante.
    const std::string zipPath = join(root, "sequencia.zip");
    {
        StoredZipWriter zip;
        AUREA_CHECK(zip.open(zipPath.c_str()).ok());
        for (u32 n = 1; n <= 6; ++n) {
            std::vector<u8> png;
            AUREA_CHECK(encode_png(frame_pixels(n, false).data(), kW, kH, false, png).ok());
            char name[32];
            std::snprintf(name, sizeof(name), "frame_%05u.png", n);
            AUREA_CHECK(zip.add(name, png.data(), png.size()).ok());
        }
        AUREA_CHECK(zip.finish().ok());
    }
    Rig rig;
    AUREA_CHECK(rig.start(docs));
    const auto probe = rig.e.probe_image_sequence({zipPath});
    if (!probe.ok()) std::printf("(zip: erro %d %.*s) ", static_cast<int>(probe.code()),
                                 static_cast<int>(probe.status().detail().size()), probe.status().detail().data());
    AUREA_CHECK(probe.ok());
    if (probe.ok()) {
        AUREA_CHECK(probe->fromArchive);
        AUREA_CHECK(probe->pattern == "frame_#####.png");
        AUREA_CHECK_EQ(probe->frameCount, 6u);
        AUREA_CHECK(!probe->hasAlpha);
        AUREA_CHECK(probe->suggestedAlpha == SequenceAlphaMode::Ignore);
        // Extraído dentro de Documentos/Media/sequences/<hash>/.
        AUREA_CHECK(probe->directory.find("sequences") != std::string::npos);
    }
    // O .zip que o app gravava até a 0.2.7 (tamanho do diretório central 10
    // bytes a menos no EOCD) continua abrindo.
    {
        std::vector<u8> bytes;
        AUREA_CHECK(fileio::read_all(zipPath, bytes, 64u << 20) && bytes.size() > 22);
        if (bytes.size() > 22) {
            u8* size = bytes.data() + bytes.size() - 22 + 12;
            const u32 v = (static_cast<u32>(size[0]) | (static_cast<u32>(size[1]) << 8) | (static_cast<u32>(size[2]) << 16)
                          | (static_cast<u32>(size[3]) << 24)) - 10;
            for (u32 i = 0; i < 4; ++i) size[i] = static_cast<u8>(v >> (8 * i));
        }
        const std::string old = join(root, "sequencia-antiga.zip");
        AUREA_CHECK(write_bytes(old, bytes));
        const auto legacy = rig.e.probe_image_sequence({old});
        AUREA_CHECK(legacy.ok());
        if (legacy.ok()) AUREA_CHECK_EQ(legacy->frameCount, 6u);
    }
    ImageSequenceImport req;
    req.paths = {zipPath};
    req.fps = 30.0;
    const auto layer = rig.e.import_image_sequence(req);
    AUREA_CHECK(layer.ok());
    if (layer.ok()) {
        const Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        const Layer* l = comp->layer(LayerId::unpack(*layer));
        const Asset* a = l ? rig.e.project()->asset(l->source) : nullptr;
        ImageSequenceSpec spec;
        AUREA_CHECK(a && decode_image_sequence(a->sourcePath, spec));
        AUREA_CHECK(spec.directory.rfind("docs:Media/sequences/", 0) == 0);
        for (i64 k : {0, 2, 5}) {
            AUREA_CHECK(rig.render_at(k));
            AUREA_CHECK_EQ(rig.shown(), static_cast<i32>(k + 1));
        }
    }
    rig.stop();
}

#endif // AUREA_TEST_VULKAN
