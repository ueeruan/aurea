// =============================================================================
//  Aurea / engine / EngineMcpTools.cpp — ver EngineMcpTools.hpp
//
//  Unidades (as mesmas do motor, ditas nas descrições das ferramentas):
//    tempo  = segundos da COMPOSIÇÃO (o motor guarda keyframes no tempo local
//             da camada; a conversão mora aqui);
//    escala = fração (1 = 100 %), opacidade 0..1, rotação em graus, posição e
//             âncora em px; cores [r, g, b(, a)] 0..1.
//  Endereço de propriedade: "position", "position.x", "scale", "rotation"
//  (número = Z), "anchor", "opacity", "skew.x", "audio.volume",
//  "text.tracking", "effect.<id do efeito>.<índice ou id do parâmetro>[.<componente>]".
//
//  Sem exceções (o motor compila sem elas no Android): cada caminho valida e
//  devolve JSON.
// =============================================================================
#include "EngineMcpTools.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "../project/AeImportBind.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace aurea {
namespace mcp_tools {

namespace {

// --- JSON ---------------------------------------------------------------------------

std::string fail(std::string_view message) {
    json::Writer w;
    w.begin_object().key("ok").value(false).key("error").value(message).end_object();
    return w.str();
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool contains(std::string_view hay, std::string_view needleLower) {
    return needleLower.empty() || lower(hay).find(needleLower) != std::string::npos;
}

std::string str_arg(const json::Value& a, const char* key) {
    const json::Value* v = a.get(key);
    return v && v->is_string() ? v->string : std::string{};
}

bool num_arg(const json::Value& a, const char* key, f64& out) {
    const json::Value* v = a.get(key);
    if (!v || !v->is_number() || !std::isfinite(v->number)) return false;
    out = v->number;
    return true;
}

bool bool_arg(const json::Value& a, const char* key, bool fallback) {
    const json::Value* v = a.get(key);
    return v && v->type == json::Value::Type::Bool ? v->boolean : fallback;
}

/// Seis casas: o bastante para quadros a 120 fps e px, sem lixo de f32.
f64 tidy(f64 v) { return std::isfinite(v) ? std::round(v * 1e6) / 1e6 : 0.0; }

const char* kind_name(LayerKind k) noexcept {
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
        case LayerKind::Composition: return "precomp";
        default: return "unknown";
    }
}

constexpr const char* kInterpNames[] = {
    "hold", "linear", "bezier", "ease_in", "ease_out", "ease_in_out", "custom", "bounce", "elastic", "steps",
    "overshoot", "repeat", "ping_pong", "wave", "random",
};
static_assert(sizeof(kInterpNames) / sizeof(kInterpNames[0]) == static_cast<usize>(kLastInterpolation) + 1,
              "um nome por Interpolation");

const char* interp_name(Interpolation i) {
    const u32 n = static_cast<u32>(i);
    return n <= static_cast<u32>(kLastInterpolation) ? kInterpNames[n] : "linear";
}

const char* param_type_name(ParamType t) {
    switch (t) {
        case ParamType::Float: return "float";
        case ParamType::Int: return "int";
        case ParamType::Bool: return "bool";
        case ParamType::Color: return "color";
        case ParamType::Point2D: return "point2d";
        case ParamType::Point3D: return "point3d";
        case ParamType::Angle: return "angle";
        case ParamType::Enum: return "enum";
        case ParamType::Curve: return "curve";
        case ParamType::Gradient: return "gradient";
        case ParamType::LayerReference: return "layer";
        case ParamType::TextureReference: return "texture";
    }
    return "float";
}

std::string color_hex(u32 rgba) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", rgba & 0xFFu, (rgba >> 8) & 0xFFu, (rgba >> 16) & 0xFFu);
    return buf;
}

/// "#RRGGBB" → RGBA8 (r no byte baixo, alfa cheio). false = formato errado.
bool parse_hex(std::string_view s, u32& out) {
    if (s.size() == 7 && s[0] == '#') s.remove_prefix(1);
    if (s.size() != 6) return false;
    u32 v = 0;
    for (char c : s) {
        const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                    : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = (v << 4) | static_cast<u32>(d);
    }
    out = 0xFF000000u | ((v >> 16) & 0xFFu) | (v & 0xFF00u) | ((v & 0xFFu) << 16);
    return true;
}

void write_floats(json::Writer& w, const f32* v, u32 n) {
    if (n == 1) { w.value(tidy(v[0])); return; }
    w.begin_array();
    for (u32 i = 0; i < n; ++i) w.value(tidy(v[i]));
    w.end_array();
}

/// Lista de números do JSON (número sozinho = lista de um). false = outro tipo.
bool numbers(const json::Value* v, std::vector<f64>& out, usize maxCount = 4) {
    out.clear();
    if (!v) return false;
    if (v->is_number()) { if (!std::isfinite(v->number)) return false; out.push_back(v->number); return true; }
    if (!v->is_array() || v->array.empty() || v->array.size() > maxCount) return false;
    for (const auto& x : v->array) {
        if (!x.is_number() || !std::isfinite(x.number)) return false;
        out.push_back(x.number);
    }
    return true;
}

// --- contexto -----------------------------------------------------------------------

struct Ctx {
    Engine& e;
    std::mutex& model;
    Composition* comp() {
        Project* p = e.project();
        return p ? p->timeline().composition(p->timeline().current()) : nullptr;
    }
};

f64 fps_of(const Composition& c) { return c.fps() > 0.0 ? c.fps() : 30.0; }

/// Segundos da composição → quadro da composição (sem limite superior além de 24 h).
bool seconds_to_frame(f64 seconds, f64 fps, i64& frame) {
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > 86400.0) return false;
    frame = static_cast<i64>(std::llround(seconds * fps));
    return true;
}

f64 frame_seconds(i64 frame, f64 fps) { return tidy(static_cast<f64>(frame) / fps); }

/// "layer": id em texto (de get_project_state) ou o índice do AE (número, 1 = a de cima).
bool find_layer(const json::Value& args, const Composition& comp, u64& id) {
    const json::Value* v = args.get("layer");
    if (!v) return false;
    if (v->is_string()) {
        char* end = nullptr;
        const unsigned long long n = std::strtoull(v->string.c_str(), &end, 10);
        if (v->string.empty() || !end || *end || n == 0) return false;
        id = static_cast<u64>(n);
        return comp.layer(LayerId::unpack(id)) != nullptr;
    }
    if (v->is_number() && v->number >= 1 && std::floor(v->number) == v->number) {
        const u32 n = static_cast<u32>(comp.order().size());
        const u32 index = static_cast<u32>(v->number);
        if (index > n) return false;
        id = comp.order().at(n - index).pack();
        return comp.layer(LayerId::unpack(id)) != nullptr;
    }
    return false;
}

constexpr const char* kNoLayer = "layer not found: pass the id string from get_project_state (or the AE index number, 1 = top)";

// --- propriedades ---------------------------------------------------------------------

enum class PropKind : u8 { Transform, Effect, Volume, Tracking };

struct Channel {
    TrackRef ref;
    std::string name;
    f32 value = 0.0f;           ///< no instante pedido
    bool expression = false;
    std::vector<Keyframe> keys; ///< tempo LOCAL
};

struct Prop {
    PropKind kind = PropKind::Transform;
    std::string name;
    std::vector<Channel> ch;
    std::string base;           ///< "rotation", "scale"... (número sozinho)
    // efeito
    u32 effectId = kInvalidIndex;
    u32 param = 0;
    ParamType type = ParamType::Float;
    bool animatable = true;
    f32 constant[4]{};
};

struct TransformName { const char* name; TrackProperty first; u32 count; };
constexpr TransformName kTransform[] = {
    {"position", TrackProperty::PositionX, 3}, {"scale", TrackProperty::ScaleX, 3},
    {"rotation", TrackProperty::RotationX, 3}, {"anchor", TrackProperty::AnchorX, 3},
    {"opacity", TrackProperty::Opacity, 1}, {"skew", TrackProperty::SkewX, 2},
};
constexpr const char* kAxis[] = {"x", "y", "z", "w"};

f32 transform_base(const Layer& l, u32 p) {
    const Transform& t = l.transform;
    const f32 v[] = {t.position.x, t.position.y, t.position.z, t.scale.x, t.scale.y, t.scale.z,
                     t.rotation.x, t.rotation.y, t.rotation.z, t.anchor.x, t.anchor.y, t.anchor.z,
                     t.opacity, t.skewX, t.skewY};
    return p < 15 ? v[p] : 0.0f;
}

void fill_channel(const Layer& l, Channel& c, FrameIndex local, f32 base) {
    const Track* t = l.tracks.find(c.ref.property, c.ref.effectIndex, c.ref.effectParamIndex);
    c.value = t ? t->value_or(local, base) : base;
    c.expression = t && t->has_expression();
    if (t) c.keys = t->keys;
}

const EffectInstance* effect_of(const Layer& l, u32 id) {
    for (const EffectInstance& e : l.effects) if (e.id == id) return &e;
    return nullptr;
}

