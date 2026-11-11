// =============================================================================
//  Lote "AE 2026" (AeEffects2026*.cpp e os consertos dos parciais): contas e
//  contratos que o teste do catálogo inteiro não vê. Propriedades da
//  implementação própria — o AE é só a especificação do comportamento, sem
//  paridade numérica. Filtro: `AeFx2026`.
// =============================================================================

namespace {

/// A camada com o efeito `key` (padrões), já na cena.
EffectInstance& ae26_add(Scene& s, LayerId id, const char* key) { return s.add_effect(id, key); }

u32 ae26_param(const char* key, const char* id) {
    const ParameterRegistry* p = gpu().effects.params(effect_type_id(key));
    return p ? p->find(id) : kInvalidIndex;
}

void ae26_set(Scene& s, LayerId id, usize fx, const char* key, const char* param, ParamValue v) {
    const u32 i = ae26_param(key, param);
    AUREA_CHECK_MSG(i != kInvalidIndex, param);
    if (i != kInvalidIndex) s.comp->layer(id)->effects[fx].params[i].constant = v;
}

/// Duas cores lado a lado (esquerda, direita), opacas.
ImagePixels ae26_split(u32 w, u32 h, const u8 (&l)[4], const u8 (&r)[4]) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
        const u8* c = x < w / 2 ? l : r;
        for (int k = 0; k < 4; ++k) p[k] = c[k];
    }
    return px;
}

} // namespace

// Unmult: o preto vira transparente e, sobre fundo preto, a camada volta a ser
// a imagem de antes (a conta é em linear, como a composição). O alvo final da
// cena é opaco: o alfa se lê pela composição sobre fundo BRANCO.
AUREA_TEST(AeFx2026, UnmultBlackBecomesTransparentAndRecomposesOverBlack) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 32);
    const u8 black[4] = {0, 0, 0, 255}, orange[4] = {230, 120, 30, 255};
    const LayerId id = s.image(ae26_split(64, 32, black, orange), 32, 16);
    const FloatImage plain = s.render();
    ae26_add(s, id, "aurea.key.unmult");
    AUREA_CHECK(image_diff(plain, s.render()) < 0.004f);   // sobre preto: a imagem de antes
    s.comp->set_background(Color{1, 1, 1, 1});
    const FloatImage img = s.render();
    const Vec4 dark = img.v(8, 16), lit = img.v(56, 16);
    // Alfa = canal mais forte (linear) do laranja; sobre branco: cor + (1 − alfa).
    const f32 r = srgb_decode(230.0f / 255.0f), g = srgb_decode(120.0f / 255.0f);
    std::printf("    unmult sobre branco: preto (%.3f %.3f %.3f), laranja (%.3f %.3f %.3f), g esperado %.3f\n",
                dark.x, dark.y, dark.z, lit.x, lit.y, lit.z, g + (1.0f - r));
    AUREA_CHECK(dark.x > 0.995f && dark.y > 0.995f && dark.z > 0.995f);   // preto: transparente
    AUREA_CHECK_NEAR(lit.x, 1.0, 0.01);
    AUREA_CHECK_NEAR(lit.y, g + (1.0f - r), 0.01);
    // Nível do preto acima do canal: o laranja some também.
    ae26_set(s, id, 0, "aurea.key.unmult", "black_level", ParamValue::scalar(100.0f));
    AUREA_CHECK(s.render().v(56, 16).y > 0.995f);
}

// Manter cor: o vermelho fica, o verde vira cinza.
AUREA_TEST(AeFx2026, LeaveColorKeepsTargetHueAndGraysTheRest) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 32);
    const u8 red[4] = {210, 20, 20, 255}, green[4] = {30, 200, 40, 255};
    const LayerId id = s.image(ae26_split(64, 32, red, green), 32, 16);
    ae26_add(s, id, "aurea.color.leave_color");   // padrão: 100%, vermelho, por matiz
    const FloatImage img = s.render();
    const Vec4 r = img.v(8, 16), g = img.v(56, 16);
    std::printf("    manter cor: vermelho (%.3f %.3f %.3f), verde (%.3f %.3f %.3f)\n", r.x, r.y, r.z, g.x, g.y, g.z);
    AUREA_CHECK_NEAR(r.x, srgb_decode(210.0f / 255.0f), 0.02);
    AUREA_CHECK(r.y < 0.02f);
    AUREA_CHECK(std::fabs(g.x - g.y) < 0.01f && std::fabs(g.y - g.z) < 0.01f);
}

