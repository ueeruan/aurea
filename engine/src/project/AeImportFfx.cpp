// =============================================================================
//  Aurea / project / AeImportFfx.cpp
//
//  Leitor do .ffx do After Effects (RIFX big-endian, forma "FaFX"). O formato
//  foi lido dos presets instalados com código próprio; o que é observado e o
//  que é inferido está em build/reference/ae2026/NOTAS.md. Resumo:
//
//    RIFX <tam> FaFX
//      head
//      LIST besc
//        beso                (offset 12: tiques por segundo)
//        LIST tdsp + tdsn    × N  caminhos (tdsi: tdix + tdmn) e nomes
//        LIST tdsp           sentinela "ADBE End of path sentinel"
//        valores, NA ORDEM dos caminhos (LIST tdbs | tdgp | sspc | btds;
//        LIST btgu não conta)
//
//    tdgp = grupo: tdsb (flags), tdsn (nome), pares tdmn + LIST, fim em
//           "ADBE Group End".
//    tdbs = propriedade: tdsb, tdsn, tdb4 (dims em [2:4]; espacial quando
//           [4:8] = 000e0003/000f0003), cdat (valor parado: os N primeiros
//           doubles) ou LIST list (lhd3 + ldat = keys), expr/Utf8 (expressão).
//    Key não espacial (8 + 40·N bytes): u32 tempo, u8 in, u8 out, u16 flags,
//           N valores, N velocidades in, N influências in, N out, N out.
//    Key espacial (56 + 24·N): cabeçalho, 8 bytes, comprimento do arco,
//           velocidade/influência in, velocidade/influência out, N valores,
//           N tangentes in, N tangentes out.
//    Cor (N = 4): ARGB 0..255 no arquivo → RGBA 0..1 aqui (como no scripting).
//    sspc = efeito: fnam, LIST parT (parn, tdmn + pard [+ pdnm]), LIST tdgp.
//    om-s = forma de máscara: LIST omks > LIST shap (shph + list de pontos
//           float32 x,y em trincas vértice, saída, entrada do próximo).
//
//  Leitura DEFENSIVA: tamanho de pedaço fora do pai, contagem absurda ou
//  profundidade demais param a leitura daquele ramo, nunca o processo.
// =============================================================================
#include "aurea/project/AeImport.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::ae {
namespace {

constexpr usize kMaxFile = 64u << 20;
constexpr u32   kMaxDepth = 64;
constexpr u32   kMaxKeys = 100000;
constexpr u32   kMaxShapePoints = 1u << 16;

u32 be32(const u8* p) noexcept {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) | p[3];
}
u16 be16(const u8* p) noexcept { return static_cast<u16>((p[0] << 8) | p[1]); }
f64 bed(const u8* p) noexcept {
    u64 v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    f64 d;
    std::memcpy(&d, &v, 8);
    return std::isfinite(d) ? d : 0.0;
}
f32 bef(const u8* p) noexcept {
    const u32 v = be32(p);
    f32 f;
    std::memcpy(&f, &v, 4);
    return std::isfinite(f) ? f : 0.0f;
}

struct Chunk {
    char id[4]{};
    const u8* body = nullptr;
    usize size = 0;
    [[nodiscard]] bool is(const char* s) const noexcept { return std::memcmp(id, s, 4) == 0; }
    /// LIST: o tipo (4 bytes) e o corpo depois dele.
    [[nodiscard]] bool list(const char* type) const noexcept {
        return is("LIST") && size >= 4 && std::memcmp(body, type, 4) == 0;
    }
};

/// Percorre os pedaços de [p, end). Tamanho que passa do pai encerra o ramo.
template <class Fn>
void each_chunk(const u8* p, const u8* end, Fn&& fn) {
    while (p + 8 <= end) {
        Chunk c;
        std::memcpy(c.id, p, 4);
        const u32 size = be32(p + 4);
        c.body = p + 8;
        if (size > static_cast<usize>(end - c.body)) {
            c.size = static_cast<usize>(end - c.body);
            fn(c);
            return;
        }
        c.size = size;
        if (!fn(c)) return;
        p = c.body + size + (size & 1u);
    }
}

