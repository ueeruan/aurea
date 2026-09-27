// Lente da câmera 3D: distância focal (mm) → FOV REAL da projeção (sensor full
// frame 36×24, altura 24 mm), keyframe de mm (zoom 24→85), a profundidade de
// campo (parâmetros, lente fina, desfazer, gravação) e o Pick Focus (raio da
// câmera até o modelo). Sem GPU: o render com DOF está em test_gpu.cpp.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace aurea;

namespace aurea {
// A câmera da cena no instante fracionário (Renderer.cpp; a mesma que o
// render e as partículas usam).
scene3d::SceneCamera particle_camera_at(const Composition& comp, f64 time, u32 w, u32 h) noexcept;
}

namespace {

Composition* comp_of(Engine& e) { return e.project()->timeline().composition(e.project()->timeline().current()); }

void seek(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}

Status lens(Engine& e, u64 cam, u32 param, f32 value) {
    Command c;
    c.type = CommandType::LayerSetCameraParam;
    c.shape_param = ShapeParamPayload{LayerId::unpack(cam), param, value};
    return e.apply_command(c);
}

void key(Engine& e, u64 layer, TrackProperty p, i64 frame, f32 value) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe.track.layer = LayerId::unpack(layer);
    c.keyframe.track.property = p;
    c.keyframe.track.effectIndex = kInvalidIndex;
    c.keyframe.track.effectParamIndex = 0;
    c.keyframe.time = FrameIndex{frame};
    c.keyframe.value = value;
    AUREA_CHECK(e.apply_command(c).ok());
}

struct LensRig {
    Engine e;
    u64 cam = 0;
    bool ok = false;
    LensRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(320, 180, 30.0, nullptr).ok();
        if (!ok) return;
        const Result<u64> r = e.add_camera();
        ok = r.ok();
        if (ok) cam = *r;
        seek(e, 0);
    }
    ~LensRig() { if (ok) e.shutdown(); }
    Layer* camera() { return comp_of(e)->layer(LayerId::unpack(cam)); }
    std::vector<f32> query() {
        std::vector<f32> v(9, 0.0f);
        AUREA_CHECK(e.query_camera_lens(cam, v.data()));
        return v;
    }
    /// Deslocamento na tela (px) de um ponto 40 px à direita do centro do plano Z=0.
    f32 projected_offset(i64 frame) {
        const Composition* c = comp_of(e);
        const Mat4 vp = comp_view_projection(*c, FrameIndex{frame});
        const Vec4 p = vp * Vec4{c->width() * 0.5f + 40.0f, c->height() * 0.5f, 0.0f, 1.0f};
        return p.x / p.w - c->width() * 0.5f;
    }
};

/// Triângulo glTF (unlit, dois lados) — o alvo do Pick Focus.
std::string write_lens_triangle() {
    const f32 pos[9] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f};
    const u16 idx[3] = {0, 1, 2};
    std::vector<u8> bin(36 + 6 + 2);
    std::memcpy(bin.data(), pos, 36);
    std::memcpy(bin.data() + 36, idx, 6);
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    for (usize i = 0; i < bin.size(); i += 3) {
        const u32 v = (static_cast<u32>(bin[i]) << 16) | (i + 1 < bin.size() ? static_cast<u32>(bin[i + 1]) << 8 : 0u)
                    | (i + 2 < bin.size() ? bin[i + 2] : 0u);
        enc += b64[(v >> 18) & 63];
        enc += b64[(v >> 12) & 63];
        enc += i + 1 < bin.size() ? b64[(v >> 6) & 63] : '=';
        enc += i + 2 < bin.size() ? b64[v & 63] : '=';
    }
    char json[2048];
    std::snprintf(json, sizeof(json),
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
        R"("materials":[{"doubleSided":true,"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1]},"extensions":{"KHR_materials_unlit":{}}}],)"
        R"("extensionsUsed":["KHR_materials_unlit"],)"
        R"("buffers":[{"byteLength":%u,"uri":"data:application/octet-stream;base64,%s"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})",
        static_cast<unsigned>(bin.size()), enc.c_str());
    const std::string path = "aurea_teste_lente_triangulo.gltf";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(json, 1, std::strlen(json), f);
        std::fclose(f);
    }
    return path;
}

} // namespace

