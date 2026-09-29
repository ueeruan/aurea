// Animação de TEXTO 3D: os presets viram animadores de camada por letra /
// palavra / linha (a mesma conta do texto 2D) e mexem em cada nó de letra da
// malha — posição, giro, escala e opacidade por letra, com atraso entre elas.
//
// O que se mede é a pose de cada letra (centro transformado, eixos) e a
// opacidade por nó que o renderer 3D recebe — no preview e no export é a
// mesma função (place_model → apply_text3d_animators).
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/timeline/LayerAnimator.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;

namespace {

std::unique_ptr<SceneAsset> glyph_asset(const char* content) {
    const auto font = text::default_font();
    if (!font) return nullptr;
    Text3DSpec spec;
    spec.content = content;
    spec.separateGlyphs = true;
    auto r = build_text3d(*font, spec);
    return r.ok() ? std::move(r.asset) : nullptr;
}

Layer text3d_layer() {
    Layer l;
    l.kind = LayerKind::Model3D;
    l.start = FrameIndex{0};
    l.end = FrameIndex{120};
    return l;
}

/// Pose das letras no instante local `t` (30 fps): deslocamento Y do centro
/// de cada letra em relação ao repouso, eixo Y de cada nó e opacidade.
struct Sample {
    std::vector<f32> dy, axisY, opacity;
};

Sample sample(const SceneAsset& asset, const Layer& l, f64 t) {
    auto rest = asset.rest_world_matrices();
    auto pose = rest;
    std::vector<f32> op;
    apply_text3d_layout(asset, l, t, pose);
    apply_text3d_animators(asset, l, t, 30.0, pose, op);
    Sample s;
    for (usize i = 0; i < asset.nodes.size(); ++i) {
        const Vec3 c = asset.meshes[static_cast<usize>(asset.nodes[i].mesh)].bounds.center();
        s.dy.push_back(pose[i].transform_point(c).y - rest[i].transform_point(c).y);
        s.axisY.push_back(pose[i].col[1].y);
        s.opacity.push_back(i < op.size() ? op[i] : 1.0f);
    }
    return s;
}

} // namespace

AUREA_TEST(Text3DAnim, RisePresetStaggersEachLetterThenSettlesAtRest) {
    const auto asset = glyph_asset("ABCD");
    AUREA_CHECK(asset != nullptr);
    if (!asset) return;
    AUREA_CHECK_EQ(asset->nodes.size(), usize{4});
    AUREA_CHECK_EQ(asset->textUnits.size(), usize{12});
    Layer l = text3d_layer();
    // Subir: 1 s por letra, 200 ms (6 quadros) entre letras.
    AUREA_CHECK(apply_text3d_anim_preset(l, 1, kText3DAnimIn, 1, 4, 1.0f, 200.0f, 30.0));
    AUREA_CHECK_EQ(l.layerAnimators.size(), usize{1});
    AUREA_CHECK(layeranim::has_units(l));
    AUREA_CHECK(!layeranim::has_whole(l));   // não mexe na camada inteira

    const Sample s0 = sample(*asset, l, 0.0);
    for (u32 i = 0; i < 4; ++i) {
        AUREA_CHECK_NEAR(s0.dy[i], -0.6f, 1e-3f);   // 60 px do animador = 0,6 altura de letra, abaixo
        AUREA_CHECK_NEAR(s0.opacity[i], 0.0f, 1e-5f);
    }
    const Sample s = sample(*asset, l, 12.0);
    // Letra 0 está 12 quadros dentro, a 1 está 6, a 2 começa agora, a 3 espera.
    AUREA_CHECK(s.opacity[0] > s.opacity[1] + 0.1f);
    AUREA_CHECK(s.opacity[1] > s.opacity[2] + 0.1f);
    AUREA_CHECK_NEAR(s.opacity[2], 0.0f, 1e-5f);
    AUREA_CHECK_NEAR(s.opacity[3], 0.0f, 1e-5f);
    AUREA_CHECK(s.dy[0] > s.dy[1] + 0.05f);
    AUREA_CHECK(s.dy[1] > s.dy[2] + 0.05f);
    // Curva "suave": p = 0,4 → peso (1 − 0,4)^3 = 0,216.
    AUREA_CHECK_NEAR(s.opacity[0], 0.784f, 1e-3f);
    AUREA_CHECK_NEAR(s.dy[0], -0.6f * 0.216f, 1e-3f);
    // A transformação de cada letra muda com o tempo.
    const Sample s6 = sample(*asset, l, 6.0);
    AUREA_CHECK(std::fabs(s6.dy[0] - s.dy[0]) > 0.05f);
    // Sub-quadro (desfoque de movimento): entre os dois quadros inteiros.
    const Sample half = sample(*asset, l, 12.5);
    AUREA_CHECK(half.opacity[0] > s.opacity[0] && half.opacity[0] < sample(*asset, l, 13.0).opacity[0]);
    // Depois de 30 + 3 × 6 quadros tudo pousou: pose de repouso, opaco.
    const Sample done = sample(*asset, l, 60.0);
    for (u32 i = 0; i < 4; ++i) {
        AUREA_CHECK_NEAR(done.dy[i], 0.0f, 1e-5f);
        AUREA_CHECK_NEAR(done.opacity[i], 1.0f, 1e-6f);
        AUREA_CHECK_NEAR(done.axisY[i], 1.0f, 1e-5f);
    }
}

