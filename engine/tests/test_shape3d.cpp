// Formas 3D prontas (scene3d/Shape3D.hpp): cada uma das 10 formas como
// GEOMETRIA (normais, UV, partes, sólido fechado e virado para fora), as
// trilhas por parte (só a parte escolhida anda; keyframes interpolam), a
// imagem por parte que sobrevive a salvar/reabrir, e — com GPU — um cubo com
// imagem que aparece e muda o quadro quando uma face se move.
#include "TestFramework.hpp"
#include "ImageIO.hpp"
#if defined(AUREA_TEST_VULKAN)
#include "VulkanBackend.hpp"
#endif

#include "aurea/Engine.hpp"
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/scene3d/MaterialPreview.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/timeline/LayerAnimator.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;

namespace {

std::string temp_file(const char* name) {
    const char* t = std::getenv("TEMP");
    return std::string(t ? t : ".") + "/" + name;
}

/// PNG de teste: quadriculado vermelho/azul (8×8 casas).
std::string checker_png(const char* name) {
    test::Image8 img;
    img.width = img.height = 64;
    img.rgba.resize(64u * 64u * 4u);
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x) {
            u8* p = &img.rgba[(static_cast<usize>(y) * 64 + x) * 4];
            const bool a = ((x / 8) + (y / 8)) % 2 == 0;
            p[0] = a ? 230 : 20; p[1] = 30; p[2] = a ? 20 : 230; p[3] = 255;
        }
    const std::string path = temp_file(name);
    AUREA_CHECK(test::write_png(path, img));
    return path;
}

Command seek_to(i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    return c;
}

std::vector<f32> parts_of(Engine& e, u64 layer) {
    std::vector<f32> v(e.query_shape3d_parts(layer, nullptr, 0));
    if (!v.empty()) e.query_shape3d_parts(layer, v.data(), static_cast<u32>(v.size()));
    return v;
}

} // namespace

AUREA_TEST(Shape3D, ImageDecodeChecksPeakBudgetAndCacheDoesNotRetainClosedSources) {
    const std::string path = checker_png("aurea-shape-budget.png");
    Image tooSmall;
    AUREA_CHECK(!load_shape3d_image(path, 64, tooSmall, 32 * 1024));
    Image first;
    AUREA_CHECK(load_shape3d_image(path, 64, first, 128 * 1024));
    std::weak_ptr<const std::vector<u8>> weak = first.sharedRgba;
    Image same;
    AUREA_CHECK(load_shape3d_image(path, 64, same, 128 * 1024));
    AUREA_CHECK(first.sharedRgba == same.sharedRgba);
    first = {}; same = {};
    AUREA_CHECK(weak.expired());
    Image reopened;
    AUREA_CHECK(load_shape3d_image(path, 64, reopened, 128 * 1024));
    AUREA_CHECK_EQ(reopened.width, 64u);
    std::remove(path.c_str());
}

AUREA_TEST(Shape3D, EveryShapeBuildsValidClosedOutwardParts) {
    constexpr u32 expected[kShape3DKindCount] = {6, 2, 3, 2, 5, 4, 6, 2, 3, 8};
    for (u32 k = 0; k < kShape3DKindCount; ++k) {
        const auto kind = static_cast<Shape3DKind>(k);
        AUREA_CHECK_EQ(shape3d_part_count(kind), expected[k]);
        auto r = build_shape3d(default_shape3d(kind));
        AUREA_CHECK(r.ok());
        if (!r.ok()) { std::printf("\n    forma %u: %s", k, r.detail.c_str()); continue; }
        const SceneAsset& a = *r.asset;
        AUREA_CHECK(a.shapeParts);
        AUREA_CHECK_EQ(a.nodes.size(), static_cast<usize>(expected[k]));
        AUREA_CHECK_EQ(a.meshes.size(), static_cast<usize>(expected[k]));
        AUREA_CHECK_EQ(a.materials.size(), static_cast<usize>(expected[k]));
        // Cabe na caixa de ~1 unidade.
        AUREA_CHECK(a.bounds.valid() && a.bounds.extent().x <= 1.001f && a.bounds.extent().y <= 1.001f && a.bounds.extent().z <= 1.001f);
        // Arestas por posição (costuras de UV duplicam vértice, não a superfície).
        std::map<std::tuple<long, long, long>, u32> ids;
        auto id_of = [&](Vec3 p) {
            const auto key = std::make_tuple(std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f));
            const auto it = ids.find(key);
            if (it != ids.end()) return it->second;
            const u32 id = static_cast<u32>(ids.size());
            ids.emplace(key, id);
            return id;
        };
        std::map<std::pair<u32, u32>, i32> edges;
        f64 volume = 0.0;
        bool normalsOk = true, uvOk = true, windingOk = true;
        for (u32 n = 0; n < a.nodes.size(); ++n) {
            AUREA_CHECK_EQ(a.nodes[n].mesh, static_cast<i32>(n));
            const Mesh& mesh = a.meshes[n];
            AUREA_CHECK_EQ(mesh.primitives.size(), usize{1});
            if (mesh.primitives.empty()) continue;
            const Primitive& p = mesh.primitives[0];
            AUREA_CHECK_EQ(p.material, static_cast<i32>(n));
            AUREA_CHECK(p.triangle_count() > 0 && p.indices.size() % 3 == 0);
            AUREA_CHECK_EQ(p.normals.size(), p.positions.size());
            AUREA_CHECK_EQ(p.uv0.size(), p.positions.size());
            for (const Vec3& nv : p.normals) normalsOk = normalsOk && std::fabs(nv.length() - 1.0f) < 1e-3f;
            for (const Vec2& t : p.uv0) uvOk = uvOk && t.x >= 0.0f && t.x <= 1.0f && t.y >= 0.0f && t.y <= 1.0f;
            for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
                const u32 i0 = p.indices[t], i1 = p.indices[t + 1], i2 = p.indices[t + 2];
                AUREA_CHECK(i0 < p.positions.size() && i1 < p.positions.size() && i2 < p.positions.size());
                const Vec3 a0 = p.positions[i0], a1 = p.positions[i1], a2 = p.positions[i2];
                volume += static_cast<f64>(a0.dot(a1.cross(a2))) / 6.0;
                const Vec3 g = (a1 - a0).cross(a2 - a0);
                windingOk = windingOk && g.dot(p.normals[i0] + p.normals[i1] + p.normals[i2]) > 0.0f;
                const u32 v[3] = {id_of(a0), id_of(a1), id_of(a2)};
                for (u32 e = 0; e < 3; ++e) {
                    const u32 x = v[e], y = v[(e + 1) % 3];
                    if (x < y) ++edges[{x, y}];
                    else --edges[{y, x}];
                }
            }
        }
        AUREA_CHECK(normalsOk);
        AUREA_CHECK(uvOk);
        AUREA_CHECK(windingOk);
        // Fechada: cada aresta é percorrida tantas vezes num sentido quanto no outro.
        u32 open = 0;
        for (const auto& [edge, balance] : edges) if (balance != 0) ++open;
        if (open) std::printf("\n    forma %s: %u arestas abertas", shape3d_kind_key(kind), open);
        AUREA_CHECK_EQ(open, 0u);
        // Virada para fora: o volume assinado é positivo.
        AUREA_CHECK(volume > 0.01);
        for (u32 i = 0; i < expected[k]; ++i) AUREA_CHECK(shape3d_part_key(kind, i) != nullptr);
    }
}

