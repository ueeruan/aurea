// Cena 3D: escolher o objeto com o dedo (Engine::scene_pick).
//
// Relato beta: "depois de deixar forma e texto em 3D e mexer no Z, não consigo
// mais selecionar para posicionar ou escalar". A seleção usava só a origem do
// modelo (36 dp) e, no plano, os cantos 2D; agora é o raio da câmera de
// navegação contra o corpo real. Aqui: qualquer Z, qualquer órbita, o mais
// perto ganha, e modelos (texto 3D, forma 3D) pegam longe da origem.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/core/Trackball.hpp"
#include "aurea/scene3d/Text3D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace aurea;

namespace {

void seek(Engine& e, i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(c).ok());
}

struct PickRig {
    Engine e;
    bool ok = false;
    PickRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        ok = e.initialize(ec).ok() && e.new_project(1080, 1920, 30.0, nullptr).ok();
    }
    ~PickRig() { if (ok) e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    LayerId square(Vec3 position, Vec3 rotation = {}) {
        const LayerId id = comp()->add_layer(LayerKind::Shape, "quadrado 3D");
        Layer* l = comp()->layer(id);
        l->threeD = true;
        l->shape.shapeType = 0;
        l->shape.bounds = Rect{0.0f, 0.0f, 300.0f, 300.0f};
        l->shape.filled = true;
        l->transform.anchor = Vec3{150.0f, 150.0f, 0.0f};
        l->transform.position = position;
        l->transform.rotation = rotation;
        l->end = FrameIndex{120};
        return id;
    }
    /// Centro do quadrilátero visto pela câmera de navegação (o que a tela mostra).
    bool center(LayerId id, f32& x, f32& y) {
        bridge::LayerDetailPOD d{};
        if (!e.query_layer_detail(id.pack(), d) || !(d.geomFlags & bridge::kGeomCornersValid)) return false;
        x = (d.corners[0] + d.corners[2] + d.corners[4] + d.corners[6]) * 0.25f;
        y = (d.corners[1] + d.corners[3] + d.corners[5] + d.corners[7]) * 0.25f;
        return true;
    }
};

struct View { f32 yaw, pitch, distance; };
constexpr View kViews[] = {{-30, 20, 3}, {0, 0, 3}, {60, -40, 2}, {170, 10, 4}, {-120, 75, 1.5f}, {45, -80, 6}};

} // namespace

AUREA_TEST(ScenePick, PlaneLayerAtAnyDepthAndOrbit) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const f32 depths[] = {0.0f, -600.0f, 900.0f, 2500.0f};
    for (f32 z : depths) {
        const LayerId id = r.square(Vec3{700.0f, 500.0f, z}, Vec3{15.0f, -30.0f, 10.0f});
        seek(r.e, 0);
        for (const View& v : kViews) {
            r.e.set_scene_editor(true, v.yaw, v.pitch, v.distance);
            f32 x = 0, y = 0;
            if (!r.center(id, x, y)) continue;   // atrás da câmera de navegação: nada a tocar
            if (x < 0 || y < 0 || x > 1080 || y > 1920) continue;
            AUREA_CHECK_EQ(r.e.scene_pick(x, y, 0.0f), id.pack());
        }
        // Vazio longe de tudo: nada (raio pequeno).
        r.e.set_scene_editor(true, -30, 20, 3);
        AUREA_CHECK_EQ(r.e.scene_pick(2.0f, 2.0f, 4.0f), 0ull);
        r.comp()->remove_layer(id);
    }
}

AUREA_TEST(ScenePick, NearestBodyWinsAndFlipsWithTheOrbit) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    // Dois quadrados no mesmo X/Y: um perto da frente (Z negativo = para a
    // câmera padrão), outro atrás. A ordem da timeline não decide: o raio sim.
    const LayerId back = r.square(Vec3{540.0f, 960.0f, 400.0f});
    const LayerId front = r.square(Vec3{540.0f, 960.0f, -400.0f});
    seek(r.e, 0);
    r.e.set_scene_editor(true, 0, 0, 3);
    f32 x = 0, y = 0;
    AUREA_CHECK(r.center(front, x, y));
    AUREA_CHECK_EQ(r.e.scene_pick(x, y, 0.0f), front.pack());
    // Do outro lado da cena, o "de trás" fica na frente.
    r.e.set_scene_editor(true, 180, 0, 3);
    AUREA_CHECK(r.center(back, x, y));
    AUREA_CHECK_EQ(r.e.scene_pick(x, y, 0.0f), back.pack());
    // Travada ou escondida não se escolhe.
    r.comp()->layer(back)->locked = true;
    AUREA_CHECK_EQ(r.e.scene_pick(x, y, 0.0f), front.pack());
    r.comp()->layer(front)->visible = false;
    AUREA_CHECK_EQ(r.e.scene_pick(x, y, 0.0f), 0ull);
}

