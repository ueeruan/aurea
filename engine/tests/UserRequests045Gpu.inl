// =============================================================================
//  Pedidos de usuários da 0.4.5 (efeitos): Reflexo na água, Envolver com luz,
//  Looks de cinema, Caneta esferográfica, Poeira e a largura das Persianas.
//  Propriedades da implementação própria (sem paridade numérica com plugins).
//  Filtro: `UserFx045`.
// =============================================================================

namespace {
constexpr const char* kFx045Water = "aurea.distort.water_reflection";
constexpr const char* kFx045Wrap = "aurea.light.light_wrap";
constexpr const char* kFx045Looks = "aurea.color.looks";
constexpr const char* kFx045Ballpoint = "aurea.transition.ballpoint";
constexpr const char* kFx045Dust = "aurea.transition.dust";

/// Cobertura de uma camada BRANCA sobre o fundo preto opaco da cena: o vermelho médio.
f32 fx045_coverage(const FloatImage& img) { return img.mean().x; }
} // namespace

AUREA_TEST(UserFx045, NewEffectsAreRegisteredWithTheirCategories) {
    AUREA_REQUIRE_GPU();
    struct Want { const char* key; const char* category; };
    for (const Want w : {Want{kFx045Water, "Distorcer"}, Want{kFx045Wrap, "Luz"}, Want{kFx045Looks, "Cor"},
                         Want{kFx045Ballpoint, "Transição"}, Want{kFx045Dust, "Transição"}}) {
        const Effect* fx = gpu().effects.find(effect_type_id(w.key));
        AUREA_CHECK_MSG(fx != nullptr, w.key);
        if (fx) AUREA_CHECK(std::strcmp(fx->info().category, w.category) == 0);
    }
    // Persianas: a largura entrou NO FIM (índice 5) — projetos antigos abrem iguais.
    const ParameterRegistry* blinds = gpu().effects.params(effect_type_id(effect_keys::kVenetianBlinds));
    AUREA_CHECK(blinds && blinds->count() == 6 && std::strcmp(blinds->at(5).id, "width") == 0);
    // Poeira: os índices do Desintegrar + a escala do ruído no fim.
    const ParameterRegistry* dust = gpu().effects.params(effect_type_id(kFx045Dust));
    const ParameterRegistry* dis = gpu().effects.params(effect_type_id("aurea.transition.disintegrate"));
    AUREA_CHECK(dust && dis && dust->count() == dis->count() + 1);
    if (dust && dis) for (u32 i = 0; i < dis->count(); ++i) AUREA_CHECK(std::strcmp(dust->at(i).id, dis->at(i).id) == 0);
}

