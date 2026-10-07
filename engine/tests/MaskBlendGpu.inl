// =============================================================================
//  Aurea / tests / MaskBlendGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  Máscara do Alight Motion em "Mesclagem e opacidade": BlendMode::Mask
//  (destino-dentro) e BlendMode::Exclude (destino-fora). A camada não aparece;
//  recorta o que já está composto abaixo dela no MESMO grupo/pré-comp. A cor
//  de fundo da composição não é recortada (entra por baixo no fim).
// =============================================================================

#include "aurea/project/Serialization.hpp"

namespace {

/// Imagem 64×64 em degradê (R sobe com x, G com y, B fixo): o meio e o canto
/// têm cores diferentes, dá para saber de onde veio cada pixel.
ImagePixels mask_test_gradient(u32 w, u32 h) {
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

Vec4 mask_test_gradient_at(u32 x, u32 y, u32 w, u32 h) {
    return Vec4{srgb_decode(static_cast<f32>(x * 255 / (w - 1)) / 255.0f),
                srgb_decode(static_cast<f32>(y * 255 / (h - 1)) / 255.0f), srgb_decode(96.0f / 255.0f), 1.0f};
}

/// Círculo branco de diâmetro `d` no centro da composição, no modo dado.
LayerId mask_test_circle(Composition* c, f32 d, f32 cx, f32 cy, BlendMode mode) {
    const LayerId id = c->add_layer(LayerKind::Shape, "mascara");
    Layer* l = c->layer(id);
    l->shape.shapeType = 1;   // elipse
    l->shape.bounds = Rect{0, 0, d, d};
    l->shape.fillColor = Vec4{1, 1, 1, 1};
    l->transform.anchor = Vec3{d * 0.5f, d * 0.5f, 0};
    l->transform.position = Vec3{cx, cy, 0};
    l->blendMode = mode;
    return id;
}

} // namespace

AUREA_TEST(Gpu, MaskBlendKeepsOnlyTheCircleOfTheImageBelow) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    for (const bool transparent : {false, true}) {
        Scene s(W, H);
        if (transparent) s.comp->set_transparent_background(true);
        s.image(mask_test_gradient(W, H), W * 0.5f, H * 0.5f);
        mask_test_circle(s.comp, 32.0f, W * 0.5f, H * 0.5f, BlendMode::Mask);
        // Acima da máscara: não é recortada.
        s.solid(6, 6, Vec4{1, 0, 0, 1}, 60, 60);
        const FloatImage img = s.render();
        const Vec4 bg = transparent ? Vec4{0, 0, 0, 0} : Vec4{0, 0, 0, 1};
        // Dentro do círculo: a imagem, sem nada do branco da máscara.
        AUREA_CHECK(near4(img.v(32, 32), mask_test_gradient_at(32, 32, W, H), 3.0f / 255.0f));
        AUREA_CHECK(near4(img.v(26, 36), mask_test_gradient_at(26, 36, W, H), 3.0f / 255.0f));
        // Fora: a imagem sumiu e sobra só o fundo da composição.
        AUREA_CHECK(near4(img.v(4, 4), bg, 1.0f / 255.0f));
        AUREA_CHECK(near4(img.v(60, 4), bg, 1.0f / 255.0f));
        AUREA_CHECK(near4(img.v(32, 3), bg, 1.0f / 255.0f));
        // A camada de cima continua.
        AUREA_CHECK(near4(img.v(60, 60), Vec4{1, 0, 0, 1}, 2.0f / 255.0f));
        // Pixels "vivos" ≈ área do círculo (π·16² ≈ 804) + o quadrado de cima (36).
        u32 alive = 0;
        for (u32 y = 0; y < H; ++y)
            for (u32 x = 0; x < W; ++x) {
                const Vec4 p = img.v(x, y);
                alive += (p.x + p.y > 0.02f) ? 1u : 0u;
            }
        std::printf("    fundo %s: %u px visíveis ", transparent ? "transparente" : "opaco", alive);
        AUREA_CHECK(alive > 760 && alive < 900);
    }
}