AUREA_TEST(ScenePick, Text3DAndShape3DPickFarFromTheOrigin) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    scene3d::Text3DSpec spec;
    spec.content = "Shawnwesley";
    const auto text = r.e.add_text3d(spec);
    AUREA_CHECK(text.ok()); if (!text.ok()) return;
    const auto shape = r.e.add_shape3d(0);
    AUREA_CHECK(shape.ok()); if (!shape.ok()) return;
    // Forma 3D acima, texto 3D em Z diferente: "mexi no Z em vários lugares".
    r.comp()->layer(LayerId::unpack(*shape))->transform.position = Vec3{540.0f, 400.0f, 700.0f};
    r.comp()->layer(LayerId::unpack(*text))->transform.position = Vec3{540.0f, 960.0f, -500.0f};
    seek(r.e, 0);
    for (const View& v : kViews) {
        r.e.set_scene_editor(true, v.yaw, v.pitch, v.distance);
        for (u64 id : {*text, *shape}) {
            f32 g[8]{};
            if (!r.e.query_gizmo(id, 1.0f, g)) continue;
            const f32 ox = g[0], oy = g[1];
            if (ox < 0 || oy < 0 || ox > 1080 || oy > 1920) continue;
            // Varre a linha da origem: o corpo inteiro responde, não só o pivô.
            f32 lo = 1e9f, hi = -1e9f;
            for (f32 x = 0; x <= 1080; x += 4) {
                if (r.e.scene_pick(x, oy, 0.0f) == id) { lo = std::min(lo, x); hi = std::max(hi, x); }
            }
            AUREA_CHECK(hi > lo);
            std::printf("    vista %.0f/%.0f %s: origem %.0f,%.0f pega de %.0f a %.0f\n", v.yaw, v.pitch, id == *text ? "texto" : "forma", ox, oy, lo, hi);
            if (id == *text && v.pitch > -60 && v.pitch < 60 && std::fabs(std::remainder(v.yaw, 180.0f)) < 45) {
                // Texto comprido visto de frente/meio de lado: pega bem longe da origem.
                AUREA_CHECK(hi - lo > 120.0f);
            }
        }
    }
}

AUREA_TEST(ScenePick, BodylessObjectsPickByOrigin) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const auto null = r.e.add_null(true); AUREA_CHECK(null.ok()); if (!null.ok()) return;
    r.comp()->layer(LayerId::unpack(*null))->transform.position.z = 800.0f;
    seek(r.e, 0);
    r.e.set_scene_editor(true, 60, -30, 3);
    f32 g[8]{};
    AUREA_CHECK(r.e.query_gizmo(*null, 1.0f, g));
    AUREA_CHECK_EQ(r.e.scene_pick(g[0] + 10.0f, g[1] - 10.0f, 30.0f), *null);
    AUREA_CHECK_EQ(r.e.scene_pick(g[0] + 80.0f, g[1], 30.0f), 0ull);
}

// -----------------------------------------------------------------------------
// 0.4.6: vistas do palco como no After Effects (Engine::set_view_mode). Só a
// prévia muda; cada modo lembra a própria órbita; a cena 3D vence o palco.
// -----------------------------------------------------------------------------

AUREA_TEST(ScenePick, ViewModesFollowAfterEffectsAndRememberTheirOrbit) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    f32 v[5]{};
    r.e.query_view(v);
    AUREA_CHECK_EQ(v[0], 0.0f);                       // começa na câmera ativa
    AUREA_CHECK(!r.e.set_view_orbit(10, 10, 2));      // câmera ativa não orbita
    AUREA_CHECK(!r.e.set_view_mode(Engine::kViewModeCount));
    AUREA_CHECK(r.e.set_view_mode(9));                // Vista personalizada 2
    AUREA_CHECK(r.e.set_view_orbit(70, -20, 4));
    AUREA_CHECK(r.e.set_view_mode(8));
    r.e.query_view(v);
    AUREA_CHECK_EQ(v[1], -45.0f);                     // a 1 tem a própria órbita
    AUREA_CHECK(r.e.set_view_mode(9));
    r.e.query_view(v);
    AUREA_CHECK_EQ(v[1], 70.0f); AUREA_CHECK_EQ(v[2], -20.0f); AUREA_CHECK_EQ(v[3], 4.0f);
    AUREA_CHECK_EQ(v[4], 0.0f);
    // Frente: sem perspectiva; o orbitar só muda o zoom.
    AUREA_CHECK(r.e.set_view_mode(2));
    AUREA_CHECK(r.e.set_view_orbit(90, 45, 2.0f));
    r.e.query_view(v);
    AUREA_CHECK_EQ(v[4], 1.0f); AUREA_CHECK_EQ(v[1], 0.0f); AUREA_CHECK_EQ(v[3], 2.0f);
    // A cena 3D vence; ao sair, volta a vista do palco (as guias mostram qual vale).
    std::vector<f32> front(4096), scene(4096), again(4096);
    const u32 nFront = r.e.query_scene_guides(front.data(), 800);
    r.e.set_scene_editor(true, -30, 20, 3);
    const u32 nScene = r.e.query_scene_guides(scene.data(), 800);
    r.e.set_scene_editor(false, 0, 0, 3);
    const u32 nAgain = r.e.query_scene_guides(again.data(), 800);
    AUREA_CHECK(nFront > 0 && nScene > 0);
    AUREA_CHECK(std::vector<f32>(front.begin(), front.begin() + nFront * 5) != std::vector<f32>(scene.begin(), scene.begin() + nScene * 5));
    AUREA_CHECK(nAgain == nFront && std::equal(front.begin(), front.begin() + nFront * 5, again.begin()));
    AUREA_CHECK(r.e.set_view_mode(0));
    AUREA_CHECK_EQ(r.e.query_scene_guides(again.data(), 800), 0u);   // câmera ativa: sem vista de navegação
}