AUREA_TEST(UserFx045, WaterReflectionMirrorsBelowTheHorizonAndRipplesInTime) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 128);
    // Camada 64×40 no alto (y 10..50): o reflexo nasce embaixo dela (horizonte 100%).
    const LayerId id = s.image(reference_image(64, 40), 48, 30);
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, kFx045Water);
    fx.params[2].constant = ParamValue::scalar(100.0f);   // opacidade
    fx.params[3].constant = ParamValue::scalar(0.0f);     // sem desvanecer
    fx.params[4].constant = ParamValue::scalar(0.0f);     // água parada
    fx.params[9].constant = ParamValue::scalar(0.0f);     // sem desfoque
    fx.params[11].constant = ParamValue::scalar(0.0f);    // sem tom
    const FloatImage still = s.render();
    // Acima da linha: intacto. Abaixo: o espelho (y 50 + d lê y 50 - d).
    AUREA_CHECK(max_diff(plain, still) > 0.05f);
    u32 mirrored = 0, total = 0;
    for (u32 x = 20; x < 76; x += 3) for (u32 d = 3; d < 36; d += 4) {
        const Vec4 a = still.v(x, 50 + d), b = still.v(x, 50 - d - 1);
        ++total;
        mirrored += std::fabs(a.x - b.x) < 0.08f && std::fabs(a.y - b.y) < 0.08f && a.w > 0.9f ? 1u : 0u;
    }
    std::printf("\n    espelho: %u/%u amostras batem ", mirrored, total);
    AUREA_CHECK(mirrored * 10 >= total * 8);
    for (u32 y = 12; y < 48; y += 5) AUREA_CHECK(std::fabs(still.v(48, y).x - plain.v(48, y).x) < 1e-3f);
    AUREA_CHECK(still.v(48, 100).x < 1e-3f && still.v(48, 100).y < 1e-3f);   // além do comprimento (40 px): o fundo

    // Ondulação animada: muda o reflexo e anda com o tempo; velocidade 0 para.
    fx.params[4].constant = ParamValue::scalar(4.0f);
    const FloatImage w0 = s.render(FrameIndex{0}), w10 = s.render(FrameIndex{10});
    AUREA_CHECK(mean_diff(w0, still) > 1e-3f);
    AUREA_CHECK(mean_diff(w0, w10) > 1e-4f);
    for (u32 y = 12; y < 48; y += 5) AUREA_CHECK(std::fabs(w0.v(30, y).y - plain.v(30, y).y) < 1e-3f);
    s.comp->layer(id)->effects.back().params[6].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(FrameIndex{0}), s.render(FrameIndex{10})) < 1e-5f);

    // Desvanecimento escurece a ponta; tom muda a cor; opacidade 0 = original.
    EffectInstance& e = s.comp->layer(id)->effects.back();
    e.params[4].constant = ParamValue::scalar(0.0f);
    e.params[3].constant = ParamValue::scalar(100.0f);
    const FloatImage faded = s.render();
    f32 fadedSum = 0, stillSum = 0;
    for (u32 x = 16; x < 80; ++x) { fadedSum += faded.v(x, 86).y; stillSum += still.v(x, 86).y; }
    AUREA_CHECK(fadedSum < 0.5f * stillSum);
    e.params[3].constant = ParamValue::scalar(0.0f);
    e.params[11].constant = ParamValue::scalar(100.0f);
    AUREA_CHECK(mean_diff(s.render(), still) > 1e-3f);
    e.params[2].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);
}

AUREA_TEST(UserFx045, LightWrapBringsTheBackgroundIntoTheEdgesOnly) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId bg = s.solid(96, 96, Vec4{1, 0, 0, 1}, 48, 48);   // fundo vermelho
    const LayerId fg = s.solid(48, 48, Vec4{0, 0, 1, 1}, 48, 48);   // azul em 24..72
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(fg, kFx045Wrap);
    fx.params[0].constant.ref = bg.pack();
    fx.params[1].constant = ParamValue::scalar(8.0f);   // largura
    const FloatImage wrapped = s.render();
    const Vec4 edge = wrapped.v(25, 48), mid = wrapped.v(48, 48), out = wrapped.v(10, 48);
    std::printf("\n    borda (%.3f %.3f %.3f) miolo (%.3f %.3f %.3f) ", edge.x, edge.y, edge.z, mid.x, mid.y, mid.z);
    AUREA_CHECK(edge.x > plain.v(25, 48).x + 0.2f);            // a luz vermelha entra pela borda
    AUREA_CHECK(std::fabs(mid.x - plain.v(48, 48).x) < 0.01f);  // o miolo fica
    const Vec4 outPlain = plain.v(10, 48);
    AUREA_CHECK(std::fabs(out.x - outPlain.x) < 1e-4f && std::fabs(out.z - outPlain.z) < 1e-4f);   // fora: intacto
    // Mais largura = a luz vai mais fundo.
    s.comp->layer(fg)->effects.back().params[1].constant = ParamValue::scalar(24.0f);
    AUREA_CHECK(s.render().v(32, 48).x > wrapped.v(32, 48).x + 0.02f);
    // Sem camada: a cor da luz (amarelada) entra pela borda.
    EffectInstance& e = s.comp->layer(fg)->effects.back();
    e.params[0].constant.ref = 0;
    const FloatImage colored = s.render();
    AUREA_CHECK(colored.v(25, 48).y > plain.v(25, 48).y + 0.1f);
    // Só o envoltório: só a luz, nada da camada no miolo.
    e.params[6].constant = ParamValue::scalar(1.0f);
    const FloatImage only = s.render();
    AUREA_CHECK(only.v(48, 48).z < 0.02f && only.v(48, 48).x > 0.9f);   // o miolo mostra o fundo vermelho
    e.params[6].constant = ParamValue::scalar(0.0f);
    // Intensidade 0 = original.
    e.params[2].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);
}