// Deformar: cada estilo é contínuo em curvatura 0 (quase identidade com 0,5%)
// e a curvatura 0 sai da cadeia.
AUREA_TEST(AeFx2026, WarpStylesAreIdentityAtZeroBend) {
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    const LayerId id = s.image(reference_image(96, 96), 48, 48);
    const FloatImage plain = s.render();
    ae26_add(s, id, "aurea.distort.warp_styles");
    ae26_set(s, id, 0, "aurea.distort.warp_styles", "bend", ParamValue::scalar(0.0f));
    AUREA_CHECK(image_diff(s.render(), plain) < 1e-6f);
    for (u32 style = 0; style < 15; ++style) {
        ae26_set(s, id, 0, "aurea.distort.warp_styles", "style", ParamValue::scalar(static_cast<f32>(style)));
        ae26_set(s, id, 0, "aurea.distort.warp_styles", "bend", ParamValue::scalar(0.05f));
        const u32 near0 = changed_pixels(s.render(), plain, 0.08f);
        ae26_set(s, id, 0, "aurea.distort.warp_styles", "bend", ParamValue::scalar(80.0f));
        const u32 far = changed_pixels(s.render(), plain, 0.08f);
        std::printf("    estilo %2u: 0,05%% muda %u px, 80%% muda %u px\n", style, near0, far);
        AUREA_CHECK_MSG(near0 < 20u, "estilo descontínuo em curvatura 0");
        AUREA_CHECK_MSG(far > 300u, "estilo não deforma");
    }
}

// Níveis do alfa: a reta de entrada → saída no alfa, a cor acompanha. Branco
// sobre o fundo preto: o valor linear lido é o próprio alfa.
AUREA_TEST(AeFx2026, AlphaLevelsMapsAlpha) {
    AUREA_REQUIRE_GPU();
    Scene s(32, 32);
    const LayerId id = s.image(uniform_image(32, 32, 255, 255, 255, 128), 16, 16);
    const f32 a0 = s.render().v(16, 16).x;
    ae26_add(s, id, "aurea.color.alpha_levels");
    AUREA_CHECK_NEAR(s.render().v(16, 16).x, a0, 0.002);                  // padrões = identidade
    ae26_set(s, id, 0, "aurea.color.alpha_levels", "output_white", ParamValue::scalar(50.0f));
    const f32 half = s.render().v(16, 16).x;
    ae26_set(s, id, 0, "aurea.color.alpha_levels", "output_white", ParamValue::scalar(100.0f));
    ae26_set(s, id, 0, "aurea.color.alpha_levels", "input_black", ParamValue::scalar(50.0f));
    const f32 cut = s.render().v(16, 16).x;
    ae26_set(s, id, 0, "aurea.color.alpha_levels", "input_black", ParamValue::scalar(0.0f));
    ae26_set(s, id, 0, "aurea.color.alpha_levels", "gamma", ParamValue::scalar(2.0f));
    const f32 g = s.render().v(16, 16).x;
    std::printf("    níveis do alfa: a0 %.4f, saída 50%% %.4f, entrada preta 50%% %.4f, gama 2 %.4f\n", a0, half, cut, g);
    AUREA_CHECK_NEAR(a0, 128.0 / 255.0, 0.004);
    AUREA_CHECK_NEAR(half, a0 * 0.5, 0.004);
    AUREA_CHECK_NEAR(cut, (a0 - 0.5) / 0.5, 0.01);
    AUREA_CHECK_NEAR(g, std::sqrt(a0), 0.01);
}