/// Parâmetro de efeito por índice ("3"), id estável ("radius") ou rótulo.
u32 find_param(const ParameterRegistry& specs, std::string_view token) {
    if (!token.empty() && std::all_of(token.begin(), token.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        const u32 i = static_cast<u32>(std::strtoul(std::string(token).c_str(), nullptr, 10));
        return i < specs.count() ? i : kInvalidIndex;
    }
    const u32 byId = specs.find(token);
    if (byId < specs.count()) return byId;
    const std::string want = lower(token);
    for (u32 i = 0; i < specs.count(); ++i)
        if (specs.at(i).label && lower(specs.at(i).label) == want) return i;
    return kInvalidIndex;
}

/// Resolve o endereço da propriedade (sob o lock do modelo).
bool resolve(Engine& e, const Layer& l, u64 layerId, std::string_view path, FrameIndex local, Prop& out, std::string& err) {
    const LayerId lid = LayerId::unpack(layerId);
    out.name = std::string(path);
    // Efeito: effect.<id>.<parâmetro>[.<componente>]
    if (path.substr(0, 7) == "effect.") {
        std::vector<std::string> parts;
        std::string_view rest = path.substr(7);
        while (!rest.empty()) {
            const usize dot = rest.find('.');
            parts.emplace_back(rest.substr(0, dot));
            if (dot == std::string_view::npos) break;
            rest.remove_prefix(dot + 1);
        }
        if (parts.size() < 2 || parts.size() > 3) { err = "effect property must be effect.<effectId>.<param>[.<component>]"; return false; }
        char* end = nullptr;
        const unsigned long fx = std::strtoul(parts[0].c_str(), &end, 10);
        const EffectInstance* inst = (!parts[0].empty() && end && !*end) ? effect_of(l, static_cast<u32>(fx)) : nullptr;
        if (!inst) { err = "effect not found on this layer (ids from get_layer_details)"; return false; }
        const ParameterRegistry* specs = e.effects().params(inst->type);
        if (!specs) { err = "effect type not available on this device"; return false; }
        const u32 p = find_param(*specs, parts[1]);
        if (p == kInvalidIndex) { err = "parameter not found (indexes and ids from get_effect)"; return false; }
        const ParamSpec& spec = specs->at(p);
        const u32 comps = std::max<u32>(1, component_count(spec.type));
        out.kind = PropKind::Effect;
        out.effectId = inst->id;
        out.param = p;
        out.type = spec.type;
        out.animatable = spec.animatable();
        if (p < inst->params.size()) for (u32 c = 0; c < 4; ++c) out.constant[c] = inst->params[p].constant.v[c];
        const ParamValue value = evaluate_param(l.tracks, *inst, p, spec, local);
        u32 first = 0, count = comps;
        if (parts.size() == 3) {
            u32 c = kInvalidIndex;
            for (u32 k = 0; k < 4; ++k) if (parts[2] == kAxis[k] || parts[2] == std::to_string(k)) c = k;
            if (parts[2] == "r") c = 0; else if (parts[2] == "g") c = 1; else if (parts[2] == "b") c = 2; else if (parts[2] == "a") c = 3;
            if (c >= comps) { err = "component out of range for this parameter"; return false; }
            first = c; count = 1;
        }
        for (u32 c = first; c < first + count; ++c) {
            Channel ch;
            ch.ref = TrackRef{lid, TrackProperty::EffectParam, inst->id, param_track_key(p, c)};
            ch.name = "effect." + std::to_string(inst->id) + "." + (spec.id && *spec.id ? spec.id : std::to_string(p))
                    + (comps > 1 ? std::string(".") + kAxis[c] : std::string{});
            const Track* t = l.tracks.find(TrackProperty::EffectParam, inst->id, param_track_key(p, c));
            ch.value = value.v[c];
            ch.expression = t && t->has_expression();
            if (t) ch.keys = t->keys;
            out.ch.push_back(std::move(ch));
        }
        return true;
    }
    if (path == "audio.volume") {
        out.kind = PropKind::Volume;
        Channel c; c.ref = TrackRef{lid, TrackProperty::AudioVolume, kInvalidIndex, 0}; c.name = "audio.volume";
        const Track* t = l.tracks.find(TrackProperty::AudioVolume);
        fill_channel(l, c, local, t ? t->staticValue : 1.0f);
        out.ch.push_back(std::move(c));
        return true;
    }
    if (path == "text.tracking") {
        if (l.kind != LayerKind::Text) { err = "text.tracking is only for text layers"; return false; }
        out.kind = PropKind::Tracking;
        Channel c; c.ref = TrackRef{lid, TrackProperty::TextTracking, kInvalidIndex, 0}; c.name = "text.tracking";
        fill_channel(l, c, local, l.text.tracking);
        out.ch.push_back(std::move(c));
        return true;
    }
    const usize dot = path.find('.');
    const std::string_view head = path.substr(0, dot);
    for (const TransformName& t : kTransform) {
        if (head != t.name) continue;
        u32 first = 0, count = t.count;
        if (dot != std::string_view::npos) {
            const std::string_view axis = path.substr(dot + 1);
            u32 a = kInvalidIndex;
            for (u32 k = 0; k < t.count; ++k) if (axis == kAxis[k]) a = k;
            if (a == kInvalidIndex) { err = "unknown axis (use .x, .y or .z)"; return false; }
            first = a; count = 1;
        }
        out.kind = PropKind::Transform;
        out.base = t.name;
        for (u32 k = first; k < first + count; ++k) {
            Channel c;
            const u32 p = static_cast<u32>(t.first) + k;
            c.ref = TrackRef{lid, static_cast<TrackProperty>(p), kInvalidIndex, 0};
            c.name = t.count > 1 ? std::string(t.name) + "." + kAxis[k] : std::string(t.name);
            fill_channel(l, c, local, transform_base(l, p));
            out.ch.push_back(std::move(c));
        }
        return true;
    }
    err = "unknown property; use position, scale, rotation, anchor, opacity, skew (with .x/.y/.z), audio.volume, "
          "text.tracking or effect.<effectId>.<param>";
    return false;
}

/// Nome legível de uma trilha qualquer da camada (get_keyframes sem propriedade).
std::string track_name(const Track& t) {
    const u32 p = static_cast<u32>(t.property);
    if (p < 15) {
        for (const TransformName& n : kTransform) {
            const u32 f = static_cast<u32>(n.first);
            if (p >= f && p < f + n.count) return n.count > 1 ? std::string(n.name) + "." + kAxis[p - f] : std::string(n.name);
        }
    }
    if (t.property == TrackProperty::EffectParam)
        return "effect." + std::to_string(t.effectIndex) + "." + std::to_string(t.effectParamIndex / 4) + "." + kAxis[t.effectParamIndex % 4];
    if (t.property == TrackProperty::AudioVolume) return "audio.volume";
    if (t.property == TrackProperty::TextTracking) return "text.tracking";
    return "track:" + std::to_string(p) + (t.effectIndex != kInvalidIndex ? ":" + std::to_string(t.effectIndex) : "")
         + ":" + std::to_string(t.effectParamIndex);
}

void write_keys(json::Writer& w, const Layer& l, const std::vector<Keyframe>& keys, f64 fps, f32 scale = 1.0f) {
    w.begin_array();
    for (const Keyframe& k : keys) {
        const i64 frame = l.timeline_time(k.time).value;
        w.begin_object().key("seconds").value(frame_seconds(frame, fps)).key("frame").value(frame)
            .key("value").value(tidy(k.value * scale)).key("interpolation").value(interp_name(k.interp));
        if (k.interp == Interpolation::Bezier || k.interp == Interpolation::CustomCurve) {
            w.key("bezier").begin_array().value(tidy(k.bx1)).value(tidy(k.by1)).value(tidy(k.bx2)).value(tidy(k.by2)).end_array();
            if (k.easePower > 1) w.key("power").value(static_cast<u32>(k.easePower));
        }
        w.end_object();
    }
    w.end_array();
}

// --- comandos -----------------------------------------------------------------------------

/// Um passo de desfazer para tudo o que a ferramenta mexe.
struct UndoGroup {
    Engine& e;
    UndoGroup(Engine& engine, const char* label) : e(engine) {
        Command c; c.type = CommandType::UndoBeginGroup;
        (void)e.apply_command(c, label);
    }
    ~UndoGroup() { Command c; c.type = CommandType::UndoEndGroup; (void)e.apply_command(c); }
    UndoGroup(const UndoGroup&) = delete;
    UndoGroup& operator=(const UndoGroup&) = delete;
};

std::string status_text(const Status& s) {
    std::string out(s.message());
    if (!s.detail().empty()) out += ": " + std::string(s.detail());
    return out;
}

Status insert_key(Engine& e, const TrackRef& ref, i64 local, f32 value) {
    Command c; c.type = CommandType::KeyframeInsert;
    c.keyframe.track = ref; c.keyframe.time = FrameIndex{local}; c.keyframe.value = value;
    return e.apply_command(c);
}

struct Ease { bool set = false; Interpolation interp = Interpolation::Linear; f32 b[4]{0.33f, 0.0f, 0.67f, 1.0f}; u8 power = 0; };

/// "interpolation" + "bezier" [x1,y1,x2,y2] + "power" 1..3 de um objeto.
bool read_ease(const json::Value& o, Ease& ease, std::string& err) {
    const std::string name = lower(str_arg(o, "interpolation"));
    const json::Value* bz = o.get("bezier");
    if (name.empty() && !bz) return true;
    ease.set = true;
    ease.interp = bz ? Interpolation::Bezier : Interpolation::Linear;
    if (!name.empty()) {
        bool found = false;
        for (u32 i = 0; i <= static_cast<u32>(kLastInterpolation); ++i)
            if (name == kInterpNames[i]) { ease.interp = static_cast<Interpolation>(i); found = true; }
        if (!found) { err = "unknown interpolation (hold, linear, bezier, ease_in, ease_out, ease_in_out, bounce, elastic, overshoot, steps...)"; return false; }
    }
    if (bz) {
        std::vector<f64> v;
        if (!numbers(bz, v) || v.size() != 4 || v[0] < 0 || v[0] > 1 || v[2] < 0 || v[2] > 1 || std::fabs(v[1]) > 10 || std::fabs(v[3]) > 10) {
            err = "bezier must be [x1, y1, x2, y2] with x in 0..1"; return false;
        }
        for (u32 i = 0; i < 4; ++i) ease.b[i] = static_cast<f32>(v[i]);
        if (ease.interp != Interpolation::Bezier && ease.interp != Interpolation::CustomCurve) ease.interp = Interpolation::Bezier;
    }
    f64 power = 0;
    if (num_arg(o, "power", power)) ease.power = clamp_ease_power(static_cast<u32>(std::llround(power)));
    return true;
}

Status apply_ease(Engine& e, const TrackRef& ref, i64 local, const Ease& ease) {
    Command c; c.type = CommandType::KeyframeSetInterpolation;
    c.keyframe_interp.track = ref; c.keyframe_interp.time = FrameIndex{local};
    c.keyframe_interp.interp = ease.interp; c.keyframe_interp.power = ease.power; c.keyframe_interp.linkAxes = 0;
    c.keyframe_interp.bx1 = ease.b[0]; c.keyframe_interp.by1 = ease.b[1];
    c.keyframe_interp.bx2 = ease.b[2]; c.keyframe_interp.by2 = ease.b[3];
    return e.apply_command(c);
}

/// O valor pedido distribuído pelos canais da propriedade: (canal, valor).
bool spread_value(const Prop& p, const json::Value* v, std::vector<std::pair<u32, f32>>& out, std::string& err) {
    out.clear();
    std::vector<f64> nums;
    if (!numbers(v, nums)) { err = "value must be a number or an array of numbers"; return false; }
    const u32 n = static_cast<u32>(p.ch.size());
    if (nums.size() == 1 && n > 1) {
        if (p.base == "rotation") { out.emplace_back(2, static_cast<f32>(nums[0])); return true; }   // Rotação do AE = Z
        if (p.base == "scale") { for (u32 c = 0; c < n; ++c) out.emplace_back(c, static_cast<f32>(nums[0])); return true; }
        err = "this property has " + std::to_string(n) + " components: pass an array"; return false;
    }
    if (nums.size() > n) { err = "too many components for this property (" + std::to_string(n) + ")"; return false; }
    for (u32 c = 0; c < nums.size(); ++c) out.emplace_back(c, static_cast<f32>(nums[c]));
    return true;
}

f32 clamp_for(const Prop& p, const Channel& c, f32 v) {
    if (p.kind == PropKind::Transform && c.ref.property == TrackProperty::Opacity) return std::clamp(v, 0.0f, 1.0f);
    if (p.kind == PropKind::Volume) return std::clamp(v, 0.0f, 2.0f);
    return v;
}

/// Lê a camada e a propriedade sob o lock. `seconds` < 0 = cabeçote.
struct Target {
    u64 layer = 0;
    f64 fps = 30.0;
    i64 frame = 0;      ///< quadro da composição pedido (ou o cabeçote)
    i64 local = 0;      ///< o mesmo no tempo da camada
    i64 duration = 0;
    i64 start = 0, offset = 0;
    bool locked = false;
    LayerKind kind = LayerKind::Unknown;
    Prop prop;
};

bool read_target(Ctx& ctx, const json::Value& args, bool needProp, const f64* seconds, Target& t, std::string& err) {
    std::lock_guard<std::mutex> lock(ctx.model);
    Composition* comp = ctx.comp();
    if (!comp) { err = "no project open"; return false; }
    if (!find_layer(args, *comp, t.layer)) { err = kNoLayer; return false; }
    const Layer* l = comp->layer(LayerId::unpack(t.layer));
    t.fps = fps_of(*comp);
    t.duration = comp->duration().value;
    t.frame = ctx.e.playback().current().value;
    if (seconds && !seconds_to_frame(*seconds, t.fps, t.frame)) { err = "seconds must be a number >= 0"; return false; }
    t.local = l->local_time(FrameIndex{t.frame}).value;
    t.start = l->start.value; t.offset = l->offset.value;
    t.locked = l->locked;
    t.kind = l->kind;
    if (!needProp) return true;
    const std::string path = str_arg(args, "property");
    if (path.empty()) { err = "missing property"; return false; }
    return resolve(ctx.e, *l, t.layer, path, FrameIndex{t.local}, t.prop, err);
}

i64 to_local(const Target& t, i64 compFrame) { return compFrame - t.start + t.offset; }

// =============================================================================
// Ferramentas
// =============================================================================

std::string get_layer_details(Ctx& ctx, const json::Value& args) {
    f64 seconds = 0;
    const bool at = num_arg(args, "seconds", seconds);
    std::lock_guard<std::mutex> lock(ctx.model);
    Composition* comp = ctx.comp();
    if (!comp) return fail("no project open");
    u64 id = 0;
    if (!find_layer(args, *comp, id)) return fail(kNoLayer);
    const Layer& l = *comp->layer(LayerId::unpack(id));
    const f64 fps = fps_of(*comp);
    i64 frame = ctx.e.playback().current().value;
    if (at && !seconds_to_frame(seconds, fps, frame)) return fail("seconds must be a number >= 0");
    const FrameIndex local = l.local_time(FrameIndex{frame});
    const u32 n = static_cast<u32>(comp->order().size());
    u32 index = 0;
    for (u32 z = 0; z < n; ++z) if (comp->order().at(z).pack() == id) index = n - z;

    json::Writer w;
    w.begin_object().key("ok").value(true)
        .key("id").value(std::to_string(id)).key("index").value(index).key("name").value(l.name)
        .key("kind").value(kind_name(l.kind)).key("atSeconds").value(frame_seconds(frame, fps))
        .key("startSeconds").value(frame_seconds(l.start.value, fps)).key("endSeconds").value(frame_seconds(l.end.value, fps))
        .key("contentOffsetSeconds").value(frame_seconds(l.offset.value, fps))
        .key("threeD").value(l.threeD).key("visible").value(l.visible).key("locked").value(l.locked)
        .key("blendMode").value(static_cast<u32>(l.blendMode));
    if (l.parent.valid() && comp->layer(l.parent)) w.key("parent").value(std::to_string(l.parent.pack()));
    if (l.speed != 1.0f) w.key("speed").value(tidy(l.speed));
    if (l.reversed) w.key("reversed").value(true);

    // Transform: valor no instante + se está animado (e quantos keyframes).
    w.key("transform").begin_object();
    for (const TransformName& t : kTransform) {
        f32 v[3]{};
        u32 keys = 0;
        bool expression = false;
        for (u32 k = 0; k < t.count; ++k) {
            const u32 p = static_cast<u32>(t.first) + k;
            const Track* tr = l.tracks.find(static_cast<TrackProperty>(p));
            v[k] = tr ? tr->value_or(local, transform_base(l, p)) : transform_base(l, p);
            if (tr) { keys += static_cast<u32>(tr->keys.size()); expression |= tr->has_expression(); }
        }
        w.key(t.name).begin_object().key("value");
        write_floats(w, v, t.count);
        if (keys) w.key("keyframes").value(keys);
        if (expression) w.key("expression").value(true);
        w.end_object();
    }
    w.end_object();

    // Efeitos: o resumo (os parâmetros saem em get_effect).
    w.key("effects").begin_array();
    for (const EffectInstance& inst : l.effects) {
        const Effect* fx = ctx.e.effects().find(inst.type);
        u32 animated = 0;
        for (u32 t = 0; t < l.tracks.size(); ++t) {
            const Track& tr = l.tracks.at(t);
            if (tr.property == TrackProperty::EffectParam && tr.effectIndex == inst.id && !tr.keys.empty()) ++animated;
        }
        w.begin_object().key("id").value(inst.id).key("key").value(fx ? fx->info().key : "")
            .key("name").value(fx ? fx->info().name : "unavailable effect").key("enabled").value(inst.enabled)
            .key("params").value(static_cast<u32>(inst.params.size()));
        if (animated) w.key("animatedChannels").value(animated);
        if (inst.role) w.key("role").value(inst.role == kEffectRoleShadow ? "shadow" : "border");
        w.end_object();
    }
    w.end_array();
    if (!l.masks.empty()) w.key("masks").value(static_cast<u32>(l.masks.size()));
    if (!l.markers.empty()) w.key("markers").value(static_cast<u32>(l.markers.size()));

    switch (l.kind) {
        case LayerKind::Text: {
            const TextData& t = l.text;
            const Track* tr = l.tracks.find(TrackProperty::TextTracking);
            static const char* align[] = {"left", "center", "right"};
            w.key("text").begin_object().key("content").value(t.content.substr(0, 4000))
                .key("font").value(t.fontFamily.empty() ? "default" : t.fontFamily).key("weight").value(static_cast<u32>(t.fontWeight))
                .key("italic").value(t.fontItalic).key("size").value(tidy(t.size)).key("color");
            write_floats(w, &t.color.x, 4);
            w.key("strokeWidth").value(tidy(t.strokeWidth)).key("strokeColor");
            write_floats(w, &t.strokeColor.x, 4);
            w.key("alignment").value(align[std::min(t.alignment, 2u)]).key("lineHeight").value(tidy(t.lineHeight))
                .key("tracking").value(tidy(tr ? tr->value_or(local, t.tracking) : t.tracking))
                .key("textAnimators").value(static_cast<u32>(t.animators.size()));
            if (!l.captions.empty()) w.key("captionBlocks").value(static_cast<u32>(l.captions.size()));
            w.end_object();
            break;
        }
        case LayerKind::Shape: {
            const ShapeData& s = l.shape;
            w.key("shape").begin_object().key("type").value(s.shapeType)
                .key("width").value(tidy(s.bounds.w)).key("height").value(tidy(s.bounds.h)).key("filled").value(s.filled).key("fill");
            write_floats(w, &s.fillColor.x, 4);
            w.key("strokeWidth").value(tidy(s.strokeWidth)).key("strokeColor");
            write_floats(w, &s.strokeColor.x, 4);
            w.key("cornerRadius").value(tidy(s.cornerRadius)).end_object();
            break;
        }
        case LayerKind::Camera:
            w.key("camera").begin_object().key("focalLengthMm").value(tidy(l.camera.focalLength)).key("fovDegrees").value(tidy(l.camera.fov))
                .key("depthOfField").value(l.camera.dofEnabled).key("focusDistance").value(tidy(l.camera.focusDistance))
                .key("aperture").value(tidy(l.camera.aperture)).key("twoNode").value(l.camera.twoNode).end_object();
            break;
        case LayerKind::Light:
            w.key("light").begin_object().key("type").value(static_cast<u32>(l.light.kind)).key("intensity").value(tidy(l.light.intensity))
                .key("color");
            write_floats(w, &l.light.color.x, 3);
            w.key("coneAngle").value(tidy(l.light.coneAngle)).key("castShadows").value(l.light.castShadows).end_object();
            break;
        default: break;
    }
    if (l.kind == LayerKind::Video || l.kind == LayerKind::Audio) {
        const Track* vol = l.tracks.find(TrackProperty::AudioVolume);
        w.key("audio").begin_object().key("gain").value(tidy(l.gain))
            .key("volume").value(tidy(vol ? vol->value_or(local, vol->staticValue) : 1.0f))
            .key("muted").value(l.muted).key("pan").value(tidy(l.pan)).end_object();
    }
    w.end_object();
    return w.str();
}

std::string get_keyframes(Ctx& ctx, const json::Value& args) {
    if (str_arg(args, "property").empty()) {
        // Sem propriedade: todas as trilhas com keyframe da camada.
        std::lock_guard<std::mutex> lock(ctx.model);
        Composition* comp = ctx.comp();
        if (!comp) return fail("no project open");
        u64 id = 0;
        if (!find_layer(args, *comp, id)) return fail(kNoLayer);
        const Layer& l = *comp->layer(LayerId::unpack(id));
        const f64 fps = fps_of(*comp);
        json::Writer w;
        w.begin_object().key("ok").value(true).key("layer").value(std::to_string(id)).key("channels").begin_array();
        u32 written = 0;
        for (u32 i = 0; i < l.tracks.size() && written < 200; ++i) {
            const Track& t = l.tracks.at(i);
            if (t.keys.empty()) continue;
            w.begin_object().key("property").value(track_name(t)).key("count").value(static_cast<u32>(t.keys.size()));
            if (t.has_expression()) w.key("expression").value(true);
            w.key("keys");
            write_keys(w, l, std::vector<Keyframe>(t.keys.begin(), t.keys.begin() + std::min<usize>(t.keys.size(), 100)), fps);
            w.end_object();
            ++written;
        }
        w.end_array().end_object();
        return w.str();
    }
    Target t;
    std::string err;
    if (!read_target(ctx, args, true, nullptr, t, err)) return fail(err);
    // As chaves saem com o tempo da composição (o mesmo relógio de get_project_state).
    json::Writer w;
    w.begin_object().key("ok").value(true).key("layer").value(std::to_string(t.layer)).key("property").value(t.prop.name)
        .key("channels").begin_array();
    for (const Channel& c : t.prop.ch) {
        w.begin_object().key("channel").value(c.name).key("valueAtPlayhead").value(tidy(c.value));
        if (c.expression) w.key("expression").value(true);
        w.key("keys").begin_array();
        for (const Keyframe& k : c.keys) {
            const i64 frame = k.time.value + t.start - t.offset;
            w.begin_object().key("seconds").value(frame_seconds(frame, t.fps)).key("frame").value(frame)
                .key("value").value(tidy(k.value)).key("interpolation").value(interp_name(k.interp));
            if (k.interp == Interpolation::Bezier || k.interp == Interpolation::CustomCurve) {
                w.key("bezier").begin_array().value(tidy(k.bx1)).value(tidy(k.by1)).value(tidy(k.bx2)).value(tidy(k.by2)).end_array();
                if (k.easePower > 1) w.key("power").value(static_cast<u32>(k.easePower));
            }
            w.end_object();
        }
        w.end_array().end_object();
    }
    w.end_array().end_object();
    return w.str();
}

/// Valor parado (sem keyframe) de um canal.
Status set_static(Ctx& ctx, const Target& t, const Prop& p, const std::vector<std::pair<u32, f32>>& values, const json::Value& args, std::string& err) {
    const LayerId lid = LayerId::unpack(t.layer);
    switch (p.kind) {
        case PropKind::Transform:
            for (const auto& [c, v] : values) {
                Command cmd; cmd.type = CommandType::KeyframeInsert;
                cmd.keyframe.track = p.ch[c].ref; cmd.keyframe.time = FrameIndex{t.local};
                cmd.keyframe.value = clamp_for(p, p.ch[c], v);
                cmd.keyframe.onlyIfChanged = kAutoKeyStaticWhenUnanimated;   // sem keyframe: o valor parado muda
                const Status s = ctx.e.apply_command(cmd);
                if (!s.ok()) return s;
            }
            return OkStatus;
        case PropKind::Volume: {
            Command cmd; cmd.type = CommandType::AudioSetVolume;
            cmd.audio_gain.layer = lid; cmd.audio_gain.gain = clamp_for(p, p.ch[0], values[0].second);
            return ctx.e.apply_command(cmd);
        }
        case PropKind::Tracking: {
            f32 style[22]{};
            if (!ctx.e.query_text_style(t.layer, style, 22)) return Status{Errc::InvalidState, "text style unavailable"};
            style[19] = values[0].second;
            return ctx.e.set_text_style(t.layer, style, 20) ? OkStatus : Status{Errc::InvalidState, "layer is locked"};
        }
        case PropKind::Effect: {
            const EffectId fx{p.effectId, 0};
            const u32 comps = component_count(p.type);
            if (p.type == ParamType::LayerReference) {
                // Camada de referência: o id dela (texto) ou -1 = nenhuma; o motor quer o índice do LayerId.
                f32 index = -1.0f;
                const json::Value* v = args.get("value");
                if (v && v->is_string()) {
                    std::lock_guard<std::mutex> lock(ctx.model);
                    Composition* comp = ctx.comp();
                    json::Value probe; probe.type = json::Value::Type::Object;
                    probe.object.emplace_back("layer", *v);
                    u64 ref = 0;
                    if (!comp || !find_layer(probe, *comp, ref)) { err = "reference layer not found"; return Errc::NotFound; }
                    index = static_cast<f32>(LayerId::unpack(ref).index);
                }
                Command cmd; cmd.type = CommandType::EffectSetParam;
                cmd.effect_param.layer = lid; cmd.effect_param.effect = fx; cmd.effect_param.paramIndex = p.param; cmd.effect_param.value = index;
                return ctx.e.apply_command(cmd);
            }
            if (comps <= 1) {
                Command cmd; cmd.type = CommandType::EffectSetParam;
                cmd.effect_param.layer = lid; cmd.effect_param.effect = fx; cmd.effect_param.paramIndex = p.param;
                cmd.effect_param.value = values[0].second;
                return ctx.e.apply_command(cmd);
            }
            f32 merged[4]{p.constant[0], p.constant[1], p.constant[2], p.constant[3]};
            // Os canais de `values` são posições dentro de p.ch, que pode ter começado num componente.
            const u32 firstComp = p.ch[0].ref.effectParamIndex % 4;
            for (const auto& [c, v] : values) merged[firstComp + c] = v;
            Command cmd; cmd.type = CommandType::EffectSetColorParam;
            cmd.effect_color.layer = lid; cmd.effect_color.effect = fx; cmd.effect_color.paramIndex = p.param;
            cmd.effect_color.r = merged[0]; cmd.effect_color.g = merged[1]; cmd.effect_color.b = merged[2]; cmd.effect_color.a = merged[3];
            return ctx.e.apply_command(cmd);
        }
    }
    return Errc::InvalidArgument;
}

std::string set_property(Ctx& ctx, const json::Value& args) {
    f64 seconds = 0;
    const bool timed = num_arg(args, "seconds", seconds);
    Target t;
    std::string err;
    if (!read_target(ctx, args, true, timed ? &seconds : nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    const Prop& p = t.prop;
    std::vector<std::pair<u32, f32>> values;
    const json::Value* raw = args.get("value");
    if (p.kind == PropKind::Effect && p.type == ParamType::LayerReference && raw && raw->is_string()) values.emplace_back(0, 0.0f);
    else if (!spread_value(p, raw, values, err)) return fail(err);
    for (const auto& [c, v] : values) if (p.ch[c].expression) return fail(p.ch[c].name + " is controlled by an expression");
    if (timed && p.kind == PropKind::Effect && !p.animatable) return fail("this parameter cannot be animated");
    bool keyed = timed;
    for (const auto& [c, v] : values) keyed |= !p.ch[c].keys.empty();
    if (p.kind == PropKind::Effect && !p.animatable) keyed = false;
    UndoGroup undo(ctx.e, "agente: propriedade");
    if (!keyed) {
        const Status s = set_static(ctx, t, p, values, args, err);
        if (!s.ok()) return fail(err.empty() ? status_text(s) : err);
    } else {
        for (const auto& [c, v] : values) {
            const Status s = insert_key(ctx.e, p.ch[c].ref, t.local, clamp_for(p, p.ch[c], v));
            if (!s.ok()) return fail(p.ch[c].name + ": " + status_text(s));
        }
    }
    json::Writer w;
    w.begin_object().key("ok").value(true).key("property").value(p.name).key("keyframe").value(keyed);
    if (keyed) w.key("seconds").value(frame_seconds(t.frame, t.fps));
    w.end_object();
    return w.str();
}

std::string add_keyframes(Ctx& ctx, const json::Value& args) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, true, nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    const Prop& p = t.prop;
    if (p.kind == PropKind::Effect && !p.animatable) return fail("this parameter cannot be animated");
    for (const Channel& c : p.ch) if (c.expression) return fail(c.name + " is controlled by an expression");
    const json::Value* keys = args.get("keys");
    if (!keys || !keys->is_array() || keys->array.empty() || keys->array.size() > 500) return fail("keys must be a list of 1..500 {seconds, value}");
    struct Key { i64 frame; std::vector<std::pair<u32, f32>> values; Ease ease; };
    std::vector<Key> plan;
    for (const json::Value& k : keys->array) {
        if (!k.is_object()) return fail("each key must be an object {seconds, value}");
        Key key;
        f64 s = 0;
        if (!num_arg(k, "seconds", s) || !seconds_to_frame(s, t.fps, key.frame)) return fail("each key needs seconds >= 0");
        if (!spread_value(p, k.get("value"), key.values, err)) return fail(err);
        if (!read_ease(k, key.ease, err)) return fail(err);
        plan.push_back(std::move(key));
    }
    UndoGroup undo(ctx.e, "agente: keyframes");
    u32 added = 0;
    for (const Key& k : plan) {
        const i64 local = to_local(t, k.frame);
        for (const auto& [c, v] : k.values) {
            const Status s = insert_key(ctx.e, p.ch[c].ref, local, clamp_for(p, p.ch[c], v));
            if (!s.ok()) return fail(p.ch[c].name + ": " + status_text(s));
            ++added;
        }
    }
    // A curva vale para o trecho que SAI do keyframe: aplicada depois de todos existirem.
    for (const Key& k : plan) {
        if (!k.ease.set) continue;
        for (const auto& [c, v] : k.values) (void)apply_ease(ctx.e, p.ch[c].ref, to_local(t, k.frame), k.ease);
    }
    json::Writer w;
    w.begin_object().key("ok").value(true).key("property").value(p.name).key("keyframes").value(added).end_object();
    return w.str();
}

/// Tempos pedidos ("seconds": número ou lista) → quadros locais; vazio + all = todos os keys.
bool key_times(const Target& t, const json::Value& args, std::vector<i64>& locals, bool& all, std::string& err) {
    all = bool_arg(args, "all", false);
    const json::Value* s = args.get("seconds");
    if (!s) { if (!all) { err = "pass seconds (a number or a list) or all: true"; return false; } return true; }
    std::vector<f64> list;
    if (!numbers(s, list, 500)) { err = "seconds must be a number or a list of numbers"; return false; }
    for (f64 v : list) {
        i64 f = 0;
        if (!seconds_to_frame(v, t.fps, f)) { err = "seconds must be >= 0"; return false; }
        locals.push_back(to_local(t, f));
    }
    return true;
}

bool has_key(const Channel& c, i64 local) {
    return std::any_of(c.keys.begin(), c.keys.end(), [local](const Keyframe& k) { return k.time.value == local; });
}

std::string delete_keyframes(Ctx& ctx, const json::Value& args) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, true, nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    std::vector<i64> locals;
    bool all = false;
    if (!key_times(t, args, locals, all, err)) return fail(err);
    UndoGroup undo(ctx.e, "agente: apagar keyframes");
    u32 removed = 0;
    for (const Channel& c : t.prop.ch) {
        for (const Keyframe& k : c.keys) {
            if (!all && std::find(locals.begin(), locals.end(), k.time.value) == locals.end()) continue;
            Command cmd; cmd.type = CommandType::KeyframeDelete;
            cmd.keyframe.track = c.ref; cmd.keyframe.time = k.time;
            if (ctx.e.apply_command(cmd).ok()) ++removed;
        }
    }
    if (removed == 0) return fail("no keyframe at those times");
    json::Writer w;
    w.begin_object().key("ok").value(true).key("deleted").value(removed).end_object();
    return w.str();
}

std::string move_keyframe(Ctx& ctx, const json::Value& args) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, true, nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    f64 from = 0, to = 0;
    i64 fromFrame = 0, toFrame = 0;
    if (!num_arg(args, "fromSeconds", from) || !num_arg(args, "toSeconds", to)
        || !seconds_to_frame(from, t.fps, fromFrame) || !seconds_to_frame(to, t.fps, toFrame)) return fail("fromSeconds and toSeconds must be numbers >= 0");
    const i64 a = to_local(t, fromFrame), b = to_local(t, toFrame);
    u32 found = 0;
    for (const Channel& c : t.prop.ch) {
        if (!has_key(c, a)) continue;
        if (a != b && has_key(c, b)) return fail(c.name + " already has a keyframe at toSeconds");
        ++found;
    }
    if (!found) return fail("no keyframe at fromSeconds");
    if (a == b) return std::string(R"({"ok":true,"moved":0})");
    UndoGroup undo(ctx.e, "agente: mover keyframe");
    u32 moved = 0;
    for (const Channel& c : t.prop.ch) {
        if (!has_key(c, a)) continue;
        Command cmd; cmd.type = CommandType::KeyframeMove;
        cmd.keyframe_move.track = c.ref; cmd.keyframe_move.fromTime = FrameIndex{a}; cmd.keyframe_move.toTime = FrameIndex{b};
        if (ctx.e.apply_command(cmd).ok()) ++moved;
    }
    json::Writer w;
    w.begin_object().key("ok").value(moved > 0).key("moved").value(moved).end_object();
    return w.str();
}

