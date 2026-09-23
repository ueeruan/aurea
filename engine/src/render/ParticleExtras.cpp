// =============================================================================
//  Aurea / render / ParticleExtras.cpp  —  AUREA PARTICULAR (8.2)
//
//  Os dados do Particular que não cabem no bloco de uniformes: pontos de
//  emissão de outra camada (camada, texto, caminho, malha 3D), a malha da
//  partícula de malha, curvas ao longo da vida, aleatórios e colisão.
//
//  A SIMULAÇÃO CONTINUA ANALÍTICA: nada aqui é estado de partícula. Os pontos
//  são uma DISTRIBUIÇÃO da fonte (onde uma partícula pode nascer); o shader
//  escolhe um deles por hash(semente, slot, geração). Mesma semente, mesmo
//  ponto — em qualquer quadro, em qualquer ordem de seek.
//
//  Custo: a fonte é amostrada na CPU UMA vez por revisão dela (chave de
//  conteúdo); o quadro só escreve o cabeçalho (576 B).
// =============================================================================
#include "aurea/render/ParticleExtras.hpp"

#include "aurea/render/MaskRaster.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::particles {
namespace {

constexpr f32 kPi = 3.14159265358979f;

u64 mix64(u64 h, u64 v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 31);
}
u64 mixf(u64 h, f32 f) noexcept { u32 b; std::memcpy(&b, &f, 4); return mix64(h, b); }