// Bug 3 do levantamento: o centro relativo é da CAMADA. Uma sombra antes (que
// cresce a região) não pode mover o Redemoinho, a Lupa nem o Raio.
AUREA_TEST(AeFx2026, RelativeCentersFollowTheLayerAfterRegionGrowth) {
    AUREA_REQUIRE_GPU();
    for (const char* key : {"aurea.distort.swirl", "aurea.distort.magnify", "aurea.generate.lightning"}) {
        // Camada menor que o quadro: a sombra pode crescer a região (o quadro
        // mostra o entorno), e o fundo preto esconde a sombra preta.
        Scene s(160, 160);
        const LayerId id = s.image(reference_image(96, 96), 80, 80);
        ae26_add(s, id, key);
        if (std::strcmp(key, "aurea.distort.swirl") == 0) ae26_set(s, id, 0, key, "angle", ParamValue::scalar(240.0f));
        if (std::strcmp(key, "aurea.distort.magnify") != 0 && std::strcmp(key, "aurea.distort.swirl") != 0)
            ae26_set(s, id, 0, key, "start", ParamValue::vec2(0.3f, 0.1f));
        const FloatImage alone = s.render();
        // A sombra entra ANTES (índice 0): cobre fora da caixa, invisível sob a foto opaca.
        Layer* l = s.comp->layer(id);
        EffectInstance sh;
        sh.id = l->alloc_effect_id();
        sh.type = effect_type_id(effect_keys::kDropShadow);
        initialize_instance(sh, *gpu().effects.params(sh.type));
        sh.params[3].constant = ParamValue::scalar(30.0f);   // distância
        sh.params[4].constant = ParamValue::scalar(10.0f);   // suavidade
        l->effects.insert(l->effects.begin(), std::move(sh));
        const FloatImage after = s.render();
        // Só dentro da caixa da camada: fora dela o Raio agora tem onde brilhar.
        f32 d = 0.0f;
        u32 n = 0;
        for (u32 y = 32; y < 128; ++y) for (u32 x = 32; x < 128; ++x) {
            f32 m = 0.0f;
            for (int c = 0; c < 3; ++c) m = std::fmax(m, std::fabs(alone.at(x, y)[c] - after.at(x, y)[c]));
            d = std::fmax(d, m);
            n += m > 0.05f ? 1u : 0u;
        }
        std::printf("    %s: depois da sombra, %u px mudaram (máx %.4f)\n", key, n, d);
        AUREA_CHECK_MSG(n < 60u, key);
    }
}

