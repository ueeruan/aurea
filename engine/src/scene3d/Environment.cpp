// =============================================================================
//  Aurea / scene3d / Environment.cpp
// =============================================================================
#include "aurea/scene3d/Environment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace aurea::scene3d {
namespace {

constexpr f32 kPi = 3.14159265358979f;

/// Cubo em float para o processamento (RGB, sem alfa).
struct FCube {
    u32 size = 0;
    std::array<std::vector<Vec3>, 6> face;
    void init(u32 s) {
        size = s;
        for (auto& f : face) f.assign(static_cast<usize>(s) * s, Vec3{0, 0, 0});
    }
    [[nodiscard]] Vec3& at(u32 f, u32 x, u32 y) { return face[f][static_cast<usize>(y) * size + x]; }
    [[nodiscard]] const Vec3& at(u32 f, u32 x, u32 y) const { return face[f][static_cast<usize>(y) * size + x]; }
};

using FChain = std::vector<FCube>;   ///< mips, do maior para o menor

/// Direção → (face, u, v) em [0,1], convenção do Vulkan/Metal.
void dir_to_face(Vec3 d, u32& face, f32& u, f32& v) noexcept {
    const f32 ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    f32 sc, tc, ma;
    if (ax >= ay && ax >= az) {
        ma = ax;
        if (d.x > 0) { face = 0; sc = -d.z; tc = -d.y; } else { face = 1; sc = d.z; tc = -d.y; }
    } else if (ay >= az) {
        ma = ay;
        if (d.y > 0) { face = 2; sc = d.x; tc = d.z; } else { face = 3; sc = d.x; tc = -d.z; }
    } else {
        ma = az;
        if (d.z > 0) { face = 4; sc = d.x; tc = -d.y; } else { face = 5; sc = -d.x; tc = -d.y; }
    }
    u = 0.5f * (sc / ma + 1.0f);
    v = 0.5f * (tc / ma + 1.0f);
}

Vec3 sample_level(const FCube& c, Vec3 d) noexcept {
    u32 f = 0;
    f32 u = 0, v = 0;
    dir_to_face(d, f, u, v);
    const f32 x = u * c.size - 0.5f, y = v * c.size - 0.5f;
    const i32 x0 = static_cast<i32>(std::floor(x)), y0 = static_cast<i32>(std::floor(y));
    const f32 fx = x - x0, fy = y - y0;
    auto px = [&](i32 xx, i32 yy) {
        xx = std::clamp(xx, 0, static_cast<i32>(c.size) - 1);
        yy = std::clamp(yy, 0, static_cast<i32>(c.size) - 1);
        return c.at(f, static_cast<u32>(xx), static_cast<u32>(yy));
    };
    const Vec3 a = px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx;
    const Vec3 b = px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx;
    return a * (1 - fy) + b * fy;
}

Vec3 sample_chain(const FChain& ch, Vec3 d, f32 lod) noexcept {
    lod = std::clamp(lod, 0.0f, static_cast<f32>(ch.size() - 1));
    const u32 l0 = static_cast<u32>(lod);
    const u32 l1 = std::min<u32>(l0 + 1, static_cast<u32>(ch.size() - 1));
    const f32 t = lod - static_cast<f32>(l0);
    const Vec3 a = sample_level(ch[l0], d);
    return t > 0.0f ? a * (1 - t) + sample_level(ch[l1], d) * t : a;
}

FChain build_chain(FCube base) {
    FChain ch;
    ch.push_back(std::move(base));
    while (ch.back().size > 1) {
        const FCube& s = ch.back();
        FCube n;
        n.init(std::max(1u, s.size / 2));
        for (u32 f = 0; f < 6; ++f) {
            for (u32 y = 0; y < n.size; ++y) {
                for (u32 x = 0; x < n.size; ++x) {
                    n.at(f, x, y) = (s.at(f, 2 * x, 2 * y) + s.at(f, 2 * x + 1, 2 * y) + s.at(f, 2 * x, 2 * y + 1)
                                     + s.at(f, 2 * x + 1, 2 * y + 1)) * 0.25f;
                }
            }
        }
        ch.push_back(std::move(n));
    }
    return ch;
}

f32 radical_inverse(u32 bits) noexcept {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<f32>(bits) * 2.3283064365386963e-10f;
}

/// Meio-vetor GGX amostrado por importância em torno de N.
Vec3 importance_ggx(f32 u1, f32 u2, Vec3 N, f32 a) noexcept {
    const f32 phi = 2.0f * kPi * u1;
    const f32 cosT = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
    const f32 sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    const Vec3 h{sinT * std::cos(phi), sinT * std::sin(phi), cosT};
    const Vec3 up = std::fabs(N.z) < 0.999f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 t = up.cross(N).normalized();
    const Vec3 b = N.cross(t);
    return (t * h.x + b * h.y + N * h.z).normalized();
}

void to_half_cube(const FCube& c, std::vector<u16>& out) {
    out.resize(static_cast<usize>(c.size) * c.size * 6 * 4);
    usize k = 0;
    for (u32 f = 0; f < 6; ++f) {
        for (const Vec3& p : c.face[f]) {
            out[k++] = float_to_half(p.x);
            out[k++] = float_to_half(p.y);
            out[k++] = float_to_half(p.z);
            out[k++] = float_to_half(1.0f);
        }
    }
}

EnvironmentMaps build_from_chain(const FChain& src) {
    EnvironmentMaps m;
    const u32 base = src[0].size;

    // --- Fundo visível: o ambiente em si -------------------------------------
    m.background.size = base;
    m.background.mips = 1;
    m.background.levels.resize(1);
    to_half_cube(src[0], m.background.levels[0]);

    // --- Irradiância por harmônicos esféricos (ordem 2) ----------------------
    {
        const FCube& s = src[std::min<usize>(src.size() - 1, 2)];   // ~32² basta para a difusa
        std::array<Vec3, 9> sh{};
        f32 wsum = 0.0f;
        for (u32 f = 0; f < 6; ++f) {
            for (u32 y = 0; y < s.size; ++y) {
                for (u32 x = 0; x < s.size; ++x) {
                    const f32 u = 2.0f * (x + 0.5f) / s.size - 1.0f, v = 2.0f * (y + 0.5f) / s.size - 1.0f;
                    const f32 w = 4.0f / std::pow(1.0f + u * u + v * v, 1.5f);   // ângulo sólido do texel
                    const Vec3 d = cube_direction(f, x, y, s.size);
                    const Vec3 c = s.at(f, x, y) * w;
                    const f32 b[9] = {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                                      1.092548f * d.x * d.y, 1.092548f * d.y * d.z,
                                      0.315392f * (3.0f * d.z * d.z - 1.0f), 1.092548f * d.x * d.z,
                                      0.546274f * (d.x * d.x - d.y * d.y)};
                    for (int i = 0; i < 9; ++i) sh[i] = sh[i] + c * b[i];
                    wsum += w;
                }
            }
        }
        const f32 norm = 4.0f * kPi / wsum;
        for (Vec3& c : sh) c = c * norm;
        FCube irr;
        irr.init(32);
        const f32 A0 = kPi, A1 = 2.0f * kPi / 3.0f, A2 = kPi / 4.0f;   // convolução com o cosseno
        for (u32 f = 0; f < 6; ++f) {
            for (u32 y = 0; y < irr.size; ++y) {
                for (u32 x = 0; x < irr.size; ++x) {
                    const Vec3 d = cube_direction(f, x, y, irr.size);
                    Vec3 e = sh[0] * (0.282095f * A0) + sh[1] * (0.488603f * d.y * A1) + sh[2] * (0.488603f * d.z * A1)
                           + sh[3] * (0.488603f * d.x * A1) + sh[4] * (1.092548f * d.x * d.y * A2)
                           + sh[5] * (1.092548f * d.y * d.z * A2) + sh[6] * (0.315392f * (3.0f * d.z * d.z - 1.0f) * A2)
                           + sh[7] * (1.092548f * d.x * d.z * A2) + sh[8] * (0.546274f * (d.x * d.x - d.y * d.y) * A2);
                    // Radiância difusa = irradiância / π (o shader multiplica pelo albedo).
                    irr.at(f, x, y) = Vec3{std::max(0.0f, e.x), std::max(0.0f, e.y), std::max(0.0f, e.z)} * (1.0f / kPi);
                }
            }
        }
        m.irradiance.size = 32;
        m.irradiance.mips = 1;
        m.irradiance.levels.resize(1);
        to_half_cube(irr, m.irradiance.levels[0]);
    }

    // --- Especular pré-filtrado (GGX, importância filtrada) --------------------
    {
        u32 mips = 0;
        for (u32 s = base; s >= 4; s /= 2) ++mips;
        m.prefiltered.size = base;
        m.prefiltered.mips = mips;
        m.prefiltered.levels.resize(mips);
        const f32 texelSolidAngle = 4.0f * kPi / (6.0f * base * base);
        for (u32 level = 0; level < mips; ++level) {
            const u32 size = base >> level;
            const f32 roughness = mips > 1 ? static_cast<f32>(level) / static_cast<f32>(mips - 1) : 0.0f;
            FCube out;
            out.init(size);
            if (level == 0) {
                out = src[0];
            } else {
                const f32 a = roughness * roughness;
                const u32 samples = level <= 2 ? 64u : 48u;
                for (u32 f = 0; f < 6; ++f) {
                    for (u32 y = 0; y < size; ++y) {
                        for (u32 x = 0; x < size; ++x) {
                            const Vec3 N = cube_direction(f, x, y, size);
                            Vec3 acc{0, 0, 0};
                            f32 wsum = 0.0f;
                            for (u32 i = 0; i < samples; ++i) {
                                const Vec3 H = importance_ggx(static_cast<f32>(i) / samples, radical_inverse(i), N, a);
                                const Vec3 L = H * (2.0f * N.dot(H)) - N;
                                const f32 NdotL = N.dot(L);
                                if (NdotL <= 0.0f) continue;
                                // Karis: lod da fonte pela pdf (importância filtrada).
                                const f32 NdotH = std::max(N.dot(H), 1e-4f);
                                const f32 a2 = a * a;
                                const f32 dd = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
                                const f32 D = a2 / (kPi * dd * dd);
                                const f32 pdf = D * 0.25f;
                                const f32 sa = 1.0f / (samples * pdf + 1e-6f);
                                const f32 lod = 0.5f * std::log2(std::max(sa / texelSolidAngle, 1.0f)) + 1.0f;
                                acc = acc + sample_chain(src, L, lod) * NdotL;
                                wsum += NdotL;
                            }
                            out.at(f, x, y) = wsum > 0.0f ? acc * (1.0f / wsum) : sample_level(src[0], N);
                        }
                    }
                }
            }
            to_half_cube(out, m.prefiltered.levels[level]);
        }
    }

    // --- LUT da BRDF (split-sum) ------------------------------------------------
    {
        const u32 n = 64;
        m.lutSize = n;
        m.brdfLut.resize(static_cast<usize>(n) * n * 4);
        const u32 samples = 128;
        for (u32 y = 0; y < n; ++y) {
            const f32 rough = (y + 0.5f) / n;
            const f32 a = rough * rough;
            for (u32 x = 0; x < n; ++x) {
                const f32 NdotV = (x + 0.5f) / n;
                const Vec3 V{std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV};
                const Vec3 N{0, 0, 1};
                f32 A = 0.0f, B = 0.0f;
                for (u32 i = 0; i < samples; ++i) {
                    const Vec3 H = importance_ggx(static_cast<f32>(i) / samples, radical_inverse(i), N, a);
                    const Vec3 L = H * (2.0f * V.dot(H)) - V;
                    const f32 NdotL = std::max(L.z, 0.0f);
                    const f32 NdotH = std::max(H.z, 0.0f);
                    const f32 VdotH = std::max(V.dot(H), 0.0f);
                    if (NdotL <= 0.0f) continue;
                    // Smith-GGX correlacionado (o mesmo do pbr.frag), forma de visibilidade.
                    const f32 a2 = a * a;
                    const f32 gv = NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
                    const f32 gl = NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
                    const f32 Vis = 0.5f / std::max(gv + gl, 1e-6f);
                    const f32 G = Vis * 4.0f * NdotL * VdotH / std::max(NdotH, 1e-6f);
                    const f32 Fc = std::pow(1.0f - VdotH, 5.0f);
                    A += (1.0f - Fc) * G;
                    B += Fc * G;
                }
                const usize k = (static_cast<usize>(y) * n + x) * 4;
                m.brdfLut[k + 0] = float_to_half(A / samples);
                m.brdfLut[k + 1] = float_to_half(B / samples);
                m.brdfLut[k + 2] = 0;
                m.brdfLut[k + 3] = float_to_half(1.0f);
            }
        }
    }
    return m;
}

/// Estúdio neutro (Y para cima; +Z = na direção da câmera padrão).
Vec3 studio_radiance(Vec3 d) noexcept {
    const Vec3 top{0.80f, 0.83f, 0.88f};
    const Vec3 horizon{0.50f, 0.51f, 0.53f};
    const Vec3 floorC{0.10f, 0.095f, 0.09f};
    Vec3 c;
    if (d.y >= 0.0f) {
        const f32 t = std::pow(d.y, 0.6f);
        c = horizon * (1 - t) + top * t;
    } else {
        const f32 t = std::min(1.0f, -d.y * 4.0f);
        c = horizon * 0.55f * (1 - t) + floorC * t;
    }
    // Caixas de luz: principal (esquerda, alto, frente) e recorte (direita, trás).
    auto box = [&](Vec3 center, f32 halfAngleDeg, f32 intensity) {
        const f32 cd = d.dot(center.normalized());
        const f32 edge = std::cos(halfAngleDeg * kPi / 180.0f);
        if (cd <= edge) return 0.0f;
        const f32 t = std::min(1.0f, (cd - edge) / (1.0f - edge) * 4.0f);
        return intensity * t * t * (3.0f - 2.0f * t);
    };
    const f32 key = box(Vec3{-0.55f, 0.65f, 0.55f}, 18.0f, 9.0f);
    const f32 rim = box(Vec3{0.75f, 0.35f, -0.55f}, 14.0f, 5.0f);
    const f32 fill = box(Vec3{0.8f, 0.1f, 0.6f}, 30.0f, 1.2f);
    return c + Vec3{1.0f, 0.97f, 0.93f} * key + Vec3{0.92f, 0.95f, 1.0f} * rim + Vec3{1, 1, 1} * fill;
}

} // namespace

