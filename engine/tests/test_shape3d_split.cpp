// Dividir o cubo 3D em partes (Shape3D.hpp: split_shape3d_spec; Engine::
// split_shape3d): a receita da fatia (caixa + UV contínua), as N camadas
// filhas de um nulo que herda o transform/keyframes, o quadro idêntico logo
// depois de dividir (mesma caixa no mundo), um passo de desfazer e o projeto
// salvo/reaberto com as fatias.
#include "TestFramework.hpp"
#include "ImageIO.hpp"

#include "aurea/Engine.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/scene3d/Shape3D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;

namespace {

std::string split_temp(const char* name) {
    const char* t = std::getenv("TEMP");
    return std::string(t ? t : ".") + "/" + name;
}

std::string split_png(const char* name) {
    test::Image8 img;
    img.width = img.height = 32;
    img.rgba.assign(32u * 32u * 4u, 200);
    const std::string path = split_temp(name);
    AUREA_CHECK(test::write_png(path, img));
    return path;
}

f32 axis_of(Vec3 v, u32 a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

/// Faixa de u (min, max) da face `part` na malha.
void u_range(const SceneAsset& a, u32 part, f32& lo, f32& hi) {
    lo = 1e9f; hi = -1e9f;
    for (const Vec2& t : a.meshes[part].primitives[0].uv0) { lo = std::min(lo, t.x); hi = std::max(hi, t.x); }
}

/// Mundo ← modelo (o layer_from_model do renderer).
Mat4 world_of(const Composition& c, const Layer& l, FrameIndex t) {
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    return layer_world_3d(c, l, t) * flip * Mat4::scale(Vec3{l.model.unitScale, l.model.unitScale, l.model.unitScale})
         * Mat4::translation(-l.model.pivot);
}

/// Os 8 cantos da caixa [lo, hi] (modelo) levados ao mundo.
std::vector<Vec3> world_corners(const Mat4& m, Vec3 lo, Vec3 hi) {
    std::vector<Vec3> out;
    for (u32 i = 0; i < 8; ++i) {
        const Vec3 p{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
        out.push_back(m.transform_point(p));
    }
    return out;
}

bool near3(Vec3 a, Vec3 b, f32 eps) { return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps; }

Composition* comp_of(Engine& e) { return e.project()->timeline().composition(e.project()->timeline().current()); }

Command seek_split(i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    return c;
}

} // namespace

AUREA_TEST(Shape3DSplit, RecipeSlicesPartitionBoxAndUvAndRoundTrip) {
    Shape3DSpec cube = default_shape3d(Shape3DKind::Cube);
    cube.parts[0].image = "docs:frente.png";
    AUREA_CHECK(shape3d_full_box(cube));
    // Fora da faixa ou forma que não é cubo: nada.
    AUREA_CHECK(split_shape3d_spec(cube, 0, 1).empty());
    AUREA_CHECK(split_shape3d_spec(cube, 0, kShape3DSplitMax + 1).empty());
    AUREA_CHECK(split_shape3d_spec(cube, 3, 3).empty());
    AUREA_CHECK(split_shape3d_spec(default_shape3d(Shape3DKind::Sphere), 0, 3).empty());

    for (u32 axis = 0; axis < 3; ++axis) {
        const std::vector<Shape3DSpec> s = split_shape3d_spec(cube, axis, 4);
        AUREA_CHECK_EQ(s.size(), static_cast<usize>(4));
        if (s.size() != 4) continue;
        for (u32 i = 0; i < 4; ++i) {
            // Fatias encostadas, cobrindo −0,5..0,5 no eixo; os outros eixos inteiros.
            AUREA_CHECK(std::fabs(axis_of(s[i].boxMin, axis) - (-0.5f + 0.25f * i)) < 1e-6f);
            AUREA_CHECK(std::fabs(axis_of(s[i].boxMax, axis) - (-0.25f + 0.25f * i)) < 1e-6f);
            if (i > 0) AUREA_CHECK(axis_of(s[i].boxMin, axis) == axis_of(s[i - 1].boxMax, axis));
            for (u32 o = 0; o < 3; ++o) {
                if (o == axis) continue;
                AUREA_CHECK(axis_of(s[i].boxMin, o) == -0.5f && axis_of(s[i].boxMax, o) == 0.5f);
            }
            // Mesmo material por face; a receita volta igual (com a caixa).
            AUREA_CHECK(s[i].parts.size() == 6 && s[i].parts[0].image == "docs:frente.png");
            Shape3DSpec back;
            AUREA_CHECK(decode_shape3d(encode_shape3d(s[i]), back));
            AUREA_CHECK(near3(back.boxMin, s[i].boxMin, 1e-5f) && near3(back.boxMax, s[i].boxMax, 1e-5f));
            AUREA_CHECK(back.parts[0].image == "docs:frente.png" && !shape3d_full_box(back));
        }
    }
    // Receita antiga (sem a chave "b") = cubo inteiro; o cubo inteiro não grava "b".
    AUREA_CHECK(encode_shape3d(cube).find(";b=") == std::string::npos);
    Shape3DSpec old;
    AUREA_CHECK(decode_shape3d("aurea-shape3d:v1;k=0;m=0.000/0.450;", old) && shape3d_full_box(old));
    // Dividir uma fatia de novo divide só a caixa dela.
    const std::vector<Shape3DSpec> s2 = split_shape3d_spec(split_shape3d_spec(cube, 0, 2)[1], 1, 2);
    AUREA_CHECK(s2.size() == 2 && s2[0].boxMin.x == 0.0f && s2[0].boxMax.y == 0.0f && s2[1].boxMin.y == 0.0f);

    // Malha: caixa da fatia e UV = pedaço do cubo inteiro. Frente (0), trás (1),
    // topo (4) e base (5) cortadas em X: a faixa de u de cada fatia é a sua
    // parte de 0..1, e juntas cobrem 0..1 sem sobrar nem faltar.
    const std::vector<Shape3DSpec> s = split_shape3d_spec(cube, 0, 4);
    for (u32 part : {0u, 1u, 4u, 5u}) {
        f32 covered = 0.0f;
        for (u32 i = 0; i < 4; ++i) {
            Shape3DSpec si = s[i];
            si.parts[0].image.clear();
            auto r = build_shape3d(si);
            AUREA_CHECK(r.ok());
            if (!r.ok()) continue;
            const SceneAsset& a = *r.asset;
            AUREA_CHECK(near3(a.bounds.min, si.boxMin, 1e-5f) && near3(a.bounds.max, si.boxMax, 1e-5f));
            f32 lo = 0, hi = 0;
            u_range(a, part, lo, hi);
            const u32 k = part == 1 ? 3 - i : i;   // a trás lê X ao contrário
            AUREA_CHECK(std::fabs(lo - 0.25f * k) < 1e-5f && std::fabs(hi - 0.25f * (k + 1)) < 1e-5f);
            covered += hi - lo;
            // As faces do corte (direita/esquerda) usam a imagem da face de fora inteira.
            f32 rl = 0, rh = 0;
            u_range(a, 2, rl, rh);
            AUREA_CHECK(std::fabs(rl) < 1e-5f && std::fabs(rh - 1.0f) < 1e-5f);
        }
        AUREA_CHECK(std::fabs(covered - 1.0f) < 1e-4f);
    }
}

AUREA_TEST(Shape3DSplit, EngineSplitsIntoChildSlicesSameFrameUndoSaveReopen) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const Result<u64> id = e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    const std::string png = split_png("aurea_teste_cubo_fatias.png");
    AUREA_CHECK(e.set_shape3d_part_style(*id, 0, nullptr, png.c_str()).ok());
    const f32 tint[4] = {0.2f, 0.9f, 0.4f, 1.0f};
    AUREA_CHECK(e.set_shape3d_part_style(*id, 4, tint, nullptr).ok());

    // Transform qualquer + keyframe de rotação Y (o grupo tem de levar).
    Composition* comp = comp_of(e);
    Layer* l = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    l->transform.position = Vec3{140.0f, 70.0f, -30.0f};
    l->transform.rotation = Vec3{20.0f, 0.0f, 10.0f};
    l->transform.scale = Vec3{1.3f, 0.8f, 1.0f};
    Track& ry = l->tracks.get_or_create(TrackProperty::RotationY);
    ry.set(FrameIndex{0}, 0.0f);
    ry.set(FrameIndex{30}, 90.0f);
    l->transform.opacity = 0.75f;
    const FrameIndex t{12};
    AUREA_CHECK(e.apply_command(seek_split(t.value)).ok());
    comp = comp_of(e);
    l = comp->layer(LayerId::unpack(*id));
    const Mat4 origWorld = world_of(*comp, *l, t);
    const std::vector<Vec3> origCorners = world_corners(origWorld, Vec3{-0.5f, -0.5f, -0.5f}, Vec3{0.5f, 0.5f, 0.5f});
    const u32 layersBefore = comp->layers().count();

    // Fora da faixa / não cubo: recusa sem mexer.
    AUREA_CHECK(!e.split_shape3d(*id, 0, 1).ok());
    AUREA_CHECK(!e.split_shape3d(*id, 3, 3).ok());
    const Result<u64> sphere = e.add_shape3d(static_cast<u32>(Shape3DKind::Sphere), "Esfera");
    AUREA_CHECK(sphere.ok() && !e.split_shape3d(*sphere, 0, 3).ok());
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());   // tira a esfera
    comp = comp_of(e);
    AUREA_CHECK_EQ(comp->layers().count(), layersBefore);