// Consertos dos parciais: um projeto ANTIGO (sem os parâmetros novos no fim)
// e uma instância nova nos padrões desenham o MESMO quadro, e o parâmetro novo
// muda o quadro quando sai do padrão.
AUREA_TEST(AeFx2026, PartialFixesKeepOldProjectsAndChangeWhenUsed) {
    AUREA_REQUIRE_GPU();
    struct Case {
        const char* key;
        u32 oldCount;           // parâmetros que um projeto de antes tem
        const char* param;      // o novo que deve mudar o quadro
        ParamValue value;
        const char* tweak;      // parâmetro antigo a tirar do neutro (ou nulo)
        ParamValue tweakValue;
    };
    const Case cases[] = {
        {effect_keys::kInvert, 2, "channel", ParamValue::scalar(1.0f), nullptr, {}},
        {effect_keys::kLevels, 5, "channel", ParamValue::scalar(4.0f), "gamma", ParamValue::scalar(1.6f)},
        {effect_keys::kExposure, 4, "channels", ParamValue::scalar(1.0f), "exposure", ParamValue::scalar(0.8f)},
        {effect_keys::kGlow, 11, "composite_original", ParamValue::scalar(1.0f), "threshold", ParamValue::scalar(30.0f)},
        {effect_keys::kWaveWarp, 10, "wave_type", ParamValue::scalar(2.0f), nullptr, {}},
        {effect_keys::kTransform, 5, "skew", ParamValue::scalar(25.0f), "rotation", ParamValue::scalar(5.0f)},
        {effect_keys::kIrisWipe, 6, "use_inner_radius", ParamValue::boolean(true), "sides", ParamValue::scalar(5.0f)},
    };
    for (const Case& c : cases) {
        Scene s(96, 96);
        if (std::strcmp(c.key, effect_keys::kGlow) == 0 || std::strcmp(c.key, effect_keys::kTransform) == 0)
            s.comp->set_background(Color{0.2f, 0.3f, 0.6f, 1.0f});
        const LayerId id = s.image(reference_image(c.key == std::string(effect_keys::kGlow) ? 64u : 96u, 64), 48, 48);
        ae26_add(s, id, c.key);
        const ParameterRegistry& reg = *gpu().effects.params(effect_type_id(c.key));
        AUREA_CHECK_MSG(reg.count() > c.oldCount, c.key);
        if (c.tweak) ae26_set(s, id, 0, c.key, c.tweak, c.tweakValue);
        if (std::strcmp(c.key, effect_keys::kWaveWarp) == 0) ae26_set(s, id, 0, c.key, "height", ParamValue::scalar(12.0f));
        const FloatImage fresh = s.render();
        // O projeto de antes: só os parâmetros que existiam (o resto lê o padrão).
        s.comp->layer(id)->effects[0].params.resize(c.oldCount);
        const FloatImage old = s.render();
        const f32 same = image_diff(fresh, old);
        // O parâmetro novo, fora do padrão.
        s.comp->layer(id)->effects[0].params.resize(reg.count());
        initialize_instance(s.comp->layer(id)->effects[0], reg);
        if (c.tweak) ae26_set(s, id, 0, c.key, c.tweak, c.tweakValue);
        if (std::strcmp(c.key, effect_keys::kWaveWarp) == 0) ae26_set(s, id, 0, c.key, "height", ParamValue::scalar(12.0f));
        if (std::strcmp(c.key, effect_keys::kIrisWipe) == 0) ae26_set(s, id, 0, c.key, "inner_radius", ParamValue::scalar(30.0f));
        ae26_set(s, id, 0, c.key, c.param, c.value);
        const u32 moved = changed_pixels(fresh, s.render(), 0.02f);
        std::printf("    %-28s antigo x novo: %.6f; com %s: %u px mudaram\n", c.key, same, c.param, moved);
        AUREA_CHECK_MSG(same == 0.0f, c.key);
        AUREA_CHECK_MSG(moved > 30u, c.key);
    }
}

// Graves e agudos: as prateleiras sobem/baixam só a faixa delas.
AUREA_TEST(AeFx2026, BassTrebleShelvesShapeOnlyTheirBand) {
    const audio::Biquad bass = audio::shelf_filter(false, 200.0, 12.0);
    const audio::Biquad treble = audio::shelf_filter(true, 4000.0, -9.0);
    std::printf("    graves +12: 30 Hz %.2f dB, 8 kHz %.2f dB; agudos -9: 30 Hz %.2f dB, 16 kHz %.2f dB\n",
                audio::biquad_response_db(bass, 30), audio::biquad_response_db(bass, 8000),
                audio::biquad_response_db(treble, 30), audio::biquad_response_db(treble, 16000));
    AUREA_CHECK_NEAR(audio::biquad_response_db(bass, 30), 12.0, 0.6);
    AUREA_CHECK_NEAR(audio::biquad_response_db(bass, 8000), 0.0, 0.3);
    AUREA_CHECK_NEAR(audio::biquad_response_db(bass, 200), 6.0, 0.6);   // meio do degrau na frequência do canto
    AUREA_CHECK_NEAR(audio::biquad_response_db(treble, 30), 0.0, 0.3);
    AUREA_CHECK_NEAR(audio::biquad_response_db(treble, 16000), -9.0, 0.8);
    const audio::Biquad flat = audio::shelf_filter(false, 200.0, 0.0);
    AUREA_CHECK_NEAR(audio::biquad_response_db(flat, 50), 0.0, 1e-6);
}

