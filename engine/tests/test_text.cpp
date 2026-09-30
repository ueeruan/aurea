// =============================================================================
//  Motor de texto (Fase 7A): shaping com HarfBuzz — kerning, ligaduras, árabe
//  contextual, RTL, texto misto e fontes de reserva.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextTransform.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <limits>

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

namespace {
struct TextEditRig {
    Engine e;
    u64 id = 0;
    TextEditRig() {
        EngineConfig cfg; cfg.workerCount = 1; cfg.disableAutosave = true;
        AUREA_CHECK(e.initialize(cfg).ok());
        AUREA_CHECK(e.new_project(320, 180, 30, "text editing").ok());
        auto added = e.add_text("AAAAAA"); AUREA_CHECK(added.ok());
        if (added.ok()) id = *added;
    }
    ~TextEditRig() { e.shutdown(); }
    Layer* layer() { return e.project()->timeline().composition(e.project()->timeline().current())->layer(LayerId::unpack(id)); }
    void seek(i64 frame) { Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{frame}, 30); AUREA_CHECK(e.apply_command(c).ok()); }
    void undo() { Command c; c.type = CommandType::Undo; AUREA_CHECK(e.apply_command(c).ok()); }
};
}

AUREA_TEST(TextAnimatorEditing, StackOperationsPreserveTracksCurvesUndoAndProject) {
    TextEditRig r;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropPosition), 0);
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropScale), 1);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, text::kPosX, 20));
    AUREA_CHECK(r.e.toggle_text_anim_key(r.id, 0, text::kPosX));
    r.seek(30);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, text::kPosX, 80));
    auto* original = r.layer()->tracks.find(TrackProperty::TextAnimParam, 0, text::kPosX);
    original->keys[0].interp = Interpolation::Hold;
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 1, text::kScaleX, 150));
    AUREA_CHECK(r.e.toggle_text_anim_key(r.id, 1, text::kScaleX));
    AUREA_CHECK_EQ(r.e.duplicate_text_animator(r.id, 0), 1);
    AUREA_CHECK_EQ(r.layer()->text.animators.size(), 3u);
    const auto* copied = r.layer()->tracks.find(TrackProperty::TextAnimParam, 1, text::kPosX);
    AUREA_CHECK(copied && copied->keys.size() == 2);
    if (copied) AUREA_CHECK(copied->keys[0].interp == Interpolation::Hold);
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::TextAnimParam, 2, text::kScaleX) != nullptr);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 1, text::kPosX, 200));
    AUREA_CHECK_NEAR(r.layer()->tracks.find(TrackProperty::TextAnimParam, 0, text::kPosX)->keys[1].value, 80, 0.001);
    AUREA_CHECK(r.e.move_text_animator(r.id, 2, 0));
    AUREA_CHECK(r.layer()->text.animators[0].props == kTextPropScale);
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::TextAnimParam, 0, text::kScaleX) != nullptr);
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::TextAnimParam, 1, text::kPosX) != nullptr);
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::TextAnimParam, 2, text::kPosX) != nullptr);
    r.undo();
    AUREA_CHECK(r.layer()->text.animators[2].props == kTextPropScale);
    AUREA_CHECK(r.e.move_text_animator(r.id, 0, 2));
    AUREA_CHECK(r.layer()->text.animators[1].props == kTextPropScale);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_text_stack_edit.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK(r.layer()->text.animators.size() == 3);
    AUREA_CHECK_NEAR(r.layer()->tracks.find(TrackProperty::TextAnimParam, 2, text::kPosX)->keys[1].value, 80, 0.001);
    AUREA_CHECK(r.e.remove_text_animator(r.id, 1));
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::TextAnimParam, 1, text::kPosX) != nullptr);
    std::remove(path.c_str());
}