    std::vector<u64> slices;
    const Result<u64> group = e.split_shape3d(*id, 0, 3, &slices);
    AUREA_CHECK(group.ok());
    if (!group.ok()) return;
    AUREA_CHECK_EQ(*group, *id);
    AUREA_CHECK_EQ(slices.size(), static_cast<usize>(3));
    if (slices.size() != 3) return;

    auto check_split = [&](const char* when) {
        Composition* c = comp_of(e);
        const Layer* g = c ? c->layer(LayerId::unpack(*id)) : nullptr;
        AUREA_CHECK(g && g->kind == LayerKind::Null && g->threeD);
        if (!g) { std::printf("\n    %s: grupo sumiu", when); return; }
        // O grupo levou transform e keyframe; nada de opacidade/efeito nele.
        AUREA_CHECK(g->tracks.find(TrackProperty::RotationY) && g->tracks.find(TrackProperty::RotationY)->keys.size() == 2);
        AUREA_CHECK(std::fabs(g->transform.rotation.x - 20.0f) < 1e-4f && std::fabs(g->transform.scale.x - 1.3f) < 1e-4f);
        AUREA_CHECK(g->effects.empty());
        AUREA_CHECK_EQ(c->layers().count(), layersBefore + 3);
        Shape3DSpec gs;
        AUREA_CHECK(!e.query_shape3d(*id, gs));   // o nulo não é mais forma
        Vec3 wmin{1e9f, 1e9f, 1e9f}, wmax{-1e9f, -1e9f, -1e9f};
        f32 last = -1.0f;
        // As fatias = filhas do grupo, na ordem do eixo (reabrir o projeto
        // pode renumerar os ids das camadas; o vínculo de pai é o que vale).
        std::vector<std::pair<f32, u64>> kids;
        for (u32 k = 0; k < c->order().size(); ++k) {
            const LayerId lid = c->order().at(k);
            const Layer* s = c->layer(lid);
            Shape3DSpec ks;
            if (s && s->parent == LayerId::unpack(*id) && e.query_shape3d(lid.pack(), ks)) kids.emplace_back(ks.boxMin.x, lid.pack());
        }
        std::sort(kids.begin(), kids.end());
        AUREA_CHECK_EQ(kids.size(), static_cast<usize>(3));
        if (kids.size() != 3) return;
        for (u32 i = 0; i < 3; ++i) {
            const Layer* s = c->layer(LayerId::unpack(kids[i].second));
            AUREA_CHECK(s && s->kind == LayerKind::Model3D && s->parent == LayerId::unpack(*id));
            if (!s) continue;
            AUREA_CHECK(!s->tracks.find(TrackProperty::RotationY));   // keyframe só no grupo
            AUREA_CHECK(std::fabs(s->transform.opacity - 0.75f) < 1e-6f);
            AUREA_CHECK(s->start == g->start && s->end == g->end);
            Shape3DSpec sp;
            AUREA_CHECK(e.query_shape3d(kids[i].second, sp));
            AUREA_CHECK(sp.kind == Shape3DKind::Cube && !sp.parts[0].image.empty() && sp.parts[4].color.y > 0.85f);
            AUREA_CHECK(sp.boxMin.x > last - 1e-6f);   // na ordem do eixo
            last = sp.boxMax.x;
            // Pivô no centro da fatia.
            AUREA_CHECK(near3(s->model.pivot, (sp.boxMin + sp.boxMax) * 0.5f, 1e-5f));
            const auto asset = e.model_asset(s->model.scene.pack());
            AUREA_CHECK(asset && asset->materials.size() == 6 && asset->materials[0].baseColorTex.valid());
            // Mesmo lugar no mundo: cantos da fatia = cantos do cubo original
            // interpolados no eixo X do modelo.
            const Mat4 w = world_of(*c, *s, t);
            const std::vector<Vec3> got = world_corners(w, sp.boxMin, sp.boxMax);
            const std::vector<Vec3> want = world_corners(origWorld, sp.boxMin, sp.boxMax);
            for (u32 k = 0; k < 8; ++k) {
                AUREA_CHECK(near3(got[k], want[k], 0.05f));
                wmin = Vec3{std::min(wmin.x, got[k].x), std::min(wmin.y, got[k].y), std::min(wmin.z, got[k].z)};
                wmax = Vec3{std::max(wmax.x, got[k].x), std::max(wmax.y, got[k].y), std::max(wmax.z, got[k].z)};
            }
        }
        AUREA_CHECK(std::fabs(last - 0.5f) < 1e-6f);
        // A caixa das fatias juntas = a do cubo original no mundo.
        Vec3 omin{1e9f, 1e9f, 1e9f}, omax{-1e9f, -1e9f, -1e9f};
        for (const Vec3& p : origCorners) {
            omin = Vec3{std::min(omin.x, p.x), std::min(omin.y, p.y), std::min(omin.z, p.z)};
            omax = Vec3{std::max(omax.x, p.x), std::max(omax.y, p.y), std::max(omax.z, p.z)};
        }
        AUREA_CHECK(near3(wmin, omin, 0.05f) && near3(wmax, omax, 0.05f));
    };
    check_split("dividido");