/// Latin-1 → UTF-8 (nomes antigos sem prefixo "Utf8").
std::string latin1(const u8* p, usize n) {
    std::string s;
    s.reserve(n);
    for (usize i = 0; i < n; ++i) {
        const u8 c = p[i];
        if (c == 0) break;
        if (c < 0x80) s.push_back(static_cast<char>(c));
        else { s.push_back(static_cast<char>(0xC0 | (c >> 6))); s.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    }
    return s;
}

/// UTF-8 inválido vira '?' (o texto atravessa JNI/NSString).
std::string clean_utf8(const u8* p, usize n) {
    std::string o;
    o.reserve(n);
    usize i = 0;
    while (i < n) {
        const u8 c = p[i];
        if (c == 0) break;
        const usize len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = len > 0 && i + len <= n;
        for (usize k = 1; ok && k < len; ++k) ok = (p[i + k] & 0xC0) == 0x80;
        if (ok) { o.append(reinterpret_cast<const char*>(p + i), len); i += len; }
        else { o.push_back('?'); ++i; }
    }
    return o;
}

/// Texto de tdsn/fnam/pdnm: "Utf8" + u32 tamanho + texto, ou C-string Latin-1.
std::string name_payload(const Chunk& c) {
    if (c.size >= 8 && std::memcmp(c.body, "Utf8", 4) == 0) {
        const u32 n = std::min<u32>(be32(c.body + 4), static_cast<u32>(c.size - 8));
        return clean_utf8(c.body + 8, n);
    }
    return latin1(c.body, c.size);
}

/// Nome sem o \t que o AE põe nos nomes "localizáveis" e sem o marcador
/// "-_0_/-" (nome padrão da propriedade, observado nos presets): vazio.
std::string strip_tab(std::string s) {
    while (!s.empty() && (s.front() == '\t' || s.front() == ' ')) s.erase(s.begin());
    if (s.rfind("-_0_/-", 0) == 0) s.clear();
    return s;
}

struct Reader {
    u32 timescale = 0;
    u32 depth = 0;

    void property(const u8* p, const u8* end, Node& n);
    void group(const u8* p, const u8* end, Node& n);
    void effect(const u8* p, const u8* end, Node& n);
    void shape(const u8* p, const u8* end, Node& n);
    void keys(const Chunk& hd, const Chunk& data, u32 scale, Node& n);
    bool value(const Chunk& c, const std::string& match, Node& out);
};

void color_fix(std::vector<f64>& v, f64 k) {
    // ARGB → RGBA, 0..255 → 0..1 (k = 1/255 para valor e velocidade).
    if (v.size() != 4) return;
    const f64 a = v[0], r = v[1], g = v[2], b = v[3];
    v = {r * k, g * k, b * k, a * k};
}

void Reader::keys(const Chunk& hd, const Chunk& data, u32 scale, Node& n) {
    if (hd.size < 20) return;
    const u32 count = std::min(be32(hd.body + 8), kMaxKeys);
    const u32 size = be32(hd.body + 16);
    if (size < 8 || size > 4096) return;
    const u32 dims = std::max<u32>(1, n.dims);
    const bool spatial = n.spatial && size == 56 + 24 * dims;
    const bool flat = !spatial && size >= 8 + 40 * dims;
    const u32 nd = spatial ? dims : flat ? (size - 8) / 40 : 0;
    const f64 ts = scale ? static_cast<f64>(scale) : (timescale ? static_cast<f64>(timescale) : 30.0);
    const bool color = nd == 4;
    for (u32 i = 0; i < count; ++i) {
        if (static_cast<usize>(i + 1) * size > data.size) break;
        const u8* k = data.body + static_cast<usize>(i) * size;
        Key key;
        key.time = static_cast<f64>(be32(k)) / ts;
        auto interp = [](u8 b) { return b == 2 ? KeyInterp::Bezier : b == 3 ? KeyInterp::Hold : KeyInterp::Linear; };
        key.in = interp(k[4]);
        key.out = interp(k[5]);
        key.flags = be16(k + 6);
        if (spatial) {
            key.inEase.push_back(Ease{bed(k + 24), bed(k + 32) * 100.0});
            key.outEase.push_back(Ease{bed(k + 40), bed(k + 48) * 100.0});
            for (u32 c = 0; c < nd; ++c) key.value.push_back(bed(k + 56 + 8 * c));
            for (u32 c = 0; c < nd; ++c) key.inTangent.push_back(bed(k + 56 + 8 * nd + 8 * c));
            for (u32 c = 0; c < nd; ++c) key.outTangent.push_back(bed(k + 56 + 16 * nd + 8 * c));
        } else if (nd > 0) {
            std::vector<f64> is(nd), ii(nd), os(nd), oi(nd);
            for (u32 c = 0; c < nd; ++c) {
                key.value.push_back(bed(k + 8 + 8 * c));
                is[c] = bed(k + 8 + 8 * nd + 8 * c);
                ii[c] = bed(k + 8 + 16 * nd + 8 * c);
                os[c] = bed(k + 8 + 24 * nd + 8 * c);
                oi[c] = bed(k + 8 + 32 * nd + 8 * c);
            }
            if (color) {
                color_fix(key.value, 1.0 / 255.0);
                color_fix(is, 1.0 / 255.0);
                color_fix(os, 1.0 / 255.0);
                color_fix(ii, 1.0);
                color_fix(oi, 1.0);
            }
            for (u32 c = 0; c < nd; ++c) {
                key.inEase.push_back(Ease{is[c], ii[c] * 100.0});
                key.outEase.push_back(Ease{os[c], oi[c] * 100.0});
            }
        } else {
            // Key de dado próprio (forma de máscara, texto): só o tempo importa.
            if (size >= 24) key.value.push_back(bed(k + 16));
        }
        n.keys.push_back(std::move(key));
    }
}

void Reader::property(const u8* p, const u8* end, Node& n) {
    n.kind = Node::Kind::Property;
    u32 scale = 0;
    Chunk hd{};
    bool haveHd = false;
    each_chunk(p, end, [&](const Chunk& c) {
        if (c.is("tdsb") && c.size >= 4) n.flags = be32(c.body);
        else if (c.is("tdsn")) n.name = strip_tab(name_payload(c));
        else if (c.is("tdb4") && c.size >= 16) {
            n.dims = be16(c.body + 2);
            const u32 sp = be32(c.body + 4);
            n.spatial = sp == 0x000e0003u || sp == 0x000f0003u;
            scale = be32(c.body + 12);
        } else if (c.is("cdat")) {
            const u32 dims = std::max<u32>(1, n.dims);
            if (c.size >= dims * 8u) {
                n.hasValue = true;
                n.value.clear();
                for (u32 i = 0; i < dims; ++i) n.value.push_back(bed(c.body + 8 * i));
                if (dims == 4) color_fix(n.value, 1.0 / 255.0);
            }
        } else if (c.list("list")) {
            Chunk h{}, d{};
            bool gh = false, gd = false;
            each_chunk(c.body + 4, c.body + c.size, [&](const Chunk& x) {
                if (x.is("lhd3")) { h = x; gh = true; }
                else if (x.is("ldat")) { d = x; gd = true; }
                return true;
            });
            if (gh && gd) keys(h, d, scale, n);
            hd = h;
            haveHd = gh;
        } else if (c.is("expr")) {
            n.expression = latin1(c.body, c.size);
        } else if (c.is("Utf8")) {
            n.expression = clean_utf8(c.body, c.size);
        }
        return true;
    });
    (void)haveHd;
    (void)hd;
    if (!n.hasValue && !n.keys.empty()) {
        n.value = n.keys.front().value;
        n.hasValue = !n.value.empty();
    }
}

void Reader::shape(const u8* p, const u8* end, Node& n) {
    n.kind = Node::Kind::Property;
    n.hasShape = false;
    each_chunk(p, end, [&](const Chunk& c) {
        if (c.list("tdbs")) {
            Node keysNode;
            property(c.body + 4, c.body + c.size, keysNode);
            n.flags = keysNode.flags;
            n.name = keysNode.name;
            n.shapeKeyed = keysNode.keys.size() > 1;
            n.expression = keysNode.expression;
        } else if (c.list("omks")) {
            each_chunk(c.body + 4, c.body + c.size, [&](const Chunk& s) {
                if (!s.list("shap") || n.hasShape) return true;
                MaskShape ms;
                std::vector<f32> pts;
                each_chunk(s.body + 4, s.body + s.size, [&](const Chunk& x) {
                    if (x.is("shph") && x.size >= 4) ms.closed = (x.body[3] & 0x08u) == 0;
                    else if (x.list("list")) {
                        Chunk h{}, d{};
                        bool gh = false, gd = false;
                        each_chunk(x.body + 4, x.body + x.size, [&](const Chunk& y) {
                            if (y.is("lhd3")) { h = y; gh = true; }
                            else if (y.is("ldat")) { d = y; gd = true; }
                            return true;
                        });
                        if (gh && gd && h.size >= 20) {
                            const u32 count = std::min(be32(h.body + 8), kMaxShapePoints);
                            const u32 size = be32(h.body + 16);
                            if (size >= 8) {
                                for (u32 i = 0; i < count && static_cast<usize>(i + 1) * size <= d.size; ++i) {
                                    pts.push_back(bef(d.body + static_cast<usize>(i) * size));
                                    pts.push_back(bef(d.body + static_cast<usize>(i) * size + 4));
                                }
                            }
                        }
                    }
                    return true;
                });
                // Trincas: vértice i, saída de i, entrada de i+1 (a do 0 é a última).
                const usize np = pts.size() / 2;
                const usize nv = np / 3;
                for (usize v = 0; v < nv; ++v) {
                    ShapeVertex sv;
                    sv.x = pts[6 * v]; sv.y = pts[6 * v + 1];
                    sv.outX = pts[6 * v + 2]; sv.outY = pts[6 * v + 3];
                    const usize in = v == 0 ? 3 * nv - 1 : 3 * v - 1;
                    sv.inX = pts[2 * in]; sv.inY = pts[2 * in + 1];
                    ms.vertices.push_back(sv);
                }
                if (!ms.vertices.empty()) {
                    n.shape = std::move(ms);
                    n.hasShape = true;
                }
                return true;
            });
        }
        return true;
    });
}

void Reader::group(const u8* p, const u8* end, Node& n) {
    n.kind = Node::Kind::Group;
    std::string pending;
    each_chunk(p, end, [&](const Chunk& c) {
        if (c.is("tdsb") && c.size >= 4) n.flags = be32(c.body);
        else if (c.is("tdsn")) n.name = strip_tab(name_payload(c));
        else if (c.is("tdmn")) pending = latin1(c.body, c.size);
        else if (c.is("LIST")) {
            if (c.list("btgu") || c.list("tdsp")) return true;
            Node child;
            if (value(c, pending, child)) n.children.push_back(std::move(child));
            pending.clear();
        }
        return true;
    });
}

void Reader::effect(const u8* p, const u8* end, Node& n) {
    n.kind = Node::Kind::Effect;
    each_chunk(p, end, [&](const Chunk& c) {
        if (c.is("fnam")) n.effectName = strip_tab(name_payload(c));
        else if (c.list("parT")) {
            each_chunk(c.body + 4, c.body + c.size, [&](const Chunk& x) {
                if (x.is("tdmn")) {
                    EffectParamDef d;
                    d.match = latin1(x.body, x.size);
                    n.params.push_back(std::move(d));
                } else if (x.is("pard") && !n.params.empty() && x.size >= 48) {
                    n.params.back().type = x.body[15];
                    n.params.back().name = strip_tab(latin1(x.body + 16, 32));
                } else if (x.is("pdnm") && !n.params.empty()) {
                    const std::string all = name_payload(x);
                    usize s = 0;
                    while (s <= all.size()) {
                        const usize bar = all.find('|', s);
                        n.params.back().popup.push_back(all.substr(s, bar == std::string::npos ? std::string::npos : bar - s));
                        if (bar == std::string::npos) break;
                        s = bar + 1;
                    }
                }
                return true;
            });
        } else if (c.list("tdgp")) {
            Node g;
            group(c.body + 4, c.body + c.size, g);
            n.flags = g.flags;
            n.name = g.name;
            n.children = std::move(g.children);
        }
        return true;
    });
}

bool Reader::value(const Chunk& c, const std::string& match, Node& out) {
    if (depth >= kMaxDepth) return false;
    ++depth;
    out.match = match;
    const u8* p = c.body + 4;
    const u8* end = c.body + c.size;
    bool ok = true;
    if (c.list("tdbs")) property(p, end, out);
    else if (c.list("tdgp")) group(p, end, out);
    else if (c.list("sspc")) effect(p, end, out);
    else if (c.list("om-s")) shape(p, end, out);
    else if (c.list("btds")) {
        out.kind = Node::Kind::TextDocument;
        each_chunk(p, end, [&](const Chunk& x) {
            if (x.list("tdbs")) {
                Node t;
                property(x.body + 4, x.body + x.size, t);
                out.flags = t.flags;
                out.name = t.name;
                out.keys = std::move(t.keys);
                out.expression = std::move(t.expression);
            }
            return true;
        });
    } else {
        out.kind = Node::Kind::Unknown;
    }
    --depth;
    return ok;
}

} // namespace

