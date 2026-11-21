// =============================================================================
//  Painel do MODELO 3D (13/11/2026, EngineModelPanel.cpp): visuais do material
//  para o modelo inteiro, deslizantes de "todos os materiais", keyframes de
//  material num passo e a Evolução do clipe de animação (com keyframe),
//  Repetir e Velocidade — com a regra de sempre quando nada disso é tocado
//  (projeto antigo abre e toca igual) e a ida e volta pelo arquivo (v75).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/scene3d/Animation.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace aurea;

namespace {

struct PanelRig {
    Engine e;
    PanelRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~PanelRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    bool undo() {
        Command u;
        u.type = CommandType::Undo;
        return e.apply_command(u).ok();
    }
    void seek(i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    }
    std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
    Layer* model_layer() {
        Layer* found = nullptr;
        comp()->layers().for_each([&](LayerId, Layer& l) { if (l.kind == LayerKind::Model3D) found = &l; });
        return found;
    }
    u64 model_id() {
        u64 found = 0;
        comp()->layers().for_each([&](LayerId id, Layer& l) { if (l.kind == LayerKind::Model3D) found = id.pack(); });
        return found;
    }
    std::vector<f32> materials(u64 id) {
        const u32 n = e.query_materials(id, nullptr, 0);
        std::vector<f32> v(n * 8u);
        if (n) (void)e.query_materials(id, v.data(), n);
        return v;
    }
};

/// Dois triângulos glTF, cada um com um MATERIAL (vermelho fosco, azul meio
/// metal), e UM clipe "Girar" de 1 s (giro do nó 0).
std::string write_model(const std::string& path) {
    const f32 pos[9] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f};
    const u16 idx[3] = {0, 1, 2};
    const f32 times[2] = {0.0f, 1.0f};
    const f32 rot[8] = {0, 0, 0, 1, 0, 0, 0.7071068f, 0.7071068f};
    std::vector<u8> bin(84, 0);
    std::memcpy(bin.data(), pos, 36);
    std::memcpy(bin.data() + 36, idx, 6);
    std::memcpy(bin.data() + 44, times, 8);
    std::memcpy(bin.data() + 52, rot, 32);
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
    char json[4096];
    std::snprintf(json, sizeof(json),
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],"nodes":[{"mesh":0},{"mesh":1,"translation":[1,0,0]}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]},)"
        R"({"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":1}]}],)"
        R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"metallicFactor":0,"roughnessFactor":0.8}},)"
        R"({"pbrMetallicRoughness":{"baseColorFactor":[0,0,1,1],"metallicFactor":0.5,"roughnessFactor":0.5}}],)"
        R"("buffers":[{"byteLength":%u,"uri":"data:application/octet-stream;base64,%s"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},)"
        R"({"buffer":0,"byteOffset":44,"byteLength":8},{"buffer":0,"byteOffset":52,"byteLength":32}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},)"
        R"({"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},)"
        R"({"bufferView":3,"componentType":5126,"count":2,"type":"VEC4"}],)"
        R"("animations":[{"name":"Girar","samplers":[{"input":2,"output":3}],"channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}]}]})",
        static_cast<unsigned>(bin.size()), enc.c_str());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(json, 1, std::strlen(json), f);
        std::fclose(f);
    }
    return path;
}

} // namespace

// -----------------------------------------------------------------------------
// Animação: a regra de sempre, a Evolução com keyframe, Repetir e Velocidade
// -----------------------------------------------------------------------------

AUREA_TEST(ModelPanel, ClipTimeWithoutEvolutionIsTheOldLoop) {
    // Nada tocado (todo projeto antigo): o mesmo tempo de scene3d::clip_time.
    Layer l;
    l.kind = LayerKind::Model3D;
    scene3d::Animation clip;
    clip.duration = 1.7f;
    for (const f32 scale : {1.0f, 1.5f, 0.25f}) {
        l.model.timeScale = scale;
        for (const f64 frame : {0.0, 7.0, 29.5, 51.0, 300.25, -12.0}) {
            const f32 expected = scene3d::clip_time(clip, frame / 30.0 * scale);
            AUREA_CHECK_NEAR(model_clip_seconds(l, clip.duration, frame, 30.0), expected, 1e-5f);
        }
    }
    // Repetir desligado: para no fim (e não volta antes do começo).
    l.model.timeScale = 1.0f;
    l.model.animLoop = false;
    AUREA_CHECK_NEAR(model_clip_seconds(l, 1.7f, 90.0, 30.0), 1.7f, 1e-6f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, 1.7f, 30.0, 30.0), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, 1.7f, -5.0, 30.0), 0.0f, 1e-6f);
    // Evolução parada: a fração do clipe, sem laço, em qualquer instante.
    l.model.animProgress = 0.5f;
    AUREA_CHECK_NEAR(model_clip_seconds(l, 1.7f, 0.0, 30.0), 0.85f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, 1.7f, 999.0, 30.0), 0.85f, 1e-5f);
    // Keyframes mandam (interpolados entre quadros, também no sub-quadro).
    Track& t = l.tracks.get_or_create(TrackProperty::ModelAnimProgress, kInvalidIndex, 0);
    (void)t.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)t.set(FrameIndex{30}, 1.0f, Interpolation::Linear);
    AUREA_CHECK_NEAR(model_clip_seconds(l, 2.0f, 15.0, 30.0), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, 2.0f, 7.5, 30.0), 0.5f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, 2.0f, 30.0, 30.0), 2.0f, 1e-5f);   // 100 % = o fim, não volta a 0
    AUREA_CHECK_NEAR(model_clip_seconds(l, 0.0f, 15.0, 30.0), 0.0f, 1e-6f);   // clipe sem duração
}

