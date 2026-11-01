// Pedidos de usuários da 0.4.5 (glitch): Glitchify completo (controles do
// plugin original, build/reference/glitchify/SPEC.md), Hyper Glitch, Block
// Glitch (estilo DaVinci) e Shift Channels (o do AE).

namespace glitch045 {

ImagePixels g045_pattern(u32 w, u32 h) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        const usize at = (static_cast<usize>(y) * w + x) * 4;
        px.rgba[at] = static_cast<u8>((x * 73 + y * 19) % 256);
        px.rgba[at + 1] = static_cast<u8>((x * 11 + y * 47) % 256);
        px.rgba[at + 2] = static_cast<u8>((x * 31 + y * 7) % 256);
    }
    return px;
}

f32 g045_diff(const FloatImage& a, const FloatImage& b) {
    f32 m = 0;
    for (usize i = 0; i < a.px.size() && i < b.px.size(); ++i) m = std::max(m, std::fabs(a.px[i] - b.px[i]));
    return m;
}

bool g045_finite(const FloatImage& a) {
    for (f32 v : a.px) if (!std::isfinite(v)) return false;
    return true;
}

u32 g045_param(const char* key, const char* id) {
    const ParameterRegistry* reg = gpu().effects.params(effect_type_id(key));
    if (!reg) return kInvalidIndex;
    for (u32 i = 0; i < reg->count(); ++i) if (std::strcmp(reg->at(i).id, id) == 0) return i;
    return kInvalidIndex;
}

} // namespace glitch045

// Os controles novos do Glitchify ficam DEPOIS dos antigos (projetos salvos
// continuam lendo os mesmos índices) e o padrão de cada um não muda o quadro:
// com todos eles nos valores neutros explícitos o quadro é o mesmo.
AUREA_TEST(Glitch045Gpu, GlitchifyNewControlsAreAppendedWithNeutralDefaults) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    AUREA_CHECK_EQ(g045_param(effect_keys::kGlitchify, "streak"), 34u);
    AUREA_CHECK_EQ(g045_param(effect_keys::kGlitchify, "transition_channel"), 35u);
    AUREA_CHECK_EQ(g045_param(effect_keys::kGlitchify, "wiggle_steps"), 59u);
    Scene s(96, 64);
    const auto id = s.image(g045_pattern(96, 64), 48, 32);
    auto& fx = s.add_effect(id, effect_keys::kGlitchify);
    fx.params[50].constant = ParamValue::scalar(24);
    fx.params[51].constant = ParamValue::scalar(16);
    const FloatImage base = s.render(FrameIndex{12});
    // Mexer e voltar ao padrão: o mesmo quadro.
    fx.params[44].constant = ParamValue::scalar(60);   // fatia
    fx.params[48].constant = ParamValue::scalar(80);   // blocos trocados
    const FloatImage changed = s.render(FrameIndex{12});
    fx.params[44].constant = ParamValue::scalar(0);
    fx.params[48].constant = ParamValue::scalar(0);
    AUREA_CHECK(g045_diff(base, changed) > 0.02f);
    AUREA_CHECK(g045_diff(base, s.render(FrameIndex{12})) < 0.001f);
}

