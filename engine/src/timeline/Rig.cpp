// =============================================================================
//  Aurea / timeline / Rig.cpp — ver Rig.hpp.
// =============================================================================
#include "aurea/timeline/Rig.hpp"

#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::rig {
namespace {

constexpr f32 kDeg = 0.017453292519943295f;

Affine rotation_about(Vec2 c, f32 deg) noexcept {
    const f32 s = std::sin(deg * kDeg), k = std::cos(deg * kDeg);
    Affine r;
    r.a = k; r.b = s; r.c = -s; r.d = k;
    r.tx = c.x - (k * c.x - s * c.y);
    r.ty = c.y - (s * c.x + k * c.y);
    return r;
}

/// (m ∘ n)(p) = m(n(p)).
Affine compose(const Affine& m, const Affine& n) noexcept {
    Affine o;
    o.a = m.a * n.a + m.c * n.b;
    o.b = m.b * n.a + m.d * n.b;
    o.c = m.a * n.c + m.c * n.d;
    o.d = m.b * n.c + m.d * n.d;
    o.tx = m.a * n.tx + m.c * n.ty + m.tx;
    o.ty = m.b * n.tx + m.d * n.ty + m.ty;
    return o;
}

f32 wrap_deg(f32 d) noexcept {
    if (!std::isfinite(d)) return 0.0f;
    d = std::fmod(d + 180.0f, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d - 180.0f;
}

f32 angle_of(Vec2 v) noexcept { return std::atan2(v.y, v.x) / kDeg; }

f32 segment_distance(Vec2 p, Vec2 a, Vec2 b) noexcept {
    const Vec2 ab = b - a;
    const f32 len2 = ab.length_sq();
    const f32 t = len2 > 1e-12f ? std::clamp((p - a).dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
    return (p - (a + ab * t)).length();
}

/// Índice do pai (−1 = raiz, pai inexistente ou apontando para si).
i32 parent_index(const RigData& rig, u32 i) noexcept {
    const u32 p = rig.joints[i].parent;
    if (p == kInvalidIndex || p == rig.joints[i].id) return -1;
    return index_of(rig, p);
}

} // namespace

i32 index_of(const RigData& rig, u32 id) noexcept {
    for (u32 i = 0; i < rig.joints.size(); ++i)
        if (rig.joints[i].id == id) return static_cast<i32>(i);
    return -1;
}

void sample_angles(const Layer& l, FrameIndex local, std::vector<f32>& deg) {
    deg.assign(l.rig.joints.size(), 0.0f);
    for (u32 i = 0; i < l.rig.joints.size(); ++i) {
        const f32 v = l.tracks.sample_or(TrackProperty::RigBone, local, 0.0f, kInvalidIndex, l.rig.joints[i].id);
        deg[i] = std::isfinite(v) ? v : 0.0f;
    }
}

void pose(const RigData& rig, const std::vector<f32>& deg, std::vector<Affine>& out) {
    const u32 n = static_cast<u32>(rig.joints.size());
    out.assign(n, Affine{});
    std::vector<u8> state(n, 0);   // 0 por fazer, 1 fazendo (ciclo), 2 pronto
    // A ordem do vetor não precisa ser pai-antes-do-filho; pai num ciclo (dado
    // corrompido) vale como identidade.
    auto solve = [&](auto& self, u32 j, u32 depth) -> Affine {
        if (state[j] == 2) return out[j];
        if (state[j] == 1 || depth > kMaxJoints) return Affine{};
        state[j] = 1;
        const i32 p = parent_index(rig, j);
        Affine m{};
        if (p >= 0)
            m = compose(self(self, static_cast<u32>(p), depth + 1),
                        rotation_about(rig.joints[static_cast<u32>(p)].pos, j < deg.size() ? deg[j] : 0.0f));
        out[j] = m;
        state[j] = 2;
        return m;
    };
    for (u32 j = 0; j < n; ++j) solve(solve, j, 0);
}

void posed_joints(const RigData& rig, const std::vector<Affine>& bones, std::vector<Vec2>& out) {
    out.resize(rig.joints.size());
    for (u32 i = 0; i < rig.joints.size(); ++i)
        out[i] = i < bones.size() ? bones[i].apply(rig.joints[i].pos) : rig.joints[i].pos;
}

void build_skin(const RigData& rig, f32 width, f32 height, u32 cells, SkinMesh& out) {
    out = SkinMesh{};
    // Ossos: juntas com pai.
    struct Seg { Vec2 a, b; u32 joint; };
    std::vector<Seg> segs;
    for (u32 i = 0; i < rig.joints.size(); ++i) {
        const i32 p = parent_index(rig, i);
        if (p >= 0) segs.push_back(Seg{rig.joints[static_cast<u32>(p)].pos, rig.joints[i].pos, i});
    }
    if (segs.empty() || !(width > 0.0f) || !(height > 0.0f)) return;
    cells = std::clamp<u32>(cells, 2u, 128u);
    const f32 longSide = std::max(width, height);
    const u32 nx = std::max<u32>(2u, static_cast<u32>(std::lround(static_cast<f32>(cells) * width / longSide)));
    const u32 ny = std::max<u32>(2u, static_cast<u32>(std::lround(static_cast<f32>(cells) * height / longSide)));
    const u32 vcount = (nx + 1) * (ny + 1);
    out.rest.reserve(vcount);
    out.uv.reserve(vcount);
    for (u32 y = 0; y <= ny; ++y) {
        for (u32 x = 0; x <= nx; ++x) {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(nx), v = static_cast<f32>(y) / static_cast<f32>(ny);
            out.rest.push_back(Vec2{u * width, v * height});
            out.uv.push_back(Vec2{u, v});
        }
    }
    out.tris.reserve(nx * ny * 6);
    for (u32 y = 0; y < ny; ++y) {
        for (u32 x = 0; x < nx; ++x) {
            const u32 i0 = y * (nx + 1) + x, i1 = i0 + 1, i2 = i0 + (nx + 1), i3 = i2 + 1;
            out.tris.insert(out.tris.end(), {i0, i1, i2, i1, i3, i2});
        }
    }
    // Pesos: pela distância ao osso RELATIVA ao osso mais perto — o mais perto
    // vale 1 e os outros caem até 0 numa faixa de mistura `blend` (≈5 % do
    // lado maior): perto de uma junta os dois ossos se misturam (a dobra fica
    // lisa) e longe dela só o osso da região manda (girar a mão não arrasta o
    // ombro). Até 4 ossos, normalizados.
    const f32 blend = std::max(4.0f, 0.05f * longSide);
    out.bone.assign(static_cast<usize>(vcount) * kMaxInfluences, 0u);
    out.weight.assign(static_cast<usize>(vcount) * kMaxInfluences, 0.0f);
    std::vector<f32> dist(segs.size());
    for (u32 v = 0; v < vcount; ++v) {
        f32 dmin = 1e30f;
        for (usize s = 0; s < segs.size(); ++s) {
            dist[s] = segment_distance(out.rest[v], segs[s].a, segs[s].b);
            dmin = std::min(dmin, dist[s]);
        }
        u32 bi[kMaxInfluences]{};
        f32 bw[kMaxInfluences]{};
        u32 have = 0;
        for (usize s = 0; s < segs.size(); ++s) {
            const f32 x = 1.0f - (dist[s] - dmin) / blend;
            if (x <= 0.0f) continue;
            const f32 w = x * x;
            // Insere mantendo os kMaxInfluences maiores (em ordem decrescente).
            u32 pos = have;
            while (pos > 0 && bw[pos - 1] < w) --pos;
            if (pos >= kMaxInfluences) continue;
            for (u32 k = std::min(have, kMaxInfluences - 1); k > pos; --k) { bw[k] = bw[k - 1]; bi[k] = bi[k - 1]; }
            bw[pos] = w;
            bi[pos] = segs[s].joint;
            have = std::min(have + 1, kMaxInfluences);
        }
        f32 sum = 0.0f;
        for (u32 k = 0; k < have; ++k) sum += bw[k];
        for (u32 k = 0; k < kMaxInfluences; ++k) {
            out.bone[v * kMaxInfluences + k] = bi[k];
            out.weight[v * kMaxInfluences + k] = sum > 0.0f ? bw[k] / sum : (k == 0 ? 1.0f : 0.0f);
        }
    }
}

void deform(const SkinMesh& m, const std::vector<Affine>& bones, std::vector<Vec2>& out) {
    out.resize(m.rest.size());
    for (usize v = 0; v < m.rest.size(); ++v) {
        Vec2 acc{0.0f, 0.0f};
        f32 wsum = 0.0f;
        for (u32 k = 0; k < kMaxInfluences; ++k) {
            const f32 w = m.weight[v * kMaxInfluences + k];
            if (w <= 0.0f) continue;
            const u32 b = m.bone[v * kMaxInfluences + k];
            acc += (b < bones.size() ? bones[b].apply(m.rest[v]) : m.rest[v]) * w;
            wsum += w;
        }
        out[v] = wsum > 0.0f ? acc / wsum : m.rest[v];
    }
}

f32 fk_delta(const RigData& rig, const std::vector<Affine>& bones, u32 index, Vec2 target) noexcept {
    if (index >= rig.joints.size() || index >= bones.size()) return 0.0f;
    const i32 p = parent_index(rig, index);
    if (p < 0) return 0.0f;
    const Vec2 pivot = bones[index].apply(rig.joints[static_cast<u32>(p)].pos);
    const Vec2 tip = bones[index].apply(rig.joints[index].pos);
    if ((target - pivot).length_sq() < 1e-6f || (tip - pivot).length_sq() < 1e-6f) return 0.0f;
    return wrap_deg(angle_of(target - pivot) - angle_of(tip - pivot));
}

bool ik_two_bone(const RigData& rig, const std::vector<Affine>& bones, u32 index, Vec2 target,
                 f32& deltaParent, f32& deltaJoint) noexcept {
    deltaParent = deltaJoint = 0.0f;
    if (index >= rig.joints.size() || bones.size() < rig.joints.size()) return false;
    const i32 p = parent_index(rig, index);
    if (p < 0) return false;
    const i32 g = parent_index(rig, static_cast<u32>(p));
    if (g < 0) return false;
    const u32 pu = static_cast<u32>(p), gu = static_cast<u32>(g);
    const Vec2 A = bones[pu].apply(rig.joints[gu].pos);
    const Vec2 B = bones[pu].apply(rig.joints[pu].pos);
    const Vec2 C = bones[index].apply(rig.joints[index].pos);
    const f32 L1 = (B - A).length(), L2 = (C - B).length();
    if (L1 < 1e-4f || L2 < 1e-4f) return false;
    const Vec2 toT = target - A;
    const f32 dist = toT.length();
    if (dist < 1e-4f) return false;
    const f32 lo = std::fabs(L1 - L2) + 1e-4f, hi = L1 + L2 - 1e-4f;
    const f32 d = std::clamp(dist, std::min(lo, hi), hi);
    const f32 cosA = std::clamp((L1 * L1 + d * d - L2 * L2) / (2.0f * L1 * d), -1.0f, 1.0f);
    const f32 alpha = std::acos(cosA) / kDeg;
    // Lado da dobra de agora (cross AB × BC); reto = um lado fixo.
    const Vec2 ab = B - A, bc = C - B;
    const f32 cross = ab.x * bc.y - ab.y * bc.x;
    const f32 side = cross >= 0.0f ? 1.0f : -1.0f;
    const f32 upperAngle = angle_of(toT) - side * alpha;
    deltaParent = wrap_deg(upperAngle - angle_of(ab));
    // Depois de girar o osso de cima: cotovelo em B', ponta em C1.
    const Affine r = rotation_about(A, deltaParent);
    const Vec2 B1 = r.apply(B), C1 = r.apply(C);
    const Vec2 goal = A + toT * (d / dist);   // fora do alcance: o ponto mais perto na reta
    deltaJoint = wrap_deg(angle_of(goal - B1) - angle_of(C1 - B1));
    return true;
}

} // namespace aurea::rig
