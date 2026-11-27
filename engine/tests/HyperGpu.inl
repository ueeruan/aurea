// =============================================================================
//  AureaHyper (0.3.2) — os caminhos TURBO na GPU (Vulkan do host). Incluído
//  em test_gpu.cpp DEPOIS de EnhanceTests.inl (usa Scene, gpu(), FloatImage
//  e o vídeo de função `enh::FnFactory`).
//
//  Medido no pixel: FSR mais nítido que o bilinear sem anel além do limite;
//  ruído temporal menor sem rastro do quadrado que anda; pente desfeito só no
//  entrelaçado; o quadrado no meio do caminho no quadro interpolado (60 fps e
//  câmera lenta dentro da camada). O custo em 1080p de cada módulo é impresso.
// =============================================================================
#include "aurea/hyper/Hyper.hpp"
#include "hyper/HyperAnalysis.hpp"

namespace {
namespace hy {

u32 param(const char* key, const char* id) {
    const ParameterRegistry* p = gpu().effects.params(effect_type_id(key));
    AUREA_CHECK_MSG(p != nullptr, key);
    if (!p) return kInvalidIndex;
    const u32 i = p->find(id);
    AUREA_CHECK_MSG(i != kInvalidIndex, id);
    return i;
}
void set(EffectInstance& e, const char* key, const char* id, f32 v) {
    const u32 i = param(key, id);
    if (i < e.params.size()) e.params[i].constant = ParamValue::scalar(v);
}

/// Luma sRGB (0..255) de um pixel renderizado.
f64 code(const FloatImage& img, u32 x, u32 y) {
    const Vec4 c = img.v(x, y);
    const f64 a = c.w > 1e-5f ? 1.0 / c.w : 0.0;
    return 255.0 * (0.2126 * srgb_encode(static_cast<f32>(c.x * a)) + 0.7152 * srgb_encode(static_cast<f32>(c.y * a))
                    + 0.0722 * srgb_encode(static_cast<f32>(c.z * a)));
}

/// Cruzamento da metade entre `lo` e `hi` na linha y (subpixel), subindo.
f64 rise(const FloatImage& img, u32 y, f64 lo, f64 hi, u32 from, u32 to) {
    const f64 mid = 0.5 * (lo + hi);
    for (u32 x = from; x + 1 < to; ++x) {
        const f64 a = code(img, x, y), b = code(img, x + 1, y);
        if (a < mid && b >= mid) return x + (mid - a) / std::max(1e-6, b - a);
    }
    return -1.0;
}
f64 mean_rise(const FloatImage& img, u32 y0, u32 y1, f64 lo, f64 hi, u32 from, u32 to) {
    f64 s = 0;
    u32 n = 0;
    for (u32 y = y0; y < y1; ++y)
        if (const f64 c = rise(img, y, lo, hi, from, to); c >= 0) { s += c; ++n; }
    return n ? s / n : -1.0;
}

/// Comb score do quadro renderizado (a mesma métrica do C++/shader).
hyper::CombScore comb_of(const FloatImage& img) {
    std::vector<u8> l(static_cast<usize>(img.width) * img.height);
    for (u32 y = 0; y < img.height; ++y)
        for (u32 x = 0; x < img.width; ++x) l[static_cast<usize>(y) * img.width + x] = static_cast<u8>(std::lround(std::clamp(code(img, x, y), 0.0, 255.0)));
    return hyper::comb_score(l.data(), img.width, img.height, img.width);
}

/// Quadrado claro que anda `speed` px/quadro (x0 + speed·i), sobre um fundo
/// com textura suave (o que a busca de blocos precisa para achar o fundo).
u8 square_scene(u32 x, u32 y, u32 i, f64 x0, f64 speed, u32 size, u32 top) {
    const f64 left = x0 + speed * i;
    if (x >= left && x < left + size && y >= top && y < top + size) return 225;
    const f64 bg = 90.0 + 18.0 * std::sin(x * 0.19) * std::cos(y * 0.23) + 10.0 * std::sin((x + 2.0 * y) * 0.07);
    return enh::to_code(bg);
}

} // namespace hy
} // namespace

