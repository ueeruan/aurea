// =============================================================================
//  ISF 2.0 → plugin "shader" (passe único, cores sRGB).
//
//  O ISF descreve o shader num comentário JSON no topo (`/*{ ... }*/`) e usa
//  os nomes do ISF no corpo (IMG_NORM_PIXEL, isf_FragNormCoord, RENDERSIZE,
//  TIME, gl_FragColor). A conversão:
//
//    - lê o JSON: INPUTS viram parâmetros (float, bool, long → enum, color,
//      point2D, event → bool); a imagem de entrada vira `aurea_source`;
//    - recusa o que o motor não reproduz fielmente: vários passes (PASSES com
//      mais de um), buffers persistentes, imagens importadas, áudio, uma
//      segunda imagem de entrada;
//    - reescreve três identificadores no corpo (`main`, `gl_FragColor`,
//      `gl_FragCoord`) e põe antes dele as macros de compatibilidade, com um
//      `#line` que mantém os números de linha do .fs nas mensagens de erro.
//
//  Coordenadas: o ISF tem a origem EMBAIXO à esquerda (como o OpenGL); o motor,
//  em cima. As macros invertem o y nos dois sentidos, então o shader vê
//  exatamente o que veria num host ISF.
// =============================================================================
#include "PluginInternal.hpp"
#include "aurea/project/Presets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace aurea::plugins {

