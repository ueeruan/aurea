// =============================================================================
//  Aurea / scene3d / Animation.cpp
// =============================================================================
#include "aurea/scene3d/Animation.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::scene3d {
namespace {

/// Chave anterior a `t` e fração até a seguinte. Fora do intervalo: segura a
/// ponta (é o que o glTF manda).
void locate(const std::vector<f32>& times, f32 t, usize& i, f32& frac) noexcept {
    const usize n = times.size();
    if (n <= 1 || t <= times.front()) {
        i = 0;
        frac = 0.0f;
        return;
    }
    if (t >= times.back()) {
        i = n - 1;
        frac = 0.0f;
        return;
    }
    const auto it = std::upper_bound(times.begin(), times.end(), t);
    i = static_cast<usize>(it - times.begin()) - 1;
    const f32 dt = times[i + 1] - times[i];
    frac = dt > 0.0f ? (t - times[i]) / dt : 0.0f;
}

/// Valor de `comps` componentes do sampler no instante `t`.
bool sample(const AnimSampler& s, f32 t, u32 comps, f32* out, bool quat) noexcept {
    const usize n = s.times.size();
    const bool cubic = s.interpolation == AnimInterp::CubicSpline;
    const usize stride = static_cast<usize>(comps) * (cubic ? 3u : 1u);
    // A failed channel must leave the bind pose (or earlier valid channel)
    // intact. Check before locating: NaN time otherwise reaches times[n].
    if (n == 0 || stride == 0 || n > s.values.size() / stride || !std::isfinite(t)) return false;
    usize i = 0;
    f32 u = 0.0f;
    locate(s.times, t, i, u);
    const usize valueOff = cubic ? comps : 0u;   // (in, valor, out)
    auto at = [&](usize k, usize c) { return s.values[k * stride + valueOff + c]; };
    if (i + 1 >= n || s.interpolation == AnimInterp::Step || u == 0.0f) {
        for (u32 c = 0; c < comps; ++c) out[c] = at(i, c);
        return true;
    }
    if (cubic) {
        // Hermite: p = (2u³−3u²+1)v0 + (u³−2u²+u)·dt·b0 + (−2u³+3u²)v1 + (u³−u²)·dt·a1
        const f32 dt = s.times[i + 1] - s.times[i];
        const f32 u2 = u * u, u3 = u2 * u;
        const f32 h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
        for (u32 c = 0; c < comps; ++c) {
            const f32 v0 = at(i, c), v1 = at(i + 1, c);
            const f32 b0 = s.values[i * stride + 2 * comps + c];   // tangente de saída de i
            const f32 a1 = s.values[(i + 1) * stride + c];         // tangente de entrada de i+1
            out[c] = h00 * v0 + h10 * dt * b0 + h01 * v1 + h11 * dt * a1;
        }
        if (quat) {
            const Quat q = Quat{out[0], out[1], out[2], out[3]}.normalized();
            out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
        }
        return true;
    }
    if (quat) {
        const Quat a{at(i, 0), at(i, 1), at(i, 2), at(i, 3)};
        const Quat b{at(i + 1, 0), at(i + 1, 1), at(i + 1, 2), at(i + 1, 3)};
        const Quat q = Quat::slerp(a.normalized(), b.normalized(), u);
        out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
        return true;
    }
    for (u32 c = 0; c < comps; ++c) out[c] = at(i, c) + (at(i + 1, c) - at(i, c)) * u;
    return true;
}

} // namespace

f32 clip_time(const Animation& clip, f64 layerSeconds) noexcept {
    if (!(clip.duration > 0.0f) || !std::isfinite(clip.duration) || !std::isfinite(layerSeconds)) return 0.0f;
    const f64 d = clip.duration;
    f64 t = std::fmod(layerSeconds, d);
    if (t < 0.0) t += d;
    return static_cast<f32>(t);
}

void evaluate_pose(const SceneAsset& asset, i32 clip, f32 t, Pose& out) {
    const usize n = asset.nodes.size();
    std::vector<Vec3> tr(n), sc(n);
    std::vector<Quat> rot(n);
    out.morphWeights.assign(n, {});
    for (usize i = 0; i < n; ++i) {
        tr[i] = asset.nodes[i].translation;
        rot[i] = asset.nodes[i].rotation;
        sc[i] = asset.nodes[i].scale;
        out.morphWeights[i] = asset.nodes[i].morphWeights;
    }
    if (clip >= 0 && clip < static_cast<i32>(asset.animations.size())) {
        const Animation& a = asset.animations[static_cast<usize>(clip)];
        f32 v[4];
        for (const AnimChannel& ch : a.channels) {
            if (ch.node < 0 || ch.node >= static_cast<i32>(n) || ch.sampler >= a.samplers.size()) continue;
            const AnimSampler& s = a.samplers[ch.sampler];
            const usize node = static_cast<usize>(ch.node);
            switch (ch.path) {
                case AnimPath::Translation: if (sample(s, t, 3, v, false)) tr[node] = Vec3{v[0], v[1], v[2]}; break;
                case AnimPath::Scale:       if (sample(s, t, 3, v, false)) sc[node] = Vec3{v[0], v[1], v[2]}; break;
                case AnimPath::Rotation:    if (sample(s, t, 4, v, true)) rot[node] = Quat{v[0], v[1], v[2], v[3]}; break;
                case AnimPath::Weights: {
                    const u32 k = std::max(1u, s.components);
                    const usize stride = static_cast<usize>(k) * (s.interpolation == AnimInterp::CubicSpline ? 3u : 1u);
                    if (s.times.empty() || s.times.size() > s.values.size() / stride || !std::isfinite(t)) break;
                    out.morphWeights[node].assign(k, 0.0f);
                    sample(s, t, k, out.morphWeights[node].data(), false);
                    break;
                }
            }
        }
    }
    // Mundo: pais antes de filhos, a partir das raízes (mesma ordem do repouso).
    out.nodeWorld.assign(n, Mat4::identity());
    std::vector<u8> seen(n, 0);
    std::vector<std::pair<i32, i32>> todo;
    for (i32 r : asset.roots) todo.push_back({r, -1});
    while (!todo.empty()) {
        auto [i, parent] = todo.back();
        todo.pop_back();
        if (i < 0 || i >= static_cast<i32>(n) || seen[static_cast<usize>(i)]) continue;
        seen[static_cast<usize>(i)] = 1;
        const usize k = static_cast<usize>(i);
        const Mat4 local = Mat4::translation(tr[k]) * Mat4::from_quat(rot[k]) * Mat4::scale(sc[k]);
        out.nodeWorld[k] = parent >= 0 ? out.nodeWorld[static_cast<usize>(parent)] * local : local;
        for (i32 c : asset.nodes[k].children) todo.push_back({c, i});
    }
    // Juntas: mundo da junta × inversa de bind (no espaço da cena do modelo).
    out.jointMatrices.clear();
    out.skinJointOffset.assign(asset.skins.size(), 0);
    for (usize s = 0; s < asset.skins.size(); ++s) {
        const Skin& skin = asset.skins[s];
        out.skinJointOffset[s] = static_cast<u32>(out.jointMatrices.size());
        for (usize j = 0; j < skin.joints.size(); ++j) {
            const i32 node = skin.joints[j];
            const Mat4 jw = node >= 0 && node < static_cast<i32>(n) ? out.nodeWorld[static_cast<usize>(node)] : Mat4::identity();
            out.jointMatrices.push_back(j < skin.inverseBind.size() ? jw * skin.inverseBind[j] : jw);
        }
    }
}

} // namespace aurea::scene3d
