// 0.4.6 · Text Animator Pro: seletores (índice, letras sem espaço, suavidade,
// modos, oscilação com correlação) e propriedades novas (âncora, agrupamento
// "tudo", espaçamento de linhas, matiz/brilho, opacidade do preenchimento).
#include "TestFramework.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::text;

namespace {
EffectInstance pro(Layer& l) {
    ParameterRegistry p; declare_animator_pro_params(p);
    EffectInstance fx; fx.type = effect_type_id(kAnimatorProEffect); fx.id = l.alloc_effect_id(); initialize_instance(fx, p);
    return fx;
}
void set(EffectInstance& fx, u32 i, f32 x, f32 y = 0, f32 z = 0) { auto& c = fx.params[i].constant; c.v[0] = x; c.v[1] = y; c.v[2] = z; }
/// 8 letras: "AB CD EF" (sem os espaços), 3 palavras, 2 linhas.
std::vector<GlyphAnim> run(const Layer& l, f64 time = 0) {
    const std::vector<GlyphUnits> units{{0, 0, 0}, {1, 0, 0}, {3, 1, 0}, {4, 1, 0}, {6, 2, 1}, {7, 2, 1}};
    std::vector<GlyphAnim> out(units.size());
    evaluate_animator_effects(l, time, 30, units, 8, 3, 2, out);
    return out;
}
} // namespace

AUREA_TEST(TextAnimatorPro, RegisteredWithStableParametersAndOnlyActsWhenEnabled) {
    EffectRegistry reg; register_builtin_effects(reg);
    const ParameterRegistry* params = reg.params(effect_type_id(kAnimatorProEffect));
    AUREA_CHECK(params != nullptr && reg.find(effect_type_id(kAnimatorProEffect)) != nullptr);
    if (!params) return;
    AUREA_CHECK_EQ(params->count(), static_cast<u32>(apCount));
    AUREA_CHECK(std::string(params->at(apUnits).id) == "units");
    AUREA_CHECK(std::string(params->at(apGroupAlign).id) == "group_align");
    AUREA_CHECK(is_animator_effect(effect_type_id(kAnimatorProEffect)) && is_animator_effect(effect_type_id(kAnimatorEffect)));
    // Padrão: seleção inteira, propriedades neutras = nada muda.
    Layer l; l.kind = LayerKind::Text; l.effects.push_back(pro(l));
    AUREA_CHECK(has_animator_effect(l));
    for (const GlyphAnim& g : run(l)) {
        AUREA_CHECK_NEAR(g.translate.x, 0.0f, 1e-5f); AUREA_CHECK_NEAR(g.scale.x, 1.0f, 1e-5f); AUREA_CHECK_NEAR(g.opacity, 1.0f, 1e-5f);
    }
}