namespace {

using json::Value;

bool is_ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

/// Troca o identificador `from` por `to` (palavra inteira).
void replace_identifier(std::string& text, std::string_view from, std::string_view to) {
    usize pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        const bool before = pos > 0 && is_ident(text[pos - 1]);
        const bool after = pos + from.size() < text.size() && is_ident(text[pos + from.size()]);
        if (before || after) { pos += from.size(); continue; }
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

/// "Bad TV.fs" → "bad-tv" (para o id).
std::string slug(std::string_view stem) {
    std::string out;
    for (const char c : stem) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

bool number_of(const Value* v, f32& out) {
    if (!v) return false;
    if (v->is_number() && std::isfinite(v->number)) { out = static_cast<f32>(v->number); return true; }
    if (v->type == Value::Type::Bool) { out = v->boolean ? 1.0f : 0.0f; return true; }
    return false;
}

} // namespace

bool isf_to_plugin(std::string_view fileName, std::string_view source, Manifest& m, std::string& shaderFile,
                   std::string& shaderText, std::string& error) {
    m = Manifest{};
    shaderFile.clear();
    shaderText.clear();
    error.clear();
    auto fail = [&](std::string message) {
        error = "ISF: " + std::move(message);
        return false;
    };
    if (source.size() > kMaxShaderBytes) return fail("arquivo maior que 64 KB");
    // O comentário JSON do topo.
    usize start = 0;
    while (start < source.size() && std::isspace(static_cast<unsigned char>(source[start]))) ++start;
    if (source.substr(start, 2) != "/*") return fail("falta o cabecalho JSON /*{ ... }*/ no inicio do arquivo");
    const usize close = source.find("*/", start + 2);
    if (close == std::string_view::npos) return fail("cabecalho JSON sem o */ de fechamento");
    const std::string_view header = source.substr(start + 2, close - start - 2);
    Value root;
    std::string jsonError;
    if (!json::parse(header, root, &jsonError) || !root.is_object())
        return fail("cabecalho JSON invalido" + (jsonError.empty() ? std::string{} : ": " + jsonError));

    if (const Value* passes = root.get("PASSES"); passes && passes->is_array()) {
        if (passes->array.size() > 1)
            return fail("ISF com varios passes (PASSES) nao e suportado; use um plugin .aureaplugin com \"passes\"");
        if (!passes->array.empty()) {
            const Value& p = passes->array[0];
            if (p.get("TARGET") || p.get("PERSISTENT"))
                return fail("ISF com buffer persistente ou TARGET nao e suportado");
        }
    }
    if (const Value* imported = root.get("IMPORTED"); imported
        && ((imported->is_object() && !imported->object.empty()) || (imported->is_array() && !imported->array.empty())))
        return fail("ISF com imagens importadas (IMPORTED) nao e suportado");
    if (const Value* persistent = root.get("PERSISTENT_BUFFERS"); persistent
        && ((persistent->is_object() && !persistent->object.empty()) || (persistent->is_array() && !persistent->array.empty())))
        return fail("ISF com PERSISTENT_BUFFERS nao e suportado");

    // Identidade do plugin, a partir do nome do arquivo.
    std::string_view base = fileName;
    if (const usize slash = base.find_last_of("/\\"); slash != std::string_view::npos) base = base.substr(slash + 1);
    std::string_view stem = base;
    if (const usize dot = stem.rfind('.'); dot != std::string_view::npos && dot > 0) stem = stem.substr(0, dot);
    std::string id = slug(stem);
    if (id.size() < 3) id = "shader-" + id;
    if (id.size() > 60) id.resize(60);
    while (!id.empty() && (id.back() == '-' || id.back() == '.')) id.pop_back();
    m.id = "isf." + id;
    if (!valid_plugin_id(m.id)) return fail("nome de arquivo que nao gera um id valido");
    m.version = "1.0.0";
    std::string display(stem);
    std::replace(display.begin(), display.end(), '_', ' ');
    m.name.emplace_back("*", display.empty() ? m.id : display);
    if (const Value* d = root.get("DESCRIPTION"); d && d->is_string() && !d->string.empty())
        m.description.emplace_back("*", d->string.substr(0, 512));
    if (const Value* c = root.get("CREDIT"); c && c->is_string()) m.author = c->string.substr(0, 128);
    m.category = "ISF";
    if (const Value* cats = root.get("CATEGORIES"); cats && cats->is_array() && !cats->array.empty()
        && cats->array[0].is_string() && !cats->array[0].string.empty())
        m.category = cats->array[0].string.substr(0, 32);
    m.kind = Kind::Shader;
    m.colorSpace = ColorSpace::Srgb;

    // Entradas → parâmetros, e as macros que dão a cada uma o tipo do ISF.
    std::string compat;
    std::string imageName;
    const Value* inputs = root.get("INPUTS");
    if (inputs && !inputs->is_array()) return fail("INPUTS deve ser uma lista");
    if (inputs) for (const Value& in : inputs->array) {
        if (!in.is_object()) return fail("entrada de INPUTS que nao e objeto");
        const Value* nameV = in.get("NAME");
        const Value* typeV = in.get("TYPE");
        if (!nameV || !nameV->is_string() || !typeV || !typeV->is_string()) return fail("entrada sem NAME ou TYPE");
        const std::string& name = nameV->string;
        const std::string& type = typeV->string;
        if (type == "image") {
            if (!imageName.empty()) return fail("mais de uma imagem de entrada (\"" + name + "\") nao e suportado");
            if (!valid_shader_identifier(name) && name != "inputImage") return fail("nome de imagem invalido \"" + name + "\"");
            imageName = name;
            continue;
        }
        if (type == "audio" || type == "audioFFT") return fail("entrada de audio \"" + name + "\" nao e suportada");
        if (!valid_shader_identifier(name))
            return fail("entrada \"" + name + "\": nome que o motor nao aceita como parametro (reservado ou invalido)");
        if (m.params.size() >= kMaxParams) return fail("mais de 16 entradas");
        ParamDecl p;
        p.id = name;
        if (const Value* label = in.get("LABEL"); label && label->is_string() && !label->string.empty())
            p.label.emplace_back("*", label->string.substr(0, 48));
        else
            p.label.emplace_back("*", name);
        const u32 slot = static_cast<u32>(m.params.size());
        const std::string ref = "aurea.params[" + std::to_string(slot) + "]";
        const Value* def = in.get("DEFAULT");
        if (type == "float") {
            p.kind = ParamKind::Float;
            p.min = 0.0f; p.max = 1.0f;
            (void)number_of(in.get("MIN"), p.min);
            (void)number_of(in.get("MAX"), p.max);
            if (!(p.max > p.min)) p.max = p.min + 1.0f;
            f32 d = p.min;
            (void)number_of(def, d);
            p.def[0] = std::clamp(d, p.min, p.max);
        } else if (type == "bool" || type == "event") {
            p.kind = ParamKind::Bool;
            f32 d = 0.0f;
            if (type == "bool") (void)number_of(def, d);
            p.def[0] = d >= 0.5f ? 1.0f : 0.0f;
            compat += "#undef " + name + "\n#define " + name + " (" + ref + ".x > 0.5)\n";
        } else if (type == "long") {
            const Value* values = in.get("VALUES");
            const Value* labels = in.get("LABELS");
            std::vector<i32> vals;
            if (values && values->is_array()) for (const Value& v : values->array) {
                f32 n = 0.0f;
                if (!number_of(&v, n)) return fail("entrada \"" + name + "\": VALUES nao numerico");
                vals.push_back(static_cast<i32>(std::lround(n)));
            }
            if (!vals.empty()) {
                if (vals.size() > kMaxEnumOptions) return fail("entrada \"" + name + "\": mais de 32 VALUES");
                p.kind = ParamKind::Enum;
                for (usize k = 0; k < vals.size(); ++k) {
                    LocalizedText t;
                    if (labels && labels->is_array() && k < labels->array.size() && labels->array[k].is_string())
                        t.emplace_back("*", labels->array[k].string.substr(0, 48));
                    else
                        t.emplace_back("*", std::to_string(vals[k]));
                    p.options.push_back(std::move(t));
                }
                f32 d = static_cast<f32>(vals[0]);
                (void)number_of(def, d);
                const auto it = std::find(vals.begin(), vals.end(), static_cast<i32>(std::lround(d)));
                p.def[0] = it == vals.end() ? 0.0f : static_cast<f32>(it - vals.begin());
                p.min = 0.0f;
                p.max = static_cast<f32>(vals.size() - 1);
                std::string list;
                for (usize k = 0; k < vals.size(); ++k) list += (k ? ", " : "") + std::to_string(vals[k]);
                compat += "int aurea_isf_long_" + name + "(float i) { const int v[" + std::to_string(vals.size())
                        + "] = int[](" + list + "); return v[clamp(int(i + 0.5), 0, " + std::to_string(vals.size() - 1)
                        + ")]; }\n#undef " + name + "\n#define " + name + " aurea_isf_long_" + name + "(" + ref + ".x)\n";
            } else {
                p.kind = ParamKind::Int;
                p.min = 0.0f; p.max = 10.0f;
                (void)number_of(in.get("MIN"), p.min);
                (void)number_of(in.get("MAX"), p.max);
                p.min = std::round(p.min); p.max = std::round(p.max);
                if (!(p.max > p.min)) p.max = p.min + 1.0f;
                f32 d = p.min;
                (void)number_of(def, d);
                p.def[0] = std::clamp(std::round(d), p.min, p.max);
                compat += "#undef " + name + "\n#define " + name + " int(" + ref + ".x + 0.5)\n";
            }
        } else if (type == "color") {
            p.kind = ParamKind::Color;
            f32 rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (def && def->is_array()) for (usize c = 0; c < def->array.size() && c < 4; ++c) (void)number_of(&def->array[c], rgba[c]);
            for (int c = 0; c < 3; ++c) p.def[c] = srgb_to_linear(rgba[c]);
            p.def[3] = std::clamp(rgba[3], 0.0f, 1.0f);
        } else if (type == "point2D") {
            p.kind = ParamKind::Point;
            f32 d[2] = {0.5f, 0.5f}, mx[2] = {-1.0f, -1.0f};
            bool hasDefault = false, hasMax = false;
            if (def && def->is_array() && def->array.size() >= 2)
                hasDefault = number_of(&def->array[0], d[0]) && number_of(&def->array[1], d[1]);
            if (const Value* maxV = in.get("MAX"); maxV && maxV->is_array() && maxV->array.size() >= 2)
                hasMax = number_of(&maxV->array[0], mx[0]) && number_of(&maxV->array[1], mx[1]);
            // Normalizado quando o próprio ISF diz que o ponto vai até 1 (MAX
            // <= 1, ou sem MAX com o padrão dentro de 0..1); senão, em pixels
            // de uma tela de referência 1920x1080 (o ISF não tem tamanho fixo).
            const bool normalized = hasMax ? (mx[0] <= 1.0f && mx[1] <= 1.0f)
                                           : (!hasDefault || (d[0] >= 0.0f && d[0] <= 1.0f && d[1] >= 0.0f && d[1] <= 1.0f));
            if (hasDefault && !normalized) { d[0] /= 1920.0f; d[1] /= 1080.0f; }
            // ISF tem y para cima; o ponto do motor, para baixo.
            p.def[0] = d[0];
            p.def[1] = 1.0f - d[1];
            p.min = -1.0f; p.max = 2.0f;
            const std::string flipped = "vec2(" + ref + ".x, 1.0 - " + ref + ".y)";
            compat += "#undef " + name + "\n#define " + name + " "
                    + (normalized ? "(" + flipped + ")" : "(" + flipped + " * RENDERSIZE)") + "\n";
        } else {
            return fail("entrada \"" + name + "\": tipo \"" + type + "\" nao suportado");
        }
        m.params.push_back(std::move(p));
    }

    // O arquivo de shader: macros do ISF, o corpo com os números de linha do
    // .fs e o aurea_main que chama o main do ISF.
    std::string body(source.substr(close + 2));
    usize bodyLine = 1;
    for (usize i = 0; i < close + 2; ++i) bodyLine += source[i] == '\n' ? 1 : 0;
    // `#version` do arquivo sai (o cabeçalho do motor já declara 450).
    for (usize at = 0; (at = body.find("#version", at)) != std::string::npos;) {
        const usize end = body.find('\n', at);
        body.replace(at, (end == std::string::npos ? body.size() : end) - at, "// (#version do ISF ignorado)");
        at += 10;
    }
    replace_identifier(body, "gl_FragColor", "aurea_isf_FragColor");
    replace_identifier(body, "gl_FragCoord", "aurea_isf_FragCoord");
    replace_identifier(body, "main", "aurea_isf_main");

    std::string out;
    out += "// Gerado pelo AUREA a partir do ISF \"" + std::string(base) + "\" (nao editar: reinstale o .fs).\n";
    out += "#define RENDERSIZE (aurea.resolution.xy)\n";
    out += "#define TIME (aurea.time.x)\n";
    out += "#define TIMEDELTA (aurea.time.z > 0.0 ? 1.0 / aurea.time.z : 0.0)\n";
    out += "#define FRAMEINDEX int(aurea.time.y)\n";
    out += "#define DATE vec4(0.0)\n";
    out += "#define PASSINDEX 0\n";
    out += "#define texture2D texture\n";
    if (!imageName.empty()) out += "#define " + imageName + " aurea_source\n";
    out += "vec2 isf_FragNormCoord;\n";
    out += "vec2 vv_FragNormCoord;\n";
    out += "vec4 aurea_isf_FragCoord;\n";
    out += "vec4 aurea_isf_FragColor;\n";
    out += "vec2 aurea_isf_uv;\n";
    out += "vec4 aurea_isf_norm(sampler2D s, vec2 c) { return texture(s, vec2(c.x, 1.0 - c.y)); }\n";
    out += "vec4 aurea_isf_pixel(sampler2D s, vec2 c) { vec2 size = vec2(textureSize(s, 0)); "
           "return texture(s, vec2(c.x, size.y - c.y) / size); }\n";
    out += "#define IMG_NORM_PIXEL(img, c) aurea_isf_norm(img, c)\n";
    out += "#define IMG_PIXEL(img, c) aurea_isf_pixel(img, c)\n";
    out += "#define IMG_THIS_PIXEL(img) texture(img, aurea_isf_uv)\n";
    out += "#define IMG_THIS_NORM_PIXEL(img) texture(img, aurea_isf_uv)\n";
    out += "#define IMG_SIZE(img) vec2(textureSize(img, 0))\n";
    out += compat;
    out += "#line " + std::to_string(bodyLine) + "\n";
    out += body;
    out += "\nvec4 aurea_main(vec2 uv) {\n"
           "    aurea_isf_uv = uv;\n"
           "    isf_FragNormCoord = vec2(uv.x, 1.0 - uv.y);\n"
           "    vv_FragNormCoord = isf_FragNormCoord;\n"
           "    aurea_isf_FragCoord = vec4(isf_FragNormCoord * RENDERSIZE, 0.0, 1.0);\n"
           "    aurea_isf_FragColor = vec4(0.0);\n"
           "    aurea_isf_main();\n"
           "    return aurea_isf_FragColor;\n"
           "}\n";
    // Nome do arquivo = o do .fs: as mensagens do glslang apontam "<arquivo>.fs:linha".
    std::string file(base);
    for (char& c : file) if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 32) c = '_';
    if (file.empty()) file = "isf.fs";
    PassDecl pass;
    pass.shader = file;
    m.passes.push_back(std::move(pass));
    shaderFile = file;
    shaderText = std::move(out);
    return true;
}

} // namespace aurea::plugins