std::string set_keyframe_easing(Ctx& ctx, const json::Value& args) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, true, nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    Ease ease;
    if (!read_ease(args, ease, err)) return fail(err);
    if (!ease.set) return fail("pass interpolation and/or bezier");
    std::vector<i64> locals;
    bool all = !args.get("seconds");
    if (!all && !key_times(t, args, locals, all, err)) return fail(err);
    UndoGroup undo(ctx.e, "agente: curva");
    u32 changed = 0;
    for (const Channel& c : t.prop.ch) {
        for (const Keyframe& k : c.keys) {
            if (!all && std::find(locals.begin(), locals.end(), k.time.value) == locals.end()) continue;
            if (apply_ease(ctx.e, c.ref, k.time.value, ease).ok()) ++changed;
        }
    }
    if (!changed) return fail("no keyframe at those times");
    json::Writer w;
    w.begin_object().key("ok").value(true).key("keyframes").value(changed).key("interpolation").value(interp_name(ease.interp)).end_object();
    return w.str();
}

// --- efeitos ----------------------------------------------------------------------------

/// matchName do AE para a chave do Aurea ("" = sem par).
const char* ae_match_for(const char* key) {
    if (!key) return "";
    for (u32 i = 0; i < ae::detail::rule_count(); ++i) {
        const ae::detail::ERule& r = ae::detail::rule_at(i);
        if (r.key && std::string_view(r.key) == key) return r.ae;
    }
    return "";
}