AUREA_TEST(Shape3D, RecipeRoundTripsColorsImagesAndRejectsOthers) {
    Shape3DSpec s = default_shape3d(Shape3DKind::Star);
    s.parts[2].color = Vec4{1.0f, 0.5f, 0.0f, 1.0f};
    s.parts[5].image = "docs:formas/foto;com=estranho.png";
    s.roughness = 0.2f;
    const std::string src = encode_shape3d(s);
    AUREA_CHECK(is_shape3d_source(src));
    Shape3DSpec d;
    AUREA_CHECK(decode_shape3d(src, d));
    AUREA_CHECK(d.kind == Shape3DKind::Star);
    AUREA_CHECK_EQ(d.parts.size(), usize{6});
    AUREA_CHECK(std::fabs(d.parts[2].color.y - 0.5f) < 0.01f);
    AUREA_CHECK(d.parts[5].image == s.parts[5].image);
    AUREA_CHECK(d.parts[0].image.empty());
    AUREA_CHECK(std::fabs(d.roughness - 0.2f) < 1e-3f);
    Shape3DSpec x;
    AUREA_CHECK(!decode_shape3d("aurea-text3d:v2;t=oi", x));
    AUREA_CHECK(!decode_shape3d("aurea-shape3d:v1;k=99;", x));
    // Imagem que não existe: a forma sai assim mesmo, com a falta anotada.
    auto r = build_shape3d(d);
    AUREA_CHECK(r.ok());
    if (r.ok()) AUREA_CHECK_EQ(r.asset->missingTextures.size(), usize{1});
}

AUREA_TEST(Shape3D, PartTracksMoveOnlyThatPartAndKeyframesInterpolate) {
    auto r = build_shape3d(default_shape3d(Shape3DKind::Cube));
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    Layer layer;
    layer.tracks.get_or_create(TrackProperty::ShapePart, 2, 0).staticValue = 0.3f;   // direita: X + 0,3
    auto& rot = layer.tracks.get_or_create(TrackProperty::ShapePart, 4, 4);          // topo: giro Y
    rot.set(FrameIndex{0}, 0.0f);
    rot.set(FrameIndex{20}, 90.0f);
    auto pose = a.rest_world_matrices();
    apply_shape3d_parts(a, layer, 10.0, pose);
    for (u32 i = 0; i < pose.size(); ++i) {
        const Vec3 c = a.meshes[i].bounds.center();
        const Vec3 moved = pose[i].transform_point(c);
        if (i == 2) AUREA_CHECK((moved - (c + Vec3{0.3f, 0, 0})).length() < 1e-5f);
        else AUREA_CHECK((moved - c).length() < 1e-5f);   // pivô no centro da parte
        if (i != 4) AUREA_CHECK(std::fabs(pose[i].col[0].x - 1.0f) < 1e-5f);
    }
    // Meio do caminho linear 0 → 90°: 45°.
    AUREA_CHECK(std::fabs(pose[4].col[0].x - std::cos(45.0f * kDeg2Rad)) < 1e-4f);
    // Fracionário (preview = export): 5,5 fica entre 5 e 6.
    auto a5 = a.rest_world_matrices(), a55 = a.rest_world_matrices(), a6 = a.rest_world_matrices();
    apply_shape3d_parts(a, layer, 5.0, a5);
    apply_shape3d_parts(a, layer, 5.5, a55);
    apply_shape3d_parts(a, layer, 6.0, a6);
    AUREA_CHECK(a55[4].col[0].x < a5[4].col[0].x && a55[4].col[0].x > a6[4].col[0].x);
    // Sem a bandeira de forma (modelo importado), as trilhas não fazem nada.
    SceneAsset plain = a;
    plain.shapeParts = false;
    auto still = plain.rest_world_matrices();
    apply_shape3d_parts(plain, layer, 10.0, still);
    AUREA_CHECK(std::fabs(still[2].col[3].x) < 1e-6f);
}

