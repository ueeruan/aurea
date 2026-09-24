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

#include "aurea/text/FontManager.hpp"

#include "hb.h"
#include "hb-ot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

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

usize Font::memory_bytes() const noexcept { return impl_ ? impl_->data.size() : 0; }

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
// vai para a primeira fonte do aparelho que tem. Carregadas UMA A UMA, só
// quando um caractere precisa (8E): antes, o primeiro caractere ausente
// carregava as 12 candidatas de uma vez — o CJK sozinho tem ~20 MB. A busca
// começa pelas fontes da escrita do caractere; as outras só se ela não tiver.
constexpr const char* kFallbackPaths[] = {
    "/system/fonts/NotoNaskhArabic-Regular.ttf", "/system/fonts/NotoSansArabic-Regular.ttf",   // 0, 1 árabe
    "/system/fonts/NotoSansHebrew-Regular.ttf",                                                  // 2 hebraico
    "/system/fonts/NotoSansDevanagari-Regular.ttf",                                              // 3 devanágari
    "/system/fonts/NotoSansThai-Regular.ttf",                                                    // 4 tailandês
    "/system/fonts/NotoSansCJK-Regular.ttc", "/system/fonts/DroidSansFallback.ttf",              // 5, 6 CJK
    "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",                                // 7, 8 geral
    "C:/Windows/Fonts/msyh.ttc",                                                                 // 9 CJK (Windows)
    "C:/Windows/Fonts/seguisym.ttf", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",     // 10, 11 símbolos
};
constexpr usize kFallbackCount = sizeof(kFallbackPaths) / sizeof(kFallbackPaths[0]);
std::mutex g_fallbackMutex;
std::shared_ptr<const Font> g_fallbacks[kFallbackCount];
bool g_fallbackTried[kFallbackCount] = {};
u32 g_fallbackLoads = 0;
u64 g_fallbackBytes = 0;

/// Ordem de busca pela escrita do caractere (índices de kFallbackPaths).
void fallback_order(u32 cp, u8 (&order)[kFallbackCount], usize& n) {
    static constexpr u8 kArabic[] = {0, 1}, kHebrew[] = {2}, kDeva[] = {3}, kThai[] = {4}, kCjk[] = {5, 9, 6},
                        kGeneral[] = {7, 8, 10, 11, 6};
    const u8* first = kGeneral;
    usize nf = sizeof(kGeneral);
    if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08FF)
        || (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)) { first = kArabic; nf = sizeof(kArabic); }
    else if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0xFB1D && cp <= 0xFB4F)) { first = kHebrew; nf = sizeof(kHebrew); }
    else if (cp >= 0x0900 && cp <= 0x097F) { first = kDeva; nf = sizeof(kDeva); }
    else if (cp >= 0x0E00 && cp <= 0x0E7F) { first = kThai; nf = sizeof(kThai); }
    else if ((cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF)
             || (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x2FFFF)) { first = kCjk; nf = sizeof(kCjk); }
    bool used[kFallbackCount] = {};
    n = 0;
    for (usize i = 0; i < nf; ++i) { order[n++] = first[i]; used[first[i]] = true; }
    for (u8 i = 0; i < kFallbackCount; ++i) if (!used[i]) order[n++] = i;
}

/// A primeira fonte de reserva que tem o caractere (nula se nenhuma).
const Font::Impl* fallback_for(u32 cp) {
    u8 order[kFallbackCount];
    usize n = 0;
    fallback_order(cp, order, n);
    std::lock_guard<std::mutex> lock(g_fallbackMutex);
    for (usize k = 0; k < n; ++k) {
        const u8 i = order[k];
        if (!g_fallbackTried[i]) {
            g_fallbackTried[i] = true;
            g_fallbacks[i] = Font::load(kFallbackPaths[i]);
            if (g_fallbacks[i]) {
                ++g_fallbackLoads;
                g_fallbackBytes += g_fallbacks[i]->memory_bytes();
            }
        }
        if (g_fallbacks[i] && stbtt_FindGlyphIndex(&g_fallbacks[i]->impl().info, static_cast<int>(cp)) != 0) return &g_fallbacks[i]->impl();
    }
    return nullptr;
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
    f32 x = 0, y = 0;    ///< px (escala pedida): x a partir do começo da linha / da caixa depois de place()
    f32 fs = 1;          ///< px por unidade daquela fonte
    f32 advance = 0;     ///< avanço deste glifo (px)
    u32 cluster = 0;     ///< caractere de origem (índice lógico no texto inteiro)
    u32 line = 0;
};

