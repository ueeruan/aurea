#include "aurea/project/Presets.hpp"

// Opt-in comparison runner. Point AUREA_SAPPHIRE_ORACLE_MATRIX at a JSON file
// in build/reference/sapphire-107 and run aurea_tests SapphireOracleMatrix.
// Input is straight sRGB RGBA8. Output is top-down linear premultiplied RGBA32F.
namespace sapphire_oracle_test {

bool whole_number(const json::Value* value, u32 min, u32 max) {
    return value && value->is_number() && value->number >= min && value->number <= max &&
           value->number == std::floor(value->number);
}

std::string apply_parameters(const json::Value& overrides, const ParameterRegistry& registry,
                             EffectInstance& effect) {
    if (!overrides.is_array()) return "params must be an array";
    constexpr std::array types{"Float", "Int", "Bool", "Color", "Point2D", "Point3D", "Angle", "Enum"};
    std::vector<bool> assigned(registry.count());
    for (const auto& entry : overrides.array) {
        const auto* index = entry.get("index");
        const auto* id = entry.get("param");
        const auto* type = entry.get("type");
        const auto* value = entry.get("value");
        if (!entry.is_object() || entry.object.size() != 3 || (index != nullptr) == (id != nullptr) ||
            !type || !type->is_string() || !value) {
            return "each override needs exactly index or param, type, and value";
        }
        const u32 slot = index && whole_number(index, 0, registry.count()) ? static_cast<u32>(index->number) :
                         id && id->is_string() ? registry.find(id->string) : kInvalidIndex;
        const std::string name = id && id->is_string() ? id->string : index && index->is_number() ?
                                 std::to_string(index->number) : "(invalid selector)";
        const std::string prefix = "parameter " + name + ": ";
        if (slot >= registry.count()) return prefix + "unknown parameter";
        if (assigned[slot]) return prefix + "duplicate override";
        const auto& spec = registry.at(slot);
        const auto typeIndex = static_cast<usize>(spec.type);
        if (typeIndex >= types.size()) return prefix + "parameter type is unsupported";
        if (type->string != types[typeIndex]) return prefix + "expected type " + types[typeIndex];
        assigned[slot] = true;

        ParamValue parsed;
        const u32 count = component_count(spec.type);
        if (spec.type == ParamType::Bool) {
            if (value->type != json::Value::Type::Bool) return prefix + "expected a JSON boolean";
            parsed = ParamValue::boolean(value->boolean);
        } else {
            if (count > 1 && (!value->is_array() || value->array.size() != count)) {
                return prefix + "expected " + std::to_string(count) + " numbers";
            }
            for (u32 i = 0; i < count; ++i) {
                const auto& component = count == 1 ? *value : value->array[i];
                const f64 number = component.number;
                if (!component.is_number() || !std::isfinite(number) ||
                    std::abs(number) > std::numeric_limits<f32>::max()) {
                    return prefix + "expected finite numbers representable as f32";
                }
                if (spec.type == ParamType::Enum && (spec.enumCount == 0 ||
                    !whole_number(&component, 0, spec.enumCount - 1))) return prefix + "invalid enum ordinal";
                if (spec.type == ParamType::Int && number != std::floor(number)) return prefix + "expected an integer";
                const f32 lo = spec.typed_min(), hi = spec.typed_max();
                parsed.v[i] = static_cast<f32>(number);
                if (spec.type != ParamType::Color && lo < hi && (parsed.v[i] < lo || parsed.v[i] > hi)) {
                    return prefix + "value outside the engine parameter range";
                }
            }
        }
        effect.params[slot].constant = parsed;
    }
    return {};
}

std::string render_case(const json::Value& item, const ImagePixels& input, f64 fps,
                        const std::filesystem::path& outputPath) {
    const auto* frame = item.get("frame");
    if (frame && !whole_number(frame, 0, std::numeric_limits<u32>::max())) return "invalid frame index";
    const FrameIndex time{frame ? static_cast<i64>(frame->number) : 0};
    const auto* key = item.get("effect");
    const auto* overrides = item.get("params");
    const ParameterRegistry* registry = nullptr;
    if (key) {
        if (!key->is_string() || key->string.empty() || key->string.find('\0') != std::string::npos) {
            return "effect must be a nonempty registry key";
        }
        registry = gpu().effects.params(gpu().effects.find_key(key->string));
        if (!registry) return "unknown Aurea effect " + key->string;
    } else if (overrides) {
        return "params require an effect";
    }

    Scene scene(input.width, input.height, fps);
    scene.comp->set_transparent_background(true);
    scene.comp->set_duration(FrameIndex{std::max<i64>(300, time.value + 1)});
    const LayerId layer = scene.image(input, input.width * 0.5f, input.height * 0.5f);
    if (registry) {
        auto& effect = scene.add_effect(layer, key->string.c_str());
        if (overrides) {
            std::string error = apply_parameters(*overrides, *registry, effect);
            if (!error.empty()) return error;
        }
    }

    const auto before = Registry::instance().failures();
    const FloatImage image = scene.render(time, 1, true);
    if (Registry::instance().failures() != before) return "offscreen render or readback failed";
    if (image.width != input.width || image.height != input.height || image.px.size() != input.rgba.size() ||
        !all_finite(image)) {
        return "invalid output image dimensions or samples";
    }
    std::ofstream output(outputPath, std::ios::binary);
    output.write(reinterpret_cast<const char*>(image.px.data()),
                 static_cast<std::streamsize>(image.px.size() * sizeof(f32)));
    output.close();
    return output.good() ? std::string{} : "could not write comparison frame";
}

} // namespace sapphire_oracle_test