AUREA_TEST(Shape3D, EnginePartsKeyframeImageUndoSaveAndReopen) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const Result<u64> id = e.add_shape3d(static_cast<u32>(Shape3DKind::Star), "Estrela");
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    std::vector<f32> p = parts_of(e, *id);
    AUREA_CHECK_EQ(p.size(), static_cast<usize>(6 * Engine::kShapePartFloats));
    if (p.size() != 6u * Engine::kShapePartFloats) return;
    AUREA_CHECK(std::fabs(p[6] - 1.0f) < 1e-6f && p[13] > 0.5f);   // escala neutra, centro visível

    // Ponta 2: valor parado, sem keyframe.
    const f32 move[9] = {0.2f, 0, 0, 0, 0, 0, 1, 1, 1};
    AUREA_CHECK(e.set_shape3d_part(*id, 1, move, 1u, false));
    p = parts_of(e, *id);
    AUREA_CHECK(std::fabs(p[Engine::kShapePartFloats + 0] - 0.2f) < 1e-6f);
    AUREA_CHECK_EQ(static_cast<u32>(p[Engine::kShapePartFloats + 9]), 0u);
    // Keyframe no 0, depois no 30 com X = 0,8: no 15, a metade.
    AUREA_CHECK_EQ(e.toggle_shape3d_part_key(*id, 1), 1);
    AUREA_CHECK(e.apply_command(seek_to(30)).ok());
    const f32 far[9] = {0.8f, 0, 0, 0, 0, 0, 1, 1, 1};
    AUREA_CHECK(e.set_shape3d_part(*id, 1, far, 1u, false));
    AUREA_CHECK(e.apply_command(seek_to(15)).ok());
    p = parts_of(e, *id);
    AUREA_CHECK(std::fabs(p[Engine::kShapePartFloats + 0] - 0.5f) < 1e-4f);
    AUREA_CHECK(static_cast<u32>(p[Engine::kShapePartFloats + 9]) & 1u);
    // Só a ponta 2 anda: as outras partes seguem no neutro.
    for (u32 part = 0; part < 6; ++part) {
        if (part == 1) continue;
        AUREA_CHECK(std::fabs(p[part * Engine::kShapePartFloats]) < 1e-6f);
    }
    // Gizmo e mover pelo mundo: andar em X da composição anda a parte em X.
    f32 g[8];
    AUREA_CHECK(e.query_shape3d_part_gizmo(*id, 1, 40.0f, g));
    f32 xyz[3];
    AUREA_CHECK(e.shape3d_part_move(*id, 1, 0, 10.0f, xyz));
    AUREA_CHECK(xyz[0] > p[Engine::kShapePartFloats + 0] && std::fabs(xyz[1] - p[Engine::kShapePartFloats + 1]) < 1e-4f);

    // Imagem só na ponta 3; cor na 1.
    const std::string png = checker_png("aurea_teste_forma3d.png");
    AUREA_CHECK(e.set_shape3d_part_style(*id, 2, nullptr, png.c_str()).ok());
    const f32 red[4] = {1, 0, 0, 1};
    AUREA_CHECK(e.set_shape3d_part_style(*id, 0, red, nullptr).ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto asset_of = [&](u64 layer) {
        Composition* c = e.project()->timeline().composition(e.project()->timeline().current());
        const Layer* l = c ? c->layer(LayerId::unpack(layer)) : nullptr;
        return l ? e.model_asset(l->model.scene.pack()) : nullptr;
    };
    auto asset = asset_of(*id);
    AUREA_CHECK(asset && asset->images.size() == 1 && asset->materials[2].baseColorTex.valid());
    AUREA_CHECK(asset && !asset->materials[0].baseColorTex.valid() && asset->materials[0].baseColor.x > 0.99f);
    // Desfazer a cor mantém a imagem; desfazer de novo tira a imagem.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    asset = asset_of(*id);
    AUREA_CHECK(asset && asset->images.size() == 1);
    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());

    const std::string path = temp_file("aurea_teste_forma3d.aurea");
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    Shape3DSpec after;
    AUREA_CHECK(e.query_shape3d(*id, after));
    AUREA_CHECK(after.kind == Shape3DKind::Star && after.parts.size() == 6);
    AUREA_CHECK(!after.parts[2].image.empty() && after.parts[1].image.empty());
    AUREA_CHECK(std::fabs(after.parts[0].color.y) < 0.01f);
    asset = asset_of(*id);
    AUREA_CHECK(asset && asset->shapeParts && asset->images.size() == 1 && asset->materials[2].baseColorTex.valid());
    AUREA_CHECK(e.apply_command(seek_to(15)).ok());
    p = parts_of(e, *id);
    AUREA_CHECK(p.size() == 6u * Engine::kShapePartFloats && std::fabs(p[Engine::kShapePartFloats + 0] - 0.5f) < 1e-4f);
    // Resetar a parte apaga as trilhas dela.
    AUREA_CHECK(e.reset_shape3d_part(*id, 1));
    p = parts_of(e, *id);
    AUREA_CHECK(p.size() == 6u * Engine::kShapePartFloats && std::fabs(p[Engine::kShapePartFloats + 0]) < 1e-6f);
    (void)comp;
    e.shutdown();
}

// =============================================================================
// Animação das partes = a do texto 3D: o efeito Shape 3D Layout (cada parte no
// papel de uma letra) e os presets de animação por unidade (parte).
// =============================================================================

namespace {

/// Estrela (6 partes) com o Shape 3D Layout nos valores padrão.
struct StarLayout {
    std::unique_ptr<SceneAsset> asset;
    Layer layer;
};

StarLayout star_with_layout() {
    StarLayout s;
    auto r = build_shape3d(default_shape3d(Shape3DKind::Star));
    if (!r.ok()) return s;
    s.asset = std::move(r.asset);
    EffectRegistry registry;
    register_builtin_effects(registry);
    EffectInstance fx;
    fx.id = 1;
    fx.type = effect_type_id(effect_keys::kShape3DLayout);
    initialize_instance(fx, *registry.params(fx.type));
    s.layer.kind = LayerKind::Model3D;
    s.layer.start = FrameIndex{0};
    s.layer.end = FrameIndex{120};
    s.layer.effects.push_back(fx);
    return s;
}

std::vector<Mat4> laid(const SceneAsset& a, const Layer& l, f64 t) {
    auto m = a.rest_world_matrices();
    apply_shape3d_layout(a, l, t, m);
    return m;
}

Vec3 xyz_of(const Vec4& v) { return Vec3{v.x, v.y, v.z}; }

} // namespace