AUREA_TEST(Scene3DLens, FovComesFromFocalLengthOnAFullFrameSensor) {
    // FOV = 2·atan(24 / 2f): a altura do sensor, porque a projeção é por FOV vertical.
    AUREA_CHECK_NEAR(camera_fov_from_focal(50.0f), 26.9915f, 0.01f);
    AUREA_CHECK_NEAR(camera_fov_from_focal(24.0f), 53.1301f, 0.01f);
    AUREA_CHECK_NEAR(camera_fov_from_focal(14.0f), 81.2026f, 0.02f);
    AUREA_CHECK_NEAR(camera_fov_from_focal(200.0f), 6.8673f, 0.01f);
    const f32 presets[] = {14, 18, 24, 35, 50, 85, 135, 200};
    for (u32 i = 0; i < 8; ++i) {
        // Menor mm = mais aberto; e o inverso é exato.
        if (i + 1 < 8) AUREA_CHECK(camera_fov_from_focal(presets[i]) > camera_fov_from_focal(presets[i + 1]));
        AUREA_CHECK_NEAR(camera_focal_from_fov(camera_fov_from_focal(presets[i])), presets[i], presets[i] * 1e-4f);
    }
}

AUREA_TEST(Scene3DLens, FocalLengthChangesTheRealProjection) {
    LensRig rig;
    if (!rig.ok) return;
    // Câmera nova: mm sincronizado com o FOV e foco no plano da composição.
    AUREA_CHECK_NEAR(rig.camera()->camera.focalLength, camera_focal_from_fov(rig.camera()->camera.fov), 1e-3f);
    AUREA_CHECK_NEAR(rig.camera()->camera.focusDistance, 216.0f, 0.5f);
    AUREA_CHECK(lens(rig.e, rig.cam, 0, 50.0f).ok());
    AUREA_CHECK_NEAR(rig.camera()->camera.fov, 26.9915f, 0.01f);
    std::vector<f32> q = rig.query();
    AUREA_CHECK_NEAR(q[0], 50.0f, 1e-3f);
    AUREA_CHECK_NEAR(q[1], 26.9915f, 0.01f);
    const f32 at50 = rig.projected_offset(0);
    AUREA_CHECK(lens(rig.e, rig.cam, 0, 100.0f).ok());
    const f32 at100 = rig.projected_offset(0);
    AUREA_CHECK(lens(rig.e, rig.cam, 0, 24.0f).ok());
    const f32 at24 = rig.projected_offset(0);
    std::printf("    lente: 40 px do centro -> 24mm %.2f / 50mm %.2f / 100mm %.2f px\n", at24, at50, at100);
    // A projeção escala com a razão das focais (plano fixo, câmera parada).
    AUREA_CHECK_NEAR(at100 / at50, 2.0f, 0.01f);
    AUREA_CHECK_NEAR(at50 / at24, 50.0f / 24.0f, 0.01f);
    // Fora da faixa: recusado sem mexer na lente.
    AUREA_CHECK(!lens(rig.e, rig.cam, 0, 1.0f).ok());
    AUREA_CHECK_NEAR(rig.query()[0], 24.0f, 1e-3f);
}

AUREA_TEST(Scene3DLens, FocalKeyframesAnimateTheZoomInMillimetres) {
    LensRig rig;
    if (!rig.ok) return;
    key(rig.e, rig.cam, TrackProperty::FocalLength, 0, 24.0f);
    key(rig.e, rig.cam, TrackProperty::FocalLength, 60, 85.0f);
    const Layer& l = *rig.camera();
    AUREA_CHECK_NEAR(camera_fov_deg_at(l, 0.0), camera_fov_from_focal(24.0f), 1e-3f);
    AUREA_CHECK_NEAR(camera_fov_deg_at(l, 60.0), camera_fov_from_focal(85.0f), 1e-3f);
    // Interpola em mm (como uma zoom de verdade), não em graus.
    AUREA_CHECK_NEAR(camera_fov_deg_at(l, 30.0), camera_fov_from_focal(54.5f), 1e-2f);
    // Fracionário (subquadro do desfoque de movimento) também.
    AUREA_CHECK_NEAR(camera_fov_deg_at(l, 15.5), camera_fov_from_focal(24.0f + 61.0f * 15.5f / 60.0f), 1e-2f);
    seek(rig.e, 30);
    std::vector<f32> q = rig.query();
    AUREA_CHECK_NEAR(q[0], 54.5f, 1e-2f);
    AUREA_CHECK((static_cast<u32>(q[7]) & 1u) != 0);
    AUREA_CHECK_NEAR(rig.projected_offset(60) / rig.projected_offset(0), 85.0f / 24.0f, 0.01f);
    // Mexer com keyframe = keyframe no cabeçote (não o valor parado).
    AUREA_CHECK(lens(rig.e, rig.cam, 0, 35.0f).ok());
    const Track* focal = rig.camera()->tracks.find(TrackProperty::FocalLength);
    AUREA_CHECK(focal && focal->keys.size() == 3);
    AUREA_CHECK_NEAR(rig.query()[0], 35.0f, 1e-3f);
    // Um passo de desfazer volta o keyframe.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(undo).ok());
    focal = rig.camera()->tracks.find(TrackProperty::FocalLength);
    AUREA_CHECK(focal && focal->keys.size() == 2);
    AUREA_CHECK_NEAR(rig.query()[0], 54.5f, 1e-2f);
}

