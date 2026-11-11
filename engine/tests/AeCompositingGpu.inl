// =============================================================================
//  Aurea / tests / AeCompositingGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  AE compositing (2026-11-10): os modos de mesclagem do After Effects que não
//  são uma fórmula B(Cb, Cs) — Estêncil/Silhueta luma, Dissolver, Dissolver
//  dançante, Adição de alfa e Luminescente pré-multiplicado —, os clássicos
//  nos casos de borda que os separam dos modernos, Preservar transparência e
//  os modos de máscara Clarear/Escurecer com a queda linear do feather. O AE é
//  só a especificação: as contas esperadas são as do cabeçalho do blend.frag
//  e do raster.frag, refeitas aqui na CPU. Os quatro modos que são fórmula
//  (Mistura forte e os três clássicos) também passam pelo laço de
//  EveryBlendModeMatchesTheFormulaPixelForPixel (BlendRef).
// =============================================================================

namespace {

bool ae_near(Vec4 a, Vec4 b, f32 tol) {
    return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol && std::fabs(a.z - b.z) <= tol && std::fabs(a.w - b.w) <= tol;
}

} // namespace

AUREA_TEST(Gpu, AeStencilAndSilhouetteLumaCutTheStackByLuminance) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    const f32 tol = 3.0f / 255.0f;
    // Cinza 50 % codificado: a luminância linear é o próprio valor linear.
    const f32 L = srgb_decode(0.5f);
    for (const BlendMode mode : {BlendMode::StencilLuma, BlendMode::SilhouetteLuma}) {
        Scene s(W, H);
        s.image(mask_test_gradient(W, H), W * 0.5f, H * 0.5f);
        const LayerId top = s.solid(32, 32, Vec4{0.5f, 0.5f, 0.5f, 1}, W * 0.5f, H * 0.5f);
        s.comp->layer(top)->blendMode = mode;
        const FloatImage img = s.render();
        const f32 k = mode == BlendMode::StencilLuma ? L : 1.0f - L;
        f32 worst = 0.0f;
        for (const u32 y : {20u, 32u, 44u})
            for (const u32 x : {20u, 32u, 44u}) {
                const Vec4 g = mask_test_gradient_at(x, y, W, H);
                const Vec4 want{g.x * k, g.y * k, g.z * k, 1.0f};   // fundo preto opaco por baixo
                const Vec4 got = img.v(x, y);
                worst = std::max({worst, std::fabs(got.x - want.x), std::fabs(got.y - want.y), std::fabs(got.z - want.z)});
                AUREA_CHECK_MSG(ae_near(got, want, tol), blend_name(mode));
            }
        // Fora do quad: o Estêncil apaga (sobra a cor de fundo), a Silhueta não mexe.
        const Vec4 g = mask_test_gradient_at(4, 4, W, H);
        const Vec4 outside = mode == BlendMode::StencilLuma ? Vec4{0, 0, 0, 1} : g;
        AUREA_CHECK_MSG(ae_near(img.v(4, 4), outside, tol), blend_name(mode));
        std::printf("    %s: pior %.2f/255 ", blend_name(mode), worst * 255.0f);
    }
}