AUREA_TEST(Shape3DLayout, RotationPerPartAndRandomMoveEachPartDeterministically) {
    StarLayout s = star_with_layout();
    AUREA_CHECK(s.asset != nullptr);
    if (!s.asset) return;
    const SceneAsset& a = *s.asset;
    AUREA_CHECK_EQ(a.nodes.size(), usize{6});
    AUREA_CHECK_EQ(s.layer.effects[0].params.size(), usize{14});
    // Padrão: nada se mexe.
    auto rest = laid(a, s.layer, 10.0);
    for (const Mat4& m : rest) AUREA_CHECK(xyz_of(m.col[3]).length() < 1e-5f && std::fabs(m.col[1].y - 1.0f) < 1e-5f);
    // Rotação Y por parte = 90°: cada parte gira em volta do PRÓPRIO centro.
    s.layer.effects[0].params[1].constant = ParamValue::scalar(90.0f);
    auto turned = laid(a, s.layer, 10.0);
    for (u32 i = 0; i < 6; ++i) {
        const Vec3 c = a.meshes[i].bounds.center();
        AUREA_CHECK((turned[i].transform_point(c) - c).length() < 1e-4f);
        AUREA_CHECK(std::fabs(turned[i].col[0].x) < 1e-4f);   // X virou Z
    }
    // Primeira/última parte: só as partes 2..4 giram.
    s.layer.effects[0].params[6].constant = ParamValue::scalar(2.0f);
    s.layer.effects[0].params[7].constant = ParamValue::scalar(4.0f);
    auto range = laid(a, s.layer, 10.0);
    for (u32 i = 0; i < 6; ++i) AUREA_CHECK((std::fabs(range[i].col[0].x) < 1e-4f) == (i >= 1 && i <= 3));
    s.layer.effects[0].params[1].constant = ParamValue::scalar(0.0f);
    s.layer.effects[0].params[6].constant = ParamValue::scalar(1.0f);
    s.layer.effects[0].params[7].constant = ParamValue::scalar(256.0f);
    // Aleatório 100 %: cada parte do seu jeito, o mesmo instante = a mesma pose.
    s.layer.effects[0].params[12].constant = ParamValue::scalar(100.0f);
    auto r1 = laid(a, s.layer, 10.0), r2 = laid(a, s.layer, 10.0), r3 = laid(a, s.layer, 40.0);
    u32 distinct = 0, moving = 0;
    for (u32 i = 0; i < 6; ++i) {
        for (u32 c = 0; c < 4; ++c) AUREA_CHECK(xyz_of(r1[i].col[c] - r2[i].col[c]).length() < 1e-6f);
        if (i > 0 && std::fabs(r1[i].col[1].y - r1[0].col[1].y) > 0.05f) ++distinct;
        if (xyz_of(r3[i].col[3] - r1[i].col[3]).length() > 1e-3f) ++moving;
    }
    AUREA_CHECK(distinct >= 3u);
    AUREA_CHECK(moving >= 5u);
    // Espalhar 200 %: cada parte se afasta do centro (nos três eixos), sem girar.
    s.layer.effects[0].params[12].constant = ParamValue::scalar(0.0f);
    s.layer.effects[0].params[4].constant = ParamValue::scalar(200.0f);
    auto spread = laid(a, s.layer, 10.0);
    const Vec3 mid = a.bounds.center();
    for (u32 i = 0; i < 6; ++i) {
        const Vec3 c = a.meshes[i].bounds.center();
        const Vec3 d0 = c - mid, d1 = spread[i].transform_point(c) - mid;
        AUREA_CHECK((d1 - d0 * 2.0f).length() < 1e-4f);
    }
    // O Text 3D Layout não mexe numa forma.
    StarLayout t = star_with_layout();
    t.layer.effects[0].type = effect_type_id(effect_keys::kText3DLayout);
    t.layer.effects[0].params[1].constant = ParamValue::scalar(90.0f);
    auto none = t.asset->rest_world_matrices();
    apply_text3d_layout(*t.asset, t.layer, 10.0, none);
    apply_shape3d_layout(*t.asset, t.layer, 10.0, none);
    for (const Mat4& m : none) AUREA_CHECK(std::fabs(m.col[0].x - 1.0f) < 1e-5f);
}

AUREA_TEST(Shape3DLayout, DelayPerPartStaggersKeyedRotationAndPartTracksStillApply) {
    StarLayout s = star_with_layout();
    AUREA_CHECK(s.asset != nullptr);
    if (!s.asset) return;
    const SceneAsset& a = *s.asset;
    // Rotação X da parte: −90° no 0 → 0° no 20; 2 quadros de atraso entre partes.
    s.layer.effects[0].params[9].constant = ParamValue::scalar(2.0f);
    auto& keys = s.layer.tracks.get_or_create(TrackProperty::EffectParam, 1, param_track_key(0, 0));
    keys.set(FrameIndex{0}, -90.0f);
    keys.set(FrameIndex{20}, 0.0f);
    auto mid = laid(a, s.layer, 10.0);
    for (u32 i = 1; i < 6; ++i) AUREA_CHECK(mid[i].col[1].y < mid[i - 1].col[1].y - 1e-3f);   // as de trás ainda mais giradas
    auto done = laid(a, s.layer, 40.0);
    for (const Mat4& m : done) AUREA_CHECK(std::fabs(m.col[1].y - 1.0f) < 1e-5f);
    // Trilha da parte depois do layout: move a partir do lugar em que o layout a pôs.
    s.layer.effects[0].params[4].constant = ParamValue::scalar(200.0f);
    s.layer.tracks.get_or_create(TrackProperty::ShapePart, 0, 1).staticValue = 0.25f;   // ponta 1: Y + 0,25
    auto pose = laid(a, s.layer, 40.0);
    const Vec3 c0 = a.meshes[0].bounds.center();
    const Vec3 laidOut = pose[0].transform_point(c0);
    AUREA_CHECK((laidOut - c0).length() > 1e-3f);
    apply_shape3d_parts(a, s.layer, 40.0, pose);
    AUREA_CHECK((pose[0].transform_point(c0) - (laidOut + Vec3{0, 0.25f, 0})).length() < 1e-4f);
}

