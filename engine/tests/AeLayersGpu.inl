// =============================================================================
//  Aurea / tests / AeLayersGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  AE layers (2026-11-12): a Qualidade da camada (o Quality switch do After
//  Effects) nos pixels. Arame = só o contorno da caixa na PRÉVIA; Rascunho =
//  a camada continua no lugar (densidade menor, vizinho mais próximo); o
//  export (finalQuality) sai sempre em Melhor; a amostragem bicúbica desenha
//  a mesma imagem (só o filtro muda).
// =============================================================================

AUREA_TEST(Gpu, AeLayerWireframeDrawsOnlyTheBoxOutlineInPreviewAndExportStaysBest) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    Scene s(W, H);
    const LayerId id = s.solid(32, 32, Vec4{1, 0, 0, 1}, W * 0.5f, H * 0.5f);   // cobre [16, 48)
    const Vec4 best = s.render().v(32, 32);
    AUREA_CHECK(best.x > 0.9f && best.y < 0.05f && best.z < 0.05f);
    s.comp->layer(id)->quality = 2;
    const FloatImage wire = s.render();
    // Dentro da caixa: nada da camada (o fundo); na borda: o traço do arame.
    const Vec4 center = wire.v(32, 32);
    AUREA_CHECK(center.x < 0.05f);
    const Vec4 edge = wire.v(17, 32);
    std::printf("    arame: centro (%.2f %.2f %.2f), borda (%.2f %.2f %.2f) ", center.x, center.y, center.z, edge.x, edge.y, edge.z);
    AUREA_CHECK(edge.z > 0.5f && edge.x < 0.5f);
    // Fora da caixa: fundo.
    const Vec4 outside = wire.v(4, 4);
    AUREA_CHECK(outside.x + outside.y + outside.z < 0.05f);
    // O export ignora a qualidade da camada.
    const Vec4 final = s.render(FrameIndex{0}, 1, true).v(32, 32);
    AUREA_CHECK(final.x > 0.9f && final.y < 0.05f && final.z < 0.05f);
}

AUREA_TEST(Gpu, AeLayerDraftKeepsTheLayerInPlaceAndBicubicDrawsTheSameImage) {
    AUREA_REQUIRE_GPU();
    const u32 W = 64, H = 64;
    const f32 tol = 0.12f;
    auto shot = [&](u8 quality, u8 sampling, bool finalQuality) {
        Scene s(W, H);
        const LayerId id = s.image(mask_test_gradient(16, 16), W * 0.5f, H * 0.5f, 3.0f);   // ampliada 3×: [8, 56)
        s.comp->layer(id)->quality = quality;
        s.comp->layer(id)->sampling = sampling;
        return s.render(FrameIndex{0}, 1, finalQuality);
    };
    const FloatImage best = shot(0, 0, false), draft = shot(1, 0, false), cubic = shot(0, 1, false), cubicFinal = shot(0, 1, true);
    f32 worstDraft = 0.0f, worstCubic = 0.0f;
    for (const u32 y : {16u, 32u, 48u})
        for (const u32 x : {16u, 32u, 48u}) {
            const Vec4 b = best.v(x, y), d = draft.v(x, y), c = cubic.v(x, y), f = cubicFinal.v(x, y);
            worstDraft = std::max({worstDraft, std::fabs(d.x - b.x), std::fabs(d.y - b.y), std::fabs(d.z - b.z)});
            worstCubic = std::max({worstCubic, std::fabs(c.x - b.x), std::fabs(c.y - b.y), std::fabs(c.z - b.z)});
            AUREA_CHECK(std::fabs(c.x - f.x) < 0.02f && std::fabs(c.y - f.y) < 0.02f && std::fabs(c.z - f.z) < 0.02f);
            AUREA_CHECK(c.w > 0.98f && d.w > 0.98f);
        }
    std::printf("    rascunho × melhor: %.3f; bicúbica × bilinear: %.3f ", worstDraft, worstCubic);
    AUREA_CHECK(worstDraft < 0.2f);    // mesma imagem, um degrau de densidade abaixo
    AUREA_CHECK(worstCubic < tol);     // mesmo desenho, filtro diferente
    // Fora da camada: fundo nos três.
    for (const FloatImage* img : {&best, &draft, &cubic}) {
        const Vec4 o = img->v(2, 2);
        AUREA_CHECK(o.x + o.y + o.z < 0.05f);
    }
}
