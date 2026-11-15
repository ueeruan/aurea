// =============================================================================
//  Aurea / vector / VectorTreeDocument.cpp
//
//  Lote X do AE: o que a árvore de formas acrescenta ao documento vetorial,
//  como fluxo de floats À PARTE (seção VectorTree do projeto). O documento de
//  sempre (VectorDocument.cpp, versão 6) não muda: os painéis do celular o
//  leem e recusam versão desconhecida. Formato em vector/VectorTree.hpp.
//
//  Um grupo de dentro vai inteiro: o nome (bytes), o documento de sempre de um
//  grupo só (o MESMO codec, encode_document/decode_document) e os extras dele.
// =============================================================================
#include "aurea/vector/VectorTree.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::vector {
namespace {

constexpr f32 kTreeVersion = 1.0f;
constexpr u32 kMaxDepth = 16;

struct Reader {
    const f32* d;
    usize n;
    usize p = 0;
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

void get_paint(Reader& in, VectorPaint& p) {
    p.type = in.u(2);
    p.color = in.v4();
    p.start = in.v2();
    p.end = in.v2();
    p.opacity = in.f();
    const u32 n = in.count(64);
    p.stops.resize(n);
    for (VectorStop& s : p.stops) { s.pos = in.f(); s.color = in.v4(); }
}

void put_stroke(const VectorStroke& s, std::vector<f32>& o) {
    o.insert(o.end(), {s.enabled ? 1.0f : 0.0f, s.width, static_cast<f32>(s.cap), static_cast<f32>(s.join), s.miterLimit, s.dashOffset,
                       static_cast<f32>(s.dashes.size())});
    o.insert(o.end(), s.dashes.begin(), s.dashes.end());
    put_paint(s.paint, o);
    o.insert(o.end(), {s.taperStart, s.taperEnd, s.taperEase, static_cast<f32>(s.widthProfile.size())});
    o.insert(o.end(), s.widthProfile.begin(), s.widthProfile.end());
}

void get_stroke(Reader& in, VectorStroke& s) {
    s.enabled = in.b();
    s.width = std::max(0.0f, in.f());
    s.cap = in.u(2);
    s.join = in.u(2);
    s.miterLimit = std::max(1.0f, in.f());
    s.dashOffset = in.f();
    const u32 nd = in.count(64);
    s.dashes.resize(nd);
    for (f32& x : s.dashes) x = std::max(0.0f, in.f());
    get_paint(in, s.paint);
    s.taperStart = std::clamp(in.f(), 0.0f, 100.0f);
    s.taperEnd = std::clamp(in.f(), 0.0f, 100.0f);
    s.taperEase = std::clamp(in.f(), 0.0f, 100.0f);
    const u32 np = in.count(4096);
    s.widthProfile.resize(np);
    for (f32& x : s.widthProfile) x = std::clamp(in.f(), 0.0f, 1.0f);
}

void put_repeater(const VectorRepeater& r, std::vector<f32>& o) {
    o.insert(o.end(), {r.enabled ? 1.0f : 0.0f, r.copies, r.offset, r.anchor.x, r.anchor.y, r.position.x, r.position.y, r.scale,
                       r.rotation, r.startOpacity, r.endOpacity, static_cast<f32>(r.above), r.scaleY, r.scaleLinked ? 1.0f : 0.0f});
}

void get_repeater(Reader& in, VectorRepeater& r) {
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
    r.scaleY = in.f();
    r.scaleLinked = in.b() ? 1 : 0;
}

void put_extras(const VectorGroup& g, std::vector<f32>& o, u32 depth) {
    o.insert(o.end(), {g.skew, g.skewAxis, g.freeOrder ? 1.0f : 0.0f, static_cast<f32>(g.order.size())});
    for (VectorItemKind k : g.order) o.push_back(static_cast<f32>(k));
    o.push_back(static_cast<f32>(g.moreFills.size()));
    for (const VectorFill& f : g.moreFills) {
        o.insert(o.end(), {f.enabled ? 1.0f : 0.0f, static_cast<f32>(f.rule)});
        put_paint(f.paint, o);
    }
    o.push_back(static_cast<f32>(g.moreStrokes.size()));
    for (const VectorStroke& s : g.moreStrokes) put_stroke(s, o);
    o.push_back(static_cast<f32>(g.moreTrims.size()));
    for (const VectorTrim& t : g.moreTrims) o.insert(o.end(), {t.enabled ? 1.0f : 0.0f, t.start, t.end, t.offset, static_cast<f32>(t.mode)});
    o.push_back(static_cast<f32>(g.moreRepeaters.size()));
    for (const VectorRepeater& r : g.moreRepeaters) put_repeater(r, o);
    o.push_back(static_cast<f32>(g.moreMerges.size()));
    for (u8 m : g.moreMerges) o.push_back(static_cast<f32>(m));
    // Grupos de dentro: além do limite de profundidade não são gravados.
    const usize children = depth + 1 < kMaxDepth ? g.children.size() : 0;
    o.push_back(static_cast<f32>(children));
    for (usize i = 0; i < children; ++i) {
        const VectorGroup& c = g.children[i];
        o.push_back(static_cast<f32>(c.name.size()));
        for (char ch : c.name) o.push_back(static_cast<f32>(static_cast<u8>(ch)));
        VectorData one;
        one.groups.push_back(c);
        std::vector<f32> doc;
        std::string names;
        encode_document(one, doc, names);
        o.push_back(static_cast<f32>(doc.size()));
        o.insert(o.end(), doc.begin(), doc.end());
        put_extras(c, o, depth + 1);
    }
}

bool get_extras(Reader& in, VectorGroup& g, u32 depth) {
    g.skew = std::clamp(in.f(), -85.0f, 85.0f);
    g.skewAxis = in.f();
    g.freeOrder = in.b();
    const u32 no = in.count(100000);
    g.order.clear();
    g.order.reserve(no);
    for (u32 i = 0; i < no && in.ok; ++i) {
        const f32 k = in.f();
        // Tipo desconhecido (árvore de uma versão mais nova): a entrada sai.
        if (k >= 0.0f && k < static_cast<f32>(VectorItemKind::Count)) g.order.push_back(static_cast<VectorItemKind>(static_cast<u8>(k)));
    }
    const u32 nf = in.count(64);
    g.moreFills.assign(nf, VectorFill{});
    for (VectorFill& f : g.moreFills) {
        f.enabled = in.b();
        f.rule = in.u(1);
        get_paint(in, f.paint);
    }
    const u32 ns = in.count(64);
    g.moreStrokes.assign(ns, VectorStroke{});
    for (VectorStroke& s : g.moreStrokes) get_stroke(in, s);
    const u32 nt = in.count(64);
    g.moreTrims.assign(nt, VectorTrim{});
    for (VectorTrim& t : g.moreTrims) {
        t.enabled = in.b();
        t.start = in.f();
        t.end = in.f();
        t.offset = in.f();
        t.mode = in.u(1);
    }
    const u32 nr = in.count(16);
    g.moreRepeaters.assign(nr, VectorRepeater{});
    for (VectorRepeater& r : g.moreRepeaters) get_repeater(in, r);
    const u32 nm = in.count(64);
    g.moreMerges.assign(nm, 0);
    for (u8& m : g.moreMerges) m = in.u(4);
    const u32 nc = in.count(256);
    if (!in.ok || (nc > 0 && depth + 1 >= kMaxDepth)) return false;
    g.children.clear();
    g.children.reserve(nc);
    for (u32 i = 0; i < nc; ++i) {
        const u32 nameBytes = in.count(4096);
        std::string name;
        name.reserve(nameBytes);
        for (u32 k = 0; k < nameBytes && in.ok; ++k) name.push_back(static_cast<char>(static_cast<u8>(std::clamp(in.f(), 0.0f, 255.0f))));
        const u32 docFloats = in.count(1u << 24);
        if (!in.ok || in.p + docFloats > in.n) return false;
        VectorData one;
        if (!decode_document(in.d + in.p, docFloats, name, one) || one.groups.size() != 1) return false;
        in.p += docFloats;
        VectorGroup child = std::move(one.groups[0]);
        child.name = name;
        if (!get_extras(in, child, depth + 1)) return false;
        g.children.push_back(std::move(child));
    }
    return in.ok;
}

} // namespace

void encode_tree(const VectorData& data, std::vector<f32>& out) {
    out.clear();
    out.push_back(kTreeVersion);
    out.push_back(static_cast<f32>(data.groups.size()));
    for (const VectorGroup& g : data.groups) put_extras(g, out, 0);
}

bool decode_tree(const f32* data, usize count, VectorData& into) {
    if (!data || count < 2) return false;
    Reader in{data, count};
    const f32 version = in.f();
    if (!in.ok || version < 1.0f || version > kTreeVersion) return false;
    const u32 groups = in.count(100000);
    if (!in.ok || groups != into.groups.size()) return false;
    VectorData d = into;   // um fluxo cortado não deixa nada pela metade
    for (u32 gi = 0; gi < groups; ++gi) {
        if (!get_extras(in, d.groups[gi], 0)) return false;
    }
    if (!in.ok) return false;
    into = std::move(d);
    return true;
}

void keep_tree(const VectorGroup& from, VectorGroup& into) {
    into.skew = from.skew;
    into.skewAxis = from.skewAxis;
    into.moreFills = from.moreFills;
    into.moreStrokes = from.moreStrokes;
    into.moreTrims = from.moreTrims;
    into.moreRepeaters = from.moreRepeaters;
    into.moreMerges = from.moreMerges;
    into.children = from.children;
    into.freeOrder = from.freeOrder;
    into.order = from.order;
}

} // namespace aurea::vector