// Os efeitos novos com camada de entrada leem a OUTRA camada.
AUREA_TEST(AeFx2026, BlendAndDifferenceMatteReadTheOtherLayer) {
    AUREA_REQUIRE_GPU();
    Scene s(64, 32);
    s.comp->set_background(Color{1, 1, 1, 1});   // o que some mostra o branco
    const LayerId target = s.image(uniform_image(64, 32, 200, 40, 40), 32, 16);
    const u8 same[4] = {200, 40, 40, 255}, other[4] = {40, 40, 200, 255};
    const LayerId ref = s.image(ae26_split(64, 32, same, other), 32, 16);
    s.comp->layer(ref)->visible = false;
    s.comp->reorder_layer(target, s.comp->order().size() - 1);
    // Fosco por diferença: onde a outra é igual some, onde difere fica.
    EffectInstance& dm = ae26_add(s, target, "aurea.key.difference_matte");
    dm.params[1].constant.ref = ref.pack();
    dm.params[1].constant.v[0] = static_cast<f32>(ref.index);
    const FloatImage m = s.render();
    const Vec4 gone = m.v(8, 16), kept = m.v(56, 16);
    std::printf("    fosco por diferença: igual (%.3f %.3f %.3f), diferente (%.3f %.3f %.3f)\n", gone.x, gone.y, gone.z,
                kept.x, kept.y, kept.z);
    AUREA_CHECK(gone.y > 0.98f && gone.z > 0.98f);   // sumiu: o fundo branco
    AUREA_CHECK(kept.y < 0.05f && kept.x > 0.5f);    // ficou: o vermelho
    // Mesclar (transição cruzada, 0% do original): vira a outra camada.
    s.comp->layer(target)->effects.clear();
    EffectInstance& bl = ae26_add(s, target, "aurea.color.blend_layers");
    bl.params[0].constant.ref = ref.pack();
    bl.params[0].constant.v[0] = static_cast<f32>(ref.index);
    bl.params[2].constant = ParamValue::scalar(0.0f);
    const Vec4 right = s.render().v(56, 16);
    AUREA_CHECK(right.z > right.x);
}

// =============================================================================
// Segunda leva (AeEffects2026Extra.cpp) e os parâmetros acrescentados ao Eco
// (Operador do eco) e ao Brilho (Dimensões do brilho). Filtro: `AeFxFinish`.
// =============================================================================

// Eco: o operador acrescentado no fim. Projeto antigo (sem o slot) e "Somar"
// desenham o MESMO quadro, bit a bit; os outros operadores juntam as cópias
// pela conta deles. Quadrado branco de 12 px andando 2 px/quadro: no quadro
// 30 a cópia atual cobre x 64..76 e o eco (3 quadros antes) x 58..70.
AUREA_TEST(AeFxFinish, EchoOperatorDefaultIsTheOldSumAndOperatorsCombine) {
    AUREA_REQUIRE_GPU();
    Scene s(kPtW, kPtH, 30.0);
    const LayerId id = precomp_temporal_square(s, false, false);
    EffectInstance& e = s.add_effect(id, effect_keys::kEchoTrail);
    const ParameterRegistry& reg = *gpu().effects.params(e.type);
    const u32 op = reg.find("echo_operator");
    AUREA_CHECK_EQ(op, 5u);   // no fim: depois de "Intensidade inicial"
    e.params[0].constant = ParamValue::scalar(1.0f);   // uma cópia
    e.params[1].constant = ParamValue::scalar(3.0f);   // 3 quadros
    e.params[2].constant = ParamValue::scalar(0.6f);   // decaimento
    const FloatImage fresh = s.render(FrameIndex{30});
    s.comp->layer(id)->effects[0].params.resize(5);    // projeto de antes do operador
    const FloatImage old = s.render(FrameIndex{30});
    std::printf("    eco: antigo x somar %.8f\n", image_diff(fresh, old));
    AUREA_CHECK(image_diff(fresh, old) == 0.0f);
    s.comp->layer(id)->effects[0].params.resize(reg.count());
    initialize_instance(s.comp->layer(id)->effects[0], reg);
    EffectInstance& ee = s.comp->layer(id)->effects[0];
    ee.params[0].constant = ParamValue::scalar(1.0f);
    ee.params[1].constant = ParamValue::scalar(3.0f);
    ee.params[2].constant = ParamValue::scalar(0.6f);
    AUREA_CHECK(image_diff(s.render(FrameIndex{30}), fresh) == 0.0f);   // 0 explícito = o mesmo
    struct Want { u32 op; f32 both, now, echo; const char* name; };
    // both = onde as duas cópias se cobrem (x 67), now = só a atual (x 73), echo = só o eco (x 61).
    const Want wants[] = {
        {0, 1.6f, 1.0f, 0.6f, "somar"},
        {1, 1.0f, 1.0f, 0.6f, "maximo"},
        {2, 0.6f, 0.0f, 0.0f, "minimo"},
        {3, 1.0f, 1.0f, 0.6f, "tela"},
        {4, 1.0f, 1.0f, 0.6f, "compor atras"},
        {5, 1.0f, 1.0f, 0.6f, "compor na frente"},
        {6, 1.0f, 0.625f, 0.375f, "mesclar"},
    };
    for (const Want& w : wants) {
        ee.params[op].constant = ParamValue::scalar(static_cast<f32>(w.op));
        const FloatImage img = s.render(FrameIndex{30});
        const f32 both = img.v(67, 36).x, now = img.v(73, 36).x, echo = img.v(61, 36).x;
        std::printf("    eco %-16s cobre %.3f atual %.3f eco %.3f (esperado %.3f %.3f %.3f)\n", w.name, both, now, echo,
                    w.both, w.now, w.echo);
        AUREA_CHECK_NEAR(both, w.both, 0.02);
        AUREA_CHECK_NEAR(now, w.now, 0.02);
        AUREA_CHECK_NEAR(echo, w.echo, 0.02);
        AUREA_CHECK(img.v(110, 36).x < 0.002f);   // longe das cópias: o fundo
    }
}