AUREA_TEST(Text3DAnim, ExitReversesTheOrderAndFinishesAtTheLayerEnd) {
    const auto asset = glyph_asset("ABCD");
    AUREA_CHECK(asset != nullptr);
    if (!asset) return;
    Layer l = text3d_layer();
    // Fade de saída: 0,5 s (15 quadros), 100 ms (3 quadros) entre letras.
    AUREA_CHECK(apply_text3d_anim_preset(l, 0, kText3DAnimOut, 1, 4, 0.5f, 100.0f, 30.0));
    AUREA_CHECK(l.layerAnimators[0].exit);
    const Sample before = sample(*asset, l, 90.0);
    for (f32 o : before.opacity) AUREA_CHECK_NEAR(o, 1.0f, 1e-6f);
    const Sample mid = sample(*asset, l, 104.0);
    AUREA_CHECK(mid.opacity[3] < mid.opacity[2]);   // a última letra sai primeiro
    AUREA_CHECK(mid.opacity[2] < mid.opacity[1]);
    AUREA_CHECK_NEAR(mid.opacity[0], 1.0f, 1e-6f);
    const Sample end = sample(*asset, l, 119.0);   // último quadro da camada: tudo saiu
    for (f32 o : end.opacity) AUREA_CHECK_NEAR(o, 0.0f, 1e-5f);
}

AUREA_TEST(Text3DAnim, LoopWaveTravelsAcrossLettersAndRepeats) {
    const auto asset = glyph_asset("WAVE");
    AUREA_CHECK(asset != nullptr);
    if (!asset) return;
    Layer l = text3d_layer();
    // Onda em loop: 0,5 s (15 quadros) de ida, 100 ms entre letras.
    AUREA_CHECK(apply_text3d_anim_preset(l, 7, kText3DAnimLoop, 1, 4, 0.5f, 100.0f, 30.0));
    AUREA_CHECK(l.layerAnimators[0].loop);
    const Sample a = sample(*asset, l, 40.0);
    const Sample b = sample(*asset, l, 70.0);   // + 2 × 15 quadros = um ciclo de vai e volta
    const Sample c = sample(*asset, l, 35.0);
    for (u32 i = 0; i < 4; ++i) {
        AUREA_CHECK_NEAR(a.opacity[i], 1.0f, 1e-6f);   // onda não apaga
        AUREA_CHECK_NEAR(a.dy[i], b.dy[i], 1e-4f);
    }
    AUREA_CHECK(std::fabs(a.dy[0] - c.dy[0]) > 0.05f);   // anda no tempo
    AUREA_CHECK(std::fabs(a.dy[0] - a.dy[2]) > 0.02f);   // fase por letra
    // Muito depois do fim do trecho dos keyframes continua ondulando.
    const Sample late = sample(*asset, l, 100.0), late2 = sample(*asset, l, 95.0);
    AUREA_CHECK(std::fabs(late.dy[0] - late2.dy[0]) > 0.05f);
}

