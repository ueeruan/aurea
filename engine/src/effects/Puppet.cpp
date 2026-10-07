// =============================================================================
//  Aurea / effects / Puppet.cpp
//
//  Malha e ARAP do Fantoche (effects/Puppet.hpp). Igarashi 2005, os dois
//  passos lineares:
//    1. similaridade: cada vértice de um triângulo é escrito nas coordenadas
//       locais (x, y) da aresta oposta no repouso; o erro de cada triângulo
//       só cresce se ele deixar de ser uma cópia girada/escalada de si.
//    2. ajuste de escala: cada triângulo do passo 1 vira uma cópia RÍGIDA
//       (escala 1) do repouso, e as arestas da malha seguem essas cópias.
//  Os pinos entram como restrições de peso alto (o ponto do pino é a soma
//  baricêntrica dos 3 vértices do triângulo dele no repouso). Mínimos
//  quadrados resolvidos por gradiente conjugado (Jacobi) nas equações
//  normais — esparso, sem montar AᵀA.
// =============================================================================
#include "aurea/effects/Puppet.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::puppet {
namespace {

struct Entry { u32 col; f64 val; };
struct Row { Entry e[6]; u32 n = 0; f64 rhs = 0.0; };

/// min ‖A x − b‖² (x tem `cols` incógnitas, começa em `x`).
void solve_lsq(const std::vector<Row>& rows, u32 cols, std::vector<f64>& x) {
    std::vector<f64> diag(cols, 0.0);
    for (const Row& r : rows) for (u32 k = 0; k < r.n; ++k) diag[r.e[k].col] += r.e[k].val * r.e[k].val;
    for (f64& d : diag) d = d > 1e-12 ? 1.0 / d : 1.0;
    std::vector<f64> ax(rows.size());
    auto normal = [&](const std::vector<f64>& v, std::vector<f64>& out) {   // out = Aᵀ A v
        for (usize i = 0; i < rows.size(); ++i) {
            f64 s = 0.0;
            for (u32 k = 0; k < rows[i].n; ++k) s += rows[i].e[k].val * v[rows[i].e[k].col];
            ax[i] = s;
        }
        std::fill(out.begin(), out.end(), 0.0);
        for (usize i = 0; i < rows.size(); ++i)
            for (u32 k = 0; k < rows[i].n; ++k) out[rows[i].e[k].col] += rows[i].e[k].val * ax[i];
    };
    std::vector<f64> r(cols, 0.0), z(cols), p(cols), q(cols);
    for (const Row& row : rows) for (u32 k = 0; k < row.n; ++k) r[row.e[k].col] += row.e[k].val * row.rhs;   // Aᵀb
    normal(x, q);
    f64 bnorm = 0.0;
    for (u32 i = 0; i < cols; ++i) { bnorm += r[i] * r[i]; r[i] -= q[i]; }
    if (bnorm <= 0.0) bnorm = 1.0;
    for (u32 i = 0; i < cols; ++i) { z[i] = r[i] * diag[i]; p[i] = z[i]; }
    f64 rz = 0.0;
    for (u32 i = 0; i < cols; ++i) rz += r[i] * z[i];
    const u32 maxIter = std::min<u32>(2000u, cols * 4u + 50u);
    for (u32 it = 0; it < maxIter; ++it) {
        normal(p, q);
        f64 pq = 0.0;
        for (u32 i = 0; i < cols; ++i) pq += p[i] * q[i];
        if (std::fabs(pq) < 1e-300) break;
        const f64 a = rz / pq;
        f64 rr = 0.0;
        for (u32 i = 0; i < cols; ++i) { x[i] += a * p[i]; r[i] -= a * q[i]; rr += r[i] * r[i]; }
        if (rr <= 1e-20 * bnorm) break;
        f64 rz2 = 0.0;
        for (u32 i = 0; i < cols; ++i) { z[i] = r[i] * diag[i]; rz2 += r[i] * z[i]; }
        const f64 beta = rz2 / rz;
        rz = rz2;
        for (u32 i = 0; i < cols; ++i) p[i] = z[i] + beta * p[i];
    }
}

/// Baricêntricas de `p` no triângulo (a, b, c); false = degenerado.
bool barycentric(Vec2 a, Vec2 b, Vec2 c, Vec2 p, f32& u, f32& v, f32& w) noexcept {
    const f32 d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(d) < 1e-12f) return false;
    u = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / d;
    v = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / d;
    w = 1.0f - u - v;
    return true;
}