AUREA_TEST(UserFx045, CinematicLooksAreDistinctAndIntensityMixes) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 64);
    const LayerId id = s.image(reference_image(64, 64), 32, 32);
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, kFx045Looks);
    const ParameterRegistry* params = gpu().effects.params(effect_type_id(kFx045Looks));
    AUREA_CHECK(params && params->at(0).enumCount == 13);
    std::vector<FloatImage> looks;
    for (u32 k = 0; k < 13; ++k) {
        s.comp->layer(id)->effects.back().params[0].constant = ParamValue::scalar(static_cast<f32>(k));
        looks.push_back(s.render());
        AUREA_CHECK_MSG(mean_diff(looks.back(), plain) > 2e-3f, "look sem efeito");
    }
    for (u32 a = 0; a < 13; ++a) for (u32 b = a + 1; b < 13; ++b)
        AUREA_CHECK_MSG(mean_diff(looks[a], looks[b]) > 1e-3f, "dois looks iguais");
    // Noir (4) é preto e branco.
    const Vec4 noir = looks[4].v(40, 20);
    AUREA_CHECK(std::fabs(noir.x - noir.y) < 0.01f && std::fabs(noir.y - noir.z) < 0.01f);
    // Intensidade: 0 = original; 50 fica no meio do caminho.
    EffectInstance& e = s.comp->layer(id)->effects.back();
    e.params[0].constant = ParamValue::scalar(0.0f);
    e.params[1].constant = ParamValue::scalar(50.0f);
    const f32 half = mean_diff(s.render(), plain);
    AUREA_CHECK(half > 0.25f * mean_diff(looks[0], plain) && half < 0.75f * mean_diff(looks[0], plain));
    e.params[1].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);
    // Ajustes por cima do look: exposição clareia.
    e.params[1].constant = ParamValue::scalar(100.0f);
    e.params[2].constant = ParamValue::scalar(1.0f);
    AUREA_CHECK(s.render().mean().y > looks[0].mean().y + 0.02f);
}

AUREA_TEST(UserFx045, VenetianBlindsWidthInPixelsMatchesTheStripCount) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 64);
    const LayerId id = s.solid(96, 64, Vec4{1, 1, 1, 1}, 48, 32);
    auto& fx = s.add_effect(id, effect_keys::kVenetianBlinds);
    fx.params[0].constant = ParamValue::scalar(40.0f);   // conclusão
    fx.params[4].constant = ParamValue::scalar(4.0f);    // 4 faixas em 96 px = 24 px
    const FloatImage byCount = s.render();
    EffectInstance& e = s.comp->layer(id)->effects.back();
    e.params[4].constant = ParamValue::scalar(10.0f);    // ignorado quando há largura
    e.params[5].constant = ParamValue::scalar(24.0f);
    const FloatImage byWidth = s.render();
    AUREA_CHECK(max_diff(byCount, byWidth) < 1e-4f);
    e.params[5].constant = ParamValue::scalar(12.0f);
    AUREA_CHECK(max_diff(byCount, s.render()) > 0.5f);
    // Animável: a conclusão por keyframe vai de inteira a vazia.
    e.params[0].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(s.render().mean().x > 0.99f);
    e.params[0].constant = ParamValue::scalar(100.0f);
    AUREA_CHECK(s.render().mean().x < 1e-3f);
}

