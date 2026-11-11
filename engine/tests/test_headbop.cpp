// =============================================================================
//  Headbop (rig e deformação, sem GPU): rig a partir de pontos sintéticos,
//  continuidade dos pesos no pescoço (sem costura), modo quadro inteiro sem
//  rosto, várias pessoas, trilha por quadro e o armazenamento.
//  O render na GPU está em HeadbopGpu.inl (test_gpu.cpp).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/effects/Headbop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace aurea;
namespace hb = aurea::headbop;

namespace {

/// 33 pontos de um rosto de frente (imagem quadrada): olhos, nariz, orelhas,
/// boca e ombros. `roll` gira o rosto em volta do meio dos olhos; `noseShift`
/// desloca o nariz na horizontal (giro).
std::vector<f32> frontal_pose(f32 cx = 0.5f, f32 cy = 0.3f, f32 eye = 0.1f, f32 rollDeg = 0.0f, f32 noseShift = 0.0f) {
    std::vector<f32> p(33 * 4, 0.0f);
    const f32 r = rollDeg * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
    auto put = [&](u32 i, f32 dx, f32 dy, f32 conf = 0.95f) {
        p[i * 4] = cx + c * dx - s * dy;
        p[i * 4 + 1] = cy + s * dx + c * dy;
        p[i * 4 + 3] = conf;
    };
    put(2, -eye * 0.5f, 0);          // olho esquerdo (da pessoa) — à esquerda da imagem aqui
    put(5, eye * 0.5f, 0);
    put(0, noseShift, eye * 0.55f);  // nariz
    put(7, -eye * 1.15f, eye * 0.1f);
    put(8, eye * 1.15f, eye * 0.1f);
    put(9, -eye * 0.3f, eye * 1.0f);  // boca
    put(10, eye * 0.3f, eye * 1.0f);
    put(11, -eye * 2.0f, eye * 3.0f); // ombros
    put(12, eye * 2.0f, eye * 3.0f);
    return p;
}

hb::Uniforms one_face_uniforms(const hb::Pose& pose, u32 W = 400, u32 H = 700) {
    hb::RigFace f;
    const auto pts = frontal_pose(0.5f, 0.3f, 0.12f);
    AUREA_CHECK(hb::face_from_landmarks(pts.data(), static_cast<f32>(W) / H, f));
    hb::BuildInput in;
    in.width = static_cast<f32>(W);
    in.height = static_cast<f32>(H);
    in.rig.count = 1;
    in.rig.faces[0] = f;
    in.pose[0] = pose;
    in.prevPose[0] = pose;
    hb::Uniforms u;
    hb::fill_uniforms(in, u);
    return u;
}

hb::Pose strong_pose() {
    hb::Pose p;
    p.torsoDx = 0.02f;
    p.torsoDy = 0.015f;
    p.torsoRoll = 4.0f;
    p.headDx = -0.01f;
    p.headDy = 0.02f;
    p.headPitch = 18.0f;
    p.headRoll = 8.0f;
    p.headYaw = 12.0f;
    return p;
}

f32 dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

} // namespace

