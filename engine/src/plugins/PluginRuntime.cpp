// =============================================================================
//  0.3.4 plugins: o runtime de um plugin com script (PluginRuntime.hpp).
//
//  Aqui moram as APIs que não dependem do projeto: `ui` (estado dos painéis),
//  `console`, `storage`, `fetch`, `clipboard`, `files` e o pedaço do `app`
//  que não é o projeto (idioma, versão, textos do manifesto, alertas). A
//  API de objetos do AUREA entra pelo `ApiInstaller` do motor.
//
//  Permissões: cada função confere o que a pessoa CONCEDEU (`granted_`), não
//  o que o plugin pediu; negada = PermissionError no script. O domínio do
//  fetch é conferido aqui, antes de qualquer byte sair do aparelho.
// =============================================================================
#include "aurea/plugins/PluginRuntime.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/project/Presets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace aurea::plugins {

using script::Context;
using script::ObjectRef;
using script::ScriptError;
using script::Value;
namespace fs = std::filesystem;

namespace {

Value arg(const std::vector<Value>& a, usize i) { return i < a.size() ? a[i] : Value(); }

fs::path path_of(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool safe_relative(std::string_view name) {
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

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// "#RRGGBB"/"#RRGGBBAA" (maiúsculas) ou vazio.
std::string normalize_color(const std::string& s) {
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') return {};
    std::string out = "#";
    for (usize i = 1; i < s.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return {};
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    }
    return out;
}

std::string color_from_components(const f64* c, usize n) {
    char b[16];
    auto byte = [](f64 v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    if (n >= 4 && c[3] < 1.0) std::snprintf(b, sizeof(b), "#%02X%02X%02X%02X", byte(c[0]), byte(c[1]), byte(c[2]), byte(c[3]));
    else std::snprintf(b, sizeof(b), "#%02X%02X%02X", byte(c[0]), byte(c[1]), byte(c[2]));
    return b;
}

/// Corta texto em `max` bytes sem partir um caractere UTF-8.
void clip_utf8(std::string& s, usize max) {
    if (s.size() <= max) return;
    usize n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
}

std::string pick_text(const LocalizedText& t, std::string_view locale) {
    if (t.empty()) return {};
    for (const auto& [l, v] : t) if (l == locale) return v;
    const std::string_view lang = locale.substr(0, locale.find('-'));
    for (const auto& [l, v] : t) if (std::string_view(l).substr(0, l.find('-')) == lang) return v;
    for (const char* f : {"*", "en", "pt-BR"})
        for (const auto& [l, v] : t) if (l == f) return v;
    return t.front().second;
}

void write_text_map(json::Writer& w, const LocalizedText& t) {
    w.begin_object();
    for (const auto& [l, v] : t) w.key(l).value(v);
    w.end_object();
}

} // namespace

// =============================================================================
PluginRuntime::PluginRuntime(PluginInfo info, std::string dataDirectory, script::Limits limits)
    : info_(std::move(info)), dataDirectory_(std::move(dataDirectory)), limits_(limits) {
    granted_ = info_.granted & info_.permissions.mask();
    for (const PanelDecl& panel : info_.ui.panels) {
        for (const ControlDecl& c : panel.controls) {
            ControlState s;
            s.decl = &c;
            switch (c.type) {
                case ControlType::Slider: case ControlType::Ruler: case ControlType::Choice: s.number = c.def; break;
                case ControlType::Toggle: s.flag = c.defBool; break;
                case ControlType::Text: case ControlType::Color: s.text = c.defText; break;
                case ControlType::List:
                    for (const ListItem& it : c.items) { s.items.emplace_back(it.id, std::string()); s.details.emplace_back(); }
                    break;
                case ControlType::Image: if (!c.src.empty()) s.image = package_file(c.src); break;
                default: break;
            }
            controls_.emplace_back(c.id, std::move(s));
        }
    }
}

PluginRuntime::~PluginRuntime() = default;

void PluginRuntime::set_io(std::shared_ptr<const HostIO> io) noexcept {
    std::lock_guard<std::mutex> lock(ioMutex_);
    io_ = std::move(io);
}

void PluginRuntime::cancel() noexcept {
    // ctx_ só muda sob callMutex_; durante uma chamada ele existe e não troca.
    if (busy_.load(std::memory_order_acquire) && ctx_) ctx_->request_cancel();
}

void PluginRuntime::reload() {
    std::lock_guard<std::mutex> lock(callMutex_);
    ctx_.reset();
    loaded_ = false;
    loadFailed_ = false;
    loadError_ = ScriptError{};
    log(1, "script recarregado");
}

void PluginRuntime::log(int level, std::string text) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    clip_utf8(text, 4096);
    const f64 t = std::chrono::duration<f64>(std::chrono::steady_clock::now() - born_).count();
    console_.push_back(ConsoleLine{std::clamp(level, 0, 3), std::move(text), t});
    if (console_.size() > kMaxConsoleLines) console_.erase(console_.begin(), console_.begin() + static_cast<std::ptrdiff_t>(console_.size() - kMaxConsoleLines));
}

void PluginRuntime::message(std::string kind, std::string text) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    clip_utf8(text, 2000);
    if (messages_.size() < 16) messages_.push_back(Message{std::move(kind), std::move(text)});
}

void PluginRuntime::clear_console() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    console_.clear();
}

