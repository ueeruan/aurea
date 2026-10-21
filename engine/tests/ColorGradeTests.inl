// =============================================================================
//  Correção de cor (0.3.0, ColorGradeEffect.cpp + color_grade.frag).
//  Incluído no fim de test_gpu.cpp (usa Scene, uniform_image, srgb_*).
// =============================================================================
namespace grade {

constexpr u32 kTemperature = 1, kExposure = 3, kContrast = 4, kSaturation = 11, kCurves = 21, kHueCurves = 22,
              kHslFirst = 23, kQualifier = 47, kQHue = 48, kQHueWidth = 49, kQShow = 56, kVignette = 65, kGrain = 68,
              kMix = 76;

ImagePixels halves(u32 w, u32 h, Vec4 left, Vec4 right) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        const Vec4 c = x < w / 2 ? left : right;
        u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
        p[0] = static_cast<u8>(c.x * 255.0f + 0.5f); p[1] = static_cast<u8>(c.y * 255.0f + 0.5f);
        p[2] = static_cast<u8>(c.z * 255.0f + 0.5f); p[3] = 255;
    }
    return px;
}

} // namespace grade

AUREA_TEST(Gpu, ColorGradeDefaultsAreNeutralAndMixFadesTheGrade) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 32);
    const LayerId id = s.image(grade::halves(64, 32, Vec4{0.8f, 0.3f, 0.2f, 1}, Vec4{0.2f, 0.5f, 0.9f, 1}), 32, 16);
    const FloatImage plain = s.render();
    EffectInstance& e = s.add_effect(id, effect_keys::kColorGrade);
    AUREA_CHECK_MSG(max_abs_diff(plain, s.render()) < 1e-4f, "padrões devem ser neutros");
    e.params[grade::kExposure].constant = ParamValue::scalar(1.0f);
    const FloatImage brighter = s.render();
    // Linear e sem teto: +1 EV dobra a luz (o realce acima de 1 continua lá).
    AUREA_CHECK_NEAR(brighter.v(10, 16).x, plain.v(10, 16).x * 2.0f, 0.03f);
    AUREA_CHECK_NEAR(brighter.v(50, 16).z, plain.v(50, 16).z * 2.0f, 0.04f);
    e.params[grade::kMix].constant = ParamValue::scalar(0.0f);
    AUREA_CHECK(max_abs_diff(plain, s.render()) < 1e-4f);
    e.params[grade::kMix].constant = ParamValue::scalar(50.0f);
    const FloatImage half = s.render();
    AUREA_CHECK_NEAR(half.v(10, 16).y, (plain.v(10, 16).y + brighter.v(10, 16).y) * 0.5f, 0.01f);
}

AUREA_TEST(Gpu, ColorGradeSaturationTemperatureAndContrastMoveTheRightWay) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 32);
    const LayerId id = s.image(grade::halves(64, 32, Vec4{0.8f, 0.3f, 0.2f, 1}, Vec4{0.5f, 0.5f, 0.5f, 1}), 32, 16);
    const FloatImage plain = s.render();
    EffectInstance& e = s.add_effect(id, effect_keys::kColorGrade);
    e.params[grade::kSaturation].constant = ParamValue::scalar(0.0f);
    const Vec4 gray = s.render().v(10, 16);
    AUREA_CHECK(std::fabs(gray.x - gray.y) < 0.01f && std::fabs(gray.y - gray.z) < 0.01f);
    e.params[grade::kSaturation].constant = ParamValue::scalar(100.0f);
    e.params[grade::kTemperature].constant = ParamValue::scalar(60.0f);
    const Vec4 warm = s.render().v(50, 16);
    AUREA_CHECK_MSG(warm.x > plain.v(50, 16).x + 0.02f && warm.z < plain.v(50, 16).z - 0.02f, "quente: mais vermelho, menos azul");
    e.params[grade::kTemperature].constant = ParamValue::scalar(0.0f);
    e.params[grade::kContrast].constant = ParamValue::scalar(60.0f);
    const FloatImage punchy = s.render();
    AUREA_CHECK_MSG(punchy.v(10, 16).x > plain.v(10, 16).x, "contraste: o claro sobe");
    AUREA_CHECK_MSG(punchy.v(10, 16).z < plain.v(10, 16).z, "contraste: o escuro desce");
}