AUREA_TEST(Headbop, RigFromSyntheticPoseFindsHeadNeckAndShoulders) {
    hb::RigFace f;
    const auto pts = frontal_pose();
    AUREA_CHECK(hb::face_from_landmarks(pts.data(), 1.0f, f));
    std::printf("    centro (%.3f, %.3f) eixos (%.3f, %.3f) pescoço (%.3f, %.3f) ", f.center.x, f.center.y, f.axes.x,
                f.axes.y, f.neck.x, f.neck.y);
    AUREA_CHECK_NEAR(f.center.x, 0.5f, 0.01f);
    AUREA_CHECK(f.center.y < 0.31f && f.center.y > 0.2f);        // um pouco acima dos olhos
    AUREA_CHECK_NEAR(f.roll, 0.0f, 0.5f);
    AUREA_CHECK_NEAR(f.yaw, 0.0f, 1.0f);
    AUREA_CHECK(std::fabs(f.pitch) < 10.0f);
    AUREA_CHECK(f.axes.x > 0.08f && f.axes.x < 0.2f);            // ~orelha a orelha
    AUREA_CHECK(f.axes.y > f.axes.x);                            // a cabeça é mais alta que larga
    AUREA_CHECK(f.neck.y > f.center.y + f.axes.y * 0.9f);        // abaixo do queixo
    AUREA_CHECK(f.neck.y < f.shoulderA.y);                       // acima dos ombros
    AUREA_CHECK(f.shoulderA.x < f.shoulderB.x);
    AUREA_CHECK_NEAR(f.shoulderA.y, 0.6f, 0.01f);

    // Rosto inclinado 12°: o roll acompanha.
    AUREA_CHECK(hb::face_from_landmarks(frontal_pose(0.5f, 0.3f, 0.1f, 12.0f).data(), 1.0f, f));
    AUREA_CHECK_NEAR(f.roll, 12.0f, 0.5f);
    // Nariz para a direita da imagem: giro positivo.
    AUREA_CHECK(hb::face_from_landmarks(frontal_pose(0.5f, 0.3f, 0.1f, 0.0f, 0.04f).data(), 1.0f, f));
    AUREA_CHECK(f.yaw > 10.0f);
    // Imagem 9:16 (aspecto 0,5625): o centro continua em fração da imagem.
    AUREA_CHECK(hb::face_from_landmarks(frontal_pose().data(), 0.5625f, f));
    AUREA_CHECK_NEAR(f.center.x, 0.5f, 0.01f);
    // Sem olhos: ninguém.
    auto blind = frontal_pose();
    blind[2 * 4 + 3] = 0.0f;
    blind[5 * 4 + 3] = 0.1f;
    AUREA_CHECK(!hb::face_from_landmarks(blind.data(), 1.0f, f));
    // Sem ombros: sintetizados abaixo da cabeça.
    auto noShoulders = frontal_pose();
    noShoulders[11 * 4 + 3] = noShoulders[12 * 4 + 3] = 0.0f;
    AUREA_CHECK(hb::face_from_landmarks(noShoulders.data(), 1.0f, f));
    AUREA_CHECK(f.shoulderA.y > f.neck.y && f.shoulderB.x > f.shoulderA.x);
}

AUREA_TEST(Headbop, WeightsAndDeformationAreContinuousAcrossTheNeck) {
    const hb::Uniforms u = one_face_uniforms(strong_pose());
    const Vec2 neck{u.face[0].neck.x, u.face[0].neck.y};
    const f32 ry = u.face[0].head.w;
    // Uma faixa que atravessa a cabeça, o pescoço e os ombros, amostrada de 0,25 px.
    const f32 step = 0.25f;
    f32 worstJump = 0.0f, worstWeight = 0.0f;
    u32 headSeen = 0, mixed = 0;
    for (f32 x = neck.x - 60.0f; x <= neck.x + 60.0f; x += 15.0f) {
        Vec2 prev = hb::deform_point(u, Vec2{x, neck.y - 2.0f * ry});
        hb::Weights pw = hb::weights(u, 0, Vec2{x, neck.y - 2.0f * ry});
        for (f32 y = neck.y - 2.0f * ry + step; y <= neck.y + 2.0f * ry; y += step) {
            const Vec2 p{x, y};
            const Vec2 d = hb::deform_point(u, p);
            const hb::Weights w = hb::weights(u, 0, p);
            // Uma costura seria um salto bem maior que o passo (a deformação é lipschitziana).
            worstJump = std::max(worstJump, dist(d, prev) / step);
            worstWeight = std::max(worstWeight, std::max(std::fabs(w.head - pw.head), std::fabs(w.torso - pw.torso)));
            headSeen += w.head > 0.99f ? 1u : 0u;
            mixed += w.head > 0.05f && w.head < 0.95f ? 1u : 0u;
            prev = d;
            pw = w;
        }
    }
    std::printf("    maior |Δdeformação|/|Δp| %.3f, maior Δpeso %.4f por 0,25 px ", worstJump, worstWeight);
    AUREA_CHECK(headSeen > 50);
    AUREA_CHECK(mixed > 50);                // há transição (não um corte seco)
    AUREA_CHECK(worstJump < 3.0f);
    AUREA_CHECK(worstWeight < 0.02f);
    // Sem dobra: o jacobiano da deformação fica positivo na região da cabeça/pescoço.
    f32 minDet = 1e9f;
    Vec2 worstAt{};
    const f32 h = 0.5f;
    for (f32 y = neck.y - 2.2f * ry; y <= neck.y + ry; y += 3.0f) {
        for (f32 x = neck.x - 1.5f * ry; x <= neck.x + 1.5f * ry; x += 3.0f) {
            const Vec2 dx = hb::deform_point(u, Vec2{x + h, y}) - hb::deform_point(u, Vec2{x - h, y});
            const Vec2 dy = hb::deform_point(u, Vec2{x, y + h}) - hb::deform_point(u, Vec2{x, y - h});
            const f32 det = (dx.x * dy.y - dx.y * dy.x) / (4.0f * h * h);
            if (det < minDet) { minDet = det; worstAt = Vec2{x - neck.x, y - neck.y}; }
        }
    }
    std::printf("menor det(J) %.3f em (%.0f, %.0f) do pescoço, ry %.0f ", minDet, worstAt.x, worstAt.y, ry);
    AUREA_CHECK(minDet > 0.2f);
    // A cabeça anda de verdade e o fundo longe dela não.
    const Vec2 head{u.face[0].head.x, u.face[0].head.y};
    AUREA_CHECK(dist(hb::deform_point(u, head), head) > 5.0f);
    AUREA_CHECK(dist(hb::deform_point(u, Vec2{5.0f, 5.0f}), Vec2{5.0f, 5.0f}) < 1e-3f);
    // Pose neutra: identidade em todo ponto.
    const hb::Uniforms still = one_face_uniforms(hb::Pose{});
    f32 worst = 0.0f;
    for (f32 y = 0; y <= 700; y += 7) for (f32 x = 0; x <= 400; x += 7) worst = std::max(worst, dist(hb::final_point(still, {x, y}), {x, y}));
    AUREA_CHECK(worst < 1e-3f);
}