AUREA_TEST(Gpu, MedianFilterRemovesSinglePixelImpulse) {
    if (!gpu().ok) return;
    constexpr u32 size = 16;
    ImagePixels pixels;
    pixels.width = size;
    pixels.height = size;
    pixels.rgba.resize(size * size * 4);
    for (usize i = size * 4; i < pixels.rgba.size(); i += 4) {
        pixels.rgba[i] = 64;
        pixels.rgba[i + 3] = 128;
    }
    const usize center = ((size / 2) * size + size / 2) * 4;
    pixels.rgba[center] = 255;
    pixels.rgba[center + 3] = 255;
    Scene scene(size, size);
    scene.comp->set_transparent_background(true);
    const LayerId layer = scene.image(std::move(pixels), size * 0.5f, size * 0.5f);
    if (!gpu().effects.params(gpu().effects.find_key("aurea.blur.median"))) {
        AUREA_CHECK_MSG(false, "median filter is missing from the registry");
        return;
    }
    const FloatImage original = scene.render();
    scene.add_effect(layer, "aurea.blur.median");
    const FloatImage filtered = scene.render();
    const f32* target = filtered.at(size / 2, size / 2);
    const f32* top = filtered.at(size / 2, 0);
    const f32* background = original.at(size / 2 - 1, size / 2);
    AUREA_CHECK(original.at(size / 2, size / 2)[3] > target[3] + 0.4f);
    for (u32 channel = 0; channel < 4; ++channel) {
        AUREA_CHECK(std::abs(target[channel] - background[channel]) < 0.002f);
        AUREA_CHECK(std::abs(top[channel] - background[channel]) < 0.002f);
    }
}

AUREA_TEST(CatalogCompletionGpu, ColorSpaceTransformKeepsAlphaAndRoundTrips) {
    if (!gpu().ok) return;
    constexpr u32 size = 32;
    ImagePixels pixels;
    pixels.width = pixels.height = size;
    for (u32 i = 0; i < size * size; ++i)
        pixels.rgba.insert(pixels.rgba.end(), {128, 96, 66, 117});
    Scene scene(size, size);
    scene.comp->set_transparent_background(true);
    const LayerId layer = scene.image(std::move(pixels), size * 0.5f, size * 0.5f);
    const FloatImage original = scene.render();
    const f32* before = original.at(12, 12);
    auto& forward = scene.add_effect(layer, "aurea.color.space_transform");
    forward.params[1].constant = ParamValue::scalar(8); // Linear Rec.709 → ACEScct.
    const FloatImage encoded = scene.render();
    const f32* encodedPixel = encoded.at(12, 12);
    AUREA_CHECK(all_finite(encoded));
    AUREA_CHECK(std::abs(encodedPixel[0] - before[0]) > 0.04f);
    AUREA_CHECK(std::abs(encodedPixel[3] - before[3]) < 0.002f);
    auto& reverse = scene.add_effect(layer, "aurea.color.space_transform");
    reverse.params[0].constant = ParamValue::scalar(8);
    const FloatImage restored = scene.render();
    const f32* after = restored.at(12, 12);
    for (u32 channel = 0; channel < 4; ++channel)
        AUREA_CHECK(std::abs(after[channel] - before[channel]) < 0.002f);
}

