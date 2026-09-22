// =============================================================================
//  Aurea / vector / Svg.cpp
//
//  Importação de SVG (subconjunto prático) para grupos vetoriais. XML lido
//  numa árvore mínima; degradês coletados antes (podem vir depois do uso);
//  cada forma vira um grupo com os caminhos já no espaço do desenho (a
//  cadeia de transforms e o viewBox aplicados nos pontos de controle — afim
//  preserva bezier). Arcos viram cúbicas de no máximo 90°.
//
//  Fora do subconjunto (ignorado sem quebrar): CSS em <style>, <use>,
//  <text>, filtros, máscaras, clipPath, padrões.
// =============================================================================
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace aurea::vector {
namespace {

struct XNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<XNode> kids;
    const std::string* attr(const char* n) const {
        for (const auto& a : attrs) if (a.first == n) return &a.second;
        return nullptr;
    }
};

std::string decode_entities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string o;
    for (usize i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            const usize e = s.find(';', i);
            if (e != std::string::npos && e - i < 8) {
                const std::string ent = s.substr(i + 1, e - i - 1);
                char c = 0;
                if (ent == "amp") c = '&'; else if (ent == "lt") c = '<'; else if (ent == "gt") c = '>';
                else if (ent == "quot") c = '"'; else if (ent == "apos") c = '\'';
                if (c) { o.push_back(c); i = e; continue; }
            }
        }
        o.push_back(s[i]);
    }
    return o;
}