AUREA_TEST(TextAnimatorEditing, FillAndStrokeKeysEvaluateAndSurviveReopen) {
    TextEditRig r;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropFill | kTextPropStroke), 0);
    const u32 params[] = {text::kFillR, text::kFillG, text::kFillB, text::kStrokeR, text::kStrokeG, text::kStrokeB};
    for (u32 p : params) { AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, p, 0)); AUREA_CHECK(r.e.toggle_text_anim_key(r.id, 0, p)); }
    r.seek(30);
    for (u32 p : params) {
        AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, p, 1));
        r.layer()->tracks.find(TrackProperty::TextAnimParam, 0, p)->keys[0].interp = Interpolation::Linear;
    }
    auto verify = [&] {
        std::vector<text::GlyphAnim> out;
        text::evaluate_text_animators(r.layer()->text, r.layer()->tracks, 15, 30, {{0, 0, 0}}, 1, 1, 1, out);
        AUREA_CHECK_NEAR(out[0].fill.x, .5, .001); AUREA_CHECK_NEAR(out[0].fill.y, .5, .001); AUREA_CHECK_NEAR(out[0].fill.z, .5, .001);
        AUREA_CHECK_NEAR(out[0].stroke.x, .5, .001); AUREA_CHECK_NEAR(out[0].stroke.y, .5, .001); AUREA_CHECK_NEAR(out[0].stroke.z, .5, .001);
        f32 values[40]{}; AUREA_CHECK_EQ(r.e.query_text_animators(r.id, values, 40), 1u);
        AUREA_CHECK((static_cast<u32>(values[36]) & (7u << 6)) == (7u << 6));
        AUREA_CHECK((static_cast<u32>(values[37]) & (7u << 19)) == (7u << 19));
    };
    verify();
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_text_colors_edit.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok()); AUREA_CHECK(r.e.load_project(path.c_str()).ok()); verify();
    std::remove(path.c_str());
    AUREA_CHECK(!r.e.set_text_anim_param(r.id, 0, text::kFillR, std::numeric_limits<f32>::quiet_NaN()));
    AUREA_CHECK(!r.e.move_text_animator(r.id, 0, 1));
    AUREA_CHECK_EQ(r.e.duplicate_text_animator(r.id, 12), -1);
    r.layer()->locked = true;
    AUREA_CHECK_EQ(r.e.duplicate_text_animator(r.id, 0), -1);
    AUREA_CHECK(!r.e.move_text_animator(r.id, 0, 0));
}

AUREA_TEST(TextAnimatorEditing, RandomCharacterOffsetUsesTheSameSelectorAsTransforms) {
    TextData t; t.content = "AAAAAAAA";
    TextAnimator a; a.props = kTextPropCharOffset | kTextPropPosition;
    a.charOffset = 1; a.position.x = 1;
    a.selector.end = 50; a.selector.randomOrder = true; a.selector.seed = 17;
    t.animators.push_back(a);
    TrackSet tracks;
    std::vector<text::GlyphUnits> units;
    for (u32 i = 0; i < 8; ++i) units.push_back({i, 0, 0});
    std::vector<text::GlyphAnim> out;
    text::evaluate_text_animators(t, tracks, 0, 30, units, 8, 1, 1, out);
    const auto changed = text::apply_char_offset(t, tracks, 0, 30);
    for (u32 i = 0; i < 8; ++i) AUREA_CHECK(changed[i] == (out[i].translate.x > .5f ? 'B' : 'A'));
    AUREA_CHECK(changed != "BBBBAAAA");
    AUREA_CHECK(changed == text::apply_char_offset(t, tracks, 0, 30));
}

