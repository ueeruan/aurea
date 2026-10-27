// =============================================================================
//  0.3.3 plugins: o formato 2 do plugin.json — "script", "permissions",
//  "strings" e "ui" (painéis e ações) — e a parte nova da lista em JSON.
//
//  Como no resto do manifesto, toda recusa diz o campo e o motivo em
//  português: é o que o autor do plugin lê no aparelho.
// =============================================================================
#include "PluginInternal.hpp"
#include "aurea/project/Presets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace aurea::plugins {

// =============================================================================
// Utilidades públicas
// =============================================================================
const char* control_type_name(ControlType t) noexcept {
    switch (t) {
        case ControlType::Button: return "button";
        case ControlType::Slider: return "slider";
        case ControlType::Ruler: return "ruler";
        case ControlType::Color: return "color";
        case ControlType::Choice: return "choice";
        case ControlType::Text: return "text";
        case ControlType::List: return "list";
        case ControlType::Image: return "image";
        case ControlType::Label: return "label";
        case ControlType::Toggle: return "toggle";
    }
    return "button";
}

const char* placement_name(u32 bit) noexcept {
    switch (bit) {
        case kPlaceEditorPanel: return "editor.panel";
        case kPlaceHomeTab: return "home.tab";
        case kPlaceEditorMenu: return "editor.menu";
        case kPlaceLayerMenu: return "layer.menu";
        case kPlaceLayerToolbar: return "layer.toolbar";
        default: return "";
    }
}

const std::vector<std::string_view>& icon_names() noexcept {
    // A mesma lista no Android (PluginUi.kt) e no iOS (PluginsV2.swift).
    static const std::vector<std::string_view> kIcons = {
        "wand", "sparkles", "text", "star", "bolt", "cloud", "music", "photo", "film", "layers", "gear",
        "brush", "clock", "list", "cube", "lightbulb", "wrench", "book", "flag", "grid", "camera", "chart",
        "sliders", "eye", "folder", "link", "speed", "waveform",
    };
    return kIcons;
}

bool valid_icon(std::string_view name) noexcept {
    const auto& names = icon_names();
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool valid_function_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64) return false;
    auto start = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; };
    if (!start(name[0])) return false;
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$')) return false;
    return true;
}

