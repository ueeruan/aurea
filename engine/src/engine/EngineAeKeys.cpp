// =============================================================================
//  Aurea / engine / EngineAeKeys.cpp
//
//  Lote K do gap_system (itens 16, 17 e 19): os assistentes de keyframe do
//  After Effects. O AE é só a especificação de comportamento; Android e iOS só
//  chamam, a regra mora aqui. Cada comando calcula em cópias e só abre o passo
//  de desfazer se algo muda.
//
//  - Inverter keyframes no tempo, escala exponencial, suavizador.
//  - Converter expressão em keyframes.
//  - Bézier automática / contínua e vagante (bits em Keyframe::flags).
//  - Motion Sketch: amostras do dedo viram keys de posição.
//
//  As contas sem motor moram em animation/KeyframeAssist.cpp; a resolução das
//  alças automáticas, em animation/Curve.cpp (Track::resolve_auto_handles).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/animation/KeyframeAssist.hpp"
#include "aurea/animation/KeyframeOptimize.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/SpatialPath.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace aurea {

namespace {

constexpr u32 kMaxRefs = 16384;
constexpr i64 kMaxBakeFrames = 200000;

bool ref_ok(const i64* ref) noexcept {
    return ref[0] >= 0 && ref[0] <= 65535 && ref[1] >= -1 && ref[1] <= static_cast<i64>(UINT32_MAX)
        && ref[2] >= 0 && ref[2] <= static_cast<i64>(UINT32_MAX) && ref[3] >= INT32_MIN && ref[3] <= INT32_MAX;
}

/// A trilha escalar da referência (a mesma regra do Easy Ease). Caminhos
/// (máscara, forma, malha) não têm valor escalar: nulo.
Track* ref_track(Layer& layer, const i64* ref) noexcept {
    const auto property = static_cast<TrackProperty>(ref[0]);
    if (property == TrackProperty::MeshWarp || property == TrackProperty::VectorPath || property == TrackProperty::MaskPath)
        return nullptr;
    if (property == TrackProperty::TimeRemap) return &layer.timeRemap;
    if (property == TrackProperty::Speed) return layer.tracks.find(property);
    return layer.tracks.find(property, static_cast<u32>(ref[1]), static_cast<u32>(ref[2]));
}

/// Keys escolhidos agrupados por trilha (índices em ordem).
struct Picked { Track* track = nullptr; std::vector<u32> keys; };

bool collect(Layer& layer, const i64* refs, u32 count, std::vector<Picked>& out) {
    if (!refs || count == 0 || count > kMaxRefs) return false;
    for (u32 i = 0; i < count; ++i) {
        const i64* ref = refs + i * 4;
        if (!ref_ok(ref)) return false;
        Track* track = ref_track(layer, ref);
        if (!track) continue;
        const u32 key = track->find_exact(FrameIndex{ref[3]});
        if (key == kInvalidIndex) continue;
        auto it = std::find_if(out.begin(), out.end(), [track](const Picked& p) { return p.track == track; });
        if (it == out.end()) { out.push_back({track, {}}); it = out.end() - 1; }
        if (std::find(it->keys.begin(), it->keys.end(), key) == it->keys.end()) it->keys.push_back(key);
    }
    for (Picked& p : out) std::sort(p.keys.begin(), p.keys.end());
    return !out.empty();
}

bool same_keys(const Track& a, const Track& b) noexcept {
    if (a.keys.size() != b.keys.size()) return false;
    for (usize i = 0; i < a.keys.size(); ++i) {
        const Keyframe& x = a.keys[i];
        const Keyframe& y = b.keys[i];
        if (x.time != y.time || x.value != y.value || x.interp != y.interp || x.bx1 != y.bx1 || x.by1 != y.by1
            || x.bx2 != y.bx2 || x.by2 != y.by2 || x.flags != y.flags || x.easePower != y.easePower) return false;
    }
    return true;
}

bool is_position(TrackProperty p) noexcept {
    return p == TrackProperty::PositionX || p == TrackProperty::PositionY || p == TrackProperty::PositionZ;
}

bool is_scale(TrackProperty p) noexcept {
    return p == TrackProperty::ScaleX || p == TrackProperty::ScaleY || p == TrackProperty::ScaleZ;
}

/// Comprimento do caminho de posição entre os quadros `a` e `b` (keys
/// vizinhos): a bézier espacial quando há tangentes, senão a reta.
f64 spatial_length(const Layer& l, Vec3 p0, Vec3 p1, i64 a, i64 b) noexcept {
    const SpatialTangent* ta = find_spatial_tangent(l.positionTangents, a);
    const SpatialTangent* tb = find_spatial_tangent(l.positionTangents, b);
    const Vec3 out0 = ta ? ta->out : Vec3{0, 0, 0};
    const Vec3 in1 = tb ? tb->in : Vec3{0, 0, 0};
    if (out0.x == 0.0f && out0.y == 0.0f && out0.z == 0.0f && in1.x == 0.0f && in1.y == 0.0f && in1.z == 0.0f)
        return static_cast<f64>((p1 - p0).length());
    const Vec3 c0 = p0 + out0, c1 = p1 + in1;
    f64 len = 0.0;
    Vec3 prev = p0;
    for (int i = 1; i <= 32; ++i) {
        const Vec3 q = spatial_bezier(p0, c0, c1, p1, static_cast<f32>(i) / 32.0f);
        len += static_cast<f64>((q - prev).length());
        prev = q;
    }
    return len;
}

/// Vagante (AE "Rove Across Time"): os keys de posição do meio marcados com
/// kKeyRoving ganham o tempo proporcional ao comprimento do caminho entre os
/// keys fixos vizinhos — velocidade constante no trecho. Os trechos que saem
/// de um key vagante ficam lineares. Devolve se algum tempo mudou.
bool rove_position(Layer& l) noexcept {
    Track* axes[3] = {l.tracks.find(TrackProperty::PositionX), l.tracks.find(TrackProperty::PositionY),
                      l.tracks.find(TrackProperty::PositionZ)};
    std::vector<i64> times;
    for (Track* t : axes) if (t) for (const Keyframe& k : t->keys) times.push_back(k.time.value);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    if (times.size() < 3) return false;
    const auto roving_at = [&](i64 time) {
        bool any = false;
        for (Track* t : axes) {
            if (!t) continue;
            const u32 k = t->find_exact(FrameIndex{time});
            if (k == kInvalidIndex) continue;
            if (!(t->keys[k].flags & kKeyRoving)) return false;
            any = true;
        }
        return any;
    };
    const auto point_at = [&](i64 time) {
        Vec3 p{0, 0, 0};
        if (axes[0]) p.x = axes[0]->sample_keys(FrameIndex{time});
        if (axes[1]) p.y = axes[1]->sample_keys(FrameIndex{time});
        if (axes[2]) p.z = axes[2]->sample_keys(FrameIndex{time});
        return p;
    };
    std::vector<std::pair<i64, i64>> moves;   // quadro antigo → novo
    usize a = 0;
    while (a + 1 < times.size()) {
        usize b = a + 1;
        while (b + 1 < times.size() && roving_at(times[b])) ++b;
        if (b > a + 1) {
            // Comprimento acumulado do key fixo `a` até o fixo `b`.
            std::vector<f64> acc{0.0};
            for (usize i = a + 1; i <= b; ++i)
                acc.push_back(acc.back() + spatial_length(l, point_at(times[i - 1]), point_at(times[i]), times[i - 1], times[i]));
            const i64 t0 = times[a], t1 = times[b];
            const usize inner = b - a - 1;
            if (t1 - t0 >= static_cast<i64>(inner) + 1) {
                const f64 total = acc.back();
                i64 previous = t0;
                for (usize i = 1; i <= inner; ++i) {
                    const f64 share = total > 1e-6 ? acc[i] / total : static_cast<f64>(i) / static_cast<f64>(inner + 1);
                    i64 target = t0 + static_cast<i64>(std::llround(share * static_cast<f64>(t1 - t0)));
                    // Ordem mantida e sem dois keys no mesmo quadro.
                    target = std::clamp(target, previous + 1, t1 - static_cast<i64>(inner - i) - 1);
                    if (target != times[a + i]) moves.emplace_back(times[a + i], target);
                    previous = target;
                }
            }
        }
        a = b;
    }
    bool changed = !moves.empty();
    const auto mapped = [&](i64 time) {
        for (const auto& m : moves) if (m.first == time) return m.second;
        return time;
    };
    for (Track* t : axes) {
        if (!t) continue;
        Track next = *t;
        for (Keyframe& k : next.keys) {
            k.time = FrameIndex{mapped(k.time.value)};
            if ((k.flags & kKeyRoving) && k.interp != Interpolation::Linear && k.interp != Interpolation::Hold) {
                k.interp = Interpolation::Linear;
                k.easePower = 1;
                k.easingPreset = 0;
                changed = true;
            }
        }
        std::stable_sort(next.keys.begin(), next.keys.end(), [](const Keyframe& x, const Keyframe& y) { return x.time < y.time; });
        next.lastIndex = 0;
        next.resolve_auto_handles();
        t->keys = std::move(next.keys);
        t->lastIndex = 0;
    }
    for (SpatialTangent& st : l.positionTangents) st.time = mapped(st.time);
    return changed;
}

} // namespace

