// =============================================================================
//  Pontos do vetor 3D no palco (EngineVector3D.cpp).
//   - projeção dos pontos e alças pela câmera do palco e a volta pelo plano da
//     camada (raio × plano): ida e volta com a camada girada, com câmera girada
//     e com a vista de navegação da Cena 3D;
//   - a mesma conta do render (layer_comp_matrix) e a homografia do
//     vector_path_at (bit3 + linha de perspectiva);
//   - ponto atrás da câmera e plano de lado são recusados;
//   - arrastar → caminho muda → um desfazer → salvar e reabrir; com keyframes
//     de forma, o arrasto grava o key do cabeçote.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/timeline/ShapeGeometry.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;

namespace {

struct Vec3dRig {
    Engine e;
    bool ok = false;
    Vec3dRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(1280, 720, 30.0, nullptr).ok();
    }
    ~Vec3dRig() { if (ok) e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* layer(u64 id) { Composition* c = comp(); return c ? c->layer(LayerId::unpack(id)) : nullptr; }
    /// A camada vetorial do projeto (depois de reabrir, sem depender do id).
    u64 first_vector() {
        Composition* c = comp();
        if (!c) return 0;
        for (u32 i = 0; i < c->order().size(); ++i) {
            const Layer* l = c->layer(c->order().at(i));
            if (l && l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector) return c->order().at(i).pack();
        }
        return 0;
    }
    bool command(CommandType t) {
        Command c;
        c.type = t;
        return e.apply_command(c).ok();
    }
    void seek(i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    }
    /// Camada vetorial com um caminho livre curvo de 4 pontos, 3D e girada.
    u64 shape(Vec3 rotation) {
        const auto id = e.add_vector_layer(0);
        AUREA_CHECK(id.ok());
        if (!id.ok()) return 0;
        using Op = Engine::PathOp;
        const f32 pts[4][3] = {{500, 260, 0}, {780, 260, 0}, {780, 460, 0}, {500, 460, 0}};
        for (const auto& p : pts) (void)e.edit_vector_path(*id, 0, 0, Op::Append, p, 3, false);
        const f32 curve[2] = {1, 1};
        (void)e.edit_vector_path(*id, 0, 0, Op::SetType, curve, 2, false);
        Layer* l = layer(*id);
        if (l) {
            l->threeD = true;
            l->transform.rotation = rotation;
        }
        return *id;
    }
    std::vector<f32> stage(u64 id) {
        std::vector<f32> s;
        AUREA_CHECK(e.vector_path_stage_points(id, 0, 0, s));
        return s;
    }
    BezierPath path(u64 id) {
        Layer* l = layer(id);
        return l ? l->shape.vector.groups[0].paths[0].path : BezierPath{};
    }
};

/// Cada ponto e alça visível: tela → plano → o mesmo ponto do grupo.
void check_round_trip(Vec3dRig& r, u64 id, f32 tol) {
    const std::vector<f32> s = r.stage(id);
    AUREA_CHECK(s.size() >= 3);
    if (s.size() < 3) return;
    const BezierPath b = r.path(id);
    const usize n = static_cast<usize>(s[0]);
    AUREA_CHECK_EQ(n, b.v.size());
    AUREA_CHECK_EQ(s.size(), 3 + n * Engine::kVectorStageFloats);
    if (s.size() != 3 + n * Engine::kVectorStageFloats) return;
    AUREA_CHECK(s[1] > 0.5f);   // 3D
    u32 seen = 0;
    for (usize i = 0; i < n; ++i) {
        const Vec2 local[3] = {b.v[i].p, b.v[i].p + b.v[i].in, b.v[i].p + b.v[i].out};
        for (u32 k = 0; k < 3; ++k) {
            const f32* q = &s[3 + i * Engine::kVectorStageFloats + k * 3];
            if (q[2] < 0.5f) continue;
            Vec2 g;
            AUREA_CHECK(r.e.vector_path_unproject(id, 0, q[0], q[1], g));
            AUREA_CHECK_NEAR(g.x, local[k].x, tol);
            AUREA_CHECK_NEAR(g.y, local[k].y, tol);
            ++seen;
        }
    }
    AUREA_CHECK(seen >= n);
}

/// O ponto do grupo pela conta do render (layer_comp_matrix), dividido por w.
bool render_projection(Vec3dRig& r, u64 id, Vec2 g, Vec2& out) {
    Composition* c = r.comp();
    Layer* l = r.layer(id);
    if (!c || !l) return false;
    const FrameIndex now{0};   // os testes que comparam com o render ficam no quadro 0
    bool persp = false;
    const Mat4 m = layer_comp_matrix(*c, *l, now, &persp);
    // A camada vetorial nasce com o grupo na identidade (sem posição/escala de grupo).
    const Vec4 q = m * Vec4{g.x, g.y, 0, 1};
    if (!(q.w > 1e-3f)) return false;
    out = Vec2{q.x / q.w, q.y / q.w};
    return persp;
}

} // namespace

