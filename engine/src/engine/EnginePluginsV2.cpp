// =============================================================================
//  Aurea / engine / EnginePluginsV2.cpp
//
//  0.3.3 plugins: os plugins do formato 2 — painéis declarados, scripts e
//  APIs. Aqui moram as funções do Engine que a interface chama
//  (plugin_call, plugin_event...) e a API de OBJETOS do AUREA que o script vê,
//  no estilo dos scripts do After Effects:
//
//      app.beginUndoGroup("Gerar")
//      const comp = app.project.activeItem
//      const t = comp.layers.addText("Olá")
//      t.position.setValueAtTime(0, [100, 540])
//      t.position.setValueAtTime(1, [960, 540])
//      app.endUndoGroup()
//
//  Regra: toda alteração passa pelos comandos e pelas funções do motor que já
//  gravam desfazer (apply_command, add_text, edit_marker...). Cada chamada da
//  interface é UM passo de desfazer (grupo aberto em volta da chamada; grupos
//  que o script esqueceu abertos fecham no fim). Leituras pegam o modelo sob
//  modelMutex_ e soltam antes de chamar qualquer função pública do motor.
//
//  Tempos em segundos da composição; índice de camada a partir de 1, de cima
//  para baixo; unidades do AE (posição em px, escala e opacidade em %, graus).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/plugins/PluginRuntime.hpp"
#include "aurea/plugins/Script.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "../plugins/PluginInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace aurea {

using plugins::script::Context;
using plugins::script::ObjectRef;
using plugins::script::Value;

struct Engine::PluginScriptState {
    std::mutex mutex;                                            ///< o mapa e o io
    std::unordered_map<std::string, std::shared_ptr<plugins::PluginRuntime>> runtimes;
    std::shared_ptr<const plugins::HostIO> io;
    /// Uma chamada de script por vez no motor inteiro: o grupo de desfazer é global.
    std::mutex callMutex;
    u32 scriptGroups = 0;                                        ///< beginUndoGroup sem end (sob callMutex)
    u64 frameCounter = 0;
};

namespace {

constexpr u32 kTagComp = 0xC0A1u;
constexpr u32 kTagLayer = 0x1A7Eu;
constexpr u32 kTagProp = 0x9607u;
constexpr u32 kTagEffect = 0xEFFEu;

Value arg(const std::vector<Value>& a, usize i) { return i < a.size() ? a[i] : Value(); }

std::string error_json(const std::string& name, const std::string& message) {
    json::Writer w;
    w.begin_object();
    w.key("error").begin_object().key("name").value(name).key("message").value(message).key("text").value(name + ": " + message)
        .key("file").value("").key("line").value(0u).key("column").value(0u).key("aborted").value(false).end_object();
    w.key("messages").begin_array().end_array();
    w.key("openPanel").value("");
    w.end_object();
    // {"ok":false,"value":null, ...} — o Writer não escreve null.
    return "{\"ok\":false,\"value\":null," + w.str().substr(1);
}

const char* kind_text(LayerKind k) {
    switch (k) {
        case LayerKind::Video: return "video";
        case LayerKind::Image: return "image";
        case LayerKind::Audio: return "audio";
        case LayerKind::Text: return "text";
        case LayerKind::Shape: return "shape";
        case LayerKind::Null: return "null";
        case LayerKind::Adjustment: return "adjustment";
        case LayerKind::Camera: return "camera";
        case LayerKind::Light: return "light";
        case LayerKind::Model3D: return "model3d";
        case LayerKind::ParticleSystem: return "particles";
        case LayerKind::Composition: return "group";
        default: return "unknown";
    }
}

/// As propriedades de transform que o script alcança (código → trilhas).
struct PropDef {
    const char* id;          ///< nome no script ("position")
    const char* aeName;      ///< nome do AE ("Position")
    TrackProperty first;     ///< trilha do componente 0
    u32 dims;                ///< componentes (posição/escala/âncora: 2, ou 3 em camada 3D)
    f32 scale;               ///< valor do script = motor × scale
};
constexpr PropDef kProps[] = {
    {"position", "Position", TrackProperty::PositionX, 2, 1.0f},
    {"scale", "Scale", TrackProperty::ScaleX, 2, 100.0f},
    {"rotation", "Rotation", TrackProperty::RotationZ, 1, 1.0f},
    {"opacity", "Opacity", TrackProperty::Opacity, 1, 100.0f},
    {"anchorPoint", "Anchor Point", TrackProperty::AnchorX, 2, 1.0f},
    {"rotationX", "X Rotation", TrackProperty::RotationX, 1, 1.0f},
    {"rotationY", "Y Rotation", TrackProperty::RotationY, 1, 1.0f},
    {"rotationZ", "Z Rotation", TrackProperty::RotationZ, 1, 1.0f},
};
constexpr i64 kEffectPropCode = 100;

i64 prop_code(std::string_view name) {
    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::erase(n, ' ');
    std::erase(n, '_');
    for (i64 i = 0; i < static_cast<i64>(std::size(kProps)); ++i) {
        std::string a = kProps[i].id, b = kProps[i].aeName;
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::erase(b, ' ');
        if (n == a || n == b) return i;
    }
    if (n == "anchor") return 4;
    if (n == "zrotation") return 7;
    return -1;
}

f32 static_component(const Layer& l, TrackProperty p) {
    const Transform& t = l.transform;
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
        default: return 0.0f;
    }
}

f32 component_at(const Layer& l, TrackProperty p, FrameIndex local) {
    const Track* t = l.tracks.find(p);
    if (t && !t->keys.empty()) return t->sample(local);
    return static_component(l, p);
}