// =============================================================================
// Inverter no tempo, escala exponencial e suavizador
// =============================================================================
u32 Engine::time_reverse_keyframe_selection(u64 layerId, const i64* refs, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<Picked> picked;
    if (!layer || layer->locked || !collect(*layer, refs, count, picked)) return 0;
    std::vector<std::pair<Track*, Track>> changed;
    for (const Picked& p : picked) {
        if (p.keys.size() < 2) continue;
        Track next = *p.track;
        // Um key cairia em cima de um não escolhido: a seleção inteira recusa.
        if (!animation::time_reverse(next, p.keys)) return 0;
        if (!same_keys(next, *p.track)) changed.emplace_back(p.track, std::move(next));
    }
    if (changed.empty()) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "inverter keyframes no tempo");
    for (auto& c : changed) { c.first->keys = std::move(c.second.keys); c.first->lastIndex = 0; }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(changed.size());
}

u32 Engine::exponential_scale_keyframe_selection(u64 layerId, const i64* refs, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<Picked> picked;
    if (!layer || layer->locked || !collect(*layer, refs, count, picked)) return 0;
    std::vector<std::pair<Track*, Track>> changed;
    for (const Picked& p : picked) {
        if (p.keys.size() < 2 || !is_scale(p.track->property) || p.track->has_expression()) continue;
        Track next = *p.track;
        if (animation::exponential_scale(next, p.keys.front(), p.keys.back()) && !same_keys(next, *p.track))
            changed.emplace_back(p.track, std::move(next));
    }
    if (changed.empty()) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "escala exponencial");
    for (auto& c : changed) { c.first->keys = std::move(c.second.keys); c.first->lastIndex = 0; }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(changed.size());
}

