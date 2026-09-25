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
