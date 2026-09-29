// =============================================================================
//  Aurea / scene3d / Shape3D.cpp
//
//  As 10 formas 3D prontas: geometria em código, uma malha por PARTE, com
//  normais (suaves nas curvas, planas nas faces), UV pensada para imagem
//  (face do cubo 0..1 inteira, lateral do cilindro/cone enrolada, esfera
//  equirretangular, frente da estrela/coração planar) e fechadas como sólido
//  (as partes, juntas, não deixam buraco). Ver Shape3D.hpp.
// =============================================================================
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/scene3d/Text3D.hpp"   // triangulate_polygon

#include "aurea/animation/Curve.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/timeline/Layer.hpp"

#if defined(_MSC_VER)
    #pragma warning(push, 0)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
#endif
#include "stb_image.h"   // a implementação mora no GltfImporter.cpp
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>

namespace aurea::scene3d {

ImportResult finalize_scene_asset(std::unique_ptr<SceneAsset> asset, const ImportOptions& options);

namespace {

// -----------------------------------------------------------------------------
// Catálogo
// -----------------------------------------------------------------------------
struct KindInfo {
    const char* key;
    u32 parts;
    const char* partKeys[kShape3DMaxParts];
};

constexpr KindInfo kKinds[kShape3DKindCount] = {
    {"cube", 6, {"front", "back", "right", "left", "top", "bottom"}},
    {"sphere", 2, {"upper", "lower"}},
    {"cylinder", 3, {"top", "bottom", "side"}},
    {"cone", 2, {"base", "side"}},
    {"pyramid", 5, {"base", "front", "right", "back", "left"}},
    {"torus", 4, {"quarter1", "quarter2", "quarter3", "quarter4"}},
    {"star", 6, {"tip1", "tip2", "tip3", "tip4", "tip5", "center"}},
    {"heart", 2, {"left", "right"}},
    {"capsule", 3, {"top", "body", "bottom"}},
    {"diamond", 8, {"facet1", "facet2", "facet3", "facet4", "facet5", "facet6", "facet7", "facet8"}},
};

/// Paleta das partes (sRGB): dá para ver cada parte logo ao adicionar.
constexpr u32 kPalette[kShape3DMaxParts] = {0xF2B84B, 0xEF6F6C, 0x3CC6B7, 0x4D8DF7, 0x9B6CF6, 0xF27BB6, 0x9BD35A, 0xE8E8EE};

Vec4 rgb(u32 c) noexcept {
    return Vec4{static_cast<f32>((c >> 16) & 0xFF) / 255.0f, static_cast<f32>((c >> 8) & 0xFF) / 255.0f,
                static_cast<f32>(c & 0xFF) / 255.0f, 1.0f};
}

f32 srgb_to_linear(f32 c) noexcept { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// -----------------------------------------------------------------------------
// Construção de malha
// -----------------------------------------------------------------------------
struct PartMesh {
    std::vector<Vec3> p, n;
    std::vector<Vec2> uv;
    std::vector<u32> idx;

    u32 vert(Vec3 pos, Vec3 nrm, Vec2 t) {
        p.push_back(pos);
        n.push_back(nrm.normalized());
        uv.push_back(Vec2{std::clamp(t.x, 0.0f, 1.0f), std::clamp(t.y, 0.0f, 1.0f)});
        return static_cast<u32>(p.size() - 1);
    }
    /// Triângulo com a frente para fora: a ordem segue as normais dos vértices
    /// (anti-horário visto de fora, a convenção do glTF). Degenerado cai fora.
    void tri(u32 a, u32 b, u32 c) {
        const Vec3 g = (p[b] - p[a]).cross(p[c] - p[a]);
        if (g.length_sq() < 1e-14f) return;
        const Vec3 ref = n[a] + n[b] + n[c];
        if (g.dot(ref) < 0.0f) std::swap(b, c);
        idx.push_back(a); idx.push_back(b); idx.push_back(c);
    }
    void quad(u32 a, u32 b, u32 c, u32 d) { tri(a, b, c); tri(a, c, d); }
};

constexpr u32 kRound = 32;   ///< segmentos em volta das formas redondas

/// Face plana convexa (leque a partir do 1º ponto) com UV dada por ponto.
void flat_poly(PartMesh& m, const std::vector<Vec3>& pts, const std::vector<Vec2>& uvs, Vec3 normal) {
    const u32 base = static_cast<u32>(m.p.size());
    for (usize i = 0; i < pts.size(); ++i) m.vert(pts[i], normal, uvs[i]);
    for (u32 i = 1; i + 1 < pts.size(); ++i) m.tri(base, base + i, base + i + 1);
}

/// Retângulo plano: centro, eixos "direita" e "cima" vistos de fora (meia
/// medida já incluída). UV 0..1 inteira, (0,0) no canto de cima à esquerda.
void flat_rect(PartMesh& m, Vec3 c, Vec3 right, Vec3 up) {
    const Vec3 normal = right.cross(up).normalized();
    flat_poly(m, {c - right + up, c + right + up, c + right - up, c - right - up},
              {Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}}, normal);
}

/// Disco (tampa) no plano Y = y, normal ±Y, com a borda nos mesmos ângulos
/// da lateral. UV planar como a face de cima/de baixo do cubo.
void disc(PartMesh& m, f32 y, f32 r, bool up) {
    const Vec3 normal{0, up ? 1.0f : -1.0f, 0};
    auto uv = [&](f32 x, f32 z) { return Vec2{x / (2 * r) + 0.5f, up ? z / (2 * r) + 0.5f : 0.5f - z / (2 * r)}; };
    const u32 c = m.vert(Vec3{0, y, 0}, normal, Vec2{0.5f, 0.5f});
    const u32 first = static_cast<u32>(m.p.size());
    for (u32 i = 0; i < kRound; ++i) {
        const f32 a = static_cast<f32>(i) / kRound * 2 * kPi;
        const f32 x = r * std::cos(a), z = r * std::sin(a);
        m.vert(Vec3{x, y, z}, normal, uv(x, z));
    }
    for (u32 i = 0; i < kRound; ++i) m.tri(c, first + i, first + (i + 1) % kRound);
}

/// Faixa de revolução em volta de Y: perfil (raio, y, normal no plano rz) por
/// anel, v de 0 (1º anel) a 1 (último). u enrola 0..1 (costura duplicada).
struct Ring { f32 r, y, nr, ny, v; };
void revolve(PartMesh& m, const std::vector<Ring>& rings) {
    const u32 base = static_cast<u32>(m.p.size());
    const u32 cols = kRound + 1;
    for (const Ring& ring : rings) {
        for (u32 i = 0; i <= kRound; ++i) {
            const f32 a = static_cast<f32>(i % kRound) / kRound * 2 * kPi;
            const f32 c = std::cos(a), s = std::sin(a);
            // No polo (raio 0) o u é o do meio do gomo: sem triângulo degenerado.
            const f32 u = ring.r < 1e-6f ? (static_cast<f32>(i) + 0.5f) / kRound : static_cast<f32>(i) / kRound;
            m.vert(Vec3{ring.r * c, ring.y, ring.r * s}, Vec3{ring.nr * c, ring.ny, ring.nr * s}, Vec2{std::min(u, 1.0f), ring.v});
        }
    }
    for (u32 j = 0; j + 1 < rings.size(); ++j) {
        for (u32 i = 0; i < kRound; ++i) {
            const u32 a = base + j * cols + i, b = a + 1, c = a + cols, d = c + 1;
            if (rings[j].r < 1e-6f) m.tri(a, c, d);
            else if (rings[j + 1].r < 1e-6f) m.tri(a, c, b);
            else m.quad(a, c, d, b);
        }
    }
}

/// Anéis de uma calota esférica de raio `r` centrada em y0, do ângulo polar
/// t0 a t1 (0 = polo de cima), com v de v0 a v1.
std::vector<Ring> sphere_rings(f32 r, f32 y0, f32 t0, f32 t1, u32 steps, f32 v0, f32 v1) {
    std::vector<Ring> out;
    for (u32 j = 0; j <= steps; ++j) {
        const f32 k = static_cast<f32>(j) / steps;
        const f32 t = t0 + (t1 - t0) * k;
        f32 s = std::sin(t);
        if (s < 1e-6f) s = 0.0f;
        out.push_back(Ring{r * s, y0 + r * std::cos(t), s, std::cos(t), v0 + (v1 - v0) * k});
    }
    return out;
}

/// Prisma de um polígono plano (XY) extrudado em Z ±d/2: frente e fundo
/// triangulados, paredes com normais suaves onde `smooth` (índice do ponto)
/// e vincadas no resto. UV da frente planar global (u = x + 0,5, v = 0,5 − y;
/// o fundo espelhado para ler certo por trás); paredes com u ao longo do
/// contorno e v da frente ao fundo.
void prism(PartMesh& m, const std::vector<Vec2>& ring, f32 depth, const std::vector<bool>& smooth) {
    const f32 zf = depth * 0.5f, zb = -depth * 0.5f;
    std::vector<u32> tris;
    if (!triangulate_polygon({ring}, tris)) return;
    f32 area = 0.0f;
    for (usize i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i], b = ring[(i + 1) % ring.size()];
        area += a.x * b.y - b.x * a.y;
    }
    const bool ccw = area > 0.0f;
    for (int side = 0; side < 2; ++side) {
        const f32 z = side == 0 ? zf : zb;
        const Vec3 normal{0, 0, side == 0 ? 1.0f : -1.0f};
        const u32 base = static_cast<u32>(m.p.size());
        for (Vec2 q : ring) m.vert(Vec3{q.x, q.y, z}, normal, Vec2{side == 0 ? q.x + 0.5f : 0.5f - q.x, 0.5f - q.y});
        for (usize t = 0; t + 2 < tris.size(); t += 3) m.tri(base + tris[t], base + tris[t + 1], base + tris[t + 2]);
    }
    // Paredes: normal para FORA (à direita da aresta num contorno anti-horário).
    const usize n = ring.size();
    auto edge_normal = [&](usize i) {
        const Vec2 e = (ring[(i + 1) % n] - ring[i]).normalized();
        return ccw ? Vec2{e.y, -e.x} : Vec2{-e.y, e.x};
    };
    f32 total = 0.0f;
    std::vector<f32> along(n + 1, 0.0f);
    for (usize i = 0; i < n; ++i) { total += (ring[(i + 1) % n] - ring[i]).length(); along[i + 1] = total; }
    total = std::max(total, 1e-6f);
    for (usize i = 0; i < n; ++i) {
        const usize j = (i + 1) % n;
        Vec2 ni = edge_normal(i), nj = ni;
        if (i < smooth.size() && smooth[i]) ni = (edge_normal((i + n - 1) % n) + edge_normal(i)).normalized();
        if (j < smooth.size() && smooth[j]) nj = (edge_normal(i) + edge_normal(j)).normalized();
        const f32 u0 = along[i] / total, u1 = along[i + 1] / total;
        const u32 a = m.vert(Vec3{ring[i].x, ring[i].y, zf}, Vec3{ni.x, ni.y, 0}, Vec2{u0, 0});
        const u32 b = m.vert(Vec3{ring[j].x, ring[j].y, zf}, Vec3{nj.x, nj.y, 0}, Vec2{u1, 0});
        const u32 c = m.vert(Vec3{ring[j].x, ring[j].y, zb}, Vec3{nj.x, nj.y, 0}, Vec2{u1, 1});
        const u32 d = m.vert(Vec3{ring[i].x, ring[i].y, zb}, Vec3{ni.x, ni.y, 0}, Vec2{u0, 1});
        m.quad(a, b, c, d);
    }
}

/// Triângulo plano com UV "base embaixo, ponta em cima" (ou invertido).
/// `left`/`right` como vistos de FORA; com a ponta embaixo a ordem gira.
void flat_tri(PartMesh& m, Vec3 left, Vec3 right, Vec3 apex, bool apexUp = true) {
    Vec3 normal = (right - left).cross(apex - left).normalized();
    if (!apexUp) normal = -normal;
    const f32 base = apexUp ? 1.0f : 0.0f;
    flat_poly(m, {left, right, apex}, {Vec2{0, base}, Vec2{1, base}, Vec2{0.5f, 1.0f - base}}, normal);
}

// --- As formas ---------------------------------------------------------------

void build_cube(std::vector<PartMesh>& parts) {
    const f32 h = 0.5f;
    flat_rect(parts[0], Vec3{0, 0, h}, Vec3{h, 0, 0}, Vec3{0, h, 0});     // frente
    flat_rect(parts[1], Vec3{0, 0, -h}, Vec3{-h, 0, 0}, Vec3{0, h, 0});   // trás
    flat_rect(parts[2], Vec3{h, 0, 0}, Vec3{0, 0, -h}, Vec3{0, h, 0});    // direita
    flat_rect(parts[3], Vec3{-h, 0, 0}, Vec3{0, 0, h}, Vec3{0, h, 0});    // esquerda
    flat_rect(parts[4], Vec3{0, h, 0}, Vec3{h, 0, 0}, Vec3{0, 0, -h});    // topo
    flat_rect(parts[5], Vec3{0, -h, 0}, Vec3{h, 0, 0}, Vec3{0, 0, h});    // base
}

void build_sphere(std::vector<PartMesh>& parts) {
    // Equirretangular na esfera INTEIRA: a metade de cima usa v 0..0,5.
    revolve(parts[0], sphere_rings(0.5f, 0.0f, 0.0f, kPi * 0.5f, 8, 0.0f, 0.5f));
    revolve(parts[1], sphere_rings(0.5f, 0.0f, kPi * 0.5f, kPi, 8, 0.5f, 1.0f));
}

void build_cylinder(std::vector<PartMesh>& parts) {
    const f32 r = 0.4f;
    disc(parts[0], 0.5f, r, true);
    disc(parts[1], -0.5f, r, false);
    revolve(parts[2], {Ring{r, 0.5f, 1, 0, 0}, Ring{r, -0.5f, 1, 0, 1}});
}

void build_cone(std::vector<PartMesh>& parts) {
    const f32 r = 0.5f, h = 1.0f;
    disc(parts[0], -0.5f, r, false);
    const Vec2 slope = Vec2{h, r}.normalized();   // normal da lateral no plano (raio, y)
    revolve(parts[1], {Ring{0, 0.5f, slope.x, slope.y, 0}, Ring{r, -0.5f, slope.x, slope.y, 1}});
}

void build_pyramid(std::vector<PartMesh>& parts) {
    const f32 h = 0.5f, y0 = -0.45f;
    const Vec3 apex{0, 0.45f, 0};
    const Vec3 fl{-h, y0, h}, fr{h, y0, h}, br{h, y0, -h}, bl{-h, y0, -h};
    flat_poly(parts[0], {bl, br, fr, fl}, {Vec2{0, 1}, Vec2{1, 1}, Vec2{1, 0}, Vec2{0, 0}}, Vec3{0, -1, 0});
    flat_tri(parts[1], fl, fr, apex);   // frente
    flat_tri(parts[2], fr, br, apex);   // direita
    flat_tri(parts[3], br, bl, apex);   // trás
    flat_tri(parts[4], bl, fl, apex);   // esquerda
}

void build_torus(std::vector<PartMesh>& parts) {
    // Em pé, de frente para a câmera (o anel no plano XY): o furo aparece.
    const f32 R = 0.35f, r = 0.15f;
    constexpr u32 seg = kRound / 4, tube = 24;
    for (u32 q = 0; q < 4; ++q) {
        PartMesh& m = parts[q];
        for (u32 i = 0; i <= seg; ++i) {
            const f32 phi = (static_cast<f32>(q) + static_cast<f32>(i) / seg) * kPi * 0.5f;
            for (u32 k = 0; k <= tube; ++k) {
                const f32 th = static_cast<f32>(k % tube) / tube * 2 * kPi;
                const Vec3 nrm{std::cos(th) * std::cos(phi), std::cos(th) * std::sin(phi), std::sin(th)};
                const Vec3 pos{(R + r * std::cos(th)) * std::cos(phi), (R + r * std::cos(th)) * std::sin(phi), r * std::sin(th)};
                m.vert(pos, nrm, Vec2{static_cast<f32>(i) / seg, static_cast<f32>(k) / tube});
            }
        }
        for (u32 i = 0; i < seg; ++i) {
            for (u32 k = 0; k < tube; ++k) {
                const u32 a = i * (tube + 1) + k, b = a + 1, c = a + tube + 1, d = c + 1;
                m.quad(a, b, d, c);
            }
        }
    }
}

void build_star(std::vector<PartMesh>& parts) {
    const f32 outer = 0.5f, inner = 0.2f, depth = 0.25f;
    Vec2 O[5], I[5];
    for (u32 k = 0; k < 5; ++k) {
        const f32 a = (90.0f + 72.0f * k) * kDeg2Rad, b = a + 36.0f * kDeg2Rad;
        O[k] = Vec2{outer * std::cos(a), outer * std::sin(a)};
        I[k] = Vec2{inner * std::cos(b), inner * std::sin(b)};
    }
    // Ponta k: a ponta O[k] entre os vales I[k−1] e I[k]. Cada ponta e o
    // centro são sólidos fechados: a parede de dentro de cada ponta encosta,
    // virada ao contrário, na parede do centro — juntas não deixam buraco.
    for (u32 k = 0; k < 5; ++k) prism(parts[k], {O[k], I[(k + 4) % 5], I[k]}, depth, {});
    prism(parts[5], {I[0], I[1], I[2], I[3], I[4]}, depth, {});
}

void build_heart(std::vector<PartMesh>& parts) {
    // Curva clássica do coração, metade direita (x ≥ 0) do entalhe à ponta.
    constexpr u32 steps = 40;
    std::vector<Vec2> right;
    const f32 scale = 1.0f / 34.0f, yOff = -(12.0f - 17.0f) * 0.5f;   // centra 12..−17 em Y
    for (u32 i = 0; i <= steps; ++i) {
        const f32 t = static_cast<f32>(i) / steps * kPi;
        const f32 s = std::sin(t);
        f32 x = 16.0f * s * s * s;
        const f32 y = 13.0f * std::cos(t) - 5.0f * std::cos(2 * t) - 2.0f * std::cos(3 * t) - std::cos(4 * t);
        if (i == 0 || i == steps) x = 0.0f;
        right.push_back(Vec2{x * scale, (y + yOff) * scale});
    }
    std::vector<bool> smooth(right.size(), true);
    smooth.front() = smooth.back() = false;   // o entalhe e a ponta são cantos
    std::vector<Vec2> left(right.rbegin(), right.rend());
    for (Vec2& q : left) q.x = -q.x;
    prism(parts[0], left, 0.3f, smooth);
    prism(parts[1], right, 0.3f, smooth);
}

void build_capsule(std::vector<PartMesh>& parts) {
    const f32 r = 0.3f, half = 0.2f;
    revolve(parts[0], sphere_rings(r, half, 0.0f, kPi * 0.5f, 8, 0.0f, 1.0f));
    revolve(parts[1], {Ring{r, half, 1, 0, 0}, Ring{r, -half, 1, 0, 1}});
    revolve(parts[2], sphere_rings(r, -half, kPi * 0.5f, kPi, 8, 0.0f, 1.0f));
}

void build_diamond(std::vector<PartMesh>& parts) {
    const f32 r = 0.42f;
    const Vec3 top{0, 0.5f, 0}, bottom{0, -0.5f, 0};
    Vec3 E[4];
    for (u32 k = 0; k < 4; ++k) {
        const f32 a = (-45.0f + 90.0f * k) * kDeg2Rad;
        E[k] = Vec3{r * std::cos(a), 0, r * std::sin(a)};
    }
    // Facetas 1..4 em cima, 5..8 embaixo. A aresta vista de fora vai da
    // esquerda para a direita (E[k+1] → E[k]).
    for (u32 k = 0; k < 4; ++k) {
        flat_tri(parts[k], E[(k + 1) % 4], E[k], top, true);
        flat_tri(parts[4 + k], E[(k + 1) % 4], E[k], bottom, false);
    }
}

// -----------------------------------------------------------------------------
// Imagens (cache por arquivo: trocar a cor não decodifica de novo)
// -----------------------------------------------------------------------------
struct CachedImage {
    std::shared_ptr<const std::vector<u8>> rgba;
    u32 w = 0, h = 0;
    bool alpha = false;
    std::filesystem::file_time_type stamp{};
    u64 bytes = 0;
};

std::mutex& image_cache_mutex() { static std::mutex m; return m; }
std::map<std::string, CachedImage>& image_cache() { static std::map<std::string, CachedImage> c; return c; }
constexpr usize kImageCacheMax = 24;

void halve_until(std::vector<u8>& rgba, u32& w, u32& h, u32 maxSize) {
    while (maxSize > 0 && (w > maxSize || h > maxSize) && w > 1 && h > 1) {
        const u32 nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        std::vector<u8> out(static_cast<usize>(nw) * nh * 4);
        for (u32 y = 0; y < nh; ++y)
            for (u32 x = 0; x < nw; ++x)
                for (u32 c = 0; c < 4; ++c) {
                    u32 s = 0;
                    for (u32 dy = 0; dy < 2; ++dy)
                        for (u32 dx = 0; dx < 2; ++dx)
                            s += rgba[(static_cast<usize>(std::min(h - 1, y * 2 + dy)) * w + std::min(w - 1, x * 2 + dx)) * 4 + c];
                    out[(static_cast<usize>(y) * nw + x) * 4 + c] = static_cast<u8>((s + 2) / 4);
                }
        rgba.swap(out);
        w = nw;
        h = nh;
    }
}

} // namespace

