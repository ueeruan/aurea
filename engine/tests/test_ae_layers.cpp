// =============================================================================
//  AE layers (2026-11-12): qualidade da camada (Best/Draft/Wireframe e
//  amostragem bicúbica), clipe de animação do modelo 3D, transições de
//  entrada/saída por camada (as linhas novas das duas interfaces chamam
//  `set_transition`), guias da prévia (B10), o trim morto das formas
//  paramétricas (B4) e a velocidade negativa nas rampas (B8). O AE é só a
//  especificação de comportamento.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/project/Serialization.hpp"
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

struct LayersRig {
    Engine e;
    LayersRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~LayersRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 shape() {
        const auto id = e.add_shape(0);
        AUREA_CHECK(id.ok());
        return id.ok() ? *id : 0;
    }
    std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
    Layer* named(const char* name) {
        Layer* found = nullptr;
        comp()->layers().for_each([&](LayerId, Layer& l) { if (l.name == name) found = &l; });
        return found;
    }
    u32 row_flags(u64 id) {
        bridge::LayerRow rows[16]{};
        const u32 n = e.query_layers(rows, 16, nullptr, 0);
        for (u32 i = 0; i < n && i < 16; ++i) if (rows[i].id == id) return rows[i].flags;
        return 0;
    }
    bool undo() {
        Command u;
        u.type = CommandType::Undo;
        return e.apply_command(u).ok();
    }
};

/// Um triângulo glTF com DUAS animações (giro do nó 0): "Andar" e uma sem nome.
std::string write_two_clip_model(const std::string& path) {
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
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
        R"("buffers":[{"byteLength":%u,"uri":"data:application/octet-stream;base64,%s"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},)"
        R"({"buffer":0,"byteOffset":44,"byteLength":8},{"buffer":0,"byteOffset":52,"byteLength":32}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},)"
        R"({"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},)"
        R"({"bufferView":3,"componentType":5126,"count":2,"type":"VEC4"}],)"
        R"("animations":[{"name":"Andar","samplers":[{"input":2,"output":3}],"channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}]},)"
        R"({"samplers":[{"input":2,"output":3}],"channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}]}]})",
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
// 1 · Qualidade (Best / Draft / Wireframe) e amostragem
// -----------------------------------------------------------------------------
AUREA_TEST(AeLayers, QualityIsUndoableRefusesBadValuesAndShowsInTheRow) {
    LayersRig r;
    const u64 a = r.shape();
    if (!a) return;
    // Padrão: Melhor + bilinear (projeto antigo abre igual).
    AUREA_CHECK_EQ(r.L(a)->quality, u8{0});
    AUREA_CHECK_EQ(r.L(a)->sampling, u8{0});
    AUREA_CHECK_EQ(r.row_flags(a) & (bridge::kLayerRowQualityMask | bridge::kLayerRowFlagBicubic), 0u);
    AUREA_CHECK(!r.e.set_layer_quality(a, 3, 0));
    AUREA_CHECK(!r.e.set_layer_quality(a, 0, 2));
    AUREA_CHECK(!r.e.set_layer_quality(0xDEADull, 1, 0));
    AUREA_CHECK(r.e.set_layer_quality(a, 1, 0));
    AUREA_CHECK_EQ(r.L(a)->quality, u8{1});
    AUREA_CHECK_EQ((r.row_flags(a) & bridge::kLayerRowQualityMask) >> bridge::kLayerRowQualityShift, 1u);
    AUREA_CHECK(r.e.set_layer_quality(a, 2, 1));
    AUREA_CHECK_EQ((r.row_flags(a) & bridge::kLayerRowQualityMask) >> bridge::kLayerRowQualityShift, 2u);
    AUREA_CHECK((r.row_flags(a) & bridge::kLayerRowFlagBicubic) != 0);
    // Os bits não pisam nos do Shy/Preservar (16, 17).
    AUREA_CHECK((r.row_flags(a) & (bridge::kLayerRowFlagShy | bridge::kLayerRowFlagPreserveTransparency)) == 0);
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.L(a)->quality, u8{1});
    AUREA_CHECK_EQ(r.L(a)->sampling, u8{0});
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.L(a)->quality, u8{0});
}

