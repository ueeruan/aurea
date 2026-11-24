// =============================================================================
//  Aurea / tests / FaceRigGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  Rig 2D de rosto (aurea.rig.face): render na GPU (pose neutra = a imagem;
//  olho fechado vira pele; boca aberta escurece), o motor acha os pontos ao
//  pôr o efeito, arrasta alça com desfazer, e salvar/reabrir.
// =============================================================================

#include "aurea/effects/FaceRig.hpp"
#include "FaceRigFixture.hpp"

namespace {

void face_rig_write(EffectInstance& e, const face_rig::Anchors& a) {
    auto put = [&](u32 p, Vec2 v) { e.params[p].constant.v[0] = v.x; e.params[p].constant.v[1] = v.y; };
    put(face_rig::kFaceCenter, a.face);
    put(face_rig::kFaceSize, a.faceR);
    put(face_rig::kEyeLeft, a.eye[0]);
    put(face_rig::kEyeLeftSize, a.eyeR[0]);
    put(face_rig::kEyeRight, a.eye[1]);
    put(face_rig::kEyeRightSize, a.eyeR[1]);
    put(face_rig::kMouth, a.mouth);
    put(face_rig::kMouthSize, a.mouthR);
    put(face_rig::kNeck, a.neck);
    put(face_rig::kHairTop, a.hairTop);
}

/// Luminância média (linear) num retângulo da imagem lida.
f32 face_rig_mean(const FloatImage& img, Vec2 c, f32 rx, f32 ry) {
    f32 sum = 0.0f;
    u32 n = 0;
    for (u32 y = static_cast<u32>(std::max(0.0f, c.y - ry)); y < static_cast<u32>(std::min<f32>(img.height, c.y + ry)); ++y)
        for (u32 x = static_cast<u32>(std::max(0.0f, c.x - rx)); x < static_cast<u32>(std::min<f32>(img.width, c.x + rx)); ++x, ++n) {
            const Vec4 v = img.v(x, y);
            sum += 0.299f * v.x + 0.587f * v.y + 0.114f * v.z;
        }
    return n ? sum / static_cast<f32>(n) : 0.0f;
}

} // namespace