AUREA_TEST(ModelPanel, EvolutionKeysRepeatAndSpeedAreUndoableAndSaved) {
    PanelRig r;
    ModelImport mi;
    mi.path = write_model("aurea_teste_painel_modelo.gltf");
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const u64 id = *model;
    f32 q[Engine::kModelAnimationValues]{};
    // Importar toca o 1º clipe sozinho, em laço (o de sempre).
    AUREA_CHECK(r.e.query_model_animation(id, q, Engine::kModelAnimationValues));
    AUREA_CHECK_EQ(q[0], 0.0f);
    AUREA_CHECK_NEAR(q[1], 1.0f, 1e-5f);
    AUREA_CHECK_EQ(q[2], -1.0f);
    AUREA_CHECK_EQ(q[4], 1.0f);
    AUREA_CHECK_EQ(q[5], 1.0f);
    AUREA_CHECK_EQ(q[6], 0.0f);
    AUREA_CHECK(!r.e.query_model_animation(id, q, 4));             // capacidade curta
    AUREA_CHECK(!r.e.set_model_animation_param(id, 6, 1.0f));      // parâmetro que não existe (v76: 3..5 existem)
    AUREA_CHECK(!r.e.set_model_animation_param(id, 0, NAN));

    // Valor parado: a pose fica no meio do clipe.
    AUREA_CHECK(r.e.set_model_animation_param(id, 0, 0.5f));
    AUREA_CHECK_NEAR(r.L(id)->model.animProgress, 0.5f, 1e-6f);
    AUREA_CHECK(r.e.query_model_animation(id, q, Engine::kModelAnimationValues));
    AUREA_CHECK_NEAR(q[3], 0.5f, 1e-5f);
    AUREA_CHECK(r.e.set_model_animation_param(id, 0, 7.0f));       // presa em 100 %
    AUREA_CHECK_NEAR(r.L(id)->model.animProgress, 1.0f, 1e-6f);
    AUREA_CHECK(r.undo());
    AUREA_CHECK_NEAR(r.L(id)->model.animProgress, 0.5f, 1e-6f);

    // Keyframe no quadro 0 (o valor da prévia: 50 %), régua no quadro 30 = key novo.
    r.seek(0);
    AUREA_CHECK(r.e.toggle_model_animation_key(id));
    r.seek(30);
    AUREA_CHECK(r.e.set_model_animation_param(id, 0, 1.0f));
    const Track* t = r.L(id)->tracks.find(TrackProperty::ModelAnimProgress);
    AUREA_CHECK(t != nullptr);
    if (!t) return;
    AUREA_CHECK_EQ(t->keys.size(), static_cast<usize>(2));
    AUREA_CHECK_NEAR(t->sample(FrameIndex{0}), 0.5f, 1e-5f);
    AUREA_CHECK_NEAR(t->sample(FrameIndex{30}), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(*r.L(id), 1.0f, 15.0, 30.0), 0.75f, 1e-5f);
    AUREA_CHECK(r.e.query_model_animation(id, q, Engine::kModelAnimationValues));
    AUREA_CHECK_EQ(q[6], 1.0f);
    AUREA_CHECK_EQ(q[7], 1.0f);
    // O 2º toque no mesmo quadro tira o keyframe; o último que sai deixa a pose parada.
    AUREA_CHECK(r.e.toggle_model_animation_key(id));
    AUREA_CHECK_EQ(r.L(id)->tracks.find(TrackProperty::ModelAnimProgress)->keys.size(), static_cast<usize>(1));
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.L(id)->tracks.find(TrackProperty::ModelAnimProgress)->keys.size(), static_cast<usize>(2));

    // Repetir e Velocidade.
    AUREA_CHECK(r.e.set_model_animation_param(id, 1, 0.0f));
    AUREA_CHECK(!r.L(id)->model.animLoop);
    AUREA_CHECK(r.e.set_model_animation_param(id, 2, 40.0f));      // presa em 16×
    AUREA_CHECK_NEAR(r.L(id)->model.timeScale, 16.0f, 1e-6f);
    AUREA_CHECK(r.e.set_model_animation_param(id, 2, 1.5f));

    // Ida e volta pelo arquivo (v75): trilha, valor parado, Repetir e Velocidade.
    const std::string path = r.temp("aurea_teste_painel_modelo.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* loaded = r.model_layer();
    AUREA_CHECK(loaded != nullptr);
    if (loaded) {
        AUREA_CHECK_NEAR(loaded->model.animProgress, 0.5f, 1e-6f);
        AUREA_CHECK(!loaded->model.animLoop);
        AUREA_CHECK_NEAR(loaded->model.timeScale, 1.5f, 1e-6f);
        const Track* lt = loaded->tracks.find(TrackProperty::ModelAnimProgress);
        AUREA_CHECK(lt && lt->keys.size() == 2);
        AUREA_CHECK_NEAR(model_clip_seconds(*loaded, 1.0f, 15.0, 30.0), 0.75f, 1e-5f);
    }

    // "Tocar sozinho": tira valor parado e keyframes — de volta ao laço de sempre.
    const u64 reloaded = r.model_id();
    AUREA_CHECK(r.e.set_model_animation_param(reloaded, 0, -1.0f));
    const Layer* back = r.model_layer();
    AUREA_CHECK(back && back->model.animProgress < 0.0f && !back->tracks.find(TrackProperty::ModelAnimProgress));
    // Travada: recusa. Camada que não é modelo: recusa.
    r.model_layer()->locked = true;
    AUREA_CHECK(!r.e.set_model_animation_param(reloaded, 1, 1.0f));
    AUREA_CHECK(!r.e.toggle_model_animation_key(reloaded));
    const auto shape = r.e.add_shape(0);
    AUREA_CHECK(shape.ok());
    if (shape.ok()) {
        AUREA_CHECK(!r.e.set_model_animation_param(*shape, 0, 0.5f));
        AUREA_CHECK(!r.e.query_model_animation(*shape, q, Engine::kModelAnimationValues));
    }
    std::remove(path.c_str());
    std::remove(mi.path.c_str());
}