std::string list_effect_types(Ctx& ctx, const json::Value& args) {
    const std::string q = lower(str_arg(args, "query"));
    f64 limitArg = 60;
    (void)num_arg(args, "limit", limitArg);
    const u32 limit = static_cast<u32>(std::clamp(limitArg, 1.0, 300.0));
    EffectRegistry& reg = ctx.e.effects();
    json::Writer w;
    w.begin_object().key("ok").value(true).key("effects").begin_array();
    u32 listed = 0, matched = 0;
    for (u32 i = 0; i < reg.count(); ++i) {
        if (reg.retired_at(i)) continue;
        const Effect& fx = reg.at(i);
        const EffectInfo& info = fx.info();
        const char* match = ae_match_for(info.key);
        if (!q.empty() && !contains(info.name, q) && !contains(info.key, q) && !contains(info.category, q) && !contains(match, q)) continue;
        ++matched;
        if (listed >= limit) continue;
        w.begin_object().key("key").value(info.key).key("name").value(info.name).key("category").value(info.category)
            .key("params").value(reg.params_at(i).count());
        if (*match) w.key("aeMatchName").value(match);
        w.end_object();
        ++listed;
    }
    w.end_array().key("matched").value(matched).end_object();
    return w.str();
}

/// Tipo do efeito pela chave ("aurea.blur.gaussian"), matchName do AE, nome exato ou parte única do nome.
EffectTypeId find_effect_type(Engine& e, std::string_view want) {
    EffectRegistry& reg = e.effects();
    if (want.empty()) return 0;
    const EffectTypeId byKey = reg.find_key(want);
    if (byKey && reg.find(byKey)) return byKey;
    const std::string w = lower(want);
    for (u32 i = 0; i < ae::detail::rule_count(); ++i) {
        const ae::detail::ERule& r = ae::detail::rule_at(i);
        if (r.key && (lower(r.ae) == w || lower(r.aeName) == w)) {
            const EffectTypeId id = reg.find_key(r.key);
            if (id && reg.find(id)) return id;
        }
    }
    EffectTypeId partial = 0;
    u32 partials = 0;
    for (u32 i = 0; i < reg.count(); ++i) {
        if (reg.retired_at(i)) continue;
        const Effect& fx = reg.at(i);
        const std::string name = lower(fx.info().name);
        if (name == w) return fx.type_id();
        if (name.find(w) != std::string::npos) { partial = fx.type_id(); ++partials; }
    }
    return partials == 1 ? partial : 0;
}