AUREA_TEST(Gpu, FaceRigRestIsTheImageAndClosingTheEyeHidesIt) {
    AUREA_REQUIRE_GPU();
    const u32 W = 360, H = 440;
    face_rig_fixture::Truth t;
    std::vector<u8> rgba = face_rig_fixture::anime_face(W, H, t);
    ImagePixels px;
    px.width = W;
    px.height = H;
    px.rgba = rgba;
    Scene base(W, H);
    base.image(px, W * 0.5f, H * 0.5f);
    const FloatImage plain = base.render();

    Scene s(W, H);
    const LayerId id = s.image(px, W * 0.5f, H * 0.5f);
    EffectInstance& e = s.add_effect(id, face_rig::kFaceRigKey);
    AUREA_CHECK(e.params.size() >= face_rig::kParamCount);
    face_rig::Anchors a;
    AUREA_CHECK_EQ(face_rig::auto_place(rgba.data(), W, H, a), 2u);
    face_rig_write(e, a);
    // Respiração ligada no quadro 0: o passe roda, mas nada sai do lugar.
    e.params[face_rig::kBreathing].constant.v[0] = 1.0f;
    const FloatImage rest = s.render(FrameIndex{0});
    f32 diff = 0.0f;
    for (usize i = 0; i < rest.px.size(); ++i) diff = std::max(diff, std::fabs(rest.px[i] - plain.px[i]));
    std::printf("    repouso vs imagem: %.5f ", diff);
    AUREA_CHECK(diff < 2e-3f);

    // Olho esquerdo fechado: o miolo escuro some (vira pele), o direito fica.
    Layer* l = s.comp->layer(id);
    EffectInstance& ef = l->effects.back();
    ef.params[face_rig::kBreathing].constant.v[0] = 0.0f;
    ef.params[face_rig::kEyeOpenLeft].constant.v[0] = 0.0f;
    const FloatImage closed = s.render();
    const f32 before = face_rig_mean(plain, t.eye[0], 0.6f * t.eyeR.x, 0.5f * t.eyeR.y);
    const f32 after = face_rig_mean(closed, t.eye[0], 0.6f * t.eyeR.x, 0.5f * t.eyeR.y);
    const f32 skin = face_rig_mean(plain, Vec2{t.face.x, t.eye[0].y + 1.5f * t.eyeR.y}, 6.0f, 4.0f);
    const f32 other = std::fabs(face_rig_mean(closed, t.eye[1], 0.6f * t.eyeR.x, 0.5f * t.eyeR.y)
                              - face_rig_mean(plain, t.eye[1], 0.6f * t.eyeR.x, 0.5f * t.eyeR.y));
    std::printf("    olho: %.3f -> %.3f (pele %.3f), outro olho muda %.4f ", before, after, skin, other);
    AUREA_CHECK(after > before + 0.8f * (skin - before));
    AUREA_CHECK(other < 0.01f);

    // Boca aberta: abaixo da linha da boca escurece (o miolo).
    ef.params[face_rig::kEyeOpenLeft].constant.v[0] = 1.0f;
    ef.params[face_rig::kMouthOpen].constant.v[0] = 1.0f;
    const FloatImage open = s.render();
    const Vec2 below{t.mouth.x, t.mouth.y + 10.0f};
    const f32 m0 = face_rig_mean(plain, below, 6.0f, 3.0f), m1 = face_rig_mean(open, below, 6.0f, 3.0f);
    std::printf("    boca: %.3f -> %.3f ", m0, m1);
    AUREA_CHECK(m1 < 0.6f * m0);

    // Salvar e reabrir: âncoras e parâmetros voltam iguais.
    const std::string path = "tests/build/prompt03/aurea_teste_rig_rosto.aurea";
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(s.project, path, SaveOptions{}, &error).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    const Layer* ll = lc ? lc->layer(id) : nullptr;
    AUREA_CHECK(ll && !ll->effects.empty());
    if (ll && !ll->effects.empty()) {
        const EffectInstance& le = ll->effects.back();
        AUREA_CHECK(le.type == effect_type_id(face_rig::kFaceRigKey));
        AUREA_CHECK(le.params.size() >= face_rig::kParamCount);
        if (le.params.size() >= face_rig::kParamCount)
            for (u32 p = 0; p < face_rig::kParamCount; ++p)
                for (u32 c = 0; c < 2; ++c) AUREA_CHECK_EQ(le.params[p].constant.v[c], ef.params[p].constant.v[c]);
    }
    std::remove(path.c_str());
}

