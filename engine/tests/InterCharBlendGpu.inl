// v72 · Mistura entre caracteres (Inter-Character Blending do After Effects, em
// "Mais opções"): onde as letras do MESMO texto se sobrepõem, cada uma entra
// sobre as anteriores com o modo escolhido. Incluído em test_gpu.cpp.

AUREA_TEST(InterCharBlendGpu, OverlappingLettersMixWithTheChosenModeAndNormalStaysIdentical) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    const auto id = rig.e.add_text("OOOO");
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    Layer* l = rig.e.project()->timeline().composition(rig.e.project()->timeline().current())->layer(LayerId::unpack(*id));
    AUREA_CHECK(l != nullptr); if (!l) return;
    l->text.color = Vec4{0.6f, 0.6f, 0.6f, 1.0f};   // antes do set_text_style (que marca o modelo como mudado)
    f32 style[22] = {};
    AUREA_CHECK(rig.e.query_text_style(*id, style, 22));
    style[19] = -450.0f;   // tracking bem negativo: cada O entra no anterior
    AUREA_CHECK(rig.e.set_text_style(*id, style, 22));
    const Image8 normal = rig.capture(320);
    auto set_blend = [&](BlendMode m) {
        style[21] = static_cast<f32>(m);
        AUREA_CHECK(rig.e.set_text_style(*id, style, 22));
    };
    auto compare = [&](const Image8& img, u32& darker, u32& brighter) {
        darker = brighter = 0;
        for (u32 y = 0; y < img.height && y < normal.height; ++y)
            for (u32 x = 0; x < img.width && x < normal.width; ++x) {
                const int a = img.at(x, y)[0], b = normal.at(x, y)[0];
                darker += a + 20 < b;
                brighter += a > b + 20;
            }
    };
    u32 darker = 0, brighter = 0, lit = 0;
    for (u32 y = 0; y < normal.height; ++y)
        for (u32 x = 0; x < normal.width; ++x) lit += normal.at(x, y)[0] > 100;
    AUREA_CHECK(lit > 100u);
    // Multiplicar: só a sobreposição escurece (0,6 · 0,6); nada clareia.
    set_blend(BlendMode::Multiply);
    const Image8 multiply = rig.capture(320);
    compare(multiply, darker, brighter);
    std::printf("    multiplicar: %u px mais escuros, %u mais claros (de %u acesos)\n", darker, brighter, lit);
    AUREA_CHECK(darker > 30u);
    AUREA_CHECK(brighter < 4u);
    AUREA_CHECK(darker < lit / 2);   // fora da sobreposição a letra fica igual
    // Tela: a sobreposição clareia.
    set_blend(BlendMode::Screen);
    const Image8 screen = rig.capture(320);
    compare(screen, darker, brighter);
    std::printf("    tela: %u px mais claros, %u mais escuros\n", brighter, darker);
    AUREA_CHECK(brighter > 30u);
    AUREA_CHECK(darker < 4u);
    // Diferença: 0,6 − 0,6 = preto onde se cruzam.
    set_blend(BlendMode::Difference);
    const Image8 difference = rig.capture(320);
    compare(difference, darker, brighter);
    std::printf("    diferença: %u px mais escuros\n", darker);
    AUREA_CHECK(darker > 30u);
    // Normal de volta: o caminho de um desenho só, pixel a pixel igual ao de antes.
    set_blend(BlendMode::Normal);
    AUREA_CHECK_EQ(max_diff(normal, rig.capture(320)), 0u);
}
