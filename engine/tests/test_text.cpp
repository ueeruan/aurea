// =============================================================================
//  Motor de texto (Fase 7A): shaping com HarfBuzz — kerning, ligaduras, árabe
//  contextual, RTL, texto misto e fontes de reserva.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/text/Text.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;

namespace {
std::shared_ptr<const text::Font> font_at(const char* path) { return text::Font::load(path); }

std::vector<text::ShapedGlyph> shape(const text::Font& f, const std::string& s, f32 size = 100.0f, f32 tracking = 0.0f) {
    TextData t;
    t.content = s;
    t.size = size;
    t.tracking = tracking;
    std::vector<text::ShapedGlyph> g;
    text::shaped_glyphs(f, t, g);
    return g;
}
} // namespace

AUREA_TEST(Text, ArabicIsShapedContextuallyAndRightToLeft) {
    auto f = font_at("C:/Windows/Fonts/arial.ttf");
    if (!f) return;
    // لا (lam + alef) vira UMA ligadura.
    const auto la = shape(*f, "\xD9\x84\xD8\xA7");
    // س sozinho x س no começo de سلام: formas diferentes (isolada x inicial).
    const auto seen = shape(*f, "\xD8\xB3");
    const auto salam = shape(*f, "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85");
    // RTL: o glifo mais à esquerda é o do ÚLTIMO caractere (م).
    u32 leftmostCluster = 99;
    f32 minX = 1e9f;
    for (const auto& g : salam) if (g.x < minX) { minX = g.x; leftmostCluster = g.cluster; }
    u32 seenGlyphInWord = 0;
    for (const auto& g : salam) if (g.cluster == 0) seenGlyphInWord = g.glyph;
    std::printf("    arabe: la = %zu glifo(s); seen isolado %u x inicial %u; mais a esquerda = caractere %u de 4\n", la.size(),
                seen.empty() ? 0u : seen[0].glyph, seenGlyphInWord, leftmostCluster);
    AUREA_CHECK(la.size() == 1);
    AUREA_CHECK(!seen.empty() && seen[0].glyph != seenGlyphInWord);
    AUREA_CHECK(leftmostCluster == 3);
}

AUREA_TEST(Text, LigaturesKerningAndMixedDirection) {
    auto calibri = font_at("C:/Windows/Fonts/calibri.ttf");
    auto arial = font_at("C:/Windows/Fonts/arial.ttf");
    if (!calibri || !arial) return;
    // "fi": ligadura (1 glifo) na Calibri.
    const auto fi = shape(*calibri, "fi");
    // "AV": kerning aproxima o V (o avanço do A diminui).
    const auto av = shape(*arial, "AV");
    const auto a = shape(*arial, "A");
    const auto aa = shape(*arial, "AA");
    const f32 kernedAdvance = av.size() == 2 ? av[1].x : 0.0f;
    const f32 plainAdvance = aa.size() == 2 ? aa[1].x : 0.0f;
    // Misto: "abc " + hebraico "שלום" + " 123": parágrafo LTR, hebraico invertido no lugar.
    const auto mix = shape(*arial, "abc \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D 123");
    // Na tela, da esquerda para a direita: a b c ␠ ם ו ל ש ␠ 1 2 3 → clusters 0 1 2 3 7 6 5 4 8 9 10 11.
    std::vector<u32> order;
    for (const auto& g : mix) order.push_back(g.cluster);
    std::string ord;
    for (u32 c : order) ord += std::to_string(c) + " ";
    std::printf("    fi = %zu glifo(s); AV avanco %.1f x AA %.1f; misto: %s\n", fi.size(), kernedAdvance, plainAdvance, ord.c_str());
    AUREA_CHECK(fi.size() == 1);
    AUREA_CHECK(kernedAdvance > 0 && kernedAdvance < plainAdvance - 1.0f);
    const std::vector<u32> want{0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11};
    AUREA_CHECK(order == want);
    (void)a;
}

AUREA_TEST(Text, MissingGlyphsFallBackToAnotherFont) {
    // Fonte só latina + texto árabe: o árabe vem de uma fonte de reserva do
    // sistema (nada de caixinhas .notdef = glifo 0).
    auto latin = font_at("C:/Windows/Fonts/consola.ttf");
    if (!latin) return;
    const auto g = shape(*latin, "ok \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85");
    u32 fallback = 0, notdef = 0;
    for (const auto& x : g) { fallback += x.fallback ? 1u : 0u; notdef += x.glyph == 0 ? 1u : 0u; }
    std::printf("    reserva: %zu glifos, %u de outra fonte, %u vazios (.notdef)\n", g.size(), fallback, notdef);
    AUREA_CHECK(fallback >= 3);
    AUREA_CHECK(notdef == 0);
}
