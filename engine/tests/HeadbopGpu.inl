// =============================================================================
//  Aurea / tests / HeadbopGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  Headbop na GPU: golden de um quadro deformado (pose fixa pelos canais de
//  keyframe, sem recorte: não depende do afinamento do motor de movimento nem
//  da rede), export = preview pixel a pixel (com o recorte da pessoa quando o
//  modelo existe), modo quadro inteiro sem rosto e o custo do passe em 1080×1920.
// =============================================================================
#include "aurea/effects/Headbop.hpp"

namespace headbop_gpu {

namespace hb = aurea::headbop;

/// "Pessoa" sintética: fundo listrado, tronco azul e cabeça cor de pele com
/// olhos — o bastante para ver cabeça, pescoço e fundo se mexendo.
ImagePixels person_image(u32 W, u32 H) {
    ImagePixels px = uniform_image(W, H, 0, 0, 0);
    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * W + x) * 4];
            const f32 fx = (x + 0.5f) / W, fy = (y + 0.5f) / H;
            f32 r = 0.25f + 0.5f * fy, g = 0.55f, b = 0.3f + 0.3f * fx;
            if ((x / 6) % 2 == 0) { r *= 0.8f; g *= 0.8f; }
            const f32 hx = (fx - 0.5f) / 0.17f, hy = (fy - 0.3f) / 0.11f;
            const bool torso = fy > 0.47f && std::fabs(fx - 0.5f) < 0.36f - 0.1f * std::max(0.0f, 0.55f - fy) * 5.0f;
            const bool neck = fy > 0.38f && fy <= 0.5f && std::fabs(fx - 0.5f) < 0.07f;
            if (torso) { r = 0.1f; g = 0.2f; b = 0.85f; }
            if (neck || hx * hx + hy * hy < 1.0f) { r = 0.93f; g = 0.72f; b = 0.58f; }
            const f32 ex = std::fabs(fx - 0.5f) - 0.06f, ey = fy - 0.29f;
            if (ex * ex * W * W + ey * ey * H * H < 4.0f) { r = g = b = 0.05f; }
            if ((y % 20) == 0) { r = g = b = 0.1f; }
            p[0] = static_cast<u8>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            p[1] = static_cast<u8>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            p[2] = static_cast<u8>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            p[3] = 255;
        }
    }
    return px;
}

/// O rig do desenho acima pelo MESMO caminho da análise (33 pontos → rosto).
void set_rig(EffectInstance& e, u32 W, u32 H) {
    std::vector<f32> pts(33 * 4, 0.0f);
    auto put = [&](u32 i, f32 x, f32 y) { pts[i * 4] = x; pts[i * 4 + 1] = y; pts[i * 4 + 3] = 0.95f; };
    put(2, 0.44f, 0.29f); put(5, 0.56f, 0.29f); put(0, 0.5f, 0.32f);
    put(7, 0.35f, 0.30f); put(8, 0.65f, 0.30f); put(9, 0.47f, 0.35f); put(10, 0.53f, 0.35f);
    put(11, 0.22f, 0.52f); put(12, 0.78f, 0.52f);
    hb::RigFace f;
    AUREA_CHECK(hb::face_from_landmarks(pts.data(), static_cast<f32>(W) / H, f));
    e.params[hb::kRigState].constant = ParamValue::scalar(2.0f);
    const u32 b = hb::kRigFirst;
    e.params[b + 0].constant = ParamValue::vec2(f.center.x, f.center.y);
    e.params[b + 1].constant = ParamValue::vec2(f.axes.x, f.axes.y);
    e.params[b + 2].constant = ParamValue::vec2(f.roll, f.yaw);
    e.params[b + 3].constant = ParamValue::vec2(f.pitch, f.depth);
    e.params[b + 4].constant = ParamValue::vec2(f.neck.x, f.neck.y);
    e.params[b + 5].constant = ParamValue::vec2(f.shoulderA.x, f.shoulderA.y);
    e.params[b + 6].constant = ParamValue::vec2(f.shoulderB.x, f.shoulderB.y);
}

/// Pose fixa pelos canais "convertidos em keyframes" (sem trilha: a constante).
void set_pose(EffectInstance& e, const hb::Pose& p) {
    e.params[hb::kPoseSource].constant = ParamValue::scalar(static_cast<f32>(hb::PoseSource::Keyframes));
    const auto ch = hb::pose_channels(p);
    for (u32 c = 0; c < hb::kPoseChannels; ++c) e.params[hb::bake_param(c)].constant = ParamValue::scalar(ch[c]);
}

