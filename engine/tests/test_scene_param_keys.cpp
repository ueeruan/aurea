// Keyframes de MATERIAL (modelo 3D importado) e de LUZ editados pelo painel
// com a trilha JÁ animada. A regra é a mesma da lente da câmera
// (LayerSetCameraParam, Auto-Key do After Effects): trilha com keyframe →
// grava/atualiza o key no cabeçote; os outros keys ficam como estavam; sem
// trilha → muda o valor parado. Os painéis de material e de luz são abertos de
// DENTRO da cena 3D (SceneLayoutWorkspace / EditorView), com set_scene_editor
// ligado — a vista de navegação não pode mudar o significado do comando.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aurea;

namespace {

Composition* comp_of(Engine& e) { return e.project()->timeline().composition(e.project()->timeline().current()); }

void seek(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}

/// O losango do painel: KeyframeInsert na trilha (material = índice do
/// material + parâmetro; luz = kInvalidIndex + 0).
void key(Engine& e, u64 layer, TrackProperty p, u32 effect, u32 param, i64 frame, f32 value) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe.track = TrackRef{LayerId::unpack(layer), p, effect, param};
    c.keyframe.time = FrameIndex{frame};
    c.keyframe.value = value;
    AUREA_CHECK(e.apply_command(c).ok());
}

void print_keys(const char* label, const Track* t) {
    std::printf("    %s:", label);
    if (!t) { std::printf(" (sem trilha)\n"); return; }
    for (const Keyframe& k : t->keys) std::printf(" [%lld → %.3f]", static_cast<long long>(k.time.value), static_cast<double>(k.value));
    std::printf("  static=%.3f\n", static_cast<double>(t->staticValue));
}

struct ParamRig {
    Engine e;
    bool ok = false;
    ParamRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(320, 180, 30.0, nullptr).ok();
        if (ok) seek(e, 0);
    }
    ~ParamRig() { if (ok) e.shutdown(); }
};

/// OBJ escrito na hora (mesmo truque de test_scene3d): um quadrado com um
/// material .mtl — o bastante para o modelo ter `materials[0]`.
Result<u64> import_quad(Engine& e) {
    const std::string obj = "aurea_teste_param_keys.obj", mtl = "aurea_teste_param_keys.mtl";
    if (std::FILE* f = std::fopen(mtl.c_str(), "wb")) { std::fputs("newmtl vermelho\nKd 1.0 0.0 0.0\n", f); std::fclose(f); }
    if (std::FILE* f = std::fopen(obj.c_str(), "wb")) {
        std::fputs("mtllib aurea_teste_param_keys.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\n"
                   "usemtl vermelho\nf 1//1 2//1 3//1 4//1\n", f);
        std::fclose(f);
    }
    ModelImport mi; mi.path = obj;
    const auto r = e.import_model(mi);
    std::remove(obj.c_str()); std::remove(mtl.c_str());
    return r;
}

} // namespace

