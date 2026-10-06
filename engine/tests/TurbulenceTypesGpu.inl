// Turbulência: tipos de deslocamento, evolução em ciclo e bordas travadas.
//
// Os projetos antigos guardam os parâmetros por ÍNDICE (0..11). Os novos vêm
// depois; com eles nos padrões, o quadro tem que sair igual ao de antes.
namespace {
namespace turb {
enum : u32 { kAmount = 0, kSize, kComplexity, kEvolution, kOffsetX, kOffsetY, kSeed,
             kHorizontal, kEdges, kSpin, kMix, kPin, kType, kCycleOn, kCycle };

f32 max_difference(const FloatImage& a, const FloatImage& b) {
    f32 largest = 0;
    AUREA_CHECK_EQ(a.px.size(), b.px.size());
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i)
        largest = std::max(largest, std::fabs(a.px[i] - b.px[i]));
    return largest;
}

// Configurações que já existiam antes dos tipos: padrões, quadro animado,
// semente/oitavas/evolução, deslocamento+giro+mistura, só horizontal,
// as três bordas e as cinco fixações antigas.
void apply_legacy_case(EffectInstance& e, int n) {
    auto set = [&](u32 i, f32 v) { e.params[i].constant.v[0] = v; };
    if (n == 0 || n == 1) return;
    set(kAmount, 55); set(kSize, 90);
    if (n >= 2) { set(kEvolution, 137); set(kComplexity, 4.5f); set(kSeed, 7); }
    if (n == 3) { set(kOffsetX, 13); set(kOffsetY, -21); set(kSpin, 30); set(kMix, 60); }
    if (n == 4) { set(kHorizontal, 1); set(kEdges, 0); }
    if (n == 5) { set(kEdges, 2); set(kAmount, 300); }
    if (n >= 6 && n <= 10) set(kPin, static_cast<f32>(n - 5));
}
constexpr int kLegacyCases = 11;

FloatImage render_legacy_case(int n) {
    Scene s(224, 160);
    const LayerId id = s.image(reference_image(160, 96), 112, 80);
    EffectInstance& e = s.add_effect(id, effect_keys::kTurbulence);
    apply_legacy_case(e, n);
    return s.render(FrameIndex{n == 1 ? 12 : 0});
}
} // namespace turb
} // namespace

// Bit a bit contra uma captura feita ANTES dos parâmetros novos. Sem
// AUREA_TURB_LEGACY_DIR o teste não tem referência e só confere que a mesma
// configuração repete exatamente; com a pasta e sem arquivo, grava a captura.
AUREA_TEST(TurbulenceGpu, LegacyConfigurationsRenderBitIdentical) {
    AUREA_REQUIRE_GPU();
    const char* dir = std::getenv("AUREA_TURB_LEGACY_DIR");
    for (int n = 0; n < turb::kLegacyCases; ++n) {
        const FloatImage img = turb::render_legacy_case(n);
        AUREA_CHECK(img.width == 224 && img.height == 160);
        if (!dir || !*dir) {
            AUREA_CHECK_NEAR(turb::max_difference(img, turb::render_legacy_case(n)), 0, 0);
            continue;
        }
        const std::string path = std::string(dir) + "/turb_legacy_" + std::to_string(n) + ".bin";
        const usize bytes = img.px.size() * sizeof(f32);
        if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
            std::vector<f32> ref(img.px.size());
            const usize got = std::fread(ref.data(), 1, bytes, f);
            std::fclose(f);
            AUREA_CHECK_EQ(got, bytes);
            const bool same = got == bytes && std::memcmp(ref.data(), img.px.data(), bytes) == 0;
            if (!same) {
                f32 largest = 0;
                for (usize i = 0; i < ref.size(); ++i) largest = std::max(largest, std::fabs(ref[i] - img.px[i]));
                std::printf("\n    caso legado %d difere da captura: diferenca maxima %g", n, largest);
            }
            AUREA_CHECK_MSG(same, "turbulencia legada mudou");
        } else if (std::FILE* w = std::fopen(path.c_str(), "wb")) {
            std::fwrite(img.px.data(), 1, bytes, w);
            std::fclose(w);
            std::printf("\n    captura gravada: %s", path.c_str());
        } else {
            AUREA_CHECK_MSG(false, "nao consegui gravar a captura legada");
        }
    }
}