AUREA_TEST(Headbop, WithoutAFaceTheWholeFrameBopsAndTheBorderStays) {
    hb::BuildInput in;
    in.width = 640;
    in.height = 360;
    in.rig.count = 0;
    hb::Pose p;
    p.torsoDx = 0.03f;
    p.torsoDy = 0.02f;
    p.torsoRoll = 3.0f;
    in.pose[0] = in.prevPose[0] = p;
    hb::Uniforms u;
    hb::fill_uniforms(in, u);
    AUREA_CHECK(u.mode.y > 0.5f);                     // quadro inteiro
    AUREA_CHECK_EQ(u.layer.z, 56.0f);                 // paisagem: 56 × 32
    AUREA_CHECK_EQ(u.layer.w, 32.0f);
    const Vec2 c{320, 180};
    AUREA_CHECK(dist(hb::deform_point(u, c), c) > 10.0f);
    for (Vec2 corner : {Vec2{0, 0}, Vec2{640, 0}, Vec2{0, 360}, Vec2{640, 360}, Vec2{320, 0}, Vec2{0, 180}})
        AUREA_CHECK(dist(hb::deform_point(u, corner), corner) < 1e-3f);
    // O solavanco global vem com zoom que cobre a borda.
    hb::Pose j;
    j.frameDy = 0.02f;
    in.pose[0] = j;
    hb::fill_uniforms(in, u);
    const f32 dy = u.frame.y;
    AUREA_CHECK(std::fabs(dy - 0.02f * 640.0f) < 1e-3f);
    const Vec2 top = hb::final_point(u, Vec2{320, 0});
    const Vec2 bottom = hb::final_point(u, Vec2{320, 360});
    AUREA_CHECK(top.y <= 1e-3f);
    AUREA_CHECK(bottom.y >= 360.0f - 1e-3f);
    // Solavanco diagonal (frameDx, 0.5.2): o quadro anda para a direita e o zoom
    // ainda cobre as quatro bordas.
    j.frameDx = 0.024f;
    j.frameDy = 0.055f * 360.0f / 640.0f;
    in.pose[0] = j;
    hb::fill_uniforms(in, u);
    AUREA_CHECK(std::fabs(u.frameShift.x - 0.024f * 640.0f) < 1e-3f);
    const Vec2 mid = hb::final_point(u, Vec2{320, 180});
    AUREA_CHECK(mid.x > 320.0f + 15.0f && mid.y > 180.0f);
    AUREA_CHECK(hb::final_point(u, Vec2{0, 180}).x <= 1e-3f);
    AUREA_CHECK(hb::final_point(u, Vec2{640, 180}).x >= 640.0f - 1e-3f);
    AUREA_CHECK(hb::final_point(u, Vec2{320, 0}).y <= 1e-3f);
    AUREA_CHECK(hb::final_point(u, Vec2{320, 360}).y >= 360.0f - 1e-3f);
    // Canais da pose: o 12º (frameDx) vai no parâmetro do fim.
    AUREA_CHECK_EQ(hb::bake_param(11), hb::kBakeFrameDx);
    AUREA_CHECK_EQ(hb::pose_from_channels(hb::pose_channels(j).data()).frameDx, j.frameDx);
    // Grade (vértices) dentro do esperado.
    AUREA_CHECK_EQ(hb::vertex_count(32, 56), 31u * 55u * 6u);
}