AUREA_TEST(HyperGpu, DeclaresTheThreeLayerEffects) {
    for (const char* key : {hyper::effect_keys::kHyperUpscale, hyper::effect_keys::kHyperClean, hyper::effect_keys::kHyperMotion}) {
        const Effect* fx = gpu().effects.find(effect_type_id(key));
        AUREA_CHECK_MSG(fx != nullptr, key);
        if (!fx) continue;
        AUREA_CHECK(std::strcmp(fx->info().category, "Hyper") == 0);
        AUREA_CHECK_MSG(hy::param(key, "path") != kInvalidIndex, "todo efeito Hyper tem o caminho");
    }
    const Effect* clean = gpu().effects.find(effect_type_id(hyper::effect_keys::kHyperClean));
    AUREA_CHECK(clean && clean->wants_neighbors());
}

AUREA_TEST(HyperGpu, AureaDetailIsSharperThanBilinearAndDoesNotRing) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 64, H = 36;
    // Degrau vertical (40 | 200) e um diagonal: o que o upscale borra.
    ImagePixels px = uniform_image(W, H, 40, 40, 40);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * W + x) * 4];
            const bool bright = x >= 32 || (x + y) % 23 < 3;
            p[0] = p[1] = p[2] = bright ? 200 : 40;
        }
    auto render = [&](bool fsr) {
        Scene s(W * 2, H * 2);
        const LayerId id = s.image(px, W, H, 2.0f);
        if (fsr) {
            EffectInstance& e = s.add_effect(id, hyper::effect_keys::kHyperUpscale);
            hy::set(e, hyper::effect_keys::kHyperUpscale, "sharpness", 50.0f);
        }
        return s.render(FrameIndex{0}, 1, true);
    };
    const FloatImage bil = render(false), detail = render(true);
    // Nitidez: o maior degrau entre vizinhos na borda vertical (linhas 4..24).
    auto steepest = [](const FloatImage& img) {
        f64 best = 0.0;
        for (u32 y = 4; y < 24; ++y)
            for (u32 x = 56; x < 72; ++x) best = std::max(best, hy::code(img, x + 1, y) - hy::code(img, x, y));
        return best;
    };
    const f64 sb = steepest(bil), sf = steepest(detail);
    std::printf("[degrau bilinear %.1f, Detail %.1f] ", sb, sf);
    AUREA_CHECK_MSG(sf > 1.05 * sb && sf < 1.22 * sb, "moderately sharper edges without excessive contrast");
    // Sem anel: nada passa do intervalo da fonte além de 3 códigos.
    f64 lo = 1e9, hi = -1e9;
    for (u32 y = 2; y < detail.height - 2; ++y)
        for (u32 x = 2; x < detail.width - 2; ++x) {
            const f64 c = hy::code(detail, x, y);
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
    std::printf("[faixa Detail %.1f..%.1f] ", lo, hi);
    AUREA_CHECK_MSG(lo >= 38.0 && hi <= 202.0, "sem anel além do limite");
    // A parte lisa continua lisa.
    AUREA_CHECK_NEAR(hy::code(detail, 8, 10), 40.0, 2.0);
    AUREA_CHECK_NEAR(hy::code(detail, 110, 10), 200.0, 2.0);
}

AUREA_TEST(HyperGpu, AureaDetailPreservesCutoutAndPremultipliedColor) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 64, H = 36;
    ImagePixels px = uniform_image(W, H, 250, 45, 25);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x)
            px.rgba[(static_cast<usize>(y) * W + x) * 4 + 3] = x < W / 2 ? 255 : 0;
    auto render = [&](bool detail) {
        Scene s(W * 2, H * 2);
        s.comp->set_background_choice(Color{0, 0, 0, 0});
        const auto layer = s.image(px, W, H, 2.0f);
        if (detail) s.add_effect(layer, hyper::effect_keys::kHyperUpscale);
        return s.render(FrameIndex{0}, 1, true);
    };
    const FloatImage before = render(false), after = render(true);
    for (u32 x = W - 4; x < W + 4; ++x) {
        const Vec4 a = before.v(x, H), b = after.v(x, H);
        AUREA_CHECK_NEAR(a.w, b.w, 0.003f);
        AUREA_CHECK_NEAR(a.x, b.x, 0.003f);
    }
}

