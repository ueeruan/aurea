// =============================================================================
//  Aurea / vector / VectorDocument.cpp
//
//  O VectorData como fluxo de floats. Mesma ordem na ponte com a UI
//  (engine/AureaEngine.kt → VectorDoc.kt) e no projeto (Serialization.cpp,
//  v19). Versão no primeiro float; campos novos entram no FIM de cada bloco
//  com versão nova.
//
//   doc    : versão, nº de grupos, grupo…
//   grupo  : visível, mesclar,
//            transform (pos x/y, âncora x/y, escala x/y, giro, opacidade),
//            preencher (liga, regra, tinta),
//            contorno (liga, largura, ponta, junta, miter, desloc. do traço,
//                      nº de traços, traços…, tinta),
//            aparar (liga, início, fim, deslocamento, modo),
//            repetidor (liga, cópias, desloc., âncora x/y, posição x/y,
//                       escala, giro, opac. inicial/final, por cima),
//            nº de caminhos, caminho…
//   tinta  : tipo, rgba, início x/y, fim x/y, opacidade, nº de paradas,
//            parada… (posição, rgba)
//   caminho: tipo, invertido, centro x/y, tamanho x/y, arredondar, pontas,
//            raio externo/interno, arredondar externo/interno, giro,
//            bezier, nº de keyframes, keyframe… (quadro, ease, bezier)
//   bezier : fechado, n, n × (p x/y, in x/y, out x/y)
// =============================================================================
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::vector {
namespace {
constexpr f32 kDocVersion = 1.0f;

struct In {
    const f32* d;
    usize n;
    usize& p;
    bool ok = true;
    f32 f() {
        if (p >= n) { ok = false; return 0.0f; }
        const f32 v = d[p++];
        if (!std::isfinite(v)) { ok = false; return 0.0f; }
        return v;
    }
    u32 count(u32 max) {
        const f32 v = f();
        if (!ok || v < 0.0f || v > static_cast<f32>(max)) { ok = false; return 0; }
        return static_cast<u32>(v);
    }
    bool b() { return f() > 0.5f; }
    u8 u(u8 max) { return static_cast<u8>(std::clamp(f(), 0.0f, static_cast<f32>(max))); }
    Vec2 v2() { const f32 x = f(); return Vec2{x, f()}; }
    Vec4 v4() { const f32 x = f(), y = f(), z = f(); return Vec4{x, y, z, f()}; }
};

void put_paint(const VectorPaint& p, std::vector<f32>& o) {
    o.insert(o.end(), {static_cast<f32>(p.type), p.color.x, p.color.y, p.color.z, p.color.w, p.start.x, p.start.y, p.end.x, p.end.y,
                       p.opacity, static_cast<f32>(p.stops.size())});
    for (const VectorStop& s : p.stops) o.insert(o.end(), {s.pos, s.color.x, s.color.y, s.color.z, s.color.w});
}

bool get_paint(In& in, VectorPaint& p) {
    p.type = in.u(2);
    p.color = in.v4();
    p.start = in.v2();
    p.end = in.v2();
    p.opacity = in.f();
    const u32 n = in.count(64);
    p.stops.resize(n);
    for (VectorStop& s : p.stops) { s.pos = in.f(); s.color = in.v4(); }
    return in.ok;
}
} // namespace

void encode_path(const BezierPath& p, std::vector<f32>& o) {
    o.push_back(p.closed ? 1.0f : 0.0f);
    o.push_back(static_cast<f32>(p.v.size()));
    for (const BezierVertex& v : p.v) o.insert(o.end(), {v.p.x, v.p.y, v.in.x, v.in.y, v.out.x, v.out.y});
}

bool decode_path(const f32* data, usize count, usize& pos, BezierPath& out) {
    In in{data, count, pos};
    out.closed = in.b();
    const u32 n = in.count(1000000);
    if (!in.ok || pos + static_cast<usize>(n) * 6 > count) return false;
    out.v.resize(n);
    for (BezierVertex& v : out.v) { v.p = in.v2(); v.in = in.v2(); v.out = in.v2(); }
    return in.ok;
}

void encode_document(const VectorData& data, std::vector<f32>& o, std::string& names) {
    o.clear();
    names.clear();
    o.push_back(kDocVersion);
    o.push_back(static_cast<f32>(data.groups.size()));
    for (usize gi = 0; gi < data.groups.size(); ++gi) {
        const VectorGroup& g = data.groups[gi];
        std::string nm = g.name;
        std::replace(nm.begin(), nm.end(), '\n', ' ');
        if (gi) names.push_back('\n');
        names += nm;
        o.insert(o.end(), {g.visible ? 1.0f : 0.0f, static_cast<f32>(g.merge), g.position.x, g.position.y, g.anchor.x, g.anchor.y,
                           g.scale.x, g.scale.y, g.rotation, g.opacity});
        o.insert(o.end(), {g.fill.enabled ? 1.0f : 0.0f, static_cast<f32>(g.fill.rule)});
        put_paint(g.fill.paint, o);
        const VectorStroke& s = g.stroke;
        o.insert(o.end(), {s.enabled ? 1.0f : 0.0f, s.width, static_cast<f32>(s.cap), static_cast<f32>(s.join), s.miterLimit, s.dashOffset,
                           static_cast<f32>(s.dashes.size())});
        o.insert(o.end(), s.dashes.begin(), s.dashes.end());
        put_paint(s.paint, o);
        const VectorTrim& t = g.trim;
        o.insert(o.end(), {t.enabled ? 1.0f : 0.0f, t.start, t.end, t.offset, static_cast<f32>(t.mode)});
        const VectorRepeater& r = g.repeater;
        o.insert(o.end(), {r.enabled ? 1.0f : 0.0f, r.copies, r.offset, r.anchor.x, r.anchor.y, r.position.x, r.position.y, r.scale,
                           r.rotation, r.startOpacity, r.endOpacity, static_cast<f32>(r.above)});
        o.push_back(static_cast<f32>(g.paths.size()));
        for (const VectorPath& p : g.paths) {
            o.insert(o.end(), {static_cast<f32>(p.kind), p.reversed ? 1.0f : 0.0f, p.center.x, p.center.y, p.size.x, p.size.y, p.roundness,
                               p.points, p.outerRadius, p.innerRadius, p.outerRoundness, p.innerRoundness, p.rotation});
            encode_path(p.path, o);
            o.push_back(static_cast<f32>(p.keys.size()));
            for (const PathKey& k : p.keys) {
                o.push_back(static_cast<f32>(k.frame));
                o.push_back(static_cast<f32>(k.ease));
                encode_path(k.path, o);
            }
        }
    }
}

bool decode_document(const f32* data, usize count, const std::string& names, VectorData& out) {
    usize pos = 0;
    In in{data, count, pos};
    if (!data || count < 2) return false;
    const f32 version = in.f();
    if (!in.ok || version < 1.0f || version > kDocVersion) return false;
    const u32 groups = in.count(100000);
    VectorData d;
    d.groups.resize(groups);
    usize nameStart = 0;
    for (u32 gi = 0; gi < groups && in.ok; ++gi) {
        VectorGroup& g = d.groups[gi];
        const usize nl = names.find('\n', nameStart);
        g.name = names.substr(nameStart, nl == std::string::npos ? std::string::npos : nl - nameStart);
        nameStart = nl == std::string::npos ? names.size() : nl + 1;
        if (g.name.empty()) g.name = "Grupo " + std::to_string(gi + 1);
        g.visible = in.b();
        g.merge = in.u(4);
        g.position = in.v2();
        g.anchor = in.v2();
        g.scale = in.v2();
        g.rotation = in.f();
        g.opacity = in.f();
        g.fill.enabled = in.b();
        g.fill.rule = in.u(1);
        if (!get_paint(in, g.fill.paint)) return false;
        VectorStroke& s = g.stroke;
        s.enabled = in.b();
        s.width = std::max(0.0f, in.f());
        s.cap = in.u(2);
        s.join = in.u(2);
        s.miterLimit = std::max(1.0f, in.f());
        s.dashOffset = in.f();
        const u32 nd = in.count(64);
        s.dashes.resize(nd);
        for (f32& x : s.dashes) x = std::max(0.0f, in.f());
        if (!get_paint(in, s.paint)) return false;
        VectorTrim& t = g.trim;
        t.enabled = in.b();
        t.start = in.f();
        t.end = in.f();
        t.offset = in.f();
        t.mode = in.u(1);
        VectorRepeater& r = g.repeater;
        r.enabled = in.b();
        r.copies = in.f();
        r.offset = in.f();
        r.anchor = in.v2();
        r.position = in.v2();
        r.scale = in.f();
        r.rotation = in.f();
        r.startOpacity = in.f();
        r.endOpacity = in.f();
        r.above = in.u(1);
        const u32 np = in.count(100000);
        g.paths.resize(np);
        for (VectorPath& p : g.paths) {
            p.kind = static_cast<VectorPathKind>(in.u(4));
            p.reversed = in.b();
            p.center = in.v2();
            p.size = in.v2();
            p.roundness = in.f();
            p.points = in.f();
            p.outerRadius = in.f();
            p.innerRadius = in.f();
            p.outerRoundness = in.f();
            p.innerRoundness = in.f();
            p.rotation = in.f();
            if (!in.ok || !decode_path(data, count, pos, p.path)) return false;
            const u32 nk = in.count(100000);
            p.keys.resize(nk);
            for (PathKey& k : p.keys) {
                k.frame = static_cast<i64>(std::llround(in.f()));
                k.ease = in.u(2);
                if (!in.ok || !decode_path(data, count, pos, k.path)) return false;
            }
            std::stable_sort(p.keys.begin(), p.keys.end(), [](const PathKey& a, const PathKey& b) { return a.frame < b.frame; });
        }
    }
    if (!in.ok) return false;
    out = std::move(d);
    return true;
}

} // namespace aurea::vector
