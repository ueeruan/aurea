// AutoRig 3D (0.3.6 BETA) — ver rig/AutoRig.hpp.
#include "aurea/rig/AutoRig.hpp"

#include "aurea/rig/Ik.hpp"
#include "aurea/scene3d/SceneAsset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace aurea::rig {
namespace {

using B = HumanoidBone;
constexpr usize kBones = kHumanoidBoneCount;
constexpr i32 kBins = 64;
constexpr i32 kCells = 128;
constexpr usize kSurfaceSamples = 90000;
constexpr usize kMaxVertexSamples = 120000;

u32 lcg(u32& s) noexcept { s = s * 1664525u + 1013904223u; return s; }
f32 rnd01(u32& s) noexcept { return static_cast<f32>(lcg(s) >> 8) * (1.0f / 16777216.0f); }

bool mesh_node(const scene3d::SceneAsset& a, usize n) noexcept {
    const scene3d::Node& node = a.nodes[n];
    return node.inScene && node.mesh >= 0 && static_cast<usize>(node.mesh) < a.meshes.size();
}

/// Nuvem de pontos da superfície em repouso (vértices + amostras por área).
bool gather_cloud(const scene3d::SceneAsset& a, const std::vector<Mat4>& world, std::vector<Vec3>& pts,
                  const AutoRigControl& ctl) {
    usize vertices = 0;
    f64 area = 0.0;
    for (usize n = 0; n < a.nodes.size(); ++n) {
        if (!mesh_node(a, n)) continue;
        const Mat4& m = world[n];
        for (const scene3d::Primitive& p : a.meshes[static_cast<usize>(a.nodes[n].mesh)].primitives) {
            vertices += p.positions.size();
            for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
                const u32 i0 = p.indices[t], i1 = p.indices[t + 1], i2 = p.indices[t + 2];
                if (i0 >= p.positions.size() || i1 >= p.positions.size() || i2 >= p.positions.size()) continue;
                const Vec3 a0 = m.transform_point(p.positions[i0]), a1 = m.transform_point(p.positions[i1]),
                           a2 = m.transform_point(p.positions[i2]);
                area += 0.5 * static_cast<f64>((a1 - a0).cross(a2 - a0).length());
            }
        }
    }
    if (vertices < 8) return false;
    const usize stride = std::max<usize>(1, vertices / kMaxVertexSamples);
    pts.clear();
    pts.reserve(std::min(vertices, kMaxVertexSamples) + kSurfaceSamples + 16);
    u32 seed = 0x2545F491u;
    f64 carry = 0.0;
    const f64 perArea = area > 1e-12 ? static_cast<f64>(kSurfaceSamples) / area : 0.0;
    usize counter = 0;
    for (usize n = 0; n < a.nodes.size(); ++n) {
        if (!mesh_node(a, n)) continue;
        if (ctl.cancelled()) return false;
        const Mat4& m = world[n];
        for (const scene3d::Primitive& p : a.meshes[static_cast<usize>(a.nodes[n].mesh)].primitives) {
            for (usize v = 0; v < p.positions.size(); ++v, ++counter)
                if (counter % stride == 0) pts.push_back(m.transform_point(p.positions[v]));
            if (perArea <= 0.0) continue;
            for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
                const u32 i0 = p.indices[t], i1 = p.indices[t + 1], i2 = p.indices[t + 2];
                if (i0 >= p.positions.size() || i1 >= p.positions.size() || i2 >= p.positions.size()) continue;
                const Vec3 a0 = m.transform_point(p.positions[i0]), a1 = m.transform_point(p.positions[i1]),
                           a2 = m.transform_point(p.positions[i2]);
                carry += 0.5 * static_cast<f64>((a1 - a0).cross(a2 - a0).length()) * perArea;
                while (carry >= 1.0) {
                    carry -= 1.0;
                    f32 u = rnd01(seed), w = rnd01(seed);
                    if (u + w > 1.0f) { u = 1.0f - u; w = 1.0f - w; }
                    pts.push_back(a0 + (a1 - a0) * u + (a2 - a0) * w);
                }
            }
        }
    }
    for (const Vec3& p : pts)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    return pts.size() >= 64;
}

struct Frame {
    bool latX = true;     ///< eixo lateral (braços) = X; senão Z
    f32 minY = 0, H = 1, minL = 0, W = 1, Lc = 0;
    [[nodiscard]] f32 lat(Vec3 p) const noexcept { return latX ? p.x : p.z; }
    [[nodiscard]] f32 dep(Vec3 p) const noexcept { return latX ? p.z : p.x; }
    [[nodiscard]] Vec3 make(f32 l, f32 y, f32 d) const noexcept { return latX ? Vec3{l, y, d} : Vec3{d, y, l}; }
    [[nodiscard]] i32 bin(f32 y) const noexcept { return std::clamp(static_cast<i32>((y - minY) / H * kBins), 0, kBins - 1); }
    [[nodiscard]] i32 cell(f32 l) const noexcept { return std::clamp(static_cast<i32>((l - minL) / W * kCells), 0, kCells - 1); }
    [[nodiscard]] f32 bin_y(f32 b) const noexcept { return minY + (b + 0.5f) / kBins * H; }
    [[nodiscard]] f32 cell_l(f32 c) const noexcept { return minL + (c + 0.5f) / kCells * W; }
};

