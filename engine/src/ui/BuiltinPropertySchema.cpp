#include "aurea/ui/BuiltinPropertySchema.hpp"

#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/timeline/Layer.hpp"

#include <charconv>
#include <cstring>

namespace aurea::ui {
namespace {

BuiltinPropertySpec scalar(const char* id, const char* label, ParamType type,
    i32 binding, i32 track, f32 def, f32 sliderMin, f32 sliderMax,
    f32 typedMin, f32 typedMax, const char* unit, u32 precision,
    const char* group) noexcept {
    BuiltinPropertySpec p;
    p.id = id; p.label = label; p.type = type;
    p.bindingParams[0] = binding; p.trackProperties[0] = track;
    p.defaultValue.x = def;
    p.sliderMin = sliderMin; p.sliderMax = sliderMax;
    p.typedMin = typedMin; p.typedMax = typedMax;
    p.unit = unit; p.precision = precision; p.group = group;
    return p;
}

void condition(BuiltinPropertySpec& p, i32 binding, f32 a, f32 b = -1) noexcept {
    auto& c = p.conditions[p.conditionCount++];
    c.bindingParam = binding; c.equals[0] = a; c.count = 1;
    if (b >= 0) { c.equals[1] = b; c.count = 2; }
}

const std::array<BuiltinPropertySpec, 9>& lights() noexcept {
    static const auto specs = [] {
        const LightData d;
        const auto track = [](TrackProperty p) { return static_cast<i32>(p); };
        std::array<BuiltinPropertySpec, 9> p;
        // Typed bounds match Engine's LayerSetLightParam validation. Sliders
        // provide a comfortable subset; numeric input retains the full range.
        p[0] = scalar("kind", "Tipo de luz", ParamType::Enum, 0, -1, static_cast<f32>(d.kind), 0, 2, 0, 2, "", 0, "light");
        p[1] = scalar("intensity", "Intensidade", ParamType::Float, 1, track(TrackProperty::LightIntensity), d.intensity, 0, 10, 0, 1'000'000, "", 2, "light");
        p[2] = scalar("color", "Cor", ParamType::Color, 2, track(TrackProperty::LightColorR), d.color.x, 0, 1, 0, 1, "", 3, "light");
        p[2].components = 3; p[2].bindingParams = {2, 3, 4, -1};
        p[2].trackProperties = {track(TrackProperty::LightColorR), track(TrackProperty::LightColorG), track(TrackProperty::LightColorB), -1};
        p[2].defaultValue = d.color; p[2].colorSpace = "linear";
        p[3] = scalar("range", "Alcance", ParamType::Float, 5, -1, d.range, 0, 5000, 0, 1'000'000, "px", 1, "light"); condition(p[3], 0, 1, 2);
        p[4] = scalar("coneAngle", "Abertura do cone", ParamType::Angle, 6, track(TrackProperty::LightConeAngle), d.coneAngle, 0, 179, 0, 179, "°", 1, "light"); condition(p[4], 0, 2);
        p[5] = scalar("penumbra", "Penumbra", ParamType::Float, 7, track(TrackProperty::LightPenumbra), d.penumbra, 0, 1, 0, 1, "%", 1, "light"); condition(p[5], 0, 2);
        p[6] = scalar("castShadows", "Sombras", ParamType::Bool, 8, -1, d.castShadows ? 1.f : 0.f, 0, 1, 0, 1, "", 0, "shadows"); condition(p[6], 0, 0);
        p[7] = scalar("shadowBias", "Deslocamento da sombra", ParamType::Float, 9, -1, d.shadowBias, 0, .01f, 0, .1f, "", 4, "shadows"); condition(p[7], 0, 0); condition(p[7], 8, 1);
        p[8] = scalar("shadowStrength", "Força da sombra", ParamType::Float, 10, -1, d.shadowStrength, 0, 1, 0, 1, "%", 1, "shadows"); condition(p[8], 0, 0); condition(p[8], 8, 1);
        return p;
    }();
    return specs;
}

const std::array<BuiltinPropertySpec, 3>& materials() noexcept {
    static const auto specs = [] {
        const scene3d::Material d;
        const auto track = static_cast<i32>(TrackProperty::MaterialParam);
        std::array<BuiltinPropertySpec, 3> p;
        p[0] = scalar("baseColor", "Cor base", ParamType::Color, 0, track, d.baseColor.x, 0, 1, 0, 1, "", 3, "material");
        p[0].components = 4; p[0].bindingParams = {0, 1, 2, 3};
        p[0].trackProperties = {track, track, track, track};
        p[0].defaultValue = d.baseColor; p[0].colorSpace = "linear";
        p[1] = scalar("metallic", "Metálico", ParamType::Float, 4, track, d.metallic, 0, 1, 0, 1, "%", 1, "material");
        p[2] = scalar("roughness", "Rugosidade", ParamType::Float, 5, track, d.roughness, 0, 1, 0, 1, "%", 1, "material");
        for (auto& property : p) property.defaultSource = "asset";
        return p;
    }();
    return specs;
}

struct Json {
    char bytes[16 * 1024]{};
    usize size = 0;
    bool ok = true;
    void text(std::string_view value) noexcept {
        if (!ok || value.size() >= sizeof(bytes) - size) { ok = false; return; }
        std::memcpy(bytes + size, value.data(), value.size()); size += value.size(); bytes[size] = '\0';
    }
    void number(f32 value) noexcept {
        char digits[48];
        const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec != std::errc{}) { ok = false; return; }
        text({digits, static_cast<usize>(result.ptr - digits)});
    }
    void number(i32 value) noexcept {
        char digits[16]; const auto result = std::to_chars(digits, digits + sizeof(digits), value);
        if (result.ec != std::errc{}) { ok = false; return; }
        text({digits, static_cast<usize>(result.ptr - digits)});
    }
    void quoted(std::string_view value) noexcept {
        text("\"");
        for (char c : value) {
            if (c == '"' || c == '\\') text("\\");
            text({&c, 1});
        }
        text("\"");
    }
    void string_field(const char* name, const char* value) noexcept {
        text(","); quoted(name); text(":"); quoted(value);
    }
};

void panel(Json& j, const char* domain, std::span<const BuiltinPropertySpec> properties) noexcept {
    j.text("{\"domain\":"); j.quoted(domain); j.text(",\"properties\":[");
    bool first = true;
    for (const auto& p : properties) {
        if (!first) j.text(",");
        first = false;
        j.text("{\"id\":"); j.quoted(p.id); j.string_field("label", p.label);
        j.text(",\"type\":"); j.number(static_cast<i32>(p.type));
        j.text(",\"components\":"); j.number(static_cast<i32>(p.components));
        j.text(",\"bindingParams\":[");
        for (u32 c = 0; c < p.components; ++c) { if (c) j.text(","); j.number(p.bindingParams[c]); }
        j.text("],\"trackProperties\":[");
        for (u32 c = 0; c < p.components; ++c) { if (c) j.text(","); j.number(p.trackProperties[c]); }
        j.text("],\"defaultValue\":[");
        const f32 defaults[]{p.defaultValue.x, p.defaultValue.y, p.defaultValue.z, p.defaultValue.w};
        for (u32 c = 0; c < p.components; ++c) { if (c) j.text(","); j.number(defaults[c]); }
        j.text("],\"sliderMin\":"); j.number(p.sliderMin); j.text(",\"sliderMax\":"); j.number(p.sliderMax);
        j.text(",\"typedMin\":"); j.number(p.typedMin); j.text(",\"typedMax\":"); j.number(p.typedMax);
        j.text(",\"precision\":"); j.number(static_cast<i32>(p.precision));
        j.string_field("unit", p.unit); j.string_field("group", p.group);
        j.string_field("colorSpace", p.colorSpace); j.string_field("defaultSource", p.defaultSource);
        j.text(",\"conditions\":[");
        for (u32 c = 0; c < p.conditionCount; ++c) {
            if (c) j.text(",");
            const auto& condition = p.conditions[c];
            j.text("{\"bindingParam\":"); j.number(condition.bindingParam); j.text(",\"equals\":[");
            for (u32 v = 0; v < condition.count; ++v) { if (v) j.text(","); j.number(condition.equals[v]); }
            j.text("]}");
        }
        j.text("]");
        if (p.type == ParamType::Enum) j.text(",\"options\":[{\"value\":0,\"id\":\"directional\",\"label\":\"Luz direcional\"},{\"value\":1,\"id\":\"point\",\"label\":\"Luz pontual\"},{\"value\":2,\"id\":\"spot\",\"label\":\"Luz spot\"}]");
        j.text("}");
    }
    j.text("]}");
}

} // namespace

std::span<const BuiltinPropertySpec> builtin_properties(PropertyDomain domain) noexcept {
    return domain == PropertyDomain::Light ? std::span<const BuiltinPropertySpec>{lights()}
                                          : std::span<const BuiltinPropertySpec>{materials()};
}

std::string_view builtin_property_schema_json() noexcept {
    static const Json schema = [] {
        Json j; j.text("{\"version\":1,\"panels\":[");
        panel(j, "light", lights()); j.text(","); panel(j, "material", materials());
        j.text("]}"); return j;
    }();
    return schema.ok ? std::string_view{schema.bytes, schema.size} : std::string_view{};
}

} // namespace aurea::ui
