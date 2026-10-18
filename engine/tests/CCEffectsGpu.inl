// Pacote CC (CCEffects.cpp, CCStylizeEffects.cpp, CCTransitions.cpp): cada
// efeito com os padrões muda o quadro sem NaN e de forma determinística, e a
// propriedade que define o efeito vale (simetria do caleidoscópio, canais
// binários do limiar, 0% = camada exata e 100% = vazio nas transições...).
namespace {
f32 cc_diff(const FloatImage& a, const FloatImage& b) {
    f32 largest = 0;
    AUREA_CHECK_EQ(a.px.size(), b.px.size());
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i) largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}
f32 cc_pixel_diff(Vec4 a, Vec4 b) {
    return std::max({std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z), std::fabs(a.w - b.w)});
}
f32 cc_alpha_sum(const FloatImage& img) {
    f32 s = 0;
    for (u32 y = 0; y < img.height; ++y) for (u32 x = 0; x < img.width; ++x) s += img.v(x, y).w;
    return s;
}
bool cc_finite(const FloatImage& img) {
    for (const f32 v : img.px) if (!std::isfinite(v)) return false;
    return true;
}
f32 cc_luma(Vec4 p) { return 0.2126f * p.x + 0.7152f * p.y + 0.0722f * p.z; }

const char* const kCcKeys[] = {
    effect_keys::kKaleidoscope, effect_keys::kSphereWrap, effect_keys::kCylinderWrap, effect_keys::kTwister,
    effect_keys::kFlowPinch, effect_keys::kGriddler, effect_keys::kRepeatTile, effect_keys::kSmear,
    effect_keys::kSplit, effect_keys::kSlant, effect_keys::kTiler, effect_keys::kHexTile,
    effect_keys::kVectorBlur, effect_keys::kLightBurst, effect_keys::kStarField, effect_keys::kThresholdRgb,
    effect_keys::kColorOffset, effect_keys::kScatter, effect_keys::kBlobby, effect_keys::kGlass,
    effect_keys::kKernel, effect_keys::kSpotlight, effect_keys::kDrizzle, effect_keys::kJaws,
    effect_keys::kScaleWipe, effect_keys::kRadialScaleWipe, effect_keys::kBurnFilm, effect_keys::kBlockLoad,
};
}

AUREA_TEST(CCEffectsGpu, EveryEffectWithDefaultsChangesTheFrameFinitelyAndDeterministically) {
    AUREA_REQUIRE_GPU();
    u32 checked = 0;
    for (const char* key : kCcKeys) {
        AUREA_CHECK_MSG(gpu().effects.find(effect_type_id(key)) != nullptr, key);
        Scene s(160, 128);
        s.comp->set_transparent_background(true);
        // Camada menor que o quadro: os efeitos que desenham fora dela aparecem.
        const auto layer = s.image(reference_image(96, 80), 80, 64);
        const auto original = s.render();
        s.add_effect(layer, key);
        const auto a = s.render(FrameIndex{3});
        const auto b = s.render(FrameIndex{3});
        const bool finite = cc_finite(a);
        const f32 change = cc_diff(original, a);
        const f32 repeat = cc_diff(a, b);
        if (!finite || change < 0.02f || repeat > 1e-5f)
            std::printf("\n    %s: finito %d, mudanca %.4f, repeticao %.6f", key, finite ? 1 : 0, change, repeat);
        AUREA_CHECK_MSG(finite, key);
        AUREA_CHECK_MSG(change >= 0.02f, key);
        AUREA_CHECK_MSG(repeat <= 1e-5f, key);
        ++checked;
    }
    AUREA_CHECK_EQ(checked, 28u);
}

