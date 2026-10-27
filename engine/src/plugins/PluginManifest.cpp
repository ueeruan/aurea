// =============================================================================
//  plugin.json: leitura, conferência e a lista em JSON para as pontes.
//
//  Toda recusa diz QUAL campo e POR QUÊ, em português — é o texto que a pessoa
//  que fez o plugin vê no aparelho (docs/plugins/README.md, "Solução de
//  problemas", lista as mesmas mensagens).
// =============================================================================
#include "PluginInternal.hpp"
#include "aurea/project/Presets.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aurea::plugins {

// =============================================================================
// Utilidades públicas
// =============================================================================
std::string localized(const LocalizedText& text, std::string_view locale) {
    if (text.empty()) return {};
    for (const auto& [lang, value] : text) if (lang == locale) return value;
    for (const char* fallback : {"en", "pt-BR", "*"})
        for (const auto& [lang, value] : text) if (lang == fallback) return value;
    return text.front().second;
}

const char* kind_name(Kind k) noexcept { return k == Kind::Stack ? "stack" : k == Kind::Panel ? "panel" : "shader"; }

const char* param_kind_name(ParamKind k) noexcept {
    switch (k) {
        case ParamKind::Float: return "float";
        case ParamKind::Int: return "int";
        case ParamKind::Bool: return "bool";
        case ParamKind::Enum: return "enum";
        case ParamKind::Color: return "color";
        case ParamKind::Point: return "point";
        case ParamKind::Angle: return "angle";
        case ParamKind::Layer: return "layer";
    }
    return "float";
}

bool valid_plugin_id(std::string_view id) noexcept {
    if (id.size() < 3 || id.size() > 64) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    if (id.front() == '.' || id.back() == '.' || id.find("..") != std::string_view::npos) return false;
    return true;
}

bool parse_version(std::string_view text, u32 (&out)[3]) noexcept {
    u32 part = 0;
    usize digits = 0;
    u32 value = 0;
    for (usize i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : '.';
        if (c >= '0' && c <= '9') {
            value = value * 10 + static_cast<u32>(c - '0');
            if (++digits > 5) return false;
            continue;
        }
        if (c != '.' || digits == 0 || part >= 3) return false;
        out[part++] = value;
        value = 0;
        digits = 0;
    }
    return part == 3;
}

int compare_versions(std::string_view a, std::string_view b) noexcept {
    u32 va[3]{}, vb[3]{};
    if (!parse_version(a, va)) va[0] = va[1] = va[2] = 0;
    if (!parse_version(b, vb)) vb[0] = vb[1] = vb[2] = 0;
    for (int i = 0; i < 3; ++i) if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    return 0;
}