u32 Engine::smooth_keyframe_selection(u64 layerId, const i64* refs, u32 count, f32 tolerance) noexcept {
    if (!std::isfinite(tolerance) || tolerance < 0.0f) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<Picked> picked;
    if (!layer || layer->locked || !collect(*layer, refs, count, picked)) return 0;
    std::vector<std::pair<Track*, Track>> changed;
    for (const Picked& p : picked) {
        if (p.keys.size() < 3) continue;   // como no AE: três keys ou mais
        Track next = *p.track;
        // Tolerância relativa (como em optimize_keyframes): fração da amplitude da trilha.
        const f32 tol = std::max(1e-5f, tolerance * animation::value_range(*p.track));
        if (animation::smooth(next, p.keys.front(), p.keys.back(), tol) > 0 && !same_keys(next, *p.track))
            changed.emplace_back(p.track, std::move(next));
    }
    if (changed.empty()) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "suavizar keyframes");
    for (auto& c : changed) { c.first->keys = std::move(c.second.keys); c.first->lastIndex = 0; }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(changed.size());
}

// =============================================================================
// Converter expressão em keyframes
// =============================================================================
u32 Engine::bake_expression_keyframes(u64 layerId, const i64* refs, u32 count) noexcept {
    if (count > kMaxRefs || (count > 0 && !refs)) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked) return 0;
    std::vector<Track*> targets;
    if (count == 0) {
        for (u32 i = 0; i < layer->tracks.size(); ++i)
            if (layer->tracks.at(i).has_expression()) targets.push_back(&layer->tracks.at(i));
        if (layer->timeRemap.has_expression()) targets.push_back(&layer->timeRemap);
    } else {
        for (u32 i = 0; i < count; ++i) {
            const i64* ref = refs + i * 4;
            if (!ref_ok(ref)) return 0;
            Track* t = ref_track(*layer, ref);
            if (t && t->has_expression() && std::find(targets.begin(), targets.end(), t) == targets.end()) targets.push_back(t);
        }
    }
    // O trecho que a camada mostra (entrada/saída), dentro da composição.
    const i64 from = std::max<i64>(layer->start.value, 0);
    const i64 to = std::min<i64>(layer->end.value, comp->duration().value);
    if (targets.empty() || to <= from || to - from > kMaxBakeFrames) return 0;
    const i64 localFirst = layer->local_time(FrameIndex{from}).value;
    std::vector<std::pair<Track*, Track>> changed;
    {
        // Mesmo contexto do renderer: a expressão acha a camada dona, as outras
        // camadas e a taxa da composição.
        const expr::Scope scope(project_->timeline());
        for (Track* t : targets) {
            const f32 fallback = expr::static_value(*layer, *t);
            std::vector<f32> values(static_cast<usize>(to - from));
            for (i64 f = from; f < to; ++f)
                values[static_cast<usize>(f - from)] = t->value_or(FrameIndex{localFirst + (f - from)}, fallback);
            Track next = *t;
            next.keys.clear();
            animation::write_samples(next, localFirst, values);
            changed.emplace_back(t, std::move(next));
        }
    }
    history_.before_mutation(*comp, project_->timeline().current(), "converter expressao em keyframes");
    for (auto& c : changed) {
        c.first->keys = std::move(c.second.keys);
        c.first->lastIndex = 0;
        // A expressão fica guardada, desligada (como no AE).
        c.first->expressionEnabled = false;
    }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(changed.size());
}