struct Line {
    std::vector<Glyph> glyphs;
    f32 width = 0.0f;
    f32 scale = 1.0f;    ///< maior escala de trecho na linha (linha de base e entrelinha)
};

struct CharAttr {
    const Font::Impl* font = nullptr;
    f32 scale = 1.0f;
};

/// Shaping de cps[a, b) (um trecho de parágrafo), com contexto do parágrafo.
/// `para` = direção do parágrafo. Glifos em ordem visual, x a partir de 0.
Line shape_span(const std::vector<u32>& cps, usize a, usize b, u32 globalBase, const std::vector<CharAttr>& attr, int para,
                f32 pxSize, f32 trackingPx, hb_buffer_t* buf) {
    Line L;
    if (b <= a) return L;
    std::vector<int> dir(cps.size(), -1);
    for (usize i = a; i < b; ++i) dir[i] = strong_dir(cps[i]);
    for (usize i = a; i < b; ++i) {
        if (dir[i] >= 0) continue;
        int before = -1, after = -1;
        for (usize k = i; k-- > a;) if (strong_dir(cps[k]) >= 0) { before = strong_dir(cps[k]); break; }
        for (usize k = i + 1; k < b; ++k) if (strong_dir(cps[k]) >= 0) { after = strong_dir(cps[k]); break; }
        const bool digit = (cps[i] >= '0' && cps[i] <= '9') || (cps[i] >= 0x0660 && cps[i] <= 0x0669);
        dir[i] = digit ? 0 : (before >= 0 && before == after ? before : para);
    }
    struct Run { usize start, len; int dir; const Font::Impl* font; f32 scale; };
    std::vector<Run> runs;
    for (usize i = a; i < b; ++i) {
        if (!runs.empty() && runs.back().dir == dir[i] && runs.back().font == attr[i].font && runs.back().scale == attr[i].scale) {
            ++runs.back().len;
            continue;
        }
        runs.push_back({i, 1, dir[i], attr[i].font, attr[i].scale});
    }
    std::vector<usize> order(runs.size());
    for (usize k = 0; k < runs.size(); ++k) order[k] = k;
    if (para == 1) {
        std::reverse(order.begin(), order.end());
        for (usize x = 0; x < order.size();) {
            usize y = x;
            while (y < order.size() && runs[order[y]].dir == 0) ++y;
            if (y - x > 1) std::reverse(order.begin() + static_cast<long>(x), order.begin() + static_cast<long>(y));
            x = y == x ? x + 1 : y;
        }
    }
    f32 pen = 0.0f;
    for (usize k : order) {
        const Run& r = runs[k];
        const Font::Impl& rf = *r.font;
        const f32 fs = stbtt_ScaleForPixelHeight(&rf.info, pxSize * r.scale);
        L.scale = std::max(L.scale, r.scale);
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
            gl.cluster = globalBase + info[g].cluster;
            gl.x = pen + static_cast<f32>(pos[g].x_offset) * fs;
            gl.y = -static_cast<f32>(pos[g].y_offset) * fs;
            gl.advance = static_cast<f32>(pos[g].x_advance) * fs + (pos[g].x_advance != 0 ? trackingPx : 0.0f);
            pen += gl.advance;
            L.glyphs.push_back(gl);
        }
    }
    if (!L.glyphs.empty() && trackingPx != 0.0f) pen -= trackingPx;
    L.width = std::max(0.0f, pen);
    return L;
}