Vec3 cube_direction(u32 face, u32 x, u32 y, u32 size) noexcept {
    const f32 u = 2.0f * (x + 0.5f) / size - 1.0f;
    const f32 v = 2.0f * (y + 0.5f) / size - 1.0f;
    Vec3 d;
    switch (face) {
        case 0: d = Vec3{1.0f, -v, -u}; break;
        case 1: d = Vec3{-1.0f, -v, u}; break;
        case 2: d = Vec3{u, 1.0f, v}; break;
        case 3: d = Vec3{u, -1.0f, -v}; break;
        case 4: d = Vec3{u, -v, 1.0f}; break;
        default: d = Vec3{-u, -v, -1.0f}; break;
    }
    return d.normalized();
}

u16 float_to_half(f32 v) noexcept {
    u32 x;
    std::memcpy(&x, &v, 4);
    const u32 sign = (x >> 16) & 0x8000u;
    i32 exp = static_cast<i32>((x >> 23) & 0xFF) - 127 + 15;
    u32 mant = x & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return static_cast<u16>(sign);
        mant = (mant | 0x800000u) >> (1 - exp);
        return static_cast<u16>(sign | ((mant + 0x1000u) >> 13));
    }
    if (exp >= 31) return static_cast<u16>(sign | 0x7BFFu);   // satura no maior finito (sem Inf no ambiente)
    const u32 h = sign | (static_cast<u32>(exp) << 10) | ((mant + 0x1000u) >> 13);
    return static_cast<u16>(h > (sign | 0x7BFFu) ? (sign | 0x7BFFu) : h);
}

