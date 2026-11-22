#pragma once

#include "aurea/core/Math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <span>
#include <vector>

namespace aurea::autorig {

enum class Part : u8 { Body, Face, Hair, LeftHand, RightHand, LeftArm, RightArm, LeftLeg, RightLeg, LeftLid, RightLid, Count };

inline bool is_lid(Part p) { return p == Part::LeftLid || p == Part::RightLid; }

struct Contour {
    std::vector<Vec2> points;
    bool hole = false;
};

struct Cut {
    Part part{};
    Vec2 pivot{}, tip{};
    std::vector<Contour> contours;
    Vec2 down{};
    Vec4 fill{};
};

namespace detail {

struct Pixel { f32 r, g, b; bool opaque; };

inline f32 color_dist(Pixel a, Pixel b) {
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

inline bool same_skin(Pixel a, Pixel b) {
    if (!a.opaque || !b.opaque || a.r + a.g + a.b < 140) return false;
    const f32 ar = a.r - a.g, ab = a.b - a.g;
    const f32 br = b.r - b.g, bb = b.b - b.g;
    return std::abs(ar - br) < 29 && std::abs(ab - bb) < 32 && color_dist(a, b) < 108;
}

struct Grid {
    i32 w, h;
    std::vector<Pixel> pixels;
    std::vector<Part> labels;

    [[nodiscard]] i32 index(i32 x, i32 y) const { return y * w + x; }
    [[nodiscard]] Vec2 center(i32 p) const { return {p % w + .5f, p / w + .5f}; }
    [[nodiscard]] i32 nearest(Vec2 p) const {
        return index(std::clamp(static_cast<i32>(p.x), 0, w - 1), std::clamp(static_cast<i32>(p.y), 0, h - 1));
    }

    template<class Visit> void neighbors(i32 p, Visit visit) const {
        if (p % w > 0) visit(p - 1);
        if (p % w + 1 < w) visit(p + 1);
        if (p >= w) visit(p - w);
        if (p + w < w * h) visit(p + w);
    }
};

template<class Predicate>
std::vector<i32> component(const Grid& g, i32 seed, std::vector<u8>& visited, Predicate eligible) {
    if (visited[seed] || !eligible(seed)) return {};
    std::vector<i32> out{seed};
    visited[seed] = 1;
    for (usize cursor = 0; cursor < out.size(); ++cursor) {
        g.neighbors(out[cursor], [&](i32 p) {
            if (!visited[p] && eligible(p)) { visited[p] = 1; out.push_back(p); }
        });
    }
    return out;
}

template<class Predicate>
std::vector<f32> skin_distances(const Grid& g, std::span<const Vec2> seeds, f32 snapDistance, Predicate eligible) {
    using Node = std::pair<f32, i32>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    std::vector<f32> distances(g.labels.size(), std::numeric_limits<f32>::infinity());
    for (Vec2 seed : seeds) {
        if (!std::isfinite(seed.x) || !std::isfinite(seed.y)) continue;
        i32 nearest = -1;
        f32 radius = snapDistance * snapDistance;
        for (i32 p = 0; p < g.w * g.h; ++p) {
            const f32 distance = (g.center(p) - seed).length_sq();
            if (distance >= radius || !eligible(p)) continue;
            nearest = p;
            radius = distance;
        }
        if (nearest >= 0 && distances[nearest] != 0.f) {
            distances[nearest] = 0.f;
            queue.emplace(0.f, nearest);
        }
    }
    while (!queue.empty()) {
        const auto [distance, p] = queue.top();
        queue.pop();
        if (distance > distances[p]) continue;
        g.neighbors(p, [&](i32 q) {
            if (!eligible(q)) return;
            const f32 edge = color_dist(g.pixels[p], g.pixels[q]) / 64.f;
            const f32 next = distance + 1.f + edge;
            if (next >= distances[q]) return;
            distances[q] = next;
            queue.emplace(next, q);
        });
    }
    return distances;
}

inline void fill_enclosed(const Grid& g, std::vector<i32>& region) {
    if (region.empty()) return;
    std::vector<u8> exterior(g.labels.size());
    for (i32 p : region) exterior[p] = 1;
    auto accessible = [](i32) { return true; };
    for (i32 x = 0; x < g.w; ++x) {
        (void)component(g, x, exterior, accessible);
        (void)component(g, g.index(x, g.h - 1), exterior, accessible);
    }
    for (i32 y = 1; y + 1 < g.h; ++y) {
        (void)component(g, g.index(0, y), exterior, accessible);
        (void)component(g, g.index(g.w - 1, y), exterior, accessible);
    }
    for (i32 p = 0; p < g.w * g.h; ++p)
        if (!exterior[p] && g.pixels[p].opaque) region.push_back(p);
}

inline f32 segment_distance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const Vec2 o = p - a;
    const f32 t = std::clamp((d.x * o.x + d.y * o.y) / std::max(1e-6f, d.length_sq()), 0.f, 1.f);
    return (o - d * t).length();
}

inline std::vector<Vec2> convex_outline(std::vector<Vec2> points) {
    std::sort(points.begin(), points.end(), [](Vec2 a, Vec2 b) {
        return a.x == b.x ? a.y < b.y : a.x < b.x;
    });
    points.erase(std::unique(points.begin(), points.end(), [](Vec2 a, Vec2 b) {
        return a.x == b.x && a.y == b.y;
    }), points.end());
    if (points.size() < 3) return {};
    std::vector<Vec2> hull;
    auto append = [&](Vec2 p, usize limit) {
        while (hull.size() >= limit + 2) {
            const Vec2 a = hull[hull.size() - 2], b = hull.back();
            const Vec2 ab = b - a, bp = p - b;
            if (ab.x * bp.y - ab.y * bp.x > 0) break;
            hull.pop_back();
        }
        hull.push_back(p);
    };
    for (Vec2 p : points) append(p, 0);
    const usize lower = hull.size();
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it) append(*it, lower - 1);
    hull.pop_back();
    return hull.size() >= 3 ? hull : std::vector<Vec2>{};
}

inline void pad_silhouette(Grid& g) {
    const auto source = g.labels;
    for (i32 y = 0; y < g.h; ++y) for (i32 x = 0; x < g.w; ++x) {
        const i32 p = g.index(x, y);
        if (source[p] != Part::Body || !g.pixels[p].opaque) continue;
        bool edge = x == 0 || y == 0 || x + 1 == g.w || y + 1 == g.h;
        g.neighbors(p, [&](i32 q) { edge |= !g.pixels[q].opaque; });
        if (!edge) continue;
        // Take the outer ink and antialiased fringe along with the part.
        // Body pixels away from the silhouette keep their existing ownership.
        g.neighbors(p, [&](i32 q) {
            if (source[q] != Part::Body) g.labels[p] = source[q];
        });
    }
    const auto foreground = g.labels;
    for (i32 y = 0; y < g.h; ++y) for (i32 x = 0; x < g.w; ++x) {
        const i32 p = g.index(x, y);
        if (g.pixels[p].opaque) continue;
        i32 distance = 10;
        for (i32 dy = -2; dy <= 2; ++dy) for (i32 dx = -2; dx <= 2; ++dx) {
            const i32 qx = x + dx, qy = y + dy, d = dx * dx + dy * dy;
            if (qx < 0 || qy < 0 || qx >= g.w || qy >= g.h || d >= distance) continue;
            const auto selected = foreground[g.index(qx, qy)];
            if (selected == Part::Body) continue;
            distance = d;
            g.labels[p] = selected;
        }
    }
}

// Trace inside-oriented pixel edges. Shared boundaries follow the same grid
// on both sides, so restoring the rest pose reconstructs the source cutout.
inline std::vector<Contour> trace(const Grid& g, Part part, f32 sx, f32 sy) {
    const i32 row = g.w + 1;
    std::vector<u8> edges(static_cast<usize>(row) * (g.h + 1));
    auto inside = [&](i32 x, i32 y) {
        return x >= 0 && y >= 0 && x < g.w && y < g.h && g.labels[g.index(x, y)] == part;
    };
    for (i32 y = 0; y < g.h; ++y) for (i32 x = 0; x < g.w; ++x) {
        if (!inside(x, y)) continue;
        const i32 v = y * row + x;
        if (!inside(x, y - 1)) edges[v] |= 1u;
        if (!inside(x + 1, y)) edges[v + 1] |= 2u;
        if (!inside(x, y + 1)) edges[v + 1 + row] |= 4u;
        if (!inside(x - 1, y)) edges[v + row] |= 8u;
    }
    const std::array<i32, 4> offset{1, row, -1, -row};
    std::vector<Contour> out;
    for (i32 start = 0; start < static_cast<i32>(edges.size()); ++start) {
        while (edges[start]) {
            i32 vertex = start, dir = 0;
            while (!(edges[start] & (1u << dir))) ++dir;
            std::vector<Vec2> points;
            i32 previous = -1;
            for (usize step = 0; step <= edges.size() * 4; ++step) {
                if (dir != previous) points.push_back({static_cast<f32>(vertex % row), static_cast<f32>(vertex / row)});
                edges[vertex] &= static_cast<u8>(~(1u << dir));
                const i32 next = vertex + offset[dir];
                if (next == start) break;
                previous = dir;
                vertex = next;
                bool found = false;
                for (i32 turn : {1, 0, 3, 2}) {
                    const i32 candidate = (previous + turn) % 4;
                    if (edges[vertex] & (1u << candidate)) { dir = candidate; found = true; break; }
                }
                if (!found) { points.clear(); break; }
            }
            f32 twiceArea = 0;
            for (usize i = 0; i < points.size(); ++i) {
                const Vec2 a = points[i], b = points[(i + 1) % points.size()];
                twiceArea += a.x * b.y - a.y * b.x;
            }
            if (std::abs(twiceArea) < 12 || points.size() < 3) continue;
            // Remove short zigzags from the traced raster edge.
            std::vector<Vec2> simplified{points.front()};
            for (usize i = 1; i + 1 < points.size(); ++i) {
                if (segment_distance(points[i], simplified.back(), points[i + 1]) > .8f)
                    simplified.push_back(points[i]);
            }
            simplified.push_back(points.back());
            for (Vec2& p : simplified) { p.x *= sx; p.y *= sy; }
            out.push_back({std::move(simplified), twiceArea < 0});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Contour& a, const Contour& b) { return !a.hole && b.hole; });
    return out;
}

} // namespace detail

