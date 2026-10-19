// Progresso do desenho (DrawingProgressEffect.cpp) na GPU real. Na camada
// VETORIAL o corte é na geometria: 50 % sequencial desenha o primeiro traço
// inteiro e nada do segundo, simultâneo a metade de cada, invertido o fim de
// cada; 0 % não desenha nada e 100 % é o quadro sem o efeito. Na imagem é a
// varredura: a tinta escura chega antes do papel claro, Inverter e Ângulo
// mudam o lado, 0 % = alfa 0, 100 % = a camada exata. E a prévia do catálogo.
namespace {
LayerId dp_vector_layer(Scene& s, std::vector<VectorGroup> groups) {
    const LayerId id = s.comp->add_layer(LayerKind::Shape, "vetor");
    Layer* l = s.comp->layer(id);
    const f32 w = static_cast<f32>(s.comp->width()), h = static_cast<f32>(s.comp->height());
    l->shape.shapeType = kShapeVector;
    l->shape.bounds = Rect{0.0f, 0.0f, w, h};
    l->shape.vector.groups = std::move(groups);
    l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    return id;
}
VectorGroup dp_line(Vec2 a, Vec2 b, f32 width) {
    VectorGroup g;
    VectorPath p;
    p.path.closed = false;
    p.path.v = {BezierVertex{a, {}, {}}, BezierVertex{b, {}, {}}};
    g.paths.push_back(p);
    g.fill.enabled = false;
    g.stroke.enabled = true;
    g.stroke.width = width;
    g.stroke.paint.color = Vec4{1, 1, 1, 1};
    return g;
}
void dp_set(LayerId id, Scene& s, u32 param, f32 v) {
    Layer* l = s.comp->layer(id);
    AUREA_CHECK(l && !l->effects.empty() && param < l->effects.back().params.size());
    if (l && !l->effects.empty() && param < l->effects.back().params.size()) l->effects.back().params[param].constant.v[0] = v;
}
/// Pixels acesos (alfa > 0,5) da linha y em [x0, x1).
u32 dp_lit(const FloatImage& img, u32 y, u32 x0, u32 x1) {
    u32 n = 0;
    for (u32 x = x0; x < x1 && x < img.width; ++x) n += img.v(x, y).w > 0.5f ? 1u : 0u;
    return n;
}
f32 dp_diff(const FloatImage& a, const FloatImage& b) {
    f32 largest = a.px.size() == b.px.size() ? 0.0f : 1.0f;
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i) largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}
f32 dp_alpha_sum(const FloatImage& img) {
    f32 s = 0.0f;
    for (u32 y = 0; y < img.height; ++y) for (u32 x = 0; x < img.width; ++x) s += img.v(x, y).w;
    return s;
}
enum : u32 { kDpProgress = 0, kDpOrder, kDpReverse, kDpSoftness, kDpAngle };
} // namespace

AUREA_TEST(DrawingProgressGpu, VectorLayerDrawsExactlyThePrefixOfTheStrokes) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 128);
    s.comp->set_transparent_background(true);
    // Dois traços de 200 px (contorno 8): o de cima é o primeiro do desenho.
    const LayerId id = dp_vector_layer(s, {dp_line(Vec2{28, 40}, Vec2{228, 40}, 8.0f), dp_line(Vec2{28, 88}, Vec2{228, 88}, 8.0f)});
    const FloatImage original = s.render();
    std::printf("    sem efeito: %u e %u px acesos nas linhas\n", dp_lit(original, 40, 0, 256), dp_lit(original, 88, 0, 256));
    AUREA_CHECK(dp_lit(original, 40, 0, 256) >= 199 && dp_lit(original, 40, 0, 256) <= 201);
    s.add_effect(id, effect_keys::kDrawingProgress);
    // 100 % (o padrão): o mesmo quadro.
    AUREA_CHECK(dp_diff(s.render(), original) < 1e-6f);
    // 0 %: nada.
    dp_set(id, s, kDpProgress, 0.0f);
    AUREA_CHECK(dp_alpha_sum(s.render()) < 1e-4f);
    // Sequencial 50 %: o primeiro traço inteiro, o segundo nada.
    dp_set(id, s, kDpProgress, 50.0f);
    const FloatImage seq = s.render();
    std::printf("    sequencial 50%%: %u e %u px\n", dp_lit(seq, 40, 0, 256), dp_lit(seq, 88, 0, 256));
    AUREA_CHECK(dp_lit(seq, 40, 0, 256) >= 199);
    AUREA_CHECK_EQ(dp_lit(seq, 88, 0, 256), 0u);
    // O traço desenhado é o mesmo pixel do quadro sem efeito.
    AUREA_CHECK(std::fabs(seq.v(128, 40).w - original.v(128, 40).w) < 1e-3f);
    // Simultâneo 50 %: a metade esquerda de cada um.
    dp_set(id, s, kDpOrder, 1.0f);
    const FloatImage sim = s.render();
    std::printf("    simultaneo 50%%: %u e %u px\n", dp_lit(sim, 40, 0, 256), dp_lit(sim, 88, 0, 256));
    for (const u32 y : {40u, 88u}) {
        AUREA_CHECK(dp_lit(sim, y, 0, 128) >= 99 && dp_lit(sim, y, 0, 128) <= 101);
        AUREA_CHECK_EQ(dp_lit(sim, y, 129, 256), 0u);
    }
    // Invertido: o fim de cada um.
    dp_set(id, s, kDpReverse, 1.0f);
    const FloatImage rev = s.render();
    for (const u32 y : {40u, 88u}) {
        AUREA_CHECK_EQ(dp_lit(rev, y, 0, 127), 0u);
        AUREA_CHECK(dp_lit(rev, y, 128, 256) >= 99 && dp_lit(rev, y, 128, 256) <= 101);
    }
    // Invertido e sequencial: o último traço primeiro.
    dp_set(id, s, kDpOrder, 0.0f);
    const FloatImage revSeq = s.render();
    AUREA_CHECK_EQ(dp_lit(revSeq, 40, 0, 256), 0u);
    AUREA_CHECK(dp_lit(revSeq, 88, 0, 256) >= 199);
    // Animado: keyframes no Progresso (0 → 100 em 10 quadros) valem na malha.
    dp_set(id, s, kDpReverse, 0.0f);
    {
        Layer* l = s.comp->layer(id);
        Track& tr = l->tracks.get_or_create(TrackProperty::EffectParam, l->effects.back().id, param_track_key(kDpProgress, 0));
        tr.set(FrameIndex{0}, 0.0f);
        tr.set(FrameIndex{10}, 100.0f);
    }
    const FloatImage at5 = s.render(FrameIndex{5});
    AUREA_CHECK(dp_lit(at5, 40, 0, 256) >= 199);
    AUREA_CHECK_EQ(dp_lit(at5, 88, 0, 256), 0u);
    AUREA_CHECK(dp_diff(s.render(FrameIndex{10}), original) < 1e-6f);
}

