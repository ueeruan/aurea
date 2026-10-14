// =============================================================================
//  Aurea / tests / ClipMaskGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  Máscara de recorte por camada: a base e as recortadas acima dela formam um
//  grupo isolado; cada recortada entra como source-atop (com a própria
//  mistura) e o grupo entra na cena com a mistura e a opacidade da base. O
//  mesmo `Renderer` serve o preview e o export (finalQuality).
// =============================================================================

namespace {

/// Degradê opaco (R com x, G com y, B fixo): a "foto" recortada.
ImagePixels clip_test_photo(u32 w, u32 h) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const usize i = (static_cast<usize>(y) * w + x) * 4;
            px.rgba[i] = static_cast<u8>(x * 255 / (w - 1));
            px.rgba[i + 1] = static_cast<u8>(y * 255 / (h - 1));
            px.rgba[i + 2] = 96;
            px.rgba[i + 3] = 255;
        }
    return px;
}

Vec4 clip_test_photo_at(u32 x, u32 y, u32 w, u32 h) {
    return Vec4{srgb_decode(static_cast<f32>(x * 255 / (w - 1)) / 255.0f),
                srgb_decode(static_cast<f32>(y * 255 / (h - 1)) / 255.0f), srgb_decode(96.0f / 255.0f), 1.0f};
}

/// Círculo de diâmetro `d` centrado em (cx, cy).
LayerId clip_test_circle(Composition* c, f32 d, f32 cx, f32 cy, Vec4 color) {
    const LayerId id = c->add_layer(LayerKind::Shape, "base");
    Layer* l = c->layer(id);
    l->shape.shapeType = 1;   // elipse
    l->shape.bounds = Rect{0, 0, d, d};
    l->shape.fillColor = color;
    l->transform.anchor = Vec3{d * 0.5f, d * 0.5f, 0};
    l->transform.position = Vec3{cx, cy, 0};
    return id;
}

/// Pixels com a cor da foto (verde > 0,02 e não azul puro do fundo).
u32 clip_test_photo_pixels(const FloatImage& img) {
    u32 n = 0;
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) {
            const Vec4 p = img.v(x, y);
            n += (p.x + p.y > 0.02f && p.z < 0.5f) ? 1u : 0u;
        }
    return n;
}

} // namespace

AUREA_TEST(Gpu, ClipMaskShowsThePhotoOnlyInsideTheCircle) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    for (const bool exportQuality : {false, true}) {
        Scene s(W, H);
        s.solid(64, 64, Vec4{0, 0, 1, 1}, 32, 32);                          // fundo azul (fora da cadeia)
        clip_test_circle(s.comp, 32.0f, 32, 32, Vec4{1, 1, 1, 1});           // base
        const LayerId photo = s.image(clip_test_photo(W, H), 32, 32);         // recortada
        s.comp->layer(photo)->clipToBelow = true;
        const FloatImage img = s.render(FrameIndex{0}, 1, exportQuality);
        // Dentro do círculo: a foto (o branco da base fica coberto).
        AUREA_CHECK(near4(img.v(32, 32), clip_test_photo_at(32, 32, W, H), 3.0f / 255.0f));
        AUREA_CHECK(near4(img.v(24, 38), clip_test_photo_at(24, 38, W, H), 3.0f / 255.0f));
        // Fora: o azul de baixo, sem nada da foto nem da base.
        AUREA_CHECK(near4(img.v(3, 3), Vec4{0, 0, 1, 1}, 1.0f / 255.0f));
        AUREA_CHECK(near4(img.v(60, 32), Vec4{0, 0, 1, 1}, 1.0f / 255.0f));
        AUREA_CHECK(near4(img.v(32, 2), Vec4{0, 0, 1, 1}, 1.0f / 255.0f));
        // A foto ocupa ≈ a área do círculo (π·16² ≈ 804 px).
        const u32 n = clip_test_photo_pixels(img);
        std::printf("    %s: %u px de foto ", exportQuality ? "export" : "preview", n);
        AUREA_CHECK(n > 740 && n < 880);
    }
}

AUREA_TEST(Gpu, ClipMaskWithoutTheFlagDrawsTheWholePhoto) {
    AUREA_REQUIRE_GPU();
    // Projeto antigo (sem recorte): a foto cobre tudo, exatamente como antes.
    const u32 W = 64, H = 64;
    Scene s(W, H);
    s.solid(64, 64, Vec4{0, 0, 1, 1}, 32, 32);
    clip_test_circle(s.comp, 32.0f, 32, 32, Vec4{1, 1, 1, 1});
    s.image(clip_test_photo(W, H), 32, 32);
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(3, 3), clip_test_photo_at(3, 3, W, H), 3.0f / 255.0f));
    AUREA_CHECK(near4(img.v(60, 32), clip_test_photo_at(60, 32, W, H), 3.0f / 255.0f));
}