/// Árvore XML mínima (tags, atributos; texto ignorado).
bool parse_xml(const std::string& s, XNode& root) {
    std::vector<XNode*> stack{&root};
    usize i = 0;
    const usize n = s.size();
    while (i < n) {
        const usize lt = s.find('<', i);
        if (lt == std::string::npos) break;
        i = lt;
        if (s.compare(i, 4, "<!--") == 0) { const usize e = s.find("-->", i + 4); i = e == std::string::npos ? n : e + 3; continue; }
        if (s.compare(i, 9, "<![CDATA[") == 0) { const usize e = s.find("]]>", i); i = e == std::string::npos ? n : e + 3; continue; }
        if (s.compare(i, 2, "<?") == 0) { const usize e = s.find("?>", i); i = e == std::string::npos ? n : e + 2; continue; }
        if (s.compare(i, 2, "<!") == 0) {
            // DOCTYPE (pode ter [ ... ] com '>' dentro).
            int depth = 0;
            for (++i; i < n; ++i) {
                if (s[i] == '[') ++depth; else if (s[i] == ']') --depth;
                else if (s[i] == '>' && depth <= 0) { ++i; break; }
            }
            continue;
        }
        if (s.compare(i, 2, "</") == 0) {
            const usize e = s.find('>', i);
            if (stack.size() > 1) stack.pop_back();
            i = e == std::string::npos ? n : e + 1;
            continue;
        }
        ++i;
        XNode node;
        while (i < n && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') node.name.push_back(s[i++]);
        // Prefixo de namespace fora (svg:path → path).
        if (const usize c = node.name.find(':'); c != std::string::npos) node.name = node.name.substr(c + 1);
        bool selfClose = false;
        while (i < n) {
            while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i >= n) break;
            if (s[i] == '>') { ++i; break; }
            if (s[i] == '/') { selfClose = true; ++i; continue; }
            std::string key;
            while (i < n && s[i] != '=' && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') key.push_back(s[i++]);
            while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            std::string val;
            if (i < n && s[i] == '=') {
                ++i;
                while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                if (i < n && (s[i] == '"' || s[i] == '\'')) {
                    const char q = s[i++];
                    const usize e = s.find(q, i);
                    val = s.substr(i, (e == std::string::npos ? n : e) - i);
                    i = e == std::string::npos ? n : e + 1;
                }
            }
            if (!key.empty()) node.attrs.emplace_back(key, decode_entities(val));
        }
        XNode* parent = stack.back();
        parent->kids.push_back(std::move(node));
        if (!selfClose) stack.push_back(&parent->kids.back());
    }
    return !root.kids.empty();
}

// --- números -----------------------------------------------------------------
struct NumReader {
    const char* p;
    const char* e;
    void skip() { while (p < e && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p; }
    bool number(f32& out) {
        skip();
        if (p >= e) return false;
        const char* s = p;
        if (*p == '+' || *p == '-') ++p;
        bool digits = false, dot = false;
        while (p < e && (std::isdigit(static_cast<unsigned char>(*p)) || (*p == '.' && !dot))) { if (*p == '.') dot = true; else digits = true; ++p; }
        if (!digits) { p = s; return false; }
        if (p < e && (*p == 'e' || *p == 'E') && p + 1 < e && (std::isdigit(static_cast<unsigned char>(p[1])) || p[1] == '-' || p[1] == '+')) {
            ++p;
            if (*p == '+' || *p == '-') ++p;
            while (p < e && std::isdigit(static_cast<unsigned char>(*p))) ++p;
        }
        out = std::strtof(std::string(s, p).c_str(), nullptr);
        return true;
    }
    bool flag(f32& out) {
        skip();
        if (p < e && (*p == '0' || *p == '1')) { out = *p == '1' ? 1.0f : 0.0f; ++p; return true; }
        return false;
    }
};

f32 parse_length(const std::string* s, f32 ref, f32 fallback) {
    if (!s || s->empty()) return fallback;
    const char* c = s->c_str();
    char* end = nullptr;
    const f32 v = std::strtof(c, &end);
    if (end == c) return fallback;
    const std::string unit(end);
    if (unit.rfind('%', 0) == 0) return v * 0.01f * ref;
    if (unit.rfind("mm", 0) == 0) return v * 96.0f / 25.4f;
    if (unit.rfind("cm", 0) == 0) return v * 96.0f / 2.54f;
    if (unit.rfind("in", 0) == 0) return v * 96.0f;
    if (unit.rfind("pt", 0) == 0) return v * 96.0f / 72.0f;
    if (unit.rfind("pc", 0) == 0) return v * 16.0f;
    return v;
}

Affine2 parse_transform(const std::string& s) {
    Affine2 m;
    usize i = 0;
    while (i < s.size()) {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i;
        const usize open = s.find('(', i);
        if (open == std::string::npos) break;
        std::string fn = s.substr(i, open - i);
        fn.erase(std::remove_if(fn.begin(), fn.end(), [](char ch) { return std::isspace(static_cast<unsigned char>(ch)); }), fn.end());
        const usize close = s.find(')', open);
        if (close == std::string::npos) break;
        NumReader r{s.c_str() + open + 1, s.c_str() + close};
        f32 v[6]{};
        int k = 0;
        while (k < 6 && r.number(v[k])) ++k;
        Affine2 t;
        if (fn == "matrix" && k == 6) t = Affine2{v[0], v[1], v[2], v[3], v[4], v[5]};
        else if (fn == "translate") t = Affine2::translate(Vec2{v[0], k > 1 ? v[1] : 0.0f});
        else if (fn == "scale") t = Affine2::scale(Vec2{v[0], k > 1 ? v[1] : v[0]});
        else if (fn == "rotate") {
            t = Affine2::rotate(v[0]);
            if (k >= 3) t = Affine2::translate(Vec2{v[1], v[2]}) * t * Affine2::translate(Vec2{-v[1], -v[2]});
        } else if (fn == "skewX") t = Affine2{1, 0, std::tan(v[0] * kDeg2Rad), 1, 0, 0};
        else if (fn == "skewY") t = Affine2{1, std::tan(v[0] * kDeg2Rad), 0, 1, 0, 0};
        m = m * t;
        i = close + 1;
    }
    return m;
}

// --- cor ---------------------------------------------------------------------
bool parse_color(std::string s, Vec4& out) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char ch) { return std::isspace(static_cast<unsigned char>(ch)); }), s.end());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (s.empty()) return false;
    auto hex = [](char h) { return h >= 'a' ? h - 'a' + 10 : h - '0'; };
    if (s[0] == '#') {
        if (s.size() == 4 || s.size() == 5) {
            out = Vec4{hex(s[1]) * 17 / 255.0f, hex(s[2]) * 17 / 255.0f, hex(s[3]) * 17 / 255.0f, s.size() == 5 ? hex(s[4]) * 17 / 255.0f : 1.0f};
            return true;
        }
        if (s.size() == 7 || s.size() == 9) {
            auto b = [&](usize i) { return static_cast<f32>(hex(s[i]) * 16 + hex(s[i + 1])) / 255.0f; };
            out = Vec4{b(1), b(3), b(5), s.size() == 9 ? b(7) : 1.0f};
            return true;
        }
        return false;
    }
    if (s.rfind("rgb", 0) == 0) {
        const usize o = s.find('('), c = s.find(')');
        if (o == std::string::npos || c == std::string::npos) return false;
        const std::string body = s.substr(o + 1, c - o - 1);
        f32 v[4]{0, 0, 0, 1};
        usize pos = 0;
        for (int k = 0; k < 4 && pos <= body.size(); ++k) {
            usize comma = body.find_first_of(",/", pos);
            const std::string tok = body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (tok.empty()) break;
            f32 x = std::strtof(tok.c_str(), nullptr);
            if (tok.back() == '%') x = x * (k < 3 ? 2.55f : 0.01f);
            v[k] = k < 3 ? x / 255.0f : x;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        out = Vec4{std::clamp(v[0], 0.0f, 1.0f), std::clamp(v[1], 0.0f, 1.0f), std::clamp(v[2], 0.0f, 1.0f), std::clamp(v[3], 0.0f, 1.0f)};
        return true;
    }
    struct Named { const char* n; u32 rgb; };
    static constexpr Named kNamed[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000}, {"lime", 0x00FF00},
        {"blue", 0x0000FF}, {"yellow", 0xFFFF00}, {"cyan", 0x00FFFF}, {"aqua", 0x00FFFF}, {"magenta", 0xFF00FF},
        {"fuchsia", 0xFF00FF}, {"gray", 0x808080}, {"grey", 0x808080}, {"silver", 0xC0C0C0}, {"maroon", 0x800000},
        {"olive", 0x808000}, {"navy", 0x000080}, {"purple", 0x800080}, {"teal", 0x008080}, {"orange", 0xFFA500},
        {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"indigo", 0x4B0082}, {"violet", 0xEE82EE},
        {"darkgray", 0xA9A9A9}, {"lightgray", 0xD3D3D3}, {"currentcolor", 0x000000},
    };
    for (const Named& c : kNamed) {
        if (s == c.n) {
            out = Vec4{static_cast<f32>((c.rgb >> 16) & 255) / 255.0f, static_cast<f32>((c.rgb >> 8) & 255) / 255.0f,
                       static_cast<f32>(c.rgb & 255) / 255.0f, 1.0f};
            return true;
        }
    }
    if (s == "transparent") { out = Vec4{0, 0, 0, 0}; return true; }
    return false;
}

