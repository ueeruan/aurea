// =============================================================================
//  Motion Capture (0.3.6 BETA): solver do motor com poses SINTÉTICAS — uma
//  caminhada e um agachamento gerados aqui, nas convenções das plataformas
//  (33 pontos, y para baixo, z para longe). Confere filtro One Euro, lacunas,
//  confiança por membro, 3D com ossos de comprimento constante, rotações,
//  retargeting com proporções diferentes, foot lock, intensidade, giro,
//  cancelamento, reamostragem, redução de keys e a prévia.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/rig/Ik.hpp"
#include "aurea/rig/Mocap.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace aurea;
using namespace aurea::rig;

namespace {

constexpr f32 kRad = 3.14159265358979323846f / 180.0f;
using HB = HumanoidBone;
constexpr usize ix(HB b) { return static_cast<usize>(b); }

/// Corpo sintético em espaço de captura (x = esquerda da pessoa, y cima, z para a câmera).
struct Body {
    std::array<Vec3, kPoseLandmarks> p{};
};

Vec3 swing_dir(f32 angleRad) { return Vec3{0.0f, -std::cos(angleRad), std::sin(angleRad)}; }

/// Caminhada no lugar (passo 1 Hz): coxas ±25°, joelho até 40°, braços opostos.
Body walk_pose(f32 t, f32 sideways) {
    Body b;
    const f32 ph = 2.0f * 3.14159265f * t;
    const Vec3 hips{sideways * t, 1.0f + 0.02f * std::cos(2.0f * ph), 0.0f};
    for (u32 s = 0; s < 2; ++s) {
        const f32 sign = s == 0 ? 1.0f : -1.0f;   // esquerda +x
        const f32 phase = ph + (s == 0 ? 0.0f : 3.14159265f);
        const f32 thigh = 25.0f * kRad * std::sin(phase);
        const f32 knee = 40.0f * kRad * std::max(0.0f, std::sin(phase + 1.2f));
        const Vec3 hip = hips + Vec3{0.1f * sign, 0, 0};
        const Vec3 kneeP = hip + swing_dir(thigh) * 0.45f;
        const Vec3 ankle = kneeP + swing_dir(thigh - knee) * 0.45f;
        const u32 H = s ? lm::RightHip : lm::LeftHip, K = s ? lm::RightKnee : lm::LeftKnee, A = s ? lm::RightAnkle : lm::LeftAnkle;
        b.p[H] = hip; b.p[K] = kneeP; b.p[A] = ankle;
        b.p[s ? lm::RightHeel : lm::LeftHeel] = ankle + Vec3{0, -0.05f, -0.05f};
        b.p[s ? lm::RightFootIndex : lm::LeftFootIndex] = ankle + Vec3{0, -0.07f, 0.15f};
        const f32 arm = -20.0f * kRad * std::sin(phase);
        const Vec3 sh = hips + Vec3{0.2f * sign, 0.5f, 0};
        const Vec3 el = sh + swing_dir(arm) * 0.3f;
        const Vec3 wr = el + swing_dir(arm + 25.0f * kRad) * 0.27f;
        b.p[s ? lm::RightShoulder : lm::LeftShoulder] = sh;
        b.p[s ? lm::RightElbow : lm::LeftElbow] = el;
        b.p[s ? lm::RightWrist : lm::LeftWrist] = wr;
        const Vec3 hand = swing_dir(arm + 25.0f * kRad) * 0.08f;
        b.p[s ? lm::RightIndex : lm::LeftIndex] = wr + hand + Vec3{0, 0, 0.02f};
        b.p[s ? lm::RightPinky : lm::LeftPinky] = wr + hand + Vec3{0, 0, -0.02f};
        b.p[s ? lm::RightThumb : lm::LeftThumb] = wr + hand * 0.5f + Vec3{0, 0, 0.03f};
        b.p[s ? lm::RightEar : lm::LeftEar] = hips + Vec3{0.08f * sign, 0.67f, 0.0f};
        b.p[s ? lm::RightEye : lm::LeftEye] = hips + Vec3{0.03f * sign, 0.69f, 0.09f};
    }
    b.p[lm::Nose] = hips + Vec3{0, 0.65f, 0.11f};
    return b;
}

/// Agachamento com os pés plantados (2 s por ciclo): o quadril desce 0,2 m.
Body squat_pose(f32 t) {
    Body b;
    const f32 down = 0.2f * 0.5f * (1.0f - std::cos(3.14159265f * t));
    const Vec3 hips{0.0f, 0.93f - down, -0.08f * down / 0.2f};
    for (u32 s = 0; s < 2; ++s) {
        const f32 sign = s == 0 ? 1.0f : -1.0f;
        const Vec3 hip = hips + Vec3{0.1f * sign, 0, 0};
        const Vec3 ankle{0.1f * sign, 0.08f, 0.0f};
        const Vec3 d = ankle - hip;
        const f32 len = d.length();
        const f32 h = std::sqrt(std::max(0.0f, 0.45f * 0.45f - 0.25f * len * len));
        Vec3 perp = Vec3{0, -d.z, d.y}.normalized();
        if (perp.z < 0) perp = -perp;
        const Vec3 knee = (hip + ankle) * 0.5f + perp * h;
        b.p[s ? lm::RightHip : lm::LeftHip] = hip;
        b.p[s ? lm::RightKnee : lm::LeftKnee] = knee;
        b.p[s ? lm::RightAnkle : lm::LeftAnkle] = ankle;
        b.p[s ? lm::RightHeel : lm::LeftHeel] = ankle + Vec3{0, -0.05f, -0.05f};
        b.p[s ? lm::RightFootIndex : lm::LeftFootIndex] = ankle + Vec3{0, -0.07f, 0.15f};
        const Vec3 sh = hips + Vec3{0.2f * sign, 0.5f, 0};
        b.p[s ? lm::RightShoulder : lm::LeftShoulder] = sh;
        b.p[s ? lm::RightElbow : lm::LeftElbow] = sh + Vec3{0, -0.3f, 0};
        b.p[s ? lm::RightWrist : lm::LeftWrist] = sh + Vec3{0, -0.57f, 0.0f};
        b.p[s ? lm::RightIndex : lm::LeftIndex] = sh + Vec3{0, -0.65f, 0.02f};
        b.p[s ? lm::RightPinky : lm::LeftPinky] = sh + Vec3{0, -0.65f, -0.02f};
        b.p[s ? lm::RightEar : lm::LeftEar] = hips + Vec3{0.08f * sign, 0.67f, 0.0f};
    }
    b.p[lm::Nose] = hips + Vec3{0, 0.65f, 0.11f};
    return b;
}

/// Quadro no formato das plataformas. `world`: metros (y baixo, z longe);
/// senão projeção ortográfica normalizada (altura 2,2 m na imagem) com a
/// profundidade relativa ao quadril na escala de x.
PoseFrame to_frame(const Body& b, f64 t, bool world, f32 conf = 0.95f) {
    PoseFrame f;
    f.time_s = t;
    f.has_world = world;
    const Vec3 mid = (b.p[lm::LeftHip] + b.p[lm::RightHip]) * 0.5f;
    for (u32 j = 0; j < kPoseLandmarks; ++j) {
        const Vec3 p = b.p[j];
        if (world) f.pts[j] = PoseLandmark{p.x, -p.y, -p.z, conf};
        else {
            constexpr f32 kH = 2.2f, kAspect = 9.0f / 16.0f;
            const f32 x = 0.5f + p.x / (kH * kAspect);
            const f32 y = 0.5f - (p.y - 1.1f) / kH;
            const f32 z = -(p.z - mid.z) / (kH * kAspect);
            f.pts[j] = PoseLandmark{x, y, z, conf};
        }
    }
    return f;
}

std::vector<PoseFrame> walk(u32 frames, bool world, f32 sideways = 0.0f) {
    std::vector<PoseFrame> out;
    for (u32 i = 0; i < frames; ++i) out.push_back(to_frame(walk_pose(i / 30.0f, sideways), i / 30.0, world));
    return out;
}

/// T-pose do modelo (Y cima, frente +Z), pernas com `legScale`.
HumanoidRest tpose(f32 legScale = 1.0f) {
    HumanoidRest r;
    r.rotation.fill(Quat::identity());
    r.localRotation.fill(Quat::identity());
    r.parentRotation.fill(Quat::identity());
    const f32 hipY = 0.08f + 0.87f * legScale;
    auto set = [&](HB b, Vec3 p) { r.position[ix(b)] = p; r.present[ix(b)] = 1; };
    set(HB::Hips, {0, hipY, 0});
    set(HB::Spine, {0, hipY + 0.15f, 0});
    set(HB::Chest, {0, hipY + 0.35f, 0});
    set(HB::Neck, {0, hipY + 0.52f, 0});
    set(HB::Head, {0, hipY + 0.62f, 0});
    for (u32 s = 0; s < 2; ++s) {
        const f32 sg = s == 0 ? 1.0f : -1.0f;
        set(s ? HB::RightUpperArm : HB::LeftUpperArm, {0.2f * sg, hipY + 0.47f, 0});
        set(s ? HB::RightLowerArm : HB::LeftLowerArm, {0.48f * sg, hipY + 0.47f, 0});
        set(s ? HB::RightHand : HB::LeftHand, {0.73f * sg, hipY + 0.47f, 0});
        set(s ? HB::RightUpperLeg : HB::LeftUpperLeg, {0.1f * sg, hipY - 0.05f, 0});
        set(s ? HB::RightLowerLeg : HB::LeftLowerLeg, {0.1f * sg, hipY - 0.05f - 0.42f * legScale, 0});
        set(s ? HB::RightFoot : HB::LeftFoot, {0.1f * sg, 0.08f, 0});
    }
    r.height = hipY + 0.75f;
    return r;
}

HumanoidMap full_map() {
    HumanoidMap m;
    for (usize b = 0; b < kHumanoidBoneCount; ++b) m.joint[b] = static_cast<i32>(b + 1);
    return m;
}

f32 angle_between(Vec3 a, Vec3 b) {
    return std::acos(std::clamp(a.normalized().dot(b.normalized()), -1.0f, 1.0f));
}

/// Posições de modelo pela cadeia de deltas (o que a malha mostra).
std::array<Vec3, kHumanoidBoneCount> chain(const HumanoidRest& r, const std::array<Quat, kHumanoidBoneCount>& d, Vec3 disp) {
    std::array<Vec3, kHumanoidBoneCount> out{};
    for (usize b = 0; b < kHumanoidBoneCount; ++b) {
        const HB p = parent_of(static_cast<HB>(b));
        if (p == HB::Count) { out[b] = r.position[b] + disp; continue; }
        out[b] = out[ix(p)] + rotate(d[ix(p)], r.position[b] - r.position[ix(p)]);
    }
    return out;
}

} // namespace