// Brilho: "Dimensões do brilho" no fim. Projeto antigo = padrão (bit a bit);
// só horizontal não acende nada acima/abaixo, só vertical nada aos lados.
AUREA_TEST(AeFxFinish, GlowDimensionsDefaultIsTheOldGlowAndAxesStayPut) {
    AUREA_REQUIRE_GPU();
    Scene s(128, 128);
    const LayerId id = s.solid(10, 10, Vec4{1, 1, 1, 1}, 64, 64);
    EffectInstance& g = s.add_effect(id, effect_keys::kGlow);
    const ParameterRegistry& reg = *gpu().effects.params(g.type);
    const u32 dims = reg.find("glow_dimensions");
    const u32 thr = reg.find("threshold");
    AUREA_CHECK_EQ(dims, reg.count() - 1);
    g.params[thr].constant = ParamValue::scalar(30.0f);
    const FloatImage fresh = s.render();
    s.comp->layer(id)->effects[0].params.resize(dims);   // projeto de antes
    const FloatImage old = s.render();
    std::printf("    brilho: antigo x padrao %.8f\n", image_diff(fresh, old));
    AUREA_CHECK(image_diff(fresh, old) == 0.0f);
    s.comp->layer(id)->effects[0].params.resize(reg.count());
    initialize_instance(s.comp->layer(id)->effects[0], reg);
    s.comp->layer(id)->effects[0].params[thr].constant = ParamValue::scalar(30.0f);
    const f32 sideBoth = fresh.v(80, 64).x, belowBoth = fresh.v(64, 80).x;
    s.comp->layer(id)->effects[0].params[dims].constant = ParamValue::scalar(1.0f);
    const FloatImage h = s.render();
    s.comp->layer(id)->effects[0].params[dims].constant = ParamValue::scalar(2.0f);
    const FloatImage v = s.render();
    std::printf("    brilho H+V lado %.4f baixo %.4f | H lado %.4f baixo %.4f | V lado %.4f baixo %.4f\n", sideBoth,
                belowBoth, h.v(80, 64).x, h.v(64, 80).x, v.v(80, 64).x, v.v(64, 80).x);
    AUREA_CHECK(sideBoth > 0.01f && belowBoth > 0.01f);
    AUREA_CHECK(h.v(80, 64).x > 0.01f);
    AUREA_CHECK(h.v(64, 80).x < 0.002f);
    AUREA_CHECK(v.v(64, 80).x > 0.01f);
    AUREA_CHECK(v.v(80, 64).x < 0.002f);
}