// --- estilo ------------------------------------------------------------------
struct SPaint {
    u8 kind = 1;          ///< 0 nenhum, 1 cor, 2 url
    Vec4 color{0, 0, 0, 1};
    std::string url;
};
struct Style {
    SPaint fill;
    SPaint stroke{0, {0, 0, 0, 1}, {}};
    f32 strokeWidth = 1.0f, fillOpacity = 1.0f, strokeOpacity = 1.0f, opacity = 1.0f, miter = 4.0f;
    u8 fillRule = 0, cap = 0, join = 0;
    std::vector<f32> dashes;
    f32 dashOffset = 0.0f;
    bool display = true;
};

SPaint parse_paint(const std::string& v, const SPaint& inherited) {
    SPaint p;
    std::string t = v;
    t.erase(0, t.find_first_not_of(" \t\n\r"));
    if (t.empty() || t == "inherit") return inherited;
    if (t == "none") { p.kind = 0; return p; }
    if (t.rfind("url(", 0) == 0) {
        const usize h = t.find('#'), c = t.find(')');
        p.kind = 2;
        if (h != std::string::npos && c != std::string::npos && c > h) p.url = t.substr(h + 1, c - h - 1);
        // Reserva depois do url(): usada se o degradê não existir.
        Vec4 fb;
        if (c != std::string::npos && parse_color(t.substr(c + 1), fb)) p.color = fb;
        return p;
    }
    Vec4 c;
    if (parse_color(t, c)) { p.kind = 1; p.color = c; return p; }
    return inherited;
}

void apply_prop(Style& st, const std::string& k, const std::string& v, f32 ref) {
    auto num = [&](f32 fb) { return parse_length(&v, ref, fb); };
    if (k == "fill") st.fill = parse_paint(v, st.fill);
    else if (k == "stroke") st.stroke = parse_paint(v, st.stroke);
    else if (k == "stroke-width") st.strokeWidth = std::max(0.0f, num(st.strokeWidth));
    else if (k == "fill-opacity") st.fillOpacity = std::clamp(num(1.0f), 0.0f, 1.0f);
    else if (k == "stroke-opacity") st.strokeOpacity = std::clamp(num(1.0f), 0.0f, 1.0f);
    else if (k == "opacity") st.opacity *= std::clamp(num(1.0f), 0.0f, 1.0f);
    else if (k == "fill-rule") st.fillRule = v.find("evenodd") != std::string::npos ? 1 : 0;
    else if (k == "stroke-linecap") st.cap = v.find("round") != std::string::npos ? 1 : v.find("square") != std::string::npos ? 2 : 0;
    else if (k == "stroke-linejoin") st.join = v.find("round") != std::string::npos ? 1 : v.find("bevel") != std::string::npos ? 2 : 0;
    else if (k == "stroke-miterlimit") st.miter = std::max(1.0f, num(4.0f));
    else if (k == "stroke-dashoffset") st.dashOffset = num(0.0f);
    else if (k == "stroke-dasharray") {
        st.dashes.clear();
        if (v.find("none") == std::string::npos) {
            NumReader r{v.c_str(), v.c_str() + v.size()};
            f32 x;
            while (r.number(x)) st.dashes.push_back(std::max(0.0f, x));
        }
    } else if (k == "display") st.display = v.find("none") == std::string::npos;
    else if (k == "visibility") { if (v.find("hidden") != std::string::npos) st.display = false; }
}

