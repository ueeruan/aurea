// 3D Shatter e 3D Stroke (Object3DEffects.cpp): identidade, mudança visível,
// peças fora da caixa da camada, determinismo e os modos de composição.
namespace {
f32 object3d_difference(const FloatImage& a, const FloatImage& b) {
    f32 largest = 0;
    AUREA_CHECK_EQ(a.px.size(), b.px.size());
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i)
        largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}
u32 object3d_outside(const FloatImage& img, u32 x0, u32 y0, u32 x1, u32 y1) {
    u32 n = 0;
    for (u32 y = 0; y < img.height; ++y) for (u32 x = 0; x < img.width; ++x) {
        if (x + 1 >= x0 && x <= x1 && y + 1 >= y0 && y <= y1) continue;
        n += img.v(x, y).w > .02f;
    }
    return n;
}
}

AUREA_TEST(Shatter3DGpu, ZeroIsIdentityAndExplosionFliesOutsideDeterministically) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 128);
    scene.comp->set_transparent_background(true);
    // Camada 96×64 no centro: caixa [48, 144) × [32, 96).
    const auto layer = scene.image(reference_image(96, 64), 96, 64);
    const auto original = scene.render();
    auto& fx = scene.add_effect(layer, effect_keys::kShatter3D);
    AUREA_CHECK_NEAR(object3d_difference(original, scene.render()), 0, 0);
    fx.params[0].constant.v[0] = 45;    // explosão
    fx.params[5].constant.v[0] = 160;   // força
    const auto a = scene.render(FrameIndex{2});
    const auto b = scene.render(FrameIndex{8});
    AUREA_CHECK_NEAR(object3d_difference(a, b), 0, 1e-5f);
    AUREA_CHECK(object3d_difference(original, a) > .2f);
    const u32 outside = object3d_outside(a, 48, 32, 144, 96);
    std::printf("    peças fora da caixa: %u\n", outside);
    AUREA_CHECK(outside > 100u);
    for (const f32 value : a.px) AUREA_CHECK(std::isfinite(value));
    // Cada forma de peça (quadrados, triângulos, vidro) desenha algo diferente.
    fx.params[1].constant.v[0] = 0;
    const auto squares = scene.render();
    fx.params[1].constant.v[0] = 1;
    const auto triangles = scene.render();
    AUREA_CHECK(object3d_difference(squares, triangles) > .05f);
    // Outra semente: outro sorteio.
    fx.params[14].constant.v[0] = 7;
    AUREA_CHECK(object3d_difference(triangles, scene.render()) > .05f);
}

AUREA_TEST(Shatter3DGpu, TinyExplosionKeepsFarPiecesExactlyInPlace) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 128);
    scene.comp->set_transparent_background(true);
    const auto layer = scene.image(reference_image(96, 64), 96, 64);
    const auto original = scene.render();
    auto& fx = scene.add_effect(layer, effect_keys::kShatter3D);
    // Explosão mínima, onda 100%, sem aleatoriedade, centro no canto
    // esquerdo de cima: as peças longe do centro ainda não soltaram e saem
    // EXATAMENTE como a camada (nada de fresta entre peças paradas).
    fx.params[0].constant.v[0] = 2;
    fx.params[4].constant = ParamValue::vec2(0.0f, 0.0f);
    fx.params[9].constant.v[0] = 0;
    fx.params[10].constant.v[0] = 100;
    const auto tiny = scene.render();
    f32 far = 0;
    for (u32 y = 70; y < 96; ++y)
        for (u32 x = 110; x < 144; ++x) {
            const Vec4 a = tiny.v(x, y), b = original.v(x, y);
            far = std::max({far, std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z), std::fabs(a.w - b.w)});
        }
    std::printf("    canto parado: maior diferença %.6f\n", far);
    AUREA_CHECK(far < 2e-3f);
}

namespace {
/// Quantos dos 64 pontos do anel (raio 56 em volta de 128,128) têm traço.
u32 ring_hits(const FloatImage& img, f32 radius) {
    u32 hits = 0;
    for (u32 i = 0; i < 64; ++i) {
        const f32 a = 6.2831853f * static_cast<f32>(i) / 64.0f;
        const u32 x = static_cast<u32>(std::lround(128.0f + std::cos(a) * radius));
        const u32 y = static_cast<u32>(std::lround(128.0f + std::sin(a) * radius));
        const Vec4 p = img.v(x, y);
        if (p.w > .6f && p.y > .6f * p.w && p.x < .3f && p.z < .3f) ++hits;
    }
    return hits;
}
}

AUREA_TEST(Stroke3DGpu, CircleStrokeDrawsOverLayerOnlyStrokeAndRotationChangesIt) {
    AUREA_REQUIRE_GPU();
    Scene scene(256, 256);
    scene.comp->set_transparent_background(true);
    // Camada 160×160 com o centro no centro do quadro (128, 128).
    const auto layer = scene.image(reference_image(160, 160), 128, 128);
    const auto original = scene.render();
    auto& fx = scene.add_effect(layer, effect_keys::kStroke3D);
    fx.params[3].constant.v[0] = 10;   // espessura
    // Cor do início = cor do fim = verde puro: o anel fica verde.
    fx.params[1].constant = ParamValue::color(0, 1, 0, 1);
    fx.params[2].constant = ParamValue::color(0, 1, 0, 1);
    const auto over = scene.render();
    AUREA_CHECK(object3d_difference(original, over) > .3f);
    // Círculo de raio 0,35 × 160 = 56 px em volta do centro (128, 128).
    const u32 green = ring_hits(over, 56.0f);
    std::printf("    pontos verdes no anel: %u/64\n", green);
    AUREA_CHECK(green > 48u);
    // O centro da camada continua a imagem.
    const Vec4 c0 = original.v(128, 128), c1 = over.v(128, 128);
    AUREA_CHECK(std::fabs(c0.x - c1.x) + std::fabs(c0.y - c1.y) + std::fabs(c0.z - c1.z) < .01f);
    // Só o traço: o centro fica transparente e o anel continua.
    fx.params[20].constant.v[0] = 1;
    const auto only = scene.render();
    AUREA_CHECK(only.v(128, 128).w < .02f);
    AUREA_CHECK(ring_hits(only, 56.0f) > 48u);
    // Girar 70° em X achata o anel na vertical: o topo do círculo some.
    fx.params[9].constant.v[0] = 70;
    const auto tilted = scene.render();
    AUREA_CHECK(object3d_difference(only, tilted) > .3f);
    AUREA_CHECK(tilted.v(128, 72).w < .05f);
    AUREA_CHECK(tilted.v(184, 128).w > .5f);   // as laterais ficam
    // Fim 50%: só metade do anel.
    fx.params[9].constant.v[0] = 0;
    fx.params[5].constant.v[0] = 50;
    const u32 halfGreen = ring_hits(scene.render(), 56.0f);
    std::printf("    metade do anel: %u/64\n", halfGreen);
    AUREA_CHECK(halfGreen > 22u && halfGreen < 42u);
    for (const f32 value : tilted.px) AUREA_CHECK(std::isfinite(value));
}