AUREA_TEST(CCEffectsGpu, KaleidoscopeMirrorModesAreSymmetricAboutTheCenterLine) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kKaleidoscope);
    for (const u32 mode : {0u, 2u, 3u}) {
        fx.params[2].constant.v[0] = static_cast<f32>(mode);   // espelhamento
        fx.params[1].constant.v[0] = 30.0f;                    // tamanho (% do lado)
        const auto img = s.render();
        AUREA_CHECK(cc_diff(original, img) > 0.1f);
        f32 worst = 0;
        // Sem rotação, todo modo com espelho é simétrico pela horizontal do centro.
        for (u32 y = 0; y < 64; ++y)
            for (u32 x = 0; x < 128; ++x) worst = std::max(worst, cc_pixel_diff(img.v(x, y), img.v(x, 127 - y)));
        std::printf("    modo %u: maior assimetria %.4f\n", mode, worst);
        AUREA_CHECK(worst < 0.03f);
    }
    // A roda (sem espelho) não é simétrica.
    fx.params[2].constant.v[0] = 1.0f;
    const auto wheel = s.render();
    f32 asym = 0;
    for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 128; ++x) asym = std::max(asym, cc_pixel_diff(wheel.v(x, y), wheel.v(x, 127 - y)));
    AUREA_CHECK(asym > 0.1f);
}

AUREA_TEST(CCEffectsGpu, SphereWrapsInsideADiskAndTurnsWithRotation) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    // Camada 96×96 no centro: raio 45% de 96 = 43,2 px em volta de (64, 64).
    const auto layer = s.image(reference_image(96, 96), 64, 64);
    auto& fx = s.add_effect(layer, effect_keys::kSphereWrap);
    const auto front = s.render();
    AUREA_CHECK(front.v(64, 64).w > 0.99f);
    AUREA_CHECK(front.v(64, 64 - 40).w > 0.99f);
    AUREA_CHECK(front.v(64 + 46, 64).w < 0.01f);
    AUREA_CHECK(front.v(10, 10).w < 0.01f);
    AUREA_CHECK(front.v(120, 120).w < 0.01f);
    // De frente aparece o disco branco do meio da camada; girada 180° em Y,
    // a borda da imagem (degradê escuro), não o branco. Média de uma janela
    // (o centro exato da camada é uma linha da grade escura).
    auto patch = [](const FloatImage& img) {
        f32 sum = 0;
        for (u32 y = 54; y < 75; ++y) for (u32 x = 54; x < 75; ++x) sum += cc_luma(img.v(x, y));
        return sum / (21.0f * 21.0f);
    };
    const f32 lit = patch(front);
    fx.params[3].constant.v[0] = 180.0f;
    const auto back = s.render();
    std::printf("    luz no centro: frente %.3f, girada %.3f\n", lit, patch(back));
    AUREA_CHECK(lit > patch(back) + 0.2f);
    // Por dentro: só a parede de dentro do fundo.
    fx.params[3].constant.v[0] = 0.0f;
    fx.params[5].constant.v[0] = 2.0f;
    AUREA_CHECK(cc_diff(front, s.render()) > 0.1f);
    AUREA_CHECK(cc_finite(back));
}

AUREA_TEST(CCEffectsGpu, CylinderWrapsTheWidthOnceAroundAnUprightTube) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    // Largura 96 → raio 96/2π ≈ 15,3 px; altura 96 (y de 16 a 112).
    const auto layer = s.image(reference_image(96, 96), 64, 64);
    auto& fx = s.add_effect(layer, effect_keys::kCylinderWrap);
    const auto upright = s.render();
    AUREA_CHECK(upright.v(64, 64).w > 0.99f);
    AUREA_CHECK(upright.v(64, 20).w > 0.99f);
    AUREA_CHECK(upright.v(64 + 22, 64).w < 0.01f);
    AUREA_CHECK(upright.v(64 - 22, 64).w < 0.01f);
    AUREA_CHECK(upright.v(64, 120).w < 0.01f);
    // Inclinado em X: a boca do tubo aparece e o quadro muda.
    fx.params[2].constant.v[0] = 60.0f;
    const auto tilted = s.render();
    AUREA_CHECK(cc_diff(upright, tilted) > 0.2f);
    AUREA_CHECK(cc_finite(tilted));
}

