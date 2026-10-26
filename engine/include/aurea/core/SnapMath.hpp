// =============================================================================
//  Aurea / core / SnapMath.hpp
//
//  Omega 0.3.2, área 05 (aparência), P1-2 e P0-2: as contas de ÍMÃ dos gestos
//  de Mover/Girar/Escala e do seletor de cor. Puras (sem modelo, sem lock): as
//  duas interfaces chamam pela ponte (JNI / AureaEngine.mm), então o número é
//  o mesmo no Android e no iOS. Os alvos do trackpad vêm do motor
//  (Engine::query_snap_targets).
//
//  1. Ímã "grudento" do trackpad (ControlPad da referência): cada alvo vira uma
//     FAIXA de dedo (25 px na referência) em que o valor fica PARADO. O gesto
//     anda num eixo de "dedo" contínuo; o valor sai por uma função monótona que
//     tem um patamar em cada alvo. Sem salto ao sair do patamar: o valor volta
//     a andar do próprio alvo.
//
//         dedo f ──►  ────/‾‾‾‾/────/‾‾‾‾/────   valor v
//                         t0        t1
//
//     `finger_from_value` dá o ponto de partida do gesto (no MEIO do patamar
//     se o valor já está num alvo) e `value_from_finger` o valor a cada passo.
//
//  2. Roda de rotação (RotateView): ímã a cada 45° com zona de 2° de cada lado.
//  3. Anel de matiz (HuePicker): ímã a cada 60° com zona de 15°; um toque a mais
//     de 45° da matiz atual salta para o múltiplo de 60° mais perto.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::snap {

/// Largura do patamar na referência, em pixels de dedo.
inline constexpr f32 kPlateauFingerPx = 25.0f;

/// Valor para a posição de dedo `finger`. `targets` em ordem CRESCENTE, sem
/// repetidos; `plateau` = largura do patamar nas unidades do valor (≥ 0).
/// `active` (opcional) recebe o índice do alvo em que o valor parou, ou −1.
[[nodiscard]] inline f32 value_from_finger(const f32* targets, u32 count, f32 plateau, f32 finger,
                                           i32* active = nullptr) noexcept {
    if (active) *active = -1;
    if (!std::isfinite(finger)) return 0.0f;
    if (!targets || count == 0 || !(plateau > 0.0f)) return finger;
    f32 offset = 0.0f;
    for (u32 i = 0; i < count; ++i) {
        const f32 t = targets[i];
        if (!std::isfinite(t)) continue;
        const f32 start = t + offset;            // onde o patamar começa no eixo do dedo
        if (finger < start) return finger - offset;
        if (finger <= start + plateau) {
            if (active) *active = static_cast<i32>(i);
            return t;
        }
        offset += plateau;
    }
    return finger - offset;
}

/// A posição de dedo de um valor (o começo do gesto). Valor em cima de um alvo
/// (|v − t| ≤ `epsilon`) cai no MEIO do patamar: dá para sair para os dois lados.
[[nodiscard]] inline f32 finger_from_value(const f32* targets, u32 count, f32 plateau, f32 value,
                                           f32 epsilon = 1e-4f) noexcept {
    if (!std::isfinite(value)) return 0.0f;
    if (!targets || count == 0 || !(plateau > 0.0f)) return value;
    f32 offset = 0.0f;
    for (u32 i = 0; i < count; ++i) {
        const f32 t = targets[i];
        if (!std::isfinite(t)) continue;
        if (std::fabs(value - t) <= epsilon) return t + offset + plateau * 0.5f;
        if (value < t) return value + offset;
        offset += plateau;
    }
    return value + offset;
}

/// Ordena e tira repetidos (|a − b| ≤ epsilon) de uma lista de alvos, no lugar.
/// `partner` (opcional) anda junto (a linha-guia de cada alvo). Devolve o novo tamanho.
inline u32 sort_unique(f32* targets, f32* partner, u32 count, f32 epsilon = 1e-3f) noexcept {
    if (!targets || count == 0) return 0;
    // Inserção: as listas são curtas (dezenas) e o par precisa andar junto.
    for (u32 i = 1; i < count; ++i) {
        const f32 t = targets[i];
        const f32 p = partner ? partner[i] : 0.0f;
        u32 j = i;
        while (j > 0 && targets[j - 1] > t) {
            targets[j] = targets[j - 1];
            if (partner) partner[j] = partner[j - 1];
            --j;
        }
        targets[j] = t;
        if (partner) partner[j] = p;
    }
    u32 n = 0;
    for (u32 i = 0; i < count; ++i) {
        if (!std::isfinite(targets[i])) continue;
        if (n > 0 && std::fabs(targets[i] - targets[n - 1]) <= epsilon) continue;
        targets[n] = targets[i];
        if (partner) partner[n] = partner[i];
        ++n;
    }
    return n;
}

