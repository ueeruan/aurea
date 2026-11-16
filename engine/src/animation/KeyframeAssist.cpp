// =============================================================================
//  Aurea / animation / KeyframeAssist.cpp
//
//  Lote K: inverter no tempo, escala exponencial, suavizador e assar amostras.
//  Ver KeyframeAssist.hpp.
// =============================================================================
#include "aurea/animation/KeyframeAssist.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace aurea::animation {
namespace {
/// Intervalos longos demais não são varridos quadro a quadro.
constexpr i64 kMaxSpan = 200000;

void sort_keys(std::vector<Keyframe>& keys) {
    std::stable_sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

/// Um trecho do suavizador: amostras [i0, i1], alças normalizadas (x = 1/3 e
/// 2/3, então o tempo é linear no parâmetro e só os y são ajustados).
struct FitSegment { i64 i0 = 0, i1 = 0; f32 y1 = 1.0f / 3.0f, y2 = 2.0f / 3.0f; bool flat = false; };

/// Mínimos quadrados dos dois y da bézier cúbica do trecho [i0, i1] sobre as
/// amostras. Devolve o maior erro e onde ele está.
f64 fit_segment(const std::vector<f64>& v, FitSegment& s, i64& worstAt) noexcept {
    const f64 a = v[static_cast<usize>(s.i0)], d = v[static_cast<usize>(s.i1)] - a;
    const f64 len = static_cast<f64>(s.i1 - s.i0);
    worstAt = s.i0;
    f64 worst = 0.0;
    if (std::fabs(d) < 1e-9) {
        // Pontas iguais: a bézier normalizada não sai do lugar; só serve parada.
        s.flat = true;
        for (i64 f = s.i0 + 1; f < s.i1; ++f) {
            const f64 e = std::fabs(v[static_cast<usize>(f)] - a);
            if (e > worst) { worst = e; worstAt = f; }
        }
        return worst;
    }
    // r = B1·w1 + B2·w2 com w = d·y; equações normais 2×2.
    f64 s11 = 0, s12 = 0, s22 = 0, r1 = 0, r2 = 0;
    for (i64 f = s.i0 + 1; f < s.i1; ++f) {
        const f64 u = static_cast<f64>(f - s.i0) / len, w = 1.0 - u;
        const f64 b1 = 3.0 * w * w * u, b2 = 3.0 * w * u * u;
        const f64 r = v[static_cast<usize>(f)] - a - d * u * u * u;
        s11 += b1 * b1; s12 += b1 * b2; s22 += b2 * b2;
        r1 += b1 * r; r2 += b2 * r;
    }
    f64 y1 = 1.0 / 3.0, y2 = 2.0 / 3.0;
    const f64 det = s11 * s22 - s12 * s12;
    if (std::fabs(det) > 1e-12) {
        y1 = (r1 * s22 - r2 * s12) / det / d;
        y2 = (r2 * s11 - r1 * s12) / det / d;
    }
    y1 = std::clamp(std::isfinite(y1) ? y1 : 1.0 / 3.0, -20.0, 21.0);
    y2 = std::clamp(std::isfinite(y2) ? y2 : 2.0 / 3.0, -20.0, 21.0);
    s.y1 = static_cast<f32>(y1);
    s.y2 = static_cast<f32>(y2);
    s.flat = false;
    for (i64 f = s.i0 + 1; f < s.i1; ++f) {
        const f64 u = static_cast<f64>(f - s.i0) / len, w = 1.0 - u;
        const f64 model = a + d * (3.0 * w * w * u * y1 + 3.0 * w * u * u * y2 + u * u * u);
        const f64 e = std::fabs(v[static_cast<usize>(f)] - model);
        if (e > worst) { worst = e; worstAt = f; }
    }
    return worst;
}
} // namespace

void mirror_segment_ease(Keyframe& k) noexcept {
    switch (k.interp) {
        case Interpolation::Bounce:
        case Interpolation::Elastic:
        case Interpolation::Overshoot: (void)mirror_parametric_ease(k); break;
        case Interpolation::EaseIn: k.interp = Interpolation::EaseOut; break;
        case Interpolation::EaseOut: k.interp = Interpolation::EaseIn; break;
        case Interpolation::Bezier:
        case Interpolation::CustomCurve: {
            const f32 x1 = k.bx1, y1 = k.by1, x2 = k.bx2, y2 = k.by2;
            k.bx1 = 1.0f - x2; k.by1 = 1.0f - y2;
            k.bx2 = 1.0f - x1; k.by2 = 1.0f - y1;
            break;
        }
        default: break;
    }
}

bool time_reverse(Track& track, const std::vector<u32>& pickedIn) noexcept {
    std::vector<u32> picked = pickedIn;
    std::sort(picked.begin(), picked.end());
    picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
    picked.erase(std::remove_if(picked.begin(), picked.end(), [&](u32 i) { return i >= track.keys.size(); }), picked.end());
    if (picked.size() < 2) return false;
    const usize n = track.keys.size();
    const i64 lo = track.keys[picked.front()].time.value, hi = track.keys[picked.back()].time.value;
    std::vector<bool> isPicked(n, false);
    for (u32 i : picked) isPicked[i] = true;
    struct Item { Keyframe key; u32 original; };
    std::vector<Item> items;
    items.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        Item it{track.keys[i], i};
        if (isPicked[i]) {
            it.key.time = FrameIndex{lo + hi - it.key.time.value};
            std::swap(it.key.tangentIn, it.key.tangentOut);
            it.key.tangentIn = -it.key.tangentIn;
            it.key.tangentOut = -it.key.tangentOut;
        }
        items.push_back(it);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key.time < b.key.time; });
    for (usize i = 1; i < items.size(); ++i) if (items[i - 1].key.time == items[i].key.time) return false;
    // O trecho entre dois escolhidos que eram vizinhos (agora na ordem
    // inversa) é o espelho do trecho original, que morava no da esquerda de antes.
    std::vector<Keyframe> out(items.size());
    for (usize j = 0; j < items.size(); ++j) {
        Keyframe k = items[j].key;
        if (j + 1 < items.size()) {
            const u32 here = items[j].original, next = items[j + 1].original;
            if (isPicked[here] && isPicked[next] && next + 1 == here) {
                const Keyframe& seg = track.keys[next];
                k.interp = seg.interp;
                k.bx1 = seg.bx1; k.by1 = seg.by1; k.bx2 = seg.bx2; k.by2 = seg.by2;
                k.easePower = seg.easePower;
                k.easingPreset = seg.easingPreset;
                mirror_segment_ease(k);
            }
        }
        out[j] = k;
    }
    track.keys = std::move(out);
    track.lastIndex = 0;
    track.resolve_auto_handles();
    return true;
}