// Segunda leva: uma conta conferida por efeito (o catálogo inteiro só confere
// que o quadro muda).
AUREA_TEST(AeFxFinish, ExtraEffectsDoTheirMath) {
    AUREA_REQUIRE_GPU();
    // Combinador de canais: o vermelho vai para o azul.
    {
        Scene s(32, 32);
        const LayerId id = s.image(uniform_image(32, 32, 200, 40, 10), 16, 16);
        ae26_add(s, id, "aurea.color.channel_combiner");
        ae26_set(s, id, 0, "aurea.color.channel_combiner", "from", ParamValue::scalar(4.0f));   // vermelho
        ae26_set(s, id, 0, "aurea.color.channel_combiner", "to", ParamValue::scalar(2.0f));     // só o azul
        const Vec4 c = s.render().v(16, 16);
        std::printf("    combinador: (%.3f %.3f %.3f)\n", c.x, c.y, c.z);
        AUREA_CHECK_NEAR(c.z, c.x, 0.01);
        AUREA_CHECK_NEAR(c.x, srgb_decode(200.0f / 255.0f), 0.01);
    }
    // Limitador de vídeo: nada passa do sinal máximo (canal codificado).
    {
        Scene s(64, 64);
        const LayerId id = s.image(reference_image(64, 64), 32, 32);
        ae26_add(s, id, "aurea.color.video_limiter");
        ae26_set(s, id, 0, "aurea.color.video_limiter", "signal_max", ParamValue::scalar(60.0f));
        const FloatImage img = s.render();
        f32 worst = 0.0f;
        for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 64; ++x) {
            const Vec4 p = img.v(x, y);
            worst = std::fmax(worst, std::fmax(srgb_encode(p.x), std::fmax(srgb_encode(p.y), srgb_encode(p.z))));
        }
        std::printf("    limitador 60%%: maior canal %.3f\n", worst);
        AUREA_CHECK(worst < 0.61f);
    }
    // Cores de transmissão: "recortar o seguro" some com o cinza escuro e deixa o branco.
    {
        Scene s(64, 32);
        const u8 gray[4] = {60, 60, 60, 255}, white[4] = {255, 255, 255, 255};
        const LayerId id = s.image(ae26_split(64, 32, gray, white), 32, 16);
        ae26_add(s, id, "aurea.color.broadcast_colors");
        ae26_set(s, id, 0, "aurea.color.broadcast_colors", "method", ParamValue::scalar(3.0f));
        ae26_set(s, id, 0, "aurea.color.broadcast_colors", "max_amplitude", ParamValue::scalar(90.0f));
        const FloatImage img = s.render();
        AUREA_CHECK(img.v(8, 16).x < 0.002f);    // seguro: recortado (fundo preto)
        AUREA_CHECK(img.v(56, 16).x > 0.99f);    // o branco passa de 90 IRE: fica
    }
    // Girar esfera VR: panorâmica de 180° troca as metades da equiretangular.
    {
        Scene s(64, 32);
        const u8 l[4] = {220, 30, 30, 255}, r[4] = {30, 30, 220, 255};
        const LayerId id = s.image(ae26_split(64, 32, l, r), 32, 16);
        ae26_add(s, id, "aurea.distort.vr_rotate_sphere");
        ae26_set(s, id, 0, "aurea.distort.vr_rotate_sphere", "pan", ParamValue::scalar(180.0f));
        const FloatImage img = s.render();
        const Vec4 a = img.v(8, 16), b = img.v(56, 16);
        std::printf("    vr 180: esquerda (%.2f %.2f) direita (%.2f %.2f)\n", a.x, a.z, b.x, b.z);
        AUREA_CHECK(a.z > a.x && b.x > b.z);
    }
    // Aritmética composta: a camada menos ela mesma = preto.
    {
        Scene s(32, 32);
        const LayerId id = s.image(reference_image(32, 32), 16, 16);
        const LayerId other = s.image(reference_image(32, 32), 16, 16);
        s.comp->layer(other)->visible = false;
        s.comp->reorder_layer(id, s.comp->order().size() - 1);
        EffectInstance& fx = ae26_add(s, id, "aurea.color.compound_arithmetic");
        fx.params[0].constant.ref = other.pack();
        fx.params[0].constant.v[0] = static_cast<f32>(other.index);
        ae26_set(s, id, 0, "aurea.color.compound_arithmetic", "operator", ParamValue::scalar(2.0f));   // subtrair
        const Vec4 m = s.render().mean();
        std::printf("    aritmetica composta (subtrair a si): media %.4f\n", m.x + m.y + m.z);
        AUREA_CHECK(m.x + m.y + m.z < 0.01f);
    }
    // Limpar com vidro: 100% sem camada a revelar = tudo transparente.
    {
        Scene s(32, 32);
        const LayerId id = s.image(reference_image(32, 32), 16, 16);
        ae26_add(s, id, "aurea.transition.glass_wipe");
        ae26_set(s, id, 0, "aurea.transition.glass_wipe", "completion", ParamValue::scalar(100.0f));
        const Vec4 m = s.render().mean();
        AUREA_CHECK(m.x + m.y + m.z < 0.002f);
    }
    // Cálculos: o canal alfa de uma camada opaca = branco.
    {
        Scene s(32, 32);
        const LayerId id = s.image(uniform_image(32, 32, 30, 90, 160), 16, 16);
        ae26_add(s, id, "aurea.color.calculations");
        ae26_set(s, id, 0, "aurea.color.calculations", "input_channel", ParamValue::scalar(5.0f));
        const Vec4 c = s.render().v(16, 16);
        AUREA_CHECK(c.x > 0.99f && c.y > 0.99f && c.z > 0.99f);
    }
}