// =============================================================================
// Catálogo e receita
// =============================================================================

u32 shape3d_part_count(Shape3DKind kind) noexcept {
    const u32 k = static_cast<u32>(kind);
    return k < kShape3DKindCount ? kKinds[k].parts : 0;
}

const char* shape3d_kind_key(Shape3DKind kind) noexcept {
    const u32 k = static_cast<u32>(kind);
    return k < kShape3DKindCount ? kKinds[k].key : nullptr;
}

const char* shape3d_part_key(Shape3DKind kind, u32 part) noexcept {
    const u32 k = static_cast<u32>(kind);
    return k < kShape3DKindCount && part < kKinds[k].parts ? kKinds[k].partKeys[part] : nullptr;
}

Shape3DSpec default_shape3d(Shape3DKind kind) {
    Shape3DSpec s;
    s.kind = static_cast<u32>(kind) < kShape3DKindCount ? kind : Shape3DKind::Cube;
    const u32 n = shape3d_part_count(s.kind);
    s.parts.resize(n);
    for (u32 i = 0; i < n; ++i) s.parts[i].color = rgb(kPalette[i % kShape3DMaxParts]);
    if (s.kind == Shape3DKind::Heart) { s.parts[0].color = rgb(0xE8455A); s.parts[1].color = rgb(0xF06277); }
    return s;
}

