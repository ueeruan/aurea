// =============================================================================
//  Aurea / tests / CameraRegressionScenarios.inl
//
//  Câmera 3D depois da Beta 0.0.2: o MESMO projeto montado pela API do motor
//  que as duas telas usam (import_image/import_video, add_text, add_shape,
//  add_camera, add_light, add_null, LayerSetParent, precompose, keyframes) e
//  capturado pelo caminho de captura do Engine. As referências em
//  data/golden/camera/ foram geradas pelo build 2138 (último público antes da
//  0.0.2) e reduzidas a 160 px: o que funcionava lá tem de continuar igual.
//
//  Variáveis de ambiente (só para investigação):
//    AUREA_CAMREG_DIR     grava os quadros de cada cenário (320 px)
//    AUREA_CAMREG_GOLDEN  grava as referências reduzidas (160 px)
//    AUREA_CAMREG_SAVE    grava o projeto de cada cenário (.aurea)
//    AUREA_CAMREG_LOAD    reabre os projetos gravados por outro build e captura
// =============================================================================
namespace camreg {

constexpr u32 kW = 320, kH = 180;

std::string env_dir(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : std::string();
}

/// Quadrantes coloridos com borda branca: translação, escala, giro e
/// perspectiva aparecem na imagem.
Image8 pattern(u32 w, u32 h) {
    Image8 img;
    img.width = w; img.height = h;
    img.rgba.resize(static_cast<usize>(w) * h * 4);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u8* p = &img.rgba[(static_cast<usize>(y) * w + x) * 4];
            const bool border = x < 4 || y < 4 || x + 4 >= w || y + 4 >= h;
            const u32 q = (x < w / 2 ? 0u : 1u) + (y < h / 2 ? 0u : 2u);
            static constexpr u8 colors[4][3]{{230, 40, 40}, {40, 200, 60}, {50, 80, 230}, {240, 210, 40}};
            p[0] = border ? 255 : colors[q][0];
            p[1] = border ? 255 : colors[q][1];
            p[2] = border ? 255 : colors[q][2];
            p[3] = 255;
        }
    return img;
}

struct Rig {
    SyntheticFactory factory;
    Engine e;
    static SyntheticConfig video_config() {
        SyntheticConfig cfg;
        cfg.width = 96; cfg.height = 54; cfg.frameCount = 600;
        cfg.pattern = SyntheticPattern::Quadrants;
        return cfg;
    }
    Rig() : factory(video_config()) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.mediaFactory = &factory;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ec.imageLoader = [](const char* path, ImagePixels& out, void*) {
            Image8 image;
            if (!read_png(path, image)) return false;
            out.width = image.width; out.height = image.height; out.rgba = std::move(image.rgba);
            return true;
        };
        AUREA_CHECK(e.initialize(ec).ok());
    }
    ~Rig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* layer(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    Image8 capture(i64 frame) {
        seek_frame(e, frame);
        Image8 img;
        // Duas capturas: o vídeo sintético decodifica no primeiro pedido.
        for (int i = 0; i < 2; ++i) {
            std::vector<u8> rgba;
            u32 w = 0, h = 0;
            AUREA_CHECK(e.capture_frame_rgba(kW, rgba, w, h).ok());
            img.width = w; img.height = h; img.rgba = std::move(rgba);
        }
        return img;
    }
};

std::string pattern_path() {
    std::string dir = env_dir("AUREA_CAMREG_DIR");
    const std::string path = (dir.empty() ? std::string(".") : dir) + "/camreg_padrao.png";
    static bool written = false;
    if (!written) written = write_png(path, pattern(128, 96));
    return path;
}

u64 add_image(Rig& r) {
    const Image8 img = pattern(128, 96);
    const std::string path = pattern_path();
    const auto id = r.e.import_image(img.rgba.data(), img.width, img.height, "padrao", path.c_str());
    AUREA_CHECK(id.ok());
    return id.ok() ? *id : 0;
}