AUREA_TEST(Gpu, ClipMaskGroupUsesBaseOpacityAndChildBlend) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    {   // Opacidade da base vale para o grupo inteiro (base + recortadas).
        Scene s(W, H);
        s.comp->set_transparent_background(true);
        const LayerId base = clip_test_circle(s.comp, 32.0f, 32, 32, Vec4{1, 1, 1, 1});
        s.comp->layer(base)->transform.opacity = 0.5f;
        const LayerId red = s.solid(64, 64, Vec4{1, 0, 0, 1}, 32, 32);
        s.comp->layer(red)->clipToBelow = true;
        const FloatImage img = s.render();
        AUREA_CHECK(near4(img.v(32, 32), Vec4{0.5f, 0, 0, 0.5f}, 3.0f / 255.0f));   // vermelho a 50 %
        AUREA_CHECK(near4(img.v(2, 2), Vec4{0, 0, 0, 0}, 1.0f / 255.0f));           // fora: nada
    }
    {   // Recortada em Multiplicar: mistura com a base só dentro dela.
        Scene s(W, H);
        clip_test_circle(s.comp, 32.0f, 32, 32, Vec4{1, 1, 0, 1});            // amarelo
        const LayerId mul = s.solid(64, 64, Vec4{1, 0.5f, 1, 1}, 32, 32);
        s.comp->layer(mul)->clipToBelow = true;
        s.comp->layer(mul)->blendMode = BlendMode::Multiply;
        const FloatImage img = s.render();
        const Vec4 shapeYellow = img.v(32, 32);
        AUREA_CHECK(shapeYellow.x > 0.95f && shapeYellow.z < 0.01f);               // amarelo × magenta-claro = vermelho/laranja
        AUREA_CHECK(shapeYellow.y > 0.15f && shapeYellow.y < 0.6f);
        AUREA_CHECK(near4(img.v(2, 2), Vec4{0, 0, 0, 1}, 1.0f / 255.0f));          // fora: o fundo, sem o multiplicar
    }
    {   // Recortada a 50 %: metade sobre a base, nada fora.
        Scene s(W, H);
        clip_test_circle(s.comp, 32.0f, 32, 32, Vec4{1, 1, 1, 1});
        const LayerId blue = s.solid(64, 64, Vec4{0, 0, 1, 1}, 32, 32);
        s.comp->layer(blue)->clipToBelow = true;
        s.comp->layer(blue)->transform.opacity = 0.5f;
        const FloatImage img = s.render();
        AUREA_CHECK(near4(img.v(32, 32), Vec4{0.5f, 0.5f, 1.0f, 1}, 3.0f / 255.0f));
        AUREA_CHECK(near4(img.v(2, 2), Vec4{0, 0, 0, 1}, 1.0f / 255.0f));
    }
}

AUREA_TEST(Gpu, ClipMaskHiddenBaseHidesTheChain) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    Scene s(W, H);
    const LayerId base = clip_test_circle(s.comp, 32.0f, 32, 32, Vec4{1, 1, 1, 1});
    const LayerId red = s.solid(64, 64, Vec4{1, 0, 0, 1}, 32, 32);
    s.comp->layer(red)->clipToBelow = true;
    s.comp->layer(base)->visible = false;
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(32, 32), Vec4{0, 0, 0, 1}, 1.0f / 255.0f));
    AUREA_CHECK(near4(img.v(2, 2), Vec4{0, 0, 0, 1}, 1.0f / 255.0f));
}

AUREA_TEST(Gpu, ClipMaskFollowsTheBasePieceAtEachTime) {
    AUREA_REQUIRE_GPU();
    // Base dividida em dois pedaços (o 2º acima do 1º na ordem): a recortada
    // usa o pedaço que existe em cada instante. Círculo à esquerda até o
    // quadro 10, à direita depois.
    const u32 W = 64, H = 64;
    Scene s(W, H);
    const LayerId left = clip_test_circle(s.comp, 24.0f, 16, 32, Vec4{1, 1, 1, 1});
    const LayerId right = clip_test_circle(s.comp, 24.0f, 48, 32, Vec4{1, 1, 1, 1});
    s.comp->layer(left)->end = FrameIndex{10};
    s.comp->layer(right)->start = FrameIndex{10};
    const LayerId red = s.solid(64, 64, Vec4{1, 0, 0, 1}, 32, 32);
    s.comp->layer(red)->clipToBelow = true;
    const FloatImage early = s.render(FrameIndex{2});
    AUREA_CHECK(near4(early.v(16, 32), Vec4{1, 0, 0, 1}, 2.0f / 255.0f));
    AUREA_CHECK(near4(early.v(48, 32), Vec4{0, 0, 0, 1}, 1.0f / 255.0f));
    const FloatImage late = s.render(FrameIndex{20});
    AUREA_CHECK(near4(late.v(48, 32), Vec4{1, 0, 0, 1}, 2.0f / 255.0f));
    AUREA_CHECK(near4(late.v(16, 32), Vec4{0, 0, 0, 1}, 1.0f / 255.0f));
}
