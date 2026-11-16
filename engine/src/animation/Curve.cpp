// =============================================================================
//  Aurea / animation / Curve.cpp
//
//  Avaliação de keyframes.
//
//  Tudo aqui é caminho quente: uma layer com 12 propriedades animadas, 60 vezes
//  por segundo, com 200 layers na timeline. A ordem de grandeza é ~150 mil
//  avaliações por segundo. Por isso:
//
//   - busca binária sobre vetor contíguo (o cache acerta a maioria);
//   - cache de último índice, porque reprodução anda para frente e o intervalo
//     buscado costuma ser o mesmo do frame anterior;
//   - nenhuma alocação.
// =============================================================================
#include "aurea/animation/Curve.hpp"
#include "aurea/expr/Expression.hpp"

namespace aurea {

u32 Track::find_exact(FrameIndex t) const noexcept {
    // Busca binária à mão: std::lower_bound seria equivalente, mas aqui o
    // compilador enxerga o tipo e vetoriza a comparação.
    i64 lo = 0;
    i64 hi = static_cast<i64>(keys.size()) - 1;
    while (lo <= hi) {
        const i64 mid = lo + (hi - lo) / 2;
        if (keys[static_cast<usize>(mid)].time.value == t.value) {
            return static_cast<u32>(mid);
        }
        if (keys[static_cast<usize>(mid)].time.value < t.value) lo = mid + 1;
        else hi = mid - 1;
    }
    return kInvalidIndex;
}

u32 Track::find_before(FrameIndex t) const noexcept {
    const usize n = keys.size();
    if (n == 0) return kInvalidIndex;
    if (t.value < keys[0].time.value) return kInvalidIndex;
    if (t.value >= keys[n - 1].time.value) return static_cast<u32>(n - 1);

    // Atalho do caso comum: o frame avançou pouco desde a última avaliação, e
    // o intervalo é o mesmo ou o seguinte. Duas comparações resolvem quase
    // sempre, e a busca binária só entra quando o usuário salta na timeline.
    const u32 cached = lastIndex;
    if (cached < n) {
        if (keys[cached].time.value <= t.value) {
            const u32 next = cached + 1;
            if (next >= n || keys[next].time.value > t.value) {
                return cached;
            }
        }
    }

    i64 lo = 0;
    i64 hi = static_cast<i64>(n) - 1;
    i64 best = 0;
    while (lo <= hi) {
        const i64 mid = lo + (hi - lo) / 2;
        const i64 kt = keys[static_cast<usize>(mid)].time.value;
        if (kt <= t.value) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    lastIndex = static_cast<u32>(best);
    return static_cast<u32>(best);
}

f32 Track::sample(FrameIndex t) const noexcept {
    // O gancho das expressões: TODA leitura de propriedade passa por aqui ou
    // por `value_or` — não há como um caminho de render ignorar a expressão.
    if (has_expression()) return expr::evaluate_track(*this, t, nullptr);
    return sample_keys(t);
}

f32 Track::value_or(FrameIndex t, f32 fallback) const noexcept {
    if (has_expression()) return expr::evaluate_track(*this, t, &fallback);
    return keys.empty() ? fallback : sample_keys(t);
}

f32 Track::sample_f(f64 t) const noexcept {
    if (!std::isfinite(t)) return staticValue;
    if (has_expression()) return expr::evaluate_track_f(*this, t, nullptr);
    return sample_keys_f(t);
}

f32 Track::value_or_f(f64 t, f32 fallback) const noexcept {
    if (!std::isfinite(t)) return fallback;
    if (has_expression()) return expr::evaluate_track_f(*this, t, &fallback);
    return keys.empty() ? fallback : sample_keys_f(t);
}

f32 Track::sample_keys_f(f64 t) const noexcept {
    if (!std::isfinite(t) || keys.empty()) return staticValue;
    if (keys.size() == 1 || t < static_cast<f64>(keys.front().time.value)) return keys.front().value;
    if (t >= static_cast<f64>(keys.back().time.value)) return keys.back().value;
    usize before = lastIndex;
    if (before + 1 >= keys.size() || t < static_cast<f64>(keys[before].time.value)
        || t >= static_cast<f64>(keys[before + 1].time.value)) {
        const auto next = std::upper_bound(keys.begin(), keys.end(), t,
            [](f64 frame, const Keyframe& key) { return frame < static_cast<f64>(key.time.value); });
        before = static_cast<usize>(next - keys.begin() - 1);
        lastIndex = static_cast<u32>(before);
    }
    const auto& a = keys[before];
    const auto& b = keys[before + 1];
    if (a.interp == Interpolation::Hold) return a.value;
    const f64 span = static_cast<f64>(b.time.value) - static_cast<f64>(a.time.value);
    if (span <= 0.0) return b.value;
    return lerpf(a.value, b.value, keyframe_ease(a,
        static_cast<f32>((t - static_cast<f64>(a.time.value)) / span)));
}

f32 Track::sample_keys(FrameIndex t) const noexcept {
    const usize n = keys.size();
    if (n == 0) return staticValue;
    if (n == 1) return keys[0].value;

    const u32 before = find_before(t);
    if (before == kInvalidIndex) {
        // Antes do primeiro keyframe: o valor é o do primeiro. Isso é o que o
        // usuário espera ao mover o playhead para antes da animação existir.
        return keys[0].value;
    }
    if (before + 1 >= n) return keys[n - 1].value;

    const Keyframe& a = keys[before];
    const Keyframe& b = keys[before + 1];

    if (a.interp == Interpolation::Hold) return a.value;

    const i64 span = b.time.value - a.time.value;
    if (span <= 0) return b.value;

    const f32 t01 = static_cast<f32>(
        static_cast<f64>(t.value - a.time.value) / static_cast<f64>(span));

    const f32 eased = keyframe_ease(a, t01);
    return lerpf(a.value, b.value, eased);
}

u32 Track::set(FrameIndex t, f32 value, Interpolation interp) noexcept {
    const usize n = keys.size();

    // Caso comum no arrasto: o keyframe já existe neste tempo. Substitui o
    // valor e mantém a interpolação que o usuário escolheu.
    const u32 exact = find_exact(t);
    if (exact != kInvalidIndex) {
        keys[exact].value = value;
        resolve_auto_handles();
        return exact;
    }

    if (n == 0) {
        keys.push_back(Keyframe{t, value, interp});
        lastIndex = 0;
        return 0;
    }

    // Busca binária pela posição de inserção, mantendo a ordem por tempo.
    i64 lo = 0;
    i64 hi = static_cast<i64>(n);
    while (lo < hi) {
        const i64 mid = lo + (hi - lo) / 2;
        if (keys[static_cast<usize>(mid)].time.value < t.value) lo = mid + 1;
        else hi = mid;
    }

    Keyframe k{};
    k.time = t;
    k.value = value;
    // Interpolação padrão linear. O keyframe novo NÃO herda a curva do anterior:
    // herdar produziria uma curva que o usuário não escolheu, e ele veria a
    // animação mudar de forma ao inserir um ponto no meio.
    k.interp = interp;

    keys.insert(keys.begin() + lo, k);
    lastIndex = static_cast<u32>(lo);
    resolve_auto_handles();
    return static_cast<u32>(lo);
}

bool Track::remove(FrameIndex t) noexcept {
    const u32 i = find_exact(t);
    if (i == kInvalidIndex) return false;
    keys.erase(keys.begin() + i);
    lastIndex = 0;
    return true;
}

u32 Track::move(FrameIndex from, FrameIndex to) noexcept {
    if (from == to) {
        const u32 i = find_exact(from);
        return i;
    }
    const u32 src = find_exact(from);
    if (src == kInvalidIndex) return kInvalidIndex;

    Keyframe k = keys[src];
    k.time = to;
    keys.erase(keys.begin() + src);

    // Se já havia um keyframe no destino, ele é substituído — o arrasto
    // sobrepõe, que é o comportamento esperado.
    const u32 dst = find_exact(to);
    if (dst != kInvalidIndex) keys.erase(keys.begin() + dst);

    i64 lo = 0;
    i64 hi = static_cast<i64>(keys.size());
    while (lo < hi) {
        const i64 mid = lo + (hi - lo) / 2;
        if (keys[static_cast<usize>(mid)].time.value < to.value) lo = mid + 1;
        else hi = mid;
    }
    const u32 inserted = static_cast<u32>(lo);
    keys.insert(keys.begin() + inserted, k);
    lastIndex = inserted;
    resolve_auto_handles();
    return inserted;
}

void Track::set_interpolation(FrameIndex t, Interpolation in, f32 bx1, f32 by1,
                              f32 bx2, f32 by2, u8 power) noexcept {
    const u32 i = find_exact(t);
    if (i == kInvalidIndex) return;
    keys[i].interp = in;
    if (power != 0) keys[i].easePower = clamp_ease_power(power);
    // Alça escolhida à mão (lote K): os dois lados do trecho deixam de ser
    // automáticos; a bézier contínua continua marcada.
    keys[i].flags = static_cast<u8>(keys[i].flags & ~kKeyAutoBezier);
    if (i + 1 < keys.size()) keys[i + 1].flags = static_cast<u8>(keys[i + 1].flags & ~kKeyAutoBezier);
    // Só grava os control points quando a curva é de fato bezier, para não
    // sobrescrever uma curva configurada por um clique acidental em "linear".
    // As curvas com parâmetros (Quique, Elástico, Overshoot) guardam-nos nos
    // mesmos quatro floats (Math.hpp, kEaseParamMarker).
    if (in == Interpolation::Bezier || in == Interpolation::CustomCurve || ease_has_params(in)) {
        keys[i].bx1 = bx1;
        keys[i].by1 = by1;
        keys[i].bx2 = bx2;
        keys[i].by2 = by2;
    }
}

// =============================================================================
// Lote K (AE): bézier automática e contínua no tempo
// =============================================================================
namespace {
/// O trecho que sai de `k` aceita alça escrita (as curvas com forma própria —
/// Segurar, Quique, Elástico... — ficam como estão).
bool segment_takes_handles(const Keyframe& k) noexcept {
    return k.interp == Interpolation::Linear || k.interp == Interpolation::Bezier || k.interp == Interpolation::CustomCurve;
}

/// Inclinação (valor por quadro) do trecho a→b na saída de `a` (side 0) ou na
/// chegada em `b` (side 1). Falso = o trecho não é linear/bézier.
bool segment_slope(const Keyframe& a, const Keyframe& b, int side, f64& out) noexcept {
    if (!segment_takes_handles(a)) return false;
    const f64 dt = static_cast<f64>(b.time.value - a.time.value);
    if (dt <= 0.0) return false;
    const f64 chord = (static_cast<f64>(b.value) - static_cast<f64>(a.value)) / dt;
    if (a.interp == Interpolation::Linear) { out = chord; return true; }
    if (side == 0) out = a.bx1 > 1e-6f ? chord * static_cast<f64>(a.by1) / static_cast<f64>(a.bx1) : chord;
    else out = a.bx2 < 1.0f - 1e-6f ? chord * (1.0 - static_cast<f64>(a.by2)) / (1.0 - static_cast<f64>(a.bx2)) : chord;
    return std::isfinite(out);
}

/// Linear vira bézier equivalente (as alças na reta), para um lado receber alça.
void make_bezier(Keyframe& a) noexcept {
    if (a.interp == Interpolation::Linear) {
        a.interp = Interpolation::Bezier;
        a.bx1 = 1.0f / 3.0f; a.by1 = 1.0f / 3.0f;
        a.bx2 = 2.0f / 3.0f; a.by2 = 2.0f / 3.0f;
    }
    a.easePower = 1;
    a.easingPreset = 0;
}

f32 clamp_handle(f64 v) noexcept {
    return std::isfinite(v) ? static_cast<f32>(std::clamp(v, -20.0, 21.0)) : 0.0f;
}

/// Escreve a inclinação `s` na saída de `a` (trecho a→b), influência 1/3.
void write_out(Keyframe& a, const Keyframe& b, f64 s) noexcept {
    make_bezier(a);
    const f64 dt = static_cast<f64>(b.time.value - a.time.value);
    const f64 dv = static_cast<f64>(b.value) - static_cast<f64>(a.value);
    a.bx1 = 1.0f / 3.0f;
    a.by1 = std::fabs(dv) < 1e-9 ? 0.0f : clamp_handle(s * (1.0 / 3.0) * dt / dv);
}

/// Escreve a inclinação `s` na chegada em `b` (trecho a→b), influência 1/3.
void write_in(Keyframe& a, const Keyframe& b, f64 s) noexcept {
    make_bezier(a);
    const f64 dt = static_cast<f64>(b.time.value - a.time.value);
    const f64 dv = static_cast<f64>(b.value) - static_cast<f64>(a.value);
    a.bx2 = 2.0f / 3.0f;
    a.by2 = std::fabs(dv) < 1e-9 ? 1.0f : clamp_handle(1.0 - s * (1.0 / 3.0) * dt / dv);
}
} // namespace

void Track::resolve_auto_handles() noexcept {
    const usize n = keys.size();
    if (n < 2 || !has_auto_handles()) return;
    // Primeiro as automáticas (dependem só dos valores), depois as contínuas
    // (dependem das alças já resolvidas dos vizinhos).
    for (int pass = 0; pass < 2; ++pass) {
        for (usize k = 0; k < n; ++k) {
            const u8 f = keys[k].flags;
            const bool autoKey = (f & kKeyAutoBezier) != 0;
            if (pass == 0 ? !autoKey : (autoKey || (f & kKeyContinuous) == 0)) continue;
            const bool hasIn = k > 0 && segment_takes_handles(keys[k - 1]) && keys[k].time.value > keys[k - 1].time.value;
            const bool hasOut = k + 1 < n && segment_takes_handles(keys[k]) && keys[k + 1].time.value > keys[k].time.value;
            f64 s = 0.0;
            if (autoKey) {
                if (k > 0 && k + 1 < n) {
                    const f64 a = keys[k - 1].value, b = keys[k].value, c = keys[k + 1].value;
                    const f64 span = static_cast<f64>(keys[k + 1].time.value - keys[k - 1].time.value);
                    // Pico ou vale: parado no key (sem passar do valor), como no AE.
                    s = (b - a) * (c - b) <= 0.0 || span <= 0.0 ? 0.0 : (c - a) / span;
                } else if (k + 1 < n) {
                    const f64 dt = static_cast<f64>(keys[k + 1].time.value - keys[k].time.value);
                    s = dt > 0.0 ? (static_cast<f64>(keys[k + 1].value) - keys[k].value) / dt : 0.0;
                } else {
                    const f64 dt = static_cast<f64>(keys[k].time.value - keys[k - 1].time.value);
                    s = dt > 0.0 ? (static_cast<f64>(keys[k].value) - keys[k - 1].value) / dt : 0.0;
                }
            } else {
                f64 si = 0.0, so = 0.0;
                const bool gi = hasIn && segment_slope(keys[k - 1], keys[k], 1, si);
                const bool go = hasOut && segment_slope(keys[k], keys[k + 1], 0, so);
                if (!gi || !go) continue;   // contínua precisa dos dois lados
                s = 0.5 * (si + so);
            }
            if (hasIn) write_in(keys[k - 1], keys[k], s);
            if (hasOut) write_out(keys[k], keys[k + 1], s);
        }
    }
}

} // namespace aurea
