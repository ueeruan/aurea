// =============================================================================
//  Aurea / scene3d / Text3D.cpp
//
//  Triangulação: ear clipping com furos ligados à borda por pontes (o
//  algoritmo do earcut da Mapbox, sem o índice espacial — um glifo tem
//  poucas centenas de pontos). Contornos classificados pelo ANINHAMENTO
//  (par = borda, ímpar = furo), não pela orientação: TrueType e CFF giram ao
//  contrário e a fonte do aparelho pode ser qualquer uma.
// =============================================================================
#include "aurea/scene3d/Text3D.hpp"

#include "aurea/text/Text.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace aurea::scene3d {

ImportResult finalize_scene_asset(std::unique_ptr<SceneAsset> asset, const ImportOptions& options);

// -----------------------------------------------------------------------------
// Receita
// -----------------------------------------------------------------------------
std::string encode_text3d(const Text3DSpec& s) {
    auto byte = [](f32 v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char head[96];
    std::snprintf(head, sizeof(head), "v1;d=%.4f;a=%u;c=%02x%02x%02x%02x;t=", static_cast<double>(s.depth), s.alignment,
                  byte(s.color.x), byte(s.color.y), byte(s.color.z), byte(s.color.w));
    return std::string(kText3DScheme) + head + s.content;
}

bool decode_text3d(const std::string& src, Text3DSpec& out) {
    const std::string scheme = kText3DScheme;
    if (src.rfind(scheme, 0) != 0) return false;
    const usize t = src.find(";t=", scheme.size());
    if (t == std::string::npos) return false;
    const std::string head = src.substr(scheme.size(), t - scheme.size());
    Text3DSpec s;
    s.content = src.substr(t + 3);
    usize i = 0;
    while (i < head.size()) {
        usize j = head.find(';', i);
        if (j == std::string::npos) j = head.size();
        const std::string kv = head.substr(i, j - i);
        if (kv.rfind("d=", 0) == 0) s.depth = std::strtof(kv.c_str() + 2, nullptr);
        else if (kv.rfind("a=", 0) == 0) s.alignment = static_cast<u32>(std::strtoul(kv.c_str() + 2, nullptr, 10));
        else if (kv.rfind("c=", 0) == 0 && kv.size() == 10) {
            const unsigned long c = std::strtoul(kv.c_str() + 2, nullptr, 16);
            s.color = Vec4{static_cast<f32>((c >> 24) & 0xFF) / 255.0f, static_cast<f32>((c >> 16) & 0xFF) / 255.0f,
                           static_cast<f32>((c >> 8) & 0xFF) / 255.0f, static_cast<f32>(c & 0xFF) / 255.0f};
        }
        i = j + 1;
    }
    s.depth = std::clamp(s.depth, 0.0f, 10.0f);
    s.alignment = std::min(s.alignment, 2u);
    out = std::move(s);
    return true;
}

// -----------------------------------------------------------------------------
// Ear clipping com furos
// -----------------------------------------------------------------------------
namespace {

struct ENode {
    u32 i;
    f64 x, y;
    ENode* prev = nullptr;
    ENode* next = nullptr;
    bool steiner = false;
};

class Earcut {
public:
    std::vector<u32>& tris;
    explicit Earcut(std::vector<u32>& out) : tris(out) {}

    ENode* ring(const std::vector<Vec2>& pts, u32 base, bool clockwise) {
        f64 sum = 0;
        for (usize i = 0, j = pts.size() - 1; i < pts.size(); j = i++)
            sum += (static_cast<f64>(pts[j].x) - pts[i].x) * (static_cast<f64>(pts[i].y) + pts[j].y);
        ENode* last = nullptr;
        if (clockwise == (sum > 0)) {
            for (usize i = 0; i < pts.size(); ++i) last = insert(base + static_cast<u32>(i), pts[i], last);
        } else {
            for (usize i = pts.size(); i-- > 0;) last = insert(base + static_cast<u32>(i), pts[i], last);
        }
        if (last && equals(last, last->next)) {
            remove(last);
            last = last->next;
        }
        return last;
    }

    ENode* eliminate_holes(const std::vector<ENode*>& holes, ENode* outer) {
        std::vector<ENode*> queue;
        for (ENode* h : holes) {
            if (!h) continue;
            if (h == h->next) h->steiner = true;
            queue.push_back(leftmost(h));
        }
        std::sort(queue.begin(), queue.end(), [](const ENode* a, const ENode* b) { return a->x < b->x || (a->x == b->x && a->y < b->y); });
        for (ENode* h : queue) outer = eliminate_hole(h, outer);
        return outer;
    }

    void run(ENode* ear, int pass) {
        if (!ear) return;
        ENode* stop = ear;
        while (ear->prev != ear->next) {
            ENode* prev = ear->prev;
            ENode* next = ear->next;
            if (is_ear(ear)) {
                tris.push_back(prev->i);
                tris.push_back(ear->i);
                tris.push_back(next->i);
                remove(ear);
                ear = next->next;
                stop = next->next;
                continue;
            }
            ear = next;
            if (ear == stop) {
                if (pass == 0) run(filter(ear), 1);
                else if (pass == 1) run(cure_local_intersections(filter(ear)), 2);
                else split(ear);
                break;
            }
        }
    }

    ENode* filter(ENode* start, ENode* end = nullptr) {
        if (!start) return start;
        if (!end) end = start;
        ENode* p = start;
        bool again;
        do {
            again = false;
            if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0)) {
                remove(p);
                p = end = p->prev;
                if (p == p->next) break;
                again = true;
            } else {
                p = p->next;
            }
        } while (again || p != end);
        return end;
    }

private:
    std::deque<ENode> pool_;

    ENode* insert(u32 i, Vec2 v, ENode* last) {
        pool_.push_back(ENode{i, v.x, v.y});
        ENode* p = &pool_.back();
        if (!last) {
            p->prev = p;
            p->next = p;
        } else {
            p->next = last->next;
            p->prev = last;
            last->next->prev = p;
            last->next = p;
        }
        return p;
    }
    static void remove(ENode* p) {
        p->next->prev = p->prev;
        p->prev->next = p->next;
    }
    static f64 area(const ENode* p, const ENode* q, const ENode* r) {
        return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y);
    }
    static bool equals(const ENode* a, const ENode* b) { return a->x == b->x && a->y == b->y; }
    static bool in_triangle(f64 ax, f64 ay, f64 bx, f64 by, f64 cx, f64 cy, f64 px, f64 py) {
        return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py)
            && (bx - px) * (cy - py) >= (cx - px) * (by - py);
    }
    static int sign(f64 v) { return (v > 0) - (v < 0); }
    static bool on_segment(const ENode* p, const ENode* q, const ENode* r) {
        return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) && q->y <= std::max(p->y, r->y)
            && q->y >= std::min(p->y, r->y);
    }
    static bool intersects(const ENode* p1, const ENode* q1, const ENode* p2, const ENode* q2) {
        const int o1 = sign(area(p1, q1, p2)), o2 = sign(area(p1, q1, q2));
        const int o3 = sign(area(p2, q2, p1)), o4 = sign(area(p2, q2, q1));
        if (o1 != o2 && o3 != o4) return true;
        if (o1 == 0 && on_segment(p1, p2, q1)) return true;
        if (o2 == 0 && on_segment(p1, q2, q1)) return true;
        if (o3 == 0 && on_segment(p2, p1, q2)) return true;
        if (o4 == 0 && on_segment(p2, q1, q2)) return true;
        return false;
    }
    static bool locally_inside(const ENode* a, const ENode* b) {
        return area(a->prev, a, a->next) < 0 ? area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0
                                             : area(a, b, a->prev) < 0 || area(a, a->next, b) < 0;
    }
    static bool middle_inside(const ENode* a, const ENode* b) {
        const ENode* p = a;
        bool inside = false;
        const f64 px = (a->x + b->x) / 2, py = (a->y + b->y) / 2;
        do {
            if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y
                && (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x))
                inside = !inside;
            p = p->next;
        } while (p != a);
        return inside;
    }
    static bool intersects_polygon(const ENode* a, const ENode* b) {
        const ENode* p = a;
        do {
            if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b)) return true;
            p = p->next;
        } while (p != a);
        return false;
    }
    static bool valid_diagonal(const ENode* a, const ENode* b) {
        return a->next->i != b->i && a->prev->i != b->i && !intersects_polygon(a, b)
            && ((locally_inside(a, b) && locally_inside(b, a) && middle_inside(a, b)
                 && (area(a->prev, a, b->prev) != 0 || area(a, b->prev, b) != 0))
                || (equals(a, b) && area(a->prev, a, a->next) > 0 && area(b->prev, b, b->next) > 0));
    }
    static ENode* leftmost(ENode* start) {
        ENode* p = start;
        ENode* l = start;
        do {
            if (p->x < l->x || (p->x == l->x && p->y < l->y)) l = p;
            p = p->next;
        } while (p != start);
        return l;
    }
    static bool sector_contains_sector(const ENode* m, const ENode* p) {
        return area(m->prev, m, p->prev) < 0 && area(p->next, m, m->next) < 0;
    }

    bool is_ear(const ENode* ear) const {
        const ENode* a = ear->prev;
        const ENode* b = ear;
        const ENode* c = ear->next;
        if (area(a, b, c) >= 0) return false;
        for (const ENode* p = c->next; p != a; p = p->next) {
            if (!(p->x == a->x && p->y == a->y) && in_triangle(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y)
                && area(p->prev, p, p->next) >= 0)
                return false;
        }
        return true;
    }

    ENode* split_polygon(ENode* a, ENode* b) {
        pool_.push_back(ENode{a->i, a->x, a->y});
        ENode* a2 = &pool_.back();
        pool_.push_back(ENode{b->i, b->x, b->y});
        ENode* b2 = &pool_.back();
        ENode* an = a->next;
        ENode* bp = b->prev;
        a->next = b;
        b->prev = a;
        a2->next = an;
        an->prev = a2;
        b2->next = a2;
        a2->prev = b2;
        bp->next = b2;
        b2->prev = bp;
        return b2;
    }

    ENode* cure_local_intersections(ENode* start) {
        ENode* p = start;
        do {
            ENode* a = p->prev;
            ENode* b = p->next->next;
            if (!equals(a, b) && intersects(a, p, p->next, b) && locally_inside(a, b) && locally_inside(b, a)) {
                tris.push_back(a->i);
                tris.push_back(p->i);
                tris.push_back(b->i);
                remove(p);
                remove(p->next);
                p = start = b;
            }
            p = p->next;
        } while (p != start);
        return filter(p);
    }

    void split(ENode* start) {
        ENode* a = start;
        do {
            ENode* b = a->next->next;
            while (b != a->prev) {
                if (a->i != b->i && valid_diagonal(a, b)) {
                    ENode* c = split_polygon(a, b);
                    a = filter(a, a->next);
                    c = filter(c, c->next);
                    run(a, 0);
                    run(c, 0);
                    return;
                }
                b = b->next;
            }
            a = a->next;
        } while (a != start);
    }

    ENode* find_bridge(ENode* hole, ENode* outer) {
        ENode* p = outer;
        const f64 hx = hole->x, hy = hole->y;
        f64 qx = -1e300;
        ENode* m = nullptr;
        do {
            if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
                const f64 x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
                if (x <= hx && x > qx) {
                    qx = x;
                    m = p->x < p->next->x ? p : p->next;
                    if (x == hx) return m;
                }
            }
            p = p->next;
        } while (p != outer);
        if (!m) return nullptr;
        ENode* stop = m;
        const f64 mx = m->x, my = m->y;
        f64 tanMin = 1e300;
        p = m;
        do {
            if (hx >= p->x && p->x >= mx && hx != p->x
                && in_triangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
                const f64 tan = std::fabs(hy - p->y) / (hx - p->x);
                if (locally_inside(p, hole)
                    && (tan < tanMin || (tan == tanMin && (p->x > m->x || (p->x == m->x && sector_contains_sector(m, p)))))) {
                    m = p;
                    tanMin = tan;
                }
            }
            p = p->next;
        } while (p != stop);
        return m;
    }

    ENode* eliminate_hole(ENode* hole, ENode* outer) {
        ENode* bridge = find_bridge(hole, outer);
        if (!bridge) return outer;
        ENode* reverse = split_polygon(bridge, hole);
        filter(reverse, reverse->next);
        return filter(bridge, bridge->next);
    }
};

