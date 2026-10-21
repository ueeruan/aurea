// =============================================================================
//  Aurea / project / AeImportBind.cpp
//
//  Endereços das propriedades do AE no modelo do Aurea e a escrita delas
//  (valor parado, keyframes com ease temporal, expressão). Ver AeImportBind.hpp.
//
//  EASE → BÉZIER (a conversão de sempre do AE para curva de tempo): no trecho
//  A → B, com Δt em segundos e Δv na unidade da propriedade,
//      x1 = influência de saída de A,   y1 = vel. de saída de A · x1 · Δt / Δv
//      x2 = 1 − influência de entrada de B,  y2 = 1 − vel. de entrada de B · (1 − x2) · Δt / Δv
//  Espacial (um ease para o caminho): Δv = distância entre os dois valores.
//  Lado linear de um trecho misto: a alça do AE em 16,67 % na direção do
//  trecho (x = y). Segurar = Hold; linear dos dois lados = Linear.
// =============================================================================
#include "AeImportBind.hpp"

#include "aurea/core/Math.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/project/Presets.hpp"
#include "aurea/text/TextAnimator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::ae {

// =============================================================================
// Relatório
// =============================================================================
namespace {
constexpr usize kMaxItems = 400;

void push_item(std::vector<SkipItem>& v, std::string kind, std::string name, std::string reason, u32 line) {
    for (const SkipItem& s : v)
        if (s.kind == kind && s.name == name && s.reason == reason && s.line == line) return;
    if (v.size() >= kMaxItems) return;
    v.push_back(SkipItem{std::move(kind), std::move(name), std::move(reason), line});
}
} // namespace

void ImportReport::skip(std::string k, std::string n, std::string r, u32 line) {
    push_item(skipped, std::move(k), std::move(n), std::move(r), line);
}
void ImportReport::note(std::string k, std::string n, std::string r, u32 line) {
    push_item(notes, std::move(k), std::move(n), std::move(r), line);
}

std::string report_json(const ImportReport& r) {
    json::Writer w;
    w.begin_object();
    w.key("ok").value(r.ok);
    w.key("kind").value(std::string_view(r.kind));
    w.key("effects").value(r.effects);
    w.key("keyframes").value(r.keyframes);
    w.key("layers").value(r.layers);
    w.key("expressions").value(r.expressions);
    w.key("animators").value(r.animators);
    auto list = [&](const char* key, const std::vector<SkipItem>& items) {
        w.key(key).begin_array();
        for (const SkipItem& s : items) {
            w.begin_object();
            w.key("kind").value(std::string_view(s.kind));
            w.key("name").value(std::string_view(s.name));
            w.key("reason").value(std::string_view(s.reason));
            w.key("line").value(s.line);
            w.end_object();
        }
        w.end_array();
    };
    list("skipped", r.skipped);
    list("notes", r.notes);
    w.key("message").value(std::string_view(r.message));
    w.end_object();
    return w.str();
}

void finish_report(ImportReport& r) {
    if (!r.ok) {
        if (r.message.empty()) r.message = "Nada do arquivo do After Effects pôde ser importado.";
        return;
    }
    std::string parts;
    auto add = [&](u32 n, const char* one, const char* many) {
        if (n == 0) return;
        if (!parts.empty()) parts += ", ";
        parts += std::to_string(n) + " " + (n == 1 ? one : many);
    };
    add(r.layers, "camada criada", "camadas criadas");
    add(r.effects, "efeito", "efeitos");
    add(r.animators, "animador de texto", "animadores de texto");
    add(r.keyframes, "keyframe", "keyframes");
    add(r.expressions, "expressão", "expressões");
    std::string m = r.kind == "jsx" ? "Script do After Effects executado" : "Preset do After Effects importado";
    m += parts.empty() ? "." : ": " + parts + ".";
    if (!r.skipped.empty())
        m += " " + std::to_string(r.skipped.size()) + (r.skipped.size() == 1 ? " item ficou de fora." : " itens ficaram de fora.");
    if (!r.notes.empty())
        m += " " + std::to_string(r.notes.size()) + (r.notes.size() == 1 ? " aproximação." : " aproximações.");
    r.message = std::move(m);
}

namespace detail {

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        if (x != y) return false;
    }
    return true;
}

i64 WriteCtx::local_frame(f64 t) const noexcept {
    const f64 f = compTime ? t * fps : (t - t0) * fps;
    const f64 c = std::clamp(f, -1.0e12, 1.0e12);
    const i64 frame = static_cast<i64>(std::llround(c));
    if (compTime) return layer ? frame - layer->start.value + layer->offset.value : frame;
    return localAnchor + frame;
}