AUREA_TEST(FaceRig, EnginePlacesOnAddMovesHandlesAndUndoes) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(360, 440, 30.0, nullptr).ok());
    face_rig_fixture::Truth t;
    std::vector<u8> rgba = face_rig_fixture::anime_face(360, 440, t);
    auto img = e.import_image(rgba.data(), 360, 440, "rosto");
    AUREA_CHECK(img.ok());
    if (!img.ok()) { e.shutdown(); return; }
    const u64 layer = *img;
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add = EffectAddPayload{LayerId::unpack(layer), effect_type_id(face_rig::kFaceRigKey), kInvalidIndex};
    AUREA_CHECK(e.apply_command(add).ok());
    std::vector<bridge::LayerEffectRow> rows(4);
    std::vector<char> blob(4096);
    const u32 n = e.query_layer_effects(layer, rows.data(), 4, blob.data(), static_cast<u32>(blob.size()));
    AUREA_CHECK(n >= 1u);
    if (n < 1u) { e.shutdown(); return; }
    const u32 fx = rows[n - 1].effectId;
    f32 q[Engine::kFaceRigQueryFloats] = {};
    AUREA_CHECK_EQ(e.query_face_rig(layer, fx, nullptr, 0), Engine::kFaceRigQueryFloats);
    AUREA_CHECK_EQ(e.query_face_rig(layer, fx, q, Engine::kFaceRigQueryFloats), Engine::kFaceRigQueryFloats);
    // Pôr o efeito já achou os pontos (no mesmo passo de desfazer).
    AUREA_CHECK_EQ(q[Engine::kFaceRigHandles * 2], 2.0f);
    const Vec2 eye{q[face_rig::kHandleEyeLeft * 2] * 360.0f, q[face_rig::kHandleEyeLeft * 2 + 1] * 440.0f};
    AUREA_CHECK((eye - t.eye[0]).length() < 8.0f);
    // Arrastar o canto do olho muda o tamanho; o gesto é um passo de desfazer.
    const f32 cu = q[face_rig::kHandleEyeLeft * 2], cv = q[face_rig::kHandleEyeLeft * 2 + 1];
    AUREA_CHECK(e.face_rig_move_handle(layer, fx, face_rig::kHandleEyeLeftEdge, cu + 0.05f, cv + 0.04f, false));
    AUREA_CHECK(e.face_rig_move_handle(layer, fx, face_rig::kHandleEyeLeftEdge, cu + 0.10f, cv + 0.08f, true));
    f32 moved[Engine::kFaceRigQueryFloats] = {};
    (void)e.query_face_rig(layer, fx, moved, Engine::kFaceRigQueryFloats);
    AUREA_CHECK_NEAR(moved[face_rig::kHandleEyeLeftEdge * 2], cu + 0.10f, 1e-5f);
    AUREA_CHECK_EQ(moved[Engine::kFaceRigHandles * 2], 3.0f);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    f32 back[Engine::kFaceRigQueryFloats] = {};
    (void)e.query_face_rig(layer, fx, back, Engine::kFaceRigQueryFloats);
    for (u32 i = 0; i < Engine::kFaceRigQueryFloats; ++i) AUREA_CHECK_EQ(back[i], q[i]);
    // Detectar de novo e pôr todas as alças de uma vez.
    AUREA_CHECK_EQ(e.face_rig_auto_place(layer, fx), 2);
    f32 all[Engine::kFaceRigHandles * 2];
    for (u32 i = 0; i < Engine::kFaceRigHandles * 2; ++i) all[i] = q[i];
    all[face_rig::kHandleMouth * 2 + 1] += 0.02f;
    AUREA_CHECK(e.face_rig_set_handles(layer, fx, all, Engine::kFaceRigHandles * 2));
    (void)e.query_face_rig(layer, fx, moved, Engine::kFaceRigQueryFloats);
    AUREA_CHECK_NEAR(moved[face_rig::kHandleMouth * 2 + 1], q[face_rig::kHandleMouth * 2 + 1] + 0.02f, 1e-5f);
    AUREA_CHECK(!e.face_rig_move_handle(layer, fx, face_rig::kHandleCount, 0.5f, 0.5f, false));
    AUREA_CHECK_EQ(e.face_rig_auto_place(layer, fx + 99), -1);
    e.shutdown();
}

