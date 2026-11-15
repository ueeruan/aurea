// 0.5.0 · Animador de texto universal: seletores combináveis, expressão com
// selectorValue real, API por chave e o formato salvo.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextTransform.hpp"
#include "aurea/timeline/Layer.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::text;

namespace {
const std::vector<GlyphUnits> kUnits{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};

TextAnimator position_animator() {
    TextAnimator a;
    a.props = kTextPropPosition;
    a.position = Vec3{100, 0, 0};
    a.selector.type = kTextSelectorRange;
    return a;
}

std::vector<f32> moved(const TextData& t, const TrackSet& tracks = TrackSet{}) {
    std::vector<GlyphAnim> out;
    evaluate_text_animators(t, tracks, 0, 30, kUnits, 4, 1, 1, out);
    std::vector<f32> x;
    for (const GlyphAnim& g : out) x.push_back(g.translate.x);
    return x;
}

struct Rig {
    Engine e;
    u64 id = 0;
    Rig() {
        EngineConfig config; config.disableAutosave = true; config.workerCount = 1;
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.new_project(320, 180, 30, nullptr).ok());
        const auto added = e.add_text("ABCD");
        AUREA_CHECK(added.ok());
        if (added.ok()) id = *added;
    }
    ~Rig() { e.shutdown(); }
    Layer* layer() { auto& t = e.project()->timeline(); return t.composition(t.current())->layer(LayerId::unpack(id)); }
};
} // namespace

