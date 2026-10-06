// =============================================================================
//  Rig 2D (personagem desenhado): pesos da pele, pose de montagem intacta,
//  osso girado mexe só a região dele, IK de 2 ossos, API do motor (montar,
//  posar com keyframe, desfazer), salvar/reabrir e o quadro na GPU.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/timeline/Rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace aurea;

namespace {

/// Braço deitado: ombro (0) → cotovelo (1) → mão (2), numa imagem 200×100.
RigData arm() {
    RigData r;
    r.joints = {RigJoint{0, kInvalidIndex, Vec2{10, 50}}, RigJoint{1, 0, Vec2{100, 50}}, RigJoint{2, 1, Vec2{190, 50}}};
    r.nextJointId = 3;
    return r;
}

Vec2 posed(const RigData& r, const std::vector<f32>& deg, u32 i) {
    std::vector<rig::Affine> b;
    std::vector<Vec2> p;
    rig::pose(r, deg, b);
    rig::posed_joints(r, b, p);
    return p[i];
}

} // namespace

AUREA_TEST(Rig, SkinWeightsAreNormalizedWithFourInfluencesAtMost) {
    RigData r = arm();
    r.joints.push_back(RigJoint{3, 1, Vec2{100, 95}});   // galho no cotovelo
    rig::SkinMesh m;
    rig::build_skin(r, 200.0f, 100.0f, 32, m);
    AUREA_CHECK(!m.rest.empty());
    AUREA_CHECK_EQ(m.weight.size(), m.rest.size() * rig::kMaxInfluences);
    AUREA_CHECK_EQ(m.tris.size() % 3, static_cast<usize>(0));
    f32 worst = 0.0f;
    for (usize v = 0; v < m.rest.size(); ++v) {
        f32 s = 0.0f;
        for (u32 k = 0; k < rig::kMaxInfluences; ++k) {
            const f32 w = m.weight[v * rig::kMaxInfluences + k];
            AUREA_CHECK(w >= 0.0f && w <= 1.0f);
            s += w;
            // A raiz não é osso: nenhum vértice segue o índice dela com peso.
            if (w > 0.0f) AUREA_CHECK(m.bone[v * rig::kMaxInfluences + k] != 0u);
        }
        worst = std::max(worst, std::fabs(s - 1.0f));
    }
    std::printf("    %zu vertices, maior erro da soma %.2e\n", m.rest.size(), static_cast<double>(worst));
    AUREA_CHECK(worst < 1e-5f);
    // Sem osso (uma junta só): malha vazia — a imagem desenha como sempre.
    RigData one;
    one.joints = {RigJoint{0, kInvalidIndex, Vec2{5, 5}}};
    rig::build_skin(one, 200.0f, 100.0f, 32, m);
    AUREA_CHECK(m.rest.empty());
}

AUREA_TEST(Rig, BindPoseLeavesTheImageUndeformed) {
    const RigData r = arm();
    rig::SkinMesh m;
    rig::build_skin(r, 200.0f, 100.0f, 32, m);
    std::vector<rig::Affine> bones;
    rig::pose(r, std::vector<f32>(r.joints.size(), 0.0f), bones);
    std::vector<Vec2> out;
    rig::deform(m, bones, out);
    f32 worst = 0.0f;
    for (usize v = 0; v < out.size(); ++v) worst = std::max(worst, (out[v] - m.rest[v]).length());
    std::printf("    maior deslocamento na montagem: %.2e px\n", static_cast<double>(worst));
    AUREA_CHECK(worst < 1e-4f);
}