// Compor atrás x na frente: numa pré-composição cada cópia é a filha NAQUELE
// instante. Com o vermelho do quadrado indo de 0 (quadro 27, o eco) a 1
// (quadro 30, agora), a ordem aparece onde as duas se cobrem: atrás = a atual
// por cima (R 1); na frente = o eco por cima (R = 0·0,6 + 1·0,4).
AUREA_TEST(AeFxFinish, EchoCompositeInBackAndInFrontKeepTheirOrder) {
    AUREA_REQUIRE_GPU();
    Scene s(kPtW, kPtH, 30.0);
    const LayerId n = precomp_temporal_square(s, true, false);
    Composition* child = s.project.timeline().composition(s.comp->layer(n)->nested.composition);
    AUREA_CHECK(child != nullptr);
    if (!child) return;
    Layer* sq = nullptr;
    child->layers().for_each([&](LayerId, Layer& l) { if (l.name == "quadrado") sq = &l; });
    AUREA_CHECK(sq != nullptr);
    if (!sq) return;
    Track& red = sq->tracks.get_or_create(TrackProperty::ShapeParam, 0, 32);   // kFillR (timeline/ShapeFill.hpp)
    (void)red.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)red.set(FrameIndex{27}, 0.0f, Interpolation::Linear);
    (void)red.set(FrameIndex{30}, 1.0f, Interpolation::Linear);
    EffectInstance& e = s.add_effect(n, effect_keys::kEchoTrail);
    const u32 op = gpu().effects.params(e.type)->find("echo_operator");
    e.params[0].constant = ParamValue::scalar(1.0f);
    e.params[1].constant = ParamValue::scalar(3.0f);
    e.params[2].constant = ParamValue::scalar(0.6f);
    e.params[op].constant = ParamValue::scalar(4.0f);
    const Vec4 back = s.render(FrameIndex{30}, 1, true).v(67, 36);
    s.comp->layer(n)->effects[0].params[op].constant = ParamValue::scalar(5.0f);
    const Vec4 front = s.render(FrameIndex{30}, 1, true).v(67, 36);
    std::printf("    eco: atras (%.3f %.3f) na frente (%.3f %.3f)\n", back.x, back.y, front.x, front.y);
    AUREA_CHECK_NEAR(back.x, 1.0, 0.03);
    AUREA_CHECK_NEAR(back.y, 1.0, 0.03);
    AUREA_CHECK_NEAR(front.x, 0.4, 0.03);
    AUREA_CHECK_NEAR(front.y, 1.0, 0.03);
}
