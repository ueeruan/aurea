// Recria um vídeo de referência como projeto EDITÁVEL: formas, texto, vetores e
// keyframes comuns, usando só o que a pessoa teria no app. Roda apenas com
// AUREA_REF_VIDEO=<pasta>; a mídia fica em build/reference e nunca entra no app.
//
// Coordenadas: "quadrinho" de 280 × 158 (T = px da composição por px do quadrinho),
// o tamanho em que os quadros da referência foram medidos.
#include "TestFramework.hpp"

#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)

#include "ImageIO.hpp"
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/project/ProjectPackage.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/ShapeGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

constexpr f32 T = 1404.0f / 280.0f;
constexpr f32 M = 1404.0f / 490.0f;   // mosaico: medido em quadros de 490 de largura
constexpr f32 kW = 280.0f, kH = 158.0f;
constexpr const char* kDevice = "/data/user/0/com.aurea.aurea.debug/files/recriar/";

Vec4 hex(u32 rgb, f32 a = 1.0f) {
    return Vec4{((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a};
}
// Paleta do Instagram. kAccent em forma vira degradê; em texto, o rosa da marca.
const u32 kAccent = 0xE1306C, kCream = 0xFFF7F3, kNavy = 0x120A1E, kDeep = 0x2A0F45, kBlush = 0xFFE6F0, kPlum = 0x5B2A86, kBlue = 0x0095F6;
const u32 kWhite = 0xFFFFFF;

std::string svg_doc(int w, int h, const std::string& body);

struct V { f32 f, v; };

/// Só para gravar o áudio no projeto: a sondagem lê o cabeçalho do WAV.
class WavProbe final : public VideoSourceFactory {
public:
    std::string host;
    bool probe(const char*, MediaProbe& out) override {
        std::ifstream f(host, std::ios::binary);
        char h[44]{}; f.read(h, 44);
        if (!f) return false;
        const u32 rate = *reinterpret_cast<const u32*>(h + 24);
        const u32 bytes = static_cast<u32>(std::filesystem::file_size(host));
        const u32 perSecond = *reinterpret_cast<const u32*>(h + 28);
        out.hasAudio = true;
        out.audioSampleRate = rate;
        out.audioChannels = *reinterpret_cast<const u16*>(h + 22);
        out.audioDurationUs = static_cast<i64>(static_cast<f64>(bytes - 44) / perSecond * 1e6);
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return nullptr; }
};

struct B {
    Engine& e;
    std::string media;
    std::string font;
    std::vector<package::MediaFile> files;
    std::vector<std::pair<i64, i64>> spans;   // uma entrada por camada, na ordem em que nasceram

    Composition& comp() { auto& t = e.project()->timeline(); return *t.composition(t.current()); }
    Layer& L(u64 id) { return *comp().layer(LayerId::unpack(id)); }

    u64 place(u64 id, const char* name, i64 a, i64 b, u64 parent) {
        Layer& l = L(id);
        l.name = name;
        spans.emplace_back(a, b);
        l.start = FrameIndex{a};
        l.end = FrameIndex{b};
        if (parent) l.parent = LayerId::unpack(parent);
        return id;
    }
    void key(u64 id, TrackProperty p, std::initializer_list<V> vs, f32 mul = 1.0f, f32 add = 0.0f,
             u32 fx = kInvalidIndex, u32 param = 0) {
        Layer& l = L(id);
        Track& t = l.tracks.get_or_create(p, fx, param);
        for (const V& x : vs) (void)t.set(l.local_time(FrameIndex{static_cast<i64>(x.f)}), x.v * mul + add, Interpolation::Linear);
    }
    void kx(u64 id, std::initializer_list<V> vs, f32 add = 0.0f) { key(id, TrackProperty::PositionX, vs, T, add * T); }
    void ky(u64 id, std::initializer_list<V> vs, f32 add = 0.0f) { key(id, TrackProperty::PositionY, vs, T, add * T); }
    void ks(u64 id, std::initializer_list<V> vs) { key(id, TrackProperty::ScaleX, vs); key(id, TrackProperty::ScaleY, vs); }
    void kr(u64 id, std::initializer_list<V> vs) { key(id, TrackProperty::RotationZ, vs); }
    void ko(u64 id, std::initializer_list<V> vs) { key(id, TrackProperty::Opacity, vs); }
    /// Caixa animada: o motor mantém o centro; o pivô continua o da caixa parada.
    void ksize(u64 id, std::initializer_list<V> w, std::initializer_list<V> h) {
        key(id, TrackProperty::ShapeParam, w, T, 0.0f, 0, shape::kParamWidth);
        key(id, TrackProperty::ShapeParam, h, T, 0.0f, 0, shape::kParamHeight);
    }
    void kcorner(u64 id, std::initializer_list<V> r) { key(id, TrackProperty::ShapeParam, r, T, 0.0f, 0, shape::kParamCorner); }

    u64 shape(u32 type, const char* name, i64 a, i64 b, f32 cx, f32 cy, f32 w, f32 h, Vec4 color, f32 corner, f32 unit, u64 parent) {
        const u64 id = *e.add_shape(0);
        Layer& l = L(id);
        l.shape.shapeType = type;
        l.shape.bounds = Rect{0.0f, 0.0f, w * unit, h * unit};
        l.shape.cornerRadius = corner * unit;
        l.shape.fillColor = color;
        l.shape.filled = color.w > 0.0f;
        if (color.x == hex(kAccent).x && color.y == hex(kAccent).y && color.z == hex(kAccent).z) {
            l.shape.fillKind = 1;
            l.shape.gradColor0 = hex(0xFA7E1E);
            l.shape.gradColor1 = hex(0xD62976);
            l.shape.gradStart = Vec2{0.0f, 1.0f};
            l.shape.gradEnd = Vec2{1.0f, 0.0f};
        }
        l.transform.anchor = Vec3{w * unit * 0.5f, h * unit * 0.5f, 0.0f};
        l.transform.position = Vec3{cx * unit, cy * unit, 0.0f};
        return place(id, name, a, b, parent);
    }
    u64 rect(const char* n, i64 a, i64 b, f32 cx, f32 cy, f32 w, f32 h, u32 c, f32 r = 0.0f, u64 p = 0) { return shape(0, n, a, b, cx, cy, w, h, hex(c), r, T, p); }
    u64 dot(const char* n, i64 a, i64 b, f32 cx, f32 cy, f32 d, u32 c, u64 p = 0) { return shape(1, n, a, b, cx, cy, d, d, hex(c), 0.0f, T, p); }
    u64 bg(const char* n, i64 a, i64 b, u32 c) {
        if (c == kAccent) return svg(n, a, b, svg_doc(282, 160, "<rect x=\"0\" y=\"0\" width=\"282\" height=\"160\" fill=\"url(#ig)\"/>"), kW * 0.5f, kH * 0.5f, T);
        return rect(n, a, b, kW * 0.5f, kH * 0.5f, kW + 2.0f, kH + 2.0f, c);
    }
    u64 mrect(const char* n, i64 a, i64 b, f32 x0, f32 y0, f32 x1, f32 y1, u32 c, u64 p) { return shape(0, n, a, b, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, x1 - x0, y1 - y0, hex(c), 0.0f, M, p); }
    /// Grupo sem desenho: move o que estiver preso nele.
    u64 group(const char* n, i64 a, i64 b, u64 p = 0, f32 cx = kW * 0.5f, f32 cy = kH * 0.5f) { return shape(0, n, a, b, cx, cy, kW, kH, Vec4{0, 0, 0, 0}, 0.0f, T, p); }

    /// `align`: 0 = x é a borda esquerda, 1 = centro, 2 = borda direita. Em px do `unit`.
    u64 text(const char* n, i64 a, i64 b, const char* s, f32 x, f32 y, f32 size, u32 c, u32 align = 1, f32 unit = T, u64 p = 0, f32 track = -18.0f, f32 line = 1.0f) {
        const u64 id = *e.add_text(s);
        Layer& l = L(id);
        l.text.size = size * unit;
        l.text.color = hex(c);
        l.text.alignment = align;
        l.text.tracking = track;
        l.text.lineHeight = line;
        l.text.fontFamily = font;
        const auto f = aurea::text::FontManager::instance().font_for(l.text);
        const aurea::text::TextExtent ext = aurea::text::measure(*f, l.text);
        const f32 box = std::ceil(ext.width + 4.0f), high = std::ceil(ext.height + 4.0f);
        l.transform.anchor = Vec3{align == 0 ? 2.0f : align == 1 ? box * 0.5f : box - 2.0f, high * 0.5f, 0.0f};
        l.transform.position = Vec3{x * unit, y * unit, 0.0f};
        return place(id, n, a, b, p);
    }
    /// O Helvetica Medium da referência: contorno fino da própria cor (a fonte do aparelho só tem o regular).
    u64 heavy(u64 id, f32 amount = 0.009f) {
        Layer& l = L(id);
        l.text.strokeWidth = l.text.size * amount;
        l.text.strokeColor = l.text.color;
        return id;
    }
    u64 svg(const char* n, i64 a, i64 b, const std::string& s, f32 cx, f32 cy, f32 scale, u64 p = 0, f32 unit = T) {
        const u64 id = *e.import_svg(s, n);
        Layer& l = L(id);
        l.transform.position = Vec3{cx * unit, cy * unit, 0.0f};
        l.transform.scale = Vec3{scale, scale, 1.0f};
        return place(id, n, a, b, p);
    }
    u64 image(const char* n, i64 a, i64 b, const char* file, f32 cx, f32 cy, f32 w, u64 p = 0) {
        Image8 img;
        const std::string path = media + "/" + file;
        // PNG comum (o leitor dos testes só abre os PNGs que ele escreve): tamanho do IHDR, pixels do .rgba ao lado.
        std::ifstream header(path, std::ios::binary);
        unsigned char ihdr[24]{};
        header.read(reinterpret_cast<char*>(ihdr), 24);
        img.width = (u32{ihdr[16]} << 24) | (u32{ihdr[17]} << 16) | (u32{ihdr[18]} << 8) | ihdr[19];
        img.height = (u32{ihdr[20]} << 24) | (u32{ihdr[21]} << 16) | (u32{ihdr[22]} << 8) | ihdr[23];
        img.rgba.resize(static_cast<usize>(img.width) * img.height * 4);
        std::ifstream raw(path + ".rgba", std::ios::binary);
        raw.read(reinterpret_cast<char*>(img.rgba.data()), static_cast<std::streamsize>(img.rgba.size()));
        AUREA_CHECK(static_cast<bool>(raw));
        const std::string device = std::string(kDevice) + file;
        files.push_back(package::MediaFile{device, path, file});
        const u64 id = *e.import_image(img.rgba.data(), img.width, img.height, n, device.c_str());
        Layer& l = L(id);
        const f32 s = w * T / static_cast<f32>(img.width);
        l.transform.position = Vec3{cx * T, cy * T, 0.0f};
        l.transform.scale = Vec3{s, s, 1.0f};
        return place(id, n, a, b, p);
    }
};

std::string hexs(u32 c) { char b[8]; std::snprintf(b, sizeof b, "#%06X", c); return b; }

std::string svg_doc(int w, int h, const std::string& body) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(w) + "\" height=\"" + std::to_string(h)
         + "\" viewBox=\"0 0 " + std::to_string(w) + " " + std::to_string(h) + "\">"
         + "<defs><linearGradient id=\"ig\" x1=\"0\" y1=\"1\" x2=\"1\" y2=\"0\">"
           "<stop offset=\"0\" stop-color=\"#FEDA75\"/><stop offset=\"0.25\" stop-color=\"#FA7E1E\"/>"
           "<stop offset=\"0.5\" stop-color=\"#D62976\"/><stop offset=\"0.75\" stop-color=\"#962FBF\"/>"
           "<stop offset=\"1\" stop-color=\"#4F5BD5\"/></linearGradient></defs>"
         + body + "</svg>";
}
std::string stroke(u32 c, f32 w) {
    return " fill=\"none\" stroke=\"" + (c == kAccent ? std::string("url(#ig)") : hexs(c)) + "\" stroke-width=\"" + std::to_string(w) + "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>";
}
std::string check_svg(u32 c, f32 w = 13) { return svg_doc(100, 100, "<path d=\"M10 60 L32 88 L92 12\"" + stroke(c, w)); }
std::string double_check_svg(u32 c, f32 w = 13) {
    return svg_doc(150, 100, "<path d=\"M8 56 L32 88 L92 10\"" + stroke(c, w) + "<path d=\"M66 76 L76 88 L142 10\"" + stroke(c, w));
}
std::string instagram_svg(u32 c) {
    return svg_doc(100, 100, "<rect x=\"8\" y=\"8\" width=\"84\" height=\"84\" rx=\"24\" ry=\"24\"" + stroke(c, 8)
        + "<circle cx=\"50\" cy=\"50\" r=\"20\"" + stroke(c, 8)
        + "<circle cx=\"73\" cy=\"27\" r=\"6\" fill=\"" + (c == kAccent ? std::string("url(#ig)") : hexs(c)) + "\"/>");
}
std::string mic_svg(u32 c) {
    return svg_doc(100, 150, "<rect x=\"30\" y=\"6\" width=\"40\" height=\"90\" rx=\"20\" ry=\"20\"" + stroke(c, 7)
        + "<path d=\"M8 66 L8 74 C8 98 26 116 50 116 C74 116 92 98 92 74 L92 66\"" + stroke(c, 7)
        + "<path d=\"M50 116 L50 144\"" + stroke(c, 7));
}
std::string camera_svg(u32 c) {
    return svg_doc(130, 110, "<path d=\"M22 24 L38 24 L46 10 L84 10 L92 24 L108 24 C116 24 124 32 124 40 L124 86 C124 94 116 102 108 102 L22 102 C14 102 6 94 6 86 L6 40 C6 32 14 24 22 24 Z\"" + stroke(c, 7)
        + "<circle cx=\"65\" cy=\"60\" r=\"24\"" + stroke(c, 7));
}
std::string video_svg(u32 c) {
    return svg_doc(150, 90, "<rect x=\"6\" y=\"8\" width=\"96\" height=\"74\" rx=\"20\" ry=\"20\"" + stroke(c, 8)
        + "<path d=\"M112 38 L142 14 L142 76 L112 52 Z\"" + stroke(c, 8));
}

/// Silhueta da onda: um pico gaussiano sobre uma base pequena, com ruído fixo.
f32 bar_height(i32 i, f32 x, f32 peak, f32 width, f32 amp, f32 base) {
    const f32 g = std::exp(-(x - peak) * (x - peak) / (2.0f * width * width));
    const f32 n = 0.55f + 0.45f * std::fabs(std::sin(static_cast<f32>(i) * 2.399f));
    return std::max(3.5f, base + amp * g * n);
}

void scene_chat(B& b) {
    b.bg("Fundo degradê", 0, 10, kAccent);
    b.bg("Fundo creme", 10, 44, kCream);
    // Letras sobem em cascata (animador de texto de verdade) e encolhem para o balão.
    const u64 bubble = b.rect("Balão chat", 10, 44, 107, 79, 250, 119, kAccent, 14);
    b.L(bubble).shape.strokeColor = hex(kNavy); b.L(bubble).shape.strokeWidth = 4.0f;
    b.ksize(bubble, {{10, 250}, {12, 182}, {14, 155}, {16, 142}, {18, 135}, {20, 133}, {44, 133}},
                    {{10, 119}, {12, 82}, {14, 66}, {16, 58}, {18, 54}, {20, 53}, {44, 53}});
    b.kx(bubble, {{10, 107}, {12, 102}, {14, 99.5f}, {16, 98}, {18, 97.5f}, {44, 97.5f}});
    b.ky(bubble, {{10, 79}, {14, 78}, {16, 77}, {18, 74}, {20, 60.5f}, {22, 50}, {24, 46}, {26, 39.5f}, {28, 5}, {30, -14}, {32, -21}, {34, -23}, {36, -24}});
    const u64 chat = b.heavy(b.text("chat", 0, 44, "chat", 135, 70, 122, kWhite, 1, T, 0, -30.0f), 0.004f);
    b.kx(chat, {{0, 135}, {6, 135}, {8, 130}, {10, 107}, {12, 102}, {14, 99.5f}, {16, 98}, {18, 97.5f}, {44, 97.5f}});
    b.ky(chat, {{0, 260}, {2, 118}, {4, 88}, {6, 72}, {8, 70}, {10, 75}, {14, 76}, {16, 75}, {18, 72}, {20, 58.5f}, {22, 48}, {24, 44}, {26, 37.5f}, {28, 3}, {30, -16}, {32, -23}, {34, -25}, {36, -26}});
    b.ks(chat, {{0, 1}, {6, 1}, {8, 0.92f}, {10, 0.525f}, {12, 0.41f}, {14, 0.368f}, {16, 0.352f}, {20, 0.342f}, {44, 0.342f}});
    const i32 anim = b.e.add_text_animator(chat, kTextPropPosition);
    AUREA_CHECK(anim >= 0);
    b.L(chat).text.animators[0].position = Vec3{0.0f, 60.0f * T, 0.0f};
    b.L(chat).text.animators[0].selector.shape = 1;   // rampa: a última letra sobe por último
    b.key(chat, TrackProperty::TextAnimParam, {{0, 100}, {2, 100}, {4, 55}, {6, 0}}, 1.0f, 0.0f, 0, text::kSelAmount);

    const u64 with = b.rect("Balão with", 20, 44, 183, 128.5f, 134, 53, kDeep, 14);
    b.kx(with, {{20, 182}, {34, 183.5f}, {36, 188}, {38, 196}, {40, 215}, {42, 267}, {43, 300}});
    b.ky(with, {{20, 128.5f}, {22, 118.5f}, {24, 113.5f}, {26, 107.5f}, {28, 72.5f}, {30, 53.5f}, {32, 47.5f}, {34, 45.5f}, {36, 44.5f}});
    const u64 withT = b.heavy(b.text("with", 20, 44, "with", 183, 126, 38.5f, kAccent));
    b.kx(withT, {{20, 182}, {34, 183.5f}, {36, 188}, {38, 196}, {40, 215}, {42, 267}, {43, 300}});
    b.ky(withT, {{20, 126.5f}, {22, 116.5f}, {24, 111.5f}, {26, 105.5f}, {28, 70.5f}, {30, 51.5f}, {32, 45.5f}, {34, 43.5f}, {36, 42.5f}});

    const u64 people = b.rect("Balão people", 28, 44, 98, 141, 134, 53, kBlush, 14);
    b.L(people).shape.strokeColor = hex(kNavy); b.L(people).shape.strokeWidth = 3.0f;
    b.kx(people, {{28, 98}, {30, 98}, {32, 100}, {34, 104}, {36, 114}, {38, 132}, {40, 169}, {42, 264}});
    b.ky(people, {{28, 141}, {30, 122.5f}, {32, 117}, {34, 115}, {36, 114}});
    const u64 peopleT = b.heavy(b.text("people.", 28, 44, "people.", 98, 139, 35.5f, kNavy));
    b.kx(peopleT, {{28, 98}, {30, 98}, {32, 100}, {34, 104}, {36, 114}, {38, 132}, {40, 169}, {42, 264}});
    b.ky(peopleT, {{28, 139}, {30, 120.5f}, {32, 115}, {34, 113}, {36, 112}});
}

void scene_time(B& b) {
    b.bg("Fundo azul-noite", 44, 68, kNavy);
    const u64 c2 = b.svg("Check branco 2", 46, 52, check_svg(0xFFFFFF, 13), 159, 79, 0.88f * T);
    b.kx(c2, {{46, 159}, {48, 168}, {50, 173.5f}, {52, 176}});
    const u64 c1 = b.svg("Check branco 1", 44, 52, check_svg(0xFFFFFF, 13), 127, 79, 0.9f * T);
    b.kx(c1, {{44, 127}, {46, 79}, {48, 111}, {50, 123.5f}, {52, 130}});
    b.ks(c1, {{44, 0.9f * T}, {46, 1.05f * T}, {48, 1.0f * T}, {50, 0.96f * T}, {52, 0.93f * T}});

    const u64 blue = b.svg("Checks azuis", 52, 68, double_check_svg(kBlue, 13), 176, 80, 0.5f * T);
    b.kx(blue, {{52, 176}, {54, 184}, {56, 186.5f}, {58, 187}, {68, 188}});
    b.ky(blue, {{52, 80}, {54, 79.5f}, {62, 73}, {64, 72}, {66, 59.5f}, {68, 50}});
    b.ks(blue, {{52, 0.51f * T}, {54, 0.43f * T}, {56, 0.395f * T}, {58, 0.39f * T}, {68, 0.38f * T}});

    const auto clock = [&](const char* value, i64 from, i64 to) {
        const u64 t = b.text(value, from, to, value, 107, 83, 43, 0xFFFFFF, 1, T, 0, -18.0f);
        b.kx(t, {{52, 69.5f}, {54, 94.5f}, {56, 102.5f}, {58, 106}, {60, 107}, {68, 107}});
        b.ky(t, {{52, 104}, {54, 91}, {56, 87}, {58, 84}, {60, 82}, {62, 81}, {64, 76}, {66, 63}, {68, 55}});
        b.ks(t, {{52, 1.33f}, {54, 1.1f}, {56, 1.03f}, {58, 1}, {68, 1}});
        return t;
    };
    (void)clock("19:41", 52, 60);
    const u64 late = clock("19:42", 60, 68);
    AUREA_CHECK(b.e.add_text_animator(late, kTextPropPosition) >= 0);
    b.L(late).text.animators[0].position = Vec3{0.0f, 14.0f * T, 0.0f};
    b.L(late).text.animators[0].selector.start = 80.0f;
    b.key(late, TrackProperty::TextAnimParam, {{60, 100}, {62, 0}}, 1.0f, 0.0f, 0, text::kSelAmount);
}

void scene_languages(B& b) {
    b.bg("Fundo roxo", 68, 84, kDeep);
    const auto line = [&](const char* n, const char* s, std::initializer_list<V> ys, i64 a = 68, i64 e = 84, u32 c = kAccent) {
        const u64 id = b.heavy(b.text(n, a, e, s, 8, 0, 57, c, 0, T, 0, -12.0f), 0.006f);
        b.ky(id, ys);
    };
    line("こんにちは", "こんにちは", {{68, 26}, {70, -30}}, 68, 70);
    line("नमस्ते", "नमस्ते", {{68, 88}, {70, -90}}, 68, 70);
    line("你好", "你好", {{68, 138}, {70, -60}}, 68, 70);
    line("hola", "hola", {{70, 8}, {72, -30}}, 70, 72);
    line("hello branco", "hello", {{70, 51}, {72, 15}}, 70, 72, 0xFFFFFF);
    line("hello", "hello", {{72, 15}, {74, -14}, {76, -20}}, 72, 78);
    line("안녕하세요", "안녕하세요", {{70, 97}, {72, 58}, {74, 40}, {76, 30}, {78, 24}, {80, 22}, {84, 22}}, 70, 84);
    line("bonjour branco", "bonjour,", {{72, 107}}, 72, 74, 0xFFFFFF);
    line("bonjour.", "bonjour.", {{70, 143}, {72, 107}, {74, 91}, {76, 82}, {78, 76}, {80, 74}, {84, 73}}, 74, 84);
    line("Привет branco", "Привет", {{76, 131}}, 76, 78, 0xFFFFFF);
    line("Привет", "Привет", {{72, 160}, {74, 139}, {76, 131}, {78, 126}, {80, 124}, {84, 123}}, 72, 84);
    // O traço do cursor que acompanha o texto.
    const u64 cur = b.rect("Cursor", 70, 72, 58, 155, 3, 16, kCream);
    (void)cur;
}

u64 long_shadow(B& b, const char* n, i64 a, i64 e, f32 cx, f32 cy, f32 d, u32 c, std::initializer_list<V> angle, std::initializer_list<V> dia) {
    const u64 s = b.rect(n, a, e, cx, cy, 340, d, c);
    b.L(s).transform.anchor = Vec3{0.0f, d * T * 0.5f, 0.0f};
    b.kr(s, angle);
    b.ks(s, dia);
    return s;
}

void scene_icons(B& b) {
    b.bg("Fundo microfone", 84, 92, kAccent);
    const u64 micS = long_shadow(b, "Sombra microfone", 84, 92, 112, 79, 140, 0x8E1C5E, {{84, 69}, {86, 53}, {88, 46}, {90, 44}, {92, 44}}, {{84, 1}});
    b.kx(micS, {{84, 112}, {86, 128}, {88, 135}, {90, 138}, {92, 139}});
    const u64 mic = b.dot("Círculo microfone", 84, 92, 112, 79, 140, kDeep);
    b.kx(mic, {{84, 112}, {86, 128}, {88, 135}, {90, 138}, {92, 139}});
    const u64 micI = b.svg("Microfone", 84, 92, mic_svg(kAccent), 112, 79, 0.57f * T);
    b.kx(micI, {{84, 112}, {86, 128}, {88, 135}, {90, 138}, {92, 139}});

    b.bg("Fundo câmera", 92, 100, kDeep);
    long_shadow(b, "Sombra câmera", 92, 100, 139, 80, 108, 0xA81F70, {{92, 44}}, {{92, 1}, {94, 0.97f}, {96, 0.96f}, {98, 0.91f}});
    const u64 cam = b.dot("Círculo câmera", 92, 100, 139, 80, 108, kAccent);
    b.ks(cam, {{92, 1}, {94, 0.97f}, {96, 0.96f}, {98, 0.91f}});
    const u64 camI = b.svg("Câmera", 92, 100, camera_svg(kWhite), 139.5f, 79.5f, 0.4f * T);
    b.ks(camI, {{92, 0.42f * T}, {94, 0.4f * T}, {98, 0.38f * T}});

    b.bg("Fundo vídeo", 100, 116, kAccent);
    const u64 vidS = long_shadow(b, "Sombra vídeo", 100, 116, 140, 79, 60, 0x8E1C5E, {{100, 45}, {110, 45}, {112, 53}, {114, 70}}, {{100, 1}, {102, 0.9f}, {104, 0.867f}});
    b.kx(vidS, {{100, 140}, {110, 140}, {112, 137.5f}, {114, 133}});
    const u64 vid = b.dot("Círculo vídeo", 100, 116, 140, 79, 60, kDeep);
    b.ks(vid, {{100, 1}, {102, 0.9f}, {104, 0.867f}});
    b.kx(vid, {{100, 140}, {110, 140}, {112, 137.5f}, {114, 133}});
    const u64 vidI = b.svg("Vídeo", 100, 116, video_svg(kAccent), 140, 79, 0.2f * T);
    b.kx(vidI, {{100, 140}, {110, 140}, {112, 137.5f}, {114, 133}});
}

void scene_logo(B& b) {
    b.bg("Fundo degradê (revelação)", 116, 166, kAccent);
    const u64 back = b.rect("Fundo logo", 116, 134, 140, 79, 300, 170, kDeep, 0);
    b.ksize(back, {{116, 300}, {128, 300}, {130, 284}, {132, 260}, {134, 112}}, {{116, 170}, {128, 170}, {130, 170}, {132, 260}, {134, 112}});
    b.kcorner(back, {{116, 0}, {128, 0}, {130, 60}, {132, 130}, {134, 56}});
    const std::string poly = svg_doc(380, 220, "<polygon points=\"176,85 335,85 248,220 40,220\" fill=\"#4A1A6B\"/>");
    const u64 sh = b.svg("Sombra logo", 116, 131, poly, 90, 110, T);
    b.kx(sh, {{116, 90}, {118, 83}, {120, 80}, {122, 77}, {124, 76}, {126, 75}, {130, 76}});
    b.ky(sh, {{116, 110}, {126, 111}, {128, 113}, {130, 116}});
    const u64 g = b.svg("Instagram logo", 116, 134, instagram_svg(kAccent), 95, 79, 0.38f * T);
    b.kx(g, {{116, 95}, {118, 87.5f}, {120, 84}, {122, 81.5f}, {124, 80}, {126, 79.5f}, {128, 79.5f}, {130, 80.5f}, {132, 83.5f}});
    b.ky(g, {{116, 79}, {126, 80}, {128, 82}, {130, 85}, {132, 94}});
    const u64 w = b.heavy(b.text("Instagram", 116, 134, "Instagram", 120, 79, 32, kWhite, 0));
    b.kx(w, {{116, 120}, {118, 113}, {120, 110}, {122, 107}, {124, 106}, {126, 105}, {128, 105}, {130, 106}, {132, 109}});
    b.ky(w, {{116, 79}, {126, 80}, {128, 82}, {130, 85}, {132, 94}});
}

void scene_wave(B& b) {
    const i32 n = 40;
    const u64 wave = b.group("Onda (grupo)", 134, 166, 0, 0, 0);
    b.L(wave).transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
    // Medido: faixa horizontal e centro vertical da onda a cada quadro.
    b.key(wave, TrackProperty::PositionX, {{146, 0}, {148, 12.5f}, {150, 40.8f}, {152, 40}, {154, 9.4f}, {156, 9.5f}, {158, 3.4f}, {160, -1.4f}, {162, -1.2f}, {164, 1}}, T);
    b.key(wave, TrackProperty::PositionY, {{146, 0}, {148, 10.5f}, {150, 33.5f}, {152, 34.7f}, {154, 30.5f}, {156, 15.3f}, {158, 0.5f}, {160, 0.5f}, {162, -0.5f}, {164, 28.3f}}, T);
    b.key(wave, TrackProperty::ScaleX, {{146, 1}, {148, 0.564f}, {150, 0.27f}, {152, 0.354f}, {154, 0.455f}, {156, 0.44f}, {158, 0.45f}, {160, 0.43f}, {162, 0.405f}, {164, 0.27f}});
    b.key(wave, TrackProperty::ScaleY, {{146, 1}, {148, 0.45f}, {150, 0.12f}, {152, 0.08f}, {154, 0.12f}, {156, 0.3f}, {158, 0.5f}, {160, 0.5f}, {162, 0.55f}, {164, 0.3f}});
    struct Shape { f32 f, peak, width, amp, base; };
    static const Shape kShapes[] = {
        {136, 225, 22, 70, 5}, {138, 190, 30, 72, 7}, {140, 140, 58, 70, 10}, {142, 130, 55, 82, 10},
        {144, 75, 18, 80, 14}, {146, 75, 16, 78, 13}, {148, 75, 16, 78, 13}, {154, 140, 90, 18, 12},
        {156, 140, 62, 76, 14}, {160, 140, 72, 78, 14}, {162, 130, 60, 76, 12}, {164, 140, 48, 60, 12},
    };
    char name[24];
    for (i32 i = 0; i < n; ++i) {
        const f32 x = 8.0f + i * 6.6f;
        std::snprintf(name, sizeof name, "Onda %02d", i + 1);
        const u64 bar = b.rect(name, 135, 166, x, 79, 4, 70, kDeep, 2, wave);
        Layer& l = b.L(bar);
        Track& t = l.tracks.get_or_create(TrackProperty::ScaleY);
        for (const Shape& s : kShapes) {
            const f32 h = bar_height(i, x, s.peak, s.width, s.amp, s.base);
            (void)t.set(l.local_time(FrameIndex{static_cast<i64>(s.f)}), h / 70.0f, Interpolation::Linear);
        }
    }
    // O círculo e os pontos que viram a onda.
    b.dot("Círculo onda", 134, 136, 140, 79, 112, kDeep);
    for (const f32 x : {9.f, 61.f, 218.f, 271.f}) b.dot("Ponto", 134, 136, x, 79, 16, kDeep);
    for (const f32 x : {35.f, 245.f}) b.rect("Pílula", 134, 136, x, 79, 16, 30, kDeep, 8);
}

void scene_panels(B& b) {
    // Quatro painéis que deslizam; as letras vivem neles.
    const u64 mint = b.rect("Painel Aa", 148, 166, 229, 50, 102, 100, kBlush);
    b.ksize(mint, {{148, 102}, {150, 125}, {152, 133}, {154, 137}, {156, 140}, {158, 143}, {160, 149}, {162, 158}, {164, 188}},
                  {{148, 100}, {150, 85}, {152, 83}, {154, 81}, {160, 81}, {162, 88}, {164, 105}});
    b.kx(mint, {{148, 229}, {150, 217.5f}, {152, 213.5f}, {154, 211.5f}, {156, 210}, {158, 208.5f}, {160, 205.5f}, {162, 201}, {164, 186}});
    b.ky(mint, {{148, 50}, {150, 42.5f}, {152, 41.5f}, {154, 40.5f}, {160, 40.5f}, {162, 44}, {164, 52.5f}});
    const u64 deep = b.rect("Painel #&!", 148, 166, 89, 129, 178, 58, kDeep);
    b.ksize(deep, {{148, 178}, {150, 155}, {152, 147}, {154, 143}, {156, 140}, {158, 137}, {160, 131}, {162, 122}, {164, 92}},
                  {{148, 58}, {150, 73}, {152, 75}, {154, 77}, {160, 77}, {162, 70}, {164, 53}});
    b.kx(deep, {{148, 89}, {150, 77.5f}, {152, 73.5f}, {154, 71.5f}, {156, 70}, {158, 68.5f}, {160, 65.5f}, {162, 61}, {164, 46}});
    b.ky(deep, {{148, 129}, {150, 121.5f}, {152, 120.5f}, {154, 119.5f}, {160, 119.5f}, {162, 123}, {164, 131.5f}});
    const u64 slate = b.rect("Painel 123", 148, 166, 229, 129, 102, 58, kPlum);
    b.ksize(slate, {{148, 102}, {150, 125}, {152, 133}, {154, 137}, {156, 140}, {158, 143}, {160, 149}, {162, 158}, {164, 188}},
                   {{148, 58}, {150, 73}, {152, 75}, {154, 77}, {160, 77}, {162, 70}, {164, 53}});
    b.kx(slate, {{148, 229}, {150, 217.5f}, {152, 213.5f}, {154, 211.5f}, {156, 210}, {158, 208.5f}, {160, 205.5f}, {162, 201}, {164, 186}});
    b.ky(slate, {{148, 129}, {150, 121.5f}, {152, 120.5f}, {154, 119.5f}, {160, 119.5f}, {162, 123}, {164, 131.5f}});

    const u64 aa = b.heavy(b.text("Aa", 148, 166, "Aa", 235, 48, 61, kPlum));
    b.kx(aa, {{148, 245}, {150, 235}, {152, 220}, {154, 215}, {156, 211}, {158, 209}, {160, 206}, {162, 202}, {164, 186}});
    b.ky(aa, {{148, 55}, {150, 46}, {152, 43}, {154, 40}, {162, 40}, {164, 55}});
    b.ks(aa, {{148, 1.2f}, {150, 1.1f}, {152, 1.03f}, {154, 1}, {162, 1}, {164, 1.3f}});
    const u64 sym = b.heavy(b.text("#&!", 148, 166, "#&!", 70, 121, 68, kAccent));
    b.kx(sym, {{148, 70}, {150, 70}, {152, 72}, {154, 70}, {156, 68}, {158, 65}, {160, 58}, {162, 45}, {164, 18}});
    b.ky(sym, {{148, 150}, {150, 128}, {152, 123}, {154, 119}, {160, 119}, {162, 122}, {164, 150}});
    const u64 num = b.heavy(b.text("123", 148, 166, "123", 218, 121, 69, kCream));
    b.kx(num, {{148, 245}, {150, 230}, {152, 227}, {154, 218}, {156, 212}, {158, 209}, {160, 205}, {162, 201}, {164, 186}});
    b.ky(num, {{148, 150}, {150, 128}, {152, 124}, {154, 119}, {160, 119}, {162, 126}, {164, 150}});

    // Bb, depois Cc Zz: a mesma célula do mosaico em tela cheia.
    b.bg("Fundo Bb", 166, 185, kNavy);
    const u64 bbM = b.rect("Célula Bb", 166, 174, 150, 74, 260, 147, kBlush);
    b.ksize(bbM, {{166, 260}, {168, 272}, {170, 276}, {172, 280}}, {{166, 147}, {168, 149}, {170, 150}, {172, 150}});
    b.kx(bbM, {{166, 150}, {168, 141}, {170, 140}, {172, 140}});
    b.ky(bbM, {{166, 74}, {168, 78}, {170, 79}, {172, 79}});
    const u64 bb = b.text("Bb", 166, 174, "Bb", 155, 74, 146, kPlum);
    b.kx(bb, {{166, 155}, {168, 148}, {170, 144}, {172, 137}});
    b.ky(bb, {{166, 72}, {168, 76}, {172, 77}});

    const u64 cM = b.rect("Célula Cc", 174, 185, 90, 79, 181, 158, kBlush);
    b.ksize(cM, {{174, 181}, {176, 152}, {178, 141}, {180, 133}, {182, 128}, {184, 110}}, {{174, 158}, {176, 158}, {178, 158}, {180, 149}, {182, 140}, {184, 124}});
    b.kx(cM, {{174, 90}, {176, 76}, {178, 73.5f}, {180, 73.5f}, {182, 76}, {184, 85}});
    b.ky(cM, {{174, 79}, {180, 78.5f}, {182, 80}, {184, 79}});
    const u64 zM = b.rect("Célula Zz", 174, 185, 230, 79, 99, 158, kDeep);
    b.ksize(zM, {{174, 99}, {176, 128}, {178, 136}, {180, 133}, {182, 128}, {184, 110}}, {{174, 158}, {176, 158}, {178, 158}, {180, 149}, {182, 140}, {184, 124}});
    b.kx(zM, {{174, 230}, {176, 216}, {178, 212}, {180, 206.5f}, {182, 204}, {184, 195}});
    b.ky(zM, {{174, 79}, {180, 78.5f}, {182, 80}, {184, 79}});
    const u64 cc = b.text("Cc", 174, 185, "Cc", 95, 78, 92, kPlum);
    b.kx(cc, {{174, 95}, {176, 80}, {178, 77}, {180, 77}, {182, 79}, {184, 85}});
    b.ks(cc, {{174, 1}, {180, 0.95f}, {182, 0.9f}, {184, 0.78f}});
    const u64 zz = b.text("Zz", 174, 185, "Zz", 225, 78, 92, kBlush);
    b.kx(zz, {{174, 225}, {176, 221}, {178, 212}, {180, 207}, {182, 204}, {184, 195}});
    b.ks(zz, {{174, 1}, {180, 0.95f}, {182, 0.9f}, {184, 0.78f}});
}

void scene_mosaic(B& b) {
    constexpr i64 A = 185, E = 217;
    b.bg("Fundo mosaico", A, E, kNavy);
    const u64 all = b.shape(0, "Mosaico (grupo)", A, E, 245, 138, 490, 276, Vec4{0, 0, 0, 0}, 0, M, 0);
    b.key(all, TrackProperty::ScaleX, {{185, 1.524f}, {186, 1.317f}, {187, 1.207f}, {188, 1.146f}, {189, 1.091f}, {190, 1.061f}, {191, 1.037f}, {192, 1.012f}, {193, 1}, {208, 1}, {209, 1.05f}, {210, 1.22f}, {212, 1.5f}, {214, 2.75f}, {216, 2.93f}});
    b.key(all, TrackProperty::ScaleY, {{185, 1.524f}, {186, 1.317f}, {187, 1.207f}, {188, 1.146f}, {189, 1.091f}, {190, 1.061f}, {191, 1.037f}, {192, 1.012f}, {193, 1}, {208, 1}, {209, 1.05f}, {210, 1.22f}, {212, 1.5f}, {214, 2.75f}, {216, 2.93f}});
    b.key(all, TrackProperty::PositionY, {{185, 118}, {186, 122}, {187, 124}, {188, 127}, {189, 129}, {190, 132}, {191, 133}, {192, 136}, {193, 138}, {209, 142}, {210, 153}, {212, 163}, {214, 177}, {216, 186}}, M);

    const u64 c1 = b.shape(0, "Coluna 1", A, E, 245, 138, 490, 276, Vec4{0, 0, 0, 0}, 0, M, all);
    b.key(c1, TrackProperty::PositionY, {{193, 138}, {194, 134}, {195, 129}, {196, 123}, {197, 117}, {198, 109}, {199, 97}, {200, 53}, {201, 48}, {202, 42}, {203, 38}, {204, 35}, {205, 32}, {206, 28}, {207, 26}, {208, 25}, {216, 22}}, M);
    const u64 c2 = b.shape(0, "Coluna 2", A, E, 245, 138, 490, 276, Vec4{0, 0, 0, 0}, 0, M, all);
    b.key(c2, TrackProperty::PositionY, {{193, 138}, {194, 142}, {195, 146}, {196, 150}, {197, 156}, {198, 165}, {199, 180}, {200, 255}, {201, 269}, {202, 278}, {203, 283}, {204, 289}, {206, 292}, {207, 294}, {216, 296}}, M);
    const u64 c3 = b.shape(0, "Coluna 3", A, E, 245, 138, 490, 276, Vec4{0, 0, 0, 0}, 0, M, all);
    b.key(c3, TrackProperty::PositionY, {{193, 138}, {194, 131}, {195, 126}, {196, 120}, {197, 113}, {198, 106}, {199, 94}, {200, 54}, {201, 46}, {202, 42}, {203, 39}, {204, 37}, {205, 34}, {206, 32}, {207, 30}, {208, 28}, {216, 24}}, M);

    const auto R = [&](const char* n, f32 x0, f32 y0, f32 x1, f32 y1, u32 c, u64 p) { return b.mrect(n, A, E, x0, y0, x1, y1, c, p); };
    const auto Tx = [&](const char* n, const char* s, f32 x, f32 y, f32 size, u32 c, u32 al, u64 p, f32 line = 1.0f) { return b.text(n, A, E, s, x, y, size, c, al, M, p, -8.0f, line); };
    const auto Icon = [&](const char* n, const std::string& s, f32 x, f32 y, f32 scale, u64 p) { return b.svg(n, A, E, s, x, y, scale, p, M); };

    // Coluna 1
    R("Bloco Video", -20, 5, 160, 105, kDeep, c1);
    Icon("Ícone Video", video_svg(kAccent), 40, 59, 0.2f * M, c1);
    Tx("Video", "Video", 56, 58, 38, kAccent, 0, c1);
    R("Creme 1", -20, 108, 50, 200, kCream, c1);
    R("Noite 1", 52, 108, 113, 168, kNavy, c1);
    R("Ameixa 1", 113, 108, 160, 168, kPlum, c1);
    R("Faixa branca", 52, 170, 160, 200, 0xFFFFFF, c1);
    R("Logo A", -20, 205, 75, 253, kDeep, c1);
    R("Logo B", 78, 205, 160, 253, kAccent, c1);
    R("Logo C", -20, 256, 75, 304, kNavy, c1);
    R("Logo D", 78, 256, 160, 304, kBlush, c1);
    Icon("Instagram A", instagram_svg(kAccent), 38, 229, 0.27f * M, c1);
    Icon("Instagram B", instagram_svg(kWhite), 119, 229, 0.27f * M, c1);
    Icon("Instagram C", instagram_svg(0xFFFFFF), 38, 280, 0.27f * M, c1);
    Icon("Instagram D", instagram_svg(kAccent), 119, 280, 0.27f * M, c1);
    R("Creme conversa", -20, 307, 160, 390, kCream, c1);
    Tx("Sigue (creme)", "Sigue la conversación\n話を続けよう\nПродолжай разговор", 72, 342, 12.5f, kNavy, 1, c1);
    // Coluna 2
    R("Bloco símbolos", 163, -165, 327, -83, kAccent, c2);
    Tx("#%&!?", "#%&!?\"\nAaBbCcDdEeFfGgHhIiJjKk\nLlMmNnOoPpQqRrSsTtUu\nVvWwXxYyZz0123456789", 172, -116, 12.5f, kWhite, 0, c2);
    R("Painel pontos", 163, -80, 327, 10, kCream, c2);
    static const u32 kDots[] = {0xFFFFFF, kCream, kBlush, kAccent, kPlum, kDeep, kNavy};
    for (i32 i = 0; i < 7; ++i) {
        const u64 d = b.shape(1, "Ponto", A, E, 179.0f + i * 22.0f, -62.0f, 11, 11, hex(kDots[i]), 0, M, c2);
        if (i < 3) { b.L(d).shape.strokeColor = hex(0xC8C4B8); b.L(d).shape.strokeWidth = 1.5f; }
        static const f32 kArc[] = {4.0f, 10.0f, 16.0f, 20.0f, 16.0f, 10.0f, 4.0f};
        b.key(d, TrackProperty::PositionY, {{208, -62}, {212, -62 + kArc[i] * 0.5f}, {214, -62 + kArc[i]}, {216, -62 + kArc[i] * 1.2f}}, M);
    }
    R("Escuro 2", 163, 13, 327, 59, kDeep, c2);
    R("Degradê 2", 163, 62, 245, 105, kAccent, c2);
    R("Rosa 2", 245, 62, 327, 105, kBlush, c2);
    R("Célula rosa", 163, 108, 245, 200, kBlush, c2);
    R("Célula escura", 245, 108, 327, 200, kDeep, c2);
    const auto swap = [&](const char* n, const char* s, f32 x, u32 c, i64 from, i64 to) { (void)b.heavy(b.text(n, from, to, s, x, 154, 58, c, 1, M, c2, -10.0f)); };
    swap("Dd", "Dd", 204, kPlum, A, 192);
    swap("Yy", "Yy", 286, kBlush, A, 192);
    swap("Ee", "Ee", 204, kPlum, 192, E);
    swap("Xx", "Xx", 286, kBlush, 192, E);
    R("Bloco Voice", 163, 205, 327, 285, kAccent, c2);
    Icon("Ícone Voice", mic_svg(kWhite), 212, 247, 0.17f * M, c2);
    Tx("Voice", "Voice", 226, 247, 36, kWhite, 0, c2);
    // Coluna 3
    R("Ameixa conversa", 330, 5, 510, 103, kPlum, c3);
    Tx("Sigue (ameixa)", "Sigue la conversación\n話を続けよう\nПродолжай разговор", 412, 46, 12.5f, kCream, 1, c3);
    const auto row = [&](const char* n, f32 y0, u32 back, u32 fore) {
        R(n, 330, y0, 510, y0 + 29, back, c3);
        Icon("Instagram linha", instagram_svg(fore), 386, y0 + 14.5f, 0.16f * M, c3);
        Tx("Instagram (nome)", "Instagram", 396, y0 + 14.5f, 16, fore, 0, c3);
    };
    row("Linha escura", 108, kDeep, kAccent);
    row("Linha degradê", 140, kAccent, 0xFFFFFF);
    row("Linha creme", 173, kCream, kDeep);
    R("Bloco fonte", 330, 206, 510, 332, kDeep, c3);
    Tx("Instagram Sans", "Instagram\nSans", 335, 225, 24, kCream, 0, c3);
    Tx("Alfabeto", "#%&!?\"\nAaBbCcDdEeFfGgHhIiJjKk\nLlMmNnOoPpQqRrSsTtUu\nVvWwXxYyZz0123456789", 335, 276, 12.5f, kCream, 0, c3);
    R("Bloco Languages", 330, 335, 510, 450, kNavy, c3);
    Tx("More Languages", "More\nLanguages", 346, 372, 34, 0xFFFFFF, 0, c3);
}

void scene_photo(B& b) {
    b.bg("Fundo creme (pontos)", 216, 234, kCream);
    struct Dot { const char* name; u32 color; f32 ys[4]; f32 xs[4]; bool line; };
    static const Dot kRow[] = {
        {"Ponto branco", 0xFFFFFF, {61, 72, 76, 78}, {38, 33, 25, 8}, true},
        {"Ponto creme", kCream, {67, 72, 76, 78}, {72, 68, 60, 42}, true},
        {"Ponto rosa", kBlush, {70, 72, 76, 78}, {106, 104, 95, 77}, true},
        {"Ponto ameixa", kPlum, {69, 72, 76, 78}, {174, 177, 184, 202}, false},
        {"Ponto escuro", kDeep, {66, 72, 76, 78}, {208, 212, 219, 237}, false},
        {"Ponto noite", kNavy, {61, 72, 76, 78}, {242, 246, 255, 272}, false},
    };
    for (const Dot& d : kRow) {
        const u64 id = b.dot(d.name, 216, 232, d.xs[0], d.ys[0], 19, d.color);
        if (d.line) { b.L(id).shape.strokeColor = hex(0xC8C4B8); b.L(id).shape.strokeWidth = 3.0f; }
        b.kx(id, {{216, d.xs[0]}, {218, d.xs[1]}, {220, d.xs[2]}, {222, d.xs[3]}, {224, d.xs[3] < 140 ? d.xs[3] - 50 : d.xs[3] + 50}, {226, d.xs[3] < 140 ? -40.0f : 320.0f}});
        b.ky(id, {{216, d.ys[0]}, {218, d.ys[1]}, {220, d.ys[2]}, {222, d.ys[3]}, {224, 79}});
    }
    const u64 g = b.dot("Ponto degradê", 216, 225, 140, 71, 21, kAccent);
    b.ky(g, {{216, 71}, {218, 72}, {220, 77}, {222, 79}});
    b.ks(g, {{216, 1}, {218, 1.05f}, {220, 1.25f}, {222, 1.6f}, {224, 2.4f}});

    b.bg("Fundo colagem", 260, 266, kDeep);
    // A foto cresce do ponto verde; depois tudo encolhe junto, como um grupo.
    const u64 frame = b.group("Colagem (grupo)", 224, 266);
    b.ks(frame, {{224, 1}, {260, 1}, {262, 0.9f}, {264, 0.833f}, {266, 0.7f}});
    const u64 snow = b.image("Foto principal", 224, 266, "foto_principal.png", 140, 79, 112, frame);
    const f32 k = b.L(snow).transform.scale.x / 112.0f;
    b.ks(snow, {{224, 112 * k}, {226, 150 * k}, {228, 162 * k}, {230, 174 * k}, {232, 250 * k}, {234, 300 * k}, {266, 300 * k}});
    const u64 dim = b.rect("Escurecer foto", 234, 240, 140, 79, 300, 170, kNavy, 0, frame);
    b.ko(dim, {{234, 0.62f}, {236, 0.35f}, {238, 0.0f}});

    b.image("Foto 1", 238, 266, "foto_1.png", 73, 79, 106, frame);
    b.image("Foto 2", 246, 266, "foto_2.png", 41.5f, 77, 65, frame);
    b.image("Foto 3", 252, 266, "foto_3.png", 100, 43.5f, 84, frame);
    b.image("Foto 4", 256, 266, "foto_4.png", 102.5f, 126, 95, frame);
    b.image("Foto 5", 260, 266, "foto_5.png", 121, 85.5f, 62, frame);

    const u64 bubble = b.group("Balão foto (grupo)", 234, 266, frame, 0, 0);
    b.L(bubble).transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
    b.key(bubble, TrackProperty::PositionY, {{234, 6}, {236, 2}, {238, 0}, {244, -1}, {266, -3}}, T);
    const std::string shape = svg_doc(272, 50, "<path d=\"M18 2 L268 2 C270 2 271 4 269 6 L258 16 L258 32 C258 42 250 48 240 48 L18 48 C8 48 2 42 2 32 L2 18 C2 8 8 2 18 2 Z\" fill=\"" + hexs(kCream) + "\"/>");
    b.svg("Balão", 234, 266, shape, 219.5f, 91.5f, 0.3988f * T, bubble);
    b.text("Look at this view!", 234, 266, "Look at this view!", 170.5f, 91.5f, 8.5f, kNavy, 0, T, bubble, 0.0f);
    b.text("18:40", 234, 266, "18:40", 235, 92.5f, 7.4f, 0x8A9096, 0, T, bubble, 0.0f);
    b.svg("Checks foto", 234, 266, double_check_svg(kBlue, 10), 259, 91, 0.066f * T, bubble);
}

void chat_row(B& b, const char* name, const char* avatar, const char* time, const char* msg, std::initializer_list<V> ys, i64 a, i64 e, i64 msgAt) {
    const u64 row = b.group(name, a, e, 0, 0, 0);
    b.L(row).transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
    b.ky(row, ys);
    b.image(avatar, a, e, avatar, 68, 0, 37.5f, row);
    std::string av = std::string(name) + " nome";
    b.text(av.c_str(), a, e, name, 98.5f, -9, 16, 0xFFFFFF, 0, T, row, 0.0f);
    b.text("Hora", a, e, time, 235, -9, 10.5f, 0x8A96A0, 2, T, row, 0.0f);
    b.svg("Checks", a, e, double_check_svg(0x8A96A0, 10), 105, 9.5f, 0.09f * T, row);
    b.text("Mensagem", msgAt, e, msg, 114.5f, 9.5f, 11.7f, 0x8A96A0, 0, T, row, 0.0f);
}

void scene_list(B& b) {
    b.bg("Fundo roxo bola", 266, 274, kDeep);
    const u64 g = b.dot("Bola degradê", 266, 276, 140, 79, 54, kAccent);
    b.ks(g, {{266, 1}, {268, 0.85f}, {270, 0.78f}, {272, 0.74f}, {274, 0.7f}});
    b.bg("Fundo lista", 274, 322, kNavy);
    // A bola verde vira o avatar; o nome sai de trás dela.
    const u64 bola2 = b.dot("Bola (avatar)", 274, 278, 140, 79, 38, kAccent);
    b.kx(bola2, {{274, 140}, {276, 87}, {278, 72}});
    chat_row(b, "Lara Mendes", "avatar_lara.png", "Agora", "어디야?",
             {{274, 80}, {276, 80}, {278, 79}, {280, 77}, {282, 75}, {284, 67}, {286, 33}, {288, 28}, {290, 22}, {292, 14}, {294, -30}}, 276, 295, 276);
    chat_row(b, "Theo Ramos", "avatar_theo.png", "16:10", "¿Vienes hoy?",
             {{284, 128}, {286, 88}, {288, 73}, {290, 68}, {292, 66}, {294, 31}, {296, 28}, {298, 23}, {300, 19}, {302, -20}}, 284, 303, 286);
    chat_row(b, "Aiko Tanaka", "avatar_aiko.png", "13:45", "週末ひま？",
             {{294, 87}, {296, 82}, {298, 77}, {300, 72}, {302, 36}, {304, 28}, {306, 26}, {308, 22}, {310, 18}, {312, -20}}, 294, 313, 296);
    chat_row(b, "Nina Weber", "avatar_nina.png", "08:30", "Bis später!",
             {{302, 91}, {304, 85}, {306, 80}, {308, 76}, {310, 67}, {312, 24}, {314, 20}, {316, 17}, {318, 15}, {320, 13}, {322, 13}}, 302, 322, 304);
    chat_row(b, "Chloé Laurent", "avatar_chloe.png", "07:05", "À demain !",
             {{312, 89}, {314, 81}, {316, 79}, {318, 77}, {322, 77}}, 312, 322, 314);

    // A última linha em verde-escuro, com os checks azuis.
    b.bg("Fundo Chloé", 322, 332, kDeep);
    const f32 U = 2.0f / T;   // px da referência (702) em quadrinhos
    b.image("Avatar Chloé", 322, 332, "avatar_chloe.png", 120 * U, 201 * U, 118 * U);
    b.text("Chloé Laurent (grande)", 322, 332, "Chloé Laurent", 216 * U, 174 * U, 46.5f * U, 0xFFFFFF, 0, T, 0, 0.0f);
    b.text("07:05 (grande)", 322, 332, "07:05", 643 * U, 173 * U, 33 * U, 0x8FA8A0, 2, T, 0, 0.0f);
    b.svg("Checks azuis (grande)", 322, 332, double_check_svg(kBlue, 10), 238 * U, 232 * U, 0.27f * U * T);
    b.text("À demain (grande)", 322, 332, "À demain !", 265 * U, 232 * U, 36 * U, 0xFFFFFF, 0, T, 0, 0.0f);
}

void scene_outro(B& b) {
    b.bg("Fundo 125", 332, 340, kAccent);
    const u64 n1 = b.heavy(b.text("125 (degradê)", 332, 340, "125", 64, 58, 54, kWhite, 0));
    b.kx(n1, {{332, 64}, {334, 42}, {336, 37}, {338, 34}, {340, 33}});
    b.bg("Fundo Languages", 340, 360, kDeep);
    const u64 n2 = b.heavy(b.text("125", 340, 360, "125", 33, 58, 54, kCream, 0));
    b.kx(n2, {{340, 33}, {342, 32}, {344, 30}, {360, 30}});
    b.ky(n2, {{340, 58}, {350, 58}, {354, 57}, {356, 55}, {358, 49}, {360, 44}});
    const u64 lang = b.heavy(b.text("Languages", 340, 360, "Languages", 47, 93, 55, kCream, 0));
    b.kx(lang, {{340, 47}, {342, 39}, {344, 35}, {346, 33}, {348, 32}, {350, 30}, {360, 30}});
    b.ky(lang, {{340, 93}, {350, 93}, {354, 92}, {356, 91}, {358, 88}, {360, 84}});

    b.bg("Fundo Message", 360, 398, kAccent);
    const u64 msg = b.text("Message", 360, 398, "Message", 139.5f, 82, 20.5f, kWhite, 1, T, 0, -8.0f);
    b.kx(msg, {{360, 139.5f}, {368, 139.5f}, {370, 113}, {372, 111}, {374, 110}, {376, 109}, {398, 109}});
    b.ky(msg, {{360, 82}, {362, 79}, {364, 78}, {398, 78}});
    const auto word = [&](const char* s, i64 a, i64 e) {
        const u64 w = b.text(s, a, e, s, 148, 79, 20.5f, kWhite, 0, T, 0, -8.0f);
        b.ky(w, {{static_cast<f32>(a), 82}, {static_cast<f32>(a + 2), 79.5f}, {static_cast<f32>(a + 4), 78}, {static_cast<f32>(a + 6), 78}, {static_cast<f32>(e - 2), 78}, {static_cast<f32>(e - 1), 77}});
    };
    word("Anyone", 370, 380);
    word("Anywhere", 380, 388);
    word("Anytime", 388, 398);

    b.bg("Fundo final", 398, 552, kDeep);
    b.svg("Instagram final", 398, 531, instagram_svg(kAccent), 140, 79, 0.23f * T);
    for (i32 i = 0; i < 8; ++i) {
        static const f32 kBars[] = {1.0f, 3.0f, 1.5f, 3.5f, 1.0f, 2.5f, 3.0f, 1.5f};
        b.rect("Barra", 531, 552, 131.5f + i * 2.6f, 79, kBars[i] * 0.55f, 11, kAccent);
    }
}

} // namespace