AUREA_TEST(Rig, RotatingABoneMovesOnlyItsSkinnedRegion) {
    const RigData r = arm();
    rig::SkinMesh m;
    rig::build_skin(r, 200.0f, 100.0f, 32, m);
    std::vector<f32> deg{0.0f, 0.0f, 45.0f};   // gira o antebraço (cotovelo → mão)
    std::vector<rig::Affine> bones;
    rig::pose(r, deg, bones);
    std::vector<Vec2> out;
    rig::deform(m, bones, out);
    f32 upperMax = 0.0f, handMin = 1e9f;
    for (usize v = 0; v < out.size(); ++v) {
        const f32 moved = (out[v] - m.rest[v]).length();
        if (m.rest[v].x <= 60.0f) upperMax = std::max(upperMax, moved);
        if (m.rest[v].x >= 170.0f) handMin = std::min(handMin, moved);
    }
    std::printf("    braco (x<=60) andou no maximo %.3f px; mao (x>=170) no minimo %.1f px\n",
                static_cast<double>(upperMax), static_cast<double>(handMin));
    AUREA_CHECK(upperMax < 0.5f);
    AUREA_CHECK(handMin > 30.0f);
    // A mão foi para onde o osso aponta (45° em volta do cotovelo, y para baixo).
    const Vec2 hand = posed(r, deg, 2);
    AUREA_CHECK_NEAR(hand.x, 100.0f + 90.0f * std::cos(0.7853982f), 1e-3f);
    AUREA_CHECK_NEAR(hand.y, 50.0f + 90.0f * std::sin(0.7853982f), 1e-3f);
}

AUREA_TEST(Rig, TwoBoneIkReachesTheTarget) {
    const RigData r = arm();
    for (Vec2 target : {Vec2{120, 120}, Vec2{60, 90}, Vec2{150, 10}}) {
        std::vector<f32> deg(3, 0.0f);
        deg[2] = 10.0f;   // dobra de partida: define o lado do cotovelo
        std::vector<rig::Affine> bones;
        rig::pose(r, deg, bones);
        f32 dp = 0, dj = 0;
        AUREA_CHECK(rig::ik_two_bone(r, bones, 2, target, dp, dj));
        deg[1] += dp;
        deg[2] += dj;
        const Vec2 tip = posed(r, deg, 2);
        std::printf("    alvo (%.0f,%.0f): ponta em (%.3f,%.3f)\n", static_cast<double>(target.x), static_cast<double>(target.y),
                    static_cast<double>(tip.x), static_cast<double>(tip.y));
        AUREA_CHECK((tip - target).length() < 0.05f);
        // Os ossos não esticam (transformações rígidas).
        AUREA_CHECK_NEAR((posed(r, deg, 1) - posed(r, deg, 0)).length(), 90.0f, 1e-2f);
    }
    // Fora do alcance: a ponta estica na direção do alvo, no alcance máximo.
    std::vector<f32> deg{0.0f, 0.0f, 20.0f};
    std::vector<rig::Affine> bones;
    rig::pose(r, deg, bones);
    f32 dp = 0, dj = 0;
    AUREA_CHECK(rig::ik_two_bone(r, bones, 2, Vec2{10, 400}, dp, dj));
    deg[1] += dp;
    deg[2] += dj;
    const Vec2 tip = posed(r, deg, 2);
    AUREA_CHECK_NEAR(tip.x, 10.0f, 0.2f);
    AUREA_CHECK_NEAR(tip.y, 50.0f + 180.0f, 0.2f);
    // Sem avô: não há IK.
    AUREA_CHECK(!rig::ik_two_bone(r, bones, 1, Vec2{0, 0}, dp, dj));
    // FK: o osso aponta para o alvo.
    rig::pose(r, std::vector<f32>(3, 0.0f), bones);
    AUREA_CHECK_NEAR(rig::fk_delta(r, bones, 1, Vec2{10, 150}), 90.0f, 1e-3f);
}

namespace {

struct RigEngine {
    Engine e;
    u64 layer = 0;
    RigEngine() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        std::vector<u8> rgba(200u * 100u * 4u, 255);
        auto id = e.import_image(rgba.data(), 200, 100, "boneco");
        AUREA_CHECK(id.ok());
        layer = id.ok() ? *id : 0;
    }
    ~RigEngine() { e.shutdown(); }
    void seek(i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    }
    std::vector<f32> joints(bool bind) {
        std::vector<f32> v(e.query_rig(layer, bind, nullptr, 0));
        e.query_rig(layer, bind, v.data(), static_cast<u32>(v.size()));
        return v;
    }
};

} // namespace