/// Gerador determinístico da CPU (splitmix64): a MESMA fonte dá os MESMOS
/// pontos em qualquer aparelho — a prévia e o export leem o mesmo buffer.
struct Rng {
    u64 s;
    u64 next() noexcept {
        u64 z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    f32 unit() noexcept { return static_cast<f32>(next() >> 40) * (1.0f / 16777216.0f); }
};

f32 srgb_lin(f32 c) noexcept { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

/// Inversa geral 4×4 (cofatores) — a mesma conta do Engine.
Mat4 inverse4(const Mat4& a) noexcept {
    const f32* m = &a.col[0].x;
    f32 inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    const f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return Mat4::identity();
    Mat4 r;
    f32* o = &r.col[0].x;
    for (int i = 0; i < 16; ++i) o[i] = inv[i] / det;
    return r;
}

/// Cena do modelo → espaço da camada: a MESMA conta de `layer_from_model` do
/// Renderer (giro de 180° em X, metros → px, pivô no centro da caixa).
Mat4 layer_from_model(const Model3DData& m) noexcept {
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    return flip * Mat4::scale(Vec3{m.unitScale, m.unitScale, m.unitScale}) * Mat4::translation(-m.pivot);
}

Vec3 xform_point(const Mat4& m, Vec3 p) noexcept {
    const Vec4 r = m * Vec4{p.x, p.y, p.z, 1.0f};
    return Vec3{r.x, r.y, r.z};
}
Vec3 xform_dir(const Mat4& m, Vec3 v) noexcept {
    const Vec4 r = m * Vec4{v.x, v.y, v.z, 0.0f};
    return Vec3{r.x, r.y, r.z};
}

// -----------------------------------------------------------------------------
// Cobertura em grade → pontos (superfície ou borda)
// -----------------------------------------------------------------------------

/// Grade de cobertura de uma fonte raster (imagem, texto, forma, vetor):
/// `on[i]` = a célula está acesa (alfa > 0,5). A célula mede `cell` px da
/// fonte e a grade começa em `origin`.
struct Grid {
    u32 w = 0, h = 0;
    f32 cell = 1.0f;
    Vec2 origin{0, 0};
    std::vector<u8> on;
    [[nodiscard]] bool at(i32 x, i32 y) const noexcept {
        return x >= 0 && y >= 0 && x < static_cast<i32>(w) && y < static_cast<i32>(h) && on[static_cast<usize>(y) * w + x] != 0;
    }
};

/// Tamanho da célula para que a grade tenha no máximo ~512×512 células.
f32 cell_for(f32 width, f32 height, f32 minCell = 1.0f) noexcept {
    const f32 area = std::max(1.0f, width * height);
    return std::max(minCell, std::sqrt(area / (512.0f * 512.0f)));
}

/// Mantém no máximo `cap` pontos, escolhidos de modo uniforme e determinístico
/// (passo fracionário — nenhuma região da fonte perde mais que outra).
void thin(std::vector<Vec4>& pts, u32 cap) {
    if (pts.size() <= cap) return;
    std::vector<Vec4> keep;
    keep.reserve(cap);
    const f64 step = static_cast<f64>(pts.size()) / static_cast<f64>(cap);
    for (u32 i = 0; i < cap; ++i) keep.push_back(pts[static_cast<usize>(static_cast<f64>(i) * step)]);
    pts.swap(keep);
}

/// Superfície: todas as células acesas. Borda: acesas com vizinho (4) apagado.
void grid_points(const Grid& g, bool edges, StaticData& out) {
    out.points.clear();
    for (u32 y = 0; y < g.h; ++y) {
        for (u32 x = 0; x < g.w; ++x) {
            if (!g.at(static_cast<i32>(x), static_cast<i32>(y))) continue;
            if (edges) {
                const i32 xi = static_cast<i32>(x), yi = static_cast<i32>(y);
                if (g.at(xi - 1, yi) && g.at(xi + 1, yi) && g.at(xi, yi - 1) && g.at(xi, yi + 1)) continue;
            }
            out.points.push_back(Vec4{g.origin.x + (static_cast<f32>(x) + 0.5f) * g.cell,
                                      g.origin.y + (static_cast<f32>(y) + 0.5f) * g.cell, 0.0f, 0.0f});
        }
    }
    thin(out.points, kMaxEmitPoints);
    out.pointMode = 1;          // célula: o ponto cai em qualquer lugar dela
    out.cellSize = g.cell;
}

/// Polilinhas → pontos distribuídos POR COMPRIMENTO (espaçamento igual em
/// todas as linhas: a densidade de partículas não depende de quantos vértices
/// o caminho tem). `vertices` = só os vértices.
void contour_points(const std::vector<std::vector<Vec2>>& lines, const std::vector<bool>& closed, bool vertices,
                    StaticData& out) {
    out.points.clear();
    out.pointMode = 0;
    out.cellSize = 0.0f;
    if (vertices) {
        for (const auto& c : lines) for (Vec2 p : c) out.points.push_back(Vec4{p.x, p.y, 0, 0});
        thin(out.points, kMaxEmitPoints);
        return;
    }
    f64 total = 0.0;
    for (usize i = 0; i < lines.size(); ++i) {
        const auto& c = lines[i];
        const usize n = c.size();
        if (n < 2) continue;
        const usize segs = closed[i] ? n : n - 1;
        for (usize k = 0; k < segs; ++k) total += (c[(k + 1) % n] - c[k]).length();
    }
    if (total <= 1e-6) return;
    const u32 count = static_cast<u32>(std::clamp(total / 0.5, 16.0, static_cast<f64>(kMaxEmitPoints)));
    const f64 step = total / static_cast<f64>(count);
    f64 carry = step * 0.5;   // o primeiro ponto no meio do primeiro passo
    for (usize i = 0; i < lines.size(); ++i) {
        const auto& c = lines[i];
        const usize n = c.size();
        if (n < 2) continue;
        const usize segs = closed[i] ? n : n - 1;
        for (usize k = 0; k < segs; ++k) {
            const Vec2 a = c[k], b = c[(k + 1) % n];
            const f64 len = (b - a).length();
            while (carry <= len && out.points.size() < kMaxEmitPoints) {
                const f32 t = len > 0.0 ? static_cast<f32>(carry / len) : 0.0f;
                out.points.push_back(Vec4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, 0, 0});
                carry += step;
            }
            carry -= len;
        }
    }
}

// -----------------------------------------------------------------------------
// Forma (SDF): a MESMA matemática de shaders/shape/shape.frag, na CPU
// -----------------------------------------------------------------------------
f32 sd_round_box(Vec2 q, Vec2 b, f32 r) {
    r = std::min(r, std::min(b.x, b.y));
    const Vec2 d{std::fabs(q.x) - b.x + r, std::fabs(q.y) - b.y + r};
    const Vec2 m{std::max(d.x, 0.0f), std::max(d.y, 0.0f)};
    return m.length() + std::min(std::max(d.x, d.y), 0.0f) - r;
}
f32 sd_ellipse(Vec2 q, Vec2 ab) {
    const f32 k0 = Vec2{q.x / ab.x, q.y / ab.y}.length();
    const f32 k1 = Vec2{q.x / (ab.x * ab.x), q.y / (ab.y * ab.y)}.length();
    return k0 < 1e-6f ? -std::min(ab.x, ab.y) : k0 * (k0 - 1.0f) / k1;
}
f32 glsl_mod(f32 x, f32 y) { return x - y * std::floor(x / y); }
f32 sd_ngon(Vec2 q, f32 r, f32 n) {
    const f32 an = kPi / n;
    const f32 bn = glsl_mod(std::atan2(q.x, -q.y), 2.0f * an) - an;
    const f32 L = q.length();
    q = Vec2{L * std::cos(bn), std::fabs(L * std::sin(bn))};
    q = q - Vec2{r * std::cos(an), r * std::sin(an)};
    q.y += std::clamp(-q.y, 0.0f, r * std::sin(an));
    return q.length() * (q.x > 0.0f ? 1.0f : (q.x < 0.0f ? -1.0f : 0.0f));
}
f32 sd_star(Vec2 q, f32 r, f32 n, f32 inner) {
    const f32 an = kPi / n;
    const f32 a = glsl_mod(std::atan2(q.x, -q.y), 2.0f * an) - an;
    const f32 L = q.length();
    q = Vec2{L * std::cos(a), std::fabs(L * std::sin(a))};
    const Vec2 tip{r, 0.0f};
    const Vec2 valley{inner * r * std::cos(an), inner * r * std::sin(an)};
    const Vec2 e = valley - tip, w = q - tip;
    const f32 h = std::clamp((w.x * e.x + w.y * e.y) / (e.x * e.x + e.y * e.y), 0.0f, 1.0f);
    const f32 d = (w - e * h).length();
    const f32 s = e.x * w.y - e.y * w.x;
    return s > 0.0f ? -d : d;
}
f32 sd_triangle(Vec2 q, Vec2 p0, Vec2 p1, Vec2 p2) {
    auto dot = [](Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; };
    const Vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    const Vec2 v0 = q - p0, v1 = q - p1, v2 = q - p2;
    const Vec2 pq0 = v0 - e0 * std::clamp(dot(v0, e0) / dot(e0, e0), 0.0f, 1.0f);
    const Vec2 pq1 = v1 - e1 * std::clamp(dot(v1, e1) / dot(e1, e1), 0.0f, 1.0f);
    const Vec2 pq2 = v2 - e2 * std::clamp(dot(v2, e2) / dot(e2, e2), 0.0f, 1.0f);
    const f32 s = (e0.x * e2.y - e0.y * e2.x) >= 0.0f ? 1.0f : -1.0f;
    f32 dx = dot(pq0, pq0), dy = s * (v0.x * e0.y - v0.y * e0.x);
    dx = std::min(dx, dot(pq1, pq1)); dy = std::min(dy, s * (v1.x * e1.y - v1.y * e1.x));
    dx = std::min(dx, dot(pq2, pq2)); dy = std::min(dy, s * (v2.x * e2.y - v2.y * e2.x));
    return -std::sqrt(dx) * (dy > 0.0f ? 1.0f : (dy < 0.0f ? -1.0f : 0.0f));
}
f32 shape_sd(const ShapeData& sh, Vec2 q, Vec2 half) {
    const u32 type = sh.shapeType;
    const f32 m = std::min(half.x, half.y);
    const Vec2 st{half.x / m, half.y / m};
    const f32 mst = std::min(st.x, st.y);
    const f32 inner = std::clamp(sh.innerRadius, 0.05f, 0.95f);
    switch (type) {
        case 0: return sd_round_box(q, half, sh.cornerRadius);
        case 1: return sd_ellipse(q, half);
        case 3: return sd_ngon(Vec2{q.x / st.x, q.y / st.y}, m, std::max(3.0f, sh.points)) * mst;
        case 4: return sd_star(Vec2{q.x / st.x, q.y / st.y}, m, std::max(3.0f, sh.points), inner) * mst;
        case 5: {
            const Vec2 a{std::fabs(q.x), std::fabs(q.y)};
            const f32 t = m * inner;
            return std::min(sd_round_box(a, Vec2{half.x, t}, 0.0f), sd_round_box(a, Vec2{t, half.y}, 0.0f));
        }
        case 6: {
            const f32 ring = m * (1.0f - inner) * 0.5f;
            return std::fabs(sd_ellipse(q, Vec2{half.x - ring, half.y - ring})) - ring;
        }
        case 7: return std::max(sd_ellipse(q, half), -std::max(-q.x, q.y));
        case 8: {
            const f32 n = std::max(3.0f, sh.points);
            const f32 r = m * (0.72f + 0.28f * std::cos(n * std::atan2(q.y, q.x)));
            return (Vec2{q.x / st.x, q.y / st.y}.length() - r) * mst * 0.8f;
        }
        case 9: {
            const f32 shaft = sd_round_box(q - Vec2{-half.x * 0.25f, 0.0f}, Vec2{half.x * 0.75f, half.y * 0.22f}, 0.0f);
            const f32 head = sd_triangle(q, Vec2{half.x * 0.1f, -half.y}, Vec2{half.x, 0.0f}, Vec2{half.x * 0.1f, half.y});
            return std::min(shaft, head);
        }
        case 10: return sd_triangle(q, Vec2{-half.x, -half.y}, Vec2{-half.x, half.y}, Vec2{half.x, half.y});
        default: return sd_round_box(q, half, 0.0f);
    }
}

// -----------------------------------------------------------------------------
// Fontes
// -----------------------------------------------------------------------------

bool image_grid(const ImagePixels& px, Grid& g) {
    if (px.width == 0 || px.height == 0 || px.rgba.size() < static_cast<usize>(px.width) * px.height * 4) return false;
    const f32 cell = std::ceil(cell_for(static_cast<f32>(px.width), static_cast<f32>(px.height)));
    const u32 step = std::max(1u, static_cast<u32>(cell));
    g.cell = static_cast<f32>(step);
    g.w = (px.width + step - 1) / step;
    g.h = (px.height + step - 1) / step;
    g.origin = Vec2{0, 0};
    g.on.assign(static_cast<usize>(g.w) * g.h, 0);
    for (u32 y = 0; y < g.h; ++y) {
        for (u32 x = 0; x < g.w; ++x) {
            const u32 sx = std::min(px.width - 1, x * step + step / 2), sy = std::min(px.height - 1, y * step + step / 2);
            g.on[static_cast<usize>(y) * g.w + x] = px.rgba[(static_cast<usize>(sy) * px.width + sx) * 4 + 3] > 127 ? 1 : 0;
        }
    }
    return true;
}

bool text_grid(const TextData& t, Grid& g) {
    const auto font = text::FontManager::instance().font_for(t);
    if (!font || t.content.empty()) return false;
    const text::TextExtent ext = text::measure(*font, t);
    // Densidade: até ~512×512 células (1 px da camada quando cabe).
    const f32 scale = std::clamp(1.0f / cell_for(ext.width + 8.0f, ext.height + 8.0f), 0.125f, 1.0f);
    text::TextRaster r;
    if (!text::rasterize(*font, t, scale, r) || r.width == 0 || r.height == 0) return false;
    g.w = r.width;
    g.h = r.height;
    g.cell = 1.0f / scale;
    g.origin = Vec2{0, 0};   // px da textura / escala = px da camada (a margem do raster é a da âncora)
    g.on.assign(static_cast<usize>(g.w) * g.h, 0);
    for (usize i = 0; i < g.on.size(); ++i) g.on[i] = r.rgba[i * 4 + 3] > 127 ? 1 : 0;
    return true;
}

bool text_contours(const TextData& t, std::vector<std::vector<Vec2>>& lines) {
    const auto font = text::FontManager::instance().font_for(t);
    if (!font || t.content.empty()) return false;
    if (!text::outline(*font, t, lines)) return false;
    // O contorno sai sem a margem do raster; a camada de texto conta a margem
    // (a âncora do recenter_text): soma a mesma margem.
    const f32 pad = (t.strokeWidth > 0.0f && t.strokeColor.w > 0.0f) ? t.strokeWidth + 2.0f : 2.0f;
    for (auto& c : lines) for (Vec2& p : c) p = p + Vec2{pad, pad};
    return true;
}

bool shape_grid(const ShapeData& sh, Grid& g) {
    const f32 w = std::max(1.0f, sh.bounds.w), h = std::max(1.0f, sh.bounds.h);
    g.cell = cell_for(w, h);
    g.w = static_cast<u32>(std::ceil(w / g.cell));
    g.h = static_cast<u32>(std::ceil(h / g.cell));
    g.origin = Vec2{0, 0};
    g.on.assign(static_cast<usize>(g.w) * g.h, 0);
    const bool fill = sh.filled && sh.fillColor.w > 0.0f;
    const f32 stroke = sh.strokeWidth > 0.0f && sh.strokeColor.w > 0.0f ? sh.strokeWidth : 0.0f;
    const Vec2 half{w * 0.5f - stroke * 0.5f, h * 0.5f - stroke * 0.5f};
    for (u32 y = 0; y < g.h; ++y) {
        for (u32 x = 0; x < g.w; ++x) {
            const Vec2 q{(static_cast<f32>(x) + 0.5f) * g.cell - w * 0.5f, (static_cast<f32>(y) + 0.5f) * g.cell - h * 0.5f};
            const f32 d = shape_sd(sh, q, Vec2{std::max(half.x, 0.5f), std::max(half.y, 0.5f)});
            const bool in = (fill && d <= 0.0f) || (stroke > 0.0f && std::fabs(d) <= stroke * 0.5f);
            g.on[static_cast<usize>(y) * g.w + x] = in ? 1 : 0;
        }
    }
    return true;
}

/// Camada vetorial: os triângulos da MESMA malha do render, rasterizados na
/// grade (só o miolo — distância à borda > 0; a franja do antisserrilhado
/// fica de fora).
bool vector_grid(const Layer& src, f64 frame, Grid& g) {
    const VectorData& vd = src.shape.vector;
    std::vector<VectorGroup> ev;
    ev.reserve(vd.groups.size());
    for (u32 gi = 0; gi < vd.groups.size(); ++gi) ev.push_back(vector::evaluate_group(vd.groups[gi], src.tracks, gi, frame));
    vector::VectorMesh mesh;
    vector::build_mesh(ev, frame, 1.0f, mesh);
    if (mesh.verts.empty()) return false;
    const Vec2 mn = mesh.min, mx = mesh.max;
    const f32 w = std::max(1.0f, mx.x - mn.x), h = std::max(1.0f, mx.y - mn.y);
    g.cell = cell_for(w, h);
    g.w = static_cast<u32>(std::ceil(w / g.cell));
    g.h = static_cast<u32>(std::ceil(h / g.cell));
    g.origin = mn;
    g.on.assign(static_cast<usize>(g.w) * g.h, 0);
    for (usize v = 0; v + 5 < mesh.verts.size(); v += 6) {
        const Vec4 A = mesh.verts[v], B = mesh.verts[v + 2], C = mesh.verts[v + 4];
        const f32 dA = mesh.verts[v + 1].x, dB = mesh.verts[v + 3].x, dC = mesh.verts[v + 5].x;
        const f32 x0 = std::min({A.x, B.x, C.x}), x1 = std::max({A.x, B.x, C.x});
        const f32 y0 = std::min({A.y, B.y, C.y}), y1 = std::max({A.y, B.y, C.y});
        const i32 gx0 = std::max(0, static_cast<i32>(std::floor((x0 - mn.x) / g.cell)));
        const i32 gx1 = std::min(static_cast<i32>(g.w) - 1, static_cast<i32>(std::floor((x1 - mn.x) / g.cell)));
        const i32 gy0 = std::max(0, static_cast<i32>(std::floor((y0 - mn.y) / g.cell)));
        const i32 gy1 = std::min(static_cast<i32>(g.h) - 1, static_cast<i32>(std::floor((y1 - mn.y) / g.cell)));
        const f32 den = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
        if (std::fabs(den) < 1e-9f) continue;
        for (i32 y = gy0; y <= gy1; ++y) {
            for (i32 x = gx0; x <= gx1; ++x) {
                const f32 px = mn.x + (static_cast<f32>(x) + 0.5f) * g.cell, py = mn.y + (static_cast<f32>(y) + 0.5f) * g.cell;
                const f32 a = ((B.y - C.y) * (px - C.x) + (C.x - B.x) * (py - C.y)) / den;
                const f32 b = ((C.y - A.y) * (px - C.x) + (A.x - C.x) * (py - C.y)) / den;
                const f32 c = 1.0f - a - b;
                if (a < -1e-4f || b < -1e-4f || c < -1e-4f) continue;
                if (a * dA + b * dB + c * dC <= 0.0f) continue;   // franja do AA
                g.on[static_cast<usize>(y) * g.w + x] = 1;
            }
        }
    }
    return true;
}

/// A 1ª máscara ativa da camada, achatada (px da camada).
bool mask_contour(const Layer& src, f64 local, std::vector<std::vector<Vec2>>& lines, std::vector<bool>& closed) {
    for (const Mask& m : src.masks) {
        if (m.points.size() < 2 && m.pathKeys.empty()) continue;
        std::vector<MaskPoint> pts;
        mask::evaluate_path(m, local, pts);
        if (pts.size() < 2) continue;
        std::vector<Vec4> edges;
        mask::flatten(pts, m.closed, 0.25f, Vec2{0, 0}, edges);
        if (edges.empty()) continue;
        std::vector<Vec2> c;
        c.reserve(edges.size() + 1);
        c.push_back(Vec2{edges[0].x, edges[0].y});
        for (const Vec4& e : edges) c.push_back(Vec2{e.z, e.w});
        if (m.closed && c.size() > 2) c.pop_back();   // a aresta de volta já fecha
        lines.push_back(std::move(c));
        closed.push_back(m.closed);
        return true;
    }
    return false;
}

/// Pontos de um modelo 3D no espaço do MODELO (pose de repouso; skin e morph
/// não entram — a distribuição é a da malha parada).
bool model_points(const scene3d::SceneAsset& A, u32 emitFrom, u64 seed, StaticData& out) {
    const std::vector<Mat4> world = A.rest_world_matrices();
    std::vector<Vec3> tri;   // 3 por triângulo
    std::vector<Vec3> verts;
    for (usize ni = 0; ni < A.nodes.size() && ni < world.size(); ++ni) {
        const i32 mi = A.nodes[ni].mesh;
        if (mi < 0 || mi >= static_cast<i32>(A.meshes.size())) continue;
        for (const scene3d::Primitive& pr : A.meshes[static_cast<usize>(mi)].primitives) {
            if (emitFrom == 0) {
                for (const Vec3& v : pr.positions) verts.push_back(xform_point(world[ni], v));
                continue;
            }
            for (usize i = 0; i + 2 < pr.indices.size(); i += 3) {
                const u32 a = pr.indices[i], b = pr.indices[i + 1], c = pr.indices[i + 2];
                if (a >= pr.positions.size() || b >= pr.positions.size() || c >= pr.positions.size()) continue;
                tri.push_back(xform_point(world[ni], pr.positions[a]));
                tri.push_back(xform_point(world[ni], pr.positions[b]));
                tri.push_back(xform_point(world[ni], pr.positions[c]));
            }
        }
    }
    out.points.clear();
    out.pointMode = 0;
    out.cellSize = 0.0f;
    if (emitFrom == 0) {
        for (const Vec3& v : verts) out.points.push_back(Vec4{v.x, v.y, v.z, 0});
        thin(out.points, kMaxEmitPoints);
        return !out.points.empty();
    }
    const usize nt = tri.size() / 3;
    if (nt == 0) return false;
    // Superfície: por ÁREA; arestas: por COMPRIMENTO. A tabela acumulada e a
    // busca binária dão a distribuição exata sem rejeição.
    std::vector<f64> cdf;
    const bool edges = emitFrom >= 2;
    cdf.reserve(edges ? nt * 3 : nt);
    f64 acc = 0.0;
    for (usize t = 0; t < nt; ++t) {
        const Vec3 A0 = tri[t * 3], B0 = tri[t * 3 + 1], C0 = tri[t * 3 + 2];
        if (edges) {
            acc += (B0 - A0).length(); cdf.push_back(acc);
            acc += (C0 - B0).length(); cdf.push_back(acc);
            acc += (A0 - C0).length(); cdf.push_back(acc);
        } else {
            acc += 0.5 * (B0 - A0).cross(C0 - A0).length();
            cdf.push_back(acc);
        }
    }
    if (acc <= 0.0) return false;
    Rng rng{seed ^ 0xA5A5A5A5ull};
    const u32 n = static_cast<u32>(std::min<usize>(kMaxEmitPoints, std::max<usize>(8192, nt * 4)));
    out.points.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        const f64 r = static_cast<f64>(rng.unit()) * acc;
        const usize k = static_cast<usize>(std::lower_bound(cdf.begin(), cdf.end(), r) - cdf.begin());
        const usize idx = std::min(k, cdf.size() - 1);
        Vec3 p;
        if (edges) {
            const usize t = idx / 3, e = idx % 3;
            const Vec3 a = tri[t * 3 + e], b = tri[t * 3 + (e + 1) % 3];
            p = a + (b - a) * rng.unit();
        } else {
            f32 u = rng.unit(), v = rng.unit();
            if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
            const Vec3 a = tri[idx * 3], b = tri[idx * 3 + 1], c = tri[idx * 3 + 2];
            p = a + (b - a) * u + (c - a) * v;
        }
        out.points.push_back(Vec4{p.x, p.y, p.z, 0});
    }
    return true;
}