AUREA_TEST(CCEffectsGpu, TwisterFlipsTheFarSideAndKeepsTheNearSide) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(96, 96), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kTwister);
    fx.params[0].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
    // 180° em volta do eixo vertical; tamanho 60% de 96 → metade 28,8 px.
    fx.params[0].constant.v[0] = 180.0f;
    const auto img = s.render();
    f32 flipped = 0, kept = 0;
    for (u32 x = 20; x < 108; ++x) {
        for (u32 y = 98; y < 110; ++y) flipped = std::max(flipped, cc_pixel_diff(img.v(x, y), original.v(127 - x, y)));
        for (u32 y = 18; y < 30; ++y) kept = std::max(kept, cc_pixel_diff(img.v(x, y), original.v(x, y)));
    }
    std::printf("    embaixo espelhado: %.4f, em cima intacto: %.4f\n", flipped, kept);
    AUREA_CHECK(flipped < 0.03f);
    AUREA_CHECK(kept < 0.01f);
}

AUREA_TEST(CCEffectsGpu, FlowPinchKeepsTheKnotAndTileEdgesFillTheBorder) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kFlowPinch);
    // Só o nó 1, no centro do pixel (38, 70): ele lê ele mesmo e o resto muda.
    fx.params[0].constant = ParamValue::vec2(38.5f / 128.0f, 70.5f / 128.0f);
    fx.params[1].constant.v[0] = 300.0f;   // força 1
    fx.params[3].constant.v[0] = 0.0f;     // força 2
    const auto single = s.render();
    AUREA_CHECK(cc_diff(original, single) > 0.2f);
    AUREA_CHECK(cc_pixel_diff(single.v(38, 70), original.v(38, 70)) < 0.02f);
    // Os dois nós puxando: o que sai da camada vira transparente.
    fx.params[3].constant.v[0] = 300.0f;
    const auto pinched = s.render();
    fx.params[5].constant.v[0] = 1.0f;     // repetir bordas
    const auto tiled = s.render();
    std::printf("    alfa: sem repetir %.0f, repetindo %.0f\n", cc_alpha_sum(pinched), cc_alpha_sum(tiled));
    AUREA_CHECK(cc_alpha_sum(tiled) >= cc_alpha_sum(pinched));
    fx.params[6].constant.v[0] = 2.0f;     // antisserrilhado alto
    AUREA_CHECK(cc_finite(s.render()));
    fx.params[1].constant.v[0] = 0.0f;
    fx.params[3].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, GriddlerCutTilesLeaveGapsAndNeutralIsIdentity) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kGriddler);
    fx.params[0].constant.v[0] = 100.0f;
    fx.params[1].constant.v[0] = 100.0f;
    fx.params[3].constant.v[0] = 0.0f;
    fx.params[4].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
    // Cartões com metade do tamanho: ~3/4 do quadro vira vão.
    fx.params[0].constant.v[0] = 50.0f;
    fx.params[1].constant.v[0] = 50.0f;
    fx.params[4].constant.v[0] = 1.0f;
    const auto cards = s.render();
    const f32 coverage = cc_alpha_sum(cards) / (128.0f * 128.0f);
    std::printf("    cobertura dos cartões: %.3f\n", coverage);
    AUREA_CHECK(coverage > 0.15f && coverage < 0.4f);
    fx.params[3].constant.v[0] = 30.0f;
    AUREA_CHECK(cc_diff(cards, s.render()) > 0.1f);
}