AUREA_TEST(Gpu, ExcludeBlendCutsAHoleInTheImageBelow) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    for (const bool transparent : {false, true}) {
        Scene s(W, H);
        if (transparent) s.comp->set_transparent_background(true);
        s.image(mask_test_gradient(W, H), W * 0.5f, H * 0.5f);
        mask_test_circle(s.comp, 32.0f, W * 0.5f, H * 0.5f, BlendMode::Exclude);
        const FloatImage img = s.render();
        const Vec4 bg = transparent ? Vec4{0, 0, 0, 0} : Vec4{0, 0, 0, 1};
        AUREA_CHECK(near4(img.v(32, 32), bg, 1.0f / 255.0f));                           // o furo
        AUREA_CHECK(near4(img.v(4, 4), mask_test_gradient_at(4, 4, W, H), 3.0f / 255.0f));  // o resto da imagem
        AUREA_CHECK(near4(img.v(60, 50), mask_test_gradient_at(60, 50, W, H), 3.0f / 255.0f));
    }
    // Opacidade 50 %: o furo fica pela metade.
    Scene s(W, H);
    s.comp->set_transparent_background(true);
    s.solid(64, 64, Vec4{0, 1, 0, 1}, 32, 32);
    const LayerId m = mask_test_circle(s.comp, 32.0f, 32, 32, BlendMode::Exclude);
    s.comp->layer(m)->transform.opacity = 0.5f;
    const FloatImage img = s.render();
    AUREA_CHECK(near4(img.v(32, 32), Vec4{0, 0.5f, 0, 0.5f}, 3.0f / 255.0f));
}

AUREA_TEST(Gpu, MaskBlendInsideAGroupOnlyClipsTheGroup) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    for (const BlendMode mode : {BlendMode::Mask, BlendMode::Exclude}) {
        Scene s(W, H);
        s.solid(64, 64, Vec4{0, 0, 1, 1}, 32, 32);   // fora do grupo, abaixo dele
        Timeline& tl = s.project.timeline();
        const CompositionId cid = tl.create_composition("grupo", W, H, 30.0);
        s.comp = tl.composition(tl.root());   // a criação pode ter realocado
        Composition* g = tl.composition(cid);
        g->set_duration(FrameIndex{300});
        g->set_transparent_background(true);
        g->set_nesting_depth(1);
        const LayerId inner = g->add_layer(LayerKind::Shape, "verde");
        Layer* il = g->layer(inner);
        il->shape.bounds = Rect{0, 0, 64, 64};
        il->shape.fillColor = Vec4{0, 1, 0, 1};
        il->transform.anchor = Vec3{32, 32, 0};
        il->transform.position = Vec3{32, 32, 0};
        mask_test_circle(g, 32.0f, 32, 32, mode);
        const LayerId gid = s.comp->add_layer(LayerKind::Composition, "grupo");
        Layer* gl = s.comp->layer(gid);
        gl->nested.composition = cid;
        gl->start = FrameIndex{0};
        gl->end = FrameIndex{300};
        gl->transform.anchor = Vec3{32, 32, 0};
        gl->transform.position = Vec3{32, 32, 0};
        const FloatImage img = s.render();
        const Vec4 green{0, 1, 0, 1}, blue{0, 0, 1, 1};
        if (mode == BlendMode::Mask) {
            AUREA_CHECK(near4(img.v(32, 32), green, 2.0f / 255.0f));   // dentro: o verde do grupo
            AUREA_CHECK(near4(img.v(4, 4), blue, 2.0f / 255.0f));      // fora: o azul de fora do grupo, intacto
        } else {
            AUREA_CHECK(near4(img.v(32, 32), blue, 2.0f / 255.0f));    // furo no verde mostra o azul
            AUREA_CHECK(near4(img.v(4, 4), green, 2.0f / 255.0f));
        }
    }
}

AUREA_TEST(Gpu, MaskBlendModesSurviveSaveAndReopen) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 64);
    s.image(mask_test_gradient(64, 64), 32, 32);
    const LayerId a = mask_test_circle(s.comp, 32.0f, 20, 20, BlendMode::Mask);
    const LayerId b = mask_test_circle(s.comp, 16.0f, 44, 44, BlendMode::Exclude);
    const std::string path = "tests/build/prompt03/aurea_teste_mascara_am.aurea";
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(s.project, path, SaveOptions{}, &error).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    AUREA_CHECK(lc != nullptr);
    if (lc) {
        AUREA_CHECK(lc->layer(a) && lc->layer(a)->blendMode == BlendMode::Mask);
        AUREA_CHECK(lc->layer(b) && lc->layer(b)->blendMode == BlendMode::Exclude);
    }
    std::remove(path.c_str());
}
