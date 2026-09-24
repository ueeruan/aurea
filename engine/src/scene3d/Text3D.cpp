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
#include "aurea/text/FontManager.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace aurea::scene3d {

ImportResult finalize_scene_asset(std::unique_ptr<SceneAsset> asset, const ImportOptions& options);

// -----------------------------------------------------------------------------
// Receita
// -----------------------------------------------------------------------------
namespace {

/// Um campo de material na receita: cor sRGB em hex + os escalares do PBR.
void write_material(std::string& out, const char* tag, const Text3DMaterial& m) {
    auto byte = [](f32 v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    // A cor vai em hex e os escalares separados por '/': sem o separador o
    // último dígito do hex colaria no começo do número ("ff80000.350").
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s=%02x%02x%02x/%.3f/%.3f/%.3f/%.3f;%se=%02x%02x%02x/%.2f;", tag, byte(m.color.x),
                  byte(m.color.y), byte(m.color.z), static_cast<double>(m.metallic), static_cast<double>(m.roughness),
                  static_cast<double>(m.specular), static_cast<double>(m.occlusion), tag, byte(m.emissive.x),
                  byte(m.emissive.y), byte(m.emissive.z), static_cast<double>(m.emissiveStrength));
    out += buf;
}

void read_material(const std::string& head, const char* tag, Text3DMaterial& m) {
    const usize at = head.find(std::string(tag) + "=");
    if (at == std::string::npos) return;
    const usize end = head.find(';', at);
    const std::string kv = head.substr(at + std::strlen(tag) + 1, (end == std::string::npos ? head.size() : end) - at - std::strlen(tag) - 1);
    // "#rrggbb" seguido de "/metal/rugos/espec/oclusao"
    if (kv.size() >= 6) {
        const unsigned long c = std::strtoul(kv.substr(0, 6).c_str(), nullptr, 16);
        m.color = Vec4{static_cast<f32>((c >> 16) & 0xFF) / 255.0f, static_cast<f32>((c >> 8) & 0xFF) / 255.0f,
                       static_cast<f32>(c & 0xFF) / 255.0f, 1.0f};
    }
    f32 v[4] = {m.metallic, m.roughness, m.specular, m.occlusion};
    usize i = 6;
    for (u32 k = 0; k < 4 && i < kv.size(); ++k) {
        if (kv[i] != '/') break;
        const usize j = kv.find('/', i + 1);
        v[k] = std::strtof(kv.substr(i + 1, (j == std::string::npos ? kv.size() : j) - i - 1).c_str(), nullptr);
        if (j == std::string::npos) break;
        i = j;
    }
    m.metallic = std::clamp(v[0], 0.0f, 1.0f);
    m.roughness = std::clamp(v[1], 0.0f, 1.0f);
    m.specular = std::clamp(v[2], 0.0f, 1.0f);
    m.occlusion = std::clamp(v[3], 0.0f, 1.0f);

    const std::string et = std::string(tag) + "e=";
    const usize e = head.find(et);
    if (e == std::string::npos) return;
    const usize eend = head.find(';', e);
    const std::string ekv = head.substr(e + et.size(), (eend == std::string::npos ? head.size() : eend) - e - et.size());
    if (ekv.size() >= 6) {
        const unsigned long c = std::strtoul(ekv.substr(0, 6).c_str(), nullptr, 16);
        m.emissive = Vec3{static_cast<f32>((c >> 16) & 0xFF) / 255.0f, static_cast<f32>((c >> 8) & 0xFF) / 255.0f,
                          static_cast<f32>(c & 0xFF) / 255.0f};
        if (ekv.size() > 7 && ekv[6] == '/')
            m.emissiveStrength = std::clamp(std::strtof(ekv.c_str() + 7, nullptr), 0.0f, 100.0f);
    }
}

} // namespace