AUREA_TEST(Headbop, SeveralPeopleEachGetTheirOwnRigAndOffset) {
    std::vector<hb::RigFace> people;
    for (f32 cx : {0.2f, 0.5f, 0.8f}) {
        hb::RigFace f;
        AUREA_CHECK(hb::face_from_landmarks(frontal_pose(cx, 0.3f, 0.06f).data(), 1.0f, f));
        people.push_back(f);
    }
    // O mesmo rosto pelo detector de rostos (um pouco deslocado) não vira outra pessoa.
    hb::RigFace dup;
    AUREA_CHECK(hb::face_from_landmarks(frontal_pose(0.505f, 0.302f, 0.06f).data(), 1.0f, dup));
    dup.confidence = 0.5f;
    people.push_back(dup);
    hb::RigFrame fr;
    hb::frame_from_people(people, fr);
    AUREA_CHECK_EQ(fr.count, 3u);
    AUREA_CHECK(fr.faces[0].center.x < fr.faces[1].center.x && fr.faces[1].center.x < fr.faces[2].center.x);
    f64 ph0 = 1, ph1 = 0, ph2 = 0;
    f32 g0 = 0, g1 = 0, g2 = 0;
    hb::person_offset(7, 0, ph0, g0);
    hb::person_offset(7, 1, ph1, g1);
    hb::person_offset(7, 2, ph2, g2);
    AUREA_CHECK_EQ(ph0, 0.0);
    AUREA_CHECK_EQ(g0, 1.0f);
    AUREA_CHECK(ph1 != ph2 && std::fabs(ph1) <= 0.2 && std::fabs(ph2) <= 0.2);
    AUREA_CHECK(g1 >= 0.85f && g1 <= 1.15f && g2 >= 0.85f && g2 <= 1.15f);
    f64 again = 0;
    f32 gAgain = 0;
    hb::person_offset(7, 1, again, gAgain);
    AUREA_CHECK_EQ(again, ph1);     // semeado: o mesmo sempre
    // Uniforms com três pessoas: cada cabeça anda pelo próprio peso.
    hb::BuildInput in;
    in.width = 900;
    in.height = 900;
    in.rig = fr;
    for (u32 i = 0; i < 3; ++i) { in.pose[i] = strong_pose(); in.prevPose[i] = in.pose[i]; }
    in.pose[2] = hb::Pose{};
    hb::Uniforms u;
    hb::fill_uniforms(in, u);
    AUREA_CHECK_EQ(u.mode.x, 3.0f);
    const Vec2 h0{u.face[0].head.x, u.face[0].head.y}, h2{u.face[2].head.x, u.face[2].head.y};
    AUREA_CHECK(dist(hb::deform_point(u, h0), h0) > 2.0f);
    AUREA_CHECK(dist(hb::deform_point(u, h2), h2) < 0.5f);   // a terceira está parada
}