u32 pack_rgba8(Vec4 c) noexcept {
    auto q = [](f32 v) { return static_cast<u32>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return q(c.x) | (q(c.y) << 8) | (q(c.z) << 16) | (q(c.w) << 24);
}

/// A malha da PARTÍCULA de malha: o modelo inteiro na pose de repouso, no
/// espaço da camada (Y para baixo), centrado e com o maior lado = 1 — o
/// "tamanho" da partícula passa a ser o tamanho da malha em px.
bool model_mesh(const scene3d::SceneAsset& A, StaticData& out) {
    const std::vector<Mat4> world = A.rest_world_matrices();
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    u64 totalTris = 0;
    for (usize ni = 0; ni < A.nodes.size() && ni < world.size(); ++ni) {
        const i32 mi = A.nodes[ni].mesh;
        if (mi < 0 || mi >= static_cast<i32>(A.meshes.size())) continue;
        for (const scene3d::Primitive& pr : A.meshes[static_cast<usize>(mi)].primitives) totalTris += pr.triangle_count();
    }
    if (totalTris == 0) return false;
    // Orçamento: cada primitiva escolhe o LOD mais fino que caiba na parte dela.
    const f64 share = std::min(1.0, static_cast<f64>(kMaxMeshTriangles) / static_cast<f64>(totalTris));
    std::vector<Vec3> P, N;
    std::vector<u32> C;
    for (usize ni = 0; ni < A.nodes.size() && ni < world.size(); ++ni) {
        const i32 mi = A.nodes[ni].mesh;
        if (mi < 0 || mi >= static_cast<i32>(A.meshes.size())) continue;
        const Mat4 M = flip * world[ni];
        for (const scene3d::Primitive& pr : A.meshes[static_cast<usize>(mi)].primitives) {
            const u32 budget = std::max<u32>(1, static_cast<u32>(std::floor(static_cast<f64>(pr.triangle_count()) * share)));
            const std::vector<u32>* idx = &pr.indices;
            for (const auto& lod : pr.lods) {
                if (idx->size() / 3 <= budget) break;
                if (!lod.empty()) idx = &lod;
            }
            Vec4 base{1, 1, 1, 1};
            if (pr.material >= 0 && pr.material < static_cast<i32>(A.materials.size())) base = A.materials[static_cast<usize>(pr.material)].baseColor;
            const usize tris = idx->size() / 3;
            // Ainda acima do orçamento: amostra uniforme de triângulos (passo fixo).
            const f64 stride = tris > budget ? static_cast<f64>(tris) / static_cast<f64>(budget) : 1.0;
            for (f64 tf = 0.0; tf < static_cast<f64>(tris); tf += stride) {
                const usize t = static_cast<usize>(tf);
                for (u32 k = 0; k < 3; ++k) {
                    const u32 vi = (*idx)[t * 3 + k];
                    if (vi >= pr.positions.size()) continue;
                    P.push_back(xform_point(M, pr.positions[vi]));
                    Vec3 n = vi < pr.normals.size() ? xform_dir(M, pr.normals[vi]) : Vec3{0, 0, -1};
                    const f32 L = n.length();
                    N.push_back(L > 1e-6f ? n * (1.0f / L) : Vec3{0, 0, -1});
                    Vec4 c = base;
                    if (vi < pr.colors.size()) {
                        const u32 vc = pr.colors[vi];
                        c = Vec4{c.x * static_cast<f32>(vc & 255) / 255.0f, c.y * static_cast<f32>((vc >> 8) & 255) / 255.0f,
                                 c.z * static_cast<f32>((vc >> 16) & 255) / 255.0f, c.w * static_cast<f32>((vc >> 24) & 255) / 255.0f};
                    }
                    C.push_back(pack_rgba8(c));
                }
                if (P.size() % 3 != 0) { P.resize(P.size() / 3 * 3); N.resize(P.size()); C.resize(P.size()); }
            }
        }
    }
    if (P.size() < 3) return false;
    scene3d::Aabb box;
    for (const Vec3& p : P) box.add(p);
    const Vec3 ctr = box.center(), ext = box.extent();
    const f32 big = std::max({ext.x, ext.y, ext.z, 1e-6f});
    out.mesh.clear();
    out.mesh.reserve(P.size() * 2);
    for (usize i = 0; i < P.size(); ++i) {
        const Vec3 p = (P[i] - ctr) * (1.0f / big);
        f32 packed;
        std::memcpy(&packed, &C[i], 4);
        out.mesh.push_back(Vec4{p.x, p.y, p.z, 0});
        out.mesh.push_back(Vec4{N[i].x, N[i].y, N[i].z, packed});
    }
    return true;
}

/// Os tipos de emissor que leem os pontos da fonte.
bool sourced_emitter(u32 type) noexcept {
    return type >= static_cast<u32>(ParticleEmitter::Layer) && type <= static_cast<u32>(ParticleEmitter::Mesh);
}

/// Monta os pontos de emissão de `src`. Cada tipo de emissor tem a sua leitura
/// da fonte; quando a fonte não oferece aquilo (emissor de texto numa imagem,
/// caminho numa camada sem máscara), cai na leitura que ela oferece — pixels.
bool build_points(const BuildContext& ctx, const Layer& src, u32 type, u32 emitFrom, u64 key, StaticData& out) {
    const FrameIndex local = src.local_time(ctx.time);
    const f64 lf = static_cast<f64>(local.value);
    const auto E = static_cast<ParticleEmitter>(type);
    const bool edges = emitFrom >= 2;

    // Malha: vértices / superfície (por área) / arestas (por comprimento).
    if (src.kind == LayerKind::Model3D) {
        auto asset = ctx.modelLookup ? ctx.modelLookup(ctx.modelCtx, src.model.scene) : nullptr;
        return asset && model_points(*asset, emitFrom, key, out);
    }
    // Caminho: a 1ª máscara, ou o caminho-guia do vetor, ou o contorno do texto.
    if (E == ParticleEmitter::Path) {
        std::vector<std::vector<Vec2>> lines;
        std::vector<bool> closed;
        bool ok = mask_contour(src, lf, lines, closed);
        if (!ok && src.kind == LayerKind::Shape && src.shape.shapeType == kShapeVector) {
            vector::Contour c;
            if (vector::guide_contour(src.shape.vector, src.tracks, lf, c) && c.pts.size() >= 2) {
                lines.push_back(c.pts);
                closed.push_back(c.closed);
                ok = true;
            }
        }
        if (!ok && src.kind == LayerKind::Text && text_contours(src.text, lines)) {
            closed.assign(lines.size(), true);
            ok = true;
        }
        if (ok) {
            contour_points(lines, closed, emitFrom == 0, out);
            return !out.points.empty();
        }
        // Sem caminho: a borda dos pixels da camada é o caminho que ela tem.
    }
    // Texto: superfície pelos glifos rasterizados; borda e vértices pelo
    // contorno vetorial (linhas nítidas, espaçamento igual).
    if (src.kind == LayerKind::Text) {
        if (E == ParticleEmitter::Text && emitFrom != 1) {
            std::vector<std::vector<Vec2>> lines;
            if (text_contours(src.text, lines)) {
                const std::vector<bool> closed(lines.size(), true);
                contour_points(lines, closed, emitFrom == 0, out);
                return !out.points.empty();
            }
        }
        Grid g;
        if (!text_grid(src.text, g)) return false;
        grid_points(g, edges || E == ParticleEmitter::Path, out);
        return !out.points.empty();
    }
    Grid g;
    bool ok = false;
    if (src.kind == LayerKind::Image) {
        const ImagePixels* px = ctx.imageLookup ? ctx.imageLookup(ctx.imageCtx, src.source) : nullptr;
        ok = px && image_grid(*px, g);
    } else if (src.kind == LayerKind::Shape) {
        ok = src.shape.shapeType == kShapeVector ? vector_grid(src, lf, g) : shape_grid(src.shape, g);
    }
    if (!ok) return false;
    grid_points(g, edges || E == ParticleEmitter::Path, out);
    return !out.points.empty();
}

/// Curvas de tamanho/opacidade: pontos ordenados pela posição (a UI pode
/// mandar fora de ordem; o shader assume crescente).
template <typename T>
u32 sorted_copy(const T* in, u32 n, T* out) {
    n = std::min(n, ParticleData::kMaxLifeStops);
    for (u32 i = 0; i < n; ++i) out[i] = in[i];
    std::sort(out, out + n, [](const T& a, const T& b) { return a.x < b.x; });
    return n;
}

} // namespace

