// =============================================================================
//  Aurea / tests / test_layer_anim.cpp
//
//  Animadores de camada (entrada "começa de", saída, força/atraso por letra,
//  curva, wiggle), copiar/colar, comprimento do desfoque por camada, escopo do
//  ajuste, câmera que atravessa o grupo e pôr/tirar camada de grupo. Tudo sem
//  GPU: a conta do motor, a API e o arquivo (o render está em test_gpu.cpp).
// =============================================================================
#include "TestFramework.hpp"
#include "OldProjects.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/timeline/LayerAnimator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace aurea;

namespace {

EngineConfig headless_anim() {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

Composition* current(Engine& e) { return e.project()->timeline().composition(e.project()->timeline().current()); }
Layer* layer(Engine& e, u64 id) { return current(e)->layer(LayerId::unpack(id)); }

void seek(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}

void undo(Engine& e) {
    Command c;
    c.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(c).ok());
}

std::string temp_file(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

/// Camada de forma com um animador e o progresso 0 → 100 % em 30 quadros.
Layer shape_with_animator(LayerAnimator a) {
    Layer l;
    l.kind = LayerKind::Shape;
    l.start = FrameIndex{0};
    l.end = FrameIndex{120};
    l.layerAnimators.push_back(a);
    Track& p = l.tracks.get_or_create(TrackProperty::LayerAnimParam, 0, layeranim::kProgress);
    (void)p.set(FrameIndex{0}, 0.0f);
    (void)p.set(FrameIndex{30}, 100.0f);
    return l;
}

} // namespace

AUREA_TEST(LayerAnim, WholeLayerEntryFollowsProgressStrengthAndEase) {
    LayerAnimator a;
    a.ease = 0;   // linear: o peso é exatamente 1 − progresso
    a.fromOpacity = 0.0f;
    a.fromPosX = -200.0f;
    a.fromScale = 50.0f;
    a.fromRotation = 90.0f;
    Layer l = shape_with_animator(a);
    const layeranim::Offset o0 = layeranim::whole_offset(l, 0.0, 30.0);
    AUREA_CHECK_NEAR(o0.opacity, 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(o0.translate.x, -200.0f, 1e-4f);
    AUREA_CHECK_NEAR(o0.scale.x, 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(o0.scale.y, 0.5f, 1e-6f);   // sem escala Y própria, Y segue X
    AUREA_CHECK_NEAR(o0.rotation.z, 90.0f, 1e-4f);
    const layeranim::Offset o15 = layeranim::whole_offset(l, 15.0, 30.0);
    AUREA_CHECK_NEAR(o15.opacity, 0.5f, 1e-5f);
    AUREA_CHECK_NEAR(o15.translate.x, -100.0f, 1e-3f);
    AUREA_CHECK_NEAR(o15.scale.x, 0.75f, 1e-5f);
    const layeranim::Offset o40 = layeranim::whole_offset(l, 40.0, 30.0);
    AUREA_CHECK_NEAR(o40.opacity, 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(o40.translate.x, 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(o40.scale.x, 1.0f, 1e-6f);
    // Força 50 %: metade do "from".
    l.layerAnimators[0].strength = 50.0f;
    AUREA_CHECK_NEAR(layeranim::whole_offset(l, 0.0, 30.0).translate.x, -100.0f, 1e-3f);
    // Escala Y própria.
    l.layerAnimators[0].strength = 100.0f;
    l.layerAnimators[0].scaleSeparated = true;
    l.layerAnimators[0].fromScaleY = 200.0f;
    AUREA_CHECK_NEAR(layeranim::whole_offset(l, 0.0, 30.0).scale.y, 2.0f, 1e-5f);
    // Curva suave: no meio já passou da metade (sai rápido, pousa devagar).
    l.layerAnimators[0].ease = 1;
    AUREA_CHECK(layeranim::whole_offset(l, 15.0, 30.0).opacity > 0.8f);
    // Desligado: nada.
    l.layerAnimators[0].enabled = false;
    AUREA_CHECK(!layeranim::has_whole(l));
    AUREA_CHECK_NEAR(layeranim::whole_offset(l, 0.0, 30.0).opacity, 1.0f, 0.0f);
}

AUREA_TEST(LayerAnim, TextUnitsWaitTheirDelayAndExitReversesTheOrder) {
    LayerAnimator a;
    a.ease = 0;
    a.unit = 1;           // letra a letra
    a.delayMs = 100.0f;   // 3 quadros a 30 fps
    a.fromPosY = 50.0f;
    a.fromOpacity = 100.0f;
    Layer l = shape_with_animator(a);
    l.kind = LayerKind::Text;
    AUREA_CHECK(!layeranim::has_whole(l));   // letra: não mexe na camada inteira
    AUREA_CHECK(layeranim::has_units(l));
    std::vector<text::GlyphUnits> units(4);
    for (u32 i = 0; i < 4; ++i) units[i] = text::GlyphUnits{i, 0, 0};
    std::vector<text::GlyphAnim> out(4);
    layeranim::apply_to_glyphs(l, 15.0, 30.0, units, 4, 1, 1, 0.0f, out);
    // Letra i: relógio 15 − 3i → progresso (15 − 3i)/30.
    for (u32 i = 0; i < 4; ++i) {
        const f32 w = 1.0f - static_cast<f32>(15 - 3 * static_cast<i32>(i)) / 30.0f;
        AUREA_CHECK_NEAR(out[i].translate.y, 50.0f * w, 1e-3f);
    }
    // Saída: a ordem se inverte (a última letra anda primeiro).
    l.layerAnimators[0].exit = true;
    std::vector<text::GlyphAnim> rev(4);
    layeranim::apply_to_glyphs(l, 15.0, 30.0, units, 4, 1, 1, 0.0f, rev);
    for (u32 i = 0; i < 4; ++i) AUREA_CHECK_NEAR(rev[i].translate.y, out[3 - i].translate.y, 1e-4f);
    // Tracking: abre a linha a partir do centro (a soma dos deslocamentos fica em volta de 0).
    l.layerAnimators[0].exit = false;
    l.layerAnimators[0].fromTracking = 10.0f;
    l.layerAnimators[0].fromPosY = 0.0f;
    std::vector<text::GlyphAnim> trk(4);
    layeranim::apply_to_glyphs(l, 0.0, 30.0, units, 4, 1, 1, 0.5f, trk);
    AUREA_CHECK(trk[0].trackingShift < 0.0f && trk[3].trackingShift > 0.0f);
    AUREA_CHECK_NEAR(trk[0].trackingShift + trk[3].trackingShift, 0.0f, 1e-3f);   // simétrico no centro
    AUREA_CHECK_NEAR(trk[3].trackingShift - trk[0].trackingShift, 30.0f, 1e-3f);  // 3 vãos de 10 px
}

AUREA_TEST(LayerAnim, WiggleIsDeterministicHoldsAndRerollsWithTheSeed) {
    // Ruído: mesmo (semente, unidade, canal, t) = mesmo valor; faixa −1..1.
    for (u32 k = 0; k < 64; ++k) {
        const f64 t = k * 0.173;
        const f32 v = layeranim::wiggle_noise(7, 2, 1, t, 0.0f);
        AUREA_CHECK(v >= -1.0f && v <= 1.0f);
        AUREA_CHECK_EQ(v, layeranim::wiggle_noise(7, 2, 1, t, 0.0f));
    }
    // Pausa de 50 %: parado na primeira metade de cada passo.
    AUREA_CHECK_EQ(layeranim::wiggle_noise(3, 0, 0, 4.1, 0.5f), layeranim::wiggle_noise(3, 0, 0, 4.45, 0.5f));
    AUREA_CHECK(layeranim::wiggle_noise(3, 0, 0, 4.9, 0.5f) != layeranim::wiggle_noise(3, 0, 0, 4.45, 0.5f));
    LayerAnimator a;
    a.progress = 100.0f;   // sem entrada: só o wiggle
    a.wigglePosX = 40.0f;
    a.wiggleSpeed = 3.0f;
    Layer l;
    l.kind = LayerKind::Image;
    l.end = FrameIndex{300};
    l.layerAnimators.push_back(a);
    f32 lo = 1e9f, hi = -1e9f;
    for (i64 f = 0; f < 120; ++f) {
        const f32 x = layeranim::whole_offset(l, static_cast<f64>(f), 30.0).translate.x;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
        AUREA_CHECK(std::fabs(x) <= 40.0f + 1e-3f);
    }
    AUREA_CHECK(hi - lo > 20.0f);   // mexe de verdade
    const f32 before = layeranim::whole_offset(l, 37.0, 30.0).translate.x;
    l.layerAnimators[0].wiggleSeed = 12345;   // "sortear outro movimento"
    AUREA_CHECK(std::fabs(layeranim::whole_offset(l, 37.0, 30.0).translate.x - before) > 1e-4f);
}

AUREA_TEST(LayerAnim, EngineApiExitCopyPasteRemoveAndUndo) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_anim()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    const auto b = e.add_shape(10);
    AUREA_CHECK(a.ok() && b.ok());
    if (!a.ok() || !b.ok()) return;
    AUREA_CHECK_EQ(e.add_layer_animator(*a), 0);
    std::vector<f32> v(Engine::kLayerAnimFloats * 32);
    seek(e, 0);
    AUREA_CHECK_EQ(e.query_layer_animators(*a, v.data(), static_cast<u32>(v.size())), 1u);
    AUREA_CHECK_NEAR(v[6 + layeranim::kProgress], 0.0f, 1e-6f);             // começo: progresso 0
    AUREA_CHECK_NEAR(v[6 + layeranim::kFromOpacity], 0.0f, 1e-6f);          // entra desbotando
    AUREA_CHECK((static_cast<u32>(v[24]) & (1u << layeranim::kProgress)) != 0);
    AUREA_CHECK((static_cast<u32>(v[25]) & (1u << layeranim::kProgress)) != 0);   // keyframe no playhead
    AUREA_CHECK_EQ(v[26], 0.0f);   // forma: sem letras
    const Layer* la = layer(e, *a);
    const i64 lastLocal = la->local_time(FrameIndex{la->end.value - 1}).value;
    // Saída: os keyframes do progresso vão para o fim (100 → 0).
    v[2] = 1.0f;
    AUREA_CHECK(e.set_layer_animator(*a, 0, v.data()));
    const Track* p = layer(e, *a)->tracks.find(TrackProperty::LayerAnimParam, 0, layeranim::kProgress);
    AUREA_CHECK(p && p->keys.size() == 2);
    if (p && p->keys.size() == 2) {
        AUREA_CHECK_EQ(p->keys.back().time.value, lastLocal);
        AUREA_CHECK_NEAR(p->keys.back().value, 0.0f, 1e-6f);
        AUREA_CHECK_NEAR(p->keys.front().value, 100.0f, 1e-6f);
    }
    AUREA_CHECK(layer(e, *a)->layerAnimators[0].exit);
    // Valor de um parâmetro (sem keyframe: o parado), com a faixa do motor.
    AUREA_CHECK(e.set_layer_anim_param(*a, 0, layeranim::kFromPosX, -300.0f));
    AUREA_CHECK_NEAR(layer(e, *a)->layerAnimators[0].fromPosX, -300.0f, 1e-6f);
    AUREA_CHECK(e.set_layer_anim_param(*a, 0, layeranim::kFromOpacity, 250.0f));
    AUREA_CHECK_NEAR(layer(e, *a)->layerAnimators[0].fromOpacity, 100.0f, 1e-6f);
    // Losango: liga e desliga o keyframe no playhead.
    AUREA_CHECK(e.toggle_layer_anim_key(*a, 0, layeranim::kWigglePosX));
    AUREA_CHECK(layer(e, *a)->tracks.find(TrackProperty::LayerAnimParam, 0, layeranim::kWigglePosX)->keys.size() == 1);
    AUREA_CHECK(e.toggle_layer_anim_key(*a, 0, layeranim::kWigglePosX));
    AUREA_CHECK(layer(e, *a)->tracks.find(TrackProperty::LayerAnimParam, 0, layeranim::kWigglePosX)->keys.empty());
    // Copiar/colar: acrescenta na outra camada, com os keyframes.
    AUREA_CHECK_EQ(e.copy_layer_animators(*a), 1u);
    const u64 targets[] = {*b};
    AUREA_CHECK_EQ(e.paste_layer_animators(targets, 1), 1u);
    AUREA_CHECK_EQ(e.paste_layer_animators(targets, 1), 1u);
    AUREA_CHECK_EQ(layer(e, *b)->layerAnimators.size(), usize{2});
    AUREA_CHECK(layer(e, *b)->tracks.find(TrackProperty::LayerAnimParam, 1, layeranim::kProgress) != nullptr);
    AUREA_CHECK_NEAR(layer(e, *b)->layerAnimators[1].fromPosX, -300.0f, 1e-6f);
    // Remover o primeiro: as trilhas do segundo descem um índice.
    AUREA_CHECK(e.remove_layer_animator(*b, 0));
    AUREA_CHECK_EQ(layer(e, *b)->layerAnimators.size(), usize{1});
    AUREA_CHECK(layer(e, *b)->tracks.find(TrackProperty::LayerAnimParam, 0, layeranim::kProgress) != nullptr);
    AUREA_CHECK(layer(e, *b)->tracks.find(TrackProperty::LayerAnimParam, 1, layeranim::kProgress) == nullptr);
    undo(e);
    AUREA_CHECK_EQ(layer(e, *b)->layerAnimators.size(), usize{2});
    // Comprimento do desfoque, escopo do ajuste.
    AUREA_CHECK(e.set_layer_motion_blur_length(*a, 2.5f));
    AUREA_CHECK_NEAR(e.query_layer_motion_blur_length(*a), 2.5f, 1e-6f);
    AUREA_CHECK(e.set_layer_motion_blur_length(*a, 9.0f));
    AUREA_CHECK_NEAR(e.query_layer_motion_blur_length(*a), 4.0f, 1e-6f);
    AUREA_CHECK(e.set_adjustment_scope(*a, 1));
    AUREA_CHECK_EQ(e.query_adjustment_scope(*a), 1u);
    AUREA_CHECK(!e.set_adjustment_scope(*a, 2));
    AUREA_CHECK_EQ(e.query_group_camera_pass_through(*a), -1);   // não é grupo
    e.shutdown();
}

AUREA_TEST(LayerAnim, SaveLoadKeepsEverythingAndOldProjectsReadDefaults) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_anim()).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    AUREA_CHECK(a.ok());
    if (!a.ok()) return;
    AUREA_CHECK_EQ(e.add_layer_animator(*a), 0);
    std::vector<f32> v(Engine::kLayerAnimFloats * 32);
    AUREA_CHECK_EQ(e.query_layer_animators(*a, v.data(), static_cast<u32>(v.size())), 1u);
    v[3] = 3.0f; v[4] = 1.0f; v[5] = 4242.0f;
    AUREA_CHECK(e.set_layer_animator(*a, 0, v.data()));
    AUREA_CHECK(e.set_layer_anim_param(*a, 0, layeranim::kWiggleRotation, 12.5f));
    AUREA_CHECK(e.set_layer_anim_param(*a, 0, layeranim::kDelay, 55.0f));
    AUREA_CHECK(e.set_layer_motion_blur_length(*a, 1.75f));
    AUREA_CHECK(e.set_adjustment_scope(*a, 1));
    const u64 ids[] = {*a};
    const auto g = e.precompose(ids, 1);
    AUREA_CHECK(g.ok());
    if (!g.ok()) return;
    AUREA_CHECK(e.set_group_camera_pass_through(*g, true));
    const std::string path = temp_file("aurea_layer_anim_roundtrip.aurea");
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    Project p;
    LoadReport r;
    AUREA_CHECK(ProjectSerializer::load(p, path, LoadOptions{}, &r).ok());
    AUREA_CHECK_EQ(r.timelineVersion, kTimelineSectionVersion);
    const Layer* found = nullptr;
    const Layer* group = nullptr;
    p.timeline().for_each_composition([&](CompositionId, const Composition& c) {
        for (u32 i = 0; i < c.order().size(); ++i) {
            const Layer* l = c.layer(c.order().at(i));
            if (l && !l->layerAnimators.empty()) found = l;
            if (l && l->kind == LayerKind::Composition) group = l;
        }
    });
    AUREA_CHECK(found != nullptr && group != nullptr);
    if (found) {
        const LayerAnimator& x = found->layerAnimators[0];
        AUREA_CHECK_EQ(x.ease, u8{3});
        AUREA_CHECK(x.scaleSeparated);
        AUREA_CHECK_EQ(x.wiggleSeed, 4242u);
        AUREA_CHECK_NEAR(x.wiggleRotation, 12.5f, 1e-6f);
        AUREA_CHECK_NEAR(x.delayMs, 55.0f, 1e-6f);
        AUREA_CHECK_NEAR(found->transform.motionBlurAmount, 1.75f, 1e-6f);
        AUREA_CHECK_EQ(found->adjustmentScope, u8{1});
        AUREA_CHECK(found->tracks.find(TrackProperty::LayerAnimParam, 0, layeranim::kProgress) != nullptr);
    }
    if (group) AUREA_CHECK(group->nested.cameraPassThrough);
    std::remove(path.c_str());
    e.shutdown();

    // Projetos REAIS de versões antigas: nenhum animador, ajuste em tudo
    // abaixo, grupo fechado para a câmera e rastro 1× — o quadro de antes.
    struct Fixture { const u8* data; usize size; };
    const Fixture fixtures[] = {{test::kOldProjectTimelineV2, sizeof(test::kOldProjectTimelineV2)},
                                {test::kOldProjectTimelineV12, sizeof(test::kOldProjectTimelineV12)}};
    for (const Fixture& fx : fixtures) {
        Project old;
        LoadReport lr;
        AUREA_CHECK(ProjectSerializer::load_bytes(old, fx.data, fx.size, LoadOptions{}, &lr).ok());
        AUREA_CHECK(lr.timelineVersion < 36);
        u32 layers = 0;
        old.timeline().for_each_composition([&](CompositionId, const Composition& c) {
            for (u32 i = 0; i < c.order().size(); ++i) {
                const Layer* l = c.layer(c.order().at(i));
                if (!l) continue;
                ++layers;
                AUREA_CHECK(l->layerAnimators.empty());
                AUREA_CHECK_EQ(l->adjustmentScope, u8{0});
                AUREA_CHECK(!l->nested.cameraPassThrough);
                AUREA_CHECK_NEAR(l->transform.motionBlurAmount, 1.0f, 0.0f);
                AUREA_CHECK(!layeranim::has_whole(*l) && !layeranim::has_units(*l));
                const layeranim::Offset o = layeranim::whole_offset(*l, 12.0, 30.0);
                AUREA_CHECK(o.opacity == 1.0f && o.scale.x == 1.0f && o.translate.x == 0.0f && o.rotation.z == 0.0f);
            }
        });
        AUREA_CHECK(layers > 0);
    }
}

AUREA_TEST(LayerAnim, AddToGroupAndRemoveFromGroupKeepTimesAndUndo) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_anim()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    const auto a = e.add_shape(10);
    const auto b = e.add_shape(10);
    AUREA_CHECK(a.ok() && b.ok());
    if (!a.ok() || !b.ok()) return;
    layer(e, *b)->start = FrameIndex{12};
    layer(e, *b)->end = FrameIndex{80};
    const u64 ids[] = {*a};
    const auto g = e.precompose(ids, 1);
    AUREA_CHECK(g.ok());
    if (!g.ok()) return;
    const u32 before = current(e)->order().size();
    const CompositionId oldChild = layer(e, *g)->nested.composition;
    // Pôr B no grupo: sai daqui, entra lá com o mesmo tempo na tela.
    const u64 moving[] = {*b};
    std::string why;
    const auto r = e.add_layers_to_group(moving, 1, *g, &why);
    AUREA_CHECK_MSG(r.ok(), why.c_str());
    AUREA_CHECK_EQ(current(e)->order().size(), before - 1);
    const Layer* G = layer(e, *g);
    const Composition* child = e.project()->timeline().composition(G->nested.composition);
    AUREA_CHECK(child != nullptr && G->nested.composition != oldChild);
    AUREA_CHECK(child && child->order().size() == 2);
    const i64 shift = G->start.value - G->offset.value;
    bool timed = false;
    if (child) {
        for (u32 i = 0; i < child->order().size(); ++i) {
            const Layer* x = child->layer(child->order().at(i));
            if (x && x->start.value + shift == 12 && x->end.value + shift == 80) timed = true;
        }
    }
    AUREA_CHECK(timed);
    // A composição original do grupo continua intacta (o desfazer volta para ela).
    AUREA_CHECK_EQ(e.project()->timeline().composition(oldChild)->order().size(), 1u);
    undo(e);
    AUREA_CHECK_EQ(current(e)->order().size(), before);
    AUREA_CHECK(layer(e, *g)->nested.composition == oldChild);
    // Um grupo não entra nele mesmo.
    const u64 self[] = {*g};
    AUREA_CHECK(!e.add_layers_to_group(self, 1, *g).ok());
    // Tirar do grupo: abre, tira, volta para fora com a camada acima do grupo.
    AUREA_CHECK(e.add_layers_to_group(moving, 1, *g).ok());
    AUREA_CHECK(e.open_precomp(*g));
    const Composition* inside = current(e);
    u64 inner = 0;
    for (u32 i = 0; i < inside->order().size(); ++i) {
        const Layer* x = inside->layer(inside->order().at(i));
        if (x && x->start.value + shift == 12) inner = inside->order().at(i).pack();
    }
    AUREA_CHECK(inner != 0);
    const auto out = e.remove_layer_from_group(inner, &why);
    AUREA_CHECK_MSG(out.ok(), why.c_str());
    AUREA_CHECK_EQ(e.precomp_depth(), 0u);
    AUREA_CHECK_EQ(current(e)->order().size(), before);
    if (out.ok()) {
        const Layer* back = layer(e, *out);
        AUREA_CHECK(back && back->start.value == 12 && back->end.value == 80);
    }
    AUREA_CHECK(!e.remove_layer_from_group(*a).ok());   // fora de grupo: recusa
    e.shutdown();
}