/// Texto posicionado (escala pedida): glifos com x/y finais na caixa (linha
/// de base), largura/altura da caixa sem margem.
struct Placed {
    std::vector<Glyph> glyphs;
    std::vector<f32> baselines;
    f32 width = 0, height = 0;
    u32 lines = 0;
    f32 sizeFactor = 1;   ///< modo "encolher para caber": fator aplicado ao tamanho
};

std::string default_family();

void place_once(const Font::Impl& f, const TextData& t, f32 pxScale, f32 factor, Placed& out) {
    const f32 size = std::max(1.0f, t.size) * factor;
    const f32 pxSize = size * pxScale;
    const f32 trackingPx = t.tracking * size / 1000.0f * pxScale;
    const f32 wrap = t.boxMode > 0 ? std::max(1.0f, t.box.w) * pxScale : 0.0f;
    const std::vector<u32> all = decode_utf8(t.content);
    // Atributos por caractere (índice lógico global): fonte (trecho → reserva) e escala.
    std::vector<CharAttr> attr(all.size());
    std::shared_ptr<const Font> spanFonts[10];
    auto spanFont = [&](u16 weight) -> const Font::Impl* {
        const u32 slot = std::min<u32>(9, weight / 100);
        if (!spanFonts[slot]) {
            TextData q;
            q.fontFamily = t.fontFamily.empty() ? default_family() : t.fontFamily;
            q.fontWeight = weight;
            q.fontItalic = t.fontItalic;
            spanFonts[slot] = FontManager::instance().font_for(q);
        }
        return spanFonts[slot] ? &spanFonts[slot]->impl() : &f;
    };
    for (usize i = 0; i < all.size(); ++i) {
        attr[i].font = &f;
        for (const TextSpan& sp : t.spans) {
            if (i < sp.start || i >= sp.end) continue;
            if (sp.weight) attr[i].font = spanFont(sp.weight);
            if (sp.scale > 0.0f) attr[i].scale = std::clamp(sp.scale, 0.1f, 10.0f);
        }
        if (is_mark(all[i]) && i > 0) { attr[i].font = attr[i - 1].font; continue; }
        const u32 cp = all[i];
        if (cp == ' ' || cp == '\n' || stbtt_FindGlyphIndex(&attr[i].font->info, static_cast<int>(cp)) != 0) continue;
        if (stbtt_FindGlyphIndex(&f.info, static_cast<int>(cp)) != 0) { attr[i].font = &f; continue; }
        if (const Font::Impl* fb = fallback_for(cp)) attr[i].font = fb;
    }
    std::vector<Line> lines;
    hb_buffer_t* buf = hb_buffer_create();
    usize p0 = 0;
    for (usize i = 0; i <= all.size(); ++i) {
        if (i < all.size() && all[i] != '\n') continue;
        // Parágrafo all[p0, i): cps locais (sem \r), mapa para o índice global.
        std::vector<u32> cps;
        std::vector<u32> gidx;
        std::vector<CharAttr> at;
        for (usize k = p0; k < i; ++k) {
            if (all[k] == '\r') continue;
            cps.push_back(all[k]);
            gidx.push_back(static_cast<u32>(k));
            at.push_back(attr[k]);
        }
        int para = -1;
        for (u32 cp : cps) if (strong_dir(cp) >= 0) { para = strong_dir(cp); break; }
        if (para < 0) para = 0;
        auto shapeRange = [&](usize a, usize b) {
            Line L = shape_span(cps, a, b, 0, at, para, pxSize, trackingPx, buf);
            for (Glyph& g : L.glyphs) g.cluster = g.cluster < gidx.size() ? gidx[g.cluster] : static_cast<u32>(i);
            return L;
        };
        if (cps.empty()) { lines.emplace_back(); p0 = i + 1; continue; }
        usize a = 0;
        while (a < cps.size()) {
            Line L = shapeRange(a, cps.size());
            if (wrap <= 0.0f || L.width <= wrap) { lines.push_back(std::move(L)); break; }
            // Largura do prefixo lógico [a, b): soma dos avanços dos glifos dele.
            auto prefix = [&](usize b) {
                f32 w = 0;
                for (const Glyph& g : L.glyphs) {
                    const u32 local = static_cast<u32>(std::find(gidx.begin(), gidx.end(), g.cluster) - gidx.begin());
                    if (local >= a && local < b) w += g.advance;
                }
                return w;
            };
            usize best = 0;
            for (usize b = a + 1; b <= cps.size(); ++b) {
                if (prefix(b) > wrap) break;
                if (b < cps.size() && cps[b - 1] != ' ' && cps[b] == ' ') best = b;   // termina antes do espaço
                if (cps[b - 1] == ' ') best = b;
            }
            if (best <= a) {   // palavra maior que a linha: quebra dura
                best = a + 1;
                while (best < cps.size() && prefix(best + 1) <= wrap) ++best;
            }
            usize end = best;
            while (end > a && cps[end - 1] == ' ') --end;   // sem espaço no fim da linha
            lines.push_back(shapeRange(a, std::max(end, a + 1)));
            a = best;
            while (a < cps.size() && cps[a] == ' ') ++a;
        }
        p0 = i + 1;
    }
    hb_buffer_destroy(buf);
    // Caixa, alinhamento e linhas de base.
    const f32 fs0 = stbtt_ScaleForPixelHeight(&f.info, pxSize);
    const f32 asc = static_cast<f32>(f.ascent) * fs0, desc = static_cast<f32>(-f.descent) * fs0;
    const f32 lineAdvance = pxSize * std::max(0.1f, t.lineHeight);
    f32 maxW = 0;
    for (const Line& l : lines) maxW = std::max(maxW, l.width);
    const f32 boxW = wrap > 0.0f ? wrap : maxW;
    out = Placed{};
    out.lines = static_cast<u32>(lines.size());
    out.sizeFactor = factor;
    f32 baseline = 0;
    for (usize li = 0; li < lines.size(); ++li) {
        const Line& l = lines[li];
        baseline = li == 0 ? asc * l.scale : baseline + lineAdvance * l.scale;
        out.baselines.push_back(baseline);
        f32 x0 = 0;
        if (t.alignment == 1) x0 = (boxW - l.width) * 0.5f;
        else if (t.alignment == 2) x0 = boxW - l.width;
        for (Glyph g : l.glyphs) {
            g.x += x0;
            g.y += baseline;
            g.line = static_cast<u32>(li);
            out.glyphs.push_back(g);
        }
    }
    const f32 lastScale = lines.empty() ? 1.0f : lines.back().scale;
    out.width = boxW;
    out.height = lines.empty() ? asc + desc : baseline + desc * lastScale;
    if (t.boxMode >= 2) {
        const f32 boxH = std::max(1.0f, t.box.h) * pxScale;
        if (t.boxMode == 2) {
            // Caixa fixa: o que passa da altura some (linha inteira).
            std::vector<Glyph> kept;
            for (const Glyph& g : out.glyphs) if (out.baselines[g.line] + desc * lines[g.line].scale <= boxH + 0.5f) kept.push_back(g);
            out.glyphs.swap(kept);
        }
        out.height = boxH;
    }
}

