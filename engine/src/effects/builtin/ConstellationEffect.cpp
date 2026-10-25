// =============================================================================
//  Aurea / effects / builtin / ConstellationEffect.cpp
//
//  Constelação (3D, 0.3.0): pontos no espaço 3D ligados por linhas quando
//  estão perto, com faces (triângulos) entre trios ligados, deformados por um
//  ruído e por um campo esférico e vistos pela câmera 3D da composição. O
//  comportamento segue o de um gerador de rede de pontos do After Effects
//  (especificado pela documentação pública dele: geometria → efetores →
//  renderizadores). A implementação é própria: nenhum código, shader ou asset
//  de terceiros.
//
//  Etapas (tudo determinístico pela semente: o mesmo quadro dá a mesma imagem
//  no preview, no scrubbing e no export):
//   1. GEOMETRIA (planejamento, CPU): nuvem numa caixa, esfera, grade X×Y×Z,
//      anel/toro, plano, ou o contorno de outra camada (pontos à mesma
//      distância, `EffectResources::repeat_path`). Depois a desordem, a escala
//      e a rotação (mais o giro por segundo) em volta do centro.
//   2. EFETORES: ruído de gradiente 4D próprio (x, y, z e a evolução no
//      tempo; um canal por eixo) e o campo esférico, que empurra (força > 0)
//      ou puxa (< 0) com queda suave até o raio.
//   3. VIZINHOS: grade espacial de hash e busca em anéis dos k mais próximos
//      dentro da distância máxima; a rede é a união. Triângulo = trio cujos
//      três lados estão na rede (e cabem na distância dos triângulos).
//   4. CÂMERA: com câmera ATIVA na composição (e "Seguir a câmera 3D"), a
//      vista e o campo de visão dela — a camada 2D é o plano z = 0 do mundo,
//      pela matriz da composição. Sem câmera (ou camada 3D/na cena), uma
//      câmera própria de "Campo de visão" (50°) olhando o centro da camada, à
//      distância em que o plano z = 0 sai 1:1 em px da camada.
//   5. GPU: uma textura de DADO RGBA32F (pontos já projetados, linhas,
//      faces), um passe de geometria sem vertex buffer (constellation.vert) e
//      o passe que põe a rede sobre a camada (constellation_compose.frag).
//
//  Convenção do espaço: px da camada; x para a direita, y para BAIXO, z para
//  LONGE da câmera (como o mundo 3D da composição). z = 0 é o plano da camada;
//  z negativo vem para perto. Rotações em graus no sentido do After Effects
//  (Z positivo = horário na tela); a ordem é X, depois Y, depois Z.
//
//  Unidades guardadas: tamanhos da geometria em % da camada (largura em % da
//  largura; altura, profundidade e raios em % da altura), distâncias em px da
//  camada, profundidade da névoa/cor em % da distância do plano 1:1 da câmera.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace aurea::builtin {
namespace {

constexpr u16 kAnim = kParamAnimatable;
constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;
constexpr u16 kAnimRel = kParamAnimatable | kParamRelative;

/// Tetos: memória, custo por quadro no aparelho e o tamanho da textura de dado.
constexpr u32 kMaxPoints = 2000;
constexpr u32 kMaxLines = 12000;
constexpr u32 kMaxTriangles = 4000;
constexpr u32 kMaxNeighbors = 32;
constexpr u32 kDataWidth = 1024;   ///< texels por linha da textura de dado

enum Param : u32 {
    // Pontos (geometria)
    kSource = 0, kCount, kSeed, kWidth, kHeight, kDepth, kRadius, kTubeRadius,
    kGridX, kGridY, kGridZ, kPathLayer, kJitter,
    // Transformação
    kCenter, kPositionZ, kScale, kRotationX, kRotationY, kRotationZ, kSpinX, kSpinY, kSpinZ,
    // Ruído
    kNoiseOn, kNoiseAmount, kNoiseScale, kNoiseSpeed, kNoiseOctaves,
    // Campo esférico
    kFieldOn, kFieldCenter, kFieldZ, kFieldRadius, kFieldStrength,
    // Linhas
    kLinesOn, kLineMaxDistance, kLineMinDistance, kConnections, kLineWidth, kLineOpacity,
    kLineFalloff, kLineColorMode, kLineColor, kLinePerspective,
    // Desenho dos pontos
    kPointsOn, kPointSize, kPointOpacity, kPointColor, kPointPerspective, kPointSoftness, kOnlyConnected,
    // Triângulos
    kTrianglesOn, kTriangleMaxDistance, kTriangleOpacity, kTriangleColorMode, kTriangleColor,
    // Profundidade (fora de seção)
    kDepthFog, kDepthNear, kDepthFar, kDepthColor, kNearColor, kFarColor,
    // Geral
    kOpacity, kBlend, kShowLayer, kFollowCamera, kFov,
    kParamCount
};

enum Source : u32 { kCloud = 0, kSphere, kGrid, kRing, kPlane, kPath, kSourceCount };

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
f32 clampf(f32 v, f32 lo, f32 hi, f32 fallback) noexcept { return std::clamp(finite_or(v, fallback), lo, hi); }
f32 smooth01(f32 t) noexcept { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
f32 smoothstepf(f32 a, f32 b, f32 x) noexcept { return b > a ? smooth01((x - a) / (b - a)) : (x >= a ? 1.0f : 0.0f); }

/// Cor do seletor (sRGB) → linear, como os outros efeitos novos.
Vec3 linear_rgb(Vec4 c) noexcept {
    return Vec3{Color::srgb_to_linear(std::clamp(finite_or(c.x, 1.0f), 0.0f, 1.0f)),
                Color::srgb_to_linear(std::clamp(finite_or(c.y, 1.0f), 0.0f, 1.0f)),
                Color::srgb_to_linear(std::clamp(finite_or(c.z, 1.0f), 0.0f, 1.0f))};
}

// --- Sorteio e ruído ----------------------------------------------------------
// Hash inteiro (mistura multiplicação/xor): o mesmo valor em todo aparelho,
// sem tabela e sem estado.
u32 mix32(u32 h) noexcept {
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

/// 0..1 de (semente, ponto, canal). O ponto i é o mesmo com mais ou menos
/// pontos: animar a quantidade acrescenta pontos, não embaralha a nuvem.
f32 rnd(u32 seed, u32 i, u32 channel) noexcept {
    const u32 h = mix32(mix32(i * 0x9E3779B9u ^ (seed + 1u) * 0x85EBCA6Bu) ^ (channel + 1u) * 0xC2B2AE35u);
    return static_cast<f32>(h >> 8) * (1.0f / 16777216.0f);
}

f32 fade(f32 t) noexcept { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

/// Os 32 gradientes 4D do ruído (um eixo zero, os outros ±1), em tabela: o
/// laço dos cantos fica sem desvio (o `switch` por canto custava o triplo).
struct Grad4Table { f32 g[32][4]; };
constexpr Grad4Table make_grad4() noexcept {
    Grad4Table t{};
    for (u32 g = 0; g < 32; ++g) {
        const f32 sign[3] = {(g & 1u) ? -1.0f : 1.0f, (g & 2u) ? -1.0f : 1.0f, (g & 4u) ? -1.0f : 1.0f};
        const u32 zero = g >> 3;
        u32 next = 0;
        for (u32 axis = 0; axis < 4; ++axis) t.g[g][axis] = axis == zero ? 0.0f : sign[next++];
    }
    return t;
}
constexpr Grad4Table kGrad4 = make_grad4();

/// Ruído de gradiente 4D (x, y, z no espaço; w = evolução), três canais de uma
/// vez (o deslocamento X, Y e Z): os mesmos cantos e pesos, e UM hash por
/// canto — 5 bits dele escolhem o gradiente de cada canal.
void gradient_noise3(f32 x, f32 y, f32 z, f32 w, u32 salt, f32 out[3]) noexcept {
    const f32 x0 = std::floor(x), y0 = std::floor(y), z0 = std::floor(z), w0 = std::floor(w);
    const u32 ix = static_cast<u32>(static_cast<i32>(x0)), iy = static_cast<u32>(static_cast<i32>(y0));
    const u32 iz = static_cast<u32>(static_cast<i32>(z0)), iw = static_cast<u32>(static_cast<i32>(w0));
    const f32 fx = x - x0, fy = y - y0, fz = z - z0, fw = w - w0;
    const f32 wx[2] = {1.0f - fade(fx), fade(fx)}, wy[2] = {1.0f - fade(fy), fade(fy)};
    const f32 wz[2] = {1.0f - fade(fz), fade(fz)}, ww[2] = {1.0f - fade(fw), fade(fw)};
    f32 o0 = 0.0f, o1 = 0.0f, o2 = 0.0f;
    for (u32 c = 0; c < 16; ++c) {
        const u32 dx = c & 1u, dy = (c >> 1) & 1u, dz = (c >> 2) & 1u, dw = (c >> 3) & 1u;
        const f32 weight = wx[dx] * wy[dy] * wz[dz] * ww[dw];
        const u32 h = mix32(((ix + dx) * 0x8DA6B343u + (iy + dy) * 0xD8163841u + (iz + dz) * 0xCB1AB31Fu
                             + (iw + dw) * 0x165667B1u) ^ salt);
        const f32 rx = fx - static_cast<f32>(dx), ry = fy - static_cast<f32>(dy);
        const f32 rz = fz - static_cast<f32>(dz), rw = fw - static_cast<f32>(dw);
        const f32* g0 = kGrad4.g[h & 31u];
        const f32* g1 = kGrad4.g[(h >> 5) & 31u];
        const f32* g2 = kGrad4.g[(h >> 10) & 31u];
        o0 += weight * (g0[0] * rx + g0[1] * ry + g0[2] * rz + g0[3] * rw);
        o1 += weight * (g1[0] * rx + g1[1] * ry + g1[2] * rz + g1[3] * rw);
        o2 += weight * (g2[0] * rx + g2[1] * ry + g2[2] * rz + g2[3] * rw);
    }
    out[0] = o0; out[1] = o1; out[2] = o2;
}

/// Soma de oitavas (frequência ×2, amplitude ×½), normalizada; cada oitava
/// com um deslocamento próprio para não alinhar as grades na origem.
void fbm3(Vec3 q, f32 w, u32 octaves, u32 salt, f32 out[3]) noexcept {
    f32 acc[3] = {0.0f, 0.0f, 0.0f};
    f32 amp = 1.0f, norm = 0.0f, freq = 1.0f;
    for (u32 o = 0; o < octaves; ++o) {
        f32 n[3];
        const f32 off = static_cast<f32>(o) * 17.31f;
        gradient_noise3(q.x * freq + off, q.y * freq - off * 0.7f, q.z * freq + off * 1.3f, w * freq,
                        salt + o * 0x3C6EF372u, n);
        for (u32 ch = 0; ch < 3; ++ch) acc[ch] += amp * n[ch];
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    // ~[-1, 1]: o desvio do gradiente 4D é ~0,35; ×1,4 deixa o típico perto
    // da metade da amplitude e o pico nela.
    for (u32 ch = 0; ch < 3; ++ch) out[ch] = std::clamp(norm > 0.0f ? 1.4f * acc[ch] / norm : 0.0f, -1.0f, 1.0f);
}

// --- Rotação ----------------------------------------------------------------
struct Rot3 { f32 m[9]; };

/// R = Rz · Ry · Rx (gira em X, depois em Y, depois em Z), em radianos.
Rot3 rotation_xyz(f32 ax, f32 ay, f32 az) noexcept {
    const f32 cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    const f32 cz = std::cos(az), sz = std::sin(az);
    // Rx: y' = y cx − z sx, z' = y sx + z cx; Ry: x' = x cy + z sy, z' = −x sy + z cy;
    // Rz: x' = x cz − y sz, y' = x sz + y cz (y para baixo: horário na tela).
    Rot3 r;
    r.m[0] = cz * cy;  r.m[1] = cz * sy * sx - sz * cx;  r.m[2] = cz * sy * cx + sz * sx;
    r.m[3] = sz * cy;  r.m[4] = sz * sy * sx + cz * cx;  r.m[5] = sz * sy * cx - cz * sx;
    r.m[6] = -sy;      r.m[7] = cy * sx;                 r.m[8] = cy * cx;
    return r;
}
Vec3 apply(const Rot3& r, Vec3 v) noexcept {
    return Vec3{r.m[0] * v.x + r.m[1] * v.y + r.m[2] * v.z,
                r.m[3] * v.x + r.m[4] * v.y + r.m[5] * v.z,
                r.m[6] * v.x + r.m[7] * v.y + r.m[8] * v.z};
}

/// Ângulo (graus) parado + giro (graus/s) × tempo, reduzido a uma volta em
/// f64 antes de virar f32 (um projeto longo não perde precisão).
f32 spin_angle(f32 baseDeg, f32 degPerSecond, f64 seconds) noexcept {
    const f64 deg = static_cast<f64>(baseDeg) + static_cast<f64>(degPerSecond) * seconds;
    const f64 wrapped = deg - 360.0 * std::floor(deg / 360.0);
    return static_cast<f32>(wrapped) * kDeg2Rad;
}

// --- Câmera -----------------------------------------------------------------
/// Projeção de um ponto (px da camada, z para longe) para px da camada na
/// tela. `ratio` = distância ao longo do eixo da câmera ÷ distância do plano
/// 1:1 (a perspectiva é 1/ratio).
struct Camera {
    bool comp = false;               ///< câmera da composição (senão a própria)
    // Própria: centro (px da camada) e distância focal F.
    f32 cx = 0.0f, cy = 0.0f, F = 1.0f;
    // Composição: camada → mundo (homografia 2D + escala em z), vista, foco.
    f64 h[9]{};                      ///< comp ← camada (linhas)
    f64 hinv[9]{};                   ///< camada ← comp
    f32 zScale = 1.0f;
    Mat4 view = Mat4::identity();
    f32 focal = 1.0f;                ///< px da composição (meia altura / tan(fov/2))
    f32 compW = 0.0f, compH = 0.0f;
    // 0.3.1, mundo (camada 3D como o Particular): camada → mundo → câmera da
    // cena → px da composição → px da folha chapada.
    bool world = false;
    Mat4 worldFromLayer = Mat4::identity(), compFromWorld = Mat4::identity(), layerFromComp = Mat4::identity();

    bool project(Vec3 p, f32& sx, f32& sy, f32& ratio) const noexcept {
        if (world) {
            const Vec4 wp = worldFromLayer * Vec4{p.x, p.y, p.z, 1.0f};
            const Vec4 v = view * wp;
            ratio = v.z / focal;
            if (!(ratio > 0.01f)) return false;
            const Vec4 q = compFromWorld * wp;
            if (!(std::fabs(q.w) > 1e-12f)) return false;
            const Vec4 l = layerFromComp * Vec4{q.x / q.w, q.y / q.w, 0.0f, 1.0f};
            if (!(std::fabs(l.w) > 1e-12f)) return false;
            sx = l.x / l.w;
            sy = l.y / l.w;
            return std::isfinite(sx) && std::isfinite(sy);
        }
        if (!comp) {
            const f32 d = F + p.z;
            ratio = d / F;
            if (!(ratio > 0.01f)) return false;
            const f32 k = F / d;
            sx = cx + (p.x - cx) * k;
            sy = cy + (p.y - cy) * k;
            return std::isfinite(sx) && std::isfinite(sy);
        }
        const f64 X = h[0] * p.x + h[1] * p.y + h[2];
        const f64 Y = h[3] * p.x + h[4] * p.y + h[5];
        const f64 W = h[6] * p.x + h[7] * p.y + h[8];
        if (!(std::fabs(W) > 1e-12)) return false;
        const Vec4 v = view * Vec4{static_cast<f32>(X / W), static_cast<f32>(Y / W), p.z * zScale, 1.0f};
        ratio = v.z / focal;
        if (!(ratio > 0.01f)) return false;
        const f64 qx = 0.5 * compW + static_cast<f64>(focal) * v.x / v.z;
        const f64 qy = 0.5 * compH + static_cast<f64>(focal) * v.y / v.z;
        const f64 lx = hinv[0] * qx + hinv[1] * qy + hinv[2];
        const f64 ly = hinv[3] * qx + hinv[4] * qy + hinv[5];
        const f64 lw = hinv[6] * qx + hinv[7] * qy + hinv[8];
        if (!(std::fabs(lw) > 1e-12)) return false;
        sx = static_cast<f32>(lx / lw);
        sy = static_cast<f32>(ly / lw);
        return std::isfinite(sx) && std::isfinite(sy);
    }
};

bool invert3(const f64 m[9], f64 out[9]) noexcept {
    const f64 a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], k = m[8];
    const f64 A = e * k - f * h, B = -(d * k - f * g), C = d * h - e * g;
    const f64 det = a * A + b * B + c * C;
    if (!(std::fabs(det) > 1e-18) || !std::isfinite(det)) return false;
    const f64 inv = 1.0 / det;
    out[0] = A * inv; out[1] = -(b * k - c * h) * inv; out[2] = (b * f - c * e) * inv;
    out[3] = B * inv; out[4] = (a * k - c * g) * inv;  out[5] = -(a * f - c * d) * inv;
    out[6] = C * inv; out[7] = -(a * h - b * g) * inv; out[8] = (a * e - b * d) * inv;
    return true;
}

// --- Vizinhos ---------------------------------------------------------------
struct Edge { u32 a, b; f32 d; };

/// Teto de células da grade densa (contagem por célula). Com uma caixa que
/// pediria mais, as células crescem: a busca em anéis continua exata.
constexpr u32 kMaxCells = 1u << 16;

/// Tudo o que o planejamento usa, reaproveitado entre quadros (sem alocar a
/// cada quadro na reprodução).
struct Workspace {
    std::vector<Vec3> pos;
    std::vector<Vec4> screen;      ///< x, y, perspectiva, alfa (0 = invisível)
    std::vector<f32> ratio;
    std::vector<u32> cellStart;    ///< início de cada célula em `order` (+1 no fim)
    std::vector<u32> cellOf;       ///< célula linear de cada ponto
    std::vector<i32> cellXYZ;      ///< 3 por ponto
    std::vector<u32> order;        ///< pontos ordenados por célula
    std::vector<Vec3> sorted;      ///< posições na ordem de `order` (leitura contígua)
    std::vector<u32> nbr, nbrCount;
    std::vector<f32> nbrD;
    std::vector<Edge> edges;
    std::vector<u32> adjStart, adj, cursor;
    std::vector<Vec4> lines, tris;
    std::vector<u8> connected;
    std::vector<Vec4> data;
};

Workspace& workspace() {
    thread_local Workspace ws;
    return ws;
}

/// k mais próximos (distância em [minD, maxD]) de cada ponto, por anéis de
/// células de lado `h` em volta da célula do ponto, numa grade densa montada
/// por contagem (sem ordenar). A rede é a união dos pares: (i, j) sai uma vez,
/// pelo menor índice quando os dois se escolheram. Ordem determinística (por
/// ponto, do vizinho mais perto ao mais longe).
void build_graph(Workspace& ws, f32 maxD, f32 minD, u32 k) {
    ws.edges.clear();
    const u32 n = static_cast<u32>(ws.pos.size());
    if (n < 2 || !(maxD >= 0.5f) || k == 0) return;
    Vec3 lo = ws.pos[0], hi = ws.pos[0];
    for (const Vec3& p : ws.pos) {
        lo = Vec3{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = Vec3{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    // Lado da célula: ~2 espaçamentos médios (estimado pela caixa), no máximo
    // a distância máxima e no mínimo 1/6 dela — poucos anéis para achar k.
    const f32 floorDim = std::max(maxD * 0.1f, 1e-3f);
    const f64 vol = static_cast<f64>(std::max(hi.x - lo.x, floorDim)) * std::max(hi.y - lo.y, floorDim)
                  * std::max(hi.z - lo.z, floorDim);
    const f32 spacing = static_cast<f32>(std::cbrt(vol / n));
    f32 h = maxD / std::clamp(std::floor(maxD / std::max(2.0f * spacing, 1e-3f)), 1.0f, 6.0f);
    u32 g[3] = {1, 1, 1};
    const f32 ext[3] = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    for (u32 attempt = 0;; ++attempt) {
        f64 cells = 1.0;
        for (u32 a = 0; a < 3; ++a) {
            g[a] = static_cast<u32>(std::min(std::floor(ext[a] / h), 65535.0f)) + 1u;
            cells *= g[a];
        }
        if (cells <= kMaxCells) break;
        // Caixa grande demais para a grade: células maiores (e mais anéis).
        h *= attempt < 6 ? static_cast<f32>(std::cbrt(cells / kMaxCells)) * 1.01f
                         : 2.0f;
    }
    // Anéis até r com (r − 1)·h < maxD; a folga evita um anel a mais por
    // arredondamento quando h = maxD / inteiro.
    const u32 m = std::max(1u, static_cast<u32>(std::ceil(maxD / h - 1e-4f)));
    const u32 cells = g[0] * g[1] * g[2];
    ws.cellStart.assign(static_cast<usize>(cells) + 1, 0);
    ws.cellOf.resize(n);
    ws.cellXYZ.resize(static_cast<usize>(n) * 3);
    for (u32 i = 0; i < n; ++i) {
        const Vec3 p = ws.pos[i];
        const f32 rel[3] = {p.x - lo.x, p.y - lo.y, p.z - lo.z};
        i32 c[3];
        for (u32 a = 0; a < 3; ++a)
            c[a] = static_cast<i32>(std::clamp(std::floor(rel[a] / h), 0.0f, static_cast<f32>(g[a] - 1)));
        ws.cellXYZ[i * 3] = c[0]; ws.cellXYZ[i * 3 + 1] = c[1]; ws.cellXYZ[i * 3 + 2] = c[2];
        const u32 cell = static_cast<u32>(c[0]) + g[0] * (static_cast<u32>(c[1]) + g[1] * static_cast<u32>(c[2]));
        ws.cellOf[i] = cell;
        ++ws.cellStart[cell + 1];
    }
    for (u32 c = 0; c < cells; ++c) ws.cellStart[c + 1] += ws.cellStart[c];
    ws.cursor.assign(ws.cellStart.begin(), ws.cellStart.end() - 1);
    ws.order.resize(n);
    ws.sorted.resize(n);
    for (u32 i = 0; i < n; ++i) {
        const u32 at = ws.cursor[ws.cellOf[i]]++;
        ws.order[at] = i;
        ws.sorted[at] = ws.pos[i];
    }

    const f32 max2 = maxD * maxD, min2 = minD * minD;
    ws.nbr.resize(static_cast<usize>(n) * k);
    ws.nbrD.resize(static_cast<usize>(n) * k);
    ws.nbrCount.assign(n, 0);
    const i32 gx = static_cast<i32>(g[0]), gy = static_cast<i32>(g[1]), gz = static_cast<i32>(g[2]);
    for (u32 i = 0; i < n; ++i) {
        const Vec3 p = ws.pos[i];
        const i32 cx = ws.cellXYZ[i * 3], cy = ws.cellXYZ[i * 3 + 1], cz = ws.cellXYZ[i * 3 + 2];
        f32* bestD = &ws.nbrD[static_cast<usize>(i) * k];
        u32* bestJ = &ws.nbr[static_cast<usize>(i) * k];
        u32 found = 0;
        for (u32 r = 0; r <= m; ++r) {
            const i32 ri = static_cast<i32>(r);
            for (i32 dz = -ri; dz <= ri; ++dz) {
                const i32 z = cz + dz;
                if (z < 0 || z >= gz) continue;
                for (i32 dy = -ri; dy <= ri; ++dy) {
                    const i32 y = cy + dy;
                    if (y < 0 || y >= gy) continue;
                    // Só a casca do anel: no miolo, os dois extremos de x.
                    const bool shell = std::abs(dz) == ri || std::abs(dy) == ri;
                    const i32 step = shell || ri == 0 ? 1 : 2 * ri;
                    const u32 row = static_cast<u32>(gx) * (static_cast<u32>(y) + static_cast<u32>(gy) * static_cast<u32>(z));
                    for (i32 dx = -ri; dx <= ri; dx += step) {
                        const i32 x = cx + dx;
                        if (x < 0 || x >= gx) continue;
                        const u32 cell = row + static_cast<u32>(x);
                        const u32 end = ws.cellStart[cell + 1];
                        for (u32 q = ws.cellStart[cell]; q < end; ++q) {
                            const Vec3 d = ws.sorted[q] - p;
                            const f32 d2 = d.x * d.x + d.y * d.y + d.z * d.z;
                            if (d2 > max2 || d2 < min2) continue;
                            const u32 j = ws.order[q];
                            if (j == i) continue;
                            if (found == k && (d2 > bestD[k - 1] || (d2 == bestD[k - 1] && j > bestJ[k - 1]))) continue;
                            u32 at = found < k ? found++ : k - 1;
                            while (at > 0 && (bestD[at - 1] > d2 || (bestD[at - 1] == d2 && bestJ[at - 1] > j))) {
                                bestD[at] = bestD[at - 1];
                                bestJ[at] = bestJ[at - 1];
                                --at;
                            }
                            bestD[at] = d2;
                            bestJ[at] = j;
                        }
                    }
                }
            }
            // O anel seguinte só tem pontos a ≥ r·h: com k achados mais perto
            // que isso, a busca acabou.
            const f32 reach = static_cast<f32>(r) * h;
            if (found == k && bestD[k - 1] <= reach * reach) break;
        }
        ws.nbrCount[i] = found;
    }
    // União sem ordenar: (i, j) com i < j sai por i; com j < i, só se i não
    // está na lista de j (senão j já o emitiu).
    for (u32 i = 0; i < n; ++i) {
        const u32* list = &ws.nbr[static_cast<usize>(i) * k];
        for (u32 q = 0; q < ws.nbrCount[i]; ++q) {
            const u32 j = list[q];
            if (j < i) {
                const u32* other = &ws.nbr[static_cast<usize>(j) * k];
                bool mutual = false;
                for (u32 t = 0; t < ws.nbrCount[j] && !mutual; ++t) mutual = other[t] == i;
                if (mutual) continue;
            }
            ws.edges.push_back(Edge{std::min(i, j), std::max(i, j), std::sqrt(ws.nbrD[static_cast<usize>(i) * k + q])});
        }
    }
}

/// O que o planejamento entrega à montagem (em `auxInfo`).
struct PlanCounts { u32 points = 0, lines = 0, triangles = 0; };

class Constellation final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kConstellation, "Constelação", "3D", EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        // Pontos (geometria)
        static const char* const kSources[] = {"Nuvem", "Esfera", "Grade", "Anel", "Plano", "Caminho da camada"};
        p.add_enum("source", "Pontos de", kSources, kSourceCount, kCloud);
        p.add_int("count", "Quantidade de pontos", 200, 2, static_cast<i32>(kMaxPoints));
        p.add_int("seed", "Semente", 0, 0, 9999);
        p.add_float("width", "Largura", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("height", "Altura", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("depth", "Profundidade", 60.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("radius", "Raio", 40.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("tube_radius", "Raio do tubo", 12.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_int("grid_x", "Colunas da grade", 10, 1, 50);
        p.add_int("grid_y", "Linhas da grade", 6, 1, 50);
        p.add_int("grid_z", "Camadas da grade", 3, 1, 20);
        p.add_layer_ref("path_layer", "Camada do caminho");
        p.add_float("jitter", "Desordem", 0.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 5000.0f);
        // Transformação
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.add_float("position_z", "Posição Z", 0.0f, -2000.0f, 2000.0f, kAnimPx, "px");
        p.typed_range(-20000.0f, 20000.0f);
        p.add_float("scale", "Escala", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 4000.0f);
        p.add_angle("rotation_x", "Rotação X", 0.0f);
        p.add_angle("rotation_y", "Rotação Y", 0.0f);
        p.add_angle("rotation_z", "Rotação Z", 0.0f);
        p.add_float("spin_x", "Giro X", 0.0f, -180.0f, 180.0f, kAnim, "°/s");
        p.typed_range(-3600.0f, 3600.0f);
        p.add_float("spin_y", "Giro Y", 6.0f, -180.0f, 180.0f, kAnim, "°/s");
        p.typed_range(-3600.0f, 3600.0f);
        p.add_float("spin_z", "Giro Z", 0.0f, -180.0f, 180.0f, kAnim, "°/s");
        p.typed_range(-3600.0f, 3600.0f);
        // Ruído
        p.add_bool("noise", "Ruído", true);
        p.add_float("noise_amount", "Amplitude do ruído", 30.0f, 0.0f, 400.0f, kAnimPx, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("noise_scale", "Escala do ruído", 300.0f, 10.0f, 2000.0f, kAnimPx, "px");
        p.typed_range(1.0f, 20000.0f);
        p.add_float("noise_speed", "Evolução do ruído", 0.3f, 0.0f, 3.0f, kAnim, "/s");
        p.typed_range(0.0f, 100.0f);
        p.add_int("noise_octaves", "Oitavas do ruído", 2, 1, 4);
        // Campo esférico
        p.add_bool("field", "Campo esférico", false);
        p.add_point2("field_center", "Centro do campo", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.add_float("field_z", "Z do campo", 0.0f, -2000.0f, 2000.0f, kAnimPx, "px");
        p.typed_range(-20000.0f, 20000.0f);
        p.add_float("field_radius", "Raio do campo", 250.0f, 0.0f, 2000.0f, kAnimPx, "px");
        p.typed_range(0.0f, 20000.0f);
        p.add_float("field_strength", "Força do campo", 120.0f, -1000.0f, 1000.0f, kAnimPx, "px");
        p.typed_range(-10000.0f, 10000.0f);
        // Linhas
        static const char* const kColorFrom[] = {"Cor própria", "Cor dos pontos"};
        p.add_bool("lines", "Linhas", true);
        p.add_float("line_max_distance", "Distância máxima", 180.0f, 0.0f, 1000.0f, kAnimPx, "px");
        p.typed_range(0.0f, 10000.0f);
        p.add_float("line_min_distance", "Distância mínima", 0.0f, 0.0f, 1000.0f, kAnimPx, "px");
        p.typed_range(0.0f, 10000.0f);
        p.add_int("max_connections", "Conexões por ponto", 6, 1, static_cast<i32>(kMaxNeighbors));
        p.add_float("line_width", "Espessura da linha", 1.5f, 0.1f, 20.0f, kAnimPx, "px");
        p.typed_range(0.1f, 200.0f);
        p.add_float("line_opacity", "Opacidade das linhas", 70.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("line_falloff", "Queda pela distância", 1.0f, 0.0f, 4.0f, kAnim);
        p.typed_range(0.0f, 16.0f);
        p.add_enum("line_color_mode", "Colorir linhas com", kColorFrom, 2, 1);
        p.add_color("line_color", "Cor das linhas", Vec4{0.55f, 0.80f, 1.0f, 1.0f});
        p.add_bool("line_perspective", "Espessura com perspectiva", true);
        // Desenho dos pontos
        p.add_bool("points", "Desenhar pontos", true);
        p.add_float("point_size", "Tamanho do ponto", 5.0f, 0.0f, 60.0f, kAnimPx, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("point_opacity", "Opacidade dos pontos", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("point_color", "Cor dos pontos", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_bool("point_perspective", "Tamanho com perspectiva", true);
        p.add_float("point_softness", "Suavidade dos pontos", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("only_connected", "Só pontos conectados", false);
        // Triângulos
        p.add_bool("triangles", "Triângulos", false);
        p.add_float("triangle_max_distance", "Distância máxima dos triângulos", 160.0f, 0.0f, 1000.0f, kAnimPx, "px");
        p.typed_range(0.0f, 10000.0f);
        p.add_float("triangle_opacity", "Opacidade dos triângulos", 20.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("triangle_color_mode", "Colorir triângulos com", kColorFrom, 2, 1);
        p.add_color("triangle_color", "Cor dos triângulos", Vec4{0.35f, 0.60f, 1.0f, 1.0f});
        // Profundidade: % da distância em que o plano z = 0 sai 1:1.
        p.add_bool("depth_fog", "Névoa de profundidade", false);
        p.add_float("depth_near", "Profundidade perto", 80.0f, 0.0f, 500.0f, kAnimPct, "%");
        p.typed_range(0.0f, 10000.0f);
        p.add_float("depth_far", "Profundidade longe", 200.0f, 0.0f, 1000.0f, kAnimPct, "%");
        p.typed_range(0.0f, 10000.0f);
        p.add_bool("depth_color", "Cor pela profundidade", false);
        p.add_color("near_color", "Cor perto", Vec4{1.0f, 0.85f, 0.55f, 1.0f});
        p.add_color("far_color", "Cor longe", Vec4{0.25f, 0.45f, 1.0f, 1.0f});
        // Geral
        static const char* const kBlends[] = {"Sobre", "Somar"};
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("blend", "Mistura", kBlends, 2, 1);
        p.add_bool("show_layer", "Mostrar camada", true);
        p.add_bool("follow_camera", "Seguir a câmera 3D", true);
        p.add_float("fov", "Campo de visão", 50.0f, 10.0f, 120.0f, kAnim, "°");

        // Sete seções (o teto): o interruptor de cada efetor/desenho vira o
        // cabeçalho. Profundidade e os gerais ficam como linhas soltas.
        p.add_section_range("points", "Pontos", kSource, kJitter);
        p.add_section_range("transform", "Transformação", kCenter, kSpinZ);
        p.add_section_range("noise", "Ruído", kNoiseAmount, kNoiseOctaves, kNoiseOn);
        p.add_section_range("field", "Campo esférico", kFieldCenter, kFieldStrength, kFieldOn, false);
        p.add_section_range("lines", "Linhas", kLineMaxDistance, kLinePerspective, kLinesOn);
        p.add_section_range("point_look", "Desenho dos pontos", kPointSize, kOnlyConnected, kPointsOn);
        p.add_section_range("triangles", "Triângulos", kTriangleMaxDistance, kTriangleColor, kTrianglesOn, false);
    }

    // Prévia do catálogo (cartão de 160×100 e menores): menos pontos e
    // distâncias curtas, para a rede caber e aparecer.
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        if (v.size() < kParamCount) return false;
        v[kCount] = ParamValue::scalar(80.0f);
        v[kLineMaxDistance] = ParamValue::scalar(60.0f);
        v[kLineWidth] = ParamValue::scalar(1.2f);
        v[kPointSize] = ParamValue::scalar(3.5f);
        v[kNoiseAmount] = ParamValue::scalar(10.0f);
        v[kNoiseScale] = ParamValue::scalar(80.0f);
        v[kSpinY] = ParamValue::scalar(30.0f);
        return true;
    }

    std::span<const EffectPreset> presets() const noexcept override {
        // Cada preset escreve o visual inteiro (geometria, efetores e os três
        // desenhos); centro, posição, semente e câmera ficam como estão.
        static constexpr EffectPresetValue kClassic[] = {
            {kSource, 0.0f}, {kCount, 200.0f}, {kWidth, 100.0f}, {kHeight, 100.0f}, {kDepth, 60.0f}, {kJitter, 0.0f},
            {kScale, 100.0f}, {kSpinX, 0.0f}, {kSpinY, 6.0f}, {kSpinZ, 0.0f},
            {kNoiseOn, 1.0f}, {kNoiseAmount, 30.0f}, {kNoiseScale, 300.0f}, {kNoiseSpeed, 0.3f}, {kNoiseOctaves, 2.0f},
            {kFieldOn, 0.0f},
            {kLinesOn, 1.0f}, {kLineMaxDistance, 180.0f}, {kLineMinDistance, 0.0f}, {kConnections, 6.0f},
            {kLineWidth, 1.5f}, {kLineOpacity, 70.0f}, {kLineFalloff, 1.0f}, {kLineColorMode, 1.0f},
            {kPointsOn, 1.0f}, {kPointSize, 5.0f}, {kPointOpacity, 100.0f}, {kPointSoftness, 50.0f},
            {kPointColor, 1.0f, 0}, {kPointColor, 1.0f, 1}, {kPointColor, 1.0f, 2}, {kPointColor, 1.0f, 3},
            {kOnlyConnected, 0.0f}, {kTrianglesOn, 0.0f}, {kDepthFog, 0.0f}, {kDepthColor, 0.0f},
            {kOpacity, 100.0f}, {kBlend, 1.0f}};
        static constexpr EffectPresetValue kSphereLook[] = {
            {kSource, 1.0f}, {kCount, 320.0f}, {kRadius, 38.0f}, {kJitter, 0.0f}, {kScale, 100.0f},
            {kSpinX, 0.0f}, {kSpinY, 12.0f}, {kSpinZ, 0.0f},
            {kNoiseOn, 1.0f}, {kNoiseAmount, 8.0f}, {kNoiseScale, 220.0f}, {kNoiseSpeed, 0.4f}, {kNoiseOctaves, 1.0f},
            {kFieldOn, 0.0f},
            {kLinesOn, 1.0f}, {kLineMaxDistance, 140.0f}, {kLineMinDistance, 0.0f}, {kConnections, 6.0f},
            {kLineWidth, 1.2f}, {kLineOpacity, 75.0f}, {kLineFalloff, 0.6f}, {kLineColorMode, 1.0f},
            {kPointsOn, 1.0f}, {kPointSize, 4.0f}, {kPointOpacity, 100.0f}, {kPointSoftness, 40.0f},
            {kPointColor, 0.70f, 0}, {kPointColor, 0.88f, 1}, {kPointColor, 1.0f, 2}, {kPointColor, 1.0f, 3},
            {kOnlyConnected, 0.0f},
            {kTrianglesOn, 1.0f}, {kTriangleMaxDistance, 140.0f}, {kTriangleOpacity, 16.0f}, {kTriangleColorMode, 1.0f},
            {kDepthFog, 1.0f}, {kDepthNear, 80.0f}, {kDepthFar, 160.0f}, {kDepthColor, 0.0f},
            {kOpacity, 100.0f}, {kBlend, 1.0f}};
        static constexpr EffectPresetValue kDust[] = {
            {kSource, 0.0f}, {kCount, 700.0f}, {kWidth, 140.0f}, {kHeight, 140.0f}, {kDepth, 150.0f}, {kJitter, 0.0f},
            {kScale, 100.0f}, {kSpinX, 0.0f}, {kSpinY, 3.0f}, {kSpinZ, 0.0f},
            {kNoiseOn, 1.0f}, {kNoiseAmount, 60.0f}, {kNoiseScale, 500.0f}, {kNoiseSpeed, 0.15f}, {kNoiseOctaves, 2.0f},
            {kFieldOn, 0.0f},
            {kLinesOn, 0.0f},
            {kPointsOn, 1.0f}, {kPointSize, 7.0f}, {kPointOpacity, 90.0f}, {kPointSoftness, 80.0f},
            {kPointColor, 1.0f, 0}, {kPointColor, 0.95f, 1}, {kPointColor, 0.85f, 2}, {kPointColor, 1.0f, 3},
            {kPointPerspective, 1.0f}, {kOnlyConnected, 0.0f},
            {kTrianglesOn, 0.0f},
            {kDepthFog, 1.0f}, {kDepthNear, 60.0f}, {kDepthFar, 260.0f}, {kDepthColor, 0.0f},
            {kOpacity, 100.0f}, {kBlend, 1.0f}};
        static constexpr EffectPresetValue kLivingGrid[] = {
            {kSource, 2.0f}, {kWidth, 110.0f}, {kHeight, 110.0f}, {kDepth, 40.0f},
            {kGridX, 14.0f}, {kGridY, 8.0f}, {kGridZ, 2.0f}, {kJitter, 0.0f}, {kScale, 100.0f},
            {kSpinX, 0.0f}, {kSpinY, 0.0f}, {kSpinZ, 0.0f},
            {kNoiseOn, 1.0f}, {kNoiseAmount, 45.0f}, {kNoiseScale, 400.0f}, {kNoiseSpeed, 0.25f}, {kNoiseOctaves, 2.0f},
            {kFieldOn, 0.0f},
            {kLinesOn, 1.0f}, {kLineMaxDistance, 240.0f}, {kLineMinDistance, 0.0f}, {kConnections, 4.0f},
            {kLineWidth, 1.2f}, {kLineOpacity, 65.0f}, {kLineFalloff, 0.5f}, {kLineColorMode, 0.0f},
            {kLineColor, 0.40f, 0}, {kLineColor, 0.85f, 1}, {kLineColor, 1.0f, 2}, {kLineColor, 1.0f, 3},
            {kPointsOn, 1.0f}, {kPointSize, 4.0f}, {kPointOpacity, 100.0f}, {kPointSoftness, 50.0f},
            {kPointColor, 1.0f, 0}, {kPointColor, 1.0f, 1}, {kPointColor, 1.0f, 2}, {kPointColor, 1.0f, 3},
            {kOnlyConnected, 0.0f}, {kTrianglesOn, 0.0f}, {kDepthFog, 0.0f}, {kDepthColor, 0.0f},
            {kOpacity, 100.0f}, {kBlend, 1.0f}};
        static constexpr EffectPreset kPresets[] = {
            {"classic", "Rede clássica", kClassic},
            {"sphere", "Esfera", kSphereLook},
            {"dust", "Poeira", kDust},
            {"living_grid", "Grade viva", kLivingGrid},
        };
        return kPresets;
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kParamCount) return false;
        // Só a rede (sem a camada): mesmo sem nada a desenhar, a camada some.
        if (!e.b(kShowLayer)) return false;
        if (!(clampf(e.f(kOpacity), 0.0f, 100.0f, 100.0f) > 0.01f)) return true;
        const bool lines = e.b(kLinesOn) && clampf(e.f(kLineOpacity), 0.0f, 100.0f, 0.0f) > 0.01f;
        const bool points = e.b(kPointsOn) && clampf(e.f(kPointOpacity), 0.0f, 100.0f, 0.0f) > 0.01f
                         && clampf(e.f(kPointSize), 0.0f, 1000.0f, 0.0f) > 0.0f;
        const bool tris = e.b(kTrianglesOn) && clampf(e.f(kTriangleOpacity), 0.0f, 100.0f, 0.0f) > 0.01f;
        return !lines && !points && !tris;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (BlendMode m : {BlendMode::Add, BlendMode::Normal}) {
            out.push_back(PipelineKey::graphics(ShaderId::effects_constellation_vert, ShaderId::effects_constellation_frag,
                                                work, true, m));
        }
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_constellation_compose_frag, work));
    }

    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{};
        if (!e.resources || !e.placement || e.count < kParamCount || is_identity(e)) return;
        Workspace& ws = workspace();
        const PlanCounts counts = plan(e, ws);
        if (!counts.points) return;
        const u32 height = static_cast<u32>(ws.data.size() / kDataWidth);
        u64 key = 1469598103934665603ull ^ 0x434F4E5354454C4Cull;   // "CONSTELL"
        for (const Vec4& v : ws.data) {
            u32 bits[4];
            std::memcpy(bits, &v, sizeof bits);
            for (const u32 b : bits) key = (key ^ b) * 1099511628211ull;
        }
        key = (key ^ height) * 1099511628211ull;
        e.aux = e.resources->data_texture(key, ws.data.data(), kDataWidth, height);
        if (e.aux.valid()) {
            e.auxInfo = Vec4{static_cast<f32>(counts.points), static_cast<f32>(counts.lines),
                             static_cast<f32>(counts.triangles), static_cast<f32>(kDataWidth)};
        }
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        out = input;
        if (e.count < kParamCount) return OkStatus;
        const bool showLayer = e.b(kShowLayer);
        const u32 points = static_cast<u32>(std::max(0.0f, e.auxInfo.x));
        const u32 lines = static_cast<u32>(std::max(0.0f, e.auxInfo.y));
        const u32 tris = static_cast<u32>(std::max(0.0f, e.auxInfo.z));
        const bool data = e.aux.valid() && points > 0 && e.auxInfo.w > 0.5f;
        // Nada planejado (sem GPU de dado, opacidade 0...): com a camada, ela
        // passa intacta; sem ela, a saída é transparente (passe vazio).
        if (!data && showLayer) return OkStatus;

        const f32 opacity = clampf(e.f(kOpacity), 0.0f, 100.0f, 100.0f) * 0.01f;
        const f32 lw = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 lh = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        struct Uniforms {
            Vec4 region, counts, texel, line, lineColor, point, tri, triColor;
        } u{};
        static_assert(sizeof(Uniforms) == 128, "layout std140 do constellation.vert");
        u.region = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.counts = Vec4{static_cast<f32>(points), static_cast<f32>(lines), static_cast<f32>(tris),
                        data ? e.auxInfo.w : 1.0f};
        u.texel = Vec4{std::max(input.texel_scale_x(), 1e-4f), 0.25f * std::max({lw, lh, 1.0f}), 0.0f, 0.0f};
        const Vec3 lc = linear_rgb(e.color(kLineColor)), tc = linear_rgb(e.color(kTriangleColor));
        u.line = Vec4{clampf(e.f(kLineWidth), 0.1f, 200.0f, 1.5f), e.b(kLinePerspective) ? 1.0f : 0.0f,
                      clampf(e.f(kLineOpacity), 0.0f, 100.0f, 70.0f) * 0.01f * opacity,
                      e.e(kLineColorMode) == 1 ? 1.0f : 0.0f};
        u.lineColor = Vec4{lc.x, lc.y, lc.z, 1.0f};
        u.point = Vec4{clampf(e.f(kPointSize), 0.0f, 1000.0f, 5.0f), e.b(kPointPerspective) ? 1.0f : 0.0f,
                       clampf(e.f(kPointOpacity), 0.0f, 100.0f, 100.0f) * 0.01f * opacity,
                       clampf(e.f(kPointSoftness), 0.0f, 100.0f, 50.0f) * 0.01f};
        u.tri = Vec4{clampf(e.f(kTriangleOpacity), 0.0f, 100.0f, 20.0f) * 0.01f * opacity,
                     e.e(kTriangleColorMode) == 1 ? 1.0f : 0.0f, 0.0f, 0.0f};
        u.triColor = Vec4{tc.x, tc.y, tc.z, 1.0f};
        const bool drawPoints = e.b(kPointsOn);
        const u32 vertices = data ? tris * 3u + lines * 6u + (drawPoints ? points * 6u : 0u) : 0u;
        const bool additive = e.e(kBlend) == 1;

        const FGTexture net = ctx.texture("constelacao-rede", input.width, input.height);
        if (ctx.geometry_pass("constelacao", PassStage::Effects, net, ShaderId::effects_constellation_vert,
                              ShaderId::effects_constellation_frag,
                              {PassTexture{{}, data ? e.aux : TextureHandle{}, CommonSampler::NearestClamp}},
                              &u, sizeof(u), vertices, false, true,
                              additive ? BlendMode::Add : BlendMode::Normal) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        const Vec4 mode{additive ? 1.0f : 0.0f, showLayer ? 1.0f : 0.0f, 0.0f, 0.0f};
        out.texture = ctx.texture("constelacao", input.width, input.height);
        if (ctx.fullscreen_pass("constelacao-composicao", PassStage::Effects, out.texture,
                                ShaderId::effects_constellation_compose_frag,
                                {PassTexture{net, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &mode, sizeof(mode)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }

private:
    /// A câmera do quadro: a da composição (ativa, seguida, camada 2D afim)
    /// ou a própria de "Campo de visão" no centro da camada.
    static Camera camera_of(const EffectEval& e, f32 lw, f32 lh) noexcept {
        Camera cam;
        const LayerPlacement& pl = *e.placement;
        // Camada 3D (ou com o Particular): a rede vive no mundo da camada e a
        // câmera da cena a vê — a camada não é um plano que a câmera move.
        if (e.b(kFollowCamera) && pl.particleSpace && pl.sceneCameraValid && pl.compWidth && pl.compHeight
            && pl.sceneFovY > 0.01f) {
            cam.world = true;
            cam.worldFromLayer = pl.worldFromLayer;
            cam.compFromWorld = pl.sceneCompFromWorld;
            cam.layerFromComp = pl.particleLayerFromComp;
            cam.view = pl.sceneView;
            cam.compW = static_cast<f32>(pl.compWidth);
            cam.compH = static_cast<f32>(pl.compHeight);
            cam.focal = 0.5f * cam.compH / std::tan(0.5f * std::clamp(pl.sceneFovY, 0.02f, 3.0f));
            return cam;
        }
        if (e.b(kFollowCamera) && pl.cameraActive && !pl.inScene3d && pl.compWidth && pl.compHeight) {
            const Mat4& m = pl.compFromLayer;
            const bool affine = std::fabs(m.col[0].w) < 1e-6f && std::fabs(m.col[1].w) < 1e-6f
                             && std::fabs(m.col[3].w - 1.0f) < 1e-6f;
            cam.h[0] = m.col[0].x; cam.h[1] = m.col[1].x; cam.h[2] = m.col[3].x;
            cam.h[3] = m.col[0].y; cam.h[4] = m.col[1].y; cam.h[5] = m.col[3].y;
            cam.h[6] = m.col[0].w; cam.h[7] = m.col[1].w; cam.h[8] = m.col[3].w;
            const f64 det = cam.h[0] * cam.h[4] - cam.h[1] * cam.h[3];
            const f32 fov = std::clamp(finite_or(pl.cameraFovY, 0.0f), 0.0f, 3.0f);
            if (affine && std::fabs(det) > 1e-12 && fov > 0.01f && invert3(cam.h, cam.hinv)) {
                cam.comp = true;
                cam.zScale = static_cast<f32>(std::sqrt(std::fabs(det)));
                cam.view = pl.cameraView;
                cam.compW = static_cast<f32>(pl.compWidth);
                cam.compH = static_cast<f32>(pl.compHeight);
                cam.focal = 0.5f * cam.compH / std::tan(0.5f * fov);
                return cam;
            }
        }
        const f32 fov = clampf(e.f(kFov), 10.0f, 120.0f, 50.0f) * kDeg2Rad;
        cam.cx = 0.5f * lw;
        cam.cy = 0.5f * lh;
        cam.F = 0.5f * std::max(lh, 1.0f) / std::tan(0.5f * fov);
        return cam;
    }

    /// Pontos no espaço próprio (em volta do centro, antes da escala e da
    /// rotação). Caminho: já em px da camada, menos o centro.
    static void generate(const EffectEval& e, f32 lw, f32 lh, Vec3 center, u32 seed, std::vector<Vec3>& out) {
        out.clear();
        const u32 source = std::min<u32>(e.e(kSource), kSourceCount - 1);
        const u32 count = static_cast<u32>(std::clamp(e.value(kCount).as_int(), 2, static_cast<i32>(kMaxPoints)));
        const f32 W = clampf(e.f(kWidth), 0.0f, 2000.0f, 100.0f) * 0.01f * lw;
        const f32 H = clampf(e.f(kHeight), 0.0f, 2000.0f, 100.0f) * 0.01f * lh;
        const f32 D = clampf(e.f(kDepth), 0.0f, 2000.0f, 60.0f) * 0.01f * lh;
        const f32 R = clampf(e.f(kRadius), 0.0f, 2000.0f, 40.0f) * 0.01f * lh;
        const f32 r = clampf(e.f(kTubeRadius), 0.0f, 1000.0f, 12.0f) * 0.01f * lh;
        constexpr f32 kGolden = 0.61803398875f;
        switch (source) {
            case kSphere: {
                // Esfera de Fibonacci: pontos espalhados por igual na casca; a
                // semente gira a espiral em volta do eixo.
                const f32 phase = kTwoPi * rnd(seed, 0, 31);
                out.reserve(count);
                for (u32 i = 0; i < count; ++i) {
                    const f32 y = 1.0f - 2.0f * (static_cast<f32>(i) + 0.5f) / static_cast<f32>(count);
                    const f32 ring = std::sqrt(std::max(0.0f, 1.0f - y * y));
                    const f32 phi = phase + kTwoPi * kGolden * static_cast<f32>(i);
                    out.push_back(Vec3{R * ring * std::cos(phi), R * y, R * ring * std::sin(phi)});
                }
                break;
            }
            case kGrid: {
                u32 gx = static_cast<u32>(std::clamp(e.value(kGridX).as_int(), 1, 50));
                u32 gy = static_cast<u32>(std::clamp(e.value(kGridY).as_int(), 1, 50));
                u32 gz = static_cast<u32>(std::clamp(e.value(kGridZ).as_int(), 1, 20));
                while (gx * gy * gz > kMaxPoints) {
                    if (gz > 1) --gz;
                    else if (gy >= gx) --gy;
                    else --gx;
                }
                auto at = [](u32 i, u32 n, f32 size) {
                    return n > 1 ? -0.5f * size + size * static_cast<f32>(i) / static_cast<f32>(n - 1) : 0.0f;
                };
                out.reserve(static_cast<usize>(gx) * gy * gz);
                for (u32 z = 0; z < gz; ++z)
                    for (u32 y = 0; y < gy; ++y)
                        for (u32 x = 0; x < gx; ++x) out.push_back(Vec3{at(x, gx, W), at(y, gy, H), at(z, gz, D)});
                break;
            }
            case kRing: {
                // Anel de frente para a câmera (eixo z): ângulo por igual na
                // volta, posição no tubo pela razão áurea (anel puro com raio 0).
                const f32 phase = rnd(seed, 0, 32);
                out.reserve(count);
                for (u32 i = 0; i < count; ++i) {
                    const f32 theta = kTwoPi * static_cast<f32>(i) / static_cast<f32>(count);
                    f32 t = phase + kGolden * static_cast<f32>(i);
                    t -= std::floor(t);
                    const f32 phi = kTwoPi * t;
                    const f32 rr = R + r * std::cos(phi);
                    out.push_back(Vec3{rr * std::cos(theta), rr * std::sin(theta), r * std::sin(phi)});
                }
                break;
            }
            case kPlane:
                out.reserve(count);
                for (u32 i = 0; i < count; ++i)
                    out.push_back(Vec3{(rnd(seed, i, 0) - 0.5f) * W, (rnd(seed, i, 1) - 0.5f) * H, 0.0f});
                break;
            case kPath: {
                if (!e.resources || !e.layer) break;
                const std::vector<Vec4> path = e.resources->repeat_path(e.layer, count, 0.0f, e.value(kPathLayer).ref);
                out.reserve(path.size());
                for (const Vec4& q : path) {
                    if (!std::isfinite(q.x) || !std::isfinite(q.y)) continue;
                    out.push_back(Vec3{q.x - center.x, q.y - center.y, 0.0f});
                }
                break;
            }
            case kCloud:
            default:
                out.reserve(count);
                for (u32 i = 0; i < count; ++i) {
                    out.push_back(Vec3{(rnd(seed, i, 0) - 0.5f) * W, (rnd(seed, i, 1) - 0.5f) * H,
                                       (rnd(seed, i, 2) - 0.5f) * D});
                }
                break;
        }
    }

    /// O planejamento inteiro: pontos, efetores, câmera, rede e o pacote da
    /// textura de dado (em `ws.data`, `kDataWidth` texels por linha).
    static PlanCounts plan(const EffectEval& e, Workspace& ws) {
        PlanCounts counts;
        const LayerPlacement& pl = *e.placement;
        const f32 lw = pl.layerWidth ? static_cast<f32>(pl.layerWidth) : 1920.0f;
        const f32 lh = pl.layerHeight ? static_cast<f32>(pl.layerHeight) : 1080.0f;
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        const f64 seconds = e.time_frames() / fps;
        const u32 seed = static_cast<u32>(std::clamp(e.value(kSeed).as_int(), 0, 9999));
        const f32 quality = std::clamp(finite_or(e.resources->effect_quality(), 1.0f), 0.25f, 1.0f);
        const Vec2 c2 = e.p2(kCenter);
        const Vec3 center{finite_or(c2.x, 0.5f) * lw, finite_or(c2.y, 0.5f) * lh,
                          clampf(e.f(kPositionZ), -20000.0f, 20000.0f, 0.0f)};

        // 1. Geometria, desordem, escala, rotação e posição.
        generate(e, lw, lh, center, seed, ws.pos);
        const u32 n = static_cast<u32>(std::min<usize>(ws.pos.size(), kMaxPoints));
        ws.pos.resize(n);
        if (n == 0) return counts;
        const f32 jitter = clampf(e.f(kJitter), 0.0f, 5000.0f, 0.0f);
        const f32 scale = clampf(e.f(kScale), 0.0f, 4000.0f, 100.0f) * 0.01f;
        const Rot3 rot = rotation_xyz(spin_angle(finite_or(e.f(kRotationX), 0.0f), clampf(e.f(kSpinX), -3600.0f, 3600.0f, 0.0f), seconds),
                                      spin_angle(finite_or(e.f(kRotationY), 0.0f), clampf(e.f(kSpinY), -3600.0f, 3600.0f, 0.0f), seconds),
                                      spin_angle(finite_or(e.f(kRotationZ), 0.0f), clampf(e.f(kSpinZ), -3600.0f, 3600.0f, 0.0f), seconds));
        for (u32 i = 0; i < n; ++i) {
            Vec3 p = ws.pos[i];
            if (jitter > 0.0f) {
                p += Vec3{rnd(seed, i, 10) * 2.0f - 1.0f, rnd(seed, i, 11) * 2.0f - 1.0f, rnd(seed, i, 12) * 2.0f - 1.0f}
                     * jitter;
            }
            ws.pos[i] = center + apply(rot, p * scale);
        }

        // 2. Efetores, no espaço da camada (o campo de ruído fica parado e a
        //    forma passa por ele ao girar), na ordem: ruído, campo.
        const f32 noiseAmount = clampf(e.f(kNoiseAmount), 0.0f, 5000.0f, 0.0f);
        if (e.b(kNoiseOn) && noiseAmount > 0.0f) {
            const f32 inv = 1.0f / clampf(e.f(kNoiseScale), 1.0f, 20000.0f, 300.0f);
            const f64 evo = seconds * static_cast<f64>(clampf(e.f(kNoiseSpeed), 0.0f, 100.0f, 0.3f));
            const f32 w = static_cast<f32>(evo - 4096.0 * std::floor(evo / 4096.0));
            const u32 octaves = static_cast<u32>(std::clamp(e.value(kNoiseOctaves).as_int(), 1, 4));
            const u32 salt = mix32(seed * 0x27D4EB2Fu + 0x165667B1u);
            for (Vec3& p : ws.pos) {
                f32 d[3];
                fbm3(p * inv, w, octaves, salt, d);
                p += Vec3{d[0], d[1], d[2]} * noiseAmount;
            }
        }
        const f32 fieldRadius = clampf(e.f(kFieldRadius), 0.0f, 20000.0f, 0.0f);
        const f32 fieldStrength = clampf(e.f(kFieldStrength), -10000.0f, 10000.0f, 0.0f);
        if (e.b(kFieldOn) && fieldRadius > 0.0f && fieldStrength != 0.0f) {
            const Vec2 f2 = e.p2(kFieldCenter);
            const Vec3 fc{finite_or(f2.x, 0.5f) * lw, finite_or(f2.y, 0.5f) * lh,
                          clampf(e.f(kFieldZ), -20000.0f, 20000.0f, 0.0f)};
            for (Vec3& p : ws.pos) {
                const Vec3 v = p - fc;
                const f32 dist = v.length();
                if (!(dist < fieldRadius) || dist < 1e-4f) continue;
                // Queda suave: 1 no centro, 0 no raio. Puxar nunca passa do centro.
                const f32 push = std::max(fieldStrength * smooth01(1.0f - dist / fieldRadius), -dist);
                p += v * (push / dist);
            }
        }

        // 3. Câmera: posição na tela, perspectiva e névoa de cada ponto.
        const Camera cam = camera_of(e, lw, lh);
        const bool fog = e.b(kDepthFog), depthColor = e.b(kDepthColor);
        const f32 nearR = clampf(e.f(kDepthNear), 0.0f, 10000.0f, 80.0f) * 0.01f;
        const f32 farR = clampf(e.f(kDepthFar), 0.0f, 10000.0f, 200.0f) * 0.01f;
        ws.screen.resize(n);
        ws.ratio.resize(n);
        for (u32 i = 0; i < n; ++i) {
            f32 sx = 0.0f, sy = 0.0f, ratio = 0.0f;
            const Vec3 p = ws.pos[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !cam.project(p, sx, sy, ratio)) {
                ws.screen[i] = Vec4{0.0f, 0.0f, 1.0f, 0.0f};
                ws.ratio[i] = 0.0f;
                continue;
            }
            // Perto da lente o ponto some suave (sem um sprite do tamanho da tela).
            f32 alpha = smoothstepf(0.02f, 0.08f, ratio);
            if (fog) alpha *= 1.0f - smoothstepf(nearR, farR, ratio);
            ws.screen[i] = Vec4{sx, sy, std::min(1.0f / ratio, 20.0f), alpha};
            ws.ratio[i] = ratio;
        }

        // 4. A rede: os k mais próximos dentro da maior distância pedida.
        const bool linesOn = e.b(kLinesOn), trisOn = e.b(kTrianglesOn);
        const f32 lineMax = clampf(e.f(kLineMaxDistance), 0.0f, 10000.0f, 180.0f);
        const f32 lineMin = clampf(e.f(kLineMinDistance), 0.0f, 10000.0f, 0.0f);
        const f32 triMax = clampf(e.f(kTriangleMaxDistance), 0.0f, 10000.0f, 160.0f);
        const f32 graphMax = std::max(linesOn ? lineMax : 0.0f, trisOn ? triMax : 0.0f);
        // A distância mínima tira da rede os pares perto demais (eles também
        // não contam entre os k); sem linhas ela não vale.
        const f32 graphMin = linesOn ? lineMin : 0.0f;
        const u32 k = static_cast<u32>(std::clamp(e.value(kConnections).as_int(), 1, static_cast<i32>(kMaxNeighbors)));
        const u32 lineCap = static_cast<u32>(static_cast<f32>(kMaxLines) * quality);
        const u32 triCap = static_cast<u32>(static_cast<f32>(kMaxTriangles) * quality);
        ws.edges.clear();
        if (graphMax >= 0.5f && graphMax > graphMin) build_graph(ws, graphMax, graphMin, k);
        if (ws.edges.size() > lineCap) {
            // Acima do teto ficam as mais curtas (as mais visíveis).
            std::sort(ws.edges.begin(), ws.edges.end(), [](const Edge& x, const Edge& y) {
                if (x.d != y.d) return x.d < y.d;
                return x.a != y.a ? x.a < y.a : x.b < y.b;
            });
            ws.edges.resize(lineCap);
            std::sort(ws.edges.begin(), ws.edges.end(),
                      [](const Edge& x, const Edge& y) { return x.a != y.a ? x.a < y.a : x.b < y.b; });
        }
        ws.connected.assign(n, 0);
        ws.lines.clear();
        if (linesOn) {
            const f32 falloff = clampf(e.f(kLineFalloff), 0.0f, 16.0f, 1.0f);
            const f32 span = lineMax - lineMin;
            for (const Edge& ed : ws.edges) {
                if (ed.d > lineMax) continue;
                if (!(ws.screen[ed.a].w > 0.0f) || !(ws.screen[ed.b].w > 0.0f)) continue;
                const f32 t = span > 1e-6f ? std::clamp((ed.d - lineMin) / span, 0.0f, 1.0f) : 0.0f;
                const f32 alpha = falloff > 0.0f ? std::pow(1.0f - t, falloff) : 1.0f;
                if (!(alpha > 1.0f / 512.0f)) continue;
                ws.lines.push_back(Vec4{static_cast<f32>(ed.a), static_cast<f32>(ed.b), alpha, 0.0f});
                ws.connected[ed.a] = ws.connected[ed.b] = 1;
            }
        }
        ws.tris.clear();
        if (trisOn && triMax >= 0.5f) {
            // Adjacência (só os lados que cabem nos triângulos), vizinhos em
            // ordem: o trio (a < b < c) sai da interseção das listas de a e b.
            ws.adjStart.assign(static_cast<usize>(n) + 1, 0);
            for (const Edge& ed : ws.edges) if (ed.d <= triMax) { ++ws.adjStart[ed.a + 1]; ++ws.adjStart[ed.b + 1]; }
            for (u32 i = 0; i < n; ++i) ws.adjStart[i + 1] += ws.adjStart[i];
            ws.adj.assign(ws.adjStart[n], 0);
            ws.cursor.assign(ws.adjStart.begin(), ws.adjStart.end() - 1);
            for (const Edge& ed : ws.edges) {
                if (ed.d > triMax) continue;
                ws.adj[ws.cursor[ed.a]++] = ed.b;
                ws.adj[ws.cursor[ed.b]++] = ed.a;
            }
            for (u32 i = 0; i < n; ++i)
                std::sort(ws.adj.begin() + ws.adjStart[i], ws.adj.begin() + ws.adjStart[i + 1]);
            for (const Edge& ed : ws.edges) {
                if (ws.tris.size() >= triCap) break;
                if (ed.d > triMax) continue;
                u32 x = ws.adjStart[ed.a], xe = ws.adjStart[ed.a + 1];
                u32 y = ws.adjStart[ed.b], ye = ws.adjStart[ed.b + 1];
                while (x < xe && y < ye && ws.tris.size() < triCap) {
                    const u32 cx = ws.adj[x], cy = ws.adj[y];
                    if (cx < cy) { ++x; continue; }
                    if (cy < cx) { ++y; continue; }
                    ++x; ++y;
                    const u32 c = cx;
                    if (c <= ed.b) continue;   // cada trio uma vez: a < b < c
                    if (!(ws.screen[ed.a].w > 0.0f) || !(ws.screen[ed.b].w > 0.0f) || !(ws.screen[c].w > 0.0f)) continue;
                    const f32 far = std::max({ed.d, (ws.pos[c] - ws.pos[ed.a]).length(), (ws.pos[c] - ws.pos[ed.b]).length()});
                    // Some suave no último terço da distância (faces grandes mais fracas).
                    const f32 alpha = smooth01((1.0f - far / triMax) / 0.3f);
                    if (!(alpha > 1.0f / 512.0f)) continue;
                    ws.tris.push_back(Vec4{static_cast<f32>(ed.a), static_cast<f32>(ed.b), static_cast<f32>(c), alpha});
                    ws.connected[ed.a] = ws.connected[ed.b] = ws.connected[c] = 1;
                }
            }
        }

        // 5. O pacote: [2i] = tela, perspectiva, alfa; [2i+1] = cor + sprite;
        //    depois as linhas e os triângulos.
        const Vec3 pointColor = linear_rgb(e.color(kPointColor));
        const Vec3 nearC = linear_rgb(e.color(kNearColor)), farC = linear_rgb(e.color(kFarColor));
        const bool onlyConnected = e.b(kOnlyConnected);
        const usize texels = static_cast<usize>(n) * 2 + ws.lines.size() + ws.tris.size();
        const usize rows = std::max<usize>(1, (texels + kDataWidth - 1) / kDataWidth);
        ws.data.assign(rows * kDataWidth, Vec4{});
        for (u32 i = 0; i < n; ++i) {
            Vec3 c = pointColor;
            if (depthColor) {
                const f32 t = farR > nearR ? std::clamp((ws.ratio[i] - nearR) / (farR - nearR), 0.0f, 1.0f)
                                           : (ws.ratio[i] >= nearR ? 1.0f : 0.0f);
                c = nearC + (farC - nearC) * t;
            }
            ws.data[static_cast<usize>(i) * 2] = ws.screen[i];
            ws.data[static_cast<usize>(i) * 2 + 1] = Vec4{c.x, c.y, c.z, (!onlyConnected || ws.connected[i]) ? 1.0f : 0.0f};
        }
        std::copy(ws.lines.begin(), ws.lines.end(), ws.data.begin() + static_cast<std::ptrdiff_t>(n) * 2);
        std::copy(ws.tris.begin(), ws.tris.end(),
                  ws.data.begin() + static_cast<std::ptrdiff_t>(n) * 2 + static_cast<std::ptrdiff_t>(ws.lines.size()));
        counts.points = n;
        counts.lines = static_cast<u32>(ws.lines.size());
        counts.triangles = static_cast<u32>(ws.tris.size());
        return counts;
    }
};

} // namespace

void register_constellation_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Constellation>()); }

} // namespace aurea::builtin