AUREA_TEST(ScenePick, OrthographicViewsLookAlongTheirAxis) {
    const u32 w = 1080, h = 1920;
    auto project = [&](SceneViewKind kind, Vec3 p, f32& x, f32& y) {
        SceneEditorView view; view.enabled = true; view.kind = kind; view.distance = 1.0f;
        const Mat4 m = scene_editor_projection(w, h, view);
        const Vec4 c = m * Vec4{p.x, p.y, p.z, 1};
        x = c.x / c.w; y = c.y / c.w;
    };
    const Vec3 center{w * 0.5f, h * 0.5f, 0};
    f32 x = 0, y = 0;
    for (SceneViewKind k : {SceneViewKind::Front, SceneViewKind::Back, SceneViewKind::Left, SceneViewKind::Right,
                            SceneViewKind::Top, SceneViewKind::Bottom}) {
        project(k, center, x, y);
        AUREA_CHECK(std::fabs(x - w * 0.5f) < 0.5f && std::fabs(y - h * 0.5f) < 0.5f);   // olha o centro
        SceneEditorView ortho; ortho.enabled = true; ortho.kind = k; ortho.distance = 1.0f;
        AUREA_CHECK(scene_editor_camera(w, h, ortho).orthographic);
    }
    // Frente: profundidade não move (sem perspectiva); x da cena = x da tela.
    project(SceneViewKind::Front, center + Vec3{100, 0, 5000}, x, y);
    AUREA_CHECK(std::fabs(x - (w * 0.5f + 100)) < 0.5f && std::fabs(y - h * 0.5f) < 0.5f);
    // Trás: espelhado em x.
    project(SceneViewKind::Back, center + Vec3{100, 0, 0}, x, y);
    AUREA_CHECK(std::fabs(x - (w * 0.5f - 100)) < 0.5f);
    // Topo: o que está mais longe da câmera padrão (+z) sobe na tela.
    project(SceneViewKind::Top, center + Vec3{0, 0, 200}, x, y);
    AUREA_CHECK(y < h * 0.5f - 100);
    // Esquerda e direita veem z em lados opostos.
    f32 left = 0, right = 0;
    project(SceneViewKind::Left, center + Vec3{0, 0, 200}, left, y);
    project(SceneViewKind::Right, center + Vec3{0, 0, 200}, right, y);
    AUREA_CHECK((left - w * 0.5f) * (right - w * 0.5f) < 0.0f);
    // Câmera padrão = a da composição sem camadas de câmera.
    SceneEditorView def; def.enabled = true; def.kind = SceneViewKind::DefaultCamera;
    const auto a = scene_editor_camera(w, h, def), b = scene3d::default_camera(w, h);
    AUREA_CHECK(std::fabs(a.position.z - b.position.z) < 1e-3f && !a.orthographic);
}

// --- Aurea PC, Cena 3D no jeito do Blender (12/11/2026) -------------------------------
// Órbita ortográfica (Numpad 5), limite do pitch para Topo/Base e enquadrar a seleção
// (Numpad .). Tudo só na vista de navegação; quem não chama as funções novas fica igual.

AUREA_TEST(SceneViewBlender, DefaultsStayAsBefore) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId id = r.square(Vec3{800.0f, 500.0f, 600.0f});
    seek(r.e, 0);
    // Sem as opções novas: pitch preso em ±80° e perspectiva, como sempre.
    r.e.set_scene_editor(true, 10, 90, 3);
    f32 v[8]{};
    r.e.query_scene_view(v);
    AUREA_CHECK(std::fabs(v[1] - 80.0f) < 1e-4f);
    AUREA_CHECK(v[3] == 0.0f);
    AUREA_CHECK(std::fabs(v[7] - 80.0f) < 1e-4f);
    f32 before[8]{}, after[8]{};
    r.e.set_scene_editor(true, -30, 20, 3);
    AUREA_CHECK(r.e.query_gizmo(id.pack(), 100.0f, before));
    // Pedir "perspectiva, 80°" é o estado padrão: nada na tela muda.
    r.e.set_scene_view_options(false, 80.0f);
    r.e.set_scene_editor(true, -30, 20, 3);
    AUREA_CHECK(r.e.query_gizmo(id.pack(), 100.0f, after));
    for (int i = 0; i < 8; ++i) AUREA_CHECK(std::fabs(before[i] - after[i]) < 1e-3f);
}

AUREA_TEST(SceneViewBlender, OrthographicOrbitAndPitchLimit) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    // Mesmo X/Y, profundidades diferentes, fora do centro.
    const LayerId nearSq = r.square(Vec3{800.0f, 500.0f, -400.0f});
    const LayerId farSq = r.square(Vec3{800.0f, 500.0f, 900.0f});
    const LayerId onPlane = r.square(Vec3{800.0f, 500.0f, 0.0f});
    seek(r.e, 0);
    f32 a[8]{}, b[8]{}, p[8]{}, o[8]{};
    r.e.set_scene_editor(true, 0, 0, 3);
    AUREA_CHECK(r.e.query_gizmo(nearSq.pack(), 100.0f, a));
    AUREA_CHECK(r.e.query_gizmo(farSq.pack(), 100.0f, b));
    AUREA_CHECK(r.e.query_gizmo(onPlane.pack(), 100.0f, p));
    // Perspectiva: o de trás fica mais perto do centro que o da frente.
    AUREA_CHECK(std::fabs(a[0] - b[0]) > 5.0f);
    r.e.set_scene_view_options(true, 89.5f);
    f32 v[8]{};
    r.e.query_scene_view(v);
    AUREA_CHECK(v[3] == 1.0f);
    AUREA_CHECK(r.e.query_gizmo(nearSq.pack(), 100.0f, a));
    AUREA_CHECK(r.e.query_gizmo(farSq.pack(), 100.0f, b));
    AUREA_CHECK(r.e.query_gizmo(onPlane.pack(), 100.0f, o));
    // Ortográfica de frente: a profundidade não mexe na tela.
    AUREA_CHECK(std::fabs(a[0] - b[0]) < 0.5f);
    AUREA_CHECK(std::fabs(a[1] - b[1]) < 0.5f);
    // Alternar a projeção não muda o zoom no plano do alvo.
    AUREA_CHECK(std::fabs(o[0] - p[0]) < 1.5f);
    AUREA_CHECK(std::fabs(o[1] - p[1]) < 1.5f);
    // A órbita seguinte mantém a ortográfica e aceita o Topo (quase 90°).
    r.e.set_scene_editor(true, 0, 90, 3);
    r.e.query_scene_view(v);
    AUREA_CHECK(v[3] == 1.0f);
    AUREA_CHECK(std::fabs(v[1] - 89.5f) < 1e-3f);
    // De cima, X continua X na tela e o Y do mundo (altura) some: os três empilham em Z.
    AUREA_CHECK(r.e.query_gizmo(nearSq.pack(), 100.0f, a));
    AUREA_CHECK(r.e.query_gizmo(farSq.pack(), 100.0f, b));
    AUREA_CHECK(std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(b[0]) && std::isfinite(b[1]));
    AUREA_CHECK(std::fabs(a[0] - b[0]) < 0.5f);
    AUREA_CHECK(std::fabs(a[1] - b[1]) > 100.0f);
    // O toque continua pegando o objeto na vista ortográfica.
    r.e.set_scene_editor(true, 0, 0, 3);
    f32 x = 0, y = 0;
    AUREA_CHECK(r.center(nearSq, x, y));
    AUREA_CHECK_EQ(r.e.scene_pick(x, y, 0.0f), nearSq.pack());
    // Voltar à perspectiva desfaz tudo.
    r.e.set_scene_view_options(false, 80.0f);
    r.e.set_scene_editor(true, 0, 90, 3);
    r.e.query_scene_view(v);
    AUREA_CHECK(v[3] == 0.0f);
    AUREA_CHECK(std::fabs(v[1] - 80.0f) < 1e-4f);
}