AUREA_TEST(Shape3DAnim, PopInStaggersPartsOverTimeAndSettlesAtRest) {
    auto r = build_shape3d(default_shape3d(Shape3DKind::Star));
    AUREA_CHECK(r.ok());
    if (!r.ok()) return;
    const SceneAsset& a = *r.asset;
    // A forma expõe as unidades do texto 3D: parte i = letra i = palavra i, uma linha.
    AUREA_CHECK_EQ(a.textUnits.size(), usize{18});
    AUREA_CHECK_EQ(a.textWords, 6u);
    AUREA_CHECK_EQ(a.textLines, 1u);
    AUREA_CHECK_EQ(a.textUnits[3 * 4], 4u);
    Layer l;
    l.kind = LayerKind::Model3D;
    l.start = FrameIndex{0};
    l.end = FrameIndex{120};
    // Pop de entrada: 0,5 s por parte, 100 ms (3 quadros) entre partes.
    AUREA_CHECK(apply_text3d_anim_preset(l, 3, kText3DAnimIn, 1, 6, 0.5f, 100.0f, 30.0));
    AUREA_CHECK(layeranim::has_units(l) && !layeranim::has_whole(l));
    // Escala (eixo Y do nó) e opacidade de cada parte no instante t.
    auto sample = [&](f64 t, std::vector<f32>& size, std::vector<f32>& opacity) {
        auto pose = a.rest_world_matrices();
        apply_shape3d_layout(a, l, t, pose);
        apply_shape3d_parts(a, l, t, pose);
        apply_text3d_animators(a, l, t, 30.0, pose, opacity);
        size.clear();
        for (const Mat4& m : pose) size.push_back(xyz_of(m.col[1]).length());
        if (opacity.empty()) opacity.assign(pose.size(), 1.0f);
    };
    std::vector<f32> sz, op;
    sample(0.0, sz, op);
    AUREA_CHECK_EQ(sz.size(), usize{6});
    for (u32 i = 0; i < 6; ++i) AUREA_CHECK(std::fabs(sz[i] - 1.0f) > 0.05f || op[i] < 0.5f);   // todas escondidas no início
    sample(7.0, sz, op);
    auto progress = [&](u32 i) { return (1.0f - std::fabs(sz[i] - 1.0f)) * op[i]; };
    // A parte 0 está mais adiantada que a 1, a 1 que a 2; a última ainda espera.
    AUREA_CHECK(progress(0) > progress(1) + 1e-3f);
    AUREA_CHECK(progress(1) > progress(2) + 1e-3f);
    AUREA_CHECK(std::fabs(sz[5] - 1.0f) > 0.05f || op[5] < 0.5f);
    // O mesmo instante, a mesma pose (preview = export).
    std::vector<f32> sz2, op2;
    sample(7.0, sz2, op2);
    for (u32 i = 0; i < 6; ++i) AUREA_CHECK(sz[i] == sz2[i] && op[i] == op2[i]);
    // Depois de 15 + 5 × 3 quadros tudo pousou: tamanho e opacidade de repouso.
    sample(60.0, sz, op);
    for (u32 i = 0; i < 6; ++i) {
        AUREA_CHECK_NEAR(sz[i], 1.0f, 1e-4f);
        AUREA_CHECK_NEAR(op[i], 1.0f, 1e-5f);
    }
}

AUREA_TEST(Shape3DAnim, EngineAcceptsShapeLayoutAndPartAnimationOnlyOnShapes) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const Result<u64> star = e.add_shape3d(static_cast<u32>(Shape3DKind::Star), "Estrela");
    AUREA_CHECK(star.ok());
    if (!star.ok()) return;
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*star);
    add.effect_add.effectType = effect_type_id(effect_keys::kShape3DLayout);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(add).ok());
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* layer = comp->layer(LayerId::unpack(*star));
    AUREA_CHECK(layer && layer->effects.size() == 1);
    // Text 3D Layout numa forma e Shape 3D Layout num texto 3D: recusados.
    Command wrong = add;
    wrong.effect_add.effectType = effect_type_id(effect_keys::kText3DLayout);
    AUREA_CHECK(!e.apply_command(wrong).ok());
    Text3DSpec spec;
    spec.content = "AB";
    const auto text = e.add_text3d(spec);
    AUREA_CHECK(text.ok());
    if (text.ok()) {
        Command onText = add;
        onText.effect_add.layer = LayerId::unpack(*text);
        AUREA_CHECK(!e.apply_command(onText).ok());
    }
    // Animação por parte: a unidade é sempre a parte (6).
    AUREA_CHECK(e.apply_text3d_anim(*star, 3, kText3DAnimIn, 3, 0.5f, 100.0f));
    f32 q[3 * Engine::kText3DAnimFloats];
    AUREA_CHECK(e.query_text3d_anim(*star, q));
    AUREA_CHECK_NEAR(q[0], 3.0f, 1e-6f);
    AUREA_CHECK_NEAR(q[1], 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(q[4], 6.0f, 1e-6f);
    // Salvar e reabrir: efeito e animação seguem na forma.
    const std::string path = temp_file("aurea_teste_forma3d_anim.aurea");
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    comp = e.project()->timeline().composition(e.project()->timeline().current());
    layer = comp->layer(LayerId::unpack(*star));
    AUREA_CHECK(layer && layer->effects.size() == 1 && layer->effects[0].type == effect_type_id(effect_keys::kShape3DLayout));
    AUREA_CHECK(layer && layer->layerAnimators.size() == 1);
    auto asset = layer ? e.model_asset(layer->model.scene.pack()) : nullptr;
    AUREA_CHECK(asset && asset->shapeParts && asset->textUnits.size() == 18u);
    e.shutdown();
}

