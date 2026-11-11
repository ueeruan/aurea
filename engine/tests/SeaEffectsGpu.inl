// =============================================================================
//  Pacote "Mar" (0.5.2): Oceano, Embaixo d'água, Cáusticas, Onda na praia e
//  os efeitos de água que já existiam (Ondulação radial ganhou velocidade e
//  brilho no fim; Reflexo na água). Propriedades da implementação própria:
//  registro e índices, determinismo no tempo (export = prévia), horizonte,
//  centro da ondulação, espelho do reflexo e o avanço da onda.
//  Incluído no fim de test_gpu.cpp. Filtro: `SeaPack`.
// =============================================================================

namespace {
constexpr const char* kSeaOcean = "aurea.generate.ocean";
constexpr const char* kSeaUnderwater = "aurea.distort.underwater";
constexpr const char* kSeaCaustics = "aurea.light.caustics";
constexpr const char* kSeaShore = "aurea.transition.shore_wave";

/// Os valores de demonstração do efeito (os da prévia do catálogo) na instância.
void sea_demo(EffectInstance& e) {
    const Effect* fx = gpu().effects.find(e.type);
    const ParameterRegistry* params = gpu().effects.params(e.type);
    if (!fx || !params) return;
    std::vector<ParamValue> values(params->count());
    for (u32 i = 0; i < params->count(); ++i) values[i] = e.params[i].constant;
    if (fx->demo_values(e, values)) for (u32 i = 0; i < params->count(); ++i) e.params[i].constant = values[i];
}

/// Imagem simétrica em x e em y em volta do centro (96×96): anéis + xadrez largo.
ImagePixels sea_symmetric_image(u32 n) {
    ImagePixels px;
    px.width = px.height = n;
    px.rgba.resize(static_cast<usize>(n) * n * 4);
    const f32 c = 0.5f * static_cast<f32>(n);
    for (u32 y = 0; y < n; ++y) for (u32 x = 0; x < n; ++x) {
        const f32 dx = std::fabs(x + 0.5f - c), dy = std::fabs(y + 0.5f - c);
        const f32 v = 0.5f + 0.25f * std::sin(dx * 0.37f) + 0.2f * std::cos(dy * 0.29f);
        u8* p = &px.rgba[(static_cast<usize>(y) * n + x) * 4];
        p[0] = static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        p[1] = static_cast<u8>(std::clamp(1.0f - v, 0.0f, 1.0f) * 255.0f + 0.5f);
        p[2] = static_cast<u8>(std::clamp(0.3f + 0.4f * std::sin((dx + dy) * 0.21f), 0.0f, 1.0f) * 255.0f + 0.5f);
        p[3] = 255;
    }
    return px;
}
} // namespace

AUREA_TEST(SeaPack, EffectsAreRegisteredAppendOnlyWithTheirCategories) {
    AUREA_REQUIRE_GPU();
    struct Want { const char* key; const char* category; u32 params; };
    for (const Want w : {Want{kSeaOcean, "Gerar", 20}, Want{kSeaUnderwater, "Distorcer", 11}, Want{kSeaCaustics, "Luz", 9},
                         Want{kSeaShore, "Transição", 13}}) {
        const Effect* fx = gpu().effects.find(effect_type_id(w.key));
        AUREA_CHECK_MSG(fx != nullptr, w.key);
        if (!fx) continue;
        AUREA_CHECK(std::strcmp(fx->info().category, w.category) == 0);
        const ParameterRegistry* p = gpu().effects.params(effect_type_id(w.key));
        AUREA_CHECK(p && p->count() == w.params);
    }
    // Ondulação radial: velocidade e brilho entraram NO FIM (6 e 7); os seis de antes intactos.
    const ParameterRegistry* ripple = gpu().effects.params(effect_type_id(effect_keys::kRipple));
    AUREA_CHECK(ripple && ripple->count() == 8);
    if (ripple && ripple->count() == 8) {
        const char* ids[] = {"center_x", "center_y", "amplitude", "wavelength", "phase", "decay", "speed", "highlight"};
        for (u32 i = 0; i < 8; ++i) AUREA_CHECK_MSG(std::strcmp(ripple->at(i).id, ids[i]) == 0, ids[i]);
        AUREA_CHECK(ripple->at(6).defaultValue.as_float() == 0.0f && ripple->at(7).defaultValue.as_float() == 0.0f);
    }
    // O pacote fica no FIM do catálogo (a ordem é a do catálogo salvo).
    const u32 n = gpu().effects.count();
    AUREA_CHECK(n >= 4);
    if (n >= 4) AUREA_CHECK(std::strcmp(gpu().effects.at(n - 1).info().key, kSeaShore) == 0 ||
                            gpu().effects.find(effect_type_id(kSeaShore)) != nullptr);
}