AUREA_TEST(SceneViewBlender, FocusPutsTheSelectionAtTheOrbitCenter) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId id = r.square(Vec3{200.0f, 300.0f, 900.0f});
    const LayerId other = r.square(Vec3{600.0f, 700.0f, -100.0f});
    const LayerId flat = r.comp()->add_layer(LayerKind::Shape, "plana 2D");
    seek(r.e, 0);
    r.e.set_scene_editor(true, -30, 20, 3);
    AUREA_CHECK(!r.e.focus_scene_view(nullptr, 0));
    const u64 none[] = {flat.pack()};
    AUREA_CHECK(!r.e.focus_scene_view(none, 1));
    const u64 one[] = {id.pack()};
    AUREA_CHECK(r.e.focus_scene_view(one, 1));
    f32 v[8]{};
    r.e.query_scene_view(v);
    AUREA_CHECK(std::fabs(v[4] - (200.0f - 540.0f)) < 0.01f);
    AUREA_CHECK(std::fabs(v[5] - (300.0f - 960.0f)) < 0.01f);
    AUREA_CHECK(std::fabs(v[6] - 900.0f) < 0.01f);
    // Em qualquer órbita o objeto fica no centro do quadro: a vista gira em volta dele.
    for (const View& view : kViews) {
        r.e.set_scene_editor(true, view.yaw, view.pitch, view.distance);
        f32 g[8]{};
        AUREA_CHECK(r.e.query_gizmo(id.pack(), 100.0f, g));
        AUREA_CHECK(std::fabs(g[0] - 540.0f) < 1.0f);
        AUREA_CHECK(std::fabs(g[1] - 960.0f) < 1.0f);
    }
    // Duas camadas: o alvo é o centro delas.
    const u64 two[] = {id.pack(), other.pack(), flat.pack()};
    AUREA_CHECK(r.e.focus_scene_view(two, 3));
    r.e.query_scene_view(v);
    AUREA_CHECK(std::fabs(v[4] - (400.0f - 540.0f)) < 0.01f);
    AUREA_CHECK(std::fabs(v[5] - (500.0f - 960.0f)) < 0.01f);
    AUREA_CHECK(std::fabs(v[6] - 400.0f) < 0.01f);
    // "Vista inicial" continua zerando o alvo.
    r.e.reset_scene_pan();
    r.e.query_scene_view(v);
    AUREA_CHECK(v[4] == 0.0f && v[5] == 0.0f && v[6] == 0.0f);
}

// --- Aurea PC, Cena 3D fase 1: caixas, pivôs, cursor 3D e alinhar a câmera ----------------