AUREA_TEST(HyperGpu, AureaDetailUsesOutputResolutionAndHonorsZeroStrength) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 64, H = 36;
    ImagePixels px = uniform_image(W, H, 96, 96, 96);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            const u8 gray = x < 30 ? 96 : x < 34 ? static_cast<u8>(100 + (x - 30) * 15) : 160;
            u8* p = &px.rgba[(static_cast<usize>(y) * W + x) * 4];
            p[0] = p[1] = p[2] = gray;
        }
    const auto render = [&](bool largerComposition, f32 strength) {
        const u32 scale = largerComposition ? 2 : 1;
        Scene s(W * scale, H * scale);
        const LayerId id = s.image(px, W * scale * .5f, H * scale * .5f, static_cast<f32>(scale));
        if (strength >= 0.0f) {
            EffectInstance& e = s.add_effect(id, hyper::effect_keys::kHyperUpscale);
            hy::set(e, hyper::effect_keys::kHyperUpscale, "sharpness", strength);
        }
        return s.render(FrameIndex{0}, 1, true, {}, largerComposition ? 1 : 2);
    };
    const auto off = render(false, -1), zero = render(false, 0);
    const auto scaledExport = render(false, 100), scaledLayer = render(true, 100);
    f64 peakChange = 0;
    for (u32 x = 48; x < 80; ++x) {
        const f64 base = hy::code(off, x, H), exportDetail = hy::code(scaledExport, x, H);
        AUREA_CHECK_NEAR(base, hy::code(zero, x, H), 0.1);
        AUREA_CHECK_NEAR(exportDetail, hy::code(scaledLayer, x, H), 0.5);
        AUREA_CHECK(exportDetail >= 95.0 && exportDetail <= 161.0);
        peakChange = std::max(peakChange, std::abs(base - exportDetail));
    }
    AUREA_CHECK_MSG(peakChange >= 3.0, "the upscaled result restores a measurable soft edge");
}

AUREA_TEST(HyperGpu, AureaEnchanceAppliesLearnedFilterAndPreservesMatte) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 63, H = 35;
    ImagePixels px = uniform_image(W, H, 64, 105, 150);
    for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) {
        u8* p = &px.rgba[(static_cast<usize>(y) * W + x) * 4];
        if (x < 7) p[3] = 0;
        else if ((3 * x + 2 * y) % 17 < 6) { p[0] = 190; p[1] = 158; p[2] = 100; }
    }
    const auto render = [&](const char* key) {
        Scene s(W * 2, H * 2);
        s.comp->set_background_choice(Color{0, 0, 0, 0});
        const auto id = s.image(px, W, H, 2.0f);
        if (key) s.add_effect(id, key);
        return s.render(FrameIndex{0}, 1, true);
    };
    const auto baseline = render(nullptr);
    const auto detail = render(hyper::effect_keys::kHyperUpscale);
    const auto enchance = render(hyper::effect_keys::kHyperEnchance);
    f64 changed = 0.0;
    for (u32 x = 0; x < W * 2; ++x) {
        AUREA_CHECK_NEAR(enchance.v(x, H).w, baseline.v(x, H).w, 0.002f);
        if (x < 18) AUREA_CHECK_NEAR(enchance.v(x, H).x, baseline.v(x, H).x, 0.002f);
        if (x > 24) changed += std::abs(hy::code(enchance, x, H) - hy::code(detail, x, H));
    }
    AUREA_CHECK_MSG(changed > 35.0, "the learned filter ran instead of the Aurea Detail fallback");
}

AUREA_TEST(HyperGpu, AnimeUpscaleKeepsLinesDarkAndThin) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 64, H = 36;
    ImagePixels px = uniform_image(W, H, 230, 220, 200);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x)
            if (x == 20 || x == 21 || (x + y == 50)) {
                u8* p = &px.rgba[(static_cast<usize>(y) * W + x) * 4];
                p[0] = p[1] = p[2] = 25;
            }
    auto render = [&](bool anime) {
        Scene s(W * 2, H * 2);
        const LayerId id = s.image(px, W, H, 2.0f);
        if (anime) {
            EffectInstance& e = s.add_effect(id, hyper::effect_keys::kHyperUpscale);
            hy::set(e, hyper::effect_keys::kHyperUpscale, "mode", 1.0f);
        }
        return s.render(FrameIndex{0}, 1, true);
    };
    const FloatImage bil = render(false), an = render(true);
    // Largura da linha vertical (abaixo da metade entre fundo e traço) na linha 10.
    auto width = [](const FloatImage& img) {
        u32 n = 0;
        for (u32 x = 30; x < 60; ++x) n += hy::code(img, x, 10) < 0.5 * (220.0 + 25.0) ? 1u : 0u;
        return n;
    };
    const u32 wb = width(bil), wa = width(an);
    std::printf("[traço: bilinear %u px, anime %u px] ", wb, wa);
    AUREA_CHECK(wa <= wb);
    f64 darkB = 1e9, darkA = 1e9;
    for (u32 x = 36; x < 50; ++x) { darkB = std::min(darkB, hy::code(bil, x, 10)); darkA = std::min(darkA, hy::code(an, x, 10)); }
    std::printf("[mais escuro: bilinear %.1f, anime %.1f] ", darkB, darkA);
    // O afinamento clareia a BORDA do traço; o miolo continua escuro.
    AUREA_CHECK_MSG(darkA <= darkB + 12.0, "o traço continua escuro");
    AUREA_CHECK_NEAR(hy::code(an, 100, 60), hy::code(bil, 100, 60), 2.0);   // área chapada intacta
}

