// =============================================================================
//  Esqueleto 3D e Fantoche mais fáceis (0.4.5): nome do osso do outro lado,
//  conta do anel na tela, anéis do osso (eixos locais na articulação), giro
//  pelo anel com um desfazer por gesto, rotações em lote, espelhar osso e
//  pose inteira, IK simples arrastando a ponta e giro de pinos em volta de
//  um pino-pivô. Tudo nas trilhas de sempre (sem GPU).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/scene3d/RigSkeleton.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace aurea;

namespace {

std::string b64(const std::vector<u8>& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (usize i = 0; i < in.size(); i += 3) {
        const u32 a = in[i], b = i + 1 < in.size() ? in[i + 1] : 0u, c = i + 2 < in.size() ? in[i + 2] : 0u;
        const u32 v = (a << 16) | (b << 8) | c;
        out += k[(v >> 18) & 63];
        out += k[(v >> 12) & 63];
        out += i + 1 < in.size() ? k[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? k[v & 63] : '=';
    }
    return out;
}

/// Boneco simétrico em T: quadril, coluna e dois braços de 3 ossos ao longo
/// de ±X (esquerda = +X). Os nós dos ossos: 1 quadril, 2 coluna, 3..5
/// braço/antebraço/mão esquerdos, 6..8 os direitos.
std::string write_symmetric_rig() {
    const f32 pos[9] = {-1.5f, 0, 0, 1.5f, 0, 0, 0, 2.0f, 0};   // cobre o boneco: o modelo cabe na tela
    const u16 idx[4] = {0, 1, 2, 0};
    const u8 joints[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const f32 weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    const f32 world[8][3] = {{0, 0, 0}, {0, 1, 0}, {0.3f, 1, 0}, {0.8f, 1, 0}, {1.3f, 1, 0},
                             {-0.3f, 1, 0}, {-0.8f, 1, 0}, {-1.3f, 1, 0}};
    f32 ibm[8 * 16];
    for (int j = 0; j < 8; ++j) {
        f32* m = ibm + j * 16;
        for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        m[12] = -world[j][0]; m[13] = -world[j][1]; m[14] = -world[j][2];
    }
    std::vector<u8> bin(616, 0);
    std::memcpy(bin.data(), pos, 36);
    std::memcpy(bin.data() + 36, idx, 6);
    std::memcpy(bin.data() + 44, joints, 12);
    std::memcpy(bin.data() + 56, weights, 48);
    std::memcpy(bin.data() + 104, ibm, 512);
    std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],"nodes":[)";
    json += R"({"mesh":0,"skin":0},{"name":"Hips","children":[2,3,6]},{"name":"Spine","translation":[0,1,0]},)";
    json += R"({"name":"LeftArm","translation":[0.3,1,0],"children":[4]},{"name":"LeftForeArm","translation":[0.5,0,0],"children":[5]},)";
    json += R"({"name":"LeftHand","translation":[0.5,0,0]},)";
    json += R"({"name":"RightArm","translation":[-0.3,1,0],"children":[7]},{"name":"RightForeArm","translation":[-0.5,0,0],"children":[8]},)";
    json += R"({"name":"RightHand","translation":[-0.5,0,0]}],)";
    json += R"("skins":[{"joints":[1,2,3,4,5,6,7,8],"inverseBindMatrices":4}],)";
    json += R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],)";
    json += R"("buffers":[{"byteLength":616,"uri":"data:application/octet-stream;base64,)" + b64(bin) + R"("}],)";
    json += R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6},)";
    json += R"({"buffer":0,"byteOffset":44,"byteLength":12},{"buffer":0,"byteOffset":56,"byteLength":48},{"buffer":0,"byteOffset":104,"byteLength":512}],)";
    json += R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1.5,0,0],"max":[1.5,2,0]},)";
    json += R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},)";
    json += R"({"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},)";
    json += R"({"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},)";
    json += R"({"bufferView":4,"componentType":5126,"count":8,"type":"MAT4"}]})";
    const std::string path = "aurea_teste_esqueleto.gltf";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
    }
    return path;
}