std::vector<u32> effect_ids(Ctx& ctx, u64 layer) {
    std::vector<u32> ids;
    std::lock_guard<std::mutex> lock(ctx.model);
    Composition* comp = ctx.comp();
    const Layer* l = comp ? comp->layer(LayerId::unpack(layer)) : nullptr;
    if (l) for (const EffectInstance& e : l->effects) ids.push_back(e.id);
    return ids;
}

std::string add_effect(Ctx& ctx, const json::Value& args) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, false, nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    const EffectTypeId type = find_effect_type(ctx.e, str_arg(args, "type"));
    if (!type) return fail("unknown effect type: use a key or name from list_effect_types (AE matchNames work too)");
    f64 index = -1;
    const bool at = num_arg(args, "index", index) && index >= 0;
    const std::vector<u32> before = effect_ids(ctx, t.layer);
    Command c; c.type = CommandType::EffectAdd;
    c.effect_add.layer = LayerId::unpack(t.layer); c.effect_add.effectType = type;
    c.effect_add.index = at ? static_cast<u32>(std::min(index, 1000.0)) : 0xFFFFFFFFu;
    const Status s = ctx.e.apply_command(c);
    if (!s.ok()) return fail("could not add the effect: " + status_text(s));
    u32 added = kInvalidIndex;
    for (u32 id : effect_ids(ctx, t.layer)) if (std::find(before.begin(), before.end(), id) == before.end()) added = id;
    const Effect* fx = ctx.e.effects().find(type);
    json::Writer w;
    w.begin_object().key("ok").value(added != kInvalidIndex).key("effectId").value(added)
        .key("key").value(fx ? fx->info().key : "").key("name").value(fx ? fx->info().name : "")
        .key("hint").value("Read its parameters with get_effect; change them with set_property effect.<effectId>.<param>.").end_object();
    return w.str();
}