namespace {

// --- campos do Aurea ------------------------------------------------------------

f32* text_field(TextAnimator& a, u32 p) noexcept {
    switch (p) {
        case text::kSelStart: return &a.selector.start;
        case text::kSelEnd: return &a.selector.end;
        case text::kSelOffset: return &a.selector.offset;
        case text::kSelAmount: return &a.selector.amount;
        case text::kSelEaseHigh: return &a.selector.easeHigh;
        case text::kSelEaseLow: return &a.selector.easeLow;
        case text::kWiggleRate: return &a.selector.wiggleRate;
        case text::kPosX: return &a.position.x;
        case text::kPosY: return &a.position.y;
        case text::kPosZ: return &a.position.z;
        case text::kScaleX: return &a.scale.x;
        case text::kScaleY: return &a.scale.y;
        case text::kRotX: return &a.rotation.x;
        case text::kRotY: return &a.rotation.y;
        case text::kRotZ: return &a.rotation.z;
        case text::kOpacity: return &a.opacity;
        case text::kTracking: return &a.tracking;
        case text::kBlur: return &a.blur;
        case text::kSkew: return &a.skew;
        case text::kStrokeWidth: return &a.strokeWidth;
        case text::kCharOffset: return &a.charOffset;
        case text::kFillR: return &a.fill.x;
        case text::kFillG: return &a.fill.y;
        case text::kFillB: return &a.fill.z;
        case text::kStrokeR: return &a.stroke.x;
        case text::kStrokeG: return &a.stroke.y;
        case text::kStrokeB: return &a.stroke.z;
        default: return nullptr;
    }
}

f32* transform_field(Layer& l, TrackProperty p) noexcept {
    Transform& t = l.transform;
    switch (p) {
        case TrackProperty::PositionX: return &t.position.x;
        case TrackProperty::PositionY: return &t.position.y;
        case TrackProperty::PositionZ: return &t.position.z;
        case TrackProperty::ScaleX: return &t.scale.x;
        case TrackProperty::ScaleY: return &t.scale.y;
        case TrackProperty::ScaleZ: return &t.scale.z;
        case TrackProperty::RotationX: return &t.rotation.x;
        case TrackProperty::RotationY: return &t.rotation.y;
        case TrackProperty::RotationZ: return &t.rotation.z;
        case TrackProperty::AnchorX: return &t.anchor.x;
        case TrackProperty::AnchorY: return &t.anchor.y;
        case TrackProperty::AnchorZ: return &t.anchor.z;
        case TrackProperty::Opacity: return &t.opacity;
        case TrackProperty::SkewX: return &t.skewX;
        case TrackProperty::SkewY: return &t.skewY;
        case TrackProperty::TextPathFirstMargin: return &l.text.pathOffset;
        case TrackProperty::TextPathLastMargin: return &l.text.pathLastMargin;
        case TrackProperty::LightIntensity: return &l.light.intensity;
        case TrackProperty::LightConeAngle: return &l.light.coneAngle;
        case TrackProperty::FocalLength: return &l.camera.focalLength;
        default: return nullptr;
    }
}

Mask* mask_of(Layer& l, u32 id) noexcept {
    for (Mask& m : l.masks) if (m.id == id) return &m;
    return nullptr;
}

/// Ponteiro para o lugar que guarda o valor parado do componente (nulo = só trilha).
f32* home_ptr(Layer& l, const Channel& c) noexcept {
    switch (c.prop) {
        case TrackProperty::EffectParam:
            for (EffectInstance& e : l.effects) {
                if (e.id != c.ei) continue;
                const u32 slot = c.pi / 4, comp = c.pi % 4;
                return slot < e.params.size() ? &e.params[slot].constant.v[comp] : nullptr;
            }
            return nullptr;
        case TrackProperty::TextAnimParam:
            return c.ei < l.text.animators.size() ? text_field(l.text.animators[c.ei], c.pi) : nullptr;
        case TrackProperty::MaskParam:
            if (Mask* m = mask_of(l, c.ei)) return c.pi == 0 ? &m->feather : c.pi == 1 ? &m->expansion : &m->opacity;
            return nullptr;
        default:
            return transform_field(l, c.prop);
    }
}

bool keyed_channel(const Channel& c) noexcept {
    return (c.home == Home::Track || c.home == Home::TrackOnly) && c.prop != TrackProperty::_Count;
}

/// Lado AE → Aurea. Falso = valor sem equivalente (menu sem opção).
bool to_aurea(const Channel& c, f64 ae, f32& out) noexcept {
    f64 x = c.src == kConstSrc ? c.add : ae;
    switch (c.conv) {
        case Conv::Linear: if (c.src != kConstSrc) x = x * c.mul + c.add; break;
        case Conv::SrgbToLinear: x = Color::srgb_to_linear(static_cast<f32>(std::clamp(x, 0.0, 1.0))); break;
        case Conv::Bool: x = std::fabs(x * c.mul + c.add) > 1e-9 ? 1.0 : 0.0; break;
        case Conv::Round: x = std::round(x * c.mul + c.add); break;
        case Conv::Enum: {
            const i64 idx = static_cast<i64>(std::llround(x));
            if (!c.map || idx < 0 || idx >= c.mapN || c.map[idx] < 0) return false;
            x = c.map[idx];
            break;
        }
        case Conv::Reciprocal: x = std::fabs(x) > 1e-9 ? c.mul / std::fabs(x) : c.hi; break;
    }
    if (!std::isfinite(x)) return false;
    out = static_cast<f32>(std::clamp(x, c.lo, c.hi));
    return true;
}

f64 to_ae(const Channel& c, f32 v) noexcept {
    switch (c.conv) {
        case Conv::Linear: return std::fabs(c.mul) > 1e-12 ? (v - c.add) / c.mul : v;
        case Conv::SrgbToLinear: return Color::linear_to_srgb(v);
        case Conv::Bool: return v;
        case Conv::Round: return std::fabs(c.mul) > 1e-12 ? (v - c.add) / c.mul : v;
        case Conv::Enum:
            for (u8 i = 0; i < c.mapN; ++i) if (c.map && c.map[i] == static_cast<i8>(std::lround(v))) return i;
            return 1;
        case Conv::Reciprocal: return std::fabs(v) > 1e-9 ? c.mul / v : 0.0;
    }
    return v;
}

bool stepped(const Channel& c) noexcept {
    return c.conv == Conv::Bool || c.conv == Conv::Enum || c.conv == Conv::Round || c.home == Home::Field;
}

void set_field(WriteCtx& w, Layer& l, const Channel& c, f32 v) {
    TextAnimator* a = c.ei < l.text.animators.size() ? &l.text.animators[c.ei] : nullptr;
    switch (c.field) {
        case Field::SelBasedOn: if (a) a->selector.basedOn = static_cast<u8>(std::clamp(v, 0.0f, 2.0f)); break;
        case Field::SelShape: if (a) a->selector.shape = static_cast<u8>(std::clamp(v, 0.0f, 5.0f)); break;
        case Field::SelRandom: if (a) a->selector.randomOrder = v > 0.5f; break;
        case Field::SelSeed: if (a) a->selector.seed = static_cast<u32>(std::clamp(v, 0.0f, 1.0e6f)); break;
        case Field::PathReverse: l.text.pathReverse = v > 0.5f; l.text.pathLegacyLayout = false; break;
        case Field::PathPerpendicular: l.text.pathPerpendicular = v > 0.5f; l.text.pathLegacyLayout = false; break;
        case Field::PathForceAlign: l.text.pathForceAlign = v > 0.5f; l.text.pathLegacyLayout = false; break;
        case Field::PathMask: w.pendingPathMask = static_cast<i32>(std::lround(v)); break;
        case Field::Grouping: w.pendingGrouping = static_cast<i32>(std::lround(v)); break;
        case Field::None: break;
    }
}

void set_home(WriteCtx& w, Layer& l, const Channel& c, f32 v) {
    if (c.home == Home::Field) { set_field(w, l, c, v); return; }
    if (c.home == Home::TrackOnly || (c.prop == TrackProperty::TextAnimParam && !home_ptr(l, c))) {
        Track& t = l.tracks.get_or_create(c.prop, c.ei, c.pi);
        t.clear();
        (void)t.set(FrameIndex{0}, v, Interpolation::Hold);
        return;
    }
    if (f32* p = home_ptr(l, c)) *p = v;
    if (Track* t = l.tracks.find(c.prop, c.ei, c.pi); t && t->keys.empty()) t->staticValue = v;
}

f32 home_value(const Layer& l, const Channel& c) noexcept {
    Layer& m = const_cast<Layer&>(l);   // só leitura (o ponteiro não é escrito)
    if (c.home == Home::Field) {
        const TextAnimator* a = c.ei < l.text.animators.size() ? &l.text.animators[c.ei] : nullptr;
        switch (c.field) {
            case Field::SelBasedOn: return a ? a->selector.basedOn : 0.0f;
            case Field::SelShape: return a ? a->selector.shape : 0.0f;
            case Field::SelRandom: return a && a->selector.randomOrder ? 1.0f : 0.0f;
            case Field::SelSeed: return a ? static_cast<f32>(a->selector.seed) : 0.0f;
            case Field::PathReverse: return l.text.pathReverse ? 1.0f : 0.0f;
            case Field::PathPerpendicular: return l.text.pathPerpendicular ? 1.0f : 0.0f;
            case Field::PathForceAlign: return l.text.pathForceAlign ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    }
    if (const f32* p = home_ptr(m, c)) return *p;
    const Track* t = l.tracks.find(c.prop, c.ei, c.pi);
    return t ? (t->keys.empty() ? t->staticValue : t->keys.front().value) : 0.0f;
}

f64 ae_comp(const std::vector<f64>& v, u8 src, const f64* mul) noexcept {
    if (src == kConstSrc) return 0.0;
    if (src < v.size()) return v[src] * (mul ? mul[std::min<u8>(src, 3)] : 1.0);
    return NAN;   // componente que o valor não trouxe (ex.: [x, y] numa posição 3D): fica o atual
}

/// O valor traz o componente do canal?
bool has_comp(const std::vector<f64>& v, const Channel& c) noexcept { return c.src == kConstSrc || c.src < v.size(); }

void mark_props(Layer& l, const Binding& b) {
    if (b.animator < l.text.animators.size()) l.text.animators[b.animator].props |= b.textProps;
}

} // namespace

void write_static(WriteCtx& w, const Binding& b, const std::vector<f64>& v, bool fromFile) {
    if (!w.layer || !b.ok()) return;
    Layer& l = *w.layer;
    bool any = false;
    for (const Channel& c : b.ch) {
        f32 out = 0;
        if (!has_comp(v, c)) continue;
        if (!to_aurea(c, ae_comp(v, c.src, fromFile ? b.fileMul : nullptr), out)) {
            if (w.report) w.report->note("parametro", b.label, "valor do menu sem equivalente no Aurea; ficou o padrão", w.line);
            continue;
        }
        set_home(w, l, c, out);
        any = true;
    }
    if (any) {
        mark_props(l, b);
        ++w.writes;
        if (!b.approx.empty() && w.report) w.report->note("propriedade", b.label, b.approx, w.line);
    }
}

u32 write_keys(WriteCtx& w, const Binding& b, const std::vector<Key>& keys, bool fromFile) {
    if (!w.layer || !b.ok() || keys.empty()) return 0;
    Layer& l = *w.layer;
    if (keys.size() == 1) {
        write_static(w, b, keys.front().value, fromFile);
        return 0;
    }
    u32 written = 0;
    bool curved = false;
    for (const Key& k : keys)
        for (f64 t : k.inTangent) curved |= std::fabs(t) > 1e-6;
    for (const Key& k : keys)
        for (f64 t : k.outTangent) curved |= std::fabs(t) > 1e-6;
    for (const Channel& c : b.ch) {
        if (c.src == kConstSrc) {
            f32 out = 0;
            if (to_aurea(c, 0.0, out)) set_home(w, l, c, out);
            continue;
        }
        if (!has_comp(keys.front().value, c)) continue;
        if (!keyed_channel(c)) {
            // Campo sem trilha: fica o valor do primeiro keyframe.
            f32 out = 0;
            if (to_aurea(c, ae_comp(keys.front().value, c.src, fromFile ? b.fileMul : nullptr), out)) set_home(w, l, c, out);
            if (w.report) w.report->note("propriedade", b.label, "animação deste ajuste não existe no Aurea; ficou o valor do primeiro keyframe", w.line);
            continue;
        }
        if (l.tracks.size() >= kMaxTrackCount) break;
        Track& t = l.tracks.get_or_create(c.prop, c.ei, c.pi);
        t.clear();
        const bool step = stepped(c);
        for (usize i = 0; i < keys.size(); ++i) {
            const Key& k = keys[i];
            f32 v = 0;
            if (!to_aurea(c, ae_comp(k.value, c.src, fromFile ? b.fileMul : nullptr), v)) continue;
            Interpolation interp = Interpolation::Linear;
            f32 x1 = 0.33f, y1 = 0.0f, x2 = 0.67f, y2 = 1.0f;
            if (i + 1 < keys.size()) {
                const Key& n = keys[i + 1];
                const f64 dt = n.time - k.time;
                if (step || k.out == KeyInterp::Hold) interp = Interpolation::Hold;
                else if (k.out == KeyInterp::Linear && n.in == KeyInterp::Linear) interp = Interpolation::Linear;
                else if (dt > 1e-9) {
                    interp = Interpolation::Bezier;
                    // Δv na unidade do ARQUIVO (a velocidade gravada está nela).
                    f64 dv = 0;
                    if (b.spatial) {
                        f64 s = 0;
                        for (usize q = 0; q < std::min(k.value.size(), n.value.size()); ++q) {
                            const f64 d = n.value[q] - k.value[q];
                            s += d * d;
                        }
                        dv = std::sqrt(s);
                    } else {
                        const u8 src = c.src == kConstSrc ? 0 : c.src;
                        dv = (src < n.value.size() ? n.value[src] : 0.0) - (src < k.value.size() ? k.value[src] : 0.0);
                    }
                    const usize ei = b.spatial ? 0 : (c.src == kConstSrc ? 0 : c.src);
                    const Ease eo = k.outEase.empty() ? Ease{} : k.outEase[std::min(ei, k.outEase.size() - 1)];
                    const Ease en = n.inEase.empty() ? Ease{} : n.inEase[std::min(ei, n.inEase.size() - 1)];
                    const bool tiny = std::fabs(dv) < 1e-9;
                    f64 bx1 = 1.0 / 6.0, by1 = 1.0 / 6.0, bx2 = 5.0 / 6.0, by2 = 5.0 / 6.0;
                    if (k.out == KeyInterp::Bezier) {
                        bx1 = std::clamp(eo.influence / 100.0, 0.001, 1.0);
                        by1 = tiny ? 0.0 : eo.speed * bx1 * dt / dv;
                    }
                    if (n.in == KeyInterp::Bezier) {
                        const f64 inf = std::clamp(en.influence / 100.0, 0.001, 1.0);
                        bx2 = 1.0 - inf;
                        by2 = tiny ? 1.0 : 1.0 - en.speed * inf * dt / dv;
                    }
                    x1 = static_cast<f32>(bx1);
                    y1 = static_cast<f32>(std::clamp(by1, -20.0, 20.0));
                    x2 = static_cast<f32>(bx2);
                    y2 = static_cast<f32>(std::clamp(by2, -20.0, 20.0));
                }
            }
            const u32 idx = t.set(FrameIndex{w.local_frame(k.time)}, v, interp);
            if (idx < t.keys.size() && interp == Interpolation::Bezier) {
                Keyframe& kf = t.keys[idx];
                kf.bx1 = x1; kf.by1 = y1; kf.bx2 = x2; kf.by2 = y2;
                kf.easePower = 1;
            }
            ++written;
        }
        // O campo parado acompanha o primeiro keyframe (se a pessoa tirar os
        // keyframes depois, fica um valor que o arquivo tinha).
        if (!t.keys.empty())
            if (f32* p = home_ptr(l, c)) *p = t.keys.front().value;
    }
    if (curved && b.spatial && w.report)
        w.report->note("propriedade", b.label, "caminho curvo (tangentes espaciais) virou trajeto reto entre os keyframes", w.line);
    if (written) {
        mark_props(l, b);
        ++w.writes;
        if (!b.approx.empty() && w.report) w.report->note("propriedade", b.label, b.approx, w.line);
    }
    w.keyframes += written;
    return written;
}

bool write_expression(WriteCtx& w, const Binding& b, const std::string& source) {
    if (!w.layer || !b.ok()) return false;
    Layer& l = *w.layer;
    if (source.size() > expr::kMaxSourceBytes) {
        if (w.report) w.report->skip("expressao", b.label, "expressão longa demais", w.line);
        return false;
    }
    bool ok = true, any = false;
    const Track* first = nullptr;
    for (const Channel& c : b.ch) {
        if (c.src == kConstSrc) {
            f32 out = 0;
            if (to_aurea(c, 0.0, out)) set_home(w, l, c, out);
            continue;
        }
        if (!keyed_channel(c)) continue;
        if (l.tracks.size() >= kMaxTrackCount) break;
        const bool existed = l.tracks.find(c.prop, c.ei, c.pi) != nullptr;
        Track& t = l.tracks.get_or_create(c.prop, c.ei, c.pi);
        if (!existed) t.staticValue = home_value(l, c);
        t.expression = expr::compile(source);
        t.expressionEnabled = true;
        if (t.expression && !t.expression->parseError.ok) ok = false;
        any = true;
        first = &t;
    }
    // Compilou: uma avaliação de prova no início da camada pega o erro de
    // execução mais comum (camada, efeito ou função que o Aurea não tem). Quem
    // chama segura o lock do modelo; a timeline vem do provedor do motor.
    if (ok && any && first && first->expression && w.report) {
        (void)expr::evaluate_track_f(*first, 0.0, nullptr);
        const expr::Diagnostic d = first->expression->error();
        if (!d.ok) {
            w.report->skip("expressao", b.label,
                           "a expressão dá erro no Aurea (" + d.message + "); ficou gravada para corrigir", w.line);
            mark_props(l, b);
            return false;
        }
    }
    if (!any) {
        if (w.report) w.report->skip("expressao", b.label, "esta propriedade não aceita expressão no Aurea", w.line);
        return false;
    }
    mark_props(l, b);
    if (!ok && w.report) {
        // Fica gravada (a pessoa pode corrigir), mas não funciona: conta como
        // "de fora" para o relatório não prometer o que não acontece.
        const auto d = expr::check_syntax(source);
        w.report->skip("expressao", b.label,
                       "a expressão não compila no Aurea (" + d.message + ", linha " + std::to_string(d.line) +
                           "); ficou gravada para corrigir", w.line);
    }
    return ok;
}

void clear_keys(WriteCtx& w, const Binding& b) {
    if (!w.layer) return;
    for (const Channel& c : b.ch) {
        if (!keyed_channel(c) || c.src == kConstSrc || c.home == Home::TrackOnly) continue;
        if (Track* t = w.layer->tracks.find(c.prop, c.ei, c.pi)) {
            if (!t->keys.empty()) {
                if (f32* p = home_ptr(*w.layer, c)) *p = t->keys.front().value;
                t->staticValue = t->keys.front().value;
            }
            t->clear();
        }
    }
}

void clear_expression(WriteCtx& w, const Binding& b) {
    if (!w.layer) return;
    for (const Channel& c : b.ch)
        if (keyed_channel(c))
            if (Track* t = w.layer->tracks.find(c.prop, c.ei, c.pi)) t->expression.reset();
}

void set_expression_enabled(WriteCtx& w, const Binding& b, bool enabled) {
    if (!w.layer) return;
    for (const Channel& c : b.ch)
        if (keyed_channel(c))
            if (Track* t = w.layer->tracks.find(c.prop, c.ei, c.pi); t && t->expression) t->expressionEnabled = enabled;
}

std::vector<f64> read_value(const Layer& l, const Binding& b, i64 frame) {
    std::vector<f64> out(std::max<u32>(1, b.dims), 0.0);
    std::vector<bool> done(out.size(), false);
    for (const Channel& c : b.ch) {
        if (c.src == kConstSrc || c.src >= out.size() || done[c.src]) continue;
        f32 v = home_value(l, c);
        if (keyed_channel(c))
            if (const Track* t = l.tracks.find(c.prop, c.ei, c.pi); t && !t->keys.empty()) v = t->sample_keys(FrameIndex{frame});
        out[c.src] = to_ae(c, v);
        done[c.src] = true;
    }
    return out;
}

// =============================================================================
// Endereços
// =============================================================================
namespace {

Channel track(TrackProperty p, u8 src, f64 mul = 1.0, f64 add = 0.0) {
    Channel c;
    c.prop = p;
    c.src = src;
    c.mul = mul;
    c.add = add;
    return c;
}
Channel text_ch(u32 a, u32 param, u8 src, f64 mul = 1.0, f64 add = 0.0) {
    Channel c = track(TrackProperty::TextAnimParam, src, mul, add);
    c.ei = a;
    c.pi = param;
    return c;
}
Channel field_ch(Field f, u32 ei, u8 src, Conv conv = Conv::Linear) {
    Channel c;
    c.home = Home::Field;
    c.field = f;
    c.ei = ei;
    c.src = src;
    c.conv = conv;
    return c;
}

/// "Based On" do AE (1 caracteres, 2 sem espaços, 3 palavras, 4 linhas).
constexpr i8 kBasedOnMap[] = {-1, 0, 0, 1, 2};
/// Forma do intervalo do AE (1 quadrado ... 6 suave) → Aurea (0..5).
constexpr i8 kShapeMap[] = {-1, 0, 1, 2, 3, 4, 5};
/// Agrupamento da âncora (1 letra, 2 palavra, 3 linha, 4 tudo).
constexpr i8 kGroupingMap[] = {-1, 0, 1, 2, -1};

} // namespace

Binding bind_transform(const Layer& l, std::string_view m) {
    Binding b;
    b.label = std::string(m);
    using TP = TrackProperty;
    if (m == "ADBE Anchor Point") {
        if (l.kind == LayerKind::Text) {
            b.unsupported = "ponto de âncora da camada de texto: no AE a origem é a linha de base, no Aurea a caixa fica centrada";
            return b;
        }
        b.dims = 3; b.spatial = true;
        b.ch = {track(TP::AnchorX, 0), track(TP::AnchorY, 1), track(TP::AnchorZ, 2)};
    } else if (m == "ADBE Position") {
        b.dims = 3; b.spatial = true;
        b.ch = {track(TP::PositionX, 0), track(TP::PositionY, 1), track(TP::PositionZ, 2)};
        if (l.kind == LayerKind::Text) b.approx = "posição do texto: no AE vale para a linha de base, no Aurea para o centro da caixa";
    } else if (m == "ADBE Position_0") { b.ch = {track(TP::PositionX, 0)};
    } else if (m == "ADBE Position_1") { b.ch = {track(TP::PositionY, 0)};
    } else if (m == "ADBE Position_2") { b.ch = {track(TP::PositionZ, 0)};
    } else if (m == "ADBE Scale") {
        b.dims = 3;
        b.ch = {track(TP::ScaleX, 0, 0.01), track(TP::ScaleY, 1, 0.01), track(TP::ScaleZ, 2, 0.01)};
        b.fileMul[0] = b.fileMul[1] = b.fileMul[2] = 100.0;
    } else if (m == "ADBE Orientation") {
        b.dims = 3;
        b.ch = {track(TP::RotationX, 0), track(TP::RotationY, 1), track(TP::RotationZ, 2)};
        b.approx = "orientação 3D virou rotação X/Y/Z";
    } else if (m == "ADBE Rotate X") { b.ch = {track(TP::RotationX, 0)};
    } else if (m == "ADBE Rotate Y") { b.ch = {track(TP::RotationY, 0)};
    } else if (m == "ADBE Rotate Z") { b.ch = {track(TP::RotationZ, 0)};
    } else if (m == "ADBE Opacity") {
        Channel c = track(TP::Opacity, 0, 0.01);
        c.lo = 0.0; c.hi = 1.0;
        b.ch = {c};
        b.fileMul[0] = 100.0;
    } else if (m.substr(0, 9) == "ADBE Envi" || m.substr(0, 9) == "ADBE Mate" || m == "ADBE Transform Group") {
        b.unsupported = "~";   // opções de material 3D: ignoradas em silêncio
    } else {
        b.unsupported = "propriedade de transformação sem equivalente";
    }
    return b;
}

Binding bind_text_property(const Layer& l, u32 a, std::string_view m) {
    Binding b;
    b.label = std::string(m);
    b.animator = a;
    if (a >= l.text.animators.size()) { b.unsupported = "animador inexistente"; return b; }
    using namespace text;
    if (m == "ADBE Text Position 3D" || m == "ADBE Text Position") {
        b.dims = m == "ADBE Text Position" ? 2 : 3;
        b.ch = {text_ch(a, kPosX, 0), text_ch(a, kPosY, 1)};
        if (b.dims == 3) b.ch.push_back(text_ch(a, kPosZ, 2));
        b.textProps = kTextPropPosition;
    } else if (m == "ADBE Text Scale 3D" || m == "ADBE Text Scale") {
        b.dims = m == "ADBE Text Scale" ? 2 : 3;
        b.ch = {text_ch(a, kScaleX, 0), text_ch(a, kScaleY, 1)};
        b.textProps = kTextPropScale;
    } else if (m == "ADBE Text Skew") {
        b.ch = {text_ch(a, kSkew, 0)};
        b.textProps = kTextPropSkew;
    } else if (m == "ADBE Text Skew Axis") {
        Channel c = text_ch(a, kSkewAxis, 0);
        c.home = Home::TrackOnly;
        b.ch = {c};
        b.textProps = kTextPropSkew;
    } else if (m == "ADBE Text Rotation") {
        b.ch = {text_ch(a, kRotZ, 0)};
        b.textProps = kTextPropRotation;
    } else if (m == "ADBE Text Rotation X") {
        b.ch = {text_ch(a, kRotX, 0)};
        b.textProps = kTextPropRotation;
    } else if (m == "ADBE Text Rotation Y") {
        b.ch = {text_ch(a, kRotY, 0)};
        b.textProps = kTextPropRotation;
    } else if (m == "ADBE Text Opacity") {
        Channel c = text_ch(a, kOpacity, 0);
        c.lo = 0.0; c.hi = 100.0;
        b.ch = {c};
        b.textProps = kTextPropOpacity;
    } else if (m == "ADBE Text Fill Color" || m == "ADBE Text Stroke Color") {
        const bool fill = m == "ADBE Text Fill Color";
        b.dims = 4;
        for (u8 c = 0; c < 3; ++c) {
            Channel ch = text_ch(a, (fill ? kFillR : kStrokeR) + c, c);
            ch.lo = 0.0; ch.hi = 1.0;
            b.ch.push_back(ch);
        }
        b.textProps = fill ? kTextPropFill : kTextPropStroke;
    } else if (m == "ADBE Text Stroke Width") {
        b.ch = {text_ch(a, kStrokeWidth, 0)};
        b.textProps = kTextPropStrokeWidth;
    } else if (m == "ADBE Text Tracking Amount") {
        Channel em = text_ch(a, kTrackingEm, kConstSrc, 1.0, 1.0);
        em.home = Home::TrackOnly;
        b.ch = {text_ch(a, kTracking, 0), em};
        b.textProps = kTextPropTracking;
    } else if (m == "ADBE Text Blur") {
        b.dims = 2;
        b.ch = {text_ch(a, kBlur, 0)};
        b.textProps = kTextPropBlur;
    } else if (m == "ADBE Text Character Offset") {
        b.ch = {text_ch(a, kCharOffset, 0)};
        b.textProps = kTextPropCharOffset;
    } else if (m == "ADBE Text Track Type" || m == "ADBE Text Character Range" || m.substr(0, 11) == "ADBE 3DText") {
        b.unsupported = "~";
    } else if (m == "ADBE Text Anchor Point 3D" || m == "ADBE Text Anchor Point") {
        b.unsupported = "ponto de âncora por caractere do animador";
    } else if (m == "ADBE Text Fill Opacity" || m == "ADBE Text Stroke Opacity") {
        b.unsupported = "opacidade do preenchimento/contorno do animador";
    } else if (m.find("Hue") != std::string_view::npos || m.find("Saturation") != std::string_view::npos ||
               m.find("Brightness") != std::string_view::npos) {
        b.unsupported = "matiz/saturação/brilho do animador";
    } else if (m == "ADBE Text Line Spacing" || m == "ADBE Text Line Anchor") {
        b.unsupported = "espaçamento/âncora de linha do animador";
    } else if (m == "ADBE Text Character Change Type" || m == "ADBE Text Character Replace" ||
               m == "ADBE Text Character Value") {
        b.unsupported = "troca de caractere do animador (o Aurea só desloca)";
    } else {
        b.unsupported = "propriedade de animador de texto sem equivalente";
    }
    return b;
}

Binding bind_text_selector(const Layer& l, u32 a, u32 kind, std::string_view m, u32 indexUnits) {
    Binding b;
    b.label = std::string(m);
    b.animator = a;
    if (a >= l.text.animators.size()) { b.unsupported = "animador inexistente"; return b; }
    using namespace text;
    auto pct = [&](u32 param) {
        Channel c = text_ch(a, param, 0);
        b.ch = {c};
    };
    if (m == "ADBE Text Range Type2") {
        Channel c = field_ch(Field::SelBasedOn, a, 0, Conv::Enum);
        c.map = kBasedOnMap; c.mapN = 5;
        b.ch = {c};
        return b;
    }
    if (m == "ADBE Text Selector Mode") { b.unsupported = "~mode"; return b; }
    if (kind == 2) {
        if (m == "ADBE Text Expressible Amount") {
            b.dims = 3;
            pct(kSelAmount);
        } else {
            b.unsupported = "~";
        }
        return b;
    }
    if (kind == 1) {
        if (m == "ADBE Text Wiggly Max Amount") pct(kSelAmount);
        else if (m == "ADBE Text Temporal Freq") {
            Channel c = text_ch(a, kWiggleRate, 0);
            c.lo = 0.0; c.hi = 60.0;
            b.ch = {c};
        } else if (m == "ADBE Text Wiggly Random Seed" || m == "ADBE Text Random Seed") {
            b.ch = {field_ch(Field::SelSeed, a, 0, Conv::Round)};
        } else b.unsupported = "~wiggly";
        return b;
    }
    if (m == "ADBE Text Percent Start") pct(kSelStart);
    else if (m == "ADBE Text Percent End") pct(kSelEnd);
    else if (m == "ADBE Text Percent Offset") pct(kSelOffset);
    else if (m == "ADBE Text Index Start" || m == "ADBE Text Index End" || m == "ADBE Text Index Offset") {
        if (indexUnits == 0) { b.unsupported = "~index"; return b; }
        const f64 k = 100.0 / static_cast<f64>(indexUnits);
        const u32 param = m == "ADBE Text Index Start" ? kSelStart : m == "ADBE Text Index End" ? kSelEnd : kSelOffset;
        b.ch = {text_ch(a, param, 0, k)};
        b.approx = "seletor por índice convertido para % com o texto atual";
    } else if (m == "ADBE Text Selector Max Amount") pct(kSelAmount);
    else if (m == "ADBE Text Levels Max Ease") pct(kSelEaseHigh);
    else if (m == "ADBE Text Levels Min Ease") pct(kSelEaseLow);
    else if (m == "ADBE Text Range Shape") {
        Channel c = field_ch(Field::SelShape, a, 0, Conv::Enum);
        c.map = kShapeMap; c.mapN = 7;
        b.ch = {c};
    } else if (m == "ADBE Text Randomize Order") {
        b.ch = {field_ch(Field::SelRandom, a, 0, Conv::Bool)};
    } else if (m == "ADBE Text Random Seed") {
        b.ch = {field_ch(Field::SelSeed, a, 0, Conv::Round)};
    } else if (m == "ADBE Text Range Units" || m == "ADBE Text Selector Smoothness") {
        b.unsupported = "~";
    } else {
        b.unsupported = "ajuste de seletor sem equivalente";
    }
    return b;
}

Binding bind_text_path(const Layer& l, std::string_view m) {
    (void)l;
    Binding b;
    b.label = std::string(m);
    if (m == "ADBE Text Path") b.ch = {field_ch(Field::PathMask, kInvalidIndex, 0, Conv::Round)};
    else if (m == "ADBE Text Reverse Path") b.ch = {field_ch(Field::PathReverse, kInvalidIndex, 0, Conv::Bool)};
    else if (m == "ADBE Text Perpendicular To Path") b.ch = {field_ch(Field::PathPerpendicular, kInvalidIndex, 0, Conv::Bool)};
    else if (m == "ADBE Text Force Align Path") b.ch = {field_ch(Field::PathForceAlign, kInvalidIndex, 0, Conv::Bool)};
    else if (m == "ADBE Text First Margin") b.ch = {track(TrackProperty::TextPathFirstMargin, 0)};
    else if (m == "ADBE Text Last Margin") b.ch = {track(TrackProperty::TextPathLastMargin, 0)};
    else b.unsupported = "opção do texto no caminho sem equivalente";
    return b;
}

Binding bind_text_more(const Layer& l, std::string_view m) {
    (void)l;
    Binding b;
    b.label = std::string(m);
    if (m == "ADBE Text Anchor Point Option") {
        Channel c = field_ch(Field::Grouping, kInvalidIndex, 0, Conv::Enum);
        c.map = kGroupingMap; c.mapN = 5;
        b.ch = {c};
    } else if (m == "ADBE Text Anchor Point Align") {
        b.dims = 2;
        b.unsupported = "alinhamento do agrupamento (Grouping Alignment)";
    } else if (m == "ADBE Text Render Order" || m == "ADBE Text Character Blend Mode") {
        b.unsupported = "~";
    } else {
        b.unsupported = "opção de texto sem equivalente";
    }
    return b;
}

Binding bind_mask(const Layer& l, u32 maskId, std::string_view m) {
    (void)l;
    Binding b;
    b.label = std::string(m);
    auto mp = [&](u32 p, u8 src, f64 mul, f64 lo, f64 hi) {
        Channel c = track(TrackProperty::MaskParam, src, mul);
        c.ei = maskId;
        c.pi = p;
        c.lo = lo; c.hi = hi;
        return c;
    };
    if (m == "ADBE Mask Feather") { b.dims = 2; b.ch = {mp(0, 0, 1.0, 0.0, 10000.0)}; }
    else if (m == "ADBE Mask Offset") b.ch = {mp(1, 0, 1.0, -10000.0, 10000.0)};
    else if (m == "ADBE Mask Opacity") { b.ch = {mp(2, 0, 0.01, 0.0, 1.0)}; b.fileMul[0] = 100.0; }
    else b.unsupported = "propriedade de máscara sem equivalente";
    return b;
}

// =============================================================================
// Utilidades
// =============================================================================

void layer_size(const Layer& l, f64 compW, f64 compH, f64& w, f64& h) noexcept {
    w = compW; h = compH;
    if (l.kind == LayerKind::Shape && l.shape.bounds.w > 0 && l.shape.bounds.h > 0) {
        w = l.shape.bounds.w; h = l.shape.bounds.h;
        return;
    }
    if ((l.kind == LayerKind::Text || l.kind == LayerKind::Video || l.kind == LayerKind::Image) &&
        l.transform.anchor.x > 0.5f && l.transform.anchor.y > 0.5f) {
        w = 2.0 * l.transform.anchor.x;
        h = 2.0 * l.transform.anchor.y;
    }
    if (!(w > 0)) w = 1;
    if (!(h > 0)) h = 1;
}

u32 text_units(const std::string& s, u32 basedOn) noexcept {
    u32 chars = 0, nonSpace = 0, words = 0, lines = 1;
    bool inWord = false;
    for (usize i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        const usize n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (c == '\n' || c == '\r') ++lines;
        ++chars;
        if (!space) { ++nonSpace; if (!inWord) { ++words; inWord = true; } }
        else inWord = false;
        i += n;
    }
    switch (basedOn) {
        case 2: return std::max<u32>(1, nonSpace);
        case 3: return std::max<u32>(1, words);
        case 4: return std::max<u32>(1, lines);
        default: return std::max<u32>(1, chars);
    }
}

namespace {
bool ident(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}
std::string trim_name(std::string_view s) {
    while (!s.empty() && (s.front() == '\t' || s.front() == ' ')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == '\t' || s.back() == ' ')) s.remove_suffix(1);
    return std::string(s);
}
/// Lê um literal de texto em `src[i]` (aspas simples ou duplas). Devolve o fim
/// (posição depois da aspa) ou npos.
usize read_string(std::string_view src, usize i, std::string& out) {
    if (i >= src.size() || (src[i] != '"' && src[i] != '\'')) return std::string_view::npos;
    const char q = src[i++];
    out.clear();
    while (i < src.size() && src[i] != q) {
        if (src[i] == '\\' && i + 1 < src.size()) {
            const char e = src[i + 1];
            out.push_back(e == 't' ? '\t' : e == 'n' ? '\n' : e);
            i += 2;
            continue;
        }
        out.push_back(src[i++]);
    }
    return i < src.size() ? i + 1 : std::string_view::npos;
}
usize skip_ws(std::string_view s, usize i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    return i;
}
} // namespace

