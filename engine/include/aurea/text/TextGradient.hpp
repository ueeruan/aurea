#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <vector>

namespace aurea {
class EffectRegistry;
class ParameterRegistry;
struct GlyphInstance;
struct Layer;

namespace text {
inline constexpr const char* kTextPrismEffect = "aurea.text.prism";
struct TextLayout;

void declare_prism_parameters(ParameterRegistry& params);

struct PrismDrawData {
    usize firstGlyph = 0;
    f32 stroke = 0;
    bool enabled = false;
    std::vector<Mat4> transforms;
};

// Appends auxiliary records before the drawable glyph range.
PrismDrawData prepare_prism(const Layer& layer, const TextLayout& layout,
                            const EffectRegistry& registry, f64 localTime,
                            std::vector<GlyphInstance>& data);
}
}
