// Twixtor e RSMB do AUREA em escala de vídeo real (1080p): o fluxo trabalha
// reduzido, então a deformação de contorno só aparece nesse tamanho. A verdade
// é analítica, sem o ruído que entra no decoder.
namespace {
SyntheticConfig hero_clip(const HeroScene& scene) {
    SyntheticConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.frameCount = 60;
    cfg.pattern = SyntheticPattern::Hero;
    cfg.hero = scene;
    return cfg;
}

HeroScene small_fast_object() {
    HeroScene s;
    s.radius = 70.0; s.vx = 120.0; s.vy = 30.0; s.startX = 120.0; s.startY = 560.0;
    return s;
}
HeroScene low_texture_object() {
    HeroScene s;
    s.radius = 130.0; s.contrast = 0.12;
    return s;
}
HeroScene repeating_object() {
    HeroScene s;
    s.periodic = true;
    return s;
}
// Câmera seguindo o personagem: quem corre é o fundo; o sujeito não pode sumir.
HeroScene tracked_subject() {
    HeroScene s;
    s.pan = -60.0; s.radius = 100.0; s.vx = 0.0; s.vy = 0.0; s.startX = 900.0; s.startY = 620.0;
    return s;
}
HeroScene whip_pan() {
    HeroScene s;
    s.pan = -90.0;
    return s;
}

struct FlowScore {
    f64 ring = 0, ringBad = 0;     // anel em volta do contorno do disco
    f64 edge = 0, edgeBad = 0;     // faixa das laterais, onde o pan traz pixel novo
    f64 rest = 0, restBad = 0;     // o resto do quadro
    f64 worst = 0;                 // percentil 99,9 do erro no quadro inteiro
};

// `truth` devolve o valor de 8 bits esperado em (x, y).
template <typename Truth>
FlowScore score_hero(const FlowTimeRig& rig, const HeroScene& scene, f64 discFrame, const char* tag, Truth&& truth) {
    constexpr f64 kBad = 20.0;
    constexpr u32 kStep = 2, kMargin = 6;
    f64 cx, cy;
    hero_disc(discFrame, scene, cx, cy);
    FlowScore s;
    u32 nRing = 0, nEdge = 0, nRest = 0;
    std::vector<u32> hist(256, 0);
    const f64 band = std::max(56.0, std::hypot(scene.vx, scene.vy) * 0.75);
    for (u32 y = kMargin; y < rig.h - kMargin; y += kStep)
        for (u32 x = kMargin; x < rig.w - kMargin; x += kStep) {
            const f64 err = std::fabs(rig.luma(x, y) - truth(static_cast<f64>(x), static_cast<f64>(y)));
            ++hist[static_cast<usize>(std::clamp(err, 0.0, 255.0))];
            const f64 bad = err > kBad ? 1.0 : 0.0;
            if (std::hypot(x - cx, y - cy) < scene.radius + band) { s.ring += err; s.ringBad += bad; ++nRing; }
            else if (x < 64 || x >= rig.w - 64) { s.edge += err; s.edgeBad += bad; ++nEdge; }
            else { s.rest += err; s.restBad += bad; ++nRest; }
        }
    s.ring /= std::max(1u, nRing); s.ringBad *= 100.0 / std::max(1u, nRing);
    s.edge /= std::max(1u, nEdge); s.edgeBad *= 100.0 / std::max(1u, nEdge);
    s.rest /= std::max(1u, nRest); s.restBad *= 100.0 / std::max(1u, nRest);
    const u64 total = static_cast<u64>(nRing) + nEdge + nRest;
    u64 seen = 0;
    for (usize i = 0; i < hist.size(); ++i) {
        seen += hist[i];
        if (static_cast<f64>(seen) >= 0.999 * static_cast<f64>(total)) { s.worst = static_cast<f64>(i); break; }
    }
    std::printf("    %-24s objeto %6.2f (%5.2f%% ruins) | laterais %6.2f | resto %5.2f (%5.2f%%) | p99.9 %3.0f\n",
                tag, s.ring, s.ringBad, s.edge, s.rest, s.restBad, s.worst);
    if (const char* dump = std::getenv("AUREA_FLOW_DUMP")) {
        // Render | verdade | erro×4 num recorte de 760×540 em volta do disco.
        constexpr u32 kW = 760, kH = 540;
        test::Image8 img;
        img.width = kW * 3; img.height = kH; img.rgba.assign(static_cast<usize>(kW) * 3 * kH * 4, 255);
        const u32 x0 = static_cast<u32>(std::clamp(cx - 380.0, 0.0, static_cast<f64>(rig.w - kW)));
        const u32 y0 = static_cast<u32>(std::clamp(cy - 270.0, 0.0, static_cast<f64>(rig.h - kH)));
        for (u32 y = 0; y < kH; ++y) for (u32 x = 0; x < kW; ++x) {
            const f64 v = rig.luma(x0 + x, y0 + y), tv = truth(static_cast<f64>(x0 + x), static_cast<f64>(y0 + y));
            const u8 vals[3] = {static_cast<u8>(std::clamp(v, 0.0, 255.0)), static_cast<u8>(std::clamp(tv, 0.0, 255.0)),
                                static_cast<u8>(std::clamp(std::fabs(v - tv) * 4.0, 0.0, 255.0))};
            for (u32 k = 0; k < 3; ++k) {
                u8* q = &img.rgba[(static_cast<usize>(y) * kW * 3 + k * kW + x) * 4];
                q[0] = q[1] = q[2] = vals[k];
            }
        }
        (void)test::write_png(std::string(dump) + "_" + tag + ".png", img);
    }
    return s;
}

// Verdade do obturador: média em luz linear dos instantes dentro de ±`half`.
f64 hero_shutter_truth(f64 x, f64 y, f64 center, f64 half, const HeroScene& scene) {
    constexpr int kInstants = 16;
    f64 acc = 0.0;
    for (int i = 0; i < kInstants; ++i) {
        const f64 at = center + ((static_cast<f64>(i) + 0.5) / kInstants - 0.5) * 2.0 * half;
        acc += srgb_decode(static_cast<f32>((hero_value(x, y, at, scene) - 16.0) / 219.0));
    }
    return srgb_encode(static_cast<f32>(acc / kInstants)) * 219.0 + 16.0;
}

FlowScore twixtor_scene(const HeroScene& scene, const char* tag) {
    FlowTimeRig rig(hero_clip(scene));
    EffectInstance* fx = rig.add(effect_keys::kFlowRetime);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return {};
    rig.render(9);
    return score_hero(rig, scene, 4.5, tag, [&](f64 x, f64 y) { return hero_value(x, y, 4.5, scene); });
}

FlowScore rsmb_scene(const HeroScene& scene, const char* tag) {
    FlowTimeRig rig(hero_clip(scene));
    EffectInstance* fx = rig.add(effect_keys::kFlowMotionBlur);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return {};
    rig.render(4);
    return score_hero(rig, scene, 4.0, tag, [&](f64 x, f64 y) { return hero_shutter_truth(x, y, 4.0, 0.25, scene); });
}
}

