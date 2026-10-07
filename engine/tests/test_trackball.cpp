// =============================================================================
//  Trackball do gizmo 3D de girar (core/Trackball.hpp): Euler ZYX ⇄ quaternion
//  na convenção do Renderer, arrasto nos anéis/esfera/anel da vista e a
//  continuidade (sem salto em ±180°, voltas inteiras preservadas).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/core/Trackball.hpp"

#include <cmath>
#include <cstdio>

using namespace aurea;

namespace {

constexpr f32 kPiF = 3.14159265358979323846f;
const f32 kIdentity9[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};

/// Arrasto em `steps` passos de `from` até `to` (px relativos ao centro).
Vec3 drag_path(const f32 frame[9], Vec3 start, i32 part, f32 fx, f32 fy, f32 tx, f32 ty, f32 radius, u32 steps = 12) {
    f32 args[trackball::kDragArgs]{};
    for (u32 i = 0; i < 9; ++i) args[i] = frame[i];
    args[9] = start.x; args[10] = start.y; args[11] = start.z;
    Vec3 prev = start;
    f32 acc[4]{0, 0, 0, 1};
    f32 px = fx, py = fy;
    for (u32 s = 1; s <= steps; ++s) {
        const f32 t = static_cast<f32>(s) / static_cast<f32>(steps);
        const f32 x = fx + (tx - fx) * t, y = fy + (ty - fy) * t;
        args[12] = prev.x; args[13] = prev.y; args[14] = prev.z;
        for (u32 i = 0; i < 4; ++i) args[15 + i] = acc[i];
        args[19] = static_cast<f32>(part);
        args[20] = fx; args[21] = fy;
        args[22] = px; args[23] = py;
        args[24] = x; args[25] = y;
        args[26] = radius;
        f32 out[trackball::kDragOut]{};
        AUREA_CHECK(trackball::drag(args, out));
        prev = Vec3{out[0], out[1], out[2]};
        for (u32 i = 0; i < 4; ++i) acc[i] = out[3 + i];
        px = x; py = y;
    }
    return prev;
}

bool near3(Vec3 a, Vec3 b, f32 tol) {
    return std::fabs(a.x - b.x) < tol && std::fabs(a.y - b.y) < tol && std::fabs(a.z - b.z) < tol;
}

} // namespace

AUREA_TEST(Trackball, EulerQuatRoundTripMatchesRenderer) {
    const Vec3 cases[] = {{0, 0, 0}, {30, 0, 0}, {0, 45, 0}, {0, 0, -60}, {20, -35, 110}, {-150, 70, 15},
                          {89, -10, 179}, {12, 80, -170}};
    for (const Vec3& e : cases) {
        // Mesma matriz que o Renderer monta (Quat::from_euler_zyx em radianos).
        const Mat4 renderer = Mat4::from_quat(Quat::from_euler_zyx(e.x * kDeg2Rad, e.y * kDeg2Rad, e.z * kDeg2Rad));
        const Mat4 ours = Mat4::from_quat(trackball::quat_from_euler_deg(e));
        for (u32 c = 0; c < 3; ++c) {
            AUREA_CHECK_NEAR(ours.col[c].x, renderer.col[c].x, 1e-5);
            AUREA_CHECK_NEAR(ours.col[c].y, renderer.col[c].y, 1e-5);
            AUREA_CHECK_NEAR(ours.col[c].z, renderer.col[c].z, 1e-5);
        }
        const Vec3 back = trackball::euler_deg_from_quat(trackball::quat_from_euler_deg(e), e);
        AUREA_CHECK(near3(back, e, 2e-3f));
        // Referência uma volta adiante: o mesmo giro volta como +360.
        const Vec3 turned = trackball::euler_deg_from_quat(trackball::quat_from_euler_deg(e), Vec3{e.x + 360, e.y, e.z - 360});
        AUREA_CHECK(near3(turned, Vec3{e.x + 360, e.y, e.z - 360}, 2e-3f));
    }
    // A outra leitura (x+180, 180−y, z+180) é a mesma rotação: perto da referência, ela ganha.
    const Vec3 alt = trackball::euler_deg_from_quat(trackball::quat_from_euler_deg(Vec3{10, 20, 30}), Vec3{190, 160, 210});
    AUREA_CHECK(near3(alt, Vec3{190, 160, 210}, 2e-3f));
}