AUREA_TEST(TextAnimatorEditing, QueuedSeekIsAppliedBeforeEditingAnimationKeys) {
    TextEditRig r;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropFill), 0);
    AUREA_CHECK(r.e.toggle_text_anim_key(r.id, 0, text::kFillR));
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{30}, 30);
    AUREA_CHECK_EQ(r.e.submit_commands(&seek, 1, nullptr, 0), 1u);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, text::kFillR, .25f));
    auto* textTrack = r.layer()->tracks.find(TrackProperty::TextAnimParam, 0, text::kFillR);
    AUREA_CHECK(textTrack && textTrack->keys.size() == 2);
    AUREA_CHECK(textTrack && textTrack->find_exact(FrameIndex{30}) != kInvalidIndex);
    AUREA_CHECK_EQ(r.e.add_layer_animator(r.id), 0);
    seek.seek.time = tick_at(FrameIndex{45}, 30);
    AUREA_CHECK_EQ(r.e.submit_commands(&seek, 1, nullptr, 0), 1u);
    AUREA_CHECK(r.e.toggle_layer_anim_key(r.id, 0, 1));
    auto* layerTrack = r.layer()->tracks.find(TrackProperty::LayerAnimParam, 0, 1);
    AUREA_CHECK(layerTrack && layerTrack->find_exact(FrameIndex{45}) != kInvalidIndex);
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

// Contornos sobrepostos (fontes variáveis e muitas fontes baixadas) deixavam
// a aresta interna no SDF do stb: risco dentro da letra e o stroke contornando
// o meio dela. O SDF usado agora nunca contradiz a cobertura real do glifo.
AUREA_TEST(Text, OverlappingContoursDoNotLeaveSeamsInGlyphSdf) {
    const char* fonts[] = {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/SegUIVar.ttf",
                           "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/impact.ttf"};
    const char* chars = "ABDGKMOQRSWXabdegkmoqsw&@%#48";
    u32 checked = 0;
    for (const char* path : fonts) {
        const auto font = text::Font::load(path);
        if (!font) continue;
        u32 rawBad = 0, fixedBad = 0;
        for (const char* c = chars; *c; ++c) {
            text::GlyphSdfProbe raw, fixed;
            if (!text::probe_glyph_sdf(*font, static_cast<u8>(*c), true, raw)) continue;
            AUREA_CHECK(text::probe_glyph_sdf(*font, static_cast<u8>(*c), false, fixed));
            AUREA_CHECK_EQ(fixed.w, raw.w);
            AUREA_CHECK_EQ(fixed.h, raw.h);
            rawBad += raw.contradictions;
            fixedBad += fixed.contradictions;
            // Onde o analítico já concordava, o SDF é o mesmo (qualidade intacta).
            if (raw.contradictions == 0) AUREA_CHECK(raw.sdf == fixed.sdf);
            ++checked;
        }
        std::printf("  %s: pixels contraditorios %u (stb cru) -> %u\n", path, rawBad, fixedBad);
        AUREA_CHECK_EQ(fixedBad, 0u);
    }
    AUREA_CHECK(checked > 0);
}

// "Fonte importada não salva no app": o arquivo ficava em docs/fontes (Android)
// ou docs/Media (iOS), mas o registro era só em memória — depois de reabrir o
// app ela sumia do seletor. Um motor novo agora a encontra nas duas pastas.
AUREA_TEST(Text, ImportedFontsReturnToTheListAfterReopeningTheApp) {
    const std::string src = "C:/Windows/Fonts/consola.ttf";
    if (!std::filesystem::exists(src)) return;
    const std::string docs = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_reabrir_fontes";
    std::filesystem::remove_all(docs);
    std::filesystem::create_directories(docs + "/fontes");
    std::filesystem::create_directories(docs + "/Media");
    const std::string android = docs + "/fontes/0123abcd.ttf";
    const std::string ios = docs + "/Media/Minha Fonte.ttf";
    std::filesystem::copy_file(src, android);
    std::filesystem::copy_file(src, ios);
    std::filesystem::copy_file(src, docs + "/fontes/importando.ttf");   // cópia interrompida: fica fora
    {
        std::FILE* junk = std::fopen((docs + "/Media/clip.mp4").c_str(), "wb");
        if (junk) { std::fputs("nao e fonte", junk); std::fclose(junk); }
    }
    Engine e;   // processo novo: nada foi importado nesta sessão
    EngineConfig ec;
    ec.workerCount = 2;
    ec.disableAutosave = true;
    ec.documentsDirectory = docs;
    AUREA_CHECK(e.initialize(ec).ok());
    bool androidListed = false, iosListed = false, partial = false;
    for (const auto& f : e.list_fonts()) {
        const std::string p = std::filesystem::path(f.path).generic_string();
        androidListed |= f.imported && p == std::filesystem::path(android).generic_string();
        iosListed |= f.imported && p == std::filesystem::path(ios).generic_string();
        partial |= p.find("importando.") != std::string::npos;
    }
    AUREA_CHECK(androidListed);
    AUREA_CHECK(iosListed);
    AUREA_CHECK(!partial);
    e.shutdown();
    std::error_code ignored;
    std::filesystem::remove_all(docs, ignored);
}