AUREA_TEST(Gpu, ColorGradeSLog3GreyBecomesMiddleGrey) {
    // S-Log3: o cinza 18 % da cena grava o código 420/1023 ≈ 0,4106. Depois
    // da entrada log, ele volta para perto de 18 % (a curva de exibição).
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const u8 code = static_cast<u8>(std::lround(420.0 / 1023.0 * 255.0));
    const LayerId id = s.image(uniform_image(32, 32, code, code, code), 16, 16);
    EffectInstance& e = s.add_effect(id, effect_keys::kColorGrade);
    e.params[0].constant = ParamValue::scalar(1.0f);   // S-Log3
    const Vec4 v = s.render().v(16, 16);
    std::printf("\n    S-Log3 cinza 18%%: %.4f %.4f %.4f", v.x, v.y, v.z);
    AUREA_CHECK_NEAR(v.y, 0.18f, 0.04f);
    AUREA_CHECK(std::fabs(v.x - v.y) < 0.02f && std::fabs(v.z - v.y) < 0.02f);
}

AUREA_TEST(Gpu, ColorGradeCurvesHslAndQualifierTargetTheirColors) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 32);
    const LayerId id = s.image(grade::halves(64, 32, Vec4{0.85f, 0.10f, 0.10f, 1}, Vec4{0.10f, 0.20f, 0.85f, 1}), 32, 16);
    const FloatImage plain = s.render();
    EffectInstance& e = s.add_effect(id, effect_keys::kColorGrade);
    // Curvas RGB: a mestra invertida.
    e.curves[e.params[grade::kCurves].constant.ref].channel[0] = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    const FloatImage inverted = s.render();
    AUREA_CHECK_NEAR(srgb_encode(inverted.v(10, 16).y), 1.0f - srgb_encode(plain.v(10, 16).y), 0.02f);
    e.curves[e.params[grade::kCurves].constant.ref] = CurveData::identity();
    // HSL: a faixa VERMELHA sem saturação; o azul não muda.
    e.params[grade::kHslFirst + 1].constant = ParamValue::scalar(-100.0f);
    const FloatImage hsl = s.render();
    const Vec4 red = hsl.v(10, 16);
    AUREA_CHECK_MSG(std::fabs(red.x - red.y) < 0.05f, "o vermelho devia ficar cinza");
    AUREA_CHECK(max_abs_diff(hsl, plain) > 0.05f);
    AUREA_CHECK(std::fabs(hsl.v(50, 16).z - plain.v(50, 16).z) < 0.01f);
    e.params[grade::kHslFirst + 1].constant = ParamValue::scalar(0.0f);
    // Curvas de matiz: matiz × saturação sobe no azul (0,66) e só o azul satura.
    CurveData& hue = e.curves[e.params[grade::kHueCurves].constant.ref];
    hue.channel[1] = {{0.0f, 0.0f}, {0.5f, 0.5f}, {0.66f, 0.9f}, {0.8f, 0.8f}, {1.0f, 1.0f}};
    const FloatImage hueSat = s.render();
    AUREA_CHECK(std::fabs(hueSat.v(10, 16).x - plain.v(10, 16).x) < 0.02f);
    AUREA_CHECK_MSG(hueSat.v(50, 16).x < plain.v(50, 16).x + 1e-3f, "o azul devia saturar (menos vermelho)");
    hue = CurveData::identity();
    // Qualificador: matiz 0 ± 30°, mostrando a seleção — branco no vermelho, preto no azul.
    e.params[grade::kQualifier].constant = ParamValue::boolean(true);
    e.params[grade::kQHue].constant = ParamValue::scalar(0.0f);
    e.params[grade::kQHueWidth].constant = ParamValue::scalar(30.0f);
    e.params[grade::kQShow].constant = ParamValue::boolean(true);
    const FloatImage mask = s.render();
    AUREA_CHECK_MSG(mask.v(10, 16).x > 0.9f, "o vermelho está na seleção");
    AUREA_CHECK_MSG(mask.v(50, 16).x < 0.05f, "o azul está fora");
}