AUREA_TEST(Scene3DLens, DepthOfFieldFollowsTheThinLens) {
    // 50 mm f/2.8 focado a 2 m, quadro de 1080 px (540 px/m): diâmetro no
    // infinito = f²/(N(S1−f)) = 2500/(2,8·1950) = 0,4579 mm → raio 10,30 px.
    scene3d::SceneCamera c;
    c.fovY = camera_fov_from_focal(50.0f) * kDeg2Rad;
    c.nearZ = 10.0f;
    c.dof = true;
    c.pixelsPerMeter = 540.0f;
    c.focusDistance = 2.0f * 540.0f;
    c.fStop = 2.8f;
    c.blurAmount = 1.0f;
    const scene3d::DofLens l = scene3d::dof_lens(c, 1080);
    AUREA_CHECK(l.active());
    AUREA_CHECK_NEAR(l.cocScale, 10.30f, 0.02f);
    AUREA_CHECK_NEAR(l.radius_at_depth(c.focusDistance, c.nearZ), 0.0f, 1e-3f);        // plano de foco: nítido
    AUREA_CHECK_NEAR(l.radius_at_depth(4.0f * 540.0f, c.nearZ), 5.15f, 0.02f);          // 4 m: metade do infinito
    AUREA_CHECK_NEAR(l.radius_at_depth(1.0f * 540.0f, c.nearZ), -10.30f, 0.02f);        // 1 m: perto, mesmo raio
    // f/1.4 dobra; força 2 dobra; teleobjetiva desfoca mais que grande-angular.
    c.fStop = 1.4f;
    AUREA_CHECK_NEAR(scene3d::dof_lens(c, 1080).cocScale, 20.60f, 0.05f);
    c.blurAmount = 2.0f;
    AUREA_CHECK_NEAR(scene3d::dof_lens(c, 1080).cocScale, 41.20f, 0.1f);
    c.blurAmount = 1.0f;
    c.fStop = 2.8f;
    c.fovY = camera_fov_from_focal(85.0f) * kDeg2Rad;
    AUREA_CHECK(scene3d::dof_lens(c, 1080).cocScale > 2.5f * 10.30f);
    // Desligado, sem força ou sem foco: nenhum desfoque.
    c.blurAmount = 0.0f;
    AUREA_CHECK(!scene3d::dof_lens(c, 1080).active());
    c.blurAmount = 1.0f;
    c.dof = false;
    AUREA_CHECK(!scene3d::dof_lens(c, 1080).active());
}