EngineConfig skeleton_config() {
    EngineConfig cfg;
    cfg.workerCount = 1;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

void undo(Engine& e) {
    Command c;
    c.type = CommandType::Undo;
    (void)e.apply_command(c);
}

/// Linha do osso `node` na lista de query_model_bones (−1 = não achou).
i32 slot_of(const std::vector<f32>& b, i32 node) {
    for (usize i = 0; i + Engine::kBoneFloats <= b.size(); i += Engine::kBoneFloats)
        if (static_cast<i32>(b[i]) == node) return static_cast<i32>(i);
    return -1;
}

std::vector<f32> bones_of(Engine& e, u64 id) {
    std::vector<f32> b(e.query_model_bones(id, nullptr, 0));
    if (!b.empty()) (void)e.query_model_bones(id, b.data(), static_cast<u32>(b.size()));
    return b;
}

Vec3 euler_of(const std::vector<f32>& b, i32 node) {
    const i32 s = slot_of(b, node);
    return s < 0 ? Vec3{1e9f, 1e9f, 1e9f} : Vec3{b[static_cast<usize>(s) + 2], b[static_cast<usize>(s) + 3], b[static_cast<usize>(s) + 4]};
}

Vec2 screen_of(const std::vector<f32>& b, i32 node) {
    const i32 s = slot_of(b, node);
    return s < 0 ? Vec2{0, 0} : Vec2{b[static_cast<usize>(s) + 7], b[static_cast<usize>(s) + 8]};
}

bool near3(Vec3 a, Vec3 b, f32 tol) {
    return std::fabs(a.x - b.x) < tol && std::fabs(a.y - b.y) < tol && std::fabs(a.z - b.z) < tol;
}

constexpr i32 kHips = 1, kSpine = 2, kLArm = 3, kLFore = 4, kLHand = 5, kRArm = 6;

} // namespace

AUREA_TEST(RigSkeleton, MirrorNamesSwapSidesAndLeaveTheMiddleAlone) {
    using scene3d::mirror_bone_name;
    AUREA_CHECK_EQ(mirror_bone_name("LeftArm"), std::string("RightArm"));
    AUREA_CHECK_EQ(mirror_bone_name("mixamorig:RightHandIndex1"), std::string("mixamorig:LeftHandIndex1"));
    AUREA_CHECK_EQ(mirror_bone_name("LEFT_FOOT"), std::string("RIGHT_FOOT"));
    AUREA_CHECK_EQ(mirror_bone_name("Bip01 L Thigh"), std::string("Bip01 R Thigh"));
    AUREA_CHECK_EQ(mirror_bone_name("upperarm_l"), std::string("upperarm_r"));
    AUREA_CHECK_EQ(mirror_bone_name("Hand.R"), std::string("Hand.L"));
    AUREA_CHECK_EQ(mirror_bone_name("L_Clavicle"), std::string("R_Clavicle"));
    AUREA_CHECK_EQ(mirror_bone_name("braco_esquerdo"), std::string("braco_direito"));
    AUREA_CHECK(mirror_bone_name("Spine").empty());
    AUREA_CHECK(mirror_bone_name("Root").empty());
    AUREA_CHECK(mirror_bone_name("hair_long").empty());
}

