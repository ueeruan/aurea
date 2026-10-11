// The native inventory checks every exposed effect, including those after the
// first 64 catalog entries. It records metadata availability, not a claim that
// an untested platform UI has exercised every control.
#include "aurea/ui/BuiltinPropertySchema.hpp"
#include <set>

namespace {
std::string property_json_quote(std::string_view value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch < 32) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += static_cast<char>(ch);
    }
    return result + '"';
}
std::string_view property_blob_string(const std::vector<char>& blob, u32 offset, u32 length) {
    if (offset > blob.size() || length > blob.size() - offset) return {};
    return {blob.data() + offset, length};
}
}

AUREA_TEST(PropertyCoverage, NativeCatalogInventoryHasCompleteStableMetadata) {
    Engine engine;
    AUREA_CHECK(engine.initialize(headless_config()).ok());
    std::vector<bridge::EffectCatalogRow> catalog(64);
    std::vector<char> blob(1024 * 1024), specsBlob(1024 * 1024);
    u32 count = 0;
    do {
        count = engine.query_effect_catalog(catalog.data(), static_cast<u32>(catalog.size()),
                                           blob.data(), static_cast<u32>(blob.size()));
        if (count < catalog.size()) break;
        AUREA_CHECK(catalog.size() < 16384);
        if (catalog.size() >= 16384) { engine.shutdown(); return; }
        catalog.resize(catalog.size() * 2);
    } while (true);
    AUREA_CHECK(count > 64); // Guards the former truncated-inventory regression.
    std::set<u32> effectIds;
    u32 parameterCount = 0, visibleCount = 0, typeCounts[12]{};
    std::string inventory = "{\"version\":1,\"scope\":\"native metadata; UI runtime coverage is reported separately\",\"effects\":[";
    for (u32 i = 0; i < count; ++i) {
        const auto& effect = catalog[i];
        AUREA_CHECK(effectIds.insert(effect.typeId).second);
        const auto effectName = property_blob_string(blob, effect.nameOffset, effect.nameLength);
        AUREA_CHECK(!effectName.empty());
        std::vector<bridge::EffectParamRow> rows(std::max(1u, effect.paramCount));
        const u32 n = engine.query_effect_specs(effect.typeId, rows.data(), static_cast<u32>(rows.size()),
                                               specsBlob.data(), static_cast<u32>(specsBlob.size()));
        AUREA_CHECK_EQ(n, effect.paramCount);
        if (i) inventory += ',';
        inventory += "{\"type_id\":" + std::to_string(effect.typeId) + ",\"name\":" +
                     property_json_quote(effectName) + ",\"properties\":[";
        std::set<std::string> ids;
        for (u32 k = 0; k < n; ++k) {
            const auto& row = rows[k];
            const auto id = property_blob_string(specsBlob, row.idOffset, row.idLength);
            const auto label = property_blob_string(specsBlob, row.labelOffset, row.labelLength);
            AUREA_CHECK_EQ(row.index, k);
            AUREA_CHECK(!id.empty() && !label.empty());
            AUREA_CHECK(ids.insert(std::string(id)).second);
            AUREA_CHECK(row.type <= static_cast<u32>(ParamType::TextureReference));
            AUREA_CHECK(std::isfinite(row.minValue) && std::isfinite(row.maxValue) &&
                        std::isfinite(row.hardMin) && std::isfinite(row.hardMax));
            AUREA_CHECK(row.hardMin <= row.minValue && row.minValue <= row.maxValue && row.maxValue <= row.hardMax);
            for (f32 value : row.defaultValue) AUREA_CHECK(std::isfinite(value));
            if (row.type == static_cast<u32>(ParamType::Enum))
                AUREA_CHECK(row.enumCount > 0 && row.enumLength > 0);
            ++parameterCount;
            if (!(row.flags & kParamHidden)) ++visibleCount;
            if (row.type < 12) ++typeCounts[row.type];
            if (k) inventory += ',';
            inventory += "{\"index\":" + std::to_string(k) + ",\"id\":" + property_json_quote(id) +
                         ",\"label\":" + property_json_quote(label) + ",\"type\":" + std::to_string(row.type) +
                         ",\"flags\":" + std::to_string(row.flags) + ",\"components\":" +
                         std::to_string(component_count(static_cast<ParamType>(row.type))) +
                         ",\"slider_min\":" + std::to_string(row.minValue) + ",\"slider_max\":" +
                         std::to_string(row.maxValue) + ",\"typed_min\":" + std::to_string(row.hardMin) +
                         ",\"typed_max\":" + std::to_string(row.hardMax) + "}";
        }
        inventory += "]}";
    }
    inventory += "],\"effect_count\":" + std::to_string(count) + ",\"parameter_count\":" +
                 std::to_string(parameterCount) + ",\"visible_parameter_count\":" + std::to_string(visibleCount) +
                 ",\"type_counts\":[";
    for (u32 type = 0; type < 12; ++type) {
        if (type) inventory += ',';
        inventory += std::to_string(typeCounts[type]);
    }
    inventory += "],\"builtin_schema\":";
    inventory += ui::builtin_property_schema_json();
    inventory += "}\n";
    if (const char* path = std::getenv("AUREA_PROPERTY_COVERAGE_PATH")) {
        std::FILE* output = std::fopen(path, "wb");
        AUREA_CHECK(output != nullptr);
        if (output) {
            AUREA_CHECK_EQ(std::fwrite(inventory.data(), 1, inventory.size(), output), inventory.size());
            AUREA_CHECK_EQ(std::fclose(output), 0);
        }
    }
    std::printf("PROPERTY_INVENTORY effects=%u parameters=%u visible=%u metadata_only=1\n", count, parameterCount, visibleCount);
    engine.shutdown();
}
