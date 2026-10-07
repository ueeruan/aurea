// Desintegrar e Bolas: identidade em 0 / mistura 0, mudança visível nos
// valores de demonstração, determinismo e pedaços FORA da caixa da camada.
namespace {
f32 disintegrate_difference(const FloatImage& a, const FloatImage& b) {
    f32 largest = 0;
    AUREA_CHECK_EQ(a.px.size(), b.px.size());
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i)
        largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}
/// Pixels com alfa fora do retângulo [x0, x1) × [y0, y1) (com folga de 1 px).
u32 disintegrate_outside(const FloatImage& img, u32 x0, u32 y0, u32 x1, u32 y1) {
    u32 n = 0;
    for (u32 y = 0; y < img.height; ++y) for (u32 x = 0; x < img.width; ++x) {
        if (x + 1 >= x0 && x <= x1 && y + 1 >= y0 && y <= y1) continue;
        n += img.v(x, y).w > .02f;
    }
    return n;
}
}

AUREA_TEST(DisintegrateGpu, ZeroAndMixZeroAreIdentityAndFullCompletionIsGone) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 128);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(reference_image(96, 64), 96, 64);
    const auto original = scene.render();
    auto& fx = scene.add_effect(layer, "aurea.transition.disintegrate");
    AUREA_CHECK_NEAR(disintegrate_difference(original, scene.render()), 0, 0);
    fx.params[0].constant.v[0] = 50;
    fx.params[12].constant.v[0] = 0;   // mistura 0
    AUREA_CHECK_NEAR(disintegrate_difference(original, scene.render()), 0, 0);
    fx.params[12].constant.v[0] = 100;
    fx.params[0].constant.v[0] = 100;
    const auto gone = scene.render();
    f32 alpha = 0;
    for (usize i = 3; i < gone.px.size(); i += 4) alpha = std::max(alpha, gone.px[i]);
    AUREA_CHECK_NEAR(alpha, 0, .001f);
}

AUREA_TEST(DisintegrateGpu, HalfwayFragmentsLeaveTheLayerBoundsDeterministically) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 128);
    scene.comp->set_transparent_background(true);
    // Camada 96×64 no centro: caixa [48, 144) × [32, 96).
    const auto layer = scene.image(reference_image(96, 64), 96, 64);
    const auto original = scene.render();
    AUREA_CHECK_EQ(disintegrate_outside(original, 48, 32, 144, 96), 0u);
    auto& fx = scene.add_effect(layer, "aurea.transition.disintegrate");
    fx.params[0].constant.v[0] = 45;   // conclusão
    fx.params[2].constant.v[0] = 4;    // tamanho
    fx.params[5].constant.v[0] = 120;  // velocidade
    fx.params[11].constant.v[0] = 60;  // brilho da borda
    const auto a = scene.render(FrameIndex{3});
    const auto b = scene.render(FrameIndex{9});
    AUREA_CHECK_NEAR(disintegrate_difference(a, b), 0, 1e-5f);
    AUREA_CHECK(disintegrate_difference(original, a) > .2f);
    const u32 outside = disintegrate_outside(a, 48, 32, 144, 96);
    std::printf("    fragmentos fora da caixa: %u\n", outside);
    AUREA_CHECK(outside > 150u);
    // Parte já solta: o lado esquerdo (direção 0° = para a direita) perdeu alfa.
    f32 leftAlpha = 0, rightAlpha = 0;
    for (u32 y = 34; y < 94; ++y) {
        leftAlpha += a.v(52, y).w;
        rightAlpha += a.v(140, y).w;
    }
    AUREA_CHECK(leftAlpha < rightAlpha);
    // Semente diferente: outro sorteio.
    fx.params[9].constant.v[0] = 7;
    AUREA_CHECK(disintegrate_difference(a, scene.render()) > .05f);
    for (const f32 value : a.px) AUREA_CHECK(std::isfinite(value));
}

AUREA_TEST(BallGridGpu, MixZeroIsIdentityScatterLeavesBoundsAndLightMoves) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 128);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(reference_image(96, 64), 96, 64);
    const auto original = scene.render();
    auto& fx = scene.add_effect(layer, effect_keys::kBallGrid);
    fx.params[5].constant.v[0] = 8;    // espaçamento
    const auto balls = scene.render();
    AUREA_CHECK(disintegrate_difference(original, balls) > .1f);
    AUREA_CHECK_EQ(disintegrate_outside(balls, 48, 32, 144, 96), 0u);
    fx.params[10].constant.v[0] = 0;   // mistura 0
    AUREA_CHECK_NEAR(disintegrate_difference(original, scene.render()), 0, 0);
    fx.params[10].constant.v[0] = 50;
    const auto half = scene.render();
    AUREA_CHECK(disintegrate_difference(half, original) > .02f);
    AUREA_CHECK(disintegrate_difference(half, balls) > .02f);
    fx.params[10].constant.v[0] = 100;
    // Luz do lado oposto: o sombreado muda.
    fx.params[9].constant.v[0] = 45;
    AUREA_CHECK(disintegrate_difference(balls, scene.render()) > .05f);
    fx.params[9].constant.v[0] = 225;
    AUREA_CHECK_NEAR(disintegrate_difference(balls, scene.render()), 0, 1e-5f);
    // Dispersão + rotação: bolas fora da caixa da camada, sem cortar.
    fx.params[0].constant.v[0] = 40;   // dispersão
    fx.params[1].constant.v[0] = 3;    // eixo XY
    fx.params[2].constant.v[0] = 35;   // rotação
    const auto cloud = scene.render(FrameIndex{2});
    const u32 outside = disintegrate_outside(cloud, 48, 32, 144, 96);
    std::printf("    bolas fora da caixa: %u\n", outside);
    AUREA_CHECK(outside > 150u);
    AUREA_CHECK_NEAR(disintegrate_difference(cloud, scene.render(FrameIndex{6})), 0, 1e-5f);
}