void apply_style(Style& st, const XNode& n, f32 ref) {
    static const char* kProps[] = {"fill", "stroke", "stroke-width", "fill-opacity", "stroke-opacity", "opacity", "fill-rule",
                                   "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "stroke-dasharray", "stroke-dashoffset",
                                   "display", "visibility"};
    for (const char* p : kProps) if (const std::string* v = n.attr(p)) apply_prop(st, p, *v, ref);
    if (const std::string* s = n.attr("style")) {
        usize i = 0;
        while (i < s->size()) {
            usize semi = s->find(';', i);
            if (semi == std::string::npos) semi = s->size();
            const std::string decl = s->substr(i, semi - i);
            const usize colon = decl.find(':');
            if (colon != std::string::npos) {
                auto trim = [](std::string x) {
                    x.erase(0, x.find_first_not_of(" \t\n\r"));
                    x.erase(x.find_last_not_of(" \t\n\r") + 1);
                    return x;
                };
                apply_prop(st, trim(decl.substr(0, colon)), trim(decl.substr(colon + 1)), ref);
            }
            i = semi + 1;
        }
    }
}

// --- degradês ----------------------------------------------------------------
struct Grad {
    bool linear = true;
    bool userSpace = false;
    bool hasUnits = false;
    Affine2 xf;
    bool hasXf = false;
    std::string x1, y1, x2, y2, cx, cy, r;
    std::vector<VectorStop> stops;
    std::string href;
};

void collect_gradients(const XNode& n, std::unordered_map<std::string, Grad>& out) {
    for (const XNode& k : n.kids) {
        if (k.name == "linearGradient" || k.name == "radialGradient") {
            Grad g;
            g.linear = k.name == "linearGradient";
            if (const std::string* u = k.attr("gradientUnits")) { g.userSpace = *u == "userSpaceOnUse"; g.hasUnits = true; }
            if (const std::string* t = k.attr("gradientTransform")) { g.xf = parse_transform(*t); g.hasXf = true; }
            auto s = [&](const char* a, std::string& d) { if (const std::string* v = k.attr(a)) d = *v; };
            s("x1", g.x1); s("y1", g.y1); s("x2", g.x2); s("y2", g.y2); s("cx", g.cx); s("cy", g.cy); s("r", g.r);
            if (const std::string* h = k.attr("xlink:href")) g.href = h->substr(h->find('#') + 1);
            else if (const std::string* h2 = k.attr("href")) g.href = h2->substr(h2->find('#') + 1);
            for (const XNode& st : k.kids) {
                if (st.name != "stop") continue;
                VectorStop gs;
                gs.pos = std::clamp(parse_length(st.attr("offset"), 1.0f, 0.0f), 0.0f, 1.0f);
                Vec4 c{0, 0, 0, 1};
                f32 op = 1.0f;
                if (const std::string* v = st.attr("stop-color")) parse_color(*v, c);
                if (const std::string* v = st.attr("stop-opacity")) op = std::strtof(v->c_str(), nullptr);
                if (const std::string* v = st.attr("style")) {
                    const usize a = v->find("stop-color:");
                    if (a != std::string::npos) parse_color(v->substr(a + 11, v->find(';', a) == std::string::npos ? std::string::npos : v->find(';', a) - a - 11), c);
                    const usize b = v->find("stop-opacity:");
                    if (b != std::string::npos) op = std::strtof(v->c_str() + b + 13, nullptr);
                }
                c.w *= std::clamp(op, 0.0f, 1.0f);
                gs.color = c;
                if (!g.stops.empty()) gs.pos = std::max(gs.pos, g.stops.back().pos);
                g.stops.push_back(gs);
            }
            if (const std::string* id = k.attr("id")) out[*id] = g;
        }
        collect_gradients(k, out);
    }
}

// --- caminho (d) ---------------------------------------------------------------
struct PathBuilder {
    std::vector<BezierPath> paths;
    BezierPath cur;
    Vec2 start{0, 0};
    bool open = false;
    void move(Vec2 p) {
        flush();
        cur = BezierPath{};
        cur.closed = false;
        cur.v.push_back(BezierVertex{p, {}, {}});
        start = p;
        open = true;
    }
    void ensure(Vec2 at) { if (!open) move(at); }
    void line(Vec2 p) { cur.v.push_back(BezierVertex{p, {}, {}}); }
    void cubic(Vec2 c1, Vec2 c2, Vec2 p) {
        cur.v.back().out = c1 - cur.v.back().p;
        cur.v.push_back(BezierVertex{p, c2 - p, {}});
    }
    void close() {
        if (!open) return;
        cur.closed = true;
        if (cur.v.size() > 1 && (cur.v.back().p - cur.v.front().p).length_sq() < 1e-8f) {
            cur.v.front().in = cur.v.back().in;
            cur.v.pop_back();
        }
        flush();
    }
    void flush() {
        if (open && !cur.v.empty()) paths.push_back(cur);
        cur = BezierPath{};
        open = false;
    }
};

