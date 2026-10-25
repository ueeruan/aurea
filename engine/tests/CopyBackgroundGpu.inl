// =============================================================================
//  COPIAR FUNDO (0.3.0): a camada vira uma janela para as camadas abaixo.
//  Pedido do usuário: "ajustar certa parte do vídeo por camadas de forma".
//
//  Uma forma vermelha no meio de uma foto, com [Copiar fundo, Inverter]: dentro
//  da forma a FOTO invertida (não o vermelho), fora dela a foto intacta. A
//  referência do "invertido" é uma camada de ajuste com Inverter no quadro todo.
//  Sem efeito depois do Copiar fundo a camada some (o quadro é o da foto).
// =============================================================================
#if defined(AUREA_TEST_VULKAN)
namespace copybg {

inline const f32* px_at(const FloatImage& img, u32 x, u32 y) {
    return &img.px[(static_cast<usize>(y) * img.width + x) * 4];
}

/// Maior diferença por canal (RGB) entre duas imagens na caixa [x0,x1)×[y0,y1).
inline f32 max_diff(const FloatImage& a, const FloatImage& b, u32 x0, u32 y0, u32 x1, u32 y1) {
    f32 worst = 0.0f;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x) {
            const f32* p = px_at(a, x, y);
            const f32* q = px_at(b, x, y);
            for (u32 c = 0; c < 3; ++c) worst = std::max(worst, std::fabs(p[c] - q[c]));
        }
    return worst;
}

/// Maior diferença FORA da caixa (a moldura em volta da forma, com folga).
inline f32 max_diff_outside(const FloatImage& a, const FloatImage& b, u32 x0, u32 y0, u32 x1, u32 y1) {
    f32 worst = 0.0f;
    for (u32 y = 0; y < a.height; ++y)
        for (u32 x = 0; x < a.width; ++x) {
            if (x >= x0 && x < x1 && y >= y0 && y < y1) continue;
            const f32* p = px_at(a, x, y);
            const f32* q = px_at(b, x, y);
            for (u32 c = 0; c < 3; ++c) worst = std::max(worst, std::fabs(p[c] - q[c]));
        }
    return worst;
}

struct Fixture {
    Scene s{96, 96};
    LayerId photo{}, shape{};
    Fixture() {
        photo = s.image(reference_image(96, 96), 48, 48);
        // Forma 40×40 no centro: 28..68 nos dois eixos.
        shape = s.solid(40, 40, Vec4{1, 0, 0, 1}, 48, 48);
    }
    /// A foto sozinha (a forma escondida).
    FloatImage plain() {
        s.comp->layer(shape)->visible = false;
        FloatImage img = s.render();
        s.comp->layer(shape)->visible = true;
        return img;
    }
    /// A foto inteira invertida (camada de ajuste com Inverter, a forma escondida).
    FloatImage inverted() {
        s.comp->layer(shape)->visible = false;
        const LayerId adj = s.solid(96, 96, Vec4{1, 0, 1, 1}, 48, 48);
        s.comp->layer(adj)->adjustment = true;
        (void)s.add_effect(adj, effect_keys::kInvert);
        FloatImage img = s.render();
        s.comp->layer(adj)->visible = false;
        s.comp->layer(shape)->visible = true;
        return img;
    }
};

} // namespace copybg

AUREA_TEST(CopyBackgroundGpu, EffectsAfterItAdjustOnlyTheBackdropInsideTheShape) {
    AUREA_REQUIRE_GPU();
    copybg::Fixture f;
    const FloatImage plain = f.plain();
    const FloatImage inv = f.inverted();
    // A referência tem de ser outra imagem (senão o teste não prova nada).
    AUREA_CHECK(copybg::max_diff(plain, inv, 34, 34, 62, 62) > 0.2f);
    (void)f.s.add_effect(f.shape, effect_keys::kCopyBackground);
    (void)f.s.add_effect(f.shape, effect_keys::kInvert);
    const FloatImage with = f.s.render();
    // Dentro (com 3 px de folga da borda): a foto invertida — nada do vermelho.
    const f32 inside = copybg::max_diff(with, inv, 31, 31, 65, 65);
    // Fora (com 3 px de folga): a foto intacta.
    const f32 outside = copybg::max_diff_outside(with, plain, 25, 25, 71, 71);
    std::printf("(dentro %.4f, fora %.4f) ", static_cast<double>(inside), static_cast<double>(outside));
    AUREA_CHECK(inside < 0.02f);
    AUREA_CHECK(outside < 0.004f);
    for (const f32 v : with.px) AUREA_CHECK(std::isfinite(v));
}

AUREA_TEST(CopyBackgroundGpu, NothingAfterItLeavesTheFrameAsWithoutTheLayer) {
    AUREA_REQUIRE_GPU();
    copybg::Fixture f;
    const FloatImage plain = f.plain();
    (void)f.s.add_effect(f.shape, effect_keys::kCopyBackground);
    const FloatImage with = f.s.render();
    AUREA_CHECK(copybg::max_diff(with, plain, 0, 0, 96, 96) < 0.004f);
    // Efeito depois dele desligado: o mesmo.
    EffectInstance& invert = f.s.add_effect(f.shape, effect_keys::kInvert);
    invert.enabled = false;
    AUREA_CHECK(copybg::max_diff(f.s.render(), plain, 0, 0, 96, 96) < 0.004f);
}