namespace {
/// Uma frase da animação "estilo Apple": um animador só (posição, opacidade e
/// desfoque), seletor em rampa, e o deslocamento entra e sai.
void apple_line(B& b, const char* s, i64 a, i64 e, f32 width, f32 inA, f32 inB, f32 outA, f32 outB) {
    const u64 id = b.text(s, a, e, s, 960, 540, 100, kWhite, 1, 1.0f, 0, 0.0f);
    {
        Layer& l = b.L(id);
        const auto font = aurea::text::FontManager::instance().font_for(l.text);
        l.text.size *= width / aurea::text::measure(*font, l.text).width;
        const aurea::text::TextExtent ext = aurea::text::measure(*font, l.text);
        l.transform.anchor = Vec3{std::ceil(ext.width + 4.0f) * 0.5f, std::ceil(ext.height + 4.0f) * 0.5f, 0.0f};
        l.blendMode = BlendMode::Difference;
        l.text.strokeWidth = l.text.size * 0.012f;
        l.text.strokeColor = l.text.color;
    }
    AUREA_CHECK(b.e.add_text_animator(id, kTextPropPosition | kTextPropOpacity | kTextPropBlur) >= 0);
    TextAnimator& an = b.L(id).text.animators[0];
    an.position = Vec3{0.0f, 100.0f, 0.0f};
    an.opacity = 0.0f;
    an.blur = 100.0f;
    an.selector.type = kTextSelectorRange;
    an.selector.basedOn = 0;
    an.selector.shape = 1;
    an.selector.easeHigh = 50.0f;
    an.selector.easeLow = 100.0f;
    Layer& l = b.L(id);
    Track& t = l.tracks.get_or_create(TrackProperty::TextAnimParam, 0, text::kSelOffset);
    const auto put = [&](f32 f, f32 v, bool fast) {
        const FrameIndex at = l.local_time(FrameIndex{static_cast<i64>(f)});
        (void)t.set(at, v, fast ? Interpolation::Bezier : Interpolation::Linear);
        if (fast) t.set_interpolation(at, Interpolation::Bezier, 0.25f, 0.35f, 0.55f, 1.0f);
    };
    put(inA, -100, true);
    put(inB, 100, false);
    put(outA, 100, true);
    put(outB, -100, false);
    Track& sx = l.tracks.get_or_create(TrackProperty::ScaleX);
    Track& sy = l.tracks.get_or_create(TrackProperty::ScaleY);
    for (Track* tr : {&sx, &sy}) {
        const FrameIndex at = l.local_time(FrameIndex{static_cast<i64>(inA)});
        (void)tr->set(at, 1.08f, Interpolation::Bezier);
        tr->set_interpolation(at, Interpolation::Bezier, 0.12f, 0.75f, 0.35f, 1.0f);
        (void)tr->set(l.local_time(FrameIndex{static_cast<i64>(inB + 6)}), 1.0f, Interpolation::Linear);
    }
}
} // namespace