AUREA_TEST(RigSkeleton, RingDragFollowsTheFingerAndMirrorReflectsRotations) {
    // Anel de frente (raio 50): 5 px ao longo da tangente em θ = 0 ≈ 5/50 rad.
    const f32 deg = scene3d::ring_drag_degrees(Vec2{50, 0}, Vec2{0, 50}, Vec2{50, 0}, Vec2{50, 5}, 50.0f);
    AUREA_CHECK_NEAR(deg, 5.0f / 50.0f * 180.0f / 3.14159265f, 0.05f);
    // Sentido contrário, do outro lado do anel.
    AUREA_CHECK(scene3d::ring_drag_degrees(Vec2{50, 0}, Vec2{0, 50}, Vec2{-50, 0}, Vec2{-50, 5}, 50.0f) < 0.0f);
    // Anel de lado (V quase nulo): a velocidade fica limitada, sem explodir.
    const f32 edge = scene3d::ring_drag_degrees(Vec2{50, 0}, Vec2{0, 0.01f}, Vec2{50, 0}, Vec2{50, 5}, 50.0f);
    AUREA_CHECK(std::isfinite(edge) && std::fabs(edge) <= 90.0f);
    AUREA_CHECK_NEAR(scene3d::swept_degrees(Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}), 90.0f, 1e-3f);
    // Reflexo no plano YZ: giro em Y vira o oposto; em X fica.
    const Quat y30 = Quat::from_axis_angle(Vec3{0, 1, 0}, 0.5235988f);
    const Quat m = scene3d::mirror_rotation(y30, 0);
    AUREA_CHECK_NEAR(m.y, -y30.y, 1e-6f);
    AUREA_CHECK_NEAR(m.w, y30.w, 1e-6f);
    const Quat x30 = Quat::from_axis_angle(Vec3{1, 0, 0}, 0.5235988f);
    AUREA_CHECK_NEAR(scene3d::mirror_rotation(x30, 0).x, x30.x, 1e-6f);
}

