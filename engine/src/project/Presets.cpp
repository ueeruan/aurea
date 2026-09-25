// =============================================================================
//  Aurea / project / Presets.cpp
//
//  JSON mínimo + presets (formato documentado em Presets.hpp). O leitor é
//  descida recursiva com limite de tamanho e profundidade: arquivo hostil não
//  estoura pilha nem memória. Todo número lido passa por validação de faixa
//  antes de virar dado de camada.
// =============================================================================
#include "aurea/project/Presets.hpp"

#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace aurea {

// =============================================================================
// JSON
// =============================================================================
namespace json {

const Value* Value::get(std::string_view key) const noexcept {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : object) if (k == key) return &v;
    return nullptr;
}

namespace {

constexpr usize kMaxText = 4u << 20;
constexpr u32   kMaxDepth = 64;

struct Parser {
    std::string_view s;
    usize i = 0;
    std::string err;

    bool fail(const char* m) {
        if (err.empty()) err = std::string(m) + " (posicao " + std::to_string(i) + ")";
        return false;
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool lit(std::string_view w) {
        if (s.substr(i, w.size()) != w) return false;
        i += w.size();
        return true;
    }
    static void utf8(std::string& o, u32 cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(u32& out) {
        if (i + 4 > s.size()) return fail("escape \\u incompleto");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<u32>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<u32>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<u32>(c - 'A' + 10);
            else return fail("escape \\u invalido");
        }
        return true;
    }
    bool str(std::string& o) {
        if (i >= s.size() || s[i] != '"') return fail("esperava string");
        ++i;
        while (true) {
            if (i >= s.size()) return fail("string sem fim");
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<u8>(c) < 0x20) return fail("caractere de controle na string");
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) return fail("escape sem fim");
            const char e = s[i++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    u32 cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        u32 lo = 0;
                        if (!lit("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("par substituto invalido");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("par substituto invalido");
                    }
                    utf8(o, cp);
                    break;
                }
                default: return fail("escape desconhecido");
            }
        }
    }
    bool num(f64& out) {
        // Gramática do JSON validada à mão; a conversão vai por strtod numa
        // cópia (o locale do processo é o "C" — o motor nunca muda).
        const usize b = i;
        if (i < s.size() && s[i] == '-') ++i;
        if (i >= s.size()) return fail("numero invalido");
        if (s[i] == '0') ++i;
        else if (s[i] >= '1' && s[i] <= '9') { while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i; }
        else return fail("numero invalido");
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') return fail("numero invalido");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') return fail("numero invalido");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        const std::string tok(s.substr(b, i - b));
        out = std::strtod(tok.c_str(), nullptr);
        if (!std::isfinite(out)) return fail("numero fora da faixa");
        return true;
    }
    bool value(Value& v, u32 depth) {
        if (depth > kMaxDepth) return fail("aninhamento profundo demais");
        ws();
        if (i >= s.size()) return fail("fim inesperado");
        const char c = s[i];
        if (c == '{') {
            ++i;
            v.type = Value::Type::Object;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("esperava ':'");
                ++i;
                Value child;
                if (!value(child, depth + 1)) return false;
                v.object.emplace_back(std::move(k), std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("esperava ',' ou '}'");
            }
        }
        if (c == '[') {
            ++i;
            v.type = Value::Type::Array;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true) {
                Value child;
                if (!value(child, depth + 1)) return false;
                v.array.push_back(std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("esperava ',' ou ']'");
            }
        }
        if (c == '"') { v.type = Value::Type::String; return str(v.string); }
        if (lit("true")) { v.type = Value::Type::Bool; v.boolean = true; return true; }
        if (lit("false")) { v.type = Value::Type::Bool; v.boolean = false; return true; }
        if (lit("null")) { v.type = Value::Type::Null; return true; }
        v.type = Value::Type::Number;
        return num(v.number);
    }
};

} // namespace

bool parse(std::string_view text, Value& out, std::string* error) {
    if (text.size() > kMaxText) {
        if (error) *error = "texto grande demais";
        return false;
    }
    Parser p{text};
    Value v;
    bool ok = p.value(v, 0);
    if (ok) {
        p.ws();
        if (p.i != text.size()) ok = p.fail("lixo depois do valor");
    }
    if (!ok) {
        if (error) *error = p.err;
        return false;
    }
    out = std::move(v);
    return true;
}

void Writer::comma() {
    if (afterKey_) { afterKey_ = false; return; }
    if (!first_.empty()) {
        if (!first_.back()) out_ += ',';
        first_.back() = false;
    }
}
Writer& Writer::begin_object() { comma(); out_ += '{'; first_.push_back(true); return *this; }
Writer& Writer::end_object() { out_ += '}'; if (!first_.empty()) first_.pop_back(); return *this; }
Writer& Writer::begin_array() { comma(); out_ += '['; first_.push_back(true); return *this; }
Writer& Writer::end_array() { out_ += ']'; if (!first_.empty()) first_.pop_back(); return *this; }
Writer& Writer::key(std::string_view k) {
    value(k);
    out_ += ':';
    afterKey_ = true;
    return *this;
}
Writer& Writer::value(std::string_view s) {
    comma();
    out_ += '"';
    for (const char c : s) {
        switch (c) {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (static_cast<u8>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", static_cast<unsigned>(static_cast<u8>(c)));
                    out_ += b;
                } else {
                    out_ += c;
                }
        }
    }
    out_ += '"';
    return *this;
}
Writer& Writer::value(f64 n) {
    comma();
    if (!std::isfinite(n)) n = 0.0;
    char b[40];
    const auto r = std::to_chars(b, b + sizeof(b), n);
    out_.append(b, r.ptr);
    return *this;
}
Writer& Writer::value(f32 n) {
    comma();
    if (!std::isfinite(n)) n = 0.0f;
    // Menor texto que volta ao MESMO f32: ida e volta exata pelo strtod.
    char b[32];
    const auto r = std::to_chars(b, b + sizeof(b), n);
    out_.append(b, r.ptr);
    return *this;
}
Writer& Writer::value(i64 n) {
    comma();
    char b[24];
    const auto r = std::to_chars(b, b + sizeof(b), n);
    out_.append(b, r.ptr);
    return *this;
}
Writer& Writer::value(bool v) {
    comma();
    out_ += v ? "true" : "false";
    return *this;
}

} // namespace json