AUREA_TEST(ReferenceVideo, AppleStyleText) {
    const char* out = std::getenv("AUREA_REF_TEXT");
    if (!out) { std::printf("(sem AUREA_REF_TEXT: pulado) "); return; }
    const std::filesystem::path frames = std::string(out) + "/render";
    std::filesystem::create_directories(frames);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    ec.defaultFontPath = std::string(std::getenv("AUREA_REF_VIDEO")) + "/media/Roboto-Regular.ttf";
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, nullptr).ok());
    B b{e, "", "", {}};
    b.comp().set_duration(FrameIndex{135});
    b.comp().set_background(Color{0.0f, 0.0f, 0.0f, 1.0f});
    const u64 flash = b.shape(0, "Fundo branco", 0, 135, 960, 540, 1924, 1084, hex(kWhite), 0, 1.0f, 0);
    b.ko(flash, {{0, 0}, {22, 0}, {26, 0.1f}, {33, 0.67f}, {42, 0.94f}, {51, 1}, {69, 1}, {78, 0.75f}, {87, 0.25f}, {96, 0.03f}, {108, 0}});
    apple_line(b, "HOW TO MAKE", 0, 42, 847, 0, 20, 28, 40);
    apple_line(b, "apple style", 34, 84, 472, 34, 56, 64, 82);
    apple_line(b, "IN AFTER Fx", 80, 135, 547, 80, 104, 113, 128);
    AUREA_CHECK(e.save_project((std::string(out) + "/Apple-Style.aurea").c_str()).ok());
    for (i64 f = 0; f < 135; ++f) {
        Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK_EQ(e.submit_commands(&c, 1), 1u);
        std::vector<u8> rgba; u32 w = 0, h = 0;
        if (!e.capture_frame_rgba(1280, rgba, w, h).ok()) { AUREA_CHECK(false); continue; }
        Image8 img; img.width = w; img.height = h; img.rgba = std::move(rgba);
        char name[32]; std::snprintf(name, sizeof name, "f%04d.png", static_cast<int>(f));
        AUREA_CHECK(write_png((frames / name).string(), img));
    }
    e.shutdown();
}