u64 add_video(Rig& r) {
    VideoImport vi;
    vi.sourcePath = "sintetico";
    vi.displayName = "video";
    const auto id = r.e.import_video(vi);
    AUREA_CHECK(id.ok());
    return id.ok() ? *id : 0;
}

void key(Rig& r, u64 layer, TrackProperty prop, i64 frame, f32 value) {
    Command k;
    k.type = CommandType::KeyframeInsert;
    k.keyframe.track = TrackRef{LayerId::unpack(layer), prop, kInvalidIndex, 0};
    k.keyframe.time = FrameIndex{frame};
    k.keyframe.value = value;
    k.keyframe.onlyIfChanged = 0;
    AUREA_CHECK(r.e.apply_command(k).ok());
}

void set_parent(Rig& r, u64 child, u64 parent) {
    Command c;
    c.type = CommandType::LayerSetParent;
    c.layer_parent.layer = LayerId::unpack(child);
    c.layer_parent.parent = LayerId::unpack(parent);
    AUREA_CHECK(r.e.apply_command(c).ok());
}

/// Câmera com dolly + órbita: X e Z andam, gira em Y. Os keyframes vão
/// nos quadros ABSOLUTOS da composição (tempo local = absoluto − início).
u64 animated_camera(Rig& r, i64 from = 0, i64 to = 90) {
    const auto cam = r.e.add_camera();
    AUREA_CHECK(cam.ok());
    if (!cam.ok()) return 0;
    Layer* c = r.layer(*cam);
    const Vec3 p = c->transform.position;
    const i64 s = c->start.value;
    key(r, *cam, TrackProperty::PositionX, from - s, p.x);
    key(r, *cam, TrackProperty::PositionX, to - s, p.x + 120.0f);
    key(r, *cam, TrackProperty::PositionZ, from - s, p.z);
    key(r, *cam, TrackProperty::PositionZ, to - s, p.z + 200.0f);
    key(r, *cam, TrackProperty::RotationY, from - s, 0.0f);
    key(r, *cam, TrackProperty::RotationY, to - s, -18.0f);
    return *cam;
}

/// Imagem, vídeo, texto e forma. `depth` = em 3D (posição Z/rotação X/Y,
/// como o painel "Mostrar X/Y/Z (3D)" grava); senão camadas 2D puras.
std::vector<u64> four_layers(Rig& r, bool depth) {
    std::vector<u64> ids;
    const u64 img = add_image(r), vid = add_video(r);
    const auto txt = r.e.add_text("AUREA");
    const auto shp = r.e.add_shape(0);
    AUREA_CHECK(txt.ok() && shp.ok());
    ids = {img, vid, txt.ok() ? *txt : 0, shp.ok() ? *shp : 0};
    const Vec2 places[4]{{100, 60}, {220, 60}, {100, 130}, {220, 130}};
    for (u32 i = 0; i < 4; ++i) {
        Layer* l = r.layer(ids[i]);
        if (!l) continue;
        l->transform.position.x = places[i].x;
        l->transform.position.y = places[i].y;
        if (i == 0) l->transform.scale = Vec3{0.6f, 0.6f, 1.0f};
        if (i == 1) l->transform.scale = Vec3{l->transform.scale.x * 0.35f, l->transform.scale.y * 0.35f, 1.0f};
        if (i == 3) l->transform.scale = Vec3{0.3f, 0.3f, 1.0f};
        if (depth) {
            l->transform.position.z = i == 0 ? 80.0f : i == 3 ? -60.0f : 0.0f;
            if (i == 1) l->transform.rotation.y = 25.0f;
            if (i == 2) l->transform.rotation.x = 15.0f;
        }
    }
    return ids;
}

struct Scenario {
    const char* name;
    bool golden;                       ///< o 2138 é o comportamento esperado
    std::vector<i64> frames;
    void (*build)(Rig&);
};