AUREA_TEST(FlowQualityGpu, TwixtorScenes) {
    AUREA_REQUIRE_GPU();
    // Metas tiradas da comparação com o código anterior (5,1 / 18,1 / 3,8 / 5,4).
    AUREA_CHECK(twixtor_scene(HeroScene{}, "twixtor-grande").ring < 6.0);
    (void)twixtor_scene(small_fast_object(), "twixtor-pequeno-rapido");
    AUREA_CHECK(twixtor_scene(low_texture_object(), "twixtor-pouca-textura").ring < 5.0);
    // Xadrez: o mesmo desenho a cada 80 px deixa o vetor ambíguo. Só medido;
    // continua sendo o caso que o fluxo não resolve.
    (void)twixtor_scene(repeating_object(), "twixtor-xadrez");
    const FlowScore pan = twixtor_scene(whip_pan(), "twixtor-panoramica");
    AUREA_CHECK(pan.ring < 12.0);
    AUREA_CHECK(pan.rest < 2.0);
    AUREA_CHECK(twixtor_scene(tracked_subject(), "twixtor-sujeito-seguido").ring < 5.0);
}

AUREA_TEST(FlowQualityGpu, RsmbScenes) {
    AUREA_REQUIRE_GPU();
    HeroScene still;
    still.pan = 0.0;
    // Metas tiradas da comparação com o código anterior (1,4 / 1,9 / 4,3 / 2,2).
    AUREA_CHECK(rsmb_scene(still, "rsmb-fundo-parado").ring < 1.6);
    const FlowScore big = rsmb_scene(HeroScene{}, "rsmb-grande");
    AUREA_CHECK(big.edge < 1.2);
    HeroScene smallStill = small_fast_object();
    smallStill.pan = 0.0;
    AUREA_CHECK(rsmb_scene(smallStill, "rsmb-pequeno-rapido").ring < 3.5);
    (void)rsmb_scene(low_texture_object(), "rsmb-pouca-textura");
    (void)rsmb_scene(repeating_object(), "rsmb-xadrez");
    AUREA_CHECK(rsmb_scene(tracked_subject(), "rsmb-sujeito-seguido").ring < 2.0);
}