std::string encode_text3d(const Text3DSpec& s) {
    auto byte = [](f32 v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char head[160];
    std::snprintf(head, sizeof(head), "v2;d=%.4f;a=%u;c=%02x%02x%02x%02x;bv=%u%.4f/%.4f/%.3f/%u;",
                  static_cast<double>(s.depth), s.alignment, byte(s.color.x), byte(s.color.y), byte(s.color.z),
                  byte(s.color.w), s.bevel ? 1u : 0u, static_cast<double>(s.bevelWidth),
                  static_cast<double>(s.bevelDepth), static_cast<double>(s.bevelRoundness), s.bevelSegments);
    std::string out = std::string(kText3DScheme) + head;
    write_material(out, "m0", s.front_material());
    if (s.regionMaterials) {
        out += "rg=1;";
        write_material(out, "m1", s.side);
        write_material(out, "m2", s.bevelMat);
    } else {
        out += "rg=0;";
    }
    out += "font=";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : s.fontPath) { out += hex[c >> 4]; out += hex[c & 15]; }
    char anim[128];
    std::snprintf(anim, sizeof(anim), ";anim=%u/%.4f/%.4f/%.4f;", s.animation,
        double(s.animationDuration), double(s.animationStagger), double(s.animationAmount));
    return out + anim + "t=" + s.content;
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
        if (kv.rfind("font=", 0) == 0) {
            auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
            for (usize k = 5; k + 1 < kv.size(); k += 2) {
                int a = digit(kv[k]), b = digit(kv[k + 1]);
                if (a < 0 || b < 0 || (a == 0 && b == 0)) { s.fontPath.clear(); break; }
                s.fontPath += static_cast<char>((a << 4) | b);
            }
        } else if (kv.rfind("anim=", 0) == 0) {
            unsigned mode = 0; float duration = 2, stagger = .12f, amount = .3f;
            if (std::sscanf(kv.c_str() + 5, "%u/%f/%f/%f", &mode, &duration, &stagger, &amount) == 4) {
                s.animation = std::min(mode, 4u);
                s.animationDuration = std::isfinite(duration) ? std::clamp(duration, .2f, 30.f) : 2.f;
                s.animationStagger = std::isfinite(stagger) ? std::clamp(stagger, 0.f, 1.f) : .12f;
                s.animationAmount = std::isfinite(amount) ? std::clamp(amount, 0.f, 2.f) : .3f;
            }
        } else if (kv.rfind("d=", 0) == 0) s.depth = std::strtof(kv.c_str() + 2, nullptr);
        else if (kv.rfind("a=", 0) == 0) s.alignment = static_cast<u32>(std::strtoul(kv.c_str() + 2, nullptr, 10));
        else if (kv.rfind("c=", 0) == 0 && kv.size() == 10) {
            const unsigned long c = std::strtoul(kv.c_str() + 2, nullptr, 16);
            s.color = Vec4{static_cast<f32>((c >> 24) & 0xFF) / 255.0f, static_cast<f32>((c >> 16) & 0xFF) / 255.0f,
                           static_cast<f32>((c >> 8) & 0xFF) / 255.0f, static_cast<f32>(c & 0xFF) / 255.0f};
        } else if (kv.rfind("bv=", 0) == 0) {
            s.bevel = kv.size() > 3 && kv[3] == '1';
            f32 v[3] = {s.bevelWidth, s.bevelDepth, s.bevelRoundness};
            usize p = 4;
            for (u32 k = 0; k < 3 && p < kv.size(); ++k) {
                const usize q = kv.find('/', p);
                v[k] = std::strtof(kv.substr(p, (q == std::string::npos ? kv.size() : q) - p).c_str(), nullptr);
                if (q == std::string::npos) { p = kv.size(); break; }
                p = q + 1;
            }
            if (p < kv.size()) s.bevelSegments = static_cast<u32>(std::strtoul(kv.c_str() + p, nullptr, 10));
            s.bevelWidth = std::clamp(v[0], 0.0f, 0.5f);
            s.bevelDepth = std::clamp(v[1], 0.0f, 0.5f);
            s.bevelRoundness = std::clamp(v[2], 0.0f, 1.0f);
        } else if (kv == "rg=1") {
            s.regionMaterials = true;
        }
        i = j + 1;
    }
    // A receita v1 só tinha a cor; nela não existe `m0=` e a cor do cabeçalho
    // já é o material da frente — e vale nas três regiões.
    if (head.find("m0=") != std::string::npos) {
        Text3DMaterial front;
        read_material(head, "m0", front);
        s.color = front.color;
        s.metallic = front.metallic;
        s.roughness = front.roughness;
        s.specular = front.specular;
        s.occlusion = front.occlusion;
        s.emissive = front.emissive;
        s.emissiveStrength = front.emissiveStrength;
    }
    s.side = s.front_material();
    s.bevelMat = s.front_material();
    if (s.regionMaterials) {
        read_material(head, "m1", s.side);
        read_material(head, "m2", s.bevelMat);
    }
    s.depth = std::clamp(s.depth, 0.0f, 10.0f);
    s.alignment = std::min(s.alignment, 2u);
    s.bevelSegments = std::clamp(s.bevelSegments, 1u, 8u);
    out = std::move(s);
    return true;
}