const Node* Node::child(std::string_view m) const noexcept {
    for (const Node& c : children) if (c.match == m) return &c;
    return nullptr;
}

bool parse_ffx(std::string_view bytes, FfxDocument& out, std::string& error) {
    out = FfxDocument{};
    const u8* b = reinterpret_cast<const u8*>(bytes.data());
    if (bytes.size() < 12 || std::memcmp(b, "RIFX", 4) != 0 || std::memcmp(b + 8, "FaFX", 4) != 0) {
        error = "nao e um preset .ffx do After Effects (RIFX FaFX)";
        return false;
    }
    if (bytes.size() > kMaxFile) {
        error = "preset grande demais";
        return false;
    }
    const u8* end = b + std::min<usize>(bytes.size(), 8 + static_cast<usize>(be32(b + 4)));
    if (end < b + 12) end = b + bytes.size();
    Reader r;
    bool found = false;
    each_chunk(b + 12, end, [&](const Chunk& c) {
        if (!c.list("besc")) return true;
        found = true;
        std::vector<std::vector<PathStep>> paths;
        std::vector<std::string> names;
        std::vector<Node> values;
        each_chunk(c.body + 4, c.body + c.size, [&](const Chunk& x) {
            if (x.is("beso") && x.size >= 16) {
                r.timescale = be32(x.body + 12);
                out.timescale = r.timescale;
            } else if (x.list("tdsp")) {
                std::vector<PathStep> path;
                each_chunk(x.body + 4, x.body + x.size, [&](const Chunk& s) {
                    if (!s.list("tdsi")) return true;
                    PathStep step;
                    each_chunk(s.body + 4, s.body + s.size, [&](const Chunk& t) {
                        if (t.is("tdix") && t.size >= 4) step.index = static_cast<i32>(be32(t.body));
                        else if (t.is("tdmn")) step.match = latin1(t.body, t.size);
                        return true;
                    });
                    path.push_back(std::move(step));
                    return true;
                });
                paths.push_back(std::move(path));
                names.emplace_back();
            } else if (x.is("tdsn")) {
                if (!names.empty()) names.back() = strip_tab(name_payload(x));
            } else if (x.is("LIST")) {
                if (x.list("btgu")) return true;
                const usize k = values.size();
                std::string leaf;
                if (k < paths.size() && !paths[k].empty()) leaf = paths[k].back().match;
                Node v;
                r.value(x, leaf, v);
                values.push_back(std::move(v));
            }
            return true;
        });
        for (usize i = 0; i < paths.size(); ++i) {
            if (paths[i].empty() || paths[i].back().match == "ADBE End of path sentinel") continue;
            if (i >= values.size()) break;
            Entry e;
            e.path = std::move(paths[i]);
            e.name = names[i];
            e.value = std::move(values[i]);
            out.entries.push_back(std::move(e));
        }
        return false;
    });
    if (!found) {
        error = "preset sem o bloco de valores (besc)";
        return false;
    }
    return true;
}

const char* detect_kind(std::string_view data, std::string_view fileName) noexcept {
    if (data.size() >= 12 && std::memcmp(data.data(), "RIFX", 4) == 0 && std::memcmp(data.data() + 8, "FaFX", 4) == 0)
        return "ffx";
    auto ends = [&](std::string_view ext) {
        if (fileName.size() < ext.size()) return false;
        for (usize i = 0; i < ext.size(); ++i) {
            char a = fileName[fileName.size() - ext.size() + i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
            if (a != ext[i]) return false;
        }
        return true;
    };
    if (ends(".jsx") || ends(".js") || ends(".jsxinc")) return "jsx";
    if (ends(".ffx")) return "ffx";
    if (ends(".jsxbin")) return "jsxbin";
    // Sem extensão: texto que parece script.
    const usize n = std::min<usize>(data.size(), 4096);
    for (usize i = 0; i < n; ++i) if (data[i] == 0) return "";
    if (data.find("app.") != std::string_view::npos || data.find("function") != std::string_view::npos) return "jsx";
    return "";
}

} // namespace aurea::ae
