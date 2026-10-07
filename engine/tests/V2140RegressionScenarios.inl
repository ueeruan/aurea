// =============================================================================
//  Aurea / tests / V2140RegressionScenarios.inl
//
//  Beta 0.0.2/0.0.3 contra a 0.0.1 (build 2140, último público bom): Flare 3D,
//  câmera 3D, partículas, vídeo de fundo, Light Rays em texto e nulo/texto 3D
//  com Z longe (-1296/-2688) e giro em X. O MESMO projeto montado pela API que
//  as duas telas usam e lido por DOIS caminhos:
//    - captura (capture_frame_rgba, o caminho do export/miniatura);
//    - reprodução (PlaybackPlay + render_frame numa superfície de verdade, com
//      a prévia em memória ligada): o quadro guardado pela reprodução é lido
//      de volta pelo passe de saída comum (acerto de cache) e comparado.
//  As referências em data/golden/v2140/ foram geradas pelo build 2140 e
//  reduzidas a 160 px.
//
//  Variáveis de ambiente (só para investigação entre builds):
//    AUREA_V2140_DIR     grava os quadros de cada cenário (320 px)
//    AUREA_V2140_GOLDEN  grava as referências reduzidas (160 px)
//    AUREA_V2140_SAVE    grava o projeto de cada cenário (.aurea)
//    AUREA_V2140_LOAD    reabre os projetos gravados por outro build e captura
//    AUREA_V2140_PLAY    também lê os quadros do caminho da tela (tocar,
//                        parado com preparo ocioso, ir e voltar na régua)
//    AUREA_V2140_ONLY    só os cenários cujo nome contém este texto
//    AUREA_V2140_LIT_ALL simula a 0.0.2/0.0.3 (toda camada recebe as luzes)
// =============================================================================
#include "aurea/scene3d/Text3D.hpp"
#if defined(_WIN32)
#include <windows.h>
#endif