AUREA_TEST(Vector3DPoints, RotatedLayerRoundTripsThroughTheLayerPlane) {
    Vec3dRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 id = r.shape(Vec3{25.0f, 40.0f, 10.0f});
    AUREA_CHECK(id != 0);
    check_round_trip(r, id, 0.05f);
    // A projeção do palco é a do render (câmera padrão).
    const std::vector<f32> s = r.stage(id);
    const BezierPath b = r.path(id);
    for (usize i = 0; i < b.v.size(); ++i) {
        Vec2 ref;
        AUREA_CHECK(render_projection(r, id, b.v[i].p, ref));
        AUREA_CHECK_NEAR(s[3 + i * Engine::kVectorStageFloats], ref.x, 0.05);
        AUREA_CHECK_NEAR(s[4 + i * Engine::kVectorStageFloats], ref.y, 0.05);
    }
    // vector_path_at: bit3 e a homografia dão os mesmos pontos na tela.
    std::vector<f32> at;
    AUREA_CHECK(r.e.vector_path_at(id, 0, 0, at));
    AUREA_CHECK(at.size() == 7 + 2 + b.v.size() * 6 + 3);
    if (at.size() == 7 + 2 + b.v.size() * 6 + 3) {
        AUREA_CHECK((static_cast<u32>(at[6]) & 8u) != 0);
        const f32* w = &at[at.size() - 3];
        for (usize i = 0; i < b.v.size(); ++i) {
            const Vec2 p = b.v[i].p;
            const f32 d = w[0] * p.x + w[1] * p.y + w[2];
            AUREA_CHECK(d > 0.0f);
            AUREA_CHECK_NEAR((at[0] * p.x + at[2] * p.y + at[4]) / d, s[3 + i * Engine::kVectorStageFloats], 0.05);
            AUREA_CHECK_NEAR((at[1] * p.x + at[3] * p.y + at[5]) / d, s[4 + i * Engine::kVectorStageFloats], 0.05);
        }
    }
    // Camada 2D: sem bit3 e sem a linha a mais (o formato de antes).
    Layer* l = r.layer(id);
    if (l) { l->threeD = false; l->force2D = true; l->transform.rotation = Vec3{0, 0, 15}; }
    std::vector<f32> flat;
    AUREA_CHECK(r.e.vector_path_at(id, 0, 0, flat));
    AUREA_CHECK((static_cast<u32>(flat[6]) & 8u) == 0);
    AUREA_CHECK_EQ(flat.size(), 7 + 2 + b.v.size() * 6);
    const std::vector<f32> s2 = r.stage(id);
    AUREA_CHECK(s2.size() > 3 && s2[1] < 0.5f);
    Vec2 g;
    AUREA_CHECK(r.e.vector_path_unproject(id, 0, s2[3], s2[4], g));
    AUREA_CHECK_NEAR(g.x, b.v[0].p.x, 0.05);
    AUREA_CHECK_NEAR(g.y, b.v[0].p.y, 0.05);
}

AUREA_TEST(Vector3DPoints, RotatedCameraAndNavigationViewRoundTrip) {
    Vec3dRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 id = r.shape(Vec3{-20.0f, 30.0f, 0.0f});
    const auto cam = r.e.add_camera();
    AUREA_CHECK(cam.ok());
    if (!cam.ok()) return;
    Layer* c = r.layer(*cam);
    AUREA_CHECK(c != nullptr);
    if (c) {
        c->transform.rotation = Vec3{12.0f, -25.0f, 5.0f};
        c->transform.position.x += 180.0f;
    }
    check_round_trip(r, id, 0.08f);
    const BezierPath b = r.path(id);
    const std::vector<f32> s = r.stage(id);
    Vec2 ref;
    AUREA_CHECK(render_projection(r, id, b.v[2].p, ref));
    AUREA_CHECK_NEAR(s[3 + 2 * Engine::kVectorStageFloats], ref.x, 0.08);
    AUREA_CHECK_NEAR(s[4 + 2 * Engine::kVectorStageFloats], ref.y, 0.08);
    // A vista de navegação da Cena 3D é o que o palco mostra: a conta segue ela.
    r.e.set_scene_editor(true, -35.0f, 25.0f, 3.0f);
    const std::vector<f32> nav = r.stage(id);
    AUREA_CHECK(std::fabs(nav[3] - s[3]) + std::fabs(nav[4] - s[4]) > 1.0f);
    check_round_trip(r, id, 0.08f);
    r.e.set_scene_editor(false, 0, 0, 3);
}