hb::Pose nod_pose() {
    hb::Pose p;
    p.torsoDx = 0.012f;
    p.torsoDy = 0.01f;
    p.torsoRoll = 2.5f;
    p.headDy = 0.015f;
    p.headPitch = 16.0f;
    p.headRoll = -7.0f;
    p.headYaw = 10.0f;
    return p;
}

f32 hb_max_diff(const FloatImage& a, const FloatImage& b) {
    f32 r = 0.0f;
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i) r = std::max(r, std::fabs(a.px[i] - b.px[i]));
    return r;
}

f32 hb_region_diff(const FloatImage& a, const FloatImage& b, u32 x0, u32 y0, u32 x1, u32 y1) {
    f32 r = 0.0f;
    u32 n = 0;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x, ++n)
            for (u32 c = 0; c < 3; ++c) r += std::fabs(a.at(x, y)[c] - b.at(x, y)[c]);
    return n ? r / (3.0f * n) : 0.0f;
}

} // namespace headbop_gpu

AUREA_TEST(Gpu, HeadbopDeformedFrameGolden) {
    AUREA_REQUIRE_GPU();
    using namespace headbop_gpu;
    constexpr u32 W = 96, H = 160;
    Scene base(W, H);
    base.image(person_image(W, H), W * 0.5f, H * 0.5f);
    const FloatImage plain = base.render();

    Scene s(W, H);
    const LayerId id = s.image(person_image(W, H), W * 0.5f, H * 0.5f);
    EffectInstance& e = s.add_effect(id, hb::kHeadbopKey);
    AUREA_CHECK_EQ(e.params.size(), static_cast<usize>(hb::kParamCount));
    e.params[hb::kCutout].constant = ParamValue::boolean(false);
    set_rig(e, W, H);
    set_pose(e, hb::Pose{});
    // Pose neutra: o quadro sai igual (só a reamostragem da grade).
    const FloatImage still = s.render();
    std::printf("    pose neutra: diferença máx %.5f ", hb_max_diff(still, plain));
    AUREA_CHECK(hb_max_diff(still, plain) < 2.0f / 255.0f);

    set_pose(s.comp->layer(id)->effects.back(), nod_pose());
    const FloatImage moved = s.render();
    for (f32 v : moved.px) AUREA_CHECK(std::isfinite(v));
    const f32 head = hb_region_diff(moved, plain, 30, 30, 66, 62);
    const f32 corner = hb_region_diff(moved, plain, 0, 0, 2, 2) + hb_region_diff(moved, plain, W - 2, H - 2, W, H);   // a borda fica presa
    std::printf("cabeça %.4f, cantos %.5f (média) ", head, corner);
    AUREA_CHECK(head > 0.03f);
    AUREA_CHECK(corner < 0.002f);
    check_golden("headbop_deformed_frame", moved);
}

AUREA_TEST(Gpu, HeadbopWholeFrameWithoutAFace) {
    AUREA_REQUIRE_GPU();
    using namespace headbop_gpu;
    constexpr u32 W = 128, H = 72;
    Scene base(W, H);
    base.image(reference_image(W, H), W * 0.5f, H * 0.5f);
    const FloatImage plain = base.render();
    Scene s(W, H);
    const LayerId id = s.image(reference_image(W, H), W * 0.5f, H * 0.5f);
    EffectInstance& e = s.add_effect(id, hb::kHeadbopKey);
    hb::Pose p;
    p.torsoDx = 0.03f;
    p.torsoRoll = 3.0f;
    set_pose(e, p);   // rig vazio (sem análise): quadro inteiro
    const FloatImage moved = s.render();
    const f32 center = hb_region_diff(moved, plain, 48, 20, 80, 52);
    const f32 border = hb_region_diff(moved, plain, 0, 0, W, 1);   // a linha da borda fica presa
    std::printf("    centro %.4f, borda de cima %.5f ", center, border);
    AUREA_CHECK(center > 0.02f);
    AUREA_CHECK(border < 0.01f);
    for (f32 v : moved.px) AUREA_CHECK(std::isfinite(v));
    // Sem nada: nenhum pixel transparente aparece (o fundo é preto, a foto é opaca).
    u32 holes = 0;
    for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) holes += moved.at(x, y)[3] < 0.99f ? 1u : 0u;
    AUREA_CHECK_EQ(holes, 0u);
}