AUREA_TEST(Text, PackPresetsKeepContentAndLocalStart) {
    for (u32 id = 19; id < text::kTextPresetCount; ++id) {
        TextData data; data.content = "Minha legenda"; TrackSet tracks;
        AUREA_CHECK(text::apply_text_preset(id, data, tracks, 17, 30, 30));
        AUREA_CHECK(data.content == "Minha legenda");
        AUREA_CHECK(!data.animators.empty());
        if (id != 22) {
            auto* track = tracks.find(TrackProperty::TextAnimParam, 0, id == 19 || id >= 23 ? text::kSelOffset : text::kSelStart);
            AUREA_CHECK(track && !track->keys.empty());
            if (track && !track->keys.empty()) AUREA_CHECK_EQ(track->keys.front().time.value, 17);
        }
    }
}

using namespace aurea::text;
namespace {
struct TransformRig {
    EffectRegistry registry;
    Layer layer;
    TextLayout layout;
    std::vector<GlyphAnim> styles;
    std::vector<Mat4> matrices;
    TransformRig() {
        register_builtin_effects(registry);
        layer.kind = LayerKind::Text;
        EffectInstance effect; effect.id = 17; effect.type = effect_type_id(kTransformEffect);
        initialize_instance(effect, *registry.params(effect.type)); layer.effects.push_back(effect);
        layout.chars = 4; layout.words = 2; layout.lines = 1;
        layout.contentWidth = 80; layout.contentHeight = 20;
        layer.transform.anchor = {42,12,0}; // natural raster includes its 2 px margin
        for (u32 i = 0; i < 4; ++i) {
            GlyphQuad q; q.x0 = i * 20.f; q.x1 = q.x0 + 20; q.y1 = 20;
            q.charIndex = i; q.wordIndex = i / 2; layout.quads.push_back(q);
        }
    }
    ParamValue& at(u32 p) { return layer.effects[0].params[p].constant; }
    void evaluate(f64 frame = 0) { styles.assign(4, {}); evaluate_transform_effects(layer, registry, frame, layout, styles, matrices); }
};
}

AUREA_TEST(TextTransform, RangePhaseAndWordsSelectExactUnits) {
    TransformRig r;
    r.at(kOffset) = ParamValue::vec2(40, 0);
    r.at(kRangeEnd) = ParamValue::scalar(50);
    r.evaluate();
    for (usize i = 0; i < 4; ++i) AUREA_CHECK_NEAR(r.matrices[i].col[3].x, i < 2 ? 40 : 0, .001);
    r.at(kPhase) = ParamValue::scalar(50); r.evaluate();
    for (usize i = 0; i < 4; ++i) AUREA_CHECK_NEAR(r.matrices[i].col[3].x, i >= 2 ? 40 : 0, .001);
    r.at(kPhase) = ParamValue::scalar(0); r.at(kComponent) = ParamValue::scalar(1); r.evaluate();
    AUREA_CHECK_NEAR(r.matrices[0].col[3].x, r.matrices[1].col[3].x, .001);
    AUREA_CHECK_NEAR(r.matrices[2].col[3].x, 0, .001);
}