AUREA_TEST(ModelPanel, UntouchedModelSavesAsBeforeAndKeepsTheLoop) {
    PanelRig r;
    ModelImport mi;
    mi.path = write_model("aurea_teste_painel_modelo_antigo.gltf");
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const std::string path = r.temp("aurea_teste_painel_modelo_antigo.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    Project saved;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load(saved, path.c_str(), LoadOptions{}, &report).ok());
    AUREA_CHECK_EQ(report.timelineVersion, kTimelineSectionVersion);
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* l = r.model_layer();
    AUREA_CHECK(l != nullptr);
    if (l) {
        AUREA_CHECK(l->model.animProgress < 0.0f);
        AUREA_CHECK(l->model.animLoop);
        AUREA_CHECK_EQ(l->model.timeScale, 1.0f);
        AUREA_CHECK(!l->tracks.find(TrackProperty::ModelAnimProgress));
        scene3d::Animation clip;
        clip.duration = 1.0f;
        AUREA_CHECK_NEAR(model_clip_seconds(*l, 1.0f, 45.0, 30.0), scene3d::clip_time(clip, 1.5), 1e-6f);
    }
    std::remove(path.c_str());
    std::remove(mi.path.c_str());
}

// -----------------------------------------------------------------------------
// Material: visuais do modelo inteiro, "todos os materiais" e keyframes
// -----------------------------------------------------------------------------

AUREA_TEST(ModelPanel, MaterialLooksCoverTheWholeModelAndOriginalRestores) {
    PanelRig r;
    ModelImport mi;
    mi.path = write_model("aurea_teste_painel_material.gltf");
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const u64 id = *model;
    std::vector<f32> m = r.materials(id);
    AUREA_CHECK_EQ(m.size(), static_cast<usize>(16));
    if (m.size() != 16) return;
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 0);              // o arquivo como veio

    // Cromo: os dois materiais prateados, metal 1, rugosidade 0,05.
    AUREA_CHECK(r.e.apply_model_material_preset(id, 4));
    m = r.materials(id);
    for (u32 i = 0; i < 2; ++i) {
        AUREA_CHECK_NEAR(m[i * 8 + 2], 0.95f, 1e-4f);
        AUREA_CHECK_NEAR(m[i * 8 + 6], 1.0f, 1e-4f);
        AUREA_CHECK_NEAR(m[i * 8 + 7], 0.05f, 1e-4f);
    }
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 4);
    // Plástico depois do Cromo: a cor volta a ser a do arquivo (não fica prateado).
    AUREA_CHECK(r.e.apply_model_material_preset(id, 1));
    m = r.materials(id);
    AUREA_CHECK_NEAR(m[2], 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(m[8 + 4], 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(m[6], 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(m[8 + 7], 0.35f, 1e-4f);
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 1);
    // Vidro: opacidade 35 %.
    AUREA_CHECK(r.e.apply_model_material_preset(id, 6));
    m = r.materials(id);
    AUREA_CHECK_NEAR(m[5], 0.35f, 1e-4f);
    AUREA_CHECK_NEAR(m[8 + 5], 0.35f, 1e-4f);

    // Deslizante do modelo inteiro: um comando, os dois materiais; o visual vira "ajustado".
    AUREA_CHECK(r.e.set_material_param(id, Engine::kAllMaterials, 4, 0.7f).ok());
    m = r.materials(id);
    AUREA_CHECK_NEAR(m[6], 0.7f, 1e-4f);
    AUREA_CHECK_NEAR(m[8 + 6], 0.7f, 1e-4f);
    AUREA_CHECK_EQ(r.e.model_material_preset(id), -1);
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 6);

    // Original: o arquivo de volta.
    AUREA_CHECK(r.e.apply_model_material_preset(id, 0));
    m = r.materials(id);
    AUREA_CHECK_NEAR(m[2], 1.0f, 1e-4f);
    AUREA_CHECK_NEAR(m[6], 0.0f, 1e-4f);
    AUREA_CHECK_NEAR(m[7], 0.8f, 1e-4f);
    AUREA_CHECK_NEAR(m[8 + 6], 0.5f, 1e-4f);
    AUREA_CHECK_NEAR(m[8 + 5], 1.0f, 1e-4f);
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 0);
    AUREA_CHECK(r.L(id)->model.materials.empty());
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 6);

    // A bola de cada visual neste modelo; fora da faixa, nada.
    std::vector<u8> ball;
    for (u32 p = 0; p < Engine::kModelMaterialPresets; ++p) {
        AUREA_CHECK(r.e.model_material_preset_preview(id, p, 32, ball));
        AUREA_CHECK_EQ(ball.size(), static_cast<usize>(32 * 32 * 4));
    }
    AUREA_CHECK(!r.e.model_material_preset_preview(id, Engine::kModelMaterialPresets, 32, ball));
    AUREA_CHECK(!r.e.apply_model_material_preset(id, Engine::kModelMaterialPresets));
    std::remove(mi.path.c_str());
}

