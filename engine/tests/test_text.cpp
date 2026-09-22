// =============================================================================
//  Motor de texto (Fase 7A): shaping com HarfBuzz — kerning, ligaduras, árabe
//  contextual, RTL, texto misto e fontes de reserva.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/text/Text.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// -----------------------------------------------------------------------------
// Gerenciador de fontes
// -----------------------------------------------------------------------------
#include "aurea/text/FontManager.hpp"
#include "aurea/Engine.hpp"

#include <chrono>
#include <filesystem>

AUREA_TEST(Text, FontManagerReadsFamiliesWeightsAndResolvesTextFonts) {
    text::FontEntry reg, bold, ital;
    if (!text::read_font_info("C:/Windows/Fonts/arial.ttf", reg)) return;
    AUREA_CHECK(text::read_font_info("C:/Windows/Fonts/arialbd.ttf", bold));
    AUREA_CHECK(text::read_font_info("C:/Windows/Fonts/ariali.ttf", ital));
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<text::FontEntry> all = text::FontManager::instance().list();
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
    u32 arial = 0;
    for (const auto& e : all) arial += e.family == "Arial" ? 1u : 0u;
    // Camada pedindo Arial 700: resolve para o arquivo bold; família inexistente = padrão.
    TextData t;
    t.fontFamily = "Arial";
    t.fontWeight = 700;
    auto fb = text::FontManager::instance().font_for(t);
    t.fontWeight = 400;
    auto fr = text::FontManager::instance().font_for(t);
    t.fontFamily = "Familia Que Nao Existe";
    auto fd = text::FontManager::instance().font_for(t);
    TextData w;
    w.content = "Aurea";
    w.size = 100;
    const f32 wb = fb ? text::measure(*fb, w).width : 0, wr = fr ? text::measure(*fr, w).width : 0;
    std::printf("    fontes: %zu no sistema (varredura %.0f ms), Arial %u estilos; '%s' %u / '%s' %u / '%s' italico %d; "
                "Aurea regular %.0f px x bold %.0f px; inexistente = padrao %d\n",
                all.size(), ms, arial, reg.style.c_str(), reg.weight, bold.style.c_str(), bold.weight, ital.style.c_str(), ital.italic ? 1 : 0,
                wr, wb, fd == text::default_font() ? 1 : 0);
    AUREA_CHECK(reg.family == "Arial" && reg.weight == 400 && !reg.italic);
    AUREA_CHECK(bold.weight == 700 && ital.italic);
    AUREA_CHECK(arial >= 4);
    AUREA_CHECK(fb && fr && fb != fr && wb > wr);
    AUREA_CHECK(fd == text::default_font());
}

AUREA_TEST(Text, ImportedFontIsUsedSavedAndReopened) {
    const std::string src = "C:/Windows/Fonts/consola.ttf";
    if (!std::filesystem::exists(src)) return;
    const std::string dir = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_fontes";
    std::filesystem::create_directories(dir);
    const std::string copy = dir + "/minha_fonte.ttf";
    std::filesystem::copy_file(src, copy, std::filesystem::copy_options::overwrite_existing);
    Engine e;
    EngineConfig ec;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    ec.documentsDirectory = dir;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, "fontes").ok());
    auto imp = e.import_font(copy.c_str());
    AUREA_CHECK(imp.ok());
    auto layer = e.add_text("iiii");
    AUREA_CHECK(layer.ok());
    if (!imp.ok() || !layer.ok()) { e.shutdown(); return; }
    AUREA_CHECK(e.set_text_font(*layer, imp->family, imp->weight, imp->italic, copy));
    auto measureLayer = [&] {
        const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        const Layer* l = c->layer(LayerId::unpack(*layer));
        auto f = text::FontManager::instance().font_for(l->text);
        return f ? text::measure(*f, l->text).width : 0.0f;
    };
    const f32 mono = measureLayer();   // monoespaçada: "iiii" larga
    const std::string path = dir + "/p.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    const Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* l = c->layer(LayerId::unpack(*layer));
    const bool stored = l && l->text.fontPath.rfind("docs:", 0) == 0 && l->text.fontFamily == imp->family;
    const f32 reopened = measureLayer();
    bool listed = false;
    for (const auto& f : e.list_fonts()) listed |= f.imported && f.path == copy;
    TextData def;
    def.content = "iiii";
    def.size = l ? l->text.size : 72;
    const f32 proportional = text::measure(*text::default_font(), def).width;
    std::printf("    fonte importada '%s': iiii %.0f px (padrao %.0f); guardada %s; reaberta %.0f px; no seletor %d\n", imp->family.c_str(), mono,
                proportional, l ? l->text.fontPath.c_str() : "?", reopened, listed ? 1 : 0);
    AUREA_CHECK(mono > proportional * 1.5f);
    AUREA_CHECK(stored);
    AUREA_CHECK(std::fabs(reopened - mono) < 0.5f);
    AUREA_CHECK(listed);
    e.shutdown();
    std::error_code ec2;
    std::filesystem::remove_all(dir, ec2);
}

