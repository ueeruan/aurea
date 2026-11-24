// =============================================================================
//  Aurea / scene3d / Explode3D.cpp — efeito "Explosão" (ver Explode3D.hpp).
//
//  Fratura: subdivisão uniforme (1→4, atributos interpolados) até haver
//  triângulos para as peças, Voronoi dos centróides dos triângulos com
//  sementes sorteadas (estratificadas) pela área e duas relaxações de Lloyd,
//  num grid de sementes. Cada célula vira uma peça: a superfície original
//  (materiais, UV, normais, cores) + a tampa que fecha a peça até um ápice
//  para dentro (a espessura), num material de interior escuro com um degradê
//  emissivo em UV1 (quente na borda da quebra).
//
//  Voo (fechado, sem estado): centro da peça C, ponto de impacto P, onda pela
//  distância d = |C−P|/alcance: q = clamp((p − w·d)/(1 − w)). Arrasto k:
//  f(q) = (1 − e^(−k q))/k. Deslocamento = dir·v·f(q) + ½·g·q²; giro em torno
//  de um eixo sorteado ∝ f(q); escala 1 → fim por q^1,5; esmaecer no fim.
// =============================================================================
#include "aurea/scene3d/Explode3D.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/platform/DevicePolicy.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace aurea::scene3d::explode3d {
namespace {

constexpr const char* kMarker = "\x1F" "aurea.explode3d";
constexpr f32 kTau = 6.28318530718f;
/// Teto de triângulos de uma malha depois da subdivisão (por nó).
constexpr u32 kNodeTriangleBudget = 240000;

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

u64 mix64(u64 x) noexcept {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}
/// Sorteio determinístico em [0, 1).
f32 hash01(u64 a, u64 b, u64 c) noexcept {
    const u64 h = mix64(mix64(a * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) ^ mix64(b + 0xD6E8FEB86659FD93ull)
                        ^ (c * 0xA0761D6478BD642Full + 7));
    return static_cast<f32>(h >> 40) / static_cast<f32>(1ull << 24);
}
Vec3 random_unit(u64 a, u64 b, u64 c) noexcept {
    const f32 z = 2.0f * hash01(a, b, c) - 1.0f;
    const f32 t = kTau * hash01(a, b, c + 101);
    const f32 r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return Vec3{r * std::cos(t), r * std::sin(t), z};
}
f32 smooth(f32 a, f32 b, f32 x) noexcept {
    const f32 t = std::clamp((x - a) / std::max(b - a, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
Vec3 safe_normalize(Vec3 v, Vec3 fallback) noexcept {
    const f32 l = v.length();
    return l > 1e-8f && std::isfinite(l) ? v / l : fallback;
}

/// Inversa da parte linear (3×3) de `m` aplicada a `v`. Falso = singular.
bool inverse_linear_apply(const Mat4& m, Vec3 v, Vec3& out) noexcept {
    const Vec3 c0{m.col[0].x, m.col[0].y, m.col[0].z};
    const Vec3 c1{m.col[1].x, m.col[1].y, m.col[1].z};
    const Vec3 c2{m.col[2].x, m.col[2].y, m.col[2].z};
    const f32 det = c0.dot(c1.cross(c2));
    if (!(std::fabs(det) > 1e-20f) || !std::isfinite(det)) return false;
    // Linhas da inversa = produtos vetoriais das colunas / det.
    const Vec3 r0 = c1.cross(c2) / det, r1 = c2.cross(c0) / det, r2 = c0.cross(c1) / det;
    out = Vec3{r0.dot(v), r1.dot(v), r2.dot(v)};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

std::atomic<u8> gTier{static_cast<u8>(DeviceTier::Ultra)};

// --- Registro dos assets fraturados (o asset → os dados das peças) ----------
std::mutex gRegistryMutex;
std::unordered_map<const SceneAsset*, const Fractured*>& registry() {
    static auto* m = new std::unordered_map<const SceneAsset*, const Fractured*>();
    return *m;
}

struct Holder {
    SceneAsset asset;
    Fractured info;
    std::shared_ptr<const SceneAsset> source;   ///< as imagens são compartilhadas com o original
    Holder() = default;
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    ~Holder() {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        registry().erase(&asset);
    }
};

// --- Malha de trabalho por primitiva -----------------------------------------
struct Soup {
    std::vector<Vec3> pos, nrm;
    std::vector<Vec4> tan;
    std::vector<Vec2> uv0, uv1;
    std::vector<u32> col;
    std::vector<u32> idx;
    i32 material = -1;
    bool outline = false;   ///< casco do contorno Cartoon: vai junto, sem tampa
    bool hasN = false, hasT = false, hasUv0 = false, hasUv1 = false, hasCol = false;
    [[nodiscard]] u32 tris() const noexcept { return static_cast<u32>(idx.size() / 3); }
};

u32 avg_color(u32 a, u32 b) noexcept {
    u32 out = 0;
    for (u32 s = 0; s < 32; s += 8) out |= ((((a >> s) & 0xFFu) + ((b >> s) & 0xFFu) + 1u) / 2u) << s;
    return out;
}

/// 1 → 4 em todos os triângulos; os pontos médios são compartilhados (a malha
/// continua fechada, inclusive entre primitivas: (a+b)/2 é exato e simétrico).
void subdivide(Soup& s) {
    std::unordered_map<u64, u32> mid;
    mid.reserve(s.idx.size());
    auto midpoint = [&](u32 a, u32 b) -> u32 {
        const u64 key = (static_cast<u64>(std::min(a, b)) << 32) | std::max(a, b);
        if (auto it = mid.find(key); it != mid.end()) return it->second;
        const u32 v = static_cast<u32>(s.pos.size());
        s.pos.push_back((s.pos[a] + s.pos[b]) * 0.5f);
        if (s.hasN) s.nrm.push_back(safe_normalize(s.nrm[a] + s.nrm[b], s.nrm[a]));
        if (s.hasT) {
            const Vec3 ta{s.tan[a].x, s.tan[a].y, s.tan[a].z}, tb{s.tan[b].x, s.tan[b].y, s.tan[b].z};
            s.tan.push_back(Vec4{safe_normalize(ta + tb, ta), s.tan[a].w});
        }
        if (s.hasUv0) s.uv0.push_back((s.uv0[a] + s.uv0[b]) * 0.5f);
        if (s.hasUv1) s.uv1.push_back((s.uv1[a] + s.uv1[b]) * 0.5f);
        if (s.hasCol) s.col.push_back(avg_color(s.col[a], s.col[b]));
        mid.emplace(key, v);
        return v;
    };
    std::vector<u32> out;
    out.reserve(s.idx.size() * 4);
    for (usize t = 0; t + 2 < s.idx.size(); t += 3) {
        const u32 a = s.idx[t], b = s.idx[t + 1], c = s.idx[t + 2];
        const u32 ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
        for (const u32 v : {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca}) out.push_back(v);
    }
    s.idx.swap(out);
}

/// Vizinho mais próximo entre as sementes, num grid uniforme.
struct SeedGrid {
    Vec3 lo{};
    f32 cell = 1.0f;
    i32 nx = 1, ny = 1, nz = 1;
    std::vector<u32> start, items;
    const std::vector<Vec3>* seeds = nullptr;

    void build(const std::vector<Vec3>& s, Vec3 bmin, Vec3 bmax) {
        seeds = &s;
        lo = bmin;
        const Vec3 ext = bmax - bmin;
        const f32 longest = std::max({ext.x, ext.y, ext.z, 1e-6f});
        cell = std::max(longest / std::cbrt(static_cast<f32>(std::max<usize>(1, s.size()))), longest * 1e-4f);
        nx = std::clamp(static_cast<i32>(std::ceil(ext.x / cell)), 1, 128);
        ny = std::clamp(static_cast<i32>(std::ceil(ext.y / cell)), 1, 128);
        nz = std::clamp(static_cast<i32>(std::ceil(ext.z / cell)), 1, 128);
        const usize cells = static_cast<usize>(nx) * static_cast<usize>(ny) * static_cast<usize>(nz);
        start.assign(cells + 1, 0);
        std::vector<u32> owner(s.size());
        for (usize i = 0; i < s.size(); ++i) { owner[i] = cell_of(s[i]); ++start[owner[i] + 1]; }
        for (usize c = 0; c < cells; ++c) start[c + 1] += start[c];
        items.assign(s.size(), 0);
        std::vector<u32> fill(start.begin(), start.end() - 1);
        for (usize i = 0; i < s.size(); ++i) items[fill[owner[i]]++] = static_cast<u32>(i);
    }
    void coords(Vec3 p, i32& x, i32& y, i32& z) const noexcept {
        x = std::clamp(static_cast<i32>(std::floor((p.x - lo.x) / cell)), 0, nx - 1);
        y = std::clamp(static_cast<i32>(std::floor((p.y - lo.y) / cell)), 0, ny - 1);
        z = std::clamp(static_cast<i32>(std::floor((p.z - lo.z) / cell)), 0, nz - 1);
    }
    [[nodiscard]] u32 cell_of(Vec3 p) const noexcept {
        i32 x, y, z;
        coords(p, x, y, z);
        return static_cast<u32>((z * ny + y) * nx + x);
    }
    [[nodiscard]] u32 nearest(Vec3 p) const noexcept {
        i32 cx, cy, cz;
        coords(p, cx, cy, cz);
        f32 best = 1e38f;
        u32 bestIndex = 0;
        bool found = false;
        const i32 maxR = std::max({nx, ny, nz});
        for (i32 r = 0; r <= maxR; ++r) {
            for (i32 z = std::max(0, cz - r); z <= std::min(nz - 1, cz + r); ++z) {
                for (i32 y = std::max(0, cy - r); y <= std::min(ny - 1, cy + r); ++y) {
                    for (i32 x = std::max(0, cx - r); x <= std::min(nx - 1, cx + r); ++x) {
                        if (std::max({std::abs(x - cx), std::abs(y - cy), std::abs(z - cz)}) != r) continue;
                        const u32 c = static_cast<u32>((z * ny + y) * nx + x);
                        for (u32 k = start[c]; k < start[c + 1]; ++k) {
                            const u32 i = items[k];
                            const f32 d = ((*seeds)[i] - p).length_sq();
                            if (d < best || (d == best && i < bestIndex)) { best = d; bestIndex = i; found = true; }
                        }
                    }
                }
            }
            const f32 reach = static_cast<f32>(r) * cell;
            if (found && reach * reach >= best) break;
        }
        return bestIndex;
    }
};

struct WeldKey {
    i64 x, y, z;
    bool operator==(const WeldKey&) const noexcept = default;
};
struct WeldHash {
    usize operator()(const WeldKey& k) const noexcept {
        return static_cast<usize>(mix64(static_cast<u64>(k.x) * 73856093ull ^ static_cast<u64>(k.y) * 19349663ull
                                        ^ static_cast<u64>(k.z) * 83492791ull));
    }
};

struct NodeWork {
    u32 node = 0;
    bool baked = false;      ///< skin: geometria já na pose de repouso, no espaço da cena
    std::vector<Soup> soups;
    f32 area = 0.0f;
    u32 pieces = 1;
};

/// Degradê do brilho da quebra (emissivo, sRGB): 1 na borda (u = 0) → 0 no ápice.
Image glow_ramp() {
    Image img;
    img.name = "explode3d-glow";
    img.width = 64;
    img.height = 1;
    auto px = std::make_shared<std::vector<u8>>(64u * 4u);
    for (u32 i = 0; i < 64; ++i) {
        const f32 u = static_cast<f32>(i) / 63.0f;
        const f32 lin = std::exp(-7.0f * u) * (1.0f - u * 0.15f);
        const u8 b = static_cast<u8>(std::lround(std::clamp(Color::linear_to_srgb(std::clamp(lin, 0.0f, 1.0f)), 0.0f, 1.0f) * 255.0f));
        (*px)[i * 4 + 0] = b; (*px)[i * 4 + 1] = b; (*px)[i * 4 + 2] = b; (*px)[i * 4 + 3] = 255;
    }
    img.sharedRgba = std::move(px);
    return img;
}

class Builder {
public:
    Builder(const std::shared_ptr<const SceneAsset>& src, const FractureSpec& spec) : src_(*src), srcPtr_(src), spec_(spec) {}

    std::shared_ptr<Holder> run(u64 sourceKey) {
        if (src_.volumetricCloud || src_.nodes.empty() || src_.meshes.empty()) return nullptr;
        rest_ = src_.rest_world_matrices();
        gather();
        if (work_.empty()) return nullptr;
        allocate();
        holder_ = std::make_shared<Holder>();
        skeleton();
        for (NodeWork& w : work_) fracture_node(w);
        if (info().fragments == 0) return nullptr;
        finish(sourceKey);
        return holder_;
    }

private:
    const SceneAsset& src_;
    std::shared_ptr<const SceneAsset> srcPtr_;
    FractureSpec spec_;
    std::vector<Mat4> rest_;
    std::vector<NodeWork> work_;
    std::shared_ptr<Holder> holder_;
    std::unordered_map<i32, u32> interior_;
    i32 rampImage_ = -1, rampSampler_ = -1;

    SceneAsset& out() noexcept { return holder_->asset; }
    Fractured& info() noexcept { return holder_->info; }

    void gather() {
        for (u32 n = 0; n < src_.nodes.size(); ++n) {
            const Node& node = src_.nodes[n];
            if (!node.inScene || node.mesh < 0 || static_cast<usize>(node.mesh) >= src_.meshes.size()) continue;
            const Mesh& mesh = src_.meshes[static_cast<usize>(node.mesh)];
            NodeWork w;
            w.node = n;
            w.baked = node.skin >= 0 && static_cast<usize>(node.skin) < src_.skins.size();
            std::vector<Mat4> joints;
            if (w.baked) {
                const Skin& skin = src_.skins[static_cast<usize>(node.skin)];
                joints.resize(skin.joints.size(), Mat4::identity());
                for (usize j = 0; j < skin.joints.size(); ++j) {
                    const i32 jn = skin.joints[j];
                    const Mat4 jw = jn >= 0 && static_cast<usize>(jn) < rest_.size() ? rest_[static_cast<usize>(jn)] : Mat4::identity();
                    joints[j] = j < skin.inverseBind.size() ? jw * skin.inverseBind[j] : jw;
                }
            }
            for (const Primitive& prim : mesh.primitives) {
                const usize vc = prim.positions.size();
                if (vc == 0 || prim.indices.size() < 3) continue;
                Soup s;
                s.material = node.material_for(prim);
                s.outline = s.material >= 0 && static_cast<usize>(s.material) < src_.materials.size()
                         && src_.materials[static_cast<usize>(s.material)].toon == 2;
                s.pos = prim.positions;
                s.hasN = prim.normals.size() == vc;
                s.hasT = prim.tangents.size() == vc;
                s.hasUv0 = prim.uv0.size() == vc;
                s.hasUv1 = prim.uv1.size() == vc;
                s.hasCol = prim.colors.size() == vc;
                if (s.hasN) s.nrm = prim.normals;
                if (s.hasT) s.tan = prim.tangents;
                if (s.hasUv0) s.uv0 = prim.uv0;
                if (s.hasUv1) s.uv1 = prim.uv1;
                if (s.hasCol) s.col = prim.colors;
                // Skin: a pose de repouso (juntas × inversa de bind) já na cena.
                if (w.baked && prim.joints.size() >= vc * 4 && prim.weights.size() >= vc && !joints.empty()) {
                    for (usize v = 0; v < vc; ++v) {
                        Vec3 p{}, nn{}, tt{};
                        const Vec4 wt = prim.weights[v];
                        const f32 ws[4] = {wt.x, wt.y, wt.z, wt.w};
                        f32 sum = 0.0f;
                        for (u32 k = 0; k < 4; ++k) {
                            const u32 j = prim.joints[v * 4 + k];
                            if (!(ws[k] > 0.0f) || j >= joints.size()) continue;
                            const Mat4& m = joints[j];
                            p += m.transform_point(prim.positions[v]) * ws[k];
                            if (s.hasN) {
                                const Vec4 r = m * Vec4{prim.normals[v], 0.0f};
                                nn += Vec3{r.x, r.y, r.z} * ws[k];
                            }
                            if (s.hasT) {
                                const Vec4 r = m * Vec4{prim.tangents[v].x, prim.tangents[v].y, prim.tangents[v].z, 0.0f};
                                tt += Vec3{r.x, r.y, r.z} * ws[k];
                            }
                            sum += ws[k];
                        }
                        if (sum > 1e-6f) {
                            s.pos[v] = p / sum;
                            if (s.hasN) s.nrm[v] = safe_normalize(nn, prim.normals[v]);
                            if (s.hasT) s.tan[v] = Vec4{safe_normalize(tt, Vec3{1, 0, 0}), prim.tangents[v].w};
                        }
                    }
                }
                s.idx.reserve(prim.indices.size());
                for (usize t = 0; t + 2 < prim.indices.size(); t += 3) {
                    const u32 a = prim.indices[t], b = prim.indices[t + 1], c = prim.indices[t + 2];
                    if (a >= vc || b >= vc || c >= vc) continue;
                    s.idx.push_back(a); s.idx.push_back(b); s.idx.push_back(c);
                }
                if (s.idx.empty()) continue;
                w.soups.push_back(std::move(s));
            }
            if (w.soups.empty()) continue;
            // Área no espaço da cena (a escala do nó entra pela média do determinante).
            f32 k = 1.0f;
            if (!w.baked && n < rest_.size()) {
                const Mat4& m = rest_[n];
                const Vec3 c0{m.col[0].x, m.col[0].y, m.col[0].z}, c1{m.col[1].x, m.col[1].y, m.col[1].z},
                           c2{m.col[2].x, m.col[2].y, m.col[2].z};
                const f32 det = std::fabs(c0.dot(c1.cross(c2)));
                k = det > 0.0f && std::isfinite(det) ? std::pow(det, 2.0f / 3.0f) : 1.0f;
            }
            for (const Soup& s : w.soups) {
                for (usize t = 0; t + 2 < s.idx.size(); t += 3) {
                    const Vec3 a = s.pos[s.idx[t]], b = s.pos[s.idx[t + 1]], c = s.pos[s.idx[t + 2]];
                    w.area += 0.5f * (b - a).cross(c - a).length() * k;
                }
            }
            if (!std::isfinite(w.area)) w.area = 0.0f;
            work_.push_back(std::move(w));
        }
    }

    /// Peças por nó: uma no mínimo, o resto pela área (maiores restos).
    void allocate() {
        const u32 total = std::max(1u, spec_.pieces);
        const u32 nodes = static_cast<u32>(work_.size());
        for (NodeWork& w : work_) w.pieces = 1;
        if (nodes >= total) return;
        f64 sum = 0.0;
        for (const NodeWork& w : work_) sum += w.area;
        const u32 extra = total - nodes;
        std::vector<std::pair<f64, u32>> rest;
        u32 given = 0;
        for (u32 i = 0; i < nodes; ++i) {
            const f64 share = sum > 0.0 ? static_cast<f64>(extra) * work_[i].area / sum : static_cast<f64>(extra) / nodes;
            const u32 whole = static_cast<u32>(std::floor(share));
            work_[i].pieces += whole;
            given += whole;
            rest.push_back({share - whole, i});
        }
        std::stable_sort(rest.begin(), rest.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (u32 i = 0; given < extra && i < rest.size(); ++i, ++given) ++work_[rest[i].second].pieces;
    }

    /// O asset derivado: os nós/materiais do original (malhas vazias) e as imagens compartilhadas.
    void skeleton() {
        SceneAsset& a = out();
        a.version = src_.version;
        a.sourceName = kMarker;
        a.textGlyphLayout = src_.textGlyphLayout;
        a.shapeParts = src_.shapeParts;
        a.textUnits = src_.textUnits;
        a.textWords = src_.textWords;
        a.textLines = src_.textLines;
        a.textLogicalUnits = src_.textLogicalUnits;
        a.textChars = src_.textChars;
        a.nodes = src_.nodes;
        a.roots = src_.roots;
        a.meshes.reserve(src_.meshes.size() + spec_.pieces);
        for (const Mesh& m : src_.meshes) {
            Mesh e;
            e.name = m.name;
            e.morphWeights = m.morphWeights;
            e.bounds = m.bounds;
            a.meshes.push_back(std::move(e));
        }
        a.materials = src_.materials;
        a.images.reserve(src_.images.size() + 1);
        for (const Image& im : src_.images) {
            Image c;
            c.name = im.name;
            c.uri = im.uri;
            c.width = im.width;
            c.height = im.height;
            c.hasAlpha = im.hasAlpha;
            // Os pixels ficam no original: a fratura não copia as texturas.
            if (im.sharedRgba) c.sharedRgba = im.sharedRgba;
            else if (!im.rgba.empty()) c.sharedRgba = std::shared_ptr<const std::vector<u8>>(srcPtr_, &im.rgba);
            a.images.push_back(std::move(c));
        }
        a.samplers = src_.samplers;
        a.cameras = src_.cameras;
        a.lights = src_.lights;
        a.skins = src_.skins;
        a.animations = src_.animations;
        a.bounds = src_.bounds;
        rampImage_ = static_cast<i32>(a.images.size());
        a.images.push_back(glow_ramp());
        Sampler clamp;
        clamp.wrapS = clamp.wrapT = Wrap::Clamp;
        clamp.mipmaps = false;
        rampSampler_ = static_cast<i32>(a.samplers.size());
        a.samplers.push_back(clamp);
        info().asset = &a;
        info().firstFragment = static_cast<u32>(a.nodes.size());
        info().baseMaterials = static_cast<u32>(a.materials.size());
        holder_->source = srcPtr_;
    }

    /// Material de interior do material `source` (−1 = padrão), criado na primeira vez.
    u32 interior_for(i32 source) {
        if (auto it = interior_.find(source); it != interior_.end()) return it->second;
        Material m;
        if (source >= 0 && static_cast<usize>(source) < src_.materials.size()) {
            m = src_.materials[static_cast<usize>(source)];
        } else {
            m.baseColor = Vec4{0.8f, 0.8f, 0.8f, 1.0f};
            m.metallic = 0.0f;
            m.roughness = 0.8f;
        }
        m.name += " (interior)";
        m.normalTex = TextureRef{};
        m.occlusionTex = TextureRef{};
        m.emissiveTex = TextureRef{};
        m.emissiveTex.image = rampImage_;
        m.emissiveTex.sampler = rampSampler_;
        m.emissiveTex.texCoord = 1;
        m.emissive = Vec3{0.0f, 0.0f, 0.0f};
        m.emissiveStrength = 1.0f;
        m.baseColor = Vec4{m.baseColor.x * 0.4f, m.baseColor.y * 0.4f, m.baseColor.z * 0.4f, m.baseColor.w};
        m.metallic *= 0.5f;
        m.roughness = std::max(m.roughness, 0.55f);
        m.clearcoat = 0.0f;
        m.transmission = 0.0f;
        m.doubleSided = true;
        m.toon = 0;
        m.toonParam = 0.0f;
        const u32 index = static_cast<u32>(out().materials.size());
        out().materials.push_back(std::move(m));
        info().interiorOf.push_back(source);
        interior_.emplace(source, index);
        return index;
    }

    void fracture_node(NodeWork& w) {
        // Subdivide até haver ~6 triângulos por peça (todas as primitivas juntas,
        // para não abrir fendas entre elas).
        u32 tris = 0;
        for (const Soup& s : w.soups) tris += s.tris();
        for (u32 level = 0; level < 7 && tris < w.pieces * 6u && tris * 4u <= kNodeTriangleBudget; ++level) {
            for (Soup& s : w.soups) subdivide(s);
            tris *= 4u;
        }
        // Triângulos: centróide, área, normal (área·n).
        struct Tri { u32 soup, first; Vec3 centroid, areaNormal; f32 area; };
        std::vector<Tri> list;
        list.reserve(tris);
        Aabb box;
        for (u32 si = 0; si < w.soups.size(); ++si) {
            const Soup& s = w.soups[si];
            for (u32 t = 0; t + 2 < s.idx.size(); t += 3) {
                const Vec3 a = s.pos[s.idx[t]], b = s.pos[s.idx[t + 1]], c = s.pos[s.idx[t + 2]];
                const Vec3 an = (b - a).cross(c - a) * 0.5f;
                Tri tri{si, t, (a + b + c) / 3.0f, an, an.length()};
                if (!std::isfinite(tri.area)) tri.area = 0.0f;
                box.add(a); box.add(b); box.add(c);
                list.push_back(tri);
            }
        }
        if (list.empty() || !box.valid()) return;
        const u32 k = std::max(1u, std::min<u32>(w.pieces, static_cast<u32>(list.size())));
        // Sementes estratificadas pela área (semente do efeito + nó).
        std::vector<f64> cumulative(list.size());
        f64 acc = 0.0;
        for (usize i = 0; i < list.size(); ++i) { acc += std::max(list[i].area, 1e-12f); cumulative[i] = acc; }
        std::vector<Vec3> seeds(k);
        for (u32 i = 0; i < k; ++i) {
            const f64 u = (static_cast<f64>(i) + hash01(spec_.seed, w.node, i)) / static_cast<f64>(k) * acc;
            const usize at = static_cast<usize>(std::lower_bound(cumulative.begin(), cumulative.end(), u) - cumulative.begin());
            seeds[i] = list[std::min(at, list.size() - 1)].centroid;
        }
        std::vector<u32> owner(list.size(), 0);
        SeedGrid grid;
        for (u32 iter = 0; iter < 3; ++iter) {
            Aabb seedBox = box;
            for (const Vec3& s : seeds) seedBox.add(s);
            grid.build(seeds, seedBox.min, seedBox.max);
            for (usize i = 0; i < list.size(); ++i) owner[i] = grid.nearest(list[i].centroid);
            if (iter == 2) break;
            // Lloyd: a semente vai para o centróide (pela área) da célula.
            std::vector<Vec3> sum(k, Vec3{});
            std::vector<f64> weight(k, 0.0);
            for (usize i = 0; i < list.size(); ++i) {
                const f32 a = std::max(list[i].area, 1e-12f);
                sum[owner[i]] += list[i].centroid * a;
                weight[owner[i]] += a;
            }
            for (u32 i = 0; i < k; ++i)
                if (weight[i] > 0.0) seeds[i] = sum[i] / static_cast<f32>(weight[i]);
        }
        // Triângulos por célula (ordem estável: por primitiva, depois índice).
        std::vector<u32> start(k + 1, 0), order(list.size());
        for (const u32 o : owner) ++start[o + 1];
        for (u32 i = 0; i < k; ++i) start[i + 1] += start[i];
        {
            std::vector<u32> fill(start.begin(), start.end() - 1);
            for (u32 i = 0; i < list.size(); ++i) order[fill[owner[i]]++] = i;
        }
        // Solda por posição (as tampas precisam das arestas da borda da peça).
        const Vec3 ext = box.extent();
        const f32 eps = std::max({ext.x, ext.y, ext.z, 1e-6f}) * 1e-5f;
        std::vector<std::vector<u32>> weld(w.soups.size());
        {
            std::unordered_map<WeldKey, u32, WeldHash> ids;
            for (u32 si = 0; si < w.soups.size(); ++si) {
                const Soup& s = w.soups[si];
                weld[si].resize(s.pos.size());
                for (usize v = 0; v < s.pos.size(); ++v) {
                    const WeldKey key{std::llround(s.pos[v].x / eps), std::llround(s.pos[v].y / eps), std::llround(s.pos[v].z / eps)};
                    auto [it, added] = ids.emplace(key, static_cast<u32>(ids.size()));
                    (void)added;
                    weld[si][v] = it->second;
                }
            }
        }
        std::vector<std::vector<u32>> stamp(w.soups.size()), remap(w.soups.size());
        for (u32 si = 0; si < w.soups.size(); ++si) {
            stamp[si].assign(w.soups[si].pos.size(), ~0u);
            remap[si].assign(w.soups[si].pos.size(), 0);
        }
        for (u32 c = 0; c < k; ++c) {
            if (start[c] == start[c + 1]) continue;
            build_piece(w, list.data(), order.data() + start[c], start[c + 1] - start[c], c, weld, stamp, remap, ext);
        }
    }

    template <typename TriT>
    void build_piece(const NodeWork& w, const TriT* list, const u32* tris, u32 count, u32 cell,
                     const std::vector<std::vector<u32>>& weld, std::vector<std::vector<u32>>& stamp,
                     std::vector<std::vector<u32>>& remap, Vec3 nodeExtent) {
        // Centro (pela área), normal média da superfície e raio.
        Vec3 g{}, nsum{};
        f64 area = 0.0, surfaceArea = 0.0;
        for (u32 i = 0; i < count; ++i) {
            const TriT& t = list[tris[i]];
            g += t.centroid * std::max(t.area, 1e-12f);
            area += std::max(t.area, 1e-12f);
            if (!w.soups[t.soup].outline) { nsum += t.areaNormal; surfaceArea += t.area; }
        }
        g = g / static_cast<f32>(area);
        const Vec3 nAvg = surfaceArea > 0.0 ? nsum / static_cast<f32>(surfaceArea) : Vec3{};
        f32 radius = 0.0f;
        for (u32 i = 0; i < count; ++i) {
            const TriT& t = list[tris[i]];
            const Soup& s = w.soups[t.soup];
            for (u32 j = 0; j < 3; ++j) radius = std::max(radius, (s.pos[s.idx[t.first + j]] - g).length());
        }
        const u32 tag = static_cast<u32>(info().fragments);
        Mesh mesh;
        mesh.name = "explode3d";
        // Superfície: a parte original, uma primitiva por primitiva de origem.
        for (u32 i = 0; i < count;) {
            const u32 si = list[tris[i]].soup;
            const Soup& s = w.soups[si];
            Primitive p;
            p.material = s.material;
            for (; i < count && list[tris[i]].soup == si; ++i) {
                const TriT& t = list[tris[i]];
                for (u32 j = 0; j < 3; ++j) {
                    const u32 v = s.idx[t.first + j];
                    if (stamp[si][v] != tag) {
                        stamp[si][v] = tag;
                        remap[si][v] = static_cast<u32>(p.positions.size());
                        p.positions.push_back(s.pos[v] - g);
                        if (s.hasN) p.normals.push_back(s.nrm[v]);
                        if (s.hasT) p.tangents.push_back(s.tan[v]);
                        if (s.hasUv0) p.uv0.push_back(s.uv0[v]);
                        if (s.hasUv1) p.uv1.push_back(s.uv1[v]);
                        if (s.hasCol) p.colors.push_back(s.col[v]);
                        p.bounds.add(s.pos[v] - g);
                    }
                    p.indices.push_back(remap[si][v]);
                }
            }
            mesh.bounds.add(p.bounds);
            mesh.primitives.push_back(std::move(p));
        }
        // Tampa: as arestas da borda da peça (usadas por um triângulo só) até o ápice.
        if (spec_.thickness > 0.0f) {
            struct Edge { u32 count, soup, from, to; };
            std::vector<Edge> edges;
            std::unordered_map<u64, u32> find;
            edges.reserve(count * 3);
            find.reserve(count * 3);
            for (u32 i = 0; i < count; ++i) {
                const TriT& t = list[tris[i]];
                if (w.soups[t.soup].outline) continue;
                const Soup& s = w.soups[t.soup];
                for (u32 j = 0; j < 3; ++j) {
                    const u32 a = s.idx[t.first + j], b = s.idx[t.first + (j + 1) % 3];
                    const u32 wa = weld[t.soup][a], wb = weld[t.soup][b];
                    if (wa == wb) continue;
                    const u64 key = (static_cast<u64>(std::min(wa, wb)) << 32) | std::max(wa, wb);
                    auto [it, added] = find.emplace(key, static_cast<u32>(edges.size()));
                    if (added) edges.push_back(Edge{1, t.soup, a, b});
                    else ++edges[it->second].count;
                }
            }
            const f32 nLen = nAvg.length();
            Vec3 apex = g;
            if (nLen > 1e-4f) {
                const Vec3 n = nAvg / nLen;
                const f32 support = std::fabs(n.x) * nodeExtent.x + std::fabs(n.y) * nodeExtent.y + std::fabs(n.z) * nodeExtent.z;
                apex = g - n * (spec_.thickness * std::min(radius, 0.5f * support) * std::min(1.0f, nLen));
            }
            Vec2 apexUv{};
            u32 boundary = 0;
            for (const Edge& e : edges) {
                if (e.count != 1) continue;
                const Soup& s = w.soups[e.soup];
                if (s.hasUv0) apexUv = apexUv + s.uv0[e.from] + s.uv0[e.to];
                ++boundary;
            }
            if (boundary) apexUv = apexUv * (0.5f / static_cast<f32>(boundary));
            std::unordered_map<u32, u32> capOf;   // material de interior → primitiva
            for (const Edge& e : edges) {
                if (e.count != 1) continue;
                const Soup& s = w.soups[e.soup];
                const u32 mat = interior_for(s.material);
                u32 slot;
                if (auto it = capOf.find(mat); it != capOf.end()) slot = it->second;
                else {
                    slot = static_cast<u32>(mesh.primitives.size());
                    capOf.emplace(mat, slot);
                    Primitive cap;
                    cap.material = static_cast<i32>(mat);
                    mesh.primitives.push_back(std::move(cap));
                }
                Primitive& cap = mesh.primitives[slot];
                // Triângulo (fim, começo, ápice): o sentido da superfície continua.
                const Vec3 pa = s.pos[e.to] - g, pb = s.pos[e.from] - g, pc = apex - g;
                const Vec3 n = safe_normalize((pb - pa).cross(pc - pa), Vec3{0, 0, 1});
                const Vec3 tangent = safe_normalize(pb - pa, Vec3{1, 0, 0});
                const u32 base = static_cast<u32>(cap.positions.size());
                const Vec3 pts[3] = {pa, pb, pc};
                const Vec2 uvs[3] = {s.hasUv0 ? s.uv0[e.to] : Vec2{}, s.hasUv0 ? s.uv0[e.from] : Vec2{}, apexUv};
                const Vec2 ramp[3] = {Vec2{0.0f, 0.5f}, Vec2{0.0f, 0.5f}, Vec2{1.0f, 0.5f}};
                const u32 cols[3] = {s.hasCol ? s.col[e.to] : 0xFFFFFFFFu, s.hasCol ? s.col[e.from] : 0xFFFFFFFFu, 0xFFFFFFFFu};
                for (u32 j = 0; j < 3; ++j) {
                    cap.positions.push_back(pts[j]);
                    cap.normals.push_back(n);
                    cap.tangents.push_back(Vec4{tangent, 1.0f});
                    cap.uv0.push_back(uvs[j]);
                    cap.uv1.push_back(ramp[j]);
                    cap.colors.push_back(cols[j]);
                    cap.bounds.add(pts[j]);
                    cap.indices.push_back(base + j);
                }
            }
            for (const auto& [mat, slot] : capOf) { (void)mat; mesh.bounds.add(mesh.primitives[slot].bounds); }
        }
        (void)cell;
        SceneAsset& a = out();
        Node node;
        node.name = "explode3d";
        node.parent = w.baked ? -1 : static_cast<i32>(w.node);
        node.translation = g;
        node.mesh = static_cast<i32>(a.meshes.size());
        node.inScene = true;
        a.meshes.push_back(std::move(mesh));
        a.nodes.push_back(std::move(node));
        info().source.push_back(w.node);
        ++info().fragments;
    }

    void finish(u64 sourceKey) {
        SceneAsset& a = out();
        const u32 original = info().firstFragment;
        // Unidades do texto 3D: a peça é da letra de onde saiu (animadores).
        auto extend = [&](std::vector<u32>& units) {
            if (units.size() < static_cast<usize>(original) * 3) return;
            units.resize(static_cast<usize>(original) * 3);
            for (const u32 s : info().source)
                for (u32 j = 0; j < 3; ++j) units.push_back(units[static_cast<usize>(s) * 3 + j]);
        };
        extend(a.textUnits);
        extend(a.textLogicalUnits);
        a.stats = ImportStats{};
        a.stats.nodes = static_cast<u32>(a.nodes.size());
        a.stats.meshes = static_cast<u32>(a.meshes.size());
        a.stats.materials = static_cast<u32>(a.materials.size());
        a.stats.images = static_cast<u32>(a.images.size());
        for (const Mesh& m : a.meshes) for (const Primitive& p : m.primitives) {
            ++a.stats.primitives;
            a.stats.vertices += p.vertex_count();
            a.stats.triangles += p.triangle_count();
            a.stats.geometryBytes += p.positions.size() * sizeof(Vec3) * 2 + p.indices.size() * sizeof(u32);
        }
        a.stats.sourceTriangles = a.stats.triangles;
        u32 thick = 0;
        std::memcpy(&thick, &spec_.thickness, sizeof thick);
        info().key = (mix64(sourceKey ^ mix64(reinterpret_cast<uintptr_t>(&src_)) ^ mix64(spec_.pieces * 31ull + spec_.seed * 1000003ull)
                            ^ mix64(thick + 0x51ull) ^ mix64(src_.stats.triangles + 0x77ull))
                     | (1ull << 63));
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        registry()[&a] = &info();
    }
};

// --- Cache das fraturas -------------------------------------------------------
struct CacheEntry {
    std::shared_ptr<const SceneAsset> source;
    u64 sourceKey = 0;
    FractureSpec spec{};
    std::shared_ptr<const Fractured> value;
    u64 lastUse = 0;
};
std::mutex gCacheMutex;
std::vector<CacheEntry>& cache() {
    static auto* c = new std::vector<CacheEntry>();
    return *c;
}
u64 gClock = 0;
constexpr usize kCacheEntries = 6;

} // namespace

// =============================================================================
// Parâmetros
// =============================================================================
void declare_parameters(ParameterRegistry& p) {
    constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
    static const char* const timing[] = {"Pelo progresso", "Automático"};
    p.add_float("progress", "Progresso", 0.0f, 0.0f, 100.0f, kAnimPct, "%");                    // 0
    p.add_enum("timing", "Tempo", timing, 2, kTimingProgress);                                  // 1
    p.add_float("start", "Começar em", 0.0f, 0.0f, 20.0f, kParamNone, "s");                     // 2
    p.typed_range(0.0f, 3600.0f);
    p.add_float("duration", "Duração", 2.0f, 0.1f, 10.0f, kParamNone, "s");                      // 3
    p.typed_range(0.05f, 600.0f);
    p.add_int("pieces", "Peças", 120, static_cast<i32>(kMinPieces), static_cast<i32>(kMaxPieces), kParamNone);  // 4
    p.add_int("seed", "Semente", 0, 0, 9999, kParamNone);                                       // 5
    p.add_float("thickness", "Espessura", 60.0f, 0.0f, 100.0f, kParamPercent, "%");            // 6
    p.add_point3("impact", "Ponto de impacto", Vec3{0.0f, 0.0f, 0.0f}, -100.0f, 100.0f);       // 7
    p.add_point3("direction", "Direção", Vec3{0.0f, 0.0f, -1.0f}, -1.0f, 1.0f);                // 8
    p.add_float("force", "Força", 100.0f, 0.0f, 400.0f, kAnimPct, "%");                         // 9
    p.typed_range(0.0f, 2000.0f);
    p.add_float("spread", "Espalhar", 75.0f, 0.0f, 100.0f, kAnimPct, "%");                      // 10
    p.add_float("velocity_random", "Variação", 40.0f, 0.0f, 100.0f, kAnimPct, "%");             // 11
    p.add_angle("spin", "Giro", 360.0f, 0.0f, 3600.0f);                                         // 12
    p.add_float("axis_random", "Eixo aleatório", 100.0f, 0.0f, 100.0f, kAnimPct, "%");          // 13
    p.add_float("gravity", "Gravidade", 40.0f, -200.0f, 200.0f, kAnimPct, "%");                 // 14
    p.typed_range(-1000.0f, 1000.0f);
    p.add_float("drag", "Arrasto", 20.0f, 0.0f, 100.0f, kAnimPct, "%");                         // 15
    p.add_float("wave", "Onda do impacto", 35.0f, 0.0f, 90.0f, kAnimPct, "%");                  // 16
    p.add_float("end_scale", "Escala no fim", 100.0f, 0.0f, 200.0f, kAnimPct, "%");             // 17
    p.add_float("fade", "Esmaecer", 0.0f, 0.0f, 100.0f, kAnimPct, "%");                         // 18
    p.add_bool("reassemble", "Montar (ao contrário)", false, kParamNone);                        // 19
    p.add_color("glow_color", "Cor do brilho", Vec4{1.0f, 0.55f, 0.15f, 1.0f});                 // 20
    p.add_float("glow", "Brilho das bordas", 60.0f, 0.0f, 400.0f, kAnimPct, "%");               // 21
    p.add_float("interior_dark", "Interior escuro", 60.0f, 0.0f, 100.0f, kAnimPct, "%");        // 22
    p.set_enabled_by(kProgress, kTiming, 1u << kTimingProgress);
    p.set_enabled_by(kStart, kTiming, 1u << kTimingAuto);
    p.set_enabled_by(kDuration, kTiming, 1u << kTimingAuto);
    p.add_section("timing", "Tempo", {kTiming, kStart, kDuration});
    p.add_section("pieces", "Peças", {kPieces, kSeed, kThickness});
    p.add_section_range("physics", "Física", kImpact, kWave);
    p.add_section_range("look", "Aparência", kEndScale, kInteriorDark, kInvalidIndex, false);
}

const ParameterRegistry& parameters() {
    static const ParameterRegistry* reg = [] {
        auto* r = new ParameterRegistry();
        declare_parameters(*r);
        return r;
    }();
    return *reg;
}

void set_device_tier(DeviceTier tier) noexcept { gTier.store(static_cast<u8>(tier), std::memory_order_relaxed); }

u32 piece_cap_for(DeviceTier tier) noexcept {
    switch (tier) {
        case DeviceTier::Low: return 250;
        case DeviceTier::Mid: return 600;
        case DeviceTier::High: return 1200;
        default: return kMaxPieces;
    }
}
u32 piece_cap() noexcept { return piece_cap_for(static_cast<DeviceTier>(gTier.load(std::memory_order_relaxed))); }

// =============================================================================
// Fratura (com cache)
// =============================================================================
std::shared_ptr<const Fractured> fracture(const std::shared_ptr<const SceneAsset>& source, u64 sourceKey,
                                          const FractureSpec& spec) {
    if (!source) return nullptr;
    FractureSpec s = spec;
    s.pieces = std::clamp(s.pieces, 1u, kMaxPieces);
    s.seed = std::min(s.seed, 9999u);
    s.thickness = std::clamp(finite_or(s.thickness, 0.0f), 0.0f, 1.0f);
    std::lock_guard<std::mutex> lock(gCacheMutex);
    auto& entries = cache();
    for (CacheEntry& e : entries) {
        if (e.source.get() == source.get() && e.sourceKey == sourceKey && e.spec == s) {
            e.lastUse = ++gClock;
            return e.value;
        }
    }
    Builder builder(source, s);
    const std::shared_ptr<Holder> holder = builder.run(sourceKey);
    std::shared_ptr<const Fractured> value = holder ? std::shared_ptr<const Fractured>(holder, &holder->info) : nullptr;
    if (entries.size() >= kCacheEntries) {
        auto oldest = std::min_element(entries.begin(), entries.end(),
                                       [](const CacheEntry& a, const CacheEntry& b) { return a.lastUse < b.lastUse; });
        entries.erase(oldest);
    }
    entries.push_back(CacheEntry{source, sourceKey, s, value, ++gClock});
    return value;
}

const Fractured* fractured_of(const SceneAsset& asset) noexcept {
    if (asset.sourceName != kMarker) return nullptr;
    std::lock_guard<std::mutex> lock(gRegistryMutex);
    const auto it = registry().find(&asset);
    return it == registry().end() ? nullptr : it->second;
}

void clear_cache() noexcept {
    std::vector<CacheEntry> dropped;
    {
        std::lock_guard<std::mutex> lock(gCacheMutex);
        dropped.swap(cache());
    }
}

// =============================================================================
// Avaliação no instante
// =============================================================================
const EffectInstance* find(const Layer& layer) noexcept {
    const EffectTypeId type = effect_type_id(effect_keys::kExplode3D);
    for (const auto& effect : layer.effects)
        if (effect.enabled && effect.type == type) return &effect;
    return nullptr;
}

Values evaluate(const Layer& layer, const EffectInstance& effect, f64 localFrames, f64 fps) noexcept {
    const ParameterRegistry& reg = parameters();
    auto at = [&](u32 i) { return evaluate_param_f(layer.tracks, effect, i, reg.at(i), localFrames); };
    Values v;
    f32 p = 0.0f;
    if (at(kTiming).as_enum() == kTimingAuto) {
        const f64 rate = fps > 0.0 && std::isfinite(fps) ? fps : 30.0;
        const f64 seconds = localFrames / rate;
        const f64 start = finite_or(at(kStart).as_float(), 0.0f);
        const f64 duration = std::max(0.01, static_cast<f64>(finite_or(at(kDuration).as_float(), 2.0f)));
        p = static_cast<f32>(std::clamp((seconds - start) / duration, 0.0, 1.0));
    } else {
        p = std::clamp(finite_or(at(kProgress).as_float(), 0.0f) / 100.0f, 0.0f, 1.0f);
    }
    if (at(kReassemble).as_bool()) p = 1.0f - p;
    v.progress = p;
    v.spec.pieces = std::min<u32>(static_cast<u32>(std::clamp(at(kPieces).as_int(), static_cast<i32>(kMinPieces),
                                                              static_cast<i32>(kMaxPieces))), piece_cap());
    v.spec.seed = static_cast<u32>(std::clamp(at(kSeed).as_int(), 0, 9999));
    v.spec.thickness = std::round(std::clamp(finite_or(at(kThickness).as_float(), 60.0f), 0.0f, 100.0f)) / 100.0f;
    const Vec3 impact = at(kImpact).as_vec3();
    v.impact = Vec3{std::clamp(finite_or(impact.x, 0.0f) / 100.0f, -2.0f, 2.0f),
                    std::clamp(finite_or(impact.y, 0.0f) / 100.0f, -2.0f, 2.0f),
                    std::clamp(finite_or(impact.z, 0.0f) / 100.0f, -2.0f, 2.0f)};
    const Vec3 dir = at(kDirection).as_vec3();
    v.direction = Vec3{finite_or(dir.x, 0.0f), finite_or(dir.y, 0.0f), finite_or(dir.z, -1.0f)};
    v.force = std::clamp(finite_or(at(kForce).as_float(), 100.0f) / 100.0f, 0.0f, 20.0f);
    v.spread = std::clamp(finite_or(at(kSpread).as_float(), 75.0f) / 100.0f, 0.0f, 1.0f);
    v.velocityRandom = std::clamp(finite_or(at(kVelocityRandom).as_float(), 40.0f) / 100.0f, 0.0f, 1.0f);
    v.spin = std::clamp(finite_or(at(kSpin).as_float(), 360.0f), 0.0f, 36000.0f) * (kTau / 360.0f);
    v.axisRandom = std::clamp(finite_or(at(kAxisRandom).as_float(), 100.0f) / 100.0f, 0.0f, 1.0f);
    v.gravity = std::clamp(finite_or(at(kGravity).as_float(), 40.0f) / 100.0f, -10.0f, 10.0f);
    v.drag = std::clamp(finite_or(at(kDrag).as_float(), 20.0f) / 100.0f, 0.0f, 1.0f);
    v.wave = std::clamp(finite_or(at(kWave).as_float(), 35.0f) / 100.0f, 0.0f, 0.9f);
    v.endScale = std::clamp(finite_or(at(kEndScale).as_float(), 100.0f) / 100.0f, 0.0f, 4.0f);
    v.fade = std::clamp(finite_or(at(kFade).as_float(), 0.0f) / 100.0f, 0.0f, 1.0f);
    const Vec4 c = at(kGlowColor).as_color();
    v.glowColor = Vec3{Color::srgb_to_linear(std::clamp(finite_or(c.x, 1.0f), 0.0f, 1.0f)),
                       Color::srgb_to_linear(std::clamp(finite_or(c.y, 0.55f), 0.0f, 1.0f)),
                       Color::srgb_to_linear(std::clamp(finite_or(c.z, 0.15f), 0.0f, 1.0f))};
    v.glow = std::clamp(finite_or(at(kGlow).as_float(), 60.0f) / 100.0f, 0.0f, 4.0f);
    v.interiorDark = std::clamp(finite_or(at(kInteriorDark).as_float(), 60.0f) / 100.0f, 0.0f, 1.0f);
    return v;
}

Active resolve(const Layer& layer, const std::shared_ptr<const SceneAsset>& source, u64 sourceKey, f64 localFrames, f64 fps) {
    Active a;
    const EffectInstance* effect = find(layer);
    if (!effect || !source) return a;
    a.values = evaluate(layer, *effect, localFrames, fps);
    if (!(a.values.progress > 0.0f)) return a;
    a.fractured = fracture(source, sourceKey, a.values.spec);
    a.exploding = a.fractured != nullptr;
    return a;
}

PieceMotion piece_motion(const Values& v, u32 index, Vec3 center, Vec3 impactPoint, f32 impactReach, f32 size,
                         Vec3 direction, Vec3 down) noexcept {
    PieceMotion m;
    const f32 d = std::clamp((center - impactPoint).length() / std::max(impactReach, 1e-6f), 0.0f, 1.0f);
    const f32 w = std::clamp(v.wave, 0.0f, 0.9f);
    const f32 q = std::clamp((v.progress - w * d) / (1.0f - w), 0.0f, 1.0f);
    if (!(q > 0.0f)) return m;
    const u64 seed = v.spec.seed;
    const Vec3 random = random_unit(seed, index, 11);
    const Vec3 radial = safe_normalize(center - impactPoint, random);
    const Vec3 dir = safe_normalize(direction, radial);
    const Vec3 heading = safe_normalize(dir * (1.0f - v.spread) + radial * v.spread + random * (0.6f * v.velocityRandom), radial);
    // Perto do impacto sai mais rápido; cada peça sorteia a sua velocidade.
    const f32 speed = v.force * 1.6f * size * (1.0f + v.velocityRandom * (hash01(seed, index, 23) - 0.5f) * 1.4f)
                    * (1.2f - 0.4f * d);
    const f32 k = v.drag * 6.0f;
    const f32 travel = k > 1e-4f ? (1.0f - std::exp(-k * q)) / k : q;
    const Vec3 offset = heading * (speed * travel) + down * (0.5f * v.gravity * 2.5f * size * q * q);
    const Vec3 side = safe_normalize(heading.cross(std::fabs(heading.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}), Vec3{0, 1, 0});
    const Vec3 axis = safe_normalize(side * (1.0f - v.axisRandom) + random_unit(seed, index, 37) * v.axisRandom, side);
    const f32 sign = hash01(seed, index, 41) < 0.5f ? -1.0f : 1.0f;
    const f32 angle = sign * v.spin * (0.35f + 1.3f * hash01(seed, index, 43)) * travel;
    m.scale = std::max(0.0f, 1.0f + (v.endScale - 1.0f) * std::pow(q, 1.5f));
    m.opacity = std::clamp(1.0f - v.fade * smooth(0.25f, 1.0f, q), 0.0f, 1.0f);
    const f32 sc = std::max(m.scale, 1e-4f);
    m.delta = Mat4::translation(center + offset) * Mat4::from_quat(Quat::from_axis_angle(axis, angle))
            * Mat4::scale(Vec3{sc, sc, sc}) * Mat4::translation(-center);
    return m;
}

void apply(const SceneAsset& asset, const Layer& layer, f64 localFrames, f64 fps, const Mat4& world,
           std::vector<Mat4>& nodeWorld, std::vector<f32>& nodeOpacity, std::vector<MaterialOverride>& materials) {
    const Fractured* f = fractured_of(asset);
    if (!f) return;
    const EffectInstance* effect = find(layer);
    if (!effect) return;
    const Values v = evaluate(layer, *effect, localFrames, fps);
    const usize n = asset.nodes.size();
    if (nodeWorld.size() < n) nodeWorld.resize(n, Mat4::identity());
    if (nodeOpacity.size() < n) nodeOpacity.resize(n, 1.0f);
    // Gravidade e direção são do MUNDO (y para baixo); a conta é na cena do modelo.
    Vec3 down{0, 1, 0}, direction = v.direction;
    if (!inverse_linear_apply(world, Vec3{0, 1, 0}, down)) down = Vec3{0, 1, 0};
    if (!inverse_linear_apply(world, v.direction, direction)) direction = v.direction;
    down = safe_normalize(down, Vec3{0, 1, 0});
    direction = safe_normalize(direction, Vec3{0, 0, 0});
    const Aabb& box = asset.bounds;
    const Vec3 center = box.valid() ? box.center() : Vec3{};
    const Vec3 half = box.valid() ? box.extent() * 0.5f : Vec3{1, 1, 1};
    const f32 size = std::max({half.x, half.y, half.z, 1e-4f}) * 2.0f;
    const Vec3 impact = center + Vec3{half.x * v.impact.x, half.y * v.impact.y, half.z * v.impact.z};
    f32 reach = 1e-4f;
    for (u32 i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z};
        if (box.valid()) reach = std::max(reach, (corner - impact).length());
    }
    for (u32 i = 0; i < f->fragments; ++i) {
        const usize k = static_cast<usize>(f->firstFragment) + i;
        if (k >= n) break;
        const Node& node = asset.nodes[k];
        const u32 src = i < f->source.size() ? f->source[i] : 0;
        const Mat4 base = node.parent >= 0 && static_cast<usize>(node.parent) < n
            ? nodeWorld[static_cast<usize>(node.parent)] * Mat4::translation(node.translation)
            : Mat4::translation(node.translation);
        const Vec3 c{base.col[3].x, base.col[3].y, base.col[3].z};
        const PieceMotion m = piece_motion(v, i, c, impact, reach, size, direction, down);
        nodeWorld[k] = m.delta * base;
        const f32 inherited = src < nodeOpacity.size() ? nodeOpacity[src] : 1.0f;
        nodeOpacity[k] = m.scale < 1e-3f ? 0.0f : std::clamp(inherited * m.opacity, 0.0f, 1.0f);
    }
    // Interior: escuro e quente na borda da quebra (esfria enquanto voa).
    const f32 heat = 0.25f + 0.75f * (1.0f - v.progress) * (1.0f - v.progress);
    const f32 strength = std::clamp(v.glow * 6.0f * heat, 0.0f, kMaterialEmissionMax);
    for (usize j = 0; j < f->interiorOf.size(); ++j) {
        const u32 index = f->baseMaterials + static_cast<u32>(j);
        const i32 source = f->interiorOf[j];
        Vec4 base{0.8f, 0.8f, 0.8f, 1.0f};
        if (source >= 0 && static_cast<usize>(source) < asset.materials.size()) base = asset.materials[static_cast<usize>(source)].baseColor;
        for (const MaterialOverride& over : materials) {
            if (source < 0 || over.materialIndex != static_cast<u32>(source)) continue;
            if (over.mask & 1) base.x = over.baseColor.x;
            if (over.mask & 2) base.y = over.baseColor.y;
            if (over.mask & 4) base.z = over.baseColor.z;
        }
        MaterialOverride over;
        over.materialIndex = index;
        over.mask = 1u | 2u | 4u | 64u | 128u | 256u | 512u;
        const f32 keep = 1.0f - v.interiorDark;
        over.baseColor = Vec4{base.x * keep, base.y * keep, base.z * keep, base.w};
        over.metallic = 0.0f;
        over.roughness = 1.0f;
        over.emissive = v.glowColor;
        over.emissiveStrength = strength;
        materials.erase(std::remove_if(materials.begin(), materials.end(),
                                       [&](const MaterialOverride& o) { return o.materialIndex == index; }),
                        materials.end());
        materials.push_back(over);
    }
}

} // namespace aurea::scene3d::explode3d
