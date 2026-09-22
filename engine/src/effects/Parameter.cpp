#include "aurea/effects/Parameter.hpp"
#include "aurea/animation/Curve.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea {

// =============================================================================
// ParameterRegistry
// =============================================================================
u32 ParameterRegistry::add(const ParamSpec& spec) {
    specs_.push_back(spec);
    return static_cast<u32>(specs_.size() - 1);
}

u32 ParameterRegistry::add_float(const char* id, const char* label, f32 def, f32 min, f32 max,
                                 u16 flags, const char* unit) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Float; s.flags = flags;
    s.defaultValue = ParamValue::scalar(def);
    s.minValue = min; s.maxValue = max; s.unit = unit;
    return add(s);
}

u32 ParameterRegistry::add_int(const char* id, const char* label, i32 def, i32 min, i32 max) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Int;
    s.defaultValue = ParamValue::scalar(static_cast<f32>(def));
    s.minValue = static_cast<f32>(min); s.maxValue = static_cast<f32>(max);
    return add(s);
}

u32 ParameterRegistry::add_bool(const char* id, const char* label, bool def) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Bool;
    s.defaultValue = ParamValue::boolean(def);
    s.minValue = 0.0f; s.maxValue = 1.0f;
    return add(s);
}

u32 ParameterRegistry::add_color(const char* id, const char* label, Vec4 def) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Color;
    s.defaultValue = ParamValue::color(def.x, def.y, def.z, def.w);
    s.minValue = 0.0f; s.maxValue = 1.0f;
    return add(s);
}

u32 ParameterRegistry::add_point2(const char* id, const char* label, Vec2 def, f32 min, f32 max,
                                  u16 flags) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Point2D; s.flags = flags;
    s.defaultValue = ParamValue::vec2(def.x, def.y);
    s.minValue = min; s.maxValue = max;
    return add(s);
}

u32 ParameterRegistry::add_point3(const char* id, const char* label, Vec3 def, f32 min, f32 max) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Point3D;
    s.defaultValue = ParamValue::vec3(def.x, def.y, def.z);
    s.minValue = min; s.maxValue = max;
    return add(s);
}

u32 ParameterRegistry::add_angle(const char* id, const char* label, f32 defDegrees, f32 min, f32 max) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Angle;
    s.defaultValue = ParamValue::scalar(defDegrees);
    s.minValue = min; s.maxValue = max; s.unit = "°";
    return add(s);
}

u32 ParameterRegistry::add_enum(const char* id, const char* label, const char* const* labels,
                                u32 count, u32 def) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Enum;
    s.flags = kParamNone;   // trocar de opção no meio de um frame não é interpolável
    s.defaultValue = ParamValue::scalar(static_cast<f32>(def));
    s.minValue = 0.0f; s.maxValue = count ? static_cast<f32>(count - 1) : 0.0f;
    s.enumCount = count; s.enumLabels = labels;
    return add(s);
}

u32 ParameterRegistry::add_curve(const char* id, const char* label) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Curve; s.flags = kParamNone;
    return add(s);
}

u32 ParameterRegistry::add_gradient(const char* id, const char* label) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::Gradient; s.flags = kParamNone;
    return add(s);
}

u32 ParameterRegistry::add_layer_ref(const char* id, const char* label) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::LayerReference; s.flags = kParamNone;
    return add(s);
}

u32 ParameterRegistry::add_texture_ref(const char* id, const char* label) {
    ParamSpec s;
    s.id = id; s.label = label; s.type = ParamType::TextureReference; s.flags = kParamNone;
    return add(s);
}

u32 ParameterRegistry::find(std::string_view id) const noexcept {
    for (u32 i = 0; i < specs_.size(); ++i) {
        if (id == specs_[i].id) return i;
    }
    return kInvalidIndex;
}

// =============================================================================
// CurveData
// =============================================================================
CurveData CurveData::identity() {
    CurveData c;
    for (auto& ch : c.channel) ch = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    return c;
}

bool CurveData::is_identity() const noexcept {
    for (const auto& ch : channel) {
        for (const Point& p : ch) {
            if (std::fabs(p.x - p.y) > 1e-6f) return false;
        }
    }
    return true;
}