AUREA_TEST(Gpu, ColorGradeVignetteGrainAndEveryLookRender) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 64);
    const LayerId id = s.image(uniform_image(96, 64, 128, 128, 128), 48, 32);
    const FloatImage plain = s.render();
    EffectInstance& e = s.add_effect(id, effect_keys::kColorGrade);
    e.params[grade::kVignette].constant = ParamValue::scalar(-80.0f);
    const FloatImage vig = s.render();
    AUREA_CHECK_MSG(vig.v(2, 2).x < plain.v(2, 2).x - 0.05f, "a vinheta escurece o canto");
    AUREA_CHECK(std::fabs(vig.v(48, 32).x - plain.v(48, 32).x) < 0.01f);
    e.params[grade::kVignette].constant = ParamValue::scalar(0.0f);
    e.params[grade::kGrain].constant = ParamValue::scalar(60.0f);
    const FloatImage g0 = s.render(FrameIndex{0}), g0b = s.render(FrameIndex{0}), g3 = s.render(FrameIndex{3});
    AUREA_CHECK(max_abs_diff(g0, g0b) < 1e-5f);
    AUREA_CHECK_MSG(max_abs_diff(g0, g3) > 0.01f, "o grão anda com o tempo");
    e.params[grade::kGrain].constant = ParamValue::scalar(0.0f);
    // Todos os looks (Filtros do modo de edição) mudam o quadro.
    const Effect* fx = gpu().effects.find(e.type);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    const auto looks = fx->presets();
    AUREA_CHECK(looks.size() >= 10);
    Scene c(64, 32);
    const LayerId cid = c.image(grade::halves(64, 32, Vec4{0.8f, 0.4f, 0.25f, 1}, Vec4{0.2f, 0.45f, 0.7f, 1}), 32, 16);
    const FloatImage base = c.render();
    for (const EffectPreset& look : looks) {
        Layer* l = c.comp->layer(cid);
        l->effects.clear();
        EffectInstance& g = c.add_effect(cid, effect_keys::kColorGrade);
        for (const EffectPresetValue& v : look.values) g.params[v.param].constant.v[v.component] = v.value;
        AUREA_CHECK_MSG(max_abs_diff(base, c.render()) > 0.01f, look.id);
    }
}

AUREA_TEST(Gpu, ColorGradeReportsFullHdGpuCost) {
    AUREA_REQUIRE_GPU();
    Scene s(1920, 1080);
    const LayerId id = s.image(uniform_image(1920, 1080, 120, 90, 60), 960, 540);
    EffectInstance& e = s.add_effect(id, effect_keys::kColorGrade);
    e.params[grade::kContrast].constant = ParamValue::scalar(20.0f);
    e.params[grade::kHslFirst + 4].constant = ParamValue::scalar(30.0f);
    e.params[grade::kQualifier].constant = ParamValue::boolean(true);
    e.params[grade::kGrain].constant = ParamValue::scalar(20.0f);
    e.params[64].constant = ParamValue::scalar(30.0f);   // nitidez
    (void)s.render(FrameIndex{0}, 1, true);
    Gpu& g = gpu();
    RenderSettings rs;
    rs.gpuTimers = true;
    rs.finalQuality = true;
    OffscreenTarget off{g.target(1920, 1080), 1920, 1080};
    f32 best = 1e9f;
    static u64 frame = 960000;
    for (int r = 0; r < 6; ++r) {
        FrameSnapshot snap; FrameStats stats; RenderTimings timings;
        g.renderer.prepare(*s.comp, s.project, FrameIndex{r}, &s.media, &Scene::lookup, &s, rs, ++frame, 0,
                           DecodeMode::Still, 1.0f, snap);
        AUREA_CHECK(g.renderer.render(snap, rs, &off, stats, timings).ok());
        g.backend.wait_idle();
        if (timings.gpuMeasured && timings.gpuEffectsMs > 0.0f) best = std::min(best, timings.gpuEffectsMs);
    }
    std::printf("\n    correcao de cor 1080p (todas as etapas): GPU dos efeitos %.2f ms\n", best);
}