inline std::vector<Cut> cut(const u8* rgba, u32 width, u32 height, const f32* pose) {
    using namespace detail;
    if (!rgba || !pose || width < 8 || height < 8) return {};
    const f32 reduction = std::min(1.f, 240.f / std::max(width, height));
    const i32 w = std::max(1, static_cast<i32>(std::round(width * reduction)));
    const i32 h = std::max(1, static_cast<i32>(std::round(height * reduction)));
    Grid g{w, h, {}, std::vector<Part>(static_cast<usize>(w) * h, Part::Body)};
    const f32 sx = static_cast<f32>(width) / w, sy = static_cast<f32>(height) / h;
    g.pixels.reserve(g.labels.size());
    usize transparent = 0;
    for (i32 y = 0; y < h; ++y) for (i32 x = 0; x < w; ++x) {
        const auto* p = rgba + (static_cast<usize>((y + .5f) * sy) * width +
            static_cast<usize>((x + .5f) * sx)) * 4;
        g.pixels.push_back({static_cast<f32>(p[0]), static_cast<f32>(p[1]), static_cast<f32>(p[2]), p[3] >= 48});
        transparent += p[3] < 48;
    }
    const bool hasCutout = transparent >= g.pixels.size() / 25;
    auto joint = [&](u32 i, f32 threshold = .43f) -> Vec2 {
        const f32* p = pose + 2 + 4 * i;
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[3]) ||
            p[0] < 0 || p[0] >= 1 || p[1] < 0 || p[1] >= 1 || p[3] < threshold) return {NAN, NAN};
        const Vec2 point{p[0] * w, p[1] * h};
        return g.pixels[g.nearest(point)].opaque ? point : Vec2{NAN, NAN};
    };
    auto valid = [](Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); };
    const Vec2 nose = joint(0), eyeA = joint(2), eyeB = joint(5);
    std::array<Vec2, static_cast<usize>(Part::Count)> pivots{}, tips{};
    auto bind = [&](Part part, Vec2 pivot, Vec2 tip) {
        pivots[static_cast<usize>(part)] = pivot;
        tips[static_cast<usize>(part)] = tip;
    };
    auto mark = [&](Part p, const std::vector<i32>& pixels) {
        for (i32 i : pixels) if (g.labels[i] == Part::Body) g.labels[i] = p;
    };
    std::vector<Cut> lids;
    if (valid(nose) && valid(eyeA) && valid(eyeB)) {
        const Vec2 eyes = (eyeA + eyeB) * .5f;
        const f32 span = (eyeA - eyeB).length();
        if (span < 6) return {};
        // Use the eye line, oriented toward the nose, so tilted portraits
        // still search above the scalp and below the cheek respectively.
        const Vec2 across = (eyeA - eyeB) / span;
        Vec2 down{-across.y, across.x};
        if ((nose.x - eyes.x) * down.x + (nose.y - eyes.y) * down.y < 0) down = down * -1.f;
        auto vertical = [&](Vec2 p) { const Vec2 d = p - eyes; return d.x * down.x + d.y * down.y; };
        auto in_head = [&](Vec2 p) {
            return (p - eyes).length() < span * 2.45f &&
                   vertical(p) < vertical(nose) + span * 1.15f;
        };

        const std::array<Vec2, 5> samples{nose + down * (.24f * span), nose + down * (.45f * span),
            nose + (down * .19f + across * .34f) * span, nose + (down * .19f - across * .34f) * span,
            nose + down * (.65f * span)};
        std::array<Pixel, samples.size()> palette{};
        for (usize i = 0; i < samples.size(); ++i) palette[i] = g.pixels[g.nearest(samples[i])];
        const auto luminance = [](Pixel p) {
            return p.opaque && p.r - p.b > 12 && p.r > p.g - 5 ? p.r + p.g + p.b : 0.f;
        };
        const Pixel cheek = *std::max_element(palette.begin(), palette.end(), [&](Pixel a, Pixel b) {
            return luminance(a) < luminance(b);
        });
        auto matches_skin = [&](i32 p) {
            return std::any_of(palette.begin(), palette.end(), [&](Pixel s) {
                return luminance(s) > 0 && color_dist(s, cheek) < 60 && same_skin(g.pixels[p], s);
            });
        };
        std::vector<u8> skin(g.labels.size());
        for (i32 p = 0; p < w * h; ++p) skin[p] = matches_skin(p);
        auto is_skin = [&](i32 p) {
            if (skin[p]) return true;
            if (!g.pixels[p].opaque || g.pixels[p].r + g.pixels[p].g + g.pixels[p].b > 170) return false;
            i32 adjacent = 0;
            g.neighbors(p, [&](i32 q) { adjacent += skin[q]; });
            return adjacent >= 2;
        };
        const std::array wrists{joint(15, .35f), joint(16, .35f)};
        const std::array supportedWrists{joint(15, .48f), joint(16, .48f)};
        std::vector<f32> faceDistances, wristDistances;
        if (hasCutout && std::any_of(supportedWrists.begin(), supportedWrists.end(), valid)) {
            // Skin color alone fails where a hand overlaps the face. Match
            // connected pixels against both landmark sets and prefer paths
            // that stay within a region of similar color.
            faceDistances = skin_distances(g, samples, span * .22f, is_skin);
            wristDistances = skin_distances(g, supportedWrists, span * .42f, is_skin);
        }
        auto belongs_to_hand = [&](i32 p) {
            return !wristDistances.empty() &&
                   wristDistances[p] + span * .15f < faceDistances[p];
        };
        auto face_domain = [&](i32 p) {
            return in_head(g.center(p)) && vertical(g.center(p)) > -span * .85f && !belongs_to_hand(p);
        };
        std::vector<u8> visited(g.labels.size());
        auto within_face = [&](i32 p) { return face_domain(p) && is_skin(p); };
        std::vector<i32> face;
        for (Vec2 s : samples) {
            auto island = component(g, g.nearest(s), visited, within_face);
            if (island.size() > face.size()) face = std::move(island);
        }
        if (face.size() > span * span * .25f) {
            // Fill the interior of the skin contour so eyes and line work move
            // with the face. Use only short gaps to avoid bridging the silhouette.
            fill_enclosed(g, face);
            std::erase_if(face, [&](i32 p) { return !face_domain(p); });
            mark(Part::Face, face);
            for (i32 y = 0; y < h; ++y) {
                i32 last = -1;
                for (i32 x = 0; x < w; ++x) {
                    if (g.labels[g.index(x, y)] != Part::Face) continue;
                    if (last >= 0 && x - last < span * .95f) {
                        for (i32 k = last + 1; k < x; ++k) {
                            const i32 p = g.index(k, y);
                            if (g.pixels[p].opaque && face_domain(p)) g.labels[p] = Part::Face;
                        }
                    }
                    last = x;
                }
            }
            bind(Part::Face, nose + down * (.85f * span), eyes);

            for (const auto part : {Part::LeftLid, Part::RightLid}) {
                const Vec2 eye = part == Part::LeftLid ? eyeA : eyeB;
                auto eye_white = [&](i32 p) {
                    const Vec2 d = g.center(p) - eye;
                    const Pixel color = g.pixels[p];
                    const f32 lo = std::min({color.r, color.g, color.b}), hi = std::max({color.r, color.g, color.b});
                    return g.labels[p] == Part::Face && lo >= 155 && hi - lo < 35 &&
                        std::abs(d.x * across.x + d.y * across.y) < span * .43f &&
                        std::abs(d.x * down.x + d.y * down.y) < span * .28f;
                };
                std::fill(visited.begin(), visited.end(), 0);
                std::vector<i32> white;
                for (i32 p = 0; p < w * h; ++p) {
                    auto patch = component(g, p, visited, eye_white);
                    if (patch.size() > white.size()) white = std::move(patch);
                }
                if (white.size() < std::max(8.f, span * span * .034f)) continue;
                Vec2 center{};
                for (i32 p : white) center = center + g.center(p);
                center = center / static_cast<f32>(white.size());
                std::vector<Vec2> samples;
                for (i32 p = 0; p < w * h; ++p)
                    if (eye_white(p) && (g.center(p) - center).length() < span * .49f)
                        samples.push_back(g.center(p));
                samples.push_back(eye);
                auto contour = convex_outline(std::move(samples));
                if (contour.empty()) continue;
                // Include the pupil and thin sclera rims at the edge of the
                // segmentation. Move along the eye plane, never along X alone.
                for (Vec2& p : contour) {
                    const Vec2 delta = p - center;
                    const f32 n = delta.length();
                    if (n > .1f) p = p + delta * (.85f / n);
                    p = {p.x * sx, p.y * sy};
                }
                const Vec2 chin = nose + down * (.85f * span);
                Vec2 direction{down.x * sx, down.y * sy};
                direction = direction / direction.length();
                lids.push_back({part, {chin.x * sx, chin.y * sy}, {eyes.x * sx, eyes.y * sy}, {{std::move(contour), false}},
                    direction, {cheek.r / 255.f, cheek.g / 255.f, cheek.b / 255.f, 1.f}});
            }
        }

        // Estimate a separate hair region only if pixels near the crown form
        // a coherent color component distinct from the detected face.
        std::vector<i32> hair;
        for (f32 y : {.52f, .85f, 1.2f, 1.6f, 2.f}) for (f32 x : {-1.45f, -.80f, -.25f, .25f, .75f}) {
            const i32 seed = g.nearest(eyes + (across * x - down * y) * span);
            const Pixel color = g.pixels[seed];
            if (!color.opaque || is_skin(seed)) continue;
            std::fill(visited.begin(), visited.end(), 0);
            auto candidate = component(g, seed, visited, [&](i32 p) {
                const Vec2 pt = g.center(p);
                return g.labels[p] == Part::Body && g.pixels[p].opaque && vertical(pt) < span * .36f &&
                       (pt - eyes).length() < span * 3.65f && color_dist(g.pixels[p], color) <= 53;
            });
            if (candidate.size() > span * span * 10 || candidate.size() < span * span * .13f) continue;
            bool touches = false;
            for (i32 p : candidate) {
                g.neighbors(p, [&](i32 q) {
                    if (g.labels[q] == Part::Face) touches = true;
                    g.neighbors(q, [&](i32 r) { touches |= g.labels[r] == Part::Face; });
                });
                if (touches) break;
            }
            if (touches && candidate.size() > hair.size()) hair = std::move(candidate);
        }
        if (!hair.empty()) {
            fill_enclosed(g, hair);
            mark(Part::Hair, hair);
            Vec2 tip = eyes;
            for (i32 p : hair) if (vertical(g.center(p)) < vertical(tip)) tip = g.center(p);
            bind(Part::Hair, eyes - down * (.34f * span), tip);
        }

        if (hasCutout) {
            // Hands in stylized crops can be visible even when the detector
            // lacks a complete arm. Use a separated skin-color component
            // and prefer a measured wrist when one is available.
            std::fill(visited.begin(), visited.end(), 0);
            std::vector<std::vector<i32>> islands;
            const Vec2 shoulders[2]{joint(11, .4f), joint(12, .4f)};
            for (i32 p = 0; p < w * h; ++p) {
                auto island = component(g, p, visited, [&](i32 q) {
                    const Vec2 pt = g.center(q);
                    return g.labels[q] == Part::Body && is_skin(q) &&
                        (belongs_to_hand(q) || !in_head(pt) || (valid(wrists[0]) && (wrists[0] - pt).length() < span * 1.15f) ||
                                        (valid(wrists[1]) && (wrists[1] - pt).length() < span * 1.15f));
                });
                if (island.size() > span * span * .22f && island.size() < span * span * 8)
                    islands.push_back(std::move(island));
            }
            std::sort(islands.begin(), islands.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
            std::array<bool, 2> used{};
            for (auto& island : islands) {
                Vec2 center{};
                for (i32 p : island) center = center + g.center(p);
                center = center / static_cast<f32>(island.size());
                f32 nearest = span * 1.8f;
                i32 side = -1;
                for (i32 i = 0; i < 2; ++i) if (!used[i] && valid(wrists[i])) {
                    const f32 distance = (wrists[i] - center).length();
                    if (distance < nearest) { side = i; nearest = distance; }
                }
                if (side < 0) side = center.x < eyes.x ? 0 : 1;
                if (used[side]) continue;
                const bool aroundShoulder = nearest >= span * 1.8f &&
                    std::any_of(std::begin(shoulders), std::end(shoulders), [&](Vec2 shoulder) {
                        return valid(shoulder) && (shoulder - center).length() < span * 1.1f;
                    });
                const auto part = aroundShoulder ? (side == 0 ? Part::LeftArm : Part::RightArm)
                                                 : (side == 0 ? Part::LeftHand : Part::RightHand);
                fill_enclosed(g, island);
                mark(part, island);
                const auto wrist = valid(wrists[side]) && (wrists[side] - center).length() < span * 2
                    ? wrists[side] : center + down * (.38f * span);
                bind(part, wrist, center);
                used[side] = true;
            }
        }
    }

    if (hasCutout) {
        const auto limb = [&](Part part, u32 rootSlot, u32 midSlot, u32 tipSlot) {
            const Vec2 a = joint(rootSlot), b = joint(midSlot), c = joint(tipSlot);
            if (!valid(a) || !valid(b) || !valid(c)) return;
            const f32 len = (b - a).length();
            if (len < 5 || (c - b).length() < 4) return;
            const f32 radius = len * (part == Part::LeftLeg || part == Part::RightLeg ? .32f : .25f);
            const Vec2 start = a + (b - a) * .18f;
            std::vector<u8> seen(g.labels.size());
            auto region = component(g, g.nearest(b), seen, [&](i32 p) {
                const Vec2 pt = g.center(p);
                return g.labels[p] == Part::Body && g.pixels[p].opaque &&
                       std::min(segment_distance(pt, start, b), segment_distance(pt, b, c)) <= radius;
            });
            if (region.size() < len * radius) return;
            mark(part, region);
            bind(part, a, c);
        };
        limb(Part::LeftArm, 11, 13, 15);
        limb(Part::RightArm, 12, 14, 16);
        limb(Part::LeftLeg, 23, 25, 27);
        limb(Part::RightLeg, 24, 26, 28);
    }

    // Give both the add mask and the matching subtraction room outside the
    // alpha edge. That keeps the base layer's seam overlap from leaving
    // behind a bright or dark copy of the part's old outline.
    if (hasCutout) pad_silhouette(g);
    std::vector<Cut> cuts;
    for (usize i = 1; i < static_cast<usize>(Part::LeftLid); ++i) {
        const auto part = static_cast<Part>(i);
        auto masks = trace(g, part, sx, sy);
        if (masks.empty() || masks.front().hole || masks.size() > 12) continue;
        const Vec2 p = pivots[i], t = tips[i];
        cuts.push_back({part, {p.x * sx, p.y * sy}, {t.x * sx, t.y * sy}, std::move(masks)});
    }
    for (auto& lid : lids) cuts.push_back(std::move(lid));
    return cuts;
}

} // namespace aurea::autorig