AUREA_TEST(TextTransform, LayerAndComponentAnchorsHaveDifferentCenters) {
    TransformRig r; r.at(kScale) = ParamValue::scalar(100);
    r.evaluate();
    AUREA_CHECK_NEAR((r.matrices[0] * Vec4{10, 10, 0, 1}).x, -20, .001);
    r.layer.transform.anchor.x = 2; r.evaluate();
    AUREA_CHECK_NEAR((r.matrices[0] * Vec4{10,10,0,1}).x,20,.001);
    r.at(kAnchor) = ParamValue::scalar(1); r.evaluate();
    for (usize i = 0; i < 4; ++i) AUREA_CHECK_NEAR((r.matrices[i] * Vec4{10 + 20.f * i, 10, 0, 1}).x, 10 + 20.f * i, .001);
}

AUREA_TEST(TextTransform, EffectKeysColorsAndStackOrderUseStableEffectIds) {
    TransformRig r;
    r.at(kOverrideFill) = ParamValue::boolean(true); r.at(kFillColor) = ParamValue::color(1, 0, 0, 1);
    Track t; t.property = TrackProperty::EffectParam; t.effectIndex = 17; t.effectParamIndex = param_track_key(kOffset, 0);
    Keyframe a; a.time = FrameIndex{0}; a.value = 0; a.interp = Interpolation::Linear;
    Keyframe b = a; b.time = FrameIndex{30}; b.value = 60;
    t.keys = {a, b}; r.layer.tracks.add(t);
    r.evaluate(15.5);
    AUREA_CHECK_NEAR(r.matrices[0].col[3].x, 31, .001);
    AUREA_CHECK_NEAR(r.styles[0].fill.x, 1, .001);
    auto scale = r.layer.effects[0]; scale.id = 42; scale.params[kScale].constant = ParamValue::scalar(100);
    scale.params[kOverrideFill].constant = ParamValue::boolean(false); r.layer.effects.push_back(scale);
    r.evaluate(15); const f32 first = r.matrices[0].col[3].x;
    std::swap(r.layer.effects[0], r.layer.effects[1]); r.evaluate(15);
    AUREA_CHECK_NEAR(first - r.matrices[0].col[3].x, 30, .001);
    r.layer.effects[0].enabled = false; r.layer.effects[1].enabled = false; r.evaluate(15);
    AUREA_CHECK_NEAR(r.matrices[0].col[3].x, 0, .001);
}

AUREA_TEST(TextTransform, RandomOrderIsStableAcrossSeeksAndOverlapRemainsFinite) {
    TransformRig r; r.at(kOffset) = ParamValue::vec2(30, 0); r.at(kRangeEnd) = ParamValue::scalar(50);
    r.at(kRandomOrder) = ParamValue::boolean(true); r.at(kSeed) = ParamValue::scalar(1.25f);
    r.evaluate(30); const auto before = r.matrices;
    r.evaluate(0); r.evaluate(30);
    for (usize i = 0; i < 4; ++i) AUREA_CHECK_NEAR(before[i].col[3].x, r.matrices[i].col[3].x, .001);
    for (u32 shape = 0; shape < 3; ++shape) for (f32 ease : {-100.f, 0.f, 100.f}) {
        r.at(kShape) = ParamValue::scalar(static_cast<f32>(shape)); r.at(kEaseIn) = ParamValue::scalar(ease);
        r.at(kEaseOut) = ParamValue::scalar(ease); r.at(kOverlap) = ParamValue::scalar(1000); r.evaluate();
        for (const auto& m : r.matrices) AUREA_CHECK(std::isfinite(m.col[3].x) && m.col[3].x >= 0 && m.col[3].x <= 30);
    }
}