void s_cam0_3d(Rig& r) { four_layers(r, true); animated_camera(r); }
void s_cam60_3d(Rig& r) { seek_frame(r.e, 60); four_layers(r, true); animated_camera(r, 60, 120); }
void s_cam_2d(Rig& r) { four_layers(r, false); animated_camera(r); }
void s_parent_cam(Rig& r) {
    const u64 img = add_image(r);
    const u64 txt = [&] { auto t = r.e.add_text("HUD"); return t.ok() ? *t : 0; }();
    Layer* l = r.layer(img);
    l->transform.position.x = 110; l->transform.scale = Vec3{0.5f, 0.5f, 1.0f};
    const u64 cam = animated_camera(r);
    set_parent(r, img, cam);
    set_parent(r, txt, cam);
    // Referência no mundo: o que NÃO está preso à câmera se move na tela.
    const u64 world = add_image(r);
    Layer* w = r.layer(world);
    w->transform.position = Vec3{240, 120, 100};
    w->transform.scale = Vec3{0.4f, 0.4f, 1.0f};
}
void s_null_rig(Rig& r) {
    four_layers(r, true);
    const auto null3d = r.e.add_null(true);
    AUREA_CHECK(null3d.ok());
    const u64 cam = animated_camera(r);
    set_parent(r, cam, *null3d);
    key(r, *null3d, TrackProperty::RotationY, 0, 0.0f);
    key(r, *null3d, TrackProperty::RotationY, 90, 20.0f);
}
u64 group_of_two(Rig& r) {
    const u64 img = add_image(r);
    const auto txt = r.e.add_text("GRUPO");
    Layer* l = r.layer(img);
    l->transform.position.z = 60.0f; l->transform.scale = Vec3{0.6f, 0.6f, 1.0f};
    Layer* t = r.layer(*txt);
    t->transform.position.y = 140.0f; t->transform.rotation.x = 10.0f;
    const u64 ids[2]{img, *txt};
    const auto g = r.e.precompose(ids, 2, "grupo");
    AUREA_CHECK(g.ok());
    return g.ok() ? *g : 0;
}
void s_group_pass(Rig& r) { const u64 g = group_of_two(r); AUREA_CHECK(r.e.set_group_camera_pass_through(g, true)); animated_camera(r); }
void s_group_flat(Rig& r) { group_of_two(r); animated_camera(r); }
void s_group_cam_short(Rig& r) {
    const u64 g = group_of_two(r); AUREA_CHECK(r.e.set_group_camera_pass_through(g, true));
    const u64 cam = animated_camera(r);
    r.layer(cam)->end = FrameIndex{45};
}
void s_group_cam_late(Rig& r) {
    const u64 g = group_of_two(r); AUREA_CHECK(r.e.set_group_camera_pass_through(g, true));
    const u64 cam = animated_camera(r);
    r.layer(cam)->start = FrameIndex{45};
}
void s_light_planes(Rig& r) {
    four_layers(r, true);
    animated_camera(r);
    const auto light = r.e.add_light(1);   // luz pontual, como para um modelo
    AUREA_CHECK(light.ok());
}
void s_light_flat2d(Rig& r) {
    four_layers(r, false);
    const auto light = r.e.add_light(0);
    AUREA_CHECK(light.ok());
}

const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> list{
        {"a_cam0_3d", true, {0, 30, 60, 89}, s_cam0_3d},
        {"a_cam60_3d", false, {0, 60, 90, 119}, s_cam60_3d},
        {"b_cam_2d", true, {0, 45, 89}, s_cam_2d},
        {"c_parent_cam", true, {0, 45, 89}, s_parent_cam},
        {"c_null_rig", true, {0, 45, 89}, s_null_rig},
        {"d_group_pass", true, {0, 45, 89}, s_group_pass},
        {"d_group_flat", true, {0, 45, 89}, s_group_flat},
        {"d_group_cam_short", false, {0, 30, 60, 89}, s_group_cam_short},
        {"d_group_cam_late", false, {0, 30, 60, 89}, s_group_cam_late},
        {"e_light_planes", true, {0, 45, 89}, s_light_planes},
        {"e_light_flat2d", true, {0, 45}, s_light_flat2d},
    };
    return list;
}