f32 srgb_to_linear(f32 c) noexcept {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

f32 linear_to_srgb(f32 c) noexcept {
    c = std::max(c, 0.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

bool valid_shader_identifier(std::string_view name) noexcept {
    if (name.empty() || name.size() > 32) return false;
    if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    if (name.find("__") != std::string_view::npos) return false;   // reservado no GLSL
    // Prefixos do motor, do GLSL e do ISF.
    for (const char* prefix : {"gl_", "aurea", "Aurea", "AUREA", "isf_", "IMG_", "vv_"})
        if (name.starts_with(prefix)) return false;
    // Palavras reservadas, tipos, funções nativas e os nomes do cabeçalho: um
    // `#define mix ...` quebraria o shader inteiro com uma mensagem estranha.
    static const char* const kReserved[] = {
        "attribute", "const", "uniform", "varying", "buffer", "shared", "coherent", "volatile", "restrict",
        "readonly", "writeonly", "atomic_uint", "layout", "centroid", "flat", "smooth", "noperspective",
        "patch", "sample", "break", "continue", "do", "for", "while", "switch", "case", "default", "if",
        "else", "subroutine", "in", "out", "inout", "float", "double", "int", "void", "bool", "true", "false",
        "invariant", "precise", "discard", "return", "lowp", "mediump", "highp", "precision", "struct",
        "uint", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4", "bvec2",
        "bvec3", "bvec4", "dvec2", "dvec3", "dvec4", "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4",
        "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4", "sampler2D", "sampler3D", "samplerCube",
        "sampler2DShadow", "texture", "texture2D", "textureLod", "textureSize", "texelFetch", "textureGrad",
        "textureOffset", "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh",
        "tanh", "asinh", "acosh", "atanh", "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt", "abs",
        "sign", "floor", "trunc", "round", "roundEven", "ceil", "fract", "mod", "modf", "min", "max",
        "clamp", "mix", "step", "smoothstep", "isnan", "isinf", "length", "distance", "dot", "cross",
        "normalize", "faceforward", "reflect", "refract", "matrixCompMult", "outerProduct", "transpose",
        "determinant", "inverse", "lessThan", "lessThanEqual", "greaterThan", "greaterThanEqual", "equal",
        "notEqual", "any", "all", "not", "dFdx", "dFdy", "fwidth", "fma", "main", "uv", "v_uv", "o_color",
        "source", "time", "RENDERSIZE", "TIME", "TIMEDELTA", "FRAMEINDEX", "DATE", "PASSINDEX",
        "floatBitsToInt", "floatBitsToUint", "intBitsToFloat", "uintBitsToFloat", "packUnorm2x16",
        "unpackUnorm2x16", "bitfieldExtract", "bitCount", "findLSB", "findMSB", "frexp", "ldexp",
    };
    for (const char* r : kReserved) if (name == r) return false;
    return true;
}

namespace {

using json::Value;

// -----------------------------------------------------------------------------
// Leitura
// -----------------------------------------------------------------------------
struct Reader {
    std::string& error;
    bool fail(std::string message) {
        if (error.empty()) error = std::move(message);
        return false;
    }
};

bool read_text(const Value* v, LocalizedText& out, const char* field, usize maxChars, bool required, Reader& r) {
    out.clear();
    if (!v || v->type == Value::Type::Null) {
        if (required) return r.fail(std::string("plugin.json: falta \"") + field + "\"");
        return true;
    }
    if (v->is_string()) {
        if (v->string.empty() && required) return r.fail(std::string("plugin.json: \"") + field + "\" vazio");
        if (v->string.size() > maxChars) return r.fail(std::string("plugin.json: \"") + field + "\" longo demais");
        out.emplace_back("*", v->string);
        return true;
    }
    if (v->is_object()) {
        for (const auto& [lang, text] : v->object) {
            const bool tagOk = lang.size() >= 2 && lang.size() <= 8
                && std::all_of(lang.begin(), lang.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '-'; });
            if (!tagOk) return r.fail(std::string("plugin.json: \"") + field + "\": idioma invalido \"" + lang + "\"");
            if (!text.is_string()) return r.fail(std::string("plugin.json: \"") + field + "\"." + lang + " deve ser texto");
            if (text.string.size() > maxChars) return r.fail(std::string("plugin.json: \"") + field + "\"." + lang + " longo demais");
            out.emplace_back(lang, text.string);
        }
        if (out.empty() && required) return r.fail(std::string("plugin.json: \"") + field + "\" sem nenhum idioma");
        return true;
    }
    return r.fail(std::string("plugin.json: \"") + field + "\" deve ser texto ou um objeto idioma -> texto");
}

} // namespace

bool read_localized(const json::Value* v, LocalizedText& out, const char* field, usize maxChars, bool required, std::string& error) {
    Reader r{error};
    return read_text(v, out, field, maxChars, required, r);
}

namespace {

bool read_number(const Value* v, f32& out) {
    if (!v) return false;
    if (v->is_number() && std::isfinite(v->number)) { out = static_cast<f32>(v->number); return true; }
    if (v->type == Value::Type::Bool) { out = v->boolean ? 1.0f : 0.0f; return true; }
    return false;
}

bool parse_hex_color(const std::string& s, f32 (&rgba)[4]) {
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (usize i = 1; i < s.size(); ++i) if (hex(s[i]) < 0) return false;
    for (int c = 0; c < 4; ++c) {
        const usize at = 1 + static_cast<usize>(c) * 2;
        rgba[c] = at + 1 < s.size() ? static_cast<f32>(hex(s[at]) * 16 + hex(s[at + 1])) / 255.0f : 1.0f;
    }
    return true;
}

bool read_param(const Value& v, u32 index, ParamDecl& p, Reader& r) {
    const std::string where = "parametro " + std::to_string(index + 1);
    if (!v.is_object()) return r.fail("plugin.json: " + where + " deve ser um objeto");
    const Value* id = v.get("id");
    if (!id || !id->is_string()) return r.fail("plugin.json: " + where + ": falta \"id\"");
    p.id = id->string;
    if (!valid_shader_identifier(p.id))
        return r.fail("plugin.json: parametro \"" + p.id + "\": id invalido (letras, digitos e _, ate 32, "
                      "sem palavra reservada do GLSL nem prefixo aurea/gl_)");
    const std::string pw = "parametro \"" + p.id + "\"";
    const Value* type = v.get("type");
    if (!type || !type->is_string()) return r.fail("plugin.json: " + pw + ": falta \"type\"");
    static const std::pair<const char*, ParamKind> kKinds[] = {
        {"float", ParamKind::Float}, {"int", ParamKind::Int}, {"bool", ParamKind::Bool}, {"enum", ParamKind::Enum},
        {"color", ParamKind::Color}, {"point", ParamKind::Point}, {"angle", ParamKind::Angle},
        {"layer", ParamKind::Layer},
    };
    bool known = false;
    for (const auto& [name, kind] : kKinds) if (type->string == name) { p.kind = kind; known = true; }
    if (!known) return r.fail("plugin.json: " + pw + ": tipo desconhecido \"" + type->string
                              + "\" (use float, int, bool, enum, color, point, angle ou layer)");
    if (!read_text(v.get("label"), p.label, "label", 48, false, r)) return false;
    if (p.label.empty()) p.label.emplace_back("*", p.id);
    if (const Value* a = v.get("animatable")) {
        if (a->type != Value::Type::Bool) return r.fail("plugin.json: " + pw + ": \"animatable\" deve ser true ou false");
        p.animatable = a->boolean;
    }
    // 0.3.1: "layer" liga uma câmera, um nulo ou uma luz da composição ao
    // plugin (só "shader3d"; o motor manda o mundo dessa camada). Sem faixa,
    // sem padrão (nenhuma camada) e nunca animável.
    if (p.kind == ParamKind::Layer) {
        p.animatable = false;
        p.min = -1.0f; p.max = 1.0e6f;
        p.def[0] = -1.0f; p.def[1] = p.def[2] = 0.0f; p.def[3] = 1.0f;
        if (const Value* accept = v.get("accept")) {
            if (!accept->is_array()) return r.fail("plugin.json: " + pw + ": \"accept\" deve ser uma lista (camera, null, light, any)");
            for (const Value& a : accept->array) {
                if (!a.is_string()) return r.fail("plugin.json: " + pw + ": \"accept\" deve ter textos");
                if (a.string == "camera") p.accept |= kAcceptCamera;
                else if (a.string == "null") p.accept |= kAcceptNull;
                else if (a.string == "light") p.accept |= kAcceptLight;
                else if (a.string == "any") p.accept = 0;
                else return r.fail("plugin.json: " + pw + ": \"accept\" desconhecido \"" + a.string + "\" (camera, null, light ou any)");
            }
        }
        return true;
    }
    // Faixas padrão por tipo.
    switch (p.kind) {
        case ParamKind::Float: p.min = 0.0f; p.max = 1.0f; break;
        case ParamKind::Int: p.min = 0.0f; p.max = 100.0f; break;
        case ParamKind::Bool: case ParamKind::Enum: case ParamKind::Color: p.min = 0.0f; p.max = 1.0f; break;
        case ParamKind::Point: p.min = -1.0f; p.max = 2.0f; break;
        case ParamKind::Angle: p.min = -360.0f; p.max = 360.0f; break;
        case ParamKind::Layer: break;
    }
    const Value* mn = v.get("min");
    const Value* mx = v.get("max");
    if (mn && !read_number(mn, p.min)) return r.fail("plugin.json: " + pw + ": \"min\" deve ser numero");
    if (mx && !read_number(mx, p.max)) return r.fail("plugin.json: " + pw + ": \"max\" deve ser numero");
    if (!(p.max > p.min) && p.kind != ParamKind::Bool && p.kind != ParamKind::Enum && p.kind != ParamKind::Color)
        return r.fail("plugin.json: " + pw + ": \"max\" deve ser maior que \"min\"");
    if (p.kind == ParamKind::Int) { p.min = std::round(p.min); p.max = std::round(p.max); }
    const Value* def = v.get("default");
    p.def[0] = p.def[1] = p.def[2] = 0.0f;
    p.def[3] = 1.0f;
    switch (p.kind) {
        case ParamKind::Float: case ParamKind::Int: case ParamKind::Angle: {
            f32 d = p.kind == ParamKind::Angle ? 0.0f : p.min;
            if (def && !read_number(def, d)) return r.fail("plugin.json: " + pw + ": \"default\" deve ser numero");
            if (p.kind == ParamKind::Int) d = std::round(d);
            if (d < p.min || d > p.max) return r.fail("plugin.json: " + pw + ": \"default\" fora de [min, max]");
            p.def[0] = d;
            break;
        }
        case ParamKind::Bool: {
            f32 d = 0.0f;
            if (def && !read_number(def, d)) return r.fail("plugin.json: " + pw + ": \"default\" deve ser true ou false");
            p.def[0] = d >= 0.5f ? 1.0f : 0.0f;
            break;
        }
        case ParamKind::Enum: {
            const Value* opts = v.get("options");
            if (!opts || !opts->is_array() || opts->array.empty())
                return r.fail("plugin.json: " + pw + ": enum precisa de \"options\" (lista de textos)");
            if (opts->array.size() > kMaxEnumOptions) return r.fail("plugin.json: " + pw + ": mais de 32 opcoes");
            for (const Value& o : opts->array) {
                LocalizedText t;
                if (!read_text(&o, t, "options", 48, true, r)) return false;
                p.options.push_back(std::move(t));
            }
            f32 d = 0.0f;
            if (def && !read_number(def, d)) return r.fail("plugin.json: " + pw + ": \"default\" deve ser o indice da opcao");
            if (d < 0.0f || d >= static_cast<f32>(p.options.size()))
                return r.fail("plugin.json: " + pw + ": \"default\" fora das opcoes");
            p.def[0] = std::round(d);
            p.min = 0.0f;
            p.max = static_cast<f32>(p.options.size() - 1);
            break;
        }
        case ParamKind::Color: {
            // Declarada em sRGB (o que um seletor de cor mostra); guardada linear.
            f32 rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (def) {
                if (def->is_string()) {
                    if (!parse_hex_color(def->string, rgba))
                        return r.fail("plugin.json: " + pw + ": cor \"default\" deve ser \"#RRGGBB\", \"#RRGGBBAA\" ou [r, g, b(, a)]");
                } else if (def->is_array() && (def->array.size() == 3 || def->array.size() == 4)) {
                    for (usize c = 0; c < def->array.size(); ++c)
                        if (!read_number(&def->array[c], rgba[c]))
                            return r.fail("plugin.json: " + pw + ": cor \"default\" com componente nao numerico");
                } else {
                    return r.fail("plugin.json: " + pw + ": cor \"default\" deve ser \"#RRGGBB\", \"#RRGGBBAA\" ou [r, g, b(, a)]");
                }
            }
            for (int c = 0; c < 3; ++c) p.def[c] = srgb_to_linear(rgba[c]);
            p.def[3] = std::clamp(rgba[3], 0.0f, 1.0f);
            break;
        }
        case ParamKind::Point: {
            p.def[0] = p.def[1] = 0.5f;
            if (def) {
                if (!def->is_array() || def->array.size() != 2 || !read_number(&def->array[0], p.def[0])
                    || !read_number(&def->array[1], p.def[1]))
                    return r.fail("plugin.json: " + pw + ": ponto \"default\" deve ser [x, y] (0..1 = largura/altura da camada)");
            }
            break;
        }
        case ParamKind::Layer: break;
    }
    return true;
}

bool read_passes(const Value& root, Manifest& m, Reader& r) {
    const Value* passes = root.get("passes");
    if (!passes || !passes->is_array() || passes->array.empty())
        return r.fail("plugin.json: plugin \"shader\" precisa de \"passes\" (1 a 8)");
    if (passes->array.size() > kMaxPasses)
        return r.fail("plugin.json: " + std::to_string(passes->array.size()) + " passes (limite " + std::to_string(kMaxPasses) + ")");
    std::vector<std::string> targets;
    for (usize i = 0; i < passes->array.size(); ++i) {
        const Value& v = passes->array[i];
        const std::string where = "passe " + std::to_string(i + 1);
        if (!v.is_object()) return r.fail("plugin.json: " + where + " deve ser um objeto");
        PassDecl pass;
        const Value* shader = v.get("shader");
        if (!shader || !shader->is_string() || shader->string.empty())
            return r.fail("plugin.json: " + where + ": falta \"shader\" (arquivo .frag no pacote)");
        pass.shader = shader->string;
        const bool last = i + 1 == passes->array.size();
        if (const Value* t = v.get("target")) {
            if (!t->is_string() || !valid_shader_identifier(t->string))
                return r.fail("plugin.json: " + where + ": \"target\" deve ser um nome valido de textura");
            if (last) return r.fail("plugin.json: o ultimo passe nao tem \"target\": ele e a saida do efeito");
            if (std::find(targets.begin(), targets.end(), t->string) != targets.end())
                return r.fail("plugin.json: " + where + ": \"target\" repetido \"" + t->string + "\"");
            pass.target = t->string;
        } else if (!last) {
            return r.fail("plugin.json: " + where + ": falta \"target\" (so o ultimo passe escreve a saida)");
        }
        if (const Value* s = v.get("scale")) {
            if (!read_number(s, pass.scale) || pass.scale < 0.125f || pass.scale > 1.0f)
                return r.fail("plugin.json: " + where + ": \"scale\" deve estar entre 0.125 e 1");
        }
        if (const Value* in = v.get("inputs")) {
            if (!in->is_object()) return r.fail("plugin.json: " + where + ": \"inputs\" deve ser um objeto nome -> origem");
            if (in->object.size() > kMaxPassInputs)
                return r.fail("plugin.json: " + where + ": mais de " + std::to_string(kMaxPassInputs) + " entradas");
            for (const auto& [name, from] : in->object) {
                if (!valid_shader_identifier(name))
                    return r.fail("plugin.json: " + where + ": entrada com nome invalido \"" + name + "\"");
                for (const ParamDecl& p : m.params)
                    if (p.id == name) return r.fail("plugin.json: " + where + ": entrada \"" + name + "\" tem o nome de um parametro");
                for (const auto& other : pass.inputs)
                    if (other.first == name) return r.fail("plugin.json: " + where + ": entrada repetida \"" + name + "\"");
                if (!from.is_string()) return r.fail("plugin.json: " + where + ": a origem de \"" + name + "\" deve ser texto");
                if (from.string != "source" && std::find(targets.begin(), targets.end(), from.string) == targets.end())
                    return r.fail("plugin.json: " + where + ": entrada \"" + name + "\" le \"" + from.string
                                  + "\", que nao e \"source\" nem o target de um passe anterior");
                pass.inputs.emplace_back(name, from.string);
            }
        }
        if (!pass.target.empty()) targets.push_back(pass.target);
        m.passes.push_back(std::move(pass));
    }
    if (const Value* margin = root.get("margin")) {
        if (!read_number(margin, m.margin) || m.margin < 0.0f || m.margin > kMaxMargin)
            return r.fail("plugin.json: \"margin\" deve estar entre 0 e 1024 (px da camada)");
    }
    if (const Value* cs = root.get("colorSpace")) {
        if (!cs->is_string() || (cs->string != "linear" && cs->string != "srgb"))
            return r.fail("plugin.json: \"colorSpace\" deve ser \"linear\" ou \"srgb\"");
        m.colorSpace = cs->string == "srgb" ? ColorSpace::Srgb : ColorSpace::Linear;
    }
    return true;
}

bool read_stack(const Value& root, Manifest& m, Reader& r) {
    const Value* stack = root.get("stack");
    if (!stack || !stack->is_array() || stack->array.empty())
        return r.fail("plugin.json: plugin \"stack\" precisa de \"stack\" (lista de efeitos embutidos)");
    if (stack->array.size() > kMaxStackSteps)
        return r.fail("plugin.json: \"stack\" com mais de " + std::to_string(kMaxStackSteps) + " efeitos");
    for (usize i = 0; i < stack->array.size(); ++i) {
        const Value& v = stack->array[i];
        const std::string where = "stack[" + std::to_string(i) + "]";
        if (!v.is_object()) return r.fail("plugin.json: " + where + " deve ser um objeto");
        StackStepDecl step;
        const Value* effect = v.get("effect");
        if (!effect || !effect->is_string() || effect->string.empty())
            return r.fail("plugin.json: " + where + ": falta \"effect\" (chave de um efeito embutido)");
        step.effect = effect->string;
        if (const Value* set = v.get("set")) {
            if (!set->is_object()) return r.fail("plugin.json: " + where + ": \"set\" deve ser um objeto parametro -> valor");
            for (const auto& [param, value] : set->object) {
                StackValue sv;
                sv.param = param;
                auto component = [&](const Value& c) -> bool {
                    if (c.is_string()) { sv.components.push_back(c.string); return true; }
                    f32 n = 0.0f;
                    if (!read_number(&c, n)) return false;
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(n));
                    sv.components.emplace_back(buf);
                    return true;
                };
                if (value.is_array()) {
                    if (value.array.empty() || value.array.size() > 4)
                        return r.fail("plugin.json: " + where + ": \"" + param + "\" com lista de 1 a 4 valores");
                    for (const Value& c : value.array)
                        if (!component(c)) return r.fail("plugin.json: " + where + ": \"" + param + "\" com valor invalido");
                } else if (!component(value)) {
                    return r.fail("plugin.json: " + where + ": \"" + param + "\" deve ser numero, true/false, expressao ou lista");
                }
                step.set.push_back(std::move(sv));
            }
        }
        m.stack.push_back(std::move(step));
    }
    return true;
}

} // namespace

bool parse_manifest(std::string_view text, Manifest& m, std::string& error) {
    m = Manifest{};
    error.clear();
    Reader r{error};
    if (text.size() > kMaxManifestBytes) return r.fail("plugin.json maior que 256 KB");
    // BOM do UTF-8 (editores do Windows): fora.
    if (text.size() >= 3 && static_cast<u8>(text[0]) == 0xEF && static_cast<u8>(text[1]) == 0xBB && static_cast<u8>(text[2]) == 0xBF)
        text.remove_prefix(3);
    Value root;
    std::string jsonError;
    if (!json::parse(text, root, &jsonError))
        return r.fail("plugin.json invalido: " + (jsonError.empty() ? std::string("JSON malformado") : jsonError));
    if (!root.is_object()) return r.fail("plugin.json invalido: a raiz deve ser um objeto");

    const Value* format = root.get("aureaPlugin");
    f32 fmt = 0.0f;
    if (!format || !read_number(format, fmt)) return r.fail("plugin.json: falta \"aureaPlugin\": 1 ou 2 (versao do formato)");
    // 0.3.3 plugins: o formato 2 traz interface, script e permissões.
    if (fmt != static_cast<f32>(kFormatVersion) && fmt != static_cast<f32>(kFormatVersionV2))
        return r.fail("plugin.json: \"aureaPlugin\" " + std::to_string(static_cast<int>(fmt))
                      + " nao suportado (este app le as versoes 1 e 2 do formato)");
    m.format = static_cast<u32>(fmt);

    const Value* id = root.get("id");
    if (!id || !id->is_string()) return r.fail("plugin.json: falta \"id\"");
    m.id = id->string;
    if (!valid_plugin_id(m.id))
        return r.fail("plugin.json: \"id\" invalido \"" + m.id + "\": use 3 a 64 caracteres a-z, 0-9, '.', '_' ou '-' "
                      "(ex.: com.estudio.brilho)");
    const Value* version = root.get("version");
    u32 v3[3]{};
    if (!version || !version->is_string() || !parse_version(version->string, v3))
        return r.fail("plugin.json: \"version\" deve ser \"x.y.z\" (ex.: \"1.0.0\")");
    m.version = version->string;
    LocalizedText author;
    if (!read_text(root.get("author"), author, "author", 128, false, r)) return false;
    m.author = localized(author, "*");
    if (!read_text(root.get("name"), m.name, "name", 64, true, r)) return false;
    if (!read_text(root.get("description"), m.description, "description", 512, false, r)) return false;
    if (const Value* cat = root.get("category")) {
        if (!cat->is_string() || cat->string.size() > 32) return r.fail("plugin.json: \"category\" deve ser texto (ate 32)");
        m.category = cat->string;
    }
    const Value* kind = root.get("kind");
    if (!kind || !kind->is_string()
        || (kind->string != "shader" && kind->string != "shader3d" && kind->string != "stack" && kind->string != "panel"))
        return r.fail("plugin.json: \"kind\" deve ser \"shader\", \"shader3d\", \"stack\" ou \"panel\"");
    if (kind->string == "panel" && m.format < kFormatVersionV2)
        return r.fail("plugin.json: \"kind\": \"panel\" pede \"aureaPlugin\": 2");
    m.kind = kind->string == "stack" ? Kind::Stack : kind->string == "panel" ? Kind::Panel : Kind::Shader;
    m.scene3d = kind->string == "shader3d";

    if (const Value* params = root.get("params")) {
        if (!params->is_array()) return r.fail("plugin.json: \"params\" deve ser uma lista");
        if (params->array.size() > kMaxParams)
            return r.fail("plugin.json: " + std::to_string(params->array.size()) + " parametros (limite "
                          + std::to_string(kMaxParams) + ")");
        for (usize i = 0; i < params->array.size(); ++i) {
            ParamDecl p;
            if (!read_param(params->array[i], static_cast<u32>(i), p, r)) return false;
            for (const ParamDecl& other : m.params)
                if (other.id == p.id) return r.fail("plugin.json: parametro repetido \"" + p.id + "\"");
            m.params.push_back(std::move(p));
        }
    }
    u32 layerParams = 0;
    for (const ParamDecl& p : m.params) if (p.kind == ParamKind::Layer) ++layerParams;
    if (layerParams && !m.scene3d)
        return r.fail("plugin.json: parametro \"layer\" so em plugin \"kind\": \"shader3d\"");
    if (layerParams > kMaxLayerParams)
        return r.fail("plugin.json: " + std::to_string(layerParams) + " parametros \"layer\" (limite "
                      + std::to_string(kMaxLayerParams) + ")");
    if (m.kind == Kind::Panel && !m.params.empty())
        return r.fail("plugin.json: plugin \"panel\" nao tem \"params\" (os controles ficam em \"ui\")");
    if (m.kind == Kind::Shader && !read_passes(root, m, r)) return false;
    if (m.kind == Kind::Stack && !read_stack(root, m, r)) return false;
    // 0.3.3 plugins: interface, script e permissões (formato 2).
    return parse_manifest_v2(root, m, m.format, error);
}

// =============================================================================
// Escrita
// =============================================================================
namespace {

void write_text(json::Writer& w, const LocalizedText& t) {
    if (t.size() == 1 && t.front().first == "*") { w.value(t.front().second); return; }
    w.begin_object();
    for (const auto& [lang, text] : t) w.key(lang).value(text);
    w.end_object();
}

void write_param(json::Writer& w, const ParamDecl& p, bool srgbColorDefaults) {
    w.begin_object();
    w.key("id").value(p.id);
    w.key("type").value(param_kind_name(p.kind));
    w.key("label");
    write_text(w, p.label);
    switch (p.kind) {
        case ParamKind::Color:
            w.key("default").begin_array();
            for (int c = 0; c < 3; ++c) w.value(srgbColorDefaults ? linear_to_srgb(p.def[c]) : p.def[c]);
            w.value(p.def[3]).end_array();
            break;
        case ParamKind::Point:
            w.key("default").begin_array().value(p.def[0]).value(p.def[1]).end_array();
            w.key("min").value(p.min).key("max").value(p.max);
            break;
        case ParamKind::Bool:
            w.key("default").value(p.def[0] >= 0.5f);
            break;
        case ParamKind::Enum:
            w.key("default").value(p.def[0]);
            w.key("options").begin_array();
            for (const LocalizedText& o : p.options) write_text(w, o);
            w.end_array();
            break;
        case ParamKind::Layer:
            if (p.accept) {
                w.key("accept").begin_array();
                if (p.accept & kAcceptCamera) w.value("camera");
                if (p.accept & kAcceptNull) w.value("null");
                if (p.accept & kAcceptLight) w.value("light");
                w.end_array();
            }
            break;
        default:
            w.key("default").value(p.def[0]).key("min").value(p.min).key("max").value(p.max);
            break;
    }
    if (!p.animatable && p.kind != ParamKind::Layer) w.key("animatable").value(false);
    w.end_object();
}

} // namespace

std::string manifest_json(const Manifest& m) {
    json::Writer w;
    w.begin_object();
    w.key("aureaPlugin").value(static_cast<u32>(m.format));
    w.key("id").value(m.id);
    w.key("version").value(m.version);
    if (!m.author.empty()) w.key("author").value(m.author);
    w.key("name");
    write_text(w, m.name);
    if (!m.description.empty()) { w.key("description"); write_text(w, m.description); }
    if (!m.category.empty()) w.key("category").value(m.category);
    w.key("kind").value(m.scene3d ? "shader3d" : kind_name(m.kind));
    w.key("params").begin_array();
    for (const ParamDecl& p : m.params) write_param(w, p, true);
    w.end_array();
    if (m.kind == Kind::Shader) {
        w.key("passes").begin_array();
        for (const PassDecl& p : m.passes) {
            w.begin_object().key("shader").value(p.shader);
            if (!p.target.empty()) w.key("target").value(p.target);
            if (p.scale != 1.0f) w.key("scale").value(p.scale);
            if (!p.inputs.empty()) {
                w.key("inputs").begin_object();
                for (const auto& [name, from] : p.inputs) w.key(name).value(from);
                w.end_object();
            }
            w.end_object();
        }
        w.end_array();
        if (m.margin > 0.0f) w.key("margin").value(m.margin);
        w.key("colorSpace").value(m.colorSpace == ColorSpace::Srgb ? "srgb" : "linear");
    } else {
        w.key("stack").begin_array();
        for (const StackStepDecl& s : m.stack) {
            w.begin_object().key("effect").value(s.effect).key("set").begin_object();
            for (const StackValue& v : s.set) {
                w.key(v.param);
                if (v.components.size() == 1) w.value(v.components[0]);
                else { w.begin_array(); for (const std::string& c : v.components) w.value(c); w.end_array(); }
            }
            w.end_object().end_object();
        }
        w.end_array();
    }
    w.end_object();
    return w.str();
}

std::string to_json(const std::vector<PluginInfo>& plugins) {
    json::Writer w;
    w.begin_array();
    for (const PluginInfo& p : plugins) {
        w.begin_object();
        w.key("id").value(p.id);
        w.key("version").value(p.version);
        w.key("author").value(p.author);
        w.key("kind").value(kind_name(p.kind));
        if (p.scene3d) w.key("scene3d").value(true);
        w.key("category").value(p.category);
        // Nomes e descrições SEMPRE como objeto idioma -> texto ("*" = todos).
        w.key("name").begin_object();
        for (const auto& [lang, text] : p.names) w.key(lang).value(text);
        w.end_object();
        w.key("description").begin_object();
        for (const auto& [lang, text] : p.descriptions) w.key(lang).value(text);
        w.end_object();
        w.key("effectKey").value(p.effectKey);
        w.key("effectTypeId").value(static_cast<i64>(p.effectTypeId));
        w.key("ok").value(p.ok);
        w.key("errorCode").value(p.errorCode);
        w.key("error").value(p.error);
        w.key("directory").value(p.directory);
        w.key("passCount").value(p.passCount);
        w.key("testRenderMs").value(p.testRenderMs);
        w.key("params").begin_array();
        for (const ParamDecl& q : p.params) {
            w.begin_object();
            w.key("id").value(q.id);
            w.key("type").value(param_kind_name(q.kind));
            w.key("label").begin_object();
            for (const auto& [lang, text] : q.label) w.key(lang).value(text);
            w.end_object();
            if (!q.options.empty()) {
                w.key("options").begin_array();
                for (const LocalizedText& o : q.options) {
                    w.begin_object();
                    for (const auto& [lang, text] : o) w.key(lang).value(text);
                    w.end_object();
                }
                w.end_array();
            }
            w.key("animatable").value(q.animatable);
            w.end_object();
        }
        w.end_array();
        write_v2_json(w, p);   // 0.3.3 plugins
        w.end_object();
    }
    w.end_array();
    return w.str();
}

} // namespace aurea::plugins