AUREA_TEST(Gpu, AeDissolveShowsWholePixelsAgainstOpacityAndDancingReseedsPerFrame) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    const f32 tol = 3.0f / 255.0f;
    auto render = [&](BlendMode mode, f32 opacity, i64 frame) {
        Scene s(W, H);
        s.solid(64, 64, Vec4{0, 0, 1, 1}, 32, 32);
        const LayerId top = s.solid(64, 64, Vec4{1, 0, 0, 1}, 32, 32);
        s.comp->layer(top)->blendMode = mode;
        s.comp->layer(top)->transform.opacity = opacity;
        return s.render(FrameIndex{frame});
    };
    // Referência: cada pixel é a camada INTEIRA (vermelho) ou o fundo (azul),
    // nunca a mistura; a fração de vermelho segue a opacidade.
    auto pattern = [&](const FloatImage& img, u32& red, u32& mixed) {
        std::vector<u8> bits(static_cast<usize>(W) * H);
        red = 0; mixed = 0;
        for (u32 y = 0; y < H; ++y)
            for (u32 x = 0; x < W; ++x) {
                const Vec4 p = img.v(x, y);
                const bool isRed = ae_near(p, Vec4{1, 0, 0, 1}, tol), isBlue = ae_near(p, Vec4{0, 0, 1, 1}, tol);
                if (!isRed && !isBlue) ++mixed;
                bits[static_cast<usize>(y) * W + x] = isRed ? 1 : 0;
                red += isRed ? 1u : 0u;
            }
        return bits;
    };
    const u32 total = W * H;
    for (const f32 opacity : {0.25f, 0.5f, 0.75f}) {
        u32 red = 0, mixed = 0;
        (void)pattern(render(BlendMode::Dissolve, opacity, 0), red, mixed);
        const f32 frac = static_cast<f32>(red) / static_cast<f32>(total);
        std::printf("    opacidade %.2f: %.3f vermelho, %u misturados ", opacity, frac, mixed);
        AUREA_CHECK_EQ(mixed, 0u);
        AUREA_CHECK_NEAR(frac, opacity, 0.05f);
    }
    // Opacidade cheia: a camada inteira (o Dissolver só age no alfa parcial).
    {
        u32 red = 0, mixed = 0;
        (void)pattern(render(BlendMode::Dissolve, 1.0f, 0), red, mixed);
        AUREA_CHECK_EQ(red, total);
    }
    u32 r0 = 0, r1 = 0, m = 0;
    const auto still0 = pattern(render(BlendMode::Dissolve, 0.5f, 0), r0, m);
    const auto still1 = pattern(render(BlendMode::Dissolve, 0.5f, 7), r1, m);
    const auto again0 = pattern(render(BlendMode::Dissolve, 0.5f, 0), r1, m);
    AUREA_CHECK(still0 == again0);   // determinístico (export = prévia)
    AUREA_CHECK(still0 == still1);   // o padrão do Dissolver não anda no tempo
    const auto dance0 = pattern(render(BlendMode::DancingDissolve, 0.5f, 0), r0, m);
    const auto dance1 = pattern(render(BlendMode::DancingDissolve, 0.5f, 1), r1, m);
    const auto dance0b = pattern(render(BlendMode::DancingDissolve, 0.5f, 0), r1, m);
    u32 changed = 0;
    for (usize i = 0; i < dance0.size(); ++i) changed += dance0[i] != dance1[i] ? 1u : 0u;
    std::printf("    dançante: %u de %u pixels trocaram de um quadro ao seguinte ", changed, total);
    AUREA_CHECK(dance0 == dance0b);
    AUREA_CHECK(changed > total / 4);   // dois sorteios independentes a 50 %: ~metade troca
}

AUREA_TEST(Gpu, AeAlphaAddAndLuminescentPremulFollowTheirFormulas) {
    AUREA_REQUIRE_GPU();
    const f32 tol = 3.0f / 255.0f;
    {
        // Adição de alfa: duas metades de alfa somam 1 (o "sobre" daria 0,75).
        Scene s(32, 32);
        s.comp->set_transparent_background(true);
        const LayerId a = s.solid(32, 32, Vec4{1, 0, 0, 1}, 16, 16);
        s.comp->layer(a)->transform.opacity = 0.5f;
        const LayerId b = s.solid(32, 32, Vec4{0, 1, 0, 1}, 16, 16);
        s.comp->layer(b)->transform.opacity = 0.5f;
        s.comp->layer(b)->blendMode = BlendMode::AlphaAdd;
        const Vec4 got = s.render().v(16, 16);
        // co = cs·αs + cb·αb·min(1, (1 − αs)/αb) = (0,5; 0,5; 0), αo = min(1, 0,5 + 0,5).
        std::printf("    adicao de alfa (%.3f %.3f %.3f %.3f) ", got.x, got.y, got.z, got.w);
        AUREA_CHECK(ae_near(got, Vec4{0.5f, 0.5f, 0.0f, 1.0f}, tol));
    }
    {
        // Luminescente pré-multiplicado sobre fundo opaco: src + dst·(1 − αs).
        Scene s(32, 32);
        s.solid(32, 32, Vec4{0.5f, 0.5f, 0.5f, 1}, 16, 16);
        const LayerId t = s.solid(32, 32, Vec4{1, 0.25f, 0, 1}, 16, 16);
        s.comp->layer(t)->transform.opacity = 0.6f;
        s.comp->layer(t)->blendMode = BlendMode::LuminescentPremul;
        const Vec4 got = s.render().v(16, 16);
        const Vec3 bg = lin3(0.5f, 0.5f, 0.5f), fg = lin3(1.0f, 0.25f, 0.0f);
        const Vec4 want{fg.x * 0.6f + bg.x * 0.4f, fg.y * 0.6f + bg.y * 0.4f, fg.z * 0.6f + bg.z * 0.4f, 1.0f};
        AUREA_CHECK(ae_near(got, want, tol));
    }
}