AUREA_TEST(AeLayers, QualityRoundTripsThroughTheProjectFile) {
    LayersRig r;
    const u64 a = r.shape(), b = r.shape(), c = r.shape();
    if (!a || !b || !c) return;
    r.L(a)->name = "rascunho";
    r.L(b)->name = "arame-bicubica";
    r.L(c)->name = "melhor";
    AUREA_CHECK(r.e.set_layer_quality(a, 1, 0));
    AUREA_CHECK(r.e.set_layer_quality(b, 2, 1));
    const std::string path = r.temp("aurea_ae_layers_quality.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* la = r.named("rascunho");
    const Layer* lb = r.named("arame-bicubica");
    const Layer* lc = r.named("melhor");
    AUREA_CHECK(la && lb && lc);
    if (la && lb && lc) {
        AUREA_CHECK_EQ(la->quality, u8{1});
        AUREA_CHECK_EQ(la->sampling, u8{0});
        AUREA_CHECK_EQ(lb->quality, u8{2});
        AUREA_CHECK_EQ(lb->sampling, u8{1});
        AUREA_CHECK_EQ(lc->quality, u8{0});
        AUREA_CHECK_EQ(lc->sampling, u8{0});
    }
    std::remove(path.c_str());
    static_assert(kTimelineSectionVersion >= 74, "a qualidade da camada entra na v74");
}

AUREA_TEST(AeLayers, DraftSkipsOnlyHeavyEffectsInThePlan) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    const Effect* echo = reg.find(reg.find_key(effect_keys::kEchoTrail));
    const Effect* blur = reg.find(reg.find_key(effect_keys::kGaussianBlur));
    const Effect* invert = reg.find(reg.find_key(effect_keys::kInvert));
    const Effect* tile = reg.find(reg.find_key(effect_keys::kMotionTile));
    AUREA_CHECK(echo && blur && invert && tile);
    if (!echo || !blur || !invert || !tile) return;
    // Pesado = precisa de outros instantes ou do quadro inteiro.
    AUREA_CHECK(EffectGraph::is_heavy(*echo));
    AUREA_CHECK(!EffectGraph::is_heavy(*blur));
    AUREA_CHECK(!EffectGraph::is_heavy(*invert));
    AUREA_CHECK(!EffectGraph::is_heavy(*tile));   // geometria nunca sai
    // Nenhum efeito de cor/vizinhança fica de fora; nenhum pesado entra.
    Layer l;
    l.kind = LayerKind::Shape;
    l.end = FrameIndex{60};
    u32 nextId = 1;
    const Effect* motion = reg.find(reg.find_key(effect_keys::kMotionDetect));
    AUREA_CHECK(motion && EffectGraph::is_heavy(*motion));   // lê o passado da camada
    // O Eco é neutro no plano (o Renderer é que o tira no Rascunho); o Detectar
    // movimento (mistura 100 %) é um pesado vivo na cadeia de pixels.
    for (const char* key : {effect_keys::kEchoTrail, effect_keys::kMotionDetect, effect_keys::kInvert, effect_keys::kGaussianBlur}) {
        EffectInstance inst;
        inst.id = nextId++;
        inst.type = reg.find_key(key);
        if (const ParameterRegistry* p = reg.params(inst.type)) initialize_instance(inst, *p);
        l.effects.push_back(std::move(inst));
    }
    LayerPlacement pl;
    pl.compWidth = 320;
    pl.compHeight = 180;
    pl.layerWidth = 200;
    pl.layerHeight = 200;
    EffectPlan full, draft;
    EffectGraph::plan(l, reg, FrameIndex{10}, 1.0f, pl, nullptr, full, 30.0, nullptr);
    EffectGraph::plan(l, reg, FrameIndex{10}, 1.0f, pl, nullptr, draft, 30.0, nullptr, 0u, kInvalidIndex, true);
    u32 heavyFull = 0, heavyDraft = 0, lightFull = 0, lightDraft = 0;
    for (const EffectEval& ev : full.evals) (EffectGraph::is_heavy(*ev.effect) ? heavyFull : lightFull)++;
    for (const EffectEval& ev : draft.evals) (EffectGraph::is_heavy(*ev.effect) ? heavyDraft : lightDraft)++;
    std::printf("    plano: %u leves + %u pesados; rascunho: %u leves + %u pesados ", lightFull, heavyFull, lightDraft, heavyDraft);
    AUREA_CHECK(heavyFull >= 1u);    // sem o Rascunho o pesado está no plano
    AUREA_CHECK(lightFull >= 1u);
    AUREA_CHECK_EQ(heavyDraft, 0u);
    AUREA_CHECK_EQ(lightDraft, lightFull);
}