// Vitrine: roda o rig numa imagem de verdade (AUREA_FACERIG_IMAGE = RGBA cru,
// AUREA_FACERIG_SIZE = "LxA") e grava as poses e uma volta animada em
// AUREA_FX_DUMP. Sem as variáveis, não faz nada.
AUREA_TEST(Gpu, FaceRigShowcaseFromFile) {
    const char* file = std::getenv("AUREA_FACERIG_IMAGE");
    const char* size = std::getenv("AUREA_FACERIG_SIZE");
    const char* dir = std::getenv("AUREA_FX_DUMP");
    if (!file || !size || !dir || !*file || !*dir) return;
    AUREA_REQUIRE_GPU();
    unsigned W = 0, H = 0;
    if (std::sscanf(size, "%ux%u", &W, &H) != 2 || !W || !H) return;
    std::vector<u8> rgba(static_cast<usize>(W) * H * 4);
    FILE* f = std::fopen(file, "rb");
    AUREA_CHECK(f != nullptr);
    if (!f) return;
    const usize got = std::fread(rgba.data(), 1, rgba.size(), f);
    std::fclose(f);
    AUREA_CHECK_EQ(got, rgba.size());
    ImagePixels px;
    px.width = W;
    px.height = H;
    px.rgba = rgba;
    Scene s(W, H);
    const LayerId id = s.image(px, W * 0.5f, H * 0.5f);
    s.add_effect(id, face_rig::kFaceRigKey);
    face_rig::Anchors a;
    const u32 placed = face_rig::auto_place(rgba.data(), W, H, a);
    std::printf("\n    achou: %u  rosto %.3f,%.3f r %.3f,%.3f  olhoE %.3f,%.3f r %.3f,%.3f  olhoD %.3f,%.3f r %.3f,%.3f"
                "  boca %.3f,%.3f r %.3f,%.3f  pescoco %.3f,%.3f  cabelo %.3f,%.3f\n",
                placed, a.face.x, a.face.y, a.faceR.x, a.faceR.y, a.eye[0].x, a.eye[0].y, a.eyeR[0].x, a.eyeR[0].y,
                a.eye[1].x, a.eye[1].y, a.eyeR[1].x, a.eyeR[1].y, a.mouth.x, a.mouth.y, a.mouthR.x, a.mouthR.y,
                a.neck.x, a.neck.y, a.hairTop.x, a.hairTop.y);
    EffectInstance& e = s.comp->layer(id)->effects.back();
    face_rig_write(e, a);
    auto set = [&](u32 i, f32 v) { e.params[i].constant.v[0] = v; };
    auto neutral = [&] {
        for (u32 i = face_rig::kTurn; i <= face_rig::kBreathing; ++i) set(i, 0.0f);
        set(face_rig::kEyeOpenLeft, 1.0f);
        set(face_rig::kEyeOpenRight, 1.0f);
    };
    auto shot = [&](const char* name) {
        (void)write_png(std::string(dir) + "/" + name + ".png", s.render().encoded());
    };
    neutral(); shot("pose_00_repouso");
    neutral(); set(face_rig::kTurn, 25.0f); shot("pose_01_vira_direita");
    neutral(); set(face_rig::kTurn, -25.0f); shot("pose_02_vira_esquerda");
    neutral(); set(face_rig::kNod, 20.0f); shot("pose_03_acena_baixo");
    neutral(); set(face_rig::kTilt, 15.0f); shot("pose_04_inclina");
    neutral(); set(face_rig::kEyeOpenLeft, 0.0f); set(face_rig::kEyeOpenRight, 0.0f); shot("pose_05_olhos_fechados");
    neutral(); set(face_rig::kMouthOpen, 0.0f); set(face_rig::kSmile, 1.0f); shot("pose_06_sorriso");
    neutral(); set(face_rig::kMouthOpen, 1.0f); shot("pose_07_boca_aberta");
    neutral(); set(face_rig::kBrowLeft, 1.0f); set(face_rig::kBrowRight, 1.0f); shot("pose_08_sobrancelhas");
    // Volta animada (36 quadros): vira, acena, inclina, pisca e fala.
    for (int k = 0; k < 36; ++k) {
        const f32 t = static_cast<f32>(k) / 36.0f * 6.2831853f;
        neutral();
        set(face_rig::kTurn, 20.0f * std::sin(t));
        set(face_rig::kNod, 8.0f * std::sin(2.0f * t));
        set(face_rig::kTilt, 6.0f * std::sin(t + 1.0f));
        const f32 blink = (k >= 14 && k <= 17) ? (k == 15 || k == 16 ? 0.0f : 0.4f) : 1.0f;
        set(face_rig::kEyeOpenLeft, blink);
        set(face_rig::kEyeOpenRight, blink);
        set(face_rig::kMouthOpen, 0.5f + 0.5f * std::sin(3.0f * t));
        set(face_rig::kBodySway, 4.0f * std::sin(t));
        char name[64];
        std::snprintf(name, sizeof(name), "anim_%02d", k);
        shot(name);
    }
}

// =============================================================================
//  Rig 2D v2 — separar em partes (FaceRigParts.hpp, EngineFaceRigParts.cpp)
// =============================================================================

#include "aurea/effects/FaceRigParts.hpp"