/// 2×2 → 1 (média): as referências guardadas têm 160 px.
Image8 half(const Image8& in) {
    Image8 out;
    out.width = in.width / 2; out.height = in.height / 2;
    out.rgba.resize(static_cast<usize>(out.width) * out.height * 4);
    for (u32 y = 0; y < out.height; ++y)
        for (u32 x = 0; x < out.width; ++x)
            for (u32 c = 0; c < 4; ++c) {
                const u32 s = in.at(2 * x, 2 * y)[c] + in.at(2 * x + 1, 2 * y)[c]
                            + in.at(2 * x, 2 * y + 1)[c] + in.at(2 * x + 1, 2 * y + 1)[c];
                out.rgba[(static_cast<usize>(y) * out.width + x) * 4 + c] = static_cast<u8>((s + 2) / 4);
            }
    return out;
}

std::string frame_name(const Scenario& s, i64 f, const char* tag = "") {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s%s_f%03lld.png", s.name, tag, static_cast<long long>(f));
    return buf;
}

struct Diff { f64 mean = 0; u32 max = 0; f64 over = 0; };
Diff diff(const Image8& a, const Image8& b) {
    Diff d;
    if (a.width != b.width || a.height != b.height || a.rgba.empty()) { d.mean = 255; d.max = 255; d.over = 1; return d; }
    u64 sum = 0, over = 0;
    const usize n = static_cast<usize>(a.width) * a.height;
    for (usize i = 0; i < n; ++i) {
        u32 px = 0;
        for (u32 c = 0; c < 3; ++c) {
            const u32 v = static_cast<u32>(std::abs(static_cast<int>(a.rgba[i * 4 + c]) - static_cast<int>(b.rgba[i * 4 + c])));
            sum += v; px = std::max(px, v);
        }
        d.max = std::max(d.max, px);
        over += px > 32 ? 1u : 0u;
    }
    d.mean = static_cast<f64>(sum) / static_cast<f64>(n * 3);
    d.over = static_cast<f64>(over) / static_cast<f64>(n);
    return d;
}

} // namespace camreg

AUREA_TEST(CameraRegressionGpu, DumpScenarios) {
    AUREA_REQUIRE_GPU();
    using namespace camreg;
    const std::string dir = env_dir("AUREA_CAMREG_DIR");
    if (dir.empty()) return;   // só sob demanda (investigação entre builds)
    const std::string golden = env_dir("AUREA_CAMREG_GOLDEN");
    const std::string save = env_dir("AUREA_CAMREG_SAVE");
    const std::string load = env_dir("AUREA_CAMREG_LOAD");
    for (const Scenario& s : scenarios()) {
        {
            Rig r;
            AUREA_CHECK(r.e.new_project(kW, kH, 30.0, nullptr).ok());
            s.build(r);
            for (const i64 f : s.frames) {
                const Image8 img = r.capture(f);
                AUREA_CHECK(write_png(dir + "/" + frame_name(s, f), img));
                if (!golden.empty() && s.golden) AUREA_CHECK(write_png(golden + "/" + frame_name(s, f), half(img)));
            }
            if (!save.empty()) AUREA_CHECK(r.e.save_project((save + "/" + s.name + ".aurea").c_str()).ok());
        }
        if (!load.empty()) {
            Rig r;
            const Status st = r.e.load_project((load + "/" + s.name + ".aurea").c_str());
            AUREA_CHECK(st.ok());
            if (!st.ok()) continue;
            for (const i64 f : s.frames) AUREA_CHECK(write_png(dir + "/" + frame_name(s, f, "_loaded"), r.capture(f)));
        }
    }
}