// Função pura do tempo: o quadro t renderizado de novo (depois de outro
// instante, fora de ordem) sai idêntico; e a água anda (t ≠ t').
AUREA_TEST(SeaPack, EveryEffectIsAPureFunctionOfTime) {
    AUREA_REQUIRE_GPU();
    for (const char* key : {kSeaOcean, kSeaUnderwater, kSeaCaustics, kSeaShore, effect_keys::kRipple}) {
        Scene s(96, 96);
        const LayerId id = s.image(reference_image(96, 96), 48, 48);
        auto& fx = s.add_effect(id, key);
        sea_demo(fx);
        if (std::strcmp(key, effect_keys::kRipple) == 0) {
            fx.params[6].constant = ParamValue::scalar(1.5f);   // velocidade
            fx.params[7].constant = ParamValue::scalar(80.0f);  // brilho
        }
        const FloatImage a = s.render(FrameIndex{17});
        const FloatImage b = s.render(FrameIndex{4});
        const FloatImage c = s.render(FrameIndex{17});
        const FloatImage d = s.render(FrameIndex{29});
        const FloatImage e2 = s.render(FrameIndex{17});
        const f32 same = std::max(max_diff(a, c), max_diff(a, e2));
        std::printf("\n    %s: t17 x t17 %.2e, t17 x t4 %.4f ", key, same, mean_diff(a, b));
        AUREA_CHECK_MSG(same == 0.0f, key);
        AUREA_CHECK_MSG(mean_diff(a, b) > 1e-4f || mean_diff(a, d) > 1e-4f, key);
        for (const f32 v : a.px) if (!std::isfinite(v)) { AUREA_CHECK_MSG(false, key); break; }
    }
}

// A ondulação nasce no CENTRO: com uma imagem simétrica em volta dele, o
// resultado é simétrico em x e em y. Velocidade anda os anéis; 0 = parado.
AUREA_TEST(SeaPack, RippleIsCenteredAndTravelsWithSpeed) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.image(sea_symmetric_image(96), 48, 48);
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, effect_keys::kRipple);
    fx.params[2].constant = ParamValue::scalar(6.0f);    // amplitude
    fx.params[3].constant = ParamValue::scalar(18.0f);   // comprimento
    fx.params[6].constant = ParamValue::scalar(1.0f);    // 1 onda por segundo
    const FloatImage r0 = s.render(FrameIndex{0});
    AUREA_CHECK(mean_diff(r0, plain) > 1e-3f);
    f32 asym = 0.0f;
    for (u32 y = 2; y < 94; y += 3) for (u32 x = 2; x < 94; x += 3) {
        const Vec4 p = r0.v(x, y), mx = r0.v(95 - x, y), my = r0.v(x, 95 - y);
        asym = std::max({asym, std::fabs(p.x - mx.x), std::fabs(p.y - mx.y), std::fabs(p.x - my.x), std::fabs(p.y - my.y)});
    }
    std::printf("\n    assimetria da ondulação: %.4f ", asym);
    AUREA_CHECK(asym < 0.02f);
    // Anda com o tempo; meia volta depois (15 quadros a 30 fps, 1 onda/s) é outra imagem.
    const FloatImage r15 = s.render(FrameIndex{15});
    AUREA_CHECK(mean_diff(r0, r15) > 1e-3f);
    // Um segundo depois: a mesma fase (1 onda por segundo) — o anel deu uma volta inteira.
    AUREA_CHECK(max_diff(r0, s.render(FrameIndex{30})) < 2e-3f);
    EffectInstance& e = s.comp->layer(id)->effects.back();
    e.params[6].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(FrameIndex{0}), s.render(FrameIndex{15})) < 1e-6f);
    // Brilho acende as cristas (só soma luz).
    const FloatImage dull = s.render();
    e.params[7].constant = ParamValue::scalar(150.0f);
    const FloatImage shiny = s.render();
    AUREA_CHECK(shiny.mean().x > dull.mean().x + 1e-3f);
    f32 darker = 0.0f;
    for (usize i = 0; i < shiny.px.size(); ++i) darker = std::max(darker, dull.px[i] - shiny.px[i]);
    AUREA_CHECK(darker < 1e-4f);
}