AUREA_TEST(CatalogCompletionGpu, SineWavesAnimateAndPreserveOpaqueMirroredEdges) {
    if (!gpu().ok) return;
    constexpr u32 size = 64;
    ImagePixels pixels;
    pixels.width = pixels.height = size;
    for (u32 y = 0; y < size; ++y)
        for (u32 x = 0; x < size; ++x)
            pixels.rgba.insert(pixels.rgba.end(), {u8(x * 4), u8(y * 4), 100, 255});
    Scene scene(size, size);
    scene.comp->set_transparent_background(true);
    scene.comp->set_duration(FrameIndex{60});
    const LayerId layer = scene.image(std::move(pixels), size * 0.5f, size * 0.5f);
    const FloatImage original = scene.render();
    auto& wave = scene.add_effect(layer, "aurea.distort.sine_waves");
    wave.params[0].constant = ParamValue::scalar(0);
    AUREA_CHECK(scene.render().px == original.px);
    wave.params[0].constant = ParamValue::scalar(18);
    wave.params[1].constant = ParamValue::scalar(3);
    wave.params[5].constant = ParamValue::scalar(1);
    const FloatImage start = scene.render(FrameIndex{0});
    const FloatImage later = scene.render(FrameIndex{7});
    AUREA_CHECK(all_finite(start));
    AUREA_CHECK(start.px != original.px && later.px != start.px);
    for (usize i = 3; i < start.px.size(); i += 4)
        AUREA_CHECK(start.px[i] > 0.99f && later.px[i] > 0.99f);
}

AUREA_TEST(Gpu, SapphireOracleMatrix) {
    using namespace sapphire_oracle_test;
    const char* configured = std::getenv("AUREA_SAPPHIRE_ORACLE_MATRIX");
    if (!configured || !*configured) return;
    const std::filesystem::path matrixPath(configured);
    std::ifstream matrix(matrixPath, std::ios::binary);
    AUREA_CHECK_MSG(matrix.is_open(), "could not open AUREA_SAPPHIRE_ORACLE_MATRIX");
    if (!matrix) return;
    const std::string source((std::istreambuf_iterator<char>(matrix)), std::istreambuf_iterator<char>());
    json::Value root;
    std::string error;
    if (!json::parse(source, root, &error) || !root.is_object()) {
        AUREA_CHECK_MSG(false, ("invalid matrix JSON: " + error).c_str());
        return;
    }
    const auto* width = root.get("width");
    const auto* height = root.get("height");
    const auto* fps = root.get("fps");
    const auto* fixture = root.get("fixture");
    const auto* cases = root.get("cases");
    if (!whole_number(width, 1, 4096) || !whole_number(height, 1, 4096) || !fixture ||
        !fixture->is_string() || fixture->string.empty() || fixture->string.find('\0') != std::string::npos ||
        !cases || !cases->is_array() || cases->array.empty() ||
        (fps && (!fps->is_number() || !(fps->number >= kMinCompositionFps && fps->number <= kMaxCompositionFps)))) {
        AUREA_CHECK_MSG(false, "matrix requires valid width, height, fixture, cases and optional fps");
        return;
    }
    ImagePixels input;
    input.width = static_cast<u32>(width->number);
    input.height = static_cast<u32>(height->number);
    input.rgba.resize(static_cast<usize>(input.width) * input.height * 4);
    const auto folder = matrixPath.parent_path();
    std::ifstream pixels(folder / fixture->string, std::ios::binary);
    if (!pixels.read(reinterpret_cast<char*>(input.rgba.data()), static_cast<std::streamsize>(input.rgba.size())) ||
        pixels.peek() != std::char_traits<char>::eof()) {
        AUREA_CHECK_MSG(false, "fixture must contain exactly width * height * 4 RGBA8 bytes");
        return;
    }
    AUREA_CHECK_MSG(gpu().ok, "comparison requires a working GPU backend");
    if (!gpu().ok) return;

    std::vector<std::string> ids;
    for (usize i = 0; i < cases->array.size(); ++i) {
        const auto& item = cases->array[i];
        const auto* id = item.get("id");
        if (!id || !id->is_string() || id->string.empty() ||
            id->string.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos ||
            std::find(ids.begin(), ids.end(), id->string) != ids.end()) {
            AUREA_CHECK_MSG(false, ("invalid or duplicate id at case " + std::to_string(i)).c_str());
            continue;
        }
        ids.push_back(id->string);
        error = render_case(item, input, fps ? fps->number : 30, folder / (id->string + ".rgba32f"));
        AUREA_CHECK_MSG(error.empty(), (id->string + ": " + error).c_str());
        if (error.empty()) std::printf("\n    rendered %s.rgba32f", id->string.c_str());
    }
    std::printf("\n    ");
}