// Cada módulo novo muda o quadro, sem NaN, e o resultado é o mesmo ao voltar
// ao mesmo instante (determinístico pela semente); outra semente muda.
AUREA_TEST(Glitch045Gpu, GlitchifyNewModulesChangeTheFrameDeterministically) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(128, 96);
    const auto id = s.image(g045_pattern(128, 96), 64, 48);
    auto& fx = s.add_effect(id, effect_keys::kGlitchify);
    // Neutro: só os módulos novos atuam.
    for (u32 p : {1u, 2u, 3u, 9u, 13u, 14u, 15u, 16u}) fx.params[p].constant.v[0] = 0;
    fx.params[50].constant = ParamValue::scalar(24);   // bloco trocado menor que a camada
    fx.params[51].constant = ParamValue::scalar(16);
    const FloatImage base = s.render(FrameIndex{20});
    struct Probe { u32 index; f32 value; const char* name; };
    const Probe probes[] = {
        {44, 70, "fatia"}, {48, 90, "blocos trocados"}, {41, 20, "corrupcao"}, {53, 90, "compressao"},
        {54, 80, "pontilhado"}, {58, 1, "mostrar ruido"},
    };
    for (const Probe& probe : probes) {
        const ParamValue before = fx.params[probe.index].constant;
        fx.params[probe.index].constant = ParamValue::scalar(probe.value);
        if (probe.index == 41) fx.params[43].constant = ParamValue::scalar(100);   // opacidade da corrupção
        const FloatImage a = s.render(FrameIndex{20});
        (void)s.render(FrameIndex{40});
        const FloatImage again = s.render(FrameIndex{20});
        const f32 d = g045_diff(base, a);
        std::printf("\n    glitchify %s: dif %.3f ", probe.name, d);
        AUREA_CHECK(d > 0.02f);
        AUREA_CHECK(g045_finite(a));
        AUREA_CHECK(g045_diff(a, again) < 0.001f);
        fx.params[probe.index].constant = before;
        if (probe.index == 41) fx.params[43].constant = ParamValue::scalar(25);
    }
    // Separação de UM componente (verde-magenta) e escala por luma.
    fx.params[3].constant = ParamValue::scalar(12);
    fx.params[38].constant = ParamValue::scalar(2);
    const FloatImage green = s.render(FrameIndex{20});
    fx.params[38].constant = ParamValue::scalar(0);
    const FloatImage rgb = s.render(FrameIndex{20});
    AUREA_CHECK(g045_diff(green, rgb) > 0.02f);
    fx.params[3].constant = ParamValue::scalar(0);
    fx.params[11].constant = ParamValue::scalar(25);
    const FloatImage uniformScale = s.render(FrameIndex{20});
    fx.params[40].constant = ParamValue::scalar(1);
    const FloatImage lumaScale = s.render(FrameIndex{20});
    AUREA_CHECK(g045_diff(uniformScale, lumaScale) > 0.02f);
    fx.params[11].constant = ParamValue::scalar(0);
    fx.params[40].constant = ParamValue::scalar(0);
    // Semente: blocos trocados com outra semente saem em outro lugar.
    fx.params[48].constant = ParamValue::scalar(90);
    const FloatImage seedA = s.render(FrameIndex{20});
    fx.params[5].constant = ParamValue::scalar(321);
    const FloatImage seedB = s.render(FrameIndex{20});
    AUREA_CHECK(g045_diff(seedA, seedB) > 0.02f);
}

// Transição pelo canal (claridade): metade do caminho some os pixels escuros
// e mantém os claros; em 100 % tudo some.
AUREA_TEST(Glitch045Gpu, GlitchifyTransitionByChannelRemovesDarkPixelsFirst) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(64, 32);
    s.comp->set_transparent_background(true);
    ImagePixels px = uniform_image(64, 32, 0, 0, 0);
    for (u32 y = 0; y < 32; ++y) for (u32 x = 32; x < 64; ++x)
        for (u32 c = 0; c < 3; ++c) px.rgba[(static_cast<usize>(y) * 64 + x) * 4 + c] = 255;
    const auto id = s.image(std::move(px), 32, 16);
    auto& fx = s.add_effect(id, effect_keys::kGlitchify);
    for (u32 p : {1u, 2u, 3u, 9u, 13u, 14u, 15u, 16u}) fx.params[p].constant.v[0] = 0;
    fx.params[35].constant = ParamValue::scalar(6);    // claridade
    fx.params[29].constant = ParamValue::scalar(50);
    fx.params[30].constant = ParamValue::scalar(10);
    const FloatImage half = s.render(FrameIndex{3});
    AUREA_CHECK(half.v(8, 16).w < 0.01f);
    AUREA_CHECK(half.v(56, 16).w > 0.99f);
    fx.params[29].constant = ParamValue::scalar(100);
    const FloatImage gone = s.render(FrameIndex{3});
    for (usize i = 3; i < gone.px.size(); i += 4) AUREA_CHECK_NEAR(gone.px[i], 0.0f, 0.001f);
}

