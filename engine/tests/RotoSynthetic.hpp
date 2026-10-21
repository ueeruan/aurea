#pragma once
// Sequência sintética do Refinar borda (test_roto.cpp): corpo de borda nítida
// com fios de "cabelo" finos e semitransparentes sobre fundo com textura,
// parado nos quadros 0-9 (só o ruído do sensor muda) e andando depois.
// Alfa e cor do objeto verdadeiros na resolução cheia.

#include "aurea/core/Types.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aurea::roto_synthetic {

struct Frame {
    u32 width = 0, height = 0;
    std::vector<u8> rgb;      ///< observado (com ruído)
    std::vector<f32> alpha;   ///< verdade
    std::vector<f32> fg;      ///< cor verdadeira do objeto (3 por px, 0..1)
    std::vector<f32> bg;      ///< fundo verdadeiro (3 por px, 0..1)
    f32 cx = 0, cy = 0, rx = 0, ry = 0;
};

inline u32 hash3(u32 x, u32 y, u32 z) noexcept {
    u32 h = x * 73856093u ^ y * 19349663u ^ z * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
inline f32 noise(u32 x, u32 y, u32 z) noexcept { return static_cast<f32>(hash3(x, y, z) & 0xFFFF) / 65535.0f - 0.5f; }

/// Centro do corpo no quadro `f` (px): parado até o 9, depois anda.
inline void center(u32 W, u32 H, u32 f, f32& cx, f32& cy) noexcept {
    const f32 t = f < 10 ? 0.0f : static_cast<f32>(f - 9);
    cx = 0.42f * W + 0.0045f * W * t;
    cy = 0.56f * H + 0.012f * H * std::sin(0.35f * t);
}

inline void make(u32 W, u32 H, u32 f, Frame& out, f32 sensorNoise = 2.5f) {
    out.width = W; out.height = H;
    const usize n = static_cast<usize>(W) * H;
    out.rgb.assign(n * 3, 0); out.alpha.assign(n, 0.0f); out.fg.assign(n * 3, 0.0f); out.bg.assign(n * 3, 0.0f);
    center(W, H, f, out.cx, out.cy);
    out.rx = 0.16f * W; out.ry = 0.27f * H;
    const f32 k = static_cast<f32>(H) / 720.0f;
    // Fundo: textura fixa (listras + grão), parado.
    for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) {
        const f32 xs = x / k, ys = y / k;
        f32* b = &out.bg[(static_cast<usize>(y) * W + x) * 3];
        b[0] = (40 + 25 * std::sin(xs * 0.07f) + 15 * std::sin(ys * 0.05f + xs * 0.02f) + 20 * noise(x, y, 1)) / 255.0f;
        b[1] = (110 + 50.0f * y / H + 20 * noise(x, y, 2)) / 255.0f;
        b[2] = (85 + 30 * std::sin((xs + 2 * ys) * 0.03f) + 20 * noise(x, y, 3)) / 255.0f;
    }
    // Corpo: elipse com borda anti-serrilhada (4×4 amostras perto da borda).
    std::vector<f32> body(n, 0.0f);
    for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) {
        auto inside = [&](f32 px, f32 py) { const f32 dx = (px - out.cx) / out.rx, dy = (py - out.cy) / out.ry; return dx * dx + dy * dy < 1.0f; };
        const f32 dx = (x + 0.5f - out.cx) / out.rx, dy = (y + 0.5f - out.cy) / out.ry;
        const f32 r = std::sqrt(dx * dx + dy * dy);
        const f32 edgePx = std::fabs(r - 1.0f) * std::min(out.rx, out.ry);
        f32 c;
        if (edgePx > 2.0f) c = r < 1.0f ? 1.0f : 0.0f;
        else {
            u32 hit = 0;
            for (u32 sy = 0; sy < 4; ++sy) for (u32 sx = 0; sx < 4; ++sx) hit += inside(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4);
            c = hit / 16.0f;
        }
        body[static_cast<usize>(y) * W + x] = c;
    }
    // Cabelo: fios de 1,2 px (escala 720p) saindo da metade de cima.
    std::vector<f32> hair(n, 0.0f);
    const u32 strands = 70;
    for (u32 s = 0; s < strands; ++s) {
        const f32 ang = 3.4f + 2.6f * (s + 0.5f * noise(s, 0, 9) + 0.5f) / strands;   // ~195°..345°
        const f32 nx = std::cos(ang), ny = std::sin(ang);
        const f32 rootX = out.cx + 0.97f * out.rx * nx, rootY = out.cy + 0.97f * out.ry * ny;
        const f32 len = (0.035f + 0.04f * (noise(s, 1, 9) + 0.5f)) * H;
        const f32 curl = 0.6f * noise(s, 2, 9);
        const f32 ex = rootX + len * (nx * std::cos(curl) - ny * std::sin(curl)), ey = rootY + len * (ny * std::cos(curl) + nx * std::sin(curl));
        const f32 mx = rootX + 0.5f * len * nx + 0.3f * len * curl * -ny, my = rootY + 0.5f * len * ny + 0.3f * len * curl * nx;
        const f32 half = 0.6f * k, opacity = 0.9f;
        constexpr u32 kSeg = 24;
        f32 px[kSeg + 1], py[kSeg + 1];
        for (u32 i = 0; i <= kSeg; ++i) {
            const f32 t = static_cast<f32>(i) / kSeg;
            px[i] = (1 - t) * (1 - t) * rootX + 2 * (1 - t) * t * mx + t * t * ex;
            py[i] = (1 - t) * (1 - t) * rootY + 2 * (1 - t) * t * my + t * t * ey;
        }
        const i32 bx0 = std::max(0, static_cast<i32>(std::min({rootX, mx, ex})) - 3), bx1 = std::min(static_cast<i32>(W) - 1, static_cast<i32>(std::max({rootX, mx, ex})) + 3);
        const i32 by0 = std::max(0, static_cast<i32>(std::min({rootY, my, ey})) - 3), by1 = std::min(static_cast<i32>(H) - 1, static_cast<i32>(std::max({rootY, my, ey})) + 3);
        for (i32 y = by0; y <= by1; ++y) for (i32 x = bx0; x <= bx1; ++x) {
            const f32 qx = x + 0.5f, qy = y + 0.5f;
            f32 best = 1e9f;
            for (u32 i = 0; i < kSeg; ++i) {
                const f32 ax = px[i + 1] - px[i], ay = py[i + 1] - py[i];
                const f32 t = std::clamp(((qx - px[i]) * ax + (qy - py[i]) * ay) / std::max(ax * ax + ay * ay, 1e-6f), 0.0f, 1.0f);
                const f32 ddx = qx - (px[i] + t * ax), ddy = qy - (py[i] + t * ay);
                best = std::min(best, ddx * ddx + ddy * ddy);
            }
            const f32 cov = std::clamp(half + 0.5f - std::sqrt(best), 0.0f, 1.0f) * opacity;
            f32& h = hair[static_cast<usize>(y) * W + static_cast<usize>(x)];
            h = 1.0f - (1.0f - h) * (1.0f - cov);
        }
    }
    u32 seed = f * 7919u + 17u;
    for (u32 y = 0; y < H; ++y) for (u32 x = 0; x < W; ++x) {
        const usize i = static_cast<usize>(y) * W + x;
        const f32 ab = body[i], ah = hair[i];
        const f32 a = 1.0f - (1.0f - ab) * (1.0f - ah);
        // Corpo laranja com sombreado suave; cabelo castanho escuro.
        const f32 shade = 0.85f + 0.15f * std::clamp(1.0f - (y - out.cy + out.ry) / (2.2f * out.ry), 0.0f, 1.0f);
        const f32 Fb[3] = {205 / 255.0f * shade, 95 / 255.0f * shade, 55 / 255.0f * shade};
        const f32 Fh[3] = {110 / 255.0f, 62 / 255.0f, 30 / 255.0f};
        f32* F = &out.fg[i * 3];
        const f32* B = &out.bg[i * 3];
        out.alpha[i] = a;
        for (u32 c = 0; c < 3; ++c) {
            F[c] = a > 1e-6f ? (ah * Fh[c] + (1.0f - ah) * ab * Fb[c]) / a : Fb[c];
            const f32 sensor = sensorNoise * noise(x, y, seed + c) * 2.0f;   // ±sensorNoise/255 por quadro
            const f32 v = (a * F[c] + (1.0f - a) * B[c]) * 255.0f + sensor;
            out.rgb[i * 3 + c] = static_cast<u8>(std::clamp(std::lround(v), 0l, 255l));
        }
    }
}

} // namespace aurea::roto_synthetic