AUREA_TEST(Gpu, HeadbopExportMatchesPreview) {
    AUREA_REQUIRE_GPU();
    using namespace headbop_gpu;
    constexpr u32 W = 120, H = 200;
    for (const bool cutout : {false, true}) {
        Scene s(W, H);
        const LayerId id = s.image(person_image(W, H), W * 0.5f, H * 0.5f);
        EffectInstance& e = s.add_effect(id, hb::kHeadbopKey);
        set_rig(e, W, H);
        e.params[hb::kCutout].constant = ParamValue::boolean(cutout);
        e.params[hb::kBeatSource].constant = ParamValue::scalar(static_cast<f32>(hb::BeatSource::Manual));
        e.params[hb::kBpm].constant = ParamValue::scalar(118.0f);
        e.params[hb::kIntensity].constant = ParamValue::scalar(180.0f);
        e.params[hb::kTurn3d].constant = ParamValue::scalar(60.0f);
        e.params[hb::kMotionBlur].constant = ParamValue::boolean(true);
        // A qualidade final espera o recorte (como a exportação); a prévia
        // depois lê o mesmo recorte do cache.
        const FrameIndex t{7};
        const FloatImage fin = s.render(t, 1, true);
        FloatImage prev = s.render(t, 1, false);
        for (int i = 0; i < 50 && hb_max_diff(prev, fin) > 1.0f / 255.0f; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            prev = s.render(t, 1, false);
        }
        const f32 d = hb_max_diff(prev, fin);
        std::printf("    recorte %s: export x preview %.6f ", cutout ? "ligado" : "desligado", d);
        AUREA_CHECK(d <= 1.0f / 255.0f);
        for (f32 v : fin.px) AUREA_CHECK(std::isfinite(v));
        // Saídas "só a pessoa": transparente fora dela (com o recorte).
        if (cutout) {
            s.comp->layer(id)->effects.back().params[hb::kOutput].constant = ParamValue::scalar(1.0f);
            const FloatImage only = s.render(t, 1, true);
            const FloatImage onlyPrev = s.render(t, 1, false);
            AUREA_CHECK(hb_max_diff(only, onlyPrev) <= 1.0f / 255.0f);
            u32 transparent = 0;
            for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) transparent += only.at(x, y)[0] + only.at(x, y)[1] + only.at(x, y)[2] < 0.02f ? 1u : 0u;
            std::printf("transparente %.2f ", static_cast<f32>(transparent) / (W * H));
        }
    }
}

#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(Gpu, HeadbopPassCostAt1080x1920) {
    AUREA_REQUIRE_GPU();
    using namespace headbop_gpu;
    constexpr u32 W = 1080, H = 1920;
    for (const bool cutout : {false, true}) {
        Scene s(W, H);
        const LayerId id = s.image(person_image(W, H), W * 0.5f, H * 0.5f);
        EffectInstance& e = s.add_effect(id, hb::kHeadbopKey);
        set_rig(e, W, H);
        e.params[hb::kCutout].constant = ParamValue::boolean(cutout);
        e.params[hb::kBeatSource].constant = ParamValue::scalar(1.0f);
        (void)s.render(FrameIndex{3}, 1, true);   // espera o recorte (cache)
        Gpu& g = gpu();
        RenderSettings rs;
        rs.previewDenominator = 1;
        rs.dither = false;
        rs.gpuTimers = true;
        std::vector<f32> effectMs, fillMs;
        static u64 frameNo = 990000;
        for (u32 k = 0; k < 14; ++k) {
            FrameSnapshot snap;
            g.renderer.prepare(*s.comp, s.project, FrameIndex{static_cast<i64>(3 + k)}, &s.media, &Scene::lookup, &s, rs,
                               ++frameNo, 0, DecodeMode::Still, 1.0f, snap);
            OffscreenTarget off{g.target(W, H), W, H};
            FrameStats stats;
            RenderTimings timings;
            AUREA_CHECK(g.renderer.render(snap, rs, &off, stats, timings).ok());
            GpuTiming passes[512];
            f32 total = 0;
            const u32 n = g.backend.read_gpu_timings(passes, 512, &total);
            f32 geo = 0, fill = 0;
            for (u32 i = 0; i < n; ++i) {
                const char* label = passes[i].label ? passes[i].label : "";
                if (std::strcmp(label, hb::kHeadbopKey) == 0) geo += passes[i].ms;
                else if (std::strncmp(label, "HeadbopFundo", 12) == 0) fill += passes[i].ms;
            }
            if (k >= 4) { effectMs.push_back(geo); fillMs.push_back(fill); }
        }
        g.backend.wait_idle();
        std::sort(effectMs.begin(), effectMs.end());
        std::sort(fillMs.begin(), fillMs.end());
        const u32 gx = cutout ? hb::kGridXLoose : hb::kGridX, gy = cutout ? hb::kGridYLoose : hb::kGridY;   // cabelo/roupa só com recorte
        std::printf("\n    Headbop 1080x1920 recorte %s: passe da pessoa %.3f ms (mediana), fundo %.3f ms, %u vertices ",
                    cutout ? "ligado" : "desligado", effectMs[effectMs.size() / 2], fillMs[fillMs.size() / 2], hb::vertex_count(gx, gy));
    }
}
#endif