// =============================================================================
// Chave de conteúdo
// =============================================================================
u64 source_key(const Layer& src, u32 emitterType, u32 emitFrom, FrameIndex localTime) noexcept {
    u64 h = mix64(0x51A7E5ull, static_cast<u64>(src.kind));
    h = mix64(h, emitterType);
    h = mix64(h, emitFrom);
    switch (src.kind) {
        case LayerKind::Image:
            h = mix64(h, src.source.pack());
            break;
        case LayerKind::Model3D:
            h = mix64(h, src.model.scene.pack());
            break;
        case LayerKind::Text:
            h = mix64(h, text::raster_key(src.text, 1.0f));
            break;
        case LayerKind::Shape: {
            const ShapeData& s = src.shape;
            h = mix64(h, s.shapeType);
            if (s.shapeType == kShapeVector) {
                const f64 lf = static_cast<f64>(localTime.value);
                std::vector<VectorGroup> ev;
                for (u32 gi = 0; gi < s.vector.groups.size(); ++gi) ev.push_back(vector::evaluate_group(s.vector.groups[gi], src.tracks, gi, lf));
                h = mix64(h, vector::content_hash(ev, lf));
            } else {
                h = mixf(mixf(mixf(mixf(h, s.bounds.w), s.bounds.h), s.cornerRadius), s.points);
                h = mixf(mixf(mixf(h, s.innerRadius), s.strokeWidth), s.strokeColor.w);
                h = mixf(mix64(h, s.filled ? 1u : 0u), s.fillColor.w);
            }
            break;
        }
        default:
            return 0;   // vídeo, pré-composição, nulo…: sem pixels na CPU
    }
    // Caminho: a forma da 1ª máscara no instante (máscara animada = pontos novos).
    if (emitterType == static_cast<u32>(ParticleEmitter::Path)) {
        for (const Mask& m : src.masks) {
            std::vector<MaskPoint> pts;
            mask::evaluate_path(m, static_cast<f64>(localTime.value), pts);
            if (pts.size() < 2) continue;
            h = mix64(h, m.closed ? 7u : 3u);
            for (const MaskPoint& p : pts) {
                h = mixf(mixf(h, p.position.x), p.position.y);
                h = mixf(mixf(mixf(mixf(h, p.inTangent.x), p.inTangent.y), p.outTangent.x), p.outTangent.y);
            }
            break;
        }
    }
    return h == 0 ? 1 : h;
}