AUREA_TEST(HyperGpu, TemporalDenoiseLowersNoiseWithoutSmearingTheMovingSquare) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 128, H = 72, N = 24;
    enh::FnFactory video(W, H, N, [](u32 x, u32 y, u32 i) {
        const f64 left = 20.0 + 4.0 * i;
        const bool sq = x >= left && x < left + 16 && y >= 24 && y < 40;
        return enh::to_code((sq ? 210.0 : 110.0) + 10.0 * enh::pixel_noise(x, y, i));
    });
    auto render = [&](bool clean) {
        Scene s(W, H);
        const LayerId id = enh::add_video(s, s.comp, video, W, H, N);
        if (clean) {
            EffectInstance& e = s.add_effect(id, hyper::effect_keys::kHyperClean);
            hy::set(e, hyper::effect_keys::kHyperClean, "denoise", 100.0f);
            hy::set(e, hyper::effect_keys::kHyperClean, "frames", 2.0f);
        }
        return s.render(FrameIndex{10}, 1, true);
    };
    const FloatImage raw = render(false), out = render(true);
    auto noise = [](const FloatImage& img) {
        f64 s = 0, s2 = 0;
        u32 n = 0;
        for (u32 y = 50; y < 68; ++y)
            for (u32 x = 4; x < 124; ++x) { const f64 c = hy::code(img, x, y); s += c; s2 += c * c; ++n; }
        const f64 m = s / n;
        return std::sqrt(std::max(0.0, s2 / n - m * m));
    };
    const f64 nb = noise(raw), na = noise(out);
    std::printf("[ruido %.2f -> %.2f] ", nb, na);
    AUREA_CHECK_MSG(na < 0.6 * nb, "o temporal tem de tirar pelo menos 40% do ruido do fundo parado");
    // O quadrado (x = 60..76 no quadro 10) no lugar e sem rastro atrás dele.
    const f64 edge = hy::mean_rise(out, 27, 37, 110.0, 210.0, 44, 70);
    std::printf("[borda %.2f] ", edge);
    AUREA_CHECK_NEAR(edge, 60.0, 1.0);
    f64 behind = 0;
    u32 n = 0;
    for (u32 y = 26; y < 38; ++y)
        for (u32 x = 46; x < 56; ++x) { behind += hy::code(out, x, y); ++n; }
    AUREA_CHECK_MSG(std::fabs(behind / n - 110.0) < 8.0, "sem fantasma do quadrado atrás dele");
}