f32 CurveData::evaluate(u32 ch, f32 x) const noexcept {
    if (ch >= 4) return x;
    const std::vector<Point>& pts = channel[ch];
    const usize n = pts.size();
    if (n == 0) return x;
    if (n == 1) return pts[0].y;
    if (x <= pts[0].x) return pts[0].y;
    if (x >= pts[n - 1].x) return pts[n - 1].y;

    usize k = 0;
    while (k + 1 < n && pts[k + 1].x < x) ++k;

    // Tangentes de Fritsch–Carlson nos dois extremos do segmento.
    auto secant = [&](usize i) noexcept {
        const f32 dx = pts[i + 1].x - pts[i].x;
        return dx > 1e-9f ? (pts[i + 1].y - pts[i].y) / dx : 0.0f;
    };
    // Tangente interna do PCHIP: média harmônica ponderada das secantes
    // vizinhas. Nos extremos, a própria secante.
    auto tangent = [&](usize i) noexcept -> f32 {
        if (i == 0) return secant(0);
        if (i == n - 1) return secant(n - 2);
        const f32 a = secant(i - 1);
        const f32 b = secant(i);
        if (a * b <= 0.0f) return 0.0f;   // extremo local: tangente plana, sem ultrapassar
        const f32 h0 = pts[i].x - pts[i - 1].x;
        const f32 h1 = pts[i + 1].x - pts[i].x;
        const f32 w1 = 2.0f * h1 + h0;
        const f32 w2 = h1 + 2.0f * h0;
        return (w1 + w2) / (w1 / a + w2 / b);
    };
    const f32 m0 = tangent(k);
    const f32 m1 = tangent(k + 1);
    const f32 h = pts[k + 1].x - pts[k].x;
    if (h <= 1e-9f) return pts[k].y;
    const f32 t = (x - pts[k].x) / h;
    const f32 t2 = t * t, t3 = t2 * t;
    const f32 h00 = 2 * t3 - 3 * t2 + 1;
    const f32 h10 = t3 - 2 * t2 + t;
    const f32 h01 = -2 * t3 + 3 * t2;
    const f32 h11 = t3 - t2;
    return h00 * pts[k].y + h10 * h * m0 + h01 * pts[k + 1].y + h11 * h * m1;
}

u64 CurveData::hash() const noexcept {
    u64 h = 1469598103934665603ull;
    for (const auto& ch : channel) {
        for (const Point& p : ch) {
            u32 bits[2];
            static_assert(sizeof(bits) == sizeof(Point));
            std::memcpy(bits, &p, sizeof(bits));
            h = (h ^ bits[0]) * 1099511628211ull;
            h = (h ^ bits[1]) * 1099511628211ull;
        }
        h = (h ^ 0xFFu) * 1099511628211ull;
    }
    return h;
}

// =============================================================================
// Avaliação
// =============================================================================
ParamValue evaluate_param(const TrackSet& tracks, const EffectInstance& effect, u32 paramIndex,
                          const ParamSpec& spec, FrameIndex localTime, bool* usedFallback) noexcept {
    if (usedFallback) *usedFallback = false;
    if (paramIndex >= effect.params.size()) return spec.defaultValue;
    const ParamSlot& slot = effect.params[paramIndex];

    ParamValue out = slot.constant;

    const bool fromExpressionSlot = slot.source == ParamSource::Expression;
    if (fromExpressionSlot) {
        // Gancho do avaliador de expressões: quando ele existir, o resultado
        // entra aqui e nada mais muda. Até lá, o valor constante vale e quem
        // chamou fica sabendo.
        if (usedFallback) *usedFallback = true;
    }

    const u32 comps = spec.animatable() && !fromExpressionSlot ? component_count(spec.type) : 0;
    for (u32 c = 0; c < comps; ++c) {
        const Track* t = tracks.find(TrackProperty::EffectParam, effect.id, param_track_key(paramIndex, c));
        if (t) out.v[c] = t->value_or(localTime, out.v[c]);   // keyframes e/ou expressão
    }
    // O efeito só vê valores dentro do contrato do parâmetro (§117). Uma
    // expressão, um keyframe antigo ou um arquivo corrompido podem trazer NaN,
    // infinito ou 1e6: o RGB no tempo com deslocamento de 1e6 quadros prendia
    // o quadro por 4–5 s esperando o decoder (fuzz Fuzz.EffectParameters…
    // RenderOnGpu). NaN/inf viram o padrão; o resto entra em [min, max].
    const u32 n = component_count(spec.type);
    for (u32 c = 0; c < n; ++c) {
        if (!std::isfinite(out.v[c])) out.v[c] = spec.defaultValue.v[c];
    }
    switch (spec.type) {
        case ParamType::Float:
        case ParamType::Int:
        case ParamType::Angle:
        case ParamType::Enum:
        case ParamType::Point2D:
        case ParamType::Point3D:
            if (spec.minValue < spec.maxValue) {
                for (u32 c = 0; c < n; ++c) out.v[c] = std::clamp(out.v[c], spec.minValue, spec.maxValue);
            }
            break;
        case ParamType::Bool:
            out.v[0] = std::clamp(out.v[0], 0.0f, 1.0f);
            break;
        default:
            break;   // cor pode passar de 1 (HDR); curva/gradiente/referência não são números
    }
    return out;
}

void initialize_instance(EffectInstance& instance, const ParameterRegistry& params) {
    instance.params.assign(params.count(), ParamSlot{});
    instance.curves.clear();
    instance.gradients.clear();
    for (u32 i = 0; i < params.count(); ++i) {
        const ParamSpec& s = params.at(i);
        ParamSlot& slot = instance.params[i];
        slot.constant = s.defaultValue;
        if (s.type == ParamType::Curve) {
            instance.curves.push_back(CurveData::identity());
            slot.constant.ref = instance.curves.size() - 1;
        } else if (s.type == ParamType::Gradient) {
            GradientData g;
            g.stops = {{0.0f, Vec4{0, 0, 0, 1}}, {1.0f, Vec4{1, 1, 1, 1}}};
            instance.gradients.push_back(std::move(g));
            slot.constant.ref = instance.gradients.size() - 1;
        }
    }
}

} // namespace aurea