f64 signed_area(const std::vector<Vec2>& c) {   // > 0 = anti-horário (Y para cima)
    f64 a = 0;
    for (usize i = 0, j = c.size() - 1; i < c.size(); j = i++)
        a += static_cast<f64>(c[j].x) * c[i].y - static_cast<f64>(c[i].x) * c[j].y;
    return a * 0.5;
}

bool point_in(const std::vector<Vec2>& c, Vec2 p) {
    bool in = false;
    for (usize i = 0, j = c.size() - 1; i < c.size(); j = i++) {
        if (((c[i].y > p.y) != (c[j].y > p.y)) && (p.x < (c[j].x - c[i].x) * (p.y - c[i].y) / (c[j].y - c[i].y) + c[i].x)) in = !in;
    }
    return in;
}

f32 srgb_to_linear(f32 c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

} // namespace

bool triangulate_polygon(const std::vector<std::vector<Vec2>>& rings, std::vector<u32>& out) {
    if (rings.empty() || rings[0].size() < 3) return false;
    const usize before = out.size();
    Earcut e(out);
    u32 base = static_cast<u32>(rings[0].size());
    ENode* outer = e.ring(rings[0], 0, true);
    if (!outer || outer->next == outer->prev) return false;
    std::vector<ENode*> holes;
    for (usize r = 1; r < rings.size(); ++r) {
        if (rings[r].size() >= 3) holes.push_back(e.ring(rings[r], base, false));
        base += static_cast<u32>(rings[r].size());
    }
    if (!holes.empty()) outer = e.eliminate_holes(holes, outer);
    e.run(outer, 0);
    return out.size() > before;
}