AUREA_TEST(SceneViewBlender, LayerAndSceneBoundsFrameTheView) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId sq = r.square(Vec3{800.0f, 500.0f, 600.0f});
    const auto cube = r.e.add_shape3d(0);
    AUREA_CHECK(cube.ok()); if (!cube.ok()) return;
    r.comp()->layer(LayerId::unpack(*cube))->transform.position = Vec3{300.0f, 1400.0f, -200.0f};
    seek(r.e, 0);
    r.e.set_scene_editor(true, -30, 20, 3);
    f32 b[Engine::kLayerBoundsFloats]{};
    AUREA_CHECK(!r.e.query_layer_bounds(sq.pack(), b, 10));
    AUREA_CHECK(r.e.query_layer_bounds(sq.pack(), b, Engine::kLayerBoundsFloats));
    // Quadrado 300x300 com a ancora no meio: a caixa do mundo e exata, sem espessura.
    AUREA_CHECK(std::fabs(b[0] - 650.0f) < 0.01f && std::fabs(b[1] - 350.0f) < 0.01f && std::fabs(b[2] - 600.0f) < 0.01f);
    AUREA_CHECK(std::fabs(b[3] - 950.0f) < 0.01f && std::fabs(b[4] - 650.0f) < 0.01f && std::fabs(b[5] - 600.0f) < 0.01f);
    AUREA_CHECK(std::fabs(b[6] - 300.0f) < 0.01f && std::fabs(b[7] - 300.0f) < 0.01f && std::fabs(b[8]) < 0.01f);
    AUREA_CHECK(b[25] == 1.0f);
    // A escala entra nas dimensoes.
    r.comp()->layer(sq)->transform.scale = Vec3{2.0f, 0.5f, 1.0f};
    AUREA_CHECK(r.e.query_layer_bounds(sq.pack(), b, Engine::kLayerBoundsFloats));
    AUREA_CHECK(std::fabs(b[6] - 600.0f) < 0.05f && std::fabs(b[7] - 150.0f) < 0.05f);
    r.comp()->layer(sq)->transform.scale = Vec3{1.0f, 1.0f, 1.0f};
    AUREA_CHECK(r.e.query_layer_bounds(sq.pack(), b, Engine::kLayerBoundsFloats));
    // Forma 3D: volume nos tres eixos, em volta da posicao.
    f32 c[Engine::kLayerBoundsFloats]{};
    AUREA_CHECK(r.e.query_layer_bounds(*cube, c, Engine::kLayerBoundsFloats));
    AUREA_CHECK(c[6] > 10.0f && c[7] > 10.0f && c[8] > 10.0f);
    AUREA_CHECK(c[0] < 300.0f && c[3] > 300.0f && c[1] < 1400.0f && c[4] > 1400.0f && c[2] < -200.0f && c[5] > -200.0f);
    std::printf("    cubo: %.0f x %.0f x %.0f px\n", c[6], c[7], c[8]);
    // A cena e a uniao; pedir uma camada so devolve a dela.
    f32 all[6]{}, one[6]{};
    AUREA_CHECK(r.e.query_scene_bounds(nullptr, 0, all));
    AUREA_CHECK(all[0] <= c[0] + 0.01f && all[3] >= b[3] - 0.01f && all[2] <= c[2] + 0.01f && all[5] >= b[5] - 0.01f);
    const u64 only[] = {sq.pack()};
    AUREA_CHECK(r.e.query_scene_bounds(only, 1, one));
    for (int i = 0; i < 6; ++i) AUREA_CHECK(std::fabs(one[i] - b[i]) < 0.01f);
    // Enquadrar tudo: o alvo e o centro da caixa e os dois objetos cabem no quadro, em qualquer orbita.
    AUREA_CHECK(r.e.frame_scene_view(nullptr, 0, 1.15f));
    f32 v[8]{};
    r.e.query_scene_view(v);
    AUREA_CHECK(std::fabs(v[4] - ((all[0] + all[3]) * 0.5f - 540.0f)) < 0.01f);
    AUREA_CHECK(std::fabs(v[6] - (all[2] + all[5]) * 0.5f) < 0.01f);
    const f32 framed = v[2];
    for (const View& view : kViews) {
        r.e.set_scene_editor(true, view.yaw, view.pitch, framed);
        for (u64 id : {sq.pack(), *cube}) {
            f32 q[Engine::kLayerBoundsFloats]{};
            AUREA_CHECK(r.e.query_layer_bounds(id, q, Engine::kLayerBoundsFloats));
            AUREA_CHECK(q[25] == 1.0f);
            for (int i = 0; i < 8; ++i) {
                AUREA_CHECK(q[9 + i * 2] >= -1.0f && q[9 + i * 2] <= 1081.0f);
                AUREA_CHECK(q[10 + i * 2] >= -1.0f && q[10 + i * 2] <= 1921.0f);
            }
        }
    }
    // Enquadrar a selecao aproxima mais que enquadrar tudo.
    AUREA_CHECK(r.e.frame_scene_view(only, 1, 1.15f));
    r.e.query_scene_view(v);
    AUREA_CHECK(v[2] < framed);
    AUREA_CHECK(std::fabs(v[4] - (800.0f - 540.0f)) < 0.01f && std::fabs(v[6] - 600.0f) < 0.01f);
}