std::shared_ptr<const text::Font> text3d_font(const Text3DSpec& spec) {
    TextData t;
    t.fontPath = spec.fontPath;
    return text::FontManager::instance().font_for(t);
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
// Recuo do contorno (a base do chanfro)
//
// O recuo é POR VÉRTICE pela bissetriz: cada anel do chanfro tem exatamente os
// mesmos vértices do contorno, então os quadriláteros entre dois anéis sempre
// casam. O que garante que o anel não se auto-intersecta é o TETO calculado em
// `contour_offset_limit` — nenhuma aresta pode encurtar até virar do avesso.
// -----------------------------------------------------------------------------
namespace {

constexpr f32 kMiterLimit = 3.0f;   ///< quina aguda: além disto o vértice é cortado

/// Deslocamento UNITÁRIO do vértice (bissetriz × limite de mitra). Com ele o
/// anel de recuo `a` é `p + o * a` — e o teto do recuo sai de graça.
Vec2 miter_offset(const std::vector<Vec2>& c, usize i) {
    const usize n = c.size();
    const Vec2 p = c[i];
    Vec2 d0{p.x - c[(i + n - 1) % n].x, p.y - c[(i + n - 1) % n].y};
    Vec2 d1{c[(i + 1) % n].x - p.x, c[(i + 1) % n].y - p.y};
    const f32 l0 = std::sqrt(d0.x * d0.x + d0.y * d0.y);
    const f32 l1 = std::sqrt(d1.x * d1.x + d1.y * d1.y);
    if (l0 < 1e-9f || l1 < 1e-9f) return Vec2{0, 0};
    d0.x /= l0; d0.y /= l0;
    d1.x /= l1; d1.y /= l1;
    // Normal ESQUERDA = lado do material (borda anti-horária, furo horário).
    const Vec2 n0{-d0.y, d0.x};
    const Vec2 n1{-d1.y, d1.x};
    Vec2 m{n0.x + n1.x, n0.y + n1.y};
    const f32 lm = std::sqrt(m.x * m.x + m.y * m.y);
    if (lm < 1e-5f) return n0;                  // espeto de 180°: segue a aresta que chega
    m.x /= lm; m.y /= lm;
    const f32 cosHalf = m.x * n0.x + m.y * n0.y;
    const f32 scale = cosHalf < 1.0f / kMiterLimit ? kMiterLimit : 1.0f / cosHalf;
    return Vec2{m.x * scale, m.y * scale};
}

} // namespace

f32 contour_offset_limit(const std::vector<Vec2>& c) {
    const usize n = c.size();
    if (n < 3) return 0.0f;
    f32 lim = 1e3f;
    for (usize i = 0; i < n; ++i) {
        const Vec2 a = c[i], b = c[(i + 1) % n];
        const Vec2 e{b.x - a.x, b.y - a.y};
        const f32 L = std::sqrt(e.x * e.x + e.y * e.y);
        if (L < 1e-9f) continue;
        const Vec2 ex{e.x / L, e.y / L};
        const Vec2 o0 = miter_offset(c, i), o1 = miter_offset(c, (i + 1) % n);
        // aresta recuada = e + a*(o1 - o0); ela vira do avesso se isto chegar a zero.
        const f32 k = (o1.x - o0.x) * ex.x + (o1.y - o0.y) * ex.y;
        if (k < -1e-9f) lim = std::min(lim, L / -k);
    }
    return lim;
}

bool offset_contour(const std::vector<Vec2>& contour, f32 amount, std::vector<Vec2>& out) {
    const usize n = contour.size();
    if (n < 3) return false;
    if (amount <= 1e-6f) {
        out = contour;
        return true;
    }
    out.resize(n);
    for (usize i = 0; i < n; ++i) {
        const Vec2 o = miter_offset(contour, i);
        out[i] = Vec2{contour[i].x + o.x * amount, contour[i].y + o.y * amount};
    }
    for (usize i = 0; i < n; ++i) {
        const Vec2 a = contour[i], b = contour[(i + 1) % n];
        const Vec2 p = out[i], q = out[(i + 1) % n];
        if ((b.x - a.x) * (q.x - p.x) + (b.y - a.y) * (q.y - p.y) <= 0.0f) { out.clear(); return false; }
    }
    const f64 a0 = signed_area(contour), a1 = signed_area(out);
    if (a0 == 0.0 || a1 == 0.0 || (a0 > 0) != (a1 > 0)) { out.clear(); return false; }
    return true;
}

// -----------------------------------------------------------------------------
// Malha
//
//  Regiões (cada uma com normais próprias, por isso não compartilham vértice):
//
//    TAMPA     frente em +z, fundo em −z, polígono recuado (o do chanfro)
//    PAREDE    a silhueta original, de −z0 a +z0
//    CHANFRO   o anel entre a silhueta e a tampa, em cada ponta
//
//  Sem chanfro, z0 = zf e a tampa é o próprio contorno. Com chanfro, a silhueta
//  fica em ±z0 e a tampa recua `bevelWidth` — a geometria muda de verdade.
// -----------------------------------------------------------------------------

namespace {

/// Um pedaço da malha com normais e UV próprios.
struct Chunk {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uv0;
    std::vector<Vec4> tangents;
    std::vector<u32>  indices;

    u32 push(Vec3 p, Vec3 n, Vec2 uv) {
        positions.push_back(p);
        normals.push_back(n);
        uv0.push_back(uv);
        return static_cast<u32>(positions.size() - 1);
    }
    [[nodiscard]] bool empty() const noexcept { return indices.empty(); }
};

/// A geometria do texto inteiro, sem material — é isto que o cache guarda.
struct TextMesh {
    Chunk caps, walls, bevels;
};

/// Perfil do chanfro: 0 = reto (chanfro de 45°), 1 = filete circular.
/// `away` = fração da largura já recuada; `rise` = fração da profundidade já subida.
void bevel_profile(f32 t, f32 roundness, f32& away, f32& rise) {
    constexpr f32 kHalfPi = 1.5707963267948966f;
    const f32 r = std::clamp(roundness, 0.0f, 1.0f);
    const f32 cirA = std::sin(t * kHalfPi), cirR = 1.0f - std::cos(t * kHalfPi);
    away = t + (cirA - t) * r;
    rise = t + (cirR - t) * r;
}

/// Tangentes a partir das UV (acumulação de Lengyel), ortonormalizadas contra a
/// normal, com o sinal da bitangente em w. Sem UV utilizável fica vazio.
void compute_tangents(Chunk& c) {
    const usize vc = c.positions.size();
    c.tangents.clear();
    if (vc == 0 || c.uv0.size() != vc || c.indices.size() < 3) return;
    std::vector<Vec3> tan(vc), bit(vc);
    for (usize t = 0; t + 2 < c.indices.size(); t += 3) {
        const u32 i0 = c.indices[t], i1 = c.indices[t + 1], i2 = c.indices[t + 2];
        const Vec3 p0 = c.positions[i0], p1 = c.positions[i1], p2 = c.positions[i2];
        const Vec2 w0 = c.uv0[i0], w1 = c.uv0[i1], w2 = c.uv0[i2];
        const Vec3 e1 = p1 - p0, e2 = p2 - p0;
        const f32 du1 = w1.x - w0.x, dv1 = w1.y - w0.y, du2 = w2.x - w0.x, dv2 = w2.y - w0.y;
        const f32 det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        const f32 r = 1.0f / det;
        const Vec3 tv{(e1.x * dv2 - e2.x * dv1) * r, (e1.y * dv2 - e2.y * dv1) * r, (e1.z * dv2 - e2.z * dv1) * r};
        const Vec3 bv{(e2.x * du1 - e1.x * du2) * r, (e2.y * du1 - e1.y * du2) * r, (e2.z * du1 - e1.z * du2) * r};
        for (u32 i : {i0, i1, i2}) { tan[i] = tan[i] + tv; bit[i] = bit[i] + bv; }
    }
    c.tangents.resize(vc);
    for (usize i = 0; i < vc; ++i) {
        const Vec3 n = c.normals[i].normalized();
        Vec3 t = tan[i];
        t = t - n * t.dot(n);                        // Gram-Schmidt
        if (t.length() < 1e-5f) t = std::fabs(n.z) < 0.9f ? Vec3{0, 0, 1}.cross(n) : Vec3{1, 0, 0}.cross(n);
        t = t.normalized();
        c.tangents[i] = Vec4{t.x, t.y, t.z, n.cross(t).dot(bit[i]) < 0.0f ? -1.0f : 1.0f};
    }
}

/// Normais externas de cada aresta do contorno — (dy, −dx)/|d|, que para uma
/// borda anti-horária (e para um furo horário) aponta para fora do material.
void edge_normals(const std::vector<Vec2>& c, std::vector<Vec3>& out) {
    const usize m = c.size();
    out.assign(m, Vec3{0, 0, 0});
    for (usize i = 0; i < m; ++i) {
        const Vec2 d = c[(i + 1) % m] - c[i];
        const f32 len = d.length();
        if (len > 1e-9f) out[i] = Vec3{d.y / len, -d.x / len, 0.0f};
    }
}

/// Normal externa de uma das pontas da aresta `edge`, olhando para a aresta
/// vizinha `other`: nas curvas (ângulo menor que 35° entre as duas) é a média,
/// que é o que dá o sombreamento liso do "O"; nas quinas é a normal da PRÓPRIA
/// aresta — os dois lados da quina ficam com a mesma normal e a aresta sai
/// viva, como no "T" e no "E". Devolver a média numa quina (ou a normal da
/// aresta vizinha) deixaria a normal de um lado contra a do outro e a face
/// pareceria virada.
Vec3 edge_end_normal(const std::vector<Vec3>& edgeNormal, usize edge, usize other) {
    constexpr f32 kSmoothCos = 0.8191520442889918f;   // cos(35°)
    const Vec3 a = edgeNormal[edge], b = edgeNormal[other];
    if (a.x * b.x + a.y * b.y >= kSmoothCos) {
        const Vec3 s{a.x + b.x, a.y + b.y, 0.0f};
        if (s.length() > 1e-6f) return s.normalized();
    }
    return a;
}

/// Normal de um anel do chanfro. Da superfície P(t, s) = C(s) + esq(s)·a(t) + Z·z(t):
///   N ∝ ( fora·z'(t) , a'(t) )
Vec3 bevel_normal(Vec3 outward, f32 slopeAway, f32 slopeRise) {
    const Vec3 n{outward.x * slopeRise, outward.y * slopeRise, slopeAway};
    return n.length() > 1e-6f ? n.normalized() : outward;
}

struct GroupInput {
    const std::vector<std::vector<Vec2>>* cs = nullptr;
    const std::vector<std::vector<u32>>* holesOf = nullptr;
};

/// Emite um grupo (uma borda e os furos dela) em `out`. Devolve falso quando o
/// recuo pedido não cabe no traço — o chamador tenta de novo com menos chanfro.
bool emit_group(TextMesh& out, const GroupInput& in, usize outer, f32 zf, f32 zb, f32 depth, f32 width, u32 segs,
                f32 bevelDepth, f32 roundness, Vec2 uvMin, Vec2 uvSize) {
    const std::vector<std::vector<Vec2>>& cs = *in.cs;
    const bool bev = segs > 0 && width > 1e-5f;
    const f32 bd = bev ? std::min(bevelDepth, depth * 0.45f) : 0.0f;
    const f32 z0 = bev ? zf - bd : zf;

    std::vector<usize> group{outer};
    for (u32 h : (*in.holesOf)[outer]) group.push_back(h);

    // `slope[k]` = (a'(t), z'(t)) do anel k — média dos trechos que ele toca. Nas
    // pontas é um trecho só, e é isso que dá a aresta viva entre a silhueta e a
    // parede e entre o chanfro e a tampa.
    const u32 nk = bev ? segs + 1 : 1;
    std::vector<f32> slopeAway(nk, 1.0f), slopeRise(nk, 1.0f);
    if (bev) {
        const f32 dt = 1.0f / static_cast<f32>(segs);
        for (u32 k = 0; k < nk; ++k) {
            f32 sa = 0.0f, sr = 0.0f;
            u32 cnt = 0;
            for (i32 j = static_cast<i32>(k) - 1; j <= static_cast<i32>(k); ++j) {
                if (j < 0 || j >= static_cast<i32>(segs)) continue;
                f32 a0 = 0, r0 = 0, a1 = 0, r1 = 0;
                bevel_profile(static_cast<f32>(j) * dt, roundness, a0, r0);
                bevel_profile(static_cast<f32>(j + 1) * dt, roundness, a1, r1);
                sa += (a1 - a0) / dt;
                sr += (r1 - r0) / dt;
                ++cnt;
            }
            if (cnt) { slopeAway[k] = sa / static_cast<f32>(cnt); slopeRise[k] = sr / static_cast<f32>(cnt); }
        }
    }

    // Anéis do recuo: `rings[gi][k]`. k = 0 é a silhueta, k = segs é a tampa.
    std::vector<std::vector<std::vector<Vec2>>> rings(group.size());
    for (usize gi = 0; gi < group.size(); ++gi) {
        const std::vector<Vec2>& c = cs[group[gi]];
        rings[gi].resize(nk);
        for (u32 k = 0; k < nk; ++k) {
            if (!bev || k == 0) { rings[gi][k] = c; continue; }
            f32 away = 0, rise = 0;
            bevel_profile(static_cast<f32>(k) / static_cast<f32>(segs), roundness, away, rise);
            if (!offset_contour(c, width * away, rings[gi][k])) return false;
        }
    }

    // --- tampas: frente em +zf (olhando +z) e fundo em −zf ------------------
    std::vector<std::vector<Vec2>> capRings(group.size());
    for (usize gi = 0; gi < group.size(); ++gi) capRings[gi] = rings[gi].back();
    std::vector<u32> idx;
    if (!triangulate_polygon(capRings, idx)) return false;
    std::vector<Vec2> flat;
    for (const auto& r : capRings) flat.insert(flat.end(), r.begin(), r.end());
    const u32 capF = static_cast<u32>(out.caps.positions.size());
    for (Vec2 p : flat)
        out.caps.push(Vec3{p.x, p.y, zf}, Vec3{0, 0, 1}, Vec2{(p.x - uvMin.x) / uvSize.x, (p.y - uvMin.y) / uvSize.y});
    // O fundo é a leitura espelhada da frente — é o que se vê ao olhar a letra
    // por trás, e mantém o triedro (tangente, bitangente, normal) coerente.
    const u32 capB = static_cast<u32>(out.caps.positions.size());
    for (Vec2 p : flat)
        out.caps.push(Vec3{p.x, p.y, zb}, Vec3{0, 0, -1},
                      Vec2{1.0f - (p.x - uvMin.x) / uvSize.x, (p.y - uvMin.y) / uvSize.y});
    for (usize t = 0; t + 2 < idx.size(); t += 3) {
        u32 a = idx[t], b = idx[t + 1], c = idx[t + 2];
        const Vec2 A = flat[a], B = flat[b], C = flat[c];
        if ((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x) < 0) std::swap(b, c);   // anti-horário = frente
        out.caps.indices.insert(out.caps.indices.end(), {capF + a, capF + b, capF + c, capB + a, capB + c, capB + b});
    }

    // --- paredes e chanfro, contorno a contorno ----------------------------
    for (usize gi = 0; gi < group.size(); ++gi) {
        const std::vector<Vec2>& c = cs[group[gi]];
        const usize m = c.size();
        if (m < 3) continue;
        std::vector<Vec3> en;
        edge_normals(c, en);
        f32 perim = 0.0f;
        for (usize i = 0; i < m; ++i) perim += (c[(i + 1) % m] - c[i]).length();
        if (perim < 1e-6f) continue;

        std::vector<f32> u(m);
        f32 arc = 0.0f;
        for (usize i = 0; i < m; ++i) {
            u[i] = arc / perim;
            arc += (c[(i + 1) % m] - c[i]).length();
        }

        // Parede: a silhueta original, de −z0 a +z0, normal horizontal.
        for (usize i = 0; i < m; ++i) {
            if (en[i].length_sq() == 0.0f) continue;
            const usize j = (i + 1) % m;
            if (en[j].length_sq() == 0.0f) continue;
            const Vec2 p0 = c[i], p1 = c[j];
            const Vec3 n0 = edge_end_normal(en, i, (i + m - 1) % m), n1 = edge_end_normal(en, i, j);
            const u32 a0 = out.walls.push(Vec3{p0.x, p0.y, z0}, n0, Vec2{u[i], 1.0f});
            const u32 a1 = out.walls.push(Vec3{p1.x, p1.y, z0}, n1, Vec2{u[j], 1.0f});
            const u32 b0 = out.walls.push(Vec3{p0.x, p0.y, -z0}, n0, Vec2{u[i], 0.0f});
            const u32 b1 = out.walls.push(Vec3{p1.x, p1.y, -z0}, n1, Vec2{u[j], 0.0f});
            out.walls.indices.insert(out.walls.indices.end(), {a0, b0, a1, a1, b0, b1});
        }
        if (!bev) continue;

        // Chanfro: frente (de ±z0 até ±zf) e fundo espelhado. As duas pontas do
        // anel compartilham os vértices com a tampa? Não — a tampa vive noutro
        // chunk justamente porque a normal dela é (0,0,±1) e a do chanfro é
        // inclinada: a aresta entre as duas é viva, como na peça real.
        // Cada quadrilátero emite os seus quatro vértices, como a parede: as
        // normais de uma aresta são as DELA, e o otimizador junta os vértices
        // repetidos no fim.
        for (u32 k = 0; k + 1 < nk; ++k) {
            const f32 A0 = width * slopeAway[k], B0 = bd * slopeRise[k];
            const f32 A1 = width * slopeAway[k + 1], B1 = bd * slopeRise[k + 1];
            f32 away0 = 0, rise0 = 0, away1 = 0, rise1 = 0;
            const f32 t0 = static_cast<f32>(k) / static_cast<f32>(segs);
            const f32 t1 = static_cast<f32>(k + 1) / static_cast<f32>(segs);
            bevel_profile(t0, roundness, away0, rise0);
            bevel_profile(t1, roundness, away1, rise1);
            const f32 zk0 = z0 + bd * rise0, zk1 = z0 + bd * rise1;
            for (usize i = 0; i < m; ++i) {
                const usize j = (i + 1) % m;
                if (en[i].length_sq() == 0.0f || en[j].length_sq() == 0.0f) continue;
                const Vec3 d0 = edge_end_normal(en, i, (i + m - 1) % m), d1 = edge_end_normal(en, i, j);
                const Vec2 p = rings[gi][k][i], q = rings[gi][k][j];
                const Vec2 p1 = rings[gi][k + 1][i], q1 = rings[gi][k + 1][j];
                const Vec3 np0 = bevel_normal(d0, A0, B0), nq0 = bevel_normal(d1, A0, B0);
                const Vec3 np1 = bevel_normal(d0, A1, B1), nq1 = bevel_normal(d1, A1, B1);
                const u32 fp0 = out.bevels.push(Vec3{p.x, p.y, zk0}, np0, Vec2{u[i], 1.0f - t0});
                const u32 fq0 = out.bevels.push(Vec3{q.x, q.y, zk0}, nq0, Vec2{u[j], 1.0f - t0});
                const u32 fp1 = out.bevels.push(Vec3{p1.x, p1.y, zk1}, np1, Vec2{u[i], 1.0f - t1});
                const u32 fq1 = out.bevels.push(Vec3{q1.x, q1.y, zk1}, nq1, Vec2{u[j], 1.0f - t1});
                out.bevels.indices.insert(out.bevels.indices.end(), {fp0, fq0, fp1, fq0, fq1, fp1});
                const u32 bp0 = out.bevels.push(Vec3{p.x, p.y, -zk0}, Vec3{np0.x, np0.y, -np0.z}, Vec2{u[i], t0});
                const u32 bq0 = out.bevels.push(Vec3{q.x, q.y, -zk0}, Vec3{nq0.x, nq0.y, -nq0.z}, Vec2{u[j], t0});
                const u32 bp1 = out.bevels.push(Vec3{p1.x, p1.y, -zk1}, Vec3{np1.x, np1.y, -np1.z}, Vec2{u[i], t1});
                const u32 bq1 = out.bevels.push(Vec3{q1.x, q1.y, -zk1}, Vec3{nq1.x, nq1.y, -nq1.z}, Vec2{u[j], t1});
                out.bevels.indices.insert(out.bevels.indices.end(), {bp0, bp1, bq0, bq0, bp1, bq1});
            }
        }
    }
    return true;
}

/// Teto do chanfro, como fração do recuo que o traço comporta. 1.0 seria o
/// limite exato (as arestas recuadas se encontram) — perto demais: num traço
/// fino a tampa vira uma tira. 0.6 deixa o traço ainda legível e o chanfro
/// visível.
constexpr f32 kBevelLimit = 0.6f;

/// Gera a geometria. `detail` sai preenchido quando não há malha.
std::shared_ptr<const TextMesh> build_text_mesh(const text::Font& font, const Text3DSpec& spec, std::string& detail, i32 glyphIndex = -1) {
    TextData td;
    td.content = spec.content;
    td.size = 100.0f;                 // contorno medido a 100 px e levado a 1 = altura da fonte
    td.alignment = spec.alignment;
    std::vector<std::vector<Vec2>> raw;
    if (!text::outline(font, td, raw, glyphIndex)) {
        detail = "texto sem letras visiveis";
        return nullptr;
    }
    // Y para cima, 1 = altura da fonte.
    std::vector<std::vector<Vec2>> cs(raw.size());
    for (usize i = 0; i < raw.size(); ++i) {
        cs[i].reserve(raw[i].size());
        for (Vec2 p : raw[i]) cs[i].push_back(Vec2{p.x / 100.0f, -p.y / 100.0f});
    }
    const usize n = cs.size();
    std::vector<f64> areas(n);
    std::vector<u32> depth(n, 0);
    for (usize i = 0; i < n; ++i) areas[i] = signed_area(cs[i]);
    for (usize i = 0; i < n; ++i)
        for (usize j = 0; j < n; ++j)
            if (i != j && std::fabs(areas[j]) > std::fabs(areas[i]) && point_in(cs[j], cs[i][0])) ++depth[i];

    // Orientação normalizada: borda anti-horária, furo horário. Com ela a
    // normal da aresta (dy, −dx) sai sempre para fora do material, e o recuo
    // pela bissetriz esquerda sempre entra no material.
    for (usize i = 0; i < n; ++i) {
        const bool hole = depth[i] & 1u;
        if ((areas[i] > 0) == hole) std::reverse(cs[i].begin(), cs[i].end());
    }

    // Cada furo vai para a borda de menor área que o contém.
    std::vector<std::vector<u32>> holesOf(n);
    for (usize h = 0; h < n; ++h) {
        if (!(depth[h] & 1u)) continue;
        usize best = n;
        for (usize k = 0; k < n; ++k) {
            if ((depth[k] & 1u) || depth[k] + 1 != depth[h] || !point_in(cs[k], cs[h][0])) continue;
            if (best == n || std::fabs(areas[k]) < std::fabs(areas[best])) best = k;
        }
        if (best != n) holesOf[best].push_back(static_cast<u32>(h));
    }

    // Caixa do texto: as UV planas da tampa, e a checagem de degenerado.
    Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
    for (const auto& c : cs)
        for (Vec2 p : c) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
    if (n == 0 || lo.x > hi.x) {
        detail = "texto sem letras visiveis";
        return nullptr;
    }
    const Vec2 uvSize{std::max(hi.x - lo.x, 1e-6f), std::max(hi.y - lo.y, 1e-6f)};

    const f32 d = std::max(0.0f, spec.depth);
    const f32 zf = d * 0.5f, zb = -d * 0.5f;
    const bool wantBevel = spec.bevel && d > 1e-4f;
    const u32 segs = wantBevel ? std::clamp(spec.bevelSegments, 1u, 8u) : 0u;

    GroupInput in;
    in.cs = &cs;
    in.holesOf = &holesOf;

    auto merge = [](TextMesh& dst, TextMesh& src) {
        auto move = [](Chunk& d, const Chunk& s) {
            const u32 base = static_cast<u32>(d.positions.size());
            d.positions.insert(d.positions.end(), s.positions.begin(), s.positions.end());
            d.normals.insert(d.normals.end(), s.normals.begin(), s.normals.end());
            d.uv0.insert(d.uv0.end(), s.uv0.begin(), s.uv0.end());
            for (u32 i : s.indices) d.indices.push_back(base + i);
        };
        move(dst.caps, src.caps);
        move(dst.walls, src.walls);
        move(dst.bevels, src.bevels);
    };

    auto mesh = std::make_shared<TextMesh>();
    u32 emitted = 0;
    for (usize o = 0; o < n; ++o) {
        if (depth[o] & 1u) continue;
        // Teto do chanfro: cada contorno tem o seu (um traço fino não pode
        // encolher o chanfro da letra inteira), e o grupo usa o menor.
        f32 width = spec.bevelWidth;
        if (wantBevel) {
            width = std::min(width, kBevelLimit * contour_offset_limit(cs[o]));
            for (u32 h : holesOf[o]) width = std::min(width, kBevelLimit * contour_offset_limit(cs[h]));
        }
        // O recuo é recalculado a cada tentativa: quando o contorno comporta o
        // chanfro pedido, sai de primeira; quando não, ele encolhe até a tampa
        // triangular. Nunca sai malha quebrada — no pior caso sai sem chanfro.
        bool ok = false;
        for (u32 attempt = 0; attempt < 6 && !ok; ++attempt) {
            TextMesh local;
            ok = emit_group(local, in, o, zf, zb, d, width, segs, spec.bevelDepth, spec.bevelRoundness, lo, uvSize);
            if (ok) merge(*mesh, local);
            width *= 0.5f;
        }
        if (!ok) {
            TextMesh local;
            if (emit_group(local, in, o, zf, zb, d, 0.0f, 0, 0.0f, 0.0f, lo, uvSize)) merge(*mesh, local);
        }
        ++emitted;
    }
    if (emitted == 0 || mesh->caps.indices.empty()) {
        detail = "contorno do texto nao triangulou";
        return nullptr;
    }
    compute_tangents(mesh->caps);
    compute_tangents(mesh->walls);
    compute_tangents(mesh->bevels);
    return mesh;
}


// --- cache da geometria ------------------------------------------------------
// A receita geométrica é pequena e o trabalho (contorno do glifo, triangulação
// com furos, chanfro, tangentes e a otimização de vértices) não é. Mexer só na
// cor ou na rugosidade reaproveita a malha já pronta — que é o caso comum,
// porque o painel reenvia a receita a cada arrasto.
struct GeomAsset {
    std::vector<Primitive> primitives;
    std::vector<std::shared_ptr<const GeomAsset>> letters;
    Aabb bounds;        ///< caixa da malha (o `Engine` usa para enquadrar a layer)
    Aabb assetBounds;   ///< caixa da cena, já com o nó — é a que o asset expõe
};

constexpr usize kGeomCacheMax = 8;
std::mutex& geom_cache_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, std::shared_ptr<const GeomAsset>>& geom_cache() {
    static std::unordered_map<std::string, std::shared_ptr<const GeomAsset>> c;
    return c;
}

std::string geom_key(const text::Font& font, const Text3DSpec& s) {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%p|%u|%.5f|%u|%.5f|%.5f|%u|%.4f|%u|%zu", static_cast<const void*>(&font), s.alignment,
                  static_cast<double>(s.depth), s.bevel ? 1u : 0u, static_cast<double>(s.bevelWidth),
                  static_cast<double>(s.bevelDepth), s.bevelSegments, static_cast<double>(s.bevelRoundness),
                  s.regionMaterials ? 1u : 0u, s.content.size());
    return std::string(buf) + (s.animation ? "|animated|" : "|static|") + s.content;
}

void append_chunk(Primitive& p, const Chunk& c) {
    const u32 base = static_cast<u32>(p.positions.size());
    p.positions.insert(p.positions.end(), c.positions.begin(), c.positions.end());
    p.normals.insert(p.normals.end(), c.normals.begin(), c.normals.end());
    p.uv0.insert(p.uv0.end(), c.uv0.begin(), c.uv0.end());
    p.tangents.insert(p.tangents.end(), c.tangents.begin(), c.tangents.end());
    for (u32 i : c.indices) p.indices.push_back(base + i);
}

/// Malha + otimização: o que o cache guarda. Devolve nulo com `detail` cheio.
std::shared_ptr<const GeomAsset> build_geom(const text::Font& font, const Text3DSpec& spec, std::string& detail, i32 glyphIndex = -1) {
    std::shared_ptr<const TextMesh> mesh = build_text_mesh(font, spec, detail, glyphIndex);
    if (!mesh) return nullptr;

    auto asset = std::make_unique<SceneAsset>();
    // Materiais de palha: só para o finalizador saber que nenhuma primitiva é
    // alpha-blend (as cores de verdade entram depois, no build_text3d).
    asset->materials.resize(spec.regionMaterials ? 3u : 1u);
    auto chunk_primitive = [](const Chunk& c, i32 material) {
        Primitive p;
        append_chunk(p, c);
        p.material = material;
        p.generatedTangents = true;
        for (const Vec3& v : p.positions) p.bounds.add(v);
        return p;
    };
    Mesh mesh0;
    if (spec.regionMaterials) {
        if (!mesh->caps.empty()) mesh0.primitives.push_back(chunk_primitive(mesh->caps, 0));
        if (!mesh->walls.empty()) mesh0.primitives.push_back(chunk_primitive(mesh->walls, 1));
        if (!mesh->bevels.empty()) mesh0.primitives.push_back(chunk_primitive(mesh->bevels, 2));
    } else {
        Primitive p;
        append_chunk(p, mesh->caps);
        append_chunk(p, mesh->walls);
        append_chunk(p, mesh->bevels);
        p.material = 0;
        p.generatedTangents = true;
        for (const Vec3& v : p.positions) p.bounds.add(v);
        mesh0.primitives.push_back(std::move(p));
    }
    if (mesh0.primitives.empty()) {
        detail = "contorno do texto nao triangulou";
        return nullptr;
    }
    std::string name = spec.content.substr(0, spec.content.find('\n'));
    if (name.size() > 32) name.resize(32);
    if (name.empty()) name = "Texto 3D";
    mesh0.name = name;
    for (const Primitive& p : mesh0.primitives) {
        mesh0.bounds.add(p.bounds.min);
        mesh0.bounds.add(p.bounds.max);
    }
    asset->sourceName = name;
    asset->meshes.push_back(std::move(mesh0));
    scene3d::Node node;
    node.name = name;
    node.mesh = 0;
    asset->nodes.push_back(node);
    asset->roots.push_back(0);
    ImportOptions o;
    o.generateLods = false;   // o texto não pode perder serifa nem furo ao afastar
    ImportResult fin = finalize_scene_asset(std::move(asset), o);
    if (!fin.ok()) {
        detail = fin.detail.empty() ? std::string("nao foi possivel finalizar a malha") : fin.detail;
        return nullptr;
    }
    auto geom = std::make_shared<GeomAsset>();
    geom->primitives = std::move(fin.asset->meshes[0].primitives);
    geom->bounds = fin.asset->meshes[0].bounds;
    geom->assetBounds = fin.asset->bounds;
    if (spec.animation && glyphIndex < 0) {
        TextData td; td.content = spec.content; td.size = 100; td.alignment = spec.alignment;
        std::vector<text::ShapedGlyph> glyphs;
        text::shaped_glyphs(font, td, glyphs);
        for (usize i = 0; i < glyphs.size(); ++i) {
            std::string unused;
            if (auto letter = build_geom(font, spec, unused, static_cast<i32>(i))) geom->letters.push_back(std::move(letter));
        }
    }
    return geom;
}

Material make_material(const char* name, const Text3DMaterial& m) {
    Material out;
    out.name = name;
    out.baseColor = Vec4{srgb_to_linear(m.color.x), srgb_to_linear(m.color.y), srgb_to_linear(m.color.z), 1.0f};
    out.metallic = std::clamp(m.metallic, 0.0f, 1.0f);
    out.roughness = std::clamp(m.roughness, 0.0f, 1.0f);
    out.specular = std::clamp(m.specular, 0.0f, 1.0f);
    out.specularColor = Vec3{out.specular, out.specular, out.specular};
    out.occlusionStrength = std::clamp(m.occlusion, 0.0f, 1.0f);
    out.emissive = Vec3{srgb_to_linear(m.emissive.x), srgb_to_linear(m.emissive.y), srgb_to_linear(m.emissive.z)};
    out.emissiveStrength = std::max(0.0f, m.emissiveStrength);
    return out;
}

} // namespace