void place(const Font::Impl& f, const TextData& t, f32 pxScale, Placed& out) {
    place_once(f, t, pxScale, 1.0f, out);
    if (t.boxMode != 3) return;
    // Encolher para caber: o maior tamanho (em passos de 5 %) cuja altura cabe.
    const f32 boxH = std::max(1.0f, t.box.h) * pxScale;
    f32 factor = 1.0f;
    Placed tmp = out;
    for (int it = 0; it < 40; ++it) {
        f32 used = 0;
        for (usize li = 0; li < tmp.baselines.size(); ++li) used = std::max(used, tmp.baselines[li]);
        const f32 fs0 = stbtt_ScaleForPixelHeight(&f.info, std::max(1.0f, t.size) * factor * pxScale);
        used += static_cast<f32>(-f.descent) * fs0;
        if (used <= boxH + 0.5f || factor < 0.06f) break;
        factor *= 0.95f;
        place_once(f, t, pxScale, factor, tmp);
    }
    out = std::move(tmp);
    out.height = boxH;
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

namespace {
std::string default_family() {
    static std::mutex m;
    static std::string fam;
    static bool tried = false;
    std::lock_guard<std::mutex> lock(m);
    if (!tried) {
        tried = true;
        for (const auto& e : FontManager::instance().list()) {
            auto d = default_font();
            if (d && FontManager::instance().load(e.path) == d) { fam = e.family; break; }
        }
    }
    return fam;
}
} // namespace

TextExtent measure(const Font& font, const TextData& t) {
    Placed p;
    place(font.impl(), t, 1.0f, p);
    return TextExtent{std::max(1.0f, std::ceil(p.width)), std::max(1.0f, std::ceil(p.height))};
}

bool outline(const Font& font, const TextData& t, std::vector<std::vector<Vec2>>& contours, i32 glyphIndex) {
    Placed p;
    place(font.impl(), t, 1.0f, p);
    const int steps = std::clamp(static_cast<int>(std::max(1.0f, t.size) / 8.0f), 4, 16);
    contours.clear();
    i32 placedIndex = 0;
    for (const Glyph& gl : p.glyphs) {
        if (glyphIndex >= 0 && placedIndex++ != glyphIndex) continue;
        stbtt_vertex* v = nullptr;
        const int n = stbtt_GetGlyphShape(&gl.font->info, gl.id, &v);
        const f32 fs = gl.fs;
        auto P = [&](f32 fx, f32 fy) { return Vec2{gl.x + fx * fs, gl.y - fy * fs}; };
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
    // Fecho explícito repetido (último == primeiro) sai; contornos degenerados também.
    for (std::vector<Vec2>& c : contours) {
        while (c.size() > 1 && std::fabs(c.back().x - c.front().x) < 1e-4f && std::fabs(c.back().y - c.front().y) < 1e-4f) c.pop_back();
    }
    contours.erase(std::remove_if(contours.begin(), contours.end(), [](const std::vector<Vec2>& c) { return c.size() < 3; }),
                   contours.end());
    return !contours.empty();
}

// =============================================================================
// Atlas de glifos SDF
// =============================================================================
namespace {
struct AtlasEntry { u16 x = 0, y = 0, w = 0, h = 0; i16 xoff = 0, yoff = 0; };
struct Atlas {
    std::mutex mutex;
    std::vector<u8> px = std::vector<u8>(static_cast<usize>(kGlyphAtlasSize) * kGlyphAtlasSize, 0);
    std::unordered_map<u64, AtlasEntry> entries;
    u32 shelfX = 1, shelfY = 1, shelfH = 0;
    u64 generation = 1;
    bool dirty = true;
    u32 rasterized = 0;   ///< SDFs gerados (8E: em regime, 0 por quadro)
    u32 resets = 0;       ///< atlas cheio → refeito
};
Atlas& atlas() {
    static Atlas a;
    return a;
}

/// Entrada do glifo (rasteriza e empacota na 1ª vez). Falso = atlas cheio.
bool atlas_glyph(Atlas& A, const Font::Impl& f, int glyph, AtlasEntry& out) {
    const u64 key = (reinterpret_cast<u64>(&f) << 20) ^ static_cast<u64>(glyph);
    if (auto it = A.entries.find(key); it != A.entries.end()) { out = it->second; return true; }
    const f32 fs = stbtt_ScaleForPixelHeight(&f.info, kGlyphBasePx);
    int w = 0, h = 0, xo = 0, yo = 0;
    u8* sdf = stbtt_GetGlyphSDF(&f.info, fs, glyph, static_cast<int>(kGlyphSpread), 128, kGlyphDistScale, &w, &h, &xo, &yo);
    AtlasEntry e;
    if (sdf && w > 0 && h > 0) {
        if (A.shelfX + static_cast<u32>(w) + 1 > kGlyphAtlasSize) { A.shelfX = 1; A.shelfY += A.shelfH + 1; A.shelfH = 0; }
        if (A.shelfY + static_cast<u32>(h) + 1 > kGlyphAtlasSize) { stbtt_FreeSDF(sdf, nullptr); return false; }
        e.x = static_cast<u16>(A.shelfX);
        e.y = static_cast<u16>(A.shelfY);
        e.w = static_cast<u16>(w);
        e.h = static_cast<u16>(h);
        e.xoff = static_cast<i16>(xo);
        e.yoff = static_cast<i16>(yo);
        for (int yy = 0; yy < h; ++yy)
            std::memcpy(&A.px[static_cast<usize>(e.y + yy) * kGlyphAtlasSize + e.x], sdf + yy * w, static_cast<usize>(w));
        A.shelfX += static_cast<u32>(w) + 1;
        A.shelfH = std::max(A.shelfH, static_cast<u32>(h));
        A.dirty = true;
    }
    if (sdf) stbtt_FreeSDF(sdf, nullptr);
    ++A.rasterized;
    A.entries[key] = e;
    out = e;
    return true;
}
} // namespace

const u8* glyph_atlas(u64& generation, bool& dirty) {
    Atlas& A = atlas();
    std::lock_guard<std::mutex> lock(A.mutex);
    generation = A.generation;
    dirty = A.dirty;
    return A.px.data();
}

void glyph_atlas_stats(u32& rasterized, u32& resets) noexcept {
    Atlas& A = atlas();
    std::lock_guard<std::mutex> lock(A.mutex);
    rasterized = A.rasterized;
    resets = A.resets;
}

void fallback_font_stats(u32& loaded, u64& bytes) noexcept {
    std::lock_guard<std::mutex> lock(g_fallbackMutex);
    loaded = g_fallbackLoads;
    bytes = g_fallbackBytes;
}

void glyph_atlas_clean() noexcept {
    Atlas& A = atlas();
    std::lock_guard<std::mutex> lock(A.mutex);
    A.dirty = false;
}

bool layout_quads(const Font& font, const TextData& t, f32 pad, TextLayout& out) {
    const Font::Impl& f = font.impl();
    Placed p;
    place(f, t, 1.0f, p);
    out = TextLayout{};
    out.pad = pad;
    out.lines = p.lines;
    out.contentWidth = std::ceil(p.width);
    out.contentHeight = std::ceil(p.height);
    out.width = std::max(1.0f, out.contentWidth + 2.0f * pad);
    out.height = std::max(1.0f, out.contentHeight + 2.0f * pad);
    // Palavra de cada caractere (separadas por espaço/quebra), em ordem lógica.
    const std::vector<u32> cps = decode_utf8(t.content);
    std::vector<u32> wordOf(cps.size() + 1, 0);
    u32 word = 0;
    bool inWord = false;
    for (usize i = 0; i < cps.size(); ++i) {
        const bool space = cps[i] == ' ' || cps[i] == '\t' || cps[i] == '\n' || cps[i] == 0x00A0 || cps[i] == '\r';
        if (!space && !inWord) inWord = true;
        if (space && inWord) { ++word; inWord = false; }
        wordOf[i] = word;
    }
    out.chars = static_cast<u32>(cps.size());
    out.words = word + (inWord ? 1u : 0u);
    Atlas& A = atlas();
    std::lock_guard<std::mutex> lock(A.mutex);
    for (int attempt = 0; attempt < 2; ++attempt) {
        out.quads.clear();
        bool full = false;
        for (const Glyph& g : p.glyphs) {
            AtlasEntry e;
            if (!atlas_glyph(A, *g.font, g.id, e)) { full = true; break; }
            if (e.w == 0) continue;
            const f32 k = g.fs / stbtt_ScaleForPixelHeight(&g.font->info, kGlyphBasePx);
            GlyphQuad q;
            q.penX = pad + g.x;
            q.baseline = pad + g.y;
            q.x0 = q.penX + static_cast<f32>(e.xoff) * k;
            q.y0 = q.baseline + static_cast<f32>(e.yoff) * k;
            q.x1 = q.x0 + static_cast<f32>(e.w) * k;
            q.y1 = q.y0 + static_cast<f32>(e.h) * k;
            const f32 inv = 1.0f / static_cast<f32>(kGlyphAtlasSize);
            q.u0 = static_cast<f32>(e.x) * inv;
            q.v0 = static_cast<f32>(e.y) * inv;
            q.u1 = static_cast<f32>(e.x + e.w) * inv;
            q.v1 = static_cast<f32>(e.y + e.h) * inv;
            q.k = k;
            q.advance = g.advance;
            q.lineIndex = g.line;
            q.charIndex = g.cluster;
            q.wordIndex = g.cluster < wordOf.size() ? wordOf[g.cluster] : 0;
            q.color = t.color;
            for (const TextSpan& sp : t.spans) if (sp.hasColor && g.cluster >= sp.start && g.cluster < sp.end) q.color = sp.color;
            out.quads.push_back(q);
        }
        if (!full) break;
        std::fill(A.px.begin(), A.px.end(), 0);
        A.entries.clear();
        A.shelfX = A.shelfY = 1;
        A.shelfH = 0;
        ++A.generation;
        ++A.resets;
        A.dirty = true;
    }
    return !out.quads.empty();
}

u32 shaped_glyphs(const Font& font, const TextData& t, std::vector<ShapedGlyph>& out) {
    const Font::Impl& f = font.impl();
    Placed p;
    place(f, t, 1.0f, p);
    out.clear();
    // Linhas começam em x = 0 (como antes do place): tira o alinhamento da caixa.
    std::vector<f32> lineX(p.lines, 1e30f);
    for (const Glyph& g : p.glyphs) lineX[g.line] = std::min(lineX[g.line], g.x);
    for (const Glyph& g : p.glyphs) {
        ShapedGlyph s2;
        s2.glyph = static_cast<u32>(g.id);
        s2.cluster = g.cluster;
        s2.line = g.line;
        s2.x = t.alignment == 0 && t.boxMode == 0 ? g.x : g.x - lineX[g.line];
        s2.y = g.y - p.baselines[g.line];
        s2.fallback = g.font != &f;
        out.push_back(s2);
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
    mix(&t.boxMode, sizeof(t.boxMode));
    mix(&t.box, sizeof(t.box));
    for (const TextSpan& sp : t.spans) mix(&sp, sizeof(sp));
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
    Placed p;
    place(f, t, scale, p);
    const f32 stroke = std::max(0.0f, t.strokeWidth) * scale;
    const bool hasStroke = stroke > 0.0f && t.strokeColor.w > 0.0f;
    const i32 pad = static_cast<i32>(std::ceil(hasStroke ? stroke + 2.0f : 2.0f));
    const i32 W = std::max(1, static_cast<i32>(std::ceil(p.width)) + 2 * pad);
    const i32 H = std::max(1, static_cast<i32>(std::ceil(p.height)) + 2 * pad);
    if (static_cast<i64>(W) * H > 4096ll * 4096ll) return false;

    std::vector<u8> fill(static_cast<usize>(W) * H, 0);
    // Distância ao contorno (px de textura, positiva fora), só com contorno.
    std::vector<f32> dist;
    if (hasStroke) dist.assign(static_cast<usize>(W) * H, 1e9f);
    const i32 sdfPad = static_cast<i32>(std::ceil(stroke)) + 2;
    const f32 onedge = 128.0f;
    const f32 distScale = 127.0f / static_cast<f32>(sdfPad);

    {
        const f32 x = static_cast<f32>(pad), baseline = static_cast<f32>(pad);
        for (const Glyph& gl : p.glyphs) {
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
