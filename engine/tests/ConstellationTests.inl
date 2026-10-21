// =============================================================================
//  Constelação (aurea.generate.constellation, 0.3.0) — testes de GPU.
//  Incluído no fim de test_gpu.cpp (usa a `Scene` e os ajudantes de lá).
//
//  Os parâmetros são achados pelo id (a declaração é a fonte): o teste não
//  quebra se uma seção mudar de lugar no painel.
// =============================================================================
namespace {

u32 constellation_param(const char* id) {
    const ParameterRegistry* params = gpu().effects.params(effect_type_id(effect_keys::kConstellation));
    const u32 i = params ? params->find(id) : kInvalidIndex;
    AUREA_CHECK_MSG(i != kInvalidIndex, id);
    return i;
}

EffectInstance& constellation_of(Scene& s, LayerId id) {
    return s.comp->layer(id)->effects.back();
}

void cl_set(Scene& s, LayerId id, const char* param, f32 v) {
    EffectInstance& fx = constellation_of(s, id);
    const u32 i = constellation_param(param);
    if (i < fx.params.size()) fx.params[i].constant = ParamValue::scalar(v);
}

void cl_color(Scene& s, LayerId id, const char* param, f32 r, f32 g, f32 b) {
    EffectInstance& fx = constellation_of(s, id);
    const u32 i = constellation_param(param);
    if (i < fx.params.size()) fx.params[i].constant = ParamValue::color(r, g, b, 1.0f);
}

/// Parada (sem ruído nem giro): só a geometria e a câmera decidem a imagem.
void constellation_still(Scene& s, LayerId id) {
    cl_set(s, id, "noise", 0.0f);
    cl_set(s, id, "spin_x", 0.0f);
    cl_set(s, id, "spin_y", 0.0f);
    cl_set(s, id, "spin_z", 0.0f);
}

f32 cl_lum(const FloatImage& img, u32 x, u32 y) {
    const Vec4 v = img.v(x, y);
    return (v.x + v.y + v.z) / 3.0f;
}

} // namespace

AUREA_TEST(EffectPackGpu, ConstellationDeclaresSectionsPresetsAndTheCatalogEntry) {
    const EffectTypeId type = effect_type_id(effect_keys::kConstellation);
    const Effect* fx = gpu().effects.find(type);
    const ParameterRegistry* params = gpu().effects.params(type);
    AUREA_CHECK(fx != nullptr && params != nullptr);
    if (!fx || !params) return;
    AUREA_CHECK(std::strcmp(fx->info().name, "Constelação") == 0);
    AUREA_CHECK(std::strcmp(fx->info().category, "3D") == 0);
    AUREA_CHECK_EQ(params->count(), 65u);
    AUREA_CHECK_EQ(params->section_count(), 7u);   // nenhuma seção recusada em silêncio
    AUREA_CHECK_EQ(fx->presets().size(), static_cast<usize>(4));
    for (const EffectPreset& preset : fx->presets())
        for (const EffectPresetValue& v : preset.values) AUREA_CHECK(v.param < params->count());
}