std::string PluginRuntime::console_json() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    json::Writer w;
    w.begin_array();
    for (const ConsoleLine& l : console_) w.begin_object().key("level").value(l.level).key("text").value(l.text).key("time").value(l.time).end_object();
    w.end_array();
    return w.str();
}

std::string PluginRuntime::text(std::string_view key) const {
    auto find_in = [&](std::string_view lang) -> const std::string* {
        for (const auto& [l, rows] : info_.strings) {
            if (l != lang) continue;
            for (const auto& [k, v] : rows) if (k == key) return &v;
        }
        return nullptr;
    };
    if (const std::string* t = find_in(locale_)) return *t;
    const std::string_view lang = std::string_view(locale_).substr(0, locale_.find('-'));
    for (const auto& [l, rows] : info_.strings) {
        if (std::string_view(l).substr(0, l.find('-')) != lang) continue;
        for (const auto& [k, v] : rows) if (k == key) return v;
    }
    for (const char* f : {"*", "en", "pt-BR"}) if (const std::string* t = find_in(f)) return *t;
    for (const auto& [l, rows] : info_.strings)
        for (const auto& [k, v] : rows) if (k == key) return v;
    return {};
}

std::string PluginRuntime::package_file(std::string_view relative) const {
    if (!safe_relative(relative) || info_.directory.empty()) return {};
    const fs::path p = path_of(info_.directory) / path_of(std::string(relative));
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return {};
    return utf8(p);
}

std::string PluginRuntime::data_folder() const {
    if (dataDirectory_.empty()) return {};
    const fs::path p = path_of(dataDirectory_) / path_of(info_.id);
    std::error_code ec;
    fs::create_directories(p, ec);
    return utf8(p);
}

void PluginRuntime::set_image(std::string_view control, std::string path) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& [id, s] : controls_) if (id == control) s.image = std::move(path);
}

PluginRuntime::ControlState* PluginRuntime::control(std::string_view id) {
    for (auto& [cid, s] : controls_) if (cid == id) return &s;
    return nullptr;
}

// --- storage ------------------------------------------------------------------------
void PluginRuntime::load_storage() {
    if (storageLoaded_) return;
    storageLoaded_ = true;
    storage_ = "{}";
    if (dataDirectory_.empty()) return;
    std::vector<u8> bytes;
    if (!fileio::read_all(utf8(path_of(dataDirectory_) / path_of(info_.id + ".json")), bytes, kMaxStorageBytes + 16)) return;
    std::string text(bytes.begin(), bytes.end());
    json::Value v;
    if (json::parse(text, v) && v.is_object()) storage_ = std::move(text);
}

void PluginRuntime::save_storage() {
    if (dataDirectory_.empty()) return;
    std::error_code ec;
    fs::create_directories(path_of(dataDirectory_), ec);
    (void)fileio::write_atomic(utf8(path_of(dataDirectory_) / path_of(info_.id + ".json")), storage_.data(), storage_.size());
}

// --- valores dos controles -----------------------------------------------------------
Value PluginRuntime::control_value(Context& c, const ControlState& s) {
    switch (s.decl->type) {
        case ControlType::Slider: case ControlType::Ruler: case ControlType::Choice: return Value::number(s.number);
        case ControlType::Toggle: return Value::boolean(s.flag);
        case ControlType::Text: case ControlType::Color: return c.str(s.text);
        case ControlType::Label: return c.str(s.textSet ? s.text : pick_text(s.decl->text, locale_));
        case ControlType::List: return s.hasSelection ? c.str(s.text) : Value::null();
        default: return Value::null();
    }
}

void PluginRuntime::set_control_value(Context& c, ControlState& s, const Value& v) {
    const ControlDecl& d = *s.decl;
    switch (d.type) {
        case ControlType::Slider: case ControlType::Ruler: {
            f64 n = c.to_number(v);
            if (!std::isfinite(n)) c.throw_error("TypeError", "ui.set(\"" + d.id + "\"): precisa de um numero");
            if (d.step > 0) n = d.min + std::round((n - d.min) / d.step) * d.step;
            s.number = std::clamp(n, d.min, d.max);
            return;
        }
        case ControlType::Choice: {
            const f64 n = c.to_number(v);
            if (!std::isfinite(n)) c.throw_error("TypeError", "ui.set(\"" + d.id + "\"): precisa do indice da opcao");
            s.number = std::clamp(std::floor(n), 0.0, static_cast<f64>(d.options.size() - 1));
            return;
        }
        case ControlType::Toggle: s.flag = Context::truthy(v); return;
        case ControlType::Text: {
            s.text = c.to_string(v);
            clip_utf8(s.text, d.maxLength);
            return;
        }
        case ControlType::Color: {
            if (v.is_array()) {
                const auto& el = v.as_object()->elements;
                f64 comp[4] = {0, 0, 0, 1};
                for (usize i = 0; i < el.size() && i < 4; ++i) comp[i] = c.to_number(el[i]);
                s.text = color_from_components(comp, el.size());
                return;
            }
            const std::string hex = normalize_color(c.to_string(v));
            if (hex.empty()) c.throw_error("TypeError", "ui.set(\"" + d.id + "\"): cor deve ser \"#RRGGBB\" ou [r, g, b] de 0 a 1");
            s.text = hex;
            return;
        }
        case ControlType::List: {
            if (v.is_nullish()) { s.hasSelection = false; s.text.clear(); return; }
            const std::string id = c.to_string(v);
            if (std::none_of(s.items.begin(), s.items.end(), [&](const auto& it) { return it.first == id; }))
                c.throw_error("RangeError", "ui.set(\"" + d.id + "\"): a lista nao tem o item \"" + id + "\"");
            s.hasSelection = true;
            s.text = id;
            return;
        }
        case ControlType::Label: {
            s.text = c.to_string(v);
            clip_utf8(s.text, 2000);
            s.textSet = true;
            return;
        }
        case ControlType::Button: {
            s.label = c.to_string(v);
            clip_utf8(s.label, 128);
            return;
        }
        case ControlType::Image:
            c.throw_error("TypeError", "ui.set(\"" + d.id + "\"): para imagem use ui.setImage");
    }
}