void normalize_shape3d(Shape3DSpec& spec) {
    if (static_cast<u32>(spec.kind) >= kShape3DKindCount) spec.kind = Shape3DKind::Cube;
    spec.parts.resize(shape3d_part_count(spec.kind));
    spec.metallic = std::isfinite(spec.metallic) ? std::clamp(spec.metallic, 0.0f, 1.0f) : 0.0f;
    spec.roughness = std::isfinite(spec.roughness) ? std::clamp(spec.roughness, 0.0f, 1.0f) : 0.45f;
    for (Shape3DPart& p : spec.parts) {
        for (f32* c : {&p.color.x, &p.color.y, &p.color.z, &p.color.w})
            *c = std::isfinite(*c) ? std::clamp(*c, 0.0f, 1.0f) : 1.0f;
    }
}

std::string encode_shape3d(const Shape3DSpec& in) {
    Shape3DSpec s = in;
    normalize_shape3d(s);
    auto byte = [](f32 v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char head[96];
    std::snprintf(head, sizeof(head), "v1;k=%u;m=%.3f/%.3f;", static_cast<unsigned>(s.kind), static_cast<double>(s.metallic),
                  static_cast<double>(s.roughness));
    std::string out = std::string(kShape3DScheme) + head;
    constexpr char hex[] = "0123456789abcdef";
    for (u32 i = 0; i < s.parts.size(); ++i) {
        const Shape3DPart& p = s.parts[i];
        char c[48];
        std::snprintf(c, sizeof(c), "c%u=%02x%02x%02x%02x;", i, byte(p.color.x), byte(p.color.y), byte(p.color.z), byte(p.color.w));
        out += c;
        if (!p.image.empty()) {
            // O caminho vai em hex (como a fonte do texto 3D): ';' e '=' no
            // nome do arquivo não quebram a receita.
            out += "i" + std::to_string(i) + "=";
            for (unsigned char ch : p.image) { out += hex[ch >> 4]; out += hex[ch & 15]; }
            out += ';';
        }
    }
    return out;
}

bool decode_shape3d(const std::string& src, Shape3DSpec& out) {
    const std::string scheme = kShape3DScheme;
    if (src.rfind(scheme, 0) != 0) return false;
    Shape3DSpec s;
    bool haveKind = false;
    std::vector<std::pair<u32, Vec4>> colors;
    std::vector<std::pair<u32, std::string>> images;
    usize i = scheme.size();
    while (i < src.size()) {
        usize j = src.find(';', i);
        if (j == std::string::npos) j = src.size();
        const std::string kv = src.substr(i, j - i);
        i = j + 1;
        const usize eq = kv.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string key = kv.substr(0, eq), value = kv.substr(eq + 1);
        if (key == "k") {
            const unsigned long k = std::strtoul(value.c_str(), nullptr, 10);
            if (k >= kShape3DKindCount) return false;
            s.kind = static_cast<Shape3DKind>(k);
            haveKind = true;
        } else if (key == "m") {
            float metal = 0.0f, rough = 0.45f;
            if (std::sscanf(value.c_str(), "%f/%f", &metal, &rough) == 2) { s.metallic = metal; s.roughness = rough; }
        } else if (key.size() >= 2 && (key[0] == 'c' || key[0] == 'i') && std::isdigit(static_cast<unsigned char>(key[1]))) {
            const u32 part = static_cast<u32>(std::strtoul(key.c_str() + 1, nullptr, 10));
            if (part >= kShape3DMaxParts) continue;
            if (key[0] == 'c' && value.size() == 8) {
                const unsigned long c = std::strtoul(value.c_str(), nullptr, 16);
                colors.emplace_back(part, Vec4{static_cast<f32>((c >> 24) & 0xFF) / 255.0f, static_cast<f32>((c >> 16) & 0xFF) / 255.0f,
                                               static_cast<f32>((c >> 8) & 0xFF) / 255.0f, static_cast<f32>(c & 0xFF) / 255.0f});
            } else if (key[0] == 'i') {
                auto digit = [](char ch) -> int {
                    if (ch >= '0' && ch <= '9') return ch - '0';
                    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                    return -1;
                };
                std::string path;
                for (usize k = 0; k + 1 < value.size(); k += 2) {
                    const int a = digit(value[k]), b = digit(value[k + 1]);
                    if (a < 0 || b < 0 || (a == 0 && b == 0)) { path.clear(); break; }
                    path += static_cast<char>((a << 4) | b);
                }
                if (!path.empty()) images.emplace_back(part, std::move(path));
            }
        }
    }
    if (!haveKind) return false;
    Shape3DSpec d = default_shape3d(s.kind);
    d.metallic = s.metallic;
    d.roughness = s.roughness;
    for (auto& [part, c] : colors) if (part < d.parts.size()) d.parts[part].color = c;
    for (auto& [part, path] : images) if (part < d.parts.size()) d.parts[part].image = std::move(path);
    normalize_shape3d(d);
    out = std::move(d);
    return true;
}

// =============================================================================
// Imagem
// =============================================================================

bool load_shape3d_image(const std::string& path, u32 maxSize, Image& out) {
    if (path.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path fp = fs::u8path(path);
    const auto stamp = fs::last_write_time(fp, ec);
    if (ec) return false;
    const u64 bytes = static_cast<u64>(fs::file_size(fp, ec));
    if (ec || bytes == 0) return false;
    const std::string key = path + "\n" + std::to_string(maxSize);
    {
        std::lock_guard<std::mutex> lock(image_cache_mutex());
        const auto it = image_cache().find(key);
        if (it != image_cache().end() && it->second.stamp == stamp && it->second.bytes == bytes) {
            out.sharedRgba = it->second.rgba;
            out.width = it->second.w;
            out.height = it->second.h;
            out.hasAlpha = it->second.alpha;
            out.rgba.clear();
            out.uri = path;
            return true;
        }
    }
    std::vector<u8> file;
    if (!fileio::read_all(path, file, 64u << 20)) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }
    std::vector<u8> rgba(px, px + static_cast<usize>(w) * static_cast<usize>(h) * 4);
    stbi_image_free(px);
    u32 uw = static_cast<u32>(w), uh = static_cast<u32>(h);
    halve_until(rgba, uw, uh, maxSize);
    CachedImage c;
    c.rgba = std::make_shared<const std::vector<u8>>(std::move(rgba));
    c.w = uw;
    c.h = uh;
    c.alpha = comp == 2 || comp == 4;
    c.stamp = stamp;
    c.bytes = bytes;
    {
        std::lock_guard<std::mutex> lock(image_cache_mutex());
        auto& cache = image_cache();
        if (cache.size() >= kImageCacheMax) cache.clear();
        cache[key] = c;
    }
    out.sharedRgba = c.rgba;
    out.width = uw;
    out.height = uh;
    out.hasAlpha = c.alpha;
    out.rgba.clear();
    out.uri = path;
    return true;
}

// =============================================================================
// Malha
// =============================================================================

ImportResult build_shape3d(const Shape3DSpec& in, const Shape3DPathResolver& resolve, u32 maxTextureSize) {
    Shape3DSpec spec = in;
    normalize_shape3d(spec);
    const u32 count = shape3d_part_count(spec.kind);
    std::vector<PartMesh> parts(count);
    switch (spec.kind) {
        case Shape3DKind::Cube:     build_cube(parts); break;
        case Shape3DKind::Sphere:   build_sphere(parts); break;
        case Shape3DKind::Cylinder: build_cylinder(parts); break;
        case Shape3DKind::Cone:     build_cone(parts); break;
        case Shape3DKind::Pyramid:  build_pyramid(parts); break;
        case Shape3DKind::Torus:    build_torus(parts); break;
        case Shape3DKind::Star:     build_star(parts); break;
        case Shape3DKind::Heart:    build_heart(parts); break;
        case Shape3DKind::Capsule:  build_capsule(parts); break;
        case Shape3DKind::Diamond:  build_diamond(parts); break;
    }
    auto asset = std::make_unique<SceneAsset>();
    asset->sourceName = shape3d_kind_key(spec.kind);
    asset->shapeParts = true;
    // Unidades da animação por parte (as mesmas do texto 3D): parte i é a
    // "letra" i e a "palavra" i; tudo numa "linha" só.
    asset->textWords = count;
    asset->textLines = 1;
    asset->textUnits.reserve(static_cast<usize>(count) * 3u);
    for (u32 i = 0; i < count; ++i) asset->textUnits.insert(asset->textUnits.end(), {i, i, 0u});
    for (u32 i = 0; i < count; ++i) {
        const Shape3DPart& part = spec.parts[i];
        Material mat;
        mat.name = shape3d_part_key(spec.kind, i);
        mat.baseColor = Vec4{srgb_to_linear(part.color.x), srgb_to_linear(part.color.y), srgb_to_linear(part.color.z), 1.0f};
        mat.metallic = spec.metallic;
        mat.roughness = spec.roughness;
        mat.specular = 0.5f;
        mat.specularColor = Vec3{0.5f, 0.5f, 0.5f};
        if (!part.image.empty()) {
            Image img;
            const std::string path = resolve ? resolve(part.image) : part.image;
            if (load_shape3d_image(path, maxTextureSize, img)) {
                img.name = mat.name;
                mat.baseColorTex.image = static_cast<i32>(asset->images.size());
                asset->images.push_back(std::move(img));
            } else {
                const usize slash = part.image.find_last_of("/\\:");
                const std::string name = slash == std::string::npos ? part.image : part.image.substr(slash + 1);
                if (std::find(asset->missingTextures.begin(), asset->missingTextures.end(), name) == asset->missingTextures.end())
                    asset->missingTextures.push_back(name);
            }
        }
        asset->materials.push_back(std::move(mat));

        PartMesh& pm = parts[i];
        Primitive prim;
        prim.positions = std::move(pm.p);
        prim.normals = std::move(pm.n);
        prim.uv0 = std::move(pm.uv);
        prim.indices = std::move(pm.idx);
        prim.material = static_cast<i32>(i);
        for (const Vec3& v : prim.positions) prim.bounds.add(v);
        Mesh mesh;
        mesh.name = shape3d_part_key(spec.kind, i);
        mesh.bounds = prim.bounds;
        mesh.primitives.push_back(std::move(prim));
        asset->meshes.push_back(std::move(mesh));
        Node node;
        node.name = shape3d_part_key(spec.kind, i);
        node.mesh = static_cast<i32>(i);
        asset->nodes.push_back(std::move(node));
        asset->roots.push_back(static_cast<i32>(i));
    }
    ImportOptions o;
    o.generateLods = false;   // as formas já são leves; LOD mudaria a silhueta das partes
    ImportResult fin = finalize_scene_asset(std::move(asset), o);
    if (fin.ok()) fin.asset->stats.images = static_cast<u32>(fin.asset->images.size());
    return fin;
}

// =============================================================================
// Transform das partes
// =============================================================================

f32 shape3d_part_value(const Layer& layer, u32 part, u32 channel, f64 localTime) noexcept {
    const f32 neutral = shape3d_channel_neutral(channel);
    const Track* t = layer.tracks.find(TrackProperty::ShapePart, part, channel);
    if (!t) return neutral;
    if (!t->driven()) return std::isfinite(t->staticValue) ? t->staticValue : neutral;
    const f64 f = std::floor(localTime);
    const f32 a = t->value_or(FrameIndex{static_cast<i64>(f)}, t->staticValue);
    const f32 k = static_cast<f32>(localTime - f);
    f32 v = a;
    if (k > 0.0f) v = a + (t->value_or(FrameIndex{static_cast<i64>(f) + 1}, t->staticValue) - a) * k;
    return std::isfinite(v) ? v : neutral;
}

Mat4 shape3d_part_matrix(const f32* c, Vec3 pivot) noexcept {
    auto safe_scale = [](f32 s) { return std::fabs(s) < 1e-4f ? (s < 0 ? -1e-4f : 1e-4f) : std::clamp(s, -100.0f, 100.0f); };
    const Quat q = Quat::from_euler_zyx(c[3] * kDeg2Rad, c[4] * kDeg2Rad, c[5] * kDeg2Rad);
    return Mat4::translation(pivot + Vec3{c[0], c[1], c[2]}) * Mat4::from_quat(q)
         * Mat4::scale(Vec3{safe_scale(c[6]), safe_scale(c[7]), safe_scale(c[8])}) * Mat4::translation(-pivot);
}

void apply_shape3d_parts(const SceneAsset& asset, const Layer& layer, f64 localTime, std::vector<Mat4>& nodeWorld) {
    if (!asset.shapeParts) return;
    // Quais partes têm trilha (a maioria dos quadros: nenhuma ou poucas).
    u32 touched = 0;
    for (u32 i = 0; i < layer.tracks.size(); ++i) {
        const Track& t = layer.tracks.at(i);
        if (t.property == TrackProperty::ShapePart && t.effectIndex < kShape3DMaxParts && t.effectParamIndex < kShape3DChannels)
            touched |= 1u << t.effectIndex;
    }
    for (u32 part = 0; touched && part < asset.nodes.size() && part < nodeWorld.size(); ++part) {
        if (!(touched & (1u << part))) continue;
        const i32 mesh = asset.nodes[part].mesh;
        if (mesh < 0 || static_cast<usize>(mesh) >= asset.meshes.size()) continue;
        f32 c[kShape3DChannels];
        bool neutral = true;
        for (u32 ch = 0; ch < kShape3DChannels; ++ch) {
            c[ch] = shape3d_part_value(layer, part, ch, localTime);
            neutral = neutral && std::fabs(c[ch] - shape3d_channel_neutral(ch)) < 1e-7f;
        }
        if (neutral) continue;
        const Vec3 pivot = nodeWorld[part].transform_point(asset.meshes[static_cast<usize>(mesh)].bounds.center());
        nodeWorld[part] = shape3d_part_matrix(c, pivot) * nodeWorld[part];
    }
}

} // namespace aurea::scene3d