AUREA_TEST(Gpu, ColorGradeEngineRolesLooksResetScopesAndThumbnails) {
    // EngineColor.cpp: o que os painéis Filtros e Ajustar pedem ao motor.
    AUREA_REQUIRE_GPU();
    SyntheticConfig cfg;
    cfg.width = 96; cfg.height = 54; cfg.frameCount = 60;
    cfg.pattern = SyntheticPattern::Quadrants;
    SyntheticFactory factory(cfg);
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.mediaFactory = &factory;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(96, 54, 30, "cor").ok());
    VideoImport v;
    v.sourcePath = "quadrantes";
    v.displayName = "video";
    const auto id = e.import_video(v);
    AUREA_CHECK(id.ok());
    if (!id.ok()) { e.shutdown(); return; }
    // Papéis: um Ajustar e um Filtro, o Filtro antes; pedir de novo devolve o mesmo.
    const u32 adjust = e.ensure_color_grade(*id, 2);
    const u32 filter = e.ensure_color_grade(*id, 1);
    AUREA_CHECK(adjust != kInvalidIndex && filter != kInvalidIndex && adjust != filter);
    AUREA_CHECK_EQ(e.ensure_color_grade(*id, 2), adjust);
    AUREA_CHECK_EQ(e.ensure_color_grade(*id, 1), filter);
    AUREA_CHECK_EQ(e.ensure_color_grade(*id, 3), kInvalidIndex);
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* l = comp->layer(LayerId::unpack(*id));
    auto index_of = [&](u32 effect) { return l->effect_index(EffectId{effect, 0}); };
    AUREA_CHECK(index_of(filter) < index_of(adjust));
    auto param = [&](u32 effect, u32 p) { return l->find_effect(EffectId{effect, 0})->params[p].constant.v[0]; };
    // Look 3 (Vintage: desbotado 35), depois o 4 (P&B: saturação 0) — o primeiro some.
    AUREA_CHECK(e.apply_color_look(*id, filter, 3));
    AUREA_CHECK_NEAR(param(filter, 60), 35.0f, 1e-4f);
    AUREA_CHECK_NEAR(param(filter, 78), 3.0f, 1e-4f);
    AUREA_CHECK(e.apply_color_look(*id, filter, 4));
    AUREA_CHECK_NEAR(param(filter, 60), 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(param(filter, 11), 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(param(filter, 77), 1.0f, 1e-4f);   // o papel fica
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    l = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK_NEAR(param(filter, 78), 3.0f, 1e-4f);
    AUREA_CHECK(!e.apply_color_look(*id, filter, 999));
    // Redefinir: tudo volta, menos papel e intensidade.
    l->find_effect(EffectId{filter, 0})->params[76].constant.v[0] = 40.0f;
    AUREA_CHECK(e.reset_color_grade(*id, filter));
    AUREA_CHECK_NEAR(param(filter, 60), 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(param(filter, 76), 40.0f, 1e-4f);
    AUREA_CHECK_NEAR(param(filter, 77), 1.0f, 1e-4f);
    // Scopes: o histograma de luz conta cada pixel uma vez.
    Engine::Scopes scopes;
    const Status sc = e.compute_scopes(96, scopes);
    AUREA_CHECK_MSG(sc.ok(), sc.message().data());
    if (sc.ok()) {
        u64 luma = 0;
        for (u32 i = 0; i < 256; ++i) luma += scopes.histogram[768 + i];
        AUREA_CHECK_EQ(luma, static_cast<u64>(scopes.width) * scopes.height);
        AUREA_CHECK_EQ(scopes.waveform.size(), static_cast<usize>(scopes.columns) * 128);
        AUREA_CHECK_EQ(scopes.vectorscope.size(), static_cast<usize>(128 * 128));
    }
    // Miniaturas: o quadro e um por look, do mesmo tamanho; os looks mudam a imagem.
    std::vector<u8> thumbs;
    u32 tw = 0, th = 0, count = 0;
    const Status ls = e.render_color_looks(64, thumbs, tw, th, count);
    AUREA_CHECK_MSG(ls.ok(), ls.message().data());
    if (ls.ok()) {
        AUREA_CHECK(count >= 11);
        const usize one = static_cast<usize>(tw) * th * 4;
        AUREA_CHECK_EQ(thumbs.size(), one * count);
        u32 changed = 0;
        for (u32 k = 1; k < count; ++k) {
            u32 diff = 0;
            for (usize b = 0; b < one; ++b) diff = std::max<u32>(diff, static_cast<u32>(std::abs(thumbs[k * one + b] - thumbs[b])));
            if (diff > 6) ++changed;
        }
        AUREA_CHECK_EQ(changed, count - 1);
    }
    e.shutdown();
}