AUREA_TEST(TextAnimatorUniversal, SelectorsCombineInOrder) {
    TextData t;
    TextAnimator a = position_animator();
    a.selector.end = 50;
    TextSelector second = make_text_selector(kTextSelectorRange);
    second.start = 25;
    a.moreSelectors.push_back(second);
    t.animators.push_back(a);
    // Somar: as duas primeiras letras e, com a segunda faixa, as três últimas.
    for (const f32 x : moved(t)) AUREA_CHECK_NEAR(x, 100.0f, 1e-3f);
    // Interseção: só o que as duas faixas pegam (a letra 2 de 4).
    t.animators[0].moreSelectors[0].mode = kTextModeIntersect;
    const auto both = moved(t);
    AUREA_CHECK_NEAR(both[0], 0.0f, 1e-3f);
    AUREA_CHECK_NEAR(both[1], 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(both[2], 0.0f, 1e-3f);
    // Subtrair: o segundo tira do primeiro.
    t.animators[0].moreSelectors[0].mode = kTextModeSubtract;
    const auto sub = moved(t);
    AUREA_CHECK_NEAR(sub[0], 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(sub[1], 0.0f, 1e-3f);
    // Seletor desligado não entra na conta.
    t.animators[0].moreSelectors[0].enabled = false;
    const auto off = moved(t);
    AUREA_CHECK_NEAR(off[1], 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(off[2], 0.0f, 1e-3f);
}

AUREA_TEST(TextAnimatorUniversal, RangeSelectorFollowsAfterEffectsShapes) {
    TextData t;
    TextAnimator a = position_animator();
    a.selector.shape = 1;   // rampa sobe: satura depois do fim
    a.selector.start = 0; a.selector.end = 50;
    t.animators.push_back(a);
    const auto ramp = moved(t);
    AUREA_CHECK(ramp[0] < ramp[1]);
    AUREA_CHECK_NEAR(ramp[2], 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(ramp[3], 100.0f, 1e-3f);
    // Por índice: Início 1, Fim 3 pega a 2a e a 3a letra.
    t.animators[0].selector.shape = 0;
    t.animators[0].selector.units = 1;
    t.animators[0].selector.start = 1; t.animators[0].selector.end = 3;
    const auto index = moved(t);
    AUREA_CHECK_NEAR(index[0], 0.0f, 1e-3f);
    AUREA_CHECK_NEAR(index[1], 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(index[2], 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(index[3], 0.0f, 1e-3f);
}

AUREA_TEST(TextAnimatorUniversal, WigglySelectorIsDeterministicAndLocksDimensions) {
    TextData t;
    TextAnimator a = position_animator();
    a.position = Vec3{100, 100, 0};
    a.selector.end = 100;
    TextSelector w = make_text_selector(kTextSelectorWiggly);
    w.lockDimensions = true;
    a.moreSelectors.push_back(w);
    t.animators.push_back(a);
    std::vector<GlyphAnim> first, again;
    evaluate_text_animators(t, TrackSet{}, 13, 30, kUnits, 4, 1, 1, first);
    evaluate_text_animators(t, TrackSet{}, 13, 30, kUnits, 4, 1, 1, again);
    bool varies = false;
    for (usize i = 0; i < first.size(); ++i) {
        AUREA_CHECK_NEAR(first[i].translate.x, again[i].translate.x, 1e-6f);
        AUREA_CHECK_NEAR(first[i].translate.x, first[i].translate.y, 1e-4f);   // travado: X e Y iguais
        varies |= std::fabs(first[i].translate.x - first[0].translate.x) > 1e-3f;
    }
    AUREA_CHECK(varies);
}

AUREA_TEST(TextAnimatorUniversal, ExpressionSelectorSeesIndexTotalAndRealSelectorValue) {
    Rig r;
    if (!r.id) return;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropPosition), 0);
    Layer* l = r.layer();
    l->text.animators[0].position = Vec3{100, 0, 0};
    l->text.animators[0].selector.end = 50;   // as duas primeiras letras = selectorValue 100; as outras, 0
    AUREA_CHECK_EQ(r.e.add_text_selector(r.id, 0, kTextSelectorExpression), 1);
    l = r.layer();
    l->text.animators[0].moreSelectors[0].mode = kTextModeReplace;
    const u32 key = selector_key(1, kAnimExprX);
    const std::string source = "selectorValue[0] * textIndex / textTotal";
    AUREA_CHECK(r.e.set_expression(r.id, static_cast<u32>(TrackProperty::TextAnimParam), 0, key, source.c_str(), nullptr).ok());
    l = r.layer();
    const Track* track = l->tracks.find(TrackProperty::TextAnimParam, 0, key);
    AUREA_CHECK(track && track->has_expression());
    expr::Scope scope(r.e.project()->timeline());
    std::vector<GlyphAnim> out;
    evaluate_text_animators(l->text, l->tracks, 0, 30, kUnits, 4, 1, 1, out);
    AUREA_CHECK_EQ(out.size(), usize{4});
    AUREA_CHECK_NEAR(out[0].translate.x, 25.0f, 1e-2f);    // 100 · 1/4
    AUREA_CHECK_NEAR(out[1].translate.x, 50.0f, 1e-2f);    // 100 · 2/4
    AUREA_CHECK_NEAR(out[2].translate.x, 0.0f, 1e-2f);     // fora da faixa: selectorValue 0
}

AUREA_TEST(TextAnimatorUniversal, ApiEditsKeysFieldsAndSelectorsWithTracksFollowing) {
    Rig r;
    if (!r.id) return;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropPosition | kTextPropFillHsb), 0);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimFillHue, 90));
    AUREA_CHECK_EQ(r.e.add_text_selector(r.id, 0, kTextSelectorWiggly), 1);
    AUREA_CHECK_EQ(r.e.add_text_selector(r.id, 0, kTextSelectorRange), 2);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, selector_key(2, kSelEnd), 40));
    AUREA_CHECK(r.e.toggle_text_anim_key(r.id, 0, selector_key(2, kSelEnd)));
    const u32 keys[] = {kAnimFillHue, selector_key(2, kSelEnd)};
    f32 values[4]{};
    AUREA_CHECK_EQ(r.e.query_text_anim_values(r.id, 0, keys, 2, values), 2u);
    AUREA_CHECK_NEAR(values[0], 90.0f, 1e-4f);
    AUREA_CHECK_NEAR(values[2], 40.0f, 1e-4f);
    AUREA_CHECK_EQ(static_cast<u32>(values[3]) & 3u, 3u);
    // Campos: modo e base do seletor 1; lista com três seletores.
    AUREA_CHECK(r.e.set_text_animator_field(r.id, 0, 16u * 2u + 4u, kTextModeMax));
    f32 fields[kTextAnimatorFieldCount]{};
    AUREA_CHECK_EQ(r.e.query_text_animator_fields(r.id, 0, fields, kTextAnimatorFieldCount), kTextAnimatorFieldCount);
    AUREA_CHECK_NEAR(fields[15], 3.0f, 1e-6f);
    AUREA_CHECK_NEAR(fields[16u * 2u + 4u], static_cast<f32>(kTextModeMax), 1e-6f);
    // Remover o seletor 1: a trilha do seletor 2 desce um bloco.
    AUREA_CHECK(r.e.remove_text_selector(r.id, 0, 1));
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::TextAnimParam, 0, selector_key(1, kSelEnd)) != nullptr);
    AUREA_CHECK(r.layer()->tracks.find(TrackProperty::TextAnimParam, 0, selector_key(2, kSelEnd)) == nullptr);
    AUREA_CHECK(!r.e.remove_text_selector(r.id, 0, 0));    // o primeiro não sai
}