AUREA_TEST(EffectPackGpu, ConstellationConnectsClosePointsAndNotFarOnes) {
    // Grade 2×2 no plano z = 0 (a câmera padrão o mostra 1:1): colunas em
    // x = 50 e 150, linhas em y = 20 e 80. Lados: vertical 60, horizontal
    // 100, diagonal ~116,6. A distância máxima decide quais pares se ligam.
    AUREA_REQUIRE_GPU();
    Scene s(200, 100);
    const auto id = s.image(uniform_image(200, 100, 0, 0, 0), 100, 50);
    s.add_effect(id, effect_keys::kConstellation);
    constellation_still(s, id);
    cl_set(s, id, "source", 2.0f);   // Grade
    cl_set(s, id, "grid_x", 2.0f);
    cl_set(s, id, "grid_y", 2.0f);
    cl_set(s, id, "grid_z", 1.0f);
    cl_set(s, id, "width", 50.0f);
    cl_set(s, id, "height", 60.0f);
    cl_set(s, id, "points", 0.0f);
    cl_set(s, id, "line_width", 3.0f);
    cl_set(s, id, "line_opacity", 100.0f);
    cl_set(s, id, "line_falloff", 0.0f);
    cl_set(s, id, "line_color_mode", 0.0f);
    cl_color(s, id, "line_color", 1.0f, 1.0f, 1.0f);
    auto at = [&](f32 maxDistance, f32 minDistance) {
        cl_set(s, id, "line_max_distance", maxDistance);
        cl_set(s, id, "line_min_distance", minDistance);
        return s.render(FrameIndex{0});
    };
    const auto vertical = at(80.0f, 0.0f);
    std::printf("\n    constelacao 80px: vertical %.3f horizontal %.3f diagonal %.3f fora %.3f",
                cl_lum(vertical, 50, 50), cl_lum(vertical, 100, 20), cl_lum(vertical, 100, 50), cl_lum(vertical, 100, 35));
    AUREA_CHECK(cl_lum(vertical, 50, 50) > 0.5f);      // 60 px: ligado
    AUREA_CHECK(cl_lum(vertical, 150, 50) > 0.5f);
    AUREA_CHECK(cl_lum(vertical, 100, 20) < 0.02f);    // 100 px: longe demais
    AUREA_CHECK(cl_lum(vertical, 100, 50) < 0.02f);    // diagonais também
    const auto sides = at(110.0f, 0.0f);
    AUREA_CHECK(cl_lum(sides, 100, 20) > 0.5f);
    AUREA_CHECK(cl_lum(sides, 100, 80) > 0.5f);
    AUREA_CHECK(cl_lum(sides, 100, 50) < 0.02f);       // 116,6 > 110
    AUREA_CHECK(cl_lum(sides, 100, 35) < 0.02f);       // nenhuma linha passa ali
    const auto all = at(120.0f, 0.0f);
    AUREA_CHECK(cl_lum(all, 100, 50) > 0.5f);          // as diagonais cruzam no meio
    const auto noShort = at(110.0f, 70.0f);         // mínima 70: sem as verticais
    AUREA_CHECK(cl_lum(noShort, 50, 50) < 0.02f);
    AUREA_CHECK(cl_lum(noShort, 100, 20) > 0.5f);
    // Uma conexão por ponto: cada ponto liga só ao mais perto (o vertical).
    cl_set(s, id, "max_connections", 1.0f);
    const auto one = at(120.0f, 0.0f);
    AUREA_CHECK(cl_lum(one, 50, 50) > 0.5f);
    AUREA_CHECK(cl_lum(one, 100, 20) < 0.02f);
    AUREA_CHECK(cl_lum(one, 100, 50) < 0.02f);
    std::printf("\n");
}

AUREA_TEST(EffectPackGpu, ConstellationTrianglesFillTrios) {
    // Três pontos ligados entre si fazem uma face; sem triângulos, o miolo
    // fica escuro.
    AUREA_REQUIRE_GPU();
    Scene s(200, 100);
    const auto id = s.image(uniform_image(200, 100, 0, 0, 0), 100, 50);
    s.add_effect(id, effect_keys::kConstellation);
    constellation_still(s, id);
    cl_set(s, id, "source", 2.0f);
    cl_set(s, id, "grid_x", 2.0f);
    cl_set(s, id, "grid_y", 2.0f);
    cl_set(s, id, "grid_z", 1.0f);
    cl_set(s, id, "width", 50.0f);
    cl_set(s, id, "height", 60.0f);
    cl_set(s, id, "points", 0.0f);
    cl_set(s, id, "lines", 0.0f);
    cl_set(s, id, "line_max_distance", 130.0f);
    const auto none = s.render(FrameIndex{0});
    cl_set(s, id, "triangles", 1.0f);
    cl_set(s, id, "triangle_max_distance", 130.0f);
    cl_set(s, id, "triangle_opacity", 100.0f);
    cl_set(s, id, "triangle_color_mode", 0.0f);
    cl_color(s, id, "triangle_color", 0.0f, 1.0f, 0.0f);
    const auto faces = s.render(FrameIndex{0});
    AUREA_CHECK(none.v(80, 40).y < 0.01f);
    AUREA_CHECK(faces.v(80, 40).y > 0.1f);          // dentro do quadrado coberto pelas faces
    AUREA_CHECK(faces.v(20, 50).y < 0.01f);         // fora da grade, nada
    cl_set(s, id, "triangle_max_distance", 110.0f);  // a diagonal não cabe: sem trio
    AUREA_CHECK(s.render(FrameIndex{0}).v(80, 40).y < 0.01f);
}

