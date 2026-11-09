// 0.5.0 · Animador de texto universal: seletores combináveis, expressão com
// selectorValue real, API por chave e o formato salvo.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/text/TextAnimator.hpp"
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