AUREA_TEST(SceneViewBlender, RotateAboutComposesRotationsAndOrbitsThePivot) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId id = r.square(Vec3{700.0f, 500.0f, 0.0f});
    seek(r.e, 0);
    r.e.set_scene_editor(true, 0, 0, 3);
    const f32 pos[3] = {700.0f, 500.0f, 0.0f};
    f32 rot[3] = {0.0f, 0.0f, 0.0f};
    f32 op[3]{}, orot[3]{};
    // Sem pivo: gira no lugar. Em volta do Z do mundo, de frente, e a Rotacao Z de sempre.
    AUREA_CHECK(r.e.scene_rotate_about(id.pack(), 2, 90.0f, false, nullptr, pos, rot, op, orot));
    AUREA_CHECK(std::fabs(op[0] - 700.0f) < 1e-3f && std::fabs(op[1] - 500.0f) < 1e-3f && std::fabs(op[2]) < 1e-3f);
    AUREA_CHECK(std::fabs(orot[0]) < 1e-3f && std::fabs(orot[1]) < 1e-3f && std::fabs(orot[2] - 90.0f) < 1e-3f);
    // Com pivo: a origem da a volta nele (X para Y, o mesmo sentido da Rotacao Z).
    const f32 pivot[3] = {540.0f, 500.0f, 0.0f};
    AUREA_CHECK(r.e.scene_rotate_about(id.pack(), 2, 90.0f, false, pivot, pos, rot, op, orot));
    AUREA_CHECK(std::fabs(op[0] - 540.0f) < 1e-2f && std::fabs(op[1] - 660.0f) < 1e-2f && std::fabs(op[2]) < 1e-2f);
    // O olhar da vista de frente e o Z do mundo; de tras, seguir o ponteiro inverte o sentido.
    AUREA_CHECK(r.e.scene_rotate_about(id.pack(), 6, 30.0f, false, nullptr, pos, rot, op, orot));
    AUREA_CHECK(std::fabs(orot[2] - 30.0f) < 1e-3f);
    r.e.set_scene_editor(true, 180, 0, 3);
    AUREA_CHECK(r.e.scene_rotate_about(id.pack(), 2, 30.0f, true, nullptr, pos, rot, op, orot));
    AUREA_CHECK(std::fabs(orot[2] + 30.0f) < 1e-3f);
    AUREA_CHECK(r.e.scene_rotate_about(id.pack(), 2, 30.0f, false, nullptr, pos, rot, op, orot));
    AUREA_CHECK(std::fabs(orot[2] - 30.0f) < 1e-3f);
    r.e.set_scene_editor(true, 0, 0, 3);
    // Objeto ja girado: 25 graus em volta do Y do MUNDO nao e somar 25 na Rotacao Y.
    rot[0] = 30.0f; rot[1] = 40.0f; rot[2] = 10.0f;
    r.comp()->layer(id)->transform.rotation = Vec3{rot[0], rot[1], rot[2]};
    f32 x0[3]{}, x1[3]{}, z[3]{};
    AUREA_CHECK(r.e.gizmo_move_local(id.pack(), 3, 1.0f, x1) && r.e.gizmo_move_local(id.pack(), 3, 0.0f, z));
    for (int i = 0; i < 3; ++i) x0[i] = x1[i] - z[i];      // eixo X local no mundo, antes
    AUREA_CHECK(r.e.scene_rotate_about(id.pack(), 1, 25.0f, false, nullptr, pos, rot, op, orot));
    AUREA_CHECK(std::fabs(orot[0] - 30.0f) > 0.5f || std::fabs(orot[2] - 10.0f) > 0.5f);
    r.comp()->layer(id)->transform.rotation = Vec3{orot[0], orot[1], orot[2]};
    AUREA_CHECK(r.e.gizmo_move_local(id.pack(), 3, 1.0f, x1) && r.e.gizmo_move_local(id.pack(), 3, 0.0f, z));
    const trackball::detail::D3 want = trackball::detail::rotate(trackball::detail::axis_angle(trackball::detail::D3{0, 1, 0}, 25.0 * trackball::detail::kRad),
                                                 trackball::detail::D3{x0[0], x0[1], x0[2]});
    AUREA_CHECK(std::fabs((x1[0] - z[0]) - want.x) < 2e-3 && std::fabs((x1[1] - z[1]) - want.y) < 2e-3 && std::fabs((x1[2] - z[2]) - want.z) < 2e-3);
    // Eixo LOCAL (3..5): o eixo X dela nao sai do lugar quando gira em volta dele.
    AUREA_CHECK(r.e.scene_rotate_about(id.pack(), 3, 70.0f, false, nullptr, pos, orot, op, rot));
    const Vec3 keep{x1[0] - z[0], x1[1] - z[1], x1[2] - z[2]};
    r.comp()->layer(id)->transform.rotation = Vec3{rot[0], rot[1], rot[2]};
    AUREA_CHECK(r.e.gizmo_move_local(id.pack(), 3, 1.0f, x1) && r.e.gizmo_move_local(id.pack(), 3, 0.0f, z));
    AUREA_CHECK(std::fabs((x1[0] - z[0]) - keep.x) < 2e-3f && std::fabs((x1[1] - z[1]) - keep.y) < 2e-3f && std::fabs((x1[2] - z[2]) - keep.z) < 2e-3f);
    // Filho de um pai girado: o passo do mundo volta para o espaco do pai e a origem da a volta no pivo.
    const LayerId parent = r.comp()->add_layer(LayerKind::Null, "pai");
    {
        Layer* p = r.comp()->layer(parent);
        p->threeD = true; p->end = FrameIndex{120};
        p->transform.position = Vec3{100.0f, 200.0f, 50.0f};
        p->transform.rotation = Vec3{0.0f, 0.0f, 90.0f};
    }
    const LayerId child = r.square(Vec3{300.0f, 0.0f, 0.0f});
    r.comp()->layer(child)->parent = parent;
    seek(r.e, 0);
    const f32 dx[3] = {10.0f, 0.0f, 0.0f};
    f32 dp[3]{};
    AUREA_CHECK(r.e.scene_world_delta_to_parent(child.pack(), dx, dp));
    AUREA_CHECK(std::fabs(dp[0]) < 1e-3f && std::fabs(dp[1] + 10.0f) < 1e-3f && std::fabs(dp[2]) < 1e-3f);
    f32 o0[3]{}, o1[3]{};
    AUREA_CHECK(r.e.query_layer_world_origin(child.pack(), o0));
    const f32 cpos[3] = {300.0f, 0.0f, 0.0f}, crot[3] = {0.0f, 0.0f, 0.0f}, cpivot[3] = {0.0f, 0.0f, 0.0f};
    AUREA_CHECK(r.e.scene_rotate_about(child.pack(), 0, 40.0f, false, cpivot, cpos, crot, op, orot));
    r.comp()->layer(child)->transform.position = Vec3{op[0], op[1], op[2]};
    r.comp()->layer(child)->transform.rotation = Vec3{orot[0], orot[1], orot[2]};
    AUREA_CHECK(r.e.query_layer_world_origin(child.pack(), o1));
    const trackball::detail::D3 arm = trackball::detail::rotate(trackball::detail::axis_angle(trackball::detail::D3{1, 0, 0}, 40.0 * trackball::detail::kRad),
                                                trackball::detail::D3{o0[0], o0[1], o0[2]});
    AUREA_CHECK(std::fabs(o1[0] - arm.x) < 0.05 && std::fabs(o1[1] - arm.y) < 0.05 && std::fabs(o1[2] - arm.z) < 0.05);
    // Entradas ruins nao passam.
    AUREA_CHECK(!r.e.scene_rotate_about(id.pack(), 7, 10.0f, false, nullptr, pos, rot, op, orot));
    AUREA_CHECK(!r.e.scene_rotate_about(0, 1, 10.0f, false, nullptr, pos, rot, op, orot));
}