    // Uma fatia gira sozinha em volta do próprio centro.
    {
        Composition* c = comp_of(e);
        Layer* s = c->layer(LayerId::unpack(slices[1]));
        const Vec3 before = world_of(*c, *s, t).transform_point(s->model.pivot);
        s->transform.rotation = Vec3{0.0f, 45.0f, 0.0f};
        const Vec3 after = world_of(*c, *s, t).transform_point(s->model.pivot);
        AUREA_CHECK(near3(before, after, 0.05f));
        s->transform.rotation = Vec3{0.0f, 0.0f, 0.0f};
    }

    // Desfazer: um passo volta ao cubo único (com imagem); refazer divide de novo.
    AUREA_CHECK(e.apply_command(undo).ok());
    comp = comp_of(e);
    AUREA_CHECK_EQ(comp->layers().count(), layersBefore);
    const Layer* back = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK(back && back->kind == LayerKind::Model3D);
    Shape3DSpec whole;
    AUREA_CHECK(e.query_shape3d(*id, whole) && shape3d_full_box(whole) && !whole.parts[0].image.empty());
    for (u64 s : slices) AUREA_CHECK(comp->layer(LayerId::unpack(s)) == nullptr);
    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());
    check_split("refeito");

    // Salvar e reabrir: fatias, grupo e o mesmo lugar no mundo.
    const std::string path = split_temp("aurea_teste_cubo_fatias.aurea");
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    AUREA_CHECK(e.apply_command(seek_split(t.value)).ok());
    check_split("reaberto");
    e.shutdown();
}