AUREA_TEST(Mocap, OneEuroRemovesJitterWithoutLag) {
    // Parado com tremor: o tremor cai.
    OneEuro still(1.0, 0.5);
    f64 rawErr = 0, filtErr = 0;
    u32 seed = 7;
    for (u32 i = 0; i < 300; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const f64 noise = (static_cast<f64>(seed >> 8) / 16777216.0 - 0.5) * 0.04;
        const f64 y = still.filter(0.3 + noise, 1.0 / 30.0);
        if (i > 30) { rawErr += noise * noise; filtErr += (y - 0.3) * (y - 0.3); }
    }
    AUREA_CHECK(filtErr < rawErr * 0.3);
    // Movimento rápido: o beta abre o filtro (atraso bem menor que sem beta).
    OneEuro fast(1.0, 5.0), slow(1.0, 0.0);
    f64 lagFast = 0, lagSlow = 0;
    for (u32 i = 0; i < 90; ++i) {
        const f64 x = i / 30.0;   // 1 unidade por segundo
        lagFast = x - fast.filter(x, 1.0 / 30.0);
        lagSlow = x - slow.filter(x, 1.0 / 30.0);
    }
    AUREA_CHECK(lagFast < 0.5 * lagSlow);
}

AUREA_TEST(Mocap, ShortGapsAreInterpolatedLongGapsHeld) {
    std::vector<PoseFrame> frames = walk(90, true);
    // Punho esquerdo some 5 quadros (lacuna curta) e o direito 40 (longa).
    for (u32 i = 20; i < 25; ++i) frames[i].pts[lm::LeftWrist].confidence = 0.0f;
    for (u32 i = 30; i < 70; ++i) frames[i].pts[lm::RightWrist].confidence = 0.1f;
    MocapParams p;
    p.smoothing = 0.0f;
    std::vector<PoseFrame> clean;
    MocapReport rep;
    AUREA_CHECK(clean_frames(frames, p, clean, rep));
    AUREA_CHECK_EQ(clean.size(), frames.size());
    // Curta: no meio da reta entre os vizinhos, com confiança reduzida (> 0).
    const PoseLandmark a = frames[19].pts[lm::LeftWrist], b = frames[25].pts[lm::LeftWrist];
    const PoseLandmark m = clean[22].pts[lm::LeftWrist];
    AUREA_CHECK_NEAR(m.x, a.x + (b.x - a.x) * 0.5f, 1e-3f);
    AUREA_CHECK_NEAR(m.y, a.y + (b.y - a.y) * 0.5f, 1e-3f);
    AUREA_CHECK(m.confidence > 0.0f);
    // Longa: segura o vizinho, confiança 0 (o membro conta como incerto).
    AUREA_CHECK_EQ(clean[35].pts[lm::RightWrist].confidence, 0.0f);
    AUREA_CHECK_NEAR(clean[35].pts[lm::RightWrist].x, frames[29].pts[lm::RightWrist].x, 1e-5f);
    AUREA_CHECK(rep.interpolatedFraction > 0.0f);
    AUREA_CHECK_NEAR(rep.lowFraction[static_cast<usize>(Limb::RightArm)], 40.0f / 90.0f, 0.02f);
    AUREA_CHECK(rep.lowFraction[static_cast<usize>(Limb::LeftLeg)] < 0.01f);
    AUREA_CHECK(rep.lowConfidence);
}