AUREA_TEST(Headbop, TrackInterpolatesSmoothsAndRoundTrips) {
    hb::RigTrack track;
    for (u32 i = 0; i < 30; ++i) {
        hb::RigFrame fr;
        fr.time = i / 30.0;
        fr.count = 1;
        AUREA_CHECK(hb::face_from_landmarks(frontal_pose(0.3f + 0.01f * i, 0.3f, 0.1f).data(), 1.0f, fr.faces[0]));
        track.frames.push_back(fr);
    }
    const hb::RigFrame mid = hb::sample_track(track, 2.5 / 30.0);
    AUREA_CHECK_NEAR(mid.faces[0].center.x, 0.325f, 2e-3f);
    const hb::RigFrame med = hb::median_frame(track);
    AUREA_CHECK_EQ(med.count, 1u);
    AUREA_CHECK_NEAR(med.faces[0].center.x, 0.45f, 0.011f);
    hb::smooth_track(track);
    AUREA_CHECK_NEAR(track.frames[10].faces[0].center.x, 0.40f, 1e-3f);   // linear: a média mantém
    hb::StoreEntry e;
    e.track = track;
    e.grid = hb::grid_from_bpm(128.0, 0.25, 10.0);
    const std::string blob = hb::serialize_entry(e);
    hb::StoreEntry back;
    AUREA_CHECK(hb::deserialize_entry(blob, back));
    AUREA_CHECK_EQ(back.track.frames.size(), track.frames.size());
    AUREA_CHECK_NEAR(back.track.frames[7].faces[0].neck.y, track.frames[7].faces[0].neck.y, 1e-6f);
    AUREA_CHECK_EQ(back.grid.beats.size(), e.grid.beats.size());
    AUREA_CHECK(!hb::deserialize_entry(blob.substr(0, blob.size() / 2), back));
    // No cache em disco: some da memória, volta do arquivo; arquivo apagado = rig parado.
    const auto dir = std::filesystem::temp_directory_path() / "aurea_headbop_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    hb::RigStore& store = hb::RigStore::shared();
    const std::string before = store.directory();
    store.set_directory(dir.string());
    const u32 token = store.new_token();
    AUREA_CHECK(token > 0 && token < (1u << 24));
    AUREA_CHECK(store.put(token, std::make_shared<hb::StoreEntry>(e)));
    store.clear_memory();
    auto loaded = store.get(token);
    AUREA_CHECK(loaded != nullptr);
    if (loaded) AUREA_CHECK_EQ(loaded->track.frames.size(), track.frames.size());
    store.clear_memory();
    std::filesystem::remove_all(dir, ec);
    AUREA_CHECK(store.get(token) == nullptr);
    store.set_directory(before);
}