// Pessoa recortada sobre fundo TRANSPARENTE (PNG sem fundo): o buraco que a
// cabeça deixa ao se mexer recebe o alfa do fundo preenchido (o 2º push-pull,
// de alfa) — transparente, não uma silhueta preta opaca.
AUREA_TEST(Gpu, HeadbopTransparentBackgroundHoleStaysTransparent) {
    AUREA_REQUIRE_GPU();
    using namespace headbop_gpu;
    constexpr u32 W = 120, H = 200;
    ImagePixels px = person_image(W, H);
    // Só a pessoa (cabeça, pescoço, tronco) fica opaca; o resto vira transparente.
    u32 sourceOpaque = 0;
    for (u32 y = 0; y < H; ++y) {
        for (u32 x = 0; x < W; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * W + x) * 4];
            const bool skin = p[0] > 200 && p[1] > 150 && p[2] > 120 && p[2] < 170;
            const bool shirt = p[0] < 40 && p[2] > 200;
            const bool eye = p[0] < 20 && p[1] < 20 && p[2] < 20 && y < H / 2 && x > W / 3 && x < 2 * W / 3;
            if (!(skin || shirt || eye)) p[0] = p[1] = p[2] = p[3] = 0;
            else ++sourceOpaque;
        }
    }
    auto render = [&](bool cutout, const hb::Pose& pose) {
        Scene s(W, H);
        s.comp->set_background(Color{1, 0, 1, 1});   // magenta: aparece onde a camada é transparente
        const LayerId id = s.image(px, W * 0.5f, H * 0.5f);
        EffectInstance& e = s.add_effect(id, hb::kHeadbopKey);
        set_rig(e, W, H);
        e.params[hb::kCutout].constant = ParamValue::boolean(cutout);
        set_pose(e, pose);
        return s.render(FrameIndex{0}, 1, true);   // qualidade final: espera o recorte
    };
    hb::Pose pose;
    pose.headDx = 0.06f;      // a cabeça vai bem para o lado: o lugar dela fica vazio
    pose.headRoll = 12.0f;
    const FloatImage with = render(true, pose);
    const FloatImage without = render(false, pose);
    // A saída da composição é opaca: o fundo MAGENTA aparece onde a camada é
    // transparente. A silhueta do bug era preta (cor preenchida com alfa 1).
    u32 sourceDark = 0, dark = 0, magenta = 0, sourceClear = 0;
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            const u8* p = &px.rgba[(static_cast<usize>(y) * W + x) * 4];
            sourceDark += p[3] > 128 && p[0] < 20 && p[1] < 20 && p[2] < 20 ? 1u : 0u;
            sourceClear += p[3] == 0 ? 1u : 0u;
            const f32* o = with.at(x, y);
            dark += o[0] < 0.06f && o[1] < 0.06f && o[2] < 0.06f ? 1u : 0u;
            magenta += o[0] > 0.9f && o[1] < 0.1f && o[2] > 0.9f ? 1u : 0u;
        }
    for (f32 v : with.px) AUREA_CHECK(std::isfinite(v));
    const bool masked = hb_max_diff(with, without) > 1.0f / 255.0f;
    std::printf("    escuros: fonte %u, saída %u; magenta (transparente) %u de %u%s ", sourceDark, dark, magenta, sourceClear,
                masked ? "" : " (sem modelo de recorte: só a deformação)");
    AUREA_CHECK(dark <= sourceDark + W * H / 100);
    AUREA_CHECK(magenta + W * H / 50 >= sourceClear);
    AUREA_CHECK(with.at(2, 2)[0] > 0.9f && with.at(2, 2)[1] < 0.1f);
}

#include "HeadbopDemo.inl"