AUREA_TEST(Mocap, HiddenLimbIsReportedMissingAndStaysAtRest) {
    std::vector<PoseFrame> frames = walk(60, true);
    for (auto& f : frames)
        for (const u32 j : {lm::LeftKnee, lm::LeftAnkle, lm::LeftHeel, lm::LeftFootIndex}) f.pts[j].confidence = 0.0f;
    MocapResult res;
    AUREA_CHECK(solve(frames, tpose(), MocapParams{}, res));
    AUREA_CHECK(res.report.missing[static_cast<usize>(Limb::LeftLeg)] != 0);
    AUREA_CHECK(res.report.lowConfidence);
    // A perna oculta segue o quadril (sem rotação própria).
    for (usize f = 0; f < res.local.size(); f += 10) {
        const Quat q = res.local[f][ix(HB::LeftLowerLeg)];
        AUREA_CHECK(std::fabs(q.w) > 0.999f);
    }
}

AUREA_TEST(Mocap, LiftKeepsBoneLengthsConstantAndRecoversDepthSign) {
    const std::vector<PoseFrame> frames = walk(90, false);
    MocapParams p;
    p.imageAspect = 9.0f / 16.0f;
    std::vector<CapturePose> cap;
    const f32 leg = lift_frames(frames, p, cap);
    AUREA_CHECK(leg > 0.0f);
    f32 lo = 1e9f, hi = 0.0f;
    u32 signOk = 0, signTotal = 0;
    for (usize f = 0; f < cap.size(); ++f) {
        const f32 l = (cap[f][lm::LeftKnee] - cap[f][lm::LeftHip]).length();
        lo = std::min(lo, l);
        hi = std::max(hi, l);
        const Body b = walk_pose(f / 30.0f, 0.0f);
        const f32 truth = b.p[lm::LeftKnee].z - b.p[lm::LeftHip].z;
        if (std::fabs(truth) > 0.08f) {
            ++signTotal;
            const f32 got = cap[f][lm::LeftKnee].z - cap[f][lm::LeftHip].z;
            if ((got > 0) == (truth > 0)) ++signOk;
        }
    }
    AUREA_CHECK(hi - lo < 0.02f * hi);   // comprimento constante (2%)
    AUREA_CHECK(signTotal > 10);
    AUREA_CHECK(signOk * 10 >= signTotal * 9);
}