AUREA_TEST(HyperGpu, DeinterlaceRemovesCombOnlyWhenInterlaced) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 128, H = 72, N = 8;
    // Barras andando 4 px/quadro; no entrelaçado as linhas ímpares são o
    // campo de meio quadro depois (+2 px).
    enh::FnFactory combed(W, H, N, [](u32 x, u32 y, u32 i) {
        const f64 shift = 4.0 * i + ((y & 1) ? 6.0 : 0.0);
        return static_cast<u8>((static_cast<i64>(std::floor((x + shift) / 8.0)) & 1) ? 190 : 60);
    });
    enh::FnFactory progressive(W, H, N, [](u32 x, u32, u32 i) {
        return static_cast<u8>((static_cast<i64>(std::floor((x + 4.0 * i) / 8.0)) & 1) ? 190 : 60);
    });
    auto render = [&](enh::FnFactory& f, u32 mode) {
        Scene s(W, H);
        const LayerId id = enh::add_video(s, s.comp, f, W, H, N);
        if (mode) {
            EffectInstance& e = s.add_effect(id, hyper::effect_keys::kHyperClean);
            hy::set(e, hyper::effect_keys::kHyperClean, "denoise", 0.0f);
            hy::set(e, hyper::effect_keys::kHyperClean, "deinterlace", static_cast<f32>(mode));
        }
        return s.render(FrameIndex{3}, 1, true);
    };
    const FloatImage cin = render(combed, 0), cout = render(combed, 1);
    const hyper::CombScore before = hy::comb_of(cin), after = hy::comb_of(cout);
    std::printf("[pente %.2f -> %.2f] ", before.ratio(), after.ratio());
    AUREA_CHECK(before.interlaced());
    AUREA_CHECK_MSG(!after.interlaced() && after.ratio() < 0.5 * before.ratio(), "o automático tem de desfazer o pente");
    // Progressivo no automático: nada muda (bit a bit).
    const FloatImage pin = render(progressive, 0), pout = render(progressive, 1);
    AUREA_CHECK_MSG(pin.px == pout.px, "progressivo passa intacto no automático");
}

AUREA_TEST(HyperGpu, MotionPutsTheMovingSquareHalfwayAt60FpsAndInSlowMotion) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 160, H = 90, N = 12;
    enh::FnFactory video(W, H, N, [](u32 x, u32 y, u32 i) { return hy::square_scene(x, y, i, 30.0, 8.0, 16, 30); });
    const f64 bg = 90.0;
    // 60 fps: o quadro 3 da composição é o instante 1,5 da fonte (entre 1 e 2).
    {
        Scene s(W, H, 60.0);
        const LayerId id = enh::add_video(s, s.comp, video, W, H, N);
        s.comp->layer(id)->end = FrameIndex{2 * N};
        (void)s.add_effect(id, hyper::effect_keys::kHyperMotion);
        const FloatImage exact = s.render(FrameIndex{2}, 1, true);   // fonte 1: x = 38
        const FloatImage mid = s.render(FrameIndex{3}, 1, true);     // fonte 1,5: x = 42
        const f64 e0 = hy::mean_rise(exact, 33, 43, bg, 225.0, 20, 60);
        const f64 e1 = hy::mean_rise(mid, 33, 43, bg, 225.0, 20, 70);
        std::printf("[60 fps: borda %.2f e %.2f] ", e0, e1);
        AUREA_CHECK_NEAR(e0, 38.0, 0.75);
        AUREA_CHECK_NEAR(e1, 42.0, 1.0);
        // Sem fantasma: na metade de cima do quadrado não há duas bordas.
        u32 crossings = 0;
        for (u32 x = 20; x + 1 < 80; ++x)
            crossings += (hy::code(mid, x, 38) < 157.0) != (hy::code(mid, x + 1, 38) < 157.0) ? 1u : 0u;
        AUREA_CHECK_EQ(crossings, 2u);
    }
    // Câmera lenta x2 dentro da camada (composição a 30): quadro 3 = fonte 1,5.
    {
        Scene s(W, H, 30.0);
        const LayerId id = enh::add_video(s, s.comp, video, W, H, N);
        s.comp->layer(id)->end = FrameIndex{2 * N};
        EffectInstance& e = s.add_effect(id, hyper::effect_keys::kHyperMotion);
        hy::set(e, hyper::effect_keys::kHyperMotion, "slow_motion", 1.0f);
        const FloatImage mid = s.render(FrameIndex{3}, 1, true);
        const f64 e1 = hy::mean_rise(mid, 33, 43, bg, 225.0, 20, 70);
        std::printf("[x2: borda %.2f] ", e1);
        AUREA_CHECK_NEAR(e1, 42.0, 1.0);
        const FloatImage four = s.render(FrameIndex{4}, 1, true);   // fonte 2: x = 46
        AUREA_CHECK_NEAR(hy::mean_rise(four, 33, 43, bg, 225.0, 20, 70), 46.0, 0.75);
    }
}