// Mesma folha da comp "CMP" do After Effects (build/reference/ae-medida): sete
// animações com os mesmos valores, fonte, FPS e tempos.
AUREA_TEST(ReferenceVideo, AeTextCompare) {
    const char* out = std::getenv("AUREA_AE_COMPARE");
    if (!out) { std::printf("(sem AUREA_AE_COMPARE: pulado) "); return; }
    const std::filesystem::path frames = out;
    std::filesystem::create_directories(frames);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    ec.defaultFontPath = "C:/Windows/Fonts/arialbd.ttf";
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1600, 1040, 30.0, nullptr).ok());
    B b{e, "", "", {}};
    b.comp().set_duration(FrameIndex{60});
    b.comp().set_background(Color{0.0f, 0.0f, 0.0f, 1.0f});
    const f32 size = 100.0f * 1.11719f;   // o tamanho do motor é a altura stb (ascendente + descendente da Arial)
    const auto row = [&](u32 i, u32 props) {
        const u64 id = b.text("linha", 0, 60, "Animate Text", i % 2 ? 860.0f : 60.0f, 200.0f + 260.0f * static_cast<f32>(i / 2), size, kWhite, 0, 1.0f, 0, 0.0f);
        b.L(id).transform.anchor = Vec3{2.0f, 2.0f + size * 0.81031f, 0.0f};
        AUREA_CHECK(e.add_text_animator(id, props) >= 0);
        TextAnimator& a = b.L(id).text.animators[0];
        a.selector.type = kTextSelectorRange;
        a.selector.basedOn = 0;
        return id;
    };
    struct K { i64 f; f32 v; };
    const auto keys = [&](u64 id, u32 param, std::initializer_list<K> ks, bool easy) {
        Track& t = b.L(id).tracks.get_or_create(TrackProperty::TextAnimParam, 0, param);
        for (const K& k : ks) {
            (void)t.set(FrameIndex{k.f}, k.v, easy ? Interpolation::Bezier : Interpolation::Linear);
            if (easy) t.set_interpolation(FrameIndex{k.f}, Interpolation::Bezier, 0.3333f, 0.0f, 0.6667f, 1.0f);
        }
    };
    const auto anim = [&](u64 id) -> TextAnimator& { return b.L(id).text.animators[0]; };
    u64 id = row(0, kTextPropPosition | kTextPropOpacity);          // Fade Up
    anim(id).position = Vec3{0, 60, 0}; anim(id).opacity = 0; anim(id).selector.shape = 1;
    keys(id, text::kSelStart, {{0, 0}, {45, 100}}, true);
    id = row(1, kTextPropOpacity);                                  // Typewriter
    anim(id).opacity = 0; anim(id).selector.shape = 0; anim(id).selector.smoothness = 0;
    keys(id, text::kSelStart, {{0, 0}, {45, 100}}, false);
    id = row(2, kTextPropOpacity | kTextPropBlur | kTextPropScale); // Smooth Reveal
    anim(id).opacity = 0; anim(id).blur = 12; anim(id).scale = Vec2{130, 130}; anim(id).selector.shape = 1;
    anim(id).selector.easeHigh = 50; anim(id).selector.easeLow = 50;
    keys(id, text::kSelStart, {{0, 0}, {45, 100}}, true);
    id = row(3, kTextPropTracking | kTextPropOpacity);              // Tracking Reveal
    anim(id).opacity = 0; anim(id).tracking = 15; anim(id).selector.shape = 0;
    keys(id, text::kSelAmount, {{0, 100}, {45, 0}}, true);
    id = row(4, kTextPropPosition);                                 // Wave
    anim(id).position = Vec3{0, -60, 0}; anim(id).selector.shape = 4; anim(id).selector.end = 25;
    keys(id, text::kSelOffset, {{0, -25}, {45, 100}}, false);
    id = row(5, kTextPropPosition);                                 // Bounce por expressão
    anim(id).position = Vec3{0, -150, 0};
    anim(id).moreSelectors.push_back(text::make_text_selector(kTextSelectorExpression));
    for (u32 k : {text::kAnimExprX, text::kAnimExprY, text::kAnimExprZ}) {
        Track& t = b.L(id).tracks.get_or_create(TrackProperty::TextAnimParam, 0, text::selector_key(1, k));
        t.expression = expr::compile("t = time - (textIndex - 1) * 0.04;\nif (t < 0) { 100 } else { Math.abs(Math.cos(t * 10)) * Math.exp(-3 * t) * 100 }");
        t.expressionEnabled = true;
    }
    id = row(6, kTextPropPosition | kTextPropRotation | kTextPropOpacity);   // 3D por caractere
    anim(id).position = Vec3{0, 0, -200}; anim(id).rotation = Vec3{0, 90, 0}; anim(id).opacity = 0; anim(id).selector.shape = 1;
    keys(id, text::kSelStart, {{0, 0}, {45, 100}}, true);
    for (i64 f : {0, 8, 15, 23, 30, 38, 45, 59}) {
        Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK_EQ(e.submit_commands(&c, 1), 1u);
        std::vector<u8> rgba; u32 w = 0, h = 0;
        if (!e.capture_frame_rgba(1600, rgba, w, h).ok()) { AUREA_CHECK(false); continue; }
        Image8 img; img.width = w; img.height = h; img.rgba = std::move(rgba);
        char name[32]; std::snprintf(name, sizeof name, "aurea_%02d.png", static_cast<int>(f));
        AUREA_CHECK(write_png((frames / name).string(), img));
    }
    e.shutdown();
}

