// =============================================================================
//  0.3.4 plugins: a biblioteca padrão do AureaScript — Math, JSON, Object,
//  Array, String, Number, Date, Error, console e os métodos de texto, lista e
//  número. Nada aqui toca disco, rede ou sistema.
//
//  Textos são UTF-8; índices e tamanhos contam CARACTERES (code points), não
//  unidades UTF-16 — "ação".length é 4, como a pessoa espera.
// =============================================================================
#include "ScriptAst.hpp"
#include "aurea/project/Presets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace aurea::plugins::script {

namespace {

Value arg(const std::vector<Value>& a, usize i) { return i < a.size() ? a[i] : Value(); }

i64 to_int32_like(f64 n) { return std::isfinite(n) ? static_cast<i64>(std::trunc(n)) : 0; }

// --- UTF-8 ---------------------------------------------------------------------
usize cp_width(unsigned char c) { return c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; }

/// Deslocamento em bytes de cada caractere, mais o fim.
std::vector<usize> offsets(const std::string& s) {
    std::vector<usize> out;
    out.reserve(s.size() + 1);
    for (usize i = 0; i < s.size(); i += cp_width(static_cast<unsigned char>(s[i]))) out.push_back(i);
    out.push_back(s.size());
    return out;
}
u32 decode(const std::string& s, usize i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const usize w = cp_width(c);
    if (i + w > s.size()) return c;
    if (w == 1) return c;
    u32 cp = c & (w == 2 ? 0x1Fu : w == 3 ? 0x0Fu : 0x07u);
    for (usize k = 1; k < w; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
    return cp;
}
void encode(std::string& out, u32 cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
/// Maiúsculas/minúsculas: ASCII, Latin-1, Latin Extended-A, grego e cirílico básicos.
u32 to_upper_cp(u32 c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        const bool oddLower = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (oddLower ? (c % 2 == 0) : (c % 2 == 1)) return c - 1;
        return c;
    }
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}
u32 to_lower_cp(u32 c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x178) return 0xFF;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        const bool oddLower = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (oddLower ? (c % 2 == 1) : (c % 2 == 0)) return c + 1;
        return c;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}
std::string map_case(const std::string& s, bool upper) {
    std::string out;
    out.reserve(s.size());
    for (usize i = 0; i < s.size(); i += cp_width(static_cast<unsigned char>(s[i]))) {
        const u32 cp = decode(s, i);
        encode(out, upper ? to_upper_cp(cp) : to_lower_cp(cp));
    }
    return out;
}
bool is_space_at(const std::string& s, usize i, usize& width) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { width = 1; return true; }
    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) { width = 2; return true; }
    return false;
}
std::string trim(const std::string& s, bool start, bool end) {
    usize a = 0, b = s.size(), w = 0;
    if (start) while (a < b && is_space_at(s, a, w)) a += w;
    if (end) {
        while (b > a) {
            if (std::isspace(static_cast<unsigned char>(s[b - 1])) && static_cast<unsigned char>(s[b - 1]) < 0x80) { --b; continue; }
            if (b >= a + 2 && static_cast<unsigned char>(s[b - 2]) == 0xC2 && static_cast<unsigned char>(s[b - 1]) == 0xA0) { b -= 2; continue; }
            break;
        }
    }
    return s.substr(a, b - a);
}

/// Índice relativo do JavaScript (negativo conta do fim), preso em [0, len].
usize rel_index(Context& c, const Value& v, usize len, usize def) {
    if (v.is_undefined()) return def;
    f64 n = std::trunc(c.to_number(v));
    if (std::isnan(n)) n = 0;
    if (n < 0) n = std::max(0.0, static_cast<f64>(len) + n);
    return static_cast<usize>(std::min(n, static_cast<f64>(len)));
}

const std::string& self_text(Context& c, const Value& self, const char* method) {
    if (!self.is_string()) c.throw_error("TypeError", std::string(method) + " e um metodo de texto");
    return self.as_string();
}
const ObjectRef& self_array(Context& c, const Value& self, const char* method) {
    if (!self.is_array()) c.throw_error("TypeError", std::string(method) + " e um metodo de lista");
    return self.as_object();
}
Value callback(Context& c, const Value& fn, std::vector<Value> args) {
    return c.call_function(fn, Value::undefined(), args);
}
void need_function(Context& c, const Value& fn, const char* method) {
    if (!fn.is_function()) c.throw_error("TypeError", std::string(method) + " precisa de uma funcao");
}

bool same_value_zero(const Value& a, const Value& b) {
    if (a.is_number() && b.is_number()) {
        const f64 x = a.as_number(), y = b.as_number();
        return x == y || (std::isnan(x) && std::isnan(y));
    }
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Type::Undefined: case Type::Null: return true;
        case Type::Boolean: return a.as_bool() == b.as_bool();
        case Type::String: return a.as_string() == b.as_string();
        case Type::Object: return a.as_object() == b.as_object();
        default: return false;
    }
}
bool strict_eq(const Value& a, const Value& b) {
    if (a.is_number() && b.is_number()) return a.as_number() == b.as_number();
    return same_value_zero(a, b);
}

/// Ordenação estável que aguenta qualquer comparador (std::sort não aguenta um inconsistente).
void merge_sort(std::vector<Value>& v, const std::function<bool(const Value&, const Value&)>& less) {
    if (v.size() < 2) return;
    std::vector<Value> tmp(v.size());
    for (usize width = 1; width < v.size(); width *= 2) {
        for (usize lo = 0; lo < v.size(); lo += 2 * width) {
            const usize mid = std::min(lo + width, v.size());
            const usize hi = std::min(lo + 2 * width, v.size());
            usize i = lo, j = mid, k = lo;
            while (i < mid && j < hi) tmp[k++] = less(v[j], v[i]) ? v[j++] : v[i++];
            while (i < mid) tmp[k++] = v[i++];
            while (j < hi) tmp[k++] = v[j++];
        }
        v.swap(tmp);
    }
}

// --- datas (UTC) ---------------------------------------------------------------
struct Civil { i64 y; u32 m; u32 d; };
Civil civil_from_days(i64 z) {
    z += 719468;
    const i64 era = (z >= 0 ? z : z - 146096) / 146097;
    const u32 doe = static_cast<u32>(z - era * 146097);
    const u32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const i64 y = static_cast<i64>(yoe) + era * 400;
    const u32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const u32 mp = (5 * doy + 2) / 153;
    const u32 d = doy - (153 * mp + 2) / 5 + 1;
    const u32 m = mp < 10 ? mp + 3 : mp - 9;
    return {y + (m <= 2), m, d};
}
i64 days_from_civil(i64 y, u32 m, u32 d) {
    y -= m <= 2;
    const i64 era = (y >= 0 ? y : y - 399) / 400;
    const u32 yoe = static_cast<u32>(y - era * 400);
    const u32 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<i64>(doe) - 719468;
}
f64 now_ms() {
    return static_cast<f64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
std::string iso_text(f64 ms) {
    const i64 t = static_cast<i64>(std::floor(ms));
    i64 days = t / 86400000;
    i64 rem = t % 86400000;
    if (rem < 0) { rem += 86400000; --days; }
    const Civil c = civil_from_days(days);
    char b[40];
    std::snprintf(b, sizeof(b), "%04lld-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ", static_cast<long long>(c.y), c.m, c.d,
                  static_cast<long long>(rem / 3600000), static_cast<long long>(rem / 60000 % 60),
                  static_cast<long long>(rem / 1000 % 60), static_cast<long long>(rem % 1000));
    return b;
}

// --- JSON ------------------------------------------------------------------------
void json_quote(std::string& out, const std::string& s) {
    out += '"';
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    out += b;
                } else out += ch;
        }
    }
    out += '"';
}

} // namespace