/// Grava um PNG RGBA8 sem compressão (blocos "stored" do deflate).
bool write_png(const std::string& path, const std::vector<u8>& rgba, u32 w, u32 h) {
    static u32 table[256];
    static bool ready = false;
    if (!ready) {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    auto crc = [&](const u8* d, usize n, u32 c = 0xFFFFFFFFu) {
        for (usize i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
        return c;
    };
    std::vector<u8> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto be32 = [&](std::vector<u8>& v, u32 x) {
        v.push_back(static_cast<u8>(x >> 24)); v.push_back(static_cast<u8>(x >> 16));
        v.push_back(static_cast<u8>(x >> 8)); v.push_back(static_cast<u8>(x));
    };
    auto chunk = [&](const char* type, const std::vector<u8>& data) {
        be32(out, static_cast<u32>(data.size()));
        std::vector<u8> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        be32(out, crc(body.data(), body.size()) ^ 0xFFFFFFFFu);
    };
    std::vector<u8> ihdr;
    be32(ihdr, w);
    be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", ihdr);
    std::vector<u8> raw;
    raw.reserve(static_cast<usize>(h) * (static_cast<usize>(w) * 4 + 1));
    for (u32 y = 0; y < h; ++y) {
        raw.push_back(0);
        const u8* row = rgba.data() + static_cast<usize>(y) * w * 4;
        raw.insert(raw.end(), row, row + static_cast<usize>(w) * 4);
    }
    std::vector<u8> z = {0x78, 0x01};
    u32 a = 1, b = 0;
    for (const u8 v : raw) { a = (a + v) % 65521u; b = (b + a) % 65521u; }
    for (usize at = 0; at < raw.size() || at == 0;) {
        const usize n = std::min<usize>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<u8>(n & 0xFF)); z.push_back(static_cast<u8>(n >> 8));
        z.push_back(static_cast<u8>(~n & 0xFF)); z.push_back(static_cast<u8>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
        if (last) break;
    }
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    return fileio::write_atomic(path, out.data(), out.size()).ok();
}

} // namespace

// =============================================================================
// Funções do Engine que a interface chama
// =============================================================================
std::shared_ptr<plugins::PluginRuntime> Engine::plugin_runtime_for(const char* id, std::string& error) noexcept {
    try {
        if (!id || !*id || !plugins_) { error = "plugin desconhecido"; return nullptr; }
        if (!pluginScripts_) pluginScripts_ = std::make_shared<PluginScriptState>();
        plugins::PluginInfo info;
        PluginScriptState& st = *pluginScripts_;
        if (!plugins_->find(id, info)) {
            std::lock_guard<std::mutex> lock(st.mutex);
            st.runtimes.erase(id);
            error = "o plugin nao esta instalado";
            return nullptr;
        }
        if (!info.ok) { error = "o plugin nao carregou: " + info.error; return nullptr; }
        if (!info.enabled) { error = "o plugin esta desligado"; return nullptr; }
        std::lock_guard<std::mutex> lock(st.mutex);
        std::shared_ptr<plugins::PluginRuntime>& rt = st.runtimes[info.id];
        if (!rt || rt->info().generation != info.generation)
            rt = std::make_shared<plugins::PluginRuntime>(info, plugins_->data_directory());
        rt->set_granted(info.granted);
        rt->set_io(st.io);
        return rt;
    } catch (...) {
        error = "sem memoria para o plugin";
        return nullptr;
    }
}

std::string Engine::plugin_run(const char* id, const std::function<std::string(plugins::PluginRuntime&,
                               const plugins::PluginRuntime::ApiInstaller&, const plugins::CallHooks&)>& body) noexcept {
    try {
        std::string error;
        std::shared_ptr<plugins::PluginRuntime> rt = plugin_runtime_for(id, error);
        if (!rt) return error_json("Error", error);
        std::shared_ptr<PluginScriptState> st = pluginScripts_;
        if (rt->busy()) return error_json("BusyError", "o plugin ainda esta rodando a acao anterior");
        std::lock_guard<std::mutex> serial(st->callMutex);
        st->scriptGroups = 0;
        plugins::CallHooks hooks;
        hooks.begin = [this](const std::string& label) {
            std::lock_guard<std::mutex> lock(modelMutex_);
            history_.begin_group(("plugin " + label).c_str());
        };
        hooks.end = [this, st] {
            std::lock_guard<std::mutex> lock(modelMutex_);
            for (; st->scriptGroups > 0; --st->scriptGroups) history_.end_group();
            history_.end_group();
        };
        plugins::PluginRuntime* raw = rt.get();
        const plugins::PluginRuntime::ApiInstaller install = [this, raw](Context& ctx, plugins::PluginRuntime&) {
            install_plugin_api(ctx, *raw);
        };
        std::string out = body(*rt, install, hooks);
        request_render();
        return out;
    } catch (...) {
        return error_json("InternalError", "falha interna ao rodar o plugin");
    }
}

std::string Engine::inspect_plugin(const char* path) noexcept {
    if (!path || !*path || !plugins_) return error_json("Error", "caminho vazio");
    return plugins_->inspect_json(path);
}

bool Engine::set_plugin_enabled(const char* id, bool on) noexcept {
    if (!id || !plugins_) return false;
    const bool ok = plugins_->set_enabled(id, on);
    if (ok && !on && pluginScripts_) {
        std::lock_guard<std::mutex> lock(pluginScripts_->mutex);
        auto it = pluginScripts_->runtimes.find(id);
        if (it != pluginScripts_->runtimes.end()) {
            it->second->cancel();
            pluginScripts_->runtimes.erase(it);
        }
    }
    return ok;
}

bool Engine::set_plugin_permissions(const char* id, u32 granted) noexcept {
    if (!id || !plugins_) return false;
    const bool ok = plugins_->set_granted(id, granted);
    if (ok && pluginScripts_) {
        plugins::PluginInfo info;
        if (plugins_->find(id, info)) {
            std::lock_guard<std::mutex> lock(pluginScripts_->mutex);
            auto it = pluginScripts_->runtimes.find(id);
            if (it != pluginScripts_->runtimes.end()) it->second->set_granted(info.granted);
        }
    }
    return ok;
}

std::string Engine::plugin_call(const char* id, const char* function, const char* argsJson, const char* locale) noexcept {
    const std::string fn = function ? function : "";
    const std::string args = argsJson ? argsJson : "";
    const std::string loc = locale ? locale : "";
    return plugin_run(id, [&](plugins::PluginRuntime& rt, const plugins::PluginRuntime::ApiInstaller& install, const plugins::CallHooks& hooks) {
        return rt.call(fn, args, loc, install, hooks);
    });
}

std::string Engine::plugin_event(const char* id, const char* control, const char* kind, const char* valueJson,
                                 const char* locale) noexcept {
    const std::string c = control ? control : "", k = kind ? kind : "", v = valueJson ? valueJson : "", loc = locale ? locale : "";
    return plugin_run(id, [&](plugins::PluginRuntime& rt, const plugins::PluginRuntime::ApiInstaller& install, const plugins::CallHooks& hooks) {
        return rt.event(c, k, v, loc, install, hooks);
    });
}

std::string Engine::plugin_open_panel(const char* id, const char* panel, const char* locale) noexcept {
    const std::string p = panel ? panel : "", loc = locale ? locale : "";
    return plugin_run(id, [&](plugins::PluginRuntime& rt, const plugins::PluginRuntime::ApiInstaller& install, const plugins::CallHooks& hooks) {
        return rt.open_panel(p, loc, install, hooks);
    });
}

std::string Engine::plugin_state(const char* id) noexcept {
    try {
        std::string error;
        const auto rt = plugin_runtime_for(id, error);
        return rt ? rt->state_json() : std::string("{}");
    } catch (...) {
        return "{}";
    }
}

std::string Engine::plugin_console(const char* id) noexcept {
    try {
        if (!pluginScripts_ || !id) return "[]";
        std::shared_ptr<plugins::PluginRuntime> rt;
        {
            std::lock_guard<std::mutex> lock(pluginScripts_->mutex);
            auto it = pluginScripts_->runtimes.find(id);
            if (it != pluginScripts_->runtimes.end()) rt = it->second;
        }
        return rt ? rt->console_json() : std::string("[]");
    } catch (...) {
        return "[]";
    }
}

void Engine::plugin_clear_console(const char* id) noexcept {
    try {
        if (!pluginScripts_ || !id) return;
        std::lock_guard<std::mutex> lock(pluginScripts_->mutex);
        auto it = pluginScripts_->runtimes.find(id);
        if (it != pluginScripts_->runtimes.end()) it->second->clear_console();
    } catch (...) {}
}

void Engine::plugin_cancel(const char* id) noexcept {
    try {
        if (!pluginScripts_ || !id) return;
        std::lock_guard<std::mutex> lock(pluginScripts_->mutex);
        auto it = pluginScripts_->runtimes.find(id);
        if (it != pluginScripts_->runtimes.end()) it->second->cancel();
    } catch (...) {}
}

void Engine::plugin_reload(const char* id) noexcept {
    try {
        if (!pluginScripts_ || !id) return;
        std::shared_ptr<plugins::PluginRuntime> rt;
        {
            std::lock_guard<std::mutex> lock(pluginScripts_->mutex);
            auto it = pluginScripts_->runtimes.find(id);
            if (it != pluginScripts_->runtimes.end()) rt = it->second;
        }
        if (rt) rt->reload();
    } catch (...) {}
}

void Engine::set_plugin_host_io(plugins::HostIO io) noexcept {
    try {
        if (!pluginScripts_) pluginScripts_ = std::make_shared<PluginScriptState>();
        auto shared = std::make_shared<const plugins::HostIO>(std::move(io));
        std::lock_guard<std::mutex> lock(pluginScripts_->mutex);
        pluginScripts_->io = shared;
        for (auto& [id, rt] : pluginScripts_->runtimes) rt->set_io(shared);
    } catch (...) {}
}

// =============================================================================
// A API de objetos do AUREA no script
// =============================================================================
void Engine::install_plugin_api(Context& c, plugins::PluginRuntime& rt) {
    Engine* const self = this;
    plugins::PluginRuntime* const runtime = &rt;
    const std::shared_ptr<PluginScriptState> state = pluginScripts_;
    PluginScriptState* const st = state.get();

    // --- protótipos (raízes do contexto) ---
    ObjectRef compProto = c.new_object();
    ObjectRef layerProto = c.new_object();
    ObjectRef propProto = c.new_object();
    ObjectRef effectProto = c.new_object();
    ObjectRef collProto = c.new_object();
    ObjectRef frameProto = c.new_object();
    for (const ObjectRef* p : {&compProto, &layerProto, &propProto, &effectProto, &collProto, &frameProto}) c.add_root(Value::object(*p));
    const std::weak_ptr<plugins::script::Object> wComp = compProto, wLayer = layerProto, wProp = propProto, wEffect = effectProto,
                                                 wColl = collProto, wFrame = frameProto;

    // --- utilidades (rodam com o modelo travado só dentro de `with_*`) ---
    auto fail = [](Context& cx, const Status& s, const std::string& what) {
        const std::string why = s.detail().empty() ? std::string(to_string(s.code())) : std::string(s.detail());
        cx.throw_error("Error", what + ": " + why);
    };
    auto apply = [self, fail](Context& cx, const Command& cmd, const std::string& what, const char* text = nullptr) {
        const Status s = self->apply_command(cmd, text);
        if (!s.ok()) fail(cx, s, what);
    };
    auto with_comp = [self](Context& cx, auto&& f) {
        std::lock_guard<std::mutex> lock(self->modelMutex_);
        Composition* comp = self->project_ ? self->current_composition() : nullptr;
        if (!comp) cx.throw_error("Error", "nenhum projeto aberto");
        return f(*comp);
    };
    auto with_layer = [self](Context& cx, u64 id, auto&& f) {
        std::lock_guard<std::mutex> lock(self->modelMutex_);
        Composition* comp = self->project_ ? self->current_composition() : nullptr;
        if (!comp) cx.throw_error("Error", "nenhum projeto aberto");
        Layer* l = comp->layer(LayerId::unpack(id));
        if (!l) cx.throw_error("ReferenceError", "a camada nao existe mais (foi apagada ou o projeto mudou)");
        return f(*comp, *l);
    };
    auto fps_of = [](const Composition& comp) { return comp.fps() > 0.0 ? comp.fps() : 30.0; };
    auto seconds_to_frame = [](Context& cx, const Value& v, f64 fps) {
        const f64 s = cx.to_number(v);
        if (!std::isfinite(s)) cx.throw_error("TypeError", "tempo invalido (use segundos)");
        return static_cast<i64>(std::llround(s * fps));
    };
    auto host = [](Context& cx, const Value& self, u32 tag, const char* what) -> const ObjectRef& {
        if (!self.is_object() || self.as_object()->hostTag != tag) cx.throw_error("TypeError", std::string(what) + ": chamado fora do objeto");
        return self.as_object();
    };
    auto make_layer = [wLayer](Context& cx, u64 id) -> Value {
        if (!id) return Value::null();
        ObjectRef o = cx.new_object_with(wLayer.lock());
        o->hostTag = kTagLayer;
        o->hostId = id;
        return Value::object(o);
    };
    auto make_prop = [wProp](Context& cx, u64 layer, i64 code, i64 extra) -> Value {
        ObjectRef o = cx.new_object_with(wProp.lock());
        o->hostTag = kTagProp;
        o->hostId = layer;
        o->hostA = code;
        o->hostB = extra;
        return Value::object(o);
    };
    auto make_effect = [wEffect](Context& cx, u64 layer, u32 effectId) -> Value {
        ObjectRef o = cx.new_object_with(wEffect.lock());
        o->hostTag = kTagEffect;
        o->hostId = layer;
        o->hostA = effectId;
        return Value::object(o);
    };
    auto make_comp = [wComp](Context& cx) -> Value {
        ObjectRef o = cx.new_object_with(wComp.lock());
        o->hostTag = kTagComp;
        return Value::object(o);
    };
    auto number_list = [](Context& cx, const Value& v, u32 maxDims, const char* what) {
        std::vector<f64> out;
        if (v.is_number()) out.push_back(v.as_number());
        else if (v.is_array()) for (const Value& e : v.as_object()->elements) out.push_back(cx.to_number(e));
        else cx.throw_error("TypeError", std::string(what) + ": precisa de numero ou lista de numeros");
        if (out.empty() || out.size() > maxDims) cx.throw_error("TypeError", std::string(what) + ": numero de componentes errado");
        for (const f64 x : out) if (!std::isfinite(x)) cx.throw_error("TypeError", std::string(what) + ": valor nao finito");
        return out;
    };
    auto value_of = [](Context& cx, const std::vector<f64>& v) -> Value {
        if (v.size() == 1) return Value::number(v[0]);
        std::vector<Value> items;
        for (const f64 x : v) items.push_back(Value::number(x));
        return Value::object(cx.new_array(std::move(items)));
    };
    auto color_value = [](Context& cx, Vec4 c, bool alpha) -> Value {
        std::vector<Value> items{Value::number(c.x), Value::number(c.y), Value::number(c.z)};
        if (alpha) items.push_back(Value::number(c.w));
        return Value::object(cx.new_array(std::move(items)));
    };
    auto color_arg = [](Context& cx, const Value& v, const char* what) -> Vec4 {
        Vec4 out{1, 1, 1, 1};
        if (v.is_array()) {
            const auto& el = v.as_object()->elements;
            if (el.size() < 3) cx.throw_error("TypeError", std::string(what) + ": cor = [r, g, b(, a)] de 0 a 1");
            f32* dst[4] = {&out.x, &out.y, &out.z, &out.w};
            for (usize i = 0; i < el.size() && i < 4; ++i) *dst[i] = static_cast<f32>(std::clamp(cx.to_number(el[i]), 0.0, 1.0));
            return out;
        }
        if (v.is_string()) {
            const std::string& s = v.as_string();
            if ((s.size() == 7 || s.size() == 9) && s[0] == '#') {
                auto hex = [&](usize i) { return static_cast<f32>(std::stoi(s.substr(i, 2), nullptr, 16)) / 255.0f; };
                bool ok = true;
                for (usize i = 1; i < s.size(); ++i) ok = ok && std::isxdigit(static_cast<unsigned char>(s[i]));
                if (ok) return Vec4{hex(1), hex(3), hex(5), s.size() == 9 ? hex(7) : 1.0f};
            }
        }
        cx.throw_error("TypeError", std::string(what) + ": cor = [r, g, b(, a)] de 0 a 1 ou \"#RRGGBB\"");
    };
    // Ordem do AE: índice 1 = a camada da frente.
    auto layer_ids = [](const Composition& comp) {
        std::vector<u64> ids;
        const u32 n = comp.order().size();
        for (u32 i = 0; i < n; ++i) ids.push_back(comp.order().at(n - 1 - i).pack());
        return ids;
    };

    // =========================================================================
    // app (o runtime já criou o objeto)
    // =========================================================================
    const Value appV = c.global("app");
    if (!appV.is_object()) return;
    const ObjectRef app = appV.as_object();
    c.method(app, "beginUndoGroup", [self, st](Context& cx, const Value&, std::vector<Value>& a) {
        std::lock_guard<std::mutex> lock(self->modelMutex_);
        if (st->scriptGroups >= 32) cx.throw_error("RangeError", "beginUndoGroup aninhado demais");
        const std::string label = a.empty() ? std::string("script") : cx.to_string(a[0]);
        self->history_.begin_group(label.c_str());
        ++st->scriptGroups;
        return Value::undefined();
    });
    c.method(app, "endUndoGroup", [self, st](Context&, const Value&, std::vector<Value>&) {
        std::lock_guard<std::mutex> lock(self->modelMutex_);
        if (st->scriptGroups > 0) {
            self->history_.end_group();
            --st->scriptGroups;
        }
        return Value::undefined();
    });
    {
        ObjectRef project = c.new_object();
        c.accessor(project, "name", [self](Context& cx, const Value&, std::vector<Value>&) {
            std::lock_guard<std::mutex> lock(self->modelMutex_);
            return cx.str(self->project_ ? self->project_->metadata().title : std::string());
        });
        c.accessor(project, "activeItem", [self, make_comp](Context& cx, const Value&, std::vector<Value>&) {
            {
                std::lock_guard<std::mutex> lock(self->modelMutex_);
                if (!self->project_ || !self->current_composition()) return Value::null();
            }
            return make_comp(cx);
        });
        c.accessor(project, "numItems", [self](Context&, const Value&, std::vector<Value>&) {
            std::lock_guard<std::mutex> lock(self->modelMutex_);
            return Value::number(self->project_ && self->current_composition() ? 1 : 0);
        });
        c.method(project, "item", [self, make_comp](Context& cx, const Value&, std::vector<Value>& a) {
            if (cx.to_number(arg(a, 0)) != 1.0) cx.throw_error("RangeError", "project.item: o AUREA expoe so a composicao aberta (item(1))");
            {
                std::lock_guard<std::mutex> lock(self->modelMutex_);
                if (!self->project_ || !self->current_composition()) return Value::null();
            }
            return make_comp(cx);
        });
        c.constant(app, "project", Value::object(project));
    }

    // =========================================================================
    // Composição
    // =========================================================================
    auto comp_number = [&](const char* name, auto getter) {
        c.accessor(compProto, name, [with_comp, getter](Context& cx, const Value&, std::vector<Value>&) {
            return Value::number(with_comp(cx, [&](Composition& comp) { return getter(comp); }));
        });
    };
    c.accessor(compProto, "name", [self](Context& cx, const Value&, std::vector<Value>&) { return cx.str(self->current_composition_name()); });
    comp_number("width", [](const Composition& comp) { return static_cast<f64>(comp.width()); });
    comp_number("height", [](const Composition& comp) { return static_cast<f64>(comp.height()); });
    comp_number("frameRate", [fps_of](const Composition& comp) { return fps_of(comp); });
    comp_number("frameDuration", [fps_of](const Composition& comp) { return 1.0 / fps_of(comp); });
    comp_number("numLayers", [](const Composition& comp) { return static_cast<f64>(comp.order().size()); });
    c.accessor(compProto, "duration", [with_comp, fps_of](Context& cx, const Value&, std::vector<Value>&) {
        return Value::number(with_comp(cx, [&](Composition& comp) { return static_cast<f64>(comp.duration().value) / fps_of(comp); }));
    }, [with_comp, fps_of, seconds_to_frame, apply](Context& cx, const Value&, std::vector<Value>& a) {
        Command cmd;
        cmd.type = CommandType::CompositionSetDuration;
        with_comp(cx, [&](Composition& comp) {
            cmd.comp_duration.comp = comp.id();
            cmd.comp_duration.duration = FrameIndex{std::max<i64>(1, seconds_to_frame(cx, arg(a, 0), fps_of(comp)))};
            return 0;
        });
        apply(cx, cmd, "comp.duration");
        return Value::undefined();
    });
    c.accessor(compProto, "time", [self, with_comp, fps_of](Context& cx, const Value&, std::vector<Value>&) {
        return Value::number(with_comp(cx, [&](Composition& comp) { return static_cast<f64>(self->playback_.current().value) / fps_of(comp); }));
    }, [with_comp, fps_of, seconds_to_frame, apply](Context& cx, const Value&, std::vector<Value>& a) {
        Command cmd;
        cmd.type = CommandType::PlaybackSeek;
        with_comp(cx, [&](Composition& comp) {
            const f64 fps = fps_of(comp);
            cmd.seek.time = tick_at(FrameIndex{std::max<i64>(0, seconds_to_frame(cx, arg(a, 0), fps))}, fps);
            return 0;
        });
        apply(cx, cmd, "comp.time");
        return Value::undefined();
    });
    // comp.layers: coleção 1-based como no AE (comp.layers[1], .length, add*)
    c.accessor(compProto, "layers", [with_comp, layer_ids, make_layer, wColl](Context& cx, const Value&, std::vector<Value>&) {
        const std::vector<u64> ids = with_comp(cx, [&](Composition& comp) { return layer_ids(comp); });
        ObjectRef coll = cx.new_object_with(wColl.lock());
        coll->hostTag = kTagComp;
        coll->props.put("length").value = Value::number(static_cast<f64>(ids.size()));
        for (usize i = 0; i < ids.size(); ++i) {
            coll->props.put(std::to_string(i + 1)).value = make_layer(cx, ids[i]);
            coll->grow(96);
        }
        return Value::object(coll);
    });
    c.accessor(compProto, "allLayers", [with_comp, layer_ids, make_layer](Context& cx, const Value&, std::vector<Value>&) {
        const std::vector<u64> ids = with_comp(cx, [&](Composition& comp) { return layer_ids(comp); });
        std::vector<Value> items;
        for (const u64 id : ids) items.push_back(make_layer(cx, id));
        return Value::object(cx.new_array(std::move(items)));
    });
    c.method(compProto, "layer", [with_comp, layer_ids, make_layer](Context& cx, const Value&, std::vector<Value>& a) {
        const Value key = arg(a, 0);
        const u64 id = with_comp(cx, [&](Composition& comp) -> u64 {
            const std::vector<u64> ids = layer_ids(comp);
            if (key.is_number()) {
                const f64 i = key.as_number();
                if (i >= 1 && i <= static_cast<f64>(ids.size()) && std::floor(i) == i) return ids[static_cast<usize>(i) - 1];
                return 0;
            }
            const std::string name = cx.to_string(key);
            for (const u64 lid : ids) if (const Layer* l = comp.layer(LayerId::unpack(lid)); l && l->name == name) return lid;
            return 0;
        });
        return make_layer(cx, id);
    });
    c.accessor(compProto, "selectedLayers", [self, layer_ids, make_layer, with_comp](Context& cx, const Value&, std::vector<Value>&) {
        const std::vector<u64> ids = with_comp(cx, [&](Composition& comp) {
            std::vector<u64> out;
            for (const u64 id : layer_ids(comp))
                if (std::find(self->selection_.begin(), self->selection_.end(), id) != self->selection_.end()) out.push_back(id);
            return out;
        });
        std::vector<Value> items;
        for (const u64 id : ids) items.push_back(make_layer(cx, id));
        return Value::object(cx.new_array(std::move(items)));
    });
    c.accessor(compProto, "markers", [with_comp, fps_of](Context& cx, const Value&, std::vector<Value>&) {
        struct Row { f64 t; std::string label; u32 color; u32 kind; };
        const std::vector<Row> rows = with_comp(cx, [&](Composition& comp) {
            std::vector<Row> out;
            const f64 fps = fps_of(comp);
            for (const Marker& m : comp.markers()) out.push_back({static_cast<f64>(m.frame.value) / fps, m.label, m.color, m.kind});
            return out;
        });
        std::vector<Value> items;
        for (const Row& r : rows) {
            ObjectRef o = cx.new_object();
            char hex[8];
            std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", r.color & 0xFF, (r.color >> 8) & 0xFF, (r.color >> 16) & 0xFF);
            o->props.put("time").value = Value::number(r.t);
            o->props.put("name").value = cx.str(r.label);
            o->props.put("color").value = cx.str(hex);
            o->props.put("beat").value = Value::boolean(r.kind == kMarkerBeat);
            o->grow(256);
            items.push_back(Value::object(o));
        }
        return Value::object(cx.new_array(std::move(items)));
    });
    c.method(compProto, "addMarker", [self, with_comp, fps_of, seconds_to_frame, color_arg](Context& cx, const Value&, std::vector<Value>& a) {
        const i64 frame = with_comp(cx, [&](Composition& comp) { return seconds_to_frame(cx, arg(a, 0), fps_of(comp)); });
        std::string label = arg(a, 1).is_nullish() ? std::string() : cx.to_string(arg(a, 1));
        if (label.size() > 1024) label.resize(1024);
        u32 color = 0xFFF7C34Fu;
        if (!arg(a, 2).is_nullish()) {
            const Vec4 cc = color_arg(cx, arg(a, 2), "addMarker");
            color = static_cast<u32>(std::lround(cc.x * 255)) | (static_cast<u32>(std::lround(cc.y * 255)) << 8)
                  | (static_cast<u32>(std::lround(cc.z * 255)) << 16) | 0xFF000000u;
        }
        return Value::boolean(self->edit_marker(-1, frame, color, label));
    });
    c.method(compProto, "removeMarker", [self, with_comp, fps_of, seconds_to_frame](Context& cx, const Value&, std::vector<Value>& a) {
        const i64 frame = with_comp(cx, [&](Composition& comp) { return seconds_to_frame(cx, arg(a, 0), fps_of(comp)); });
        return Value::boolean(self->delete_marker(frame));
    });
    c.method(compProto, "renderFrame", [self, runtime, st, wFrame](Context& cx, const Value&, std::vector<Value>& a) {
        u32 maxSize = 512;
        if (arg(a, 0).is_object()) {
            const Value m = cx.get(arg(a, 0), "maxSize");
            if (!m.is_nullish()) maxSize = static_cast<u32>(std::clamp(cx.to_number(m), 16.0, 1024.0));
        }
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        Status s;
        {
            struct Clock {
                Context& c;
                explicit Clock(Context& cc) : c(cc) { c.pause_clock(); }
                ~Clock() { c.resume_clock(); }
            } clock(cx);
            s = self->capture_frame_rgba(maxSize, rgba, w, h);
        }
        if (!s.ok() || !w || !h || rgba.size() < static_cast<usize>(w) * h * 4)
            cx.throw_error("Error", "renderFrame: nao foi possivel desenhar o quadro agora (" + std::string(to_string(s.code())) + ")");
        const std::string folder = runtime->data_folder();
        std::string path;
        if (!folder.empty()) {
            const u64 n = ++st->frameCounter;
            const std::filesystem::path dir(std::u8string(folder.begin(), folder.end()));
            const auto name = [&](u64 k) { return dir / ("quadro-" + std::to_string(k) + ".png"); };
            const auto p8 = name(n).u8string();
            path.assign(p8.begin(), p8.end());
            if (!write_png(path, rgba, w, h)) path.clear();
            if (n > 8) {
                std::error_code ec;
                std::filesystem::remove(name(n - 8), ec);
            }
        }
        ObjectRef f = cx.new_object_with(wFrame.lock());
        f->hostTag = plugins::kHostTagFrame;
        f->hostData = std::make_shared<std::string>(path);
        f->props.put("width").value = Value::number(w);
        f->props.put("height").value = Value::number(h);
        plugins::script::Prop& px = f->props.put("@rgba");
        px.value = cx.str(std::string(rgba.begin(), rgba.end()));
        px.hidden = true;
        f->grow(256);
        return Value::object(f);
    });
    auto frame_pixels = [](Context& cx, const Value& self, u32& w, u32& h) -> const std::string& {
        if (!self.is_object() || self.as_object()->hostTag != plugins::kHostTagFrame) cx.throw_error("TypeError", "metodo de quadro");
        const ObjectRef& o = self.as_object();
        w = static_cast<u32>(cx.to_number(cx.get(self, "width")));
        h = static_cast<u32>(cx.to_number(cx.get(self, "height")));
        const plugins::script::Prop* p = o->props.find("@rgba");
        if (!p || !p->value.is_string()) cx.throw_error("Error", "quadro sem pixels");
        return p->value.as_string();
    };
    c.method(frameProto, "pixel", [frame_pixels, color_value](Context& cx, const Value& self, std::vector<Value>& a) {
        u32 w = 0, h = 0;
        const std::string& px = frame_pixels(cx, self, w, h);
        const f64 x = cx.to_number(arg(a, 0)), y = cx.to_number(arg(a, 1));
        if (!(x >= 0 && y >= 0 && x < w && y < h)) cx.throw_error("RangeError", "pixel fora do quadro");
        const usize at = (static_cast<usize>(y) * w + static_cast<usize>(x)) * 4;
        auto ch = [&](usize k) { return static_cast<f32>(static_cast<u8>(px[at + k])) / 255.0f; };
        return color_value(cx, Vec4{ch(0), ch(1), ch(2), ch(3)}, true);
    });
    c.method(frameProto, "average", [frame_pixels, color_value](Context& cx, const Value& self, std::vector<Value>&) {
        u32 w = 0, h = 0;
        const std::string& px = frame_pixels(cx, self, w, h);
        f64 sum[4] = {0, 0, 0, 0};
        const usize n = static_cast<usize>(w) * h;
        for (usize i = 0; i < n; ++i) for (usize k = 0; k < 4; ++k) sum[k] += static_cast<u8>(px[i * 4 + k]);
        const f64 d = n ? 255.0 * static_cast<f64>(n) : 1.0;
        return color_value(cx, Vec4{static_cast<f32>(sum[0] / d), static_cast<f32>(sum[1] / d), static_cast<f32>(sum[2] / d), static_cast<f32>(sum[3] / d)}, true);
    });

    // --- comp.layers.add* ---
    auto created = [self, make_layer, fail](Context& cx, const Result<u64>& r, const char* what) {
        if (!r.ok()) fail(cx, r.status(), what);
        {
            std::lock_guard<std::mutex> lock(self->modelMutex_);
            self->bump_model_revision_locked();
        }
        return make_layer(cx, *r);
    };
    c.method(collProto, "addText", [self, created](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string text = arg(a, 0).is_nullish() ? std::string("Texto") : cx.to_string(arg(a, 0));
        return created(cx, self->add_text(text.c_str()), "addText");
    });
    c.method(collProto, "addShape", [self, created](Context& cx, const Value&, std::vector<Value>& a) {
        u32 preset = 0;
        const Value p = arg(a, 0);
        if (p.is_number()) preset = static_cast<u32>(std::clamp(p.as_number(), 0.0, 32.0));
        else if (p.is_string()) {
            static const std::pair<const char*, u32> kNames[] = {
                {"circle", 0}, {"roundedSquare", 1}, {"cross", 2}, {"ring", 3}, {"triangle", 4}, {"hexagon", 6},
                {"arrow", 8}, {"square", 10}, {"star", 11}, {"capsule", 12}, {"heart", 26}, {"line", 24}, {"diamond", 25},
            };
            bool known = false;
            for (const auto& [n, k] : kNames) if (p.as_string() == n) { preset = k; known = true; }
            if (!known) cx.throw_error("TypeError", "addShape: forma desconhecida \"" + p.as_string() + "\"");
        }
        return created(cx, self->add_shape(preset), "addShape");
    });
    c.method(collProto, "addSolid", [self, created, color_arg, apply](Context& cx, const Value&, std::vector<Value>& a) {
        const Vec4 color = arg(a, 0).is_nullish() ? Vec4{1, 1, 1, 1} : color_arg(cx, arg(a, 0), "addSolid");
        Value layer = created(cx, self->add_shape(10), "addSolid");
        const u64 id = layer.as_object()->hostId;
        Command fill;
        fill.type = CommandType::ShapeSetFill;
        fill.text_color.layer = LayerId::unpack(id);
        fill.text_color.r = color.x; fill.text_color.g = color.y; fill.text_color.b = color.z; fill.text_color.a = color.w;
        apply(cx, fill, "addSolid");
        for (u32 k = 0; k < 2; ++k) {
            const Value v = arg(a, 2 + k);
            if (v.is_nullish()) continue;
            Command size;
            size.type = CommandType::ShapeSetParam;
            size.shape_param.layer = LayerId::unpack(id);
            size.shape_param.param = 5 + k;
            size.shape_param.value = static_cast<f32>(std::clamp(cx.to_number(v), 1.0, 16384.0));
            apply(cx, size, "addSolid");
        }
        if (!arg(a, 1).is_nullish()) {
            Command name;
            name.type = CommandType::LayerSetName;
            name.layer_ref.layer = LayerId::unpack(id);
            const std::string text = cx.to_string(arg(a, 1));
            apply(cx, name, "addSolid", text.c_str());
        }
        return layer;
    });
    c.method(collProto, "addNull", [self, created](Context& cx, const Value&, std::vector<Value>& a) {
        return created(cx, self->add_null(Context::truthy(arg(a, 0))), "addNull");
    });
    c.method(collProto, "addCamera", [self, created](Context& cx, const Value&, std::vector<Value>&) {
        return created(cx, self->add_camera(), "addCamera");
    });
    c.method(collProto, "addLight", [self, created](Context& cx, const Value&, std::vector<Value>& a) {
        return created(cx, self->add_light(static_cast<u32>(std::clamp(cx.to_number(arg(a, 0).is_nullish() ? Value::number(0) : arg(a, 0)), 0.0, 3.0))), "addLight");
    });
    c.method(collProto, "byName", [with_comp, layer_ids, make_layer](Context& cx, const Value&, std::vector<Value>& a) {
        const std::string name = cx.to_string(arg(a, 0));
        const u64 id = with_comp(cx, [&](Composition& comp) -> u64 {
            for (const u64 lid : layer_ids(comp)) if (const Layer* l = comp.layer(LayerId::unpack(lid)); l && l->name == name) return lid;
            return 0;
        });
        return make_layer(cx, id);
    });
    c.method(collProto, "toArray", [](Context& cx, const Value& self, std::vector<Value>&) {
        std::vector<Value> items;
        const f64 n = cx.to_number(cx.get(self, "length"));
        for (usize i = 1; i <= static_cast<usize>(n); ++i) items.push_back(cx.get(self, std::to_string(i)));
        return Value::object(cx.new_array(std::move(items)));
    });

    // =========================================================================
    // Camada
    // =========================================================================
    auto layer_id = [host](Context& cx, const Value& self) { return host(cx, self, kTagLayer, "camada")->hostId; };
    c.accessor(layerProto, "id", [layer_id](Context& cx, const Value& self, std::vector<Value>&) { return cx.str(std::to_string(layer_id(cx, self))); });
    c.accessor(layerProto, "index", [layer_id, with_layer, layer_ids](Context& cx, const Value& self, std::vector<Value>&) {
        const u64 id = layer_id(cx, self);
        return Value::number(with_layer(cx, id, [&](Composition& comp, Layer&) {
            const std::vector<u64> ids = layer_ids(comp);
            return static_cast<f64>(std::find(ids.begin(), ids.end(), id) - ids.begin() + 1);
        }));
    });
    c.accessor(layerProto, "name", [layer_id, with_layer](Context& cx, const Value& self, std::vector<Value>&) {
        return cx.str(with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) { return l.name; }));
    }, [layer_id, apply](Context& cx, const Value& self, std::vector<Value>& a) {
        Command cmd;
        cmd.type = CommandType::LayerSetName;
        cmd.layer_ref.layer = LayerId::unpack(layer_id(cx, self));
        std::string name = cx.to_string(arg(a, 0));
        if (name.size() > 512) name.resize(512);
        apply(cx, cmd, "layer.name", name.c_str());
        return Value::undefined();
    });
    c.accessor(layerProto, "type", [layer_id, with_layer](Context& cx, const Value& self, std::vector<Value>&) {
        return cx.str(with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) { return std::string(kind_text(l.kind)); }));
    });
    auto time_range = [layer_id, with_layer, fps_of, seconds_to_frame, apply](Context& cx, const Value& self, const Value& v, bool isIn) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        const u64 id = layer_id(cx, self);
        with_layer(cx, id, [&](Composition& comp, Layer& l) {
            const i64 f = std::max<i64>(0, seconds_to_frame(cx, v, fps_of(comp)));
            cmd.layer_range.layer = LayerId::unpack(id);
            cmd.layer_range.start = isIn ? FrameIndex{f} : l.start;
            cmd.layer_range.end = isIn ? l.end : FrameIndex{f};
            cmd.layer_range.offset = l.offset;
            cmd.layer_range.setOffset = 0;
            return 0;
        });
        if (cmd.layer_range.end.value <= cmd.layer_range.start.value) cx.throw_error("RangeError", "inPoint precisa ser antes de outPoint");
        apply(cx, cmd, isIn ? "layer.inPoint" : "layer.outPoint");
    };
    c.accessor(layerProto, "inPoint", [layer_id, with_layer, fps_of](Context& cx, const Value& self, std::vector<Value>&) {
        return Value::number(with_layer(cx, layer_id(cx, self), [&](Composition& comp, Layer& l) { return static_cast<f64>(l.start.value) / fps_of(comp); }));
    }, [time_range](Context& cx, const Value& self, std::vector<Value>& a) { time_range(cx, self, arg(a, 0), true); return Value::undefined(); });
    c.accessor(layerProto, "outPoint", [layer_id, with_layer, fps_of](Context& cx, const Value& self, std::vector<Value>&) {
        return Value::number(with_layer(cx, layer_id(cx, self), [&](Composition& comp, Layer& l) { return static_cast<f64>(l.end.value) / fps_of(comp); }));
    }, [time_range](Context& cx, const Value& self, std::vector<Value>& a) { time_range(cx, self, arg(a, 0), false); return Value::undefined(); });
    c.accessor(layerProto, "startTime", [layer_id, with_layer, fps_of](Context& cx, const Value& self, std::vector<Value>&) {
        return Value::number(with_layer(cx, layer_id(cx, self), [&](Composition& comp, Layer& l) {
            return static_cast<f64>(l.start.value - l.offset.value) / fps_of(comp);
        }));
    });
    auto flag_prop = [&](const char* name, auto read, CommandType type) {
        c.accessor(layerProto, name, [layer_id, with_layer, read](Context& cx, const Value& self, std::vector<Value>&) {
            return Value::boolean(with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) { return read(l); }));
        }, [layer_id, apply, type, name](Context& cx, const Value& self, std::vector<Value>& a) {
            Command cmd;
            cmd.type = type;
            if (type == CommandType::LayerSetVisible) {
                cmd.layer_visible.layer = LayerId::unpack(layer_id(cx, self));
                cmd.layer_visible.visible = Context::truthy(arg(a, 0));
            } else {
                cmd.layer_locked.layer = LayerId::unpack(layer_id(cx, self));
                cmd.layer_locked.locked = Context::truthy(arg(a, 0));
            }
            apply(cx, cmd, std::string("layer.") + name);
            return Value::undefined();
        });
    };
    flag_prop("enabled", [](const Layer& l) { return l.visible; }, CommandType::LayerSetVisible);
    flag_prop("locked", [](const Layer& l) { return l.locked; }, CommandType::LayerSetLocked);
    c.accessor(layerProto, "threeD", [layer_id, with_layer](Context& cx, const Value& self, std::vector<Value>&) {
        return Value::boolean(with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) { return l.threeD; }));
    }, [self, layer_id](Context& cx, const Value& me, std::vector<Value>& a) {
        if (!self->set_layer_3d(layer_id(cx, me), Context::truthy(arg(a, 0)))) cx.throw_error("Error", "layer.threeD: esta camada nao muda de 2D/3D");
        return Value::undefined();
    });
    c.accessor(layerProto, "selected", [self, layer_id](Context& cx, const Value& me, std::vector<Value>&) {
        return Value::boolean(self->is_selected(layer_id(cx, me)));
    }, [self, runtime, layer_id](Context& cx, const Value& me, std::vector<Value>& a) {
        const u64 id = layer_id(cx, me);
        std::vector<u64> sel;
        {
            std::lock_guard<std::mutex> lock(self->modelMutex_);
            sel = self->selection_;
        }
        std::erase(sel, id);
        if (Context::truthy(arg(a, 0))) sel.push_back(id);
        self->set_selection(sel.data(), static_cast<u32>(sel.size()));
        std::string list = "[";
        for (usize i = 0; i < sel.size(); ++i) list += (i ? "," : "") + std::string("\"") + std::to_string(sel[i]) + "\"";
        runtime->message("select", list + "]");
        return Value::undefined();
    });
    c.accessor(layerProto, "parent", [layer_id, with_layer, make_layer](Context& cx, const Value& self, std::vector<Value>&) {
        const u64 parent = with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) { return l.parent.valid() ? l.parent.pack() : u64{0}; });
        return make_layer(cx, parent);
    }, [layer_id, apply](Context& cx, const Value& self, std::vector<Value>& a) {
        Command cmd;
        cmd.type = CommandType::LayerSetParent;
        cmd.layer_parent.layer = LayerId::unpack(layer_id(cx, self));
        const Value p = arg(a, 0);
        if (!p.is_nullish()) {
            if (!p.is_object() || p.as_object()->hostTag != kTagLayer) cx.throw_error("TypeError", "layer.parent: use outra camada ou null");
            cmd.layer_parent.parent = LayerId::unpack(p.as_object()->hostId);
        }
        apply(cx, cmd, "layer.parent");
        return Value::undefined();
    });
    // Texto
    c.accessor(layerProto, "text", [layer_id, with_layer](Context& cx, const Value& self, std::vector<Value>&) {
        return with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) {
            return l.kind == LayerKind::Text ? cx.str(l.text.content) : Value::null();
        });
    }, [layer_id, with_layer, apply](Context& cx, const Value& self, std::vector<Value>& a) {
        const u64 id = layer_id(cx, self);
        const bool isText = with_layer(cx, id, [&](Composition&, Layer& l) { return l.kind == LayerKind::Text; });
        if (!isText) cx.throw_error("TypeError", "layer.text: a camada nao e de texto");
        Command cmd;
        cmd.type = CommandType::TextSetContent;
        cmd.layer_ref.layer = LayerId::unpack(id);
        std::string text = cx.to_string(arg(a, 0));
        if (text.size() > 64 * 1024) cx.throw_error("RangeError", "layer.text: texto grande demais");
        apply(cx, cmd, "layer.text", text.c_str());
        return Value::undefined();
    });
    c.accessor(layerProto, "fontSize", [layer_id, with_layer](Context& cx, const Value& self, std::vector<Value>&) {
        return with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) {
            return l.kind == LayerKind::Text ? Value::number(l.text.size) : Value::null();
        });
    }, [layer_id, apply](Context& cx, const Value& self, std::vector<Value>& a) {
        Command cmd;
        cmd.type = CommandType::TextSetSize;
        cmd.text_size.layer = LayerId::unpack(layer_id(cx, self));
        cmd.text_size.size = static_cast<f32>(cx.to_number(arg(a, 0)));
        apply(cx, cmd, "layer.fontSize");
        return Value::undefined();
    });
    c.accessor(layerProto, "fillColor", [layer_id, with_layer, color_value](Context& cx, const Value& self, std::vector<Value>&) {
        return with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) {
            if (l.kind == LayerKind::Text) return color_value(cx, l.text.color, true);
            if (l.kind == LayerKind::Shape) return color_value(cx, l.shape.fillColor, true);
            return Value::null();
        });
    }, [layer_id, with_layer, color_arg, apply](Context& cx, const Value& self, std::vector<Value>& a) {
        const u64 id = layer_id(cx, self);
        const LayerKind kind = with_layer(cx, id, [&](Composition&, Layer& l) { return l.kind; });
        if (kind != LayerKind::Text && kind != LayerKind::Shape) cx.throw_error("TypeError", "layer.fillColor: so texto e forma");
        const Vec4 col = color_arg(cx, arg(a, 0), "layer.fillColor");
        Command cmd;
        cmd.type = kind == LayerKind::Text ? CommandType::TextSetColor : CommandType::ShapeSetFill;
        cmd.text_color.layer = LayerId::unpack(id);
        cmd.text_color.r = col.x; cmd.text_color.g = col.y; cmd.text_color.b = col.z; cmd.text_color.a = col.w;
        apply(cx, cmd, "layer.fillColor");
        return Value::undefined();
    });
    // Propriedades
    c.method(layerProto, "property", [layer_id, make_prop](Context& cx, const Value& self, std::vector<Value>& a) {
        const i64 code = prop_code(cx.to_string(arg(a, 0)));
        if (code < 0) return Value::null();
        return make_prop(cx, layer_id(cx, self), code, 0);
    });
    for (i64 code = 0; code < static_cast<i64>(std::size(kProps)); ++code) {
        c.accessor(layerProto, kProps[code].id, [layer_id, make_prop, code](Context& cx, const Value& self, std::vector<Value>&) {
            return make_prop(cx, layer_id(cx, self), code, 0);
        });
    }
    c.accessor(layerProto, "transform", [layer_id, make_prop](Context& cx, const Value& self, std::vector<Value>&) {
        const u64 id = layer_id(cx, self);
        ObjectRef t = cx.new_object();
        for (i64 code = 0; code < static_cast<i64>(std::size(kProps)); ++code) {
            t->props.put(kProps[code].id).value = make_prop(cx, id, code, 0);
            t->grow(96);
        }
        return Value::object(t);
    });
    // Efeitos
    c.accessor(layerProto, "effects", [layer_id, with_layer, make_effect](Context& cx, const Value& self, std::vector<Value>&) {
        const u64 id = layer_id(cx, self);
        const std::vector<u32> ids = with_layer(cx, id, [&](Composition&, Layer& l) {
            std::vector<u32> out;
            for (const EffectInstance& e : l.effects) if (e.role == kEffectRoleNone) out.push_back(e.id);
            return out;
        });
        std::vector<Value> items;
        for (const u32 e : ids) items.push_back(make_effect(cx, id, e));
        return Value::object(cx.new_array(std::move(items)));
    });
    c.method(layerProto, "effect", [self, layer_id, with_layer, make_effect](Context& cx, const Value& me, std::vector<Value>& a) {
        const u64 id = layer_id(cx, me);
        const Value key = arg(a, 0);
        const u32 found = with_layer(cx, id, [&](Composition&, Layer& l) -> u32 {
            u32 n = 0;
            for (const EffectInstance& e : l.effects) {
                if (e.role != kEffectRoleNone) continue;
                ++n;
                if (key.is_number() && key.as_number() == static_cast<f64>(n)) return e.id;
                if (key.is_string()) {
                    const Effect* fx = self->effectRegistry_.find(e.type);
                    if (fx && (key.as_string() == fx->info().key || key.as_string() == fx->info().name)) return e.id;
                }
            }
            return kInvalidIndex;
        });
        return found == kInvalidIndex ? Value::null() : make_effect(cx, id, found);
    });
    c.method(layerProto, "addEffect", [self, layer_id, with_layer, make_effect, apply](Context& cx, const Value& me, std::vector<Value>& a) {
        const u64 id = layer_id(cx, me);
        const std::string key = cx.to_string(arg(a, 0));
        EffectTypeId type = self->effectRegistry_.find_key(key);
        if (!type) {
            for (u32 i = 0; i < self->effectRegistry_.count(); ++i) {
                if (self->effectRegistry_.retired_at(i)) continue;
                const Effect& fx = self->effectRegistry_.at(i);
                if (key == fx.info().name) { type = fx.type_id(); break; }
            }
        }
        if (!type) cx.throw_error("TypeError", "addEffect: efeito desconhecido \"" + key + "\" (use a chave, ex.: \"aurea.blur.gaussian\")");
        const std::vector<u32> before = with_layer(cx, id, [&](Composition&, Layer& l) {
            std::vector<u32> out;
            for (const EffectInstance& e : l.effects) out.push_back(e.id);
            return out;
        });
        Command cmd;
        cmd.type = CommandType::EffectAdd;
        cmd.effect_add.layer = LayerId::unpack(id);
        cmd.effect_add.effectType = type;
        cmd.effect_add.index = static_cast<u32>(before.size());
        apply(cx, cmd, "addEffect");
        const u32 added = with_layer(cx, id, [&](Composition&, Layer& l) -> u32 {
            for (const EffectInstance& e : l.effects)
                if (std::find(before.begin(), before.end(), e.id) == before.end()) return e.id;
            return kInvalidIndex;
        });
        return added == kInvalidIndex ? Value::null() : make_effect(cx, id, added);
    });
    c.method(layerProto, "remove", [layer_id, apply](Context& cx, const Value& self, std::vector<Value>&) {
        Command cmd;
        cmd.type = CommandType::LayerDelete;
        cmd.layer_ref.layer = LayerId::unpack(layer_id(cx, self));
        apply(cx, cmd, "layer.remove");
        return Value::undefined();
    });
    c.method(layerProto, "duplicate", [layer_id, with_comp, apply, make_layer](Context& cx, const Value& self, std::vector<Value>&) {
        const u64 id = layer_id(cx, self);
        auto all = [&] {
            return with_comp(cx, [&](Composition& comp) {
                std::vector<u64> out;
                for (u32 i = 0; i < comp.order().size(); ++i) out.push_back(comp.order().at(i).pack());
                return out;
            });
        };
        const std::vector<u64> before = all();
        Command cmd;
        cmd.type = CommandType::LayerDuplicate;
        cmd.layer_ref.layer = LayerId::unpack(id);
        apply(cx, cmd, "layer.duplicate");
        for (const u64 n : all()) if (std::find(before.begin(), before.end(), n) == before.end()) return make_layer(cx, n);
        return Value::null();
    });
    auto move_to = [layer_id, with_comp, apply](Context& cx, const Value& self, f64 aeIndex) {
        const u64 id = layer_id(cx, self);
        Command cmd;
        cmd.type = CommandType::LayerReorder;
        cmd.layer_reorder.layer = LayerId::unpack(id);
        with_comp(cx, [&](Composition& comp) {
            const f64 n = static_cast<f64>(comp.order().size());
            if (!(aeIndex >= 1 && aeIndex <= n)) cx.throw_error("RangeError", "moveTo: indice fora de 1.." + Context::number_text(n));
            cmd.layer_reorder.newIndex = static_cast<u32>(n - std::floor(aeIndex));
            return 0;
        });
        apply(cx, cmd, "layer.moveTo");
    };
    c.method(layerProto, "moveTo", [move_to](Context& cx, const Value& self, std::vector<Value>& a) {
        move_to(cx, self, cx.to_number(arg(a, 0)));
        return Value::undefined();
    });
    c.method(layerProto, "moveToBeginning", [move_to](Context& cx, const Value& self, std::vector<Value>&) {
        move_to(cx, self, 1);
        return Value::undefined();
    });
    c.method(layerProto, "moveToEnd", [move_to, with_comp](Context& cx, const Value& self, std::vector<Value>&) {
        const f64 n = with_comp(cx, [&](Composition& comp) { return static_cast<f64>(comp.order().size()); });
        move_to(cx, self, n);
        return Value::undefined();
    });
    c.method(layerProto, "toString", [layer_id, with_layer](Context& cx, const Value& self, std::vector<Value>&) {
        return cx.str("[Layer " + with_layer(cx, layer_id(cx, self), [&](Composition&, Layer& l) { return l.name; }) + "]");
    });

    // =========================================================================
    // Efeito
    // =========================================================================
    auto effect_ref = [host](Context& cx, const Value& self, u64& layer, u32& effect) {
        const ObjectRef& o = host(cx, self, kTagEffect, "efeito");
        layer = o->hostId;
        effect = static_cast<u32>(o->hostA);
    };
    auto with_effect = [with_layer](Context& cx, u64 layer, u32 effect, auto&& f) {
        return with_layer(cx, layer, [&](Composition& comp, Layer& l) {
            EffectId eid;
            eid.index = effect;
            EffectInstance* e = l.find_effect(eid);
            if (!e) cx.throw_error("ReferenceError", "o efeito nao existe mais nesta camada");
            return f(comp, l, *e);
        });
    };
    c.accessor(effectProto, "name", [self, effect_ref, with_effect](Context& cx, const Value& me, std::vector<Value>&) {
        u64 l = 0; u32 e = 0;
        effect_ref(cx, me, l, e);
        return cx.str(with_effect(cx, l, e, [&](Composition&, Layer&, EffectInstance& fx) {
            const Effect* d = self->effectRegistry_.find(fx.type);
            return std::string(d ? d->info().name : "?");
        }));
    });
    c.accessor(effectProto, "matchName", [self, effect_ref, with_effect](Context& cx, const Value& me, std::vector<Value>&) {
        u64 l = 0; u32 e = 0;
        effect_ref(cx, me, l, e);
        return cx.str(with_effect(cx, l, e, [&](Composition&, Layer&, EffectInstance& fx) {
            const Effect* d = self->effectRegistry_.find(fx.type);
            return std::string(d ? d->info().key : "");
        }));
    });
    c.accessor(effectProto, "numProperties", [effect_ref, with_effect](Context& cx, const Value& me, std::vector<Value>&) {
        u64 l = 0; u32 e = 0;
        effect_ref(cx, me, l, e);
        return Value::number(with_effect(cx, l, e, [&](Composition&, Layer&, EffectInstance& fx) { return static_cast<f64>(fx.params.size()); }));
    });
    c.accessor(effectProto, "enabled", [effect_ref, with_effect](Context& cx, const Value& me, std::vector<Value>&) {
        u64 l = 0; u32 e = 0;
        effect_ref(cx, me, l, e);
        return Value::boolean(with_effect(cx, l, e, [&](Composition&, Layer&, EffectInstance& fx) { return fx.enabled; }));
    }, [effect_ref, apply](Context& cx, const Value& me, std::vector<Value>& a) {
        u64 l = 0; u32 e = 0;
        effect_ref(cx, me, l, e);
        Command cmd;
        cmd.type = CommandType::EffectSetEnabled;
        cmd.effect_enabled.layer = LayerId::unpack(l);
        cmd.effect_enabled.effect.index = e;
        cmd.effect_enabled.enabled = Context::truthy(arg(a, 0));
        apply(cx, cmd, "effect.enabled");
        return Value::undefined();
    });
    c.method(effectProto, "remove", [effect_ref, apply](Context& cx, const Value& me, std::vector<Value>&) {
        u64 l = 0; u32 e = 0;
        effect_ref(cx, me, l, e);
        Command cmd;
        cmd.type = CommandType::EffectRemove;
        cmd.effect_ref.layer = LayerId::unpack(l);
        cmd.effect_ref.effect.index = e;
        apply(cx, cmd, "effect.remove");
        return Value::undefined();
    });
    c.method(effectProto, "property", [self, effect_ref, with_effect, make_prop](Context& cx, const Value& me, std::vector<Value>& a) {
        u64 l = 0; u32 e = 0;
        effect_ref(cx, me, l, e);
        const Value key = arg(a, 0);
        const i64 index = with_effect(cx, l, e, [&](Composition&, Layer&, EffectInstance& fx) -> i64 {
            const ParameterRegistry* specs = self->effectRegistry_.params(fx.type);
            if (!specs) return -1;
            if (key.is_number()) {
                const f64 i = key.as_number();
                return i >= 1 && i <= specs->count() && std::floor(i) == i ? static_cast<i64>(i) - 1 : -1;
            }
            const std::string k = cx.to_string(key);
            for (u32 i = 0; i < specs->count(); ++i)
                if (k == specs->at(i).id || k == specs->at(i).label) return i;
            return -1;
        });
        if (index < 0) return Value::null();
        return make_prop(cx, l, kEffectPropCode, (static_cast<i64>(e) << 16) | index);
    });

    // =========================================================================
    // Propriedade (transform ou parâmetro de efeito)
    // =========================================================================
    struct PropRef {
        u64 layer = 0;
        i64 code = 0;
        u32 effect = 0;
        u32 param = 0;
    };
    auto prop_ref = [host](Context& cx, const Value& self) {
        const ObjectRef& o = host(cx, self, kTagProp, "propriedade");
        PropRef r;
        r.layer = o->hostId;
        r.code = o->hostA;
        if (r.code == kEffectPropCode) {
            r.effect = static_cast<u32>(o->hostB >> 16);
            r.param = static_cast<u32>(o->hostB & 0xFFFF);
        }
        return r;
    };
    /// As trilhas da propriedade (uma por componente) e as escalas.
    struct PropShape {
        std::vector<TrackRef> tracks;
        f64 scale = 1.0;
        ParamType type = ParamType::Float;
        bool effect = false;
        std::string name;
    };
    auto shape_of = [self, with_layer](Context& cx, const PropRef& r) {
        return with_layer(cx, r.layer, [&](Composition&, Layer& l) {
            PropShape s;
            if (r.code == kEffectPropCode) {
                EffectId eid;
                eid.index = r.effect;
                const EffectInstance* e = l.find_effect(eid);
                if (!e) cx.throw_error("ReferenceError", "o efeito nao existe mais nesta camada");
                const ParameterRegistry* specs = self->effectRegistry_.params(e->type);
                if (!specs || r.param >= specs->count()) cx.throw_error("ReferenceError", "parametro inexistente");
                const ParamSpec& spec = specs->at(r.param);
                s.effect = true;
                s.type = spec.type;
                s.name = spec.label;
                const u32 comps = spec.type == ParamType::Color ? 4 : spec.type == ParamType::Point2D ? 2 : spec.type == ParamType::Point3D ? 3 : 1;
                for (u32 k = 0; k < comps; ++k) {
                    TrackRef t;
                    t.layer = LayerId::unpack(r.layer);
                    t.property = TrackProperty::EffectParam;
                    t.effectIndex = r.effect;
                    t.effectParamIndex = param_track_key(r.param, k);
                    s.tracks.push_back(t);
                }
                return s;
            }
            const PropDef& d = kProps[r.code];
            s.name = d.aeName;
            s.scale = d.scale;
            u32 dims = d.dims;
            if (dims == 2 && l.threeD) dims = 3;
            for (u32 k = 0; k < dims; ++k) {
                TrackRef t;
                t.layer = LayerId::unpack(r.layer);
                t.property = static_cast<TrackProperty>(static_cast<u16>(d.first) + k);
                s.tracks.push_back(t);
            }
            return s;
        });
    };
    /// Valor no instante `frame` da composição (unidades do script).
    auto value_at = [self, with_layer](Context& cx, const PropRef& r, const PropShape& s, i64 frame) {
        return with_layer(cx, r.layer, [&](Composition&, Layer& l) {
            const FrameIndex local = l.local_time(FrameIndex{frame});
            std::vector<f64> out;
            if (s.effect) {
                EffectId eid;
                eid.index = r.effect;
                const EffectInstance* e = l.find_effect(eid);
                const ParameterRegistry* specs = e ? self->effectRegistry_.params(e->type) : nullptr;
                if (!e || !specs) cx.throw_error("ReferenceError", "o efeito nao existe mais nesta camada");
                const ParamValue v = evaluate_param(l.tracks, *e, r.param, specs->at(r.param), local);
                for (usize k = 0; k < s.tracks.size(); ++k) {
                    f64 x = v.v[k];
                    if (s.type == ParamType::Color && k < 3) x = plugins::linear_to_srgb(static_cast<f32>(x));
                    out.push_back(x);
                }
                return out;
            }
            for (const TrackRef& t : s.tracks) out.push_back(static_cast<f64>(component_at(l, t.property, local)) * s.scale);
            return out;
        });
    };
    auto current_frame = [self, with_comp](Context& cx) {
        return with_comp(cx, [&](Composition&) { return self->playback_.current().value; });
    };
    auto keys_of = [with_layer](Context& cx, const PropRef& r, const PropShape& s) {
        // Tempos (locais) das chaves do componente 0 + o deslocamento local→composição.
        return with_layer(cx, r.layer, [&](Composition&, Layer& l) {
            std::vector<i64> out;
            const Track* t = l.tracks.find(s.tracks[0].property, s.tracks[0].effectIndex, s.tracks[0].effectParamIndex);
            if (t) for (const Keyframe& k : t->keys) out.push_back(l.timeline_time(k.time).value);
            return out;
        });
    };
    auto has_keys = [with_layer](Context& cx, const PropRef& r, const PropShape& s) {
        return with_layer(cx, r.layer, [&](Composition&, Layer& l) {
            for (const TrackRef& t : s.tracks) {
                const Track* track = l.tracks.find(t.property, t.effectIndex, t.effectParamIndex);
                if (track && !track->keys.empty()) return true;
            }
            return false;
        });
    };
    auto to_engine = [](const PropShape& s, usize k, f64 v) -> f32 {
        if (s.effect && s.type == ParamType::Color && k < 3) return plugins::srgb_to_linear(static_cast<f32>(std::clamp(v, 0.0, 1.0)));
        f64 x = v / s.scale;
        if (!s.effect && s.tracks[k].property == TrackProperty::Opacity) x = std::clamp(x, 0.0, 1.0);
        return static_cast<f32>(x);
    };
    auto fps_now = [with_comp, fps_of](Context& cx) { return with_comp(cx, [&](Composition& comp) { return fps_of(comp); }); };

    c.accessor(propProto, "name", [prop_ref, shape_of](Context& cx, const Value& me, std::vector<Value>&) {
        return cx.str(shape_of(cx, prop_ref(cx, me)).name);
    });
    c.accessor(propProto, "value", [prop_ref, shape_of, value_at, current_frame, value_of](Context& cx, const Value& me, std::vector<Value>&) {
        const PropRef r = prop_ref(cx, me);
        return value_of(cx, value_at(cx, r, shape_of(cx, r), current_frame(cx)));
    });
    c.accessor(propProto, "numKeys", [prop_ref, shape_of, keys_of](Context& cx, const Value& me, std::vector<Value>&) {
        const PropRef r = prop_ref(cx, me);
        return Value::number(static_cast<f64>(keys_of(cx, r, shape_of(cx, r)).size()));
    });
    c.accessor(propProto, "isTimeVarying", [prop_ref, shape_of, has_keys](Context& cx, const Value& me, std::vector<Value>&) {
        const PropRef r = prop_ref(cx, me);
        return Value::boolean(has_keys(cx, r, shape_of(cx, r)));
    });
    c.accessor(propProto, "dimensions", [prop_ref, shape_of](Context& cx, const Value& me, std::vector<Value>&) {
        return Value::number(static_cast<f64>(shape_of(cx, prop_ref(cx, me)).tracks.size()));
    });
    c.method(propProto, "valueAtTime", [prop_ref, shape_of, value_at, value_of, seconds_to_frame, fps_now](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        return value_of(cx, value_at(cx, r, shape_of(cx, r), seconds_to_frame(cx, arg(a, 0), fps_now(cx))));
    });
    c.method(propProto, "setValue", [self, prop_ref, shape_of, has_keys, number_list, value_at, current_frame, to_engine, apply]
             (Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const PropShape s = shape_of(cx, r);
        if (has_keys(cx, r, s)) cx.throw_error("Error", "setValue: a propriedade \"" + s.name + "\" tem keyframes; use setValueAtTime (como no After Effects)");
        std::vector<f64> v = number_list(cx, arg(a, 0), static_cast<u32>(s.tracks.size()), "setValue");
        // Componentes que faltam mantêm o valor atual (posição [x, y] numa camada 3D).
        const std::vector<f64> now = value_at(cx, r, s, current_frame(cx));
        while (v.size() < s.tracks.size()) v.push_back(now[v.size()]);
        Command cmd;
        if (s.effect) {
            if (s.tracks.size() == 1) {
                cmd.type = CommandType::EffectSetParam;
                cmd.effect_param.layer = LayerId::unpack(r.layer);
                cmd.effect_param.effect.index = r.effect;
                cmd.effect_param.paramIndex = r.param;
                cmd.effect_param.value = to_engine(s, 0, v[0]);
            } else {
                cmd.type = CommandType::EffectSetColorParam;
                cmd.effect_color.layer = LayerId::unpack(r.layer);
                cmd.effect_color.effect.index = r.effect;
                cmd.effect_color.paramIndex = r.param;
                f32* dst[4] = {&cmd.effect_color.r, &cmd.effect_color.g, &cmd.effect_color.b, &cmd.effect_color.a};
                *dst[3] = 1.0f;
                for (usize k = 0; k < v.size() && k < 4; ++k) *dst[k] = to_engine(s, k, v[k]);
            }
            apply(cx, cmd, "setValue");
            return Value::undefined();
        }
        const TrackProperty first = s.tracks[0].property;
        const LayerId lid = LayerId::unpack(r.layer);
        auto comp3 = [&](usize k) { return k < v.size() ? to_engine(s, k, v[k]) : 0.0f; };
        if (first == TrackProperty::PositionX) {
            cmd.type = CommandType::LayerSetPosition;
            cmd.position.layer = lid;
            cmd.position.x = comp3(0); cmd.position.y = comp3(1);
            cmd.position.z = s.tracks.size() > 2 ? comp3(2) : static_cast<f32>(now.size() > 2 ? now[2] : 0.0);
        } else if (first == TrackProperty::ScaleX) {
            cmd.type = CommandType::LayerSetScale;
            cmd.scale.layer = lid;
            cmd.scale.sx = comp3(0); cmd.scale.sy = comp3(1);
            f32 z = 1.0f;
            {
                std::lock_guard<std::mutex> lock(self->modelMutex_);
                Composition* comp = self->current_composition();
                if (const Layer* l = comp ? comp->layer(lid) : nullptr) z = l->transform.scale.z;
            }
            cmd.scale.sz = s.tracks.size() > 2 ? comp3(2) : z;
        } else if (first == TrackProperty::AnchorX) {
            cmd.type = CommandType::LayerSetAnchor;
            cmd.anchor.layer = lid;
            cmd.anchor.ax = comp3(0); cmd.anchor.ay = comp3(1);
            f32 z = 0.0f;
            {
                std::lock_guard<std::mutex> lock(self->modelMutex_);
                Composition* comp = self->current_composition();
                if (const Layer* l = comp ? comp->layer(lid) : nullptr) z = l->transform.anchor.z;
            }
            cmd.anchor.az = s.tracks.size() > 2 ? comp3(2) : z;
        } else if (first == TrackProperty::Opacity) {
            cmd.type = CommandType::LayerSetOpacity;
            cmd.opacity.layer = lid;
            cmd.opacity.opacity = comp3(0);
        } else {
            // Rotação: os três eixos vão juntos; o que não é este fica.
            Vec3 rot{0, 0, 0};
            {
                std::lock_guard<std::mutex> lock(self->modelMutex_);
                Composition* comp = self->current_composition();
                if (const Layer* l = comp ? comp->layer(lid) : nullptr) rot = l->transform.rotation;
            }
            if (first == TrackProperty::RotationX) rot.x = comp3(0);
            else if (first == TrackProperty::RotationY) rot.y = comp3(0);
            else rot.z = comp3(0);
            cmd.type = CommandType::LayerSetRotation;
            cmd.rotation.layer = lid;
            cmd.rotation.rx = rot.x; cmd.rotation.ry = rot.y; cmd.rotation.rz = rot.z;
        }
        apply(cx, cmd, "setValue");
        return Value::undefined();
    });
    auto set_at = [with_layer, number_list, to_engine, apply](Context& cx, const PropRef& r, const PropShape& s, i64 frame, const Value& value) {
        const std::vector<f64> v = number_list(cx, value, static_cast<u32>(s.tracks.size()), "setValueAtTime");
        const FrameIndex local = with_layer(cx, r.layer, [&](Composition&, Layer& l) { return l.local_time(FrameIndex{frame}); });
        if (local.value < 0) cx.throw_error("RangeError", "setValueAtTime: antes do inicio da camada");
        for (usize k = 0; k < v.size(); ++k) {
            Command cmd;
            cmd.type = CommandType::KeyframeInsert;
            cmd.keyframe.track = s.tracks[k];
            cmd.keyframe.time = local;
            cmd.keyframe.value = to_engine(s, k, v[k]);
            cmd.keyframe.onlyIfChanged = 0;
            apply(cx, cmd, "setValueAtTime");
        }
    };
    c.method(propProto, "setValueAtTime", [prop_ref, shape_of, set_at, seconds_to_frame, fps_now](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const PropShape s = shape_of(cx, r);
        set_at(cx, r, s, seconds_to_frame(cx, arg(a, 0), fps_now(cx)), arg(a, 1));
        return Value::undefined();
    });
    c.method(propProto, "setValuesAtTimes", [prop_ref, shape_of, set_at, seconds_to_frame, fps_now](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const PropShape s = shape_of(cx, r);
        if (!arg(a, 0).is_array() || !arg(a, 1).is_array()) cx.throw_error("TypeError", "setValuesAtTimes(tempos[], valores[])");
        const auto times = arg(a, 0).as_object()->elements;
        const auto values = arg(a, 1).as_object()->elements;
        if (times.size() != values.size()) cx.throw_error("TypeError", "setValuesAtTimes: listas de tamanhos diferentes");
        const f64 fps = fps_now(cx);
        for (usize i = 0; i < times.size(); ++i) set_at(cx, r, s, seconds_to_frame(cx, times[i], fps), values[i]);
        return Value::undefined();
    });
    c.method(propProto, "addKey", [prop_ref, shape_of, set_at, value_at, value_of, seconds_to_frame, fps_now, keys_of](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const PropShape s = shape_of(cx, r);
        const i64 frame = seconds_to_frame(cx, arg(a, 0), fps_now(cx));
        set_at(cx, r, s, frame, value_of(cx, value_at(cx, r, s, frame)));
        const std::vector<i64> keys = keys_of(cx, r, s);
        return Value::number(static_cast<f64>(std::find(keys.begin(), keys.end(), frame) - keys.begin() + 1));
    });
    auto key_index = [](Context& cx, const Value& v, usize n) {
        const f64 k = cx.to_number(v);
        if (!(k >= 1 && k <= static_cast<f64>(n)) || std::floor(k) != k) cx.throw_error("RangeError", "indice de keyframe fora de 1.." + std::to_string(n));
        return static_cast<usize>(k) - 1;
    };
    c.method(propProto, "keyTime", [prop_ref, shape_of, keys_of, key_index, fps_now](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const std::vector<i64> keys = keys_of(cx, r, shape_of(cx, r));
        return Value::number(static_cast<f64>(keys[key_index(cx, arg(a, 0), keys.size())]) / fps_now(cx));
    });
    c.method(propProto, "keyValue", [prop_ref, shape_of, keys_of, key_index, value_at, value_of](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const PropShape s = shape_of(cx, r);
        const std::vector<i64> keys = keys_of(cx, r, s);
        return value_of(cx, value_at(cx, r, s, keys[key_index(cx, arg(a, 0), keys.size())]));
    });
    c.method(propProto, "nearestKeyIndex", [prop_ref, shape_of, keys_of, seconds_to_frame, fps_now](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const std::vector<i64> keys = keys_of(cx, r, shape_of(cx, r));
        if (keys.empty()) cx.throw_error("RangeError", "a propriedade nao tem keyframes");
        const i64 f = seconds_to_frame(cx, arg(a, 0), fps_now(cx));
        usize best = 0;
        for (usize i = 1; i < keys.size(); ++i) if (std::llabs(keys[i] - f) < std::llabs(keys[best] - f)) best = i;
        return Value::number(static_cast<f64>(best + 1));
    });
    c.method(propProto, "removeKey", [self, prop_ref, shape_of, keys_of, key_index, with_layer](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const PropShape s = shape_of(cx, r);
        const std::vector<i64> keys = keys_of(cx, r, s);
        const i64 at = keys[key_index(cx, arg(a, 0), keys.size())];
        const FrameIndex local = with_layer(cx, r.layer, [&](Composition&, Layer& l) { return l.local_time(FrameIndex{at}); });
        for (const TrackRef& t : s.tracks) {
            Command cmd;
            cmd.type = CommandType::KeyframeDelete;
            cmd.keyframe.track = t;
            cmd.keyframe.time = local;
            (void)self->apply_command(cmd);   // componente sem chave ali: NotFound, sem problema
        }
        return Value::undefined();
    });
    c.method(propProto, "setInterpolationTypeAtKey", [prop_ref, shape_of, keys_of, key_index, with_layer, apply](Context& cx, const Value& me, std::vector<Value>& a) {
        const PropRef r = prop_ref(cx, me);
        const PropShape s = shape_of(cx, r);
        const std::vector<i64> keys = keys_of(cx, r, s);
        const i64 at = keys[key_index(cx, arg(a, 0), keys.size())];
        std::string type = cx.to_string(arg(a, 1));
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        Interpolation interp;
        if (type == "linear") interp = Interpolation::Linear;
        else if (type == "hold") interp = Interpolation::Hold;
        else if (type == "bezier") interp = Interpolation::Bezier;
        else if (type == "ease" || type == "easeinout") interp = Interpolation::EaseInOut;
        else if (type == "easein") interp = Interpolation::EaseIn;
        else if (type == "easeout") interp = Interpolation::EaseOut;
        else cx.throw_error("TypeError", "interpolacao desconhecida \"" + type + "\" (linear, hold, bezier, ease, easeIn, easeOut)");
        const FrameIndex local = with_layer(cx, r.layer, [&](Composition&, Layer& l) { return l.local_time(FrameIndex{at}); });
        Command cmd;
        cmd.type = CommandType::KeyframeSetInterpolation;
        cmd.keyframe_interp.track = s.tracks[0];
        cmd.keyframe_interp.time = local;
        cmd.keyframe_interp.interp = interp;
        cmd.keyframe_interp.power = 0;
        cmd.keyframe_interp.linkAxes = 1;
        cmd.keyframe_interp.bx1 = 0.33f; cmd.keyframe_interp.by1 = 0.0f;
        cmd.keyframe_interp.bx2 = 0.67f; cmd.keyframe_interp.by2 = 1.0f;
        apply(cx, cmd, "setInterpolationTypeAtKey");
        return Value::undefined();
    });
}

} // namespace aurea