// =============================================================================
// Cache do estático
// =============================================================================
std::shared_ptr<const StaticData> StaticCache::find(u64 key, u64 frameNumber) noexcept {
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    it->second.lastFrame = frameNumber;
    ++hits_;
    return it->second.data;
}

void StaticCache::put(std::shared_ptr<const StaticData> data, u64 frameNumber) noexcept {
    if (!data) return;
    ++builds_;
    entries_[data->key] = Entry{std::move(data), frameNumber};
}

void StaticCache::collect(u64 frameNumber, u64 age) noexcept {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (frameNumber > it->second.lastFrame + age) it = entries_.erase(it);
        else ++it;
    }
}

u64 StaticCache::resident_bytes() const noexcept {
    u64 b = 0;
    for (const auto& [k, e] : entries_) if (e.data) b += e.data->bytes();
    return b;
}

// =============================================================================
// O quadro
// =============================================================================
void build_frame(const BuildContext& ctx, const Layer& pl, const ParticleData& pd, StaticCache& cache,
                 FrameData& out) noexcept {
    for (Vec4& v : out.header) v = Vec4{};
    out.statics.reset();
    out.meshMode = false;
    out.meshVertices = 0;
    out.texture = AssetId{};
    const Composition* comp = ctx.comp;

    Vec4* H = out.header;
    u32 flags = 0;
    // --- Aleatórios, rastro, malha ------------------------------------------
    H[2] = Vec4{std::clamp(pd.sizeRandom, 0.0f, 1.0f), std::clamp(pd.opacityRandom, 0.0f, 1.0f),
                std::clamp(pd.colorRandom, 0.0f, 1.0f), std::clamp(pd.auxProbability, 0.0f, 1.0f)};
    H[3] = Vec4{std::max(0.0f, pd.trailWidth), std::clamp(pd.trailOpacity, 0.0f, 1.0f), std::max(0.001f, pd.meshScale),
                pd.meshLit ? 1.0f : 0.0f};
    // --- Colisão esfera/caixa -------------------------------------------------
    // Centro a partir do CENTRO DO EMISSOR (px da camada), X/Y/Z: X e Z do
    // `collisionCenter`, Y do mesmo controle da altura do plano (CollisionY) —
    // na esfera/caixa ele é o deslocamento vertical do centro.
    {
        const f32 lw = comp ? static_cast<f32>(comp->width()) : 0.0f, lh = comp ? static_cast<f32>(comp->height()) : 0.0f;
        const Vec2 emitter{lw * 0.5f + pd.emitterOffset.x, lh * 0.5f + pd.emitterOffset.y};
        H[4] = Vec4{emitter.x + pd.collisionCenter.x, emitter.y + pd.collisionY, pd.collisionCenter.z, std::max(0.0f, pd.collisionRadius)};
        H[5] = Vec4{std::max(0.0f, pd.collisionBox.x) * 0.5f, std::max(0.0f, pd.collisionBox.y) * 0.5f,
                    std::max(0.0f, pd.collisionBox.z) * 0.5f, std::clamp(pd.collisionBounce, 0.0f, 1.0f)};
    }
    // --- Curvas ao longo da vida ---------------------------------------------
    {
        Vec4 cs[ParticleData::kMaxLifeStops];
        Vec2 sc[ParticleData::kMaxLifeStops], oc[ParticleData::kMaxLifeStops];
        const u32 nc = sorted_copy(pd.colorStops, pd.colorStopCount, cs);
        const u32 ns = sorted_copy(pd.sizeCurve, pd.sizeCurveCount, sc);
        const u32 no = sorted_copy(pd.opacityCurve, pd.opacityCurveCount, oc);
        H[10] = Vec4{static_cast<f32>(nc), static_cast<f32>(ns), static_cast<f32>(no), 0};
        for (u32 i = 0; i < nc; ++i) H[11 + i] = Vec4{std::clamp(cs[i].x, 0.0f, 1.0f), srgb_lin(cs[i].y), srgb_lin(cs[i].z), srgb_lin(cs[i].w)};
        for (u32 i = 0; i < ns; ++i) H[19 + i] = Vec4{std::clamp(sc[i].x, 0.0f, 1.0f), std::max(0.0f, sc[i].y), 0, 0};
        for (u32 i = 0; i < no; ++i) H[27 + i] = Vec4{std::clamp(oc[i].x, 0.0f, 1.0f), std::clamp(oc[i].y, 0.0f, 1.0f), 0, 0};
    }
    // Matriz da fonte: identidade até haver fonte.
    H[6] = Vec4{1, 0, 0, 0}; H[7] = Vec4{0, 1, 0, 0}; H[8] = Vec4{0, 0, 1, 0}; H[9] = Vec4{0, 0, 0, 1};

    u64 staticKey = 0;
    bool hasStatic = false;

    // --- Emissor de camada/texto/caminho/malha -------------------------------
    const Layer* src = (comp && pd.emitterSource != 0 && sourced_emitter(pd.emitterType))
                           ? comp->layer(LayerId::unpack(pd.emitterSource)) : nullptr;
    if (src == &pl) src = nullptr;
    std::shared_ptr<const StaticData> points;
    if (src) {
        const u64 key = source_key(*src, pd.emitterType, pd.emitFrom, src->local_time(ctx.time));
        if (key != 0) {
            points = cache.find(key, ctx.frameNumber);
            if (!points) {
                auto built = std::make_shared<StaticData>();
                built->key = key;
                (void)build_points(ctx, *src, pd.emitterType, pd.emitFrom, key, *built);   // vazio também é resposta (não refaz)
                cache.put(built, ctx.frameNumber);
                points = std::move(built);
            }
        }
        if (points && !points->points.empty()) {
            // px da camada de partículas ← espaço da fonte, no instante. A
            // fonte 3D (modelo, camada 3D) sai projetada pela câmera.
            Mat4 compFromSrc = layer_comp_matrix(*comp, *src, ctx.time);
            if (src->kind == LayerKind::Model3D) compFromSrc = compFromSrc * layer_from_model(src->model);
            const Mat4 M = inverse4(layer_comp_matrix(*comp, pl, ctx.time)) * compFromSrc;
            H[6] = M.col[0]; H[7] = M.col[1]; H[8] = M.col[2]; H[9] = M.col[3];
            flags |= kFlagSourcePoints;
            staticKey = mix64(staticKey, points->key);
            hasStatic = true;
        }
    }
    // --- Partícula de malha ----------------------------------------------------
    std::shared_ptr<const StaticData> mesh;
    if (pd.particleType == static_cast<u32>(ParticleShape::Mesh) && comp && pd.meshSource != 0) {
        const Layer* ml = comp->layer(LayerId::unpack(pd.meshSource));
        if (ml && ml->kind == LayerKind::Model3D) {
            const u64 key = mix64(0x3E5Bull, ml->model.scene.pack());
            mesh = cache.find(key, ctx.frameNumber);
            if (!mesh) {
                auto built = std::make_shared<StaticData>();
                built->key = key;
                auto asset = ctx.modelLookup ? ctx.modelLookup(ctx.modelCtx, ml->model.scene) : nullptr;
                if (asset) (void)model_mesh(*asset, *built);
                if (asset) cache.put(built, ctx.frameNumber);   // sem asset (ainda carregando): tenta de novo
                mesh = std::move(built);
            }
            if (mesh && mesh->mesh_vertices() >= 3) {
                flags |= kFlagMesh;
                out.meshMode = true;
                out.meshVertices = mesh->mesh_vertices();
                staticKey = mix64(staticKey, mesh->key);
                hasStatic = true;
            }
        }
    }
    // --- Partícula de textura ------------------------------------------------
    if (pd.particleType == static_cast<u32>(ParticleShape::Texture) && pd.textureAsset != 0 && ctx.imageLookup) {
        const AssetId a = AssetId::unpack(pd.textureAsset);
        const ImagePixels* px = ctx.imageLookup(ctx.imageCtx, a);
        if (px && px->width > 0 && px->height > 0) {
            out.texture = a;
            flags |= kFlagTexture;
        }
    }

    // O estático do quadro: pontos + malha juntos (um buffer só por camada).
    if (hasStatic) {
        if ((points && (flags & kFlagSourcePoints)) && !(flags & kFlagMesh)) {
            out.statics = points;
        } else if (!(flags & kFlagSourcePoints) && mesh) {
            out.statics = mesh;
        } else {
            // Os dois: junta numa cópia em cache (a chave combina as duas).
            auto both = cache.find(staticKey, ctx.frameNumber);
            if (!both) {
                auto static_part = std::make_shared<StaticData>();
                static_part->key = staticKey;
                static_part->points = points->points;
                static_part->pointMode = points->pointMode;
                static_part->cellSize = points->cellSize;
                static_part->mesh = mesh->mesh;
                cache.put(static_part, ctx.frameNumber);
                both = static_part;
            }
            out.statics = std::move(both);
        }
    }
    // Índices absolutos no buffer da camada: [kRing cabeçalhos][pontos][malha].
    const u32 staticBase = kRing * kHeaderVec4;
    if (out.statics && (flags & kFlagSourcePoints)) {
        H[0] = Vec4{static_cast<f32>(out.statics->points.size()), static_cast<f32>(staticBase),
                    static_cast<f32>(out.statics->pointMode), out.statics->cellSize};
    }
    // A malha vem logo depois dos pontos (sem pontos, o estático é a própria
    // malha e ela começa no início do estático).
    if (out.statics && (flags & kFlagMesh)) {
        H[1].x = static_cast<f32>(out.statics->mesh_vertices());
        H[1].y = static_cast<f32>(staticBase + out.statics->points.size());
    }
    H[1].z = static_cast<f32>(flags);
}