AUREA_TEST(HyperGpu, FullHdCostPerModulePrints) {
    AUREA_REQUIRE_GPU();
    constexpr u32 W = 1920, H = 1080;
    Gpu& g = gpu();
    static u64 frame = 1990000;
    std::vector<std::pair<std::string, f32>> breakdown;
    auto cost = [&](Scene& s, i64 t, f32 previewScale = 1.0f, bool finalQuality = true) {
        RenderSettings rs;
        rs.gpuTimers = true;
        rs.finalQuality = finalQuality;
        rs.heavyScale = previewScale;
        rs.dither = false;
        OffscreenTarget off{g.target(s.comp->width(), s.comp->height()), s.comp->width(), s.comp->height()};
        f32 best = 1e9f;
        for (int r = 0; r < 8; ++r) {
            FrameSnapshot snap;
            for (int attempt = 0; attempt < 6000; ++attempt) {
                g.renderer.prepare(*s.comp, s.project, FrameIndex{t}, &s.media, &Scene::lookup, &s, rs, ++frame, 0,
                                   DecodeMode::Still, 1.0f, snap);
                if (snap.missingVideoFrames == 0 && snap.staleVideoFrames == 0) break;
                for (RenderLayer& l : snap.layers) l.source.frame.reset();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            FrameStats stats;
            RenderTimings timings;
            AUREA_CHECK(g.renderer.render(snap, rs, &off, stats, timings).ok());
            g.backend.wait_idle();
            if (r > 1 && timings.gpuMeasured && timings.gpuTotalMs > 0.0f && timings.gpuTotalMs < best) {
                best = timings.gpuTotalMs;
                breakdown.clear();
                const GpuTiming* passes = nullptr;
                const u32 n = g.renderer.last_gpu_passes(passes);
                for (u32 k = 0; passes && k < n; ++k) {
                    const std::string label = passes[k].label ? passes[k].label : "?";
                    auto it = std::find_if(breakdown.begin(), breakdown.end(), [&](const auto& p) { return p.first == label; });
                    if (it == breakdown.end()) breakdown.emplace_back(label, passes[k].ms);
                    else it->second += passes[k].ms;
                }
            }
        }
        return best < 1e8f ? best : 0.0f;
    };
    auto print_breakdown = [&](const char* what) {
        std::printf("\n      por passe (%s):", what);
        for (const auto& [label, ms] : breakdown)
            if (ms >= 0.05f) std::printf("\n        %-28s %.3f ms", label.c_str(), ms);
    };
    // Upscale: fonte 960x540 ampliada para 1080p (480p->1080p no medidor).
    {
        enh::FnFactory video(960, 540, 30, [](u32 x, u32 y, u32 i) {
            return enh::to_code(pan_texture_value(x * 0.5, y * 0.5, i * 0.5));
        });
        Scene s(W, H);
        const LayerId id = enh::add_video(s, s.comp, video, 960, 540, 30);
        s.comp->layer(id)->transform.position = Vec3{W * 0.5f, H * 0.5f, 0};
        s.comp->layer(id)->transform.scale = Vec3{2, 2, 1};
        const f32 none = cost(s, 10);
        EffectInstance& e = s.add_effect(id, hyper::effect_keys::kHyperUpscale);
        const f32 general = cost(s, 10);
        hy::set(e, hyper::effect_keys::kHyperUpscale, "mode", 1.0f);
        const f32 anime = cost(s, 10);
        std::printf("\n    1080p (de 540p) GPU total: sem %.3f ms, Aurea Detail Geral +%.3f ms, Anime +%.3f ms",
                    none, general - none, anime - none);
    }
    // Clean e Motion em 1080p.
    {
        enh::FnFactory video(W, H, 30, [](u32 x, u32 y, u32 i) {
            return enh::to_code(pan_texture_value(x * 0.25, y * 0.25, i * 0.5) + 6.0 * enh::fast_noise(x, y, i));
        });
        Scene s(W, H, 60.0);
        const LayerId id = enh::add_video(s, s.comp, video, W, H, 30);
        s.comp->layer(id)->end = FrameIndex{60};
        const f32 none = cost(s, 20);
        EffectInstance& c = s.add_effect(id, hyper::effect_keys::kHyperClean);
        hy::set(c, hyper::effect_keys::kHyperClean, "denoise", 60.0f);
        hy::set(c, hyper::effect_keys::kHyperClean, "frames", 1.0f);
        const f32 dn3 = cost(s, 20);
        hy::set(c, hyper::effect_keys::kHyperClean, "frames", 2.0f);
        const f32 dn5 = cost(s, 20);
        print_breakdown("Clean 5 quadros");
        const f32 dnPreviewHalf = cost(s, 20, 0.5f, false);
        const f32 dnPreviewQuarter = cost(s, 20, 0.25f, false);
        std::printf("\n    Clean 5-frames adaptive preview: density 1/2 %.3f ms, density 1/4 %.3f ms", dnPreviewHalf, dnPreviewQuarter);
        hy::set(c, hyper::effect_keys::kHyperClean, "denoise", 0.0f);
        hy::set(c, hyper::effect_keys::kHyperClean, "deblock", 1.0f);
        const f32 deblock = cost(s, 20);
        hy::set(c, hyper::effect_keys::kHyperClean, "deblock", 0.0f);
        hy::set(c, hyper::effect_keys::kHyperClean, "deinterlace", 2.0f);
        const f32 deint = cost(s, 20);
        s.comp->layer(id)->effects.clear();
        (void)s.add_effect(id, hyper::effect_keys::kHyperMotion);
        // Quadro 21 = fonte 10,5: interpola. Sem o cache: campo + quadro; com
        // ele, o mesmo par (câmera lenta) só refaz o quadro do meio.
        g.renderer.set_flow_cache_enabled(false);
        const f32 motion = cost(s, 21);
        print_breakdown("Motion sem cache");
        const f32 motionPreviewHalf = cost(s, 21, 0.5f, false);
        const f32 motionPreviewQuarter = cost(s, 21, 0.25f, false);
        std::printf("\n    Motion adaptive preview without pair cache: density 1/2 %.3f ms, density 1/4 %.3f ms", motionPreviewHalf, motionPreviewQuarter);
        g.renderer.set_flow_cache_enabled(true);
        const f32 motionCached = cost(s, 21);
        std::printf("\n    1080p GPU total: sem %.3f ms; Clean denoise 3 quadros +%.3f ms, 5 quadros +%.3f ms, "
                    "deblock +%.3f ms, deinterlace +%.3f ms; Motion +%.3f ms (par em cache +%.3f ms)\n",
                    none, dn3 - none, dn5 - none, deblock - none, deint - none, motion - none, motionCached - none);
    }
}

AUREA_TEST(HyperAdaptive, ExportReferenceFrames) {
    const char* directory = std::getenv("AUREA_HYPER_FRAME_DIR");
    if (!directory || !*directory) return;
    AUREA_REQUIRE_GPU();
    std::filesystem::create_directories(directory);
    for (u32 kind = 0; kind < 3; ++kind) {
        enh::FnFactory video(384, 216, 30, [](u32 x, u32 y, u32 frame) {
            const f64 value = hy::square_scene(x, y, frame, 25, 7.5, 32, 75);
            const f64 cut = frame >= 12 ? 24.0 : 0.0;
            return enh::to_code(value + cut + 5.0 * enh::fast_noise(x, y, frame));
        });
        Scene scene(385, 217, kind == 2 ? 60.0 : 30.0);
        const LayerId id = enh::add_video(scene, scene.comp, video, 384, 216, 30);
        if (kind == 2) {
            scene.add_effect(id, hyper::effect_keys::kHyperMotion);
        } else {
            auto& effect = scene.add_effect(id, hyper::effect_keys::kHyperClean);
            hy::set(effect, hyper::effect_keys::kHyperClean, "denoise", 60.0f);
            hy::set(effect, hyper::effect_keys::kHyperClean, "frames", kind == 0 ? 1.0f : 2.0f);
            hy::set(effect, hyper::effect_keys::kHyperClean, "deblock", 1.0f);
        }
        for (i64 frame : {0, 1, 11, 23}) {
            const FloatImage image = scene.render(FrameIndex{frame}, 1, true);
            const auto path = std::filesystem::path(directory)
                / ("hyper-" + std::to_string(kind) + "-" + std::to_string(frame) + ".rgba32f");
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(image.px.data()),
                         static_cast<std::streamsize>(image.px.size() * sizeof(f32)));
            AUREA_CHECK(output.good());
            std::array<GpuTiming, 512> gpuTimes{};
            f32 gpuTotal = 0.0f;
            const u32 measured = gpu().backend.read_gpu_timings(gpuTimes.data(),
                static_cast<u32>(gpuTimes.size()), &gpuTotal);
            const auto& graph = gpu().renderer.frame_graph();
            std::ofstream report(path.string() + ".passes.tsv");
            const auto memory = gpu().backend.memory_stats();
            report << "# GPU-total-ms\t" << gpuTotal << "\n# GPU-used-bytes\t" << memory.usedBytes
                   << "\n# GPU-reserved-bytes\t" << memory.reservedBytes << '\n';
            report << "pass\tCPU-record-ms\tGPU-ms\tGPU-measured\testimated-read-bytes\testimated-write-bytes\tworking-set-bytes\n";
            for (u32 position = 0; position < graph.order().size(); ++position) {
                const u32 pass = graph.order()[position];
                const auto metrics = graph.pass_metrics(pass);
                const bool gpuMeasured = position < measured && gpuTimes[position].label
                    && std::strcmp(gpuTimes[position].label, graph.pass_name(pass)) == 0;
                report << graph.pass_name(pass) << '\t' << static_cast<f64>(metrics.cpuRecordNs) * 1e-6
                       << '\t' << (gpuMeasured ? gpuTimes[position].ms : 0.0f) << '\t' << gpuMeasured
                       << '\t' << metrics.estimatedReadBytes << '\t' << metrics.estimatedWriteBytes
                       << '\t' << metrics.workingSetBytes << '\n';
            }
            AUREA_CHECK(report.good());
        }
    }
}