AUREA_TEST(Scene3DLens, LensParametersKeyUndoAndSurviveSaveAndReopen) {
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_lente_camera.aurea";
    {
        LensRig rig;
        if (!rig.ok) return;
        AUREA_CHECK(lens(rig.e, rig.cam, 1, 1.0f).ok());
        AUREA_CHECK(lens(rig.e, rig.cam, 0, 85.0f).ok());
        AUREA_CHECK(lens(rig.e, rig.cam, 2, 500.0f).ok());
        AUREA_CHECK(lens(rig.e, rig.cam, 3, 1.4f).ok());
        AUREA_CHECK(lens(rig.e, rig.cam, 4, 2.0f).ok());
        AUREA_CHECK(!lens(rig.e, rig.cam, 3, 100.0f).ok());   // f/100 não existe aqui
        key(rig.e, rig.cam, TrackProperty::FocusDistance, 0, 300.0f);
        key(rig.e, rig.cam, TrackProperty::FocusDistance, 60, 900.0f);
        key(rig.e, rig.cam, TrackProperty::CameraBlur, 0, 2.0f);
        seek(rig.e, 30);
        std::vector<f32> q = rig.query();
        AUREA_CHECK_NEAR(q[2], 1.0f, 0.0f);
        AUREA_CHECK_NEAR(q[3], 600.0f, 0.5f);
        AUREA_CHECK_NEAR(q[4], 1.4f, 1e-4f);
        AUREA_CHECK_NEAR(q[5], 2.0f, 1e-4f);
        AUREA_CHECK_NEAR(q[6], 90.0f, 1e-3f);
        AUREA_CHECK_EQ(static_cast<u32>(q[7]), 2u | 8u);
        // Foco animado: mexer grava no cabeçote; desfazer desfaz só isso.
        AUREA_CHECK(lens(rig.e, rig.cam, 2, 700.0f).ok());
        AUREA_CHECK_NEAR(rig.query()[3], 700.0f, 0.5f);
        Command undo;
        undo.type = CommandType::Undo;
        AUREA_CHECK(rig.e.apply_command(undo).ok());
        AUREA_CHECK_NEAR(rig.query()[3], 600.0f, 0.5f);
        // O render lê o mesmo (câmera da cena no instante).
        const scene3d::SceneCamera sc = particle_camera_at(*comp_of(rig.e), 30.0, 320, 180);
        AUREA_CHECK(sc.dof);
        AUREA_CHECK_NEAR(sc.focusDistance, 600.0f, 0.5f);
        AUREA_CHECK_NEAR(sc.fStop, 1.4f, 1e-4f);
        AUREA_CHECK_NEAR(sc.blurAmount, 2.0f, 1e-4f);
        AUREA_CHECK_NEAR(sc.fovY / kDeg2Rad, camera_fov_from_focal(85.0f), 1e-3f);
        AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    }
    {
        LensRig rig;
        if (!rig.ok) return;
        AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
        const Composition* c = comp_of(rig.e);
        const Layer* cam = nullptr;
        for (u32 i = 0; i < c->order().size(); ++i) {
            const Layer* l = c->layer(c->order().at(i));
            if (l && l->kind == LayerKind::Camera && l->camera.dofEnabled) cam = l;
        }
        AUREA_CHECK(cam != nullptr);
        if (!cam) return;
        AUREA_CHECK_NEAR(cam->camera.focalLength, 85.0f, 1e-3f);
        AUREA_CHECK_NEAR(cam->camera.fov, camera_fov_from_focal(85.0f), 1e-3f);
        AUREA_CHECK_NEAR(cam->camera.aperture, 1.4f, 1e-4f);
        AUREA_CHECK_NEAR(cam->camera.blurAmount, 2.0f, 1e-4f);
        const Track* focus = cam->tracks.find(TrackProperty::FocusDistance);
        AUREA_CHECK(focus && focus->keys.size() == 2);
        const Track* blur = cam->tracks.find(TrackProperty::CameraBlur);
        AUREA_CHECK(blur && blur->keys.size() == 1);
    }
    std::remove(path.c_str());
}

AUREA_TEST(Scene3DLens, PickFocusMeasuresTheDistanceToTheTappedModel) {
    LensRig rig;
    if (!rig.ok) return;
    ModelImport mi;
    mi.path = write_lens_triangle();
    const Result<u64> model = rig.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    // A câmera nova fica a 1,2 × altura (216 px) do plano Z=0, onde o modelo nasce.
    const f32 atPlane = rig.e.pick_focus_distance(rig.cam, 160.0f, 95.0f);
    AUREA_CHECK_NEAR(atPlane, 216.0f, 1.0f);
    // Longe do modelo: nada a focar.
    AUREA_CHECK(rig.e.pick_focus_distance(rig.cam, 2.0f, 2.0f) < 0.0f);
    // Modelo 300 px mais fundo: a distância acompanha (eixo ótico).
    Layer* m = comp_of(rig.e)->layer(LayerId::unpack(*model));
    m->transform.position.z += 300.0f;
    const f32 deeper = rig.e.pick_focus_distance(rig.cam, 160.0f, 92.0f);
    std::printf("    pick focus: plano %.2f px, 300 px atras %.2f px\n", atPlane, deeper);
    AUREA_CHECK_NEAR(deeper, 516.0f, 1.5f);
    // Gravar o medido: DOF focado no modelo.
    AUREA_CHECK(lens(rig.e, rig.cam, 2, deeper).ok());
    AUREA_CHECK_NEAR(rig.query()[3], deeper, 1e-3f);
    std::remove(mi.path.c_str());
}