// Material metálico com keys em 0 (0.0) e 60 (1.0). Na cena 3D, no quadro 30,
// a régua manda 0.9 → esperado: key NOVO em 30 = 0.9; 0 e 60 intactos.
AUREA_TEST(SceneParamKeys, MaterialParamInsideSceneEditorKeysAtPlayheadKeepsOtherKeys) {
    ParamRig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    Engine& e = rig.e;
    const auto model = import_quad(e); AUREA_CHECK(model.ok()); if (!model.ok()) return;
    const u64 id = *model;
    AUREA_CHECK(e.query_materials(id, nullptr, 0) >= 1u);
    key(e, id, TrackProperty::MaterialParam, 0, 4, 0, 0.0f);
    key(e, id, TrackProperty::MaterialParam, 0, 4, 60, 1.0f);
    const Layer* l = comp_of(e)->layer(LayerId::unpack(id));
    AUREA_CHECK(l != nullptr); if (!l) return;

    e.set_scene_editor(true, -30, 20, 3);
    seek(e, 30);
    AUREA_CHECK(e.set_material_param(id, 0, 4, 0.9f).ok());
    const Track* t = l->tracks.find(TrackProperty::MaterialParam, 0, 4);
    print_keys("material metálico, cena LIGADA, régua 0.9 no quadro 30", t);
    AUREA_CHECK(t != nullptr); if (!t) return;
    AUREA_CHECK_EQ(t->keys.size(), 3u);
    AUREA_CHECK_NEAR(t->sample(FrameIndex{0}), 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(t->sample(FrameIndex{30}), 0.9f, 1e-4f);
    AUREA_CHECK_NEAR(t->sample(FrameIndex{60}), 1.0f, 1e-4f);
    f32 values[8]{};
    AUREA_CHECK_EQ(e.query_materials(id, values, 1), 1u);
    AUREA_CHECK_NEAR(values[2 + 4], 0.9f, 1e-4f);

    // A MESMA régua com a cena desligada: hoje o motor troca de comportamento.
    e.set_scene_editor(false, 0, 0, 3);
    seek(e, 30);
    AUREA_CHECK(e.set_material_param(id, 0, 4, 0.7f).ok());
    print_keys("material metálico, cena DESLIGADA, régua 0.7 no quadro 30", t);
    AUREA_CHECK_EQ(t->keys.size(), 3u);
    AUREA_CHECK_NEAR(t->sample(FrameIndex{30}), 0.7f, 1e-4f);
}

// Luz com intensidade em keys 0 (0.0) e 60 (6.0). No quadro 30 (valor 3.0) o
// campo manda 1.0 → esperado: key NOVO em 30 = 1.0; 0 e 60 intactos. Vale
// dentro e fora da cena 3D.
AUREA_TEST(SceneParamKeys, LightParamKeysAtPlayheadKeepsOtherKeys) {
    ParamRig rig; AUREA_CHECK(rig.ok); if (!rig.ok) return;
    Engine& e = rig.e;
    const auto light = e.add_light(1); AUREA_CHECK(light.ok()); if (!light.ok()) return;
    const u64 id = *light;
    const Layer* l = comp_of(e)->layer(LayerId::unpack(id));
    AUREA_CHECK(l != nullptr); if (!l) return;
    key(e, id, TrackProperty::LightIntensity, kInvalidIndex, 0, 0, 0.0f);
    key(e, id, TrackProperty::LightIntensity, kInvalidIndex, 0, 60, 6.0f);

    for (const bool scene : {false, true}) {
        e.set_scene_editor(scene, -30, 20, 3);
        seek(e, 30);
        Command c; c.type = CommandType::LayerSetLightParam;
        c.shape_param = ShapeParamPayload{LayerId::unpack(id), 1, 1.0f};
        AUREA_CHECK(e.apply_command(c).ok());
        const Track* t = l->tracks.find(TrackProperty::LightIntensity);
        print_keys(scene ? "luz intensidade, cena LIGADA, campo 1.0 no quadro 30" : "luz intensidade, cena DESLIGADA, campo 1.0 no quadro 30", t);
        AUREA_CHECK(t != nullptr); if (!t) return;
        AUREA_CHECK_EQ(t->keys.size(), 3u);
        AUREA_CHECK_NEAR(t->sample(FrameIndex{0}), 0.0f, 1e-4f);
        AUREA_CHECK_NEAR(t->sample(FrameIndex{30}), 1.0f, 1e-4f);
        AUREA_CHECK_NEAR(t->sample(FrameIndex{60}), 6.0f, 1e-4f);
        // Nenhum key pode ficar negativo: o comando recusa v < 0 direto.
        for (const Keyframe& k : t->keys) AUREA_CHECK(k.value >= 0.0f);
        f32 values[10]{};
        AUREA_CHECK(e.query_light(id, values));
        AUREA_CHECK_NEAR(values[1], 1.0f, 1e-4f);
        // Volta ao estado dos dois keys para a segunda rodada.
        if (!scene && t->keys.size() == 3) {
            Command del; del.type = CommandType::KeyframeDelete;
            del.keyframe.track = TrackRef{LayerId::unpack(id), TrackProperty::LightIntensity, kInvalidIndex, 0};
            del.keyframe.time = FrameIndex{30};
            (void)e.apply_command(del);
        }
    }
    e.set_scene_editor(false, 0, 0, 3);
}
