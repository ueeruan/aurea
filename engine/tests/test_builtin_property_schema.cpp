#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/ui/BuiltinPropertySchema.hpp"

#include <limits>
#include <string_view>

using namespace aurea;

AUREA_TEST(BuiltinPropertySchema, EveryEditableLightAndMaterialBindingHasOneDescriptor) {
    for (const auto domain : {ui::PropertyDomain::Light, ui::PropertyDomain::Material}) {
        bool seen[11]{};
        const u32 count = domain == ui::PropertyDomain::Light ? 11u : 6u;
        for (const auto& property : ui::builtin_properties(domain)) {
            AUREA_CHECK(property.id && property.id[0]);
            AUREA_CHECK(property.components >= 1 && property.components <= 4);
            AUREA_CHECK(property.typedMin <= property.sliderMin && property.sliderMin <= property.sliderMax && property.sliderMax <= property.typedMax);
            if (domain == ui::PropertyDomain::Light && property.type == ParamType::Color) AUREA_CHECK_EQ(property.components, 3u);
            if (domain == ui::PropertyDomain::Material) AUREA_CHECK(std::string_view(property.defaultSource) == "asset");
            for (u32 c = 0; c < property.components; ++c) {
                const auto binding = property.bindingParams[c];
                AUREA_CHECK(binding >= 0 && static_cast<u32>(binding) < count);
                if (binding < 0 || static_cast<u32>(binding) >= count) continue;
                AUREA_CHECK(!seen[binding]); seen[binding] = true;
                if (domain == ui::PropertyDomain::Material) AUREA_CHECK_EQ(property.trackProperties[c], static_cast<i32>(TrackProperty::MaterialParam));
            }
        }
        for (u32 c = 0; c < count; ++c) AUREA_CHECK(seen[c]);
    }
    const auto json = ui::builtin_property_schema_json();
    AUREA_CHECK(!json.empty());
    AUREA_CHECK(json.find("\"version\":1") != std::string_view::npos);
    AUREA_CHECK(json.find("\"id\":\"spot\"") != std::string_view::npos);
    AUREA_CHECK(json.data()[json.size()] == '\0');
    AUREA_CHECK(json.data() == ui::builtin_property_schema_json().data());
}

AUREA_TEST(BuiltinPropertySchema, NativeLightAcceptsTypedBoundsAndRejectsOutOfRangeWithoutMutation) {
    Engine e;
    EngineConfig config; config.workerCount = 1; config.disableAutosave = true;
    const bool ready = e.initialize(config).ok() && e.new_project(320, 180, 30, nullptr).ok();
    AUREA_CHECK(ready); if (!ready) return;
    const auto added = e.add_light(2);
    AUREA_CHECK(added.ok()); if (!added.ok()) { e.shutdown(); return; }
    const auto id = *added;
    for (const auto& property : ui::builtin_properties(ui::PropertyDomain::Light)) {
        if (property.type == ParamType::Bool || property.type == ParamType::Enum) continue;
        for (u32 c = 0; c < property.components; ++c) {
            const auto binding = static_cast<u32>(property.bindingParams[c]);
            const auto set = [&](f32 value) {
                Command command; command.type = CommandType::LayerSetLightParam;
                command.shape_param.layer = LayerId::unpack(id); command.shape_param.param = binding; command.shape_param.value = value;
                return e.apply_command(command);
            };
            AUREA_CHECK(set(property.typedMax).ok());
            f32 before[11]{}; AUREA_CHECK(e.query_light(id, before, 11));
            AUREA_CHECK_NEAR(before[binding], property.typedMax, 1e-6f);
            const f32 invalid = std::nextafter(property.typedMax, std::numeric_limits<f32>::infinity());
            AUREA_CHECK(!set(invalid).ok());
            f32 after[11]{}; AUREA_CHECK(e.query_light(id, after, 11));
            AUREA_CHECK_NEAR(after[binding], before[binding], 0.f);
            AUREA_CHECK(set(property.typedMin).ok());
            AUREA_CHECK(!set(-.0001f).ok());
        }
    }
    e.shutdown();
}