AUREA_TEST(CCEffectsGpu, RepeatTileUnfoldsAndRepeatsAroundTheLayer) {
    AUREA_REQUIRE_GPU();
    Scene s(192, 192);
    s.comp->set_transparent_background(true);
    // Camada 64×64 em [64, 128) × [64, 128).
    const auto layer = s.image(reference_image(64, 64), 96, 96);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kRepeatTile);
    const auto unfold = s.render();
    f32 inside = 0, mirror = 0;
    for (u32 y = 66; y < 126; ++y) {
        for (u32 x = 66; x < 126; ++x) inside = std::max(inside, cc_pixel_diff(unfold.v(x, y), original.v(x, y)));
        for (u32 k = 0; k < 30; ++k) mirror = std::max(mirror, cc_pixel_diff(unfold.v(128 + k, y), original.v(127 - k, y)));
    }
    std::printf("    camada intacta %.4f, ladrilho espelhado %.4f\n", inside, mirror);
    AUREA_CHECK(inside < 0.01f);
    AUREA_CHECK(mirror < 0.02f);
    AUREA_CHECK(unfold.v(10, 10).w > 0.99f);
    // Repetir: o ladrilho da direita é a camada de novo.
    fx.params[4].constant.v[0] = 0.0f;
    const auto repeat = s.render();
    f32 again = 0;
    for (u32 y = 66; y < 126; ++y) for (u32 k = 2; k < 30; ++k) again = std::max(again, cc_pixel_diff(repeat.v(128 + k, y), original.v(64 + k, y)));
    AUREA_CHECK(again < 0.02f);
    // Sem expansão e sem deslocamento: identidade.
    for (u32 i = 0; i < 4; ++i) fx.params[i].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, SmearDragsTheOriginColorToTheDestination) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kSmear);
    // De (25,6; 108,8) até (64; 108,8): fora do disco branco do meio.
    fx.params[0].constant = ParamValue::vec2(0.2f, 0.85f);
    fx.params[1].constant = ParamValue::vec2(0.5f, 0.85f);
    const auto img = s.render();
    const f32 dragged = cc_pixel_diff(img.v(64, 108), original.v(26, 108));
    std::printf("    destino x origem: %.4f; destino x antes: %.4f\n", dragged, cc_pixel_diff(img.v(64, 108), original.v(64, 108)));
    AUREA_CHECK(dragged < 0.06f);
    AUREA_CHECK(cc_pixel_diff(img.v(64, 108), original.v(64, 108)) > 0.2f);
    // Longe do caminho nada muda.
    AUREA_CHECK(cc_pixel_diff(img.v(100, 20), original.v(100, 20)) < 0.01f);
    fx.params[2].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, SplitOpensATransparentGapClosedAtTheEnds) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kSplit);
    const auto img = s.render();
    // Corte de (38,4; 64) a (89,6; 64): abertura de ~5 px de cada lado no meio.
    AUREA_CHECK(img.v(64, 64).w < 0.05f);
    AUREA_CHECK(img.v(64, 62).w < 0.05f);
    AUREA_CHECK(img.v(64, 76).w > 0.99f);
    // Antes de A e depois de B: intacto.
    AUREA_CHECK(cc_pixel_diff(img.v(20, 64), original.v(20, 64)) < 0.01f);
    AUREA_CHECK(cc_pixel_diff(img.v(110, 64), original.v(110, 64)) < 0.01f);
    fx.params[2].constant.v[0] = 0.0f;
    fx.params[3].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, SlantShearsFromTheFloorAndSetColorPaintsTheShape) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    // Camada 64×64 em [96, 160) × [32, 96); chão na borda de baixo (y = 96).
    const auto layer = s.image(reference_image(64, 64), 128, 64);
    auto& fx = s.add_effect(layer, effect_keys::kSlant);
    fx.params[0].constant.v[0] = 45.0f;
    const auto img = s.render();
    // No alto (2 px abaixo do topo) a camada andou ~62 px para a direita.
    AUREA_CHECK(img.v(100, 34).w < 0.01f);
    AUREA_CHECK(img.v(200, 34).w > 0.99f);
    // Rente ao chão quase não anda.
    AUREA_CHECK(img.v(98, 95).w > 0.99f);
    fx.params[3].constant.v[0] = 1.0f;     // usar cor
    fx.params[4].constant = ParamValue::color(1, 0, 0, 1);
    const auto red = s.render();
    const Vec4 p = red.v(200, 34);
    AUREA_CHECK(p.x > 0.95f && p.y < 0.02f && p.z < 0.02f);
}

