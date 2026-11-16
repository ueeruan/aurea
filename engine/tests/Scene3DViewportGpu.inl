// =============================================================================
//  Aurea PC, Cena 3D (lote do RENDERER, 12/11/2026): sombreamento da vista,
//  contorno da seleção, Material Options e luzes do AE, luz de área, primitivas.
//
//  O SceneRenderer e os shaders 3D são COMPARTILHADOS com Android e iOS. A
//  primeira suíte daqui (Scene3DBaselineGpu) prova que projetos existentes saem
//  com os MESMOS pixels depois das mudanças: as cenas de referência foram
//  gravadas com o motor de ANTES do lote (AUREA_R3D_RECORD=1) em
//  build/reference/blender/r3d_baseline/ e o teste compara byte a byte.
//  Incluído depois de Scene3D052Gpu.inl (usa Scene3DRig, Scene e ao_test).
// =============================================================================
namespace r3d_base {

std::filesystem::path dir() {
    const auto root = std::filesystem::path(AUREA_TEST_DATA_DIR).parent_path().parent_path().parent_path();
    const auto path = root / "build/reference/blender/r3d_baseline";
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path;
}

bool recording() { return std::getenv("AUREA_R3D_RECORD") != nullptr; }

/// Grava (modo de gravação) ou compara com a referência. Exige igualdade exata.
void check_bytes(const std::string& name, const void* data, usize bytes, u32 w, u32 h) {
    const auto path = dir() / (name + ".bin");
    if (recording()) {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        AUREA_CHECK(f != nullptr);
        if (!f) return;
        const u32 header[2]{w, h};
        std::fwrite(header, sizeof(header), 1, f);
        std::fwrite(data, 1, bytes, f);
        std::fclose(f);
        std::printf("\n    gravado %s (%ux%u, %zu bytes)", name.c_str(), w, h, static_cast<size_t>(bytes));
        return;
    }
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    AUREA_CHECK_MSG(f != nullptr, "referencia ausente (rode com AUREA_R3D_RECORD=1 no motor antigo)");
    if (!f) return;
    u32 header[2]{};
    std::vector<u8> ref(bytes + 1);
    const usize got = std::fread(header, sizeof(header), 1, f) == 1 ? std::fread(ref.data(), 1, ref.size(), f) : 0;
    std::fclose(f);
    AUREA_CHECK_EQ(header[0], w);
    AUREA_CHECK_EQ(header[1], h);
    AUREA_CHECK_EQ(got, bytes);
    if (got != bytes) return;
    const u8* now = static_cast<const u8*>(data);
    usize changed = 0, first = bytes;
    for (usize i = 0; i < bytes; ++i)
        if (now[i] != ref[i]) { ++changed; if (first == bytes) first = i; }
    std::printf("\n    %s: %zu de %zu bytes diferentes", name.c_str(), static_cast<size_t>(changed), static_cast<size_t>(bytes));
    AUREA_CHECK_MSG(changed == 0, "o render padrao mudou");
}

void check(const std::string& name, const Image8& img) {
    AUREA_CHECK(img.width > 0 && !img.rgba.empty());
    if (recording()) (void)write_png((dir() / (name + ".png")).string(), img);
    check_bytes(name, img.rgba.data(), img.rgba.size(), img.width, img.height);
}

void check(const std::string& name, const FloatImage& img) {
    AUREA_CHECK(img.width > 0 && !img.px.empty());
    check_bytes(name, img.px.data(), img.px.size() * sizeof(f32), img.width, img.height);
}

Image8 preview(Scene3DRig& rig, u32 maxDim) {
    Image8 img;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    AUREA_CHECK(rig.e.capture_preview_frame_rgba(maxDim, rgba, w, h).ok());
    img.width = w;
    img.height = h;
    img.rgba = std::move(rgba);
    return img;
}

Layer* layer_of(Scene3DRig& rig, const Result<u64>& id) {
    return id.ok() ? current_comp(rig.e)->layer(LayerId::unpack(*id)) : nullptr;
}

/// Camada 2D (sólido) no espaço 3D, na composição do rig.
Layer* plane(Composition* comp, f32 w, f32 h, Vec4 color, Vec3 pos, Vec3 rot, bool lit) {
    const LayerId id = comp->add_layer(LayerKind::Shape, "plano");
    Layer* l = comp->layer(id);
    l->shape.bounds = Rect{0, 0, w, h};
    l->shape.fillColor = color;
    l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0};
    l->transform.position = pos;
    l->transform.rotation = rot;
    l->threeD = true;
    l->acceptsLights = lit;
    l->end = comp->duration();
    return l;
}

/// Luz direto na composição (cenas do `Scene`, sem o Engine).
Layer* light(Composition* comp, LightKind kind, Vec3 pos, Vec3 rot, f32 intensity, bool shadows) {
    const LayerId id = comp->add_layer(LayerKind::Light, "luz");
    Layer* l = comp->layer(id);
    l->threeD = true;
    l->end = comp->duration();
    l->light.kind = kind;
    l->light.intensity = intensity;
    l->light.range = 900.0f;
    l->light.castShadows = shadows;
    l->transform.position = pos;
    l->transform.rotation = rot;
    return l;
}

} // namespace r3d_base