// =============================================================================
// Bézier automática / contínua e vagante
// =============================================================================
u32 Engine::set_keyframe_temporal_mode(u64 layerId, const i64* refs, u32 count, u32 mode) noexcept {
    if (mode > kKeyModeRoveOff) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<Picked> picked;
    if (!layer || layer->locked || !collect(*layer, refs, count, picked)) return 0;
    u32 touched = 0;
    if (mode == kKeyModeRoveOn || mode == kKeyModeRoveOff) {
        // Vagante: só posição, e só os keys do meio (o primeiro e o último ficam
        // fixos). Vale para os três eixos no mesmo quadro.
        std::vector<i64> times;
        for (const Picked& p : picked)
            if (is_position(p.track->property))
                for (u32 k : p.keys) times.push_back(p.track->keys[k].time.value);
        if (times.empty()) return 0;
        // Tudo numa cópia da camada; o passo de desfazer só abre se algo mudar.
        Layer work = *layer;
        Track* axes[3] = {work.tracks.find(TrackProperty::PositionX), work.tracks.find(TrackProperty::PositionY),
                          work.tracks.find(TrackProperty::PositionZ)};
        i64 lo = INT64_MAX, hi = INT64_MIN;
        for (Track* t : axes) if (t && !t->keys.empty()) { lo = std::min(lo, t->keys.front().time.value); hi = std::max(hi, t->keys.back().time.value); }
        for (Track* t : axes) {
            if (!t) continue;
            for (Keyframe& k : t->keys) {
                if (std::find(times.begin(), times.end(), k.time.value) == times.end()) continue;
                const bool inner = k.time.value > lo && k.time.value < hi;
                const u8 next = mode == kKeyModeRoveOn && inner ? static_cast<u8>(k.flags | kKeyRoving)
                                                                : static_cast<u8>(k.flags & ~kKeyRoving);
                if (next != k.flags) { k.flags = next; ++touched; }
            }
        }
        const bool moved = mode == kKeyModeRoveOn && rove_position(work);
        if (touched == 0 && !moved) return 0;
        history_.before_mutation(*comp, project_->timeline().current(),
                                 mode == kKeyModeRoveOn ? "keyframes vagantes" : "keyframes fixos no tempo");
        for (const TrackProperty p : {TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ}) {
            const Track* src = work.tracks.find(p);
            Track* dst = layer->tracks.find(p);
            if (src && dst) { dst->keys = src->keys; dst->lastIndex = 0; }
        }
        layer->positionTangents = std::move(work.positionTangents);
    } else {
        std::vector<std::pair<Track*, Track>> changed;
        for (const Picked& p : picked) {
            Track next = *p.track;
            for (u32 k : p.keys) {
                Keyframe& key = next.keys[k];
                u8 f = static_cast<u8>(key.flags & ~(kKeyAutoBezier | kKeyContinuous));
                if (mode == kKeyModeAuto) f |= kKeyAutoBezier;
                else if (mode == kKeyModeContinuous) f |= kKeyContinuous;
                if (f != key.flags) { key.flags = f; ++touched; }
            }
            next.resolve_auto_handles();
            if (!same_keys(next, *p.track)) changed.emplace_back(p.track, std::move(next));
        }
        if (changed.empty()) return 0;
        history_.before_mutation(*comp, project_->timeline().current(),
                                 mode == kKeyModeAuto ? "bezier automatica" : mode == kKeyModeContinuous ? "bezier continua" : "bezier");
        for (auto& c : changed) { c.first->keys = std::move(c.second.keys); c.first->lastIndex = 0; }
        if (touched == 0) touched = static_cast<u32>(changed.size());
    }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return touched;
}

