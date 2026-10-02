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
#include "aurea/scene3d/Text3D.hpp"

#include <cmath>
#include <cstdio>

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
