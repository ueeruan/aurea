// =============================================================================
//  Aurea / text / Text.cpp
// =============================================================================
#include "aurea/text/Text.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/timeline/Layer.hpp"

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include "hb.h"
#include "hb-ot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace aurea::text {

struct Font::Impl {
    std::vector<u8> data;
    stbtt_fontinfo info{};
    int ascent = 0, descent = 0, lineGap = 0;
    // HarfBuzz sobre os MESMOS bytes (o shaping: kerning GPOS, ligaduras,
    // formas contextuais do árabe, marcas). Escala = unidades da fonte.
    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* hb = nullptr;
    ~Impl() {
        if (hb) hb_font_destroy(hb);
        if (face) hb_face_destroy(face);
        if (blob) hb_blob_destroy(blob);
    }
};

Font::~Font() = default;

std::shared_ptr<const Font> Font::load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->data.resize(static_cast<usize>(size));
    const usize got = std::fread(impl->data.data(), 1, impl->data.size(), f);
    std::fclose(f);
    if (got != impl->data.size()) return nullptr;
    const int offset = stbtt_GetFontOffsetForIndex(impl->data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&impl->info, impl->data.data(), offset)) return nullptr;
    stbtt_GetFontVMetrics(&impl->info, &impl->ascent, &impl->descent, &impl->lineGap);
    impl->blob = hb_blob_create(reinterpret_cast<const char*>(impl->data.data()), static_cast<unsigned>(impl->data.size()),
                                HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    impl->face = hb_face_create(impl->blob, 0);
    impl->hb = hb_font_create(impl->face);
    const unsigned upem = hb_face_get_upem(impl->face);
    hb_font_set_scale(impl->hb, static_cast<int>(upem), static_cast<int>(upem));
    auto font = std::shared_ptr<Font>(new Font());
    font->impl_ = std::move(impl);
    return font;
}

namespace {
std::mutex g_fontMutex;
std::string g_fontPath;
std::shared_ptr<const Font> g_font;
bool g_fontTried = false;

/// UTF-8 → pontos de código (inválido vira U+FFFD).
std::vector<u32> decode_utf8(const std::string& s) {
    std::vector<u32> out;
    out.reserve(s.size());
    for (usize i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        u32 cp = 0xFFFD;
        usize n = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 0x6 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<u8>(s[i + 1]) & 0x3Fu); n = 2; }
        else if ((c >> 4) == 0xE && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<u8>(s[i + 2]) & 0x3Fu);
            n = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 12) | ((static_cast<u8>(s[i + 2]) & 0x3Fu) << 6)
               | (static_cast<u8>(s[i + 3]) & 0x3Fu);
            n = 4;
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}

// --- Fontes de reserva (fallback) ----------------------------------------------
// Caractere que a fonte escolhida não tem (árabe no Roboto, CJK, hebraico…)
// vai para a primeira fonte do aparelho que tem — carregadas sob demanda.
std::mutex g_fallbackMutex;
std::vector<std::shared_ptr<const Font>> g_fallbacks;
bool g_fallbacksTried = false;

const std::vector<std::shared_ptr<const Font>>& fallbacks() {
    std::lock_guard<std::mutex> lock(g_fallbackMutex);
    if (!g_fallbacksTried) {
        g_fallbacksTried = true;
        for (const char* c : {"/system/fonts/NotoNaskhArabic-Regular.ttf", "/system/fonts/NotoSansArabic-Regular.ttf",
                              "/system/fonts/NotoSansHebrew-Regular.ttf", "/system/fonts/NotoSansDevanagari-Regular.ttf",
                              "/system/fonts/NotoSansThai-Regular.ttf", "/system/fonts/NotoSansCJK-Regular.ttc",
                              "/system/fonts/DroidSansFallback.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                              "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/seguisym.ttf",
                              "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"}) {
            if (auto f = Font::load(c)) g_fallbacks.push_back(std::move(f));
        }
    }
    return g_fallbacks;
}

bool is_mark(u32 cp) {   // combinantes, seletores de variação, ZWJ/ZWNJ: seguem o anterior
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F)
        || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200C || cp == 0x200D
        || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x20D0 && cp <= 0x20FF);
}

