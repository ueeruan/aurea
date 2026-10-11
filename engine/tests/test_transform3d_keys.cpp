// Keyframes de transformação — em especial a 3D (posição e rotação em X/Y/Z).
//
// O caso relatado: "marco um keyframe, marco outro e animo; a animação inicial
// não fica igual — é como se não salvasse". Aqui a sequência da UI é repetida
// comando a comando (losango = KeyframeInsert com o valor AVALIADO no playhead;
// mexer numa propriedade animada = KeyframeInsert no playhead) e a pose de cada
// quadro é CONFERIDA pela mesma consulta que o painel usa.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aurea;

namespace {

/// A mesma sequência do losango do painel (`toggleTransformKeyframe`): insere o
/// valor AVALIADO no playhead para cada propriedade do grupo.
void marcar(Engine& e, LayerId layer, const bridge::LayerDetailPOD& d, std::initializer_list<TrackProperty> props) {
    for (TrackProperty p : props) {
        Command c;
        c.type = CommandType::KeyframeInsert;
        c.keyframe.track.layer = layer;
        c.keyframe.track.property = p;
        c.keyframe.track.effectIndex = kInvalidIndex;
        c.keyframe.track.effectParamIndex = 0;
        c.keyframe.time = FrameIndex{d.localPlayhead};
        c.keyframe.value = 0.0f;
        switch (p) {
            case TrackProperty::PositionX: c.keyframe.value = d.position[0]; break;
            case TrackProperty::PositionY: c.keyframe.value = d.position[1]; break;
            case TrackProperty::PositionZ: c.keyframe.value = d.position[2]; break;
            case TrackProperty::RotationX: c.keyframe.value = d.rotation[0]; break;
            case TrackProperty::RotationY: c.keyframe.value = d.rotation[1]; break;
            case TrackProperty::RotationZ: c.keyframe.value = d.rotation[2]; break;
            default: break;
        }
        AUREA_CHECK(e.apply_command(c).ok());
    }
}

/// Mexer numa propriedade animada: a UI grava no playhead (KeyframeInsert).
void editar(Engine& e, LayerId layer, TrackProperty p, FrameIndex local, f32 v) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe.track.layer = layer;
    c.keyframe.track.property = p;
    c.keyframe.track.effectIndex = kInvalidIndex;
    c.keyframe.track.effectParamIndex = 0;
    c.keyframe.time = local;
    c.keyframe.value = v;
    AUREA_CHECK(e.apply_command(c).ok());
}

bridge::LayerDetailPOD detalhe(Engine& e, u64 layer) {
    bridge::LayerDetailPOD d{};
    AUREA_CHECK(e.query_layer_detail(layer, d));
    return d;
}

void seek(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}

struct Rig {
    Engine e;
    LayerId id{};
    bool ok = false;

    Rig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(320, 180, 30.0, nullptr).ok();
        if (!ok) return;
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        id = comp->add_layer(LayerKind::Shape, "quadrado 3D");
        Layer* l = comp->layer(id);
        l->threeD = true;
        l->shape.shapeType = 0;
        l->shape.bounds = Rect{0.0f, 0.0f, 80.0f, 80.0f};
        l->shape.filled = true;
        l->transform.anchor = Vec3{40.0f, 40.0f, 0.0f};
        l->transform.position = Vec3{140.0f, 90.0f, 30.0f};
        l->transform.rotation = Vec3{12.0f, 24.0f, 36.0f};
        l->end = FrameIndex{120};
        seek(e, 0);
    }
    ~Rig() { if (ok) e.shutdown(); }
};

constexpr u32 kBit(TrackProperty p) { return 1u << static_cast<u32>(p); }

} // namespace