AUREA_TEST(Mocap, WalkRotationsFollowTheCapturedLimbs) {
    for (const bool world : {true, false}) {
        const std::vector<PoseFrame> frames = walk(120, world);
        MocapParams p;
        p.smoothing = 0.15f;
        p.imageAspect = 9.0f / 16.0f;
        p.footLock = false;
        p.footIk = false;
        p.handIk = false;
        const HumanoidRest rest = tpose();
        MocapResult res;
        AUREA_CHECK(solve(frames, rest, p, res));
        AUREA_CHECK_EQ(res.world.size(), frames.size());
        const Vec3 restThigh = rest.position[ix(HB::LeftLowerLeg)] - rest.position[ix(HB::LeftUpperLeg)];
        const Vec3 restShin = rest.position[ix(HB::LeftFoot)] - rest.position[ix(HB::LeftLowerLeg)];
        const Vec3 restArm = rest.position[ix(HB::RightLowerArm)] - rest.position[ix(HB::RightUpperArm)];
        f32 worstThigh = 0, worstShin = 0, worstArm = 0;
        for (usize f = 5; f + 5 < frames.size(); ++f) {
            const Body b = walk_pose(f / 30.0f, 0.0f);
            const auto& d = res.world[f];
            worstThigh = std::max(worstThigh, angle_between(rotate(d[ix(HB::LeftUpperLeg)], restThigh), b.p[lm::LeftKnee] - b.p[lm::LeftHip]));
            worstShin = std::max(worstShin, angle_between(rotate(d[ix(HB::LeftLowerLeg)], restShin), b.p[lm::LeftAnkle] - b.p[lm::LeftKnee]));
            worstArm = std::max(worstArm, angle_between(rotate(d[ix(HB::RightUpperArm)], restArm), b.p[lm::RightElbow] - b.p[lm::RightShoulder]));
        }
        const f32 tol = (world ? 6.0f : 12.0f) * kRad;
        AUREA_CHECK_MSG(worstThigh < tol, world ? "coxa (3D)" : "coxa (2D)");
        AUREA_CHECK_MSG(worstShin < tol, world ? "canela (3D)" : "canela (2D)");
        AUREA_CHECK_MSG(worstArm < tol, world ? "braço (3D)" : "braço (2D)");
        // Rotação local = o que reconstrói o mundo: R⁻¹·Δpai⁻¹·Δ·R com R = identidade.
        const auto& d = res.world[40];
        const Quat expect = (conj(d[ix(HB::LeftUpperLeg)]) * d[ix(HB::LeftLowerLeg)]).normalized();
        const Quat got = res.local[40][ix(HB::LeftLowerLeg)];
        AUREA_CHECK(std::fabs(expect.x * got.x + expect.y * got.y + expect.z * got.z + expect.w * got.w) > 0.9999f);
    }
}