namespace {
namespace turb {
// Imagem que só varia ao longo de X (listras verticais) ou de Y.
ImagePixels stripes(u32 w, u32 h, bool alongX) {
    ImagePixels px = uniform_image(w, h, 0, 0, 0);
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) {
        u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
        const u32 t = alongX ? x : y;
        p[0] = static_cast<u8>((t * 37) % 256);
        p[1] = static_cast<u8>(t % 12 < 6 ? 220 : 30);
        p[2] = static_cast<u8>(255 - (t * 11) % 256);
    }
    return px;
}

// Diferença máxima numa caixa [x0, x1) x [y0, y1).
f32 box_difference(const FloatImage& a, const FloatImage& b, u32 x0, u32 y0, u32 x1, u32 y1) {
    f32 largest = 0;
    for (u32 y = y0; y < y1; ++y) for (u32 x = x0; x < x1; ++x)
        for (int c = 0; c < 4; ++c) largest = std::max(largest, std::fabs(a.at(x, y)[c] - b.at(x, y)[c]));
    return largest;
}

f64 mean_difference(const FloatImage& a, const FloatImage& b) {
    f64 sum = 0;
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i) sum += std::fabs(a.px[i] - b.px[i]);
    return a.px.empty() ? 0.0 : sum / static_cast<f64>(a.px.size());
}

// Camada 160x96 no centro de 224x160: ocupa x 32..191, y 32..127.
constexpr u32 kX0 = 32, kX1 = 192, kY0 = 32, kY1 = 128;
} // namespace turb
} // namespace

// Projeto salvo antes dos tipos tem 12 parâmetros: os que faltam caem no
// padrão, e o padrão explícito desenha o mesmo quadro que o legado.
AUREA_TEST(TurbulenceGpu, OldProjectsAndExplicitDefaultsDrawTheLegacyField) {
    AUREA_REQUIRE_GPU();
    Scene s(224, 160);
    const LayerId id = s.image(reference_image(160, 96), 112, 80);
    EffectInstance& e = s.add_effect(id, effect_keys::kTurbulence);
    AUREA_CHECK_EQ(e.params.size(), static_cast<usize>(15));
    turb::apply_legacy_case(e, 3);
    const FloatImage full = s.render();
    e.params[turb::kType].constant.v[0] = 0;
    e.params[turb::kCycleOn].constant.v[0] = 0;
    e.params[turb::kCycle].constant.v[0] = 7;   // ciclo desligado: o número não importa
    AUREA_CHECK_NEAR(turb::max_difference(full, s.render()), 0, 0);
    e.params.resize(12);
    AUREA_CHECK_NEAR(turb::max_difference(full, s.render()), 0, 0);
}

AUREA_TEST(TurbulenceGpu, EveryDisplacementTypeMovesThePictureDifferently) {
    AUREA_REQUIRE_GPU();
    Scene s(224, 160);
    const LayerId id = s.image(reference_image(160, 96), 112, 80);
    const FloatImage original = s.render();
    EffectInstance& e = s.add_effect(id, effect_keys::kTurbulence);
    e.params[turb::kAmount].constant.v[0] = 24;
    e.params[turb::kSize].constant.v[0] = 60;
    e.params[turb::kEvolution].constant.v[0] = 50;
    std::vector<FloatImage> frames;
    for (u32 type = 0; type < 9; ++type) {
        e.params[turb::kType].constant.v[0] = static_cast<f32>(type);
        frames.push_back(s.render());
        const FloatImage& f = frames.back();
        bool finite = true;
        for (const f32 v : f.px) finite = finite && std::isfinite(v);
        AUREA_CHECK_MSG(finite, "turbulencia com valor nao finito");
        const f64 moved = turb::mean_difference(original, f);
        std::printf("\n    tipo %u: diferenca media do original %.4f", type, moved);
        AUREA_CHECK(moved > 0.004);
    }
    for (u32 a = 0; a < 9; ++a) for (u32 b = a + 1; b < 9; ++b) {
        const f64 apart = turb::mean_difference(frames[a], frames[b]);
        if (apart <= 0.002) std::printf("\n    tipos %u e %u quase iguais: %.5f", a, b, apart);
        AUREA_CHECK(apart > 0.002);
    }
}

