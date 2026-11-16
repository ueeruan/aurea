#include "aurea/effects/ExtrudeReference.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::extrude_reference {
namespace {
struct Vertex {
    Vec4 position;
    Vec4 source;
    Vec4 color;
    Vec4 stroke;
};
static_assert(sizeof(Vertex) == sizeof(Vec4) * 4);

bool contains(const std::vector<Vec2>& ring, Vec2 point) {
    bool inside = false;
    for (usize index = 0, previous = ring.size() - 1; index < ring.size(); previous = index++) {
        const Vec2 a = ring[index], b = ring[previous];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

struct InversePlane {
    f64 matrix[3][3]{};
    bool valid = false;
    explicit InversePlane(const Mat4& transform) {
        const f64 a = transform.col[0].x, b = transform.col[1].x, c = transform.col[3].x;
        const f64 d = transform.col[0].y, e = transform.col[1].y, f = transform.col[3].y;
        const f64 g = transform.col[0].w, h = transform.col[1].w, i = transform.col[3].w;
        const f64 determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12) return;
        const f64 inverse = 1 / determinant;
        matrix[0][0] = (e * i - f * h) * inverse;
        matrix[0][1] = (c * h - b * i) * inverse;
        matrix[0][2] = (b * f - c * e) * inverse;
        matrix[1][0] = (f * g - d * i) * inverse;
        matrix[1][1] = (a * i - c * g) * inverse;
        matrix[1][2] = (c * d - a * f) * inverse;
        matrix[2][0] = (d * h - e * g) * inverse;
        matrix[2][1] = (b * g - a * h) * inverse;
        matrix[2][2] = (a * e - b * d) * inverse;
        valid = true;
    }
    Vec2 apply(Vec2 point) const {
        const f64 w = matrix[2][0] * point.x + matrix[2][1] * point.y + matrix[2][2];
        if (!valid || !std::isfinite(w) || std::abs(w) < 1e-9) return {NAN, NAN};
        return {static_cast<f32>((matrix[0][0] * point.x + matrix[0][1] * point.y + matrix[0][2]) / w),
                static_cast<f32>((matrix[1][0] * point.x + matrix[1][1] * point.y + matrix[1][2]) / w)};
    }
};

struct Projection {
    const EffectEval& eval;
    u32 mode;
    Vec2 direction, vanishing;
    Mat4 world, camera;
    InversePlane layerFromComp;
    f32 depthRange;
    explicit Projection(const EffectEval& value)
        : eval(value), mode(value.e(kMode)),
          layerFromComp(value.placement ? value.placement->compFromLayer : Mat4::identity()) {
        const f32 angle = value.f(kDirection) * kDeg2Rad;
        direction = {std::cos(angle), std::sin(angle)};
        const auto* placement = value.placement;
        const f32 width = placement ? static_cast<f32>(placement->layerWidth) : 1;
        const f32 height = placement ? static_cast<f32>(placement->layerHeight) : 1;
        vanishing = {value.p2(kVanishing).x * width, value.p2(kVanishing).y * height};
        depthRange = std::max(std::abs(value.f(kFrontLength)) + std::abs(value.f(kBackLength)), 1.f);
        if (!placement) return;
        camera = placement->sceneCompFromWorld;
        world = placement->layerWorldValid ? placement->layerWorld *
            Mat4::translation({-placement->sourceShift.x, -placement->sourceShift.y, 0}) : placement->compFromLayer;
        const Vec2 anchor{value.p2(kAnchor).x * width, value.p2(kAnchor).y * height};
        const u64 parent = value.value(kParent).ref;
        LayerWorldInfo parentWorld;
        const f64 compFrame = value.layer ? value.time_frames() +
            static_cast<f64>(value.layer->start.value - value.layer->offset.value) : value.time_frames();
        if (parent && value.resources && value.resources->layer_world(value.layer, parent, compFrame, parentWorld))
            world = parentWorld.worldFromLayer * Mat4::translation({-anchor.x, -anchor.y, 0});
        else world = world * Mat4::translation({width * .5f - anchor.x, height * .5f - anchor.y, 0});
    }

    Vertex point(Vec2 source, bool front, Vec4 color, f32 normalAngle, u32 kind) const {
        const f32 distance = front ? eval.f(kFrontLength) : -eval.f(kBackLength);
        Vec2 position = source;
        f32 depth = .5f + distance / (depthRange * 2.1f);
        f32 perspective = 1;
        if (mode == 0) position += direction * distance;
        else if (mode == 1) {
            const f32 amount = (front ? eval.f(kFrontAmount) : eval.f(kBackAmount)) * .01f;
            position += (vanishing - source) * amount;
            depth = front ? .75f : .25f;
        } else if (eval.placement && eval.placement->sceneCameraValid) {
            const Vec4 projected = camera * world * Vec4{source.x, source.y, -distance, 1};
            perspective = projected.w;
            if (!std::isfinite(perspective) || !(perspective > 1e-6f))
                return {{NAN, NAN, 0, 1}, {}, {}, {}};
            position = layerFromComp.apply({projected.x / perspective, projected.y / perspective});
            depth = projected.z / perspective;
        }
        if (eval.e(kDebugView) != 4) position += {eval.f(kDebugX), eval.f(kDebugY)};
        return {{position.x, position.y, depth, perspective},
                {source.x, source.y, normalAngle, static_cast<f32>(kind)}, color, {}};
    }
};

void triangle(std::vector<Vec4>& vertices, const Vertex& a, const Vertex& b, const Vertex& c) {
    for (const Vertex* vertex : {&a, &b, &c}) {
        if (!std::isfinite(vertex->position.x) || !std::isfinite(vertex->position.y)) return;
    }
    for (const Vertex* vertex : {&a, &b, &c}) {
        vertices.push_back(vertex->position);
        vertices.push_back(vertex->source);
        vertices.push_back(vertex->color);
        vertices.push_back(vertex->stroke);
    }
}

void quad(std::vector<Vec4>& vertices, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
    triangle(vertices, a, b, c);
    triangle(vertices, a, c, d);
}

void stroke_line(std::vector<Vec4>& vertices, Vertex a, Vertex b, f32 width, Vec4 color) {
    const Vec2 delta{b.position.x - a.position.x, b.position.y - a.position.y};
    const f32 length = delta.length();
    if (!(length > 1e-6f)) return;
    const Vec2 offset = Vec2{-delta.y, delta.x} * ((width * .5f + 1) / length);
    a.color = color; b.color = color;
    a.source.w = 3; b.source.w = 3;
    a.position.z = std::min(a.position.z + 1e-5f, 1.f);
    b.position.z = std::min(b.position.z + 1e-5f, 1.f);
    Vertex c = b, d = a;
    a.position.x += offset.x; a.position.y += offset.y;
    b.position.x += offset.x; b.position.y += offset.y;
    c.position.x -= offset.x; c.position.y -= offset.y;
    d.position.x -= offset.x; d.position.y -= offset.y;
    const f32 edge = width * .5f + 1;
    a.stroke = {edge, width * .5f, 0, 0}; b.stroke = a.stroke;
    c.stroke = {-edge, width * .5f, 0, 0}; d.stroke = c.stroke;
    quad(vertices, a, b, c, d);
}

void caps(const Projection& projection, const SourcePaths& source, std::vector<Vec4>& vertices) {
    const auto& rings = source.rings;
    for (usize outer = 0; outer < rings.size(); ++outer) {
        if (vector::signed_area(rings[outer].pts) <= 0) continue;
        std::vector<std::vector<Vec2>> polygon{rings[outer].pts};
        for (usize hole = 0; hole < rings.size(); ++hole) {
            if (vector::signed_area(rings[hole].pts) < 0 &&
                contains(rings[outer].pts, rings[hole].pts.front())) polygon.push_back(rings[hole].pts);
        }
        std::vector<u32> indices;
        if (!scene3d::triangulate_polygon(polygon, indices)) continue;
        std::vector<Vec2> points;
        for (const auto& ring : polygon) points.insert(points.end(), ring.begin(), ring.end());
        for (bool front : {false, true}) {
            if (projection.eval.e(kDebugView) < 4 && projection.eval.e(kDebugView) != (front ? 0u : 1u)) continue;
            Vec4 color = source.color;
            color.w *= source.opacity;
            for (usize index = 0; index + 2 < indices.size(); index += 3) {
                triangle(vertices,
                    projection.point(points[indices[index]], front, color, 0, front ? 0u : 1u),
                    projection.point(points[indices[index + 1]], front, color, 0, front ? 0u : 1u),
                    projection.point(points[indices[index + 2]], front, color, 0, front ? 0u : 1u));
            }
        }
    }
}
}

Status make_geometry(const EffectEval& eval, const std::vector<SourcePaths>& sources,
                     std::vector<Vec4>& vertices, Rect& bounds) {
    vertices.clear();
    bounds = {};
    Projection projection(eval);
    Vec4 strokeColor = eval.color(kStrokeColor);
    strokeColor.w *= std::clamp(eval.f(kStrokeOpacity), 0.f, 100.f) * .01f;
    for (const auto& source : sources) {
        caps(projection, source, vertices);
        for (const auto& ring : source.rings) {
            const bool hole = vector::signed_area(ring.pts) < 0;
            if (hole && !eval.b(kInner)) continue;
            for (usize index = 0; index < ring.pts.size(); ++index) {
                const Vec2 a = ring.pts[index], b = ring.pts[(index + 1) % ring.pts.size()];
                const Vec2 edge = b - a;
                const f32 angle = std::atan2(-edge.x, edge.y) / kDeg2Rad;
                Vec4 color = source.color;
                color.w *= source.opacity;
                const Vertex af = projection.point(a, true, color, angle, 2);
                const Vertex bf = projection.point(b, true, color, angle, 2);
                const Vertex ab = projection.point(a, false, color, angle, 2);
                const Vertex bb = projection.point(b, false, color, angle, 2);
                if (eval.e(kDebugView) == 2 || eval.e(kDebugView) == 4) quad(vertices, af, bf, bb, ab);
                if (eval.f(kStrokeWidth) > 0 && (eval.e(kDebugView) == 3 || eval.e(kDebugView) == 4)) {
                    stroke_line(vertices, af, bf, eval.f(kStrokeWidth), strokeColor);
                    stroke_line(vertices, ab, bb, eval.f(kStrokeWidth), strokeColor);
                    const Vec2 previous = a - ring.pts[(index + ring.pts.size() - 1) % ring.pts.size()];
                    const f32 cosine = previous.normalized().dot(edge.normalized());
                    const f32 corner = std::acos(std::clamp(cosine, -1.f, 1.f)) / kDeg2Rad;
                    if (corner > eval.f(kCornerThreshold))
                        stroke_line(vertices, af, ab, eval.f(kStrokeWidth), strokeColor);
                }
            }
        }
        if (vertices.size() > 4 * 1000000) return Errc::BudgetExceeded;
    }
    Vec2 minimum{1e30f, 1e30f}, maximum{-1e30f, -1e30f};
    for (usize index = 0; index + 3 < vertices.size(); index += 4) {
        const Vec4 point = vertices[index];
        minimum.x = std::min(minimum.x, point.x); minimum.y = std::min(minimum.y, point.y);
        maximum.x = std::max(maximum.x, point.x); maximum.y = std::max(maximum.y, point.y);
    }
    if (!vertices.empty()) bounds = {minimum.x - 1, minimum.y - 1, maximum.x - minimum.x + 2, maximum.y - minimum.y + 2};
    return OkStatus;
}

} // namespace aurea::extrude_reference