AUREA_TEST(Mocap, KneeNeverBendsForward) {
    std::vector<PoseFrame> frames = walk(60, true);
    // Ruído grosseiro: tornozelo esquerdo à frente do joelho dobrado ao contrário.
    for (auto& f : frames) {
        f.pts[lm::LeftAnkle].z = f.pts[lm::LeftKnee].z - 0.3f;
        f.pts[lm::LeftAnkle].y = f.pts[lm::LeftKnee].y + 0.35f;
    }
    const HumanoidRest rest = tpose();
    MocapResult res;
    MocapParams p;
    p.footLock = false;
    AUREA_CHECK(solve(frames, rest, p, res));
    for (usize f = 0; f < res.world.size(); f += 7) {
        const auto& d = res.world[f];
        const Vec3 thigh = rotate(d[ix(HB::LeftUpperLeg)], rest.position[ix(HB::LeftLowerLeg)] - rest.position[ix(HB::LeftUpperLeg)]);
        const Vec3 shin = rotate(d[ix(HB::LeftLowerLeg)], rest.position[ix(HB::LeftFoot)] - rest.position[ix(HB::LeftLowerLeg)]);
        const Vec3 axis = rotate(d[ix(HB::LeftUpperLeg)], Vec3{1, 0, 0});
        const f32 bend = std::atan2(thigh.cross(shin).dot(axis), thigh.dot(shin));
        AUREA_CHECK(bend > -2.0f * kRad);
    }
}