ImportResult build_text3d(const text::Font& font, const Text3DSpec& spec) {
    ImportResult res;
    const std::string key = geom_key(font, spec);
    std::shared_ptr<const GeomAsset> geom;
    {
        std::lock_guard<std::mutex> lock(geom_cache_mutex());
        const auto it = geom_cache().find(key);
        if (it != geom_cache().end()) geom = it->second;
    }
    if (!geom) {
        geom = build_geom(font, spec, res.detail);
        if (!geom) {
            res.error = ImportError::NoGeometry;
            if (res.detail.empty()) res.detail = "texto sem geometria";
            return res;
        }
        std::lock_guard<std::mutex> lock(geom_cache_mutex());
        auto& cache = geom_cache();
        if (cache.size() >= kGeomCacheMax) cache.clear();
        cache[key] = geom;
    }

    auto asset = std::make_unique<SceneAsset>();
    std::string name = spec.content.substr(0, spec.content.find('\n'));
    if (name.size() > 32) name.resize(32);
    asset->sourceName = name.empty() ? "Texto 3D" : name;
    if (spec.regionMaterials) {
        asset->materials.push_back(make_material("frente", spec.front_material()));
        asset->materials.push_back(make_material("lateral", spec.side));
        asset->materials.push_back(make_material("chanfro", spec.bevelMat));
    } else {
        asset->materials.push_back(make_material("texto", spec.front_material()));
    }
    asset->bounds = geom->assetBounds;
    Mesh mesh0;
    mesh0.name = asset->sourceName;
    mesh0.bounds = geom->bounds;           // a caixa da malha vem do cache
    mesh0.primitives = geom->primitives;   // cópia rasa do cache: os materiais são o que muda
    asset->meshes.push_back(std::move(mesh0));
    scene3d::Node node;
    node.name = asset->sourceName;
    node.mesh = 0;
    asset->nodes.push_back(node);
    asset->roots.push_back(0);

    if (spec.animation && !geom->letters.empty()) {
        asset->meshes.clear(); asset->nodes.clear(); asset->roots.clear();
        Animation anim; anim.name = "Letras";
        anim.duration = std::clamp(spec.animationDuration, .2f, 30.f);
        for (usize i = 0; i < geom->letters.size(); ++i) {
            const auto& letter = *geom->letters[i];
            const Vec3 pivot = letter.bounds.center();
            Mesh mesh; mesh.name = "Letra " + std::to_string(i + 1); mesh.primitives = letter.primitives;
            for (auto& prim : mesh.primitives) {
                prim.bounds = Aabb{};
                for (auto& v : prim.positions) { v = v - pivot; prim.bounds.add(v); }
                mesh.bounds.add(prim.bounds);
            }
            asset->meshes.push_back(std::move(mesh));
            Node n; n.mesh = static_cast<i32>(i); n.translation = pivot;
            asset->nodes.push_back(n); asset->roots.push_back(static_cast<i32>(i));
            AnimSampler sampler;
            sampler.components = spec.animation == 1 ? 3 : 4;
            // Fixed keys, sampled by the existing timeline pose evaluator. No
            // tessellation or mesh upload during playback, including scrubbing.
            for (u32 k = 0; k <= 64; ++k) {
                const f32 t = anim.duration * static_cast<f32>(k) / 64.f;
                const f32 phase = 6.283185307f * (t / anim.duration - static_cast<f32>(i) * spec.animationStagger);
                const f32 amount = std::sin(phase) * std::clamp(spec.animationAmount, 0.f, 2.f);
                sampler.times.push_back(t);
                if (spec.animation == 1) {
                    sampler.values.insert(sampler.values.end(), {pivot.x, pivot.y + amount, pivot.z});
                } else {
                    Vec3 axis = spec.animation == 2 ? Vec3{1,0,0} : spec.animation == 3 ? Vec3{0,1,0} : Vec3{0,0,1};
                    const Quat q = Quat::from_axis_angle(axis, amount * 6.283185307f);
                    sampler.values.insert(sampler.values.end(), {q.x, q.y, q.z, q.w});
                }
            }
            anim.channels.push_back({static_cast<i32>(i), spec.animation == 1 ? AnimPath::Translation : AnimPath::Rotation, static_cast<u32>(anim.samplers.size())});
            anim.samplers.push_back(std::move(sampler));
        }
        asset->animations.push_back(std::move(anim));
    }
    res.error = ImportError::None;
    res.asset = std::move(asset);
    return res;
}

} // namespace aurea::scene3d