/// Direção forte do caractere: 1 = RTL, 0 = LTR, −1 = neutro/fraco
/// (espaço, pontuação, dígitos seguem o contexto).
int strong_dir(u32 cp) {
    if ((cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)
        || (cp >= 0x10800 && cp <= 0x10FFF) || (cp >= 0x1E800 && cp <= 0x1EFFF)) {
        return (cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9) ? -1 : 1;   // dígitos árabes: fracos
    }
    if (cp < 0x80) return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') ? 0 : -1;
    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F) || is_mark(cp)) return -1;
    if (cp >= 0x00A1 && cp <= 0x00BF) return -1;
    return 0;
}

struct Glyph {
    const Font::Impl* font = nullptr;
    int id = 0;          ///< índice do glifo NA fonte dele
    f32 x = 0, y = 0;    ///< px (na escala pedida) a partir do começo da linha, na linha de base
    f32 fs = 1;          ///< px por unidade daquela fonte
    u32 cluster = 0;     ///< índice do 1º caractere (na linha) que gerou o glifo
};

struct Line {
    std::vector<Glyph> glyphs;
    f32 width = 0.0f;
    u32 chars = 0;
};

/// Quebra em linhas e faz o shaping (HarfBuzz) de cada uma em `pxSize`
/// (tamanho em px de textura). Runs de mesma direção e mesma fonte; parágrafo
/// RTL (1º forte é árabe/hebraico) inverte a ordem dos runs na tela. Bidi
/// simplificado: sem marcas de embedding explícitas (LRE/RLE…).
std::vector<Line> layout(const Font::Impl& f, const TextData& t, f32 pxSize, f32 trackingPx) {
    std::vector<std::vector<u32>> lineCps(1);
    for (u32 cp : decode_utf8(t.content)) {
        if (cp == '\n') { lineCps.emplace_back(); continue; }
        if (cp == '\r') continue;
        lineCps.back().push_back(cp);
    }
    std::vector<Line> lines(lineCps.size());
    hb_buffer_t* buf = hb_buffer_create();
    for (usize li = 0; li < lineCps.size(); ++li) {
        const std::vector<u32>& cps = lineCps[li];
        Line& L = lines[li];
        L.chars = static_cast<u32>(cps.size());
        if (cps.empty()) continue;
        // Fonte e direção por caractere.
        std::vector<const Font::Impl*> fontOf(cps.size(), &f);
        std::vector<int> dir(cps.size(), -1);
        int para = -1;
        for (usize i = 0; i < cps.size(); ++i) {
            dir[i] = strong_dir(cps[i]);
            if (para < 0 && dir[i] >= 0) para = dir[i];
            if (is_mark(cps[i]) && i > 0) { fontOf[i] = fontOf[i - 1]; continue; }
            if (cps[i] == ' ' || stbtt_FindGlyphIndex(&f.info, static_cast<int>(cps[i])) != 0) continue;
            for (const auto& fb : fallbacks()) {
                if (stbtt_FindGlyphIndex(&fb->impl().info, static_cast<int>(cps[i])) != 0) { fontOf[i] = &fb->impl(); break; }
            }
        }
        if (para < 0) para = 0;
        // Neutros: a direção dos vizinhos fortes se iguais, senão a do parágrafo.
        for (usize i = 0; i < cps.size(); ++i) {
            if (dir[i] >= 0) continue;
            int before = -1, after = -1;
            for (usize k = i; k-- > 0;) if (strong_dir(cps[k]) >= 0) { before = strong_dir(cps[k]); break; }
            for (usize k = i + 1; k < cps.size(); ++k) if (strong_dir(cps[k]) >= 0) { after = strong_dir(cps[k]); break; }
            const bool digit = (cps[i] >= '0' && cps[i] <= '9') || (cps[i] >= 0x0660 && cps[i] <= 0x0669);
            dir[i] = digit ? 0 : (before >= 0 && before == after ? before : para);
        }
        // Runs (direção, fonte) em ordem lógica.
        struct Run { usize start, len; int dir; const Font::Impl* font; };
        std::vector<Run> runs;
        for (usize i = 0; i < cps.size(); ++i) {
            if (!runs.empty() && runs.back().dir == dir[i] && runs.back().font == fontOf[i]) { ++runs.back().len; continue; }
            runs.push_back({i, 1, dir[i], fontOf[i]});
        }
        // Parágrafo RTL: runs da direita para a esquerda (e runs LTR vizinhos
        // continuam na ordem deles — números, palavras latinas).
        std::vector<usize> order(runs.size());
        for (usize k = 0; k < runs.size(); ++k) order[k] = k;
        if (para == 1) {
            std::reverse(order.begin(), order.end());
            // Sequências LTR consecutivas voltam à ordem lógica entre si.
            for (usize a = 0; a < order.size();) {
                usize b = a;
                while (b < order.size() && runs[order[b]].dir == 0) ++b;
                if (b - a > 1) std::reverse(order.begin() + static_cast<long>(a), order.begin() + static_cast<long>(b));
                a = b == a ? a + 1 : b;
            }
        }
        f32 pen = 0.0f;
        for (usize k : order) {
            const Run& r = runs[k];
            const Font::Impl& rf = *r.font;
            const f32 fs = stbtt_ScaleForPixelHeight(&rf.info, pxSize);
            hb_buffer_clear_contents(buf);
            hb_buffer_add_codepoints(buf, cps.data(), static_cast<int>(cps.size()), static_cast<unsigned>(r.start), static_cast<int>(r.len));
            hb_buffer_guess_segment_properties(buf);
            hb_buffer_set_direction(buf, r.dir == 1 ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            hb_shape(rf.hb, buf, nullptr, 0);
            unsigned n = 0;
            const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
            const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
            for (unsigned g = 0; g < n; ++g) {
                Glyph gl;
                gl.font = &rf;
                gl.id = static_cast<int>(info[g].codepoint);
                gl.fs = fs;
                gl.cluster = info[g].cluster;
                gl.x = pen + static_cast<f32>(pos[g].x_offset) * fs;
                gl.y = -static_cast<f32>(pos[g].y_offset) * fs;
                L.glyphs.push_back(gl);
                pen += static_cast<f32>(pos[g].x_advance) * fs;
                if (pos[g].x_advance != 0) pen += trackingPx;
            }
        }
        if (!L.glyphs.empty() && trackingPx != 0.0f) pen -= trackingPx;   // sem espaço depois do último
        L.width = std::max(0.0f, pen);
    }
    hb_buffer_destroy(buf);
    return lines;
}

} // namespace