AUREA_TEST(TextAnimatorUniversal, NewFormatSurvivesProjectRoundTrip) {
    Rig r;
    if (!r.id) return;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropPosition | kTextPropFillHsb | kTextPropLineSpacing), 0);
    AUREA_CHECK_EQ(r.e.add_text_selector(r.id, 0, kTextSelectorWiggly), 1);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, selector_key(1, kAnimCorrelation), 25));
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimLineSpacingY, 14));
    AUREA_CHECK(r.e.set_text_animator_field(r.id, 0, 3, 3));
    std::filesystem::create_directories("build/reference/text-universal");
    const char* path = "build/reference/text-universal/project.aurea";
    AUREA_CHECK(r.e.save_project(path).ok());
    AUREA_CHECK(r.e.load_project(path).ok());
    const Layer* l = r.layer();
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    AUREA_CHECK_EQ(l->text.animators.size(), usize{1});
    const TextAnimator& a = l->text.animators[0];
    AUREA_CHECK_EQ(a.moreSelectors.size(), usize{1});
    if (a.moreSelectors.empty()) return;
    AUREA_CHECK_EQ(static_cast<u32>(a.moreSelectors[0].type), static_cast<u32>(kTextSelectorWiggly));
    AUREA_CHECK_NEAR(a.moreSelectors[0].correlation, 25.0f, 1e-4f);
    AUREA_CHECK_NEAR(a.lineSpacing.y, 14.0f, 1e-4f);
    AUREA_CHECK_EQ(static_cast<u32>(a.anchorGrouping), 3u);
}