f32 half_to_float(u16 h) noexcept {
    const u32 sign = (h & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1F;
    u32 mant = h & 0x3FFu;
    u32 bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 1;
            while (!(mant & 0x400u)) { mant <<= 1; --exp; }
            mant &= 0x3FFu;
            bits = sign | ((exp + 112) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 112) << 23) | (mant << 13);
    }
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}

EnvironmentMaps build_studio_environment(u32 baseSize) noexcept {
    FCube base;
    base.init(baseSize);
    // Superamostra 2×2 por texel: as bordas das caixas de luz não serrilham.
    for (u32 f = 0; f < 6; ++f) {
        for (u32 y = 0; y < baseSize; ++y) {
            for (u32 x = 0; x < baseSize; ++x) {
                Vec3 acc{0, 0, 0};
                for (u32 sy = 0; sy < 2; ++sy) {
                    for (u32 sx = 0; sx < 2; ++sx) {
                        acc = acc + studio_radiance(cube_direction(f, x * 2 + sx, y * 2 + sy, baseSize * 2));
                    }
                }
                base.at(f, x, y) = acc * 0.25f;
            }
        }
    }
    return build_from_chain(build_chain(std::move(base)));
}

EnvironmentMaps build_environment_from_equirect(const f32* rgb, u32 width, u32 height, u32 baseSize) noexcept {
    FCube base;
    base.init(baseSize);
    if (!rgb || width == 0 || height == 0) return build_studio_environment(baseSize);
    auto fetch = [&](f32 u, f32 v) {
        const f32 x = u * width - 0.5f, y = v * height - 0.5f;
        const i32 x0 = static_cast<i32>(std::floor(x)), y0 = static_cast<i32>(std::floor(y));
        const f32 fx = x - x0, fy = y - y0;
        auto px = [&](i32 xx, i32 yy) {
            xx = ((xx % static_cast<i32>(width)) + static_cast<i32>(width)) % static_cast<i32>(width);   // dá a volta
            yy = std::clamp(yy, 0, static_cast<i32>(height) - 1);
            const f32* p = rgb + (static_cast<usize>(yy) * width + static_cast<usize>(xx)) * 3;
            return Vec3{p[0], p[1], p[2]};
        };
        const Vec3 a = px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx;
        const Vec3 b = px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx;
        return a * (1 - fy) + b * fy;
    };
    for (u32 f = 0; f < 6; ++f) {
        for (u32 y = 0; y < baseSize; ++y) {
            for (u32 x = 0; x < baseSize; ++x) {
                const Vec3 d = cube_direction(f, x, y, baseSize);
                // Equiretangular: azimute em torno de +Y, centro do mapa em −Z.
                const f32 u = 0.5f + std::atan2(d.x, -d.z) / (2.0f * kPi);
                const f32 v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
                const Vec3 c = fetch(u, v);
                base.at(f, x, y) = Vec3{std::min(c.x, 6.0e4f), std::min(c.y, 6.0e4f), std::min(c.z, 6.0e4f)};
            }
        }
    }
    return build_from_chain(build_chain(std::move(base)));
}

} // namespace aurea::scene3d
