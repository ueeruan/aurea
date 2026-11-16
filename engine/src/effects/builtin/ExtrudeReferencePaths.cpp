#include "aurea/effects/ExtrudeReference.hpp"
#include "aurea/text/TextLayerOutline.hpp"
#include "aurea/text/Captions.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/render/MaskRaster.hpp"
#include "aurea/vector/VectorTree.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::extrude_reference {
namespace {
f32 tolerance(const EffectEval& eval) {
    return std::clamp(eval.f(kFlatness), .005f, 10.f) *
        std::clamp(eval.f(kResolution), 1.f, 8.f);
}

void append_rings(std::vector<SourcePaths>& out, std::vector<vector::Contour> rings,
                  Vec4 color, f32 opacity, const vector::Affine2& transform = {}) {
    for (auto& ring : rings) for (auto& point : ring.pts) point = transform.apply(point);
    std::vector<vector::Contour> resolved;
    vector::resolve_fill(rings, vector::FillRule::NonZero, resolved);
    if (!resolved.empty()) out.push_back({std::move(resolved), color, opacity});
}

void capture_masks(const EffectEval& eval, std::vector<SourcePaths>& out) {
    const auto& masks = eval.layer->masks;
    const u32 selected = eval.e(kMaskPath);
    std::vector<vector::Contour> rings;
    for (u32 index = 0; index < masks.size(); ++index) {
        if (!eval.b(kAllPaths) && index + 1 != selected) continue;
        const auto& source = masks[index];
        std::vector<MaskPoint> points;
        std::vector<Vec4> edges;
        mask::evaluate_path(source, eval.time_frames(), points);
        mask::flatten(points, source.closed, tolerance(eval), {}, edges);
        vector::Contour ring;
        for (const Vec4 edge : edges) ring.pts.push_back({edge.x, edge.y});
        if (!source.closed && !edges.empty()) ring.pts.push_back({edges.back().z, edges.back().w});
        if (ring.pts.size() >= 3) rings.push_back(std::move(ring));
    }
    append_rings(out, std::move(rings), eval.color(kFrontColor), 1);
}

void capture_vectors(const EffectEval& eval, std::vector<SourcePaths>& out) {
    const auto& layer = *eval.layer;
    for (u32 index = 0; index < layer.shape.vector.groups.size(); ++index) {
        const auto group = vector::evaluate_group(layer.shape.vector.groups[index], layer.tracks,
            index, eval.time_frames(), eval.framesPerSecond);
        if (!group.visible) continue;
        vector::TreeResult tree;
        vector::evaluate_tree(group, eval.time_frames(), tolerance(eval), tree);
        const auto groupTransform = vector::group_matrix(group);
        for (const auto& paint : tree.layers) {
            if (paint.paint >= tree.paints.size()) continue;
            const auto& ink = tree.paints[paint.paint];
            append_rings(out, paint.rings, ink.color,
                group.opacity * .01f * paint.alpha * ink.opacity * .01f,
                groupTransform * paint.M);
        }
    }
}

void capture_shape(const EffectEval& eval, std::vector<SourcePaths>& out) {
    const auto& shape = eval.layer->shape;
    const Vec2 center{shape.bounds.x + shape.bounds.w * .5f, shape.bounds.y + shape.bounds.h * .5f};
    const Vec2 size{shape.bounds.w, shape.bounds.h};
    vector::Contour ring;
    BezierPath path;
    switch (shape.shapeType) {
        case 0: path = vector::make_rect(center, size, shape.cornerRadius); break;
        case 1: path = vector::make_ellipse(center, size); break;
        case 2: ring.pts = shape.path; break;
        case 3:
        case 4:
            path = vector::make_polystar({}, shape.points, .5f, shape.innerRadius * .5f,
                shape.cornerRadius, 0, -90, shape.shapeType == 4);
            for (auto& vertex : path.v) {
                vertex.p = {center.x + vertex.p.x * size.x, center.y + vertex.p.y * size.y};
                vertex.in = {vertex.in.x * size.x, vertex.in.y * size.y};
                vertex.out = {vertex.out.x * size.x, vertex.out.y * size.y};
            }
            break;
        default: return;
    }
    if (!path.v.empty()) vector::flatten(path, tolerance(eval), ring);
    if (ring.pts.size() >= 3) append_rings(out, {std::move(ring)}, shape.fillColor, 1);
}

bool caption_layer(const EffectEval& eval, Layer& result) {
    result = *eval.layer;
    const auto* segment = text::active_caption(result.captions, static_cast<i64>(std::floor(eval.time_frames())));
    if (!segment) return false;
    const auto options = text::caption_block_options(result.captionOptions, *segment);
    if (segment->look && eval.placement)
        text::apply_caption_block_look(result.captionOptions, options,
            std::min(eval.placement->compWidth, eval.placement->compHeight), result.text);
    result.text.content = options.uppercase ? text::upper_text(segment->text) : segment->text;
    std::vector<i64> starts;
    for (const auto& word : segment->words) starts.push_back(word.start);
    TextData timedText;
    TrackSet timedTracks;
    text::apply_caption_animation(options, timedText, timedTracks, starts, segment->end);
    const u32 base = static_cast<u32>(result.text.animators.size());
    result.text.animators.insert(result.text.animators.end(), timedText.animators.begin(), timedText.animators.end());
    for (u32 index = 0; index < timedTracks.size(); ++index) {
        Track track = timedTracks.at(index);
        track.effectIndex += base;
        result.tracks.add(std::move(track));
    }
    result.captions.clear();
    result.text.sourceKeys.clear();
    result.text.sourceExpressionEnabled = false;
    return true;
}

vector::Contour text_path(const EffectEval& eval, const Layer& layer) {
    vector::Contour path;
    if (layer.text.pathMask != kInvalidIndex) {
        for (const auto& mask : layer.masks) {
            if (mask.id != layer.text.pathMask) continue;
            std::vector<MaskPoint> points;
            std::vector<Vec4> edges;
            mask::evaluate_path(mask, eval.time_frames(), points);
            mask::flatten(points, mask.closed, tolerance(eval), {}, edges);
            path.closed = mask.closed;
            for (const Vec4 edge : edges) path.pts.push_back({edge.x, edge.y});
            if (!mask.closed && !edges.empty()) path.pts.push_back({edges.back().z, edges.back().w});
            break;
        }
    } else if (layer.text.pathLayer && eval.resources) {
        const auto samples = eval.resources->repeat_path(eval.layer, 2048, 0, layer.text.pathLayer);
        for (const Vec4 sample : samples) path.pts.push_back({sample.x, sample.y});
        if (path.pts.size() >= 2) {
            const f32 firstStep = (path.pts[1] - path.pts[0]).length();
            path.closed = (path.pts.back() - path.pts.front()).length() < firstStep * 2;
        }
    }
    return path;
}
}