AUREA_TEST(Rig, EngineBuildsPosesWithKeyframesAndUndo) {
    RigEngine r;
    // Imagem 200×100 encaixada na composição 320×180 (escala 1,6): tudo em px
    // da composição, como a UI manda; o motor converte para px da imagem.
    const i32 a = r.e.rig_add_joint(r.layer, -1, 70, 90);
    const i32 b = r.e.rig_add_joint(r.layer, a, 160, 90);
    const i32 c = r.e.rig_add_joint(r.layer, b, 250, 90);
    AUREA_CHECK(a >= 0 && b >= 0 && c >= 0);
    AUREA_CHECK_EQ(r.e.rig_add_joint(r.layer, 99, 0, 0), -1);   // pai inexistente
    std::vector<f32> j = r.joints(true);
    AUREA_CHECK_EQ(j.size(), static_cast<usize>(3 * Engine::kRigJointFloats));
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 2], 250.0f, 1e-3f);
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 1], static_cast<f32>(b), 1e-6f);
    AUREA_CHECK_NEAR(j[1], -1.0f, 1e-6f);
    // Mover na montagem: a junta anda; arrastar continua no mesmo passo.
    AUREA_CHECK(r.e.rig_move_joint(r.layer, static_cast<u32>(c), 240, 90, false));
    AUREA_CHECK(r.e.rig_move_joint(r.layer, static_cast<u32>(c), 250, 90, true));

    // Animar no quadro 15: puxa a mão (ponta com avô = IK) para baixo.
    r.seek(15);
    AUREA_CHECK(r.e.rig_pose_joint(r.layer, static_cast<u32>(c), 180, 160, false));
    j = r.joints(false);
    std::printf("    mao posada em (%.2f, %.2f)\n", static_cast<double>(j[12]), static_cast<double>(j[13]));
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 2], 180.0f, 0.1f);
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 3], 160.0f, 0.1f);
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 4], 1.0f, 1e-6f);   // keyframe no playhead
    // Outro instante sem keyframe: a trilha de um key só segura o valor.
    r.seek(0);
    AUREA_CHECK(r.e.rig_pose_joint(r.layer, static_cast<u32>(b), 160, 20, false));   // cotovelo: FK do braço
    r.seek(8);   // no meio: interpolado entre 0 e 15
    j = r.joints(false);
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 4], 0.0f, 1e-6f);
    // Desfazer tira o último keyframe; desfazer de novo, o primeiro.
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    AUREA_CHECK(r.e.apply_command(undo).ok());
    r.seek(15);
    j = r.joints(false);
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 2], 250.0f, 1e-2f);
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats * 2 + 3], 90.0f, 1e-2f);
    // Apagar o cotovelo: a mão passa para o ombro.
    AUREA_CHECK(r.e.rig_remove_joint(r.layer, static_cast<u32>(b)));
    j = r.joints(true);
    AUREA_CHECK_EQ(j.size(), static_cast<usize>(2 * Engine::kRigJointFloats));
    AUREA_CHECK_NEAR(j[Engine::kRigJointFloats + 1], static_cast<f32>(a), 1e-6f);
    AUREA_CHECK(r.e.rig_clear(r.layer));
    AUREA_CHECK_EQ(r.e.query_rig(r.layer, true, nullptr, 0), 0u);
}

// Esqueleto automático: juntas sobre a parte VISÍVEL do desenho (alfa),
// pernas só no corpo inteiro, e a mão puxa o braço (IK) logo de cara.
AUREA_TEST(Rig, AutoHumanoidSitsOnTheVisibleCharacterAndPoses) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30.0, nullptr).ok());
    // 200×400 transparente com o "personagem" opaco em x 50..150, y 20..380.
    std::vector<u8> rgba(200u * 400u * 4u, 0);
    for (u32 y = 20; y < 380; ++y)
        for (u32 x = 50; x < 150; ++x) rgba[(static_cast<usize>(y) * 200 + x) * 4 + 3] = 255;
    auto id = e.import_image(rgba.data(), 200, 400, "personagem");
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    const u64 layer = *id;
    AUREA_CHECK_EQ(e.rig_auto_humanoid(layer), 14u);   // alto e estreito: corpo inteiro
    std::vector<f32> j(e.query_rig(layer, true, nullptr, 0));
    e.query_rig(layer, true, j.data(), static_cast<u32>(j.size()));
    AUREA_CHECK_EQ(j.size(), static_cast<usize>(14 * Engine::kRigJointFloats));
    // A mão esquerda (junta 6) é ponta com avô: puxar faz IK e grava keyframe.
    const f32 hx = j[6 * Engine::kRigJointFloats + 2], hy = j[6 * Engine::kRigJointFloats + 3];
    AUREA_CHECK(e.rig_pose_joint(layer, 6, hx - 20.0f, hy - 60.0f, false));
    std::vector<f32> posed(j.size());
    e.query_rig(layer, false, posed.data(), static_cast<u32>(posed.size()));
    AUREA_CHECK(std::fabs(posed[6 * Engine::kRigJointFloats + 3] - hy) > 10.0f);
    AUREA_CHECK_NEAR(posed[6 * Engine::kRigJointFloats + 4], 1.0f, 1e-6f);
    // De novo troca o esqueleto (e tira os keyframes); busto largo sai sem pernas.
    std::vector<u8> wide(400u * 300u * 4u, 255);
    auto bust = e.import_image(wide.data(), 400, 300, "busto");
    AUREA_CHECK(bust.ok());
    if (bust.ok()) AUREA_CHECK_EQ(e.rig_auto_humanoid(*bust), 10u);
    AUREA_CHECK_EQ(e.rig_auto_humanoid(layer), 14u);
    e.query_rig(layer, false, posed.data(), static_cast<u32>(posed.size()));
    AUREA_CHECK_NEAR(posed[6 * Engine::kRigJointFloats + 3], hy, 1e-2f);
    AUREA_CHECK_EQ(e.rig_auto_humanoid(0), 0u);
    e.shutdown();
}