/// Triângulo da malha (pontos `pts`) que contém `p`, ou o mais perto.
i32 locate(const Mesh& m, const std::vector<Vec2>& pts, Vec2 p, f32 bc[3], bool* inside) noexcept {
    i32 best = -1;
    f32 bestScore = -1e30f;
    for (u32 t = 0; t * 3 + 2 < m.tris.size(); ++t) {
        f32 u, v, w;
        if (!barycentric(pts[m.tris[t * 3]], pts[m.tris[t * 3 + 1]], pts[m.tris[t * 3 + 2]], p, u, v, w)) continue;
        const f32 score = std::min({u, v, w});
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<i32>(t);
            bc[0] = u; bc[1] = v; bc[2] = w;
        }
    }
    if (inside) *inside = bestScore >= -1e-4f;
    return best;
}

} // namespace

void build_mesh(f32 width, f32 height, u32 triangles, f32 expansion, Mesh& out,
                const u8* occupancy, u32 ow, u32 oh) {
    out = Mesh{};
    width = std::max(1.0f, std::isfinite(width) ? width : 1.0f);
    height = std::max(1.0f, std::isfinite(height) ? height : 1.0f);
    expansion = std::clamp(std::isfinite(expansion) ? expansion : 0.0f, 0.0f, 1000.0f);
    triangles = std::clamp(triangles, 8u, 4000u);
    const f32 W = width + 2.0f * expansion, H = height + 2.0f * expansion;
    // 2·cols·rows ≈ triangles com células quase quadradas.
    const f32 cell = std::sqrt(W * H / (static_cast<f32>(triangles) * 0.5f));
    const u32 cols = std::clamp(static_cast<u32>(std::lround(W / cell)), 1u, 128u);
    const u32 rows = std::clamp(static_cast<u32>(std::lround(H / cell)), 1u, 128u);
    std::vector<i32> remap((cols + 1) * (rows + 1), -1);
    auto keep = [&](u32 c, u32 r) {
        if (!occupancy || !ow || !oh) return true;
        // Célula da malha → janela na ocupação (com uma célula de folga).
        const f32 x0 = (c * W / cols - expansion) / width, x1 = ((c + 1) * W / cols - expansion) / width;
        const f32 y0 = (r * H / rows - expansion) / height, y1 = ((r + 1) * H / rows - expansion) / height;
        const i32 ox0 = std::max(0, static_cast<i32>(std::floor(x0 * ow)) - 1), ox1 = std::min(static_cast<i32>(ow) - 1, static_cast<i32>(std::ceil(x1 * ow)));
        const i32 oy0 = std::max(0, static_cast<i32>(std::floor(y0 * oh)) - 1), oy1 = std::min(static_cast<i32>(oh) - 1, static_cast<i32>(std::ceil(y1 * oh)));
        for (i32 y = oy0; y <= oy1; ++y)
            for (i32 x = ox0; x <= ox1; ++x)
                if (occupancy[static_cast<usize>(y) * ow + x]) return true;
        return false;
    };
    auto vertex = [&](u32 c, u32 r) -> u32 {
        i32& slot = remap[r * (cols + 1) + c];
        if (slot < 0) {
            slot = static_cast<i32>(out.rest.size());
            out.rest.push_back(Vec2{c * W / cols - expansion, r * H / rows - expansion});
        }
        return static_cast<u32>(slot);
    };
    for (u32 r = 0; r < rows; ++r)
        for (u32 c = 0; c < cols; ++c) {
            if (!keep(c, r)) continue;
            const u32 a = vertex(c, r), b = vertex(c + 1, r), d = vertex(c, r + 1), e = vertex(c + 1, r + 1);
            // Diagonal alternada: a malha não tem direção preferida ao dobrar.
            if ((r + c) & 1u) out.tris.insert(out.tris.end(), {a, b, e, a, e, d});
            else out.tris.insert(out.tris.end(), {a, b, d, b, e, d});
        }
}