AUREA_TEST(ModelPanel, MaterialKeysForTheWholeModelInOneStep) {
    PanelRig r;
    ModelImport mi;
    mi.path = write_model("aurea_teste_painel_material_keys.gltf");
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const u64 id = *model;
    // ◆ do Metal no quadro 0: um keyframe em cada material, com o valor de cada um.
    r.seek(0);
    AUREA_CHECK(r.e.toggle_material_keys(id, Engine::kAllMaterials, 16u));
    const Track* a = r.L(id)->tracks.find(TrackProperty::MaterialParam, 0, 4);
    const Track* b = r.L(id)->tracks.find(TrackProperty::MaterialParam, 1, 4);
    AUREA_CHECK(a && b && a->keys.size() == 1 && b->keys.size() == 1);
    if (!a || !b) return;
    AUREA_CHECK_NEAR(a->keys[0].value, 0.0f, 1e-5f);
    AUREA_CHECK_NEAR(b->keys[0].value, 0.5f, 1e-5f);
    // Régua no quadro 30 com keyframe: key novo nos dois.
    r.seek(30);
    AUREA_CHECK(r.e.set_material_param(id, Engine::kAllMaterials, 4, 1.0f).ok());
    AUREA_CHECK_EQ(a->keys.size(), static_cast<usize>(2));
    AUREA_CHECK_EQ(b->keys.size(), static_cast<usize>(2));
    // Um visual com o metal animado: o keyframe no cabeçote recebe o valor do visual.
    AUREA_CHECK(r.e.apply_model_material_preset(id, 2));          // Fosco: metal 0
    AUREA_CHECK_NEAR(a->sample(FrameIndex{30}), 0.0f, 1e-5f);
    AUREA_CHECK_NEAR(b->sample(FrameIndex{30}), 0.0f, 1e-5f);
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 2);
    // ◆ de novo no quadro 30: tira dos dois num passo; desfazer devolve.
    AUREA_CHECK(r.e.toggle_material_keys(id, Engine::kAllMaterials, 16u));
    AUREA_CHECK_EQ(a->keys.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(b->keys.size(), static_cast<usize>(1));
    AUREA_CHECK(r.undo());
    a = r.L(id)->tracks.find(TrackProperty::MaterialParam, 0, 4);
    AUREA_CHECK(a && a->keys.size() == 2);
    // Um material só, e a cor (três canais) de uma vez.
    r.seek(10);
    AUREA_CHECK(r.e.toggle_material_keys(id, 1, 7u));
    for (u32 p = 0; p < 3; ++p) {
        const Track* c = r.L(id)->tracks.find(TrackProperty::MaterialParam, 1, p);
        AUREA_CHECK(c && c->keys.size() == 1);
        AUREA_CHECK(!r.L(id)->tracks.find(TrackProperty::MaterialParam, 0, p));
    }
    AUREA_CHECK(!r.e.toggle_material_keys(id, 2, 7u));             // não existe
    AUREA_CHECK(!r.e.toggle_material_keys(id, 0, 0u));
    // Original tira os keyframes de material também.
    AUREA_CHECK(r.e.apply_model_material_preset(id, 0));
    AUREA_CHECK(!r.L(id)->tracks.find(TrackProperty::MaterialParam, 0, 4));
    AUREA_CHECK_EQ(r.e.model_material_preset(id), 0);
    std::remove(mi.path.c_str());
}