AUREA_TEST(UserFx045, DustBlowsTheLayerAwayDeterministically) {
    AUREA_REQUIRE_GPU();
    std::vector<ParamValue> values;
    FloatImage half;
    {
        Scene s(96, 64);
        const LayerId id = s.solid(64, 40, Vec4{1, 1, 1, 1}, 48, 32);
        const FloatImage plain = s.render();
        s.add_effect(id, kFx045Dust);
        AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);   // conclusão 0 = a camada
        EffectInstance& e = s.comp->layer(id)->effects.back();
        e.params[0].constant = ParamValue::scalar(50.0f);
        e.params[5].constant = ParamValue::scalar(60.0f);   // vento curto (cabe no quadro)
        e.params[13].constant = ParamValue::scalar(10.0f);  // manchas menores que a camada pequena
        half = s.render();
        AUREA_CHECK(max_diff(half, s.render()) < 1e-5f);    // determinística
        // A cobertura cai aos poucos com a conclusão (a frente anda por igual).
        f32 prev = fx045_coverage(plain) + 1e-3f;
        for (const f32 c : {10.0f, 25.0f, 40.0f, 60.0f, 80.0f}) {
            e.params[0].constant = ParamValue::scalar(c);
            const f32 cov = fx045_coverage(s.render());
            std::printf("\n    poeira %.0f%%: cobertura %.3f ", c, cov);
            AUREA_CHECK(cov < prev);
            prev = cov;
        }
        e.params[0].constant = ParamValue::scalar(50.0f);
        std::printf("\n    poeira 50%%: cobertura %.3f (camada %.3f) ", fx045_coverage(half), fx045_coverage(plain));
        AUREA_CHECK(fx045_coverage(half) > 0.05f && fx045_coverage(half) < fx045_coverage(plain) - 0.05f);
        for (u32 p = 0; p < 13; ++p) values.push_back(e.params[p].constant);
        // Outra semente: outra poeira. 100% = tudo levado.
        e.params[9].constant = ParamValue::scalar(7.0f);
        AUREA_CHECK(mean_diff(s.render(), half) > 1e-3f);
        e.params[0].constant = ParamValue::scalar(100.0f);
        AUREA_CHECK(fx045_coverage(s.render()) < 1e-4f);
    }
    // O Desintegrar com os mesmos valores dá outra imagem (grão redondo, frente de ruído).
    Scene s(96, 64);
    const LayerId id = s.solid(64, 40, Vec4{1, 1, 1, 1}, 48, 32);
    auto& dis = s.add_effect(id, "aurea.transition.disintegrate");
    for (u32 p = 0; p < 13; ++p) dis.params[p].constant = values[p];
    AUREA_CHECK(mean_diff(s.render(), half) > 1e-3f);
}

AUREA_TEST(UserFx045, BallpointRevealsOrRemovesWithExactEnds) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 64);
    const LayerId id = s.solid(96, 64, Vec4{1, 1, 1, 1}, 48, 32);
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, kFx045Ballpoint);
    AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);   // revelar em 100% = a camada
    EffectInstance& e = s.comp->layer(id)->effects.back();
    e.params[0].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(fx045_coverage(s.render()) < 1e-4f);
    e.params[0].constant = ParamValue::scalar(40.0f);
    const FloatImage mid = s.render();
    std::printf("\n    caneta 40%%: cobertura %.3f ", fx045_coverage(mid));
    AUREA_CHECK(fx045_coverage(mid) > 0.1f && fx045_coverage(mid) < 0.9f);
    // A primeira faixa (topo) aparece antes da última (embaixo).
    f32 top = 0, bottom = 0;
    for (u32 x = 0; x < 96; ++x) { top += mid.v(x, 3).x; bottom += mid.v(x, 60).x; }
    AUREA_CHECK(top > bottom + 5.0f);
    // A tinta azul aparece no traço fresco.
    e.params[9].constant = ParamValue::scalar(100.0f);
    bool blue = false;
    const FloatImage inked = s.render();
    for (u32 y = 0; y < 64 && !blue; ++y) for (u32 x = 0; x < 96 && !blue; ++x) {
        const Vec4 c = inked.v(x, y);
        blue = c.z > c.x + 0.3f;
    }
    AUREA_CHECK(blue);
    // Remover: 0% = a camada, 100% = vazio.
    e.params[1].constant = ParamValue::scalar(1.0f);
    e.params[0].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);
    e.params[0].constant = ParamValue::scalar(100.0f);
    AUREA_CHECK(fx045_coverage(s.render()) < 1e-4f);
    // Transição automática: segue o trecho visível da camada.
    e.params[1].constant = ParamValue::scalar(0.0f);
    e.params[2].constant = ParamValue::scalar(1.0f);
    Layer* l = s.comp->layer(id);
    l->start = FrameIndex{0};
    l->end = FrameIndex{30};
    const f32 a5 = fx045_coverage(s.render(FrameIndex{5})), a25 = fx045_coverage(s.render(FrameIndex{25}));
    AUREA_CHECK(a25 > a5 + 0.2f);
}