AUREA_TEST(Vector3DPoints, BehindCameraAndEdgeOnAreRejected) {
    Vec3dRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 id = r.shape(Vec3{0.0f, 0.0f, 0.0f});
    Layer* l = r.layer(id);
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    // Plano de lado para a câmera (90° em Y, no eixo dela): o dedo não acha o plano.
    l->transform.rotation = Vec3{0.0f, 90.0f, 0.0f};
    Vec2 g;
    AUREA_CHECK(!r.e.vector_path_unproject(id, 0, 640.0f, 360.0f, g));
    const u32 pt[1] = {0};
    AUREA_CHECK_EQ(r.e.drag_vector_path_on_stage(id, 0, 0, 0, pt, 1, 640, 360, 700, 360, 0, false), -1);
    // A camada inteira atrás da câmera padrão (z = −1,2 × altura): tudo invisível e recusado.
    l->transform.rotation = Vec3{0.0f, 0.0f, 0.0f};
    l->transform.position.z = -2.0f * 720.0f;
    const std::vector<f32> s = r.stage(id);
    AUREA_CHECK(s.size() > 3);
    for (usize i = 0; i < static_cast<usize>(s[0]); ++i) AUREA_CHECK(s[3 + i * Engine::kVectorStageFloats + 2] < 0.5f);
    AUREA_CHECK(!r.e.vector_path_unproject(id, 0, 640.0f, 360.0f, g));
    // Metade atrás: girada 80° em Y e empurrada para a câmera — uma ponta some, a outra fica.
    l->transform.position.z = -720.0f * 1.2f + 60.0f;
    l->transform.rotation = Vec3{0.0f, 80.0f, 0.0f};
    const std::vector<f32> half = r.stage(id);
    u32 vis = 0, hidden = 0;
    for (usize i = 0; i < static_cast<usize>(half[0]); ++i) (half[3 + i * Engine::kVectorStageFloats + 2] > 0.5f ? vis : hidden)++;
    AUREA_CHECK(vis > 0);
    AUREA_CHECK(hidden > 0);
    // Nada mudou no caminho com as recusas.
    AUREA_CHECK(r.path(id).v.size() == 4);
}