// Reflexo na água (o efeito que já existia, agora no grupo Mar): espelha em
// volta da linha do horizonte numa imagem que enche o quadro.
AUREA_TEST(SeaPack, WaterReflectionMirrorsAroundTheHorizon) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.image(reference_image(96, 96), 48, 48);
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, "aurea.distort.water_reflection");
    fx.params[0].constant = ParamValue::scalar(50.0f);    // horizonte no meio (y = 48)
    fx.params[2].constant = ParamValue::scalar(100.0f);   // opacidade
    fx.params[3].constant = ParamValue::scalar(0.0f);     // sem desvanecer
    fx.params[4].constant = ParamValue::scalar(0.0f);     // água parada
    fx.params[9].constant = ParamValue::scalar(0.0f);     // sem desfoque
    fx.params[11].constant = ParamValue::scalar(0.0f);    // sem tom
    const FloatImage r = s.render();
    u32 ok = 0, total = 0;
    for (u32 x = 4; x < 92; x += 5) for (u32 d = 2; d < 44; d += 4) {
        const Vec4 below = r.v(x, 48 + d), above = plain.v(x, 47 - d);
        ++total;
        ok += std::fabs(below.x - above.x) < 0.06f && std::fabs(below.y - above.y) < 0.06f ? 1u : 0u;
    }
    std::printf("\n    espelho no horizonte: %u/%u ", ok, total);
    AUREA_CHECK(ok * 10 >= total * 8);
    for (u32 y = 4; y < 46; y += 6) AUREA_CHECK(std::fabs(r.v(30, y).x - plain.v(30, y).x) < 1e-3f);   // acima: intacto
}

// Oceano: céu acima do horizonte, mar abaixo; o céu pode ser a própria camada;
// "Recortar pela camada" respeita o alfa.
AUREA_TEST(SeaPack, OceanDrawsSkyAboveAndSeaBelowTheHorizon) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 96);
    const LayerId id = s.solid(128, 96, Vec4{1, 0, 0, 1}, 64, 48);   // sólido vermelho: o gerador cobre
    auto& fx = s.add_effect(id, kSeaOcean);
    fx.params[14].constant = ParamValue::scalar(40.0f);   // horizonte em y = 38,4
    fx.params[13].constant = ParamValue::scalar(70.0f);   // sol alto, fora do quadro (o céu fica só degradê)
    const FloatImage img = s.render(FrameIndex{12});
    const Vec4 top = img.v(64, 2), nearSky = img.v(20, 30), sea = img.v(64, 80);
    std::printf("\n    céu (%.3f %.3f %.3f) mar (%.3f %.3f %.3f) ", top.x, top.y, top.z, sea.x, sea.y, sea.z);
    AUREA_CHECK(top.z > top.x + 0.2f && top.z > 0.5f);          // céu azul, nada do vermelho
    AUREA_CHECK(nearSky.z > nearSky.x);
    AUREA_CHECK(sea.z > sea.x && sea.x < 0.5f);                  // água: azul-esverdeada
    // O mar tem relevo (ondas): variação ao longo de uma linha abaixo do horizonte.
    f32 lo = 1e9f, hi = -1e9f;
    for (u32 x = 0; x < 128; ++x) { const f32 g = img.v(x, 84).y; lo = std::min(lo, g); hi = std::max(hi, g); }
    AUREA_CHECK(hi - lo > 0.01f);
    // Linhas do céu são lisas (degradê e halo do sol, sem ondas): vizinhos quase iguais.
    f32 jump = 0.0f;
    for (u32 x = 40; x < 88; ++x) jump = std::max(jump, std::fabs(img.v(x + 1, 6).z - img.v(x, 6).z));
    std::printf("degrau no céu %.4f ", jump);
    AUREA_CHECK(jump < 0.01f);
    // Ondas maiores mudam o mar, não o céu.
    fx.params[0].constant = ParamValue::scalar(3.0f);
    const FloatImage big = s.render(FrameIndex{12});
    AUREA_CHECK(mean_diff(big, img) > 1e-3f);
    AUREA_CHECK(std::fabs(big.v(64, 4).z - img.v(64, 4).z) < 1e-4f);
    // Céu = imagem da camada: acima do horizonte, a própria camada (vermelha).
    s.comp->layer(id)->effects.back().params[7].constant = ParamValue::scalar(1);
    const FloatImage own = s.render(FrameIndex{12});
    AUREA_CHECK(own.v(64, 10).x > 0.95f && own.v(64, 10).z < 0.05f);
    AUREA_CHECK(own.v(64, 80).x > 0.05f);   // o vermelho também se reflete na água
}

