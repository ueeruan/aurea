// =============================================================================
//  Aurea / media / ImageResample.hpp
//
//  Reduzir uma foto que não cabe no espaço de imagens do projeto em vez de
//  recusá-la. Antes, cada foto de celular (12–50 MP, decodificada até 4096 px)
//  ocupava ~48 MB residentes e o espaço é no máximo 256 MB: a 5ª foto dava
//  "Não foi possível importar a imagem" (relato do SM-A566B, build 2182).
//
//  Também a orientação EXIF (1..8): antes nenhuma das duas plataformas girava
//  a foto e o retrato do celular entrava deitado.
//
//  Só `prepare_pixels` aloca (pode lançar std::bad_alloc; quem chama captura).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace aurea::image {

/// Marca em Asset::profile.profile: os pixels da imagem estão na orientação de
/// exibição (EXIF aplicado pelo motor) e podem ter sido reduzidos no import.
/// Assets sem a marca (projetos antigos) reabrem crus, como sempre.
inline constexpr u32 kDisplayOriented = 0x4F52544Eu;   // 'ORTN'

/// Menor lado maior aceito ao reduzir: abaixo disso a foto já não serve para
/// vídeo e o motor recusa (BudgetExceeded) em vez de entregar um borrão.
inline constexpr u32 kShrinkFloorSide = 512;

struct Size { u32 width = 0; u32 height = 0; };

/// Cabe inteira em `roomBytes`: tamanho original. Não cabe: reduz (mesma
/// proporção) para ocupar no máximo METADE do que sobra — assim a foto
/// seguinte ainda tem lugar, em vez de a 6ª encher tudo e a 7ª falhar.
/// {0,0} = nem reduzida a kShrinkFloorSide cabe.
[[nodiscard]] inline Size fit_to_room(u32 w, u32 h, u64 roomBytes, u64 overhead) noexcept {
    if (!w || !h) return {};
    const auto cost = [overhead](u64 a, u64 b) { return a * b * 4u + overhead; };
    if (cost(w, h) <= roomBytes) return Size{w, h};
    const u64 share = roomBytes / 2;
    if (share <= overhead) return {};
    const f64 k = std::sqrt(static_cast<f64>(share - overhead) / (static_cast<f64>(w) * h * 4.0));
    u32 nw = std::max(1u, static_cast<u32>(std::floor(w * k)));
    u32 nh = std::max(1u, static_cast<u32>(std::floor(h * k)));
    while (cost(nw, nh) > share && nw > 1 && nh > 1) {   // arredondamento do sqrt
        if (w >= h) { --nw; nh = std::max(1u, static_cast<u32>(static_cast<u64>(h) * nw / w)); }
        else { --nh; nw = std::max(1u, static_cast<u32>(static_cast<u64>(w) * nh / h)); }
    }
    if (std::max(nw, nh) < kShrinkFloorSide || cost(nw, nh) > share) return {};
    return Size{nw, nh};
}

/// A mesma foto numa escala maior que a gravada no projeto (o arredondamento
/// de cada eixo cabe na tolerância). É o que deixa reabrir a foto reduzida no
/// import: a plataforma decodifica no tamanho canônico e o motor reduz de novo.
[[nodiscard]] inline bool same_picture_larger(u32 pw, u32 ph, u32 sw, u32 sh) noexcept {
    if (!pw || !ph || !sw || !sh || pw < sw || ph < sh || (pw == sw && ph == sh)) return false;
    const u64 a = static_cast<u64>(pw) * sh, b = static_cast<u64>(ph) * sw;
    return (a > b ? a - b : b - a) <= static_cast<u64>(pw) + ph;
}