// v70 · o restante do animador do After Effects: HSB e opacidade do traço.
AUREA_TEST(TextAnimatorUniversal, StrokeHueSaturationBrightnessAndOpacity) {
    TextData t;
    t.strokeColor = Vec4{1, 0, 0, 1};   // vermelho
    TextAnimator a;
    a.selector.type = kTextSelectorRange;
    a.props = kTextPropStrokeHsb | kTextPropStrokeOpacity;
    a.strokeHue = 120.0f;               // vermelho + 120° = verde
    a.strokeOpacity = 40.0f;
    t.animators.push_back(a);
    std::vector<GlyphAnim> out;
    evaluate_text_animators(t, TrackSet{}, 0, 30, kUnits, 4, 1, 1, out);
    AUREA_CHECK_EQ(out.size(), usize{4});
    for (const GlyphAnim& g : out) {
        AUREA_CHECK_NEAR(g.stroke.x, 0.0f, 1e-4f);
        AUREA_CHECK_NEAR(g.stroke.y, 1.0f, 1e-4f);
        AUREA_CHECK_NEAR(g.stroke.z, 0.0f, 1e-4f);
        AUREA_CHECK_NEAR(g.stroke.w, 1.0f, 1e-4f);
        AUREA_CHECK_NEAR(g.strokeOpacity, 0.4f, 1e-4f);
    }
    // Brilho −100 apaga a cor; sem a propriedade de opacidade o traço fica inteiro.
    t.animators[0].strokeBrightness = -100.0f;
    t.animators[0].props = kTextPropStrokeHsb;
    evaluate_text_animators(t, TrackSet{}, 0, 30, kUnits, 4, 1, 1, out);
    AUREA_CHECK_NEAR(out[0].stroke.y, 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(out[0].strokeOpacity, 1.0f, 1e-6f);
}

// Tipo de tracking (antes e depois, antes, depois) e âncora da linha.
AUREA_TEST(TextAnimatorUniversal, TrackingTypeAndLineAnchor) {
    TextData t;
    TextAnimator a;
    a.selector.type = kTextSelectorRange;
    a.props = kTextPropTracking;
    a.tracking = 10.0f;
    t.animators.push_back(a);
    auto shifts = [&]() {
        std::vector<GlyphAnim> out;
        evaluate_text_animators(t, TrackSet{}, 0, 30, kUnits, 4, 1, 1, out);
        std::vector<f32> s;
        for (const GlyphAnim& g : out) s.push_back(g.trackingShift);
        return s;
    };
    auto expect = [](const std::vector<f32>& got, std::initializer_list<f32> want) {
        AUREA_CHECK_EQ(got.size(), want.size());
        usize i = 0;
        for (f32 w : want) { if (i < got.size()) AUREA_CHECK_NEAR(got[i], w, 1e-4f); ++i; }
    };
    expect(shifts(), {0, 10, 20, 30});          // depois: o padrão de sempre do Aurea
    t.animators[0].trackingType = 1;
    expect(shifts(), {10, 20, 30, 40});         // antes: a própria letra também anda
    t.animators[0].trackingType = 0;
    expect(shifts(), {5, 15, 25, 35});          // antes e depois: metade de cada lado
    t.animators[0].trackingType = 2;
    t.animators[0].props |= kTextPropLineAnchor;
    t.animators[0].lineAnchor = 50.0f;          // o meio da linha fica parado
    expect(shifts(), {-20, -10, 0, 10});
}

// Valor de caractere e o intervalo (maiúsculas e dígitos, ou Unicode completo).
AUREA_TEST(TextAnimatorUniversal, CharacterValueAndCharacterRange) {
    TextData t;
    t.content = "Az9!";
    TextAnimator a;
    a.selector.type = kTextSelectorRange;
    a.props = kTextPropCharOffset;
    a.charOffset = 1.0f;
    t.animators.push_back(a);
    AUREA_CHECK_EQ(apply_char_offset(t, TrackSet{}, 0, 30), std::string("Ba0!"));   // como sempre foi
    t.animators[0].charRange = 1;
    AUREA_CHECK_EQ(apply_char_offset(t, TrackSet{}, 0, 30), std::string("B{:\""));  // Unicode completo
    t.animators[0].props = kTextPropCharValue;
    t.animators[0].charValue = 0x41;   // "A"
    AUREA_CHECK_EQ(apply_char_offset(t, TrackSet{}, 0, 30), std::string("AAAA"));
    // Valor e deslocamento juntos: troca e depois anda; só a metade selecionada.
    t.animators[0].props = kTextPropCharValue | kTextPropCharOffset;
    t.animators[0].charRange = 0;
    t.animators[0].selector.end = 50.0f;
    AUREA_CHECK_EQ(apply_char_offset(t, TrackSet{}, 0, 30), std::string("BB9!"));
    t.animators[0].charValue = 0;      // 0 deixa a letra
    t.animators[0].props = kTextPropCharValue;
    AUREA_CHECK_EQ(apply_char_offset(t, TrackSet{}, 0, 30), std::string("Az9!"));
}

AUREA_TEST(TextAnimatorUniversal, AfterEffectsRemainderSurvivesProjectRoundTrip) {
    Rig r;
    if (!r.id) return;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropStrokeHsb | kTextPropStrokeOpacity | kTextPropCharValue | kTextPropLineAnchor |
                                                kTextPropTracking), 0);
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimStrokeHue, 90));
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimStrokeSaturation, -30));
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimStrokeBrightness, 20));
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimStrokeOpacity, 55));
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimCharValue, 66.4f));
    AUREA_CHECK(r.e.set_text_anim_param(r.id, 0, kAnimLineAnchor, 75));
    AUREA_CHECK(r.e.set_text_animator_field(r.id, 0, 10, 1));
    AUREA_CHECK(r.e.set_text_animator_field(r.id, 0, 11, 0));
    f32 style[21] = {};
    AUREA_CHECK(r.e.query_text_style(r.id, style, 21));
    AUREA_CHECK_NEAR(style[20], 0.0f, 1e-6f);
    style[20] = 2;
    AUREA_CHECK(r.e.set_text_style(r.id, style, 21));
    std::filesystem::create_directories("build/reference/text-universal");
    const char* path = "build/reference/text-universal/ae-remainder.aurea";
    AUREA_CHECK(r.e.save_project(path).ok());
    AUREA_CHECK(r.e.load_project(path).ok());
    const Layer* l = r.layer();
    AUREA_CHECK(l != nullptr);
    if (!l || l->text.animators.empty()) return;
    const TextAnimator& a = l->text.animators[0];
    AUREA_CHECK((a.props & (kTextPropStrokeHsb | kTextPropStrokeOpacity | kTextPropCharValue | kTextPropLineAnchor)) ==
                (kTextPropStrokeHsb | kTextPropStrokeOpacity | kTextPropCharValue | kTextPropLineAnchor));
    AUREA_CHECK_NEAR(a.strokeHue, 90.0f, 1e-4f);
    AUREA_CHECK_NEAR(a.strokeSaturation, -30.0f, 1e-4f);
    AUREA_CHECK_NEAR(a.strokeBrightness, 20.0f, 1e-4f);
    AUREA_CHECK_NEAR(a.strokeOpacity, 55.0f, 1e-4f);
    AUREA_CHECK_NEAR(a.charValue, 66.0f, 1e-4f);    // código inteiro
    AUREA_CHECK_NEAR(a.lineAnchor, 75.0f, 1e-4f);
    AUREA_CHECK_EQ(static_cast<u32>(a.charRange), 1u);
    AUREA_CHECK_EQ(static_cast<u32>(a.trackingType), 0u);
    AUREA_CHECK_EQ(static_cast<u32>(l->text.renderOrder), 2u);
    f32 fields[kTextAnimatorFieldCount] = {};
    AUREA_CHECK(r.e.query_text_animator_fields(r.id, 0, fields, kTextAnimatorFieldCount) > 0);
    AUREA_CHECK_NEAR(fields[10], 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(fields[11], 0.0f, 1e-6f);
}