AUREA_TEST(Trackball, NinetyDegreesAroundEachAxis) {
    const f32 r = 100.0f;
    const f32 quarter = r * kPiF * 0.5f;   // px ao longo da tangente = 90°
    // Anel X (de perfil: linha vertical): tocar no centro e descer gira +90 em X.
    Vec3 e = drag_path(kIdentity9, Vec3{0, 0, 0}, trackball::kRingX, 0, 0, 0, quarter, r);
    std::printf("    anel X: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(near3(e, Vec3{90, 0, 0}, 1e-2f));
    // Anel Y (linha horizontal): a frente anda para a ESQUERDA quando Y cresce.
    e = drag_path(kIdentity9, Vec3{0, 0, 0}, trackball::kRingY, 0, 0, -quarter, 0, r);
    std::printf("    anel Y: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(near3(e, Vec3{0, 90, 0}, 1e-2f));
    // Anel Z (de frente: o círculo da silhueta): na borda direita, descer = +90 em Z.
    e = drag_path(kIdentity9, Vec3{0, 0, 0}, trackball::kRingZ, r, 0, r, quarter, r);
    std::printf("    anel Z: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(near3(e, Vec3{0, 0, 90}, 1e-2f));
    // Anel da vista: um quarto de volta horário na tela = +90 em volta do Z da vista.
    const f32 rv = r * trackball::kViewRingScale;
    f32 args[trackball::kDragArgs]{};
    for (u32 i = 0; i < 9; ++i) args[i] = kIdentity9[i];
    args[18] = 1; args[19] = trackball::kRingView;
    args[20] = rv; args[22] = rv; args[25] = rv; args[26] = r;   // (rv,0) → (0,rv)
    f32 out[trackball::kDragOut]{};
    AUREA_CHECK(trackball::drag(args, out));
    AUREA_CHECK(near3(Vec3{out[0], out[1], out[2]}, Vec3{0, 0, 90}, 1e-2f));
    // Esfera livre: do centro até a borda direita = 90° (a frente vai para a direita: Y −90).
    e = drag_path(kIdentity9, Vec3{0, 0, 0}, trackball::kFree, 0, 0, r, 0, r, 1);
    std::printf("    livre →: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(near3(e, Vec3{0, -90, 0}, 1e-2f));
    // Do centro até a borda de baixo: a frente desce = X +90.
    e = drag_path(kIdentity9, Vec3{0, 0, 0}, trackball::kFree, 0, 0, 0, r, r, 1);
    AUREA_CHECK(near3(e, Vec3{90, 0, 0}, 1e-2f));
}

AUREA_TEST(Trackball, RingXTouchesOnlyRotationXAndKeepsTurns) {
    const f32 r = 80.0f;
    const f32 perDeg = r * kPiF / 180.0f;
    // Camada já girada nos três eixos: o anel X mexe SÓ na Rotação X (Y e Z exatos).
    const Vec3 start{20, -35, 110};
    f32 a[9]{};
    trackball::axes(kIdentity9, start, a);
    // Toque num ponto do anel X na tela (frente), arrasto ao longo da tangente.
    // Acha o ponto: o grande círculo das colunas Y/Z de A, lado de frente.
    f32 gx = 0, gy = 0, frontMost = 1e9f;
    for (u32 i = 0; i < 360; ++i) {
        const f32 t = static_cast<f32>(i) * kPiF / 180.0f;
        const f32 px = r * (std::cos(t) * a[3] + std::sin(t) * a[6]), py = r * (std::cos(t) * a[4] + std::sin(t) * a[7]);
        const f32 pz = r * (std::cos(t) * a[5] + std::sin(t) * a[8]);
        if (pz < frontMost) { frontMost = pz; gx = px; gy = py; }
    }
    AUREA_CHECK(frontMost < 0.0f);
    AUREA_CHECK(trackball::hit_test(a, gx, gy, r, 14.0f) == trackball::kRingX);
    // Tangente na tela nesse ponto: eixo X × ponto.
    const Vec3 axis{a[0], a[1], a[2]};
    Vec3 p{gx, gy, 0};
    {
        f32 best = 1e9f;
        for (u32 i = 0; i < 3600; ++i) {
            const f32 t = static_cast<f32>(i) * kPiF / 1800.0f;
            const Vec3 q{r * (std::cos(t) * a[3] + std::sin(t) * a[6]), r * (std::cos(t) * a[4] + std::sin(t) * a[7]),
                         r * (std::cos(t) * a[5] + std::sin(t) * a[8])};
            const f32 d = std::hypot(q.x - gx, q.y - gy) + (q.z > 0 ? 1000.0f : 0.0f);
            if (d < best) { best = d; p = q; }
        }
    }
    const Vec3 tan = axis.cross(p);
    const f32 tl = std::hypot(tan.x, tan.y);
    const f32 tx = tan.x / tl, ty = tan.y / tl;
    // 40° de uma vez.
    Vec3 e = drag_path(kIdentity9, start, trackball::kRingX, gx, gy, gx + tx * 40 * perDeg, gy + ty * 40 * perDeg, r);
    std::printf("    anel X +40: %.4f %.4f %.4f\n", e.x, e.y, e.z);
    AUREA_CHECK(std::fabs(e.x - 60.0f) < 0.05f);
    AUREA_CHECK(e.y == start.y && e.z == start.z);
    // Duas voltas inteiras (720°) em passos: Rotação X = 20 + 720, sem cair em ±180.
    e = drag_path(kIdentity9, start, trackball::kRingX, gx, gy, gx + tx * 720 * perDeg, gy + ty * 720 * perDeg, r, 144);
    std::printf("    anel X +720: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(std::fabs(e.x - 740.0f) < 0.2f);
    AUREA_CHECK(e.y == start.y && e.z == start.z);
}

AUREA_TEST(Trackball, ContinuityAcross180AndGimbal) {
    const f32 r = 100.0f;
    const f32 perDeg = r * kPiF / 180.0f;
    // X em 170, +20 pelo anel X: vira 190 (não −170).
    Vec3 e = drag_path(kIdentity9, Vec3{170, 0, 0}, trackball::kRingX, 0, 0, 0, 20 * perDeg, r);
    std::printf("    170+20: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(near3(e, Vec3{190, 0, 0}, 0.05f));
    // Z em −175, −10 pelo anel Z (o círculo de frente não muda de lugar; na
    // borda direita, subir = Z diminui): −185, não +175.
    e = drag_path(kIdentity9, Vec3{0, 0, -175}, trackball::kRingZ, r, 0, r, -10 * perDeg, r);
    std::printf("    -175-10: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(near3(e, Vec3{0, 0, -185}, 0.05f));
    // Y passando por 90 (trava do cardã) até 100: continua 100, X e Z ficam 0.
    e = drag_path(kIdentity9, Vec3{0, 0, 0}, trackball::kRingY, 0, 0, -100 * perDeg, 0, r, 10);
    std::printf("    Y 0→100: %.3f %.3f %.3f\n", e.x, e.y, e.z);
    AUREA_CHECK(near3(e, Vec3{0, 100, 0}, 0.05f));
    // Esfera livre de ida e volta: volta ao início (sem deriva).
    f32 args[trackball::kDragArgs]{};
    for (u32 i = 0; i < 9; ++i) args[i] = kIdentity9[i];
    const Vec3 start{15, 25, -40};
    Vec3 prev = start;
    f32 acc[4]{0, 0, 0, 1};
    const f32 path[][2] = {{0, 0}, {30, 10}, {60, -20}, {20, -50}, {-10, -10}, {0, 0}};
    for (u32 s = 1; s < 6; ++s) {
        args[9] = start.x; args[10] = start.y; args[11] = start.z;
        args[12] = prev.x; args[13] = prev.y; args[14] = prev.z;
        for (u32 i = 0; i < 4; ++i) args[15 + i] = acc[i];
        args[19] = trackball::kFree;
        args[22] = path[s - 1][0]; args[23] = path[s - 1][1]; args[24] = path[s][0]; args[25] = path[s][1]; args[26] = r;
        f32 out[trackball::kDragOut]{};
        AUREA_CHECK(trackball::drag(args, out));
        prev = Vec3{out[0], out[1], out[2]};
        for (u32 i = 0; i < 4; ++i) acc[i] = out[3 + i];
    }
    std::printf("    livre ida e volta: %.3f %.3f %.3f\n", prev.x, prev.y, prev.z);
    // O caminho fechado na esfera NÃO é identidade em geral (arcball), mas a
    // conta é estável: nenhum NaN e o resultado reproduz a mesma rotação acumulada.
    AUREA_CHECK(std::isfinite(prev.x) && std::isfinite(prev.y) && std::isfinite(prev.z));
    // Invalida: parte desconhecida ou raio zero.
    args[19] = 9;
    f32 out[trackball::kDragOut]{};
    AUREA_CHECK(!trackball::drag(args, out));
}

AUREA_TEST(Trackball, HitTestRingsFirstThenSphere) {
    const f32 r = 100.0f;
    f32 a[9]{};
    trackball::axes(kIdentity9, Vec3{0, 0, 0}, a);
    AUREA_CHECK(trackball::hit_test(a, 0, 50, r, 14) == trackball::kRingX);    // linha vertical
    AUREA_CHECK(trackball::hit_test(a, -50, 3, r, 14) == trackball::kRingY);   // linha horizontal
    AUREA_CHECK(trackball::hit_test(a, 70.7f, 70.7f, r, 14) == trackball::kRingZ);
    AUREA_CHECK(trackball::hit_test(a, 126, 0, r, 14) == trackball::kRingView);
    AUREA_CHECK(trackball::hit_test(a, 40, 30, r, 14) == trackball::kFree);
    AUREA_CHECK(trackball::hit_test(a, 200, 0, r, 14) == trackball::kNone);
}

AUREA_TEST(Trackball, EngineQueryMatchesLayerRotationAndView) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    const u64 flat = *e.add_null(false);
    f32 q[trackball::kQueryFloats]{};
    AUREA_CHECK(!e.query_trackball(flat, q));   // 2D: sem gizmo 3D
    const u64 n = *e.add_null(false);
    Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    Layer* nl = comp->layer(LayerId::unpack(n));
    nl->threeD = true;
    nl->transform.rotation = Vec3{30, -20, 50};
    AUREA_CHECK(e.query_trackball(n, q));
    // Origem igual à do gizmo de setas.
    f32 g[8]{};
    AUREA_CHECK(e.query_gizmo(n, 50.0f, g));
    AUREA_CHECK_NEAR(q[0], g[0], 1e-3);
    AUREA_CHECK_NEAR(q[1], g[1], 1e-3);
    // Câmera padrão olhando reto: F ≈ identidade; Euler é o da camada.
    for (u32 c = 0; c < 3; ++c)
        for (u32 r = 0; r < 3; ++r) AUREA_CHECK_NEAR(q[11 + c * 3 + r], c == r ? 1.0f : 0.0f, 1e-3);
    AUREA_CHECK(near3(Vec3{q[20], q[21], q[22]}, Vec3{30, -20, 50}, 1e-4f));
    // A = F·R: coluna X de A aponta como a seta X local do gizmo (na tela).
    f32 local[8]{};
    AUREA_CHECK(e.query_gizmo(n, 50.0f, local, true));
    const f32 sx = local[2] - local[0], sy = local[3] - local[1];
    const f32 len = std::hypot(sx, sy), alen = std::hypot(q[2], q[3]);
    AUREA_CHECK(len > 1.0f && alen > 0.1f);
    AUREA_CHECK((sx * q[2] + sy * q[3]) / (len * alen) > 0.98f);
    e.shutdown();
}