/// Média por área (cada pixel de destino = a caixa de origem que ele cobre,
/// com as bordas fracionárias). Alfa RETO na entrada e na saída; a cor é
/// ponderada pelo alfa, senão a borda de um PNG recortado escurece.
/// `dst` tem dw×dh×4 bytes; só reduz (dw ≤ sw, dh ≤ sh).
inline bool resample_area(const u8* src, u32 sw, u32 sh, u8* dst, u32 dw, u32 dh) noexcept {
    if (!src || !dst || !sw || !sh || !dw || !dh || dw > sw || dh > sh) return false;
    const f64 kx = static_cast<f64>(sw) / dw, ky = static_cast<f64>(sh) / dh;
    for (u32 dy = 0; dy < dh; ++dy) {
        const f64 fy0 = dy * ky, fy1 = (dy + 1) * ky;
        const u32 y0 = static_cast<u32>(fy0), y1 = std::min<u32>(sh, static_cast<u32>(std::ceil(fy1)));
        for (u32 dx = 0; dx < dw; ++dx) {
            const f64 fx0 = dx * kx, fx1 = (dx + 1) * kx;
            const u32 x0 = static_cast<u32>(fx0), x1 = std::min<u32>(sw, static_cast<u32>(std::ceil(fx1)));
            f64 weighted[3] = {0, 0, 0}, plain[3] = {0, 0, 0}, alpha = 0, total = 0;
            for (u32 y = y0; y < y1; ++y) {
                const f64 wy = std::min<f64>(fy1, y + 1.0) - std::max<f64>(fy0, y);
                if (wy <= 0) continue;
                const u8* row = src + static_cast<usize>(y) * sw * 4u;
                for (u32 x = x0; x < x1; ++x) {
                    const f64 w = (std::min<f64>(fx1, x + 1.0) - std::max<f64>(fx0, x)) * wy;
                    if (w <= 0) continue;
                    const u8* p = row + static_cast<usize>(x) * 4u;
                    const f64 a = p[3] * w;
                    for (int c = 0; c < 3; ++c) { weighted[c] += p[c] * a; plain[c] += p[c] * w; }
                    alpha += a;
                    total += w;
                }
            }
            u8* out = dst + (static_cast<usize>(dy) * dw + dx) * 4u;
            if (total <= 0) { out[0] = out[1] = out[2] = out[3] = 0; continue; }
            for (int c = 0; c < 3; ++c) {
                const f64 v = alpha > 0 ? weighted[c] / alpha : plain[c] / total;
                out[c] = static_cast<u8>(std::clamp(v + 0.5, 0.0, 255.0));
            }
            out[3] = static_cast<u8>(std::clamp(alpha / total + 0.5, 0.0, 255.0));
        }
    }
    return true;
}

/// Tamanho na tela de uma foto w×h com a orientação EXIF `o` (5..8 trocam os lados).
[[nodiscard]] inline Size oriented_size(u32 w, u32 h, u8 o) noexcept {
    return o >= 5 && o <= 8 ? Size{h, w} : Size{w, h};
}

/// Pixels crus (w×h) → orientação de exibição. `dst` tem w×h×4 bytes e o
/// tamanho de `oriented_size`. Tabela do EXIF: 2 espelho H, 3 180°, 4 espelho V,
/// 5 transposta, 6 90° horário, 7 transversa, 8 90° anti-horário.
inline bool orient(const u8* src, u32 w, u32 h, u8 o, u8* dst) noexcept {
    if (!src || !dst || !w || !h) return false;
    if (o < 1 || o > 8) o = 1;
    const Size out = oriented_size(w, h, o);
    for (u32 y = 0; y < out.height; ++y) {
        for (u32 x = 0; x < out.width; ++x) {
            u32 sx = x, sy = y;
            switch (o) {
                case 2: sx = w - 1 - x; break;
                case 3: sx = w - 1 - x; sy = h - 1 - y; break;
                case 4: sy = h - 1 - y; break;
                case 5: sx = y; sy = x; break;
                case 6: sx = y; sy = h - 1 - x; break;
                case 7: sx = w - 1 - y; sy = h - 1 - x; break;
                case 8: sx = w - 1 - y; sy = x; break;
                default: break;
            }
            std::memcpy(dst + (static_cast<usize>(y) * out.width + x) * 4u,
                        src + (static_cast<usize>(sy) * w + sx) * 4u, 4u);
        }
    }
    return true;
}