Status source_paths(const EffectEval& eval, std::vector<SourcePaths>& out) {
    out.clear();
    if (!eval.layer) return Errc::InvalidArgument;
    if (!eval.layer->masks.empty() && eval.e(kMaskPath) != 0) {
        capture_masks(eval, out);
    } else if (eval.layer->kind == LayerKind::Text) {
        Layer caption;
        const Layer* layer = eval.layer;
        if (!layer->captions.empty()) {
            if (!caption_layer(eval, caption)) return OkStatus;
            layer = &caption;
        }
        const auto guide = text_path(eval, *layer);
        std::vector<text::OutlinedLayerGlyph> glyphs;
        const f64 compFrame = static_cast<f64>(eval.layer->start.value - eval.layer->offset.value) + eval.time_frames();
        if (!text::outline_layer(*layer, eval.time_frames(), compFrame, eval.framesPerSecond,
            glyphs, guide.pts.size() >= 2 ? &guide : nullptr))
            return OkStatus;
        for (const auto& glyph : glyphs) {
            std::vector<vector::Contour> rings;
            for (const auto& curve : glyph.contours) {
                vector::Contour ring;
                vector::flatten(curve, tolerance(eval), ring);
                if (ring.pts.size() >= 3) rings.push_back(std::move(ring));
            }
            append_rings(out, std::move(rings), glyph.fill, glyph.opacity);
        }
    } else if (eval.layer->kind == LayerKind::Shape) {
        if (eval.layer->shape.shapeType == kShapeVector) capture_vectors(eval, out);
        else capture_shape(eval, out);
    } else if (!eval.layer->masks.empty()) {
        capture_masks(eval, out);
    }
    if (eval.placement) {
        for (auto& source : out) for (auto& ring : source.rings)
            for (auto& point : ring.pts) point += eval.placement->sourceShift;
    }
    usize points = 0;
    for (const auto& source : out) for (const auto& ring : source.rings) points += ring.pts.size();
    if (points > 100000) return Errc::BudgetExceeded;
    return OkStatus;
}

} // namespace aurea::extrude_reference