u32 Engine::query_keyframe_flags(u64 layerId, const i64* refs, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    InlineEditScope inlineScope(*this, layerId);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    std::vector<Picked> picked;
    if (!layer || !collect(*layer, refs, count, picked)) return 0;
    u32 all = 0xFFu, any = 0;
    for (const Picked& p : picked)
        for (u32 k : p.keys) { all &= p.track->keys[k].flags; any |= p.track->keys[k].flags; }
    return (all & 0xFFu) | ((any & 0xFFu) << 8);
}

// =============================================================================
// Motion Sketch
// =============================================================================
u32 Engine::set_position_keys(u64 layerId, const f64* samples, u32 count, f64 captureSpeed, f32 smoothing) noexcept {
    if (!samples || count < 2 || count > (1u << 20) || !std::isfinite(captureSpeed) || captureSpeed <= 0.0
        || captureSpeed > 100.0 || !std::isfinite(smoothing) || smoothing < 0.0f) return 0;
    struct Sample { f64 t, x, y; };
    std::vector<Sample> s;
    s.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const Sample v{samples[i * 3], samples[i * 3 + 1], samples[i * 3 + 2]};
        if (!std::isfinite(v.t) || !std::isfinite(v.x) || !std::isfinite(v.y) || std::fabs(v.t) > 1e9) continue;
        s.push_back(v);
    }
    // Ordem do tempo; o último registro de um mesmo instante vence.
    std::stable_sort(s.begin(), s.end(), [](const Sample& a, const Sample& b) { return a.t < b.t; });
    std::vector<Sample> clean;
    for (const Sample& v : s) {
        if (!clean.empty() && std::fabs(clean.back().t - v.t) < 1e-9) clean.back() = v;
        else clean.push_back(v);
    }
    if (clean.size() < 2) return 0;
    // Velocidade da captura: o tempo gravado estica a partir da 1ª amostra.
    const f64 origin = clean.front().t;
    for (Sample& v : clean) v.t = origin + (v.t - origin) * captureSpeed;
    const i64 first = static_cast<i64>(std::ceil(clean.front().t - 1e-6));
    const i64 last = static_cast<i64>(std::floor(clean.back().t + 1e-6));
    if (last <= first || last - first > kMaxBakeFrames) return 0;
    std::vector<f32> xs, ys;
    xs.reserve(static_cast<usize>(last - first + 1));
    ys.reserve(static_cast<usize>(last - first + 1));
    usize j = 0;
    for (i64 f = first; f <= last; ++f) {
        const f64 t = static_cast<f64>(f);
        while (j + 2 < clean.size() && clean[j + 1].t < t) ++j;
        const Sample& a = clean[j];
        const Sample& b = clean[std::min(j + 1, clean.size() - 1)];
        const f64 span = b.t - a.t;
        const f64 u = span > 1e-9 ? std::clamp((t - a.t) / span, 0.0, 1.0) : 0.0;
        xs.push_back(static_cast<f32>(a.x + (b.x - a.x) * u));
        ys.push_back(static_cast<f32>(a.y + (b.y - a.y) * u));
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked) return 0;
    const i64 localFirst = layer->local_time(FrameIndex{first}).value;
    const i64 localLast = localFirst + (last - first);
    history_.before_mutation(*comp, project_->timeline().current(), "motion sketch");
    for (int axis = 0; axis < 2; ++axis) {
        Track& t = layer->tracks.get_or_create(axis == 0 ? TrackProperty::PositionX : TrackProperty::PositionY);
        animation::write_samples(t, localFirst, axis == 0 ? xs : ys);
        if (smoothing > 0.0f) {
            const u32 a = t.find_exact(FrameIndex{localFirst}), b = t.find_exact(FrameIndex{localLast});
            if (a != kInvalidIndex && b != kInvalidIndex && b >= a + 2) (void)animation::smooth(t, a, b, smoothing);
        }
    }
    // O caminho curvo antigo do trecho não vale mais (o dedo desenhou outro).
    auto& tangents = layer->positionTangents;
    tangents.erase(std::remove_if(tangents.begin(), tangents.end(), [&](const SpatialTangent& st) {
        return st.time >= localFirst && st.time <= localLast;
    }), tangents.end());
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(last - first + 1);
}

} // namespace aurea