bool effect_arg(const json::Value& args, u32& id) {
    f64 v = 0;
    if (!num_arg(args, "effect", v) || v < 0 || std::floor(v) != v || v > 1e9) return false;
    id = static_cast<u32>(v);
    return true;
}

bool effect_on_layer(Ctx& ctx, u64 layer, u32 effect) {
    const std::vector<u32> ids = effect_ids(ctx, layer);
    return std::find(ids.begin(), ids.end(), effect) != ids.end();
}

std::string effect_command(Ctx& ctx, const json::Value& args, CommandType type) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, false, nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    u32 effect = 0;
    if (!effect_arg(args, effect) || !effect_on_layer(ctx, t.layer, effect)) return fail("effect not found on this layer (ids from get_layer_details)");
    Command c; c.type = type;
    const LayerId lid = LayerId::unpack(t.layer);
    if (type == CommandType::EffectRemove) { c.effect_ref.layer = lid; c.effect_ref.effect = EffectId{effect, 0}; }
    else if (type == CommandType::EffectReorder) {
        f64 index = 0;
        if (!num_arg(args, "index", index) || index < 0) return fail("index must be a number >= 0 (0 = first)");
        c.effect_reorder.layer = lid; c.effect_reorder.effect = EffectId{effect, 0};
        c.effect_reorder.newIndex = static_cast<u32>(std::min(index, 1000.0));
        const u32 count = static_cast<u32>(effect_ids(ctx, t.layer).size());
        if (c.effect_reorder.newIndex >= count) c.effect_reorder.newIndex = count - 1;
    } else {
        const json::Value* on = args.get("enabled");
        if (!on || on->type != json::Value::Type::Bool) return fail("enabled must be true or false");
        c.effect_enabled.layer = lid; c.effect_enabled.effect = EffectId{effect, 0}; c.effect_enabled.enabled = on->boolean;
    }
    const Status s = ctx.e.apply_command(c);
    return s.ok() ? std::string(R"({"ok":true})") : fail(status_text(s));
}

std::string get_effect(Ctx& ctx, const json::Value& args) {
    f64 seconds = 0;
    const bool at = num_arg(args, "seconds", seconds);
    u32 effect = 0;
    if (!effect_arg(args, effect)) return fail("effect must be the effect id from get_layer_details");
    std::lock_guard<std::mutex> lock(ctx.model);
    Composition* comp = ctx.comp();
    if (!comp) return fail("no project open");
    u64 id = 0;
    if (!find_layer(args, *comp, id)) return fail(kNoLayer);
    const Layer& l = *comp->layer(LayerId::unpack(id));
    const EffectInstance* inst = effect_of(l, effect);
    if (!inst) return fail("effect not found on this layer (ids from get_layer_details)");
    const f64 fps = fps_of(*comp);
    i64 frame = ctx.e.playback().current().value;
    if (at && !seconds_to_frame(seconds, fps, frame)) return fail("seconds must be a number >= 0");
    const FrameIndex local = l.local_time(FrameIndex{frame});
    const Effect* fx = ctx.e.effects().find(inst->type);
    const ParameterRegistry* specs = ctx.e.effects().params(inst->type);
    json::Writer w;
    w.begin_object().key("ok").value(true).key("effectId").value(inst->id).key("key").value(fx ? fx->info().key : "")
        .key("name").value(fx ? fx->info().name : "unavailable effect").key("enabled").value(inst->enabled)
        .key("atSeconds").value(frame_seconds(frame, fps)).key("params").begin_array();
    for (u32 i = 0; specs && i < specs->count() && i < 160; ++i) {
        const ParamSpec& spec = specs->at(i);
        if (spec.flags & kParamHidden) continue;
        const u32 comps = component_count(spec.type);
        w.begin_object().key("index").value(i).key("id").value(spec.id).key("label").value(spec.label).key("type").value(param_type_name(spec.type));
        if (comps > 0) {
            const ParamValue v = evaluate_param(l.tracks, *inst, i, spec, local);
            w.key("value");
            write_floats(w, v.v, comps);
            w.key("default");
            write_floats(w, spec.defaultValue.v, comps);
            if (spec.type != ParamType::Bool && spec.type != ParamType::Color && spec.type != ParamType::Enum)
                w.key("min").value(tidy(spec.typed_min())).key("max").value(tidy(spec.typed_max()));
            u32 keys = 0;
            for (u32 c = 0; c < comps; ++c)
                if (const Track* t = l.tracks.find(TrackProperty::EffectParam, inst->id, param_track_key(i, c))) keys += static_cast<u32>(t->keys.size());
            if (keys) w.key("keyframes").value(keys);
            if (!spec.animatable()) w.key("animatable").value(false);
        } else if (spec.type == ParamType::LayerReference) {
            const u64 ref = i < inst->params.size() ? inst->params[i].constant.ref : 0;
            if (ref && comp->layer(LayerId::unpack(ref))) w.key("value").value(std::to_string(ref));
        }
        if (spec.unit && *spec.unit) w.key("unit").value(spec.unit);
        if (spec.enumCount && spec.enumLabels) {
            w.key("options").begin_array();
            for (u32 k = 0; k < spec.enumCount && k < 64; ++k) w.value(spec.enumLabels[k] ? spec.enumLabels[k] : "");
            w.end_array();
        }
        w.end_object();
    }
    w.end_array().key("address").value("effect." + std::to_string(inst->id) + ".<index or id>").end_object();
    return w.str();
}

// --- presets ----------------------------------------------------------------------------

/// Presets de texto do modelo Motion (Engine::apply_motion_text_preset), na ordem do motor.
constexpr const char* kMotionTextPresets[Engine::kMotionTextPresetCount] = {
    "Typewriter with cursor", "Decode", "Uncensor", "Reveal password", "Open title (letters spread)",
    "Rise by letter", "Wave", "Cascade spin", "Grow by word", "Letters out (random order)",
};

std::string list_presets(Ctx& ctx, const json::Value& args) {
    const std::string kind = lower(str_arg(args, "kind"));
    if (kind == "text" || kind.empty()) {
        json::Writer w;
        w.begin_object().key("ok").value(true).key("kind").value("text").key("presets").begin_array();
        for (u32 i = 0; i < text::kTextPresetCount; ++i)
            w.begin_object().key("id").value(i).key("name").value(text::text_preset_name(i)).end_object();
        for (u32 i = 0; i < Engine::kMotionTextPresetCount; ++i)
            w.begin_object().key("id").value(Engine::kMotionTextPresetBase + i).key("name").value(kMotionTextPresets[i]).end_object();
        w.end_array().key("hint").value("apply_preset with kind text on a text layer; it replaces the layer's text animation and starts at the layer's in point.").end_object();
        return w.str();
    }
    if (kind != "effect") return fail("kind must be text or effect");
    Target t;
    std::string err;
    if (!read_target(ctx, args, false, nullptr, t, err)) return fail(err);
    u32 effect = 0;
    if (!effect_arg(args, effect)) return fail("effect must be the effect id from get_layer_details");
    u32 type = 0;
    {
        std::lock_guard<std::mutex> lock(ctx.model);
        Composition* comp = ctx.comp();
        const Layer* l = comp ? comp->layer(LayerId::unpack(t.layer)) : nullptr;
        const EffectInstance* inst = l ? effect_of(*l, effect) : nullptr;
        if (!inst) return fail("effect not found on this layer");
        type = inst->type;
    }
    const auto presets = ctx.e.effect_presets(type);
    json::Writer w;
    w.begin_object().key("ok").value(true).key("kind").value("effect").key("presets").begin_array();
    for (u32 i = 0; i < presets.size(); ++i)
        w.begin_object().key("id").value(i).key("key").value(presets[i].first).key("name").value(presets[i].second).end_object();
    w.end_array().end_object();
    return w.str();
}