AUREA_TEST(ReferenceVideo, BuildEditableProject) {
    const char* out = std::getenv("AUREA_REF_VIDEO");
    if (!out) { std::printf("(sem AUREA_REF_VIDEO: pulado) "); return; }
    const std::string media = std::string(out) + "/media";
    const std::filesystem::path frames = std::string(out) + "/render";
    std::filesystem::create_directories(frames);

    WavProbe probe;
    probe.host = media + "/audio.wav";
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    ec.mediaFactory = &probe;
    ec.defaultFontPath = media + "/Roboto-Regular.ttf";
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1404, 792, 25.0, nullptr).ok());

    B b{e, media, "", {}};
    b.comp().set_duration(FrameIndex{552});
    b.comp().set_background(Color{0.063f, 0.102f, 0.129f, 1.0f});
    scene_chat(b);
    scene_time(b);
    scene_languages(b);
    scene_icons(b);
    scene_logo(b);
    scene_wave(b);
    scene_panels(b);
    scene_mosaic(b);
    scene_photo(b);
    scene_list(b);
    scene_outro(b);

    VideoImport audio;
    audio.sourcePath = std::string(kDevice) + "audio.wav";
    audio.displayName = "Trilha";
    const auto track = e.import_audio(audio);
    AUREA_CHECK(track.ok());
    b.files.push_back(package::MediaFile{audio.sourcePath, media + "/audio.wav", "audio.wav"});

    b.comp().set_duration(FrameIndex{552});

    {
        std::ofstream rows(std::string(out) + "/layers.txt");
        for (const auto& [from, to] : b.spans) rows << from << ' ' << to << '\n';
    }
    const std::string aurea = std::string(out) + "/Recriado-Instagram.aurea";
    AUREA_CHECK(e.save_project(aurea.c_str()).ok());
    AUREA_CHECK(package::write_package(aurea, std::string(out) + "/Recriado-Instagram.aureaproj", "Recriado Instagram", "0.5.0", b.files).ok());

    const char* only = std::getenv("AUREA_REF_FRAMES");
    std::vector<i64> list;
    if (only) {
        for (const char* p = only; *p;) { char* end; list.push_back(std::strtoll(p, &end, 10)); p = *end ? end + 1 : end; }
    } else {
        for (i64 f = 0; f < 400; f += 2) list.push_back(f);
        for (i64 f = 400; f < 552; f += 30) list.push_back(f);
    }
    for (const i64 f : list) {
        Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{f}, 25.0);
        AUREA_CHECK_EQ(e.submit_commands(&c, 1), 1u);
        std::vector<u8> rgba; u32 w = 0, h = 0;
        if (!e.capture_frame_rgba(702, rgba, w, h).ok()) { AUREA_CHECK(false); continue; }
        Image8 img; img.width = w; img.height = h; img.rgba = std::move(rgba);
        char name[32]; std::snprintf(name, sizeof name, "f%04d.png", static_cast<int>(f));
        AUREA_CHECK(write_png((frames / name).string(), img));
    }
    e.shutdown();
}

#endif