/// Arco elíptico SVG (ponto final) → cúbicas (SVG 1.1, apêndice F.6).
void arc_to(PathBuilder& b, Vec2 p0, f32 rx, f32 ry, f32 phiDeg, bool large, bool sweep, Vec2 p1) {
    if ((p1 - p0).length_sq() < 1e-12f) return;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx < 1e-6f || ry < 1e-6f) { b.line(p1); return; }
    const f64 phi = static_cast<f64>(phiDeg) * 3.14159265358979323846 / 180.0;
    const f64 cp = std::cos(phi), sp = std::sin(phi);
    const f64 dx = (static_cast<f64>(p0.x) - p1.x) * 0.5, dy = (static_cast<f64>(p0.y) - p1.y) * 0.5;
    const f64 x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
    f64 Rx = rx, Ry = ry;
    const f64 lam = (x1 * x1) / (Rx * Rx) + (y1 * y1) / (Ry * Ry);
    if (lam > 1.0) { Rx *= std::sqrt(lam); Ry *= std::sqrt(lam); }
    const f64 num = Rx * Rx * Ry * Ry - Rx * Rx * y1 * y1 - Ry * Ry * x1 * x1;
    const f64 den = Rx * Rx * y1 * y1 + Ry * Ry * x1 * x1;
    f64 coef = den > 0.0 ? std::sqrt(std::max(0.0, num / den)) : 0.0;
    if (large == sweep) coef = -coef;
    const f64 cxp = coef * Rx * y1 / Ry, cyp = -coef * Ry * x1 / Rx;
    const f64 cx = cp * cxp - sp * cyp + (static_cast<f64>(p0.x) + p1.x) * 0.5;
    const f64 cy = sp * cxp + cp * cyp + (static_cast<f64>(p0.y) + p1.y) * 0.5;
    auto ang = [](f64 ux, f64 uy, f64 vx, f64 vy) {
        const f64 a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        return a;
    };
    const f64 th1 = ang(1, 0, (x1 - cxp) / Rx, (y1 - cyp) / Ry);
    f64 dth = ang((x1 - cxp) / Rx, (y1 - cyp) / Ry, (-x1 - cxp) / Rx, (-y1 - cyp) / Ry);
    const f64 twoPi = 2.0 * 3.14159265358979323846;
    if (!sweep && dth > 0) dth -= twoPi;
    else if (sweep && dth < 0) dth += twoPi;
    const int segs = std::max(1, static_cast<int>(std::ceil(std::fabs(dth) / (twoPi / 4.0) - 1e-9)));
    const f64 d = dth / segs;
    const f64 k = 4.0 / 3.0 * std::tan(d / 4.0);
    auto pt = [&](f64 t) {
        const f64 x = Rx * std::cos(t), y = Ry * std::sin(t);
        return Vec2{static_cast<f32>(cp * x - sp * y + cx), static_cast<f32>(sp * x + cp * y + cy)};
    };
    auto der = [&](f64 t) {
        const f64 x = -Rx * std::sin(t), y = Ry * std::cos(t);
        return Vec2{static_cast<f32>(cp * x - sp * y), static_cast<f32>(sp * x + cp * y)};
    };
    for (int i = 0; i < segs; ++i) {
        const f64 t0 = th1 + d * i, t1 = t0 + d;
        const Vec2 a = pt(t0), e = (i + 1 == segs) ? p1 : pt(t1);
        b.cubic(a + der(t0) * static_cast<f32>(k), e - der(t1) * static_cast<f32>(k), e);
    }
}