/// O que fica residente: a foto crua w×h girada por `o` e reduzida a
/// `target` (≤ o tamanho girado). Pode lançar std::bad_alloc: só nas
/// fronteiras de import (/EHsc, -fexceptions), que traduzem em Status.
inline bool make_display_pixels(const u8* src, u32 w, u32 h, u8 o, Size target, std::vector<u8>& out) {
    if (o < 1 || o > 8) o = 1;
    const Size shown = oriented_size(w, h, o);
    if (!src || !target.width || !target.height || target.width > shown.width || target.height > shown.height) return false;
    const usize shownBytes = static_cast<usize>(shown.width) * shown.height * 4u;
    const usize targetBytes = static_cast<usize>(target.width) * target.height * 4u;
    const bool shrink = target.width != shown.width || target.height != shown.height;
    if (o == 1 && !shrink) { out.assign(src, src + shownBytes); return true; }
    if (!shrink) { out.resize(shownBytes); return orient(src, w, h, o, out.data()); }
    if (o == 1) { out.resize(targetBytes); return resample_area(src, w, h, out.data(), target.width, target.height); }
    std::vector<u8> turned(shownBytes);
    if (!orient(src, w, h, o, turned.data())) return false;
    out.resize(targetBytes);
    return resample_area(turned.data(), shown.width, shown.height, out.data(), target.width, target.height);
}

/// Teto do que fica residente por foto: 2× a área da composição (zoom de
/// ~1,4× sem perder nitidez), nunca menos que 4 MP. Com 4× cada foto num vídeo
/// 1080×1920 ocupava 33 MB: só ~7 cabiam inteiras em 256 MB e, como cada
/// seguinte leva metade do que sobra, a 11ª caía abaixo do piso e era
/// recusada. Com 2×: ~16 MB, ~15 fotos inteiras antes de reduzir.
[[nodiscard]] inline u64 pixel_cap(u32 compW, u32 compH) noexcept {
    return std::max<u64>(4ull << 20, 2ull * compW * compH);
}

/// Tamanho residente de uma foto que aparece `shown` na tela: o teto por foto,
/// depois o espaço que sobra no projeto. {0,0} = não cabe nem reduzida.
[[nodiscard]] inline Size target_size(Size shown, u32 compW, u32 compH, u64 roomBytes, u64 overhead) noexcept {
    if (!shown.width || !shown.height) return {};
    Size s = shown;
    const u64 cap = pixel_cap(compW, compH);
    if (static_cast<u64>(s.width) * s.height > cap) {
        const f64 k = std::sqrt(static_cast<f64>(cap) / (static_cast<f64>(s.width) * s.height));
        s = Size{std::max(1u, static_cast<u32>(std::floor(s.width * k))),
                 std::max(1u, static_cast<u32>(std::floor(s.height * k)))};
    }
    return fit_to_room(s.width, s.height, roomBytes, overhead);
}

/// Import: pixels crus da plataforma (w×h, orientação EXIF `o`) → os pixels
/// residentes, já girados e reduzidos. Errc::BudgetExceeded = não cabe.
/// Pode lançar std::bad_alloc (só fronteiras de import chamam).
[[nodiscard]] inline Errc prepare_pixels(const u8* src, u32 w, u32 h, u8 o, u32 compW, u32 compH,
                                         u64 roomBytes, u64 overhead, u32& outW, u32& outH, std::vector<u8>& out) {
    if (o < 1 || o > 8) o = 1;
    const Size target = target_size(oriented_size(w, h, o), compW, compH, roomBytes, overhead);
    if (!target.width) return Errc::BudgetExceeded;
    if (!make_display_pixels(src, w, h, o, target, out)) return Errc::InvalidArgument;
    outW = target.width; outH = target.height;
    return Errc::Ok;
}

/// Reabrir um asset marcado (kDisplayOriented): a plataforma devolve a foto
/// crua no tamanho canônico; o motor gira e reduz até o tamanho GRAVADO. Outro
/// tamanho/proporção = outra foto (religar). false = não serve.
[[nodiscard]] inline bool reopen_pixels(const u8* rgba, u32 w, u32 h, u8 orientation, u32 savedW, u32 savedH,
                                        std::vector<u8>& out) {
    const u8 o = orientation < 1 || orientation > 8 ? 1 : orientation;
    const Size shown = oriented_size(w, h, o);
    const bool exact = shown.width == savedW && shown.height == savedH;
    if (!exact && !same_picture_larger(shown.width, shown.height, savedW, savedH)) return false;
    return make_display_pixels(rgba, w, h, o, Size{savedW, savedH}, out);
}

} // namespace aurea::image