// -----------------------------------------------------------------------------
// 2 · Clipe de animação do modelo 3D
// -----------------------------------------------------------------------------
AUREA_TEST(AeLayers, ModelAnimationClipPickerListsSetsAndUndoes) {
    LayersRig r;
    ModelImport mi;
    mi.path = write_two_clip_model("aurea_teste_layers_dois_clipes.gltf");   // relativo, como o test_camera_lens
    const Result<u64> model = r.e.import_model(mi);
    AUREA_CHECK(model.ok());
    if (!model.ok()) return;
    const std::vector<std::string> clips = r.e.model_animation_clips(*model);
    AUREA_CHECK_EQ(clips.size(), static_cast<usize>(2));
    if (clips.size() == 2) {
        AUREA_CHECK(clips[0] == "Andar");
        // Sem nome no arquivo: o importador numera ("Animacao 2"); se vier vazio, a UI numera.
        std::printf("    clipes: \"%s\", \"%s\" ", clips[0].c_str(), clips[1].c_str());
        AUREA_CHECK(clips[1] != "Andar");
    }
    // Importar toca o primeiro (como antes).
    AUREA_CHECK_EQ(r.e.model_animation_clip(*model), 0);
    AUREA_CHECK(r.e.set_model_animation_clip(*model, 1));
    AUREA_CHECK_EQ(r.L(*model)->model.animationClip, 1);
    AUREA_CHECK(r.e.set_model_animation_clip(*model, -1));   // pose parada
    AUREA_CHECK_EQ(r.e.model_animation_clip(*model), -1);
    AUREA_CHECK(!r.e.set_model_animation_clip(*model, 2));   // o arquivo não tem
    AUREA_CHECK(!r.e.set_model_animation_clip(*model, -2));
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.e.model_animation_clip(*model), 1);
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.e.model_animation_clip(*model), 0);
    // Camada que não é modelo: lista vazia, recusa.
    const u64 s = r.shape();
    AUREA_CHECK(r.e.model_animation_clips(s).empty());
    AUREA_CHECK(!r.e.set_model_animation_clip(s, 0));
    AUREA_CHECK_EQ(r.e.model_animation_clip(s), -1);
    // Travada: recusa.
    r.L(*model)->locked = true;
    AUREA_CHECK(!r.e.set_model_animation_clip(*model, 1));
    std::remove(mi.path.c_str());
}