// User video VN20261009_124648: opening XYZ controls must be distinguished
// from opting a flat media layer into the scene. A real 3D plane needs no
// compensating tilt when the camera is parented directly to a fresh null.
AUREA_TEST(CameraRegressionGpu, FlatMediaRespondsToNullCameraOnlyWhenIn3D) {
    AUREA_REQUIRE_GPU();
    using namespace camreg;
    for (bool video : {false, true}) {
        Rig r;
        AUREA_CHECK(r.e.new_project(kW, kH, 30.0, nullptr).ok());
        const u64 media = video ? add_video(r) : add_image(r);
        Layer* plane = r.layer(media);
        AUREA_CHECK(plane != nullptr);
        if (!plane) continue;
        plane->transform.rotation = Vec3{0, 0, 0};
        plane->transform.position.z = 0;
        plane->threeD = false;
        const auto camera = r.e.add_camera();
        const auto null = r.e.add_null(true);
        AUREA_CHECK(camera.ok() && null.ok());
        if (!camera.ok() || !null.ok()) continue;
        plane = r.layer(media);
        const Image8 flat = r.capture(0);
        set_parent(r, *camera, *null);
        AUREA_CHECK(diff(flat, r.capture(0)).mean < 0.05);
        r.layer(*null)->transform.rotation.y = 25;
        const Image8 ignored = r.capture(0);
        const Diff bypass = diff(flat, ignored);
        AUREA_CHECK(!wants_layer_3d(*r.comp(), *plane, FrameIndex{0}));
        AUREA_CHECK(bypass.mean < 0.05);

        // This is the workaround seen in the video: any nonzero X/Y tilt
        // routes otherwise-flat media through the scene camera.
        plane->transform.rotation.y = 0.01f;
        const Image8 tilted = r.capture(0);
        AUREA_CHECK(wants_layer_3d(*r.comp(), *plane, FrameIndex{0}));
        AUREA_CHECK(diff(flat, tilted).mean > 2);

        // Persistent 3D works at exactly zero tilt/depth, including when the
        // camera is attached to a null. The flag survives save/reload.
        plane->transform.rotation.y = 0;
        AUREA_CHECK(r.e.enable_layer_3d(media));
        AUREA_CHECK(r.e.enable_layer_3d(media)); // idempotent: no second undo step
        const Image8 explicit3d = r.capture(0);
        const Diff orbit = diff(flat, explicit3d);
        AUREA_CHECK(wants_layer_3d(*r.comp(), *plane, FrameIndex{0}));
        AUREA_CHECK(orbit.mean > 2);
        const Diff tinyTilt = diff(tilted, explicit3d);
        std::printf("    tiny tilt versus explicit 3D: %.4f\n", tinyTilt.mean);
        AUREA_CHECK(tinyTilt.mean < 1);
        Command history;
        history.type = CommandType::Undo;
        AUREA_CHECK(r.e.apply_command(history).ok());
        AUREA_CHECK(!r.layer(media)->threeD);
        AUREA_CHECK(diff(flat, r.capture(0)).mean < 0.05);
        history.type = CommandType::Redo;
        AUREA_CHECK(r.e.apply_command(history).ok());
        plane = r.layer(media);
        AUREA_CHECK(plane->threeD);
        AUREA_CHECK(plane->transform.rotation.length_sq() == 0);
        AUREA_CHECK(plane->transform.position.z == 0);
        AUREA_CHECK(diff(explicit3d, r.capture(0)).mean < 0.05);
        r.layer(*null)->transform.rotation.y = 0;
        const Image8 rest = r.capture(0);
        AUREA_CHECK(diff(flat, rest).mean < 0.1);
        r.layer(*null)->transform.rotation.y = 25;
        const std::string dir = env_dir("AUREA_CAMREG_DIR");
        const std::string stem = video ? "flat_video_null" : "flat_image_null";
        const std::string path = (dir.empty() ? std::string(".") : dir) + "/" + stem + ".aurea";
        AUREA_CHECK(r.e.save_project(path.c_str()).ok());
        AUREA_CHECK(r.e.load_project(path.c_str()).ok());
        AUREA_CHECK(r.layer(media)->threeD);
        AUREA_CHECK(diff(explicit3d, r.capture(0)).mean < 0.05);
        std::printf("    %s: flat camera bypass %.4f; zero-tilt 3D orbit %.4f\n",
                    video ? "video" : "image", bypass.mean, orbit.mean);
        if (!dir.empty()) {
            AUREA_CHECK(write_png(dir + "/" + stem + "_2d.png", ignored));
            AUREA_CHECK(write_png(dir + "/" + stem + "_3d.png", explicit3d));
        } else std::remove(path.c_str());
    }
}