namespace {

/// O motor com GPU e um leitor de PNG de verdade (as partes são PNG comprimido).
struct FaceRigPartsEngine {
    Engine e;
    explicit FaceRigPartsEngine(u32 w, u32 h) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ec.imageLoader = [](const char* path, ImagePixels& out, void*) {
            std::FILE* f = std::fopen(path, "rb");
            if (!f) return false;
            std::vector<u8> bytes;
            u8 buf[65536];
            usize n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
            std::fclose(f);
            int w = 0, h = 0, c = 0;
            u8* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
            if (!px) return false;
            out.width = static_cast<u32>(w);
            out.height = static_cast<u32>(h);
            out.rgba.assign(px, px + static_cast<usize>(w) * h * 4);
            std::free(px);
            return true;
        };
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(w, h, 30.0, nullptr).ok());
    }
    ~FaceRigPartsEngine() { e.shutdown(); }
    Image8 capture(u32 maxDim) {
        Image8 img;
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        AUREA_CHECK(e.capture_frame_rgba(maxDim, rgba, w, h).ok());
        img.width = w;
        img.height = h;
        img.rgba = std::move(rgba);
        return img;
    }
    void command(Command c) { AUREA_CHECK(e.apply_command(c).ok()); }
    void seek(i64 frame) {
        Command s;
        s.type = CommandType::PlaybackSeek;
        s.seek.time = tick_at(FrameIndex{frame}, 30.0);
        command(s);
    }
    u32 add_effect(u64 layer, const char* key) {
        Command add;
        add.type = CommandType::EffectAdd;
        add.effect_add = EffectAddPayload{LayerId::unpack(layer), effect_type_id(key), kInvalidIndex};
        command(add);
        std::vector<bridge::LayerEffectRow> rows(8);
        std::vector<char> blob(4096);
        const u32 n = e.query_layer_effects(layer, rows.data(), 8, blob.data(), static_cast<u32>(blob.size()));
        return n ? rows[n - 1].effectId : kInvalidIndex;
    }
    /// Parâmetro do Controle (dentro da pré-composição do rig).
    void set_control(u64 rigLayer, u64 control, u32 param, f32 value) {
        AUREA_CHECK(e.open_precomp(rigLayer));
        Command c;
        c.type = CommandType::EffectSetParam;
        c.effect_param.layer = LayerId::unpack(control);
        c.effect_param.effect = EffectId{0, 0};
        c.effect_param.paramIndex = param;
        c.effect_param.value = value;
        command(c);
        AUREA_CHECK(e.close_precomp());
    }
};

/// Pior e média da diferença (0..1) entre duas capturas.
void face_rig_capture_diff(const Image8& a, const Image8& b, f32& worst, f32& mean) {
    worst = 1.0f;
    mean = 1.0f;
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) return;
    worst = 0.0f;
    f64 sum = 0.0;
    for (usize i = 0; i < a.rgba.size(); ++i) {
        const f32 d = std::fabs(static_cast<f32>(a.rgba[i]) - static_cast<f32>(b.rgba[i])) / 255.0f;
        worst = std::max(worst, d);
        sum += d;
    }
    mean = static_cast<f32>(sum / static_cast<f64>(a.rgba.size()));
}

} // namespace