AUREA_TEST(EffectPackGpu, ConstellationIsDeterministicSeededAndAnimated) {
    AUREA_REQUIRE_GPU();
    Scene s(160, 90);
    const auto id = s.image(uniform_image(160, 90, 0, 0, 0), 80, 45);
    s.add_effect(id, effect_keys::kConstellation);
    cl_set(s, id, "count", 120.0f);
    cl_set(s, id, "line_max_distance", 60.0f);
    const auto a = s.render(FrameIndex{10});
    const auto b = s.render(FrameIndex{10});
    AUREA_CHECK_MSG(a.px == b.px, "o mesmo quadro tem de sair igual, byte a byte");
    AUREA_CHECK(a.mean().x > 0.002f);
    AUREA_CHECK_MSG(max_abs_diff(a, s.render(FrameIndex{10}, 1, true)) < 1e-3f, "preview = export");
    const auto later = s.render(FrameIndex{40});
    AUREA_CHECK_MSG(max_abs_diff(a, later) > 0.05f, "giro e ruído animam");
    cl_set(s, id, "seed", 7.0f);
    const auto seeded = s.render(FrameIndex{10});
    AUREA_CHECK_MSG(max_abs_diff(a, seeded) > 0.05f, "outra semente, outra nuvem");
    AUREA_CHECK(seeded.px == s.render(FrameIndex{10}).px);
    // Parada: o tempo não muda nada.
    constellation_still(s, id);
    AUREA_CHECK(s.render(FrameIndex{10}).px == s.render(FrameIndex{40}).px);
    // Só o giro (sem ruído) também anima.
    cl_set(s, id, "spin_y", 40.0f);
    AUREA_CHECK(max_abs_diff(s.render(FrameIndex{10}), s.render(FrameIndex{40})) > 0.05f);
    // Opacidade 0: a camada passa intacta.
    const auto black = [&] {
        cl_set(s, id, "opacity", 0.0f);
        return s.render(FrameIndex{10});
    }();
    AUREA_CHECK(black.mean().x < 1e-4f);
}

AUREA_TEST(EffectPackGpu, ConstellationFollowsTheCompositionCamera) {
    // Câmera da composição no centro a 45/tan(25°) px com 50°: é a mesma
    // projeção da câmera própria (50°) — a imagem bate. Girar a câmera gira
    // a rede; "Seguir a câmera 3D" desligado volta à câmera própria.
    AUREA_REQUIRE_GPU();
    Scene s(160, 90);
    const auto id = s.image(uniform_image(160, 90, 0, 0, 0), 80, 45);
    s.add_effect(id, effect_keys::kConstellation);
    constellation_still(s, id);
    cl_set(s, id, "count", 120.0f);
    cl_set(s, id, "line_max_distance", 60.0f);
    const auto plain = s.render(FrameIndex{10});
    const LayerId cam = s.comp->add_layer(LayerKind::Camera, "camera");
    Layer* c = s.comp->layer(cam);
    c->start = FrameIndex{0};
    c->end = s.comp->duration();
    c->transform.anchor = Vec3{0, 0, 0};
    c->transform.position = Vec3{80, 45, -45.0f / std::tan(25.0f * kDeg2Rad)};
    c->camera.fov = 50.0f;
    c->camera.active = true;
    const auto straight = s.render(FrameIndex{10});
    std::printf("\n    constelacao: camera reta x propria %.5f", max_abs_diff(plain, straight));
    AUREA_CHECK_MSG(max_abs_diff(plain, straight) < 0.02f, "câmera reta = câmera própria");
    s.comp->layer(cam)->transform.rotation = Vec3{0, 35, 0};
    const auto turned = s.render(FrameIndex{10});
    std::printf("  girada %.4f\n", max_abs_diff(straight, turned));
    AUREA_CHECK_MSG(max_abs_diff(straight, turned) > 0.05f, "girar a câmera devia girar a rede");
    s.comp->layer(cam)->transform.rotation = Vec3{0, 0, 0};
    s.comp->layer(cam)->transform.position = Vec3{80, 45, -200.0f};   // recua: a rede encolhe
    const auto back = s.render(FrameIndex{10});
    AUREA_CHECK(max_abs_diff(straight, back) > 0.05f);
    cl_set(s, id, "follow_camera", 0.0f);
    AUREA_CHECK_MSG(max_abs_diff(plain, s.render(FrameIndex{10})) < 1e-5f, "sem seguir, a câmera não mexe na rede");
}