// -----------------------------------------------------------------------------
// 3 · Transições de entrada/saída por camada (as linhas novas das interfaces)
// -----------------------------------------------------------------------------
AUREA_TEST(AeLayers, LayerTransitionsSetClampUndoAndReportInTheInfo) {
    LayersRig r;
    const u64 a = r.shape();
    if (!a) return;
    Layer* l = r.L(a);
    l->start = FrameIndex{0};
    l->end = FrameIndex{40};
    AUREA_CHECK(r.e.set_transition(a, false, 1, 10));
    AUREA_CHECK(r.e.set_transition(a, true, 4, 500));   // preso na metade do trecho
    AUREA_CHECK_EQ(l->transitionIn, u8{1});
    AUREA_CHECK_EQ(l->transitionInFrames, 10u);
    AUREA_CHECK_EQ(l->transitionOut, u8{4});
    AUREA_CHECK_EQ(l->transitionOutFrames, 20u);
    AUREA_CHECK(!r.e.set_transition(a, false, 6, 10));
    // O que as linhas leem: LayerDetailPOD.reserved0 (tipo 4+4 bits, quadros 12+12).
    bridge::LayerDetailPOD info{};
    AUREA_CHECK(r.e.query_layer_detail(a, info));
    AUREA_CHECK_EQ(info.reserved0 & 0xFu, 1u);
    AUREA_CHECK_EQ((info.reserved0 >> 4) & 0xFu, 4u);
    AUREA_CHECK_EQ((info.reserved0 >> 8) & 0xFFFu, 10u);
    AUREA_CHECK_EQ((info.reserved0 >> 20) & 0xFFFu, 20u);
    // Tipo 0 = nenhuma (os quadros zeram).
    AUREA_CHECK(r.e.set_transition(a, false, 0, 10));
    AUREA_CHECK_EQ(l->transitionIn, u8{0});
    AUREA_CHECK_EQ(l->transitionInFrames, 0u);
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.L(a)->transitionIn, u8{1});
}

// -----------------------------------------------------------------------------
// B10 · guias da prévia
// -----------------------------------------------------------------------------
AUREA_TEST(AeLayers, PreviewGuidesAreViewStateNotProjectState) {
    LayersRig r;
    AUREA_CHECK_EQ(r.e.preview_guides(), 0u);
    r.e.set_preview_guides(1u | 2u | 4u | 8u, 50.0f);   // bit 3 não existe
    AUREA_CHECK_EQ(r.e.preview_guides(), 7u);
    // Não entra no desfazer (é estado do visor, não do projeto).
    (void)r.undo();
    AUREA_CHECK_EQ(r.e.preview_guides(), 7u);
    r.e.set_preview_guides(0, 0.0f);
    AUREA_CHECK_EQ(r.e.preview_guides(), 0u);
}

