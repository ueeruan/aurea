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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace aurea::text {

struct Font::Impl {
    std::vector<u8> data;
    stbtt_fontinfo info{};
    int ascent = 0, descent = 0, lineGap = 0;
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

struct Line {
    std::vector<u32> cps;
    f32 width = 0.0f;   // px de fonte na escala pedida
};

/// Quebra em linhas e mede cada uma na escala `px` (px por unidade de fonte).
std::vector<Line> layout(const Font::Impl& f, const TextData& t, f32 fontScale, f32 trackingPx) {
    std::vector<Line> lines(1);
    for (u32 cp : decode_utf8(t.content)) {
        if (cp == '\n') { lines.emplace_back(); continue; }
        if (cp == '\r') continue;
        lines.back().cps.push_back(cp);
    }
    for (Line& l : lines) {
        f32 x = 0.0f;
        for (usize i = 0; i < l.cps.size(); ++i) {
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&f.info, static_cast<int>(l.cps[i]), &adv, &lsb);
            x += static_cast<f32>(adv) * fontScale;
            if (i + 1 < l.cps.size()) {
                x += static_cast<f32>(stbtt_GetCodepointKernAdvance(&f.info, static_cast<int>(l.cps[i]),
                                                                    static_cast<int>(l.cps[i + 1]))) * fontScale;
                x += trackingPx;
            }
        }
        l.width = x;
    }
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
    const std::vector<Line> lines = layout(f, t, fs, tracking);
    f32 w = 0.0f;
    for (const Line& l : lines) w = std::max(w, l.width);
    const f32 lineAdvance = size * std::max(0.1f, t.lineHeight);
    const f32 ascent = static_cast<f32>(f.ascent) * fs, descent = static_cast<f32>(-f.descent) * fs;
    const f32 h = ascent + descent + lineAdvance * static_cast<f32>(lines.size() - 1);
    return TextExtent{std::max(1.0f, std::ceil(w)), std::max(1.0f, std::ceil(h))};
}

u64 raster_key(const TextData& t, f32 scale) noexcept {
    u64 h = 1469598103934665603ull;
    auto mix = [&](const void* p, usize n) {
        const u8* b = static_cast<const u8*>(p);
        for (usize i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    mix(t.content.data(), t.content.size());
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
    const std::vector<Line> lines = layout(f, t, fs, tracking);
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
        for (usize i = 0; i < l.cps.size(); ++i) {
            const int cp = static_cast<int>(l.cps[i]);
            const f32 xs = x - std::floor(x);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetCodepointBitmapBoxSubpixel(&f.info, cp, fs, fs, xs, 0.0f, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            const int ox = static_cast<int>(std::floor(x)) + x0;
            const int oy = static_cast<int>(std::floor(baseline)) + y0;
            if (gw > 0 && gh > 0) {
                std::vector<u8> g(static_cast<usize>(gw) * gh);
                stbtt_MakeCodepointBitmapSubpixel(&f.info, g.data(), gw, gh, gw, fs, fs, xs, 0.0f, cp);
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
                u8* sdf = stbtt_GetCodepointSDF(&f.info, fs, cp, sdfPad, static_cast<u8>(onedge), distScale, &sw, &sh, &sx, &sy);
                if (sdf) {
                    const int bx = static_cast<int>(std::floor(x)) + sx;
                    const int by = static_cast<int>(std::floor(baseline)) + sy;
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
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
            x += static_cast<f32>(adv) * fs;
            if (i + 1 < l.cps.size()) {
                x += static_cast<f32>(stbtt_GetCodepointKernAdvance(&f.info, cp, static_cast<int>(l.cps[i + 1]))) * fs + tracking;
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