AUREA_TEST(SeaPack, OceanCanFillOnlyTheLayerAlpha) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    auto px = reference_image(96, 96);
    for (u32 y = 0; y < 96; ++y) for (u32 x = 0; x < 48; ++x) px.rgba[(static_cast<usize>(y) * 96 + x) * 4 + 3] = 0;
    const LayerId id = s.image(std::move(px), 48, 48);
    (void)s.add_effect(id, kSeaOcean);
    const FloatImage full = s.render();
    AUREA_CHECK(full.v(10, 80).z > 0.02f);   // sem recorte: enche a caixa toda
    s.comp->layer(id)->effects.back().params[18].constant = ParamValue::boolean(true);
    const FloatImage cut = s.render();
    AUREA_CHECK(cut.v(10, 80).x + cut.v(10, 80).y + cut.v(10, 80).z < 1e-4f);   // metade transparente: fundo preto
    AUREA_CHECK(std::fabs(cut.v(80, 80).z - full.v(80, 80).z) < 1e-4f);
}

// Embaixo d'água: tom da água, mais fundo embaixo; o alfa da camada fica.
AUREA_TEST(SeaPack, UnderwaterTintsDeeperTowardTheBottomInsideTheLayer) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.solid(64, 64, Vec4{0.9f, 0.9f, 0.9f, 1}, 48, 48);   // 16..80
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, kSeaUnderwater);
    fx.params[0].constant = ParamValue::scalar(0.0f);   // sem ondulação
    fx.params[3].constant = ParamValue::scalar(0.0f);   // sem cáusticas
    fx.params[7].constant = ParamValue::scalar(0.0f);   // sem raios
    fx.params[8].constant = ParamValue::scalar(0.0f);   // sem partículas
    const FloatImage img = s.render();
    const Vec4 top = img.v(48, 18), bottom = img.v(48, 78);
    std::printf("\n    topo (%.3f %.3f %.3f) fundo (%.3f %.3f %.3f) ", top.x, top.y, top.z, bottom.x, bottom.y, bottom.z);
    AUREA_CHECK(top.z > top.x + 0.1f);                                   // o vermelho some primeiro
    AUREA_CHECK(bottom.x + bottom.y + bottom.z < top.x + top.y + top.z);  // mais escuro no fundo
    AUREA_CHECK(img.v(5, 5).x + img.v(5, 5).y + img.v(5, 5).z < 1e-4f);   // fora da camada: nada
    // Tudo a zero: a camada como estava.
    s.comp->layer(id)->effects.back().params[6].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);
    // Cáusticas, raios e partículas acendem (só por dentro).
    EffectInstance& e = s.comp->layer(id)->effects.back();
    e.params[3].constant = ParamValue::scalar(80.0f);
    e.params[4].constant = ParamValue::scalar(20.0f);
    e.params[7].constant = ParamValue::scalar(80.0f);
    e.params[8].constant = ParamValue::scalar(80.0f);
    const FloatImage lit = s.render(FrameIndex{5});
    AUREA_CHECK(lit.mean().y > plain.mean().y + 1e-3f);
    AUREA_CHECK(lit.v(5, 5).x + lit.v(5, 5).y + lit.v(5, 5).z < 1e-4f);
}

