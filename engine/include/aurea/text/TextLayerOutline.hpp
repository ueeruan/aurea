#pragma once

#include "aurea/text/TextOutline.hpp"

namespace aurea { struct Layer; }
namespace aurea::vector { struct Contour; }
namespace aurea::text {

struct OutlinedLayerGlyph {
    std::string label;
    std::vector<BezierPath> contours;
    Vec4 fill{1, 1, 1, 1};
    Vec4 stroke{0, 0, 0, 1};
    f32 strokeWidth = 0;
    f32 opacity = 1;
};

// Shared geometry used by text-to-shapes and live vector effects. Coordinates
// include the text layer's base raster padding.
[[nodiscard]] bool outline_layer(const Layer& layer, f64 localFrame, f64 compFrame,
                                 f64 fps, std::vector<OutlinedLayerGlyph>& out,
                                 const vector::Contour* path = nullptr);

} // namespace aurea::text
