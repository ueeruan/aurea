// =============================================================================
//  Aurea / timeline / PathRig.cpp
//
//  Lote M (máscaras e rig do AE): RotoBezier, "Points Follow Nulls", "Nulls
//  Follow Points" e "Trace Path". Ver PathRig.hpp.
// =============================================================================
#include "aurea/timeline/PathRig.hpp"

#include "aurea/expr/Expression.hpp"      // live_timelines: a timeline em avaliação
#include "aurea/render/MaskRaster.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/LayerWorld.hpp"
#include "aurea/timeline/Timeline.hpp"
#include "aurea/vector/ContourTrace.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace aurea::pathrig {
namespace {

struct Owner {
    const Composition* comp = nullptr;
    const Layer*       layer = nullptr;
};

/// A camada (e a composição) que satisfaz `pred`, nas timelines que a thread
/// enxerga. Varredura simples: só roda para quem TEM ligação.
template <typename Pred>
Owner find_owner(Pred&& pred) noexcept {
    const Timeline* timelines[64];
    const u32 n = expr::live_timelines(timelines, 64);
    Owner found;
    for (u32 i = 0; i < n && !found.layer; ++i) {
        timelines[i]->for_each_composition([&](CompositionId, const Composition& c) {
            if (found.layer) return;
            c.layers().for_each([&](LayerId, const Layer& l) {
                if (!found.layer && pred(l)) found = Owner{&c, &l};
            });
        });
    }
    return found;
}

/// Quem está sendo resolvido nesta thread: reentrar no mesmo (o nulo segue o
/// ponto que segue o nulo) devolve "sem ligação" em vez de recursar.
constexpr u32 kMaxBusy = 24;
thread_local const void* tBusy[kMaxBusy];
thread_local u32 tBusyCount = 0;

struct Busy {
    bool ok = false;
    explicit Busy(const void* key) noexcept {
        if (tBusyCount >= kMaxBusy) return;
        for (u32 i = 0; i < tBusyCount; ++i) if (tBusy[i] == key) return;
        tBusy[tBusyCount++] = key;
        ok = true;
    }
    ~Busy() { if (ok) --tBusyCount; }
    Busy(const Busy&) = delete;
    Busy& operator=(const Busy&) = delete;
};

std::atomic<bool> gVectorDrivers{false};

f64 comp_frame(const Layer& l, f64 localFrame) noexcept {
    return localFrame + static_cast<f64>(l.start.value) - static_cast<f64>(l.offset.value);
}
f64 local_frame(const Layer& l, f64 compFrame) noexcept {
    return compFrame - static_cast<f64>(l.start.value) + static_cast<f64>(l.offset.value);
}

bool finite(Vec3 v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/// O ponto de âncora da camada na composição (mundo) no instante.
bool anchor_in_comp(const Composition& comp, const Layer& l, f64 compFrame, Vec3& out) noexcept {
    const f64 local = local_frame(l, compFrame);
    auto s = [&](TrackProperty p, f32 fallback) noexcept {
        const Track* tr = l.tracks.find(p);
        return tr ? world::sample_frac(*tr, local, fallback) : fallback;
    };
    const Vec3 anchor{s(TrackProperty::AnchorX, l.transform.anchor.x), s(TrackProperty::AnchorY, l.transform.anchor.y),
                      s(TrackProperty::AnchorZ, l.transform.anchor.z)};
    out = world::layer_world_frac(comp, l, compFrame).transform_point(anchor);
    return finite(out);
}

vector::Affine2 affine_of(const Mat4& m) noexcept {
    return vector::Affine2{m.col[0].x, m.col[0].y, m.col[1].x, m.col[1].y, m.col[3].x, m.col[3].y};
}

const Mask* mask_by_id(const Layer& l, u32 id) noexcept {
    for (const Mask& m : l.masks) if (m.id == id) return &m;
    return nullptr;
}

} // namespace

void roto_bezier(std::vector<MaskPoint>& pts, bool closed, const std::vector<f32>& tension) noexcept {
    const usize n = pts.size();
    if (n < 2) return;
    // As posições não mudam; as tangentes saem das posições (cópia: a conta de
    // um vértice não lê a tangente nova do vizinho).
    for (usize i = 0; i < n; ++i) {
        const bool hasPrev = closed || i > 0, hasNext = closed || i + 1 < n;
        const Vec2 p = pts[i].position;
        const Vec2 a = pts[(i + n - 1) % n].position, b = pts[(i + 1) % n].position;
        f32 k = i < tension.size() ? tension[i] : 1.0f;
        k = std::isfinite(k) ? std::clamp(k, 0.0f, 1.0f) : 1.0f;
        pts[i].inTangent = Vec2{0.0f, 0.0f};
        pts[i].outTangent = Vec2{0.0f, 0.0f};
        if (k <= 0.0f || (n == 2 && closed)) continue;
        Vec2 dir{0.0f, 0.0f};
        if (hasPrev && hasNext) dir = Vec2{b.x - a.x, b.y - a.y};
        else if (hasNext) dir = Vec2{b.x - p.x, b.y - p.y};
        else if (hasPrev) dir = Vec2{p.x - a.x, p.y - a.y};
        const f32 len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (!(len > 1e-6f)) continue;
        dir = Vec2{dir.x / len, dir.y / len};
        if (hasPrev) {
            const f32 d = std::sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y)) * (k / 3.0f);
            pts[i].inTangent = Vec2{-dir.x * d, -dir.y * d};
        }
        if (hasNext) {
            const f32 d = std::sqrt((b.x - p.x) * (b.x - p.x) + (b.y - p.y) * (b.y - p.y)) * (k / 3.0f);
            pts[i].outTangent = Vec2{dir.x * d, dir.y * d};
        }
    }
}