// -----------------------------------------------------------------------------
// Malha
// -----------------------------------------------------------------------------
ImportResult build_text3d(const text::Font& font, const Text3DSpec& spec) {
    ImportResult res;
    TextData td;
    td.content = spec.content;
    td.size = 100.0f;                 // contorno medido a 100 px e levado a 1 = altura da fonte
    td.alignment = spec.alignment;
    std::vector<std::vector<Vec2>> raw;
    if (!text::outline(font, td, raw)) {
        res.error = ImportError::NoGeometry;
        res.detail = "texto sem letras visiveis";
        return res;
    }
    // Y para cima, 1 = altura da fonte.
    std::vector<std::vector<Vec2>> cs(raw.size());
    for (usize i = 0; i < raw.size(); ++i) {
        cs[i].reserve(raw[i].size());
        for (Vec2 p : raw[i]) cs[i].push_back(Vec2{p.x / 100.0f, -p.y / 100.0f});
    }
    // Aninhamento: quantos contornos contêm cada um (par = borda, ímpar = furo).
    const usize n = cs.size();
    std::vector<f64> areas(n);
    std::vector<u32> depth(n, 0);
    for (usize i = 0; i < n; ++i) areas[i] = signed_area(cs[i]);
    for (usize i = 0; i < n; ++i) {
        for (usize j = 0; j < n; ++j) {
            if (i != j && std::fabs(areas[j]) > std::fabs(areas[i]) && point_in(cs[j], cs[i][0])) ++depth[i];
        }
    }
    // Orientação normalizada: borda anti-horária, furo horário (a normal de
    // cada aresta lateral sai sempre para fora do material).
    for (usize i = 0; i < n; ++i) {
        const bool hole = depth[i] & 1u;
        if ((areas[i] > 0) == hole) std::reverse(cs[i].begin(), cs[i].end());
    }

    const f32 d = std::max(0.0f, spec.depth);
    Primitive prim;
    auto vert = [&](Vec2 p, f32 z, Vec3 nrm) {
        prim.positions.push_back(Vec3{p.x, p.y, z});
        prim.normals.push_back(nrm);
        return static_cast<u32>(prim.positions.size() - 1);
    };

    // Frente (+Z, z = d/2) e fundo (−Z, z = −d/2): o centro da espessura na origem.
    const f32 zf = d * 0.5f, zb = -d * 0.5f;
    for (usize o = 0; o < n; ++o) {
        if (depth[o] & 1u) continue;
        std::vector<std::vector<Vec2>> rings{cs[o]};
        for (usize h = 0; h < n; ++h) {
            if (!(depth[h] & 1u) || depth[h] != depth[o] + 1) continue;
            // Pai do furo = a borda de menor área que o contém.
            usize best = n;
            for (usize k = 0; k < n; ++k) {
                if ((depth[k] & 1u) || depth[k] + 1 != depth[h] || !point_in(cs[k], cs[h][0])) continue;
                if (best == n || std::fabs(areas[k]) < std::fabs(areas[best])) best = k;
            }
            if (best == o) rings.push_back(cs[h]);
        }
        std::vector<u32> idx;
        if (!triangulate_polygon(rings, idx)) continue;
        std::vector<Vec2> flat;
        for (const auto& r : rings) flat.insert(flat.end(), r.begin(), r.end());
        const u32 front0 = static_cast<u32>(prim.positions.size());
        for (Vec2 p : flat) vert(p, zf, Vec3{0, 0, 1});
        const u32 back0 = static_cast<u32>(prim.positions.size());
        if (d > 0.0f) for (Vec2 p : flat) vert(p, zb, Vec3{0, 0, -1});
        for (usize t = 0; t + 2 < idx.size(); t += 3) {
            u32 a = idx[t], b = idx[t + 1], c = idx[t + 2];
            const Vec2 A = flat[a], B = flat[b], C = flat[c];
            if ((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x) < 0) std::swap(b, c);   // anti-horário = frente
            prim.indices.insert(prim.indices.end(), {front0 + a, front0 + b, front0 + c});
            if (d > 0.0f) prim.indices.insert(prim.indices.end(), {back0 + a, back0 + c, back0 + b});
        }
    }
    const usize capFaces = prim.indices.size();

    // Laterais: um quad por aresta. Normal da quina = média das arestas
    // vizinhas quando o ângulo entre elas é pequeno (curva lisa), senão a da
    // própria aresta (canto vincado, como o "T" e o "E").
    if (d > 0.0f) {
        const f32 smoothCos = std::cos(35.0f * 3.14159265f / 180.0f);
        for (const std::vector<Vec2>& c : cs) {
            const usize m = c.size();
            std::vector<Vec3> en(m);
            for (usize i = 0; i < m; ++i) {
                const Vec2 a = c[i], b = c[(i + 1) % m];
                const f32 dx = b.x - a.x, dy = b.y - a.y;
                const f32 len = std::sqrt(dx * dx + dy * dy);
                en[i] = len > 1e-9f ? Vec3{dy / len, -dx / len, 0} : Vec3{0, 0, 0};
            }
            auto corner = [&](usize edge, usize other) {
                const Vec3 a = en[edge], b = en[other];
                if (a.x * b.x + a.y * b.y >= smoothCos) {
                    const Vec3 s{a.x + b.x, a.y + b.y, 0};
                    const f32 l = std::sqrt(s.x * s.x + s.y * s.y);
                    if (l > 1e-6f) return Vec3{s.x / l, s.y / l, 0};
                }
                return a;
            };
            for (usize i = 0; i < m; ++i) {
                if (en[i].x == 0 && en[i].y == 0) continue;
                const Vec2 p0 = c[i], p1 = c[(i + 1) % m];
                const Vec3 n0 = corner(i, (i + m - 1) % m), n1 = corner(i, (i + 1) % m);
                const u32 a0 = vert(p0, zf, n0), a1 = vert(p1, zf, n1), b0 = vert(p0, zb, n0), b1 = vert(p1, zb, n1);
                prim.indices.insert(prim.indices.end(), {a0, b0, a1, a1, b0, b1});
            }
        }
    }
    if (capFaces == 0) {
        res.error = ImportError::NoGeometry;
        res.detail = "contorno do texto nao triangulou";
        return res;
    }
    for (const Vec3& p : prim.positions) prim.bounds.add(p);
    prim.material = 0;

    auto asset = std::make_unique<SceneAsset>();
    std::string name = spec.content.substr(0, spec.content.find('\n'));
    if (name.size() > 32) name.resize(32);
    asset->sourceName = name.empty() ? "Texto 3D" : name;
    Material mat;
    mat.name = "texto";
    mat.baseColor = Vec4{srgb_to_linear(spec.color.x), srgb_to_linear(spec.color.y), srgb_to_linear(spec.color.z), 1.0f};
    mat.metallic = 0.0f;
    mat.roughness = 0.35f;
    asset->materials.push_back(mat);
    Mesh mesh;
    mesh.name = asset->sourceName;
    mesh.bounds = prim.bounds;
    mesh.primitives.push_back(std::move(prim));
    asset->meshes.push_back(std::move(mesh));
    scene3d::Node node;
    node.name = asset->sourceName;
    node.mesh = 0;
    asset->nodes.push_back(node);
    asset->roots.push_back(0);
    ImportOptions o;
    o.generateLods = false;   // o texto não pode perder serifa nem furo ao afastar
    return finalize_scene_asset(std::move(asset), o);
}

} // namespace aurea::scene3d