struct Centroid {
    f64 l = 0, y = 0, d = 0;
    u32 n = 0;
    [[nodiscard]] bool ok() const noexcept { return n > 0; }
};

template <class Pred>
Centroid centroid(const std::vector<Vec3>& pts, const Frame& f, Pred pred) {
    Centroid c;
    for (const Vec3& p : pts) {
        if (!pred(p)) continue;
        c.l += f.lat(p); c.y += p.y; c.d += f.dep(p); ++c.n;
    }
    if (c.n) { c.l /= c.n; c.y /= c.n; c.d /= c.n; }
    return c;
}

f32 dist_to_segment(Vec3 p, Vec3 a, Vec3 b) noexcept {
    const Vec3 ab = b - a;
    const f32 len2 = ab.length_sq();
    const f32 t = len2 > 1e-12f ? std::clamp((p - a).dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
    return (p - (a + ab * t)).length();
}

/// Segmento (início, fim) de cada osso para os pesos.
void bone_segments(const AutoRigJoints& j, std::array<Vec3, kBones>& s0, std::array<Vec3, kBones>& s1) noexcept {
    auto P = [&](B b) { return j.position[static_cast<usize>(b)]; };
    for (usize i = 0; i < kBones; ++i) s0[i] = j.position[i];
    s1[static_cast<usize>(B::Hips)] = P(B::Spine);
    s1[static_cast<usize>(B::Spine)] = P(B::Chest);
    s1[static_cast<usize>(B::Chest)] = P(B::Neck);
    s1[static_cast<usize>(B::Neck)] = P(B::Head);
    s1[static_cast<usize>(B::Head)] = j.headTop;
    s1[static_cast<usize>(B::LeftUpperArm)] = P(B::LeftLowerArm);
    s1[static_cast<usize>(B::LeftLowerArm)] = P(B::LeftHand);
    s1[static_cast<usize>(B::LeftHand)] = j.leftHandTip;
    s1[static_cast<usize>(B::RightUpperArm)] = P(B::RightLowerArm);
    s1[static_cast<usize>(B::RightLowerArm)] = P(B::RightHand);
    s1[static_cast<usize>(B::RightHand)] = j.rightHandTip;
    s1[static_cast<usize>(B::LeftUpperLeg)] = P(B::LeftLowerLeg);
    s1[static_cast<usize>(B::LeftLowerLeg)] = P(B::LeftFoot);
    s1[static_cast<usize>(B::LeftFoot)] = j.leftToe;
    s1[static_cast<usize>(B::RightUpperLeg)] = P(B::RightLowerLeg);
    s1[static_cast<usize>(B::RightLowerLeg)] = P(B::RightFoot);
    s1[static_cast<usize>(B::RightFoot)] = j.rightToe;
}

/// −1 esquerdo, +1 direito, 0 centro.
i32 bone_side(usize b) noexcept {
    switch (static_cast<B>(b)) {
        case B::LeftUpperArm: case B::LeftLowerArm: case B::LeftHand:
        case B::LeftUpperLeg: case B::LeftLowerLeg: case B::LeftFoot: return -1;
        case B::RightUpperArm: case B::RightLowerArm: case B::RightHand:
        case B::RightUpperLeg: case B::RightLowerLeg: case B::RightFoot: return 1;
        default: return 0;
    }
}

struct Influence {
    std::array<u16, 4> joint{0, 0, 0, 0};
    std::array<f32, 4> weight{1, 0, 0, 0};
};

/// Os 4 maiores de um vetor denso, normalizados.
Influence top4(const f32* dense) noexcept {
    Influence r;
    r.weight = {0, 0, 0, 0};
    for (usize b = 0; b < kBones; ++b) {
        const f32 w = dense[b];
        if (!(w > 0.0f)) continue;
        usize slot = 4;
        for (usize k = 0; k < 4; ++k) if (w > r.weight[k]) { slot = k; break; }
        if (slot == 4) continue;
        for (usize k = 3; k > slot; --k) { r.weight[k] = r.weight[k - 1]; r.joint[k] = r.joint[k - 1]; }
        r.weight[slot] = w;
        r.joint[slot] = static_cast<u16>(b);
    }
    f32 sum = r.weight[0] + r.weight[1] + r.weight[2] + r.weight[3];
    // Corta a cauda (< 2%): menos borrão entre ossos distantes.
    if (sum > 0.0f) for (f32& w : r.weight) if (w / sum < 0.02f) w = 0.0f;
    sum = r.weight[0] + r.weight[1] + r.weight[2] + r.weight[3];
    if (!(sum > 0.0f)) { r.weight = {1, 0, 0, 0}; r.joint = {0, 0, 0, 0}; return r; }
    for (f32& w : r.weight) w /= sum;
    return r;
}

} // namespace