AUREA_TEST(Rig, SerializationRoundTripKeepsJointsAndPose) {
    auto p = Project::create_new(320, 180, 30.0, "rig");
    AUREA_CHECK(p.ok());
    Project project = std::move(*p);
    Composition* comp = project.timeline().composition(project.timeline().root());
    const LayerId id = comp->add_layer(LayerKind::Image, "boneco");
    Layer* l = comp->layer(id);
    l->rig = arm();
    l->rig.joints[2].pos = Vec2{187.25f, 61.5f};
    l->rig.nextJointId = 7;
    Track& t = l->tracks.get_or_create(TrackProperty::RigBone, kInvalidIndex, 2);
    t.set(FrameIndex{0}, 0.0f);
    t.set(FrameIndex{12}, 33.5f);
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(project, SaveOptions{}, bytes).ok());
    Project back;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load_bytes(back, bytes.data(), bytes.size(), LoadOptions{}, &report).ok());
    AUREA_CHECK_EQ(report.timelineVersion, kTimelineSectionVersion);
    const Composition* bc = back.timeline().composition(back.timeline().root());
    const Layer* bl = bc ? bc->layer(id) : nullptr;
    AUREA_CHECK(bl != nullptr);
    if (!bl) return;
    AUREA_CHECK(bl->rig == l->rig);
    std::vector<f32> deg;
    rig::sample_angles(*bl, FrameIndex{6}, deg);
    AUREA_CHECK_NEAR(deg[2], 16.75f, 1e-4f);
    AUREA_CHECK_NEAR(deg[1], 0.0f, 1e-6f);
}

#if defined(AUREA_TEST_VULKAN)

#include "VulkanBackend.hpp"

namespace {

bool rig_vulkan_ok() {
    static const int ok = [] {
        vk::Backend b;
        BackendConfig cfg;
        cfg.enableValidation = false;
        const bool r = b.initialize(cfg).ok();
        if (r) b.shutdown();
        return r ? 1 : 0;
    }();
    return ok != 0;
}

f32 rig_half(u16 h) {
    const u32 sign = (h & 0x8000u) << 16, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
    u32 bits = 0;
    if (exp == 0) {
        if (mant != 0) {
            u32 e = 113, m = mant;
            while (!(m & 0x400u)) { m <<= 1; --e; }
            bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
        } else bits = sign;
    } else if (exp == 31) bits = sign | 0x7F800000u | (mant << 13);
    else bits = sign | ((exp + 112) << 23) | (mant << 13);
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}

} // namespace