bool valid_domain(std::string_view host) noexcept {
    if (host.size() < 3 || host.size() > 253) return false;
    if (host.find('.') == std::string_view::npos) return false;
    if (host.front() == '.' || host.back() == '.' || host.front() == '-') return false;
    if (host.find("..") != std::string_view::npos) return false;
    for (const char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
    return true;
}

namespace {

using json::Value;

/// Id de painel/controle/ação: [A-Za-z0-9_.-]{1,32}.
bool valid_ui_id(std::string_view id) {
    if (id.empty() || id.size() > 32) return false;
    for (const char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

bool safe_file(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    if (name.front() == '/' || name.front() == '\\' || name.find(':') != std::string_view::npos) return false;
    usize start = 0;
    while (start <= name.size()) {
        usize end = name.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = name.size();
        const std::string_view part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

std::string lower_ext(std::string_view name) {
    const usize dot = name.rfind('.');
    std::string ext = dot == std::string_view::npos ? std::string() : std::string(name.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

struct V2 {
    std::string& error;
    bool fail(std::string m) {
        if (error.empty()) error = "plugin.json: " + std::move(m);
        return false;
    }
    bool text(const Value* v, LocalizedText& out, const std::string& field, usize maxChars, bool required) {
        return read_localized(v, out, field.c_str(), maxChars, required, error);
    }
    bool number(const Value* v, f64& out, const std::string& field) {
        if (!v) return true;
        if (!v->is_number() || !std::isfinite(v->number)) return fail(field + " deve ser numero");
        out = v->number;
        return true;
    }
    bool boolean(const Value* v, bool& out, const std::string& field) {
        if (!v) return true;
        if (v->type != Value::Type::Bool) return fail(field + " deve ser true ou false");
        out = v->boolean;
        return true;
    }
    bool function(const Value* v, std::string& out, const std::string& field) {
        if (!v) return true;
        if (!v->is_string() || !valid_function_name(v->string))
            return fail(field + " deve ser o nome de uma funcao do script (letras, digitos, _ e $)");
        out = v->string;
        return true;
    }
    bool only_keys(const Value& obj, std::initializer_list<const char*> keys, const std::string& where) {
        for (const auto& [k, v] : obj.object) {
            (void)v;
            if (std::none_of(keys.begin(), keys.end(), [&](const char* allowed) { return k == allowed; }))
                return fail(where + ": campo desconhecido \"" + k + "\"");
        }
        return true;
    }
};

bool parse_color(const std::string& s) {
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') return false;
    for (usize i = 1; i < s.size(); ++i) if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

bool read_control(const Value& v, ControlDecl& c, V2& r) {
    if (!v.is_object()) return r.fail("controle deve ser um objeto");
    const Value* id = v.get("id");
    if (!id || !id->is_string() || !valid_ui_id(id->string))
        return r.fail("controle sem \"id\" valido (letras, digitos, _ . -, ate 32)");
    c.id = id->string;
    const std::string where = "controle \"" + c.id + "\"";
    const Value* type = v.get("type");
    if (!type || !type->is_string()) return r.fail(where + ": falta \"type\"");
    static const std::pair<const char*, ControlType> kTypes[] = {
        {"button", ControlType::Button}, {"slider", ControlType::Slider}, {"ruler", ControlType::Ruler},
        {"color", ControlType::Color}, {"choice", ControlType::Choice}, {"text", ControlType::Text},
        {"list", ControlType::List}, {"image", ControlType::Image}, {"label", ControlType::Label},
        {"toggle", ControlType::Toggle},
    };
    bool known = false;
    for (const auto& [name, t] : kTypes) if (type->string == name) { c.type = t; known = true; }
    if (!known)
        return r.fail(where + ": tipo desconhecido \"" + type->string
                      + "\" (button, slider, ruler, color, choice, text, list, image, label ou toggle)");
    if (!r.only_keys(v, {"id", "type", "label", "placeholder", "text", "style", "min", "max", "step", "default", "unit",
                         "multiline", "maxLength", "options", "items", "src", "aspect", "onClick", "onChange", "onSelect"},
                     where))
        return false;
    if (!r.text(v.get("label"), c.label, "\"" + c.id + "\".label", 64, c.type == ControlType::Button)) return false;
    if (!r.function(v.get("onClick"), c.onClick, where + ": \"onClick\"")) return false;
    if (!r.function(v.get("onChange"), c.onChange, where + ": \"onChange\"")) return false;
    if (!r.function(v.get("onSelect"), c.onSelect, where + ": \"onSelect\"")) return false;
    if (!c.onClick.empty() && c.type != ControlType::Button) return r.fail(where + ": \"onClick\" so em button");
    if (!c.onSelect.empty() && c.type != ControlType::List) return r.fail(where + ": \"onSelect\" so em list");
    if (!c.onChange.empty() && (c.type == ControlType::Button || c.type == ControlType::List || c.type == ControlType::Image
                                || c.type == ControlType::Label))
        return r.fail(where + ": \"onChange\" nao vale em " + control_type_name(c.type));
    if (const Value* unit = v.get("unit")) {
        if (!unit->is_string() || unit->string.size() > 8) return r.fail(where + ": \"unit\" deve ser texto curto (ate 8)");
        c.unit = unit->string;
    }
    switch (c.type) {
        case ControlType::Button: {
            if (c.onClick.empty()) return r.fail(where + ": botao precisa de \"onClick\" (a funcao do script)");
            if (const Value* style = v.get("style")) {
                if (!style->is_string() || (style->string != "primary" && style->string != "secondary" && style->string != "danger"))
                    return r.fail(where + ": \"style\" deve ser \"primary\", \"secondary\" ou \"danger\"");
                c.style = style->string;
            } else c.style = "primary";
            break;
        }
        case ControlType::Slider: case ControlType::Ruler: {
            if (c.type == ControlType::Ruler) { c.min = -1.0e6; c.max = 1.0e6; }
            if (!r.number(v.get("min"), c.min, where + ": \"min\"") || !r.number(v.get("max"), c.max, where + ": \"max\"")
                || !r.number(v.get("step"), c.step, where + ": \"step\""))
                return false;
            if (!(c.max > c.min)) return r.fail(where + ": \"max\" deve ser maior que \"min\"");
            if (c.step < 0) return r.fail(where + ": \"step\" deve ser >= 0");
            c.def = c.type == ControlType::Ruler ? std::clamp(0.0, c.min, c.max) : c.min;
            if (!r.number(v.get("default"), c.def, where + ": \"default\"")) return false;
            if (c.def < c.min || c.def > c.max) return r.fail(where + ": \"default\" fora de [min, max]");
            break;
        }
        case ControlType::Color: {
            c.defText = "#FFFFFF";
            if (const Value* d = v.get("default")) {
                if (!d->is_string() || !parse_color(d->string)) return r.fail(where + ": cor \"default\" deve ser \"#RRGGBB\" ou \"#RRGGBBAA\"");
                c.defText = d->string;
                std::transform(c.defText.begin(), c.defText.end(), c.defText.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            }
            break;
        }
        case ControlType::Choice: {
            const Value* opts = v.get("options");
            if (!opts || !opts->is_array() || opts->array.empty())
                return r.fail(where + ": \"choice\" precisa de \"options\" (lista de textos)");
            if (opts->array.size() > kMaxChoiceOptions) return r.fail(where + ": mais de 32 opcoes");
            for (const Value& o : opts->array) {
                LocalizedText t;
                if (!r.text(&o, t, "\"" + c.id + "\".options", 64, true)) return false;
                c.options.push_back(std::move(t));
            }
            if (!r.number(v.get("default"), c.def, where + ": \"default\"")) return false;
            if (c.def < 0 || c.def >= static_cast<f64>(c.options.size()) || std::floor(c.def) != c.def)
                return r.fail(where + ": \"default\" deve ser o indice de uma opcao");
            break;
        }
        case ControlType::Text: {
            if (const Value* d = v.get("default")) {
                if (!d->is_string()) return r.fail(where + ": \"default\" deve ser texto");
                c.defText = d->string;
            }
            if (!r.text(v.get("placeholder"), c.placeholder, "\"" + c.id + "\".placeholder", 128, false)) return false;
            if (!r.boolean(v.get("multiline"), c.multiline, where + ": \"multiline\"")) return false;
            f64 maxLen = c.maxLength;
            if (!r.number(v.get("maxLength"), maxLen, where + ": \"maxLength\"")) return false;
            if (maxLen < 1 || maxLen > 10000) return r.fail(where + ": \"maxLength\" entre 1 e 10000");
            c.maxLength = static_cast<u32>(maxLen);
            if (c.defText.size() > c.maxLength) return r.fail(where + ": \"default\" maior que \"maxLength\"");
            break;
        }
        case ControlType::List: {
            if (const Value* items = v.get("items")) {
                if (!items->is_array()) return r.fail(where + ": \"items\" deve ser uma lista");
                if (items->array.size() > kMaxListItems) return r.fail(where + ": mais de 200 itens");
                for (const Value& it : items->array) {
                    ListItem item;
                    if (!it.is_object()) return r.fail(where + ": item deve ser {\"id\", \"label\"}");
                    const Value* iid = it.get("id");
                    if (!iid || !iid->is_string() || iid->string.empty() || iid->string.size() > 64)
                        return r.fail(where + ": item sem \"id\" (texto ate 64)");
                    item.id = iid->string;
                    if (!r.text(it.get("label"), item.label, "\"" + c.id + "\".items.label", 128, true)) return false;
                    if (!r.text(it.get("detail"), item.detail, "\"" + c.id + "\".items.detail", 256, false)) return false;
                    c.items.push_back(std::move(item));
                }
            }
            break;
        }
        case ControlType::Image: {
            if (const Value* src = v.get("src")) {
                if (!src->is_string() || !safe_file(src->string)) return r.fail(where + ": \"src\" deve ser um arquivo do pacote");
                const std::string ext = lower_ext(src->string);
                if (ext != "png" && ext != "jpg" && ext != "jpeg") return r.fail(where + ": \"src\" deve ser .png ou .jpg");
                c.src = src->string;
            }
            f64 aspect = 0.0;
            if (!r.number(v.get("aspect"), aspect, where + ": \"aspect\"")) return false;
            if (aspect < 0 || aspect > 8) return r.fail(where + ": \"aspect\" entre 0 e 8 (largura/altura)");
            c.aspect = static_cast<f32>(aspect);
            break;
        }
        case ControlType::Label: {
            if (!r.text(v.get("text"), c.text, "\"" + c.id + "\".text", 512, c.label.empty())) return false;
            break;
        }
        case ControlType::Toggle: {
            if (!r.boolean(v.get("default"), c.defBool, where + ": \"default\"")) return false;
            break;
        }
    }
    return true;
}

bool read_placement(const Value* v, u32 allowed, u32& out, const std::string& where, V2& r) {
    if (!v) return true;
    std::vector<const Value*> names;
    if (v->is_string()) names.push_back(v);
    else if (v->is_array()) for (const Value& e : v->array) names.push_back(&e);
    else return r.fail(where + ": \"placement\" deve ser texto ou lista");
    out = 0;
    for (const Value* n : names) {
        if (!n->is_string()) return r.fail(where + ": \"placement\" com valor que nao e texto");
        u32 bit = 0;
        for (u32 b = 1; b <= kPlaceLayerToolbar; b <<= 1) if (n->string == placement_name(b)) bit = b;
        if (!bit || !(bit & allowed)) {
            std::string list;
            for (u32 b = 1; b <= kPlaceLayerToolbar; b <<= 1) if (b & allowed) { if (!list.empty()) list += ", "; list += placement_name(b); }
            return r.fail(where + ": lugar \"" + n->string + "\" invalido aqui (use " + list + ")");
        }
        out |= bit;
    }
    if (!out) return r.fail(where + ": \"placement\" vazio");
    return true;
}

} // namespace

bool parse_manifest_v2(const json::Value& root, Manifest& m, u32 format, std::string& error) {
    V2 r{error};
    const bool hasV2 = root.get("ui") || root.get("script") || root.get("permissions") || root.get("strings");
    if (format < kFormatVersionV2) {
        if (hasV2) return r.fail("\"ui\", \"script\", \"permissions\" e \"strings\" pedem \"aureaPlugin\": 2");
        return true;
    }
    // --- script ---
    if (const Value* s = root.get("script")) {
        if (!s->is_string() || !safe_file(s->string) || lower_ext(s->string) != "js")
            return r.fail("\"script\" deve ser um arquivo .js do pacote (ex.: \"main.js\")");
        m.script = s->string;
    }
    // --- permissions ---
    if (const Value* p = root.get("permissions")) {
        if (!p->is_object()) return r.fail("\"permissions\" deve ser um objeto");
        if (!r.only_keys(*p, {"network", "clipboard", "files"}, "\"permissions\"")) return false;
        if (const Value* net = p->get("network")) {
            if (!net->is_array()) return r.fail("\"permissions\".network deve ser a lista de dominios (ex.: [\"api.exemplo.com\"])");
            if (net->array.size() > kMaxNetworkDomains) return r.fail("\"permissions\".network com mais de 8 dominios");
            for (const Value& d : net->array) {
                if (!d.is_string()) return r.fail("\"permissions\".network deve ter textos");
                std::string host = d.string;
                std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (host == "*" || host.find('*') != std::string::npos)
                    return r.fail("\"permissions\".network: \"*\" nao e aceito; liste cada dominio que o plugin usa");
                if (host.find("://") != std::string::npos || host.find('/') != std::string::npos || host.find(':') != std::string::npos)
                    return r.fail("\"permissions\".network: \"" + d.string + "\" deve ser so o dominio (sem https://, porta ou caminho)");
                if (!valid_domain(host)) return r.fail("\"permissions\".network: dominio invalido \"" + d.string + "\"");
                if (std::find(m.permissions.network.begin(), m.permissions.network.end(), host) == m.permissions.network.end())
                    m.permissions.network.push_back(host);
            }
        }
        if (!r.boolean(p->get("clipboard"), m.permissions.clipboard, "\"permissions\".clipboard")) return false;
        if (!r.boolean(p->get("files"), m.permissions.files, "\"permissions\".files")) return false;
    }
    // --- strings ---
    if (const Value* st = root.get("strings")) {
        if (!st->is_object()) return r.fail("\"strings\" deve ser um objeto idioma -> {chave: texto}");
        for (const auto& [lang, table] : st->object) {
            const bool tagOk = lang.size() >= 1 && lang.size() <= 8
                && std::all_of(lang.begin(), lang.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '-' || c == '*'; });
            if (!tagOk) return r.fail("\"strings\": idioma invalido \"" + lang + "\"");
            if (!table.is_object()) return r.fail("\"strings\"." + lang + " deve ser um objeto chave -> texto");
            if (table.object.size() > kMaxStringKeys) return r.fail("\"strings\"." + lang + " com mais de 512 chaves");
            std::vector<std::pair<std::string, std::string>> rows;
            for (const auto& [key, text] : table.object) {
                if (!text.is_string() || text.string.size() > 2000) return r.fail("\"strings\"." + lang + "." + key + " deve ser texto (ate 2000)");
                rows.emplace_back(key, text.string);
            }
            m.strings.emplace_back(lang, std::move(rows));
        }
    }
    // --- ui ---
    std::vector<std::string> controlIds;
    std::vector<std::string> functions;   // o que a interface chama
    if (const Value* ui = root.get("ui")) {
        if (!ui->is_object()) return r.fail("\"ui\" deve ser um objeto com \"panels\" e/ou \"actions\"");
        if (!r.only_keys(*ui, {"panels", "actions"}, "\"ui\"")) return false;
        if (const Value* panels = ui->get("panels")) {
            if (!panels->is_array()) return r.fail("\"ui\".panels deve ser uma lista");
            if (panels->array.size() > kMaxPanels) return r.fail("\"ui\".panels com mais de 8 paineis");
            for (const Value& pv : panels->array) {
                PanelDecl p;
                if (!pv.is_object()) return r.fail("painel deve ser um objeto");
                const Value* id = pv.get("id");
                if (!id || !id->is_string() || !valid_ui_id(id->string)) return r.fail("painel sem \"id\" valido");
                p.id = id->string;
                const std::string where = "painel \"" + p.id + "\"";
                if (!r.only_keys(pv, {"id", "title", "icon", "placement", "controls", "onOpen"}, where)) return false;
                for (const PanelDecl& other : m.ui.panels) if (other.id == p.id) return r.fail(where + " repetido");
                if (!r.text(pv.get("title"), p.title, "\"" + p.id + "\".title", 48, true)) return false;
                if (const Value* icon = pv.get("icon")) {
                    if (!icon->is_string() || !valid_icon(icon->string)) return r.fail(where + ": icone desconhecido (veja a lista em docs/plugins)");
                    p.icon = icon->string;
                } else p.icon = "wand";
                if (!read_placement(pv.get("placement"), kPlaceEditorPanel | kPlaceHomeTab, p.placement, where, r)) return false;
                if (!r.function(pv.get("onOpen"), p.onOpen, where + ": \"onOpen\"")) return false;
                if (!p.onOpen.empty()) functions.push_back(p.onOpen);
                const Value* controls = pv.get("controls");
                if (!controls || !controls->is_array() || controls->array.empty()) return r.fail(where + ": precisa de \"controls\" (1 a 32)");
                if (controls->array.size() > kMaxControlsPerPanel) return r.fail(where + ": mais de 32 controles");
                for (const Value& cv : controls->array) {
                    ControlDecl c;
                    if (!read_control(cv, c, r)) return false;
                    if (std::find(controlIds.begin(), controlIds.end(), c.id) != controlIds.end())
                        return r.fail("controle \"" + c.id + "\" repetido (os ids valem para o plugin inteiro)");
                    controlIds.push_back(c.id);
                    for (const std::string* f : {&c.onClick, &c.onChange, &c.onSelect}) if (!f->empty()) functions.push_back(*f);
                    p.controls.push_back(std::move(c));
                }
                m.ui.panels.push_back(std::move(p));
            }
        }
        if (const Value* actions = ui->get("actions")) {
            if (!actions->is_array()) return r.fail("\"ui\".actions deve ser uma lista");
            if (actions->array.size() > kMaxActions) return r.fail("\"ui\".actions com mais de 16 acoes");
            for (const Value& av : actions->array) {
                ActionDecl a;
                if (!av.is_object()) return r.fail("acao deve ser um objeto");
                const Value* id = av.get("id");
                if (!id || !id->is_string() || !valid_ui_id(id->string)) return r.fail("acao sem \"id\" valido");
                a.id = id->string;
                const std::string where = "acao \"" + a.id + "\"";
                if (!r.only_keys(av, {"id", "title", "icon", "placement", "call", "panel", "needsLayer"}, where)) return false;
                for (const ActionDecl& other : m.ui.actions) if (other.id == a.id) return r.fail(where + " repetida");
                if (!r.text(av.get("title"), a.title, "\"" + a.id + "\".title", 48, true)) return false;
                if (!av.get("placement")) return r.fail(where + ": falta \"placement\" (editor.menu, layer.menu ou layer.toolbar)");
                if (!read_placement(av.get("placement"), kPlaceEditorMenu | kPlaceLayerMenu | kPlaceLayerToolbar, a.placement, where, r))
                    return false;
                if (const Value* icon = av.get("icon")) {
                    if (!icon->is_string() || !valid_icon(icon->string)) return r.fail(where + ": icone desconhecido (veja a lista em docs/plugins)");
                    a.icon = icon->string;
                } else if (a.placement & kPlaceLayerToolbar) {
                    return r.fail(where + ": botao da barra da camada precisa de \"icon\"");
                } else a.icon = "wand";
                if (!r.function(av.get("call"), a.call, where + ": \"call\"")) return false;
                if (const Value* panel = av.get("panel")) {
                    if (!panel->is_string()) return r.fail(where + ": \"panel\" deve ser o id de um painel");
                    a.panel = panel->string;
                }
                if (a.call.empty() == a.panel.empty()) return r.fail(where + ": use \"call\" (funcao) ou \"panel\" (painel), um dos dois");
                a.needsLayer = (a.placement & (kPlaceLayerMenu | kPlaceLayerToolbar)) != 0;
                if (!r.boolean(av.get("needsLayer"), a.needsLayer, where + ": \"needsLayer\"")) return false;
                if (!a.call.empty()) functions.push_back(a.call);
                m.ui.actions.push_back(std::move(a));
            }
        }
        for (const ActionDecl& a : m.ui.actions) {
            if (a.panel.empty()) continue;
            const PanelDecl* p = m.ui.panel(a.panel);
            if (!p) return r.fail("acao \"" + a.id + "\" abre o painel \"" + a.panel + "\", que nao existe");
            if (!(p->placement & kPlaceEditorPanel))
                return r.fail("acao \"" + a.id + "\" abre \"" + a.panel + "\", que nao tem o lugar \"editor.panel\"");
        }
    }
    if (!functions.empty() && m.script.empty())
        return r.fail("a interface chama \"" + functions.front() + "\", mas o plugin nao tem \"script\"");
    if (m.kind == Kind::Panel && m.ui.empty() && m.script.empty())
        return r.fail("plugin \"panel\" precisa de \"ui\" (paineis ou acoes) e \"script\"");
    return true;
}

// =============================================================================
// Lista em JSON (a parte da interface)
// =============================================================================
namespace {

void text_json(json::Writer& w, const LocalizedText& t) {
    w.begin_object();
    for (const auto& [lang, text] : t) w.key(lang).value(text);
    w.end_object();
}

void placement_json(json::Writer& w, u32 placement) {
    w.begin_array();
    for (u32 b = 1; b <= kPlaceLayerToolbar; b <<= 1) if (placement & b) w.value(placement_name(b));
    w.end_array();
}

} // namespace

void write_v2_json(json::Writer& w, const PluginInfo& p) {
    w.key("format").value(p.format);
    w.key("hasScript").value(!p.script.empty());
    w.key("enabled").value(p.enabled);
    w.key("generation").value(static_cast<i64>(p.generation));
    w.key("permissions").begin_object();
    w.key("network").begin_array();
    for (const std::string& d : p.permissions.network) w.value(d);
    w.end_array();
    w.key("clipboard").value(p.permissions.clipboard);
    w.key("files").value(p.permissions.files);
    w.end_object();
    w.key("granted").begin_object();
    w.key("network").value((p.granted & kPermNetwork) != 0);
    w.key("clipboard").value((p.granted & kPermClipboard) != 0);
    w.key("files").value((p.granted & kPermFiles) != 0);
    w.end_object();
    w.key("ui").begin_object();
    w.key("panels").begin_array();
    for (const PanelDecl& panel : p.ui.panels) {
        w.begin_object();
        w.key("id").value(panel.id);
        w.key("title");
        text_json(w, panel.title);
        w.key("icon").value(panel.icon);
        w.key("placement");
        placement_json(w, panel.placement);
        w.key("controls").begin_array();
        for (const ControlDecl& c : panel.controls) {
            w.begin_object();
            w.key("id").value(c.id);
            w.key("type").value(control_type_name(c.type));
            w.key("label");
            text_json(w, c.label);
            switch (c.type) {
                case ControlType::Button:
                    w.key("style").value(c.style);
                    break;
                case ControlType::Slider: case ControlType::Ruler:
                    w.key("min").value(c.min).key("max").value(c.max).key("step").value(c.step).key("unit").value(c.unit);
                    break;
                case ControlType::Choice:
                    w.key("options").begin_array();
                    for (const LocalizedText& o : c.options) text_json(w, o);
                    w.end_array();
                    break;
                case ControlType::Text:
                    w.key("placeholder");
                    text_json(w, c.placeholder);
                    w.key("multiline").value(c.multiline).key("maxLength").value(c.maxLength);
                    break;
                case ControlType::Image:
                    w.key("aspect").value(c.aspect);
                    break;
                case ControlType::Label:
                    w.key("text");
                    text_json(w, c.text);
                    break;
                default: break;
            }
            w.key("hasHandler").value(!c.onClick.empty() || !c.onChange.empty() || !c.onSelect.empty());
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.key("actions").begin_array();
    for (const ActionDecl& a : p.ui.actions) {
        w.begin_object();
        w.key("id").value(a.id);
        w.key("title");
        text_json(w, a.title);
        w.key("icon").value(a.icon);
        w.key("placement");
        placement_json(w, a.placement);
        w.key("panel").value(a.panel);
        w.key("call").value(a.call);
        w.key("needsLayer").value(a.needsLayer);
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

} // namespace aurea::plugins