const char* to_string(AutoRigError e) noexcept {
    switch (e) {
        case AutoRigError::None:          return "ok";
        case AutoRigError::NoGeometry:    return "o modelo nao tem geometria suficiente";
        case AutoRigError::AlreadyRigged: return "o modelo ja tem esqueleto";
        case AutoRigError::NotHumanoid:   return "nao parece um personagem humanoide";
        case AutoRigError::NotTPose:      return "o personagem nao esta em T-pose (bracos abertos na horizontal)";
        case AutoRigError::Cancelled:     return "cancelado";
        case AutoRigError::TooHeavy:      return "modelo pesado demais para este aparelho";
        case AutoRigError::NotImported:   return "selecione um modelo 3D importado";
        case AutoRigError::Busy:          return "outro AutoRig em andamento";
        case AutoRigError::Failed:        return "falhou";
    }
    return "?";
}

bool has_skin(const scene3d::SceneAsset& asset) noexcept {
    if (!asset.skins.empty()) return true;
    for (const scene3d::Mesh& m : asset.meshes)
        for (const scene3d::Primitive& p : m.primitives) if (p.skinned()) return true;
    return false;
}

AutoRigError detect_tpose(const scene3d::SceneAsset& asset, AutoRigJoints& out, const AutoRigControl& ctl) {
    if (asset.textGlyphLayout || asset.shapeParts) return AutoRigError::NotImported;
    if (has_skin(asset)) return AutoRigError::AlreadyRigged;
    const std::vector<Mat4> world = asset.rest_world_matrices();
    std::vector<Vec3> pts;
    ctl.report(0.02f);
    if (!gather_cloud(asset, world, pts, ctl)) return ctl.cancelled() ? AutoRigError::Cancelled : AutoRigError::NoGeometry;
    ctl.report(0.25f);
    scene3d::Aabb box;
    for (const Vec3& p : pts) box.add(p);
    Frame f;
    const Vec3 ext = box.extent();
    f.latX = ext.x >= ext.z;
    f.minY = box.min.y;
    f.H = ext.y;
    f.minL = f.latX ? box.min.x : box.min.z;
    f.W = f.latX ? ext.x : ext.z;
    if (!(f.H > 1e-6f) || !(f.W > 1e-6f)) return AutoRigError::NoGeometry;
    // De pé: mais alto que fundo. Braços abertos: envergadura perto da altura.
    if ((f.latX ? ext.z : ext.x) > f.H * 1.2f) return AutoRigError::NotHumanoid;
    if (f.W < f.H * 0.55f) return AutoRigError::NotTPose;
    const f32 H = f.H;
    auto h01 = [&](const Vec3& p) { return (p.y - f.minY) / H; };

    // Linha do centro: mediana lateral da faixa quadril/pernas.
    {
        std::vector<f32> mid;
        for (const Vec3& p : pts) { const f32 h = h01(p); if (h > 0.25f && h < 0.6f) mid.push_back(f.lat(p)); }
        if (mid.size() < 16) return AutoRigError::NotHumanoid;
        std::nth_element(mid.begin(), mid.begin() + static_cast<long>(mid.size() / 2), mid.end());
        f.Lc = mid[mid.size() / 2];
    }
    // Ocupação por fatia × célula lateral, e largura de cada fatia.
    std::vector<u32> occ(static_cast<usize>(kBins) * kCells, 0);
    std::array<f32, kBins> bmin{}, bmax{};
    bmin.fill(1e30f); bmax.fill(-1e30f);
    for (const Vec3& p : pts) {
        const i32 b = f.bin(p.y), c = f.cell(f.lat(p));
        ++occ[static_cast<usize>(b) * kCells + static_cast<usize>(c)];
        bmin[static_cast<usize>(b)] = std::min(bmin[static_cast<usize>(b)], f.lat(p));
        bmax[static_cast<usize>(b)] = std::max(bmax[static_cast<usize>(b)], f.lat(p));
    }
    auto occupied = [&](i32 b, i32 c) { return c >= 0 && c < kCells && occ[static_cast<usize>(b) * kCells + static_cast<usize>(c)] > 0; };
    const i32 cc = f.cell(f.Lc);
    // Trecho contínuo (tolera 1 célula vazia) que contém o centro.
    auto run = [&](i32 b, i32& r0, i32& r1) {
        i32 c = -1;
        for (const i32 d : {0, -1, 1, -2, 2}) if (occupied(b, cc + d)) { c = cc + d; break; }
        if (c < 0) return false;
        r0 = r1 = c;
        while (occupied(b, r0 - 1) || occupied(b, r0 - 2)) r0 -= occupied(b, r0 - 1) ? 1 : 2;
        while (occupied(b, r1 + 1) || occupied(b, r1 + 2)) r1 += occupied(b, r1 + 1) ? 1 : 2;
        return true;
    };
    auto width = [&](i32 b) { return bmax[static_cast<usize>(b)] > bmin[static_cast<usize>(b)] ? bmax[static_cast<usize>(b)] - bmin[static_cast<usize>(b)] : 0.0f; };

    // Faixa dos braços: a fatia mais larga na metade de cima, e as vizinhas quase tão largas.
    i32 bArm = -1;
    for (i32 b = static_cast<i32>(kBins * 0.40f); b < static_cast<i32>(kBins * 0.98f); ++b)
        if (bArm < 0 || width(b) > width(bArm)) bArm = b;
    if (bArm < 0 || width(bArm) < H * 0.5f) return AutoRigError::NotTPose;
    i32 band0 = bArm, band1 = bArm;
    while (band0 > 0 && width(band0 - 1) >= width(bArm) * 0.8f) --band0;
    while (band1 + 1 < kBins && width(band1 + 1) >= width(bArm) * 0.8f) ++band1;
    if (static_cast<f32>(band1 - band0 + 1) / kBins > 0.3f) return AutoRigError::NotHumanoid;   // bloco largo, não braços
    const f32 armY = f.bin_y(static_cast<f32>(band0 + band1) * 0.5f);
    // Tronco logo abaixo das axilas: tem de ser bem mais estreito que a envergadura.
    const i32 bChest = std::max(0, band0 - std::max(1, static_cast<i32>(kBins * 0.05f)));
    i32 t0 = 0, t1 = 0;
    if (!run(bChest, t0, t1)) return AutoRigError::NotHumanoid;
    const f32 torsoW = static_cast<f32>(t1 - t0 + 1) * f.W / kCells;
    if (torsoW > width(bArm) * 0.65f) return AutoRigError::NotHumanoid;
    const f32 torsoHalf = torsoW * 0.5f;
    const f32 torsoMid = f.cell_l(static_cast<f32>(t0 + t1) * 0.5f);
    if (std::fabs(torsoMid - f.Lc) < torsoHalf) f.Lc = (f.Lc + torsoMid) * 0.5f;
    // Algo acima dos ombros (cabeça).
    if ((box.max.y - f.bin_y(static_cast<f32>(band1))) < H * 0.06f) return AutoRigError::NotHumanoid;
    ctl.report(0.32f);
    if (ctl.cancelled()) return AutoRigError::Cancelled;

    // Virilha: subindo, o centro vazio (entre as pernas) fecha.
    f32 crotchY = f.minY + 0.47f * H;
    {
        bool sawGap = false;
        i32 closed = -1;
        for (i32 b = static_cast<i32>(kBins * 0.10f); b < band0; ++b) {
            const bool gap = !occupied(b, cc) && !occupied(b, cc - 1) && !occupied(b, cc + 1);
            if (gap) { sawGap = true; continue; }
            if (sawGap) { closed = b; break; }
        }
        if (sawGap && closed < 0) return AutoRigError::NotHumanoid;
        if (closed >= 0) crotchY = f.minY + static_cast<f32>(closed) / kBins * H;
    }
    if (crotchY > armY - 0.15f * H) return AutoRigError::NotHumanoid;

    // Pescoço: a fatia mais estreita entre os ombros e o topo.
    f32 neckY = armY + 0.05f * H;
    {
        const i32 topBin = f.bin(box.max.y);
        const i32 from = band1 + 1, to = band1 + std::max(1, (topBin - band1) / 2);
        f32 best = 1e30f;
        for (i32 b = from; b <= to && b < kBins; ++b) {
            i32 r0, r1;
            if (!run(b, r0, r1)) continue;
            const f32 w = static_cast<f32>(r1 - r0 + 1);
            if (w < best) { best = w; neckY = f.bin_y(static_cast<f32>(b)); }
        }
        neckY = std::clamp(neckY, armY + 0.02f * H, box.max.y - 0.06f * H);
    }
    const f32 topY = box.max.y;

    // Frente: os dedos dos pés vão à frente da canela.
    auto sideOf = [&](const Vec3& p) { return f.lat(p) - f.Lc; };
    const Centroid footC = centroid(pts, f, [&](const Vec3& p) { return h01(p) < 0.03f; });
    const Centroid shinC = centroid(pts, f, [&](const Vec3& p) { const f32 h = h01(p); return h > 0.06f && h < 0.12f; });
    f32 frontSign = 1.0f;
    if (footC.ok() && shinC.ok() && std::fabs(footC.d - shinC.d) > 0.004 * H) frontSign = footC.d > shinC.d ? 1.0f : -1.0f;
    // Esquerda do personagem = cima × frente.
    const f32 leftSign = f.latX ? frontSign : -frontSign;

    // Tronco: profundidade pelo centróide da fatia central.
    auto torsoAt = [&](f32 y, f32 half) {
        const Centroid c = centroid(pts, f, [&](const Vec3& p) {
            return std::fabs(p.y - y) < half && std::fabs(sideOf(p)) <= torsoHalf;
        });
        return f.make(f.Lc, y, c.ok() ? static_cast<f32>(c.d) : f.dep(box.center()));
    };
    const f32 hipsY = std::min(crotchY + 0.06f * H, crotchY + 0.4f * (armY - crotchY));
    AutoRigJoints j;
    auto J = [&](B b) -> Vec3& { return j.position[static_cast<usize>(b)]; };
    J(B::Hips) = torsoAt(hipsY, 0.03f * H);
    J(B::Neck) = torsoAt(neckY, 0.02f * H);
    J(B::Spine) = torsoAt(hipsY + 0.30f * (neckY - hipsY), 0.03f * H);
    J(B::Chest) = torsoAt(hipsY + 0.68f * (neckY - hipsY), 0.03f * H);
    J(B::Head) = torsoAt(neckY + 0.28f * (topY - neckY), 0.03f * H);
    j.headTop = f.make(f.Lc, topY, f.dep(J(B::Head)));
    ctl.report(0.36f);

    // Pernas.
    for (const f32 s : {leftSign, -leftSign}) {
        const bool left = s == leftSign;
        auto onSide = [&](const Vec3& p) { return sideOf(p) * s > 0.0f; };
        const Centroid thigh = centroid(pts, f, [&](const Vec3& p) {
            return onSide(p) && p.y > crotchY - 0.08f * H && p.y < crotchY - 0.01f * H && std::fabs(sideOf(p)) < width(bArm) * 0.3f;
        });
        const Centroid shin = centroid(pts, f, [&](const Vec3& p) { const f32 h = h01(p); return onSide(p) && h > 0.06f && h < 0.11f; });
        if (!thigh.ok() || !shin.ok()) return AutoRigError::NotHumanoid;
        const Vec3 upper = f.make(static_cast<f32>(thigh.l), crotchY + 0.035f * H, static_cast<f32>(thigh.d));
        const Vec3 ankle = f.make(static_cast<f32>(shin.l), f.minY + 0.045f * H, static_cast<f32>(shin.d));
        const f32 kneeY = upper.y + (ankle.y - upper.y) * 0.52f;
        const Centroid knee = centroid(pts, f, [&](const Vec3& p) { return onSide(p) && std::fabs(p.y - kneeY) < 0.03f * H; });
        const Vec3 kneeP = knee.ok() ? f.make(static_cast<f32>(knee.l), kneeY, static_cast<f32>(knee.d)) : (upper + ankle) * 0.5f;
        // Ponta do pé: o mais à frente na fatia do chão.
        f32 toeD = f.dep(ankle);
        for (const Vec3& p : pts)
            if (onSide(p) && h01(p) < 0.04f && std::fabs(f.lat(p) - f.lat(ankle)) < 0.08f * H)
                toeD = frontSign > 0 ? std::max(toeD, f.dep(p)) : std::min(toeD, f.dep(p));
        const Vec3 toe = f.make(f.lat(ankle), f.minY + 0.01f * H, toeD);
        J(left ? B::LeftUpperLeg : B::RightUpperLeg) = upper;
        J(left ? B::LeftLowerLeg : B::RightLowerLeg) = kneeP;
        J(left ? B::LeftFoot : B::RightFoot) = ankle;
        (left ? j.leftToe : j.rightToe) = toe;
    }
    ctl.report(0.40f);

    // Braços: eixo pelos centróides das seções ao longo da envergadura.
    const f32 bandLo = f.bin_y(static_cast<f32>(band0)) - 0.06f * H, bandHi = f.bin_y(static_cast<f32>(band1)) + 0.06f * H;
    for (const f32 s : {leftSign, -leftSign}) {
        const bool left = s == leftSign;
        auto armPt = [&](const Vec3& p) { return sideOf(p) * s > torsoHalf * 0.9f && p.y > bandLo && p.y < bandHi; };
        f32 reach = 0.0f;
        for (const Vec3& p : pts) if (armPt(p)) reach = std::max(reach, sideOf(p) * s);
        if (reach < torsoHalf + 0.1f * H) return AutoRigError::NotTPose;
        auto section = [&](f32 dist, f32 half) {
            return centroid(pts, f, [&](const Vec3& p) { return armPt(p) && std::fabs(sideOf(p) * s - dist) < half; });
        };
        const f32 shoulderDist = torsoHalf * 0.8f;
        const Centroid root = section(torsoHalf + 0.03f * H, 0.03f * H);
        const Centroid tip = section(reach - 0.015f * H, 0.015f * H);
        if (!root.ok() || !tip.ok()) return AutoRigError::NotTPose;
        const Vec3 shoulder = f.make(f.Lc + s * shoulderDist, static_cast<f32>(root.y), static_cast<f32>(root.d));
        const Vec3 tipP = f.make(f.Lc + s * reach, static_cast<f32>(tip.y), static_cast<f32>(tip.d));
        auto along = [&](f32 t) {
            const f32 dist = shoulderDist + (reach - shoulderDist) * t;
            const Centroid c = section(dist, 0.02f * H);
            const Vec3 lin = shoulder + (tipP - shoulder) * t;
            return c.ok() ? f.make(f.Lc + s * dist, static_cast<f32>(c.y), static_cast<f32>(c.d)) : lin;
        };
        const Vec3 elbow = along(0.42f);
        const Vec3 wrist = along(0.76f);
        // T-pose: o braço até o punho não cai mais que ~35° da horizontal.
        const f32 drop = std::fabs(wrist.y - shoulder.y), run2 = std::fabs(f.lat(wrist) - f.lat(shoulder));
        if (run2 < 1e-6f || std::atan2(drop, run2) > 0.61f) return AutoRigError::NotTPose;
        J(left ? B::LeftUpperArm : B::RightUpperArm) = shoulder;
        J(left ? B::LeftLowerArm : B::RightLowerArm) = elbow;
        J(left ? B::LeftHand : B::RightHand) = wrist;
        (left ? j.leftHandTip : j.rightHandTip) = tipP;
    }
    // Ordem vertical de um corpo de pé.
    auto y = [&](B b) { return J(b).y; };
    const bool ordered = y(B::LeftFoot) < y(B::LeftLowerLeg) && y(B::LeftLowerLeg) < y(B::LeftUpperLeg)
                      && y(B::RightFoot) < y(B::RightLowerLeg) && y(B::RightLowerLeg) < y(B::RightUpperLeg)
                      && y(B::LeftUpperLeg) < y(B::Hips) + 0.02f * H && y(B::Hips) < y(B::Spine) && y(B::Spine) < y(B::Chest)
                      && y(B::Chest) < y(B::Neck) && y(B::Neck) < y(B::Head) && y(B::Head) < topY
                      && y(B::LeftUpperArm) > y(B::Spine) && y(B::RightUpperArm) > y(B::Spine);
    if (!ordered) return AutoRigError::NotHumanoid;
    for (const Vec3& p : j.position)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return AutoRigError::NotHumanoid;
    out = j;
    ctl.report(0.42f);
    return AutoRigError::None;
}