AUREA_TEST(Text3DAnim, WordUnitsMoveTogetherAndFlipTurnsLettersInDepth) {
    const auto asset = glyph_asset("AB CD");
    AUREA_CHECK(asset != nullptr);
    if (!asset) return;
    AUREA_CHECK_EQ(asset->nodes.size(), usize{4});
    AUREA_CHECK_EQ(asset->textWords, 2u);
    AUREA_CHECK_EQ(asset->textLines, 1u);
    Layer l = text3d_layer();
    AUREA_CHECK(apply_text3d_anim_preset(l, 0, kText3DAnimIn, 2, 2, 1.0f, 500.0f, 30.0));
    const Sample w = sample(*asset, l, 10.0);
    AUREA_CHECK_NEAR(w.opacity[0], w.opacity[1], 1e-6f);
    AUREA_CHECK_NEAR(w.opacity[2], w.opacity[3], 1e-6f);
    AUREA_CHECK(w.opacity[0] > w.opacity[2] + 0.1f);

    // Virar em X (entrada), trocando o preset do mesmo modo: um animador só.
    AUREA_CHECK(apply_text3d_anim_preset(l, 5, kText3DAnimIn, 1, 4, 1.0f, 100.0f, 30.0));
    AUREA_CHECK_EQ(l.layerAnimators.size(), usize{1});
    u32 mode = 9;
    AUREA_CHECK_EQ(text3d_anim_preset_of(l.layerAnimators[0], &mode), 5);
    AUREA_CHECK_EQ(mode, 0u);
    const Sample f = sample(*asset, l, 0.0);
    for (f32 y : f.axisY) AUREA_CHECK(std::fabs(y) < 1e-3f);   // 90° em X: o eixo Y deitou
    const Sample g = sample(*asset, l, 60.0);
    for (f32 y : g.axisY) AUREA_CHECK_NEAR(y, 1.0f, 1e-5f);
    // Entrada + saída + loop convivem; −1 remove só o do modo.
    AUREA_CHECK(apply_text3d_anim_preset(l, 0, kText3DAnimOut, 1, 4, 0.5f, 60.0f, 30.0));
    AUREA_CHECK(apply_text3d_anim_preset(l, 7, kText3DAnimLoop, 1, 4, 0.5f, 60.0f, 30.0));
    AUREA_CHECK_EQ(l.layerAnimators.size(), usize{3});
    AUREA_CHECK(apply_text3d_anim_preset(l, -1, kText3DAnimIn, 1, 4, 0.5f, 60.0f, 30.0));
    AUREA_CHECK_EQ(l.layerAnimators.size(), usize{2});
    AUREA_CHECK(l.tracks.find(TrackProperty::LayerAnimParam, 0, layeranim::kProgress) != nullptr);
    AUREA_CHECK(l.tracks.find(TrackProperty::LayerAnimParam, 1, layeranim::kProgress) != nullptr);
    AUREA_CHECK(l.tracks.find(TrackProperty::LayerAnimParam, 2, layeranim::kProgress) == nullptr);
    // Texto 3D sem letras separadas (malha única) fica parado.
    Text3DSpec plain; plain.content = "AB";
    const auto font = text::default_font();
    auto one = build_text3d(*font, plain);
    AUREA_CHECK(one.ok());
    if (one.ok()) {
        auto pose = one.asset->rest_world_matrices();
        std::vector<f32> op{0.5f};
        apply_text3d_animators(*one.asset, l, 0.0, 30.0, pose, op);
        AUREA_CHECK(op.empty());
    }
}