AUREA_TEST(SceneViewBlender, CursorPointProjectionAndAlignCamera) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId sq = r.square(Vec3{300.0f, 400.0f, 200.0f});
    seek(r.e, 0);
    r.e.set_scene_editor(true, 0, 0, 3);
    f32 p[3]{};
    // Sem plano: o plano de frente para a vista que passa pelo alvo da orbita (o centro, z = 0).
    AUREA_CHECK(r.e.scene_point_at(540.0f, 960.0f, nullptr, false, p));
    AUREA_CHECK(std::fabs(p[0] - 540.0f) < 0.05f && std::fabs(p[1] - 960.0f) < 0.05f && std::fabs(p[2]) < 0.05f);
    // Com plano: o ponto dado define a profundidade; e o ponto volta ao mesmo pixel.
    const f32 through[3] = {0.0f, 0.0f, 500.0f};
    r.e.set_scene_editor(true, 40, 25, 3);
    AUREA_CHECK(r.e.scene_point_at(700.0f, 300.0f, through, false, p));
    f32 xy[2]{};
    AUREA_CHECK_EQ(r.e.project_scene_points(p, 1, xy), 1u);
    AUREA_CHECK(std::fabs(xy[0] - 700.0f) < 0.5f && std::fabs(xy[1] - 300.0f) < 0.5f);
    // Superficie: com um cubo no meio, o ponto cai na face da frente (mais perto que o plano do alvo).
    r.e.set_scene_editor(true, 0, 0, 3);
    const auto cube = r.e.add_shape3d(0);
    AUREA_CHECK(cube.ok()); if (!cube.ok()) return;
    r.comp()->layer(LayerId::unpack(*cube))->transform.position = Vec3{540.0f, 960.0f, 0.0f};
    AUREA_CHECK(r.e.scene_point_at(540.0f, 960.0f, nullptr, true, p));
    AUREA_CHECK(p[2] < -1.0f);
    AUREA_CHECK(r.e.scene_point_at(540.0f, 960.0f, nullptr, false, p));
    AUREA_CHECK(std::fabs(p[2]) < 0.05f);
    r.comp()->layer(LayerId::unpack(*cube))->visible = false;

    // Alinhar a camera a vista: ela passa a olhar o alvo da orbita de onde a vista esta.
    const auto cam = r.e.add_camera();
    AUREA_CHECK(cam.ok()); if (!cam.ok()) return;
    AUREA_CHECK(!r.e.align_camera_to_scene_view(sq.pack()));     // nao e camera
    r.e.set_scene_editor(true, 35, 20, 2.5f);
    const u64 one[] = {sq.pack()};
    AUREA_CHECK(r.e.focus_scene_view(one, 1));
    f32 nav[8]{}, cameraView[8]{};
    AUREA_CHECK(r.e.query_gizmo(sq.pack(), 100.0f, nav));
    const Vec3 before = r.comp()->layer(LayerId::unpack(*cam))->transform.position;
    AUREA_CHECK(r.e.align_camera_to_scene_view(*cam));
    const Layer* c = r.comp()->layer(LayerId::unpack(*cam));
    AUREA_CHECK(c->camera.twoNode);
    AUREA_CHECK(std::fabs(c->camera.poi.x - 300.0f) < 0.01f && std::fabs(c->camera.poi.y - 400.0f) < 0.01f && std::fabs(c->camera.poi.z - 200.0f) < 0.01f);
    // Pela camera (cena desligada) o alvo fica no meio do quadro, como na vista de navegacao.
    r.e.set_scene_editor(false, 35, 20, 2.5f);
    AUREA_CHECK(r.e.query_gizmo(sq.pack(), 100.0f, cameraView));
    AUREA_CHECK(std::fabs(cameraView[0] - 540.0f) < 1.5f && std::fabs(cameraView[1] - 960.0f) < 1.5f);
    AUREA_CHECK(std::fabs(nav[0] - 540.0f) < 1.5f && std::fabs(nav[1] - 960.0f) < 1.5f);
    // UM desfazer devolve a camera inteira.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    const Layer* back = r.comp()->layer(LayerId::unpack(*cam));
    AUREA_CHECK(back != nullptr);
    if (back) {
        AUREA_CHECK(std::fabs(back->transform.position.x - before.x) < 1e-3f && std::fabs(back->transform.position.z - before.z) < 1e-3f);
        AUREA_CHECK(!back->camera.twoNode);
    }
}

// --- Aurea PC, Cena 3D fase 2: as restricoes do editor de propriedades sao expressoes do motor ----
// (PcScene3DOps.kt, `constraintParts`): os mesmos textos, avaliados aqui.