// v72, Character Alignment do AE: a letra trocada fica na vaga da original
// (esquerda, centro, direita) ou o layout é refeito com o avanço novo (kerning).
AUREA_TEST(TextAnimatorUniversal, CharacterAlignmentKeepsTheSlotOfTheOriginalLetter) {
    const auto font = default_font();
    AUREA_CHECK(font != nullptr);
    if (!font) return;
    TextData t;
    t.content = "iiii";
    t.size = 100.0f;
    TextAnimator a;
    a.selector.type = kTextSelectorRange;
    a.selector.end = 25.0f;              // só a primeira letra
    a.props = kTextPropCharValue;
    a.charValue = 0x57;                  // "W", bem mais largo que "i"
    t.animators.push_back(a);
    TextLayout plain;
    AUREA_CHECK(layout_quads(*font, t, 0.0f, plain));
    AUREA_CHECK_EQ(plain.quads.size(), 4u);
    if (plain.quads.size() != 4u) return;
    const f32 slot = plain.quads[0].advance;
    auto shaped = [&](u8 align, TextLayout& L) {
        t.animators[0].charAlign = align;
        TextData out = t;
        apply_text_content(t, TrackSet{}, 0, 30, out);
        AUREA_CHECK_EQ(out.content, std::string("Wiii"));
        AUREA_CHECK_EQ(out.charSlots.empty(), align == 3);
        AUREA_CHECK(layout_quads(*font, out, 0.0f, L));
        return out;
    };
    TextLayout kern, left, center, right;
    (void)shaped(3, kern);
    (void)shaped(0, left);
    const TextData centered = shaped(1, center);
    (void)shaped(2, right);
    if (kern.quads.size() != 4u || left.quads.size() != 4u || center.quads.size() != 4u || right.quads.size() != 4u) return;
    const f32 wide = kern.quads[0].advance;
    std::printf("vaga do i %.2f px, W %.2f px; ", slot, wide);
    AUREA_CHECK(wide > slot + 10.0f);
    // Ajustar kerning (o de sempre): as letras seguintes andam.
    AUREA_CHECK(kern.quads[1].penX > plain.quads[1].penX + 10.0f);
    // Na vaga: as seguintes ficam onde estavam e a vaga mantém a largura da original.
    for (const TextLayout* L : {&left, &center, &right}) {
        AUREA_CHECK_NEAR(L->quads[1].penX, plain.quads[1].penX, 0.05f);
        AUREA_CHECK_NEAR(L->quads[3].penX, plain.quads[3].penX, 0.05f);
        AUREA_CHECK_NEAR(L->quads[0].advance, slot, 0.05f);
    }
    // O W dentro da vaga: à esquerda no começo, no centro meio excesso antes, à direita o excesso todo.
    const f32 excess = wide - slot;
    AUREA_CHECK_NEAR((center.quads[0].x0 - center.quads[1].penX) - (left.quads[0].x0 - left.quads[1].penX), -excess * 0.5f, 0.6f);
    AUREA_CHECK_NEAR((right.quads[0].x0 - right.quads[1].penX) - (left.quads[0].x0 - left.quads[1].penX), -excess, 0.6f);
    // A vaga entra na chave do cache do raster (o mesmo texto com outro alinhamento muda os pixels).
    TextData kerned = centered;
    kerned.charSlots.clear();
    AUREA_CHECK(raster_key(centered, 1.0f) != raster_key(kerned, 1.0f));
    // Campo 12 do animador: lido, escrito e preso em 0..3.
    f32 fields[kTextAnimatorFieldCount] = {};
    read_animator_fields(t.animators[0], fields);
    AUREA_CHECK_NEAR(fields[12], 2.0f, 1e-6f);
    AUREA_CHECK(write_animator_field(t.animators[0], 12, 9.0f));
    AUREA_CHECK_EQ(static_cast<u32>(t.animators[0].charAlign), 3u);
    AUREA_CHECK_EQ(static_cast<u32>(TextAnimator{}.charAlign), 3u);   // projeto antigo: o layout de sempre
}