void derive_tips(AutoRigJoints& j) noexcept {
    auto P = [&](B b) { return j.position[static_cast<usize>(b)]; };
    j.headTop = P(B::Head) + (P(B::Head) - P(B::Neck)) * 2.5f;
    j.leftHandTip = P(B::LeftHand) + (P(B::LeftHand) - P(B::LeftLowerArm)) * 0.7f;
    j.rightHandTip = P(B::RightHand) + (P(B::RightHand) - P(B::RightLowerArm)) * 0.7f;
    // Sem a frente medida: +Z (convenção glTF), meia canela de comprimento.
    j.leftToe = P(B::LeftFoot) + Vec3{0, 0, (P(B::LeftLowerLeg) - P(B::LeftFoot)).length() * 0.55f};
    j.rightToe = P(B::RightFoot) + Vec3{0, 0, (P(B::RightLowerLeg) - P(B::RightFoot)).length() * 0.55f};
}

AutoRigError build_rigged_asset(scene3d::SceneAsset& asset, const AutoRigJoints& joints, const AutoRigControl& ctl) {
    if (has_skin(asset)) return AutoRigError::AlreadyRigged;
    if (asset.textGlyphLayout || asset.shapeParts) return AutoRigError::NotImported;
    const std::vector<Mat4> world = asset.rest_world_matrices();
    std::array<Vec3, kBones> s0{}, s1{};
    bone_segments(joints, s0, s1);
    // Escala do corpo e linha do centro (para o lado de cada vértice).
    const Vec3 hips = joints.position[static_cast<usize>(B::Hips)];
    const f32 H = std::max(1e-4f, (joints.headTop - (joints.position[static_cast<usize>(B::LeftFoot)]
                                                    + joints.position[static_cast<usize>(B::RightFoot)]) * 0.5f).length());
    Vec3 lateral = joints.position[static_cast<usize>(B::LeftUpperArm)] - joints.position[static_cast<usize>(B::RightUpperArm)];
    lateral.y = 0.0f;
    lateral = lateral.length_sq() > 1e-12f ? lateral.normalized() : Vec3{1, 0, 0};
    const f32 eps = 0.012f * H;
    const f32 sideMargin = 0.015f * H;

    // Uma malha por nó: malha compartilhada por dois nós vira duas.
    std::vector<i32> meshUsers(asset.meshes.size(), 0);
    usize totalVerts = 0;
    for (usize n = 0; n < asset.nodes.size(); ++n) {
        if (!mesh_node(asset, n)) continue;
        scene3d::Node& node = asset.nodes[n];
        if (meshUsers[static_cast<usize>(node.mesh)]++ > 0) {
            asset.meshes.push_back(asset.meshes[static_cast<usize>(node.mesh)]);
            node.mesh = static_cast<i32>(asset.meshes.size() - 1);
        }
        for (const scene3d::Primitive& p : asset.meshes[static_cast<usize>(node.mesh)].primitives) totalVerts += p.positions.size();
    }
    if (totalVerts == 0) return AutoRigError::NoGeometry;
    usize doneVerts = 0;
    for (usize n = 0; n < asset.nodes.size(); ++n) {
        if (!mesh_node(asset, n)) continue;
        const Mat4& m = world[n];
        for (scene3d::Primitive& p : asset.meshes[static_cast<usize>(asset.nodes[n].mesh)].primitives) {
            const usize nv = p.positions.size();
            if (nv == 0) continue;
            // Soldar por posição: os vértices duplicados nas costuras de UV
            // recebem o MESMO peso (sem rachar ao dobrar).
            std::vector<u32> rep(nv);
            {
                std::unordered_map<u64, u32> cellOf;
                cellOf.reserve(nv);
                const f32 q = 1.0f / std::max(1e-6f, H * 1e-5f);
                for (usize v = 0; v < nv; ++v) {
                    const Vec3 w = m.transform_point(p.positions[v]);
                    const u64 kx = static_cast<u64>(static_cast<i64>(std::llround(w.x * q)) & 0x1FFFFF);
                    const u64 ky = static_cast<u64>(static_cast<i64>(std::llround(w.y * q)) & 0x1FFFFF);
                    const u64 kz = static_cast<u64>(static_cast<i64>(std::llround(w.z * q)) & 0x1FFFFF);
                    const u64 key = (kx << 42) | (ky << 21) | kz;
                    const auto ins = cellOf.emplace(key, static_cast<u32>(v));
                    rep[v] = ins.first->second;
                }
            }
            // Pesos crus por distância ao segmento.
            std::vector<Influence> inf(nv);
            f32 dense[kBones];
            for (usize v = 0; v < nv; ++v) {
                if ((v & 4095) == 0) {
                    if (ctl.cancelled()) return AutoRigError::Cancelled;
                    ctl.report(0.42f + 0.40f * static_cast<f32>(doneVerts + v) / static_cast<f32>(totalVerts));
                }
                if (rep[v] != v) continue;
                const Vec3 w = m.transform_point(p.positions[v]);
                const f32 side = (w - hips).dot(lateral);   // > 0 = esquerda
                for (usize b = 0; b < kBones; ++b) {
                    const i32 bs = bone_side(b);
                    if ((bs < 0 && side < -sideMargin) || (bs > 0 && side > sideMargin)) { dense[b] = 0.0f; continue; }
                    const f32 d = std::max(dist_to_segment(w, s0[b], s1[b]), eps);
                    const f32 d2 = d * d;
                    dense[b] = 1.0f / (d2 * d2);
                }
                inf[v] = top4(dense);
            }
            // Vizinhança pelos triângulos (nos representantes).
            std::vector<u32> start(nv + 1, 0), adj;
            for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
                const u32 a = p.indices[t], b = p.indices[t + 1], c = p.indices[t + 2];
                if (a >= nv || b >= nv || c >= nv) continue;
                start[rep[a] + 1] += 2; start[rep[b] + 1] += 2; start[rep[c] + 1] += 2;
            }
            for (usize v = 0; v < nv; ++v) start[v + 1] += start[v];
            adj.resize(start[nv]);
            {
                std::vector<u32> fill(start.begin(), start.end() - 1);
                auto link = [&](u32 x, u32 y) { adj[fill[x]++] = y; };
                for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
                    const u32 a = p.indices[t], b = p.indices[t + 1], c = p.indices[t + 2];
                    if (a >= nv || b >= nv || c >= nv) continue;
                    const u32 ra = rep[a], rb = rep[b], rc = rep[c];
                    link(ra, rb); link(ra, rc); link(rb, ra); link(rb, rc); link(rc, ra); link(rc, rb);
                }
            }
            // Suavização: metade do próprio peso + metade da média dos vizinhos.
            std::vector<Influence> next(nv);
            for (int pass = 0; pass < 2; ++pass) {
                if (ctl.cancelled()) return AutoRigError::Cancelled;
                for (usize v = 0; v < nv; ++v) {
                    if (rep[v] != v) continue;
                    std::fill(std::begin(dense), std::end(dense), 0.0f);
                    for (usize k = 0; k < 4; ++k) dense[inf[v].joint[k]] += 0.5f * inf[v].weight[k];
                    const u32 count = start[v + 1] - start[v];
                    if (count == 0) { next[v] = inf[v]; continue; }
                    const f32 share = 0.5f / static_cast<f32>(count);
                    for (u32 e = start[v]; e < start[v + 1]; ++e) {
                        const Influence& o = inf[adj[e]];
                        for (usize k = 0; k < 4; ++k) dense[o.joint[k]] += share * o.weight[k];
                    }
                    next[v] = top4(dense);
                }
                for (usize v = 0; v < nv; ++v) if (rep[v] == v) inf[v] = next[v];
            }
            p.joints.assign(nv * 4, 0);
            p.weights.assign(nv, Vec4{1, 0, 0, 0});
            for (usize v = 0; v < nv; ++v) {
                const Influence& i = inf[rep[v]];
                for (usize k = 0; k < 4; ++k) p.joints[v * 4 + k] = i.joint[k];
                p.weights[v] = Vec4{i.weight[0], i.weight[1], i.weight[2], i.weight[3]};
            }
            doneVerts += nv;
        }
    }
    ctl.report(0.88f);
    // Ossos: nós "aurea:<osso>" só com translação (o repouso é a T-pose).
    std::array<i32, kBones> nodeOf{};
    const usize first = asset.nodes.size();
    for (usize b = 0; b < kBones; ++b) {
        nodeOf[b] = static_cast<i32>(first + b);
        scene3d::Node node;
        node.name = std::string(kAutoRigNodePrefix) + bone_name(static_cast<B>(b));
        asset.nodes.push_back(std::move(node));
    }
    for (usize b = 0; b < kBones; ++b) {
        scene3d::Node& node = asset.nodes[first + b];
        const B parent = parent_of(static_cast<B>(b));
        if (parent == B::Count) {
            node.parent = -1;
            node.translation = joints.position[b];
            asset.roots.push_back(nodeOf[b]);
        } else {
            node.parent = nodeOf[static_cast<usize>(parent)];
            node.translation = joints.position[b] - joints.position[static_cast<usize>(parent)];
            asset.nodes[static_cast<usize>(node.parent)].children.push_back(nodeOf[b]);
        }
    }
    // Uma skin por nó de malha: inversa de bind = −junta × mundo do nó da malha
    // (o renderer não usa o nó de malha com skin — regra do glTF).
    for (usize n = 0; n < first; ++n) {
        if (!mesh_node(asset, n)) continue;
        scene3d::Skin skin;
        skin.name = "aurea";
        skin.skeleton = nodeOf[0];
        for (usize b = 0; b < kBones; ++b) {
            skin.joints.push_back(nodeOf[b]);
            skin.inverseBind.push_back(Mat4::translation(-joints.position[b]) * world[n]);
        }
        asset.nodes[n].skin = static_cast<i32>(asset.skins.size());
        asset.skins.push_back(std::move(skin));
    }
    asset.stats.skins = static_cast<u32>(asset.skins.size());
    asset.stats.nodes = static_cast<u32>(asset.nodes.size());
    ctl.report(0.95f);
    return AutoRigError::None;
}