// -----------------------------------------------------------------------------
// B4 · trim das formas paramétricas: lido e descartado, gravado no padrão
// -----------------------------------------------------------------------------
AUREA_TEST(AeLayers, ParametricShapeTrimIsGoneButFilesStillRoundTrip) {
    LayersRig r;
    const u64 a = r.shape();
    if (!a) return;
    r.L(a)->name = "forma";
    r.L(a)->shape.cornerRadius = 12.0f;
    const std::string path = r.temp("aurea_ae_layers_trim.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    const Layer* l = r.named("forma");
    AUREA_CHECK(l != nullptr);
    if (l) AUREA_CHECK_NEAR(l->shape.cornerRadius, 12.0f, 1e-6f);   // os campos depois do trim no lugar
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// B8 · velocidade negativa numa rampa com keys
// -----------------------------------------------------------------------------
AUREA_TEST(AeLayers, NegativeSpeedInAKeyedRampPlaysTheSourceBackwards) {
    // Rampa 2× → −1×: a fonte anda para a frente, para e volta.
    Layer l;
    l.kind = LayerKind::Video;
    l.start = FrameIndex{0};
    l.end = FrameIndex{100};
    l.offset = FrameIndex{0};
    Track& t = l.tracks.get_or_create(TrackProperty::Speed);
    (void)t.set(FrameIndex{0}, 2.0f, Interpolation::Linear);
    (void)t.set(FrameIndex{40}, -1.0f, Interpolation::Linear);
    AUREA_CHECK_NEAR(Layer::speed_value(t, 40.0), -1.0, 1e-9);
    AUREA_CHECK_NEAR(Layer::speed_value(t, 80.0), -1.0, 1e-9);   // depois da última: a da ponta
    // Velocidade cruza zero em 2/3 do trecho (2 + (−3)·u = 0).
    const f64 peakT = 40.0 * 2.0 / 3.0;
    const f64 peak = l.source_frame_f(peakT);
    AUREA_CHECK(peak > l.source_frame_f(peakT - 5.0));
    AUREA_CHECK(peak > l.source_frame_f(peakT + 5.0));
    // Integral exata do trecho linear: 2t − 3t²/80 em t = 80/3 → 26,67 quadros.
    AUREA_CHECK_NEAR(peak, 2.0 * peakT - 3.0 * peakT * peakT / 80.0, 1e-6);
    // Depois da última key anda a −1×: 1 quadro da fonte para trás por quadro.
    AUREA_CHECK_NEAR(l.source_frame_f(50.0) - l.source_frame_f(51.0), 1.0, 1e-9);
    // Nunca antes do quadro 0 da fonte.
    AUREA_CHECK_NEAR(l.source_frame_f(99.0), 0.0, 1e-9);
    AUREA_CHECK(l.source_frame_f(70.0) >= 0.0);
    // Velocidades positivas de sempre: o mesmo resultado de antes (limite 16).
    Layer p = l;
    Track& pt = p.tracks.get_or_create(TrackProperty::Speed);
    pt.keys.clear();
    (void)pt.set(FrameIndex{0}, 1.0f, Interpolation::Linear);
    (void)pt.set(FrameIndex{10}, 40.0f, Interpolation::Linear);
    AUREA_CHECK_NEAR(Layer::speed_value(pt, 10.0), 16.0, 1e-9);
    AUREA_CHECK_NEAR(Layer::speed_value(pt, 20.0), 16.0, 1e-9);
    AUREA_CHECK_NEAR(Layer::speed_value(pt, -1.0), 1.0, 1e-9);
}

AUREA_TEST(AeLayers, SpeedKeyEditKeepsTheSignAndClampsTheMagnitude) {
    LayersRig r;
    // Um vídeo não está disponível aqui: o comando age em qualquer camada.
    const u64 a = r.shape();
    if (!a) return;
    Layer* l = r.L(a);
    l->start = FrameIndex{0};
    l->end = FrameIndex{60};
    const i32 k0 = r.e.edit_speed_key(a, -1, 0, 0.0f, -1);
    const i32 k1 = r.e.edit_speed_key(a, -1, 30, 0.0f, -1);
    AUREA_CHECK(k0 >= 0 && k1 >= 0);
    if (k0 < 0 || k1 < 0) return;
    AUREA_CHECK(r.e.edit_speed_key(a, k1, 30, -2.5f, -1) >= 0);
    const Track* t = r.L(a)->tracks.find(TrackProperty::Speed);
    AUREA_CHECK(t && t->keys.size() == 2);
    if (!t || t->keys.size() != 2) return;
    AUREA_CHECK_NEAR(t->keys[1].value, -2.5f, 1e-6f);
    AUREA_CHECK(r.e.edit_speed_key(a, 1, 30, -99.0f, -1) >= 0);
    AUREA_CHECK_NEAR(r.L(a)->tracks.find(TrackProperty::Speed)->keys[1].value, -16.0f, 1e-6f);
    AUREA_CHECK(r.e.edit_speed_key(a, 1, 30, -0.001f, -1) >= 0);
    AUREA_CHECK_NEAR(r.L(a)->tracks.find(TrackProperty::Speed)->keys[1].value, -0.05f, 1e-6f);
    AUREA_CHECK(r.e.edit_speed_key(a, 1, 30, 3.0f, -1) >= 0);
    AUREA_CHECK_NEAR(r.L(a)->tracks.find(TrackProperty::Speed)->keys[1].value, 3.0f, 1e-6f);
}
