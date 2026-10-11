#pragma once
#include "aurea/core/Types.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace aurea {
struct PreviewViewportScale { u32 numerator = 1, denominator = 1; };

// AUTO allocates for the fitted physical preview, with modest oversampling for
// edges and text. Manual scales and callers without a drawable stay untouched.
// This is an editor ceiling, not an export/capture resolution policy.
inline PreviewViewportScale preview_viewport_scale(bool automatic, u32 width, u32 height,
    u32 surfaceWidth, u32 surfaceHeight, f32 zoom, u32 numerator, u32 denominator, f32 fps = 0.0f) noexcept {
    PreviewViewportScale scale{std::max(1u, numerator), std::max(1u, denominator)};
    if (!automatic || !width || !height || !surfaceWidth || !surfaceHeight) return scale;
    const f64 fit = std::min(f64(surfaceWidth) / width, f64(surfaceHeight) / height);
    const f64 magnification = std::clamp(std::isfinite(zoom) ? f64(zoom) : 1., .01, 64.);
    // Keep a readable minimum for small previews, without upscaling small media.
    const f64 floor = std::min(1., 360. / std::min(width, height));
    // `fps` (Beta 0.0.5, "trava em 2K/4K a 60 fps; a 30 fps fica normal"): o
    // tempo por quadro cai com o fps, então os pixels também — vazão de pixels
    // constante: k = √(30/fps) (60 fps → 0,71: metade dos pixels). Sobre-
    // amostragem 1,25·k e lado menor até 1080·k (o teto do app antigo; 764 a
    // 60 fps, 540 a 120). O zoom continua devolvendo o detalhe. Sem fps (0):
    // a regra anterior, sem teto.
    f64 view = fit * magnification * 1.25;
    if (std::isfinite(fps) && fps > 0.f) {
        const f64 k = std::min(1., std::sqrt(30. / f64(fps)));
        const f64 cap = std::max(floor, 1080. * k / std::min(width, height));
        view = std::min(fit * 1.25 * k, cap) * magnification;
    }
    const f64 ceiling = std::min(1., std::max(floor, view));
    const u32 longest = std::max(width, height);
    // Bucket sizes prevent tiny layout changes from discarding the frame cache.
    const u32 target = static_cast<u32>(std::min(f64(longest), std::ceil(longest * ceiling / 64.) * 64.));
    if (f64(target) / longest >= f64(scale.numerator) / scale.denominator) return scale;
    const u32 divisor = std::gcd(target, longest);
    return {target / divisor, longest / divisor};
}
}