bool apply_mask_drivers(const Mask& mask, f64 localFrame, std::vector<MaskPoint>& points) noexcept {
    if (mask.drivers.empty() || points.empty() || !std::isfinite(localFrame)) return false;
    const Busy busy(&mask);
    if (!busy.ok) return false;
    const Owner o = find_owner([&](const Layer& l) {
        return !l.masks.empty() && &mask >= l.masks.data() && &mask < l.masks.data() + l.masks.size();
    });
    if (!o.layer) return false;
    const f64 compF = comp_frame(*o.layer, localFrame);
    Mat4 inv;
    if (!world::invert(world::layer_world_frac(*o.comp, *o.layer, compF), inv)) return false;
    bool changed = false;
    for (const PathVertexDriver& d : mask.drivers) {
        if (d.vertex >= points.size()) continue;
        const Layer* driver = o.comp->layer(d.driver);
        if (!driver || driver == o.layer) continue;
        Vec3 c;
        if (!anchor_in_comp(*o.comp, *driver, compF, c)) continue;
        const Vec3 p = inv.transform_point(c);
        if (!finite(p)) continue;
        points[d.vertex].position = Vec2{p.x, p.y};
        changed = true;
    }
    return changed;
}

void note_vector_drivers() noexcept { gVectorDrivers.store(true, std::memory_order_release); }

void apply_vector_drivers(const TrackSet& tracks, u32 groupIndex, f64 localFrame, VectorGroup& evaluated) noexcept {
    if (!gVectorDrivers.load(std::memory_order_acquire) || !std::isfinite(localFrame)) return;
    const Owner o = find_owner([&](const Layer& l) { return &l.tracks == &tracks; });
    if (!o.layer || o.layer->vectorDrivers.empty()) return;
    bool any = false;
    for (const VectorPathDriver& d : o.layer->vectorDrivers) any = any || d.group == groupIndex;
    if (!any) return;
    const Busy busy(&o.layer->vectorDrivers);
    if (!busy.ok) return;
    const f64 compF = comp_frame(*o.layer, localFrame);
    const vector::Affine2 inv =
        (affine_of(world::layer_world_frac(*o.comp, *o.layer, compF)) * vector::group_matrix(evaluated)).inverse();
    for (u32 pi = 0; pi < evaluated.paths.size(); ++pi) {
        bool mine = false;
        for (const VectorPathDriver& d : o.layer->vectorDrivers) mine = mine || (d.group == groupIndex && d.path == pi);
        if (!mine) continue;
        VectorPath& vp = evaluated.paths[pi];
        BezierPath b = vector::path_at(vp, localFrame);
        bool changed = false;
        for (const VectorPathDriver& d : o.layer->vectorDrivers) {
            if (d.group != groupIndex || d.path != pi || d.vertex >= b.v.size()) continue;
            const Layer* driver = o.comp->layer(d.driver);
            if (!driver || driver == o.layer) continue;
            Vec3 c;
            if (!anchor_in_comp(*o.comp, *driver, compF, c)) continue;
            const Vec2 p = inv.apply(Vec2{c.x, c.y});
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
            b.v[d.vertex].p = p;
            changed = true;
        }
        if (!changed) continue;
        // A cópia avaliada passa a ser um caminho livre parado na forma do instante.
        vp.kind = VectorPathKind::Free;
        vp.path = std::move(b);
        vp.keys.clear();
        vp.reversed = false;
    }
}

