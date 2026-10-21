// Flow Retime e Flow Motion Blur (0.1.8, FlowTimeEffects.cpp): o tempo da
// fonte segue a velocidade do efeito (com keyframes), os quadros do meio saem
// do optical flow e o desfoque borra ao longo do movimento da filmagem.
namespace {
struct FlowTimeRig {
    SyntheticFactory factory;
    Engine e;
    u64 layer = 0;
    TextureHandle target{};
    u32 w, h;
    std::vector<u16> half;
    FlowTimeRig(SyntheticPattern pattern, u32 width, u32 height) : factory([&] {
        SyntheticConfig cfg;
        cfg.width = width;
        cfg.height = height;
        cfg.frameCount = 300;
        cfg.pattern = pattern;
        return cfg;
    }()), w(width), h(height), half(static_cast<usize>(width) * height * 4) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.framesInFlight = 2;
        ec.mediaFactory = &factory;
        ec.workerCount = 2;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(width, height, 30.0, "flow time").ok());
        VideoImport imp;
        imp.sourcePath = "sintetico";
        imp.displayName = "clipe";
        auto id = e.import_video(imp);
        AUREA_CHECK(id.ok());
        layer = id.ok() ? *id : 0;
        if (Layer* l = model()) {
            l->end = FrameIndex{l->start.value + 400};
            comp()->set_duration(FrameIndex{400});
        }
        TextureDesc d;
        d.width = width;
        d.height = height;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.transferSrc = true;
        target = *e.gpu()->create_texture(d);
    }
    ~FlowTimeRig() {
        if (target.valid()) e.gpu()->destroy_texture(target);
        e.shutdown();
    }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* model() { return comp() ? comp()->layer(LayerId::unpack(layer)) : nullptr; }
    EffectInstance* add(const char* key) {
        Command c;
        c.type = CommandType::EffectAdd;
        c.effect_add.layer = LayerId::unpack(layer);
        c.effect_add.effectType = effect_type_id(key);
        c.effect_add.index = kInvalidIndex;
        AUREA_CHECK(e.apply_command(c).ok());
        Layer* l = model();
        return l && !l->effects.empty() ? &l->effects.back() : nullptr;
    }
    void render(i64 frame) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
        AUREA_CHECK(e.render_offscreen(target, w, h).ok());
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), w * 8).ok());
    }
    f64 luma(u32 x, u32 y) const {
        return srgb_encode(half_to_float(half[(static_cast<usize>(y) * w + x) * 4])) * 219.0 + 16.0;
    }
};
}

AUREA_TEST(FlowTimeGpu, RetimeHalfSpeedShowsHalfTheSourceAndKeyframesIntegrate) {
    AUREA_REQUIRE_GPU();
    FlowTimeRig rig(SyntheticPattern::FrameGray, 64, 36);
    EffectInstance* fx = rig.add(effect_keys::kFlowRetime);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    fx->params[1].constant = ParamValue::scalar(0.0f);   // quadro mais próximo: o tempo exato
    auto shown = [&](i64 frame) { rig.render(frame); return rig.luma(32, 18); };
    // 50% (o padrão): o quadro 100 da camada mostra o 50 da fonte.
    AUREA_CHECK_NEAR(shown(100), frame_gray_code(50), 0.5);
    AUREA_CHECK_NEAR(shown(40), frame_gray_code(20), 0.5);
    // 200%: o dobro.
    fx->params[0].constant = ParamValue::scalar(200.0f);
    AUREA_CHECK_NEAR(shown(40), frame_gray_code(80), 0.5);
    // Keyframes segurados: 100% até o quadro 30, depois 25%. No 70: 30 + 40 × 0,25 = 40.
    Layer* l = rig.model();
    Track& t = l->tracks.get_or_create(TrackProperty::EffectParam, fx->id, param_track_key(0, 0));
    t.set(FrameIndex{0}, 100.0f, Interpolation::Hold);
    t.set(FrameIndex{30}, 25.0f, Interpolation::Hold);
    AUREA_CHECK_NEAR(shown(70), frame_gray_code(40), 0.5);
    // Desligado: o tempo normal.
    fx->enabled = false;
    AUREA_CHECK_NEAR(shown(70), frame_gray_code(70), 0.5);
}

