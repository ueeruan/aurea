#include "aurea/scene3d/GroundPlacement.hpp"

namespace aurea::scene3d {

GroundPlacement ground_placement(const SceneFrame& frame, std::span<const GpuModel* const> models,
                                 std::span<const ScenePlane> planes) noexcept {
    GroundPlacement gp;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    auto add = [&](const Aabb& b, const Mat4& m) {
        if (!b.valid()) return;
        for (int c = 0; c < 8; ++c) {
            const Vec3 pt{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y, (c & 4) ? b.max.z : b.min.z};
            const Vec3 w = m.transform_point(pt);
            lo = Vec3{std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
            hi = Vec3{std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
            gp.valid = true;
        }
    };
    // Vértices com skin na pose (Σ peso · junta × inversa de bind), no mundo.
    // Até ~8 mil por primitiva: o passo pula vértices vizinhos, e a sola de um
    // pé tem centenas deles. Morph fica na caixa (a deformação não está aqui).
    auto skin_points = [&](const Primitive& p, const auto& pose, i32 skin, const Mat4& world) {
        if (!p.skinned() || !p.morphTargets.empty() || skin < 0) return false;
        if (static_cast<usize>(skin) >= pose.skinJointOffset.size()) return false;
        const usize nv = p.positions.size();
        if (nv == 0 || p.joints.size() < 4 * nv || p.weights.size() < nv) return false;
        const usize first = pose.skinJointOffset[static_cast<usize>(skin)];
        const usize last = static_cast<usize>(skin) + 1 < pose.skinJointOffset.size()
                         ? pose.skinJointOffset[static_cast<usize>(skin) + 1] : pose.jointMatrices.size();
        if (first >= last || last > pose.jointMatrices.size()) return false;
        const usize step = std::max<usize>(1, nv / 8192);
        bool added = false;
        for (usize v = 0; v < nv; v += step) {
            const Vec4& w = p.weights[v];
            const f32 ws[4] = {w.x, w.y, w.z, w.w};
            Vec3 s{0, 0, 0};
            f32 total = 0.0f;
            for (int k = 0; k < 4; ++k) {
                if (!(ws[k] > 0.0f)) continue;
                const usize j = first + p.joints[4 * v + static_cast<usize>(k)];
                if (j >= last) continue;
                const Vec3 q = pose.jointMatrices[j].transform_point(p.positions[v]);
                s = Vec3{s.x + q.x * ws[k], s.y + q.y * ws[k], s.z + q.z * ws[k]};
                total += ws[k];
            }
            if (!(total > 1e-6f)) continue;
            const Vec3 pt = world.transform_point(Vec3{s.x / total, s.y / total, s.z / total});
            if (!std::isfinite(pt.x) || !std::isfinite(pt.y) || !std::isfinite(pt.z)) continue;
            lo = Vec3{std::min(lo.x, pt.x), std::min(lo.y, pt.y), std::min(lo.z, pt.z)};
            hi = Vec3{std::max(hi.x, pt.x), std::max(hi.y, pt.y), std::max(hi.z, pt.z)};
            gp.valid = true;
            added = true;
        }
        gp.verticesVisited += (nv + step - 1) / step;
        return added;
    };
    struct Candidate { f32 boxY; const Primitive* prim; Mat4 world; };
    std::vector<Candidate> cands;
    bool hasSkin = false;
    for (usize instanceIndex = 0; instanceIndex < frame.instances.size(); ++instanceIndex) {
        const SceneInstance& inst = frame.instances[instanceIndex];
        if (!inst.asset) continue;
        const SceneAsset& a = *inst.asset;
        const GpuModel* model = instanceIndex < models.size() ? models[instanceIndex] : nullptr;
        bool any = false;
        auto visit = [&](usize n) {
            ++gp.nodesVisited;
            const i32 mi = a.nodes[n].mesh;
            if (mi < 0 || mi >= static_cast<i32>(a.meshes.size())) return;
            hasSkin |= a.nodes[n].skin >= 0;
            const Mat4 world = a.nodes[n].skin >= 0 ? inst.world
                             : inst.world * (n < inst.pose().nodeWorld.size() ? inst.pose().nodeWorld[n] : Mat4::identity());
            for (const Primitive& p : a.meshes[static_cast<usize>(mi)].primitives) {
                if (p.material >= 0 && p.material < static_cast<i32>(a.materials.size())) {
                    const Material& m = a.materials[static_cast<usize>(p.material)];
                    if (m.alphaMode == AlphaMode::Blend || m.transmission > 0.0f) continue;
                }
                // 0.5.2: com skin, os vértices na pose do quadro. A caixa de
                // bind × camada não passa pelas juntas: num FBX em centímetros
                // (esqueleto com escala 0,01) ela fica ~100× maior, e o balanço
                // de poucos graus da camada inclinava os cantos dela centenas
                // de px abaixo dos pés — o chão descia e sumia no giro.
                if (p.bounds.valid() && a.nodes[n].skin >= 0 && skin_points(p, inst.pose(), a.nodes[n].skin, inst.world)) {
                    any = true;
                    continue;
                }
                add(p.bounds, world);
                any = any || p.bounds.valid();
                if (p.bounds.valid() && a.nodes[n].skin < 0 && !p.positions.empty()) {
                    f32 by = -1e30f;
                    for (int c = 0; c < 8; ++c) {
                        const Vec3 pt{(c & 1) ? p.bounds.max.x : p.bounds.min.x, (c & 2) ? p.bounds.max.y : p.bounds.min.y,
                                      (c & 4) ? p.bounds.max.z : p.bounds.min.z};
                        by = std::max(by, world.transform_point(pt).y);
                    }
                    cands.push_back(Candidate{by, &p, world});
                }
            }
        };
        // Uploaded topology has already selected the scene's drawable nodes.
        // The fallback keeps this helper usable before GPU upload completes.
        if (model) { for (u32 n : model->drawableNodes) visit(n); }
        else { for (usize n = 0; n < a.nodes.size(); ++n) visit(n); }
        if (!any) add(a.bounds, inst.world);
    }
    // 0.4.1: os quatro cantos de cada camada 2D no espaço 3D (região em px da camada).
    // Só numa cena SEM modelo: com modelo o chão fica sob o modelo, como antes da
    // 0.4.2. Um fundo 2D em 3D (camada de tela cheia, câmera "afeta camadas
    // abaixo") descia o chão até a borda de baixo dele e alargava o raio: a
    // sombra do modelo caía longe, fora do quadro, e mexer a luz não mudava nada
    // perto do objeto. O reflexo das camadas 2D não depende disto (Ground.cpp).
    const bool modelBounds = gp.valid;
    f32 planeBottom = -1e30f;
    for (const ScenePlane& p : planes) {
        if (modelBounds) break;
        const Vec4& r = p.region;
        if (!(r.z > 0.0f) || !(r.w > 0.0f)) continue;
        Aabb b;
        b.min = Vec3{r.x, r.y, 0.0f};
        b.max = Vec3{r.x + r.z, r.y + r.w, 0.0f};
        add(b, p.light.worldFromLayer);
        for (int c = 0; c < 4; ++c) {
            const Vec3 w = p.light.worldFromLayer.transform_point(Vec3{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y, 0.0f});
            if (std::isfinite(w.y)) planeBottom = std::max(planeBottom, w.y);
        }
    }
    if (!gp.valid) return gp;
    f32 floorY = hi.y;
    if (!cands.empty()) {
        std::sort(cands.begin(), cands.end(), [](const Candidate& x, const Candidate& y) { return x.boxY > y.boxY; });
        f32 exact = -1e30f;
        const f32 boxOnly = hasSkin ? hi.y : -1e30f;
        usize verts = 0;
        for (const Candidate& c : cands) {
            if (c.boxY <= exact || verts > 2000000) break;
            const Mat4& m = c.world;
            const u32 yAxes = static_cast<u32>(m.col[0].y != 0.f) + static_cast<u32>(m.col[1].y != 0.f)
                            + static_cast<u32>(m.col[2].y != 0.f);
            // For one exact axis (including reflection/negative scale), the
            // authored AABB's extremum is attained by an actual vertex. No
            // tolerance: even a tiny rotation/shear retains the vertex scan.
            // Deformed primitives retain their existing path.
            if (yAxes <= 1 && !c.prim->skinned() && c.prim->morphTargets.empty()) {
                exact = std::max(exact, c.boxY);
                continue;
            }
            for (const Vec3& v : c.prim->positions) {
                exact = std::max(exact, m.col[0].y * v.x + m.col[1].y * v.y + m.col[2].y * v.z + m.col[3].y);
            }
            verts += c.prim->positions.size();
            gp.verticesVisited += c.prim->positions.size();
        }
        if (exact > -1e29f) floorY = std::max(exact, boxOnly);
    }
    floorY = std::max(floorY, planeBottom);
    gp.center = Vec3{(lo.x + hi.x) * 0.5f, floorY, (lo.z + hi.z) * 0.5f};
    gp.radius = std::max(1.0f, (hi - lo).length() * 0.5f);
    if (!std::isfinite(gp.radius) || !std::isfinite(gp.center.y)) gp.valid = false;
    return gp;
}

} // namespace aurea::scene3d