std::string Context::json_stringify(const Value& root, std::string_view indent) {
    std::string out;
    std::vector<const Object*> stack;
    std::function<bool(const Value&, usize)> write = [&](const Value& v, usize depth) -> bool {
        if (out.size() > limits_.maxStringBytes) throw_error("RangeError", "JSON grande demais");
        switch (v.type()) {
            case Type::Undefined: return false;
            case Type::Null: out += "null"; return true;
            case Type::Boolean: out += v.as_bool() ? "true" : "false"; return true;
            case Type::Number: out += std::isfinite(v.as_number()) ? number_text(v.as_number()) : "null"; return true;
            case Type::String: json_quote(out, v.as_string()); return true;
            case Type::Object: break;
        }
        if (v.is_function()) return false;
        const Object* o = v.as_object().get();
        if (std::find(stack.begin(), stack.end(), o) != stack.end()) throw_error("TypeError", "JSON.stringify: estrutura circular");
        if (stack.size() > 64) throw_error("RangeError", "JSON.stringify: aninhado demais");
        // toJSON nativo (Date)
        if (const Prop* tj = o->proto ? o->proto->props.find("toJSON") : nullptr; tj && tj->value.is_function()) {
            std::vector<Value> none;
            return write(call_function(tj->value, v, none), depth);
        }
        stack.push_back(o);
        const std::string pad = indent.empty() ? std::string() : "\n" + [&] { std::string s; for (usize i = 0; i <= depth; ++i) s += indent; return s; }();
        const std::string padEnd = indent.empty() ? std::string() : "\n" + [&] { std::string s; for (usize i = 0; i < depth; ++i) s += indent; return s; }();
        if (o->kind == ObjKind::Array) {
            out += '[';
            for (usize i = 0; i < o->elements.size(); ++i) {
                if (i) out += ',';
                out += pad;
                if (!write(o->elements[i], depth + 1)) out += "null";
            }
            if (!o->elements.empty()) out += padEnd;
            out += ']';
        } else {
            out += '{';
            bool first = true;
            for (const auto& [key, p] : o->props.entries()) {
                if (p.hidden) continue;
                Value val = p.value;
                if (p.getter) { std::vector<Value> none; val = p.getter(*this, v, none); }
                if (val.is_undefined() || val.is_function()) continue;
                const usize mark = out.size();
                if (!first) out += ',';
                out += pad;
                json_quote(out, key);
                out += indent.empty() ? ":" : ": ";
                if (!write(val, depth + 1)) { out.resize(mark); continue; }
                first = false;
            }
            if (!first) out += padEnd;
            out += '}';
        }
        stack.pop_back();
        return true;
    };
    if (!write(root, 0)) return std::string();
    return out;
}

Value Context::json_parse(std::string_view text) {
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err)) throw_error("SyntaxError", "JSON invalido: " + (err.empty() ? std::string("malformado") : err));
    std::function<Value(const json::Value&)> conv = [&](const json::Value& j) -> Value {
        switch (j.type) {
            case json::Value::Type::Null: return Value::null();
            case json::Value::Type::Bool: return Value::boolean(j.boolean);
            case json::Value::Type::Number: return Value::number(j.number);
            case json::Value::Type::String: return str(j.string);
            case json::Value::Type::Array: {
                std::vector<Value> items;
                items.reserve(j.array.size());
                for (const json::Value& e : j.array) items.push_back(conv(e));
                return Value::object(new_array(std::move(items)));
            }
            case json::Value::Type::Object: {
                ObjectRef o = new_object();
                for (const auto& [k, e] : j.object) {
                    o->props.put(k).value = conv(e);
                    o->grow(64 + static_cast<i64>(k.size()));
                }
                return Value::object(std::move(o));
            }
        }
        return Value::null();
    };
    return conv(root);
}