// Modelos PBR: sol com sombra no chão que só pega sombra; export e prévia.
AUREA_TEST(Scene3DBaselineGpu, SunCubeShadowCatcher) {
    AUREA_REQUIRE_GPU();
    using aurea::scene3d::Shape3DKind;
    Scene3DRig rig(320, 180);
    AUREA_CHECK(rig.e.set_scene_floor(1, .5f, .5f, .5f, .5f, .3f));
    const auto cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    const auto sun = rig.e.add_light(0);
    AUREA_CHECK(cube.ok() && sun.ok());
    if (!cube.ok() || !sun.ok()) return;
    Layer* c = r3d_base::layer_of(rig, cube);
    c->transform.rotation = Vec3{20, 30, 0};
    c->transform.position = Vec3{150, 80, 40};
    seek_frame(rig.e, 0);
    r3d_base::check("b1_sol_export", rig.capture(320));
    r3d_base::check("b1_sol_previa", r3d_base::preview(rig, 320));
}

// Luz pontual e spot com sombra (atlas das luzes locais) e chão que reflete.
AUREA_TEST(Scene3DBaselineGpu, PointAndSpotShadowsReflectiveFloor) {
    AUREA_REQUIRE_GPU();
    using aurea::scene3d::Shape3DKind;
    Scene3DRig rig(320, 180);
    AUREA_CHECK(rig.e.apply_reflective_floor());
    const auto sphere = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Sphere), "Esfera");
    const auto box = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Torus), "Toro");
    const auto point = rig.e.add_light(1);
    const auto spot = rig.e.add_light(2);
    AUREA_CHECK(sphere.ok() && box.ok() && point.ok() && spot.ok());
    if (!sphere.ok() || !box.ok() || !point.ok() || !spot.ok()) return;
    r3d_base::layer_of(rig, sphere)->transform.position = Vec3{110, 90, 60};
    Layer* t = r3d_base::layer_of(rig, box);
    t->transform.position = Vec3{215, 95, 30};
    t->transform.rotation = Vec3{60, 10, 0};
    Layer* p = r3d_base::layer_of(rig, point);
    p->transform.position = Vec3{60, 40, -60};
    p->light.castShadows = true;
    p->light.intensity = 3.0f;
    p->light.color = Vec4{1.0f, 0.8f, 0.6f, 1.0f};
    Layer* s = r3d_base::layer_of(rig, spot);
    s->transform.position = Vec3{260, 20, -80};
    s->transform.rotation = Vec3{-30, -25, 0};
    s->light.castShadows = true;
    s->light.intensity = 2.0f;
    s->light.coneAngle = 60.0f;
    seek_frame(rig.e, 0);
    r3d_base::check("b2_locais_export", rig.capture(320));
    r3d_base::check("b2_locais_previa", r3d_base::preview(rig, 320));
}

// Luz ambiente + pontual sem sombra, com alcance curto.
AUREA_TEST(Scene3DBaselineGpu, AmbientPlusRangedPoint) {
    AUREA_REQUIRE_GPU();
    using aurea::scene3d::Shape3DKind;
    Scene3DRig rig(320, 180);
    const auto cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cylinder), "Cilindro");
    const auto amb = rig.e.add_light(3);
    const auto point = rig.e.add_light(1);
    AUREA_CHECK(cube.ok() && amb.ok() && point.ok());
    if (!cube.ok() || !amb.ok() || !point.ok()) return;
    r3d_base::layer_of(rig, cube)->transform.rotation = Vec3{25, 0, 15};
    Layer* p = r3d_base::layer_of(rig, point);
    p->transform.position = Vec3{200, 50, -40};
    p->light.range = 260.0f;
    seek_frame(rig.e, 0);
    r3d_base::check("b3_ambiente_export", rig.capture(320));
}

// Modelo importado com esqueleto (Fox) num estúdio, quadro animado.
AUREA_TEST(Scene3DBaselineGpu, SkinnedFoxInStudio) {
    AUREA_REQUIRE_GPU();
    const std::string path = std::string(AUREA_TEST_DATA_DIR) + "/gltf/Fox.glb";
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) std::fclose(f);
    else { std::printf("(Fox.glb ausente: pulado) "); return; }
    Scene3DRig rig(320, 180);
    AUREA_CHECK(rig.e.set_studio_environment(1).ok());
    ModelImport mi;
    mi.path = path;
    const auto fox = rig.e.import_model(mi);
    AUREA_CHECK(fox.ok());
    if (!fox.ok()) return;
    seek_frame(rig.e, 6);
    r3d_base::check("b4_raposa_export", rig.capture(320));
}

