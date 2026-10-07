// =============================================================================
//  Aurea / effects / builtin / MeshWarpEffect.cpp
//
//  Malha de deformação (aurea.distort.mesh_warp), o "Mesh Warp" do After
//  Effects. Implementação própria: a grade de vértices com alças de bezier
//  (aurea/effects/MeshWarp.hpp) vira retalhos de Coons; cada retalho é
//  tesselado na CPU (subdivisões pela Qualidade) e a grade resultante vai para
//  uma textura RGBA32F que o vertex shader lê — triângulos texturizados com a
//  camada, desenhados na região que a malha deformada ocupa.
//
//  Malha de fábrica = nenhum passe (a camada sai idêntica, bit a bit).
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/MeshWarp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace aurea {
namespace mesh_warp {

namespace {
constexpr u32 kPos = 0, kLeft = 2, kRight = 4, kUp = 6, kDown = 8;

inline Vec2 at(const f32* v, u32 cols, u32 r, u32 c, u32 slot) noexcept {
    const f32* p = v + (static_cast<usize>(r) * (cols + 1) + c) * kMeshWarpVertexFloats + slot;
    return Vec2{p[0], p[1]};
}
inline Vec2 bez(Vec2 a, Vec2 b, Vec2 c, Vec2 d, f32 t) noexcept {
    const f32 s = 1.0f - t;
    const f32 k0 = s * s * s, k1 = 3.0f * s * s * t, k2 = 3.0f * s * t * t, k3 = t * t * t;
    return Vec2{k0 * a.x + k1 * b.x + k2 * c.x + k3 * d.x, k0 * a.y + k1 * b.y + k2 * c.y + k3 * d.y};
}
} // namespace

void identity(u32 rows, u32 cols, std::vector<f32>& out) {
    rows = std::clamp(rows, 1u, kMeshWarpMaxDivisions);
    cols = std::clamp(cols, 1u, kMeshWarpMaxDivisions);
    out.assign(value_count(rows, cols), 0.0f);
    const f32 hx = 1.0f / (3.0f * static_cast<f32>(cols)), hy = 1.0f / (3.0f * static_cast<f32>(rows));
    for (u32 r = 0; r <= rows; ++r)
        for (u32 c = 0; c <= cols; ++c) {
            f32* p = out.data() + (static_cast<usize>(r) * (cols + 1) + c) * kMeshWarpVertexFloats;
            p[0] = static_cast<f32>(c) / static_cast<f32>(cols);
            p[1] = static_cast<f32>(r) / static_cast<f32>(rows);
            p[kLeft] = -hx;
            p[kRight] = hx;
            p[kUp + 1] = -hy;
            p[kDown + 1] = hy;
        }
}

bool is_identity(const std::vector<f32>& values, u32 rows, u32 cols) noexcept {
    if (values.size() != value_count(rows, cols)) return true;
    std::vector<f32> id;
    identity(rows, cols, id);
    for (usize i = 0; i < id.size(); ++i)
        if (!(std::fabs(values[i] - id[i]) <= 1e-6f)) return false;
    return true;
}

bool matches(const MeshWarpData& d, u32 rows, u32 cols) noexcept {
    return d.rows == rows && d.cols == cols;
}

i32 key_at(const MeshWarpData& d, i64 frame) noexcept {
    for (usize i = 0; i < d.keys.size(); ++i)
        if (d.keys[i].frame == frame) return static_cast<i32>(i);
    return -1;
}

bool evaluate(const MeshWarpData* d, u32 rows, u32 cols, f64 frame, std::vector<f32>& out) {
    const usize n = value_count(rows, cols);
    auto fallback = [&] { identity(rows, cols, out); return false; };
    if (!d || !matches(*d, rows, cols)) return fallback();
    if (d->keys.empty()) {
        if (d->values.size() != n) return fallback();
        out = d->values;
        return !is_identity(out, rows, cols);
    }
    // Keys ordenados por tempo (o motor mantém; a leitura do projeto também).
    const auto& k = d->keys;
    usize i = 0;
    while (i + 1 < k.size() && static_cast<f64>(k[i + 1].frame) <= frame) ++i;
    const MeshWarpKey& a = k[i];
    if (a.values.size() != n) return fallback();
    if (frame <= static_cast<f64>(k.front().frame) || i + 1 >= k.size() || a.interp == 0) {
        out = (frame <= static_cast<f64>(k.front().frame)) ? k.front().values : a.values;
        if (out.size() != n) return fallback();
        return !is_identity(out, rows, cols);
    }
    const MeshWarpKey& b = k[i + 1];
    if (b.values.size() != n) return fallback();
    const f64 span = static_cast<f64>(b.frame - a.frame);
    f32 t = span > 0.0 ? static_cast<f32>(std::clamp((frame - static_cast<f64>(a.frame)) / span, 0.0, 1.0)) : 1.0f;
    if (a.interp == 2) t = t * t * (3.0f - 2.0f * t);
    out.resize(n);
    for (usize j = 0; j < n; ++j) out[j] = a.values[j] + (b.values[j] - a.values[j]) * t;
    return !is_identity(out, rows, cols);
}

u32 subdivisions(u32 quality, u32 divisions, u32 maxGrid) noexcept {
    quality = std::clamp(quality, 1u, 10u);
    divisions = std::max(divisions, 1u);
    const u32 wanted = quality * 2u;
    const u32 cap = std::max(1u, (maxGrid - 1u) / divisions);
    return std::clamp(wanted, 1u, cap);
}

Vec2 patch_point(const f32* v, u32 cols, u32 r, u32 c, f32 u, f32 w) noexcept {
    const Vec2 p00 = at(v, cols, r, c, kPos), p01 = at(v, cols, r, c + 1, kPos);
    const Vec2 p10 = at(v, cols, r + 1, c, kPos), p11 = at(v, cols, r + 1, c + 1, kPos);
    const Vec2 top = bez(p00, p00 + at(v, cols, r, c, kRight), p01 + at(v, cols, r, c + 1, kLeft), p01, u);
    const Vec2 bot = bez(p10, p10 + at(v, cols, r + 1, c, kRight), p11 + at(v, cols, r + 1, c + 1, kLeft), p11, u);
    const Vec2 lef = bez(p00, p00 + at(v, cols, r, c, kDown), p10 + at(v, cols, r + 1, c, kUp), p10, w);
    const Vec2 rig = bez(p01, p01 + at(v, cols, r, c + 1, kDown), p11 + at(v, cols, r + 1, c + 1, kUp), p11, w);
    const f32 su = 1.0f - u, sw = 1.0f - w;
    // Coons: soma das duas regradas menos a bilinear dos cantos.
    return Vec2{
        sw * top.x + w * bot.x + su * lef.x + u * rig.x
            - (su * sw * p00.x + u * sw * p01.x + su * w * p10.x + u * w * p11.x),
        sw * top.y + w * bot.y + su * lef.y + u * rig.y
            - (su * sw * p00.y + u * sw * p01.y + su * w * p10.y + u * w * p11.y)};
}

void tessellate(const std::vector<f32>& values, u32 rows, u32 cols, u32 subX, u32 subY,
                std::vector<Vec2>& out, u32& gx, u32& gy) {
    out.clear();
    gx = gy = 0;
    if (values.size() != value_count(rows, cols) || subX == 0 || subY == 0) return;
    gx = cols * subX + 1;
    gy = rows * subY + 1;
    out.resize(static_cast<usize>(gx) * gy);
    for (u32 j = 0; j < gy; ++j) {
        const u32 r = std::min(j / subY, rows - 1);
        const f32 w = static_cast<f32>(j - r * subY) / static_cast<f32>(subY);
        for (u32 i = 0; i < gx; ++i) {
            const u32 c = std::min(i / subX, cols - 1);
            const f32 u = static_cast<f32>(i - c * subX) / static_cast<f32>(subX);
            out[static_cast<usize>(j) * gx + i] = patch_point(values.data(), cols, r, c, u, w);
        }
    }
}

bool move_handle(std::vector<f32>& values, u32 rows, u32 cols, u32 vertex, u32 handle, Vec2 p) noexcept {
    if (values.size() != value_count(rows, cols) || vertex >= vertex_count(rows, cols) || handle > 4) return false;
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    // A malha pode sair da camada, mas com teto (projeto e dedo enlouquecidos).
    p.x = std::clamp(p.x, -4.0f, 5.0f);
    p.y = std::clamp(p.y, -4.0f, 5.0f);
    f32* v = values.data() + static_cast<usize>(vertex) * kMeshWarpVertexFloats;
    if (handle == 0) {   // as alças são relativas: andam junto com o vértice
        v[0] = p.x;
        v[1] = p.y;
    } else {
        f32* h = v + handle * 2u;
        h[0] = p.x - v[0];
        h[1] = p.y - v[1];
    }
    return true;
}

} // namespace mesh_warp

namespace builtin {
namespace {

struct MeshWarpUniforms {
    Vec4 region{};      // região de saída (px da camada)
    Vec4 inputRegion{}; // região da entrada
    Vec4 grid{};        // largura, altura da camada (px), pontos por eixo (gx, gy)
};

class MeshWarpEffect final : public Effect {
public:
    enum : u32 { Rows = 0, Columns, Quality };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kMeshWarpKey, "Mesh Warp", "Distorcer", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        // Como no AE: linhas, colunas e qualidade não são animáveis; a malha é.
        p.add_int("rows", "Linhas", 7, 1, static_cast<i32>(kMeshWarpMaxDivisions), kParamNone);
        p.add_int("columns", "Colunas", 7, 1, static_cast<i32>(kMeshWarpMaxDivisions), kParamNone);
        p.add_int("quality", "Qualidade", 8, 1, 10, kParamNone);
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_mesh_warp_vert, ShaderId::effects_mesh_warp_frag, f));
    }

    bool needs_full_input() const noexcept override { return true; }

    bool demo_values(EffectInstance& instance, std::vector<ParamValue>& values) const noexcept override {
        (void)values;
        // A prévia do catálogo: um vértice do meio puxado e as alças tortas.
        MeshWarpData d;
        d.rows = d.cols = 7;
        mesh_warp::identity(7, 7, d.values);
        const u32 v = 4 * 8 + 4;
        (void)mesh_warp::move_handle(d.values, 7, 7, v, 0, Vec2{0.68f, 0.66f});
        (void)mesh_warp::move_handle(d.values, 7, 7, v, 2, Vec2{0.80f, 0.50f});
        (void)mesh_warp::move_handle(d.values, 7, 7, 2 * 8 + 2, 0, Vec2{0.20f, 0.34f});
        instance.meshes.assign(1, std::move(d));
        return true;
    }

    static void grid_of(const EffectEval& e, u32& rows, u32& cols, u32& quality) noexcept {
        rows = static_cast<u32>(std::clamp(e.value(Rows).as_int(), 1, static_cast<i32>(kMeshWarpMaxDivisions)));
        cols = static_cast<u32>(std::clamp(e.value(Columns).as_int(), 1, static_cast<i32>(kMeshWarpMaxDivisions)));
        quality = static_cast<u32>(std::clamp(e.value(Quality).as_int(), 1, 10));
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        if (!e.instance) return false;   // fora do planejamento: decide no build
        if (e.instance->meshes.empty()) return true;
        u32 rows, cols, quality;
        grid_of(e, rows, cols, quality);
        std::vector<f32> v;
        return !mesh_warp::evaluate(&e.instance->meshes.front(), rows, cols, e.time_frames(), v);
    }

    void resolve_resources(EffectEval& e) const noexcept override {
        e.pathSamples.reset();
        e.aux = TextureHandle{};
        if (!e.instance || e.instance->meshes.empty() || !e.placement) return;
        u32 rows, cols, quality;
        grid_of(e, rows, cols, quality);
        std::vector<f32> values;
        if (!mesh_warp::evaluate(&e.instance->meshes.front(), rows, cols, e.time_frames(), values)) return;
        const u32 subX = mesh_warp::subdivisions(quality, cols), subY = mesh_warp::subdivisions(quality, rows);
        std::vector<Vec2> grid;
        u32 gx = 0, gy = 0;
        mesh_warp::tessellate(values, rows, cols, subX, subY, grid, gx, gy);
        if (grid.empty()) return;
        const f32 w = static_cast<f32>(std::max(1u, e.placement->layerWidth));
        const f32 h = static_cast<f32>(std::max(1u, e.placement->layerHeight));
        // [0] = cabeçalho; depois um texel por ponto (px da camada).
        auto samples = std::make_shared<std::vector<Vec4>>();
        samples->reserve(grid.size() + 1);
        samples->push_back(Vec4{static_cast<f32>(gx), static_cast<f32>(gy), w, h});
        u64 key = 1469598103934665603ull ^ 0x4d455348ull;
        for (const Vec2& g : grid) {
            samples->push_back(Vec4{g.x * w, g.y * h, 0.0f, 1.0f});
            u32 bits[2];
            std::memcpy(bits, &samples->back(), sizeof bits);
            key = (key ^ bits[0]) * 1099511628211ull;
            key = (key ^ bits[1]) * 1099511628211ull;
        }
        key = (key ^ gx) * 1099511628211ull;
        key = (key ^ gy) * 1099511628211ull;
        if (e.resources) e.aux = e.resources->data_texture(key, samples->data() + 1, gx, gy);
        e.pathSamples = std::move(samples);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin,
                 LayerImage& out) const override {
        if (!e.pathSamples || e.pathSamples->size() < 5 || !e.aux.valid()) {
            out = in;
            return OkStatus;
        }
        const auto& s = *e.pathSamples;
        const u32 gx = static_cast<u32>(s[0].x), gy = static_cast<u32>(s[0].y);
        if (gx < 2 || gy < 2 || s.size() < static_cast<usize>(gx) * gy + 1) {
            out = in;
            return OkStatus;
        }
        f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (usize i = 1; i < s.size(); ++i) {
            minX = std::min(minX, s[i].x); maxX = std::max(maxX, s[i].x);
            minY = std::min(minY, s[i].y); maxY = std::max(maxY, s[i].y);
        }
        if (!(maxX > minX) || !(maxY > minY)) {
            out = in;
            return OkStatus;
        }
        Rect region = spread_region({minX, minY, maxX - minX, maxY - minY}, 0.0f, 0.0f, e.placement, margin);
        if (region.w <= 0.0f || region.h <= 0.0f) {
            out = in;
            return OkStatus;
        }
        // A saída na MESMA grade de texels da entrada: com a origem fora da
        // grade (a malha cresceu meio texel para cima, por exemplo), a parte que
        // não mudou era reamostrada e a camada inteira saía levemente borrada.
        {
            const f32 tx = in.texel_scale_x(), ty = in.texel_scale_y();
            if (tx > 0.0f && ty > 0.0f) {
                const f32 x0 = in.region.x + std::floor((region.x - in.region.x) * tx + 1e-3f) / tx;
                const f32 y0 = in.region.y + std::floor((region.y - in.region.y) * ty + 1e-3f) / ty;
                const f32 x1 = in.region.x + std::ceil((region.x + region.w - in.region.x) * tx - 1e-3f) / tx;
                const f32 y1 = in.region.y + std::ceil((region.y + region.h - in.region.y) * ty - 1e-3f) / ty;
                region = Rect{x0, y0, std::max(x1 - x0, 1.0f / tx), std::max(y1 - y0, 1.0f / ty)};
            }
        }
        if (region.w <= 0.0f || region.h <= 0.0f) {
            out = in;
            return OkStatus;
        }
        MeshWarpUniforms u;
        u.region = {region.x, region.y, region.w, region.h};
        u.inputRegion = {in.region.x, in.region.y, in.region.w, in.region.h};
        u.grid = {s[0].z, s[0].w, static_cast<f32>(gx), static_cast<f32>(gy)};
        u32 w = 0, h = 0;
        ctx.region_size(region, in.texel_scale_x(), w, h);
        out = {ctx.texture(info().key, w, h), region, w, h};
        if (ctx.geometry_pass(info().key, PassStage::Effects, out.texture, ShaderId::effects_mesh_warp_vert,
                              ShaderId::effects_mesh_warp_frag,
                              {PassTexture{in.texture, {}, CommonSampler::LinearBorder},
                               PassTexture{{}, e.aux, CommonSampler::NearestClamp}},
                              &u, sizeof(u), (gx - 1) * (gy - 1) * 6, false, false) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_mesh_warp_effect(EffectRegistry& r) { (void)r.add(std::make_unique<MeshWarpEffect>()); }

} // namespace builtin
} // namespace aurea
