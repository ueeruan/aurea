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

AUREA_TEST(HyperGpu, FsrIsSharperThanBilinearAndDoesNotRing) {
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
    const FloatImage bil = render(false), fsr = render(true);
    // Nitidez: o maior degrau entre vizinhos na borda vertical (linhas 4..24).
    auto steepest = [](const FloatImage& img) {
        f64 best = 0.0;
        for (u32 y = 4; y < 24; ++y)
            for (u32 x = 56; x < 72; ++x) best = std::max(best, hy::code(img, x + 1, y) - hy::code(img, x, y));
        return best;
    };
    const f64 sb = steepest(bil), sf = steepest(fsr);
    std::printf("[degrau bilinear %.1f, FSR %.1f] ", sb, sf);
    AUREA_CHECK_MSG(sf > 1.25 * sb, "o FSR tem de deixar a borda mais nítida que o bilinear");
    // Sem anel: nada passa do intervalo da fonte além de 3 códigos.
    f64 lo = 1e9, hi = -1e9;
    for (u32 y = 2; y < fsr.height - 2; ++y)
        for (u32 x = 2; x < fsr.width - 2; ++x) {
            const f64 c = hy::code(fsr, x, y);
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
    std::printf("[faixa FSR %.1f..%.1f] ", lo, hi);
    // EASU é preso ao mínimo/máximo dos 4 vizinhos; o RCAS (nitidez de 1
    // parada) só passa do intervalo no lobo limitado: até 12 códigos (5%).
    AUREA_CHECK_MSG(lo >= 28.0 && hi <= 212.0, "sem anel além do limite");
    // A parte lisa continua lisa.
    AUREA_CHECK_NEAR(hy::code(fsr, 8, 10), 40.0, 2.0);
    AUREA_CHECK_NEAR(hy::code(fsr, 110, 10), 200.0, 2.0);
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
    auto cost = [&](Scene& s, i64 t) {
        RenderSettings rs;
        rs.gpuTimers = true;
        rs.finalQuality = true;
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
        std::printf("\n    1080p (de 540p) GPU total: sem %.3f ms, Upscale General (FSR 1) +%.3f ms, Anime +%.3f ms",
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
        g.renderer.set_flow_cache_enabled(true);
        const f32 motionCached = cost(s, 21);
        std::printf("\n    1080p GPU total: sem %.3f ms; Clean denoise 3 quadros +%.3f ms, 5 quadros +%.3f ms, "
                    "deblock +%.3f ms, deinterlace +%.3f ms; Motion +%.3f ms (par em cache +%.3f ms)\n",
                    none, dn3 - none, dn5 - none, deblock - none, deint - none, motion - none, motionCached - none);
    }
}