void Context::install_builtins() {
    Context& c = *this;
    objectProto = ObjectRef(new Object(heap_.get(), ObjKind::Plain));
    track(objectProto);
    functionProto = new_object_with(objectProto);
    arrayProto = new_object_with(objectProto);
    stringProto = new_object_with(objectProto);
    numberProto = new_object_with(objectProto);
    errorProto = new_object_with(objectProto);

    // --- Object.prototype -----------------------------------------------------------
    method(objectProto, "hasOwnProperty", [](Context& cx, const Value& self, std::vector<Value>& a) {
        if (!self.is_object()) return Value::boolean(false);
        const std::string key = cx.to_string(arg(a, 0));
        const ObjectRef& o = self.as_object();
        if (o->kind == ObjKind::Array && !key.empty() && key.size() < 10
            && std::all_of(key.begin(), key.end(), [](char ch) { return ch >= '0' && ch <= '9'; })
            && (key.size() == 1 || key[0] != '0'))
            return Value::boolean(std::stoul(key) < o->elements.size());
        const Prop* p = o->props.find(key);
        return Value::boolean(p && !p->hidden);
    });
    method(objectProto, "toString", [](Context& cx, const Value& self, std::vector<Value>&) {
        return cx.str(self.is_object() && self.as_object()->kind != ObjKind::Plain ? cx.to_string(self) : std::string("[object Object]"));
    });

    // --- Function.prototype -----------------------------------------------------------
    method(functionProto, "call", [](Context& cx, const Value& self, std::vector<Value>& a) {
        std::vector<Value> rest(a.size() > 1 ? a.begin() + 1 : a.end(), a.end());
        return cx.call_function(self, arg(a, 0), rest);
    });
    method(functionProto, "apply", [](Context& cx, const Value& self, std::vector<Value>& a) {
        std::vector<Value> rest;
        if (arg(a, 1).is_array()) rest = arg(a, 1).as_object()->elements;
        return cx.call_function(self, arg(a, 0), rest);
    });
    method(functionProto, "bind", [](Context& cx, const Value& self, std::vector<Value>& a) {
        if (!self.is_function()) cx.throw_error("TypeError", "bind precisa de uma funcao");
        // O alvo fica em propriedades escondidas (a coleta enxerga; a captura não).
        ObjectRef bound = cx.new_native(self.as_object()->nativeName, nullptr);
        std::weak_ptr<Object> weak = bound;
        bound->native = [weak](Context& cy, const Value&, std::vector<Value>& args) {
            const ObjectRef me = weak.lock();
            if (!me) return Value::undefined();
            const Prop* target = me->props.find("@target");
            const Prop* thisV = me->props.find("@this");
            const Prop* pre = me->props.find("@args");
            std::vector<Value> all;
            if (pre && pre->value.is_array()) all = pre->value.as_object()->elements;
            for (Value& v : args) all.push_back(std::move(v));
            return cy.call_function(target ? target->value : Value(), thisV ? thisV->value : Value(), all);
        };
        Prop& t = bound->props.put("@target"); t.value = self; t.hidden = true;
        Prop& th = bound->props.put("@this"); th.value = arg(a, 0); th.hidden = true;
        Prop& pa = bound->props.put("@args");
        pa.value = Value::object(cx.new_array(std::vector<Value>(a.size() > 1 ? a.begin() + 1 : a.end(), a.end())));
        pa.hidden = true;
        return Value::object(bound);
    });

    // --- Error ------------------------------------------------------------------------
    method(errorProto, "toString", [](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(cx.to_string(self)); });
    for (const char* name : {"Error", "TypeError", "RangeError", "ReferenceError", "SyntaxError", "PermissionError",
                             "NetworkError", "CancelError"}) {
        ObjectRef proto = std::string_view(name) == "Error" ? errorProto : new_object_with(errorProto);
        if (proto != errorProto) {
            Prop& link = errorProto->props.put(std::string("@") + name);
            link.value = Value::object(proto);
            link.hidden = true;
        }
        const std::string errorName = name;
        ObjectRef ctor = new_native(name, [errorName](Context& cx, const Value&, std::vector<Value>& a) {
            const Value msg = arg(a, 0);
            return cx.make_error(errorName, msg.is_undefined() ? std::string() : cx.to_string(msg));
        }, true);
        Prop& p = ctor->props.put("prototype");
        p.value = Value::object(proto);
        p.hidden = true;
        set_global(name, Value::object(ctor));
    }

    // --- console ------------------------------------------------------------------------
    {
        ObjectRef console = new_object();
        auto writer = [](int level) {
            return [level](Context& cx, const Value&, std::vector<Value>& a) {
                std::string line;
                for (usize i = 0; i < a.size(); ++i) {
                    if (i) line += ' ';
                    const Value& v = a[i];
                    if (v.is_string()) line += v.as_string();
                    else if (v.is_object() && (v.as_object()->kind == ObjKind::Plain || v.as_object()->kind == ObjKind::Array)) {
                        std::string j = cx.json_stringify(v);
                        line += j.empty() ? cx.to_string(v) : j;
                    } else line += cx.to_string(v);
                    if (line.size() > 4096) { line.resize(4096); line += "..."; break; }
                }
                if (cx.onConsole) cx.onConsole(level, line);
                return Value::undefined();
            };
        };
        method(console, "log", writer(0));
        method(console, "debug", writer(0));
        method(console, "info", writer(1));
        method(console, "warn", writer(2));
        method(console, "error", writer(3));
        set_global("console", Value::object(console));
    }

    // --- valores e funções globais ---------------------------------------------------------
    set_global("undefined", Value::undefined());
    set_global("NaN", Value::number(std::nan("")));
    set_global("Infinity", Value::number(INFINITY));
    set_global("isNaN", Value::object(new_native("isNaN", [](Context& cx, const Value&, std::vector<Value>& a) {
        return Value::boolean(std::isnan(cx.to_number(arg(a, 0))));
    })));
    set_global("isFinite", Value::object(new_native("isFinite", [](Context& cx, const Value&, std::vector<Value>& a) {
        return Value::boolean(std::isfinite(cx.to_number(arg(a, 0))));
    })));
    auto parse_int = [](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string s = trim(cx.to_string(arg(a, 0)), true, true);
        int radix = arg(a, 1).is_undefined() ? 0 : static_cast<int>(cx.to_number(arg(a, 1)));
        usize i = 0;
        bool neg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
        if ((radix == 0 || radix == 16) && i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { i += 2; radix = 16; }
        if (radix == 0) radix = 10;
        if (radix < 2 || radix > 36) return Value::number(std::nan(""));
        f64 v = 0;
        bool any = false;
        for (; i < s.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(s[i]);
            int d = std::isdigit(ch) ? ch - '0' : std::isalpha(ch) ? std::tolower(ch) - 'a' + 10 : 99;
            if (d >= radix) break;
            v = v * radix + d;
            any = true;
        }
        return Value::number(any ? (neg ? -v : v) : std::nan(""));
    };
    auto parse_float = [](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string s = trim(cx.to_string(arg(a, 0)), true, false);
        usize i = 0;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (s.compare(i, 8, "Infinity") == 0) return Value::number(s[0] == '-' ? -INFINITY : INFINITY);
        const usize digitsStart = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '.') { ++i; while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i; }
        if (i == digitsStart || (i == digitsStart + 1 && s[digitsStart] == '.')) return Value::number(std::nan(""));
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            usize j = i + 1;
            if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
            if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
                while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
                i = j;
            }
        }
        return Value::number(std::strtod(s.substr(0, i).c_str(), nullptr));
    };
    set_global("parseInt", Value::object(new_native("parseInt", parse_int)));
    set_global("parseFloat", Value::object(new_native("parseFloat", parse_float)));
    // Endereços para o fetch: encodeURIComponent/decodeURIComponent (UTF-8).
    set_global("encodeURIComponent", Value::object(new_native("encodeURIComponent", [](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string s = cx.to_string(arg(a, 0));
        std::string out;
        for (const char ch : s) {
            const unsigned char u = static_cast<unsigned char>(ch);
            if (std::isalnum(u) || std::strchr("-_.!~*'()", ch)) out += ch;
            else {
                char b[4];
                std::snprintf(b, sizeof(b), "%%%02X", u);
                out += b;
            }
        }
        return cx.str(std::move(out));
    })));
    set_global("decodeURIComponent", Value::object(new_native("decodeURIComponent", [](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string s = cx.to_string(arg(a, 0));
        std::string out;
        for (usize i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else out += s[i];
        }
        return cx.str(std::move(out));
    })));

    // --- Math -----------------------------------------------------------------------------
    {
        ObjectRef math = new_object();
        constant(math, "PI", Value::number(3.14159265358979323846));
        constant(math, "E", Value::number(2.71828182845904523536));
        constant(math, "LN2", Value::number(0.69314718055994530942));
        constant(math, "LN10", Value::number(2.30258509299404568402));
        constant(math, "SQRT2", Value::number(1.41421356237309504880));
        using F1 = f64 (*)(f64);
        const std::pair<const char*, F1> unary[] = {
            {"abs", [](f64 x) { return std::fabs(x); }}, {"floor", [](f64 x) { return std::floor(x); }},
            {"ceil", [](f64 x) { return std::ceil(x); }}, {"trunc", [](f64 x) { return std::trunc(x); }},
            {"round", [](f64 x) { return std::floor(x + 0.5); }},
            {"sign", [](f64 x) { return std::isnan(x) ? x : x > 0 ? 1.0 : x < 0 ? -1.0 : x; }},
            {"sqrt", [](f64 x) { return std::sqrt(x); }}, {"cbrt", [](f64 x) { return std::cbrt(x); }},
            {"exp", [](f64 x) { return std::exp(x); }}, {"log", [](f64 x) { return std::log(x); }},
            {"log2", [](f64 x) { return std::log2(x); }}, {"log10", [](f64 x) { return std::log10(x); }},
            {"sin", [](f64 x) { return std::sin(x); }}, {"cos", [](f64 x) { return std::cos(x); }},
            {"tan", [](f64 x) { return std::tan(x); }}, {"asin", [](f64 x) { return std::asin(x); }},
            {"acos", [](f64 x) { return std::acos(x); }}, {"atan", [](f64 x) { return std::atan(x); }},
            {"sinh", [](f64 x) { return std::sinh(x); }}, {"cosh", [](f64 x) { return std::cosh(x); }},
            {"tanh", [](f64 x) { return std::tanh(x); }}, {"fround", [](f64 x) { return static_cast<f64>(static_cast<f32>(x)); }},
        };
        for (const auto& [name, fn] : unary) {
            const F1 f = fn;
            method(math, name, [f](Context& cx, const Value&, std::vector<Value>& a) { return Value::number(f(cx.to_number(arg(a, 0)))); });
        }
        method(math, "atan2", [](Context& cx, const Value&, std::vector<Value>& a) { return Value::number(std::atan2(cx.to_number(arg(a, 0)), cx.to_number(arg(a, 1)))); });
        method(math, "pow", [](Context& cx, const Value&, std::vector<Value>& a) { return Value::number(std::pow(cx.to_number(arg(a, 0)), cx.to_number(arg(a, 1)))); });
        method(math, "hypot", [](Context& cx, const Value&, std::vector<Value>& a) {
            f64 s = 0;
            for (const Value& v : a) { const f64 x = cx.to_number(v); s += x * x; }
            return Value::number(std::sqrt(s));
        });
        method(math, "min", [](Context& cx, const Value&, std::vector<Value>& a) {
            f64 m = INFINITY;
            for (const Value& v : a) { const f64 x = cx.to_number(v); if (std::isnan(x)) return Value::number(x); m = std::min(m, x); }
            return Value::number(m);
        });
        method(math, "max", [](Context& cx, const Value&, std::vector<Value>& a) {
            f64 m = -INFINITY;
            for (const Value& v : a) { const f64 x = cx.to_number(v); if (std::isnan(x)) return Value::number(x); m = std::max(m, x); }
            return Value::number(m);
        });
        method(math, "random", [](Context& cx, const Value&, std::vector<Value>&) {
            u64 x = cx.rng_;
            x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
            cx.rng_ = x;
            return Value::number(static_cast<f64>((x * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0);
        });
        set_global("Math", Value::object(math));
    }

    // --- JSON -------------------------------------------------------------------------------
    {
        ObjectRef j = new_object();
        method(j, "parse", [](Context& cx, const Value&, std::vector<Value>& a) { return cx.json_parse(cx.to_string(arg(a, 0))); });
        method(j, "stringify", [](Context& cx, const Value&, std::vector<Value>& a) {
            std::string indent;
            const Value sp = arg(a, 2);
            if (sp.is_number()) indent.assign(static_cast<usize>(std::clamp(sp.as_number(), 0.0, 10.0)), ' ');
            else if (sp.is_string()) indent = sp.as_string().substr(0, 10);
            const Value v = arg(a, 0);
            if (v.is_undefined() || v.is_function()) return Value::undefined();
            return cx.str(cx.json_stringify(v, indent));
        });
        set_global("JSON", Value::object(j));
    }

    // --- Object -----------------------------------------------------------------------------
    {
        ObjectRef obj = new_native("Object", [](Context& cx, const Value&, std::vector<Value>& a) {
            const Value v = arg(a, 0);
            return v.is_object() ? v : Value::object(cx.new_object());
        }, true);
        auto own_entries = [](Context& cx, const Value& v) {
            std::vector<std::pair<std::string, Value>> out;
            if (v.is_array()) {
                const auto& el = v.as_object()->elements;
                for (usize i = 0; i < el.size(); ++i) out.emplace_back(std::to_string(i), el[i]);
            } else if (v.is_string()) {
                const std::string& s = v.as_string();
                usize i = 0;
                for (usize k = 0; k < s.size(); k += cp_width(static_cast<unsigned char>(s[k])), ++i)
                    out.emplace_back(std::to_string(i), cx.str(s.substr(k, cp_width(static_cast<unsigned char>(s[k])))));
                return out;
            }
            if (v.is_object()) {
                for (const auto& [key, p] : v.as_object()->props.entries()) {
                    if (p.hidden) continue;
                    if (p.getter) { std::vector<Value> none; out.emplace_back(key, p.getter(cx, v, none)); }
                    else out.emplace_back(key, p.value);
                }
            }
            return out;
        };
        method(obj, "keys", [own_entries](Context& cx, const Value&, std::vector<Value>& a) {
            std::vector<Value> out;
            for (auto& [k, v] : own_entries(cx, arg(a, 0))) out.push_back(cx.str(k));
            return Value::object(cx.new_array(std::move(out)));
        });
        method(obj, "values", [own_entries](Context& cx, const Value&, std::vector<Value>& a) {
            std::vector<Value> out;
            for (auto& [k, v] : own_entries(cx, arg(a, 0))) out.push_back(v);
            return Value::object(cx.new_array(std::move(out)));
        });
        method(obj, "entries", [own_entries](Context& cx, const Value&, std::vector<Value>& a) {
            std::vector<Value> out;
            for (auto& [k, v] : own_entries(cx, arg(a, 0))) out.push_back(Value::object(cx.new_array({cx.str(k), v})));
            return Value::object(cx.new_array(std::move(out)));
        });
        method(obj, "assign", [own_entries](Context& cx, const Value&, std::vector<Value>& a) {
            const Value target = arg(a, 0);
            if (!target.is_object()) cx.throw_error("TypeError", "Object.assign precisa de um objeto");
            for (usize i = 1; i < a.size(); ++i)
                for (auto& [k, v] : own_entries(cx, a[i])) cx.set(target, k, v);
            return target;
        });
        method(obj, "fromEntries", [](Context& cx, const Value&, std::vector<Value>& a) {
            ObjectRef out = cx.new_object();
            const Value src = arg(a, 0);
            if (!src.is_array()) cx.throw_error("TypeError", "Object.fromEntries precisa de uma lista de pares");
            for (const Value& pair : src.as_object()->elements) {
                if (!pair.is_array()) continue;
                const auto& el = pair.as_object()->elements;
                cx.set(Value::object(out), el.empty() ? std::string("undefined") : cx.to_string(el[0]), el.size() > 1 ? el[1] : Value());
            }
            return Value::object(out);
        });
        method(obj, "freeze", [](Context&, const Value&, std::vector<Value>& a) {
            if (arg(a, 0).is_object()) arg(a, 0).as_object()->frozen = true;
            return arg(a, 0);
        });
        method(obj, "isFrozen", [](Context&, const Value&, std::vector<Value>& a) {
            return Value::boolean(!arg(a, 0).is_object() || arg(a, 0).as_object()->frozen);
        });
        method(obj, "hasOwn", [](Context& cx, const Value&, std::vector<Value>& a) {
            if (!arg(a, 0).is_object()) return Value::boolean(false);
            const Prop* p = arg(a, 0).as_object()->props.find(cx.to_string(arg(a, 1)));
            return Value::boolean(p && !p->hidden);
        });
        Prop& p = obj->props.put("prototype");
        p.value = Value::object(objectProto);
        p.hidden = true;
        set_global("Object", Value::object(obj));
    }

    // --- Array ------------------------------------------------------------------------------
    {
        ObjectRef arr = new_native("Array", [](Context& cx, const Value&, std::vector<Value>& a) {
            if (a.size() == 1 && a[0].is_number()) {
                const f64 n = a[0].as_number();
                if (!(n >= 0) || n > cx.limits().maxArrayLength || std::floor(n) != n) cx.throw_error("RangeError", "tamanho de lista invalido");
                return Value::object(cx.new_array(std::vector<Value>(static_cast<usize>(n))));
            }
            return Value::object(cx.new_array(a));
        }, true);
        method(arr, "isArray", [](Context&, const Value&, std::vector<Value>& a) { return Value::boolean(arg(a, 0).is_array()); });
        method(arr, "of", [](Context& cx, const Value&, std::vector<Value>& a) { return Value::object(cx.new_array(a)); });
        method(arr, "from", [](Context& cx, const Value&, std::vector<Value>& a) {
            const Value src = arg(a, 0);
            const Value map = arg(a, 1);
            std::vector<Value> items;
            if (src.is_array()) items = src.as_object()->elements;
            else if (src.is_string()) {
                const std::string& s = src.as_string();
                for (usize k = 0; k < s.size(); k += cp_width(static_cast<unsigned char>(s[k])))
                    items.push_back(cx.str(s.substr(k, cp_width(static_cast<unsigned char>(s[k])))));
            } else if (src.is_object()) {
                const f64 n = cx.to_number(cx.get(src, "length"));
                if (n > 0) {
                    if (n > cx.limits().maxArrayLength) cx.throw_error("RangeError", "lista grande demais");
                    for (usize i = 0; i < static_cast<usize>(n); ++i) items.push_back(cx.get(src, std::to_string(i)));
                }
            }
            if (map.is_function())
                for (usize i = 0; i < items.size(); ++i) items[i] = callback(cx, map, {items[i], Value::number(static_cast<f64>(i))});
            return Value::object(cx.new_array(std::move(items)));
        });
        Prop& p = arr->props.put("prototype");
        p.value = Value::object(arrayProto);
        p.hidden = true;
        set_global("Array", Value::object(arr));
    }
    const ObjectRef& ap = arrayProto;
    method(ap, "push", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef& o = self_array(cx, self, "push");
        if (o->frozen) cx.throw_error("TypeError", "lista congelada");
        if (o->elements.size() + a.size() > cx.limits().maxArrayLength) cx.throw_error("RangeError", "lista grande demais");
        o->grow(static_cast<i64>(a.size() * sizeof(Value)));
        for (Value& v : a) o->elements.push_back(std::move(v));
        return Value::number(static_cast<f64>(o->elements.size()));
    });
    method(ap, "pop", [](Context& cx, const Value& self, std::vector<Value>&) {
        const ObjectRef& o = self_array(cx, self, "pop");
        if (o->elements.empty()) return Value::undefined();
        Value v = std::move(o->elements.back());
        o->elements.pop_back();
        return v;
    });
    method(ap, "shift", [](Context& cx, const Value& self, std::vector<Value>&) {
        const ObjectRef& o = self_array(cx, self, "shift");
        if (o->elements.empty()) return Value::undefined();
        Value v = std::move(o->elements.front());
        o->elements.erase(o->elements.begin());
        return v;
    });
    method(ap, "unshift", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef& o = self_array(cx, self, "unshift");
        if (o->elements.size() + a.size() > cx.limits().maxArrayLength) cx.throw_error("RangeError", "lista grande demais");
        o->grow(static_cast<i64>(a.size() * sizeof(Value)));
        o->elements.insert(o->elements.begin(), a.begin(), a.end());
        return Value::number(static_cast<f64>(o->elements.size()));
    });
    method(ap, "slice", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const auto& el = self_array(cx, self, "slice")->elements;
        const usize s = rel_index(cx, arg(a, 0), el.size(), 0);
        const usize e = rel_index(cx, arg(a, 1), el.size(), el.size());
        return Value::object(cx.new_array(s < e ? std::vector<Value>(el.begin() + static_cast<std::ptrdiff_t>(s), el.begin() + static_cast<std::ptrdiff_t>(e)) : std::vector<Value>{}));
    });
    method(ap, "splice", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef& o = self_array(cx, self, "splice");
        auto& el = o->elements;
        const usize s = rel_index(cx, arg(a, 0), el.size(), 0);
        usize count = a.size() < 2 ? el.size() - s : static_cast<usize>(std::clamp(cx.to_number(a[1]), 0.0, static_cast<f64>(el.size() - s)));
        std::vector<Value> removed(el.begin() + static_cast<std::ptrdiff_t>(s), el.begin() + static_cast<std::ptrdiff_t>(s + count));
        el.erase(el.begin() + static_cast<std::ptrdiff_t>(s), el.begin() + static_cast<std::ptrdiff_t>(s + count));
        if (a.size() > 2) {
            if (el.size() + a.size() - 2 > cx.limits().maxArrayLength) cx.throw_error("RangeError", "lista grande demais");
            o->grow(static_cast<i64>((a.size() - 2) * sizeof(Value)));
            el.insert(el.begin() + static_cast<std::ptrdiff_t>(s), a.begin() + 2, a.end());
        }
        return Value::object(cx.new_array(std::move(removed)));
    });
    method(ap, "concat", [](Context& cx, const Value& self, std::vector<Value>& a) {
        std::vector<Value> out = self_array(cx, self, "concat")->elements;
        for (const Value& v : a) {
            if (v.is_array()) for (const Value& e : v.as_object()->elements) out.push_back(e);
            else out.push_back(v);
        }
        return Value::object(cx.new_array(std::move(out)));
    });
    method(ap, "join", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const auto& el = self_array(cx, self, "join")->elements;
        const std::string sep = arg(a, 0).is_undefined() ? std::string(",") : cx.to_string(arg(a, 0));
        std::string out;
        for (usize i = 0; i < el.size(); ++i) {
            if (i) out += sep;
            if (!el[i].is_nullish()) out += cx.to_string(el[i]);
            if (out.size() > cx.limits().maxStringBytes) cx.throw_error("RangeError", "texto grande demais");
        }
        return cx.str(std::move(out));
    });
    method(ap, "reverse", [](Context& cx, const Value& self, std::vector<Value>&) {
        auto& el = self_array(cx, self, "reverse")->elements;
        std::reverse(el.begin(), el.end());
        return self;
    });
    method(ap, "indexOf", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const auto& el = self_array(cx, self, "indexOf")->elements;
        for (usize i = rel_index(cx, arg(a, 1), el.size(), 0); i < el.size(); ++i)
            if (strict_eq(el[i], arg(a, 0))) return Value::number(static_cast<f64>(i));
        return Value::number(-1);
    });
    method(ap, "lastIndexOf", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const auto& el = self_array(cx, self, "lastIndexOf")->elements;
        for (usize i = el.size(); i > 0; --i)
            if (strict_eq(el[i - 1], arg(a, 0))) return Value::number(static_cast<f64>(i - 1));
        return Value::number(-1);
    });
    method(ap, "includes", [](Context& cx, const Value& self, std::vector<Value>& a) {
        for (const Value& v : self_array(cx, self, "includes")->elements) if (same_value_zero(v, arg(a, 0))) return Value::boolean(true);
        return Value::boolean(false);
    });
    method(ap, "at", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const auto& el = self_array(cx, self, "at")->elements;
        f64 n = std::trunc(cx.to_number(arg(a, 0)));
        if (std::isnan(n)) n = 0;
        if (n < 0) n += static_cast<f64>(el.size());
        return n >= 0 && n < static_cast<f64>(el.size()) ? el[static_cast<usize>(n)] : Value::undefined();
    });
    // Métodos com função: a lista pode mudar durante o laço; lê o tamanho a cada volta.
    method(ap, "forEach", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef o = self_array(cx, self, "forEach");
        need_function(cx, arg(a, 0), "forEach");
        for (usize i = 0; i < o->elements.size(); ++i) (void)callback(cx, a[0], {o->elements[i], Value::number(static_cast<f64>(i)), self});
        return Value::undefined();
    });
    method(ap, "map", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef o = self_array(cx, self, "map");
        need_function(cx, arg(a, 0), "map");
        std::vector<Value> out;
        out.reserve(o->elements.size());
        for (usize i = 0; i < o->elements.size(); ++i) out.push_back(callback(cx, a[0], {o->elements[i], Value::number(static_cast<f64>(i)), self}));
        return Value::object(cx.new_array(std::move(out)));
    });
    method(ap, "filter", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef o = self_array(cx, self, "filter");
        need_function(cx, arg(a, 0), "filter");
        std::vector<Value> out;
        for (usize i = 0; i < o->elements.size(); ++i) {
            const Value v = o->elements[i];
            if (Context::truthy(callback(cx, a[0], {v, Value::number(static_cast<f64>(i)), self}))) out.push_back(v);
        }
        return Value::object(cx.new_array(std::move(out)));
    });
    auto finder = [](bool wantIndex, bool fromEnd) {
        return [wantIndex, fromEnd](Context& cx, const Value& self, std::vector<Value>& a) {
            const ObjectRef o = self_array(cx, self, "find");
            need_function(cx, arg(a, 0), "find");
            const usize n = o->elements.size();
            for (usize k = 0; k < n && k < o->elements.size(); ++k) {
                const usize i = fromEnd ? n - 1 - k : k;
                if (i >= o->elements.size()) continue;
                const Value v = o->elements[i];
                if (Context::truthy(callback(cx, a[0], {v, Value::number(static_cast<f64>(i)), self})))
                    return wantIndex ? Value::number(static_cast<f64>(i)) : v;
            }
            return wantIndex ? Value::number(-1) : Value::undefined();
        };
    };
    method(ap, "find", finder(false, false));
    method(ap, "findIndex", finder(true, false));
    method(ap, "findLast", finder(false, true));
    method(ap, "findLastIndex", finder(true, true));
    method(ap, "some", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef o = self_array(cx, self, "some");
        need_function(cx, arg(a, 0), "some");
        for (usize i = 0; i < o->elements.size(); ++i)
            if (Context::truthy(callback(cx, a[0], {o->elements[i], Value::number(static_cast<f64>(i)), self}))) return Value::boolean(true);
        return Value::boolean(false);
    });
    method(ap, "every", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef o = self_array(cx, self, "every");
        need_function(cx, arg(a, 0), "every");
        for (usize i = 0; i < o->elements.size(); ++i)
            if (!Context::truthy(callback(cx, a[0], {o->elements[i], Value::number(static_cast<f64>(i)), self}))) return Value::boolean(false);
        return Value::boolean(true);
    });
    auto reducer = [](bool right) {
        return [right](Context& cx, const Value& self, std::vector<Value>& a) {
            const ObjectRef o = self_array(cx, self, "reduce");
            need_function(cx, arg(a, 0), "reduce");
            const usize n = o->elements.size();
            usize k = 0;
            Value acc;
            if (a.size() > 1) acc = a[1];
            else {
                if (n == 0) cx.throw_error("TypeError", "reduce de lista vazia sem valor inicial");
                acc = o->elements[right ? n - 1 : 0];
                k = 1;
            }
            for (; k < n; ++k) {
                const usize i = right ? n - 1 - k : k;
                if (i >= o->elements.size()) continue;
                acc = callback(cx, a[0], {acc, o->elements[i], Value::number(static_cast<f64>(i)), self});
            }
            return acc;
        };
    };
    method(ap, "reduce", reducer(false));
    method(ap, "reduceRight", reducer(true));
    method(ap, "sort", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef& o = self_array(cx, self, "sort");
        const Value cmp = arg(a, 0);
        std::vector<Value> items = o->elements;
        // undefined vai para o fim, como no JavaScript.
        std::vector<Value> undef;
        std::erase_if(items, [&](const Value& v) { if (v.is_undefined()) { undef.push_back(v); return true; } return false; });
        if (cmp.is_function()) {
            merge_sort(items, [&](const Value& x, const Value& y) {
                const f64 r = cx.to_number(callback(cx, cmp, {x, y}));
                return r < 0;
            });
        } else {
            merge_sort(items, [&](const Value& x, const Value& y) { return cx.to_string(x) < cx.to_string(y); });
        }
        for (Value& v : undef) items.push_back(std::move(v));
        o->elements = std::move(items);
        return self;
    });
    method(ap, "flat", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const int depth = arg(a, 0).is_undefined() ? 1 : static_cast<int>(cx.to_number(arg(a, 0)));
        std::vector<Value> out;
        std::function<void(const std::vector<Value>&, int)> go = [&](const std::vector<Value>& el, int d) {
            for (const Value& v : el) {
                if (v.is_array() && d > 0) go(v.as_object()->elements, d - 1);
                else out.push_back(v);
                if (out.size() > cx.limits().maxArrayLength) cx.throw_error("RangeError", "lista grande demais");
            }
        };
        go(self_array(cx, self, "flat")->elements, std::clamp(depth, 0, 16));
        return Value::object(cx.new_array(std::move(out)));
    });
    method(ap, "flatMap", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const ObjectRef o = self_array(cx, self, "flatMap");
        need_function(cx, arg(a, 0), "flatMap");
        std::vector<Value> out;
        for (usize i = 0; i < o->elements.size(); ++i) {
            const Value r = callback(cx, a[0], {o->elements[i], Value::number(static_cast<f64>(i)), self});
            if (r.is_array()) for (const Value& v : r.as_object()->elements) out.push_back(v);
            else out.push_back(r);
        }
        return Value::object(cx.new_array(std::move(out)));
    });
    method(ap, "fill", [](Context& cx, const Value& self, std::vector<Value>& a) {
        auto& el = self_array(cx, self, "fill")->elements;
        const usize s = rel_index(cx, arg(a, 1), el.size(), 0);
        const usize e = rel_index(cx, arg(a, 2), el.size(), el.size());
        for (usize i = s; i < e; ++i) el[i] = arg(a, 0);
        return self;
    });
    method(ap, "toString", [](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(cx.to_string(self)); });

    // --- String -----------------------------------------------------------------------------
    {
        ObjectRef s = new_native("String", [](Context& cx, const Value&, std::vector<Value>& a) {
            return a.empty() ? cx.str("") : cx.str(cx.to_string(a[0]));
        });
        method(s, "fromCharCode", [](Context& cx, const Value&, std::vector<Value>& a) {
            std::string out;
            for (const Value& v : a) encode(out, static_cast<u32>(to_int32_like(cx.to_number(v))) & 0xFFFFu);
            return cx.str(std::move(out));
        });
        method(s, "fromCodePoint", [](Context& cx, const Value&, std::vector<Value>& a) {
            std::string out;
            for (const Value& v : a) {
                const f64 n = cx.to_number(v);
                if (!(n >= 0 && n <= 0x10FFFF) || std::floor(n) != n) cx.throw_error("RangeError", "codigo de caractere invalido");
                encode(out, static_cast<u32>(n));
            }
            return cx.str(std::move(out));
        });
        set_global("String", Value::object(s));
    }
    const ObjectRef& sp = stringProto;
    method(sp, "toUpperCase", [](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(map_case(self_text(cx, self, "toUpperCase"), true)); });
    method(sp, "toLowerCase", [](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(map_case(self_text(cx, self, "toLowerCase"), false)); });
    method(sp, "trim", [](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(trim(self_text(cx, self, "trim"), true, true)); });
    method(sp, "trimStart", [](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(trim(self_text(cx, self, "trimStart"), true, false)); });
    method(sp, "trimEnd", [](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(trim(self_text(cx, self, "trimEnd"), false, true)); });
    method(sp, "includes", [](Context& cx, const Value& self, std::vector<Value>& a) {
        return Value::boolean(self_text(cx, self, "includes").find(cx.to_string(arg(a, 0))) != std::string::npos);
    });
    method(sp, "startsWith", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "startsWith");
        const auto off = offsets(s);
        const usize from = off[rel_index(cx, arg(a, 1), off.size() - 1, 0)];
        return Value::boolean(s.compare(from, cx.to_string(arg(a, 0)).size(), cx.to_string(arg(a, 0))) == 0);
    });
    method(sp, "endsWith", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "endsWith");
        const std::string t = cx.to_string(arg(a, 0));
        return Value::boolean(s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0);
    });
    auto index_of = [](bool last) {
        return [last](Context& cx, const Value& self, std::vector<Value>& a) {
            const std::string& s = self_text(cx, self, "indexOf");
            const std::string t = cx.to_string(arg(a, 0));
            const usize at = last ? s.rfind(t) : s.find(t);
            if (at == std::string::npos) return Value::number(-1);
            usize cp = 0;
            for (usize i = 0; i < at; i += cp_width(static_cast<unsigned char>(s[i]))) ++cp;
            return Value::number(static_cast<f64>(cp));
        };
    };
    method(sp, "indexOf", index_of(false));
    method(sp, "lastIndexOf", index_of(true));
    method(sp, "slice", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "slice");
        const auto off = offsets(s);
        const usize n = off.size() - 1;
        const usize b = rel_index(cx, arg(a, 0), n, 0), e = rel_index(cx, arg(a, 1), n, n);
        return cx.str(b < e ? s.substr(off[b], off[e] - off[b]) : std::string());
    });
    method(sp, "substring", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "substring");
        const auto off = offsets(s);
        const usize n = off.size() - 1;
        auto clampi = [&](const Value& v, usize def) {
            if (v.is_undefined()) return def;
            const f64 x = cx.to_number(v);
            return std::isnan(x) ? usize{0} : static_cast<usize>(std::clamp(std::trunc(x), 0.0, static_cast<f64>(n)));
        };
        usize b = clampi(arg(a, 0), 0), e = clampi(arg(a, 1), n);
        if (b > e) std::swap(b, e);
        return cx.str(s.substr(off[b], off[e] - off[b]));
    });
    method(sp, "substr", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "substr");
        const auto off = offsets(s);
        const usize n = off.size() - 1;
        const usize b = rel_index(cx, arg(a, 0), n, 0);
        const usize len = arg(a, 1).is_undefined() ? n - b : static_cast<usize>(std::clamp(cx.to_number(arg(a, 1)), 0.0, static_cast<f64>(n - b)));
        return cx.str(s.substr(off[b], off[b + len] - off[b]));
    });
    method(sp, "charAt", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "charAt");
        const auto off = offsets(s);
        const f64 i = arg(a, 0).is_undefined() ? 0 : std::trunc(cx.to_number(arg(a, 0)));
        if (!(i >= 0) || i >= static_cast<f64>(off.size() - 1)) return cx.str("");
        const usize k = static_cast<usize>(i);
        return cx.str(s.substr(off[k], off[k + 1] - off[k]));
    });
    auto code_at = [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "charCodeAt");
        const auto off = offsets(s);
        const f64 i = arg(a, 0).is_undefined() ? 0 : std::trunc(cx.to_number(arg(a, 0)));
        if (!(i >= 0) || i >= static_cast<f64>(off.size() - 1)) return Value::number(std::nan(""));
        return Value::number(static_cast<f64>(decode(s, off[static_cast<usize>(i)])));
    };
    method(sp, "charCodeAt", code_at);
    method(sp, "codePointAt", code_at);
    method(sp, "at", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "at");
        const auto off = offsets(s);
        const f64 n = static_cast<f64>(off.size() - 1);
        f64 i = std::trunc(cx.to_number(arg(a, 0)));
        if (std::isnan(i)) i = 0;
        if (i < 0) i += n;
        if (i < 0 || i >= n) return Value::undefined();
        const usize k = static_cast<usize>(i);
        return cx.str(s.substr(off[k], off[k + 1] - off[k]));
    });
    method(sp, "split", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "split");
        std::vector<Value> out;
        const usize limit = arg(a, 1).is_undefined() ? static_cast<usize>(cx.limits().maxArrayLength)
                                                      : static_cast<usize>(std::clamp(cx.to_number(arg(a, 1)), 0.0, 4294967295.0));
        if (arg(a, 0).is_undefined()) { if (limit) out.push_back(self); return Value::object(cx.new_array(std::move(out))); }
        const std::string sep = cx.to_string(arg(a, 0));
        if (sep.empty()) {
            for (usize i = 0; i < s.size() && out.size() < limit; i += cp_width(static_cast<unsigned char>(s[i])))
                out.push_back(cx.str(s.substr(i, cp_width(static_cast<unsigned char>(s[i])))));
            return Value::object(cx.new_array(std::move(out)));
        }
        usize start = 0;
        while (out.size() < limit) {
            const usize at = s.find(sep, start);
            if (at == std::string::npos) { out.push_back(cx.str(s.substr(start))); break; }
            out.push_back(cx.str(s.substr(start, at - start)));
            start = at + sep.size();
            if (out.size() > cx.limits().maxArrayLength) cx.throw_error("RangeError", "lista grande demais");
        }
        return Value::object(cx.new_array(std::move(out)));
    });
    auto replacer = [](bool all) {
        return [all](Context& cx, const Value& self, std::vector<Value>& a) {
            const std::string& s = self_text(cx, self, all ? "replaceAll" : "replace");
            const std::string pat = cx.to_string(arg(a, 0));
            const Value with = arg(a, 1);
            std::string out;
            usize start = 0;
            for (;;) {
                const usize at = s.find(pat, start);
                if (at == std::string::npos) break;
                out += s.substr(start, at - start);
                if (with.is_function()) out += cx.to_string(callback(cx, with, {cx.str(pat), Value::number(static_cast<f64>(at))}));
                else out += cx.to_string(with);
                if (out.size() > cx.limits().maxStringBytes) cx.throw_error("RangeError", "texto grande demais");
                start = at + pat.size();
                if (pat.empty()) {
                    if (start < s.size()) out += s.substr(start, cp_width(static_cast<unsigned char>(s[start])));
                    start += start < s.size() ? cp_width(static_cast<unsigned char>(s[start])) : 1;
                    if (start > s.size()) return cx.str(std::move(out));
                }
                if (!all) break;
            }
            if (start <= s.size()) out += s.substr(start);
            return cx.str(std::move(out));
        };
    };
    method(sp, "replace", replacer(false));
    method(sp, "replaceAll", replacer(true));
    method(sp, "repeat", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const std::string& s = self_text(cx, self, "repeat");
        const f64 n = cx.to_number(arg(a, 0));
        if (!(n >= 0) || std::isinf(n)) cx.throw_error("RangeError", "repeat precisa de um numero >= 0");
        if (s.size() * static_cast<usize>(n) > cx.limits().maxStringBytes) cx.throw_error("RangeError", "texto grande demais");
        std::string out;
        for (usize i = 0; i < static_cast<usize>(n); ++i) out += s;
        return cx.str(std::move(out));
    });
    auto padder = [](bool start) {
        return [start](Context& cx, const Value& self, std::vector<Value>& a) {
            const std::string& s = self_text(cx, self, "padStart");
            const f64 target = cx.to_number(arg(a, 0));
            const std::string fill = arg(a, 1).is_undefined() ? std::string(" ") : cx.to_string(arg(a, 1));
            const usize len = offsets(s).size() - 1;
            if (!(target > static_cast<f64>(len)) || fill.empty() || target > 1e6) return self;
            const auto fo = offsets(fill);
            const usize fn = fo.size() - 1;
            std::string pad;
            for (usize i = 0; i < static_cast<usize>(target) - len; ++i) {
                const usize k = i % fn;
                pad += fill.substr(fo[k], fo[k + 1] - fo[k]);
            }
            return cx.str(start ? pad + s : s + pad);
        };
    };
    method(sp, "padStart", padder(true));
    method(sp, "padEnd", padder(false));
    method(sp, "concat", [](Context& cx, const Value& self, std::vector<Value>& a) {
        std::string out = self_text(cx, self, "concat");
        for (const Value& v : a) out += cx.to_string(v);
        return cx.str(std::move(out));
    });
    method(sp, "localeCompare", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const int r = map_case(self_text(cx, self, "localeCompare"), false).compare(map_case(cx.to_string(arg(a, 0)), false));
        return Value::number(r < 0 ? -1 : r > 0 ? 1 : 0);
    });
    method(sp, "toString", [](Context&, const Value& self, std::vector<Value>&) { return self; });

    // --- Number / Boolean -----------------------------------------------------------------
    {
        ObjectRef num = new_native("Number", [](Context& cx, const Value&, std::vector<Value>& a) {
            return Value::number(a.empty() ? 0.0 : cx.to_number(a[0]));
        });
        method(num, "isInteger", [](Context&, const Value&, std::vector<Value>& a) {
            return Value::boolean(arg(a, 0).is_number() && std::isfinite(arg(a, 0).as_number()) && std::floor(arg(a, 0).as_number()) == arg(a, 0).as_number());
        });
        method(num, "isFinite", [](Context&, const Value&, std::vector<Value>& a) {
            return Value::boolean(arg(a, 0).is_number() && std::isfinite(arg(a, 0).as_number()));
        });
        method(num, "isNaN", [](Context&, const Value&, std::vector<Value>& a) {
            return Value::boolean(arg(a, 0).is_number() && std::isnan(arg(a, 0).as_number()));
        });
        method(num, "parseFloat", parse_float);
        method(num, "parseInt", parse_int);
        constant(num, "MAX_SAFE_INTEGER", Value::number(9007199254740991.0));
        constant(num, "MIN_SAFE_INTEGER", Value::number(-9007199254740991.0));
        constant(num, "EPSILON", Value::number(2.220446049250313e-16));
        constant(num, "MAX_VALUE", Value::number(1.7976931348623157e308));
        set_global("Number", Value::object(num));
        set_global("Boolean", Value::object(new_native("Boolean", [](Context&, const Value&, std::vector<Value>& a) {
            return Value::boolean(Context::truthy(arg(a, 0)));
        })));
    }
    const ObjectRef& np = numberProto;
    method(np, "toFixed", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const f64 x = cx.to_number(self);
        const int d = static_cast<int>(std::clamp(cx.to_number(arg(a, 0)), 0.0, 100.0));
        if (!std::isfinite(x)) return cx.str(Context::number_text(x));
        char b[160];
        std::snprintf(b, sizeof(b), "%.*f", d, x);
        return cx.str(b);
    });
    method(np, "toPrecision", [](Context& cx, const Value& self, std::vector<Value>& a) {
        const f64 x = cx.to_number(self);
        if (arg(a, 0).is_undefined() || !std::isfinite(x)) return cx.str(Context::number_text(x));
        const int p = static_cast<int>(std::clamp(cx.to_number(arg(a, 0)), 1.0, 100.0));
        char b[160];
        std::snprintf(b, sizeof(b), "%.*g", p, x);
        return cx.str(b);
    });
    method(np, "toString", [](Context& cx, const Value& self, std::vector<Value>& a) {
        if (self.is_bool()) return cx.str(self.as_bool() ? "true" : "false");
        const f64 x = cx.to_number(self);
        const int radix = arg(a, 0).is_undefined() ? 10 : static_cast<int>(cx.to_number(arg(a, 0)));
        if (radix < 2 || radix > 36) cx.throw_error("RangeError", "base de 2 a 36");
        if (radix == 10 || !std::isfinite(x)) return cx.str(Context::number_text(x));
        f64 n = std::trunc(std::fabs(x));
        std::string out;
        do {
            const int d = static_cast<int>(std::fmod(n, radix));
            out += static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
            n = std::floor(n / radix);
        } while (n > 0 && out.size() < 1100);
        if (x < 0) out += '-';
        std::reverse(out.begin(), out.end());
        return cx.str(std::move(out));
    });

    // --- Date (UTC; o relógio é o único dado do sistema que o script lê) ----------------------------
    {
        ObjectRef dateProto = new_object_with(objectProto);
        auto ms_of = [](Context& cx, const Value& self) -> f64 {
            if (!self.is_object() || self.as_object()->hostTag != 0xDA7E) cx.throw_error("TypeError", "metodo de Date");
            return static_cast<f64>(self.as_object()->hostA);
        };
        method(dateProto, "getTime", [ms_of](Context& cx, const Value& self, std::vector<Value>&) { return Value::number(ms_of(cx, self)); });
        method(dateProto, "valueOf", [ms_of](Context& cx, const Value& self, std::vector<Value>&) { return Value::number(ms_of(cx, self)); });
        method(dateProto, "toISOString", [ms_of](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(iso_text(ms_of(cx, self))); });
        method(dateProto, "toJSON", [ms_of](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(iso_text(ms_of(cx, self))); });
        method(dateProto, "toString", [ms_of](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(iso_text(ms_of(cx, self))); });
        auto part = [ms_of](int which) {
            return [ms_of, which](Context& cx, const Value& self, std::vector<Value>&) {
                const i64 t = static_cast<i64>(ms_of(cx, self));
                i64 days = t / 86400000, rem = t % 86400000;
                if (rem < 0) { rem += 86400000; --days; }
                const Civil c = civil_from_days(days);
                switch (which) {
                    case 0: return Value::number(static_cast<f64>(c.y));
                    case 1: return Value::number(static_cast<f64>(c.m - 1));
                    case 2: return Value::number(static_cast<f64>(c.d));
                    case 3: return Value::number(static_cast<f64>(rem / 3600000));
                    case 4: return Value::number(static_cast<f64>(rem / 60000 % 60));
                    case 5: return Value::number(static_cast<f64>(rem / 1000 % 60));
                    case 6: return Value::number(static_cast<f64>(rem % 1000));
                    default: return Value::number(static_cast<f64>(((days % 7) + 11) % 7));   // 1970-01-01 = quinta (4)
                }
            };
        };
        const char* const names[] = {"getUTCFullYear", "getUTCMonth", "getUTCDate", "getUTCHours", "getUTCMinutes", "getUTCSeconds", "getUTCMilliseconds", "getUTCDay"};
        const char* const local[] = {"getFullYear", "getMonth", "getDate", "getHours", "getMinutes", "getSeconds", "getMilliseconds", "getDay"};
        for (int i = 0; i < 8; ++i) { method(dateProto, names[i], part(i)); method(dateProto, local[i], part(i)); }
        add_root(Value::object(dateProto));
        std::weak_ptr<Object> weakProto = dateProto;
        ObjectRef date = new_native("Date", [weakProto](Context& cx, const Value&, std::vector<Value>& a) {
            f64 ms = now_ms();
            if (!a.empty()) {
                if (a.size() >= 2) {
                    const i64 y = static_cast<i64>(cx.to_number(a[0]));
                    const f64 m = cx.to_number(a[1]);
                    const f64 d = a.size() > 2 ? cx.to_number(a[2]) : 1.0;
                    const i64 days = days_from_civil(y, static_cast<u32>(std::clamp(m, 0.0, 11.0)) + 1, static_cast<u32>(std::clamp(d, 1.0, 31.0)));
                    ms = static_cast<f64>(days) * 86400000.0
                       + (a.size() > 3 ? cx.to_number(a[3]) * 3600000.0 : 0) + (a.size() > 4 ? cx.to_number(a[4]) * 60000.0 : 0)
                       + (a.size() > 5 ? cx.to_number(a[5]) * 1000.0 : 0);
                } else if (a[0].is_string()) {
                    // "AAAA-MM-DD" ou "AAAA-MM-DDTHH:MM:SS(.mmm)Z"
                    const std::string& t = a[0].as_string();
                    usize at = 0;
                    auto num = [&](usize width) -> int {
                        int v = 0;
                        usize k = 0;
                        while (at < t.size() && k < width && std::isdigit(static_cast<unsigned char>(t[at]))) { v = v * 10 + (t[at] - '0'); ++at; ++k; }
                        return k ? v : -1;
                    };
                    auto skip = [&](char ch) { if (at < t.size() && t[at] == ch) { ++at; return true; } return false; };
                    const int y = num(4);
                    const int mo = skip('-') ? num(2) : -1;
                    const int d = skip('-') ? num(2) : -1;
                    int h = 0, mi = 0;
                    double sec = 0;
                    if (skip('T') || skip(' ')) {
                        h = std::max(0, num(2));
                        if (skip(':')) mi = std::max(0, num(2));
                        if (skip(':')) {
                            sec = std::max(0, num(2));
                            if (skip('.')) { const usize before = at; const int frac = num(3); if (frac > 0) sec += frac / std::pow(10.0, static_cast<f64>(at - before)); }
                        }
                    }
                    if (y < 0 || mo < 1 || d < 1) ms = std::nan("");
                    else ms = static_cast<f64>(days_from_civil(y, static_cast<u32>(std::clamp(mo, 1, 12)), static_cast<u32>(std::clamp(d, 1, 31)))) * 86400000.0
                            + h * 3600000.0 + mi * 60000.0 + sec * 1000.0;
                } else ms = cx.to_number(a[0]);
            }
            if (!cx.constructing()) return cx.str(iso_text(ms));
            ObjectRef proto = weakProto.lock();
            ObjectRef o = cx.new_object_with(proto);
            o->hostTag = 0xDA7E;
            o->hostA = std::isfinite(ms) ? static_cast<i64>(ms) : 0;
            return Value::object(o);
        }, true);
        method(date, "now", [](Context&, const Value&, std::vector<Value>&) { return Value::number(now_ms()); });
        Prop& p = date->props.put("prototype");
        p.value = Value::object(dateProto);
        p.hidden = true;
        set_global("Date", Value::object(date));
    }
    (void)c;
}

} // namespace aurea::plugins::script