// Vertical só mexe em Y: listras verticais (que não mudam em Y) ficam iguais
// longe das bordas de cima/baixo. O mesmo para Horizontal com listras deitadas.
// O Cruzado mexe X conforme Y: listras verticais mudam.
AUREA_TEST(TurbulenceGpu, VerticalAndHorizontalMoveOnlyTheirAxis) {
    AUREA_REQUIRE_GPU();
    for (const bool alongX : {true, false}) {
        Scene s(224, 160);
        const LayerId id = s.image(turb::stripes(160, 96, alongX), 112, 80);
        const FloatImage original = s.render();
        EffectInstance& e = s.add_effect(id, effect_keys::kTurbulence);
        e.params[turb::kAmount].constant.v[0] = 16;
        e.params[turb::kSize].constant.v[0] = 50;
        e.params[turb::kEvolution].constant.v[0] = 120;
        const u32 still = alongX ? 6u : 7u, moving = alongX ? 7u : 6u;
        e.params[turb::kType].constant.v[0] = static_cast<f32>(still);
        const FloatImage kept = s.render();
        e.params[turb::kType].constant.v[0] = static_cast<f32>(moving);
        const FloatImage moved = s.render();
        e.params[turb::kType].constant.v[0] = 8;
        const FloatImage crossed = s.render();
        // Longe (> intensidade) das bordas que o eixo deslocado atravessa.
        const u32 x0 = alongX ? turb::kX0 + 2 : turb::kX0 + 20, x1 = alongX ? turb::kX1 - 2 : turb::kX1 - 20;
        const u32 y0 = alongX ? turb::kY0 + 20 : turb::kY0 + 2, y1 = alongX ? turb::kY1 - 20 : turb::kY1 - 2;
        AUREA_CHECK(turb::box_difference(original, kept, x0, y0, x1, y1) < 0.01f);
        AUREA_CHECK(turb::box_difference(original, moved, x0, y0, x1, y1) > 0.1f);
        AUREA_CHECK(turb::box_difference(original, crossed, x0, y0, x1, y1) > 0.1f);
    }
}

// Ciclo de N revoluções: a evolução 0 e a N*360 são o MESMO quadro (bit a
// bit), a emenda é tão suave quanto um passo comum, e uma revolução só não
// fecha o laço quando N > 1.
AUREA_TEST(TurbulenceGpu, CycleEvolutionLoopsExactlyEveryNRevolutions) {
    AUREA_REQUIRE_GPU();
    Scene s(224, 160);
    const LayerId id = s.image(reference_image(160, 96), 112, 80);
    EffectInstance& e = s.add_effect(id, effect_keys::kTurbulence);
    e.params[turb::kAmount].constant.v[0] = 30;
    e.params[turb::kSize].constant.v[0] = 70;
    e.params[turb::kComplexity].constant.v[0] = 4;
    e.params[turb::kCycleOn].constant.v[0] = 1;
    auto at = [&](f32 evolution) {
        e.params[turb::kEvolution].constant.v[0] = evolution;
        return s.render();
    };
    for (const u32 type : {0u, 2u, 4u, 8u}) {
        e.params[turb::kType].constant.v[0] = static_cast<f32>(type);
        for (const u32 n : {1u, 3u}) {
            e.params[turb::kCycle].constant.v[0] = static_cast<f32>(n);
            const FloatImage start = at(0);
            const f32 loop = 360.0f * static_cast<f32>(n);
            AUREA_CHECK_NEAR(turb::max_difference(start, at(loop)), 0, 0);
            AUREA_CHECK_NEAR(turb::max_difference(start, at(-loop)), 0, 0);
            AUREA_CHECK_NEAR(turb::max_difference(start, at(5 * loop)), 0, 0);
            const f64 step = turb::mean_difference(start, at(2));
            const f64 seam = turb::mean_difference(start, at(loop - 2));
            const f64 half = turb::mean_difference(start, at(loop * 0.5f));
            std::printf("\n    tipo %u ciclo %u: passo %.5f emenda %.5f meio %.5f", type, n, step, seam, half);
            AUREA_CHECK(seam < step * 2.0 + 1e-4);
            AUREA_CHECK(half > step * 4.0);
            if (n > 1) AUREA_CHECK(turb::mean_difference(start, at(360)) > step * 4.0);
        }
    }
    // Desligado, o número do ciclo não mexe em nada: volta o campo legado.
    e.params[turb::kType].constant.v[0] = 0;
    e.params[turb::kCycleOn].constant.v[0] = 0;
    const FloatImage legacyLook = at(90);
    e.params[turb::kCycle].constant.v[0] = 1;
    AUREA_CHECK_NEAR(turb::max_difference(legacyLook, at(90)), 0, 0);
}