AUREA_TEST(EffectPackGpu, ConstellationPathSourceFollowsTheLayerBelow) {
    // "Caminho da camada": pontos à mesma distância no contorno da camada de
    // baixo (aqui um quadrado de 60 px). Mais de 64 pontos (o teto antigo).
    AUREA_REQUIRE_GPU();
    Scene s(96, 96);
    s.solid(60, 60, Vec4{0, 0, 0, 1}, 48, 48);
    const auto id = s.image(uniform_image(96, 96, 0, 0, 0), 48, 48);
    s.add_effect(id, effect_keys::kConstellation);
    constellation_still(s, id);
    cl_set(s, id, "source", 5.0f);
    cl_set(s, id, "count", 200.0f);
    cl_set(s, id, "lines", 0.0f);
    cl_set(s, id, "point_size", 3.0f);
    cl_set(s, id, "point_softness", 0.0f);
    const auto img = s.render(FrameIndex{0});
    std::printf("\n    caminho: borda %.3f %.3f centro %.3f\n", cl_lum(img, 18, 48), cl_lum(img, 48, 78), cl_lum(img, 48, 48));
    AUREA_CHECK(cl_lum(img, 18, 48) > 0.3f);   // lado esquerdo do quadrado (x = 18)
    AUREA_CHECK(cl_lum(img, 48, 78) > 0.3f);   // lado de baixo (y = 78)
    AUREA_CHECK(cl_lum(img, 48, 48) < 0.02f);  // miolo vazio
}

AUREA_TEST(EffectPackGpu, ConstellationFullHdCostWithAThousandPoints) {
    // Custo em 1080p com 1000 pontos: GPU dos efeitos (consultas de tempo, o
    // melhor de 8 quadros) e o planejamento na CPU (prepare inteiro).
    AUREA_REQUIRE_GPU();
    Scene s(1920, 1080);
    const auto id = s.image(uniform_image(1920, 1080, 0, 0, 0), 960, 540);
    s.add_effect(id, effect_keys::kConstellation);
    cl_set(s, id, "count", 1000.0f);
    struct Look { const char* name; bool triangles; };
    for (const Look look : {Look{"linhas+pontos", false}, Look{"com triangulos", true}}) {
        cl_set(s, id, "triangles", look.triangles ? 1.0f : 0.0f);
        const Vec4 mean = s.render(FrameIndex{30}).mean();   // aquece o pipeline
        AUREA_CHECK(mean.x > 0.001f);
        Gpu& g = gpu();
        RenderSettings rs;
        rs.gpuTimers = true;
        rs.finalQuality = true;
        OffscreenTarget off{g.target(1920, 1080), 1920, 1080};
        f32 best = 1e9f;
        f64 bestPrepare[2] = {1e9, 1e9};   // com o efeito / sem (a diferença é o planejamento dele)
        static u64 frame = 930000;
        for (int pass = 0; pass < 2; ++pass) {
            constellation_of(s, id).enabled = pass == 0;
            for (int r = 0; r < 8; ++r) {
                FrameSnapshot snap;
                FrameStats stats;
                RenderTimings timings;
                const auto t0 = std::chrono::steady_clock::now();
                g.renderer.prepare(*s.comp, s.project, FrameIndex{31 + r}, &s.media, &Scene::lookup, &s, rs, ++frame, 0,
                                   DecodeMode::Still, 1.0f, snap);
                const auto t1 = std::chrono::steady_clock::now();
                bestPrepare[pass] = std::min(bestPrepare[pass], std::chrono::duration<f64, std::milli>(t1 - t0).count());
                AUREA_CHECK(g.renderer.render(snap, rs, &off, stats, timings).ok());
                g.backend.wait_idle();
                if (pass == 0 && timings.gpuMeasured && timings.gpuEffectsMs > 0.0f) best = std::min(best, timings.gpuEffectsMs);
            }
        }
        constellation_of(s, id).enabled = true;
        std::printf("\n    constelacao 1080p 1000 pontos (%s): GPU dos efeitos %.2f ms, prepare (CPU) %.2f ms (sem o efeito %.2f), media %.4f",
                    look.name, best, bestPrepare[0], bestPrepare[1], mean.x);
    }
    std::printf("\n");
}