void deform(const Mesh& mesh, const std::vector<Pin>& pins, f32 rigidity, std::vector<Vec2>& out) {
    out = mesh.rest;
    const u32 n = static_cast<u32>(mesh.rest.size());
    if (!n || mesh.tris.empty() || pins.empty()) return;

    // Pinos: triângulo e baricêntricas no repouso.
    struct Bound { u32 v[3]; f32 b[3]; Vec2 target; };
    std::vector<Bound> bound;
    for (const Pin& p : pins) {
        f32 bc[3];
        const i32 t = locate(mesh, mesh.rest, p.rest, bc, nullptr);
        if (t < 0) continue;
        Bound b;
        for (int k = 0; k < 3; ++k) { b.v[k] = mesh.tris[static_cast<u32>(t) * 3 + k]; b.b[k] = bc[k]; }
        b.target = p.pos;
        bound.push_back(b);
    }
    if (bound.empty()) return;

    // Melhor movimento rígido dos pinos (rotação + translação) — vale sozinho
    // com 1 pino (só translação) e é o alvo da rigidez.
    Vec2 cr{}, cp{};
    for (const Pin& p : pins) { cr.x += p.rest.x; cr.y += p.rest.y; cp.x += p.pos.x; cp.y += p.pos.y; }
    cr.x /= pins.size(); cr.y /= pins.size(); cp.x /= pins.size(); cp.y /= pins.size();
    f64 sa = 0.0, sb = 0.0;
    for (const Pin& p : pins) {
        const f64 rx = p.rest.x - cr.x, ry = p.rest.y - cr.y, qx = p.pos.x - cp.x, qy = p.pos.y - cp.y;
        sa += rx * qx + ry * qy;
        sb += rx * qy - ry * qx;
    }
    const f64 ang = (std::fabs(sa) + std::fabs(sb)) > 1e-9 ? std::atan2(sb, sa) : 0.0;
    const f64 cs = std::cos(ang), sn = std::sin(ang);
    auto rigid = [&](Vec2 v) {
        const f64 x = v.x - cr.x, y = v.y - cr.y;
        return Vec2{static_cast<f32>(cs * x - sn * y + cp.x), static_cast<f32>(sn * x + cs * y + cp.y)};
    };
    std::vector<Vec2> rigidOut(n);
    for (u32 i = 0; i < n; ++i) rigidOut[i] = rigid(mesh.rest[i]);
    if (bound.size() == 1 || rigidity >= 0.999f) { out = rigidOut; return; }

    const f64 kPin = 1000.0;
    const u32 ntri = static_cast<u32>(mesh.tris.size() / 3);

    // Passo 1: similaridade (x e y acoplados). Incógnitas 2i (x), 2i+1 (y).
    std::vector<Row> rows;
    rows.reserve(ntri * 6 + bound.size() * 2);
    for (u32 t = 0; t < ntri; ++t) {
        const u32* tv = &mesh.tris[t * 3];
        for (int k = 0; k < 3; ++k) {
            const u32 vi = tv[(k + 1) % 3], vj = tv[(k + 2) % 3], vk = tv[k];
            const Vec2 pi = mesh.rest[vi], pj = mesh.rest[vj], pk = mesh.rest[vk];
            const f64 dx = pj.x - pi.x, dy = pj.y - pi.y, len2 = dx * dx + dy * dy;
            if (len2 < 1e-12) continue;
            const f64 ex = pk.x - pi.x, ey = pk.y - pi.y;
            const f64 x = (ex * dx + ey * dy) / len2;        // ao longo da aresta
            const f64 y = (ey * dx - ex * dy) / len2;        // na perpendicular R(d) = (−dy, dx)
            Row rx;   // vk.x − vi.x − x(vj.x − vi.x) + y(vj.y − vi.y) = 0
            rx.e[0] = {2 * vk, 1.0}; rx.e[1] = {2 * vi, -1.0 + x}; rx.e[2] = {2 * vj, -x};
            rx.e[3] = {2 * vj + 1, y}; rx.e[4] = {2 * vi + 1, -y}; rx.n = 5;
            Row ry;   // vk.y − vi.y − x(vj.y − vi.y) − y(vj.x − vi.x) = 0
            ry.e[0] = {2 * vk + 1, 1.0}; ry.e[1] = {2 * vi + 1, -1.0 + x}; ry.e[2] = {2 * vj + 1, -x};
            ry.e[3] = {2 * vj, -y}; ry.e[4] = {2 * vi, y}; ry.n = 5;
            rows.push_back(rx);
            rows.push_back(ry);
        }
    }
    for (const Bound& b : bound) {
        Row rx, ry;
        for (int k = 0; k < 3; ++k) {
            rx.e[k] = {2 * b.v[k], kPin * b.b[k]};
            ry.e[k] = {2 * b.v[k] + 1, kPin * b.b[k]};
        }
        rx.n = ry.n = 3;
        rx.rhs = kPin * b.target.x;
        ry.rhs = kPin * b.target.y;
        rows.push_back(rx);
        rows.push_back(ry);
    }
    std::vector<f64> x(2 * n);
    for (u32 i = 0; i < n; ++i) { x[2 * i] = rigidOut[i].x; x[2 * i + 1] = rigidOut[i].y; }
    solve_lsq(rows, 2 * n, x);

    // Passo 2: cada triângulo vira cópia rígida do repouso; as arestas seguem.
    rows.clear();
    for (u32 t = 0; t < ntri; ++t) {
        const u32* tv = &mesh.tris[t * 3];
        Vec2 rc{}, qc{};
        Vec2 q[3];
        for (int k = 0; k < 3; ++k) {
            q[k] = Vec2{static_cast<f32>(x[2 * tv[k]]), static_cast<f32>(x[2 * tv[k] + 1])};
            rc.x += mesh.rest[tv[k]].x / 3.0f; rc.y += mesh.rest[tv[k]].y / 3.0f;
            qc.x += q[k].x / 3.0f; qc.y += q[k].y / 3.0f;
        }
        f64 a = 0.0, b = 0.0;
        for (int k = 0; k < 3; ++k) {
            const f64 px = mesh.rest[tv[k]].x - rc.x, py = mesh.rest[tv[k]].y - rc.y;
            const f64 qx = q[k].x - qc.x, qy = q[k].y - qc.y;
            a += px * qx + py * qy;
            b += px * qy - py * qx;
        }
        const f64 th = (std::fabs(a) + std::fabs(b)) > 1e-12 ? std::atan2(b, a) : 0.0;
        const f64 c = std::cos(th), s = std::sin(th);
        Vec2 f[3];
        for (int k = 0; k < 3; ++k) {
            const f64 px = mesh.rest[tv[k]].x - rc.x, py = mesh.rest[tv[k]].y - rc.y;
            f[k] = Vec2{static_cast<f32>(c * px - s * py + qc.x), static_cast<f32>(s * px + c * py + qc.y)};
        }
        for (int k = 0; k < 3; ++k) {
            const u32 i = tv[k], j = tv[(k + 1) % 3];
            const Vec2 fi = f[k], fj = f[(k + 1) % 3];
            Row rx, ry;
            rx.e[0] = {2 * i, 1.0}; rx.e[1] = {2 * j, -1.0}; rx.n = 2; rx.rhs = fi.x - fj.x;
            ry.e[0] = {2 * i + 1, 1.0}; ry.e[1] = {2 * j + 1, -1.0}; ry.n = 2; ry.rhs = fi.y - fj.y;
            rows.push_back(rx);
            rows.push_back(ry);
        }
    }
    for (const Bound& b : bound) {
        Row rx, ry;
        for (int k = 0; k < 3; ++k) {
            rx.e[k] = {2 * b.v[k], kPin * b.b[k]};
            ry.e[k] = {2 * b.v[k] + 1, kPin * b.b[k]};
        }
        rx.n = ry.n = 3;
        rx.rhs = kPin * b.target.x;
        ry.rhs = kPin * b.target.y;
        rows.push_back(rx);
        rows.push_back(ry);
    }
    solve_lsq(rows, 2 * n, x);

    const f32 r = std::clamp(std::isfinite(rigidity) ? rigidity : 0.0f, 0.0f, 1.0f);
    for (u32 i = 0; i < n; ++i) {
        const Vec2 a{static_cast<f32>(x[2 * i]), static_cast<f32>(x[2 * i + 1])};
        out[i] = std::isfinite(a.x) && std::isfinite(a.y)
            ? Vec2{a.x + (rigidOut[i].x - a.x) * r, a.y + (rigidOut[i].y - a.y) * r}
            : rigidOut[i];
    }
}