AUREA_TEST(TextAnimatorPro, RangeInIndexUnitsSmoothnessAndCharactersExcludingSpaces) {
    // Índice: Início 1, Fim 3 de 6 unidades = a 2a e a 3a.
    for (u32 u = 0; u < 6; ++u) {
        const f32 w = pro_range_weight(1, 3, 0, 1, 0, 1.0f, 0, 0, u, 6);
        AUREA_CHECK_NEAR(w, (u == 1 || u == 2) ? 1.0f : 0.0f, 1e-5f);
    }
    // Quadrada com metade da unidade dentro: suavidade 100 % = 0,5; 0 % = corte seco.
    AUREA_CHECK_NEAR(pro_range_weight(0, 25, 0, 0, 0, 1.0f, 0, 0, 1, 4), 0.0f, 1e-5f);
    AUREA_CHECK_NEAR(pro_range_weight(0, 37.5f, 0, 0, 0, 1.0f, 0, 0, 1, 4), 0.5f, 1e-5f);
    AUREA_CHECK_NEAR(pro_range_weight(0, 37.4f, 0, 0, 0, 0.0f, 0, 0, 1, 4), 0.0f, 1e-5f);
    AUREA_CHECK_NEAR(pro_range_weight(0, 37.6f, 0, 0, 0, 0.0f, 0, 0, 1, 4), 1.0f, 1e-5f);
    // Rampa crescente satura fora do intervalo.
    AUREA_CHECK_NEAR(pro_range_weight(20, 40, 0, 0, 1, 1, 0, 0, 3, 4), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(pro_range_weight(20, 40, 0, 0, 1, 1, 0, 0, 0, 4), 0.0f, 1e-5f);
    // Letras sem espaços: 6 glifos visíveis; Fim 50 % = as 3 primeiras (o
    // índice do layout pularia por causa dos espaços).
    Layer l; l.kind = LayerKind::Text;
    EffectInstance fx = pro(l);
    set(fx, apBasedOn, 1); set(fx, apEnd, 50); set(fx, apPosition, 0, -10, 0);
    l.effects.push_back(fx);
    const auto g = run(l);
    for (usize i = 0; i < g.size(); ++i) AUREA_CHECK_NEAR(g[i].translate.y, i < 3 ? -10.0f : 0.0f, 1e-4f);
}

AUREA_TEST(TextAnimatorPro, ModesCombineSelectorsAndWigglyRespectsCorrelationAndLimits) {
    AUREA_CHECK_NEAR(combine_selector(0.25f, 0.5f, 0), 0.75f, 1e-6f);
    AUREA_CHECK_NEAR(combine_selector(1.0f, 0.25f, 1), 0.75f, 1e-6f);
    AUREA_CHECK_NEAR(combine_selector(0.5f, 0.5f, 2), 0.25f, 1e-6f);
    AUREA_CHECK_NEAR(combine_selector(0.2f, 0.7f, 3), 0.2f, 1e-6f);
    AUREA_CHECK_NEAR(combine_selector(0.2f, 0.7f, 4), 0.7f, 1e-6f);
    AUREA_CHECK_NEAR(combine_selector(0.2f, 0.7f, 5), 0.5f, 1e-6f);
    // Correlação 100 %: todas as unidades iguais; 0 %: diferentes. Sempre entre mín. e máx.
    bool differ = false;
    for (f64 t = 0; t < 3; t += 0.37) {
        const f32 a = pro_wiggle(4, 0, 0, t, 2, 1.0f, 0, 0, -1, 1), b = pro_wiggle(4, 5, 0, t, 2, 1.0f, 0, 0, -1, 1);
        AUREA_CHECK_NEAR(a, b, 1e-5f);
        const f32 c = pro_wiggle(4, 0, 0, t, 2, 0.0f, 0, 0, 0.2f, 0.6f), d = pro_wiggle(4, 5, 0, t, 2, 0.0f, 0, 0, 0.2f, 0.6f);
        AUREA_CHECK(c >= 0.2f - 1e-5f && c <= 0.6f + 1e-5f && d >= 0.2f - 1e-5f && d <= 0.6f + 1e-5f);
        differ |= std::fabs(c - d) > 1e-3f;
    }
    AUREA_CHECK(differ);
    // Subtrair sozinho inverte a seleção (parte de "tudo").
    Layer l; l.kind = LayerKind::Text;
    EffectInstance fx = pro(l);
    set(fx, apMode, 1); set(fx, apUnits, 1); set(fx, apEnd, 2); set(fx, apOpacity, 0);
    l.effects.push_back(fx);
    const auto g = run(l);
    AUREA_CHECK_NEAR(g[0].opacity, 1.0f, 1e-5f); AUREA_CHECK_NEAR(g[1].opacity, 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(g[2].opacity, 0.0f, 1e-5f); AUREA_CHECK_NEAR(g[5].opacity, 0.0f, 1e-5f);
}

AUREA_TEST(TextAnimatorPro, AnchorGroupingLineSpacingAndFillColorAdjustments) {
    Layer l; l.kind = LayerKind::Text; l.text.color = Vec4{1, 0, 0, 1};
    EffectInstance fx = pro(l);
    set(fx, apAnchor, 5, -3); set(fx, apGrouping, 3); set(fx, apGroupAlign, -100, 100);
    set(fx, apLineSpacing, 0, 12); set(fx, apFillHue, 120); set(fx, apFillOpacity, 50); set(fx, apFillBrightness, -50);
    l.effects.push_back(fx);
    const auto g = run(l);
    AUREA_CHECK_EQ(g[0].anchorGrouping, 3u);
    AUREA_CHECK_NEAR(g[0].anchor.x, 5.0f, 1e-5f); AUREA_CHECK_NEAR(g[0].anchor.y, -3.0f, 1e-5f);
    AUREA_CHECK_NEAR(g[0].groupAlign.x, -1.0f, 1e-5f); AUREA_CHECK_NEAR(g[0].groupAlign.y, 1.0f, 1e-5f);
    // Linha 0 parada, linha 1 desce 12 px.
    AUREA_CHECK_NEAR(g[0].translate.y, 0.0f, 1e-5f); AUREA_CHECK_NEAR(g[5].translate.y, 12.0f, 1e-5f);
    // Vermelho + 120° de matiz = verde, com metade do brilho e metade da opacidade.
    AUREA_CHECK_NEAR(g[0].fill.x, 0.0f, 1e-3f); AUREA_CHECK_NEAR(g[0].fill.y, 0.5f, 1e-3f); AUREA_CHECK_NEAR(g[0].fill.z, 0.0f, 1e-3f);
    AUREA_CHECK_NEAR(g[0].fill.w, 1.0f, 1e-5f); AUREA_CHECK_NEAR(g[0].fillOpacity, 0.5f, 1e-5f);
}