AUREA_TEST(CCEffectsGpu, TilerAndHexTileRepeatPeriodically) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& tiler = s.add_effect(layer, effect_keys::kTiler);
    tiler.params[0].constant.v[0] = 50.0f;
    const auto quads = s.render();
    f32 period = 0;
    for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 64; ++x) period = std::max(period, cc_pixel_diff(quads.v(x, y), quads.v(x + 64, y)));
    std::printf("    mosaico: maior diferença entre cópias %.4f\n", period);
    AUREA_CHECK(period < 0.02f);
    tiler.params[2].constant.v[0] = 100.0f;   // mistura total com o original
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
    s.comp->layer(layer)->effects.clear();
    auto& hex = s.add_effect(layer, effect_keys::kHexTile);
    // Raio com √3·R = 48 px: a colmeia repete a cada 48 px na horizontal.
    hex.params[2].constant.v[0] = 48.0f / 1.7320508f / 128.0f * 100.0f;
    const auto comb = s.render();
    u32 off = 0, total = 0;
    for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x + 48 < 128; ++x, ++total) off += cc_pixel_diff(comb.v(x, y), comb.v(x + 48, y)) > 0.03f;
    std::printf("    colmeia: %u de %u pixels fora do período\n", off, total);
    AUREA_CHECK(off < total / 50);
    AUREA_CHECK(cc_diff(original, comb) > 0.1f);
}

AUREA_TEST(CCEffectsGpu, VectorBlurLeavesFlatAreasAndSmearsEdges) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto flat = s.image(uniform_image(128, 128, 90, 160, 200), 64, 64);
    const auto plain = s.render();
    s.add_effect(flat, effect_keys::kVectorBlur);
    const auto blurred = s.render();
    f32 interior = 0;
    for (u32 y = 30; y < 98; ++y) for (u32 x = 30; x < 98; ++x) interior = std::max(interior, cc_pixel_diff(blurred.v(x, y), plain.v(x, y)));
    AUREA_CHECK(interior < 0.01f);
    Scene t(128, 128);
    t.comp->set_transparent_background(true);
    const auto layer = t.image(reference_image(128, 128), 64, 64);
    const auto original = t.render();
    auto& fx = t.add_effect(layer, effect_keys::kVectorBlur);
    AUREA_CHECK(cc_diff(original, t.render()) > 0.05f);
    fx.params[1].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, t.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, LightBurstBrightensAndThrowsRaysOutsideTheLayer) {
    AUREA_REQUIRE_GPU();
    Scene s(160, 160);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(80, 80), 80, 80);   // [40, 120)²
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kLightBurst);
    const auto img = s.render();
    f32 before = 0, after = 0;
    for (u32 y = 0; y < 160; ++y) for (u32 x = 0; x < 160; ++x) { before += cc_luma(original.v(x, y)); after += cc_luma(img.v(x, y)); }
    std::printf("    luz total: %.1f → %.1f; alfa fora da camada %.3f\n", before, after, img.v(140, 80).w);
    AUREA_CHECK(after > before * 1.2f);
    AUREA_CHECK(img.v(140, 80).w > 0.05f);
    AUREA_CHECK(original.v(140, 80).w < 0.01f);
    fx.params[1].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, StarFieldTurnsTheLayerIntoDotsThatMoveWithTime) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kStarField);
    const auto a = s.render(FrameIndex{0});
    const auto b = s.render(FrameIndex{12});
    AUREA_CHECK(cc_diff(a, b) > 0.1f);
    u32 empty = 0;
    for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x < 128; ++x) empty += a.v(x, y).w < 0.02f;
    std::printf("    pixels vazios entre os pontos: %u de %u\n", empty, 128u * 128u);
    AUREA_CHECK(empty > 128u * 128u / 10u);
    fx.params[5].constant.v[0] = 100.0f;   // mistura total com o original
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, ThresholdRgbIsBinaryPerChannelAndInvertFlips) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    auto& fx = s.add_effect(layer, effect_keys::kThresholdRgb);
    const auto img = s.render();
    u32 notBinary = 0, ones = 0;
    for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x < 128; ++x) {
        const Vec4 p = img.v(x, y);
        for (const f32 c : {p.x, p.y, p.z}) { notBinary += c > 0.01f && c < 0.99f; ones += c > 0.99f; }
    }
    AUREA_CHECK_EQ(notBinary, 0u);
    AUREA_CHECK(ones > 1000u);
    fx.params[3].constant.v[0] = 1.0f;   // inverter vermelho
    const auto inv = s.render();
    f32 worst = 0;
    for (u32 y = 0; y < 128; ++y) for (u32 x = 0; x < 128; ++x) worst = std::max(worst, std::fabs(inv.v(x, y).x - (1.0f - img.v(x, y).x)));
    AUREA_CHECK(worst < 0.01f);
}