void parse_d(const std::string& d, PathBuilder& b) {
    NumReader r{d.c_str(), d.c_str() + d.size()};
    Vec2 p{0, 0}, lastCtrl{0, 0};
    char cmd = 0, prev = 0;
    while (true) {
        r.skip();
        if (r.p >= r.e) break;
        if (std::isalpha(static_cast<unsigned char>(*r.p))) { cmd = *r.p++; }
        else if (cmd == 0) break;
        const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
        const char C = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
        const Vec2 base = rel ? p : Vec2{0, 0};
        f32 v[7];
        auto read = [&](int n) { for (int i = 0; i < n; ++i) if (!r.number(v[i])) return false; return true; };
        if (C == 'Z') {
            b.close();
            p = b.start;
            prev = 'Z';
            // Depois do Z, sem M: o próximo subcaminho começa no início.
            r.skip();
            if (r.p < r.e && !std::isalpha(static_cast<unsigned char>(*r.p))) break;
            continue;
        }
        bool ok = true;
        switch (C) {
            case 'M':
                if (!(ok = read(2))) break;
                p = base + Vec2{v[0], v[1]};
                b.move(p);
                cmd = rel ? 'l' : 'L';   // pares seguintes = linhas
                break;
            case 'L':
                if (!(ok = read(2))) break;
                b.ensure(p);
                p = base + Vec2{v[0], v[1]};
                b.line(p);
                break;
            case 'H':
                if (!(ok = read(1))) break;
                b.ensure(p);
                p = Vec2{(rel ? p.x : 0.0f) + v[0], p.y};
                b.line(p);
                break;
            case 'V':
                if (!(ok = read(1))) break;
                b.ensure(p);
                p = Vec2{p.x, (rel ? p.y : 0.0f) + v[0]};
                b.line(p);
                break;
            case 'C': {
                if (!(ok = read(6))) break;
                b.ensure(p);
                const Vec2 c1 = base + Vec2{v[0], v[1]}, c2 = base + Vec2{v[2], v[3]}, e = base + Vec2{v[4], v[5]};
                b.cubic(c1, c2, e);
                lastCtrl = c2;
                p = e;
                break;
            }
            case 'S': {
                if (!(ok = read(4))) break;
                b.ensure(p);
                const char pu = static_cast<char>(std::toupper(static_cast<unsigned char>(prev)));
                const Vec2 c1 = (pu == 'C' || pu == 'S') ? p * 2.0f - lastCtrl : p;
                const Vec2 c2 = base + Vec2{v[0], v[1]}, e = base + Vec2{v[2], v[3]};
                b.cubic(c1, c2, e);
                lastCtrl = c2;
                p = e;
                break;
            }
            case 'Q': {
                if (!(ok = read(4))) break;
                b.ensure(p);
                const Vec2 q = base + Vec2{v[0], v[1]}, e = base + Vec2{v[2], v[3]};
                b.cubic(p + (q - p) * (2.0f / 3.0f), e + (q - e) * (2.0f / 3.0f), e);
                lastCtrl = q;
                p = e;
                break;
            }
            case 'T': {
                if (!(ok = read(2))) break;
                b.ensure(p);
                const char pu = static_cast<char>(std::toupper(static_cast<unsigned char>(prev)));
                const Vec2 q = (pu == 'Q' || pu == 'T') ? p * 2.0f - lastCtrl : p;
                const Vec2 e = base + Vec2{v[0], v[1]};
                b.cubic(p + (q - p) * (2.0f / 3.0f), e + (q - e) * (2.0f / 3.0f), e);
                lastCtrl = q;
                p = e;
                break;
            }
            case 'A': {
                if (!(ok = r.number(v[0]) && r.number(v[1]) && r.number(v[2]) && r.flag(v[3]) && r.flag(v[4]) && r.number(v[5]) && r.number(v[6]))) break;
                b.ensure(p);
                const Vec2 e = base + Vec2{v[5], v[6]};
                arc_to(b, p, v[0], v[1], v[2], v[3] > 0.5f, v[4] > 0.5f, e);
                p = e;
                break;
            }
            default: ok = false; break;
        }
        if (!ok) break;
        prev = C == 'M' ? 'L' : cmd;
        if (C == 'M') prev = 'M';
    }
    b.flush();
}

// --- percurso ------------------------------------------------------------------
struct Ctx {
    std::unordered_map<std::string, Grad> grads;
    SvgResult* out = nullptr;
    Vec2 viewport{0, 0};
    u32 index = 0;
};

void bbox_of(const std::vector<BezierPath>& ps, Vec2& mn, Vec2& mx) {
    bool any = false;
    for (const BezierPath& p : ps) {
        Contour c;
        flatten(p, 0.5f, c);
        for (Vec2 q : c.pts) {
            if (!any) { mn = mx = q; any = true; continue; }
            mn = Vec2{std::min(mn.x, q.x), std::min(mn.y, q.y)};
            mx = Vec2{std::max(mx.x, q.x), std::max(mx.y, q.y)};
        }
    }
    if (!any) mn = mx = Vec2{0, 0};
}

/// Degradê resolvido (href herdado) em espaço do desenho.
bool resolve_gradient(const Ctx& ctx, const std::string& id, const std::vector<BezierPath>& userPaths, const Affine2& ctm, VectorPaint& paint) {
    auto it = ctx.grads.find(id);
    if (it == ctx.grads.end()) return false;
    Grad g = it->second;
    for (int depth = 0; depth < 8 && !g.href.empty(); ++depth) {
        auto h = ctx.grads.find(g.href);
        if (h == ctx.grads.end()) break;
        const Grad& p = h->second;
        if (g.stops.empty()) g.stops = p.stops;
        if (!g.hasUnits && p.hasUnits) { g.userSpace = p.userSpace; g.hasUnits = true; }
        if (!g.hasXf && p.hasXf) { g.xf = p.xf; g.hasXf = true; }
        auto inh = [](std::string& a, const std::string& b) { if (a.empty()) a = b; };
        inh(g.x1, p.x1); inh(g.y1, p.y1); inh(g.x2, p.x2); inh(g.y2, p.y2); inh(g.cx, p.cx); inh(g.cy, p.cy); inh(g.r, p.r);
        g.href = p.href;
    }
    if (g.stops.empty()) return false;
    Vec2 mn, mx;
    bbox_of(userPaths, mn, mx);
    // Espaço do degradê → usuário: transform do degradê, depois a caixa (unidades
    // do objeto) e a cadeia do elemento.
    Affine2 toUser = g.xf;
    const f32 rw = g.userSpace ? ctx.viewport.x : 1.0f, rh = g.userSpace ? ctx.viewport.y : 1.0f;
    if (!g.userSpace) toUser = Affine2{mx.x - mn.x, 0, 0, mx.y - mn.y, mn.x, mn.y} * g.xf;
    const Affine2 M = ctm * toUser;
    auto L = [](const std::string& s, f32 ref, f32 fb) { return parse_length(s.empty() ? nullptr : &s, ref, fb); };
    paint.stops = g.stops;
    if (g.linear) {
        paint.type = 1;
        paint.start = M.apply(Vec2{L(g.x1, rw, 0.0f), L(g.y1, rh, 0.0f)});
        paint.end = M.apply(Vec2{L(g.x2, rw, rw), L(g.y2, rh, 0.0f)});
    } else {
        paint.type = 2;
        const f32 diag = std::sqrt((rw * rw + rh * rh) * 0.5f);
        const Vec2 c{L(g.cx, rw, rw * 0.5f), L(g.cy, rh, rh * 0.5f)};
        const f32 r = L(g.r, g.userSpace ? diag : 1.0f, g.userSpace ? diag * 0.5f : 0.5f);
        paint.start = M.apply(c);
        // Raio: média dos eixos transformados (elíptico vira circular).
        const f32 rx = M.apply_vec(Vec2{r, 0}).length(), ry = M.apply_vec(Vec2{0, r}).length();
        paint.end = paint.start + Vec2{(rx + ry) * 0.5f, 0.0f};
    }
    return true;
}