AUREA_TEST(Headbop, ParametersMapToTheMotionEngine) {
    f32 v[hb::kSeed + 1]{};
    v[hb::kPreset] = 4; v[hb::kIntensity] = 150; v[hb::kHead] = 50; v[hb::kBody] = 100; v[hb::kJolt] = 0;
    v[hb::kSway] = 200; v[hb::kTurn3d] = 40; v[hb::kDelay] = 100; v[hb::kSmoothness] = 100; v[hb::kVariation] = 0;
    v[hb::kPhase] = 0.5f; v[hb::kSubdivision] = 0; v[hb::kBeatZoom] = 5; v[hb::kBeatSource] = 1; v[hb::kBpm] = 90;
    v[hb::kSeed] = 3;
    const hb::Params p = hb::motion_params(v, hb::kSeed + 1);
    AUREA_CHECK(p.preset == hb::Preset::Groove);
    AUREA_CHECK_NEAR(p.intensity, 1.5f, 1e-6f);
    AUREA_CHECK_NEAR(p.head, 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(p.turn3d, 0.4f, 1e-6f);
    AUREA_CHECK_NEAR(p.subdivision, 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(p.beatZoom, 0.05f, 1e-6f);
    AUREA_CHECK_NEAR(static_cast<f32>(p.manualBpm), 90.0f, 1e-6f);
    AUREA_CHECK_EQ(p.seed, 3u);
    v[hb::kBeatSource] = 0;
    AUREA_CHECK_EQ(hb::motion_params(v, hb::kSeed + 1).manualBpm, 0.0);
    // Grade por fonte: tap usa os toques; automática sem análise = 120 BPM.
    AUREA_CHECK_NEAR(hb::beat_grid(2, 0, 0, 100, 1.5f, nullptr).bpm, 100.0, 1e-9);
    AUREA_CHECK_NEAR(hb::beat_grid(0, 0, 0, 0, 0, nullptr).bpm, 120.0, 1e-9);
    hb::StoreEntry e;
    e.grid = hb::grid_from_bpm(100.0, 0.0, 5.0);
    AUREA_CHECK(!hb::beat_grid(0, 0, 0, 0, 0, &e).beats.empty());
    // Canais da pose ida e volta.
    const hb::Pose s = strong_pose();
    const auto ch = hb::pose_channels(s);
    const hb::Pose b = hb::pose_from_channels(ch.data());
    AUREA_CHECK_EQ(b.headYaw, s.headYaw);
    AUREA_CHECK_EQ(b.torsoRoll, s.torsoRoll);
}

// ---- 0.5.2: movimento secundário (cabelo e roupa) ------------------------------

namespace {
hb::Params bop_secondary(float hair, float cloth, float physics = 1.0f) {
    hb::Params p;
    p.preset = hb::Preset::Bop;
    p.manualBpm = 118.0;
    p.variation = 0.0f;
    p.hair = hair;
    p.cloth = cloth;
    p.physics = physics;
    return p;
}
} // namespace

AUREA_TEST(Headbop, SecondaryMotionIsDeterministicAndOffMeansUnchanged) {
    const hb::BeatGrid g;   // BPM manual
    const hb::Params on = bop_secondary(1.0f, 1.0f);
    // Qualquer ordem de avaliação dá o mesmo (forma fechada, sem histórico).
    std::vector<hb::Pose> fwd, back(120);
    for (int i = 0; i < 120; ++i) fwd.push_back(hb::evaluate(on, g, i / 30.0));
    for (int i = 119; i >= 0; --i) back[i] = hb::evaluate(on, g, i / 30.0);
    for (int i = 0; i < 120; ++i) {
        AUREA_CHECK(std::memcmp(&fwd[i], &back[i], sizeof(hb::Pose)) == 0);
        const auto ch = hb::pose_channels(fwd[i]);
        for (f32 v : ch) AUREA_CHECK(std::isfinite(v));
    }
    // Cabelo/roupa 0: a pose é a de antes bit a bit (qualquer física).
    for (int i = 0; i < 60; ++i) {
        const hb::Pose a = hb::evaluate(bop_secondary(0, 0, 1.0f), g, i / 30.0);
        const hb::Pose b = hb::evaluate(bop_secondary(0, 0, 2.0f), g, i / 30.0);
        hb::Params legacy;
        legacy.preset = hb::Preset::Bop;
        legacy.manualBpm = 118.0;
        legacy.variation = 0.0f;
        const hb::Pose c = hb::evaluate(legacy, g, i / 30.0);
        AUREA_CHECK(std::memcmp(&a, &b, sizeof a) == 0);
        AUREA_CHECK(std::memcmp(&a, &c, sizeof a) == 0);
        AUREA_CHECK_EQ(a.hairRoll, 0.0f);
        AUREA_CHECK_EQ(a.clothDy, 0.0f);
    }
    // Parâmetros extremos: nunca NaN.
    for (float h : {0.0f, 0.01f, 2.0f, 4.0f})
        for (float ph : {0.0f, 2.0f})
            for (int i = 0; i < 30; ++i) {
                const auto ch = hb::pose_channels(hb::evaluate(bop_secondary(h, h, ph), g, i * 0.37 - 3.0));
                for (f32 v : ch) AUREA_CHECK(std::isfinite(v));
            }
}

AUREA_TEST(Headbop, HairLagsTheHeadAndOvershootsClothLagsTheTorso) {
    const hb::BeatGrid g;
    const hb::Params p = bop_secondary(1.0f, 1.0f);
    // Uma batida no regime (batida 8 a 118 BPM).
    const double beat = 60.0 / 118.0, t0 = 8.0 * beat;
    double headPeakT = 0, hairPeakT = 0, headPeak = -1e9, hairPeak = -1e9, torsoPeakT = 0, clothPeakT = 0;
    double torsoPeak = -1e9, clothPeak = -1e9;
    std::vector<double> delta;
    for (int i = 0; i < 500; ++i) {
        const double t = t0 + i * beat / 500.0;
        const hb::Pose q = hb::evaluate(p, g, t);
        const double head = q.headRoll, hair = q.headRoll + q.hairRoll;
        if (head > headPeak) { headPeak = head; headPeakT = t; }
        if (hair > hairPeak) { hairPeak = hair; hairPeakT = t; }
        const double torso = q.torsoDy, cloth = q.torsoDy + q.clothDy;
        if (torso > torsoPeak) { torsoPeak = torso; torsoPeakT = t; }
        if (cloth > clothPeak) { clothPeak = cloth; clothPeakT = t; }
        delta.push_back(q.hairRoll);
    }
    std::printf("    pico cabeça %.1f graus em %.0f ms, cabelo %.1f graus em %.0f ms; tronco %.0f ms, roupa %.0f ms ",
                headPeak, (headPeakT - t0) * 1000, hairPeak, (hairPeakT - t0) * 1000, (torsoPeakT - t0) * 1000, (clothPeakT - t0) * 1000);
    // O cabelo chega ao pico DEPOIS da cabeça e passa do ponto (pico maior).
    AUREA_CHECK(hairPeakT > headPeakT + 0.01);
    AUREA_CHECK(hairPeak > headPeak * 1.02);
    AUREA_CHECK(clothPeakT > torsoPeakT + 0.005);
    // Balança: a diferença cabelo − cabeça troca de sinal ao menos duas vezes na batida.
    u32 flips = 0;
    for (usize i = 1; i < delta.size(); ++i) flips += (delta[i - 1] < 0) != (delta[i] < 0) ? 1u : 0u;
    std::printf("trocas de sinal %u ", flips);
    AUREA_CHECK(flips >= 2);
    // Mais física = mais solto (balança mais longe).
    double loose = 0, firm = 0;
    for (int i = 0; i < 200; ++i) {
        const double t = t0 + i * beat / 200.0;
        loose = std::max(loose, std::fabs(static_cast<double>(hb::evaluate(bop_secondary(1, 1, 2.0f), g, t).hairRoll)));
        firm = std::max(firm, std::fabs(static_cast<double>(hb::evaluate(bop_secondary(1, 1, 0.0f), g, t).hairRoll)));
    }
    std::printf("fisica 0 -> %.1f, 2 -> %.1f graus ", firm, loose);
    AUREA_CHECK(loose > firm);
}

AUREA_TEST(Headbop, LooseHairAndClothDeformSmoothlyAndZeroIsIdentical) {
    const auto pts = frontal_pose(0.5f, 0.3f, 0.12f);
    hb::RigFace f;
    AUREA_CHECK(hb::face_from_landmarks(pts.data(), 400.0f / 700.0f, f));
    hb::BuildInput in;
    in.width = 400;
    in.height = 700;
    in.rig.count = 1;
    in.rig.faces[0] = f;
    in.mask = true;
    hb::Pose p = strong_pose();
    p.hairDx = 0.015f; p.hairDy = -0.01f; p.hairRoll = 9.0f; p.hairPitch = 4.0f;
    p.clothDx = 0.01f; p.clothDy = 0.008f; p.clothRoll = 2.0f;
    p.flutterX = 0.8f; p.flutterY = -0.5f; p.wavePhase = 1.0f; p.waveAmp = 0.004f;
    in.pose[0] = in.prevPose[0] = p;
    // Cabelo/roupa desligados: a deformação é a do rig rígido, bit a bit.
    hb::Uniforms off, rigid;
    hb::fill_uniforms(in, off);
    hb::BuildInput plain = in;
    plain.pose[0] = plain.prevPose[0] = strong_pose();
    hb::fill_uniforms(plain, rigid);
    for (f32 y = 0; y <= 700; y += 9)
        for (f32 x = 0; x <= 400; x += 9) {
            const Vec2 a = hb::deform_point(off, {x, y}, false, 1.0f), b = hb::deform_point(rigid, {x, y}, false, 1.0f);
            AUREA_CHECK(a.x == b.x && a.y == b.y);
        }
    in.hair = in.cloth = true;
    hb::Uniforms u;
    hb::fill_uniforms(in, u);
    AUREA_CHECK_EQ(u.layer.z, static_cast<f32>(hb::kGridXLoose));
    // Soltura: 0 no rosto, alta nas pontas do cabelo e na barra da camisa.
    const Vec2 head{u.face[0].head.x, u.face[0].head.y};
    const f32 ry = u.face[0].head.w;
    AUREA_CHECK(hb::looseness(u, 0, head, 1.0f).hair < 1e-4f);
    AUREA_CHECK(hb::looseness(u, 0, head + Vec2{0, -1.6f * ry}, 1.0f).hair > 0.5f);
    AUREA_CHECK(hb::looseness(u, 0, Vec2{u.face[0].neck.z, u.face[0].neck.w + 0.8f * u.face[0].body.y}, 1.0f).cloth > 0.6f);
    AUREA_CHECK(hb::looseness(u, 0, head + Vec2{0, -1.6f * ry}, 0.0f).hair == 0.0f);   // fora da pessoa: nada solto
    // Sem costura e sem dobra: ao longo de uma batida do Bop real (100 %), o menor
    // det(J) com cabelo/roupa soltos não pode ser pior que o do rig rígido.
    const f32 h = 0.5f;
    hb::Params bop = bop_secondary(1.0f, 1.0f);
    const double beat = 60.0 / 118.0;
    // Silhueta sintética (cabeça com cabelo + tronco), borda macia: é o recorte do
    // render. Dobra fora da pessoa não aparece (o fundo é lido no ponto da tela).
    const Vec2 neckPt{u.face[0].neck.x, u.face[0].neck.y};
    const f32 rx = u.face[0].head.z;
    auto personMask = [&](Vec2 p) {
        const f32 hx = (p.x - head.x) / (1.35f * rx), hy = (p.y - head.y + 0.25f * ry) / (1.45f * ry);
        const f32 headBlob = std::clamp((1.0f - std::sqrt(hx * hx + hy * hy)) * 2.5f, 0.0f, 1.0f);   // borda macia como a média de 5 amostras do vértice
        const f32 body = std::clamp((p.y - neckPt.y) / 20.0f, 0.0f, 1.0f) *
                         std::clamp((u.face[0].body.x * 1.1f - std::fabs(p.x - neckPt.x)) / 20.0f, 0.0f, 1.0f);
        return std::max(headBlob, body);
    };
    auto scan = [&](bool loose, Vec2& at, f32& stretch) {
        f32 minDet = 1e9f;
        stretch = 0.0f;
        for (int k = 0; k < 8; ++k) {
            hb::BuildInput step = in;
            step.hair = step.cloth = loose;
            step.pose[0] = step.prevPose[0] = hb::evaluate(bop, hb::BeatGrid{}, 8.0 * beat + k * beat / 8.0);
            hb::Uniforms uk;
            hb::fill_uniforms(step, uk);
            for (f32 y = 30; y <= 670; y += 5)
                for (f32 x = 20; x <= 380; x += 5) {
                    if (personMask({x, y}) < 0.5f) continue;   // só onde a pessoa aparece
                    auto D = [&](Vec2 q) { return hb::deform_point(uk, q, false, personMask(q)); };
                    const Vec2 dx = D({x + h, y}) - D({x - h, y});
                    const Vec2 dy = D({x, y + h}) - D({x, y - h});
                    const f32 det = (dx.x * dy.y - dx.y * dy.x) / (4.0f * h * h);
                    if (det < minDet) { minDet = det; at = Vec2{x - head.x, y - head.y}; }
                    stretch = std::max(stretch, std::max(dist(dx, {0, 0}), dist(dy, {0, 0})) / (2.0f * h));
                }
        }
        return minDet;
    };
    Vec2 rigidAt{}, looseAt{};
    f32 rigidStretch = 0, looseStretch = 0;
    const f32 rigidDet = scan(false, rigidAt, rigidStretch);
    const f32 looseDet = scan(true, looseAt, looseStretch);
    std::printf("    Bop 100%%: menor det(J) rígido %.3f em (%.0f, %.0f), solto %.3f em (%.0f, %.0f); estiramento %.2f / %.2f ",
                rigidDet, rigidAt.x, rigidAt.y, looseDet, looseAt.x, looseAt.y, rigidStretch, looseStretch);
    AUREA_CHECK(looseDet > 0.0f);
    AUREA_CHECK(rigidDet > 0.0f);
    AUREA_CHECK(looseStretch < 3.0f);
    // O cabelo anda além da cabeça.
    const Vec2 tip = head + Vec2{0, -1.8f * ry};
    AUREA_CHECK(dist(hb::deform_point(u, tip), hb::deform_point(off, tip)) > 1.0f);
}