AUREA_TEST(Vector3DPoints, DragMovesOnThePlaneWithOneUndoAndSurvivesSave) {
    Vec3dRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 id = r.shape(Vec3{15.0f, 45.0f, 0.0f});
    const BezierPath before = r.path(id);
    std::vector<f32> s = r.stage(id);
    const f32 x0 = s[3], y0 = s[4];
    // Arrasto do ponto 0 em dois passos (o 2º continua o mesmo passo de desfazer).
    const u32 pt[1] = {0};
    AUREA_CHECK(r.e.drag_vector_path_on_stage(id, 0, 0, 0, pt, 1, x0, y0, x0 + 20, y0 + 10, 0, false) >= 0);
    AUREA_CHECK(r.e.drag_vector_path_on_stage(id, 0, 0, 0, pt, 1, x0 + 20, y0 + 10, x0 + 40, y0 + 25, 0, true) >= 0);
    s = r.stage(id);
    // O ponto fica sob o dedo (no plano, em perspectiva); os outros não andam.
    AUREA_CHECK_NEAR(s[3], x0 + 40, 0.1);
    AUREA_CHECK_NEAR(s[4], y0 + 25, 0.1);
    BezierPath after = r.path(id);
    AUREA_CHECK(std::fabs(after.v[0].p.x - before.v[0].p.x) > 1.0f);
    AUREA_CHECK(after.v[1] == before.v[1]);
    // Alça de saída do ponto 1, com o ângulo travado: a de entrada fica na mesma reta.
    const f32* h = &s[3 + 1 * Engine::kVectorStageFloats + 6];
    AUREA_CHECK(h[2] > 0.5f);
    AUREA_CHECK_EQ(r.e.drag_vector_path_on_stage(id, 0, 0, 2, pt, 0, 0, 0, 1, 1, 0, false), -1);   // sem índice
    const u32 one[1] = {1};
    AUREA_CHECK(r.e.drag_vector_path_on_stage(id, 0, 0, 2, one, 1, h[0], h[1], h[0] + 30, h[1] - 15, 1, false) >= 0);
    after = r.path(id);
    const Vec2 in = after.v[1].in, out = after.v[1].out;
    AUREA_CHECK_NEAR(in.x * out.y - in.y * out.x, 0.0, 0.05 * in.length() * out.length() + 1e-3);
    AUREA_CHECK(in.x * out.x + in.y * out.y < 0.0f);
    // Um desfazer tira a alça; outro tira o arrasto inteiro do ponto.
    AUREA_CHECK(r.command(CommandType::Undo));
    AUREA_CHECK(r.path(id).v[1] == before.v[1]);
    AUREA_CHECK(std::fabs(r.path(id).v[0].p.x - before.v[0].p.x) > 1.0f);
    AUREA_CHECK(r.command(CommandType::Undo));
    AUREA_CHECK(r.path(id).v[0] == before.v[0]);
    AUREA_CHECK(r.command(CommandType::Redo));
    const BezierPath moved = r.path(id);
    AUREA_CHECK(std::fabs(moved.v[0].p.x - before.v[0].p.x) > 1.0f);
    // Salvar e reabrir: o ponto movido (no plano, planar) fica.
    const std::string file = (std::filesystem::temp_directory_path() / "aurea_vector3d_points.aurea").string();
    AUREA_CHECK(r.e.save_project(file.c_str()).ok());
    AUREA_CHECK(r.e.load_project(file.c_str()).ok());
    const u64 back = r.first_vector();
    AUREA_CHECK(back != 0);
    const BezierPath reopened = r.path(back);
    AUREA_CHECK(reopened.v.size() == moved.v.size());
    if (!reopened.v.empty()) {
        AUREA_CHECK_NEAR(reopened.v[0].p.x, moved.v[0].p.x, 1e-3);
        AUREA_CHECK_NEAR(reopened.v[0].p.y, moved.v[0].p.y, 1e-3);
    }
    Layer* l = r.layer(back);
    AUREA_CHECK(l && l->threeD);
    std::error_code ec;
    std::filesystem::remove(file, ec);
}

AUREA_TEST(Vector3DPoints, DragWithShapeKeysWritesThePlayheadKey) {
    Vec3dRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u64 id = r.shape(Vec3{0.0f, 35.0f, 0.0f});
    AUREA_CHECK(r.e.toggle_vector_path_key(id, 0, 0));   // key no quadro 0
    r.seek(20);
    const std::vector<f32> s = r.stage(id);
    const u32 pt[1] = {2};
    const f32* q = &s[3 + 2 * Engine::kVectorStageFloats];
    AUREA_CHECK(r.e.drag_vector_path_on_stage(id, 0, 0, 0, pt, 1, q[0], q[1], q[0] - 30, q[1] + 20, 0, false) >= 0);
    Layer* l = r.layer(id);
    AUREA_CHECK(l != nullptr);
    if (!l) return;
    const VectorPath& vp = l->shape.vector.groups[0].paths[0];
    AUREA_CHECK_EQ(vp.keys.size(), 2u);
    if (vp.keys.size() == 2) {
        const i64 local = l->local_time(FrameIndex{20}).value;
        AUREA_CHECK_EQ(vp.keys[1].frame, local);
        AUREA_CHECK(!(vp.keys[1].path.v[2] == vp.keys[0].path.v[2]));
        AUREA_CHECK(vp.keys[1].path.v[0] == vp.keys[0].path.v[0]);
    }
    // O ponto arrastado no cabeçote fica sob o dedo.
    const std::vector<f32> s2 = r.stage(id);
    AUREA_CHECK_NEAR(s2[3 + 2 * Engine::kVectorStageFloats], q[0] - 30, 0.1);
    AUREA_CHECK_NEAR(s2[4 + 2 * Engine::kVectorStageFloats], q[1] + 20, 0.1);
    AUREA_CHECK(r.command(CommandType::Undo));
    l = r.layer(id);
    if (l) AUREA_CHECK_EQ(l->shape.vector.groups[0].paths[0].keys.size(), 1u);
}
