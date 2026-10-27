#pragma once
// =============================================================================
//  Aurea / media / MediaDuration.hpp
//
//  0.2.4 (C6): a duração que o contêiner DECLARA nem sempre é a das amostras.
//  Um arquivo com a duração falsa entrava na timeline com horas de "último
//  quadro repetido" e inflava a composição. As plataformas leem a última
//  amostra de verdade; a regra de qual valor vale fica aqui, igual nas duas.
// =============================================================================
#include "aurea/core/Types.hpp"

#include <algorithm>

namespace aurea::media {

/// `declaredUs`: duração do contêiner (0 = ausente). `lastSampleUs`: maior
/// instante de amostra lido perto do fim (< 0 = não lido). `frameUs`: duração
/// de uma amostra (1/fps). Devolve a duração a usar, em µs.
[[nodiscard]] constexpr i64 reconcile_duration_us(i64 declaredUs, i64 lastSampleUs, i64 frameUs) noexcept {
    if (lastSampleUs < 0) return std::max<i64>(declaredUs, 0);
    const i64 actual = lastSampleUs + std::max<i64>(frameUs, 0);
    if (declaredUs <= 0) return actual;
    // Folga para contêineres que arredondam ou têm áudio um pouco mais longo:
    // 1 s ou 2 %, o que for maior. Só corta quando passa muito disso.
    const i64 slack = std::max<i64>(1'000'000, actual / 50);
    return declaredUs > actual + slack ? actual : declaredUs;
}

/// 0.3.6: taxa de quadros pelos instantes das primeiras amostras, já em ordem
/// crescente (`count` itens). 0 = não deu para medir (quem chama usa 30).
/// Com intervalos todos perto da mediana (±1,5 ms: arredondamento do
/// contêiner), vale a MÉDIA — Matroska grava em milissegundos e um 60 fps sai
/// 16/17/17 ms: a mediana dava 17 ms = 58,8 fps. Com intervalos variados (VFR,
/// um buraco), a mediana, como antes.
[[nodiscard]] inline f64 frame_rate_from_sorted_times(const i64* times, usize count) noexcept {
    if (!times || count < 3) return 0.0;
    i64 diffs[64];
    usize n = 0;
    for (usize i = 1; i < count && n < 64; ++i)
        if (times[i] > times[i - 1]) diffs[n++] = times[i] - times[i - 1];
    if (n == 0) return 0.0;
    i64 sorted[64];
    for (usize i = 0; i < n; ++i) sorted[i] = diffs[i];
    std::sort(sorted, sorted + n);
    const i64 median = sorted[n / 2];
    if (median <= 0) return 0.0;
    if (n >= 2 && sorted[0] >= median - 1500 && sorted[n - 1] <= median + 1500) {
        i64 sum = 0;
        for (usize i = 0; i < n; ++i) sum += diffs[i];
        return 1e6 * static_cast<f64>(n) / static_cast<f64>(sum);
    }
    return 1e6 / static_cast<f64>(median);
}

} // namespace aurea::media