// Camadas 2D no 3D com "Aceita luzes" ao lado de um modelo (plane.frag no grupo),
// as três luzes e o chão que reflete os planos (Ground.cpp).
AUREA_TEST(Scene3DBaselineGpu, LitPlanesBesideAModel) {
    AUREA_REQUIRE_GPU();
    using aurea::scene3d::Shape3DKind;
    Scene3DRig rig(320, 180);
    AUREA_CHECK(rig.e.apply_reflective_floor());
    const auto cube = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    const auto sun = rig.e.add_light(0);
    const auto point = rig.e.add_light(1);
    const auto spot = rig.e.add_light(2);
    AUREA_CHECK(cube.ok() && sun.ok() && point.ok() && spot.ok());
    if (!cube.ok() || !sun.ok() || !point.ok() || !spot.ok()) return;
    Composition* comp = current_comp(rig.e);
    r3d_base::plane(comp, 120, 90, Vec4{0.9f, 0.3f, 0.2f, 1}, Vec3{90, 90, 80}, Vec3{0, 35, 0}, true);
    r3d_base::plane(comp, 140, 70, Vec4{0.2f, 0.6f, 0.9f, 1}, Vec3{230, 80, 120}, Vec3{-20, -30, 10}, true);
    r3d_base::plane(comp, 60, 60, Vec4{0.9f, 0.9f, 0.2f, 1}, Vec3{160, 140, 20}, Vec3{70, 0, 0}, false);
    r3d_base::layer_of(rig, cube)->transform.position = Vec3{170, 70, 60};
    r3d_base::layer_of(rig, point)->transform.position = Vec3{100, 40, -30};
    Layer* s = r3d_base::layer_of(rig, spot);
    s->transform.position = Vec3{250, 30, -60};
    s->transform.rotation = Vec3{-20, -20, 0};
    seek_frame(rig.e, 0);
    r3d_base::check("b5_planos_export", rig.capture(320));
    r3d_base::check("b5_planos_previa", r3d_base::preview(rig, 320));
}

// Só planos (sem modelo): Motion Tile no plano (plane_tile.frag), extrusão
// (raster_extrude.frag) e 3D Shatter (shatter.frag), todos acesos.
AUREA_TEST(Scene3DBaselineGpu, TileExtrudeShatterPlanesLit) {
    AUREA_REQUIRE_GPU();
    for (int variant = 0; variant < 3; ++variant) {
        Scene s(256, 144);
        const LayerId id = s.image(reference_image(96, 64), 128, 72);
        Layer* l = s.comp->layer(id);
        l->threeD = true;
        l->acceptsLights = true;
        l->transform.rotation = Vec3{15, 30, 0};
        r3d_base::light(s.comp, LightKind::Point, Vec3{70, 30, -120}, Vec3{}, 1.4f, false);
        r3d_base::light(s.comp, LightKind::Spot, Vec3{200, 40, -150}, Vec3{-10, -25, 0}, 1.2f, false);
        r3d_base::light(s.comp, LightKind::Ambient, Vec3{}, Vec3{}, 0.15f, false);
        if (variant == 0) {
            auto& tile = s.add_effect(id, effect_keys::kMotionTile);
            (void)tile;
        } else if (variant == 1) {
            auto& fx = s.add_effect(id, effect_keys::kRasterExtrude);
            fx.params[0].constant = ParamValue::scalar(24);
        } else {
            auto& fx = s.add_effect(id, effect_keys::kShatter3D);
            fx.params[0].constant.v[0] = 45;
            fx.params[5].constant.v[0] = 160;
        }
        const char* names[3] = {"b6_tile", "b6_extrude", "b6_shatter"};
        r3d_base::check(names[variant], s.render(FrameIndex{4}, 1, true));
    }
}

// A vista de navegação do editor (Aurea PC) com os planos e um modelo.
AUREA_TEST(Scene3DBaselineGpu, EditorOrbitViewOfLitPlanes) {
    AUREA_REQUIRE_GPU();
    Scene s(256, 144);
    r3d_base::plane(s.comp, 120, 90, Vec4{0.9f, 0.3f, 0.2f, 1}, Vec3{90, 72, 40}, Vec3{0, 35, 0}, true);
    r3d_base::plane(s.comp, 100, 70, Vec4{0.2f, 0.6f, 0.9f, 1}, Vec3{170, 72, 90}, Vec3{-20, -30, 10}, true);
    r3d_base::light(s.comp, LightKind::Point, Vec3{60, 30, -100}, Vec3{}, 1.5f, true);
    r3d_base::light(s.comp, LightKind::Directional, Vec3{128, 0, -200}, Vec3{-40, 20, 0}, 0.6f, true);
    SceneEditorView view;
    view.enabled = true;
    view.yaw = -25.0f;
    view.pitch = 18.0f;
    view.distance = 2.5f;
    r3d_base::check("b7_editor_planos", s.render(FrameIndex{0}, 1, false, view));
}