bool comp_path(const Composition& comp, const Layer& owner, u8 pathKind, u32 a, u32 b, f64 compFrame,
               BezierPath& out) noexcept {
    out.v.clear();
    const f64 local = local_frame(owner, compFrame);
    const Mat4 world = world::layer_world_frac(comp, owner, compFrame);
    if (pathKind == PathFollow::kMask) {
        const Mask* m = mask_by_id(owner, a);
        if (!m) return false;
        std::vector<MaskPoint> pts;
        mask::evaluate_path(*m, local, pts);
        out.closed = m->closed;
        out.v.reserve(pts.size());
        for (const MaskPoint& q : pts) {
            const Vec3 p = world.transform_point(Vec3{q.position.x, q.position.y, 0.0f});
            const Vec3 i = world.transform_point(Vec3{q.position.x + q.inTangent.x, q.position.y + q.inTangent.y, 0.0f});
            const Vec3 o = world.transform_point(Vec3{q.position.x + q.outTangent.x, q.position.y + q.outTangent.y, 0.0f});
            out.v.push_back(BezierVertex{Vec2{p.x, p.y}, Vec2{i.x - p.x, i.y - p.y}, Vec2{o.x - p.x, o.y - p.y}});
        }
        return !out.v.empty();
    }
    const VectorData& vd = owner.shape.vector;
    if (a >= vd.groups.size() || b >= vd.groups[a].paths.size()) return false;
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    const VectorGroup g = vector::evaluate_group(vd.groups[a], owner.tracks, a, local, fps);
    if (b >= g.paths.size()) return false;
    out = vector::transform_path(vector::path_at(g.paths[b], local), affine_of(world) * vector::group_matrix(g));
    return !out.v.empty();
}

bool point_on_path(const BezierPath& path, f64 fraction, Vec2& point, Vec2& tangent) noexcept {
    if (path.v.empty() || !std::isfinite(fraction)) return false;
    if (path.v.size() == 1) { point = path.v[0].p; tangent = Vec2{1.0f, 0.0f}; return true; }
    vector::Contour c;
    vector::flatten(path, 0.1f, c);
    const f64 length = vector::length_of(c);
    if (!(length > 1e-9)) { point = path.v[0].p; tangent = Vec2{1.0f, 0.0f}; return true; }
    f64 f = std::clamp(fraction, 0.0, 1.0);
    // Caminho fechado: 100 % volta ao começo; o ponto logo antes mantém a tangente.
    if (c.closed && f >= 1.0) f = 1.0 - 1e-9;
    return vector::sample_at(c, f * length, point, tangent);
}

bool follow_position(const Layer& layer, f64 localFrame, Vec3& position, f32* tangentRad) noexcept {
    const PathFollow& f = layer.pathFollow;
    if (f.mode == PathFollow::kNone || !std::isfinite(localFrame)) return false;
    const Busy busy(&layer.pathFollow);
    if (!busy.ok) return false;
    const Owner o = find_owner([&](const Layer& l) { return &l == &layer; });
    if (!o.comp) return false;
    const Layer* source = o.comp->layer(f.source);
    if (!source || source == &layer) return false;
    const f64 compF = comp_frame(layer, localFrame);
    BezierPath path;
    if (!comp_path(*o.comp, *source, f.pathKind, f.a, f.b, compF, path) || path.v.empty()) return false;
    Vec2 pt{0.0f, 0.0f}, tan{1.0f, 0.0f};
    if (f.mode == PathFollow::kVertex) {
        if (f.vertex >= path.v.size()) return false;
        pt = path.v[f.vertex].p;
    } else {
        f64 progress = static_cast<f64>(f.progress);
        if (f.progressEffect != kInvalidIndex) {
            for (const EffectInstance& e : layer.effects) {
                if (e.id != f.progressEffect || e.params.empty()) continue;
                progress = static_cast<f64>(e.params[0].constant.v[0]);
                const Track* tr = layer.tracks.find(TrackProperty::EffectParam, e.id, param_track_key(0, 0));
                if (tr && tr->driven()) {
                    f64 at = localFrame;
                    // Laço: depois do último key o trecho dos keys recomeça (o
                    // loopOut("cycle") que o script do AE põe no Progresso).
                    if (f.loop && tr->keys.size() >= 2 && !tr->has_expression()) {
                        const f64 first = static_cast<f64>(tr->keys.front().time.value);
                        const f64 last = static_cast<f64>(tr->keys.back().time.value);
                        if (last > first && at > last) at = first + std::fmod(at - first, last - first);
                    }
                    progress = static_cast<f64>(tr->value_or_f(at, e.params[0].constant.v[0]));
                }
                break;
            }
        }
        if (!std::isfinite(progress)) progress = 0.0;
        f64 fraction = progress / 100.0;
        if (f.loop) {
            // 100 % exato fica no fim; além disso recomeça.
            if (fraction > 1.0 || fraction < 0.0) fraction -= std::floor(fraction);
        } else {
            fraction = std::clamp(fraction, 0.0, 1.0);
        }
        if (!point_on_path(path, fraction, pt, tan)) return false;
    }
    Vec3 p{pt.x, pt.y, 0.0f};
    bool parented = false;
    if (layer.parent.valid()) {
        if (const Layer* parent = o.comp->layer(layer.parent); parent && parent != &layer) {
            Mat4 inv;
            if (world::invert(world::layer_world_frac(*o.comp, *parent, compF), inv)) {
                p = inv.transform_point(p);
                parented = true;
            }
        }
    }
    if (!finite(p)) return false;
    position.x = p.x;
    position.y = p.y;
    if (parented) position.z = p.z;
    if (tangentRad && f.mode == PathFollow::kTrace && f.orient) *tangentRad = std::atan2(tan.y, tan.x);
    return true;
}

} // namespace aurea::pathrig