AUREA_TEST(RigGpu, PosedRigChangesTheRenderedFrame) {
    if (!rig_vulkan_ok()) {
        std::printf("(sem GPU Vulkan: pulado) ");
        return;
    }
    Engine e;
    EngineConfig ec;
    ec.backend = new vk::Backend();
    ec.backendConfig.enableValidation = false;
    ec.disableAutosave = true;
    ec.workerCount = 2;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(256, 256, 30.0, nullptr).ok());
    // Imagem 256×256 (encaixe 1:1): faixa vermelha opaca nas linhas 104..151,
    // o resto transparente. px da imagem = px da composição.
    std::vector<u8> rgba(256u * 256u * 4u, 0);
    for (u32 y = 104; y < 152; ++y)
        for (u32 x = 0; x < 256; ++x) { u8* p = &rgba[(static_cast<usize>(y) * 256 + x) * 4]; p[0] = 255; p[3] = 255; }
    auto id = e.import_image(rgba.data(), 256, 256, "barra");
    AUREA_CHECK(id.ok());
    const u64 layer = id.ok() ? *id : 0;
    auto grab = [&] {
        std::vector<u8> px;
        u32 w = 0, h = 0;
        AUREA_CHECK(e.capture_frame_rgba(256, px, w, h).ok());
        AUREA_CHECK(w == 256 && h == 256);
        return px;
    };
    auto red = [](const std::vector<u8>& px, u32 x, u32 y) { const u8* p = &px[(static_cast<usize>(y) * 256 + x) * 4]; return p[0] > 128 && p[1] < 60; };
    auto red_below = [&](const std::vector<u8>& px) {
        u32 n = 0;
        for (u32 y = 170; y < 256; ++y)
            for (u32 x = 0; x < 256; ++x) n += red(px, x, y) ? 1u : 0u;
        return n;
    };
    const i32 a = e.rig_add_joint(layer, -1, 24, 128);
    const i32 b = e.rig_add_joint(layer, a, 128, 128);
    const i32 c = e.rig_add_joint(layer, b, 232, 128);
    AUREA_CHECK(a >= 0 && b >= 0 && c >= 0);
    const std::vector<u8> bind = grab();   // montagem: igual à imagem
    AUREA_CHECK(red(bind, 228, 128));
    AUREA_CHECK_EQ(red_below(bind), 0u);
    // Puxa a mão para baixo (IK): o antebraço desce.
    AUREA_CHECK(e.rig_pose_joint(layer, static_cast<u32>(c), 128, 232, false));
    const std::vector<u8> posedPx = grab();
    u32 changed = 0;
    for (usize i = 0; i < posedPx.size(); i += 4) changed += posedPx[i] != bind[i] ? 1u : 0u;
    const u32 below = red_below(posedPx);
    std::printf("    %u pixels mudaram; %u pixels vermelhos abaixo de y=170\n", changed, below);
    AUREA_CHECK(!red(posedPx, 228, 128));
    AUREA_CHECK(below > 300u);
    AUREA_CHECK(changed > 2000u);
    // Montagem aberta: o PREVIEW volta a mostrar a imagem sem deformação. A
    // captura (capture_frame_rgba) segue o contrato do export — que sempre
    // mostra a pose —, então a montagem é lida de um quadro de prévia.
    auto grab_preview = [&] {
        std::vector<u8> px(256u * 256u * 4u, 0);
        TextureDesc d;
        d.width = d.height = 256;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.sampled = true;
        d.transferSrc = true;
        auto t = e.gpu()->create_texture(d);
        AUREA_CHECK(t.ok());
        if (!t.ok()) return px;
        std::vector<u16> half(px.size());
        AUREA_CHECK(e.render_offscreen(*t, 256, 256, true).ok());
        AUREA_CHECK(e.gpu()->read_texture(*t, half.data(), 256 * 8).ok());
        e.gpu()->destroy_texture(*t);
        auto enc = [](f32 v) {
            v = std::clamp(v, 0.0f, 1.0f);
            return static_cast<u8>(std::lround((v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f) * 255.0f));
        };
        for (usize i = 0; i < px.size(); i += 4) {
            const f32 a = rig_half(half[i + 3]);
            const f32 inv = a > 1e-5f ? 1.0f / a : 0.0f;   // pré-multiplicado
            for (usize c = 0; c < 3; ++c) px[i + c] = enc(rig_half(half[i + c]) * inv);
            px[i + 3] = static_cast<u8>(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));
        }
        return px;
    };
    e.set_rig_setup_layer(layer);
    const std::vector<u8> setup = grab_preview();
    AUREA_CHECK(red(setup, 228, 128));
    AUREA_CHECK_EQ(red_below(setup), 0u);
    e.set_rig_setup_layer(0);
    e.shutdown();
}

#endif