namespace v2140reg {

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

/// Janela Win32 escondida: a superfície de verdade do caminho de reprodução
/// (render_frame só compõe e guarda a prévia com superfície ligada).
struct HiddenWindow {
    void* hwnd = nullptr;
    HiddenWindow(u32 w, u32 h) {
#if defined(_WIN32)
        hwnd = CreateWindowExW(0, L"STATIC", L"aurea-v2140", WS_POPUP, 0, 0, static_cast<int>(w), static_cast<int>(h),
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
    }
    ~HiddenWindow() {
#if defined(_WIN32)
        if (hwnd) DestroyWindow(static_cast<HWND>(hwnd));
#endif
    }
};

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
    u32 w = kW, h = kH;   ///< tamanho da composição do cenário
    Image8 capture(i64 frame) {
        seek_frame(e, frame);
        Image8 img;
        // Duas capturas: o vídeo sintético decodifica no primeiro pedido.
        for (int i = 0; i < 2; ++i) {
            std::vector<u8> rgba;
            u32 cw = 0, ch = 0;
            AUREA_CHECK(e.capture_frame_rgba(std::max(w, h), rgba, cw, ch).ok());
            img.width = cw; img.height = ch; img.rgba = std::move(rgba);
        }
        return img;
    }
};

/// O quadro que a REPRODUÇÃO guardou na prévia em memória, pelo passe de
/// saída comum (o mesmo da tela) num alvo legível. Falso se não está guardado.
bool cached_frame(Rig& r, i64 f, Image8& out) {
    Renderer& ren = r.e.renderer();
    GPUBackend* g = r.e.gpu();
    if (!g || !ren.preview_cached(FrameIndex{f})) return false;
    const u32 kW = r.w, kH = r.h;
    TextureDesc cd;
    cd.width = kW; cd.height = kH; cd.format = SurfaceFormat::RGBA16F;
    cd.renderTarget = true; cd.transferSrc = true; cd.sampled = true;
    TextureDesc dd = cd;
    dd.format = SurfaceFormat::RGBA8;
    auto comp = g->create_texture(cd);
    auto disp = g->create_texture(dd);
    if (!comp.ok() || !disp.ok()) return false;
    FrameSnapshot snap;
    snap.compWidth = kW; snap.compHeight = kH; snap.time = FrameIndex{f};
    snap.background = Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    RenderSettings s;
    s.previewCacheRevision = 1;   // render() procura o quadro só pelo instante
    s.dither = false;
    OffscreenTarget off;
    off.texture = *comp; off.width = kW; off.height = kH;
    off.display = *disp; off.displayWidth = kW; off.displayHeight = kH;
    FrameStats st; RenderTimings tm;
    const bool rendered = ren.render(snap, s, &off, st, tm).ok() && ren.last_preview_cache_hit();
    g->wait_idle();
    bool ok = false;
    if (rendered) {
        out.width = kW; out.height = kH;
        out.rgba.assign(static_cast<usize>(kW) * kH * 4, 0);
        ok = g->read_texture(*disp, out.rgba.data(), kW * 4).ok();
        for (usize i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    }
    g->destroy_texture(*comp);
    g->destroy_texture(*disp);
    return ok;
}

/// Caminhos da tela (render_frame com superfície, prévia em memória ligada):
///   Play  — PlaybackPlay de 0 até o último quadro pedido;
///   Idle  — parado em 0 e depois em 30: o preparo ocioso enche a prévia;
///   Scrub — vai e volta (60 → 10 → 45 → 20) e toca até o fim.
/// Cada passe começa com a prévia vazia e lê cada quadro pedido assim que a
/// tela o guarda. Devolve quantos leu.
enum class Pass { Play, Idle, Scrub };
const char* pass_tag(Pass p) { return p == Pass::Play ? "_play" : p == Pass::Idle ? "_idle" : "_scrub"; }

u32 drive(Rig& r, Pass pass, const std::vector<i64>& frames, std::vector<Image8>& out, std::vector<bool>& have) {
    out.assign(frames.size(), Image8{});
    have.assign(frames.size(), false);
    HiddenWindow window(r.w, r.h);
    if (!window.hwnd || !r.e.attach_surface(window.hwnd, r.w, r.h).ok()) {
        std::printf("    (sem superficie: caminho da tela pulado)\n");
        return 0;
    }
    r.e.renderer().clear_preview_cache();
    u32 read = 0;
    auto harvest = [&] {
        for (usize i = 0; i < frames.size(); ++i)
            if (!have[i] && cached_frame(r, frames[i], out[i])) { have[i] = true; ++read; }
    };
    auto pump = [&](f64 seconds) {
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count() < seconds) {
            (void)r.e.render_frame(true);
            harvest();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    auto play_to_end = [&](i64 from) {
        seek_frame(r.e, from);
        (void)r.e.render_frame(false);
        Command play; play.type = CommandType::PlaybackPlay;
        AUREA_CHECK(r.e.apply_command(play).ok());
        const auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            (void)r.e.render_frame(true);
            harvest();
            const i64 now = r.e.project()->timeline().playhead().value;
            const f64 secs = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
            if (read == frames.size() || now >= frames.back() || secs > 12.0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Command pause; pause.type = CommandType::PlaybackPause;
        (void)r.e.apply_command(pause);
    };
    if (pass == Pass::Play) {
        play_to_end(0);
    } else if (pass == Pass::Idle) {
        seek_frame(r.e, 0);
        pump(2.5);
        seek_frame(r.e, 30);
        pump(2.5);
    } else {
        for (const i64 f : {60, 10, 45, 20}) { seek_frame(r.e, f); pump(0.25); }
        play_to_end(20);
    }
    harvest();
    r.e.detach_surface();
    return read;
}

std::string pattern_path() {
    std::string dir = env_dir("AUREA_V2140_DIR");
    const std::string path = (dir.empty() ? std::string(".") : dir) + "/v2140_padrao.png";
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

u64 add_text(Rig& r, const char* s) {
    const auto t = r.e.add_text(s);
    AUREA_CHECK(t.ok());
    return t.ok() ? *t : 0;
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

EffectInstance* add_effect(Rig& r, u64 layer, const char* fx) {
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(layer);
    add.effect_add.effectType = effect_type_id(fx);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(r.e.apply_command(add).ok());
    Layer* l = r.layer(layer);
    return l && !l->effects.empty() ? &l->effects.back() : nullptr;
}

/// Light Rays como no teste do texto (LightRaysTextGpu).
void rays(Rig& r, u64 layer) {
    EffectInstance* fx = add_effect(r, layer, effect_keys::kRays);
    AUREA_CHECK(fx != nullptr);
    if (!fx || fx->params.size() < 6) return;
    fx->params[0].constant = ParamValue::scalar(3);
    fx->params[1].constant = ParamValue::scalar(85);
    fx->params[2].constant = ParamValue::scalar(10);
    fx->params[3].constant = ParamValue::scalar(10);
    fx->params[5].constant = ParamValue::scalar(64);
}

/// Câmera com dolly + órbita: X e Z andam, gira em Y.
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

/// Vídeo de fundo 2D na tela inteira (o "fundo" do relato).
u64 background_video(Rig& r) {
    const u64 v = add_video(r);
    Layer* l = r.layer(v);
    if (l) {
        l->transform.position = Vec3{kW * 0.5f, kH * 0.5f, 0.0f};
        l->transform.scale = Vec3{l->transform.scale.x * 1.2f, l->transform.scale.y * 1.2f, 1.0f};
    }
    return v;
}

/// Imagem, vídeo e texto no espaço 3D (Z/rotação como o painel grava).
std::vector<u64> media_3d(Rig& r) {
    const u64 img = add_image(r), vid = add_video(r), txt = add_text(r, "AUREA");
    const std::vector<u64> ids{img, vid, txt};
    const Vec2 places[3]{{90, 70}, {230, 70}, {160, 135}};
    for (u32 i = 0; i < 3; ++i) {
        Layer* l = r.layer(ids[i]);
        if (!l) continue;
        l->transform.position.x = places[i].x;
        l->transform.position.y = places[i].y;
        if (i == 0) l->transform.scale = Vec3{0.6f, 0.6f, 1.0f};
        if (i == 1) l->transform.scale = Vec3{l->transform.scale.x * 0.35f, l->transform.scale.y * 0.35f, 1.0f};
        l->transform.position.z = i == 0 ? 80.0f : i == 1 ? -40.0f : 20.0f;
        if (i == 1) l->transform.rotation.y = 25.0f;
        if (i == 2) l->transform.rotation.x = 15.0f;
    }
    return ids;
}

/// Flare 3D (a luz com o Flare da cena, como o "+ > Flare 3D" das telas)
/// andando pela cena.
u64 flare(Rig& r) {
    const auto f = r.e.add_light(3);
    AUREA_CHECK(f.ok());
    if (!f.ok()) return 0;
    const Layer* l = r.layer(*f);
    const i64 s = l ? l->start.value : 0;
    key(r, *f, TrackProperty::PositionX, 0 - s, 60.0f);
    key(r, *f, TrackProperty::PositionX, 90 - s, 260.0f);
    return *f;
}

u64 particles(Rig& r) {
    const auto p = r.e.add_particles(particular::kPresetBase);
    AUREA_CHECK(p.ok());
    return p.ok() ? *p : 0;
}

struct Scenario {
    const char* name;
    bool golden;                       ///< o 2140 é o comportamento esperado
    std::vector<i64> frames;
    void (*build)(Rig&);
    u32 w = kW, h = kH;
};

// --- F: Flare 3D + câmera + partículas + fundo (relato da 0.0.2/0.0.3).
void s_flare_world(Rig& r) {
    background_video(r);
    media_3d(r);
    animated_camera(r);
    flare(r);
    particles(r);
}
void s_flare_noparticles(Rig& r) {
    background_video(r);
    media_3d(r);
    animated_camera(r);
    flare(r);
}
void s_flare_group_pass(Rig& r) {
    background_video(r);
    const std::vector<u64> ids = media_3d(r);
    const auto g = r.e.precompose(ids.data(), static_cast<u32>(ids.size()), "grupo3d");
    AUREA_CHECK(g.ok());
    if (g.ok()) AUREA_CHECK(r.e.set_group_camera_pass_through(*g, true));
    animated_camera(r);
    flare(r);
    particles(r);
}
void s_flare_all_precomp(Rig& r) {
    std::vector<u64> ids;
    ids.push_back(background_video(r));
    for (const u64 id : media_3d(r)) ids.push_back(id);
    ids.push_back(animated_camera(r));
    ids.push_back(flare(r));
    ids.push_back(particles(r));
    const auto g = r.e.precompose(ids.data(), static_cast<u32>(ids.size()), "tudo");
    AUREA_CHECK(g.ok());
}
void s_flare_text3d(Rig& r) {
    background_video(r);
    scene3d::Text3DSpec spec;
    spec.content = "AUREA";
    const auto t = r.e.add_text3d(spec);
    AUREA_CHECK(t.ok());
    animated_camera(r);
    flare(r);
    particles(r);
}

// --- R: o projeto do testador (9:16) que funcionava na 0.0.1 e quebrou na
// 0.0.2: câmera presa a uma corrente de QUATRO nulos 3D animados (cada um
// com keyframes em instantes diferentes), Flare 3D, Particular, texto com
// keyframes e vídeo de fundo.
void s_rig_null_chain(Rig& r) {
    const u64 bg = background_video(r);
    if (Layer* l = r.layer(bg)) {
        l->transform.position = Vec3{r.w * 0.5f, r.h * 0.5f, 0.0f};
        l->transform.scale = Vec3{3.4f, 3.4f, 1.0f};
    }
    const u64 txt = add_text(r, "AUREA");
    key(r, txt, TrackProperty::PositionY, 0, r.h * 0.40f);
    key(r, txt, TrackProperty::PositionY, 60, r.h * 0.60f);
    key(r, txt, TrackProperty::Opacity, 0, 0.3f);
    key(r, txt, TrackProperty::Opacity, 30, 1.0f);
    particles(r);
    (void)flare(r);
    const auto cam = r.e.add_camera();
    AUREA_CHECK(cam.ok());
    u64 nulls[4]{};
    for (u32 i = 0; i < 4; ++i) {
        const auto n = r.e.add_null(true);
        AUREA_CHECK(n.ok());
        nulls[i] = n.ok() ? *n : 0;
    }
    // Cada nulo com keyframes em instantes diferentes (losangos espalhados).
    key(r, nulls[0], TrackProperty::RotationY, 0, 0.0f);
    key(r, nulls[0], TrackProperty::RotationY, 89, 25.0f);
    key(r, nulls[1], TrackProperty::PositionX, 10, r.w * 0.5f);
    key(r, nulls[1], TrackProperty::PositionX, 70, r.w * 0.5f + 40.0f);
    key(r, nulls[2], TrackProperty::PositionZ, 20, 0.0f);
    key(r, nulls[2], TrackProperty::PositionZ, 80, -60.0f);
    key(r, nulls[3], TrackProperty::RotationX, 5, 0.0f);
    key(r, nulls[3], TrackProperty::RotationX, 50, -10.0f);
    key(r, nulls[3], TrackProperty::RotationZ, 30, 0.0f);
    key(r, nulls[3], TrackProperty::RotationZ, 89, 8.0f);
    // "Nulo 3D cópia cópia" → "Nulo 3D cópia" → "Nulo 3D" → "Nulo 3D"; a
    // câmera presa ao último da corrente.
    set_parent(r, nulls[3], nulls[2]);
    set_parent(r, nulls[2], nulls[1]);
    set_parent(r, nulls[1], nulls[0]);
    if (cam.ok()) set_parent(r, *cam, nulls[3]);
}

// --- G: Light Rays em texto.
void s_rays_text2d(Rig& r) { rays(r, add_text(r, "LIGHT")); }
void s_rays_text3d(Rig& r) {
    const u64 t = add_text(r, "LIGHT");
    Layer* l = r.layer(t);
    if (l) { l->transform.position.z = 40.0f; l->transform.rotation.y = 20.0f; }
    rays(r, t);
    animated_camera(r);
}
void s_rays_text_in_precomp(Rig& r) {
    const u64 t = add_text(r, "LIGHT");
    rays(r, t);
    const auto g = r.e.precompose(&t, 1, "texto");
    AUREA_CHECK(g.ok());
}
void s_rays_on_precomp(Rig& r) {
    const u64 t = add_text(r, "LIGHT");
    const auto g = r.e.precompose(&t, 1, "texto");
    AUREA_CHECK(g.ok());
    if (g.ok()) rays(r, *g);
}
void s_rays_text3d_layer(Rig& r) {
    scene3d::Text3DSpec spec;
    spec.content = "RAYS";
    const auto t = r.e.add_text3d(spec);
    AUREA_CHECK(t.ok());
    if (t.ok()) rays(r, *t);
}

// --- H: nulo 3D / texto com Z longe e giro em X (pivô).
u64 null_with_child(Rig& r) {
    const auto n = r.e.add_null(true);
    AUREA_CHECK(n.ok());
    const u64 img = add_image(r);
    Layer* l = r.layer(img);
    if (l) l->transform.scale = Vec3{0.8f, 0.8f, 1.0f};
    if (n.ok()) set_parent(r, img, *n);
    return n.ok() ? *n : 0;
}
// Os valores do relato (-1296 / -2688 px numa composição 1080p) na escala
// desta composição: a câmera padrão fica a 1,2 × altura (1296 em 1080p), então
// -1296 é o próprio plano da câmera e -2688 fica atrás dela.
constexpr f32 kAt1080 = static_cast<f32>(kH) / 1080.0f;
void z_sweep(Rig& r, u64 id) {
    key(r, id, TrackProperty::PositionZ, 0, 0.0f);
    key(r, id, TrackProperty::PositionZ, 30, -1296.0f * kAt1080);
    key(r, id, TrackProperty::PositionZ, 60, -2688.0f * kAt1080);
}
void rotx_sweep(Rig& r, u64 id, f32 z) {
    key(r, id, TrackProperty::PositionZ, 0, z);
    key(r, id, TrackProperty::RotationX, 0, 0.0f);
    key(r, id, TrackProperty::RotationX, 89, 120.0f);
}
void s_null_z(Rig& r) { z_sweep(r, null_with_child(r)); }
void s_null_rotx(Rig& r) { rotx_sweep(r, null_with_child(r), -1036.8f * kAt1080); }   // 0,8 da câmera
void s_null_rotx_near(Rig& r) { rotx_sweep(r, null_with_child(r), 0.0f); }
void s_null_rotx_cam(Rig& r) { rotx_sweep(r, null_with_child(r), -1036.8f * kAt1080); (void)r.e.add_camera(); }
void s_text_z(Rig& r) { z_sweep(r, add_text(r, "PIVOT")); }
void s_text_rotx(Rig& r) { rotx_sweep(r, add_text(r, "PIVOT"), -1036.8f * kAt1080); }
void s_text_rotx_near(Rig& r) { rotx_sweep(r, add_text(r, "PIVOT"), 0.0f); }

const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> list{
        {"r_rig_null_chain", true, {0, 10, 20, 30, 45, 60, 69, 80, 89}, s_rig_null_chain, 180, 320},
        {"f_flare_world", true, {0, 20, 45, 70, 89}, s_flare_world},
        {"f_flare_noparticles", true, {0, 45, 89}, s_flare_noparticles},
        {"f_flare_group_pass", true, {0, 45, 89}, s_flare_group_pass},
        {"f_flare_all_precomp", true, {0, 45, 89}, s_flare_all_precomp},
        {"f_flare_text3d", true, {0, 45, 89}, s_flare_text3d},
        {"g_rays_text2d", true, {0, 30}, s_rays_text2d},
        {"g_rays_text3d", true, {0, 45, 89}, s_rays_text3d},
        {"g_rays_text_in_precomp", true, {0, 30}, s_rays_text_in_precomp},
        {"g_rays_on_precomp", true, {0, 30}, s_rays_on_precomp},
        // Texto 3D extrudado: o 2140 NÃO desenhava os raios (o modelo 3D saía
        // sem plano de efeitos) — a referência dele é o defeito. Coberto por
        // LightRays3DGpu.ExtrudedText3DCastsBeamsBeyondItsGlyphs.
        {"g_rays_text3d_layer", false, {0, 30}, s_rays_text3d_layer},
        {"h_null_z", true, {0, 15, 25, 28, 30, 45, 60}, s_null_z},
        {"h_null_rotx", true, {0, 15, 30, 45, 60, 75, 89}, s_null_rotx},
        {"h_null_rotx_near", true, {0, 15, 30, 45, 60, 75, 89}, s_null_rotx_near},
        {"h_null_rotx_cam", true, {0, 30, 60, 89}, s_null_rotx_cam},
        {"h_text_z", true, {0, 15, 25, 28, 30, 45, 60}, s_text_z},
        {"h_text_rotx", true, {0, 15, 30, 45, 60, 75, 89}, s_text_rotx},
        {"h_text_rotx_near", true, {0, 15, 30, 45, 60, 75, 89}, s_text_rotx_near},
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

} // namespace v2140reg

AUREA_TEST(V2140RegressionGpu, DumpScenarios) {
    AUREA_REQUIRE_GPU();
    using namespace v2140reg;
    const std::string dir = env_dir("AUREA_V2140_DIR");
    if (dir.empty()) return;   // só sob demanda (investigação entre builds)
    const std::string golden = env_dir("AUREA_V2140_GOLDEN");
    const std::string save = env_dir("AUREA_V2140_SAVE");
    const std::string load = env_dir("AUREA_V2140_LOAD");
    const bool screenPath = !env_dir("AUREA_V2140_PLAY").empty();
    const std::string only = env_dir("AUREA_V2140_ONLY");
    for (const Scenario& s : scenarios()) {
        if (!only.empty() && std::string(s.name).find(only) == std::string::npos) continue;
        {
            Rig r;
            r.w = s.w; r.h = s.h;
            AUREA_CHECK(r.e.new_project(s.w, s.h, 30.0, nullptr).ok());
            s.build(r);
            // Investigação: o comportamento da 0.0.2/0.0.3 (toda camada 2D no
            // espaço 3D recebia as luzes, inclusive a do Flare 3D).
            if (!env_dir("AUREA_V2140_LIT_ALL").empty())
                for (u32 i = 0; i < r.comp()->order().size(); ++i)
                    if (Layer* l = r.comp()->layer(r.comp()->order().at(i))) l->acceptsLights = true;
            if (!save.empty()) AUREA_CHECK(r.e.save_project((save + "/" + s.name + ".aurea").c_str()).ok());
            for (const i64 f : s.frames) {
                const Image8 img = r.capture(f);
                AUREA_CHECK(write_png(dir + "/" + frame_name(s, f), img));
                if (!golden.empty() && s.golden) AUREA_CHECK(write_png(golden + "/" + frame_name(s, f), half(img)));
            }
            if (screenPath) for (const Pass pass : {Pass::Play, Pass::Idle, Pass::Scrub}) {
                std::vector<Image8> shown;
                std::vector<bool> have;
                const u32 n = drive(r, pass, s.frames, shown, have);
                std::printf("    %s%s: tela leu %u/%zu quadros\n", s.name, pass_tag(pass), n, s.frames.size());
                for (usize i = 0; i < s.frames.size(); ++i)
                    if (have[i]) AUREA_CHECK(write_png(dir + "/" + frame_name(s, s.frames[i], pass_tag(pass)), shown[i]));
            }
        }
        if (!load.empty()) {
            Rig r;
            r.w = s.w; r.h = s.h;
            const Status st = r.e.load_project((load + "/" + s.name + ".aurea").c_str());
            AUREA_CHECK(st.ok());
            if (!st.ok()) { std::printf("    %s: projeto nao abriu\n", s.name); continue; }
            for (const i64 f : s.frames) AUREA_CHECK(write_png(dir + "/" + frame_name(s, f, "_loaded"), r.capture(f)));
            if (screenPath) {
                std::vector<Image8> shown;
                std::vector<bool> have;
                (void)drive(r, Pass::Play, s.frames, shown, have);
                for (usize i = 0; i < s.frames.size(); ++i)
                    if (have[i]) AUREA_CHECK(write_png(dir + "/" + frame_name(s, s.frames[i], "_loadedplay"), shown[i]));
            }
        }
    }
}

// O que funcionava no 2140 (0.0.1) continua igual: a captura de cada cenário
// contra a referência gravada por aquele build (160 px).
AUREA_TEST(V2140RegressionGpu, ScenariosMatch2140References) {
    AUREA_REQUIRE_GPU();
    using namespace v2140reg;
    const std::string dir = std::string(AUREA_TEST_DATA_DIR) + "/golden/v2140/";
    for (const Scenario& s : scenarios()) {
        if (!s.golden) continue;
        Rig r;
        r.w = s.w; r.h = s.h;
        AUREA_CHECK(r.e.new_project(s.w, s.h, 30.0, nullptr).ok());
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

// Caminho da TELA (render_frame tocando, parado com o preparo ocioso, e indo e
// voltando na régua, com a prévia em memória ligada): o quadro guardado é o
// mesmo da referência do 2140 naquele instante.
AUREA_TEST(V2140RegressionGpu, ScreenFramesMatch2140References) {
    AUREA_REQUIRE_GPU();
    using namespace v2140reg;
    const std::string dir = std::string(AUREA_TEST_DATA_DIR) + "/golden/v2140/";
    u32 compared = 0;
    for (const Scenario& s : scenarios()) {
        if (!s.golden || (s.name[0] != 'f' && s.name[0] != 'r')) continue;   // câmera + Flare 3D + partículas
        Rig r;
        r.w = s.w; r.h = s.h;
        AUREA_CHECK(r.e.new_project(s.w, s.h, 30.0, nullptr).ok());
        s.build(r);
        (void)r.capture(0);   // decoders abertos, como no editor
        for (const Pass pass : {Pass::Play, Pass::Idle, Pass::Scrub}) {
            if (pass != Pass::Play && s.name[0] != 'r') continue;
            std::vector<Image8> shown;
            std::vector<bool> have;
            (void)drive(r, pass, s.frames, shown, have);
            for (usize i = 0; i < s.frames.size(); ++i) {
                if (!have[i]) continue;
                Image8 ref;
                if (!read_png(dir + frame_name(s, s.frames[i]), ref)) continue;
                const Diff d = diff(half(shown[i]), ref);
                ++compared;
                if (d.mean > 1.5 || d.over > 0.01)
                    std::printf("    %s (%s): media %.2f, max %u, %.2f%% acima de 32\n",
                                frame_name(s, s.frames[i]).c_str(), pass_tag(pass) + 1, d.mean, d.max, d.over * 100.0);
                AUREA_CHECK(d.mean <= 1.5);
                AUREA_CHECK(d.over <= 0.01);
            }
        }
    }
    std::printf("    quadros da tela comparados: %u\n", compared);
}