// =============================================================================
// MOSTRAR INTERIOR (Model3DData::interior, v43) e a MINIATURA DE MATERIAL
// (scene3d/MaterialPreview.hpp) das listas do painel 3D.
// =============================================================================

AUREA_TEST(Shape3DInterior, DefaultsUndoSaveAndReopenKeepTheChoice) {
    // Automático: a forma pronta mostra o interior; texto 3D e modelo importado não.
    static_assert(model_interior_visible(ModelInterior::Auto, true));
    static_assert(!model_interior_visible(ModelInterior::Auto, false));
    static_assert(model_interior_visible(ModelInterior::On, false));
    static_assert(!model_interior_visible(ModelInterior::Off, true));
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const Result<u64> cube = e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    const Result<u64> keep = e.add_shape3d(static_cast<u32>(Shape3DKind::Sphere), "Esfera");
    Text3DSpec spec;
    spec.content = "IN";
    const Result<u64> text = e.add_text3d(spec);
    AUREA_CHECK(cube.ok() && keep.ok() && text.ok());
    if (!cube.ok() || !keep.ok() || !text.ok()) { e.shutdown(); return; }
    AUREA_CHECK_EQ(e.query_model_interior(*cube), 1);
    AUREA_CHECK_EQ(e.query_model_interior(*keep), 1);
    AUREA_CHECK_EQ(e.query_model_interior(*text), 0);
    // Camada que não é objeto 3D: recusa.
    AUREA_CHECK_EQ(e.query_model_interior(0), -1);
    AUREA_CHECK(!e.set_model_interior(0, true));

    // Um passo de desfazer por troca.
    AUREA_CHECK(e.set_model_interior(*cube, false));
    AUREA_CHECK_EQ(e.query_model_interior(*cube), 0);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK_EQ(e.query_model_interior(*cube), 1);
    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK_EQ(e.query_model_interior(*cube), 0);
    AUREA_CHECK(e.set_model_interior(*text, true));
    AUREA_CHECK_EQ(e.query_model_interior(*text), 1);

    // Salvar e reabrir: desligado explícito, automático e ligado explícito.
    const std::string path = temp_file("aurea_teste_interior.aurea");
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    AUREA_CHECK_EQ(e.query_model_interior(*cube), 0);
    AUREA_CHECK_EQ(e.query_model_interior(*keep), 1);
    AUREA_CHECK_EQ(e.query_model_interior(*text), 1);
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    auto mode = [&](u64 id) {
        const Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
        return l ? static_cast<int>(l->model.interior) : -1;
    };
    AUREA_CHECK_EQ(mode(*cube), static_cast<int>(ModelInterior::Off));
    AUREA_CHECK_EQ(mode(*keep), static_cast<int>(ModelInterior::Auto));
    AUREA_CHECK_EQ(mode(*text), static_cast<int>(ModelInterior::On));
    std::remove(path.c_str());
    e.shutdown();
}

namespace {

struct BallStats {
    f32 r = 0, g = 0, b = 0;   ///< média da cor onde a bola cobre (0..255)
    u32 opaque = 0;            ///< pixels com alfa cheio
    u32 reds = 0, blues = 0;   ///< pixels nitidamente vermelhos / azuis
};

BallStats ball_stats(const std::vector<u8>& rgba, u32 size) {
    BallStats s;
    u32 n = 0;
    for (u32 i = 0; i < size * size; ++i) {
        const u8* p = &rgba[i * 4u];
        if (p[3] < 255) continue;
        ++s.opaque; ++n;
        s.r += p[0]; s.g += p[1]; s.b += p[2];
        if (p[0] > p[2] + 40) ++s.reds;
        if (p[2] > p[0] + 40) ++s.blues;
    }
    if (n) { s.r /= static_cast<f32>(n); s.g /= static_cast<f32>(n); s.b /= static_cast<f32>(n); }
    return s;
}

} // namespace

AUREA_TEST(MaterialPreview, PresetBallsAreRoundDistinctAndDeterministic) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    constexpr u32 size = 64;
    std::vector<std::vector<u8>> balls(7);
    for (u32 p = 0; p < 7; ++p) {
        AUREA_CHECK(e.text3d_preset_preview(p, size, balls[p]));
        AUREA_CHECK_EQ(balls[p].size(), static_cast<usize>(size * size * 4));
        if (balls[p].size() != size * size * 4) { e.shutdown(); return; }
        // Fundo transparente nos cantos, bola cheia no meio.
        AUREA_CHECK_EQ(balls[p][3], 0);
        AUREA_CHECK_EQ(balls[p][(size * size - 1) * 4 + 3], 0);
        AUREA_CHECK_EQ(balls[p][((size / 2) * size + size / 2) * 4 + 3], 255);
        AUREA_CHECK(ball_stats(balls[p], size).opaque > size * size / 2);
    }
    // Fora da faixa: nada.
    std::vector<u8> none;
    AUREA_CHECK(!e.text3d_preset_preview(7, size, none) && none.empty());
    AUREA_CHECK(!e.text3d_preset_preview(0, 8, none) && none.empty());
    AUREA_CHECK(!e.text3d_preset_preview(0, 512, none) && none.empty());
    const BallStats chrome = ball_stats(balls[0], size), gold = ball_stats(balls[1], size),
                    glossy = ball_stats(balls[3], size), matte = ball_stats(balls[4], size), neon = ball_stats(balls[5], size);
    std::printf("\n    cromado %.0f,%.0f,%.0f ouro %.0f,%.0f,%.0f vermelho %.0f,%.0f,%.0f fosco %.0f,%.0f,%.0f neon %.0f,%.0f,%.0f",
                chrome.r, chrome.g, chrome.b, gold.r, gold.g, gold.b, glossy.r, glossy.g, glossy.b, matte.r, matte.g, matte.b,
                neon.r, neon.g, neon.b);
    // O ouro é quente; o cromado é neutro; o vermelho brilhante é vermelho; o neon é ciano e claro.
    AUREA_CHECK(gold.r > gold.b + 30.0f);
    AUREA_CHECK(std::fabs(chrome.r - chrome.b) < 20.0f);
    AUREA_CHECK(glossy.r > glossy.g + 40.0f && glossy.r > glossy.b + 40.0f);
    AUREA_CHECK(neon.g > neon.r + 40.0f && neon.b > neon.r + 40.0f);
    // Todos diferentes entre si (a lista não mostra duas bolas iguais).
    for (u32 a = 0; a < 7; ++a)
        for (u32 b = a + 1; b < 7; ++b) AUREA_CHECK(balls[a] != balls[b]);
    // Determinística: o mesmo pedido devolve os mesmos bytes (e passa pelo cache).
    std::vector<u8> again;
    AUREA_CHECK(e.text3d_preset_preview(1, size, again));
    AUREA_CHECK(again == balls[1]);
    MaterialBall gold1;
    AUREA_CHECK(text3d_preset_ball(1, gold1));
    AUREA_CHECK(render_material_ball(gold1, size) == balls[1]);
    e.shutdown();
}