AUREA_TEST(EffectPackGpu, ConstellationCatalogPreviewShowsTheNetwork) {
    // A prévia do catálogo roda fora do laço de render: a textura de dado
    // pedida no planejamento dela tem de subir antes do passe (senão o cartão
    // sai igual à cartela). Comparada com um controle que não desenha.
    AUREA_REQUIRE_GPU();
    Gpu& g = gpu();
    g.renderer.set_effect_preview_source({}, 0, 0);
    std::vector<u8> plate, net, again;
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kCheckboxControl), 160, 100, plate).ok());
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kConstellation), 160, 100, net).ok());
    AUREA_CHECK(g.renderer.render_effect_preview(g.effects, effect_type_id(effect_keys::kConstellation), 160, 100, again).ok());
    AUREA_CHECK_EQ(net.size(), plate.size());
    usize changed = 0;
    for (usize i = 0; i < net.size() && i < plate.size(); ++i) changed += std::abs(int(net[i]) - int(plate[i])) > 24 ? 1u : 0u;
    std::printf("\n    previa do catalogo: %zu canais mudaram\n", changed);
    AUREA_CHECK_MSG(changed > 300, "a prévia da Constelação tem de mostrar a rede");
    AUREA_CHECK(net == again);   // determinística
}

AUREA_TEST(EffectPackGpu, ConstellationPresetsRenderReferenceImages) {
    // Os presets em 1080p, salvos para conferir a olho
    // (constelacao_<preset>.png na pasta de trabalho do teste). Cada um desenha.
    AUREA_REQUIRE_GPU();
    const Effect* fx = gpu().effects.find(effect_type_id(effect_keys::kConstellation));
    AUREA_CHECK(fx != nullptr);
    if (!fx) return;
    for (const EffectPreset& preset : fx->presets()) {
        Scene s(1920, 1080);
        const auto id = s.image(uniform_image(1920, 1080, 0, 0, 0), 960, 540);
        EffectInstance& inst = s.add_effect(id, effect_keys::kConstellation);
        for (const EffectPresetValue& v : preset.values)
            if (v.param < inst.params.size() && v.component < 4) inst.params[v.param].constant.v[v.component] = v.value;
        const auto img = s.render(FrameIndex{45});
        (void)write_png(std::string("constelacao_") + preset.id + ".png", img.encoded());
        f32 peak = 0.0f;
        for (usize i = 0; i + 3 < img.px.size(); i += 4) peak = std::max(peak, img.px[i]);
        std::printf("\n    preset %-12s media %.4f pico %.3f", preset.id, img.mean().x, peak);
        AUREA_CHECK(img.mean().x > 0.0005f);   // a poeira é esparsa: média baixa
        AUREA_CHECK(peak > 0.3f);              // mas algo aceso de verdade
    }
    std::printf("\n");
}
