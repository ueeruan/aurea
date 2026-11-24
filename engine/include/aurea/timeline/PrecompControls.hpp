#pragma once

#include "aurea/timeline/ShapeFill.hpp"

namespace aurea {

inline constexpr u32 kMaxPrecompControls = 256;
inline constexpr usize kMaxPrecompText = 4096;

[[nodiscard]] inline u8 precomp_control_bit(PrecompControl kind) noexcept {
    return static_cast<u8>(1u << static_cast<u8>(kind));
}

[[nodiscard]] inline u8 available_precomp_controls(const Layer& layer) noexcept {
    u8 mask = 0;
    const auto driven = [&](TrackProperty property, u32 param = 0) {
        const Track* track = layer.tracks.find(property, 0, param);
        return track && track->driven();
    };
    if (layer.kind == LayerKind::Text && layer.captions.empty() && !layer.text.dynamic &&
        layer.text.sourceKeys.empty() && !layer.text.sourceExpression && layer.text.spans.empty()) {
        mask |= precomp_control_bit(PrecompControl::Text);
    }
    if (layer.kind == LayerKind::Shape && layer.shape.shapeType != kShapeVector && layer.shape.filled &&
        layer.shape.fillKind == 0 && !driven(TrackProperty::ShapeParam, shape_fill::kFillR) &&
        !driven(TrackProperty::ShapeParam, shape_fill::kFillG) && !driven(TrackProperty::ShapeParam, shape_fill::kFillB) &&
        !driven(TrackProperty::ShapeParam, shape_fill::kFillA)) {
        mask |= precomp_control_bit(PrecompControl::Color);
    }
    if (layer.kind != LayerKind::Audio && layer.kind != LayerKind::Camera && layer.kind != LayerKind::Light &&
        !driven(TrackProperty::Opacity)) {
        mask |= precomp_control_bit(PrecompControl::Opacity);
    }
    return mask;
}

inline void apply_precomp_value(Layer& layer, const PrecompControlValue& value) {
    switch (value.kind) {
        case PrecompControl::Text: layer.text.content = value.text; break;
        case PrecompControl::Color: layer.shape.fillColor = value.rgba; break;
        case PrecompControl::Opacity: layer.transform.opacity = value.rgba.x; break;
    }
}

} // namespace aurea