std::string apply_preset(Ctx& ctx, const json::Value& args) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, false, nullptr, t, err)) return fail(err);
    if (t.locked) return fail("layer is locked");
    const std::string kind = lower(str_arg(args, "kind"));
    f64 preset = -1;
    const json::Value* pv = args.get("preset");
    if (pv && pv->is_number()) preset = pv->number;
    if (kind == "text") {
        if (pv && pv->is_string()) {
            const std::string want = lower(pv->string);
            for (u32 i = 0; i < text::kTextPresetCount && preset < 0; ++i) if (lower(text::text_preset_name(i)) == want) preset = i;
            for (u32 i = 0; i < Engine::kMotionTextPresetCount && preset < 0; ++i)
                if (lower(kMotionTextPresets[i]) == want) preset = Engine::kMotionTextPresetBase + i;
        }
        if (preset < 0 || std::floor(preset) != preset) return fail("preset must be an id or name from list_presets (kind text)");
        if (t.kind != LayerKind::Text) return fail("text presets need a text layer");
        return ctx.e.apply_text_preset(t.layer, static_cast<u32>(preset)) ? std::string(R"({"ok":true})")
                                                                           : fail("preset not applicable to this layer");
    }
    if (kind == "effect") {
        u32 effect = 0;
        if (!effect_arg(args, effect) || !effect_on_layer(ctx, t.layer, effect)) return fail("effect must be the effect id from get_layer_details");
        if (pv && pv->is_string()) {
            u32 type = 0;
            {
                std::lock_guard<std::mutex> lock(ctx.model);
                Composition* comp = ctx.comp();
                const Layer* l = comp ? comp->layer(LayerId::unpack(t.layer)) : nullptr;
                const EffectInstance* inst = l ? effect_of(*l, effect) : nullptr;
                type = inst ? inst->type : 0;
            }
            const auto presets = ctx.e.effect_presets(type);
            const std::string want = lower(pv->string);
            for (u32 i = 0; i < presets.size() && preset < 0; ++i) if (lower(presets[i].second) == want || lower(presets[i].first) == want) preset = i;
        }
        if (preset < 0 || std::floor(preset) != preset) return fail("preset must be an id or name from list_presets (kind effect)");
        return ctx.e.apply_effect_preset(t.layer, effect, static_cast<u32>(preset)) ? std::string(R"({"ok":true})") : fail("preset not found for this effect");
    }
    if (kind == "json") {
        const std::string text = str_arg(args, "json");
        if (text.empty() || text.size() > 200 * 1024) return fail("json must be an Aurea preset (aurea_preset 1), up to 200 KB");
        f64 dur = 0;
        i64 frames = 0;
        if (num_arg(args, "durationSeconds", dur) && !seconds_to_frame(dur, t.fps, frames)) return fail("durationSeconds must be >= 0");
        std::string why;
        return ctx.e.apply_preset(t.layer, text, frames, &why) ? std::string(R"({"ok":true})") : fail(why.empty() ? "preset not applicable" : why);
    }
    return fail("kind must be text, effect or json");
}

// --- texto e fontes --------------------------------------------------------------------

std::string list_fonts(Ctx& ctx, const json::Value& args) {
    const std::string q = lower(str_arg(args, "query"));
    f64 limitArg = 80;
    (void)num_arg(args, "limit", limitArg);
    const u32 limit = static_cast<u32>(std::clamp(limitArg, 1.0, 400.0));
    std::vector<text::FontEntry> fonts = ctx.e.list_fonts();
    std::stable_sort(fonts.begin(), fonts.end(), [](const text::FontEntry& a, const text::FontEntry& b) {
        return a.family != b.family ? lower(a.family) < lower(b.family) : a.weight < b.weight;
    });
    static const char* categories[] = {"sans", "serif", "display", "handwriting", "mono"};
    json::Writer w;
    w.begin_object().key("ok").value(true).key("fonts").begin_array();
    u32 families = 0;
    for (usize i = 0; i < fonts.size();) {
        usize j = i;
        while (j < fonts.size() && fonts[j].family == fonts[i].family) ++j;
        if (contains(fonts[i].family, q) && families < limit) {
            w.begin_object().key("family").value(fonts[i].family).key("category").value(categories[std::min<u32>(fonts[i].category, 4)])
                .key("imported").value(fonts[i].imported).key("styles").begin_array();
            for (usize k = i; k < j && k < i + 24; ++k)
                w.begin_object().key("style").value(fonts[k].style).key("weight").value(static_cast<u32>(fonts[k].weight))
                    .key("italic").value(fonts[k].italic).end_object();
            w.end_array().end_object();
            ++families;
        }
        i = j;
    }
    w.end_array().key("families").value(families).end_object();
    return w.str();
}

std::string set_text_style(Ctx& ctx, const json::Value& args) {
    Target t;
    std::string err;
    if (!read_target(ctx, args, false, nullptr, t, err)) return fail(err);
    if (t.kind != LayerKind::Text) return fail("set_text_style needs a text layer");
    if (t.locked) return fail("layer is locked");
    const LayerId lid = LayerId::unpack(t.layer);
    // Valida tudo antes de mexer em qualquer coisa.
    const std::string content = str_arg(args, "text");
    const json::Value* textArg = args.get("text");
    if (textArg && (!textArg->is_string() || content.size() > 20000)) return fail("text must be a string up to 20000 bytes");
    const std::string family = str_arg(args, "font");
    f64 weight = 400, size = 0, stroke = 0, tracking = 0, lineHeight = 0;
    const bool hasWeight = num_arg(args, "weight", weight);
    const json::Value* italicArg = args.get("italic");
    const bool hasSize = num_arg(args, "size", size), hasStroke = num_arg(args, "strokeWidth", stroke);
    const bool hasTracking = num_arg(args, "tracking", tracking), hasLine = num_arg(args, "lineHeight", lineHeight);
    if (hasSize && (size < 1 || size > 2000)) return fail("size must be 1..2000 px");
    if (hasStroke && (stroke < 0 || stroke > 200)) return fail("strokeWidth must be 0..200 px");
    if (hasLine && (lineHeight < 0.1 || lineHeight > 10)) return fail("lineHeight must be 0.1..10");
    if (hasTracking && std::fabs(tracking) > 1000) return fail("tracking must be -1000..1000");
    std::vector<f64> color, strokeColor;
    if (args.get("color") && (!numbers(args.get("color"), color) || color.size() < 3)) return fail("color must be [r, g, b] or [r, g, b, a] 0..1");
    if (args.get("strokeColor") && (!numbers(args.get("strokeColor"), strokeColor) || strokeColor.size() < 3)) return fail("strokeColor must be [r, g, b(, a)] 0..1");
    i32 align = -1;
    const std::string alignment = lower(str_arg(args, "alignment"));
    if (!alignment.empty()) align = alignment == "left" ? 0 : alignment == "center" ? 1 : alignment == "right" ? 2 : -2;
    if (align == -2) return fail("alignment must be left, center or right");
    // Fonte: família da lista do aparelho (o peso mais próximo; importada leva o arquivo).
    text::FontEntry chosen;
    bool font = false;
    if (!family.empty() || hasWeight || italicArg) {
        std::string want = lower(family);
        if (want.empty()) {
            std::lock_guard<std::mutex> lock(ctx.model);
            Composition* comp = ctx.comp();
            const Layer* l = comp ? comp->layer(lid) : nullptr;
            want = l ? lower(l->text.fontFamily) : std::string{};
        }
        const bool italic = italicArg && italicArg->type == json::Value::Type::Bool && italicArg->boolean;
        i64 best = -1;
        for (const text::FontEntry& f : ctx.e.list_fonts()) {
            if (lower(f.family) != want) continue;
            const i64 score = std::llabs(static_cast<i64>(f.weight) - static_cast<i64>(weight)) + (f.italic == italic ? 0 : 5000);
            if (best < 0 || score < best) { best = score; chosen = f; }
        }
        if (best < 0) {
            if (!want.empty()) return fail("font family not found on this device: see list_fonts");
            chosen.family.clear(); chosen.weight = static_cast<u16>(std::clamp(weight, 100.0, 1000.0)); chosen.italic = italic;
        }
        font = true;
    }
    UndoGroup undo(ctx.e, "agente: estilo do texto");
    auto run = [&](Command& c, const char* text = nullptr) -> bool { return ctx.e.apply_command(c, text).ok(); };
    if (textArg) { Command c; c.type = CommandType::TextSetContent; c.layer_ref.layer = lid; if (!run(c, content.c_str())) return fail("could not change the text"); }
    if (font && !ctx.e.set_text_font(t.layer, chosen.family, chosen.weight, chosen.italic, chosen.imported ? chosen.path : std::string{}))
        return fail("could not change the font");
    if (hasSize) { Command c; c.type = CommandType::TextSetSize; c.text_size.layer = lid; c.text_size.size = static_cast<f32>(size); (void)run(c); }
    if (!color.empty()) {
        Command c; c.type = CommandType::TextSetColor; c.text_color.layer = lid;
        c.text_color.r = static_cast<f32>(color[0]); c.text_color.g = static_cast<f32>(color[1]); c.text_color.b = static_cast<f32>(color[2]);
        c.text_color.a = color.size() > 3 ? static_cast<f32>(color[3]) : 1.0f;
        (void)run(c);
    }
    if (hasStroke) { Command c; c.type = CommandType::TextSetStrokeWidth; c.text_stroke_width.layer = lid; c.text_stroke_width.width = static_cast<f32>(stroke); (void)run(c); }
    if (!strokeColor.empty()) {
        Command c; c.type = CommandType::TextSetStrokeColor; c.text_color.layer = lid;
        c.text_color.r = static_cast<f32>(strokeColor[0]); c.text_color.g = static_cast<f32>(strokeColor[1]); c.text_color.b = static_cast<f32>(strokeColor[2]);
        c.text_color.a = strokeColor.size() > 3 ? static_cast<f32>(strokeColor[3]) : 1.0f;
        (void)run(c);
    }
    if (align >= 0) { Command c; c.type = CommandType::TextSetAlignment; c.text_align.layer = lid; c.text_align.alignment = static_cast<u32>(align); (void)run(c); }
    if (hasTracking || hasLine) {
        f32 style[22]{};
        if (ctx.e.query_text_style(t.layer, style, 22)) {
            if (hasLine) style[18] = static_cast<f32>(lineHeight);
            if (hasTracking) style[19] = static_cast<f32>(tracking);
            (void)ctx.e.set_text_style(t.layer, style, 20);
        }
    }
    return std::string(R"({"ok":true})");
}