// Hyper Glitch: o padrão muda o quadro; em segundos (24 e 60 qps dão o mesmo
// quadro no mesmo instante); determinístico; intensidade 0 = intacto; a
// semente e a intensidade (animáveis) mudam o resultado.
AUREA_TEST(Glitch045Gpu, HyperGlitchIsHeavyDeterministicAndSeeded) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    std::vector<FloatImage> atOneSecond;
    for (int fps : {24, 60}) {
        Scene s(128, 96, fps);
        const auto id = s.image(g045_pattern(128, 96), 64, 48);
        const FloatImage plain = s.render(FrameIndex{fps});
        auto& fx = s.add_effect(id, effect_keys::kHyperGlitch);
        const FloatImage a = s.render(FrameIndex{fps});
        (void)s.render(FrameIndex{fps * 2});
        const FloatImage again = s.render(FrameIndex{fps});
        AUREA_CHECK(g045_diff(plain, a) > 0.1f);
        AUREA_CHECK(g045_finite(a));
        AUREA_CHECK(g045_diff(a, again) < 0.001f);
        atOneSecond.push_back(a);
        fx.params[1].constant = ParamValue::scalar(4242);
        AUREA_CHECK(g045_diff(a, s.render(FrameIndex{fps})) > 0.02f);
        fx.params[0].constant = ParamValue::scalar(0);
        AUREA_CHECK(g045_diff(plain, s.render(FrameIndex{fps})) < 0.001f);
    }
    AUREA_CHECK(g045_diff(atOneSecond[0], atOneSecond[1]) < 0.001f);
}

// Block Glitch: blocos inteiros deslocados (o padrão muda o quadro), a
// direção vertical dá outro resultado, quantidade 0 = intacto, determinístico.
AUREA_TEST(Glitch045Gpu, BlockGlitchDisplacesBlocksDeterministically) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(160, 96);
    const auto id = s.image(g045_pattern(160, 96), 80, 48);
    const FloatImage plain = s.render(FrameIndex{10});
    auto& fx = s.add_effect(id, effect_keys::kBlockGlitch);
    fx.params[0].constant = ParamValue::scalar(80);
    const FloatImage h = s.render(FrameIndex{10});
    (void)s.render(FrameIndex{33});
    AUREA_CHECK(g045_diff(h, s.render(FrameIndex{10})) < 0.001f);
    AUREA_CHECK(g045_diff(plain, h) > 0.1f);
    AUREA_CHECK(g045_finite(h));
    fx.params[8].constant = ParamValue::scalar(1);
    AUREA_CHECK(g045_diff(h, s.render(FrameIndex{10})) > 0.02f);
    fx.params[9].constant = ParamValue::scalar(77);
    const FloatImage otherSeed = s.render(FrameIndex{10});
    fx.params[9].constant = ParamValue::scalar(3);
    AUREA_CHECK(g045_diff(otherSeed, s.render(FrameIndex{10})) > 0.02f);
    fx.params[0].constant = ParamValue::scalar(0);
    AUREA_CHECK(g045_diff(plain, s.render(FrameIndex{10})) < 0.001f);
}

// Shift Channels: o padrão (cada canal do próprio) não muda nada; vermelho do
// verde etc. troca exatamente; "cheio"/"desligado" e a luminância.
AUREA_TEST(Glitch045Gpu, ShiftChannelsTakesEachChannelFromTheChosenSource) {
    AUREA_REQUIRE_GPU();
    using namespace glitch045;
    Scene s(32, 32);
    s.comp->set_transparent_background(true);
    const auto id = s.image(uniform_image(32, 32, 200, 100, 30), 16, 16);
    const FloatImage plain = s.render();
    auto& fx = s.add_effect(id, effect_keys::kShiftChannels);
    AUREA_CHECK(g045_diff(plain, s.render()) < 0.001f);
    const Vec4 o = plain.v(16, 16);
    fx.params[1].constant = ParamValue::scalar(2);   // vermelho ← verde
    fx.params[2].constant = ParamValue::scalar(3);   // verde ← azul
    fx.params[3].constant = ParamValue::scalar(1);   // azul ← vermelho
    const Vec4 r = s.render().v(16, 16);
    AUREA_CHECK_NEAR(r.x, o.y, 0.01f);
    AUREA_CHECK_NEAR(r.y, o.z, 0.01f);
    AUREA_CHECK_NEAR(r.z, o.x, 0.01f);
    fx.params[1].constant = ParamValue::scalar(8);   // cheio
    fx.params[2].constant = ParamValue::scalar(9);   // desligado
    fx.params[3].constant = ParamValue::scalar(4);   // luminância
    const Vec4 f = s.render().v(16, 16);
    AUREA_CHECK_NEAR(f.x, 1.0f, 0.01f);
    AUREA_CHECK_NEAR(f.y, 0.0f, 0.01f);
    AUREA_CHECK(f.z > o.z && f.z < o.x);
    fx.params[0].constant = ParamValue::scalar(9);   // alfa desligado
    AUREA_CHECK_NEAR(s.render().v(16, 16).w, 0.0f, 0.01f);
}
