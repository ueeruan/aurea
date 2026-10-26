// =============================================================================
//  Aurea / core / ColorCode.hpp
//
//  Omega 0.3.2, área 05 (aparência), P1-7: o CÓDIGO da cor do seletor —
//  copiar e colar. Puro e sem alocação; as duas interfaces chamam pela ponte,
//  então o mesmo texto vira a mesma cor no Android e no iOS.
//
//  Colar aceita (maiúsculas ou minúsculas, espaços nas pontas ignorados):
//    #RRGGBB      0xRRGGBB      RRGGBB            → alfa 100 %
//    #RRGGBBAA    0xRRGGBBAA    RRGGBBAA          → alfa do último par
//    #RGB         RGB                              → cada dígito dobrado (alfa 100 %)
//    RRGGBB NN%   #RRGGBB NN%   (NN = 0..100, aceita "NN %" e decimais)
//  Qualquer outra coisa não é cor (o "Colar" fica desabilitado).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cmath>
#include <cstdio>
#include <string_view>

namespace aurea::color_code {

namespace detail {
[[nodiscard]] constexpr i32 hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
[[nodiscard]] constexpr bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
[[nodiscard]] constexpr std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}
/// `n` dígitos hexadecimais seguidos → bytes (n par). Falso se algum não for hex.
[[nodiscard]] constexpr bool hex_bytes(std::string_view s, u8* out, u32 bytes) noexcept {
    if (s.size() != bytes * 2) return false;
    for (u32 i = 0; i < bytes; ++i) {
        const i32 hi = hex_digit(s[i * 2]), lo = hex_digit(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<u8>(hi * 16 + lo);
    }
    return true;
}
} // namespace detail

/// Texto → RGBA 0..1 (sRGB reto). Falso = não é um código de cor.
[[nodiscard]] inline bool parse(std::string_view text, f32 rgba[4]) noexcept {
    std::string_view s = detail::trim(text);
    if (s.empty() || !rgba) return false;
    // "RRGGBB NN%": separa a porcentagem do fim.
    f32 alphaPercent = -1.0f;
    if (s.back() == '%') {
        std::string_view body = detail::trim(s.substr(0, s.size() - 1));
        usize cut = body.size();
        while (cut > 0 && ((body[cut - 1] >= '0' && body[cut - 1] <= '9') || body[cut - 1] == '.' || body[cut - 1] == ','))
            --cut;
        const std::string_view number = body.substr(cut);
        if (number.empty() || cut == 0 || !detail::is_space(body[cut - 1])) return false;
        f64 value = 0.0, scale = 0.0;
        bool digits = false;
        for (char c : number) {
            if (c == '.' || c == ',') { if (scale > 0.0) return false; scale = 1.0; continue; }
            digits = true;
            if (scale > 0.0) { scale *= 0.1; value += (c - '0') * scale; }
            else value = value * 10.0 + (c - '0');
        }
        if (!digits || value > 100.0) return false;
        alphaPercent = static_cast<f32>(value);
        s = detail::trim(body.substr(0, cut));
    }
    if (s.size() >= 1 && s.front() == '#') s.remove_prefix(1);
    else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.remove_prefix(2);
    u8 b[4] = {0, 0, 0, 255};
    if (s.size() == 3) {
        for (u32 i = 0; i < 3; ++i) {
            const i32 d = detail::hex_digit(s[i]);
            if (d < 0) return false;
            b[i] = static_cast<u8>(d * 17);
        }
    } else if (s.size() == 6) {
        if (!detail::hex_bytes(s, b, 3)) return false;
    } else if (s.size() == 8) {
        if (alphaPercent >= 0.0f) return false;   // alfa duas vezes: ambíguo
        if (!detail::hex_bytes(s, b, 4)) return false;
    } else {
        return false;
    }
    rgba[0] = b[0] / 255.0f;
    rgba[1] = b[1] / 255.0f;
    rgba[2] = b[2] / 255.0f;
    rgba[3] = alphaPercent >= 0.0f ? alphaPercent / 100.0f : b[3] / 255.0f;
    return true;
}

/// Componente 0..1 → byte (arredondado, preso).
[[nodiscard]] inline u8 to_byte(f32 v) noexcept {
    if (!std::isfinite(v)) return 0;
    const f32 c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<u8>(std::lround(c * 255.0f));
}

/// RGBA → "#RRGGBB" (alfa cheio ou `withAlpha` falso) ou "#RRGGBBAA".
/// `out` precisa de 10 bytes. Devolve o comprimento escrito.
inline u32 format(const f32 rgba[4], bool withAlpha, char* out, u32 capacity) noexcept {
    if (!out || capacity < 10 || !rgba) return 0;
    const u8 a = to_byte(rgba[3]);
    const bool alpha = withAlpha && a != 255;
    const int n = alpha ? std::snprintf(out, capacity, "#%02X%02X%02X%02X", to_byte(rgba[0]), to_byte(rgba[1]), to_byte(rgba[2]), a)
                        : std::snprintf(out, capacity, "#%02X%02X%02X", to_byte(rgba[0]), to_byte(rgba[1]), to_byte(rgba[2]));
    return n > 0 ? static_cast<u32>(n) : 0u;
}

/// Texto/ícones por cima da cor: escuros se a luminância passa de ½ ou o alfa
/// fica abaixo de ½ (o xadrez claro aparece), senão claros. Luminância de
/// Rec.709 sobre o sRGB codificado, como a faixa do seletor mostra.
[[nodiscard]] inline bool wants_dark_text(const f32 rgba[4]) noexcept {
    if (!rgba) return false;
    if (rgba[3] <= 0.5f) return true;
    return 0.2126f * rgba[0] + 0.7152f * rgba[1] + 0.0722f * rgba[2] > 0.5f;
}

} // namespace aurea::color_code