void emit_shape(Ctx& ctx, const XNode& n, const Style& st, const Affine2& ctm, std::vector<BezierPath> paths) {
    if (paths.empty() || !st.display) return;
    VectorGroup g;
    const std::string* id = n.attr("id");
    g.name = id && !id->empty() ? *id : n.name + " " + std::to_string(++ctx.index);
    const f32 s = ctm.mean_scale();
    for (const BezierPath& p : paths) {
        VectorPath vp;
        vp.path = transform_path(p, ctm);
        g.paths.push_back(std::move(vp));
    }
    auto paint_of = [&](const SPaint& sp, f32 op, VectorPaint& out) {
        out.opacity = op * 100.0f;
        if (sp.kind == 2 && resolve_gradient(ctx, sp.url, paths, ctm, out)) return true;
        out.type = 0;
        out.color = sp.color;
        return sp.kind != 0;
    };
    g.fill.enabled = paint_of(st.fill, st.fillOpacity, g.fill.paint) && st.fill.kind != 0;
    g.fill.rule = st.fillRule;
    g.stroke.enabled = st.stroke.kind != 0 && st.strokeWidth > 0.0f && paint_of(st.stroke, st.strokeOpacity, g.stroke.paint);
    g.stroke.width = st.strokeWidth * s;
    g.stroke.cap = st.cap;
    g.stroke.join = st.join;
    g.stroke.miterLimit = st.miter;
    for (f32 d : st.dashes) g.stroke.dashes.push_back(d * s);
    g.stroke.dashOffset = st.dashOffset * s;
    g.opacity = st.opacity * 100.0f;
    if (!g.fill.enabled && !g.stroke.enabled) return;
    ctx.out->data.groups.push_back(std::move(g));
    ++ctx.out->elements;
}