std::string rewrite_expression(std::string_view src, const std::vector<EffectAlias>& aliases) {
    std::string out;
    out.reserve(src.size() + 16);
    usize i = 0;
    while (i < src.size()) {
        if (src.compare(i, 6, "effect") == 0 && (i == 0 || !ident(src[i - 1])) && (i + 6 >= src.size() || !ident(src[i + 6]))) {
            usize j = skip_ws(src, i + 6);
            std::string name;
            usize end = std::string_view::npos;
            if (j < src.size() && src[j] == '(') {
                j = skip_ws(src, j + 1);
                end = read_string(src, j, name);
                if (end != std::string_view::npos) {
                    end = skip_ws(src, end);
                    if (end >= src.size() || src[end] != ')') end = std::string_view::npos;
                }
            }
            // Todos os apelidos com esse nome: um efeito de controles do AE com
            // vários parâmetros vira vários controles do Aurea com o mesmo nome,
            // e o parâmetro citado escolhe qual.
            std::vector<const EffectAlias*> hits;
            if (end != std::string_view::npos) {
                const std::string want = trim_name(name);
                for (const EffectAlias& a : aliases) if (iequals(trim_name(a.name), want)) hits.push_back(&a);
            }
            if (!hits.empty()) {
                usize k = skip_ws(src, end + 1);
                // O parâmetro logo depois: ("Nome" | "matchName-NNNN" | número).
                if (k < src.size() && src[k] == '(') {
                    usize a = skip_ws(src, k + 1);
                    usize close = std::string_view::npos;
                    std::string pname;
                    u32 num = 0;
                    bool byName = false;
                    if (const usize e = read_string(src, a, pname); e != std::string_view::npos) {
                        byName = true;
                        close = skip_ws(src, e);
                    } else {
                        usize d = a;
                        while (d < src.size() && src[d] >= '0' && src[d] <= '9') num = num * 10 + static_cast<u32>(src[d++] - '0');
                        if (d > a) close = skip_ws(src, d);
                    }
                    // Por número: a posição na interface do efeito do preset
                    // (`uiOrder`); sem ela, a ordem das regras (efeitos do .jsx).
                    if (close < src.size() && src[close] == ')') {
                        bool done = false;
                        for (const EffectAlias* h : hits) {
                            if (!h->rule) continue;
                            u16 ae = 0;
                            if (byName) ae = effect_param_number(*h->rule, trim_name(pname));
                            else if (!h->uiOrder.empty()) ae = num >= 1 && num <= h->uiOrder.size() ? h->uiOrder[num - 1] : 0;
                            else if (hits.size() == 1) ae = effect_param_by_position(*h->rule, num);
                            const char* id = ae ? aurea_param_for(*h->rule, ae) : nullptr;
                            if (!id) continue;
                            out += "effect(\"" + h->aureaRef + "\")(\"" + std::string(id) + "\")";
                            i = close + 1;
                            done = true;
                            break;
                        }
                        if (done) continue;
                    }
                }
                if (hits.size() == 1) {
                    out += "effect(\"" + hits.front()->aureaRef + "\")";
                    i = end + 1;
                    continue;
                }
                // Vários candidatos e nenhum resolveu: o texto fica como veio.
            }
        }
        out.push_back(src[i++]);
    }
    return out;
}

} // namespace detail
} // namespace aurea::ae