AUREA_TEST(FlowTimeGpu, RetimeMidFramesComeFromOpticalFlow) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 192, H = 108;
    FlowTimeRig rig(SyntheticPattern::PanTexture, W, H);
    EffectInstance* fx = rig.add(effect_keys::kFlowRetime);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    fx->params[0].constant = ParamValue::scalar(25.0f);  // 4× mais lento
    auto error_at = [&](i64 frame, f64 truth) {
        rig.render(frame);
        f64 sum = 0;
        u32 n = 0;
        for (u32 y = 16; y < H - 16; ++y)
            for (u32 x = 16; x < W - 16; ++x) { sum += std::fabs(rig.luma(x, y) - pan_texture_value(x, y, truth)); ++n; }
        return sum / std::max(1u, n);
    };
    // Quadro 82 → fonte 20,5; 85 → 21,25 (frações diferentes do mesmo par).
    const f64 mid = error_at(82, 20.5), quarter = error_at(85, 21.25);
    fx->params[1].constant = ParamValue::scalar(1.0f);   // mistura, para comparar
    const f64 blend = error_at(82, 20.5);
    std::printf("    flow retime 25%%: erro meio %.2f, quarto %.2f; mistura %.2f\n", mid, quarter, blend);
    AUREA_CHECK(mid < blend * 0.5);
    AUREA_CHECK(mid < 3.0 && quarter < 3.0);
}

AUREA_TEST(FlowTimeGpu, FlowMotionBlurSmearsAlongTheFootageMotion) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 192, H = 108;
    FlowTimeRig rig(SyntheticPattern::PanTexture, W, H);
    // Detalhe horizontal: a soma das diferenças entre vizinhos numa linha.
    auto detail = [&](i64 frame) {
        rig.render(frame);
        f64 dx = 0, dy = 0;
        for (u32 y = 20; y < H - 20; ++y)
            for (u32 x = 20; x < W - 20; ++x) {
                dx += std::fabs(rig.luma(x + 1, y) - rig.luma(x, y));
                dy += std::fabs(rig.luma(x, y + 1) - rig.luma(x, y));
            }
        return std::pair<f64, f64>{dx, dy};
    };
    const auto sharp = detail(60);
    EffectInstance* fx = rig.add(effect_keys::kFlowMotionBlur);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    const auto natural = detail(60);                        // 100%, 180°: borrão de câmera (2,6 px)
    fx->params[0].constant = ParamValue::scalar(400.0f);
    fx->params[1].constant = ParamValue::scalar(360.0f);    // 4 × um quadro inteiro: ~21 px em x
    const auto blurred = detail(60);
    fx->params[0].constant = ParamValue::scalar(0.0f);
    const auto off = detail(60);
    std::printf("    flow motion blur: detalhe x %.0f -> natural %.0f -> forte %.0f (y %.0f -> %.0f); quantidade 0: %.0f\n",
                sharp.first, natural.first, blurred.first, sharp.second, blurred.second, off.first);
    AUREA_CHECK(natural.first < sharp.first);
    // O movimento é sobretudo em x (5,25 px vs 1,75): o x perde muito mais.
    AUREA_CHECK(blurred.first < sharp.first * 0.8);
    AUREA_CHECK(blurred.first / sharp.first < blurred.second / sharp.second);
    AUREA_CHECK_NEAR(off.first, sharp.first, sharp.first * 0.01);
}

AUREA_TEST(FlowTimeGpu, RandomEditsShowsShuffledSegmentsOfTheSource) {
    // "Edições aleatórias" (0.2.7): trechos de 3 quadros embaralhados pela
    // semente. Cada quadro mostra o quadro da fonte que a conta compartilhada
    // (effects/RandomEdits.hpp) manda — e o trecho anda junto, quadro a quadro.
    AUREA_REQUIRE_GPU();
    FlowTimeRig rig(SyntheticPattern::FrameGray, 64, 36);
    if (Layer* l = rig.model()) l->end = FrameIndex{l->start.value + 150};
    EffectInstance* fx = rig.add(effect_keys::kRandomEdits);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    fx->params[0].constant = ParamValue::scalar(3.0f);
    fx->params[1].constant = ParamValue::scalar(0.5f);
    auto shown = [&](i64 frame) { rig.render(frame); return rig.luma(32, 18); };
    u32 moved = 0;
    for (i64 frame : {0, 1, 2, 3, 4, 5, 30, 31, 77, 149}) {
        const i64 source = random_edits::frame(frame, 150, 3, 0.5f);
        if (source != frame) ++moved;
        AUREA_CHECK_NEAR(shown(frame), frame_gray_code(static_cast<u32>(source)), 0.5);
    }
    AUREA_CHECK(moved >= 6);
    // Desligado: o tempo normal.
    fx->enabled = false;
    AUREA_CHECK_NEAR(shown(31), frame_gray_code(31), 0.5);
}