// v72: a varredura (janela do Transformar texto do Motion) como seletor do animador comum.
AUREA_TEST(TextAnimatorUniversal, SweepSelectorFollowsTheMotionWindow) {
    TextData t;
    TextAnimator a = position_animator();
    a.selector = make_text_selector(kTextSelectorSweep);
    a.selector.start = 0.0f;
    a.selector.end = 50.0f;
    a.selector.overlap = 100.0f;
    t.animators.push_back(a);
    const std::vector<f32> x = moved(t);
    AUREA_CHECK_EQ(x.size(), 4u);
    bool some = false, none = false;
    for (u32 i = 0; i < x.size() && i < 4; ++i) {
        const f32 w = transform_influence(0.0f, 0.5f, 0.0f, 1.0f, 0, 0.0f, 0.0f, i, 4);
        AUREA_CHECK_NEAR(x[i], 100.0f * w, 1e-3f);
        some |= w > 0.01f;
        none |= w < 0.99f;
    }
    AUREA_CHECK(some && none);   // a janela pega parte do texto, não tudo nem nada
    // Somada a uma faixa por cima: entra como seletor seguinte também.
    t.animators[0].selector = make_text_selector(kTextSelectorRange);
    t.animators[0].selector.end = 0.0f;   // a faixa não pega nada
    TextSelector sweep = make_text_selector(kTextSelectorSweep);
    sweep.start = 0.0f; sweep.end = 50.0f; sweep.overlap = 100.0f;
    t.animators[0].moreSelectors.push_back(sweep);
    const std::vector<f32> y = moved(t);
    for (u32 i = 0; i < y.size() && i < x.size(); ++i) AUREA_CHECK_NEAR(y[i], x[i], 1e-3f);
    // A interface acrescenta a varredura pela API.
    Rig r;
    if (!r.id) return;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropPosition), 0);
    AUREA_CHECK_EQ(r.e.add_text_selector(r.id, 0, kTextSelectorSweep), 1);
    f32 fields[kTextAnimatorFieldCount] = {};
    AUREA_CHECK(r.e.query_text_animator_fields(r.id, 0, fields, kTextAnimatorFieldCount) > 0);
    AUREA_CHECK_NEAR(fields[15], 2.0f, 1e-6f);
    AUREA_CHECK_NEAR(fields[16 * 2 + 1], static_cast<f32>(kTextSelectorSweep), 1e-6f);
}