// --- estado em JSON ---------------------------------------------------------------------
std::string PluginRuntime::state_json() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_json_locked();
}

std::string PluginRuntime::state_json_locked() const {
    json::Writer w;
    w.begin_object();
    w.key("values").begin_object();
    for (const auto& [id, s] : controls_) {
        switch (s.decl->type) {
            case ControlType::Slider: case ControlType::Ruler: case ControlType::Choice: w.key(id).value(s.number); break;
            case ControlType::Toggle: w.key(id).value(s.flag); break;
            case ControlType::Text: case ControlType::Color: w.key(id).value(s.text); break;
            case ControlType::List:
                w.key(id);
                if (s.hasSelection) w.value(s.text);
                else w.begin_array().end_array();   // sem escolha (a interface lê "não é texto")
                break;
            default: break;
        }
    }
    w.end_object();
    w.key("items").begin_object();
    for (const auto& [id, s] : controls_) {
        if (s.decl->type != ControlType::List) continue;
        w.key(id).begin_array();
        for (usize i = 0; i < s.items.size(); ++i) {
            w.begin_object().key("id").value(s.items[i].first);
            if (s.itemsFromScript) {
                w.key("label").value(s.items[i].second);
                w.key("detail").value(i < s.details.size() ? s.details[i] : std::string());
            } else if (i < s.decl->items.size()) {
                w.key("label");
                write_text_map(w, s.decl->items[i].label);
                w.key("detail");
                write_text_map(w, s.decl->items[i].detail);
            }
            w.end_object();
        }
        w.end_array();
    }
    w.end_object();
    w.key("texts").begin_object();
    for (const auto& [id, s] : controls_) if (s.textSet) w.key(id).value(s.text);
    w.end_object();
    w.key("labels").begin_object();
    for (const auto& [id, s] : controls_) if (!s.label.empty()) w.key(id).value(s.label);
    w.end_object();
    w.key("disabled").begin_array();
    for (const auto& [id, s] : controls_) if (s.disabled) w.value(id);
    w.end_array();
    w.key("hidden").begin_array();
    for (const auto& [id, s] : controls_) if (s.hidden) w.value(id);
    w.end_array();
    w.key("images").begin_object();
    for (const auto& [id, s] : controls_) if (s.decl->type == ControlType::Image) w.key(id).value(s.image);
    w.end_object();
    w.key("progress");
    if (progress_ >= 0) w.value(progress_);
    else w.begin_array().end_array();
    w.key("progressText").value(progressText_);
    w.key("busy").value(busy_.load(std::memory_order_acquire));
    w.end_object();
    return w.str();
}

std::string PluginRuntime::result_json(bool ok, const std::string& valueJson, const ScriptError* error) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    std::string out = "{\"ok\":";
    out += ok ? "true" : "false";
    out += ",\"value\":";
    out += valueJson.empty() ? "null" : valueJson;
    json::Writer w;
    w.begin_object();
    if (error) {
        w.key("error").begin_object();
        w.key("name").value(error->name);
        w.key("message").value(error->message);
        w.key("text").value(error->text());
        w.key("file").value(error->file);
        w.key("line").value(error->line);
        w.key("column").value(error->column);
        w.key("aborted").value(error->aborted);
        w.end_object();
    }
    w.key("messages").begin_array();
    for (const Message& m : messages_) w.begin_object().key("kind").value(m.kind).key("text").value(m.text).end_object();
    w.end_array();
    w.key("openPanel").value(openPanel_);
    w.key("state");
    const std::string head = w.str();
    // `head` = {"error":...,"messages":[...],"openPanel":"...","state":   (sem fechar)
    out += ",";
    out += head.substr(1);
    out += state_json_locked();
    out += "}";
    return out;
}