// A caixa de seleção de uma parte (modelo 3D filho do nulo do grupo) tem de
// cair em cima da parte na tela. Antes o motor não mandava cantos de modelo 3D
// e a UI montava a caixa só com a posição local da camada — longe do objeto.
AUREA_TEST(Shape3DSplit, SliceSelectionCornersFollowTheParentNull) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const Result<u64> id = e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    std::vector<u64> slices;
    AUREA_CHECK(e.split_shape3d(*id, 0, 3, &slices).ok());
    AUREA_CHECK_EQ(slices.size(), static_cast<usize>(3));
    if (slices.size() != 3) return;
    auto center = [&](u64 layer, f32& cx, f32& cy) {
        bridge::LayerDetailPOD d{};
        AUREA_CHECK(e.query_layer_detail(layer, d));
        AUREA_CHECK((d.geomFlags & bridge::kGeomCornersValid) != 0);
        cx = (d.corners[0] + d.corners[2] + d.corners[4] + d.corners[6]) * 0.25f;
        cy = (d.corners[1] + d.corners[3] + d.corners[5] + d.corners[7]) * 0.25f;
    };
    f32 x[3]{}, y[3]{};
    for (u32 i = 0; i < 3; ++i) center(slices[i], x[i], y[i]);
    std::printf("    partes na tela: (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f)\n", x[0], y[0], x[1], y[1], x[2], y[2]);
    AUREA_CHECK(x[0] < x[1] && x[1] < x[2]);                          // da esquerda para a direita
    AUREA_CHECK(std::fabs(x[1] - 160.0f) < 3.0f && std::fabs(y[1] - 90.0f) < 3.0f);   // a do meio no centro
    // Mover o nulo do grupo leva a caixa da parte junto.
    Layer* group = comp_of(e)->layer(LayerId::unpack(*id));
    AUREA_CHECK(group != nullptr);
    if (!group) return;
    group->transform.position.x += 50.0f;
    f32 mx = 0, my = 0;
    center(slices[1], mx, my);
    std::printf("    nulo +50 px: parte do meio (%.1f,%.1f), trilha X %d\n", mx, my, group->tracks.find(TrackProperty::PositionX) ? 1 : 0);
    // +50 px no mundo; na tela um pouco mais (o cubo fica à frente do plano
    // Z=0 da câmera padrão: perspectiva).
    AUREA_CHECK(mx - x[1] > 45.0f && mx - x[1] < 65.0f && std::fabs(my - y[1]) < 3.0f);
}