// =============================================================================
//  Auto-trace: canal → contornos bezier (marching squares + ajuste em bézier).
// =============================================================================
namespace aurea::masktrace {
namespace {

/// Par-ímpar: `p` está dentro da volta?
bool inside_loop(const vector::Loop& loop, Vec2 p) noexcept {
    bool in = false;
    for (usize i = 0, n = loop.size(), j = n - 1; i < n; j = i++) {
        const Vec2 a = loop[i], b = loop[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

} // namespace

bool trace(const u8* rgba, u32 w, u32 h, const Options& o, Vec2 scale, u32 maxContours, std::vector<Traced>& out) {
    out.clear();
    if (!rgba || w < 2 || h < 2 || maxContours == 0) return false;
    if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !(scale.x > 0.0f) || !(scale.y > 0.0f)) scale = Vec2{1.0f, 1.0f};
    const usize count = static_cast<usize>(w) * h;
    std::vector<f32> value(count);
    for (usize i = 0; i < count; ++i) {
        const u8* px = rgba + i * 4;
        f32 v = 0.0f;
        switch (o.channel) {
            case 1: v = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2]; break;
            case 2: v = px[0]; break;
            case 3: v = px[1]; break;
            case 4: v = px[2]; break;
            default: v = px[3]; break;
        }
        value[i] = v / 255.0f;
    }
    // Desfoque de caixa antes de traçar (duas passadas): tira o serrilhado do
    // canal, que viraria degraus no contorno.
    // Arredondar cantos (Corner Roundness): o canal é suavizado antes do limiar,
    // até 6 px da imagem traçada em 100 %. Um canto vira curva com raio dessa
    // ordem; uma borda reta fica no mesmo lugar (o meio da rampa não anda).
    const f32 roundness = std::isfinite(o.cornerRoundness) ? std::clamp(o.cornerRoundness, 0.0f, 100.0f) : 50.0f;
    const f32 base = std::isfinite(o.blur) ? std::max(0.0f, o.blur) : 0.0f;
    const int radius = std::clamp(static_cast<int>(std::lround(std::max(base, roundness * 0.06f))), 0, 32);
    if (radius > 0) {
        std::vector<f32> scratch(count);
        const auto box = [&](bool horizontal) {
            const int len = horizontal ? static_cast<int>(w) : static_cast<int>(h), lines = horizontal ? static_cast<int>(h) : static_cast<int>(w);
            const auto index = [&](int line, int k) {
                return horizontal ? static_cast<usize>(line) * w + static_cast<usize>(k) : static_cast<usize>(k) * w + static_cast<usize>(line);
            };
            for (int line = 0; line < lines; ++line) {
                f32 sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) sum += value[index(line, std::clamp(k, 0, len - 1))];
                for (int k = 0; k < len; ++k) {
                    scratch[index(line, k)] = sum / static_cast<f32>(2 * radius + 1);
                    sum += value[index(line, std::min(k + radius + 1, len - 1))] - value[index(line, std::max(k - radius, 0))];
                }
            }
            value.swap(scratch);
        };
        for (int pass = 0; pass < 2; ++pass) { box(true); box(false); }
    }
    const f32 threshold = std::isfinite(o.threshold) ? std::clamp(o.threshold, 0.002f, 0.998f) : 0.5f;
    const u32 side = std::max(w, h);
    const f32 cs = std::max(1.0f, static_cast<f32>(side) / 1400.0f);
    const f32 cx = static_cast<f32>(w) * 0.5f, cy = static_cast<f32>(h) * 0.5f;
    const bool invert = o.invert;
    const auto sd = [&](Vec2 q) {
        const f32 x = q.x + cx - 0.5f, y = q.y + cy - 0.5f;   // centro do texel
        // Fora da imagem é fora da forma: o contorno fecha na moldura.
        if (x < 0.0f || y < 0.0f || x > static_cast<f32>(w - 1) || y > static_cast<f32>(h - 1)) return 1.0f;
        const u32 x0 = std::min(static_cast<u32>(x), w - 1), y0 = std::min(static_cast<u32>(y), h - 1);
        const u32 x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const f32 fx = x - static_cast<f32>(x0), fy = y - static_cast<f32>(y0);
        const auto at = [&](u32 i, u32 j) { return value[static_cast<usize>(j) * w + i]; };
        const f32 a = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx, b = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
        const f32 v = a + (b - a) * fy;
        return invert ? v - threshold : threshold - v;
    };
    std::vector<vector::Loop> loops = vector::trace_zero(sd, Vec2{cx + 3.0f * cs, cy + 3.0f * cs}, cs);
    if (loops.empty()) return false;
    const f32 pixelArea = scale.x * scale.y;
    const f32 minArea = std::max(cs * cs * 2.0f, (std::isfinite(o.minArea) ? std::max(0.0f, o.minArea) : 0.0f) / pixelArea);
    loops.erase(std::remove_if(loops.begin(), loops.end(),
                               [&](const vector::Loop& l) { return std::fabs(vector::loop_area(l)) < minArea; }),
                loops.end());
    if (loops.empty()) return false;
    std::sort(loops.begin(), loops.end(), [](const vector::Loop& l, const vector::Loop& r) {
        return std::fabs(vector::loop_area(l)) > std::fabs(vector::loop_area(r));
    });
    // Furo = dentro de um número ímpar de voltas maiores.
    std::vector<u8> hole(loops.size(), 0);
    for (usize i = 1; i < loops.size(); ++i) {
        u32 depth = 0;
        for (usize j = 0; j < i; ++j) if (inside_loop(loops[j], loops[i].front())) ++depth;
        hole[i] = static_cast<u8>(depth & 1u);
    }
    if (loops.size() > maxContours) loops.resize(maxContours);
    const f32 mean = std::sqrt(pixelArea);
    const f32 error = std::max(0.2f, (std::isfinite(o.tolerance) ? std::max(0.0f, o.tolerance) : 1.0f) / mean);
    for (usize i = 0; i < loops.size(); ++i) {
        BezierPath b = vector::fit_loop(loops[i], cs * 3.0f, error);
        if (b.v.size() < 3) {
            // O ajuste desistiu (volta minúscula): o polígono cru ainda é o contorno.
            b.v.clear();
            const usize step = std::max<usize>(1, loops[i].size() / 64);
            for (usize k = 0; k < loops[i].size(); k += step) b.v.push_back(BezierVertex{loops[i][k], Vec2{0, 0}, Vec2{0, 0}});
            if (b.v.size() < 3) continue;
        }
        Traced t;
        t.hole = hole[i] != 0;
        t.area = std::fabs(vector::loop_area(loops[i])) * pixelArea;
        t.points.reserve(b.v.size());
        bool ok = true;
        for (const BezierVertex& v : b.v) {
            MaskPoint m;
            m.position = Vec2{(v.p.x + cx) * scale.x, (v.p.y + cy) * scale.y};
            m.inTangent = Vec2{v.in.x * scale.x, v.in.y * scale.y};
            m.outTangent = Vec2{v.out.x * scale.x, v.out.y * scale.y};
            ok = ok && std::isfinite(m.position.x) && std::isfinite(m.position.y) && std::isfinite(m.inTangent.x)
                 && std::isfinite(m.inTangent.y) && std::isfinite(m.outTangent.x) && std::isfinite(m.outTangent.y);
            t.points.push_back(m);
        }
        if (ok) out.push_back(std::move(t));
    }
    return !out.empty();
}

} // namespace aurea::masktrace