// Câmera lenta com desfoque: o campo é do quadro A e a imagem está no instante t.
AUREA_TEST(FlowQualityGpu, TwixtorWithMotionBlurStaysOnTheObject) {
    AUREA_REQUIRE_GPU();
    const HeroScene scene;
    FlowTimeRig rig(hero_clip(scene));
    EffectInstance* fx = rig.add(effect_keys::kFlowRetime);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    fx->params[3].constant = ParamValue::scalar(100.0f);
    rig.render(15);
    const FlowScore s = score_hero(rig, scene, 7.5, "twixtor-com-desfoque", [&](f64 x, f64 y) { return hero_shutter_truth(x, y, 7.5, 0.125, scene); });
    AUREA_CHECK(s.ring < 6.0);
}

// Corte seco entre os dois quadros: nada de vetor inventado.
AUREA_TEST(FlowQualityGpu, SceneCutUsesNoVectors) {
    AUREA_REQUIRE_GPU();
    const HeroScene scene;
    SyntheticConfig cfg = hero_clip(scene);
    cfg.heroCutAt = 8;
    auto meanError = [&](const FlowTimeRig& rig, f64 src, bool after) {
        f64 acc = 0.0;
        u32 n = 0;
        for (u32 y = 8; y < rig.h - 8; y += 4)
            for (u32 x = 8; x < rig.w - 8; x += 4) {
                const f64 shot = hero_value(x + (after ? 613.0 : 0.0), y, src, scene);
                acc += std::fabs(rig.luma(x, y) - (after ? 251.0 - shot : shot));
                ++n;
            }
        return acc / std::max(1u, n);
    };
    {
        FlowTimeRig rig(cfg);
        AUREA_CHECK(rig.add(effect_keys::kFlowRetime) != nullptr);
        rig.render(15);   // fonte 7,5: entre o último quadro antes do corte e o primeiro depois
        const f64 before = meanError(rig, 7.0, false), after = meanError(rig, 8.0, true);
        std::printf("    corte no twixtor: erro para o quadro anterior %.2f, para o seguinte %.2f\n", before, after);
        AUREA_CHECK(std::min(before, after) < 3.0);
    }
    {
        FlowTimeRig rig(cfg);
        AUREA_CHECK(rig.add(effect_keys::kFlowMotionBlur) != nullptr);
        rig.render(7);
        const f64 sharp = meanError(rig, 7.0, false);
        std::printf("    corte no rsmb: erro para o quadro nitido %.2f\n", sharp);
        AUREA_CHECK(sharp < 3.0);
    }
}