AUREA_TEST(SceneViewBlender, ConstraintExpressionsCopyAndTrackTheTarget) {
    PickRig r; AUREA_CHECK(r.ok); if (!r.ok) return;
    const LayerId target = r.square(Vec3{800.0f, 300.0f, 400.0f}, Vec3{10.0f, 20.0f, 30.0f});
    r.comp()->layer(target)->name = "Alvo";
    r.comp()->layer(target)->transform.scale = Vec3{2.0f, 1.5f, 1.0f};
    const LayerId follower = r.square(Vec3{100.0f, 100.0f, 0.0f});
    seek(r.e, 0);
    const u32 none = kInvalidIndex;
    auto keys = [&](TrackProperty base, u32 count, u32* out) {
        for (u32 i = 0; i < count; ++i) { out[i * 3] = static_cast<u32>(base) + i; out[i * 3 + 1] = none; out[i * 3 + 2] = 0; }
    };
    auto value = [&](TrackProperty p) {
        Engine::ExpressionInfo info;
        AUREA_CHECK(r.e.query_expression(follower.pack(), static_cast<u32>(p), none, 0, info));
        AUREA_CHECK(info.exists);
        if (!info.error.ok) std::printf("    erro: %s\n", info.error.message.c_str());
        AUREA_CHECK(info.error.ok);
        return info.value;
    };
    u32 k[9]{};
    expr::Diagnostic diag;
    // Copiar posicao: a origem do alvo no mundo.
    keys(TrackProperty::PositionX, 3, k);
    AUREA_CHECK(r.e.set_expressions(follower.pack(), k, 3,
        "thisComp.layer(\"Alvo\").toWorld(thisComp.layer(\"Alvo\").transform.anchorPoint)", &diag).ok());
    AUREA_CHECK(diag.ok);
    AUREA_CHECK(std::fabs(value(TrackProperty::PositionX) - 800.0f) < 0.05f);
    AUREA_CHECK(std::fabs(value(TrackProperty::PositionY) - 300.0f) < 0.05f);
    AUREA_CHECK(std::fabs(value(TrackProperty::PositionZ) - 400.0f) < 0.05f);
    // Copiar escala e rotacao.
    keys(TrackProperty::ScaleX, 3, k);
    AUREA_CHECK(r.e.set_expressions(follower.pack(), k, 3, "thisComp.layer(\"Alvo\").transform.scale", &diag).ok());
    AUREA_CHECK(diag.ok);
    AUREA_CHECK(std::fabs(value(TrackProperty::ScaleX) - 2.0f) < 1e-3f && std::fabs(value(TrackProperty::ScaleY) - 1.5f) < 1e-3f);
    const char* turns[3] = {"thisComp.layer(\"Alvo\").transform.xRotation", "thisComp.layer(\"Alvo\").transform.yRotation",
                            "thisComp.layer(\"Alvo\").transform.zRotation"};
    const f32 wantTurn[3] = {10.0f, 20.0f, 30.0f};
    for (u32 i = 0; i < 3; ++i) {
        const TrackProperty p = static_cast<TrackProperty>(static_cast<u32>(TrackProperty::RotationX) + i);
        AUREA_CHECK(r.e.set_expression(follower.pack(), static_cast<u32>(p), none, 0, turns[i], &diag).ok());
        AUREA_CHECK(diag.ok);
        AUREA_CHECK(std::fabs(value(p) - wantTurn[i]) < 1e-3f);
    }
    // Fonte vazia remove (o "tirar restricao").
    keys(TrackProperty::PositionX, 3, k);
    AUREA_CHECK(r.e.set_expressions(follower.pack(), k, 3, "", &diag).ok());
    Engine::ExpressionInfo gone;
    (void)r.e.query_expression(follower.pack(), static_cast<u32>(TrackProperty::PositionX), none, 0, gone);
    AUREA_CHECK(!gone.exists);
    for (u32 i = 0; i < 3; ++i)
        AUREA_CHECK(r.e.set_expression(follower.pack(), static_cast<u32>(TrackProperty::RotationX) + i, none, 0, "", &diag).ok());
    // Mirar (Track To): o Z do seguidor aponta para o alvo.
    // Rotacao X e Y sao trilhas escalares: uma expressao para cada (a Z fica livre para o rolamento).
    const char* aimX = "var d = sub(thisComp.layer(\"Alvo\").toWorld(thisComp.layer(\"Alvo\").transform.anchorPoint), thisLayer.transform.position); "
                       "radiansToDegrees(Math.asin(-d[1] / length(d)))";
    const char* aimY = "var d = sub(thisComp.layer(\"Alvo\").toWorld(thisComp.layer(\"Alvo\").transform.anchorPoint), thisLayer.transform.position); "
                       "radiansToDegrees(Math.atan2(d[0], d[2]))";
    AUREA_CHECK(r.e.set_expression(follower.pack(), static_cast<u32>(TrackProperty::RotationX), none, 0, aimX, &diag).ok());
    AUREA_CHECK(diag.ok);
    AUREA_CHECK(r.e.set_expression(follower.pack(), static_cast<u32>(TrackProperty::RotationY), none, 0, aimY, &diag).ok());
    AUREA_CHECK(diag.ok);
    std::printf("    mirar: rot %.2f %.2f\n", value(TrackProperty::RotationX), value(TrackProperty::RotationY));
    f32 z1[3]{}, z0[3]{};
    AUREA_CHECK(r.e.gizmo_move_local(follower.pack(), 5, 1.0f, z1) && r.e.gizmo_move_local(follower.pack(), 5, 0.0f, z0));
    const Vec3 forward = Vec3{z1[0] - z0[0], z1[1] - z0[1], z1[2] - z0[2]}.normalized();
    const Vec3 want = Vec3{700.0f, 200.0f, 400.0f}.normalized();
    std::printf("    mirar: frente %.3f %.3f %.3f, alvo %.3f %.3f %.3f\n", forward.x, forward.y, forward.z, want.x, want.y, want.z);
    AUREA_CHECK(forward.dot(want) > 0.995f);
}