AUREA_TEST(CopyBackgroundGpu, InvertedWindowAdjustsEverythingButTheShape) {
    AUREA_REQUIRE_GPU();
    copybg::Fixture f;
    const FloatImage plain = f.plain();
    const FloatImage inv = f.inverted();
    EffectInstance& copy = f.s.add_effect(f.shape, effect_keys::kCopyBackground);
    copy.params[0].constant = ParamValue::scalar(1.0f);   // Alfa invertido
    (void)f.s.add_effect(f.shape, effect_keys::kInvert);
    const FloatImage with = f.s.render();
    AUREA_CHECK(copybg::max_diff(with, plain, 31, 31, 65, 65) < 0.004f);
    AUREA_CHECK(copybg::max_diff_outside(with, inv, 25, 25, 71, 71) < 0.02f);
    // A forma fora da tela: a janela invertida vale para o quadro todo.
    f.s.comp->layer(f.shape)->transform.position = Vec3{400, 400, 0};
    AUREA_CHECK(copybg::max_diff(f.s.render(), inv, 0, 0, 96, 96) < 0.02f);
}

AUREA_TEST(CopyBackgroundGpu, OpacityAndLayerMixWeighTheResult) {
    AUREA_REQUIRE_GPU();
    copybg::Fixture f;
    const FloatImage plain = f.plain();
    const FloatImage inv = f.inverted();
    (void)f.s.add_effect(f.shape, effect_keys::kCopyBackground);
    (void)f.s.add_effect(f.shape, effect_keys::kInvert);
    // Opacidade 50 %: no centro, metade foto, metade invertida.
    f.s.comp->layer(f.shape)->transform.opacity = 0.5f;
    const FloatImage half = f.s.render();
    const f32* h = copybg::px_at(half, 48, 40);
    const f32* p = copybg::px_at(plain, 48, 40);
    const f32* q = copybg::px_at(inv, 48, 40);
    for (u32 c = 0; c < 3; ++c) AUREA_CHECK(std::fabs(h[c] - 0.5f * (p[c] + q[c])) < 0.02f);
    // Misturar com a camada 100 %: dentro, o vermelho da própria forma.
    f.s.comp->layer(f.shape)->transform.opacity = 1.0f;
    f.s.comp->layer(f.shape)->effects[0].params[1].constant = ParamValue::scalar(100.0f);
    const FloatImage own = f.s.render();
    const f32* r = copybg::px_at(own, 48, 40);
    AUREA_CHECK(r[0] > 0.9f && r[1] < 0.05f && r[2] < 0.05f);
    AUREA_CHECK(copybg::max_diff_outside(own, plain, 25, 25, 71, 71) < 0.004f);
}

AUREA_TEST(CopyBackgroundGpu, EffectsBeforeItShapeTheWindowOnly) {
    AUREA_REQUIRE_GPU();
    copybg::Fixture f;
    const FloatImage plain = f.plain();
    const FloatImage inv = f.inverted();
    // Inverter ANTES do Copiar fundo só mexe na cor da janela (descartada):
    // com Inverter depois, o resultado é o mesmo do caso sem o de antes.
    (void)f.s.add_effect(f.shape, effect_keys::kInvert);
    (void)f.s.add_effect(f.shape, effect_keys::kCopyBackground);
    (void)f.s.add_effect(f.shape, effect_keys::kInvert);
    const FloatImage with = f.s.render();
    AUREA_CHECK(copybg::max_diff(with, inv, 31, 31, 65, 65) < 0.02f);
    AUREA_CHECK(copybg::max_diff_outside(with, plain, 25, 25, 71, 71) < 0.004f);
    // Desfoque antes: a borda da janela fica suave (meio-termo entre a foto e a
    // invertida bem na borda), o centro continua invertido.
    Layer* l = f.s.comp->layer(f.shape);
    l->effects.erase(l->effects.begin());
    EffectInstance blur;
    blur.id = l->alloc_effect_id();
    blur.type = effect_type_id(effect_keys::kGaussianBlur);
    initialize_instance(blur, *gpu().effects.params(blur.type));
    blur.params[0].constant = ParamValue::scalar(12.0f);
    l->effects.insert(l->effects.begin(), std::move(blur));
    const FloatImage soft = f.s.render();
    AUREA_CHECK(copybg::max_diff(soft, inv, 46, 46, 50, 50) < 0.03f);
    const f32* e = copybg::px_at(soft, 28, 48);
    const f32* p = copybg::px_at(plain, 28, 48);
    const f32* q = copybg::px_at(inv, 28, 48);
    bool between = false;
    for (u32 c = 0; c < 3; ++c)
        between |= std::fabs(p[c] - q[c]) > 0.2f && std::fabs(e[c] - p[c]) > 0.05f && std::fabs(e[c] - q[c]) > 0.05f;
    AUREA_CHECK(between);
}
#endif