std::string encode_autorig_spec(const AutoRigJoints& j) {
    std::string s = "arig1 a";
    char buf[48];
    auto put = [&](Vec3 v) {
        for (const f32 c : {v.x, v.y, v.z}) {
            std::snprintf(buf, sizeof buf, " %.7g", static_cast<double>(c));
            s += buf;
        }
    };
    for (const Vec3& p : j.position) put(p);
    put(j.headTop); put(j.leftHandTip); put(j.rightHandTip); put(j.leftToe); put(j.rightToe);
    return s;
}

std::string encode_map_spec(const HumanoidMap& map) {
    std::string s = "arig1 m";
    for (const i32 n : map.joint) s += " " + std::to_string(n);
    return s;
}

bool decode_rig_spec(const std::string& spec, bool& isAuto, AutoRigJoints& joints, HumanoidMap& map) noexcept {
    if (spec.size() < 8 || spec.rfind("arig1 ", 0) != 0) return false;
    const char kind = spec[6];
    const char* p = spec.c_str() + 7;
    char* end = nullptr;
    if (kind == 'a') {
        f32 v[(kBones + 5) * 3];
        for (f32& x : v) {
            x = std::strtof(p, &end);
            if (end == p || !std::isfinite(x)) return false;
            p = end;
        }
        for (usize b = 0; b < kBones; ++b) joints.position[b] = Vec3{v[b * 3], v[b * 3 + 1], v[b * 3 + 2]};
        const f32* t = v + kBones * 3;
        joints.headTop = Vec3{t[0], t[1], t[2]};
        joints.leftHandTip = Vec3{t[3], t[4], t[5]};
        joints.rightHandTip = Vec3{t[6], t[7], t[8]};
        joints.leftToe = Vec3{t[9], t[10], t[11]};
        joints.rightToe = Vec3{t[12], t[13], t[14]};
        isAuto = true;
        return true;
    }
    if (kind == 'm') {
        for (i32& n : map.joint) {
            const long x = std::strtol(p, &end, 10);
            if (end == p || x < -1 || x > (1 << 24)) return false;
            n = static_cast<i32>(x);
            p = end;
        }
        isAuto = false;
        return true;
    }
    return false;
}

HumanoidMap humanoid_map(const scene3d::SceneAsset& asset, const std::string& spec) {
    bool isAuto = false;
    AutoRigJoints j;
    HumanoidMap m;
    if (!spec.empty() && decode_rig_spec(spec, isAuto, j, m) && !isAuto) {
        for (i32& n : m.joint) if (n >= static_cast<i32>(asset.nodes.size())) n = -1;
        return m;
    }
    return map_by_names(asset);
}

AutoRigError apply_rig_spec(scene3d::SceneAsset& asset, const std::string& spec) {
    bool isAuto = false;
    AutoRigJoints j;
    HumanoidMap m;
    if (spec.empty() || !decode_rig_spec(spec, isAuto, j, m)) return AutoRigError::Failed;
    if (!isAuto) return AutoRigError::None;
    return build_rigged_asset(asset, j);
}

} // namespace aurea::rig