AUREA_TEST(Gpu, AeClassicBurnAndDodgeDropTheModernEdgeCases) {
    AUREA_REQUIRE_GPU();
    const f32 tol = 3.0f / 255.0f;
    auto over = [&](Vec4 below, Vec4 above, BlendMode mode) {
        Scene s(16, 16);
        s.solid(16, 16, below, 8, 8);
        const LayerId t = s.solid(16, 16, above, 8, 8);
        s.comp->layer(t)->blendMode = mode;
        return s.render().v(8, 8);
    };
    // Branco queimado por preto: o moderno guarda o branco (b = 1), o clássico zera (s = 0).
    AUREA_CHECK(ae_near(over(Vec4{1, 1, 1, 1}, Vec4{0, 0, 0, 1}, BlendMode::ColorBurn), Vec4{1, 1, 1, 1}, tol));
    AUREA_CHECK(ae_near(over(Vec4{1, 1, 1, 1}, Vec4{0, 0, 0, 1}, BlendMode::ClassicColorBurn), Vec4{0, 0, 0, 1}, tol));
    // Preto subexposto por branco: o moderno guarda o preto (b = 0), o clássico vai a 1 (s = 1).
    AUREA_CHECK(ae_near(over(Vec4{0, 0, 0, 1}, Vec4{1, 1, 1, 1}, BlendMode::ColorDodge), Vec4{0, 0, 0, 1}, tol));
    AUREA_CHECK(ae_near(over(Vec4{0, 0, 0, 1}, Vec4{1, 1, 1, 1}, BlendMode::ClassicColorDodge), Vec4{1, 1, 1, 1}, tol));
    // Mistura forte: por canal, 1 se b + s ≥ 1 (lineares 0,604 + 0,604) e 0 abaixo (0,214 + 0,214).
    AUREA_CHECK(ae_near(over(Vec4{0.8f, 0.5f, 0.8f, 1}, Vec4{0.8f, 0.5f, 0.2f, 1}, BlendMode::HardMix), Vec4{1, 0, 0, 1}, tol));
}

AUREA_TEST(Gpu, AePreserveTransparencyPaintsOnlyOverTheStackBelow) {
    AUREA_REQUIRE_GPU();
    const f32 tol = 3.0f / 255.0f;
    auto build = [](Scene& s, bool preserve, f32 belowOpacity) {
        const LayerId below = s.solid(20, 20, Vec4{0, 0, 1, 1}, 32, 32);
        s.comp->layer(below)->transform.opacity = belowOpacity;
        const LayerId top = s.solid(64, 64, Vec4{1, 0, 0, 1}, 32, 32);
        s.comp->layer(top)->preserveTransparency = preserve;
    };
    {
        // Fundo transparente: dentro do quadrado azul a camada pinta (alfa 1);
        // fora dele, onde não há nada abaixo, não pinta nada.
        Scene s(64, 64);
        s.comp->set_transparent_background(true);
        build(s, true, 1.0f);
        const FloatImage img = s.render();
        AUREA_CHECK(ae_near(img.v(32, 32), Vec4{1, 0, 0, 1}, tol));
        AUREA_CHECK(ae_near(img.v(4, 4), Vec4{0, 0, 0, 0}, tol));
    }
    {
        // A cor de fundo da composição não conta como imagem abaixo (como no AE).
        Scene s(64, 64);
        build(s, true, 1.0f);
        const FloatImage img = s.render();
        AUREA_CHECK(ae_near(img.v(32, 32), Vec4{1, 0, 0, 1}, tol));
        AUREA_CHECK(ae_near(img.v(4, 4), Vec4{0, 0, 0, 1}, tol));
    }
    {
        // Alfa parcial abaixo: o resultado fica com o alfa de baixo (0,5) e a cor
        // da camada (pré-multiplicada pelo alfa de baixo).
        Scene s(64, 64);
        s.comp->set_transparent_background(true);
        build(s, true, 0.5f);
        const Vec4 got = s.render().v(32, 32);
        std::printf("    alfa parcial (%.3f %.3f %.3f %.3f) ", got.x, got.y, got.z, got.w);
        AUREA_CHECK(ae_near(got, Vec4{0.5f, 0, 0, 0.5f}, tol));
    }
    {
        // Desligado: a camada cobre tudo (o de sempre).
        Scene s(64, 64);
        s.comp->set_transparent_background(true);
        build(s, false, 1.0f);
        AUREA_CHECK(ae_near(s.render().v(4, 4), Vec4{1, 0, 0, 1}, tol));
    }
}