// --- carga e chamada ----------------------------------------------------------------------
bool PluginRuntime::ensure_loaded(const ApiInstaller& install, ScriptError& error) {
    if (loaded_ && ctx_) return true;
    if (loadFailed_) {
        error = loadError_;
        return false;
    }
    try {
        ctx_ = std::make_unique<Context>(limits_);
        u64 seed = 1469598103934665603ull;
        for (const char ch : info_.id) seed = (seed ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
        ctx_->seed(seed);
        ctx_->onConsole = [this](int level, const std::string& t) { log(level, t); };
        install_runtime_api();
        if (install) install(*ctx_, *this);
    } catch (...) {
        ctx_.reset();
        error = ScriptError{};
        error.name = "LimitError";
        error.message = "sem memoria para iniciar o script";
        return false;
    }
    if (!info_.script.empty()) {
        std::vector<u8> bytes;
        const std::string path = package_file(info_.script);
        if (path.empty() || !fileio::read_all(path, bytes, kMaxScriptBytes + 1) || bytes.size() > kMaxScriptBytes) {
            error = ScriptError{};
            error.name = "Error";
            error.file = info_.script;
            error.message = "nao foi possivel ler o script do plugin";
            loadFailed_ = true;
            loadError_ = error;
            log(3, error.text());
            return false;
        }
        const std::string source(bytes.begin(), bytes.end());
        if (!ctx_->run(source, info_.script, error)) {
            loadFailed_ = true;
            loadError_ = error;
            log(3, "o script nao carregou: " + error.text());
            return false;
        }
        (void)ctx_->collect();
    }
    loaded_ = true;
    return true;
}

std::string PluginRuntime::run(const std::function<bool(Value&, ScriptError&)>& body, std::string_view label, std::string_view locale,
                               const ApiInstaller& install, const CallHooks& hooks) noexcept {
    std::unique_lock<std::mutex> lock(callMutex_, std::try_to_lock);
    ScriptError err;
    if (!lock.owns_lock()) {
        err.name = "BusyError";
        err.message = "o plugin ainda esta rodando a acao anterior";
        return result_json(false, "null", &err);
    }
    if (!info_.enabled) {
        err.name = "Error";
        err.message = "o plugin esta desligado";
        return result_json(false, "null", &err);
    }
    {
        std::lock_guard<std::mutex> state(stateMutex_);
        messages_.clear();
        openPanel_.clear();
        progress_ = -1.0;
        progressText_.clear();
        locale_ = std::string(locale.empty() ? std::string_view("pt-BR") : locale);
    }
    busy_.store(true, std::memory_order_release);
    struct Idle {
        std::atomic<bool>& b;
        ~Idle() { b.store(false, std::memory_order_release); }
    } idle{busy_};
    if (!ensure_loaded(install, err)) return result_json(false, "null", &err);
    if (hooks.begin) hooks.begin(std::string(label));
    Value result;
    bool ok = false;
    try {
        ok = body(result, err);
    } catch (...) {
        ok = false;
        err.name = "InternalError";
        err.message = "falha interna ao chamar o script";
    }
    if (hooks.end) hooks.end();
    std::string valueJson = "null";
    if (ok) {
        try {
            if (!result.is_undefined() && !result.is_function()) {
                std::string j = ctx_->json_stringify(result);
                if (!j.empty()) valueJson = std::move(j);
            }
        } catch (...) {
            valueJson = "null";
        }
    } else {
        log(3, err.text());
    }
    result = Value();
    (void)ctx_->collect();
    {
        std::lock_guard<std::mutex> state(stateMutex_);
        progress_ = -1.0;
        progressText_.clear();
    }
    busy_.store(false, std::memory_order_release);
    return result_json(ok, valueJson, ok ? nullptr : &err);
}

std::string PluginRuntime::call(std::string_view function, std::string_view argsJson, std::string_view locale,
                                const ApiInstaller& install, const CallHooks& hooks) noexcept {
    const std::string fn(function);
    const std::string args(argsJson);
    return run([&](Value& result, ScriptError& error) {
        std::vector<Value> values;
        if (!args.empty()) {
            try {
                const Value parsed = ctx_->json_parse(args);
                if (parsed.is_array()) values = parsed.as_object()->elements;
                else values.push_back(parsed);
            } catch (...) {
                error.name = "TypeError";
                error.message = "argumentos invalidos (JSON)";
                return false;
            }
        }
        return ctx_->call(fn, std::move(values), result, error);
    }, fn, locale, install, hooks);
}

std::string PluginRuntime::event(std::string_view controlId, std::string_view kind, std::string_view valueJson,
                                 std::string_view locale, const ApiInstaller& install, const CallHooks& hooks) noexcept {
    ScriptError err;
    const ControlDecl* decl = info_.ui.control(controlId);
    if (!decl) {
        err.name = "Error";
        err.message = "controle desconhecido: " + std::string(controlId);
        return result_json(false, "null", &err);
    }
    // O valor novo entra no estado sem precisar do script (controle sem função).
    json::Value jv;
    const bool hasValue = (kind == "change" || kind == "select") && !valueJson.empty() && json::parse(valueJson, jv);
    if (hasValue) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        ControlState* s = control(controlId);
        if (s && !s->disabled) {
            switch (decl->type) {
                case ControlType::Slider: case ControlType::Ruler:
                    if (jv.is_number() && std::isfinite(jv.number)) {
                        f64 n = jv.number;
                        if (decl->step > 0) n = decl->min + std::round((n - decl->min) / decl->step) * decl->step;
                        s->number = std::clamp(n, decl->min, decl->max);
                    }
                    break;
                case ControlType::Choice:
                    if (jv.is_number()) s->number = std::clamp(std::floor(jv.number), 0.0, static_cast<f64>(decl->options.size() - 1));
                    break;
                case ControlType::Toggle:
                    if (jv.type == json::Value::Type::Bool) s->flag = jv.boolean;
                    break;
                case ControlType::Text:
                    if (jv.is_string()) { s->text = jv.string; clip_utf8(s->text, decl->maxLength); }
                    break;
                case ControlType::Color:
                    if (jv.is_string()) { const std::string hex = normalize_color(jv.string); if (!hex.empty()) s->text = hex; }
                    break;
                case ControlType::List:
                    if (jv.is_string() && std::any_of(s->items.begin(), s->items.end(), [&](const auto& it) { return it.first == jv.string; })) {
                        s->hasSelection = true;
                        s->text = jv.string;
                    }
                    break;
                default: break;
            }
        }
    }
    bool disabled = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (const ControlState* s = control(controlId)) disabled = s->disabled;
    }
    if (disabled) {
        err.name = "Error";
        err.message = "controle desligado";
        return result_json(false, "null", &err);
    }
    const std::string& handler = kind == "click" ? decl->onClick : kind == "select" ? decl->onSelect : decl->onChange;
    if (handler.empty()) {
        { std::lock_guard<std::mutex> lock(stateMutex_); messages_.clear(); openPanel_.clear(); }
        return result_json(true, "null", nullptr);
    }
    const std::string id(controlId);
    return run([&](Value& result, ScriptError& error) {
        Value current;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (ControlState* s = control(id)) current = control_value(*ctx_, *s);
        }
        return ctx_->call(handler, {current, ctx_->str(id)}, result, error);
    }, handler, locale, install, hooks);
}