AUREA_TEST(Gpu, FaceRigPartsRestIsTheImageAnimatesDeterministicallyAndReopens) {
    AUREA_REQUIRE_GPU();
    const u32 W = 360, H = 440;
    FaceRigPartsEngine rig(W, H);
    face_rig_fixture::Truth t;
    std::vector<u8> rgba = face_rig_fixture::anime_face(W, H, t, true);
    auto img = rig.e.import_image(rgba.data(), W, H, "rosto");
    AUREA_CHECK(img.ok());
    if (!img.ok()) return;
    const u64 layer = *img;
    const Image8 base = rig.capture(H);
    const u32 fx = rig.add_effect(layer, face_rig::kFaceRigKey);
    AUREA_CHECK(fx != kInvalidIndex);
    u64 control = 0;
    const auto split = rig.e.face_rig_split_parts(layer, fx, &control);
    AUREA_CHECK(split.ok());
    if (!split.ok()) return;
    const u64 rigLayer = *split;
    AUREA_CHECK(control != 0);
    AUREA_CHECK_EQ(rig.e.face_rig_split_progress(), 1.0f);
    // Um passo de desfazer: a pré-composição sai e a original volta a aparecer; refazer volta.
    {
        Command u;
        u.type = CommandType::Undo;
        rig.command(u);
        const Composition* root = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        AUREA_CHECK(root->layer(LayerId::unpack(rigLayer)) == nullptr);
        const Layer* original = root->layer(LayerId::unpack(layer));
        AUREA_CHECK(original && original->visible);
        Command r;
        r.type = CommandType::Redo;
        rig.command(r);
        root = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        AUREA_CHECK(root->layer(LayerId::unpack(rigLayer)) != nullptr);
    }

    // A estrutura: a pré-composição em cima da original (oculta), o Controle e as partes.
    {
        const Composition* root = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        const Layer* original = root->layer(LayerId::unpack(layer));
        const Layer* rl = root->layer(LayerId::unpack(rigLayer));
        AUREA_CHECK(original && !original->visible);
        AUREA_CHECK(rl && rl->kind == LayerKind::Composition);
        AUREA_CHECK_EQ(root->z_index_of(LayerId::unpack(rigLayer)), root->z_index_of(LayerId::unpack(layer)) + 1);
        const Composition* inner = rl ? rig.e.project()->timeline().composition(rl->nested.composition) : nullptr;
        AUREA_CHECK(inner != nullptr);
        if (inner) {
            const Layer* nl = inner->layer(LayerId::unpack(control));
            AUREA_CHECK(nl && nl->kind == LayerKind::Null && !nl->effects.empty());
            if (nl && !nl->effects.empty()) AUREA_CHECK_EQ(nl->effects[0].params[face_rig::kPartsRig].constant.v[0], 1.0f);
            u32 parts = 0, glasses = 0;
            for (u32 i = 0; i < inner->order().size(); ++i) {
                const Layer* pl = inner->layer(inner->order().at(i));
                if (!pl || pl->kind != LayerKind::Image) continue;
                ++parts;
                AUREA_CHECK(pl->parent == LayerId::unpack(control));
                AUREA_CHECK(!pl->effects.empty() && pl->effects[0].type == effect_type_id(face_rig::kFaceRigPartKey));
                if (!pl->effects.empty() && pl->effects[0].params[face_rig::kPartParamRole].constant.v[0] == face_rig::kPartGlasses) ++glasses;
                if (!pl->effects.empty()) AUREA_CHECK_EQ(pl->effects[0].params[face_rig::kPartParamController].constant.ref, control);
            }
            std::printf("\n    %u partes ", parts);
            AUREA_CHECK(parts >= 10u);
            AUREA_CHECK_EQ(glasses, 1u);
        }
    }
    // Repouso: as partes compostas são a imagem.
    const Image8 rest = rig.capture(H);
    f32 worst = 0, mean = 0;
    face_rig_capture_diff(base, rest, worst, mean);
    std::printf("repouso vs imagem: pior %.4f média %.5f ", worst, mean);
    AUREA_CHECK(worst < 0.04f && mean < 0.002f);

    // Piscar: os óculos ficam (cada pixel opaco deles igual), o olho some.
    face_rig::Anchors a;
    (void)face_rig::auto_place(rgba.data(), W, H, a);
    face_rig::PartSet set;
    AUREA_CHECK(face_rig::split_parts(rgba.data(), W, H, a, set));
    rig.set_control(rigLayer, control, face_rig::kEyeOpenLeft, 0.0f);
    rig.set_control(rigLayer, control, face_rig::kEyeOpenRight, 0.0f);
    const Image8 blink = rig.capture(H);
    f32 glassesWorst = 0.0f;
    u32 glassesPx = 0;
    for (const face_rig::Part& p : set.parts) {
        if (p.role != face_rig::kPartGlasses) continue;
        for (u32 y = 0; y < p.h; ++y)
            for (u32 x = 0; x < p.w; ++x) {
                if (p.rgba[(static_cast<usize>(y) * p.w + x) * 4 + 3] < 255) continue;
                const u8* r0 = rest.at(p.x0 + x, p.y0 + y);
                const u8* r1 = blink.at(p.x0 + x, p.y0 + y);
                for (u32 k = 0; k < 3; ++k) glassesWorst = std::max(glassesWorst, std::fabs(static_cast<f32>(r0[k]) - r1[k]) / 255.0f);
                ++glassesPx;
            }
    }
    const f32 eyeBefore = 0.299f * rest.at(static_cast<u32>(t.eye[0].x), static_cast<u32>(t.eye[0].y))[0] / 255.0f;
    const f32 eyeAfter = 0.299f * blink.at(static_cast<u32>(t.eye[0].x), static_cast<u32>(t.eye[0].y))[0] / 255.0f;
    std::printf("óculos no piscar: %u px, pior %.4f; olho %.3f -> %.3f ", glassesPx, glassesWorst, eyeBefore, eyeAfter);
    AUREA_CHECK(glassesPx > 50u);
    AUREA_CHECK(glassesWorst < 0.02f);
    AUREA_CHECK(eyeAfter > eyeBefore + 0.1f);

    // Animação com keyframes (virar) e física do cabelo: o mesmo quadro duas
    // vezes é igual; ir para longe e voltar também.
    rig.set_control(rigLayer, control, face_rig::kEyeOpenLeft, 1.0f);
    rig.set_control(rigLayer, control, face_rig::kEyeOpenRight, 1.0f);
    {
        const Composition* root = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        const Layer* rl = root->layer(LayerId::unpack(rigLayer));
        Composition* inner = rig.e.project()->timeline().composition(rl->nested.composition);
        Layer* nl = inner->layer(LayerId::unpack(control));
        Track& turn = nl->tracks.get_or_create(TrackProperty::EffectParam, nl->effects[0].id, param_track_key(face_rig::kTurn, 0));
        turn.set(FrameIndex{0}, 0.0f);
        turn.set(FrameIndex{8}, 25.0f);
        nl->effects[0].params[face_rig::kTurn].source = ParamSource::Keyframes;
    }
    rig.set_control(rigLayer, control, face_rig::kHairPhysics, 1.0f);   // e marca o modelo como mudado
    rig.seek(11);
    const Image8 a1 = rig.capture(H);
    const Image8 a2 = rig.capture(H);
    rig.seek(40);
    const Image8 far = rig.capture(H);
    rig.seek(11);
    const Image8 a3 = rig.capture(H);
    f32 same = 0, sameMean = 0, back = 0, backMean = 0, moved = 0, movedMean = 0, swing = 0, swingMean = 0;
    face_rig_capture_diff(a1, a2, same, sameMean);
    face_rig_capture_diff(a1, a3, back, backMean);
    face_rig_capture_diff(rest, a1, moved, movedMean);
    face_rig_capture_diff(a1, far, swing, swingMean);   // quadro 40: cabeça parada há tempo, cabelo assentado
    std::printf("mesmo quadro %.4f, ida e volta %.4f, virou %.4f, balanço %.5f ", same, back, moved, swingMean);
    AUREA_CHECK(same == 0.0f && back == 0.0f);
    AUREA_CHECK(moved > 0.2f);
    AUREA_CHECK(swingMean > 0.0f);

    // Salvar e reabrir: as partes (PNG), o Controle e os keyframes voltam; o
    // mesmo quadro sai igual.
    const std::string path = "tests/build/prompt03/aurea_teste_rig_partes.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    rig.seek(11);
    const Image8 reopened = rig.capture(H);
    f32 re = 0, reMean = 0;
    face_rig_capture_diff(a1, reopened, re, reMean);
    std::printf("reaberto vs antes: pior %.4f média %.5f ", re, reMean);
    AUREA_CHECK(re < 0.01f);
    std::remove(path.c_str());
}