void set_default_font_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_fontMutex);
    if (path == g_fontPath) return;
    g_fontPath = path;
    g_font.reset();
    g_fontTried = false;
}

std::shared_ptr<const Font> default_font() {
    std::lock_guard<std::mutex> lock(g_fontMutex);
    if (g_font || g_fontTried) return g_font;
    g_fontTried = true;
    std::vector<std::string> candidates;
    if (!g_fontPath.empty()) candidates.push_back(g_fontPath);
    for (const char* c : {"/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf",
                          "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf",
                          "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                          "/System/Library/Fonts/Helvetica.ttc", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) {
        candidates.emplace_back(c);
    }
    for (const std::string& p : candidates) {
        if ((g_font = Font::load(p))) {
            AUREA_LOG_INFO("texto: fonte padrao %s", p.c_str());
            return g_font;
        }
    }
    AUREA_LOG_ERROR("texto: nenhuma fonte encontrada — camadas de texto nao desenham");
    return nullptr;
}

TextExtent measure(const Font& font, const TextData& t) {
    const Font::Impl& f = font.impl();
    const f32 size = std::max(1.0f, t.size);
    const f32 fs = stbtt_ScaleForPixelHeight(&f.info, size);
    const f32 tracking = t.tracking * size / 1000.0f;
    const std::vector<Line> lines = layout(f, t, size, tracking);
    f32 w = 0.0f;
    for (const Line& l : lines) w = std::max(w, l.width);
    const f32 lineAdvance = size * std::max(0.1f, t.lineHeight);
    const f32 ascent = static_cast<f32>(f.ascent) * fs, descent = static_cast<f32>(-f.descent) * fs;
    const f32 h = ascent + descent + lineAdvance * static_cast<f32>(lines.size() - 1);
    return TextExtent{std::max(1.0f, std::ceil(w)), std::max(1.0f, std::ceil(h))};
}

bool outline(const Font& font, const TextData& t, std::vector<std::vector<Vec2>>& contours) {
    const Font::Impl& f = font.impl();
    const f32 size = std::max(1.0f, t.size);
    const f32 fs0 = stbtt_ScaleForPixelHeight(&f.info, size);
    const f32 tracking = t.tracking * size / 1000.0f;
    const std::vector<Line> lines = layout(f, t, size, tracking);
    f32 maxW = 0.0f;
    for (const Line& l : lines) maxW = std::max(maxW, l.width);
    const f32 lineAdvance = size * std::max(0.1f, t.lineHeight);
    const f32 ascent = static_cast<f32>(f.ascent) * fs0;
    // Passos por curva: ~1 segmento a cada 2 px do tamanho pedido, entre 4 e 16.
    const int steps = std::clamp(static_cast<int>(size / 8.0f), 4, 16);
    contours.clear();
    for (usize li = 0; li < lines.size(); ++li) {
        const Line& l = lines[li];
        f32 x0 = 0.0f;
        if (t.alignment == 1) x0 += (maxW - l.width) * 0.5f;
        else if (t.alignment == 2) x0 += maxW - l.width;
        const f32 baseline = ascent + lineAdvance * static_cast<f32>(li);
        for (const Glyph& gl : l.glyphs) {
            stbtt_vertex* v = nullptr;
            const int n = stbtt_GetGlyphShape(&gl.font->info, gl.id, &v);
            const f32 x = x0 + gl.x, fs = gl.fs;
            auto P = [&](f32 fx, f32 fy) { return Vec2{x + fx * fs, baseline + gl.y - fy * fs}; };
            Vec2 cur{0, 0};
            for (int k = 0; k < n; ++k) {
                const stbtt_vertex& e = v[k];
                const Vec2 to = P(e.x, e.y);
                if (e.type == STBTT_vmove) {
                    contours.emplace_back();
                    contours.back().push_back(to);
                } else if (!contours.empty()) {
                    std::vector<Vec2>& c = contours.back();
                    if (e.type == STBTT_vline) {
                        c.push_back(to);
                    } else if (e.type == STBTT_vcurve) {
                        const Vec2 q = P(e.cx, e.cy);
                        for (int st = 1; st <= steps; ++st) {
                            const f32 u = static_cast<f32>(st) / static_cast<f32>(steps), w = 1.0f - u;
                            c.push_back(Vec2{w * w * cur.x + 2 * w * u * q.x + u * u * to.x, w * w * cur.y + 2 * w * u * q.y + u * u * to.y});
                        }
                    } else if (e.type == STBTT_vcubic) {
                        const Vec2 q0 = P(e.cx, e.cy), q1 = P(e.cx1, e.cy1);
                        for (int st = 1; st <= steps; ++st) {
                            const f32 u = static_cast<f32>(st) / static_cast<f32>(steps), w = 1.0f - u;
                            c.push_back(Vec2{w * w * w * cur.x + 3 * w * w * u * q0.x + 3 * w * u * u * q1.x + u * u * u * to.x,
                                             w * w * w * cur.y + 3 * w * w * u * q0.y + 3 * w * u * u * q1.y + u * u * u * to.y});
                        }
                    }
                }
                cur = to;
            }
            if (v) stbtt_FreeShape(&gl.font->info, v);
        }
    }
    // Fecho explícito repetido (último == primeiro) sai; contornos degenerados também.
    for (std::vector<Vec2>& c : contours) {
        while (c.size() > 1 && std::fabs(c.back().x - c.front().x) < 1e-4f && std::fabs(c.back().y - c.front().y) < 1e-4f) c.pop_back();
    }
    contours.erase(std::remove_if(contours.begin(), contours.end(), [](const std::vector<Vec2>& c) { return c.size() < 3; }),
                   contours.end());
    return !contours.empty();
}

u32 shaped_glyphs(const Font& font, const TextData& t, std::vector<ShapedGlyph>& out) {
    const Font::Impl& f = font.impl();
    const f32 size = std::max(1.0f, t.size);
    const std::vector<Line> lines = layout(f, t, size, t.tracking * size / 1000.0f);
    out.clear();
    for (usize li = 0; li < lines.size(); ++li) {
        for (const Glyph& g : lines[li].glyphs) {
            ShapedGlyph s2;
            s2.glyph = static_cast<u32>(g.id);
            s2.cluster = g.cluster;
            s2.line = static_cast<u32>(li);
            s2.x = g.x;
            s2.y = g.y;
            s2.fallback = g.font != &f;
            out.push_back(s2);
        }
    }
    return static_cast<u32>(out.size());
}

u64 raster_key(const TextData& t, f32 scale) noexcept {
    u64 h = 1469598103934665603ull;
    auto mix = [&](const void* p, usize n) {
        const u8* b = static_cast<const u8*>(p);
        for (usize i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    mix(t.content.data(), t.content.size());
    mix(t.fontFamily.data(), t.fontFamily.size());
    mix(&t.fontWeight, sizeof(t.fontWeight));
    mix(&t.fontItalic, sizeof(t.fontItalic));
    mix(t.fontPath.data(), t.fontPath.size());
    mix(&t.size, sizeof(t.size));
    mix(&t.color, sizeof(t.color));
    mix(&t.strokeWidth, sizeof(t.strokeWidth));
    mix(&t.strokeColor, sizeof(t.strokeColor));
    mix(&t.alignment, sizeof(t.alignment));
    mix(&t.lineHeight, sizeof(t.lineHeight));
    mix(&t.tracking, sizeof(t.tracking));
    mix(&scale, sizeof(scale));
    return h;
}

bool rasterize(const Font& font, const TextData& t, f32 scale, TextRaster& out) {
    const Font::Impl& f = font.impl();
    scale = std::clamp(scale, 0.125f, 8.0f);
    const f32 size = std::max(1.0f, t.size);
    const f32 fsLayer = stbtt_ScaleForPixelHeight(&f.info, size);
    const f32 fs = fsLayer * scale;                       // px de textura por unidade de fonte
    const f32 tracking = t.tracking * size / 1000.0f * scale;
    const std::vector<Line> lines = layout(f, t, size * scale, tracking);
    f32 maxW = 0.0f;
    for (const Line& l : lines) maxW = std::max(maxW, l.width);

    const f32 stroke = std::max(0.0f, t.strokeWidth) * scale;
    const bool hasStroke = stroke > 0.0f && t.strokeColor.w > 0.0f;
    const i32 pad = static_cast<i32>(std::ceil(hasStroke ? stroke + 2.0f : 2.0f));
    const f32 lineAdvance = size * std::max(0.1f, t.lineHeight) * scale;
    const f32 ascent = static_cast<f32>(f.ascent) * fs, descent = static_cast<f32>(-f.descent) * fs;
    const i32 W = std::max(1, static_cast<i32>(std::ceil(maxW)) + 2 * pad);
    const i32 H = std::max(1, static_cast<i32>(std::ceil(ascent + descent + lineAdvance * static_cast<f32>(lines.size() - 1))) + 2 * pad);
    if (static_cast<i64>(W) * H > 4096ll * 4096ll) return false;

    std::vector<u8> fill(static_cast<usize>(W) * H, 0);
    // Distância ao contorno (px de textura, positiva fora), só com contorno.
    std::vector<f32> dist;
    if (hasStroke) dist.assign(static_cast<usize>(W) * H, 1e9f);
    const i32 sdfPad = static_cast<i32>(std::ceil(stroke)) + 2;
    const f32 onedge = 128.0f;
    const f32 distScale = 127.0f / static_cast<f32>(sdfPad);

    for (usize li = 0; li < lines.size(); ++li) {
        const Line& l = lines[li];
        f32 x = static_cast<f32>(pad);
        if (t.alignment == 1) x += (maxW - l.width) * 0.5f;
        else if (t.alignment == 2) x += maxW - l.width;
        const f32 baseline = static_cast<f32>(pad) + ascent + lineAdvance * static_cast<f32>(li);
        for (const Glyph& gl : l.glyphs) {
            const stbtt_fontinfo* gi = &gl.font->info;
            const f32 gfs = gl.fs;
            const f32 gx = x + gl.x, gy = baseline + gl.y;
            const f32 xs = gx - std::floor(gx);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetGlyphBitmapBoxSubpixel(gi, gl.id, gfs, gfs, xs, 0.0f, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            const int ox = static_cast<int>(std::floor(gx)) + x0;
            const int oy = static_cast<int>(std::floor(gy)) + y0;
            if (gw > 0 && gh > 0) {
                std::vector<u8> g(static_cast<usize>(gw) * gh);
                stbtt_MakeGlyphBitmapSubpixel(gi, g.data(), gw, gh, gw, gfs, gfs, xs, 0.0f, gl.id);
                for (int yy = 0; yy < gh; ++yy) {
                    const int py = oy + yy;
                    if (py < 0 || py >= H) continue;
                    for (int xx = 0; xx < gw; ++xx) {
                        const int px = ox + xx;
                        if (px < 0 || px >= W) continue;
                        u8& d = fill[static_cast<usize>(py) * W + px];
                        d = std::max(d, g[static_cast<usize>(yy) * gw + xx]);
                    }
                }
            }
            if (hasStroke) {
                int sw = 0, sh = 0, sx = 0, sy = 0;
                u8* sdf = stbtt_GetGlyphSDF(gi, gfs, gl.id, sdfPad, static_cast<u8>(onedge), distScale, &sw, &sh, &sx, &sy);
                if (sdf) {
                    const int bx = static_cast<int>(std::floor(gx)) + sx;
                    const int by = static_cast<int>(std::floor(gy)) + sy;
                    for (int yy = 0; yy < sh; ++yy) {
                        const int py = by + yy;
                        if (py < 0 || py >= H) continue;
                        for (int xx = 0; xx < sw; ++xx) {
                            const int px = bx + xx;
                            if (px < 0 || px >= W) continue;
                            const f32 dd = (onedge - static_cast<f32>(sdf[yy * sw + xx])) / distScale;
                            f32& cur = dist[static_cast<usize>(py) * W + px];
                            cur = std::min(cur, dd);
                        }
                    }
                    stbtt_FreeSDF(sdf, nullptr);
                }
            }
        }
    }

    // Composição: preenchimento por cima do contorno (alfa reto, sRGB).
    out.rgba.assign(static_cast<usize>(W) * H * 4, 0);
    const Vec4 fc = t.color, sc = t.strokeColor;
    for (usize i = 0; i < static_cast<usize>(W) * H; ++i) {
        const f32 fa = static_cast<f32>(fill[i]) / 255.0f * fc.w;
        f32 sa = 0.0f;
        if (hasStroke) sa = std::clamp(stroke + 0.5f - dist[i], 0.0f, 1.0f) * sc.w;
        const f32 a = fa + sa * (1.0f - fa);
        if (a <= 0.0f) continue;
        const f32 r = (fc.x * fa + sc.x * sa * (1.0f - fa)) / a;
        const f32 g = (fc.y * fa + sc.y * sa * (1.0f - fa)) / a;
        const f32 b = (fc.z * fa + sc.z * sa * (1.0f - fa)) / a;
        u8* o = out.rgba.data() + i * 4;
        o[0] = static_cast<u8>(std::lround(std::clamp(r, 0.0f, 1.0f) * 255.0f));
        o[1] = static_cast<u8>(std::lround(std::clamp(g, 0.0f, 1.0f) * 255.0f));
        o[2] = static_cast<u8>(std::lround(std::clamp(b, 0.0f, 1.0f) * 255.0f));
        o[3] = static_cast<u8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
    }
    out.width = static_cast<u32>(W);
    out.height = static_cast<u32>(H);
    out.layerWidth = static_cast<f32>(W) / scale;
    out.layerHeight = static_cast<f32>(H) / scale;
    out.padding = static_cast<f32>(pad) / scale;
    return true;
}

} // namespace aurea::text
