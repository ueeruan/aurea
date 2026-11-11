// =============================================================================
//  0.5.2: preenchimento com TEXTURA do texto e o efeito Textura, na GPU.
//  Cada textura desenha, fica dentro das letras e difere da cor lisa; a
//  textura acompanha o animador; prévia = export; a imagem da galeria entra
//  no texto; o efeito sobrepõe cada textura e respeita "só dentro da camada".
//  Incluído no fim de test_gpu.cpp.
// =============================================================================

namespace texture_test {

LayerId add_text(Scene& s, const char* content, f32 size, Vec3 at) {
    const LayerId id = s.comp->add_layer(LayerKind::Text, "textura");
    Layer* l = s.comp->layer(id);
    l->text.content = content;
    l->text.size = size;
    l->text.color = Vec4{1, 1, 1, 1};
    l->transform.position = at;
    return id;
}

f32 luma(const FloatImage& img, u32 x, u32 y) {
    const f32* p = img.at(x, y);
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

struct Stats { f32 meanDiff = 0; f32 meanLuma = 0; bool finite = true; u32 spill = 0; u32 mask = 0; };

/// `flat` = o texto liso (branco no preto). Máscara = onde o liso é letra.
Stats compare(const FloatImage& flat, const FloatImage& tex) {
    Stats s;
    f64 diff = 0, lum = 0;
    for (u32 y = 0; y < flat.height; ++y) for (u32 x = 0; x < flat.width; ++x) {
        const f32* a = flat.at(x, y);
        const f32* b = tex.at(x, y);
        for (int c = 0; c < 4; ++c) s.finite &= std::isfinite(b[c]);
        const f32 l = luma(flat, x, y);
        if (l > 0.5f) {
            ++s.mask;
            diff += std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
            lum += luma(tex, x, y);
        }
        // Onde o liso é fundo puro, a textura também (nada vaza da letra).
        if (a[0] + a[1] + a[2] == 0.0f && b[0] + b[1] + b[2] > 1e-4f) ++s.spill;
    }
    if (s.mask) { s.meanDiff = static_cast<f32>(diff / s.mask); s.meanLuma = static_cast<f32>(lum / s.mask); }
    return s;
}

f32 max_abs(const FloatImage& a, const FloatImage& b) {
    f32 m = 0;
    for (usize i = 0; i < a.px.size() && i < b.px.size(); ++i) m = std::max(m, std::fabs(a.px[i] - b.px[i]));
    return m;
}

} // namespace texture_test

AUREA_TEST(TextTextureGpu, EveryPresetDrawsInsideTheLettersAndDiffersFromTheFlatFill) {
    AUREA_REQUIRE_GPU();
    using namespace texture_test;
    Scene s(320, 140);
    const LayerId id = add_text(s, "AUREA", 80, Vec3{12, 24, 0});
    const FloatImage flat = s.render();
    std::vector<FloatImage> shots;
    for (u8 kind = 1; kind <= kTextTextureProcedural; ++kind) {
        TextTexture& t = s.comp->layer(id)->text.texture;
        t = TextTexture{};
        t.kind = kind;
        const FloatImage img = s.render(FrameIndex{5});
        const Stats st = compare(flat, img);
        std::printf("    textura %2u: diferença %.3f, luz %.3f, vazamento %u\n", kind, st.meanDiff, st.meanLuma, st.spill);
        AUREA_CHECK(st.mask > 2000u);
        AUREA_CHECK_MSG(st.finite, "pixel nao finito");
        AUREA_CHECK_MSG(st.meanDiff > 0.05f, "textura igual a cor lisa");
        AUREA_CHECK_MSG(st.meanLuma > 0.01f, "textura apagou a letra");
        AUREA_CHECK_EQ(st.spill, 0u);
        shots.push_back(img);
    }
    // Dezoito texturas diferentes entre si.
    for (usize i = 0; i < shots.size(); ++i)
        for (usize j = i + 1; j < shots.size(); ++j) AUREA_CHECK(max_abs(shots[i], shots[j]) > 0.02f);
    // Cor lisa de novo: o mesmo pixel de antes (nenhum estado sobra).
    s.comp->layer(id)->text.texture = TextTexture{};
    AUREA_CHECK_EQ(max_abs(flat, s.render()), 0.0f);
}

AUREA_TEST(TextTextureGpu, TextureTravelsWithTheTextAnimatorAndMappingAndReliefChangeIt) {
    AUREA_REQUIRE_GPU();
    using namespace texture_test;
    Scene s(360, 140);
    const LayerId id = add_text(s, "MOVE", 80, Vec3{10, 24, 0});
    Layer* l = s.comp->layer(id);
    l->text.texture.kind = static_cast<u8>(ProceduralTexture::Wood);
    l->text.texture.relief = 0.0f;   // sem relevo: só a textura, sem derivada de tela
    TextAnimator animator;
    animator.props = kTextPropPosition;
    l->text.animators.push_back(animator);
    Track& px = l->tracks.get_or_create(TrackProperty::TextAnimParam, 0, text::kPosX);
    px.set(FrameIndex{0}, 0.0f);
    const FloatImage still = s.render();
    px.set(FrameIndex{0}, 40.0f);
    const FloatImage moved = s.render();
    // Cada letra leva o pedaço de textura dela: o quadro andado é o parado 40 px adiante.
    f32 worst = 0;
    u32 lit = 0;
    for (u32 y = 0; y < still.height; ++y) for (u32 x = 0; x + 40 < still.width; ++x) {
        const f32* a = still.at(x, y);
        const f32* b = moved.at(x + 40, y);
        for (int c = 0; c < 3; ++c) worst = std::max(worst, std::fabs(a[c] - b[c]));
        lit += luma(still, x, y) > 0.05f;
    }
    std::printf("    animador +40 px: maior diferença %.5f em %u pixels de letra\n", worst, lit);
    AUREA_CHECK(lit > 2000u);
    AUREA_CHECK(worst < 0.01f);
    AUREA_CHECK(max_abs(still, moved) > 0.1f);
    // Por letra e relevo mudam a imagem.
    l->text.texture.mapping = 1;
    const FloatImage perLetter = s.render();
    AUREA_CHECK(max_abs(moved, perLetter) > 0.02f);
    l->text.texture.relief = 1.0f;
    const FloatImage relief = s.render();
    AUREA_CHECK(max_abs(perLetter, relief) > 0.02f);
    // Girar e escalar a textura também.
    l->text.texture.scale = 3.0f;
    l->text.texture.rotation = 40.0f;
    AUREA_CHECK(max_abs(relief, s.render()) > 0.02f);
}

AUREA_TEST(TextTextureGpu, ExportMatchesPreviewAndOnlyAnimatedTexturesMoveInTime) {
    AUREA_REQUIRE_GPU();
    using namespace texture_test;
    Scene s(320, 140);
    const LayerId id = add_text(s, "FOGO", 80, Vec3{12, 24, 0});
    Layer* l = s.comp->layer(id);
    l->text.strokeWidth = 4.0f;
    l->text.strokeColor = Vec4{0, 0, 0, 1};
    for (const ProceduralTexture kind : {ProceduralTexture::Fire, ProceduralTexture::Gold, ProceduralTexture::Glitter,
                                         ProceduralTexture::Marble}) {
        l->text.texture = TextTexture{};
        l->text.texture.kind = static_cast<u8>(kind);
        l->text.texture.stroke = kind == ProceduralTexture::Gold;
        const FloatImage preview = s.render(FrameIndex{11});
        const FloatImage final = s.render(FrameIndex{11}, 1, true);
        const f32 d = max_abs(preview, final);
        std::printf("    textura %u: prévia x export %.5f\n", static_cast<u32>(kind), d);
        AUREA_CHECK(d < 0.015f);
        const f32 moved = max_abs(s.render(FrameIndex{0}), s.render(FrameIndex{20}));
        if (kind == ProceduralTexture::Fire || kind == ProceduralTexture::Glitter) AUREA_CHECK(moved > 0.02f);
        else AUREA_CHECK_EQ(moved, 0.0f);
        // Velocidade 0: o fogo para.
        if (kind == ProceduralTexture::Fire) {
            l->text.texture.speed = 0.0f;
            AUREA_CHECK_EQ(max_abs(s.render(FrameIndex{0}), s.render(FrameIndex{20})), 0.0f);
        }
    }
}

AUREA_TEST(TextTextureGpu, GalleryImageFillsTheLettersAndMissingImageFallsBackToFlat) {
    AUREA_REQUIRE_GPU();
    using namespace texture_test;
    Scene s(320, 140);
    const LayerId id = add_text(s, "FOTO", 90, Vec3{12, 16, 0});
    const FloatImage flat = s.render();
    Asset a; a.kind = AssetKind::Image;
    const AssetId aid = s.project.add_asset(std::move(a));
    s.images[aid.pack()] = quadrants(64, 64);
    Layer* l = s.comp->layer(id);
    l->text.texture.kind = kTextTextureImage;
    l->text.texture.image = aid.pack();
    l->text.texture.relief = 0.0f;
    const FloatImage img = s.render();
    const Stats st = compare(flat, img);
    u32 red = 0, green = 0;
    for (u32 y = 0; y < img.height; ++y) for (u32 x = 0; x < img.width; ++x) {
        const f32* p = img.at(x, y);
        red += p[0] > 0.6f && p[1] < 0.1f && p[2] < 0.1f;
        green += p[1] > 0.6f && p[0] < 0.1f && p[2] < 0.1f;
    }
    std::printf("    imagem: diferença %.3f, vermelhos %u, verdes %u, vazamento %u\n", st.meanDiff, red, green, st.spill);
    AUREA_CHECK(st.meanDiff > 0.2f);
    AUREA_CHECK(red > 200u && green > 200u);
    AUREA_CHECK_EQ(st.spill, 0u);
    // Asset sem pixels (não carregou): a cor lisa, sem buraco.
    s.images.erase(aid.pack());
    gpu().renderer.release_project_resources();
    AUREA_CHECK_EQ(max_abs(flat, s.render()), 0.0f);
}

AUREA_TEST(TextTextureGpu, OverlayEffectChangesEveryPresetKeepsAlphaAndExportMatchesPreview) {
    AUREA_REQUIRE_GPU();
    using namespace texture_test;
    Scene s(160, 160);
    s.comp->set_transparent_background(true);
    // Metade esquerda opaca, direita transparente.
    ImagePixels half = uniform_image(128, 128, 120, 140, 160, 255);
    for (u32 y = 0; y < 128; ++y) for (u32 x = 64; x < 128; ++x) half.rgba[(static_cast<usize>(y) * 128 + x) * 4 + 3] = 0;
    const LayerId id = s.image(std::move(half), 80, 80);
    const FloatImage original = s.render();
    auto& fx = s.add_effect(id, "aurea.stylize.texture_overlay");
    for (u32 preset = 0; preset < 14; ++preset) {
        fx.params[0].constant = ParamValue::scalar(static_cast<f32>(preset));
        fx.params[2].constant = ParamValue::scalar(80.0f);
        const FloatImage preview = s.render(FrameIndex{7});
        const FloatImage final = s.render(FrameIndex{7}, 1, true);
        f32 change = 0, outside = 0;
        bool finite = true;
        for (u32 y = 20; y < 140; ++y) for (u32 x = 20; x < 140; ++x) {
            const f32* a = original.at(x, y);
            const f32* b = preview.at(x, y);
            for (int c = 0; c < 4; ++c) finite &= std::isfinite(b[c]);
            if (x < 76) change += std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
            if (x > 84) outside = std::max(outside, b[3]);
        }
        const f32 d = max_abs(preview, final);
        std::printf("    efeito Textura %2u: mudança %.1f, alfa fora %.4f, prévia x export %.5f\n", preset, change, outside, d);
        AUREA_CHECK(finite);
        AUREA_CHECK(change > 5.0f);
        AUREA_CHECK_EQ(outside, 0.0f);   // só dentro da camada (padrão)
        AUREA_CHECK(d < 0.015f);
    }
    // Fora da camada também: a folha aparece onde a camada é transparente.
    fx.params[0].constant = ParamValue::scalar(6.0f);
    fx.params[6].constant = ParamValue::boolean(false);
    const FloatImage sheet = s.render();
    AUREA_CHECK(sheet.v(110, 80).w > 0.5f);
    // Opacidade 0: identidade.
    fx.params[6].constant = ParamValue::boolean(true);
    fx.params[2].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK_EQ(max_abs(original, s.render()), 0.0f);
    // Imagem da galeria: a outra camada (escondida) vira a textura.
    fx.params[2].constant = ParamValue::scalar(100.0f);
    fx.params[1].constant = ParamValue::scalar(4.0f);   // Normal
    fx.params[0].constant = ParamValue::scalar(14.0f);
    AUREA_CHECK_EQ(max_abs(original, s.render()), 0.0f);   // sem camada escolhida: nada
    const LayerId src = s.image(quadrants(160, 160), 80, 80);
    s.comp->layer(src)->visible = false;
    fx.params[10].constant.ref = src.pack();
    const FloatImage photo = s.render();
    const Vec4 p = photo.v(30, 30);
    std::printf("    imagem da camada: (%.3f %.3f %.3f %.3f)\n", p.x, p.y, p.z, p.w);
    AUREA_CHECK(max_abs(original, photo) > 0.3f);
}