AUREA_TEST(HyperAdaptive, PreviewMotionDensityChangesWithoutChangingFinalFrames) {
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    constexpr u32 width = 384, height = 216;
    for (bool motion : {false, true}) {
        enh::FnFactory video(width, height, 30, [](u32 x, u32 y, u32 frame) {
            return enh::to_code(hy::square_scene(x, y, frame, 20, 6.0, 32, 75)
                + 4.0 * enh::fast_noise(x, y, frame));
        });
        Scene scene(width, height, motion ? 60.0 : 30.0);
        const LayerId id = enh::add_video(scene, scene.comp, video, width, height, 30);
        scene.add_effect(id, motion ? hyper::effect_keys::kHyperMotion : hyper::effect_keys::kHyperClean);
        std::vector<u16> reference;
        u32 fullGrid = 0;
        for (u32 mode = 0; mode < 4; ++mode) {
            RenderSettings settings;
            settings.dither = false;
            settings.gpuTimers = true;
            settings.finalQuality = mode == 0 || mode == 3;
            settings.heavyScale = mode == 1 ? 0.5f : 0.25f;
            settings.heavyExplicit = mode == 3;
            settings.heavy = HeavyQuality::from_scale(0.25f);
            FrameSnapshot snapshot;
            for (u32 attempt = 0; attempt < 1500; ++attempt) {
                g.renderer.prepare(*scene.comp, scene.project, FrameIndex{motion ? 21 : 11},
                    &scene.media, &Scene::lookup, &scene, settings, 2'300'000 + mode, 0,
                    DecodeMode::Still, 1.0f, snapshot);
                if (!snapshot.missingVideoFrames && !snapshot.staleVideoFrames) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            OffscreenTarget target{g.target(width, height), width, height};
            FrameStats stats;
            RenderTimings timings;
            AUREA_CHECK(g.renderer.render(snapshot, settings, &target, stats, timings).ok());
            g.backend.wait_idle();
            std::vector<u16> pixels(static_cast<usize>(width) * height * 4);
            AUREA_CHECK(g.backend.read_texture(target.texture, pixels.data(), width * 8).ok());
            const auto& graph = g.renderer.frame_graph();
            u32 grid = 0;
            for (u32 resource = 0; resource < graph.resource_count(); ++resource) {
                const auto& desc = graph.desc(FGTexture{resource});
                if (desc.debugName && std::strncmp(desc.debugName, "hyper-campo", 11) == 0)
                    grid = std::max(grid, desc.width);
            }
            AUREA_CHECK(grid > 0);
            if (mode == 0) {
                reference = std::move(pixels);
                fullGrid = grid;
            } else if (mode == 3) {
                AUREA_CHECK_EQ(grid, fullGrid);
                AUREA_CHECK(pixels == reference);
            } else {
                AUREA_CHECK(grid <= (fullGrid + 1) / 2);
                for (u16 pixel : pixels) AUREA_CHECK(std::isfinite(half_to_float(pixel)));
            }
        }
    }
}