bool outline_cells(const f32* rows, std::vector<u8>& cells) {
    cells.assign(kOutline * kOutline, 0);
    bool any = false;
    for (u32 y = 0; y < kOutline; ++y) {
        const f32 r = rows ? rows[y] : 0.0f;
        const u32 bits = std::isfinite(r) && r > 0.0f ? static_cast<u32>(std::min(r, 16777215.0f) + 0.5f) : 0u;
        for (u32 x = 0; x < kOutline; ++x) {
            const u8 on = (bits >> x) & 1u ? 1 : 0;
            cells[y * kOutline + x] = on;
            any |= on != 0;
        }
    }
    return any;
}

void outline_rows(const f32* coverage, u32 w, u32 h, f32 threshold, f32* rows) {
    for (u32 y = 0; y < kOutline; ++y) rows[y] = 0.0f;
    if (!coverage || !w || !h) return;
    u32 bits[kOutline] = {};
    for (u32 py = 0; py < h; ++py) {
        const u32 cy = std::min(kOutline - 1, py * kOutline / h);
        for (u32 px = 0; px < w; ++px)
            if (coverage[static_cast<usize>(py) * w + px] > threshold) bits[cy] |= 1u << std::min(kOutline - 1, px * kOutline / w);
    }
    for (u32 y = 0; y < kOutline; ++y) rows[y] = static_cast<f32>(bits[y]);
}

bool rest_point(const Mesh& mesh, const std::vector<Vec2>& deformed, Vec2 p, Vec2& rest) noexcept {
    rest = p;
    if (deformed.size() != mesh.rest.size() || mesh.tris.empty()) return false;
    f32 bc[3];
    bool inside = false;
    const i32 t = locate(mesh, deformed, p, bc, &inside);
    if (t < 0 || !inside) return false;
    rest = Vec2{0, 0};
    for (int k = 0; k < 3; ++k) {
        const Vec2 r = mesh.rest[mesh.tris[static_cast<u32>(t) * 3 + k]];
        rest.x += bc[k] * r.x;
        rest.y += bc[k] * r.y;
    }
    return true;
}

} // namespace aurea::puppet