AUREA_TEST(Mocap, FootLockKeepsPlantedFeetStillWithDifferentProportions) {
    std::vector<PoseFrame> frames;
    for (u32 i = 0; i < 120; ++i) frames.push_back(to_frame(squat_pose(i / 30.0f), i / 30.0, true));
    const HumanoidRest rest = tpose(1.35f);   // pernas 35% mais longas que as da captura
    f32 slideLocked = 0, slideFree = 0;
    for (const bool lock : {true, false}) {
        MocapParams p;
        p.footLock = lock;
        p.smoothing = 0.3f;
        MocapResult res;
        AUREA_CHECK(solve(frames, rest, p, res));
        if (lock) AUREA_CHECK(res.report.footLockedFraction > 0.7f);
        Vec3 first{};
        f32 worst = 0;
        for (usize f = 0; f < res.world.size(); ++f) {
            const auto pos = chain(rest, res.world[f], res.hipsLocal[f]);
            const Vec3 foot = pos[ix(HB::LeftFoot)];
            if (f == 10) first = foot;
            if (f > 10 && (!lock || res.footLocked[f][0])) worst = std::max(worst, (foot - first).length());
            // Nunca abaixo do chão do modelo (tornozelo em repouso).
            AUREA_CHECK(foot.y > rest.position[ix(HB::LeftFoot)].y - 0.01f);
        }
        (lock ? slideLocked : slideFree) = worst;
        // Retargeting: o quadril desce ~0,2 m × (perna do modelo / perna da captura).
        f32 lowest = 1e9f;
        for (const Vec3 h : res.hipsLocal) lowest = std::min(lowest, h.y);
        const f32 scale = res.legLength / 0.9f;
        AUREA_CHECK_NEAR(lowest - res.hipsLocal.front().y, -0.2f * scale, 0.05f * scale);
        // De pé no começo (joelhos quase retos): o quadril quase não sai do repouso.
        AUREA_CHECK(std::fabs(res.hipsLocal.front().y) < 0.1f * scale);
    }
    AUREA_CHECK(slideLocked < 0.01f);
    AUREA_CHECK(slideLocked <= slideFree + 1e-3f);
}

AUREA_TEST(Mocap, IntensityYawAndSpeed) {
    const std::vector<PoseFrame> frames = walk(90, true);
    const HumanoidRest rest = tpose();
    MocapParams zero;
    zero.intensity = 0.0f;
    zero.footLock = false;
    zero.handIk = false;
    zero.rootMotion = false;
    MocapResult r0;
    AUREA_CHECK(solve(frames, rest, zero, r0));
    f32 worst = 0;
    for (const auto& l : r0.local)
        for (const Quat q : l) worst = std::max(worst, 2.0f * std::acos(std::min(1.0f, std::fabs(q.w))));
    AUREA_CHECK(worst < 0.5f * kRad);
    // Giro de 90°: o quadril gira 90° em volta de cima a mais que sem giro.
    MocapParams a, b;
    b.yawDeg = 90.0f;
    MocapResult ra, rb;
    AUREA_CHECK(solve(frames, rest, a, ra));
    AUREA_CHECK(solve(frames, rest, b, rb));
    const Vec3 fa = rotate(ra.world[30][ix(HB::Hips)], Vec3{0, 0, 1});
    const Vec3 fb = rotate(rb.world[30][ix(HB::Hips)], Vec3{0, 0, 1});
    AUREA_CHECK_NEAR(angle_between(Vec3{fa.x, 0, fa.z}, Vec3{fb.x, 0, fb.z}), 90.0f * kRad, 2.0f * kRad);
    // Velocidade 2×: metade dos quadros na timeline.
    MocapTimeline t1, t2;
    resample(ra, a, 30.0, 10, t1);
    MocapParams fast = a;
    fast.speed = 2.0f;
    resample(ra, fast, 30.0, 10, t2);
    AUREA_CHECK_EQ(t1.startFrame, 10);
    AUREA_CHECK_EQ(t1.local.size(), static_cast<usize>(90));
    AUREA_CHECK_EQ(t2.local.size(), static_cast<usize>(45));
}

AUREA_TEST(Mocap, CancelStopsAndProgressReachesTheEnd) {
    const std::vector<PoseFrame> frames = walk(200, false);
    std::atomic<bool> cancel{true};
    std::atomic<f32> progress{0.0f};
    MocapControl c;
    c.cancel = &cancel;
    c.progress = &progress;
    MocapResult res;
    AUREA_CHECK(!solve(frames, tpose(), MocapParams{}, res, c));
    cancel.store(false);
    AUREA_CHECK(solve(frames, tpose(), MocapParams{}, res, c));
    AUREA_CHECK_NEAR(progress.load(), 1.0f, 1e-6f);
}

