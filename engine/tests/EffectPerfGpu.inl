// =============================================================================
//  Custo por efeito do catálogo em 1080p — a medição que guia as otimizações
//  de efeito (o efeito é o custo dominante por quadro no celular).
//  Incluído no fim de test_gpu.cpp (usa a `Scene`, gpu() e half_to_float/
//  srgb_encode de lá).
//
//  Cada efeito do registro (menos os aposentados e os que só existem noutro
//  tipo de camada) vai numa camada de imagem texturizada de 1920×1080 — os de
//  TEMPO (histórico/vizinhos) numa camada de vídeo 1080p que anda, porque numa
//  foto eles não têm passado. Duas configurações: o padrão de fábrica (o que o
//  botão "aplicar" põe) e os valores de demonstração (os da prévia do
//  catálogo; o padrão é neutro em quase tudo e um efeito neutro nem chega à
//  GPU). Para cada uma: o melhor e a mediana de N quadros do tempo de GPU dos
//  passes do EFEITO (timestamp queries, sem a imagem, a composição e a saída)
//  e da CPU de planejamento (prepare) e de gravação (build dos passes).
//
//  Só IMPRIME e grava engine/build/effect_perf.tsv: não falha por lentidão (o
//  tempo do host varia com o que mais roda na máquina). Falha se algum efeito
//  der pixel não-finito.
//
//  Variáveis de ambiente (opcionais):
//    AUREA_EFFECT_PERF_ONLY=chave1,chave2  só os efeitos cuja chave contém um
//                                          dos trechos (antes/depois rápido)
//    AUREA_EFFECT_PERF_FRAMES=N            quadros medidos (padrão 8)
//    AUREA_EFFECT_PERF_OUT=arquivo.tsv     onde gravar a tabela
//    AUREA_EFFECT_PERF_DUMP=pasta          grava o 1º quadro de cada efeito
//                                          (sRGB 8 bits cru, .rgb)
//    AUREA_EFFECT_PERF_REF=pasta           compara com um DUMP anterior (máx.
//                                          diferença em 1/255 e PSNR) — é a
//                                          prova "antes × depois" de uma
//                                          otimização, na mesma entrada
// =============================================================================
#if defined(AUREA_TEST_VULKAN)
namespace effect_perf {

/// Foto sintética de 1080p: degradês suaves, um disco claro (para o que
/// depende de luz: brilho, limiar), textura de média frequência (detalhe de
/// foto), linhas finas (borda afiada) e manchas saturadas.
ImagePixels plate(u32 w, u32 h) {
    ImagePixels px;
    px.width = w;
    px.height = h;
    px.rgba.resize(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            u8* p = &px.rgba[(static_cast<usize>(y) * w + x) * 4];
            const f32 fx = static_cast<f32>(x), fy = static_cast<f32>(y);
            const f32 tex = 0.5f + 0.18f * std::sin(fx * 0.071f + fy * 0.023f) + 0.14f * std::sin(fx * 0.019f - fy * 0.083f)
                          + 0.10f * std::sin((fx + fy) * 0.137f);
            f32 r = 0.15f + 0.6f * fx / static_cast<f32>(w - 1) * tex;
            f32 g = 0.10f + 0.6f * fy / static_cast<f32>(h - 1) * tex;
            f32 b = 0.20f + 0.35f * tex;
            const f32 dx = fx - w * 0.5f, dy = fy - h * 0.5f;
            if (dx * dx + dy * dy < (h * 0.2f) * (h * 0.2f)) r = g = b = 1.0f;
            if (x > w / 12 && x < w / 5 && y > h / 8 && y < h / 3) { r = 0.95f; g = 0.12f; b = 0.08f; }
            if (x > w * 3 / 4 && x < w * 7 / 8 && y > h * 2 / 3 && y < h * 7 / 8) { r = 0.1f; g = 0.85f; b = 0.25f; }
            if (x % 48 == 0 || y % 48 == 0) r = g = b = 0.08f;
            p[0] = static_cast<u8>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            p[1] = static_cast<u8>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            p[2] = static_cast<u8>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            p[3] = 255;
        }
    }
    return px;
}