AUREA_TEST(RigSkeleton, JointRingsRotateTheBoneInItsLocalSpaceWithOneUndoPerGesture) {
    Engine e;
    AUREA_CHECK(e.initialize(skeleton_config()).ok());
    AUREA_CHECK(e.new_project(640, 640, 30.0, "esqueleto").ok());
    ModelImport req;
    req.path = write_symmetric_rig();
    auto imported = e.import_model(req);
    AUREA_CHECK(imported.ok());
    if (!imported.ok()) { e.shutdown(); std::remove(req.path.c_str()); return; }
    const u64 id = *imported;
    std::vector<f32> b = bones_of(e, id);
    AUREA_CHECK_EQ(b.size(), usize{8} * Engine::kBoneFloats);
    if (b.size() != usize{8} * Engine::kBoneFloats) { e.shutdown(); std::remove(req.path.c_str()); return; }
    // Anéis: base ortonormal projetada (|X|² + |Y|² + |Z|² = 2·r²).
    constexpr f32 r = 60.0f;
    f32 g[Engine::kBoneGizmoFloats];
    AUREA_CHECK_EQ(e.query_bone_gizmo(id, kLArm, r, g, Engine::kBoneGizmoFloats), Engine::kBoneGizmoFloats);
    AUREA_CHECK_EQ(g[2], 1.0f);
    const Vec2 shoulder = screen_of(b, kLArm);
    AUREA_CHECK(shoulder.x > 0.0f && shoulder.x < 640.0f && shoulder.y > 0.0f && shoulder.y < 640.0f);   // na tela
    AUREA_CHECK_NEAR(g[0], shoulder.x, 0.5f);
    AUREA_CHECK_NEAR(g[1], shoulder.y, 0.5f);
    const Vec2 ax{g[3 + 6 + 3], g[3 + 6 + 4]}, ay{g[3], g[4]}, az{g[6], g[7]};
    const f32 sum = ax.length_sq() + ay.length_sq() + az.length_sq();
    std::printf("    anéis: X %.1f Y %.1f Z %.1f (r %.0f)\n", ax.length(), ay.length(), az.length(), r);
    AUREA_CHECK_NEAR(sum, 2.0f * r * r, 0.03f * r * r);
    AUREA_CHECK_EQ(e.query_bone_gizmo(id, 0, r, g, Engine::kBoneGizmoFloats), 0u);   // nó da malha não é osso

    // 90° no eixo LOCAL Z do braço: a junta fica, a mão sobe (+X → +Y no arquivo).
    const Vec2 hand0 = screen_of(b, kLHand);
    AUREA_CHECK(e.rotate_bone_axis(id, kLArm, 2, 90.0f, false));
    b = bones_of(e, id);
    AUREA_CHECK(near3(euler_of(b, kLArm), Vec3{0, 0, 90}, 0.05f));
    AUREA_CHECK_NEAR(screen_of(b, kLArm).x, shoulder.x, 0.01f);
    AUREA_CHECK_NEAR(screen_of(b, kLArm).y, shoulder.y, 0.01f);
    const Vec2 hand90 = screen_of(b, kLHand);
    std::printf("    mão: (%.0f, %.0f) -> (%.0f, %.0f)\n", hand0.x, hand0.y, hand90.x, hand90.y);
    AUREA_CHECK(hand90.y < shoulder.y - 20.0f);   // para cima na tela
    AUREA_CHECK_NEAR(hand90.x, shoulder.x, 2.0f);

    // Anel Z arrastado 10° ao longo do anel, em dois eventos do MESMO gesto.
    AUREA_CHECK_EQ(e.query_bone_gizmo(id, kLArm, r, g, Engine::kBoneGizmoFloats), Engine::kBoneGizmoFloats);
    const f32* z = g + 3 + 2 * 6;
    const Vec2 c{g[0], g[1]}, u{z[0], z[1]}, v{z[3], z[4]};
    auto at = [&](f32 degs) {
        const f32 t = degs * 3.14159265f / 180.0f;
        return c + u * std::cos(t) + v * std::sin(t);
    };
    const Vec2 p0 = at(0), p1 = at(5), p2 = at(10);
    AUREA_CHECK(e.drag_bone_ring(id, kLArm, 2, p0.x, p0.y, p1.x, p1.y, r, false));
    AUREA_CHECK(e.drag_bone_ring(id, kLArm, 2, p1.x, p1.y, p2.x, p2.y, r, true));
    b = bones_of(e, id);
    std::printf("    anel Z: %.2f°\n", euler_of(b, kLArm).z);
    AUREA_CHECK_NEAR(euler_of(b, kLArm).z, 100.0f, 0.6f);
    undo(e);   // um passo só para o gesto inteiro
    b = bones_of(e, id);
    AUREA_CHECK_NEAR(euler_of(b, kLArm).z, 90.0f, 0.05f);
    undo(e);
    b = bones_of(e, id);
    AUREA_CHECK(near3(euler_of(b, kLArm), Vec3{0, 0, 0}, 0.05f));

    // Anel de fora (eixo 3) = giro em volta do olhar: mexe o osso.
    const Vec2 o0 = c + Vec2{r * 1.2f, 0}, o1 = c + Vec2{0, r * 1.2f};
    AUREA_CHECK(e.drag_bone_ring(id, kLArm, 3, o0.x, o0.y, o1.x, o1.y, r, false));
    b = bones_of(e, id);
    AUREA_CHECK(std::fabs(screen_of(b, kLHand).y - hand0.y) > 20.0f);
    undo(e);
    e.shutdown();
    std::remove(req.path.c_str());
}