// Cáusticas: uma REDE de luz (claro e escuro dentro da camada), na cor escolhida;
// "Só as cáusticas" não passa do alfa da camada.
AUREA_TEST(SeaPack, CausticsLightANetworkInsideTheLayer) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.solid(64, 64, Vec4{0.3f, 0.3f, 0.3f, 1}, 48, 48);   // 16..80
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, kSeaCaustics);
    fx.params[1].constant = ParamValue::scalar(20.0f);   // células de 20 px
    fx.params[6].constant = ParamValue::scalar(0.0f);    // sem refração
    const FloatImage img = s.render(FrameIndex{9});
    f32 lo = 1e9f, hi = -1e9f;
    for (u32 y = 18; y < 78; ++y) for (u32 x = 18; x < 78; ++x) { const f32 g = img.v(x, y).y; lo = std::min(lo, g); hi = std::max(hi, g); }
    std::printf("\n    cáusticas: min %.3f max %.3f (sólido %.3f) ", lo, hi, plain.v(48, 48).y);
    AUREA_CHECK(hi > plain.v(48, 48).y + 0.1f);      // linhas acesas
    AUREA_CHECK(lo < plain.v(48, 48).y + 0.03f);     // e vãos quase sem luz
    AUREA_CHECK(img.v(5, 5).x + img.v(5, 5).y + img.v(5, 5).z < 1e-4f);
    // Só as cáusticas: cobertura ≤ a da camada; fora dela nada.
    s.comp->layer(id)->effects.back().params[5].constant = ParamValue::scalar(4);
    const FloatImage only = s.render(FrameIndex{9});
    AUREA_CHECK(only.v(5, 90).y < 1e-4f);
    AUREA_CHECK(only.mean().y < img.mean().y);
    // Intensidade 0 e refração 0: a camada intacta.
    EffectInstance& e = s.comp->layer(id)->effects.back();
    e.params[5].constant = ParamValue::scalar(0);
    e.params[3].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_diff(s.render(), plain) < 1e-5f);
}

// Onda na praia: a onda anda na direção do ângulo; revela atrás dela; nos
// extremos some; Remover é o inverso.
AUREA_TEST(SeaPack, ShoreWaveRevealsBehindTheFoamAlongTheAngle) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.solid(96, 96, Vec4{1, 0, 0, 1}, 48, 48);   // vermelho enchendo o quadro
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, kSeaShore);
    auto at = [&](f32 progress, u32 mode, f32 angle) {
        EffectInstance& e = s.comp->layer(id)->effects.back();
        e.params[0].constant = ParamValue::scalar(progress);
        e.params[1].constant = ParamValue::scalar(static_cast<f32>(mode));
        e.params[3].constant = ParamValue::scalar(angle);
        return s.render(FrameIndex{6});
    };
    (void)fx;
    const FloatImage mid = at(50.0f, 0, 0.0f);   // da esquerda para a direita
    std::printf("\n    meio: esquerda (%.3f %.3f %.3f) direita (%.3f %.3f %.3f) ", mid.v(4, 48).x, mid.v(4, 48).y,
                mid.v(4, 48).z, mid.v(92, 48).x, mid.v(92, 48).y, mid.v(92, 48).z);
    AUREA_CHECK(mid.v(4, 48).x > 0.9f && mid.v(4, 48).y < 0.05f);                          // já passou: a camada
    AUREA_CHECK(mid.v(92, 48).x + mid.v(92, 48).y + mid.v(92, 48).z < 1e-3f);             // ainda não: nada
    // Espuma branca em algum ponto do meio.
    f32 whitest = 0.0f;
    for (u32 x = 20; x < 76; ++x) whitest = std::max(whitest, std::min(mid.v(x, 48).y, mid.v(x, 48).z));
    AUREA_CHECK(whitest > 0.5f);
    // Avança com o progresso.
    auto red = [](const FloatImage& im) { f32 s2 = 0; for (u32 x = 0; x < 96; ++x) s2 += im.v(x, 48).x - im.v(x, 48).z; return s2; };
    AUREA_CHECK(red(at(30.0f, 0, 0.0f)) < red(at(70.0f, 0, 0.0f)));
    // Extremos: 0 = nada (revelar), 100 = a camada intacta.
    const FloatImage start = at(0.0f, 0, 0.0f);
    AUREA_CHECK(start.mean().x + start.mean().y + start.mean().z < 1e-3f);
    AUREA_CHECK(max_diff(at(100.0f, 0, 0.0f), plain) < 1e-5f);
    // Remover: o inverso (a direita ainda tem a camada).
    const FloatImage rem = at(50.0f, 1, 0.0f);
    AUREA_CHECK(rem.v(92, 48).x > 0.9f && rem.v(4, 48).x < 0.3f);
    AUREA_CHECK(max_diff(at(0.0f, 1, 0.0f), plain) < 1e-5f);
    // Ângulo 90°: de cima para baixo.
    const FloatImage down = at(50.0f, 0, 90.0f);
    AUREA_CHECK(down.v(48, 4).x > 0.9f && down.v(48, 92).x < 0.05f);
}