void walk(Ctx& ctx, const XNode& n, const Style& parent, const Affine2& parentCtm) {
    for (const XNode& k : n.kids) {
        if (k.name == "defs" || k.name == "linearGradient" || k.name == "radialGradient" || k.name == "clipPath" || k.name == "mask"
            || k.name == "pattern" || k.name == "symbol" || k.name == "style" || k.name == "title" || k.name == "desc" || k.name == "metadata")
            continue;
        Style st = parent;
        st.opacity = parent.opacity;
        apply_style(st, k, std::max(ctx.viewport.x, ctx.viewport.y));
        if (!st.display) continue;
        Affine2 ctm = parentCtm;
        if (const std::string* t = k.attr("transform")) ctm = ctm * parse_transform(*t);
        const f32 vw = ctx.viewport.x, vh = ctx.viewport.y;
        auto len = [&](const char* a, f32 ref, f32 fb = 0.0f) { return parse_length(k.attr(a), ref, fb); };
        std::vector<BezierPath> paths;
        if (k.name == "g" || k.name == "a" || k.name == "svg") {
            walk(ctx, k, st, ctm);
            continue;
        } else if (k.name == "path") {
            if (const std::string* d = k.attr("d")) { PathBuilder b; parse_d(*d, b); paths = std::move(b.paths); }
        } else if (k.name == "rect") {
            const f32 x = len("x", vw), y = len("y", vh), w = len("width", vw), h = len("height", vh);
            if (w > 0.0f && h > 0.0f) {
                const std::string* rxs = k.attr("rx");
                const std::string* rys = k.attr("ry");
                f32 rx = parse_length(rxs, vw, -1.0f), ry = parse_length(rys, vh, -1.0f);
                if (rx < 0.0f) rx = ry;
                if (ry < 0.0f) ry = rx;
                rx = std::clamp(rx, 0.0f, w * 0.5f);
                ry = std::clamp(ry, 0.0f, h * 0.5f);
                if (rx <= 0.0f || ry <= 0.0f) {
                    paths.push_back(make_rect(Vec2{x + w * 0.5f, y + h * 0.5f}, Vec2{w, h}, 0.0f));
                } else {
                    // Cantos elípticos (rx ≠ ry).
                    const f32 kx = rx * 0.5522847498f, ky = ry * 0.5522847498f;
                    BezierPath p;
                    p.v = {{{x + rx, y}, {-kx, 0}, {}}, {{x + w - rx, y}, {}, {kx, 0}}, {{x + w, y + ry}, {0, -ky}, {}},
                           {{x + w, y + h - ry}, {}, {0, ky}}, {{x + w - rx, y + h}, {kx, 0}, {}}, {{x + rx, y + h}, {}, {-kx, 0}},
                           {{x, y + h - ry}, {0, ky}, {}}, {{x, y + ry}, {}, {0, -ky}}};
                    paths.push_back(p);
                }
            }
        } else if (k.name == "circle") {
            const f32 r = len("r", std::sqrt((vw * vw + vh * vh) * 0.5f));
            if (r > 0.0f) paths.push_back(make_ellipse(Vec2{len("cx", vw), len("cy", vh)}, Vec2{r * 2.0f, r * 2.0f}));
        } else if (k.name == "ellipse") {
            const f32 rx = len("rx", vw), ry = len("ry", vh);
            if (rx > 0.0f && ry > 0.0f) paths.push_back(make_ellipse(Vec2{len("cx", vw), len("cy", vh)}, Vec2{rx * 2.0f, ry * 2.0f}));
        } else if (k.name == "line") {
            BezierPath p;
            p.closed = false;
            p.v = {{{len("x1", vw), len("y1", vh)}, {}, {}}, {{len("x2", vw), len("y2", vh)}, {}, {}}};
            paths.push_back(p);
        } else if (k.name == "polyline" || k.name == "polygon") {
            if (const std::string* pts = k.attr("points")) {
                NumReader r{pts->c_str(), pts->c_str() + pts->size()};
                BezierPath p;
                p.closed = k.name == "polygon";
                f32 a, b;
                while (r.number(a) && r.number(b)) p.v.push_back(BezierVertex{Vec2{a, b}, {}, {}});
                if (p.v.size() >= 2) paths.push_back(p);
            }
        } else {
            continue;
        }
        emit_shape(ctx, k, st, ctm, std::move(paths));
    }
}

} // namespace

bool parse_svg(const std::string& text, SvgResult& out, std::string* error) {
    out = SvgResult{};
    XNode doc;
    if (!parse_xml(text, doc)) { if (error) *error = "XML vazio ou inválido"; return false; }
    const XNode* svg = nullptr;
    for (const XNode& k : doc.kids) if (k.name == "svg") { svg = &k; break; }
    if (!svg) { if (error) *error = "sem elemento <svg>"; return false; }
    Ctx ctx;
    ctx.out = &out;
    // viewBox → px (preserveAspectRatio padrão: xMidYMid meet).
    f32 vb[4]{0, 0, 0, 0};
    bool hasVb = false;
    if (const std::string* v = svg->attr("viewBox")) {
        NumReader r{v->c_str(), v->c_str() + v->size()};
        hasVb = r.number(vb[0]) && r.number(vb[1]) && r.number(vb[2]) && r.number(vb[3]) && vb[2] > 0.0f && vb[3] > 0.0f;
    }
    f32 w = parse_length(svg->attr("width"), hasVb ? vb[2] : 300.0f, hasVb ? vb[2] : 300.0f);
    f32 h = parse_length(svg->attr("height"), hasVb ? vb[3] : 150.0f, hasVb ? vb[3] : 150.0f);
    if (w <= 0.0f) w = hasVb ? vb[2] : 300.0f;
    if (h <= 0.0f) h = hasVb ? vb[3] : 150.0f;
    Affine2 root;
    if (hasVb) {
        const std::string* par = svg->attr("preserveAspectRatio");
        const bool none = par && par->find("none") != std::string::npos;
        const f32 sx = w / vb[2], sy = h / vb[3];
        if (none) {
            root = Affine2::scale(Vec2{sx, sy}) * Affine2::translate(Vec2{-vb[0], -vb[1]});
        } else {
            const f32 s = std::min(sx, sy);
            const Vec2 off{(w - vb[2] * s) * 0.5f, (h - vb[3] * s) * 0.5f};
            root = Affine2::translate(off) * Affine2::scale(Vec2{s, s}) * Affine2::translate(Vec2{-vb[0], -vb[1]});
        }
        ctx.viewport = Vec2{vb[2], vb[3]};
    } else {
        ctx.viewport = Vec2{w, h};
    }
    out.size = Vec2{w, h};
    collect_gradients(*svg, ctx.grads);
    Style st;
    apply_style(st, *svg, std::max(ctx.viewport.x, ctx.viewport.y));
    walk(ctx, *svg, st, root);
    if (out.data.groups.empty()) { if (error) *error = "nenhuma forma suportada"; return false; }
    return true;
}

} // namespace aurea::vector