/// Roda de rotação: ângulo acumulado (graus) → ângulo mostrado/gravado. Dentro
/// de `zone` graus de um múltiplo de `step`, gruda no múltiplo. `snapped`
/// (opcional) diz se grudou (para o háptico na ENTRADA do ímã).
[[nodiscard]] inline f32 dial_snap(f32 accumDeg, bool* snapped = nullptr, f32 step = 45.0f, f32 zone = 2.0f) noexcept {
    if (snapped) *snapped = false;
    if (!std::isfinite(accumDeg) || !(step > 0.0f)) return 0.0f;
    const f32 nearest = std::round(accumDeg / step) * step;
    if (std::fabs(accumDeg - nearest) <= zone) {
        if (snapped) *snapped = true;
        return nearest;
    }
    return accumDeg;
}

/// Limite prático da roda (a referência: ±3.600.000°).
inline constexpr f32 kDialLimitDeg = 3600000.0f;

/// Voltas inteiras e o ângulo dentro da volta (o centro da roda mostra os dois).
/// `turns` = trunc(|a| / 360) com o sinal de `a`; `within` ∈ (−360, 360).
inline void dial_turns(f32 angleDeg, i32& turns, f32& within) noexcept {
    if (!std::isfinite(angleDeg)) { turns = 0; within = 0.0f; return; }
    const f32 t = std::trunc(angleDeg / 360.0f);
    turns = static_cast<i32>(t);
    within = angleDeg - t * 360.0f;
}

/// Anel de matiz: matiz (graus, 0..360) depois do ímã de 60° com zona de 15°.
[[nodiscard]] inline f32 hue_snap(f32 hueDeg, bool* snapped = nullptr) noexcept {
    if (snapped) *snapped = false;
    if (!std::isfinite(hueDeg)) return 0.0f;
    f32 h = std::fmod(hueDeg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const f32 nearest = std::round(h / 60.0f) * 60.0f;
    if (std::fabs(h - nearest) <= 7.5f) {   // zona de 15° (7,5° de cada lado)
        if (snapped) *snapped = true;
        return nearest >= 360.0f ? 0.0f : nearest;
    }
    return h;
}

/// Toque no anel de matiz: a mais de 45° da matiz atual, salta para o múltiplo
/// de 60° mais perto do toque; senão continua da matiz atual (giro relativo).
[[nodiscard]] inline f32 hue_touch_start(f32 currentDeg, f32 touchDeg) noexcept {
    if (!std::isfinite(currentDeg) || !std::isfinite(touchDeg)) return 0.0f;
    f32 d = std::fmod(touchDeg - currentDeg, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    if (std::fabs(d) <= 45.0f) return currentDeg;
    f32 h = std::fmod(touchDeg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const f32 nearest = std::round(h / 60.0f) * 60.0f;
    return nearest >= 360.0f ? 0.0f : nearest;
}

/// Aceleração opcional do trackpad (ControlPad): fator (v/600)^1,1 preso em
/// 0,5..2,5, com a velocidade (px/s) amortecida 0,65 antiga + 0,35 nova.
[[nodiscard]] inline f32 pad_acceleration(f32 smoothedSpeedPxPerSec) noexcept {
    if (!std::isfinite(smoothedSpeedPxPerSec) || smoothedSpeedPxPerSec <= 0.0f) return 0.5f;
    return std::clamp(std::pow(smoothedSpeedPxPerSec / 600.0f, 1.1f), 0.5f, 2.5f);
}
[[nodiscard]] inline f32 pad_smooth_speed(f32 previous, f32 current) noexcept {
    return previous * 0.65f + current * 0.35f;
}

/// Toque-empurrão de 1 px: um toque CURTO (sem arrasto) numa faixa de ¼ da
/// borda do trackpad empurra para aquele lado; canto não empurra. Devolve
/// 0 nada, 1 esquerda, 2 cima, 3 direita, 4 baixo. `x`,`y` relativos ao pad.
[[nodiscard]] inline u32 nudge_side(f32 x, f32 y, f32 width, f32 height) noexcept {
    if (!(width > 0.0f) || !(height > 0.0f)) return 0;
    const bool left = x < width * 0.25f, right = x > width * 0.75f;
    const bool top = y < height * 0.25f, bottom = y > height * 0.75f;
    const u32 sides = (left ? 1u : 0u) + (right ? 1u : 0u) + (top ? 1u : 0u) + (bottom ? 1u : 0u);
    if (sides != 1) return 0;   // centro ou canto
    return left ? 1u : top ? 2u : right ? 3u : 4u;
}

} // namespace aurea::snap