std::string PluginRuntime::open_panel(std::string_view panelId, std::string_view locale, const ApiInstaller& install,
                                      const CallHooks& hooks) noexcept {
    const PanelDecl* panel = info_.ui.panel(panelId);
    ScriptError err;
    if (!panel) {
        err.name = "Error";
        err.message = "painel desconhecido: " + std::string(panelId);
        return result_json(false, "null", &err);
    }
    if (panel->onOpen.empty()) {
        { std::lock_guard<std::mutex> lock(stateMutex_); messages_.clear(); openPanel_.clear(); }
        return result_json(true, "null", nullptr);
    }
    const std::string id(panelId);
    return run([&](Value& result, ScriptError& error) {
        return ctx_->call(panel->onOpen, {ctx_->str(id)}, result, error);
    }, panel->onOpen, locale, install, hooks);
}

// =============================================================================
// As APIs do runtime: ui, storage, fetch, clipboard, files, app (parte)
// =============================================================================
void PluginRuntime::install_runtime_api() {
    Context& c = *ctx_;

    // --- app (o motor acrescenta project, beginUndoGroup...) ---
    ObjectRef app = c.new_object();
    c.accessor(app, "language", [this](Context& cx, const Value&, std::vector<Value>&) { return cx.str(locale_); });
    c.constant(app, "version", c.str("0.3.4"));
    {
        ObjectRef plugin = c.new_object();
        c.constant(plugin, "id", c.str(info_.id));
        c.constant(plugin, "version", c.str(info_.version));
        c.accessor(plugin, "name", [this](Context& cx, const Value&, std::vector<Value>&) { return cx.str(pick_text(info_.names, locale_)); });
        plugin->frozen = true;
        c.constant(app, "plugin", Value::object(plugin));
    }
    c.method(app, "t", [this](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string key = cx.to_string(arg(a, 0));
        std::string t = text(key);
        if (t.empty()) return cx.str(key);
        for (usize i = 1; i < a.size() && i < 10; ++i) {
            const std::string mark = "{" + std::to_string(i - 1) + "}";
            const std::string with = cx.to_string(a[i]);
            for (usize at = t.find(mark); at != std::string::npos; at = t.find(mark, at + with.size())) t.replace(at, mark.size(), with);
        }
        return cx.str(std::move(t));
    });
    c.method(app, "alert", [this](Context& cx, const Value&, std::vector<Value>& a) {
        message("alert", cx.to_string(arg(a, 0)));
        return Value::undefined();
    });
    c.set_global("app", Value::object(app));

    // --- ui ---
    ObjectRef ui = c.new_object();
    auto need = [this](Context& cx, const Value& id, const char* fn) -> ControlState& {
        const std::string key = cx.to_string(id);
        ControlState* s = control(key);
        if (!s) cx.throw_error("TypeError", std::string("ui.") + fn + ": o plugin nao tem o controle \"" + key + "\"");
        return *s;
    };
    c.method(ui, "get", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return control_value(cx, need(cx, arg(a, 0), "get"));
    });
    c.method(ui, "set", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        set_control_value(cx, need(cx, arg(a, 0), "set"), arg(a, 1));
        return Value::undefined();
    });
    c.method(ui, "setItems", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        const Value list = arg(a, 1);
        if (!list.is_array()) cx.throw_error("TypeError", "ui.setItems: precisa de uma lista");
        std::vector<std::pair<std::string, std::string>> items;
        std::vector<std::string> details;
        for (const Value& v : list.as_object()->elements) {
            if (items.size() >= kMaxListItems) break;
            std::string id, label, detail;
            if (v.is_object() && !v.is_array()) {
                id = cx.to_string(cx.get(v, "id"));
                label = cx.to_string(cx.get(v, "label"));
                const Value d = cx.get(v, "detail");
                if (!d.is_nullish()) detail = cx.to_string(d);
            } else {
                id = label = cx.to_string(v);
            }
            clip_utf8(id, 64);
            clip_utf8(label, 200);
            clip_utf8(detail, 300);
            items.emplace_back(std::move(id), std::move(label));
            details.push_back(std::move(detail));
        }
        std::lock_guard<std::mutex> lock(stateMutex_);
        ControlState& s = need(cx, arg(a, 0), "setItems");
        if (s.decl->type != ControlType::List) cx.throw_error("TypeError", "ui.setItems: \"" + s.decl->id + "\" nao e uma lista");
        s.items = std::move(items);
        s.details = std::move(details);
        s.itemsFromScript = true;
        if (s.hasSelection && std::none_of(s.items.begin(), s.items.end(), [&](const auto& it) { return it.first == s.text; })) {
            s.hasSelection = false;
            s.text.clear();
        }
        return Value::undefined();
    });
    c.method(ui, "setText", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        ControlState& s = need(cx, arg(a, 0), "setText");
        if (s.decl->type == ControlType::Label) {
            s.text = cx.to_string(arg(a, 1));
            clip_utf8(s.text, 2000);
            s.textSet = true;
        } else {
            set_control_value(cx, s, arg(a, 1));
        }
        return Value::undefined();
    });
    c.method(ui, "setLabel", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        ControlState& s = need(cx, arg(a, 0), "setLabel");
        s.label = arg(a, 1).is_nullish() ? std::string() : cx.to_string(arg(a, 1));
        clip_utf8(s.label, 128);
        return Value::undefined();
    });
    c.method(ui, "setEnabled", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        need(cx, arg(a, 0), "setEnabled").disabled = !Context::truthy(a.size() > 1 ? a[1] : Value::boolean(true));
        return Value::undefined();
    });
    c.method(ui, "setVisible", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        need(cx, arg(a, 0), "setVisible").hidden = !Context::truthy(a.size() > 1 ? a[1] : Value::boolean(true));
        return Value::undefined();
    });
    c.method(ui, "setImage", [this, need](Context& cx, const Value&, std::vector<Value>& a) {
        std::string path;
        const Value src = arg(a, 1);
        if (src.is_object() && src.as_object()->hostTag == kHostTagFrame && src.as_object()->hostData) {
            path = *static_cast<const std::string*>(src.as_object()->hostData.get());
        } else if (src.is_string()) {
            const std::string ext = lower(src.as_string().substr(src.as_string().rfind('.') == std::string::npos ? src.as_string().size() : src.as_string().rfind('.')));
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") cx.throw_error("TypeError", "ui.setImage: so .png ou .jpg do pacote");
            path = package_file(src.as_string());
            if (path.empty()) cx.throw_error("TypeError", "ui.setImage: o pacote nao tem \"" + src.as_string() + "\"");
        } else if (!src.is_nullish()) {
            cx.throw_error("TypeError", "ui.setImage: use um arquivo do pacote, um quadro de comp.renderFrame() ou null");
        }
        std::lock_guard<std::mutex> lock(stateMutex_);
        ControlState& s = need(cx, arg(a, 0), "setImage");
        if (s.decl->type != ControlType::Image) cx.throw_error("TypeError", "ui.setImage: \"" + s.decl->id + "\" nao e uma imagem");
        s.image = std::move(path);
        return Value::undefined();
    });
    c.method(ui, "toast", [this](Context& cx, const Value&, std::vector<Value>& a) {
        message("toast", cx.to_string(arg(a, 0)));
        return Value::undefined();
    });
    c.method(ui, "alert", [this](Context& cx, const Value&, std::vector<Value>& a) {
        message("alert", cx.to_string(arg(a, 0)));
        return Value::undefined();
    });
    c.method(ui, "openPanel", [this](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string id = cx.to_string(arg(a, 0));
        const PanelDecl* p = info_.ui.panel(id);
        if (!p) cx.throw_error("TypeError", "ui.openPanel: o plugin nao tem o painel \"" + id + "\"");
        std::lock_guard<std::mutex> lock(stateMutex_);
        openPanel_ = id;
        return Value::undefined();
    });
    c.method(ui, "progress", [this](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        const Value v = arg(a, 0);
        progress_ = v.is_nullish() ? -1.0 : std::clamp(cx.to_number(v), 0.0, 1.0);
        progressText_ = arg(a, 1).is_nullish() ? std::string() : cx.to_string(arg(a, 1));
        clip_utf8(progressText_, 200);
        return Value::undefined();
    });
    c.set_global("ui", Value::object(ui));

    // --- storage: chave-valor do plugin (JSON, 256 KB, persistente) ---
    ObjectRef storage = c.new_object();
    c.method(storage, "get", [this](Context& cx, const Value&, std::vector<Value>& a) {
        load_storage();
        const Value all = cx.json_parse(storage_);
        const Value v = cx.get(all, cx.to_string(arg(a, 0)));
        return v.is_undefined() ? (a.size() > 1 ? a[1] : Value::null()) : v;
    });
    auto write = [this](Context& cx, const std::string& key, const Value* v) {
        load_storage();
        const Value all = cx.json_parse(storage_);
        if (v) cx.set(all, key, *v);
        else all.as_object()->props.erase(key);
        std::string text = cx.json_stringify(all);
        if (text.size() > kMaxStorageBytes) cx.throw_error("RangeError", "storage passou de 256 KB");
        storage_ = std::move(text);
        save_storage();
    };
    c.method(storage, "set", [write](Context& cx, const Value&, std::vector<Value>& a) {
        const Value v = arg(a, 1);
        if (v.is_function()) cx.throw_error("TypeError", "storage.set: funcoes nao sao guardadas");
        // Ida e volta pelo JSON: o que fica é o que dá para gravar.
        const std::string text = cx.json_stringify(v);
        const Value copy = text.empty() ? Value::null() : cx.json_parse(text);
        write(cx, cx.to_string(arg(a, 0)), &copy);
        return Value::undefined();
    });
    c.method(storage, "remove", [write](Context& cx, const Value&, std::vector<Value>& a) {
        write(cx, cx.to_string(arg(a, 0)), nullptr);
        return Value::undefined();
    });
    c.method(storage, "keys", [this](Context& cx, const Value&, std::vector<Value>&) {
        load_storage();
        const Value all = cx.json_parse(storage_);
        std::vector<Value> keys;
        for (const auto& [k, p] : all.as_object()->props.entries()) if (!p.hidden) keys.push_back(cx.str(k));
        return Value::object(cx.new_array(std::move(keys)));
    });
    c.set_global("storage", Value::object(storage));

    // --- fetch (HTTPS, domínios declarados e concedidos) ---
    ObjectRef responseProto = c.new_object();
    c.method(responseProto, "text", [](Context& cx, const Value& self, std::vector<Value>&) {
        if (!self.is_object()) cx.throw_error("TypeError", "text() da resposta");
        const script::Prop* body = self.as_object()->props.find("@body");
        return body ? body->value : cx.str("");
    });
    c.method(responseProto, "json", [](Context& cx, const Value& self, std::vector<Value>&) {
        if (!self.is_object()) cx.throw_error("TypeError", "json() da resposta");
        const script::Prop* body = self.as_object()->props.find("@body");
        return cx.json_parse(body ? body->value.as_string() : std::string());
    });
    c.add_root(Value::object(responseProto));
    std::weak_ptr<script::Object> weakResponse = responseProto;
    c.set_global("fetch", Value::object(c.new_native("fetch", [this, weakResponse](Context& cx, const Value&, std::vector<Value>& a) {
        if (info_.permissions.network.empty())
            cx.throw_error("PermissionError", "fetch: o plugin nao declarou \"permissions.network\" no plugin.json");
        if (!(granted_ & kPermNetwork))
            cx.throw_error("PermissionError", "fetch: a pessoa nao concedeu acesso a rede para este plugin (Plugins > permissoes)");
        const std::string url = cx.to_string(arg(a, 0));
        if (lower(url.substr(0, 8)) != "https://") cx.throw_error("PermissionError", "fetch: so endereco https:// e aceito");
        const usize end = url.find_first_of("/?#", 8);
        const std::string authority = url.substr(8, end == std::string::npos ? std::string::npos : end - 8);
        if (authority.find('@') != std::string::npos) cx.throw_error("PermissionError", "fetch: endereco com usuario@ nao e aceito");
        if (authority.find(':') != std::string::npos) cx.throw_error("PermissionError", "fetch: porta explicita nao e aceita");
        const std::string host = lower(authority);
        if (host.empty()) cx.throw_error("TypeError", "fetch: endereco sem dominio");
        const bool allowed = std::any_of(info_.permissions.network.begin(), info_.permissions.network.end(), [&](const std::string& d) {
            return host == d || (host.size() > d.size() && host.compare(host.size() - d.size(), d.size(), d) == 0
                                 && host[host.size() - d.size() - 1] == '.');
        });
        if (!allowed) cx.throw_error("PermissionError", "fetch: o dominio \"" + host + "\" nao esta em permissions.network do plugin");
        HttpRequest req;
        req.url = url;
        const Value opts = arg(a, 1);
        bool hasContentType = false;
        if (opts.is_object()) {
            const Value method = cx.get(opts, "method");
            if (!method.is_nullish()) {
                std::string m = cx.to_string(method);
                std::transform(m.begin(), m.end(), m.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                if (m != "GET" && m != "POST" && m != "PUT" && m != "PATCH" && m != "DELETE" && m != "HEAD")
                    cx.throw_error("TypeError", "fetch: metodo \"" + m + "\" nao aceito");
                req.method = m;
            }
            const Value headers = cx.get(opts, "headers");
            if (headers.is_object()) {
                usize total = 0;
                for (const auto& [k, p] : headers.as_object()->props.entries()) {
                    if (p.hidden) continue;
                    if (req.headers.size() >= 32) cx.throw_error("RangeError", "fetch: mais de 32 cabecalhos");
                    std::string v = cx.to_string(p.value);
                    if (k.find_first_of("\r\n:") != std::string::npos || v.find_first_of("\r\n") != std::string::npos)
                        cx.throw_error("TypeError", "fetch: cabecalho invalido");
                    total += k.size() + v.size();
                    if (lower(k) == "content-type") hasContentType = true;
                    req.headers.emplace_back(k, std::move(v));
                }
                if (total > 16 * 1024) cx.throw_error("RangeError", "fetch: cabecalhos grandes demais");
            }
            const Value body = cx.get(opts, "body");
            if (!body.is_nullish()) {
                if (body.is_string()) req.body = body.as_string();
                else {
                    req.body = cx.json_stringify(body);
                    if (!hasContentType) req.headers.emplace_back("Content-Type", "application/json");
                }
                if (req.body.size() > kMaxFetchBodyOut) cx.throw_error("RangeError", "fetch: corpo maior que 256 KB");
            }
            const Value timeout = cx.get(opts, "timeout");
            if (!timeout.is_nullish()) req.timeoutMs = static_cast<u32>(std::clamp(cx.to_number(timeout), 1000.0, static_cast<f64>(kMaxFetchTimeoutMs)));
        }
        std::shared_ptr<const HostIO> io;
        {
            std::lock_guard<std::mutex> lock(ioMutex_);
            io = io_;
        }
        if (!io || !io->fetch) cx.throw_error("NetworkError", "fetch: rede indisponivel neste app");
        HttpResponse resp;
        {
            struct Clock {
                Context& c;
                explicit Clock(Context& cc) : c(cc) { c.pause_clock(); }
                ~Clock() { c.resume_clock(); }
            } clock(cx);
            try {
                resp = io->fetch(req);
            } catch (...) {
                resp.ok = false;
                resp.error = "falha na plataforma";
            }
        }
        log(1, "fetch " + req.method + " " + host + " -> " + (resp.ok ? std::to_string(resp.status) : "erro: " + resp.error));
        if (!resp.ok) cx.throw_error("NetworkError", "fetch: " + (resp.error.empty() ? std::string("sem resposta") : resp.error));
        if (resp.body.size() > req.maxBytes) cx.throw_error("NetworkError", "fetch: resposta maior que 2 MB");
        ObjectRef r = cx.new_object_with(weakResponse.lock());
        r->props.put("ok").value = Value::boolean(resp.status >= 200 && resp.status < 300);
        r->props.put("status").value = Value::number(resp.status);
        r->props.put("url").value = cx.str(resp.url.empty() ? url : resp.url);
        ObjectRef headers = cx.new_object();
        for (const auto& [k, v] : resp.headers) headers->props.put(lower(k)).value = cx.str(v);
        r->props.put("headers").value = Value::object(headers);
        script::Prop& body = r->props.put("@body");
        body.value = cx.str(std::move(resp.body));
        body.hidden = true;
        r->grow(512);
        return Value::object(r);
    })));

    // --- clipboard ---
    ObjectRef clip = c.new_object();
    c.method(clip, "readText", [this](Context& cx, const Value&, std::vector<Value>&) {
        if (!(granted_ & kPermClipboard)) cx.throw_error("PermissionError", "clipboard: permissao nao concedida");
        std::shared_ptr<const HostIO> io;
        { std::lock_guard<std::mutex> lock(ioMutex_); io = io_; }
        std::string text;
        if (!io || !io->clipboardRead || !io->clipboardRead(text)) return cx.str("");
        clip_utf8(text, 1u << 20);
        return cx.str(std::move(text));
    });
    c.method(clip, "writeText", [this](Context& cx, const Value&, std::vector<Value>& a) {
        if (!(granted_ & kPermClipboard)) cx.throw_error("PermissionError", "clipboard: permissao nao concedida");
        std::shared_ptr<const HostIO> io;
        { std::lock_guard<std::mutex> lock(ioMutex_); io = io_; }
        std::string text = cx.to_string(arg(a, 0));
        if (text.size() > (1u << 20)) cx.throw_error("RangeError", "clipboard: texto maior que 1 MB");
        return Value::boolean(io && io->clipboardWrite && io->clipboardWrite(text));
    });
    c.set_global("clipboard", Value::object(clip));

    // --- files: só o que a pessoa escolher no seletor ---
    ObjectRef fileProto = c.new_object();
    auto read_file = [](Context& cx, const Value& self) -> std::string {
        if (!self.is_object()) cx.throw_error("TypeError", "metodo de arquivo");
        const script::Prop* p = self.as_object()->props.find("@path");
        std::vector<u8> bytes;
        if (!p || !fileio::read_all(p->value.as_string(), bytes, kMaxPickedFileBytes + 1))
            cx.throw_error("Error", "nao foi possivel ler o arquivo escolhido");
        if (bytes.size() > kMaxPickedFileBytes) cx.throw_error("RangeError", "arquivo maior que 1 MB");
        return std::string(bytes.begin(), bytes.end());
    };
    c.method(fileProto, "text", [read_file](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(read_file(cx, self)); });
    c.method(fileProto, "json", [read_file](Context& cx, const Value& self, std::vector<Value>&) { return cx.json_parse(read_file(cx, self)); });
    c.add_root(Value::object(fileProto));
    std::weak_ptr<script::Object> weakFile = fileProto;
    ObjectRef files = c.new_object();
    c.method(files, "pick", [this, weakFile](Context& cx, const Value&, std::vector<Value>& a) {
        if (!(granted_ & kPermFiles)) cx.throw_error("PermissionError", "files: permissao nao concedida");
        std::vector<std::string> types;
        const Value opts = arg(a, 0);
        if (opts.is_object()) {
            const Value t = cx.get(opts, "types");
            if (t.is_array())
                for (const Value& v : t.as_object()->elements) if (types.size() < 16) types.push_back(lower(cx.to_string(v)));
        }
        std::shared_ptr<const HostIO> io;
        { std::lock_guard<std::mutex> lock(ioMutex_); io = io_; }
        if (!io || !io->pickFile) cx.throw_error("Error", "files.pick: seletor indisponivel neste app");
        PickedFile picked;
        {
            struct Clock {
                Context& c;
                explicit Clock(Context& cc) : c(cc) { c.pause_clock(); }
                ~Clock() { c.resume_clock(); }
            } clock(cx);
            try {
                picked = io->pickFile(info_.id, types);
            } catch (...) {
                picked.ok = false;
            }
        }
        if (!picked.ok) {
            if (!picked.error.empty()) cx.throw_error("Error", "files.pick: " + picked.error);
            return Value::null();
        }
        ObjectRef f = cx.new_object_with(weakFile.lock());
        f->props.put("name").value = cx.str(picked.name);
        f->props.put("size").value = Value::number(static_cast<f64>(picked.size));
        script::Prop& p = f->props.put("@path");
        p.value = cx.str(picked.path);
        p.hidden = true;
        f->grow(256);
        return Value::object(f);
    });
    c.set_global("files", Value::object(files));
}

} // namespace aurea::plugins