// =============================================================================
// GPU
// =============================================================================
BufferHandle GpuBuffers::upload(GPUBackend& backend, const FrameData& fd, u64 frameNumber, u32& headerBase) noexcept {
    Entry& e = entries_[fd.layerKey];
    const u64 skey = fd.statics ? fd.statics->key : 0;
    const usize staticVec4 = fd.statics ? fd.statics->points.size() + fd.statics->mesh.size() : 0;
    const u64 need = (static_cast<u64>(kRing) * kHeaderVec4 + staticVec4) * sizeof(Vec4);
    const bool rebuild = !e.buffer.valid() || e.staticKey != skey || e.bytes < need;
    if (rebuild) {
        // Buffer NOVO quando o estático muda: um quadro em voo pode estar lendo
        // o velho (o backend só o destrói depois da GPU terminar).
        if (e.buffer.valid()) backend.destroy_buffer(e.buffer);
        e = Entry{};
        BufferDesc bd;
        bd.bytes = need;
        bd.usage = BufferUsage::Storage;
        bd.access = MemoryAccess::Upload;
        bd.debugName = "particular-dados";
        auto b = backend.create_buffer(bd);
        if (!b.ok()) { entries_.erase(fd.layerKey); return BufferHandle{}; }
        e.buffer = *b;
        e.bytes = need;
        e.staticKey = skey;
    }
    void* ptr = nullptr;
    if (!backend.map_buffer(e.buffer, ptr).ok() || !ptr) return BufferHandle{};
    auto* dst = static_cast<Vec4*>(ptr);
    e.ring = (e.ring + 1) % kRing;
    headerBase = e.ring * kHeaderVec4;
    std::memcpy(dst + headerBase, fd.header, sizeof(fd.header));
    if (rebuild) {
        // Todos os cabeçalhos do anel com o do quadro (nenhum lixo em cópia
        // nunca escrita) e o estático uma vez.
        for (u32 r = 0; r < kRing; ++r) std::memcpy(dst + r * kHeaderVec4, fd.header, sizeof(fd.header));
        if (fd.statics) {
            Vec4* s = dst + static_cast<usize>(kRing) * kHeaderVec4;
            if (!fd.statics->points.empty()) std::memcpy(s, fd.statics->points.data(), fd.statics->points.size() * sizeof(Vec4));
            if (!fd.statics->mesh.empty())
                std::memcpy(s + fd.statics->points.size(), fd.statics->mesh.data(), fd.statics->mesh.size() * sizeof(Vec4));
        }
    }
    backend.unmap_buffer(e.buffer);
    e.lastFrame = frameNumber;
    return e.buffer;
}

void GpuBuffers::collect(GPUBackend& backend, u64 frameNumber, u64 age) noexcept {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (frameNumber > it->second.lastFrame + age) {
            if (it->second.buffer.valid()) backend.destroy_buffer(it->second.buffer);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void GpuBuffers::release(GPUBackend& backend) noexcept {
    for (auto& [k, e] : entries_) if (e.buffer.valid()) backend.destroy_buffer(e.buffer);
    entries_.clear();
}

u64 GpuBuffers::resident_bytes() const noexcept {
    u64 b = 0;
    for (const auto& [k, e] : entries_) b += e.bytes;
    return b;
}

} // namespace aurea::particles
