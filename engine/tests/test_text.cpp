// =============================================================================
//  Motor de texto (Fase 7A): shaping com HarfBuzz — kerning, ligaduras, árabe
//  contextual, RTL, texto misto e fontes de reserva.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
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

AUREA_TEST(Text, AnimatorSelectorWeights) {
    TextAnimator a;
    a.props = kTextPropOpacity;
    a.selector.start = 0;
    a.selector.end = 50;
    TrackSet tr;
    // Quadrado, 4 letras, 0–50%: as duas primeiras dentro, as duas últimas fora.
    f32 w[4];
    for (u32 i = 0; i < 4; ++i) w[i] = text::selector_weight(a, 0, tr, 0, 0, i, 4);
    AUREA_CHECK(w[0] > 0.99f && w[1] > 0.99f && w[2] < 0.01f && w[3] < 0.01f);
    // Fração: 0–37.5% cobre metade da segunda letra.
    a.selector.end = 37.5f;
    AUREA_CHECK(std::fabs(text::selector_weight(a, 0, tr, 0, 0, 1, 4) - 0.5f) < 0.01f);
    // Deslocamento +50% leva a seleção para as duas últimas.
    a.selector.end = 50;
    a.selector.offset = 50;
    AUREA_CHECK(text::selector_weight(a, 0, tr, 0, 0, 0, 4) < 0.01f && text::selector_weight(a, 0, tr, 0, 0, 3, 4) > 0.99f);
    // Keyframe no deslocamento vence o valor parado (e interpola no sub-quadro).
    Track& k = tr.get_or_create(TrackProperty::TextAnimParam, 0, text::kSelOffset);
    k.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    k.set(FrameIndex{10}, 50.0f, Interpolation::Linear);
    AUREA_CHECK(text::selector_weight(a, 0, tr, 0, 0, 0, 4) > 0.99f);
    AUREA_CHECK(std::fabs(text::anim_param(tr, 0, text::kSelOffset, 2.5, 0) - 12.5f) < 0.01f);
    // Ordem aleatória: mesma quantidade selecionada, outra ordem.
    TrackSet none;
    a.selector.offset = 0;
    a.selector.randomOrder = true;
    a.selector.seed = 7;
    {
        TextData t;
        t.animators.push_back(a);
        t.animators[0].opacity = 0;
        std::vector<text::GlyphUnits> units(8);
        for (u32 i = 0; i < 8; ++i) units[i].charIndex = i;
        std::vector<text::GlyphAnim> out;
        text::evaluate_text_animators(t, none, 0, 30.0, units, 8, 1, 1, out);
        u32 hidden = 0;
        bool moved = false;
        for (u32 i = 0; i < 8; ++i) {
            hidden += out[i].opacity < 0.5f;
            if ((out[i].opacity < 0.5f) != (i < 4)) moved = true;
        }
        AUREA_CHECK_EQ(hidden, 4u);
        AUREA_CHECK(moved);
    }
    // Wiggly: dentro de −1..1 e muda com o tempo.
    a.selector.type = 1;
    f32 lo = 1, hi = -1;
    for (u32 f = 0; f < 60; ++f) {
        const f32 x = text::selector_weight(a, 0, none, f, f / 30.0, 2, 8);
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    AUREA_CHECK(lo >= -1.0f && hi <= 1.0f && hi - lo > 0.5f);
}

AUREA_TEST(Text, PresetsAreDataAndCharOffsetRolls) {
    for (u32 p = 0; p < text::kTextPresetCount; ++p) {
        TextData t;
        t.content = "Ola mundo";
        TrackSet tr;
        AUREA_CHECK(text::apply_text_preset(p, t, tr, 0, 30, 30.0));
        AUREA_CHECK(!t.animators.empty());
        AUREA_CHECK(text::text_preset_name(p) != nullptr);
    }
    TextData t;
    t.content = "Az9 !";
    TextAnimator a;
    a.props = kTextPropCharOffset;
    a.charOffset = 1;
    t.animators.push_back(a);
    TrackSet tr;
    // A→B, z→a (volta), 9→0, espaço e pontuação ficam.
    AUREA_CHECK_EQ(text::apply_char_offset(t, tr, 0, 30.0), std::string("Ba0 !"));
}

AUREA_TEST(Text, TypewriterUsesUnicodeCharactersAndKeepsInitialFrameHidden) {
    auto check = [](const std::string& content, u32 count, i64 duration) {
        TextData t; t.content = content;
        TrackSet tracks;
        AUREA_CHECK(text::apply_text_preset(8, t, tracks, 10, duration, 30.0));
        std::vector<text::GlyphUnits> units(count);
        for (u32 c = 0; c < count; ++c) units[c].charIndex = c;
        std::vector<text::GlyphAnim> output;
        const i64 frames[] = {duration, 0, duration / 2, 0, duration};
        for (i64 frame : frames) {
            text::evaluate_text_animators(t, tracks, 10 + frame, 30.0, units, count, 1, 1, output);
            u32 visible = 0;
            for (const auto& glyph : output) {
                AUREA_CHECK(glyph.opacity < 0.0001f || glyph.opacity > 0.9999f);
                visible += glyph.opacity > 0.5f;
            }
            AUREA_CHECK_EQ(visible, static_cast<u32>(frame * count / duration));
        }
    };
    check("A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80", 4, 12);
    check("abcdefghij", 10, 2);
}

AUREA_TEST(Text, ScriptFontMissingLatinUsesConfiguredDefaultAndKeepsClusters) {
    const char* scriptPath = "C:/Windows/Fonts/segmdl2.ttf";
    const char* defaultPath = "C:/Windows/Fonts/calibri.ttf";
    auto script = font_at(scriptPath), normal = font_at(defaultPath);
    #if defined(_WIN32)
    AUREA_CHECK(script && normal);
    #endif
    if (!script || !normal) return;
    text::set_default_font_path(defaultPath);
    const auto expected = shape(*normal, "a\xC3\xA7\xC3\xA3o");
    const auto actual = shape(*script, "a\xC3\xA7\xC3\xA3o");
    AUREA_CHECK_EQ(actual.size(), expected.size());
    for (usize i = 0; i < actual.size() && i < expected.size(); ++i) {
        AUREA_CHECK(actual[i].fallback);
        AUREA_CHECK(actual[i].glyph != 0);
        AUREA_CHECK_EQ(actual[i].glyph, expected[i].glyph);
        AUREA_CHECK_NEAR(actual[i].x, expected[i].x, .001f);
    }
    const auto composed = shape(*script, "\xC3\xA9");
    const auto decomposed = shape(*script, "e\xCC\x81");
    AUREA_CHECK_EQ(composed.size(), 1u); AUREA_CHECK_EQ(decomposed.size(), 1u);
    if (!composed.empty() && !decomposed.empty()) AUREA_CHECK_EQ(composed[0].glyph, decomposed[0].glyph);
    const auto authored = shape(*script, "\xEE\x9C\x80"); // MDL2's actual U+E700 glyph remains authored.
    AUREA_CHECK(!authored.empty());
    if (!authored.empty()) { AUREA_CHECK(!authored[0].fallback); AUREA_CHECK(authored[0].glyph != 0); }
    // A joiner is shaped together with its neighboring letters, never on a
    // different fallback face. Compare HarfBuzz's real result, not a mock glyph.
    const auto joined = shape(*script, "f\xE2\x80\x8Di"), joinedExpected = shape(*normal, "f\xE2\x80\x8Di");
    AUREA_CHECK_EQ(joined.size(), joinedExpected.size());
    for (usize i = 0; i < joined.size() && i < joinedExpected.size(); ++i) {
        AUREA_CHECK_EQ(joined[i].glyph, joinedExpected[i].glyph);
        AUREA_CHECK_EQ(joined[i].cluster, joinedExpected[i].cluster);
    }
    if (const auto display = font_at("C:/Windows/Fonts/impact.ttf")) {
        const auto base = shape(*display, "a");
        AUREA_CHECK(!base.empty() && !base[0].fallback);
        const auto marked = shape(*display, "a\xD6\xB0"); // Base present, Hebrew combining mark missing.
        const auto markedExpected = shape(*normal, "a\xD6\xB0");
        AUREA_CHECK_EQ(marked.size(), markedExpected.size());
        for (usize i = 0; i < marked.size() && i < markedExpected.size(); ++i) {
            AUREA_CHECK(marked[i].glyph != 0);
            AUREA_CHECK_EQ(marked[i].glyph, markedExpected[i].glyph);
        }
    }
    text::set_default_font_path("");
}