AUREA_TEST(Text, ParagraphBoxesWrapClipAndShrink) {
    auto f = text::default_font();
    if (!f) return;
    TextData t;
    t.content = "Um texto longo o bastante para quebrar em varias linhas dentro da caixa";
    t.size = 40;
    text::TextLayout point, para, fixed, shrink;
    AUREA_CHECK(text::layout_quads(*f, t, 2, point));
    t.boxMode = 1;
    t.box.w = 300;
    AUREA_CHECK(text::layout_quads(*f, t, 2, para));
    t.boxMode = 2;
    t.box.h = 100;
    AUREA_CHECK(text::layout_quads(*f, t, 2, fixed));
    t.boxMode = 3;
    AUREA_CHECK(text::layout_quads(*f, t, 2, shrink));
    f32 maxRight = 0, shrinkBottom = 0;
    for (const auto& q : para.quads) maxRight = std::max(maxRight, q.penX + q.advance);   // o quad tem a margem do SDF
    for (const auto& q : shrink.quads) shrinkBottom = std::max(shrinkBottom, q.baseline);
    std::printf("    caixas: ponto 1 linha %.0f px; paragrafo 300 px -> %u linhas, altura %.0f, direita max %.0f; fixa 100 px %zu de %zu glifos; "
                "encolher: %zu glifos, ultima linha de base %.0f (caixa 100)\n",
                point.contentWidth, para.lines, para.contentHeight, maxRight, fixed.quads.size(), para.quads.size(), shrink.quads.size(),
                shrinkBottom - 2);
    AUREA_CHECK(point.lines == 1 && point.contentWidth > 600);
    AUREA_CHECK(para.lines >= 4 && para.contentWidth == 300 && maxRight <= 2 + 300 + 0.5f);
    AUREA_CHECK(fixed.contentHeight == 100 && fixed.quads.size() < para.quads.size());
    AUREA_CHECK(shrink.quads.size() == para.quads.size() && shrinkBottom - 2 <= 100);
}

AUREA_TEST(Text, RichTextSpansColorBoldAndSize) {
    auto f = text::default_font();
    if (!f) return;
    TextData t;
    t.content = "EDITAR ficou FACIL";
    t.size = 60;
    text::TextLayout plain, rich;
    AUREA_CHECK(text::layout_quads(*f, t, 2, plain));
    TextSpan sp;
    sp.start = 13;
    sp.end = 18;
    sp.hasColor = true;
    sp.color = Vec4{0.2f, 1, 0.3f, 1};
    sp.weight = 700;
    sp.scale = 1.4f;
    t.spans.push_back(sp);
    AUREA_CHECK(text::layout_quads(*f, t, 2, rich));
    u32 green = 0, white = 0;
    f32 hPlain = 0, hRich = 0;
    for (const auto& q : rich.quads) {
        const bool g = q.color.y > 0.9f && q.color.x < 0.3f;
        green += g ? 1u : 0u;
        white += q.color.x > 0.9f ? 1u : 0u;
        if (q.charIndex == 13) hRich = q.y1 - q.y0;
    }
    for (const auto& q : plain.quads) if (q.charIndex == 13) hPlain = q.y1 - q.y0;
    std::printf("    rich text: %u glifos verdes (FACIL), %u brancos; F normal %.1f px x trecho %.1f px; largura %.0f -> %.0f\n", green, white, hPlain,
                hRich, plain.contentWidth, rich.contentWidth);
    AUREA_CHECK(green == 5 && white == 11);
    AUREA_CHECK(hRich > hPlain * 1.3f);
    AUREA_CHECK(rich.contentWidth > plain.contentWidth);
}