// =============================================================================
// 3D opções extras (14/11/2026, v76): Começar em, De trás para frente, Ida e
// volta e Velocidade com keyframe; EMISSÃO por material (e o visual Neon);
// posição do modelo (assentar, centralizar, ajustar à tela).
// =============================================================================

AUREA_TEST(ModelPanel, StartReversePingPongAndKeyedSpeedFollowOneRule) {
    Layer l;
    l.kind = LayerKind::Model3D;
    const f32 d = 2.0f;
    // Começar em 25 %: no quadro 0 o clipe está em 0,5 s; em laço, segue de lá.
    l.model.animStart = 0.25f;
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 0.0, 30.0), 0.5f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 30.0, 30.0), 1.5f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 60.0, 30.0), 0.5f, 1e-5f);   // 2,5 s → laço
    l.model.animStart = 0.0f;
    // De trás para frente: começa no fim e corre para o começo.
    l.model.animReverse = true;
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 0.0, 30.0), 2.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 15.0, 30.0), 1.5f, 1e-5f);
    l.model.animLoop = false;
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 300.0, 30.0), 0.0f, 1e-6f);   // para no começo
    l.model.animLoop = true;
    l.model.animReverse = false;
    // Ida e volta: 0 → 2 s → 0 em 4 s; sem Repetir, para de volta no começo.
    l.model.animPingPong = true;
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 30.0, 30.0), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 60.0, 30.0), 2.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 90.0, 30.0), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 135.0, 30.0), 0.5f, 1e-5f);   // 4,5 s = 0,5 s de novo
    l.model.animLoop = false;
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 90.0, 30.0), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 999.0, 30.0), 0.0f, 1e-6f);
    l.model.animLoop = true;
    l.model.animPingPong = false;
    // Velocidade com keyframe: 2× parada em keyframe = a mesma conta da escala 2×.
    Layer fixed = l;
    fixed.model.timeScale = 2.0f;
    Track& s = l.tracks.get_or_create(TrackProperty::ModelAnimSpeed, kInvalidIndex, 0);
    (void)s.set(FrameIndex{0}, 2.0f, Interpolation::Linear);
    for (const f64 frame : {0.0, 7.0, 20.5, 33.0})
        AUREA_CHECK_NEAR(model_clip_seconds(l, d, frame, 30.0), model_clip_seconds(fixed, d, frame, 30.0), 1e-4f);
    // Rampa 0 → 2× em 30 quadros: a integral (½ · 2 · 1 s = 1 s), sem pulo.
    s.keys.clear();
    (void)s.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)s.set(FrameIndex{30}, 2.0f, Interpolation::Linear);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 30.0, 30.0), 1.0f, 1e-3f);
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 15.0, 30.0), 0.25f, 1e-3f);
    // A Evolução continua mandando por cima de tudo.
    l.model.animReverse = true;
    l.model.animProgress = 0.5f;
    AUREA_CHECK_NEAR(model_clip_seconds(l, d, 15.0, 30.0), 1.0f, 1e-5f);
}