AUREA_TEST(DrawingProgressGpu, ImageRevealsInASweepWithTheInkFirst) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 64);
    s.comp->set_transparent_background(true);
    // Metade de cima preta (a tinta), metade de baixo branca (o papel), opaca.
    ImagePixels px = uniform_image(128, 64, 255, 255, 255);
    for (u32 y = 0; y < 32; ++y)
        for (u32 x = 0; x < 128; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * 128 + x) * 4];
            p[0] = p[1] = p[2] = 0;
        }
    const LayerId id = s.image(px, 64, 32);
    const FloatImage original = s.render();
    s.add_effect(id, effect_keys::kDrawingProgress);
    AUREA_CHECK(dp_diff(s.render(), original) < 1e-6f);
    dp_set(id, s, kDpProgress, 0.0f);
    AUREA_CHECK(dp_alpha_sum(s.render()) < 1e-4f);
    // 50 %, ponta dura: a esquerda inteira, no meio só a tinta, a direita nada.
    dp_set(id, s, kDpProgress, 50.0f);
    dp_set(id, s, kDpSoftness, 0.0f);
    const FloatImage half = s.render();
    std::printf("    varredura 50%%: x12 %.2f/%.2f  x64 %.2f/%.2f  x116 %.2f/%.2f (tinta/papel)\n", half.v(12, 16).w,
                half.v(12, 48).w, half.v(64, 16).w, half.v(64, 48).w, half.v(116, 16).w, half.v(116, 48).w);
    AUREA_CHECK(half.v(12, 16).w > 0.99f && half.v(12, 48).w > 0.99f);
    AUREA_CHECK(half.v(64, 16).w > 0.99f);
    AUREA_CHECK(half.v(64, 48).w < 0.01f);
    AUREA_CHECK(half.v(116, 16).w < 0.01f && half.v(116, 48).w < 0.01f);
    AUREA_CHECK(std::fabs(half.v(12, 48).x - original.v(12, 48).x) < 1e-3f);
    // Inverter: começa do outro lado.
    dp_set(id, s, kDpReverse, 1.0f);
    const FloatImage rev = s.render();
    AUREA_CHECK(rev.v(12, 16).w < 0.01f);
    AUREA_CHECK(rev.v(63, 16).w > 0.99f && rev.v(63, 48).w < 0.01f);
    AUREA_CHECK(rev.v(116, 16).w > 0.99f && rev.v(116, 48).w > 0.99f);
    // Ângulo 90°: de cima para baixo (a metade de cima aparece, a de baixo não).
    dp_set(id, s, kDpReverse, 0.0f);
    dp_set(id, s, kDpAngle, 90.0f);
    const FloatImage down = s.render();
    AUREA_CHECK(down.v(100, 10).w > 0.99f && down.v(100, 50).w < 0.01f);
    // Ponta suave: a borda vira uma rampa (pixels de meio alfa na linha).
    dp_set(id, s, kDpAngle, 0.0f);
    dp_set(id, s, kDpSoftness, 30.0f);
    const FloatImage soft = s.render();
    u32 partial = 0;
    for (u32 x = 0; x < 128; ++x) { const f32 a = soft.v(x, 16).w; partial += a > 0.05f && a < 0.95f ? 1u : 0u; }
    std::printf("    ponta suave 30%%: %u px de meio alfa\n", partial);
    AUREA_CHECK(partial >= 20u);
}

AUREA_TEST(DrawingProgressGpu, CatalogPreviewShowsAPartlyDrawnCard) {
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    std::vector<u8> rgba;
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kDrawingProgress), 160, 100, rgba).ok());
    AUREA_CHECK_EQ(rgba.size(), static_cast<usize>(160) * 100 * 4);
    if (rgba.size() != static_cast<usize>(160) * 100 * 4) return;
    // 60 %: a esquerda da cartela já está desenhada, a direita ainda não.
    const u8 left = rgba[(static_cast<usize>(50) * 160 + 8) * 4 + 3];
    const u8 right = rgba[(static_cast<usize>(50) * 160 + 152) * 4 + 3];
    std::printf("    previa: alfa esquerda %u, direita %u\n", left, right);
    AUREA_CHECK(left > 250);
    AUREA_CHECK(right < 5);
}
