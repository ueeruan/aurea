// Beta "nem vejo a prévia" (aparelho no OpenGL ES): um passe novo cujo
// pipeline o driver recusa NÃO pode derrubar o quadro. O gancho de teste da
// ShaderLibrary recusa todo pipeline de um shader; o quadro tem de sair, com
// a outra camada no lugar e o efeito pulado (a entrada passa igual).
namespace {
f32 gles_fallback_difference(const FloatImage& a, const FloatImage& b) {
    f32 largest = 0;
    AUREA_CHECK_EQ(a.px.size(), b.px.size());
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i)
        largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}
f32 gles_fallback_alpha_sum(const FloatImage& img) {
    f32 s = 0;
    for (usize i = 3; i < img.px.size(); i += 4) s += img.px[i];
    return s;
}
struct GlesFallbackCase {
    const char* effect;   ///< nullptr = camada em BlendMode::Mask (blend.frag)
    ShaderId shader;
};
}

AUREA_TEST(GlesFallbackGpu, RejectedNewPipelineSkipsOnlyItsPassAndTheFrameStillRenders) {
    AUREA_REQUIRE_GPU();
    const GlesFallbackCase cases[] = {
        {"aurea.transition.disintegrate", ShaderId::effects_disintegrate_vert},
        {"aurea.transition.disintegrate", ShaderId::effects_disintegrate_frag},
        {"aurea.transition.disintegrate", ShaderId::effects_disintegrate_compose_frag},
        {effect_keys::kBallGrid, ShaderId::effects_ball_grid_vert},
        {effect_keys::kBallGrid, ShaderId::effects_ball_grid_frag},
        {effect_keys::kGlow, ShaderId::effects_glow_octave_prefilter_frag},
        {effect_keys::kGlow, ShaderId::effects_glow_octave_down_frag},
        {effect_keys::kGlow, ShaderId::effects_glow_octave_up_frag},
        {effect_keys::kGlow, ShaderId::effects_glow_octave_composite_frag},
        {nullptr, ShaderId::composite_blend_frag},
    };
    ShaderLibrary& lib = gpu().renderer.shaders();
    for (const GlesFallbackCase& c : cases) {
        std::printf("\n    %s sem %s", c.effect ? c.effect : "Mascara", kShaderNames[static_cast<u32>(c.shader)]);
        Scene scene(192, 128);
        scene.comp->set_transparent_background(true);
        // Sólido à esquerda (testemunha) e a imagem com o efeito à direita.
        scene.solid(40, 40, Vec4{0.2f, 0.6f, 0.9f, 1.0f}, 30, 64);
        const LayerId layer = scene.image(reference_image(96, 64), 128, 64);
        const FloatImage baseline = scene.render();
        if (c.effect) {
            auto& fx = scene.add_effect(layer, c.effect);
            const Effect* effect = gpu().effects.find(fx.type);
            AUREA_CHECK(effect != nullptr);
            if (!effect) continue;
            std::vector<ParamValue> v(fx.params.size());
            for (usize i = 0; i < v.size(); ++i) v[i] = fx.params[i].constant;
            if (effect->demo_values(fx, v)) for (usize i = 0; i < v.size(); ++i) fx.params[i].constant = v[i];
        } else {
            scene.comp->layer(layer)->blendMode = BlendMode::Mask;
        }
        const FloatImage working = scene.render();
        // O caso testa algo: com o pipeline, o efeito muda o quadro.
        AUREA_CHECK(gles_fallback_difference(baseline, working) > 0.01f);

        lib.set_test_failing_shader(c.shader);
        const FloatImage skipped = scene.render();   // AUREA_CHECK_MSG(render ok) lá dentro
        lib.set_test_failing_shader(ShaderId::Count);
        // Não ficou preto/vazio e a testemunha continua lá.
        AUREA_CHECK(gles_fallback_alpha_sum(skipped) > 100.0f);
        AUREA_CHECK(near4(skipped.v(30, 64), baseline.v(30, 64), 0.002f));
        if (c.effect) {
            // Efeito pulado = a camada como sem ele.
            AUREA_CHECK(gles_fallback_difference(baseline, skipped) < 0.002f);
        }
        for (const f32 value : skipped.px) AUREA_CHECK(std::isfinite(value));
        // O gancho não envenena o cache: o pipeline volta no quadro seguinte.
        AUREA_CHECK(gles_fallback_difference(working, scene.render()) < 1e-5f);
    }
}