AUREA_TEST(ModelPanel, AnimationExtrasAreUndoableKeyedAndSavedInV76) {
    PanelRig r;
    ModelImport mi;
    mi.path = write_model("aurea_teste_painel_extras.gltf");
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const u64 id = *model;
    f32 q[Engine::kModelAnimationValuesV76]{};
    AUREA_CHECK(r.e.query_model_animation(id, q, Engine::kModelAnimationValuesV76));
    AUREA_CHECK_EQ(q[8], 0.0f);
    AUREA_CHECK_EQ(q[9], 0.0f);
    AUREA_CHECK_EQ(q[10], 0.0f);
    AUREA_CHECK_EQ(q[11], 0.0f);
    AUREA_CHECK_NEAR(q[13], 1.0f, 1e-6f);
    AUREA_CHECK(!r.e.set_model_animation_param(id, 6, 1.0f));
    AUREA_CHECK(r.e.set_model_animation_param(id, 3, 0.4f));
    AUREA_CHECK(r.e.set_model_animation_param(id, 4, 1.0f));
    AUREA_CHECK(r.e.set_model_animation_param(id, 5, 1.0f));
    AUREA_CHECK_NEAR(r.L(id)->model.animStart, 0.4f, 1e-6f);
    AUREA_CHECK(r.L(id)->model.animReverse && r.L(id)->model.animPingPong);
    AUREA_CHECK(r.undo());
    AUREA_CHECK(!r.L(id)->model.animPingPong);
    AUREA_CHECK(r.L(id)->model.animReverse);
    AUREA_CHECK(r.e.set_model_animation_param(id, 3, 7.0f));      // preso em 100 %
    AUREA_CHECK_NEAR(r.L(id)->model.animStart, 1.0f, 1e-6f);
    AUREA_CHECK(r.e.set_model_animation_param(id, 3, 0.4f));

    // ◆ da Velocidade: quadro 0 leva a velocidade parada; a régua no quadro 30 vira keyframe.
    AUREA_CHECK(!r.e.toggle_model_animation_param_key(id, 1));    // Repetir não tem ◆
    r.seek(0);
    AUREA_CHECK(r.e.set_model_animation_param(id, 2, 1.5f));
    AUREA_CHECK(r.e.toggle_model_animation_param_key(id, 2));
    r.seek(30);
    AUREA_CHECK(r.e.set_model_animation_param(id, 2, 3.0f));
    const Track* sp = r.L(id)->tracks.find(TrackProperty::ModelAnimSpeed);
    AUREA_CHECK(sp && sp->keys.size() == 2);
    if (!sp) return;
    AUREA_CHECK_NEAR(sp->sample(FrameIndex{0}), 1.5f, 1e-5f);
    AUREA_CHECK_NEAR(sp->sample(FrameIndex{30}), 3.0f, 1e-5f);
    AUREA_CHECK(r.e.query_model_animation(id, q, Engine::kModelAnimationValuesV76));
    AUREA_CHECK_EQ(q[11], 1.0f);
    AUREA_CHECK_EQ(q[12], 1.0f);
    AUREA_CHECK_NEAR(q[13], 3.0f, 1e-5f);
    // Capacidade antiga (8) continua valendo.
    f32 old[Engine::kModelAnimationValues]{};
    AUREA_CHECK(r.e.query_model_animation(id, old, Engine::kModelAnimationValues));
    // O ◆ da Evolução segue pelo caminho genérico também.
    AUREA_CHECK(r.e.toggle_model_animation_param_key(id, 0));
    AUREA_CHECK(r.L(id)->tracks.find(TrackProperty::ModelAnimProgress) != nullptr);
    AUREA_CHECK(r.undo());

    // Ida e volta pelo arquivo (v76): Começar em, De trás para frente e a trilha da Velocidade.
    const std::string path = r.temp("aurea_teste_painel_extras.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    Project saved;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load(saved, path.c_str(), LoadOptions{}, &report).ok());
    AUREA_CHECK_EQ(report.timelineVersion, kTimelineSectionVersion);
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* loaded = r.model_layer();
    AUREA_CHECK(loaded != nullptr);
    if (loaded) {
        AUREA_CHECK_NEAR(loaded->model.animStart, 0.4f, 1e-6f);
        AUREA_CHECK(loaded->model.animReverse);
        AUREA_CHECK(!loaded->model.animPingPong);
        const Track* ls = loaded->tracks.find(TrackProperty::ModelAnimSpeed);
        AUREA_CHECK(ls && ls->keys.size() == 2);
    }
    // Tirar o último ◆ da Velocidade deixa a velocidade parada nele.
    const u64 again = r.model_id();
    r.seek(30);
    AUREA_CHECK(r.e.toggle_model_animation_param_key(again, 2));
    r.seek(0);
    AUREA_CHECK(r.e.toggle_model_animation_param_key(again, 2));
    AUREA_CHECK(!r.model_layer()->tracks.find(TrackProperty::ModelAnimSpeed));
    AUREA_CHECK_NEAR(r.model_layer()->model.timeScale, 1.5f, 1e-5f);
    std::remove(path.c_str());
    std::remove(mi.path.c_str());
}

AUREA_TEST(ModelPanel, UnlitColorsSurviveUndoAndProjectReload) {
    PanelRig rig;
    ModelImport source;
    source.path = write_model("aurea_test_model_unlit_colors.gltf");
    const auto model = rig.e.import_model(source);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    AUREA_CHECK(!rig.e.model_unlit_colors(*model));
    AUREA_CHECK(rig.e.set_model_unlit_colors(*model, true));
    AUREA_CHECK(rig.e.model_unlit_colors(*model));
    AUREA_CHECK(rig.undo());
    AUREA_CHECK(!rig.e.model_unlit_colors(*model));
    AUREA_CHECK(rig.e.set_model_unlit_colors(*model, true));
    const std::string path = rig.temp("aurea_test_model_unlit_colors.aurea");
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.model_unlit_colors(rig.model_id()));
    std::remove(path.c_str());
    std::remove(source.path.c_str());
}