bool exponential_scale(Track& track, u32 first, u32 last) noexcept {
    if (first >= last || last >= track.keys.size()) return false;
    const Keyframe a = track.keys[first], b = track.keys[last];
    const i64 t0 = a.time.value, t1 = b.time.value;
    const f64 v0 = a.value, v1 = b.value;
    if (t1 - t0 < 2 || t1 - t0 > kMaxSpan || !(v0 * v1 > 0.0) || v0 == v1) return false;
    std::vector<Keyframe> out;
    out.reserve(track.keys.size() + static_cast<usize>(t1 - t0));
    for (u32 i = 0; i < first; ++i) out.push_back(track.keys[i]);
    Keyframe head = a;
    head.interp = Interpolation::Linear;
    head.easePower = 1;
    head.easingPreset = 0;
    head.flags = static_cast<u8>(head.flags & ~(kKeyAutoBezier | kKeyContinuous));
    out.push_back(head);
    const f64 ratio = v1 / v0, span = static_cast<f64>(t1 - t0);
    for (i64 f = t0 + 1; f < t1; ++f) {
        Keyframe k{};
        k.time = FrameIndex{f};
        k.value = static_cast<f32>(v0 * std::pow(ratio, static_cast<f64>(f - t0) / span));
        k.interp = Interpolation::Linear;
        out.push_back(k);
    }
    Keyframe tail = b;
    tail.flags = static_cast<u8>(tail.flags & ~(kKeyAutoBezier | kKeyContinuous));
    out.push_back(tail);
    for (usize i = last + 1; i < track.keys.size(); ++i) out.push_back(track.keys[i]);
    track.keys = std::move(out);
    track.lastIndex = 0;
    track.resolve_auto_handles();
    return true;
}