AUREA_TEST(MaterialPreview, LayerBallShowsPartImageColorAndMaterialEdits) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const Result<u64> id = e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    AUREA_CHECK(id.ok());
    if (!id.ok()) { e.shutdown(); return; }
    constexpr u32 size = 96;
    std::vector<u8> plain, textured, recolored, metal, other;
    // Parte 1 (de trás) é a referência: a imagem vai só na frente (parte 0).
    AUREA_CHECK(e.material_preview(*id, 0, size, plain));
    AUREA_CHECK(e.material_preview(*id, 1, size, other));
    AUREA_CHECK_EQ(plain.size(), static_cast<usize>(size * size * 4));
    AUREA_CHECK(plain != other);   // cada parte tem a cor dela na paleta padrão
    const std::string png = checker_png("aurea_teste_bola_material.png");
    const f32 white[4] = {1, 1, 1, 1};
    AUREA_CHECK(e.set_shape3d_part_style(*id, 0, white, png.c_str()).ok());
    AUREA_CHECK(e.material_preview(*id, 0, size, textured));
    const BallStats t = ball_stats(textured, size);
    std::printf("\n    bola com imagem: vermelho %u, azul %u (de %u)", t.reds, t.blues, t.opaque);
    // A textura aplicada aparece na bola: as duas cores do quadriculado.
    AUREA_CHECK(t.reds > 300 && t.blues > 300);
    std::vector<u8> back;
    AUREA_CHECK(e.material_preview(*id, 1, size, back));
    AUREA_CHECK(back == other);    // só a parte que mudou muda
    // Cor da parte tinge a imagem: o quadriculado fica sem azul.
    const f32 red[4] = {1, 0, 0, 1};
    AUREA_CHECK(e.set_shape3d_part_style(*id, 0, red, nullptr).ok());
    AUREA_CHECK(e.material_preview(*id, 0, size, recolored));
    const BallStats rc = ball_stats(recolored, size);
    AUREA_CHECK(rc.blues < t.blues / 4 && recolored != textured);
    // Ajuste de material da camada (o mesmo da lista do modelo importado).
    AUREA_CHECK(e.set_material_param(*id, 1, 4, 1.0f).ok());   // metal
    AUREA_CHECK(e.set_material_param(*id, 1, 5, 0.05f).ok());  // rugosidade
    AUREA_CHECK(e.material_preview(*id, 1, size, metal));
    AUREA_CHECK(metal != other);
    // Fora da faixa / sem material / não é 3D: nada.
    std::vector<u8> none;
    AUREA_CHECK(!e.material_preview(*id, 6, size, none) && none.empty());
    AUREA_CHECK(!e.material_preview(*id, 0, 4, none) && none.empty());
    AUREA_CHECK(!e.material_preview(0, 0, size, none) && none.empty());
    std::remove(png.c_str());
    e.shutdown();
}

