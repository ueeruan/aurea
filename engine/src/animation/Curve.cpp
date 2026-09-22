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

    const f32 eased = apply_easing(a.interp, t01, a.bx1, a.by1, a.bx2, a.by2);
    return lerpf(a.value, b.value, eased);
}

u32 Track::set(FrameIndex t, f32 value, Interpolation interp) noexcept {
    const usize n = keys.size();

    // Caso comum no arrasto: o keyframe já existe neste tempo. Substitui o
    // valor e mantém a interpolação que o usuário escolheu.
    const u32 exact = find_exact(t);
    if (exact != kInvalidIndex) {
        keys[exact].value = value;
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
    return inserted;
}

void Track::set_interpolation(FrameIndex t, Interpolation in, f32 bx1, f32 by1,
                              f32 bx2, f32 by2) noexcept {
    const u32 i = find_exact(t);
    if (i == kInvalidIndex) return;
    keys[i].interp = in;
    // Só grava os control points quando a curva é de fato bezier, para não
    // sobrescrever uma curva configurada por um clique acidental em "linear".
    if (in == Interpolation::Bezier || in == Interpolation::CustomCurve) {
        keys[i].bx1 = bx1;
        keys[i].by1 = by1;
        keys[i].bx2 = bx2;
        keys[i].by2 = by2;
    }
}

} // namespace aurea