// Vitrine do v2: a imagem de verdade (AUREA_FACERIG_IMAGE, AUREA_FACERIG_SIZE)
// separada em partes; grava cada parte, os rótulos e 36 quadros animados
// (virar + acenar + inclinar + piscar + falar + física do cabelo) em AUREA_FX_DUMP.
AUREA_TEST(Gpu, FaceRigPartsShowcaseFromFile) {
    const char* file = std::getenv("AUREA_FACERIG_IMAGE");
    const char* size = std::getenv("AUREA_FACERIG_SIZE");
    const char* dir = std::getenv("AUREA_FX_DUMP");
    if (!file || !size || !dir || !*file || !*dir) return;
    AUREA_REQUIRE_GPU();
    unsigned W = 0, H = 0;
    if (std::sscanf(size, "%ux%u", &W, &H) != 2 || !W || !H) return;
    std::vector<u8> rgba(static_cast<usize>(W) * H * 4);
    FILE* f = std::fopen(file, "rb");
    AUREA_CHECK(f != nullptr);
    if (!f) return;
    const usize got = std::fread(rgba.data(), 1, rgba.size(), f);
    std::fclose(f);
    AUREA_CHECK_EQ(got, rgba.size());
    const std::string out(dir);
    // Partes e rótulos (CPU, as mesmas âncoras que o motor acha ao pôr o efeito).
    face_rig::Anchors a;
    (void)face_rig::auto_place(rgba.data(), W, H, a);
    face_rig::PartSet set;
    AUREA_CHECK(face_rig::split_parts(rgba.data(), W, H, a, set));
    for (const face_rig::Part& p : set.parts) {
        Image8 img;
        img.width = W;
        img.height = H;
        img.rgba.assign(static_cast<usize>(W) * H * 4, 0);
        for (u32 y = 0; y < p.h; ++y)
            std::memcpy(&img.rgba[(static_cast<usize>(p.y0 + y) * W + p.x0) * 4], &p.rgba[static_cast<usize>(y) * p.w * 4],
                        static_cast<usize>(p.w) * 4);
        char name[96];
        std::snprintf(name, sizeof name, "/part_%02u.png", p.role);
        (void)write_png(out + name, img);
    }
    {
        static const u8 kInk[face_rig::kPartRoleCount][3] = {
            {0, 0, 0}, {60, 60, 200}, {230, 120, 40}, {200, 170, 120}, {255, 220, 190}, {255, 150, 0}, {255, 255, 255},
            {230, 230, 255}, {0, 120, 255}, {0, 200, 255}, {140, 0, 140}, {200, 0, 200}, {120, 70, 0}, {160, 100, 20},
            {255, 0, 0}, {200, 0, 60}, {0, 200, 0}, {0, 120, 60}, {255, 0, 255}};
        Image8 lab;
        lab.width = W;
        lab.height = H;
        lab.rgba.assign(static_cast<usize>(W) * H * 4, 0);
        for (usize i = 0; i < set.labels.size(); ++i) {
            u8* d = &lab.rgba[i * 4];
            const u8 l = set.labels[i];
            if (l < face_rig::kPartRoleCount) { d[0] = kInk[l][0]; d[1] = kInk[l][1]; d[2] = kInk[l][2]; d[3] = 255; }
        }
        (void)write_png(out + "/labels.png", lab);
    }
    // O motor: importa, põe o rig, separa e anima o Controle com keyframes.
    FaceRigPartsEngine rig(W, H);
    auto img = rig.e.import_image(rgba.data(), W, H, "personagem");
    AUREA_CHECK(img.ok());
    if (!img.ok()) return;
    const u64 layer = *img;
    const u32 fx = rig.add_effect(layer, face_rig::kFaceRigKey);
    u64 control = 0;
    const auto split = rig.e.face_rig_split_parts(layer, fx, &control);
    AUREA_CHECK(split.ok());
    if (!split.ok()) return;
    const u64 rigLayer = *split;
    {
        const Composition* root = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        const Layer* rl = root->layer(LayerId::unpack(rigLayer));
        Composition* inner = rig.e.project()->timeline().composition(rl->nested.composition);
        Layer* nl = inner->layer(LayerId::unpack(control));
        EffectInstance& ce = nl->effects[0];
        auto key = [&](u32 param, i64 frame, f32 value) {
            nl->tracks.get_or_create(TrackProperty::EffectParam, ce.id, param_track_key(param, 0)).set(FrameIndex{frame}, value);
            ce.params[param].source = ParamSource::Keyframes;
        };
        for (i64 k = 0; k <= 36; ++k) {
            const f32 ph = static_cast<f32>(k) / 36.0f * 6.2831853f;
            key(face_rig::kTurn, k, 22.0f * std::sin(ph));
            key(face_rig::kNod, k, 9.0f * std::sin(2.0f * ph));
            key(face_rig::kTilt, k, 8.0f * std::sin(ph + 1.0f));
            const f32 blink = (k >= 14 && k <= 17) ? (k == 15 || k == 16 ? 0.0f : 0.4f) : 1.0f;
            key(face_rig::kEyeOpenLeft, k, blink);
            key(face_rig::kEyeOpenRight, k, blink);
            key(face_rig::kMouthOpen, k, 0.5f + 0.5f * std::sin(3.0f * ph));
            key(face_rig::kBodySway, k, 4.0f * std::sin(ph));
        }
    }
    rig.set_control(rigLayer, control, face_rig::kHairPhysics, 1.0f);
    for (i64 k = 0; k < 36; ++k) {
        rig.seek(k);
        char name[64];
        std::snprintf(name, sizeof name, "/anim_%02lld.png", static_cast<long long>(k));
        (void)write_png(out + name, rig.capture(std::max(W, H)));
    }
}