AUREA_TEST(Transform3D, StaticTracksFollowAnchorAndPositionEditsOnNull) {
    Rig rig;
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    auto* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    auto* layer = comp->layer(rig.id);
    layer->kind = LayerKind::Null;
    layer->tracks.get_or_create(TrackProperty::AnchorX).staticValue = -77;
    layer->tracks.get_or_create(TrackProperty::PositionX).staticValue = -99;
    Command anchor; anchor.type = CommandType::LayerSetAnchor;
    anchor.anchor.layer = rig.id; anchor.anchor.ax = 13; anchor.anchor.ay = 17; anchor.anchor.az = 23;
    AUREA_CHECK(rig.e.apply_command(anchor).ok());
    AUREA_CHECK_NEAR(detalhe(rig.e, rig.id.pack()).anchor[0], 13, 1e-5);
    Command position; position.type = CommandType::LayerSetPosition;
    position.position.layer = rig.id; position.position.x = 180; position.position.y = 90; position.position.z = 30;
    AUREA_CHECK(rig.e.apply_command(position).ok());
    AUREA_CHECK_NEAR(detalhe(rig.e, rig.id.pack()).position[0], 180, 1e-5);
    AUREA_CHECK_NEAR(layer->tracks.find(TrackProperty::PositionX)->staticValue, 180, 1e-5);
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(rig.e.apply_command(undo).ok());
    AUREA_CHECK_NEAR(detalhe(rig.e, rig.id.pack()).anchor[0], 13, 1e-5);
}