// O que funcionava no 2138 continua igual: cada cenário marcado contra a
// referência gravada por aquele build (160 px). Tolerância só para a borda
// antialiasada (o pior quadro medido entre 2138 e 0.0.3: média 0,05).
AUREA_TEST(CameraRegressionGpu, ScenariosMatch2138References) {
    AUREA_REQUIRE_GPU();
    using namespace camreg;
    const std::string dir = std::string(AUREA_TEST_DATA_DIR) + "/golden/camera/";
    for (const Scenario& s : scenarios()) {
        if (!s.golden) continue;
        Rig r;
        AUREA_CHECK(r.e.new_project(kW, kH, 30.0, nullptr).ok());
        s.build(r);
        for (const i64 f : s.frames) {
            Image8 ref;
            const bool have = read_png(dir + frame_name(s, f), ref);
            AUREA_CHECK(have);
            if (!have) { std::printf("    sem referencia: %s\n", frame_name(s, f).c_str()); continue; }
            const Diff d = diff(half(r.capture(f)), ref);
            if (d.mean > 0.75 || d.over > 0.005)
                std::printf("    %s: media %.2f, max %u, %.2f%% acima de 32\n", frame_name(s, f).c_str(), d.mean, d.max, d.over * 100.0);
            AUREA_CHECK(d.mean <= 0.75);
            AUREA_CHECK(d.over <= 0.005);
        }
    }
}

// "Aceita luzes" é opção da camada: desligada (padrão e projetos antigos) a
// luz da composição não toca a imagem; ligada, ilumina; grava no projeto.
AUREA_TEST(CameraRegressionGpu, AcceptsLightsIsOptInPerLayer) {
    AUREA_REQUIRE_GPU();
    using namespace camreg;
    Rig r;
    AUREA_CHECK(r.e.new_project(kW, kH, 30.0, nullptr).ok());
    const u64 img = add_image(r);
    Layer* l = r.layer(img);
    l->transform.position.z = 80.0f;   // plano no espaço 3D
    l->transform.scale = Vec3{0.8f, 0.8f, 1.0f};
    const Image8 noLight = r.capture(0);
    const auto light = r.e.add_light(1);
    const auto cam = r.e.add_camera();
    AUREA_CHECK(light.ok() && cam.ok());
    AUREA_CHECK_EQ(r.e.query_layer_accepts_lights(img), 0);
    AUREA_CHECK_EQ(r.e.query_layer_accepts_lights(*light), -1);
    AUREA_CHECK_EQ(r.e.query_layer_accepts_lights(*cam), -1);
    const Image8 off = r.capture(0);
    AUREA_CHECK(r.e.set_layer_accepts_lights(img, true));
    AUREA_CHECK_EQ(r.e.query_layer_accepts_lights(img), 1);
    const Image8 on = r.capture(0);
    const Diff same = diff(noLight, off), lit = diff(off, on);
    std::printf("    sem luz x luz (aceita desligado): media %.3f; aceita ligado: media %.2f\n", same.mean, lit.mean);
    AUREA_CHECK(same.mean < 0.05);
    AUREA_CHECK(lit.mean > 2.0);
    const std::string path = "aurea_teste_aceita_luzes.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    Rig back;
    AUREA_CHECK(back.e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(back.e.query_layer_accepts_lights(img), 1);
    AUREA_CHECK(diff(back.capture(0), on).mean < 0.05);
}