// v72: mistura entre caracteres e alinhamento de caractere saem e voltam do projeto.
AUREA_TEST(TextAnimatorUniversal, InterCharacterBlendAndCharacterAlignmentSurviveRoundTrip) {
    Rig r;
    if (!r.id) return;
    AUREA_CHECK_EQ(r.e.add_text_animator(r.id, kTextPropCharOffset), 0);
    AUREA_CHECK(r.e.set_text_animator_field(r.id, 0, 12, 1));
    f32 style[22] = {};
    AUREA_CHECK(r.e.query_text_style(r.id, style, 22));
    AUREA_CHECK_NEAR(style[21], 0.0f, 1e-6f);   // Normal
    style[21] = static_cast<f32>(BlendMode::Multiply);
    AUREA_CHECK(r.e.set_text_style(r.id, style, 22));
    std::filesystem::create_directories("build/reference/text-universal");
    const char* path = "build/reference/text-universal/inter-char-blend.aurea";
    AUREA_CHECK(r.e.save_project(path).ok());
    AUREA_CHECK(r.e.load_project(path).ok());
    const Layer* l = r.layer();
    AUREA_CHECK(l != nullptr && l->text.animators.size() == 1u);
    if (!l || l->text.animators.size() != 1u) return;
    AUREA_CHECK_EQ(static_cast<u32>(l->text.interCharBlend), static_cast<u32>(BlendMode::Multiply));
    AUREA_CHECK_EQ(static_cast<u32>(l->text.animators[0].charAlign), 1u);
    f32 back[22] = {};
    AUREA_CHECK(r.e.query_text_style(r.id, back, 22));
    AUREA_CHECK_NEAR(back[21], static_cast<f32>(BlendMode::Multiply), 1e-6f);
    // Os modos de recorte não pintam cor entre letras: viram Normal.
    back[21] = static_cast<f32>(BlendMode::Mask);
    AUREA_CHECK(r.e.set_text_style(r.id, back, 22));
    AUREA_CHECK_EQ(static_cast<u32>(r.layer()->text.interCharBlend), 0u);
    // Com 21 valores (interface antiga) a mistura não muda.
    back[21] = 0;
    AUREA_CHECK(r.e.set_text_style(r.id, style, 22));
    AUREA_CHECK(r.e.set_text_style(r.id, back, 21));
    AUREA_CHECK_EQ(static_cast<u32>(r.layer()->text.interCharBlend), static_cast<u32>(BlendMode::Multiply));
}
