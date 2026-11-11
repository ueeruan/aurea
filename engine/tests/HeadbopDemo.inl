// =============================================================================
//  Aurea / tests / HeadbopDemo.inl  (incluído por HeadbopGpu.inl)
//
//  Demonstração, não verificação: com AUREA_HEADBOP_DEMO=<foto.png> e
//  AUREA_HEADBOP_DEMO_OUT=<pasta>, aplica o Headbop (Bop, 117,5 BPM, recorte da
//  pessoa) na foto e grava 3 s de quadros PNG em qualidade final. Os pontos do
//  corpo vêm de AUREA_HEADBOP_DEMO_PTS ("i:x,y;i:x,y;…" normalizados, índices
//  BlazePose), como a análise do app entregaria. Sem as variáveis, não faz nada.
// =============================================================================

AUREA_TEST(Gpu, HeadbopDemoOnPhoto) {
    const char* src = std::getenv("AUREA_HEADBOP_DEMO");
    const char* out = std::getenv("AUREA_HEADBOP_DEMO_OUT");
    const char* pts = std::getenv("AUREA_HEADBOP_DEMO_PTS");
    if (!src || !out || !pts || !*src || !*out || !*pts) return;
    AUREA_REQUIRE_GPU();
    using namespace headbop_gpu;
    Image8 photo;
    AUREA_CHECK(read_png(src, photo));
    if (photo.width == 0) return;
    const u32 W = photo.width, H = photo.height;
    ImagePixels px;
    px.width = W;
    px.height = H;
    px.rgba = photo.rgba;

    Scene s(W, H);
    s.comp->set_background(Color{1, 1, 1, 1});
    const LayerId id = s.image(std::move(px), W * 0.5f, H * 0.5f);
    EffectInstance& e = s.add_effect(id, hb::kHeadbopKey);

    std::vector<f32> lm(33 * 4, 0.0f);
    for (const char* p = pts; *p;) {
        unsigned i = 0;
        float x = 0, y = 0;
        int n = 0;
        if (std::sscanf(p, "%u:%f,%f%n", &i, &x, &y, &n) != 3 || i >= 33) break;
        lm[i * 4] = x;
        lm[i * 4 + 1] = y;
        lm[i * 4 + 3] = 0.95f;
        p += n;
        if (*p == ';') ++p;
    }
    hb::RigFace f;
    AUREA_CHECK(hb::face_from_landmarks(lm.data(), static_cast<f32>(W) / H, f));
    e.params[hb::kRigState].constant = ParamValue::scalar(2.0f);
    const u32 b = hb::kRigFirst;
    e.params[b + 0].constant = ParamValue::vec2(f.center.x, f.center.y);
    e.params[b + 1].constant = ParamValue::vec2(f.axes.x, f.axes.y);
    e.params[b + 2].constant = ParamValue::vec2(f.roll, f.yaw);
    e.params[b + 3].constant = ParamValue::vec2(f.pitch, f.depth);
    e.params[b + 4].constant = ParamValue::vec2(f.neck.x, f.neck.y);
    e.params[b + 5].constant = ParamValue::vec2(f.shoulderA.x, f.shoulderA.y);
    e.params[b + 6].constant = ParamValue::vec2(f.shoulderB.x, f.shoulderB.y);
    const char* cut = std::getenv("AUREA_HEADBOP_DEMO_CUTOUT");
    const char* outMode = std::getenv("AUREA_HEADBOP_DEMO_OUTPUT");
    const char* frames = std::getenv("AUREA_HEADBOP_DEMO_FRAMES");
    e.params[hb::kCutout].constant = ParamValue::boolean(!(cut && *cut == '0'));
    if (outMode && *outMode) e.params[hb::kOutput].constant = ParamValue::scalar(static_cast<f32>(std::atoi(outMode)));
    e.params[hb::kBeatSource].constant = ParamValue::scalar(static_cast<f32>(hb::BeatSource::Manual));
    e.params[hb::kBpm].constant = ParamValue::scalar(117.5f);
    // 0.5.2 movimento secundário: AUREA_HEADBOP_DEMO_HAIR / _CLOTH / _PHYSICS em % (padrão: o do efeito).
    if (const char* v = std::getenv("AUREA_HEADBOP_DEMO_HAIR"); v && *v) e.params[hb::kHair].constant = ParamValue::scalar(static_cast<f32>(std::atof(v)));
    if (const char* v = std::getenv("AUREA_HEADBOP_DEMO_CLOTH"); v && *v) e.params[hb::kCloth].constant = ParamValue::scalar(static_cast<f32>(std::atof(v)));
    if (const char* v = std::getenv("AUREA_HEADBOP_DEMO_PHYSICS"); v && *v) e.params[hb::kPhysics].constant = ParamValue::scalar(static_cast<f32>(std::atof(v)));

    const u32 count = frames && *frames ? static_cast<u32>(std::atoi(frames)) : 90u;
    for (u32 frame = 0; frame < count; ++frame) {
        const FloatImage img = s.render(FrameIndex{static_cast<i64>(frame)}, 1, true);
        char name[64];
        std::snprintf(name, sizeof name, "/f%03u.png", frame);
        AUREA_CHECK(write_png(std::string(out) + name, img.encoded()));
    }
}