AUREA_TEST(Transform3D, TwoKeysKeepTheFirstPoseAndInterpolate) {
    Rig rig;
    if (!rig.ok) return;
    Engine& e = rig.e;
    const u64 id = rig.id.pack();

    // --- 1) o losango no quadro 0 marca posição e rotação -------------------
    const bridge::LayerDetailPOD d0 = detalhe(e, id);
    marcar(e, rig.id, d0, {TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ,
                   TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ});
    const bridge::LayerDetailPOD marcado = detalhe(e, id);
    const u32 bits = kBit(TrackProperty::PositionX) | kBit(TrackProperty::PositionY) | kBit(TrackProperty::PositionZ)
                   | kBit(TrackProperty::RotationX) | kBit(TrackProperty::RotationY) | kBit(TrackProperty::RotationZ);
    AUREA_CHECK((marcado.animatedMask & bits) == bits);
    AUREA_CHECK((marcado.keyAtPlayheadMask & bits) == bits);
    AUREA_CHECK(std::fabs(marcado.position[0] - 140.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(marcado.position[2] - 30.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(marcado.rotation[0] - 12.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(marcado.rotation[2] - 36.0f) < 1e-3f);

    // --- 2) no quadro 60, mexe: vira o segundo keyframe ----------------------
    seek(e, 60);
    editar(e, rig.id, TrackProperty::PositionX, FrameIndex{60}, 240.0f);
    editar(e, rig.id, TrackProperty::RotationZ, FrameIndex{60}, 90.0f);
    const bridge::LayerDetailPOD em60 = detalhe(e, id);
    AUREA_CHECK(std::fabs(em60.position[0] - 240.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(em60.rotation[2] - 90.0f) < 1e-3f);

    // --- 3) A POSE INICIAL NÃO MUDOU (era isto que "não salvava") -----------
    seek(e, 0);
    const bridge::LayerDetailPOD volta = detalhe(e, id);
    std::printf("\n    quadro 0: x %.2f z %.2f rotX %.2f rotZ %.2f (era 140/30/12/36)\n", volta.position[0],
                volta.position[2], volta.rotation[0], volta.rotation[2]);
    AUREA_CHECK(std::fabs(volta.position[0] - 140.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(volta.position[1] - 90.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(volta.position[2] - 30.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(volta.rotation[0] - 12.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(volta.rotation[1] - 24.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(volta.rotation[2] - 36.0f) < 1e-3f);

    // --- 4) no meio, interpola -------------------------------------------------
    seek(e, 30);
    const bridge::LayerDetailPOD meio = detalhe(e, id);
    std::printf("    quadro 30: x %.2f (esperado 190) rotZ %.2f (esperado 63)\n", meio.position[0], meio.rotation[2]);
    AUREA_CHECK(std::fabs(meio.position[0] - 190.0f) < 1.0f);
    AUREA_CHECK(std::fabs(meio.rotation[2] - 63.0f) < 1.0f);
    // O que NÃO foi animado não se mexeu.
    AUREA_CHECK(std::fabs(meio.position[1] - 90.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(meio.rotation[0] - 12.0f) < 1e-3f);

    // --- 5) e sobrevive a salvar e reabrir ------------------------------------
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_3dkeys.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    seek(e, 0);
    const bridge::LayerDetailPOD reaberto = detalhe(e, id);
    seek(e, 60);
    const bridge::LayerDetailPOD reaberto60 = detalhe(e, id);
    std::printf("    reaberto: quadro 0 x %.2f, quadro 60 x %.2f\n", reaberto.position[0], reaberto60.position[0]);
    AUREA_CHECK(std::fabs(reaberto.position[0] - 140.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(reaberto.rotation[2] - 36.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(reaberto60.position[0] - 240.0f) < 1e-3f);
    std::remove(path.c_str());
}

AUREA_TEST(Transform3D, EditingAnAnimatedAxisKeysThatAxisAndNotTheStaticPose) {
    Rig rig;
    if (!rig.ok) return;
    Engine& e = rig.e;
    const u64 id = rig.id.pack();

    // Só o Z está animado (dois keyframes). Mexer no X, que NÃO está animado,
    // muda o valor FIXO — é o que a UI faz (`setTransform` sem trilha vai para
    // `setPosition`, não para o keyframe). O Z animado não pode sentir.
    seek(e, 0);
    editar(e, rig.id, TrackProperty::PositionZ, FrameIndex{0}, 30.0f);
    seek(e, 60);
    editar(e, rig.id, TrackProperty::PositionZ, FrameIndex{60}, 90.0f);
    seek(e, 30);
    AUREA_CHECK(std::fabs(detalhe(e, id).position[2] - 60.0f) < 1.0f);

    Command c;
    c.type = CommandType::LayerSetPosition;
    c.position.layer = rig.id;
    c.position.x = 200.0f;
    c.position.y = 90.0f;
    c.position.z = 30.0f;
    AUREA_CHECK(e.apply_command(c).ok());

    const bridge::LayerDetailPOD depois = detalhe(e, id);
    AUREA_CHECK((depois.animatedMask & kBit(TrackProperty::PositionX)) == 0u);
    AUREA_CHECK(std::fabs(depois.position[0] - 200.0f) < 1e-3f);
    seek(e, 0);
    const bridge::LayerDetailPOD inicio = detalhe(e, id);
    seek(e, 60);
    const bridge::LayerDetailPOD fim = detalhe(e, id);
    std::printf("\n    X fixo: quadro 0 x %.1f, quadro 60 x %.1f; Z animado 0 -> %.1f, 60 -> %.1f\n",
                static_cast<double>(inicio.position[0]), static_cast<double>(fim.position[0]),
                static_cast<double>(inicio.position[2]), static_cast<double>(fim.position[2]));
    AUREA_CHECK(std::fabs(inicio.position[0] - 200.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(fim.position[0] - 200.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(inicio.position[2] - 30.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(fim.position[2] - 90.0f) < 1e-3f);
}

AUREA_TEST(Transform3D, ThreeAxisRotationKeysAsOneInstant) {
    Rig rig;
    if (!rig.ok) return;
    Engine& e = rig.e;
    const u64 id = rig.id.pack();

    // O losango da Rotação vale para X, Y e Z juntos: um instante, três eixos.
    const bridge::LayerDetailPOD d0 = detalhe(e, id);
    marcar(e, rig.id, d0, {TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ});
    seek(e, 48);
    bridge::LayerDetailPOD d48 = detalhe(e, id);
    editar(e, rig.id, TrackProperty::RotationX, FrameIndex{48}, 60.0f);
    editar(e, rig.id, TrackProperty::RotationY, FrameIndex{48}, d48.rotation[1]);
    editar(e, rig.id, TrackProperty::RotationZ, FrameIndex{48}, d48.rotation[2]);

    // Voltar ao quadro 0 tem de devolver a pose marcada, não a de 48.
    seek(e, 0);
    const bridge::LayerDetailPOD volta = detalhe(e, id);
    seek(e, 24);
    const bridge::LayerDetailPOD meio = detalhe(e, id);
    seek(e, 48);
    const bridge::LayerDetailPOD fim = detalhe(e, id);
    std::printf("\n    rotacao 3 eixos: quadro 0 (%.1f %.1f %.1f), 24 (%.1f %.1f %.1f), 48 (%.1f %.1f %.1f)\n",
                static_cast<double>(volta.rotation[0]), static_cast<double>(volta.rotation[1]),
                static_cast<double>(volta.rotation[2]), static_cast<double>(meio.rotation[0]),
                static_cast<double>(meio.rotation[1]), static_cast<double>(meio.rotation[2]),
                static_cast<double>(fim.rotation[0]), static_cast<double>(fim.rotation[1]),
                static_cast<double>(fim.rotation[2]));
    AUREA_CHECK(std::fabs(volta.rotation[0] - 12.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(volta.rotation[1] - 24.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(volta.rotation[2] - 36.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(fim.rotation[0] - 60.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(meio.rotation[0] - 36.0f) < 2.0f);
    AUREA_CHECK(std::fabs(meio.rotation[1] - 24.0f) < 1e-3f);
}

AUREA_TEST(Transform3D, SceneLayoutPreservesAnimationAndHandlesZeroScale) {
    Rig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    auto& e = rig.e;
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto* layer = comp->layer(rig.id);
    editar(e, rig.id, TrackProperty::PositionZ, FrameIndex{0}, 30);
    editar(e, rig.id, TrackProperty::PositionZ, FrameIndex{30}, 90);
    seek(e, 0);
    Command c; c.type = CommandType::LayerLayoutTransform;
    c.shape_param = ShapeParamPayload{rig.id, 2, 50};
    AUREA_CHECK(e.apply_command(c).ok());
    AUREA_CHECK(std::fabs(detalhe(e, rig.id.pack()).position[2] - 50) < .001f);
    seek(e, 30);
    AUREA_CHECK(std::fabs(detalhe(e, rig.id.pack()).position[2] - 110) < .001f);
    AUREA_CHECK_EQ(layer->tracks.find(TrackProperty::PositionZ)->keys.size(), 2u);
    c.shape_param = ShapeParamPayload{rig.id, 6, 35};
    AUREA_CHECK(e.apply_command(c).ok());
    AUREA_CHECK(layer->tracks.find(TrackProperty::RotationX) == nullptr);
    editar(e, rig.id, TrackProperty::ScaleX, FrameIndex{0}, 1);
    editar(e, rig.id, TrackProperty::ScaleX, FrameIndex{30}, 2);
    c.shape_param = ShapeParamPayload{rig.id, 3, 4};
    AUREA_CHECK(e.apply_command(c).ok());
    seek(e, 0);
    AUREA_CHECK(std::fabs(detalhe(e, rig.id.pack()).scale[0] - 2) < .001f);
    editar(e, rig.id, TrackProperty::ScaleY, FrameIndex{0}, 0);
    editar(e, rig.id, TrackProperty::ScaleY, FrameIndex{30}, 1);
    c.shape_param = ShapeParamPayload{rig.id, 4, 2};
    AUREA_CHECK(e.apply_command(c).ok());
    seek(e, 30);
    AUREA_CHECK(std::fabs(detalhe(e, rig.id.pack()).scale[1] - 3) < .001f);
    const auto camera = e.add_camera(); AUREA_CHECK(camera.ok());
    const auto null = e.add_null(true); AUREA_CHECK(null.ok());
    f32 guides[1280]{};
    e.set_scene_editor(true, -30, 20, 3);
    const u32 count = e.query_scene_guides(guides, 256);
    AUREA_CHECK(count >= 20);
    bool cameraFound = false, nullFound = false;
    for (u32 i = 0; i < count; ++i) { cameraFound |= guides[i * 5 + 4] == 1; nullFound |= guides[i * 5 + 4] == 2; }
    AUREA_CHECK(cameraFound); AUREA_CHECK(nullFound);
    e.set_scene_editor(false, 0, 0, 3);
    AUREA_CHECK_EQ(e.query_scene_guides(guides, 256), 0u);
}

// A câmera com dois keys (quadro 0 e 60). Auto-Key (After Effects): mexer no
// quadro 30 é KeyframeInsert — os keys de 0 e 60 NÃO mudam. O que a cena 3D
// manda hoje (LayerLayoutTransform) desloca a curva INTEIRA: o key inicial e o
// final andam junto — era "os keyframes da câmera mudam a posição inicial e
// final sempre". O contrato do motor está certo; a escolha do comando é dos apps.
AUREA_TEST(Transform3D, CameraKeyAtPlayheadKeepsOtherKeysButLayoutShiftsThem) {
    Rig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    Engine& e = rig.e;
    const auto camera = e.add_camera(); AUREA_CHECK(camera.ok()); if (!camera.ok()) return;
    const u64 id = *camera;
    const LayerId lid = LayerId::unpack(id);
    auto* comp = e.project()->timeline().composition(e.project()->timeline().current());

    seek(e, 0);
    editar(e, lid, TrackProperty::PositionX, FrameIndex{0}, 100.0f);
    editar(e, lid, TrackProperty::PositionY, FrameIndex{0}, 50.0f);
    seek(e, 60);
    editar(e, lid, TrackProperty::PositionX, FrameIndex{60}, 300.0f);
    editar(e, lid, TrackProperty::PositionY, FrameIndex{60}, 150.0f);

    // --- Auto-Key: mover a câmera no quadro 30 cria o key de X e de Y ali ---
    seek(e, 30);
    editar(e, lid, TrackProperty::PositionX, FrameIndex{30}, 500.0f);
    editar(e, lid, TrackProperty::PositionY, FrameIndex{30}, 400.0f);
    const bridge::LayerDetailPOD em30 = detalhe(e, id);
    const u32 xy = kBit(TrackProperty::PositionX) | kBit(TrackProperty::PositionY);
    AUREA_CHECK((em30.animatedMask & xy) == xy);
    AUREA_CHECK((em30.keyAtPlayheadMask & xy) == xy);
    AUREA_CHECK(std::fabs(em30.position[0] - 500.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(em30.position[1] - 400.0f) < 1e-3f);
    seek(e, 0);
    const bridge::LayerDetailPOD inicio = detalhe(e, id);
    seek(e, 60);
    const bridge::LayerDetailPOD fim = detalhe(e, id);
    std::printf("\n    camera Auto-Key: quadro 0 (%.0f, %.0f) quadro 60 (%.0f, %.0f) — era 100/50 e 300/150\n",
                static_cast<double>(inicio.position[0]), static_cast<double>(inicio.position[1]),
                static_cast<double>(fim.position[0]), static_cast<double>(fim.position[1]));
    AUREA_CHECK(std::fabs(inicio.position[0] - 100.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(inicio.position[1] - 50.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(fim.position[0] - 300.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(fim.position[1] - 150.0f) < 1e-3f);
    AUREA_CHECK_EQ(comp->layer(lid)->tracks.find(TrackProperty::PositionX)->keys.size(), 3u);

    // --- O que a cena 3D manda hoje: LayerLayoutTransform no quadro 0 -------
    // Alvo X = 160 no quadro 0 (delta +60): TODOS os keys de X andam 60.
    seek(e, 0);
    Command c; c.type = CommandType::LayerLayoutTransform;
    c.shape_param = ShapeParamPayload{lid, 0, 160.0f};
    AUREA_CHECK(e.apply_command(c).ok());
    AUREA_CHECK(std::fabs(detalhe(e, id).position[0] - 160.0f) < 1e-3f);
    seek(e, 30);
    const f32 x30 = detalhe(e, id).position[0];
    seek(e, 60);
    const f32 x60 = detalhe(e, id).position[0];
    std::printf("    camera layout no quadro 0 (+60): quadro 30 x %.0f (era 500), quadro 60 x %.0f (era 300)\n",
                static_cast<double>(x30), static_cast<double>(x60));
    AUREA_CHECK(std::fabs(x30 - 560.0f) < 1e-3f);
    AUREA_CHECK(std::fabs(x60 - 360.0f) < 1e-3f);
    AUREA_CHECK_EQ(comp->layer(lid)->tracks.find(TrackProperty::PositionX)->keys.size(), 3u);
    // Y não foi tocado pelo layout de X.
    AUREA_CHECK(std::fabs(detalhe(e, id).position[1] - 150.0f) < 1e-3f);
}

// O relato "os keyframes de qualquer camada e nulo 3D em X/Y não marcam": a
// cena 3D ligada (set_scene_editor) só troca a vista de navegação — o motor
// NÃO bloqueia KeyframeInsert no nulo 3D nem na câmera. Quem decide entre
// keyframe e LayerLayoutTransform é a UI (hoje manda layout na cena).
AUREA_TEST(Transform3D, SceneEditorNeverBlocksPositionKeysOnNullAndCamera) {
    Rig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    Engine& e = rig.e;
    const auto null3d = e.add_null(true); AUREA_CHECK(null3d.ok());
    const auto camera = e.add_camera(); AUREA_CHECK(camera.ok());
    if (!null3d.ok() || !camera.ok()) return;
    e.set_scene_editor(true, -30, 20, 3);
    const u32 xy = kBit(TrackProperty::PositionX) | kBit(TrackProperty::PositionY);
    for (const u64 id : {*null3d, *camera}) {
        seek(e, 24);
        const LayerId lid = LayerId::unpack(id);
        const bridge::LayerDetailPOD antes = detalhe(e, id);
        AUREA_CHECK_EQ(antes.animatedMask & xy, 0u);
        // A mesma sequência de `setTransform2` com a trilha animada (Auto-Key ON):
        // um KeyframeInsert por componente, no cabeçote LOCAL da camada.
        editar(e, lid, TrackProperty::PositionX, FrameIndex{antes.localPlayhead}, antes.position[0] + 40.0f);
        editar(e, lid, TrackProperty::PositionY, FrameIndex{antes.localPlayhead}, antes.position[1] - 25.0f);
        const bridge::LayerDetailPOD marcado = detalhe(e, id);
        AUREA_CHECK_EQ(marcado.animatedMask & xy, xy);
        AUREA_CHECK_EQ(marcado.keyAtPlayheadMask & xy, xy);
        AUREA_CHECK_NEAR(marcado.position[0], antes.position[0] + 40.0f, 1e-3f);
        AUREA_CHECK_NEAR(marcado.position[1], antes.position[1] - 25.0f, 1e-3f);
        // Segundo key mais adiante: o primeiro fica como foi marcado.
        seek(e, 60);
        editar(e, lid, TrackProperty::PositionX, FrameIndex{detalhe(e, id).localPlayhead}, antes.position[0] + 120.0f);
        seek(e, 24);
        AUREA_CHECK_NEAR(detalhe(e, id).position[0], antes.position[0] + 40.0f, 1e-3f);
        seek(e, 42);
        AUREA_CHECK_NEAR(detalhe(e, id).position[0], antes.position[0] + 80.0f, 1.0f);
        const Layer* l = e.project()->timeline().composition(e.project()->timeline().current())->layer(lid);
        AUREA_CHECK(l && l->tracks.find(TrackProperty::PositionX) && l->tracks.find(TrackProperty::PositionX)->keys.size() == 2);
        AUREA_CHECK(l && l->tracks.find(TrackProperty::PositionY) && l->tracks.find(TrackProperty::PositionY)->keys.size() == 1);
    }
    e.set_scene_editor(false, 0, 0, 3);
}

AUREA_TEST(Transform3D, LayoutOpacityAndSkewPreserveKeysAndUndo) {
    Rig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    auto& engine = rig.e;
    auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    for (u32 property : {12u, 13u, 14u}) {
        editar(engine, rig.id, static_cast<TrackProperty>(property), FrameIndex{0}, 0.2f);
        editar(engine, rig.id, static_cast<TrackProperty>(property), FrameIndex{30}, 0.6f);
        seek(engine, 15);
        Command command; command.type = CommandType::LayerLayoutTransform;
        command.shape_param = ShapeParamPayload{rig.id, property, 0.5f};
        AUREA_CHECK(engine.apply_command(command).ok());
        const Track* track = comp->layer(rig.id)->tracks.find(static_cast<TrackProperty>(property));
        AUREA_CHECK(track && track->keys.size() == 2);
        if (track && track->keys.size() == 2) {
            AUREA_CHECK(std::fabs(track->keys[0].value - 0.3f) < .001f);
            AUREA_CHECK(std::fabs(track->keys[1].value - 0.7f) < .001f);
            AUREA_CHECK_EQ(track->keys[0].time.value, 0);
            AUREA_CHECK_EQ(track->keys[1].time.value, 30);
        }
        Command undo; undo.type = CommandType::Undo;
        AUREA_CHECK(engine.apply_command(undo).ok());
        track = comp->layer(rig.id)->tracks.find(static_cast<TrackProperty>(property));
        AUREA_CHECK(track && std::fabs(track->keys[0].value - 0.2f) < .001f);
    }
}

AUREA_TEST(Transform3D, CameraOffsetAndSceneOrbitKeepEndpoints) {
    Rig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    auto& e = rig.e;
    auto camera = e.add_camera(); AUREA_CHECK(camera.ok()); if (!camera.ok()) return;
    auto id = LayerId::unpack(*camera);
    auto* layer = e.project()->timeline().composition(e.project()->timeline().current())->layer(id);
    layer->start = FrameIndex{20}; layer->end = FrameIndex{100}; layer->offset = FrameIndex{7};
    auto anchor = layer->transform.anchor;
    editar(e, id, TrackProperty::PositionX, FrameIndex{7}, 100);
    editar(e, id, TrackProperty::PositionX, FrameIndex{67}, 300);
    seek(e, 50);
    auto before = detalhe(e, *camera); AUREA_CHECK_EQ(before.localPlayhead, 37);
    e.set_scene_editor(true, 75, -30, 4);
    auto orbit = detalhe(e, *camera);
    AUREA_CHECK_NEAR(orbit.position[0], before.position[0], .001f);
    editar(e, id, TrackProperty::PositionX, FrameIndex{orbit.localPlayhead}, 500);
    e.set_scene_editor(false, 0, 0, 3);
    seek(e, 20); AUREA_CHECK_NEAR(detalhe(e, *camera).position[0], 100, .001f);
    seek(e, 80); AUREA_CHECK_NEAR(detalhe(e, *camera).position[0], 300, .001f);
    seek(e, 50); AUREA_CHECK_NEAR(detalhe(e, *camera).position[0], 500, .001f);
    AUREA_CHECK_NEAR(layer->transform.anchor.x, anchor.x, .001f);
    AUREA_CHECK_NEAR(layer->transform.anchor.y, anchor.y, .001f);
    AUREA_CHECK_NEAR(layer->transform.anchor.z, anchor.z, .001f);
}

AUREA_TEST(Transform3D, XYZGroupPreservesOtherAxesEndpointsAndUndoOnShapeNullCamera) {
    Rig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    auto& e = rig.e;
    auto null3d = e.add_null(true), camera = e.add_camera();
    AUREA_CHECK(null3d.ok() && camera.ok()); if (!null3d.ok() || !camera.ok()) return;
    for (u64 id : {rig.id.pack(), *null3d, *camera}) {
        const auto lid = LayerId::unpack(id);
        auto* layer = e.project()->timeline().composition(e.project()->timeline().current())->layer(lid);
        layer->start = FrameIndex{10}; layer->offset = FrameIndex{5}; layer->end = FrameIndex{100};
        for (u32 base : {0u, 3u, 6u, 9u}) {
            const auto property = static_cast<TrackProperty>(base);
            editar(e, lid, property, FrameIndex{5}, 10);
            editar(e, lid, property, FrameIndex{65}, 30);
            for (bool scene : {false, true}) {
                e.set_scene_editor(scene, -30, 20, 3);
                seek(e, 40); const auto before = detalhe(e, id);
                AUREA_CHECK_EQ(before.localPlayhead, 35);
                const float* values = base == 0 ? before.position : base == 3 ? before.scale : base == 6 ? before.rotation : before.anchor;
                const float y = values[1], z = values[2];
                Command c; c.type = CommandType::UndoBeginGroup; AUREA_CHECK(e.apply_command(c).ok());
                editar(e, lid, property, FrameIndex{35}, 50);
                editar(e, lid, static_cast<TrackProperty>(base + 1), FrameIndex{35}, y);
                editar(e, lid, static_cast<TrackProperty>(base + 2), FrameIndex{35}, z);
                c.type = CommandType::UndoEndGroup; AUREA_CHECK(e.apply_command(c).ok());
                const auto keyed = detalhe(e, id);
                AUREA_CHECK_EQ(keyed.keyAtPlayheadMask & (7u << base), 7u << base);
                auto* current = e.project()->timeline().composition(e.project()->timeline().current())->layer(lid);
                const auto* track = current->tracks.find(property);
                AUREA_CHECK_NEAR(track->sample(FrameIndex{5}), 10, .001f);
                AUREA_CHECK_NEAR(track->sample(FrameIndex{65}), 30, .001f);
                AUREA_CHECK_NEAR(current->tracks.find(static_cast<TrackProperty>(base + 1))->sample(FrameIndex{35}), y, .001f);
                AUREA_CHECK_NEAR(current->tracks.find(static_cast<TrackProperty>(base + 2))->sample(FrameIndex{35}), z, .001f);
                c.type = CommandType::Undo; AUREA_CHECK(e.apply_command(c).ok());
                AUREA_CHECK_EQ(detalhe(e, id).keyAtPlayheadMask & (7u << base), 0u);
            }
        }
    }
}