AUREA_TEST(CCEffectsGpu, ColorOffsetZeroPhaseKeepsColorsAndHalfTurnMovesGreen) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 64);
    s.comp->set_transparent_background(true);
    // Verde sRGB 64/255 ≈ 0,25: meia volta leva a ~0,75.
    const auto layer = s.image(uniform_image(64, 64, 200, 64, 30), 32, 32);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kColorOffset);
    for (u32 mode = 0; mode < 2; ++mode) {
        fx.params[0].constant.v[0] = 0.0f;
        fx.params[1].constant.v[0] = 0.0f;
        fx.params[2].constant.v[0] = 0.0f;
        fx.params[3].constant.v[0] = static_cast<f32>(mode);
        AUREA_CHECK(cc_diff(original, s.render()) < 0.005f);
    }
    fx.params[3].constant.v[0] = 0.0f;
    fx.params[1].constant.v[0] = 180.0f;
    const Vec4 p = s.render().v(32, 32);
    const f32 g = Color::linear_to_srgb(p.y);
    std::printf("    verde depois de meia volta: %.3f\n", g);
    AUREA_CHECK(std::fabs(g - (64.0f / 255.0f + 0.5f)) < 0.02f);
    AUREA_CHECK(std::fabs(p.x - original.v(32, 32).x) < 0.005f);
}

AUREA_TEST(CCEffectsGpu, ScatterIsStableAcrossFramesAndFollowsTheSeed) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kScatter);
    const auto a = s.render(FrameIndex{0});
    AUREA_CHECK(cc_diff(original, a) > 0.1f);
    AUREA_CHECK_NEAR(cc_diff(a, s.render(FrameIndex{9})), 0.0f, 1e-5f);
    fx.params[4].constant.v[0] = 7.0f;
    AUREA_CHECK(cc_diff(a, s.render()) > 0.1f);
    fx.params[1].constant.v[0] = 90.0f;   // torção à direita
    AUREA_CHECK(cc_finite(s.render()));
    fx.params[0].constant.v[0] = 0.0f;
    fx.params[1].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, BlobbyCutAwayRemovesMoreAsItRises) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    auto& fx = s.add_effect(layer, effect_keys::kBlobby);
    f32 last = 1e30f;
    for (const f32 cut : {0.0f, 30.0f, 60.0f, 95.0f}) {
        fx.params[2].constant.v[0] = cut;
        const auto img = s.render();
        const f32 alpha = cc_alpha_sum(img);
        std::printf("    corte %.0f%%: alfa %.0f\n", static_cast<f64>(cut), static_cast<f64>(alpha));
        AUREA_CHECK(cc_finite(img));
        AUREA_CHECK(alpha <= last + 1.0f);
        last = alpha;
    }
    AUREA_CHECK(last < 0.2f * 128.0f * 128.0f);
}

AUREA_TEST(CCEffectsGpu, GlassRefractsByTheDisplacement) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kGlass);
    const auto glass = s.render();
    AUREA_CHECK(cc_diff(original, glass) > 0.05f);
    fx.params[3].constant.v[0] = 0.0f;   // sem deslocamento: só a luz muda
    AUREA_CHECK(cc_diff(glass, s.render()) > 0.02f);
    AUREA_CHECK(cc_finite(glass));
}

AUREA_TEST(CCEffectsGpu, KernelIdentityKeepsTheLayerAndSharpenKeepsFlatAreas) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto flat = s.image(uniform_image(128, 128, 120, 60, 200), 64, 64);
    const auto plain = s.render();
    auto& fx = s.add_effect(flat, effect_keys::kKernel);
    const auto sharp = s.render();
    f32 interior = 0;
    for (u32 y = 8; y < 120; ++y) for (u32 x = 8; x < 120; ++x) interior = std::max(interior, cc_pixel_diff(sharp.v(x, y), plain.v(x, y)));
    AUREA_CHECK(interior < 0.01f);
    for (u32 i = 0; i < 9; ++i) fx.params[i].constant.v[0] = i == 4 ? 1.0f : 0.0f;
    AUREA_CHECK_NEAR(cc_diff(plain, s.render()), 0.0f, 0.0f);
    // Relevo (emboss): muda a camada.
    for (u32 i = 0; i < 9; ++i) fx.params[i].constant.v[0] = 0.0f;
    fx.params[0].constant.v[0] = -2.0f; fx.params[4].constant.v[0] = 1.0f; fx.params[8].constant.v[0] = 2.0f;
    Scene t(128, 128);
    const auto layer = t.image(reference_image(128, 128), 64, 64);
    const auto original = t.render();
    auto& emboss = t.add_effect(layer, effect_keys::kKernel);
    emboss.params = fx.params;
    AUREA_CHECK(cc_diff(original, t.render()) > 0.1f);
}