u32 smooth(Track& track, u32 first, u32 last, f32 tolerance) noexcept {
    if (first >= last || last >= track.keys.size() || !(tolerance >= 0.0f)) return 0;
    const i64 t0 = track.keys[first].time.value, t1 = track.keys[last].time.value;
    if (t1 - t0 < 2 || t1 - t0 > kMaxSpan) return 0;
    std::vector<f64> v(static_cast<usize>(t1 - t0 + 1));
    for (i64 f = t0; f <= t1; ++f) {
        const f32 x = track.sample_keys(FrameIndex{f});
        v[static_cast<usize>(f - t0)] = std::isfinite(x) ? x : 0.0;
    }
    // Ajuste recursivo (o de Schneider, em uma dimensão): um trecho que passa
    // da tolerância se divide no quadro do maior erro.
    std::vector<FitSegment> done;
    std::vector<FitSegment> todo{FitSegment{0, t1 - t0}};
    const f64 tol = static_cast<f64>(tolerance);
    while (!todo.empty()) {
        FitSegment s = todo.back();
        todo.pop_back();
        i64 worstAt = s.i0;
        const f64 err = fit_segment(v, s, worstAt);
        if (err <= tol || s.i1 - s.i0 <= 1) { done.push_back(s); continue; }
        const i64 cut = std::clamp(worstAt, s.i0 + 1, s.i1 - 1);
        todo.push_back(FitSegment{cut, s.i1});
        todo.push_back(FitSegment{s.i0, cut});
    }
    std::sort(done.begin(), done.end(), [](const FitSegment& a, const FitSegment& b) { return a.i0 < b.i0; });
    std::vector<Keyframe> out;
    out.reserve(track.keys.size() + done.size());
    for (u32 i = 0; i < first; ++i) out.push_back(track.keys[i]);
    for (usize j = 0; j < done.size(); ++j) {
        const FitSegment& s = done[j];
        Keyframe k = j == 0 ? track.keys[first] : Keyframe{};
        if (j > 0) {
            k.time = FrameIndex{t0 + s.i0};
            k.value = static_cast<f32>(v[static_cast<usize>(s.i0)]);
            k.flags = kKeyContinuous;
        }
        k.easePower = 1;
        k.easingPreset = 0;
        if (s.flat) {
            k.interp = Interpolation::Linear;
        } else {
            k.interp = Interpolation::Bezier;
            k.bx1 = 1.0f / 3.0f; k.by1 = s.y1;
            k.bx2 = 2.0f / 3.0f; k.by2 = s.y2;
        }
        out.push_back(k);
    }
    for (usize i = last; i < track.keys.size(); ++i) out.push_back(track.keys[i]);
    track.keys = std::move(out);
    track.lastIndex = 0;
    // Os keys novos do meio são contínuos: a mesma velocidade dos dois lados.
    track.resolve_auto_handles();
    return static_cast<u32>(done.size() + 1);
}

void write_samples(Track& track, i64 firstFrame, const std::vector<f32>& values) noexcept {
    if (values.empty()) return;
    const i64 lastFrame = firstFrame + static_cast<i64>(values.size()) - 1;
    std::vector<Keyframe> out;
    out.reserve(track.keys.size() + values.size());
    for (const Keyframe& k : track.keys)
        if (k.time.value < firstFrame || k.time.value > lastFrame) out.push_back(k);
    for (usize i = 0; i < values.size(); ++i) {
        Keyframe k{};
        k.time = FrameIndex{firstFrame + static_cast<i64>(i)};
        k.value = std::isfinite(values[i]) ? values[i] : 0.0f;
        k.interp = Interpolation::Linear;
        out.push_back(k);
    }
    sort_keys(out);
    track.keys = std::move(out);
    track.lastIndex = 0;
    track.resolve_auto_handles();
}

} // namespace aurea::animation
