#include "aurea/scene3d/GroundPlacement.hpp"

namespace aurea::scene3d {

GroundPlacement ground_placement(const SceneFrame& frame, std::span<const GpuModel* const> models) noexcept {
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
    gp.center = Vec3{(lo.x + hi.x) * 0.5f, floorY, (lo.z + hi.z) * 0.5f};
    gp.radius = std::max(1.0f, (hi - lo).length() * 0.5f);
    if (!std::isfinite(gp.radius) || !std::isfinite(gp.center.y)) gp.valid = false;
    return gp;
}

} // namespace aurea::scene3d
