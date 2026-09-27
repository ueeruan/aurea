// =============================================================================
//  Aurea / scene3d / Environment.cpp
//
//  Pipeline (CPU, determinístico, fora da thread de render):
//
//    equiretangular ──pirâmide de caixa 2×2──► cubo-fonte: 2×2 subamostras
//    por texel, cada uma lida no mip da pirâmide que casa com a SUA pegada
//    (área e anisotropia — perto dos polos a pegada é larga em u e estreita
//    em v, então vão mais amostras ao longo de u num mip mais fino). Um sol
//    de 3 texels num 4K não some (energia preservada) nem pisca.
//
//    cubo-fonte ──caixa 2×2 pesada pelo ângulo sólido──► cadeia de mips
//      ├─ fundo: a cadeia a partir do tamanho do fundo, até 1²
//      ├─ especular mip 0: a cadeia no tamanho do especular (filtrado da fonte)
//      ├─ especular mips 1..: GGX por importância FILTRADA (Karis): cada
//      │  amostra lê a cadeia no LOD da sua pdf — sem vaga-lumes de sol.
//      │  Amostras crescem com a rugosidade (48 → 512).
//      └─ irradiância: SH ordem 2 do nível de 32²
//
//  Cada texel é independente: as linhas dos cubos são repartidas entre
//  threads e o resultado é o mesmo com 1 ou 8.
// =============================================================================
#include "aurea/scene3d/Environment.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

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

// --- Threads ------------------------------------------------------------------

u32 worker_count(u32 requested) noexcept {
    if (requested) return std::clamp(requested, 1u, 16u);
    const u32 hw = std::thread::hardware_concurrency();
    return std::clamp(hw > 1 ? hw - 1 : 1u, 1u, 8u);
}

/// `fn(row)` para row em [0, rows), repartido entre `threads` (fatias
/// dinâmicas de 4 linhas). Sem thread disponível, roda tudo aqui.
template <class Fn>
void parallel_rows(u32 threads, u32 rows, const Fn& fn) noexcept {
    constexpr u32 kChunk = 4;
    std::atomic<u32> next{0};
    auto work = [&]() noexcept {
        for (;;) {
            const u32 r0 = next.fetch_add(kChunk, std::memory_order_relaxed);
            if (r0 >= rows) break;
            const u32 r1 = std::min(rows, r0 + kChunk);
            for (u32 r = r0; r < r1; ++r) fn(r);
        }
    };
    const u32 n = std::min(threads, std::max(1u, rows / kChunk));
    std::vector<std::thread> pool;
#if defined(__cpp_exceptions)
    try {
        pool.reserve(n);
        for (u32 i = 1; i < n; ++i) pool.emplace_back(work);
    } catch (...) {
        // Sem thread: quem já subiu continua; o resto roda aqui.
    }
#else
    // Android compila sem exceções (-fno-exceptions): não há o que capturar.
    pool.reserve(n);
    for (u32 i = 1; i < n; ++i) pool.emplace_back(work);
#endif
    work();
    for (auto& t : pool) t.join();
}

// --- Cubo ---------------------------------------------------------------------

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

/// Direção (não normalizada) do ponto (u, v) ∈ [−1,1]² da face.
Vec3 face_point(u32 face, f32 u, f32 v) noexcept {
    switch (face) {
        case 0: return Vec3{1.0f, -v, -u};
        case 1: return Vec3{-1.0f, -v, u};
        case 2: return Vec3{u, 1.0f, v};
        case 3: return Vec3{u, -1.0f, -v};
        case 4: return Vec3{u, -v, 1.0f};
        default: return Vec3{-u, -v, -1.0f};
    }
}