AUREA_TEST(RigSkeleton, BatchRotationsMirrorAndSimpleIkUseOneUndoEach) {
    Engine e;
    AUREA_CHECK(e.initialize(skeleton_config()).ok());
    AUREA_CHECK(e.new_project(640, 640, 30.0, "esqueleto").ok());
    ModelImport req;
    req.path = write_symmetric_rig();
    auto imported = e.import_model(req);
    AUREA_CHECK(imported.ok());
    if (!imported.ok()) { e.shutdown(); std::remove(req.path.c_str()); return; }
    const u64 id = *imported;
    AUREA_CHECK_EQ(e.bone_mirror_partner(id, kLArm), kRArm);
    AUREA_CHECK_EQ(e.bone_mirror_partner(id, kRArm), kLArm);
    AUREA_CHECK_EQ(e.bone_mirror_partner(id, kSpine), -1);
    AUREA_CHECK_EQ(e.bone_mirror_partner(id, 0), -1);

    // Lote (campos numéricos / colar): um passo de desfazer.
    const f32 batch[8]{static_cast<f32>(kLArm), 0, 0, 45, static_cast<f32>(kSpine), 0, 30, 0};
    AUREA_CHECK(e.set_bone_rotations(id, batch, 2, false));
    std::vector<f32> b = bones_of(e, id);
    AUREA_CHECK(near3(euler_of(b, kLArm), Vec3{0, 0, 45}, 1e-3f));
    AUREA_CHECK(near3(euler_of(b, kSpine), Vec3{0, 30, 0}, 1e-3f));
    const f32 bad[4]{0, 1, 2, 3};   // nó 0 é a malha
    AUREA_CHECK(!e.set_bone_rotations(id, bad, 1, false));

    // Espelhar o braço esquerdo para o direito: o giro em Z troca de sinal.
    AUREA_CHECK(e.mirror_bone_pose(id, kLArm));
    b = bones_of(e, id);
    std::printf("    espelho: dir. (%.1f %.1f %.1f)\n", euler_of(b, kRArm).x, euler_of(b, kRArm).y, euler_of(b, kRArm).z);
    AUREA_CHECK(near3(euler_of(b, kRArm), Vec3{0, 0, -45}, 0.05f));
    // As duas mãos na mesma altura (a pose ficou simétrica).
    AUREA_CHECK_NEAR(screen_of(b, kLHand).y, screen_of(b, kLHand + 3).y, 1.0f);
    AUREA_CHECK(!e.mirror_bone_pose(id, kSpine));   // osso do meio não tem par

    // Pose inteira: os lados trocam e o meio espelha nele mesmo.
    const f32 right[4]{static_cast<f32>(kRArm), 10, 0, 0};
    AUREA_CHECK(e.set_bone_rotations(id, right, 1, false));
    AUREA_CHECK(e.mirror_bone_pose(id, -1));
    b = bones_of(e, id);
    AUREA_CHECK(near3(euler_of(b, kLArm), Vec3{10, 0, 0}, 0.05f));
    AUREA_CHECK(near3(euler_of(b, kRArm), Vec3{0, 0, -45}, 0.05f));
    AUREA_CHECK(near3(euler_of(b, kSpine), Vec3{0, -30, 0}, 0.05f));
    undo(e);   // a troca inteira volta num passo
    b = bones_of(e, id);
    AUREA_CHECK(near3(euler_of(b, kLArm), Vec3{0, 0, 45}, 0.05f));
    AUREA_CHECK(near3(euler_of(b, kRArm), Vec3{10, 0, 0}, 0.05f));
    AUREA_CHECK(near3(euler_of(b, kSpine), Vec3{0, 30, 0}, 0.05f));

    // IK simples: a mão vai 40 % do braço na direção do ombro; a cadeia dobra.
    AUREA_CHECK(e.reset_bone_pose(id, -1));
    b = bones_of(e, id);
    const Vec2 sh = screen_of(b, kLArm), hand = screen_of(b, kLHand);
    const Vec2 d = (sh - hand) * 0.4f;
    AUREA_CHECK(e.drag_bone_ik(id, kLHand, d.x * 0.5f, d.y * 0.5f, false));
    AUREA_CHECK(e.drag_bone_ik(id, kLHand, d.x * 0.5f, d.y * 0.5f, true));
    b = bones_of(e, id);
    const Vec2 now = screen_of(b, kLHand), want = hand + d;
    std::printf("    IK: mão (%.0f, %.0f) -> (%.0f, %.0f), alvo (%.0f, %.0f)\n", hand.x, hand.y, now.x, now.y, want.x, want.y);
    AUREA_CHECK((now - want).length() < 2.0f);
    AUREA_CHECK((screen_of(b, kLArm) - sh).length() < 0.01f);   // o ombro fica
    AUREA_CHECK(!near3(euler_of(b, kLFore), Vec3{0, 0, 0}, 1.0f));   // o cotovelo dobrou
    AUREA_CHECK(near3(euler_of(b, kLHand), Vec3{0, 0, 0}, 1e-3f));   // a mão não gira
    AUREA_CHECK(!e.drag_bone_ik(id, kSpine, 5, 5, false));   // coluna: sem dois ossos acima
    undo(e);
    b = bones_of(e, id);
    AUREA_CHECK((screen_of(b, kLHand) - hand).length() < 0.01f);
    (void)kHips;
    e.shutdown();
    std::remove(req.path.c_str());
}

