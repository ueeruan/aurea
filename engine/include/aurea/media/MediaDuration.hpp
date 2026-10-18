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

} // namespace aurea::media
