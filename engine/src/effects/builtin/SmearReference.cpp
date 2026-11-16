// =============================================================================
//  Smear reference (aurea.distort.smear_mask_reference) — AE Distort › Smear
//  (ADBE SCHMEAR) reconstructed from REA evidence (batch-c/rea/Smear_New):
//
//  - controls: Source Mask, Boundary Mask, Mask Offset, Mask Rotation, Mask
//    Scale (0.1..2), Percent, Elasticity (9: Stiff … Super Fluid),
//    Interpolation Method (Discrete, Linear, Smooth);
//  - both masks must be set, closed and different, otherwise no distortion;
//  - the moved source: identity, rotate and scale about the MEAN OF THE SOURCE
//    MASK VERTICES, then move that point to Mask Offset.
//
//  The native distortion engine is an AE library outside the plugin; the
//  solver here is Aurea's: a backward displacement grid (moved source pulls
//  from the inverse matrix, outside the boundary is fixed, the rest relaxes
//  harmonically by SOR; Elasticity sets how far the relaxation runs), sampled
//  per pixel by smear_reference.frag with the chosen interpolation.
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/SmearReference.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>

namespace aurea::smear_reference {
namespace {

bool inside(const std::vector<Vec2>& ring, Vec2 p) noexcept {
    bool in = false;
    for (usize i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Vec2 a = ring[i], b = ring[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

struct Placement {
    Vec2 pivot, target;
    f32 c = 1, s = 0, scale = 1;
    Vec2 forward(Vec2 p) const noexcept {
        const Vec2 d = (p - pivot) * scale;
        return target + Vec2{c * d.x - s * d.y, s * d.x + c * d.y};
    }
    Vec2 inverse(Vec2 q) const noexcept {
        const Vec2 d = q - target;
        return pivot + Vec2{c * d.x + s * d.y, -s * d.x + c * d.y} * (1.0f / scale);
    }
};

/// Relaxation sweeps per elasticity step (Stiff … Super Fluid). The AE frames
/// (batch-c/runtime) differ only slightly between Stiff and Super Fluid (mean
/// 0.009 on the checker), so every step runs the field close to convergence.
constexpr u32 kSweeps[kElasticityCount] = {160, 180, 200, 230, 260, 300, 350, 420, 500};

Vec2 bilinear(const std::vector<Vec2>& field, u32 w, u32 h, Vec2 g) noexcept {
    const f32 fx = std::floor(g.x), fy = std::floor(g.y);
    const i32 x0 = static_cast<i32>(fx), y0 = static_cast<i32>(fy);
    const f32 tx = g.x - fx, ty = g.y - fy;
    auto at = [&](i32 x, i32 y) -> Vec2 {
        if (x < 0 || y < 0 || x >= static_cast<i32>(w) || y >= static_cast<i32>(h)) return {};
        return field[static_cast<usize>(y) * w + static_cast<usize>(x)];
    };
    return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}

} // namespace

// The FORWARD field (where each pixel goes) is harmonic between the source
// mask (moved rigidly) and the boundary (fixed); the grid then stores its
// inverse (where each output pixel comes from), found by fixed-point
// iteration. Against the AE frames this halves the error of a harmonic
// backward field on translations (batch-c/models/smear_forward.py).
Grid solve(const Inputs& in, u32 maxNodes) {
    Grid grid;
    if (in.source.size() < 3 || in.boundary.size() < 3 || in.sourceVertices.empty() || !(in.percent > 0.0f))
        return grid;
    Placement m;
    for (Vec2 v : in.sourceVertices) m.pivot += v;
    m.pivot = m.pivot * (1.0f / static_cast<f32>(in.sourceVertices.size()));
    const f32 t = std::clamp(in.percent, 0.0f, 1.0f);
    m.target = m.pivot + (in.offset - m.pivot) * t;
    const f32 angle = in.rotationDegrees * t * kDeg2Rad;
    m.c = std::cos(angle); m.s = std::sin(angle);
    m.scale = std::max(1e-3f, 1.0f + (std::clamp(in.scale, 0.1f, 2.0f) - 1.0f) * t);
    std::vector<Vec2> moved;
    moved.reserve(in.source.size());
    for (Vec2 p : in.source) moved.push_back(m.forward(p));
    Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
    const std::vector<Vec2>* rings[3] = {&in.boundary, &moved, &in.source};
    for (const std::vector<Vec2>* ring : rings)
        for (Vec2 p : *ring) { lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); }
    if (!(hi.x > lo.x) || !(hi.y > lo.y) || !std::isfinite(lo.x + lo.y + hi.x + hi.y)) return grid;
    const u32 nodes = std::clamp<u32>(maxNodes, 8u, 256u);
    grid.cell = std::max(1.0f, std::max(hi.x - lo.x, hi.y - lo.y) / static_cast<f32>(nodes - 3));
    lo = lo - Vec2{grid.cell, grid.cell};
    grid.width = static_cast<u32>(std::ceil((hi.x - lo.x) / grid.cell)) + 2;
    grid.height = static_cast<u32>(std::ceil((hi.y - lo.y) / grid.cell)) + 2;
    grid.area = {lo.x, lo.y, grid.cell * static_cast<f32>(grid.width - 1), grid.cell * static_cast<f32>(grid.height - 1)};
    const usize count = static_cast<usize>(grid.width) * grid.height;
    std::vector<Vec2> forward(count);
    std::vector<u8> free(count, 0);
    for (u32 y = 0; y < grid.height; ++y) for (u32 x = 0; x < grid.width; ++x) {
        const Vec2 q{lo.x + grid.cell * static_cast<f32>(x), lo.y + grid.cell * static_cast<f32>(y)};
        const usize i = static_cast<usize>(y) * grid.width + x;
        if (inside(in.source, q)) forward[i] = m.forward(q) - q;
        else if (inside(in.boundary, q)) free[i] = 1;
    }
    const u32 sweeps = kSweeps[std::min(in.elasticity, kElasticityCount - 1)];
    constexpr f32 kOmega = 1.85f;
    for (u32 sweep = 0; sweep < sweeps; ++sweep) {
        for (u32 y = 1; y + 1 < grid.height; ++y) for (u32 x = 1; x + 1 < grid.width; ++x) {
            const usize i = static_cast<usize>(y) * grid.width + x;
            if (!free[i]) continue;
            const Vec2 avg = (forward[i - 1] + forward[i + 1] + forward[i - grid.width] + forward[i + grid.width]) * 0.25f;
            forward[i] = forward[i] + (avg - forward[i]) * kOmega;
        }
    }
    // Backward map: D(q) = −F(q + D(q)).
    grid.displacement.assign(count, {});
    const f32 inv = 1.0f / grid.cell;
    for (u32 y = 0; y < grid.height; ++y) for (u32 x = 0; x < grid.width; ++x) {
        const Vec2 q{lo.x + grid.cell * static_cast<f32>(x), lo.y + grid.cell * static_cast<f32>(y)};
        Vec2 d{};
        for (u32 k = 0; k < 40; ++k) {
            const Vec2 p = q + d;
            const Vec2 f = bilinear(forward, grid.width, grid.height, Vec2{(p.x - lo.x) * inv, (p.y - lo.y) * inv});
            d = d * 0.5f + f * -0.5f;
        }
        grid.displacement[static_cast<usize>(y) * grid.width + x] = d;
    }
    return grid;
}

namespace {

void flatten_mask(const Mask& mask, f64 frame, Vec2 shift, std::vector<Vec2>& ring, std::vector<Vec2>& vertices) {
    std::vector<MaskPoint> points;
    std::vector<Vec4> edges;
    mask::evaluate_path(mask, frame, points);
    mask::flatten(points, true, 0.25f, {}, edges);
    for (const MaskPoint& p : points) vertices.push_back(p.position + shift);
    for (const Vec4 e : edges) ring.push_back(Vec2{e.x, e.y} + shift);
}

struct Uniforms {
    Vec4 uvMap, inRegion, outRegion, grid, gridSize;
};

class Smear final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kKey, "Mancha", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("source_mask", "Máscara de origem", 0, 0, 32, kParamNone);
        p.add_int("boundary_mask", "Máscara de limite", 0, 0, 32, kParamNone);
        p.add_point2("mask_offset", "Deslocamento da máscara", {.5f, .5f}, -2, 3, kParamAnimatable | kParamRelative);
        p.add_angle("mask_rotation", "Rotação da máscara", 0);
        p.add_float("mask_scale", "Escala da máscara", 1, 0.1f, 2, kParamAnimatable);
        p.add_float("percent", "Porcentagem", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        static const char* const kElastic[] = {"Rígido", "Menos rígido", "Abaixo do normal", "Normal",
                                               "Absolutamente normal", "Acima da média", "Solto", "Líquido", "Superfluido"};
        p.add_enum("elasticity", "Elasticidade", kElastic, kElasticityCount, 0);
        static const char* const kInterp[] = {"Discreto", "Linear", "Suave"};
        p.add_enum("interpolation", "Método de interpolação", kInterp, 3, 1);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kPercent] = ParamValue::scalar(100);
        v[kMaskRotation] = ParamValue::scalar(30);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(e.f(kPercent) > 0.0f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_smear_reference_frag, work));
    }
    static bool masks(const EffectEval& e, Inputs& in) {
        if (!e.layer) return false;
        const auto& list = e.layer->masks;
        const u32 source = static_cast<u32>(std::max(0.0f, std::round(e.f(kSourceMask))));
        const u32 boundary = static_cast<u32>(std::max(0.0f, std::round(e.f(kBoundaryMask))));
        if (!source || !boundary || source == boundary || source > list.size() || boundary > list.size()) return false;
        const Mask& s = list[source - 1];
        const Mask& b = list[boundary - 1];
        if (!s.closed || !b.closed) return false;   // native: "Cannot use an open path"
        const Vec2 shift = e.placement ? e.placement->sourceShift : Vec2{};
        std::vector<Vec2> unused;
        flatten_mask(s, e.time_frames(), shift, in.source, in.sourceVertices);
        flatten_mask(b, e.time_frames(), shift, in.boundary, unused);
        return true;
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = {};
        e.auxInfo = {};
        e.pathSamples.reset();
        if (is_identity(e)) return;
        Inputs in;
        if (!masks(e, in)) return;
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : 1.0f;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : 1.0f;
        const Vec2 shift = e.placement ? e.placement->sourceShift : Vec2{};
        in.offset = Vec2{e.p2(kMaskOffset).x * w, e.p2(kMaskOffset).y * h} + shift;
        in.rotationDegrees = e.f(kMaskRotation);
        in.scale = e.f(kMaskScale);
        in.percent = std::clamp(e.f(kPercent), 0.0f, 100.0f) * 0.01f;
        in.elasticity = std::min(e.e(kElasticity), kElasticityCount - 1);
        Grid grid = solve(in);
        if (grid.empty()) return;
        std::vector<Vec4> texels(grid.displacement.size());
        u64 key = 0x534d454152524546ull;
        for (usize i = 0; i < texels.size(); ++i) {
            texels[i] = Vec4{grid.displacement[i].x, grid.displacement[i].y, 0, 0};
            key = (key ^ std::bit_cast<u32>(texels[i].x)) * 1099511628211ull;
            key = (key ^ std::bit_cast<u32>(texels[i].y)) * 1099511628211ull;
        }
        key = (key ^ grid.width) * 1099511628211ull;
        if (e.resources) e.aux = e.resources->data_texture(key, texels.data(), grid.width, grid.height);
        e.auxInfo = Vec4{static_cast<f32>(grid.width), static_cast<f32>(grid.height), grid.cell, 1};
        e.pathSamples = std::make_shared<const std::vector<Vec4>>(
            std::initializer_list<Vec4>{{grid.area.x, grid.area.y, grid.area.w, grid.area.h}});
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        if (!e.aux.valid() || !e.pathSamples || e.pathSamples->empty() || !input.width || !input.height) {
            out = input;
            return OkStatus;
        }
        const Vec4 area = e.pathSamples->front();
        Uniforms u;
        u.uvMap = EffectBuildContext::uv_map(input.region, input.region);
        u.inRegion = {input.region.x, input.region.y, std::max(input.region.w, 1e-3f), std::max(input.region.h, 1e-3f)};
        u.outRegion = u.inRegion;
        u.grid = {area.x, area.y, e.auxInfo.z, static_cast<f32>(std::min(e.e(kInterpolation), 2u))};
        u.gridSize = {e.auxInfo.x, e.auxInfo.y, 0, 0};
        out = input;
        out.texture = ctx.texture("smear-reference", input.width, input.height);
        if (ctx.fullscreen_pass("smear-reference", PassStage::Effects, out.texture, ShaderId::effects_smear_reference_frag,
                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}, PassTexture{{}, e.aux, CommonSampler::NearestClamp}},
                &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_effect(EffectRegistry& registry) { (void)registry.add(std::make_unique<Smear>()); }

} // namespace aurea::smear_reference