AUREA_TEST(CCEffectsGpu, SpotlightLightsTheTargetAndDarkensOutsideTheCone) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(uniform_image(128, 128, 180, 180, 180), 64, 64);
    auto& fx = s.add_effect(layer, effect_keys::kSpotlight);
    const auto lit = s.render();
    // Alvo em (64; 70,4); canto de baixo à direita fora do cone.
    std::printf("    alvo %.3f, canto %.3f\n", cc_luma(lit.v(64, 70)), cc_luma(lit.v(124, 124)));
    AUREA_CHECK(cc_luma(lit.v(64, 70)) > 3.0f * cc_luma(lit.v(124, 124)));
    fx.params[8].constant.v[0] = 1.0f;   // só a luz
    const auto only = s.render();
    AUREA_CHECK(only.v(64, 70).w > 0.5f);
    AUREA_CHECK(only.v(124, 124).w < 0.05f);
}

AUREA_TEST(CCEffectsGpu, DrizzleRipplesChangeWithTimeAndRepeatAtTheSameFrame) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(128, 128), 64, 64);
    const auto original = s.render();
    auto& fx = s.add_effect(layer, effect_keys::kDrizzle);
    fx.params[3].constant.v[0] = 40.0f;    // raio da onda
    fx.params[4].constant.v[0] = 10.0f;    // deslocamento
    const auto a = s.render(FrameIndex{4});
    AUREA_CHECK(cc_diff(original, a) > 0.05f);
    AUREA_CHECK_NEAR(cc_diff(a, s.render(FrameIndex{4})), 0.0f, 1e-5f);
    AUREA_CHECK(cc_diff(a, s.render(FrameIndex{19})) > 0.05f);
    fx.params[4].constant.v[0] = 0.0f;
    fx.params[5].constant.v[0] = 0.0f;
    AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
}

AUREA_TEST(CCEffectsGpu, TransitionsAreExactAtZeroAndEmptyAtOneHundred) {
    AUREA_REQUIRE_GPU();
    // Jaws, Radial Scale Wipe, Burn Film e Block Load: parâmetro 0 é a conclusão.
    for (const char* key : {effect_keys::kJaws, effect_keys::kRadialScaleWipe, effect_keys::kBurnFilm, effect_keys::kBlockLoad}) {
        Scene s(128, 128);
        s.comp->set_transparent_background(true);
        const auto layer = s.image(reference_image(96, 96), 64, 64);
        const auto original = s.render();
        auto& fx = s.add_effect(layer, key);
        fx.params[0].constant.v[0] = 0.0f;
        AUREA_CHECK_NEAR(cc_diff(original, s.render()), 0.0f, 0.0f);
        fx.params[0].constant.v[0] = 50.0f;
        const auto half = s.render();
        AUREA_CHECK_MSG(cc_diff(original, half) > 0.1f, key);
        AUREA_CHECK_MSG(cc_finite(half), key);
        fx.params[0].constant.v[0] = 100.0f;
        const f32 left = cc_alpha_sum(s.render());
        if (left > 1.0f) std::printf("\n    %s: alfa em 100%%: %.2f", key, static_cast<f64>(left));
        AUREA_CHECK_MSG(left < 1.0f, key);
        // Logo no começo (10%) a transição já aparece; perto do fim (90%) ainda sobra camada.
        fx.params[0].constant.v[0] = 10.0f;
        const f32 early = cc_diff(original, s.render());
        fx.params[0].constant.v[0] = 90.0f;
        const f32 late = cc_alpha_sum(s.render());
        std::printf("    %s: 10%% muda %.3f; 90%% ainda tem alfa %.1f\n", key, static_cast<f64>(early), static_cast<f64>(late));
        AUREA_CHECK_MSG(early > 0.02f, key);
        AUREA_CHECK_MSG(late > 1.0f, key);
    }
}