// --- marcas e área de trabalho -------------------------------------------------------------

void write_marker(json::Writer& w, const Marker& m, i64 frame, f64 fps) {
    w.begin_object().key("seconds").value(frame_seconds(frame, fps)).key("frame").value(frame)
        .key("label").value(m.label).key("color").value(color_hex(m.color)).key("kind").value(m.kind == kMarkerBeat ? "beat" : "marker");
    if (m.duration > 0) w.key("durationSeconds").value(frame_seconds(m.duration, fps));
    if (!m.comment.empty()) w.key("comment").value(m.comment);
    w.end_object();
}

std::string get_markers(Ctx& ctx, const json::Value& args) {
    std::lock_guard<std::mutex> lock(ctx.model);
    Composition* comp = ctx.comp();
    if (!comp) return fail("no project open");
    const f64 fps = fps_of(*comp);
    json::Writer w;
    w.begin_object().key("ok").value(true).key("composition").begin_array();
    u32 n = 0;
    for (const Marker& m : comp->markers()) if (n++ < 500) write_marker(w, m, m.frame.value, fps);
    w.end_array();
    if (args.get("layer")) {
        u64 id = 0;
        if (!find_layer(args, *comp, id)) return fail(kNoLayer);
        const Layer& l = *comp->layer(LayerId::unpack(id));
        w.key("layer").begin_array();
        n = 0;
        for (const Marker& m : l.markers) if (n++ < 500) write_marker(w, m, l.timeline_time(m.frame).value, fps);
        w.end_array();
    }
    w.end_object();
    return w.str();
}

std::string set_marker(Ctx& ctx, const json::Value& args) {
    f64 seconds = 0;
    if (!num_arg(args, "seconds", seconds)) return fail("seconds is required");
    const bool onLayer = args.get("layer") != nullptr;
    Target t;
    std::string err;
    i64 frame = 0, from = -1, duration = 0;
    u32 color = 0xFFF7C34Fu;
    std::string label = str_arg(args, "label"), comment = str_arg(args, "comment");
    bool keepLabel = !args.get("label"), keepComment = !args.get("comment"), keepColor = !args.get("color");
    {
        std::lock_guard<std::mutex> lock(ctx.model);
        Composition* comp = ctx.comp();
        if (!comp) return fail("no project open");
        t.fps = fps_of(*comp);
        if (!seconds_to_frame(seconds, t.fps, frame)) return fail("seconds must be >= 0");
        const std::vector<Marker>* list = &comp->markers();
        const Layer* l = nullptr;
        if (onLayer) {
            if (!find_layer(args, *comp, t.layer)) return fail(kNoLayer);
            l = comp->layer(LayerId::unpack(t.layer));
            list = &l->markers;
        }
        for (const Marker& m : *list) {
            const i64 at = l ? l->timeline_time(m.frame).value : m.frame.value;
            if (at != frame) continue;
            from = frame;
            if (keepLabel) label = m.label;
            if (keepComment) comment = m.comment;
            if (keepColor) color = m.color;
            duration = m.duration;
        }
    }
    f64 durationSeconds = 0;
    if (num_arg(args, "durationSeconds", durationSeconds) && !seconds_to_frame(durationSeconds, t.fps, duration)) return fail("durationSeconds must be >= 0");
    const std::string hex = str_arg(args, "color");
    if (!hex.empty() && !parse_hex(hex, color)) return fail("color must be #RRGGBB");
    if (label.size() > 4096 || comment.size() > 4096) return fail("label and comment are limited to 4096 bytes");
    const bool ok = onLayer ? ctx.e.edit_layer_marker(t.layer, from, frame, color, label, duration, comment)
                            : ctx.e.edit_marker_ex(from, frame, color, label, duration, comment);
    if (!ok) return fail(onLayer ? "could not set the layer marker (locked layer?)" : "could not set the marker (inside the composition duration?)");
    json::Writer w;
    w.begin_object().key("ok").value(true).key("created").value(from < 0).key("seconds").value(frame_seconds(frame, t.fps)).end_object();
    return w.str();
}

std::string delete_marker(Ctx& ctx, const json::Value& args) {
    f64 seconds = 0;
    if (!num_arg(args, "seconds", seconds)) return fail("seconds is required");
    u64 layer = 0;
    i64 frame = 0;
    {
        std::lock_guard<std::mutex> lock(ctx.model);
        Composition* comp = ctx.comp();
        if (!comp) return fail("no project open");
        if (!seconds_to_frame(seconds, fps_of(*comp), frame)) return fail("seconds must be >= 0");
        if (args.get("layer") && !find_layer(args, *comp, layer)) return fail(kNoLayer);
    }
    const bool ok = layer ? ctx.e.delete_layer_marker(layer, frame) : ctx.e.delete_marker(frame);
    return ok ? std::string(R"({"ok":true})") : fail("no marker at that time");
}

std::string get_work_area(Ctx& ctx) {
    f64 fps = 30.0;
    i64 duration = 0;
    {
        std::lock_guard<std::mutex> lock(ctx.model);
        Composition* comp = ctx.comp();
        if (!comp) return fail("no project open");
        fps = fps_of(*comp);
        duration = comp->duration().value;
    }
    i64 in = 0, out = 0;
    const bool active = ctx.e.query_work_area(in, out);
    json::Writer w;
    w.begin_object().key("ok").value(true).key("active").value(active)
        .key("startSeconds").value(frame_seconds(in, fps)).key("endSeconds").value(frame_seconds(out, fps))
        .key("compositionSeconds").value(frame_seconds(duration, fps)).end_object();
    return w.str();
}

std::string set_work_area(Ctx& ctx, const json::Value& args) {
    f64 fps = 30.0;
    {
        std::lock_guard<std::mutex> lock(ctx.model);
        Composition* comp = ctx.comp();
        if (!comp) return fail("no project open");
        fps = fps_of(*comp);
    }
    if (bool_arg(args, "clear", false)) return ctx.e.set_work_area(-1, -1) ? std::string(R"({"ok":true,"active":false})") : fail("could not clear");
    f64 a = 0, b = 0;
    i64 in = 0, out = 0;
    if (!num_arg(args, "startSeconds", a) || !num_arg(args, "endSeconds", b) || !seconds_to_frame(a, fps, in) || !seconds_to_frame(b, fps, out) || out <= in)
        return fail("pass startSeconds < endSeconds (or clear: true)");
    if (!ctx.e.set_work_area(in, out)) return fail("could not set the work area");
    return get_work_area(ctx);
}

} // namespace

bool call(Engine& engine, std::mutex& model, const std::string& name, const json::Value& args, std::string& out) noexcept {
    Ctx ctx{engine, model};
    if (name == "get_layer_details") out = get_layer_details(ctx, args);
    else if (name == "get_keyframes") out = get_keyframes(ctx, args);
    else if (name == "set_property") out = set_property(ctx, args);
    else if (name == "add_keyframes") out = add_keyframes(ctx, args);
    else if (name == "delete_keyframes") out = delete_keyframes(ctx, args);
    else if (name == "move_keyframe") out = move_keyframe(ctx, args);
    else if (name == "set_keyframe_easing") out = set_keyframe_easing(ctx, args);
    else if (name == "list_effect_types") out = list_effect_types(ctx, args);
    else if (name == "get_effect") out = get_effect(ctx, args);
    else if (name == "add_effect") out = add_effect(ctx, args);
    else if (name == "remove_effect") out = effect_command(ctx, args, CommandType::EffectRemove);
    else if (name == "move_effect") out = effect_command(ctx, args, CommandType::EffectReorder);
    else if (name == "set_effect_enabled") out = effect_command(ctx, args, CommandType::EffectSetEnabled);
    else if (name == "list_presets") out = list_presets(ctx, args);
    else if (name == "apply_preset") out = apply_preset(ctx, args);
    else if (name == "list_fonts") out = list_fonts(ctx, args);
    else if (name == "set_text_style") out = set_text_style(ctx, args);
    else if (name == "get_markers") out = get_markers(ctx, args);
    else if (name == "set_marker") out = set_marker(ctx, args);
    else if (name == "delete_marker") out = delete_marker(ctx, args);
    else if (name == "get_work_area") out = get_work_area(ctx);
    else if (name == "set_work_area") out = set_work_area(ctx, args);
    else return false;
    return true;
}

} // namespace mcp_tools
} // namespace aurea