AUREA_TEST(RigSkeleton, PuppetPinsRotateAroundThePivotPinInLayerPixels) {
    Engine e;
    AUREA_CHECK(e.initialize(skeleton_config()).ok());
    AUREA_CHECK(e.new_project(320, 240, 30.0, "pinos").ok());
    const auto added = e.add_shape(0);
    AUREA_CHECK(added.ok());
    if (!added.ok()) { e.shutdown(); return; }
    const u64 id = *added;
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(id);
    add.effect_add.effectType = effect_type_id("aurea.distort.puppet");
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(e.apply_command(add).ok());
    // O id do efeito: o único na camada (o primeiro id que o motor dá).
    u32 fx = 0;
    f32 pins[64]{};
    for (u32 cand = 0; cand < 64; ++cand)
        if (e.puppet_add_pin(id, cand, 0.5f, 0.5f) == 0) { fx = cand; break; }
    AUREA_CHECK_EQ(e.puppet_add_pin(id, fx, 0.75f, 0.5f), 1);
    AUREA_CHECK_EQ(e.puppet_add_pin(id, fx, 0.5f, 0.25f), 2);
    // Meia volta: o pino 1 vai para o outro lado do pivô (independe do formato da camada).
    AUREA_CHECK(e.puppet_rotate_pins(id, fx, 0, 1u << 1, 0.75f, 0.5f, 0.25f, 0.5f, false, false));
    AUREA_CHECK_EQ(e.query_puppet(id, fx, pins, 64), 12u);
    AUREA_CHECK_NEAR(pins[4 + 1], 0.25f, 1e-4f);
    AUREA_CHECK_NEAR(pins[4 + 2], 0.5f, 1e-4f);
    AUREA_CHECK_NEAR(pins[1], 0.5f, 1e-6f);   // o pivô fica
    AUREA_CHECK_NEAR(pins[8 + 2], 0.25f, 1e-6f);   // fora do grupo fica
    // Mais 90° no MESMO gesto: em px da camada, o pino desce para cima do pivô.
    AUREA_CHECK(e.puppet_rotate_pins(id, fx, 0, (1u << 1) | 1u, 0.25f, 0.5f, 0.5f, 0.1f, false, true));
    (void)e.query_puppet(id, fx, pins, 64);
    AUREA_CHECK_NEAR(pins[4 + 1], 0.5f, 1e-4f);
    AUREA_CHECK(pins[4 + 2] < 0.5f);
    AUREA_CHECK(!e.puppet_rotate_pins(id, fx, 0, 0, 0.75f, 0.5f, 0.25f, 0.5f, false, false));   // grupo vazio
    AUREA_CHECK(!e.puppet_rotate_pins(id, fx, 9, 1u << 1, 0.75f, 0.5f, 0.25f, 0.5f, false, false));   // pivô sem pino
    undo(e);   // o gesto inteiro num passo
    (void)e.query_puppet(id, fx, pins, 64);
    AUREA_CHECK_NEAR(pins[4 + 1], 0.75f, 1e-4f);
    AUREA_CHECK_NEAR(pins[4 + 2], 0.5f, 1e-4f);
    e.shutdown();
}