struct Sample {
    f32 gpuBest = 0, gpuMedian = 0;          ///< passes do efeito (ms)
    f32 prepBest = 0, prepMedian = 0;        ///< prepare (planejamento) na CPU
    f32 recBest = 0, recMedian = 0;          ///< gravação dos passes na CPU
    u32 passes = 0;
    f32 heaterMedian = 0;                    ///< GPU do aquecedor (fora da conta)
    std::string topPass;
    f32 topPassMs = 0;
    bool finite = true;
    bool compared = false;
    u32 maxDiff = 0;                         ///< em 1/255 (sRGB)
    f64 psnr = 0;
    f64 over2 = 0;                           ///< fração de canais com diferença > 2/255
};

f32 median_of(std::vector<f32> v) {
    if (v.empty()) return 0.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

bool is_source_pass(const char* s) {
    auto starts = [s](const char* p) { return std::strncmp(s, p, std::strlen(p)) == 0; };
    return starts("cor-do-video") || starts("imagem") || starts("solido") || starts("composicao") || starts("saida");
}

/// AQUECEDOR: a GPU de mesa baixa o relógio quando o quadro tem pouco
/// trabalho (DVFS) — um passe de 0,1 ms media 1 ms no meio de uma fila de
/// efeitos baratos. Uma camada sólida EMBAIXO da imagem opaca, com ruído
/// fractal pesado, mantém a GPU ocupada nos quadros medidos; os passes dela
/// (pelo nome) ficam fora da conta. Desligada no quadro de prova de imagem.
struct Heater {
    const char* key;
    const char* label;
};
Heater heater_for(const Effect* fx) {
    if (fx && std::strcmp(fx->info().key, effect_keys::kFractalNoise) == 0)
        return Heater{"aurea.generate.gradient_ramp", "degrade"};
    return Heater{effect_keys::kFractalNoise, "ruido-fractal"};
}
const char* gHeaterLabel = "ruido-fractal";

/// Um quadro: prepare (CPU, só a tentativa que valeu) + gravação + GPU dos
/// passes do efeito. Os quadros vão SEGUIDOS, sem wait_idle entre eles: a
/// GPU do host que para entre um quadro e outro baixa o relógio e um passe de
/// 0,1 ms aparece como 1 ms (o que a 1ª versão deste teste mediu). A leitura
/// das timestamps é a do último quadro CONCLUÍDO (1–2 atrás, anel de 2): o
/// mesmo efeito com os mesmos valores, porque os primeiros são descartados.
struct FrameCost {
    f32 prep = 0, rec = 0, gpu = 0, heater = 0;
    u32 passes = 0;
    std::vector<std::pair<std::string, f32>> byPass;
};

FrameCost frame(Scene& s, FrameIndex t, const RenderSettings& rs) {
    Gpu& g = gpu();
    static u64 frameNo = 970000;
    FrameSnapshot snap;
    f32 prep = 0;
    for (int attempt = 0; attempt < 3000; ++attempt) {
        const auto t0 = std::chrono::steady_clock::now();
        g.renderer.prepare(*s.comp, s.project, t, &s.media, &Scene::lookup, &s, rs, ++frameNo, 0, DecodeMode::Still, 1.0f, snap);
        prep = static_cast<f32>(std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (snap.missingVideoFrames == 0 && snap.staleVideoFrames == 0) break;
        for (RenderLayer& l : snap.layers) l.source.frame.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const u32 w = s.comp->width(), h = s.comp->height();
    OffscreenTarget off{g.target(w, h), w, h};
    FrameStats stats;
    RenderTimings timings;
    AUREA_CHECK(g.renderer.render(snap, rs, &off, stats, timings).ok());
    FrameCost c;
    c.prep = prep;
    c.rec = timings.cpuRecordMs;
    GpuTiming passes[512];
    f32 total = 0;
    const u32 n = g.backend.read_gpu_timings(passes, 512, &total);
    for (u32 i = 0; i < n; ++i) {
        const char* label = passes[i].label ? passes[i].label : "?";
        if (std::strcmp(label, gHeaterLabel) == 0) { c.heater += passes[i].ms; continue; }
        if (is_source_pass(label)) continue;
        c.gpu += passes[i].ms;
        ++c.passes;
        auto it = std::find_if(c.byPass.begin(), c.byPass.end(), [&](const auto& p) { return p.first == label; });
        if (it == c.byPass.end()) c.byPass.emplace_back(label, passes[i].ms);
        else it->second += passes[i].ms;
    }
    return c;
}

std::vector<u8> srgb8(const FloatImage& img) {
    std::vector<u8> out(static_cast<usize>(img.width) * img.height * 3);
    for (usize i = 0, j = 0; i < img.px.size(); i += 4, j += 3)
        for (int c = 0; c < 3; ++c)
            out[j + c] = static_cast<u8>(std::lround(std::fmin(1.0f, srgb_encode(img.px[i + c])) * 255.0f));
    return out;
}

std::string file_name(const char* key, const char* config) {
    std::string n = key;
    for (char& c : n) if (c == '.') c = '_';
    return n + "_" + config + ".rgb";
}

bool selected(const char* key) {
    const char* only = std::getenv("AUREA_EFFECT_PERF_ONLY");
    if (!only || !*only) return true;
    std::string list = only;
    usize start = 0;
    while (start <= list.size()) {
        const usize end = std::min(list.find(',', start), list.size());
        const std::string part = list.substr(start, end - start);
        if (!part.empty() && std::strstr(key, part.c_str())) return true;
        start = end + 1;
    }
    return false;
}

/// Mede um efeito numa configuração. `values` nulo = cena sem efeito (base).
Sample measure(const Effect* fx, const ParameterRegistry* params, bool demo, bool moving, u32 frames, const char* config) {
    constexpr u32 W = 1920, H = 1080;
    Scene s(W, H);
    const Heater heat = heater_for(fx);
    gHeaterLabel = heat.label;
    const LayerId heaterId = s.solid(static_cast<f32>(W), static_cast<f32>(H), Vec4{0.2f, 0.2f, 0.2f, 1.0f}, W * 0.5f, H * 0.5f);
    for (u32 k = 0; k < 4; ++k) {
        EffectInstance& h = s.add_effect(heaterId, heat.key);
        const ParameterRegistry& hp = *gpu().effects.params(h.type);
        if (const u32 c = hp.find("complexity"); c != kInvalidIndex) h.params[c].constant = ParamValue::scalar(8.0f);
    }
    s.comp->layer(heaterId)->visible = false;   // ligada só nos quadros medidos
    LayerId id;
    if (moving) {
        SyntheticConfig cfg;
        cfg.width = W;
        cfg.height = H;
        cfg.fps = 30.0;
        cfg.frameCount = 300;
        cfg.pattern = SyntheticPattern::PanTable;
        id = s.video(cfg, W * 0.5f, H * 0.5f);
    } else {
        static const ImagePixels kPlate = plate(W, H);
        id = s.image(kPlate, W * 0.5f, H * 0.5f);
    }
    if (fx) {
        Layer* l = s.comp->layer(id);
        EffectInstance e;
        e.id = l->alloc_effect_id();
        e.type = fx->type_id();
        initialize_instance(e, *params);
        if (demo) {
            std::vector<ParamValue> values(params->count());
            for (u32 p = 0; p < params->count(); ++p) values[p] = e.params[p].constant;
            if (fx->demo_values(e, values))
                for (u32 p = 0; p < params->count(); ++p) e.params[p].constant = values[p];
        }
        l->effects.push_back(std::move(e));
    }
    Sample out;
    // O 1º quadro (qualidade final, como o export): pixels finitos e, com
    // DUMP/REF, a prova de imagem. Também aquece pipelines e caches.
    const FrameIndex t0{moving ? 20 : 30};
    const FloatImage img = s.render(t0, 1, true);
    for (const f32 v : img.px) if (!std::isfinite(v)) { out.finite = false; break; }
    const char* key = fx ? fx->info().key : "base";
    if (const char* dump = std::getenv("AUREA_EFFECT_PERF_DUMP"); dump && *dump) {
        const std::vector<u8> rgb = srgb8(img);
        if (std::FILE* f = std::fopen((std::string(dump) + "/" + file_name(key, config)).c_str(), "wb")) {
            (void)std::fwrite(rgb.data(), 1, rgb.size(), f);
            std::fclose(f);
        }
    }
    if (const char* ref = std::getenv("AUREA_EFFECT_PERF_REF"); ref && *ref) {
        const std::vector<u8> rgb = srgb8(img);
        std::vector<u8> want(rgb.size());
        if (std::FILE* f = std::fopen((std::string(ref) + "/" + file_name(key, config)).c_str(), "rb")) {
            const bool ok = std::fread(want.data(), 1, want.size(), f) == want.size();
            std::fclose(f);
            if (ok) {
                out.compared = true;
                f64 se = 0;
                u64 over = 0;
                for (usize k = 0; k < rgb.size(); ++k) {
                    const i32 d = std::abs(static_cast<i32>(rgb[k]) - static_cast<i32>(want[k]));
                    out.maxDiff = std::max<u32>(out.maxDiff, static_cast<u32>(d));
                    se += static_cast<f64>(d) * d;
                    over += d > 2 ? 1u : 0u;
                }
                const f64 mse = se / static_cast<f64>(rgb.size());
                out.psnr = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 999.0;
                out.over2 = static_cast<f64>(over) / static_cast<f64>(rgb.size());
            }
        }
    }
    RenderSettings rs;
    rs.gpuTimers = true;
    rs.finalQuality = true;
    rs.dither = false;
    s.comp->layer(heaterId)->visible = true;
    std::vector<f32> gpus, preps, recs, heats;
    FrameCost best;
    best.gpu = 1e9f;
    constexpr u32 kWarm = 6;   // relógio da GPU sobe; o anel passa do quadro de leitura
    for (u32 r = 0; r < frames + kWarm; ++r) {
        // Imagem parada: o mesmo instante (custo em regime). Vídeo: anda um
        // quadro por medição (o efeito de tempo vê movimento de verdade).
        const FrameIndex t{moving ? t0.value + 1 + static_cast<i64>(r) : t0.value};
        FrameCost c = frame(s, t, rs);
        if (r < kWarm) continue;
        gpus.push_back(c.gpu);
        preps.push_back(c.prep);
        recs.push_back(c.rec);
        heats.push_back(c.heater);
        if (c.gpu < best.gpu) best = std::move(c);
    }
    gpu().backend.wait_idle();
    out.heaterMedian = median_of(heats);
    if (const char* v = std::getenv("AUREA_EFFECT_PERF_VERBOSE"); v && *v) {
        std::printf("\n      %s %s (aquecedor %.3f ms): GPU", key, config, out.heaterMedian);
        for (const f32 ms : gpus) std::printf(" %.3f", ms);
        std::printf("  | prepare");
        for (const f32 ms : preps) std::printf(" %.3f", ms);
        for (const auto& [label, ms] : best.byPass) std::printf("\n        passe %-28s %.3f ms", label.c_str(), ms);
    }
    out.gpuBest = gpus.empty() ? 0.0f : *std::min_element(gpus.begin(), gpus.end());
    out.gpuMedian = median_of(gpus);
    out.prepBest = preps.empty() ? 0.0f : *std::min_element(preps.begin(), preps.end());
    out.prepMedian = median_of(preps);
    out.recBest = recs.empty() ? 0.0f : *std::min_element(recs.begin(), recs.end());
    out.recMedian = median_of(recs);
    out.passes = best.passes;
    for (const auto& [label, ms] : best.byPass) {
        if (ms > out.topPassMs) { out.topPassMs = ms; out.topPass = label; }
    }
    return out;
}

/// Os que só existem noutro tipo de camada ou contexto (o motor recusa numa
/// foto) e o Roto Brush (carregaria o modelo de IA). Os mesmos cortes do
/// Gpu.EveryCatalogEffectChangesTheProjectFrame.
bool skipped(const Effect& fx) {
    const EffectTypeId t = fx.type_id();
    const char* key = fx.info().key;
    static const EffectTypeId kSkip[] = {
        effect_type_id(effect_keys::kText3DLayout), effect_type_id(effect_keys::kShape3DLayout),
        effect_type_id(effect_keys::kPart3DTransform), effect_type_id(effect_keys::kPart3DHide),
        effect_type_id(effect_keys::kPart3DTexture), effect_type_id(text::kTransformEffect),
        effect_type_id(text::kAnimatorEffect), effect_type_id(effect_keys::kSceneFlare),
        effect_type_id(effect_keys::kRotobrush),
        // Aposentados do catálogo (Engine::query_effect_catalog).
        effect_type_id("aurea.audio.reverb"), effect_type_id("aurea.audio.flanger"),
        effect_type_id("aurea.audio.echo"), effect_type_id(effect_keys::kAudioSpectrum),
        effect_type_id(effect_keys::kDeepGlow2),
    };
    if (std::find(std::begin(kSkip), std::end(kSkip), t) != std::end(kSkip)) return true;
    return std::strcmp(key, "aurea.layout.grid_builder") == 0 || std::strcmp(key, "aurea.layout.grid_item") == 0;
}

} // namespace effect_perf

AUREA_TEST(EffectPerf, EveryCatalogEffectGpuCostAt1080p) {
    AUREA_REQUIRE_GPU();
    using namespace effect_perf;
    u32 frames = 8;
    if (const char* f = std::getenv("AUREA_EFFECT_PERF_FRAMES"); f && std::atoi(f) > 0) frames = static_cast<u32>(std::atoi(f));
    struct Row {
        std::string key, name, cls;
        bool moving = false;
        Sample def, demo;
        [[nodiscard]] f32 cost() const { return std::max(def.gpuBest, demo.gpuBest); }
    };
    static const char* const kClass[] = {"PerPixel", "Neighborhood", "Domain", "Temporal", "Global"};
    std::vector<Row> rows;
    std::vector<std::string> nonFinite;
    const Sample base = measure(nullptr, nullptr, false, false, frames, "base");
    const Sample baseVideo = measure(nullptr, nullptr, false, true, frames, "base_video");
    Gpu& g = gpu();
    const auto started = std::chrono::steady_clock::now();
    for (u32 i = 0; i < g.effects.count(); ++i) {
        if (g.effects.retired_at(i)) continue;
        const Effect& fx = g.effects.at(i);
        if (skipped(fx) || !selected(fx.info().key)) continue;
        const ParameterRegistry& params = g.effects.params_at(i);
        Row row;
        row.key = fx.info().key;
        row.name = fx.info().name;
        row.cls = kClass[std::min<u32>(static_cast<u32>(fx.effect_class()), 4u)];
        row.moving = fx.effect_class() == EffectClass::Temporal || fx.wants_history() || fx.wants_neighbors();
        row.def = measure(&fx, &params, false, row.moving, frames, "padrao");
        row.demo = measure(&fx, &params, true, row.moving, frames, "demo");
        if (!row.def.finite) nonFinite.push_back(row.key + " (padrao)");
        if (!row.demo.finite) nonFinite.push_back(row.key + " (demo)");
        rows.push_back(std::move(row));
    }
    const f64 seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - started).count();
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.cost() > b.cost(); });

    std::printf("\n    1080p, qualidade final, melhor/mediana de %u quadros (%.0f s). Base sem efeito: GPU %.3f ms, prepare %.3f ms,"
                " gravacao %.3f ms (imagem); prepare %.3f ms (video); aquecedor %.3f ms (fora da conta; a CPU o inclui)\n",
                frames, seconds, base.gpuBest, base.prepBest, base.recBest, baseVideo.prepBest, base.heaterMedian);
    std::printf("    %-3s %-34s %-12s %9s %9s %9s %9s %8s %8s %6s  %s\n", "#", "efeito", "classe", "GPU pad", "GPU demo",
                "med demo", "CPU prep", "CPU grav", "CPU pad", "passes", "passe mais caro (demo)");
    std::string tsv = "rank\tkey\tname\tclass\tvideo\tgpu_default_best_ms\tgpu_default_median_ms\tgpu_demo_best_ms\t"
                      "gpu_demo_median_ms\tcpu_prepare_demo_best_ms\tcpu_record_demo_best_ms\tcpu_prepare_default_best_ms\t"
                      "cpu_record_default_best_ms\tpasses_demo\ttop_pass_demo\ttop_pass_demo_ms\tref_max_diff_default\t"
                      "ref_psnr_default\tref_max_diff_demo\tref_psnr_demo\n";
    char line[1024];
    for (usize r = 0; r < rows.size(); ++r) {
        const Row& row = rows[r];
        std::printf("    %-3zu %-34s %-12s %9.3f %9.3f %9.3f %9.3f %8.3f %8.3f %6u  %s %.3f", r + 1, row.key.c_str(),
                    row.cls.c_str(), row.def.gpuBest, row.demo.gpuBest, row.demo.gpuMedian, row.demo.prepBest,
                    row.demo.recBest, row.def.prepBest + row.def.recBest, row.demo.passes, row.demo.topPass.c_str(),
                    row.demo.topPassMs);
        for (const Sample* smp : {&row.def, &row.demo}) {
            if (smp->compared)
                std::printf("  [%s: max %u/255, PSNR %.1f dB, >2: %.4f%%]", smp == &row.def ? "pad" : "demo", smp->maxDiff,
                            smp->psnr, smp->over2 * 100.0);
        }
        std::printf("\n");
        std::snprintf(line, sizeof(line), "%zu\t%s\t%s\t%s\t%d\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%u\t%s\t%.4f\t%d\t%.2f\t%d\t%.2f\n",
                      r + 1, row.key.c_str(), row.name.c_str(), row.cls.c_str(), row.moving ? 1 : 0, row.def.gpuBest,
                      row.def.gpuMedian, row.demo.gpuBest, row.demo.gpuMedian, row.demo.prepBest, row.demo.recBest,
                      row.def.prepBest, row.def.recBest, row.demo.passes, row.demo.topPass.c_str(), row.demo.topPassMs,
                      row.def.compared ? static_cast<int>(row.def.maxDiff) : -1, row.def.compared ? row.def.psnr : -1.0,
                      row.demo.compared ? static_cast<int>(row.demo.maxDiff) : -1, row.demo.compared ? row.demo.psnr : -1.0);
        tsv += line;
    }
    // Os piores planejadores (CPU de prepare + gravação acima da base).
    std::vector<const Row*> cpu;
    for (const Row& row : rows) cpu.push_back(&row);
    auto cpuCost = [&](const Row* r) {
        return std::max(r->demo.prepBest + r->demo.recBest, r->def.prepBest + r->def.recBest) - (base.prepBest + base.recBest);
    };
    std::stable_sort(cpu.begin(), cpu.end(), [&](const Row* a, const Row* b) { return cpuCost(a) > cpuCost(b); });
    std::printf("\n    CPU por quadro acima da base (prepare + gravacao, o pior das duas configuracoes):\n");
    for (usize r = 0; r < std::min<usize>(15, cpu.size()); ++r)
        std::printf("    %-3zu %-34s %8.3f ms (demo: prepare %.3f, gravacao %.3f)\n", r + 1, cpu[r]->key.c_str(), cpuCost(cpu[r]),
                    cpu[r]->demo.prepBest, cpu[r]->demo.recBest);

    std::string out = std::string(AUREA_GOLDEN_DIR) + "/../../build/effect_perf.tsv";
    if (const char* o = std::getenv("AUREA_EFFECT_PERF_OUT"); o && *o) out = o;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(out).parent_path(), ec);
    if (std::FILE* f = std::fopen(out.c_str(), "wb")) {
        (void)std::fwrite(tsv.data(), 1, tsv.size(), f);
        std::fclose(f);
        std::printf("    tabela gravada em %s\n", out.c_str());
    } else {
        std::printf("    (nao consegui gravar %s)\n", out.c_str());
    }
    for (const std::string& n : nonFinite) std::printf("\n    pixel nao-finito: %s", n.c_str());
    AUREA_CHECK_MSG(nonFinite.empty(), "efeito do catalogo deu pixel nao-finito em 1080p");
}
#endif