// =============================================================================
// Presets
// =============================================================================
namespace presets {

namespace {

constexpr i64 kMaxRelFrames = 10'000'000;    ///< ~92 h a 30 fps: além disso é lixo
constexpr u32 kMaxPresetKeys = 100'000;      ///< por trilha
constexpr u32 kMaxAnimators = 32;
constexpr u32 kMaxCurves = 64, kMaxCurvePoints = 4096, kMaxStops = 256;
constexpr u32 kMaxTracks = 4096;

constexpr const char* kKindNames[] = {"effects", "text", "animation", "caption", "curve"};

/// Nomes das trilhas de transform (TrackProperty 0..SkewY), na ordem do enum.
constexpr const char* kPropNames[] = {
    "positionX", "positionY", "positionZ", "scaleX", "scaleY", "scaleZ", "rotationX", "rotationY",
    "rotationZ", "anchorX", "anchorY", "anchorZ", "opacity", "skewX", "skewY",
};
constexpr u32 kPropCount = sizeof(kPropNames) / sizeof(kPropNames[0]);
static_assert(static_cast<u32>(TrackProperty::SkewY) + 1 == kPropCount, "nomes do transform fora de ordem");

bool is_transform_prop(TrackProperty p) noexcept { return static_cast<u32>(p) < kPropCount; }
bool is_relative_prop(TrackProperty p) noexcept {
    return (p >= TrackProperty::PositionX && p <= TrackProperty::PositionZ) || (p >= TrackProperty::AnchorX && p <= TrackProperty::AnchorZ);
}

/// Valor parado do transform para a propriedade (o que o render usa sem keyframe).
f32 transform_value(const Transform& t, TrackProperty p) noexcept {
    switch (p) {
        case TrackProperty::PositionX: return t.position.x;
        case TrackProperty::PositionY: return t.position.y;
        case TrackProperty::PositionZ: return t.position.z;
        case TrackProperty::ScaleX: return t.scale.x;
        case TrackProperty::ScaleY: return t.scale.y;
        case TrackProperty::ScaleZ: return t.scale.z;
        case TrackProperty::RotationX: return t.rotation.x;
        case TrackProperty::RotationY: return t.rotation.y;
        case TrackProperty::RotationZ: return t.rotation.z;
        case TrackProperty::AnchorX: return t.anchor.x;
        case TrackProperty::AnchorY: return t.anchor.y;
        case TrackProperty::AnchorZ: return t.anchor.z;
        case TrackProperty::Opacity: return t.opacity;
        case TrackProperty::SkewX: return t.skewX;
        case TrackProperty::SkewY: return t.skewY;
        default: return 0.0f;
    }
}

// --- escrita -----------------------------------------------------------------

void write_keys(json::Writer& w, const Track& t) {
    w.key("static").value(t.staticValue);
    w.key("keys").begin_array();
    for (const Keyframe& k : t.keys) {
        w.begin_array();
        w.value(k.time.value).value(k.value).value(static_cast<u32>(k.interp));
        w.value(k.bx1).value(k.by1).value(k.bx2).value(k.by2);
        w.value(k.tangentIn).value(k.tangentOut).value(static_cast<u32>(k.easingPreset));
        w.end_array();
    }
    w.end_array();
}

void write_vec(json::Writer& w, const char* k, const f32* v, u32 n) {
    w.key(k).begin_array();
    for (u32 i = 0; i < n; ++i) w.value(v[i]);
    w.end_array();
}
void write_vec4(json::Writer& w, const char* k, const Vec4& v) { const f32 a[4] = {v.x, v.y, v.z, v.w}; write_vec(w, k, a, 4); }
void write_vec3(json::Writer& w, const char* k, const Vec3& v) { const f32 a[3] = {v.x, v.y, v.z}; write_vec(w, k, a, 3); }
void write_vec2(json::Writer& w, const char* k, const Vec2& v) { const f32 a[2] = {v.x, v.y}; write_vec(w, k, a, 2); }

void write_effects(json::Writer& w, const Preset& p, const EffectRegistry* reg) {
    w.key("effects").begin_array();
    for (u32 ei = 0; ei < p.effects.size(); ++ei) {
        const EffectInstance& e = p.effects[ei];
        const Effect* fx = reg ? reg->find(e.type) : nullptr;
        const ParameterRegistry* specs = reg ? reg->params(e.type) : nullptr;
        w.begin_object();
        if (fx) w.key("key").value(fx->info().key);
        w.key("type").value(static_cast<i64>(e.type));
        w.key("enabled").value(e.enabled);
        w.key("params").begin_array();
        for (u32 pi = 0; pi < e.params.size(); ++pi) {
            const ParamSlot& s = e.params[pi];
            w.begin_object();
            if (specs && pi < specs->count()) w.key("id").value(specs->at(pi).id);
            w.key("index").value(pi);
            write_vec(w, "v", s.constant.v, 4);
            w.key("ref").value(static_cast<i64>(s.constant.ref));
            w.key("src").value(static_cast<u32>(s.source));
            w.end_object();
        }
        w.end_array();
        w.key("curves").begin_array();
        for (const CurveData& c : e.curves) {
            w.begin_array();
            for (const auto& ch : c.channel) {
                w.begin_array();
                for (const CurveData::Point& pt : ch) w.value(pt.x).value(pt.y);
                w.end_array();
            }
            w.end_array();
        }
        w.end_array();
        w.key("gradients").begin_array();
        for (const GradientData& g : e.gradients) {
            w.begin_array();
            for (const GradientStop& s : g.stops) {
                w.begin_array().value(s.position).value(s.color.x).value(s.color.y).value(s.color.z).value(s.color.w).end_array();
            }
            w.end_array();
        }
        w.end_array();
        w.key("tracks").begin_array();
        for (const Track& t : p.effectTracks) {
            if (t.effectIndex != ei) continue;
            const u32 param = t.effectParamIndex / 4u, comp = t.effectParamIndex % 4u;
            w.begin_object();
            if (specs && param < specs->count()) w.key("param").value(specs->at(param).id);
            w.key("index").value(param);
            w.key("comp").value(comp);
            write_keys(w, t);
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
}

void write_text(json::Writer& w, const Preset& p) {
    if (p.textParts & kTextStyle) {
        const TextData& t = p.style;
        w.key("style").begin_object();
        w.key("fontFamily").value(t.fontFamily);
        w.key("fontWeight").value(static_cast<u32>(t.fontWeight));
        w.key("fontItalic").value(t.fontItalic);
        w.key("size").value(t.size);
        write_vec4(w, "color", t.color);
        w.key("strokeWidth").value(t.strokeWidth);
        write_vec4(w, "strokeColor", t.strokeColor);
        w.key("alignment").value(t.alignment);
        w.key("lineHeight").value(t.lineHeight);
        w.key("tracking").value(t.tracking);
        w.key("boxMode").value(t.boxMode);
        w.key("autoSize").value(t.autoSize);
        const f32 box[4] = {t.box.x, t.box.y, t.box.w, t.box.h};
        write_vec(w, "box", box, 4);
        w.key("background").value(t.background);
        write_vec4(w, "backgroundColor", t.backgroundColor);
        w.key("backgroundPadding").value(t.backgroundPadding);
        w.key("backgroundRadius").value(t.backgroundRadius);
        w.key("shadow").value(t.shadow);
        write_vec4(w, "shadowColor", t.shadowColor);
        write_vec2(w, "shadowOffset", t.shadowOffset);
        w.key("shadowBlur").value(t.shadowBlur);
        w.end_object();
    }
    if (p.textParts & kTextAnimators) {
        w.key("animators").begin_array();
        for (const TextAnimator& a : p.animators) {
            w.begin_object();
            w.key("name").value(a.name);
            w.key("enabled").value(a.enabled);
            const TextSelector& s = a.selector;
            w.key("selector").begin_object();
            w.key("basedOn").value(static_cast<u32>(s.basedOn));
            w.key("type").value(static_cast<u32>(s.type));
            w.key("shape").value(static_cast<u32>(s.shape));
            w.key("randomOrder").value(s.randomOrder);
            w.key("seed").value(s.seed);
            w.key("start").value(s.start);
            w.key("end").value(s.end);
            w.key("offset").value(s.offset);
            w.key("amount").value(s.amount);
            w.key("easeHigh").value(s.easeHigh);
            w.key("easeLow").value(s.easeLow);
            w.key("wiggleRate").value(s.wiggleRate);
            w.end_object();
            w.key("props").value(a.props);
            write_vec3(w, "position", a.position);
            write_vec2(w, "scale", a.scale);
            write_vec3(w, "rotation", a.rotation);
            w.key("opacity").value(a.opacity);
            w.key("tracking").value(a.tracking);
            w.key("blur").value(a.blur);
            w.key("skew").value(a.skew);
            w.key("strokeWidth").value(a.strokeWidth);
            w.key("charOffset").value(a.charOffset);
            write_vec4(w, "fill", a.fill);
            write_vec4(w, "stroke", a.stroke);
            w.end_object();
        }
        w.end_array();
        w.key("tracks").begin_array();
        for (const Track& t : p.textTracks) {
            w.begin_object();
            w.key("animator").value(t.effectIndex);
            w.key("param").value(t.effectParamIndex);
            write_keys(w, t);
            w.end_object();
        }
        w.end_array();
    }
}

void write_caption(json::Writer& w, const text::CaptionOptions& o, bool removeFillers) {
    w.key("caption").begin_object();
    w.key("mode").value(o.mode);
    w.key("maxWords").value(o.maxWords);
    w.key("maxChars").value(o.maxChars);
    w.key("maxLines").value(o.maxLines);
    w.key("style").value(o.style);
    w.key("highlight").value(o.highlight);
    w.key("uppercase").value(o.uppercase);
    w.key("breakOnPause").value(o.breakOnPause);
    w.key("pauseSec").value(o.pauseSec);
    w.key("posY").value(o.posY);
    w.key("sizeFrac").value(o.sizeFrac);
    write_vec4(w, "highlightColor", o.highlightColor);
    w.key("removeFillers").value(removeFillers);
    w.end_object();
}

// --- leitura -----------------------------------------------------------------

/// Leitura com erro acumulado: o primeiro problema derruba o preset inteiro.
struct Reader {
    std::string err;
    bool fail(std::string m) {
        if (err.empty()) err = std::move(m);
        return false;
    }
    bool ok() const noexcept { return err.empty(); }

    f64 num(const json::Value& o, const char* k, f64 def, f64 lo, f64 hi) {
        const json::Value* v = o.get(k);
        if (!v) return def;
        if (!v->is_number()) { fail(std::string("campo nao numerico: ") + k); return def; }
        if (v->number < lo || v->number > hi) { fail(std::string("valor fora da faixa: ") + k); return def; }
        return v->number;
    }
    f32 f(const json::Value& o, const char* k, f32 def, f64 lo = -1e7, f64 hi = 1e7) {
        return static_cast<f32>(num(o, k, def, lo, hi));
    }
    u32 u(const json::Value& o, const char* k, u32 def, u32 hi) {
        const f64 x = num(o, k, def, 0, hi);
        if (x != std::floor(x)) { fail(std::string("esperava inteiro: ") + k); return def; }
        return static_cast<u32>(x);
    }
    bool b(const json::Value& o, const char* k, bool def) {
        const json::Value* v = o.get(k);
        if (!v) return def;
        if (v->type != json::Value::Type::Bool) { fail(std::string("campo nao booleano: ") + k); return def; }
        return v->boolean;
    }
    std::string s(const json::Value& o, const char* k) {
        const json::Value* v = o.get(k);
        if (!v) return {};
        if (!v->is_string()) { fail(std::string("campo nao texto: ") + k); return {}; }
        if (v->string.size() > 1024) { fail(std::string("texto longo demais: ") + k); return {}; }
        return v->string;
    }
    /// Array de `n` números (faltando = padrão).
    void vec(const json::Value& o, const char* k, f32* out, u32 n, f64 lo = -1e7, f64 hi = 1e7) {
        const json::Value* v = o.get(k);
        if (!v) return;
        if (!v->is_array() || v->array.size() != n) { fail(std::string("vetor invalido: ") + k); return; }
        for (u32 i = 0; i < n; ++i) {
            const json::Value& x = v->array[i];
            if (!x.is_number() || x.number < lo || x.number > hi) { fail(std::string("vetor invalido: ") + k); return; }
            out[i] = static_cast<f32>(x.number);
        }
    }
    Vec4 vec4(const json::Value& o, const char* k, Vec4 def, f64 lo = -1e7, f64 hi = 1e7) {
        f32 a[4] = {def.x, def.y, def.z, def.w};
        vec(o, k, a, 4, lo, hi);
        return Vec4{a[0], a[1], a[2], a[3]};
    }
    Vec3 vec3(const json::Value& o, const char* k, Vec3 def) {
        f32 a[3] = {def.x, def.y, def.z};
        vec(o, k, a, 3);
        return Vec3{a[0], a[1], a[2]};
    }
    Vec2 vec2(const json::Value& o, const char* k, Vec2 def) {
        f32 a[2] = {def.x, def.y};
        vec(o, k, a, 2);
        return Vec2{a[0], a[1]};
    }
    const json::Value* arr(const json::Value& o, const char* k, usize maxN) {
        const json::Value* v = o.get(k);
        if (!v) return nullptr;
        if (!v->is_array()) { fail(std::string("esperava lista: ") + k); return nullptr; }
        if (v->array.size() > maxN) { fail(std::string("lista grande demais: ") + k); return nullptr; }
        return v;
    }

    /// "static" + "keys" → trilha (ordenada; tempo repetido: fica o último).
    bool keys(const json::Value& o, Track& t) {
        t.staticValue = f(o, "static", 0.0f);
        const json::Value* ks = arr(o, "keys", kMaxPresetKeys);
        if (!ok()) return false;
        t.keys.clear();
        if (ks) {
            t.keys.reserve(ks->array.size());
            for (const json::Value& kv : ks->array) {
                if (!kv.is_array() || kv.array.size() < 2 || kv.array.size() > 10) return fail("keyframe invalido");
                f64 a[10] = {0, 0, 1, 0.33, 0.0, 0.67, 1.0, 0, 0, 0};
                for (usize i = 0; i < kv.array.size(); ++i) {
                    if (!kv.array[i].is_number()) return fail("keyframe invalido");
                    a[i] = kv.array[i].number;
                }
                if (a[0] != std::floor(a[0]) || std::fabs(a[0]) > static_cast<f64>(kMaxRelFrames)) return fail("tempo de keyframe invalido");
                if (std::fabs(a[1]) > 1e9) return fail("valor de keyframe fora da faixa");
                if (a[2] < 0 || a[2] > static_cast<f64>(Interpolation::Steps) || a[2] != std::floor(a[2])) return fail("interpolacao invalida");
                for (int i = 3; i < 9; ++i) if (std::fabs(a[i]) > 1e7) return fail("curva de keyframe fora da faixa");
                if (a[9] < 0 || a[9] > 65535) return fail("easing invalido");
                Keyframe k;
                k.time = FrameIndex{static_cast<i64>(a[0])};
                k.value = static_cast<f32>(a[1]);
                k.interp = static_cast<Interpolation>(static_cast<u8>(a[2]));
                k.bx1 = static_cast<f32>(a[3]); k.by1 = static_cast<f32>(a[4]);
                k.bx2 = static_cast<f32>(a[5]); k.by2 = static_cast<f32>(a[6]);
                k.tangentIn = static_cast<f32>(a[7]); k.tangentOut = static_cast<f32>(a[8]);
                k.easingPreset = static_cast<u16>(a[9]);
                t.keys.push_back(k);
            }
        }
        std::stable_sort(t.keys.begin(), t.keys.end(), [](const Keyframe& x, const Keyframe& y) { return x.time < y.time; });
        std::vector<Keyframe> uniq;
        uniq.reserve(t.keys.size());
        for (const Keyframe& k : t.keys) {
            if (!uniq.empty() && uniq.back().time == k.time) uniq.back() = k;
            else uniq.push_back(k);
        }
        t.keys = std::move(uniq);
        t.lastIndex = 0;
        return true;
    }
};

bool read_effects(Reader& r, const json::Value& root, const EffectRegistry* reg, Preset& p) {
    const json::Value* list = r.arr(root, "effects", kMaxEffectCount);
    if (!r.ok()) return false;
    if (!list || list->array.empty()) return r.fail("preset de efeitos vazio");
    for (u32 ei = 0; ei < list->array.size(); ++ei) {
        const json::Value& ev = list->array[ei];
        if (!ev.is_object()) return r.fail("efeito invalido");
        EffectInstance e;
        e.id = ei;
        const std::string key = r.s(ev, "key");
        if (!key.empty()) e.type = effect_type_id(key);
        else e.type = static_cast<EffectTypeId>(r.num(ev, "type", 0, 0, 4294967295.0));
        if (!r.ok()) return false;
        if (e.type == 0) return r.fail("efeito sem tipo");
        const ParameterRegistry* specs = reg ? reg->params(e.type) : nullptr;
        if (reg && !specs) return r.fail("efeito desconhecido: " + (key.empty() ? std::to_string(e.type) : key));
        e.enabled = r.b(ev, "enabled", true);
        if (specs) initialize_instance(e, *specs);

        // Parâmetro: pelo id estável quando o efeito é conhecido; senão, índice.
        auto param_index = [&](const json::Value& o, const char* idKey) -> u32 {
            if (specs) {
                const json::Value* idv = o.get(idKey);
                if (idv && idv->is_string()) {
                    const u32 k = specs->find(idv->string);
                    if (k != kInvalidIndex) return k;
                }
            }
            const u32 idx = r.u(o, "index", kInvalidIndex, 255);
            if (specs && idx != kInvalidIndex && idx >= specs->count()) return kInvalidIndex;
            return idx;
        };

        if (const json::Value* ps = r.arr(ev, "params", 256)) {
            for (const json::Value& pv : ps->array) {
                if (!pv.is_object()) return r.fail("parametro invalido");
                const u32 idx = param_index(pv, "id");
                if (!r.ok()) return false;
                if (idx == kInvalidIndex) continue;   // parâmetro que não existe mais
                if (idx >= e.params.size()) e.params.resize(idx + 1);
                ParamSlot& s = e.params[idx];
                r.vec(pv, "v", s.constant.v, 4, -1e9, 1e9);
                s.constant.ref = static_cast<u64>(r.num(pv, "ref", 0, 0, 1e15));
                const u32 src = r.u(pv, "src", 0, static_cast<u32>(ParamSource::Expression));
                // Expressão é do projeto: vira o valor constante.
                s.source = src == static_cast<u32>(ParamSource::Keyframes) ? ParamSource::Keyframes : ParamSource::Constant;
                s.expression = kInvalidIndex;
                if (!r.ok()) return false;
            }
        }
        if (const json::Value* cs = r.arr(ev, "curves", kMaxCurves)) {
            for (const json::Value& cv : cs->array) {
                if (!cv.is_array() || cv.array.size() != 4) return r.fail("curva invalida");
                CurveData c;
                for (u32 ch = 0; ch < 4; ++ch) {
                    const json::Value& pts = cv.array[ch];
                    if (!pts.is_array() || pts.array.size() % 2 != 0 || pts.array.size() > 2 * kMaxCurvePoints) return r.fail("curva invalida");
                    for (usize k = 0; k < pts.array.size(); k += 2) {
                        const json::Value& x = pts.array[k];
                        const json::Value& y = pts.array[k + 1];
                        if (!x.is_number() || !y.is_number() || x.number < -1 || x.number > 2 || y.number < -1 || y.number > 2) return r.fail("curva invalida");
                        c.channel[ch].push_back(CurveData::Point{static_cast<f32>(x.number), static_cast<f32>(y.number)});
                    }
                    std::stable_sort(c.channel[ch].begin(), c.channel[ch].end(), [](const auto& a, const auto& b) { return a.x < b.x; });
                }
                e.curves.push_back(std::move(c));
            }
        }
        if (const json::Value* gs = r.arr(ev, "gradients", kMaxCurves)) {
            for (const json::Value& gv : gs->array) {
                if (!gv.is_array() || gv.array.size() > kMaxStops) return r.fail("degrade invalido");
                GradientData g;
                for (const json::Value& sv : gv.array) {
                    if (!sv.is_array() || sv.array.size() != 5) return r.fail("degrade invalido");
                    f32 a[5];
                    for (u32 k = 0; k < 5; ++k) {
                        if (!sv.array[k].is_number() || std::fabs(sv.array[k].number) > 1e6) return r.fail("degrade invalido");
                        a[k] = static_cast<f32>(sv.array[k].number);
                    }
                    g.stops.push_back(GradientStop{a[0], Vec4{a[1], a[2], a[3], a[4]}});
                }
                e.gradients.push_back(std::move(g));
            }
        }
        // Referências internas (curva/degradê) dentro do que veio; referências
        // externas (camada/textura) não viajam.
        if (specs) {
            for (u32 k = 0; k < e.params.size() && k < specs->count(); ++k) {
                u64& ref = e.params[k].constant.ref;
                switch (specs->at(k).type) {
                    case ParamType::Curve: if (ref >= e.curves.size()) ref = 0; break;
                    case ParamType::Gradient: if (ref >= e.gradients.size()) ref = 0; break;
                    case ParamType::LayerReference:
                    case ParamType::TextureReference: ref = 0; break;
                    default: break;
                }
            }
        }
        if (const json::Value* ts = r.arr(ev, "tracks", kMaxTracks)) {
            for (const json::Value& tv : ts->array) {
                if (!tv.is_object()) return r.fail("trilha invalida");
                const u32 idx = param_index(tv, "param");
                const u32 comp = r.u(tv, "comp", 0, 3);
                if (!r.ok()) return false;
                if (idx == kInvalidIndex) continue;
                if (specs && comp >= component_count(specs->at(idx).type)) continue;
                Track t;
                t.property = TrackProperty::EffectParam;
                t.effectIndex = ei;
                t.effectParamIndex = param_track_key(idx, comp);
                if (!r.keys(tv, t)) return false;
                for (const Track& o : p.effectTracks) {
                    if (o.effectIndex == ei && o.effectParamIndex == t.effectParamIndex) return r.fail("trilha repetida");
                }
                p.effectTracks.push_back(std::move(t));
            }
        }
        if (!r.ok()) return false;
        p.effects.push_back(std::move(e));
    }
    return true;
}

bool read_text(Reader& r, const json::Value& root, Preset& p) {
    if (const json::Value* sv = root.get("style")) {
        if (!sv->is_object()) return r.fail("estilo invalido");
        TextData t;   // padrões para o que faltar
        t.fontFamily = r.s(*sv, "fontFamily");
        t.fontWeight = static_cast<u16>(r.u(*sv, "fontWeight", 400, 1000));
        if (t.fontWeight == 0) t.fontWeight = 400;
        t.fontItalic = r.b(*sv, "fontItalic", false);
        t.size = r.f(*sv, "size", 72.0f, 1.0, 2000.0);
        t.color = r.vec4(*sv, "color", t.color, 0.0, 1.0);
        t.strokeWidth = r.f(*sv, "strokeWidth", 0.0f, 0.0, 200.0);
        t.strokeColor = r.vec4(*sv, "strokeColor", t.strokeColor, 0.0, 1.0);
        t.alignment = r.u(*sv, "alignment", 0, 2);
        t.lineHeight = r.f(*sv, "lineHeight", 1.2f, 0.1, 10.0);
        t.tracking = r.f(*sv, "tracking", 0.0f, -1000.0, 1000.0);
        t.boxMode = r.u(*sv, "boxMode", 0, 3);
        t.autoSize = r.b(*sv, "autoSize", true);
        f32 box[4] = {t.box.x, t.box.y, t.box.w, t.box.h};
        r.vec(*sv, "box", box, 4, -1e5, 1e5);
        t.box = Rect{box[0], box[1], std::max(1.0f, box[2]), std::max(1.0f, box[3])};
        t.background = r.b(*sv, "background", false);
        t.backgroundColor = r.vec4(*sv, "backgroundColor", t.backgroundColor, 0.0, 1.0);
        t.backgroundPadding = r.f(*sv, "backgroundPadding", 14.0f, 0.0, 1000.0);
        t.backgroundRadius = r.f(*sv, "backgroundRadius", 10.0f, 0.0, 1000.0);
        t.shadow = r.b(*sv, "shadow", false);
        t.shadowColor = r.vec4(*sv, "shadowColor", t.shadowColor, 0.0, 1.0);
        t.shadowOffset = r.vec2(*sv, "shadowOffset", t.shadowOffset);
        t.shadowBlur = r.f(*sv, "shadowBlur", 6.0f, 0.0, 500.0);
        if (!r.ok()) return false;
        t.content.clear();
        p.style = std::move(t);
        p.textParts |= kTextStyle;
    }
    if (const json::Value* av = r.arr(root, "animators", kMaxAnimators)) {
        for (const json::Value& ao : av->array) {
            if (!ao.is_object()) return r.fail("animador invalido");
            TextAnimator a;
            a.name = r.s(ao, "name");
            a.enabled = r.b(ao, "enabled", true);
            if (const json::Value* so = ao.get("selector")) {
                if (!so->is_object()) return r.fail("seletor invalido");
                TextSelector& s = a.selector;
                s.basedOn = static_cast<u8>(r.u(*so, "basedOn", 0, 2));
                s.type = static_cast<u8>(r.u(*so, "type", 0, 1));
                s.shape = static_cast<u8>(r.u(*so, "shape", 0, 5));
                s.randomOrder = r.b(*so, "randomOrder", false);
                s.seed = r.u(*so, "seed", 1, 0xFFFFFFFFu);
                s.start = r.f(*so, "start", 0.0f, -1e5, 1e5);
                s.end = r.f(*so, "end", 100.0f, -1e5, 1e5);
                s.offset = r.f(*so, "offset", 0.0f, -1e5, 1e5);
                s.amount = r.f(*so, "amount", 100.0f, -1e5, 1e5);
                s.easeHigh = r.f(*so, "easeHigh", 0.0f, -100.0, 100.0);
                s.easeLow = r.f(*so, "easeLow", 0.0f, -100.0, 100.0);
                s.wiggleRate = r.f(*so, "wiggleRate", 2.0f, 0.0, 1000.0);
            }
            a.props = r.u(ao, "props", 0, 0x7FF);
            a.position = r.vec3(ao, "position", a.position);
            a.scale = r.vec2(ao, "scale", a.scale);
            a.rotation = r.vec3(ao, "rotation", a.rotation);
            a.opacity = r.f(ao, "opacity", 100.0f);
            a.tracking = r.f(ao, "tracking", 0.0f);
            a.blur = r.f(ao, "blur", 0.0f, 0.0, 1000.0);
            a.skew = r.f(ao, "skew", 0.0f);
            a.strokeWidth = r.f(ao, "strokeWidth", 0.0f, 0.0, 200.0);
            a.charOffset = r.f(ao, "charOffset", 0.0f);
            a.fill = r.vec4(ao, "fill", a.fill, 0.0, 1.0);
            a.stroke = r.vec4(ao, "stroke", a.stroke, 0.0, 1.0);
            if (!r.ok()) return false;
            p.animators.push_back(std::move(a));
        }
        p.textParts |= kTextAnimators;
        if (const json::Value* ts = r.arr(root, "tracks", kMaxTracks)) {
            for (const json::Value& tv : ts->array) {
                if (!tv.is_object()) return r.fail("trilha invalida");
                Track t;
                t.property = TrackProperty::TextAnimParam;
                t.effectIndex = r.u(tv, "animator", 0, kMaxAnimators);
                t.effectParamIndex = r.u(tv, "param", 0, 63);
                if (!r.ok()) return false;
                if (t.effectIndex >= p.animators.size()) return r.fail("trilha de animador inexistente");
                if (!r.keys(tv, t)) return false;
                for (const Track& o : p.textTracks) {
                    if (o.effectIndex == t.effectIndex && o.effectParamIndex == t.effectParamIndex) return r.fail("trilha repetida");
                }
                p.textTracks.push_back(std::move(t));
            }
        }
    }
    if (!r.ok()) return false;
    if (p.textParts == 0) return r.fail("preset de texto vazio");
    return true;
}

bool read_animation(Reader& r, const json::Value& root, Preset& p) {
    const json::Value* ts = r.arr(root, "tracks", kPropCount);
    if (!r.ok()) return false;
    if (!ts || ts->array.empty()) return r.fail("preset de animacao vazio");
    i64 span = 0;
    for (const json::Value& tv : ts->array) {
        if (!tv.is_object()) return r.fail("trilha invalida");
        const std::string name = r.s(tv, "prop");
        u32 prop = kPropCount;
        for (u32 i = 0; i < kPropCount; ++i) if (name == kPropNames[i]) prop = i;
        if (prop == kPropCount) return r.fail("propriedade desconhecida: " + name);
        AnimTrack a;
        a.property = static_cast<TrackProperty>(prop);
        a.relative = r.b(tv, "relative", false);
        a.track.property = a.property;
        if (!r.keys(tv, a.track)) return false;
        if (a.track.keys.empty()) return r.fail("trilha sem keyframe: " + name);
        if (a.track.keys.front().time.value < 0) return r.fail("tempo negativo na animacao");
        for (const AnimTrack& o : p.animTracks) if (o.property == a.property) return r.fail("propriedade repetida: " + name);
        span = std::max(span, a.track.keys.back().time.value);
        p.animTracks.push_back(std::move(a));
    }
    p.span = span;
    return true;
}

bool read_caption(Reader& r, const json::Value& root, Preset& p) {
    const json::Value* c = root.get("caption");
    if (!c || !c->is_object()) return r.fail("preset de legenda sem bloco caption");
    text::CaptionOptions o;
    o.mode = r.u(*c, "mode", o.mode, 1);
    o.maxWords = std::max(1u, r.u(*c, "maxWords", o.maxWords, 20));
    o.maxChars = std::max(4u, r.u(*c, "maxChars", o.maxChars, 80));
    o.maxLines = std::max(1u, r.u(*c, "maxLines", o.maxLines, 5));
    o.style = r.u(*c, "style", o.style, text::kCaptionStyleCount - 1);
    o.highlight = r.b(*c, "highlight", o.highlight);
    o.uppercase = r.b(*c, "uppercase", o.uppercase);
    o.breakOnPause = r.b(*c, "breakOnPause", o.breakOnPause);
    o.pauseSec = r.f(*c, "pauseSec", o.pauseSec, 0.05, 5.0);
    o.posY = r.f(*c, "posY", o.posY, 0.1, 0.9);
    o.sizeFrac = r.f(*c, "sizeFrac", o.sizeFrac, 0.01, 0.3);
    o.highlightColor = r.vec4(*c, "highlightColor", o.highlightColor, 0.0, 1.0);
    const bool rf = r.b(*c, "removeFillers", true);
    if (!r.ok()) return false;
    p.caption = o;
    p.removeFillers = rf;
    return true;
}

bool read_curve(Reader& r, const json::Value& root, Preset& p) {
    const json::Value* c = root.get("curve");
    if (!c || !c->is_object()) return r.fail("preset de curva sem bloco curve");
    const u32 interp = r.u(*c, "interp", static_cast<u32>(Interpolation::Bezier), static_cast<u32>(Interpolation::Steps));
    const f32 x1 = r.f(*c, "x1", 0.42f, 0.0, 1.0), y1 = r.f(*c, "y1", 0.0f, -2.0, 3.0);
    const f32 x2 = r.f(*c, "x2", 0.58f, 0.0, 1.0), y2 = r.f(*c, "y2", 1.0f, -2.0, 3.0);
    if (!r.ok()) return false;
    p.curveInterp = static_cast<Interpolation>(static_cast<u8>(interp));
    p.x1 = x1; p.y1 = y1; p.x2 = x2; p.y2 = y2;
    return true;
}

/// Tempo relativo → instante local de destino (fps reescalado ou esticado).
i64 retime(i64 rel, i64 anchor, f64 scale) noexcept {
    return anchor + static_cast<i64>(std::llround(static_cast<f64>(rel) * scale));
}

/// Keys com os tempos novos; colisão depois de arredondar: fica o último.
void retime_track(Track& t, i64 anchor, f64 scale, f32 valueAdd) {
    std::vector<Keyframe> out;
    out.reserve(t.keys.size());
    for (Keyframe k : t.keys) {
        k.time = FrameIndex{retime(k.time.value, anchor, scale)};
        k.value += valueAdd;
        if (!out.empty() && out.back().time == k.time) out.back() = k;
        else out.push_back(k);
    }
    t.keys = std::move(out);
    t.staticValue += valueAdd;
    t.lastIndex = 0;
}

f64 fps_scale(const Preset& p, f64 fps) noexcept {
    if (!(p.fps > 0.0) || !(fps > 0.0) || std::fabs(p.fps - fps) < 1e-6) return 1.0;
    return fps / p.fps;
}

/// Copia o efeito `src` da camada para a posição `pos` do preset, com os
/// keyframes dele (tempo relativo ao início da camada). Expressão e
/// referência externa não viajam.
void capture_one_effect(const Layer& l, const EffectInstance& src, u32 pos, const EffectRegistry* registry, Preset& p) {
    const i64 base = l.offset.value;
    EffectInstance e = src;
    const u32 oldId = e.id;
    e.id = pos;
    e.mask = MaskId{};
    const ParameterRegistry* specs = registry ? registry->params(e.type) : nullptr;
    for (u32 k = 0; k < e.params.size(); ++k) {
        ParamSlot& s = e.params[k];
        if (s.source == ParamSource::Expression) s.source = ParamSource::Constant;
        s.expression = kInvalidIndex;
        if (specs && k < specs->count()
            && (specs->at(k).type == ParamType::LayerReference || specs->at(k).type == ParamType::TextureReference)) {
            s.constant.ref = 0;
        }
    }
    for (u32 t = 0; t < l.tracks.size(); ++t) {
        const Track& tr = l.tracks.at(t);
        if (tr.property != TrackProperty::EffectParam || tr.effectIndex != oldId) continue;
        Track c = tr;
        c.effectIndex = pos;
        retime_track(c, -base, 1.0, 0.0f);
        p.effectTracks.push_back(std::move(c));
    }
    p.effects.push_back(std::move(e));
}

} // namespace

const char* kind_name(PresetKind k) noexcept {
    const u32 i = static_cast<u32>(k);
    return i < 5 ? kKindNames[i] : "";
}

PresetKind kind_from_name(std::string_view s) noexcept {
    for (u32 i = 0; i < 5; ++i) if (s == kKindNames[i]) return static_cast<PresetKind>(i);
    return PresetKind::Invalid;
}

std::string write(const Preset& p, const EffectRegistry* registry) {
    if (p.kind == PresetKind::Invalid) return {};
    json::Writer w;
    w.begin_object();
    w.key("aurea_preset").value(kPresetVersion);
    w.key("kind").value(kind_name(p.kind));
    w.key("name").value(p.name);
    switch (p.kind) {
        case PresetKind::Effects:
            w.key("fps").value(p.fps);
            write_effects(w, p, registry);
            break;
        case PresetKind::Text:
            w.key("fps").value(p.fps);
            write_text(w, p);
            break;
        case PresetKind::Animation:
            w.key("fps").value(p.fps);
            w.key("span").value(p.span);
            w.key("tracks").begin_array();
            for (const AnimTrack& a : p.animTracks) {
                w.begin_object();
                w.key("prop").value(kPropNames[static_cast<u32>(a.property)]);
                w.key("relative").value(a.relative);
                write_keys(w, a.track);
                w.end_object();
            }
            w.end_array();
            break;
        case PresetKind::Caption: write_caption(w, p.caption, p.removeFillers); break;
        case PresetKind::Curve:
            w.key("curve").begin_object();
            w.key("interp").value(static_cast<u32>(p.curveInterp));
            w.key("x1").value(p.x1).key("y1").value(p.y1).key("x2").value(p.x2).key("y2").value(p.y2);
            w.end_object();
            break;
        default: break;
    }
    w.end_object();
    return w.str();
}

bool parse(std::string_view text, Preset& out, const EffectRegistry* registry, std::string* error) {
    json::Value root;
    std::string jerr;
    if (!json::parse(text, root, &jerr)) {
        if (error) *error = "JSON invalido: " + jerr;
        return false;
    }
    return parse_value(root, out, registry, error);
}

bool parse_value(const json::Value& root, Preset& out, const EffectRegistry* registry, std::string* error) {
    Reader r;
    Preset p;
    if (!root.is_object()) r.fail("preset nao e um objeto");
    const u32 version = r.ok() ? r.u(root, "aurea_preset", 0, 1000000) : 0;
    if (r.ok() && version == 0) r.fail("nao e um preset do Aurea");
    if (r.ok() && version > kPresetVersion) r.fail("preset de versao mais nova (" + std::to_string(version) + ")");
    if (r.ok()) {
        p.kind = kind_from_name(r.s(root, "kind"));
        if (p.kind == PresetKind::Invalid) r.fail("tipo de preset desconhecido");
    }
    if (r.ok()) {
        p.name = r.s(root, "name");
        p.fps = r.num(root, "fps", 30.0, 1.0, 1000.0);
    }
    if (r.ok()) {
        switch (p.kind) {
            case PresetKind::Effects: read_effects(r, root, registry, p); break;
            case PresetKind::Text: read_text(r, root, p); break;
            case PresetKind::Animation: read_animation(r, root, p); break;
            case PresetKind::Caption: read_caption(r, root, p); break;
            case PresetKind::Curve: read_curve(r, root, p); break;
            default: break;
        }
    }
    if (!r.ok()) {
        if (error) *error = r.err;
        return false;
    }
    out = std::move(p);
    return true;
}

bool capture(const Layer& l, PresetKind kind, std::string name, f64 fps, u32 parts, const EffectRegistry* registry, Preset& out) {
    Preset p;
    p.kind = kind;
    p.name = std::move(name);
    p.fps = fps > 0.0 ? fps : 30.0;
    // Tempo relativo ao INÍCIO da camada = local − local(início) = local − offset.
    const i64 base = l.offset.value;
    switch (kind) {
        case PresetKind::Effects: {
            if (l.effects.empty()) return false;
            for (u32 i = 0; i < l.effects.size(); ++i) capture_one_effect(l, l.effects[i], i, registry, p);
            break;
        }
        case PresetKind::Text: {
            if (l.kind != LayerKind::Text) return false;
            p.textParts = parts & kTextAll;
            if (p.textParts == 0) return false;
            if (p.textParts & kTextStyle) {
                p.style = l.text;
                p.style.content.clear();
                p.style.fontPath.clear();
                p.style.spans.clear();
                p.style.animators.clear();
                p.style.captionSource = 0;
            }
            if (p.textParts & kTextAnimators) {
                p.animators = l.text.animators;
                for (u32 t = 0; t < l.tracks.size(); ++t) {
                    const Track& tr = l.tracks.at(t);
                    if (tr.property != TrackProperty::TextAnimParam || tr.effectIndex >= p.animators.size()) continue;
                    Track c = tr;
                    retime_track(c, -base, 1.0, 0.0f);
                    p.textTracks.push_back(std::move(c));
                }
            }
            break;
        }
        case PresetKind::Animation: {
            i64 t0 = std::numeric_limits<i64>::max();
            for (u32 t = 0; t < l.tracks.size(); ++t) {
                const Track& tr = l.tracks.at(t);
                if (is_transform_prop(tr.property) && tr.effectIndex == kInvalidIndex && !tr.keys.empty()) {
                    t0 = std::min(t0, tr.keys.front().time.value);
                }
            }
            if (t0 == std::numeric_limits<i64>::max()) return false;
            for (u32 t = 0; t < l.tracks.size(); ++t) {
                const Track& tr = l.tracks.at(t);
                if (!is_transform_prop(tr.property) || tr.effectIndex != kInvalidIndex || tr.keys.empty()) continue;
                AnimTrack a;
                a.property = tr.property;
                a.relative = is_relative_prop(tr.property);
                a.track = tr;
                a.track.effectIndex = kInvalidIndex;
                a.track.effectParamIndex = 0;
                retime_track(a.track, -t0, 1.0, a.relative ? -tr.keys.front().value : 0.0f);
                p.span = std::max(p.span, a.track.keys.back().time.value);
                p.animTracks.push_back(std::move(a));
            }
            break;
        }
        default: return false;
    }
    out = std::move(p);
    return true;
}

bool capture_effect(const Layer& l, u32 effectId, std::string name, f64 fps, const EffectRegistry* registry, Preset& out) {
    if (effectId == kInvalidIndex) return false;
    for (const EffectInstance& e : l.effects) {
        if (e.id != effectId) continue;
        Preset p;
        p.kind = PresetKind::Effects;
        p.name = std::move(name);
        p.fps = fps > 0.0 ? fps : 30.0;
        capture_one_effect(l, e, 0, registry, p);
        out = std::move(p);
        return true;
    }
    return false;
}

bool applicable(const Preset& p, const Layer& l) noexcept {
    switch (p.kind) {
        case PresetKind::Effects: return !p.effects.empty() && l.effects.size() + p.effects.size() <= kMaxEffectCount;
        case PresetKind::Text: return l.kind == LayerKind::Text && p.textParts != 0;
        case PresetKind::Animation: return !p.animTracks.empty() && l.kind != LayerKind::Audio;
        default: return false;
    }
}

bool apply(const Preset& p, Layer& l, i64 anchorLocal, i64 durationFrames, f64 fps, const EffectRegistry* registry) {
    if (!applicable(p, l)) return false;
    const f64 scale = fps_scale(p, fps);
    const i64 start = l.offset.value;   // instante local do início da camada
    switch (p.kind) {
        case PresetKind::Effects: {
            // Ids novos depois do maior da camada (e do contador): os keyframes
            // seguem pelo id, e o próximo efeito adicionado não colide.
            u32 next = l.nextEffectId;
            for (const EffectInstance& e : l.effects) if (e.id != kInvalidIndex) next = std::max(next, e.id + 1);
            for (u32 i = 0; i < p.effects.size(); ++i) {
                EffectInstance e = p.effects[i];
                e.id = next++;
                e.mask = MaskId{};
                // Efeito que ganhou parâmetro depois do preset: completa com o padrão.
                if (const ParameterRegistry* specs = registry ? registry->params(e.type) : nullptr) {
                    if (e.params.size() < specs->count()) {
                        EffectInstance d;
                        initialize_instance(d, *specs);
                        for (usize k = e.params.size(); k < d.params.size(); ++k) e.params.push_back(d.params[k]);
                    }
                }
                for (const Track& t : p.effectTracks) {
                    if (t.effectIndex != i) continue;
                    Track c = t;
                    c.effectIndex = e.id;
                    retime_track(c, start, scale, 0.0f);
                    l.tracks.add(std::move(c));
                }
                l.effects.push_back(std::move(e));
            }
            l.nextEffectId = next;
            return true;
        }
        case PresetKind::Text: {
            if (p.textParts & kTextStyle) {
                TextData& t = l.text;
                const TextData& s = p.style;
                t.fontFamily = s.fontFamily;
                t.fontWeight = s.fontWeight;
                t.fontItalic = s.fontItalic;
                t.fontPath.clear();
                t.size = s.size;
                t.color = s.color;
                t.strokeWidth = s.strokeWidth;
                t.strokeColor = s.strokeColor;
                t.alignment = s.alignment;
                t.lineHeight = s.lineHeight;
                t.tracking = s.tracking;
                t.boxMode = s.boxMode;
                t.autoSize = s.autoSize;
                t.box = s.box;
                t.background = s.background;
                t.backgroundColor = s.backgroundColor;
                t.backgroundPadding = s.backgroundPadding;
                t.backgroundRadius = s.backgroundRadius;
                t.shadow = s.shadow;
                t.shadowColor = s.shadowColor;
                t.shadowOffset = s.shadowOffset;
                t.shadowBlur = s.shadowBlur;
            }
            if (p.textParts & kTextAnimators) {
                l.tracks.remove_if([](const Track& x) { return x.property == TrackProperty::TextAnimParam; });
                l.text.animators = p.animators;
                for (const Track& t : p.textTracks) {
                    Track c = t;
                    retime_track(c, start, scale, 0.0f);
                    l.tracks.add(std::move(c));
                }
            }
            return true;
        }
        case PresetKind::Animation: {
            f64 s = scale;
            if (durationFrames > 0 && p.span > 0) s = static_cast<f64>(durationFrames) / static_cast<f64>(p.span);
            for (const AnimTrack& a : p.animTracks) {
                // Base do deslocamento: o valor que a camada tem no instante.
                f32 add = 0.0f;
                if (a.relative) {
                    const Track* cur = l.tracks.find(a.property);
                    add = (cur && !cur->keys.empty()) ? cur->sample(FrameIndex{anchorLocal}) : transform_value(l.transform, a.property);
                }
                Track c = a.track;
                c.property = a.property;
                c.effectIndex = kInvalidIndex;
                c.effectParamIndex = 0;
                retime_track(c, anchorLocal, s, add);
                const TrackProperty prop = a.property;
                l.tracks.remove_if([prop](const Track& x) { return x.property == prop && x.effectIndex == kInvalidIndex; });
                l.tracks.add(std::move(c));
            }
            return true;
        }
        default: return false;
    }
}

std::string make_caption_preset(const std::string& name, const text::CaptionOptions& o, bool removeFillers) {
    Preset p;
    p.kind = PresetKind::Caption;
    p.name = name;
    p.caption = o;
    p.removeFillers = removeFillers;
    return write(p);
}

std::string make_curve_preset(const std::string& name, Interpolation interp, f32 x1, f32 y1, f32 x2, f32 y2) {
    Preset p;
    p.kind = PresetKind::Curve;
    p.name = name;
    p.curveInterp = interp;
    p.x1 = x1; p.y1 = y1; p.x2 = x2; p.y2 = y2;
    return write(p);
}

} // namespace presets
} // namespace aurea