AUREA_TEST(ModelPanel, EmissionChannelsNeonLookAndV76RoundTrip) {
    PanelRig r;
    ModelImport mi;
    mi.path = write_model("aurea_teste_painel_emissao.gltf");
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const u64 id = *model;
    f32 em[4]{};
    AUREA_CHECK(r.e.query_material_emission(id, Engine::kAllMaterials, em, 4));
    AUREA_CHECK_EQ(em[0], 0.0f);
    AUREA_CHECK_NEAR(em[3], 1.0f, 1e-6f);                          // a força do glTF
    AUREA_CHECK(!r.e.query_material_emission(id, 7, em, 4));
    AUREA_CHECK(!r.e.query_material_emission(id, 0, em, 3));
    // Cor da emissão no modelo inteiro e força acima de 1; fora da faixa: recusa.
    AUREA_CHECK(r.e.set_material_param(id, Engine::kAllMaterials, 6, 1.0f).ok());
    AUREA_CHECK(r.e.set_material_param(id, Engine::kAllMaterials, 7, 0.5f).ok());
    AUREA_CHECK(r.e.set_material_param(id, Engine::kAllMaterials, 9, 4.0f).ok());
    AUREA_CHECK(!r.e.set_material_param(id, 0, 6, 1.5f).ok());
    AUREA_CHECK(!r.e.set_material_param(id, 0, 9, kMaterialEmissionMax + 1.0f).ok());
    AUREA_CHECK(!r.e.set_material_param(id, 0, kMaterialParamCount, 0.5f).ok());
    AUREA_CHECK(r.e.query_material_emission(id, 1, em, 4));
    AUREA_CHECK_NEAR(em[0], 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(em[1], 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(em[3], 4.0f, 1e-6f);
    AUREA_CHECK_EQ(r.e.model_material_preset(id), -1);
    // ◆ da força no modelo inteiro (bit 9), num passo; o keyframe passa de 1.
    r.seek(0);
    AUREA_CHECK(r.e.toggle_material_keys(id, Engine::kAllMaterials, 1u << 9));
    const Track* st = r.L(id)->tracks.find(TrackProperty::MaterialParam, 1, 9);
    AUREA_CHECK(st && st->keys.size() == 1 && std::fabs(st->keys[0].value - 4.0f) < 1e-5f);
    r.seek(30);
    AUREA_CHECK(r.e.set_material_param(id, Engine::kAllMaterials, 9, 8.0f).ok());
    AUREA_CHECK(st && st->keys.size() == 2);
    r.seek(15);
    AUREA_CHECK(r.e.query_material_emission(id, 0, em, 4));
    AUREA_CHECK_NEAR(em[3], 6.0f, 1e-3f);

    // Ida e volta (v76): a emissão parada e o keyframe da força.
    const std::string path = r.temp("aurea_teste_painel_emissao.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* loaded = r.model_layer();
    AUREA_CHECK(loaded != nullptr);
    if (loaded) {
        u32 glowing = 0;
        for (const MaterialOverride& o : loaded->model.materials) {
            if ((o.mask & kMaterialEmissionMask) != (64u | 128u | 512u)) continue;
            ++glowing;
            AUREA_CHECK_NEAR(o.emissive.x, 1.0f, 1e-6f);
            AUREA_CHECK_NEAR(o.emissive.y, 0.5f, 1e-6f);
            AUREA_CHECK_NEAR(o.emissiveStrength, 8.0f, 1e-6f);   // a régua com ◆ também grava o valor parado (a regra de sempre)
        }
        AUREA_CHECK_EQ(glowing, 2u);
        AUREA_CHECK(loaded->tracks.find(TrackProperty::MaterialParam, 0, 9) != nullptr);
    }
    const u64 reloaded = r.model_id();
    // Neon: corpo escuro e emissão ciano; Original tira tudo (inclusive a emissão).
    AUREA_CHECK(r.e.apply_model_material_preset(reloaded, 7));
    AUREA_CHECK_EQ(r.e.model_material_preset(reloaded), 7);
    AUREA_CHECK(r.e.query_material_emission(reloaded, 0, em, 4));
    AUREA_CHECK_NEAR(em[2], 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(em[3], 1.2f, 1e-3f);
    std::vector<u8> neon, original;
    AUREA_CHECK(r.e.model_material_preset_preview(reloaded, 7, 32, neon));
    AUREA_CHECK(r.e.model_material_preset_preview(reloaded, 0, 32, original));
    AUREA_CHECK(neon.size() == original.size() && neon != original);
    AUREA_CHECK(r.e.apply_model_material_preset(reloaded, 0));
    AUREA_CHECK(r.e.query_material_emission(reloaded, 0, em, 4));
    AUREA_CHECK_EQ(em[0], 0.0f);
    AUREA_CHECK(r.model_layer()->model.materials.empty());
    std::remove(path.c_str());
    std::remove(mi.path.c_str());
}

AUREA_TEST(ModelPanel, PlacementSitsCentersAndFitsInOneUndoStepEach) {
    PanelRig r;
    ModelImport mi;
    mi.path = write_model("aurea_teste_painel_posicao.gltf");
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const u64 id = *model;
    // Clipe parado: a caixa do arquivo é a mesma em qualquer quadro.
    AUREA_CHECK(r.e.set_model_animation_clip(id, -1));
    auto box = [&](f32 out[6]) { return r.e.query_scene_bounds(&id, 1, out); };
    f32 b[6]{};
    AUREA_CHECK(box(b));
    const Vec3 start = r.L(id)->transform.position;
    // Ajustar à tela: a silhueta ocupa 80 % do quadro (o lado que limita) e fica no centro.
    AUREA_CHECK(r.e.place_model_layer(id, 2));
    AUREA_CHECK(box(b));
    const f32 ex = b[3] - b[0], ey = b[4] - b[1];
    AUREA_CHECK(ex <= 0.8f * 320.0f + 0.5f && ey <= 0.8f * 180.0f + 0.5f);
    AUREA_CHECK(std::fabs(ex - 0.8f * 320.0f) < 0.5f || std::fabs(ey - 0.8f * 180.0f) < 0.5f);
    AUREA_CHECK_NEAR((b[0] + b[3]) * 0.5f, 160.0f, 0.05f);
    AUREA_CHECK_NEAR((b[1] + b[4]) * 0.5f, 90.0f, 0.05f);
    AUREA_CHECK_NEAR(r.L(id)->transform.scale.x, 1.0f, 1e-6f);    // a escala da camada não muda
    // Assentar no chão (sem outro modelo): a base em 90 % da altura, X intacto.
    const f32 cx = (b[0] + b[3]) * 0.5f;
    AUREA_CHECK(r.e.place_model_layer(id, 0));
    AUREA_CHECK(box(b));
    AUREA_CHECK_NEAR(b[4], 0.9f * 180.0f, 0.05f);
    AUREA_CHECK_NEAR((b[0] + b[3]) * 0.5f, cx, 0.05f);
    // Centralizar: de volta ao centro; um passo de desfazer cada.
    AUREA_CHECK(r.e.place_model_layer(id, 1));
    AUREA_CHECK(box(b));
    AUREA_CHECK_NEAR((b[1] + b[4]) * 0.5f, 90.0f, 0.05f);
    AUREA_CHECK(r.undo());
    AUREA_CHECK(box(b));
    AUREA_CHECK_NEAR(b[4], 0.9f * 180.0f, 0.05f);
    AUREA_CHECK(r.undo());
    AUREA_CHECK(r.undo());
    AUREA_CHECK_NEAR(r.L(id)->transform.position.x, start.x, 1e-4f);
    AUREA_CHECK_NEAR(r.L(id)->transform.position.y, start.y, 1e-4f);
    // Posição com keyframes: todos andam juntos (a animação fica igual, no lugar novo).
    Command key;
    key.type = CommandType::KeyframeInsert;
    key.keyframe.track.layer = LayerId::unpack(id);
    key.keyframe.track.property = TrackProperty::PositionY;
    key.keyframe.track.effectIndex = kInvalidIndex;
    key.keyframe.track.effectParamIndex = 0;
    key.keyframe.time = FrameIndex{0};
    key.keyframe.value = 20.0f;
    key.keyframe.onlyIfChanged = 0;
    AUREA_CHECK(r.e.apply_command(key).ok());
    key.keyframe.time = FrameIndex{30};
    key.keyframe.value = 60.0f;
    AUREA_CHECK(r.e.apply_command(key).ok());
    r.seek(0);
    AUREA_CHECK(r.e.place_model_layer(id, 1));
    const Track* py = r.L(id)->tracks.find(TrackProperty::PositionY);
    AUREA_CHECK(py && py->keys.size() == 2);
    if (py) AUREA_CHECK_NEAR(py->keys[1].value - py->keys[0].value, 40.0f, 1e-4f);
    AUREA_CHECK(box(b));
    AUREA_CHECK_NEAR((b[1] + b[4]) * 0.5f, 90.0f, 0.05f);
    // Travada ou que não é modelo: recusa.
    AUREA_CHECK(!r.e.place_model_layer(id, 3));
    r.L(id)->locked = true;
    AUREA_CHECK(!r.e.place_model_layer(id, 1));
    const auto shape = r.e.add_shape(0);
    AUREA_CHECK(shape.ok());
    if (shape.ok()) AUREA_CHECK(!r.e.place_model_layer(*shape, 1));
    std::remove(mi.path.c_str());
}