AUREA_TEST(Text3DAnim, EngineApiSeparatesGlyphsUndoesAndSurvivesSaveLoad) {
    Engine e;
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    Text3DSpec spec;
    spec.content = "Oi 3D";
    const auto id = e.add_text3d(spec);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    f32 q[Engine::kText3DAnimFloats * 3] = {};
    AUREA_CHECK(e.query_text3d_anim(*id, q));
    AUREA_CHECK_EQ(q[0], -1.0f);
    AUREA_CHECK(e.apply_text3d_anim(*id, 1, kText3DAnimIn, 1, 0.8f, 90.0f));
    Text3DSpec now;
    AUREA_CHECK(e.query_text3d(*id, now));
    AUREA_CHECK(now.separateGlyphs);   // as letras viraram nós próprios
    AUREA_CHECK(e.apply_text3d_anim(*id, 7, kText3DAnimLoop, 1, 0.5f, 100.0f));
    AUREA_CHECK(e.query_text3d_anim(*id, q));
    AUREA_CHECK_EQ(q[0], 1.0f);
    AUREA_CHECK_NEAR(q[2], 0.8f, 1e-4f);
    AUREA_CHECK_NEAR(q[3], 90.0f, 1e-4f);
    AUREA_CHECK_EQ(q[4], 4.0f);   // "Oi3D": 4 letras visíveis
    AUREA_CHECK_EQ(q[Engine::kText3DAnimFloats], -1.0f);
    AUREA_CHECK_EQ(q[Engine::kText3DAnimFloats * 2], 7.0f);
    // A lista genérica de animadores vê o loop e as unidades (texto 3D).
    std::vector<f32> v(Engine::kLayerAnimFloats * 32);
    AUREA_CHECK_EQ(e.query_layer_animators(*id, v.data(), static_cast<u32>(v.size())), 2u);
    AUREA_CHECK_EQ(v[26], 1.0f);
    AUREA_CHECK_EQ(v[27], 0.0f);
    AUREA_CHECK_EQ(v[Engine::kLayerAnimFloats + 27], 1.0f);
    // Um passo de desfazer por toque.
    AUREA_CHECK(e.apply_text3d_anim(*id, 3, kText3DAnimOut, 2, 0.4f, 50.0f));
    AUREA_CHECK_EQ(e.query_layer_animators(*id, v.data(), static_cast<u32>(v.size())), 3u);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.query_layer_animators(*id, v.data(), static_cast<u32>(v.size())), 2u);

    const std::string path = (std::filesystem::temp_directory_path() / "aurea_text3d_anim_roundtrip.aurea").string();
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    Project p;
    LoadReport r;
    AUREA_CHECK(ProjectSerializer::load(p, path, LoadOptions{}, &r).ok());
    const Layer* found = nullptr;
    p.timeline().for_each_composition([&](CompositionId, const Composition& c) {
        for (u32 i = 0; i < c.order().size(); ++i) {
            const Layer* l = c.layer(c.order().at(i));
            if (l && l->kind == LayerKind::Model3D && !l->layerAnimators.empty()) found = l;
        }
    });
    AUREA_CHECK(found != nullptr);
    if (found) {
        AUREA_CHECK_EQ(found->layerAnimators.size(), usize{2});
        const LayerAnimator& in = found->layerAnimators[0];
        const LayerAnimator& loop = found->layerAnimators[1];
        u32 mode = 9;
        AUREA_CHECK_EQ(text3d_anim_preset_of(in, &mode), 1);
        AUREA_CHECK_EQ(mode, 0u);
        AUREA_CHECK(!in.loop && !in.exit);
        AUREA_CHECK_EQ(in.unit, u8{1});
        AUREA_CHECK_EQ(in.ease, u8{1});
        AUREA_CHECK_NEAR(in.delayMs, 90.0f, 1e-5f);
        AUREA_CHECK_NEAR(in.fromPosY, 60.0f, 1e-5f);
        AUREA_CHECK_EQ(text3d_anim_preset_of(loop, &mode), 7);
        AUREA_CHECK_EQ(mode, 2u);
        AUREA_CHECK(loop.loop);
        AUREA_CHECK_EQ(loop.ease, u8{2});   // o bit do loop não suja a curva
        const Track* tr = found->tracks.find(TrackProperty::LayerAnimParam, 1, layeranim::kProgress);
        AUREA_CHECK(tr != nullptr && tr->keys.size() == 2);
        // O asset reaberto é a receita com letras separadas.
        const Asset* a = p.asset(found->model.scene);
        Text3DSpec reread;
        AUREA_CHECK(a && decode_text3d(a->sourcePath, reread) && reread.separateGlyphs);
    }
    std::remove(path.c_str());
    e.shutdown();
}