Vec3 sample_level(const FCube& c, Vec3 d) noexcept {
    u32 f = 0;
    f32 u = 0, v = 0;
    dir_to_face(d, f, u, v);
    const f32 x = u * c.size - 0.5f, y = v * c.size - 0.5f;
    const i32 x0 = static_cast<i32>(std::floor(x)), y0 = static_cast<i32>(std::floor(y));
    const f32 fx = x - x0, fy = y - y0;
    const i32 last = static_cast<i32>(c.size) - 1;
    const u32 xa = static_cast<u32>(std::clamp(x0, 0, last)), xb = static_cast<u32>(std::clamp(x0 + 1, 0, last));
    const u32 ya = static_cast<u32>(std::clamp(y0, 0, last)), yb = static_cast<u32>(std::clamp(y0 + 1, 0, last));
    const Vec3 a = c.at(f, xa, ya) * (1 - fx) + c.at(f, xb, ya) * fx;
    const Vec3 b = c.at(f, xa, yb) * (1 - fx) + c.at(f, xb, yb) * fx;
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

/// Ângulo sólido (relativo) do texel (x, y) de uma face `size`²: o texel do
/// centro da face cobre ~5× o do canto.
f32 texel_weight(u32 x, u32 y, u32 size) noexcept {
    const f32 u = 2.0f * (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size) - 1.0f;
    const f32 v = 2.0f * (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size) - 1.0f;
    const f32 q = 1.0f + u * u + v * v;
    return 1.0f / (q * std::sqrt(q));
}

/// Média 2×2 PONDERADA pelo ângulo sólido: a caixa simples na face perde
/// energia no centro (texels grandes) e ganha nos cantos — um sol no meio da
/// face ficava ~25% mais fraco nos mips ásperos.
template <class Fetch>
Vec3 weighted_quad(const Fetch& px, u32 f, u32 x, u32 y, u32 size) noexcept {
    const u32 x0 = 2 * x, y0 = 2 * y;
    const f32 w00 = texel_weight(x0, y0, size), w10 = texel_weight(x0 + 1, y0, size);
    const f32 w01 = texel_weight(x0, y0 + 1, size), w11 = texel_weight(x0 + 1, y0 + 1, size);
    return (px(f, x0, y0) * w00 + px(f, x0 + 1, y0) * w10 + px(f, x0, y0 + 1) * w01 + px(f, x0 + 1, y0 + 1) * w11)
         * (1.0f / (w00 + w10 + w01 + w11));
}

FCube downsample(const FCube& s, u32 threads) {
    FCube n;
    n.init(std::max(1u, s.size / 2));
    if (s.size < 2) { n = s; return n; }
    auto px = [&](u32 f, u32 x, u32 y) { return s.at(f, x, y); };
    parallel_rows(threads, 6 * n.size, [&](u32 row) noexcept {
        const u32 f = row / n.size, y = row % n.size;
        for (u32 x = 0; x < n.size; ++x) n.at(f, x, y) = weighted_quad(px, f, x, y, s.size);
    });
    return n;
}

/// Metade do tamanho, lendo um nível já em half (RGBA16F) — o fundo grande
/// não precisa existir também em float.
FCube downsample_half(const std::vector<u16>& lvl, u32 size, u32 threads) {
    FCube n;
    n.init(std::max(1u, size / 2));
    const usize face = static_cast<usize>(size) * size * 4;
    auto px = [&](u32 f, u32 x, u32 y) {
        const u16* p = lvl.data() + face * f + (static_cast<usize>(y) * size + x) * 4;
        return Vec3{half_to_float(p[0]), half_to_float(p[1]), half_to_float(p[2])};
    };
    parallel_rows(threads, 6 * n.size, [&](u32 row) noexcept {
        const u32 f = row / n.size, y = row % n.size;
        for (u32 x = 0; x < n.size; ++x) n.at(f, x, y) = weighted_quad(px, f, x, y, size);
    });
    return n;
}

FChain build_chain(FCube base, u32 threads) {
    FChain ch;
    ch.push_back(std::move(base));
    while (ch.back().size > 1) ch.push_back(downsample(ch.back(), threads));
    return ch;
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

// --- GGX ----------------------------------------------------------------------

f32 radical_inverse(u32 bits) noexcept {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<f32>(bits) * 2.3283064365386963e-10f;
}

/// Meio-vetor GGX (espaço tangente, N = +Z) amostrado por importância.
Vec3 importance_ggx_tangent(f32 u1, f32 u2, f32 a) noexcept {
    const f32 phi = 2.0f * kPi * u1;
    const f32 cosT = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
    const f32 sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    return Vec3{sinT * std::cos(phi), sinT * std::sin(phi), cosT};
}

/// Base tangente em torno de N (a mesma para toda a geração).
void tangent_frame(Vec3 N, Vec3& t, Vec3& b) noexcept {
    const Vec3 up = std::fabs(N.z) < 0.999f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    t = up.cross(N).normalized();
    b = N.cross(t);
}

/// LUT da BRDF (split-sum). Não depende do ambiente: calculada uma vez por
/// processo (antes era refeita a cada ambiente).
const std::vector<u16>& brdf_lut(u32& n) noexcept {
    static const u32 kN = 64;
    static const std::vector<u16> lut = [] {
        std::vector<u16> out(static_cast<usize>(kN) * kN * 4);
        const u32 samples = 512;
        for (u32 y = 0; y < kN; ++y) {
            const f32 rough = (y + 0.5f) / kN;
            const f32 a = rough * rough;
            for (u32 x = 0; x < kN; ++x) {
                const f32 NdotV = (x + 0.5f) / kN;
                const Vec3 V{std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV};
                f32 A = 0.0f, B = 0.0f;
                for (u32 i = 0; i < samples; ++i) {
                    const Vec3 H = importance_ggx_tangent(static_cast<f32>(i) / samples, radical_inverse(i), a);
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
                const usize k = (static_cast<usize>(y) * kN + x) * 4;
                out[k + 0] = float_to_half(A / samples);
                out[k + 1] = float_to_half(B / samples);
                out[k + 2] = 0;
                out[k + 3] = float_to_half(1.0f);
            }
        }
        return out;
    }();
    n = kN;
    return lut;
}

/// Amostras GGX do mip `level` (sobe com a rugosidade: o lóbulo largo precisa
/// de mais; o estreito é curto e a importância filtrada já o alisa).
u32 prefilter_samples(u32 level) noexcept { return std::min(512u, 48u << std::min(level - 1, 4u)); }

/// Irradiância, especular e LUT a partir da cadeia (nível 0 = base do especular).
void build_lighting(EnvironmentMaps& m, const FChain& src, u32 threads) {
    const u32 base = src[0].size;

    // --- Irradiância por harmônicos esféricos (ordem 2) ----------------------
    {
        usize li = 0;   // o maior nível com ≤ 32² (basta para a difusa)
        while (li + 1 < src.size() && src[li].size > 32) ++li;
        const FCube& s = src[li];
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
        mips = std::max(mips, 1u);
        m.prefiltered.size = base;
        m.prefiltered.mips = mips;
        m.prefiltered.levels.resize(mips);
        to_half_cube(src[0], m.prefiltered.levels[0]);   // espelho: a fonte filtrada por área
        const f32 texelSolidAngle = 4.0f * kPi / (6.0f * static_cast<f32>(base) * static_cast<f32>(base));
        struct Tap { Vec3 L; f32 w; f32 lod; };
        std::vector<Tap> taps;
        for (u32 level = 1; level < mips; ++level) {
            const u32 size = base >> level;
            const f32 roughness = specular_roughness_at(static_cast<f32>(level), mips);
            const f32 a = roughness * roughness;
            const f32 a2 = a * a;
            const u32 samples = prefilter_samples(level);
            // As direções e o LOD de cada amostra só dependem de i (N = V):
            // calculados uma vez por nível, girados para cada texel.
            taps.clear();
            f32 wsum = 0.0f;
            for (u32 i = 0; i < samples; ++i) {
                const Vec3 H = importance_ggx_tangent(static_cast<f32>(i) / samples, radical_inverse(i), a);
                const Vec3 L{2.0f * H.z * H.x, 2.0f * H.z * H.y, 2.0f * H.z * H.z - 1.0f};
                if (L.z <= 0.0f) continue;
                // Karis: LOD da fonte pela pdf (importância filtrada).
                const f32 NdotH = std::max(H.z, 1e-4f);
                const f32 dd = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
                const f32 D = a2 / (kPi * dd * dd);
                const f32 pdf = D * 0.25f;
                const f32 sa = 1.0f / (static_cast<f32>(samples) * pdf + 1e-6f);
                const f32 lod = 0.5f * std::log2(std::max(sa / texelSolidAngle, 1.0f)) + 1.0f;
                taps.push_back(Tap{L, L.z, lod});
                wsum += L.z;
            }
            const f32 inv = wsum > 0.0f ? 1.0f / wsum : 0.0f;
            FCube out;
            out.init(size);
            parallel_rows(threads, 6 * size, [&](u32 row) noexcept {
                const u32 f = row / size, y = row % size;
                for (u32 x = 0; x < size; ++x) {
                    const Vec3 N = cube_direction(f, x, y, size);
                    if (taps.empty()) { out.at(f, x, y) = sample_level(src[0], N); continue; }
                    Vec3 t, b;
                    tangent_frame(N, t, b);
                    Vec3 acc{0, 0, 0};
                    for (const Tap& s : taps) {
                        const Vec3 L = t * s.L.x + b * s.L.y + N * s.L.z;
                        acc = acc + sample_chain(src, L, s.lod) * s.w;
                    }
                    out.at(f, x, y) = acc * inv;
                }
            });
            to_half_cube(out, m.prefiltered.levels[level]);
        }
    }

    // --- LUT da BRDF (split-sum) ------------------------------------------------
    m.brdfLut = brdf_lut(m.lutSize);
}

/// Fecha o conjunto: `above` = níveis do fundo MAIORES que o especular (já em
/// half); `chain` começa no tamanho do especular. `bgSize` 0 = sem fundo.
EnvironmentMaps finish_maps(std::vector<std::vector<u16>> above, u32 aboveTop, const FChain& chain, u32 bgSize,
                            u32 threads) {
    EnvironmentMaps m;
    if (bgSize > 0) {
        m.background.size = above.empty() ? bgSize : aboveTop;
        m.background.levels = std::move(above);
        for (const FCube& c : chain) {
            if (c.size > bgSize) continue;   // fundo menor que o especular (HDRI pequeno)
            m.background.levels.emplace_back();
            to_half_cube(c, m.background.levels.back());
        }
        m.background.mips = static_cast<u32>(m.background.levels.size());
    }
    build_lighting(m, chain, threads);
    return m;
}

u32 pow2_ceil(u32 v) noexcept {
    u32 p = 1;
    while (p < v && p < (1u << 30)) p <<= 1;
    return p;
}

u32 pow2_floor(u32 v) noexcept {
    u32 p = 1;
    while (p * 2 <= v && p < (1u << 30)) p <<= 1;
    return p;
}

// --- Equiretangular -----------------------------------------------------------

/// Pirâmide de caixa da equiretangular (nível 0 = a imagem do chamador, sem
/// cópia; valores não finitos/negativos viram 0). u dá a volta; v fica preso.
struct EqPyramid {
    struct Level { u32 w = 0, h = 0; const f32* px = nullptr; std::vector<f32> own; };
    std::vector<Level> lv;

    static f32 clean(f32 v) noexcept { return v >= 0.0f ? std::min(v, 1.0e7f) : 0.0f; }   // NaN → 0

    [[nodiscard]] Vec3 texel(const Level& l, i32 x, i32 y) const noexcept {
        const i32 w = static_cast<i32>(l.w);
        x = ((x % w) + w) % w;
        y = std::clamp(y, 0, static_cast<i32>(l.h) - 1);
        const f32* p = l.px + (static_cast<usize>(y) * l.w + static_cast<usize>(x)) * 3;
        return Vec3{clean(p[0]), clean(p[1]), clean(p[2])};
    }
    [[nodiscard]] Vec3 bilinear(u32 level, f32 u, f32 v) const noexcept {
        const Level& l = lv[level];
        const f32 x = u * static_cast<f32>(l.w) - 0.5f, y = v * static_cast<f32>(l.h) - 0.5f;
        const f32 fx0 = std::floor(x), fy0 = std::floor(y);
        const i32 x0 = static_cast<i32>(fx0), y0 = static_cast<i32>(fy0);
        const f32 fx = x - fx0, fy = y - fy0;
        const Vec3 a = texel(l, x0, y0) * (1 - fx) + texel(l, x0 + 1, y0) * fx;
        const Vec3 b = texel(l, x0, y0 + 1) * (1 - fx) + texel(l, x0 + 1, y0 + 1) * fx;
        return a * (1 - fy) + b * fy;
    }
    [[nodiscard]] Vec3 trilinear(f32 u, f32 v, f32 lod) const noexcept {
        lod = std::clamp(lod, 0.0f, static_cast<f32>(lv.size() - 1));
        const u32 l0 = static_cast<u32>(lod);
        const f32 t = lod - static_cast<f32>(l0);
        const Vec3 a = bilinear(l0, u, v);
        return t > 1e-4f && l0 + 1 < lv.size() ? a * (1 - t) + bilinear(l0 + 1, u, v) * t : a;
    }

    /// A radiância média na pegada quadrada de lado angular `side` (rad) em
    /// torno de `d`. A pegada é medida em texels do nível 0: em v é fixa
    /// (π/H por linha); em u encolhe com sin θ (os texels perto dos polos são
    /// estreitos). O LOD vem do eixo MENOR (nítido) e o maior leva várias
    /// amostras (anisotropia) — senão os polos borrariam em v.
    [[nodiscard]] Vec3 footprint(Vec3 d, f32 side) const noexcept {
        const Level& l0 = lv[0];
        const f32 W = static_cast<f32>(l0.w), H = static_cast<f32>(l0.h);
        const f32 sinT = std::sqrt(std::max(d.x * d.x + d.z * d.z, 0.0f));
        const f32 u = 0.5f + std::atan2(d.x, -d.z) * (0.5f / kPi);
        const f32 v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
        const f32 fu = side / std::max(2.0f * kPi / W * sinT, 1e-7f);
        const f32 fv = side / (kPi / H);
        const bool alongU = fu >= fv;
        const f32 major = alongU ? fu : fv, minor = alongU ? fv : fu;
        constexpr u32 kMaxTaps = 64;
        const u32 n = std::clamp(static_cast<u32>(std::ceil(major / std::max(minor, 1.0f))), 1u, kMaxTaps);
        const f32 lod = std::log2(std::max({minor, major / static_cast<f32>(kMaxTaps), 1.0f}));
        if (n == 1) return trilinear(u, v, lod);
        Vec3 acc{0, 0, 0};
        const f32 invN = 1.0f / static_cast<f32>(n);
        for (u32 j = 0; j < n; ++j) {
            const f32 t = ((static_cast<f32>(j) + 0.5f) * invN - 0.5f) * major;   // texels do nível 0
            acc = acc + (alongU ? trilinear(u + t / W, v, lod) : trilinear(u, v + t / H, lod));
        }
        return acc * invN;
    }
};

EqPyramid build_pyramid(const f32* rgb, u32 width, u32 height, u32 threads) {
    EqPyramid p;
    p.lv.push_back(EqPyramid::Level{width, height, rgb, {}});
    while (p.lv.back().w > 1 || p.lv.back().h > 1) {
        const EqPyramid::Level& s = p.lv.back();
        EqPyramid::Level n;
        n.w = std::max(1u, s.w / 2);
        n.h = std::max(1u, s.h / 2);
        n.own.resize(static_cast<usize>(n.w) * n.h * 3);
        const bool halfW = s.w > 1, halfH = s.h > 1;
        parallel_rows(threads, n.h, [&](u32 y) noexcept {
            const i32 sy = static_cast<i32>(halfH ? 2 * y : y);
            for (u32 x = 0; x < n.w; ++x) {
                const i32 sx = static_cast<i32>(halfW ? 2 * x : x);
                const Vec3 c = (p.texel(s, sx, sy) + p.texel(s, sx + (halfW ? 1 : 0), sy)
                                + p.texel(s, sx, sy + (halfH ? 1 : 0)) + p.texel(s, sx + (halfW ? 1 : 0), sy + (halfH ? 1 : 0)))
                             * 0.25f;
                f32* o = &n.own[(static_cast<usize>(y) * n.w + x) * 3];
                o[0] = c.x; o[1] = c.y; o[2] = c.z;
            }
        });
        n.px = n.own.data();
        p.lv.push_back(std::move(n));
    }
    return p;
}

/// Um texel do cubo: `sub`×`sub` subamostras, cada uma com a pegada de
/// 1/sub² do ângulo sólido do texel (a pegada é que filtra por área; as
/// subamostras só deixam o filtro mais justo ao quadrado do texel).
Vec3 gather_texel(const EqPyramid& p, u32 face, u32 x, u32 y, u32 size, u32 sub) noexcept {
    const f32 inv = 1.0f / static_cast<f32>(size);
    const f32 uc = 2.0f * (static_cast<f32>(x) + 0.5f) * inv - 1.0f;
    const f32 vc = 2.0f * (static_cast<f32>(y) + 0.5f) * inv - 1.0f;
    const f32 q = 1.0f + uc * uc + vc * vc;
    const f32 texelSA = 4.0f * inv * inv / (q * std::sqrt(q));
    const f32 invSub = 1.0f / static_cast<f32>(sub);
    const f32 side = std::sqrt(texelSA) * invSub;
    Vec3 acc{0, 0, 0};
    for (u32 sy = 0; sy < sub; ++sy) {
        for (u32 sx = 0; sx < sub; ++sx) {
            const f32 fu = uc + ((static_cast<f32>(sx) + 0.5f) * invSub - 0.5f) * 2.0f * inv;
            const f32 fv = vc + ((static_cast<f32>(sy) + 0.5f) * invSub - 0.5f) * 2.0f * inv;
            acc = acc + p.footprint(face_point(face, fu, fv).normalized(), side);
        }
    }
    return acc * (invSub * invSub);
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

u32 clamp_specular(u32 s) noexcept { return std::clamp(pow2_floor(std::max(s, 16u)), 16u, 1024u); }

} // namespace

Vec3 cube_direction(u32 face, u32 x, u32 y, u32 size) noexcept {
    const f32 u = 2.0f * (x + 0.5f) / size - 1.0f;
    const f32 v = 2.0f * (y + 0.5f) / size - 1.0f;
    return face_point(face, u, v).normalized();
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

f32 specular_lod(f32 roughness, u32 mips) noexcept {
    const f32 r = std::clamp(roughness, 0.0f, 1.0f);
    const f32 maxLod = mips > 1 ? static_cast<f32>(mips - 1) : 0.0f;
    return maxLod * r * (1.7f - 0.7f * r);
}

f32 specular_roughness_at(f32 lod, u32 mips) noexcept {
    if (mips <= 1) return 0.0f;
    const f32 x = std::clamp(lod / static_cast<f32>(mips - 1), 0.0f, 1.0f);
    // Inversa de r·(1,7 − 0,7·r) = x.
    return std::clamp((1.7f - std::sqrt(std::max(2.89f - 2.8f * x, 0.0f))) / 1.4f, 0.0f, 1.0f);
}

BackgroundSampling background_sampling(f32 blur, f32 fovY, u32 viewportHeight, u32 backgroundSize,
                                       u32 specularMips) noexcept {
    BackgroundSampling s;
    const f32 b = std::isfinite(blur) ? std::clamp(blur, 0.0f, 1.0f) : 0.0f;
    if (b <= 0.0f || backgroundSize == 0) return s;
    // LOD que o hardware escolhe no centro da tela (pixel vs texel da face).
    const f32 pixelAngle = 2.0f * std::tan(std::clamp(fovY, 1e-3f, 3.1f) * 0.5f) / static_cast<f32>(std::max(viewportHeight, 1u));
    const f32 lodPixel = std::log2(pixelAngle * static_cast<f32>(backgroundSize) * 0.5f);
    // O desfoque como um lóbulo GGX de rugosidade `b`: raio ≈ 2α (rad).
    const f32 radius = 2.0f * b * b;
    const f32 lodBlur = std::log2(std::max(radius, 1e-6f) * static_cast<f32>(backgroundSize) * 0.5f);
    s.gradScale = std::exp2(std::clamp(lodBlur - lodPixel, 0.0f, 16.0f));
    s.specLod = specular_lod(b, specularMips);
    // Mip de caixa muito ampliado fica quadriculado: acima de ~0,05 o
    // especular (GGX, liso) assume.
    const f32 t = std::clamp((b - 0.05f) / 0.15f, 0.0f, 1.0f);
    s.specBlend = t * t * (3.0f - 2.0f * t);
    return s;
}

EnvironmentMaps build_studio_environment(const EnvironmentQuality& q) noexcept {
    const u32 threads = worker_count(q.threads);
    const u32 spec = clamp_specular(q.specularSize);
    const u32 bg = q.backgroundSize ? std::clamp(pow2_floor(q.backgroundSize), 16u, 2048u) : 0u;
    const u32 src = std::max(spec, bg);
    FCube base;
    base.init(src);
    // Superamostra 2×2 por texel: as bordas das caixas de luz não serrilham.
    parallel_rows(threads, 6 * src, [&](u32 row) noexcept {
        const u32 f = row / src, y = row % src;
        for (u32 x = 0; x < src; ++x) {
            Vec3 acc{0, 0, 0};
            for (u32 sy = 0; sy < 2; ++sy) {
                for (u32 sx = 0; sx < 2; ++sx) {
                    acc = acc + studio_radiance(cube_direction(f, x * 2 + sx, y * 2 + sy, src * 2));
                }
            }
            base.at(f, x, y) = acc * 0.25f;
        }
    });
    std::vector<std::vector<u16>> above;
    while (base.size > spec) {
        above.emplace_back();
        to_half_cube(base, above.back());
        base = downsample(base, threads);
    }
    return finish_maps(std::move(above), src, build_chain(std::move(base), threads), bg, threads);
}

EnvironmentMaps build_studio_environment(u32 baseSize) noexcept {
    return build_studio_environment(EnvironmentQuality{baseSize, 0u, 0u});
}

EnvironmentMaps build_environment_from_equirect(const f32* rgb, u32 width, u32 height,
                                                const EnvironmentQuality& q) noexcept {
    if (!rgb || width == 0 || height == 0) return build_studio_environment(q);
    const u32 threads = worker_count(q.threads);
    // Tetos pela resolução da fonte: uma face de W/4 já tem a densidade do
    // HDRI (90° de 360°); ampliar além disso só gasta memória.
    const u32 native = std::max(128u, pow2_ceil(std::max(width / 4, 1u)));
    const u32 spec = std::min(clamp_specular(q.specularSize), native);
    const u32 bg = q.backgroundSize ? std::min(std::clamp(pow2_floor(q.backgroundSize), 16u, 2048u), native) : 0u;
    const u32 src = std::max(spec, bg);
    const EqPyramid pyr = build_pyramid(rgb, width, height, threads);
    std::vector<std::vector<u16>> above;
    FCube base;
    if (src > spec) {
        // O fundo é maior que o especular: o nível de cima vai direto para
        // half (é o que sobe para a GPU) — sem uma cópia float de 75 MB.
        std::vector<u16> top(static_cast<usize>(src) * src * 6 * 4);
        parallel_rows(threads, 6 * src, [&](u32 row) noexcept {
            const u32 f = row / src, y = row % src;
            u16* o = top.data() + (static_cast<usize>(f) * src * src + static_cast<usize>(y) * src) * 4;
            for (u32 x = 0; x < src; ++x, o += 4) {
                const Vec3 c = gather_texel(pyr, f, x, y, src, 1);   // só fundo: a GPU ainda filtra
                o[0] = float_to_half(c.x); o[1] = float_to_half(c.y); o[2] = float_to_half(c.z); o[3] = float_to_half(1.0f);
            }
        });
        base = downsample_half(top, src, threads);
        above.push_back(std::move(top));
        while (base.size > spec) {
            above.emplace_back();
            to_half_cube(base, above.back());
            base = downsample(base, threads);
        }
    } else {
        base.init(src);
        parallel_rows(threads, 6 * src, [&](u32 row) noexcept {
            const u32 f = row / src, y = row % src;
            for (u32 x = 0; x < src; ++x) base.at(f, x, y) = gather_texel(pyr, f, x, y, src, 2);
        });
    }
    return finish_maps(std::move(above), src, build_chain(std::move(base), threads), bg, threads);
}

EnvironmentMaps build_environment_from_equirect(const f32* rgb, u32 width, u32 height, u32 baseSize) noexcept {
    return build_environment_from_equirect(rgb, width, height, EnvironmentQuality{baseSize, 0u, 0u});
}

} // namespace aurea::scene3d