// Travadas: os pixels da borda da camada ficam onde estavam. Sem trava
// (bordas horizontais), a borda de cima/baixo não puxa nada de fora — o
// alfa fica cheio —, mas o conteúdo ainda desliza ao longo dela.
AUREA_TEST(TurbulenceGpu, LockedPinningHoldsTheLayerEdges) {
    AUREA_REQUIRE_GPU();
    Scene s(224, 160);
    s.comp->set_transparent_background(true);
    const LayerId id = s.image(reference_image(160, 96), 112, 80);
    const FloatImage original = s.render();
    EffectInstance& e = s.add_effect(id, effect_keys::kTurbulence);
    e.params[turb::kAmount].constant.v[0] = 30;
    e.params[turb::kSize].constant.v[0] = 40;
    e.params[turb::kEvolution].constant.v[0] = 77;
    e.params[turb::kSpin].constant.v[0] = 25;
    using turb::kX0; using turb::kX1; using turb::kY0; using turb::kY1;
    auto columns = [&](const FloatImage& f) {
        return std::max(turb::box_difference(original, f, kX0, kY0, kX0 + 1, kY1),
                        turb::box_difference(original, f, kX1 - 1, kY0, kX1, kY1));
    };
    auto rows = [&](const FloatImage& f) {
        return std::max(turb::box_difference(original, f, kX0, kY0, kX1, kY0 + 1),
                        turb::box_difference(original, f, kX0, kY1 - 1, kX1, kY1));
    };
    auto min_alpha_rows = [&](const FloatImage& f) {
        f32 alpha = 1;
        for (u32 x = kX0 + 34; x < kX1 - 34; ++x) alpha = std::min({alpha, f.at(x, kY0)[3], f.at(x, kY1 - 1)[3]});
        return alpha;
    };
    auto min_alpha_columns = [&](const FloatImage& f) {
        f32 alpha = 1;
        for (u32 y = kY0 + 34; y < kY1 - 34; ++y) alpha = std::min({alpha, f.at(kX0, y)[3], f.at(kX1 - 1, y)[3]});
        return alpha;
    };
    for (const u32 type : {0u, 1u, 5u}) {
        e.params[turb::kType].constant.v[0] = static_cast<f32>(type);
        e.params[turb::kPin].constant.v[0] = 0;
        const FloatImage free = s.render();
        AUREA_CHECK(columns(free) > 0.05f);
        AUREA_CHECK(rows(free) > 0.05f);
        e.params[turb::kPin].constant.v[0] = 8;   // todas travadas
        const FloatImage all = s.render();
        AUREA_CHECK(columns(all) < 0.02f);
        AUREA_CHECK(rows(all) < 0.02f);
        AUREA_CHECK(turb::box_difference(original, all, kX0 + 40, kY0 + 30, kX1 - 40, kY1 - 30) > 0.05f);
        e.params[turb::kPin].constant.v[0] = 9;   // horizontais travadas
        const FloatImage horizontal = s.render();
        AUREA_CHECK(rows(horizontal) < 0.02f);
        AUREA_CHECK(columns(horizontal) > 0.05f);
        e.params[turb::kPin].constant.v[0] = 10;  // verticais travadas
        const FloatImage vertical = s.render();
        AUREA_CHECK(columns(vertical) < 0.02f);
        AUREA_CHECK(rows(vertical) > 0.05f);
        // Sem trava: nada atravessa a borda presa, mas o conteúdo desliza nela.
        e.params[turb::kPin].constant.v[0] = 6;   // bordas horizontais
        const FloatImage slide = s.render();
        std::printf("\n    tipo %u: alfa minimo nas linhas livre %.3f / horizontais %.3f", type,
                    min_alpha_rows(free), min_alpha_rows(slide));
        AUREA_CHECK(min_alpha_rows(slide) > 0.99f);
        AUREA_CHECK(rows(slide) > 0.05f);
        AUREA_CHECK(min_alpha_rows(free) < 0.9f);
        e.params[turb::kPin].constant.v[0] = 7;   // bordas verticais
        const FloatImage sideways = s.render();
        AUREA_CHECK(min_alpha_columns(sideways) > 0.99f);
        AUREA_CHECK(columns(sideways) > 0.05f);
    }
}