#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(Gpu, Shape3DInteriorShowsTexturedInnerFacesFromInsideTheCube) {
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    if (!e.initialize(ec).ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    AUREA_CHECK(e.new_project(256, 144, 30.0, nullptr).ok());
    const Result<u64> id = e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    AUREA_CHECK(id.ok());
    if (!id.ok()) { e.shutdown(); return; }
    // A mesma imagem nas seis faces (parte −1 = todas).
    const std::string png = checker_png("aurea_teste_interior_gpu.png");
    const f32 white[4] = {1, 1, 1, 1};
    AUREA_CHECK(e.set_shape3d_part_style(*id, -1, white, png.c_str()).ok());
    auto capture = [&] {
        test::Image8 img;
        AUREA_CHECK(e.capture_frame_rgba(256, img.rgba, img.width, img.height).ok());
        return img;
    };
    struct Look { f32 coverage = 0; u32 reds = 0, blues = 0; };
    auto look = [](const test::Image8& img) {
        Look l;
        u32 lit = 0;
        for (u32 y = 0; y < img.height; ++y)
            for (u32 x = 0; x < img.width; ++x) {
                const u8* q = img.at(x, y);
                if (q[0] > 8 || q[1] > 8 || q[2] > 8) ++lit;
                if (q[0] > q[2] + 30) ++l.reds;
                if (q[2] > q[0] + 30) ++l.blues;
            }
        l.coverage = static_cast<f32>(lit) / static_cast<f32>(std::max(1u, img.width * img.height));
        return l;
    };
    auto set_scale = [&](f32 s) {
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* l = comp ? comp->layer(LayerId::unpack(*id)) : nullptr;
        AUREA_CHECK(l != nullptr);
        // Escala Z da camada é relativa à X (a profundidade acompanha): {s, s, 1} = cubo s× maior.
        if (l) l->transform.scale = Vec3{s, s, 1.0f};
        e.request_render();
    };

    // De FORA: ligar o interior não muda nada (a face de trás fica atrás da da frente).
    AUREA_CHECK_EQ(e.query_model_interior(*id), 1);
    const test::Image8 outsideOn = capture();
    AUREA_CHECK(e.set_model_interior(*id, false));
    const test::Image8 outsideOff = capture();
    u32 worst = 0;
    for (usize i = 0; i < outsideOn.rgba.size() && i < outsideOff.rgba.size(); ++i)
        worst = std::max(worst, static_cast<u32>(std::abs(int(outsideOn.rgba[i]) - int(outsideOff.rgba[i]))));
    std::printf("\n    de fora: maior diferença ligado/desligado %u", worst);
    AUREA_CHECK(look(outsideOn).coverage > 0.05f);
    AUREA_CHECK(worst <= 4u);

    // DENTRO: cubo 12× maior (aresta 0,45·12 da altura; a câmera padrão fica a
    // 1,2 alturas do centro) — a câmera está dentro do cubo.
    set_scale(12.0f);
    const Look off = look(capture());
    std::printf("\n    dentro, interior desligado: cobertura %.3f", off.coverage);
    // Sem o interior todas as faces estão de costas para a câmera: nada desenhado.
    AUREA_CHECK(off.coverage < 0.01f);
    AUREA_CHECK(e.set_model_interior(*id, true));
    const Look on = look(capture());
    std::printf("\n    dentro, interior ligado: cobertura %.3f, vermelho %u, azul %u", on.coverage, on.reds, on.blues);
    // Com o interior: as faces de dentro cobrem o quadro, com a imagem (as
    // duas cores do quadriculado) — não só a cor chapada.
    AUREA_CHECK(on.coverage > 0.95f);
    AUREA_CHECK(on.reds > 500 && on.blues > 500);
    // Desfazer volta a recortar.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    set_scale(12.0f);
    AUREA_CHECK_EQ(e.query_model_interior(*id), 0);
    AUREA_CHECK(look(capture()).coverage < 0.01f);
    std::remove(png.c_str());
    e.shutdown();
}
#endif

#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(Gpu, Shape3DTexturedCubeRendersAndMovingOneFaceChangesTheFrame) {
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    if (!e.initialize(ec).ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    AUREA_CHECK(e.new_project(256, 144, 30.0, nullptr).ok());
    const Result<u64> id = e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    AUREA_CHECK(id.ok());
    if (!id.ok()) { e.shutdown(); return; }
    auto capture = [&] {
        test::Image8 img;
        AUREA_CHECK(e.capture_frame_rgba(256, img.rgba, img.width, img.height).ok());
        return img;
    };
    auto diff = [](const test::Image8& a, const test::Image8& b) {
        if (a.width != b.width || a.height != b.height || a.width == 0) return 1.0f;
        u32 n = 0;
        for (u32 y = 0; y < a.height; ++y)
            for (u32 x = 0; x < a.width; ++x) {
                const u8* p = a.at(x, y);
                const u8* q = b.at(x, y);
                if (std::abs(int(p[0]) - int(q[0])) + std::abs(int(p[1]) - int(q[1])) + std::abs(int(p[2]) - int(q[2])) > 24) ++n;
            }
        return static_cast<f32>(n) / static_cast<f32>(a.width * a.height);
    };
    const test::Image8 plain = capture();
    // Cobertura: o cubo aparece (fundo preto).
    u32 lit = 0;
    for (u32 y = 0; y < plain.height; ++y)
        for (u32 x = 0; x < plain.width; ++x) { const u8* q = plain.at(x, y); if (q[0] > 8 || q[1] > 8 || q[2] > 8) ++lit; }
    const f32 coverage = static_cast<f32>(lit) / static_cast<f32>(std::max(1u, plain.width * plain.height));
    std::printf("\n    cubo cobre %.3f do quadro", coverage);
    AUREA_CHECK(coverage > 0.05f && coverage < 0.9f);
    // Imagem na FRENTE: o quadriculado aparece (vermelho e azul no meio).
    const std::string png = checker_png("aurea_teste_forma3d_gpu.png");
    const f32 white[4] = {1, 1, 1, 1};
    AUREA_CHECK(e.set_shape3d_part_style(*id, 0, white, png.c_str()).ok());
    const test::Image8 textured = capture();
    const f32 dTex = diff(plain, textured);
    u32 reds = 0, blues = 0;
    for (u32 y = textured.height / 3; y < textured.height * 2 / 3; ++y)
        for (u32 x = textured.width / 3; x < textured.width * 2 / 3; ++x) {
            const u8* q = textured.at(x, y);
            if (q[0] > q[2] + 40) ++reds;
            if (q[2] > q[0] + 40) ++blues;
        }
    std::printf("\n    imagem na frente: %.3f do quadro mudou (vermelho %u, azul %u)", dTex, reds, blues);
    AUREA_CHECK(dTex > 0.02f);
    AUREA_CHECK(reds > 20 && blues > 20);
    // Só a FRENTE anda para a direita: o quadro muda e o resto do cubo fica.
    const f32 move[9] = {0.8f, 0, 0.4f, 0, 0, 0, 1, 1, 1};
    AUREA_CHECK(e.set_shape3d_part(*id, 0, move, 0b101u, false));
    const test::Image8 moved = capture();
    const f32 dMove = diff(textured, moved);
    std::printf("\n    frente movida: %.3f do quadro mudou", dMove);
    AUREA_CHECK(dMove > 0.02f);
    e.shutdown();
}
#endif