AUREA_TEST(Gpu, AeMaskLightenAndDarkenMatchTheCpuCoverage) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 96);
    const LayerId id = white_full(s);
    Layer* l = s.comp->layer(id);
    struct Probe { u32 x, y; f32 lighten, darken; };
    // A (10..70) a 60 %, B (40..100 × 40..90) a 40 %.
    const Probe probes[] = {{55, 55, 0.6f, 0.4f}, {20, 20, 0.6f, 0.0f}, {90, 85, 0.4f, 0.0f}, {5, 5, 0.0f, 0.0f}};
    for (const MaskOperation op : {MaskOperation::Lighten, MaskOperation::Darken}) {
        l->masks = {rect_mask(0, 10, 10, 70, 70), rect_mask(1, 40, 40, 100, 90, op)};
        l->masks[0].opacity = 0.6f;
        l->masks[1].opacity = 0.4f;
        const FloatImage img = s.render();
        std::vector<Vec4> block;
        f32 start = 0;
        u64 key = 0;
        const u32 n = mask::build_block(*l, 0.0, Vec2{0, 0}, 0.2f, block, start, key);
        AUREA_CHECK_EQ(n, 2u);
        for (const Probe& p : probes) {
            const f32 want = op == MaskOperation::Lighten ? p.lighten : p.darken;
            const f32 cpu = mask::coverage_at(block.data(), n, start, Vec2{p.x + 0.5f, p.y + 0.5f}, 1.0f);
            AUREA_CHECK_NEAR(cpu, want, 0.004f);
            AUREA_CHECK_NEAR(img.v(p.x, p.y).x, want, 0.006f);
        }
    }
    // Escurecer como PRIMEIRA máscara começa de "tudo dentro" (a interseção):
    // sozinha ela é a própria máscara.
    l->masks = {rect_mask(0, 10, 10, 70, 70, MaskOperation::Darken)};
    const FloatImage solo = s.render();
    AUREA_CHECK_NEAR(solo.v(40, 40).x, 1.0f, 0.006f);
    AUREA_CHECK_NEAR(solo.v(100, 40).x, 0.0f, 0.006f);
}

AUREA_TEST(Gpu, AeMaskLinearFalloffIsAStraightRampOnGpuAndCpu) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 96);
    const LayerId id = white_full(s);
    Layer* l = s.comp->layer(id);
    l->masks = {rect_mask(0, 20, 16, 100, 80)};
    l->masks[0].feather = 20.0f;   // σ = 5 px, rampa de ±2σ
    const FloatImage smooth = s.render();
    l->masks[0].featherFalloff = 1;
    const FloatImage linear = s.render();
    std::vector<Vec4> block;
    f32 start = 0;
    u64 key = 0;
    const u32 n = mask::build_block(*l, 0.0, Vec2{0, 0}, 0.2f, block, start, key);
    const f32 w = std::sqrt(16.0f * 25.0f + 1.0f);   // largura da rampa (feather e texel)
    f32 worst = 0.0f;
    for (const u32 x : {12u, 18u, 20u, 22u, 25u, 28u, 30u, 40u}) {
        const f32 sd = static_cast<f32>(x) + 0.5f - 20.0f;   // distância assinada até a borda esquerda
        const f32 want = std::clamp(sd / w + 0.5f, 0.0f, 1.0f);
        const f32 cpu = mask::coverage_at(block.data(), n, start, Vec2{x + 0.5f, 48.5f}, 1.0f);
        const f32 got = linear.v(x, 48).x;
        worst = std::max(worst, std::fabs(got - want));
        AUREA_CHECK_NEAR(cpu, want, 0.003f);
        AUREA_CHECK_NEAR(got, want, 0.006f);
    }
    // A rampa reta difere da gaussiana dentro da borda (5,5 px: 0,775 contra ≈ 0,86).
    std::printf("    linear %.3f suave %.3f (pior %.4f) ", linear.v(25, 48).x, smooth.v(25, 48).x, worst);
    AUREA_CHECK(smooth.v(25, 48).x - linear.v(25, 48).x > 0.05f);
    // Mesma largura: a borda continua no meio (0,5 ± meio texel) nos dois.
    AUREA_CHECK_NEAR(linear.v(20, 48).x, smooth.v(20, 48).x, 0.03f);
}