AUREA_TEST(CCEffectsGpu, TransitionShapesBehaveAsDescribed) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    s.comp->set_transparent_background(true);
    const auto layer = s.image(reference_image(96, 96), 64, 64);
    const auto original = s.render();
    // Mandíbulas em 50%: o meio abriu; a borda de cima/baixo ainda tem camada.
    auto& jaws = s.add_effect(layer, effect_keys::kJaws);
    const auto open = s.render();
    AUREA_CHECK(open.v(64, 64).w < 0.01f);
    AUREA_CHECK(open.v(64, 20).w > 0.99f || open.v(64, 108).w > 0.99f);
    jaws.params[5].constant.v[0] = 1.0f;   // robô
    AUREA_CHECK(cc_diff(open, s.render()) > 0.1f);
    s.comp->layer(layer)->effects.clear();
    // Scale Wipe em 100%: à direita do centro, cada linha repete o pixel do centro.
    auto& wipe = s.add_effect(layer, effect_keys::kScaleWipe);
    wipe.params[0].constant.v[0] = 100.0f;
    const auto streak = s.render();
    f32 worst = 0;
    for (u32 y = 20; y < 108; ++y) for (u32 x = 70; x < 108; ++x) worst = std::max(worst, cc_pixel_diff(streak.v(x, y), streak.v(70, y)));
    AUREA_CHECK(worst < 0.02f);
    AUREA_CHECK(cc_pixel_diff(streak.v(30, 40), original.v(30, 40)) < 0.01f);
    s.comp->layer(layer)->effects.clear();
    // Radial: furo no meio; invertido, o meio fica e a borda some.
    auto& radial = s.add_effect(layer, effect_keys::kRadialScaleWipe);
    const auto hole = s.render();
    AUREA_CHECK(hole.v(64, 64).w < 0.01f);
    AUREA_CHECK(hole.v(22, 64).w > 0.99f);
    radial.params[2].constant.v[0] = 1.0f;
    const auto shrink = s.render();
    AUREA_CHECK(shrink.v(64, 64).w > 0.99f);
    AUREA_CHECK(shrink.v(18, 18).w < 0.01f);
    s.comp->layer(layer)->effects.clear();
    // Filme queimando: o centro queima primeiro; outra semente, outra borda.
    auto& burn = s.add_effect(layer, effect_keys::kBurnFilm);
    burn.params[0].constant.v[0] = 30.0f;
    const auto burnt = s.render();
    AUREA_CHECK(burnt.v(64, 64).w < 0.01f);
    AUREA_CHECK(burnt.v(20, 20).w > 0.99f);
    burn.params[2].constant.v[0] = 5.0f;
    AUREA_CHECK(cc_diff(burnt, s.render()) > 0.1f);
    s.comp->layer(layer)->effects.clear();
    // Carga em blocos em 50%: blocos (vizinhos iguais) em vez de pixels.
    auto& blocks = s.add_effect(layer, effect_keys::kBlockLoad);
    blocks.params[1].constant.v[0] = 0.0f;   // sem varredura: a etapa inteira
    blocks.params[0].constant.v[0] = 40.0f;
    const auto coarse = s.render();
    u32 same = 0, sameBefore = 0;
    for (u32 y = 20; y < 108; ++y) for (u32 x = 20; x < 107; ++x) {
        same += cc_pixel_diff(coarse.v(x, y), coarse.v(x + 1, y)) < 1e-4f;
        sameBefore += cc_pixel_diff(original.v(x, y), original.v(x + 1, y)) < 1e-4f;
    }
    std::printf("    vizinhos iguais: antes %u, em blocos %u\n", sameBefore, same);
    AUREA_CHECK(same > sameBefore + 2000u);
}