AUREA_TEST(Mocap, KeyReductionKeepsTheCurveWithinTolerance) {
    // Reta + uma lombada: a reta vira 2 keys; a lombada fica.
    std::vector<f32> v;
    for (u32 i = 0; i < 100; ++i) v.push_back(i < 40 || i > 60 ? 0.5f * i : 0.5f * i + 10.0f * std::sin((i - 40) * 3.14159265f / 20.0f));
    std::vector<u8> keep;
    reduce_keys(v, 1, {0.2f}, keep);
    u32 kept = 0;
    for (const u8 k : keep) kept += k;
    AUREA_CHECK(keep.front() && keep.back());
    AUREA_CHECK(kept < 30);
    usize a = 0;
    for (usize i = 1; i < v.size(); ++i) {
        if (!keep[i]) continue;
        for (usize k = a + 1; k < i; ++k) {
            const f32 s = static_cast<f32>(k - a) / static_cast<f32>(i - a);
            AUREA_CHECK(std::fabs(v[k] - (v[a] + (v[i] - v[a]) * s)) <= 0.2f + 1e-4f);
        }
        a = i;
    }
    // Keys da caminhada: menos keys que quadros, quadril com translação.
    const HumanoidRest rest = tpose();
    MocapResult res;
    AUREA_CHECK(solve(walk(90, true, 0.5f), rest, MocapParams{}, res));
    MocapTimeline tl;
    resample(res, MocapParams{}, 30.0, 0, tl);
    std::vector<BoneKey> keys;
    bake_keys(tl, full_map(), rest, true, 0.35f, keys);
    AUREA_CHECK(!keys.empty());
    AUREA_CHECK(keys.size() < tl.local.size() * kHumanoidBoneCount);
    bool hipsMoved = false;
    for (const BoneKey& k : keys) {
        AUREA_CHECK(k.frame >= 0 && k.frame < static_cast<i64>(tl.local.size()));
        if (k.node == full_map().joint[ix(HB::Hips)]) {
            AUREA_CHECK(k.hasTranslation);
            hipsMoved = hipsMoved || std::fabs(k.translation.x) > 0.2f;
        } else {
            AUREA_CHECK(!k.hasTranslation);
        }
    }
    AUREA_CHECK(hipsMoved);   // andou de lado 0,5 m/s × 3 s
}

AUREA_TEST(Mocap, PreviewSamplesTheTimelineAndOnlyTheActiveOneCounts) {
    const HumanoidRest rest = tpose();
    MocapResult res;
    AUREA_CHECK(solve(walk(60, true), rest, MocapParams{}, res));
    MocapTimeline tl;
    resample(res, MocapParams{}, 30.0, 5, tl);
    auto pv = std::make_shared<const MocapPreview>(make_preview(tl, full_map(), true));
    AUREA_CHECK_EQ(pv->nodes.size(), kHumanoidBoneCount);
    AUREA_CHECK_EQ(pv->frames(), static_cast<u32>(tl.local.size()));
    Quat q;
    pv->sample(5.0 + 12.0, ix(HB::LeftUpperLeg), q);
    const Quat e = tl.local[12][ix(HB::LeftUpperLeg)];
    AUREA_CHECK(std::fabs(q.x * e.x + q.y * e.y + q.z * e.z + q.w * e.w) > 0.9999f);
    AUREA_CHECK(rig::active_preview(pv) == nullptr);
    set_active_preview(pv.get());
    AUREA_CHECK(rig::active_preview(pv) == pv.get());
    set_active_preview(nullptr);
    AUREA_CHECK(rig::active_preview(pv) == nullptr);
}

AUREA_TEST(Mocap, UnpackSkipsBrokenFrames) {
    std::vector<f32> data(kMocapFrameFloats * 2, 0.5f);
    data[0] = 0.0f;
    data[1] = 1.0f;
    data[kMocapFrameFloats] = std::nanf("");
    std::vector<PoseFrame> out;
    unpack_frames(data.data(), 2, out);
    AUREA_CHECK_EQ(out.size(), static_cast<usize>(1));
    AUREA_CHECK(out[0].has_world);
    AUREA_CHECK_NEAR(out[0].pts[32].confidence, 0.5f, 1e-6f);
}
