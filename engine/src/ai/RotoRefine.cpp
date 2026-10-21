#include "aurea/ai/RotoRefine.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <system_error>
#include <thread>

namespace aurea::ai {
namespace {

// Valores medidos na sequência sintética de test_roto.cpp (corpo nítido +
// fios de cabelo sobre textura), 720p e 1080p.
// Trimapa: valor do recorte ampliado que ainda é "incerto" (a rede indecisa).
constexpr f32 kSoftLo = 0.05f, kSoftHi = 0.95f;
// Filtro guiado: variância de cor abaixo disto é textura (suaviza), acima é
// aresta. Raio do filtro = raio da borda × kGuidedRadius.
constexpr f32 kGuidedEps = 1e-4f;
constexpr f32 kGuidedRadius = 0.125f;
// Fora da reta fundo→objeto (resíduo maior que ¼ da separação), vale a
// distância até o fundo; abaixo de kTextureFloor é textura do fundo.
constexpr f32 kOffLine = 0.0625f;
constexpr f32 kTextureFloor = 0.08f;
constexpr f32 kHaze = 0.08f;
// Reduzir trepidação: diferença típica do ruído (alfa 0..1). Acima, é
// movimento real e o quadro atual manda.
constexpr f32 kChatterTau = 0.12f;
constexpr f32 kChatterMax = 0.85f;
// Segunda passada do Blur-Fusion (resolução cheia), px.
constexpr u32 kFuseRadius = 3;
// Primeira passada (grade reduzida), px: janela pequena = a cor do fio, não a
// do corpo (medido: 2-3 px dão o menor erro de cor no cabelo e na borda).
constexpr f32 kDeconRadius = 3.0f;
// Memória de trabalho de todas as threads juntas (bytes).
constexpr usize kThreadBudget = 32u * 1024u * 1024u;

f32 smooth01(f32 e0, f32 e1, f32 x) noexcept {
    const f32 t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// O recorte grosso ampliado: valor e gradiente (por px) com as tabelas da
/// amostra bilinear por coluna e por linha.
struct Coarse {
    const f32* m = nullptr;
    u32 n = 0;
    f32 sx = 0, sy = 0;           // grade por px
    std::vector<u32> x0, y0;
    std::vector<f32> fx, fy;
    void init(const f32* coarse, u32 size, u32 w, u32 h) {
        m = coarse; n = size;
        sx = static_cast<f32>(n) / static_cast<f32>(w);
        sy = static_cast<f32>(n) / static_cast<f32>(h);
        auto table = [&](u32 count, f32 s, std::vector<u32>& i0, std::vector<f32>& f) {
            i0.resize(count); f.resize(count);
            for (u32 i = 0; i < count; ++i) {
                const f32 g = std::clamp((static_cast<f32>(i) + 0.5f) * s - 0.5f, 0.0f, static_cast<f32>(n - 1));
                const u32 a = std::min(static_cast<u32>(g), n >= 2 ? n - 2 : 0u);
                i0[i] = a; f[i] = g - static_cast<f32>(a);
            }
        };
        table(w, sx, x0, fx);
        table(h, sy, y0, fy);
    }
    void at(u32 x, u32 y, f32& v, f32& gx, f32& gy) const noexcept {
        const u32 a = x0[x], b = y0[y];
        const u32 a1 = std::min(a + 1, n - 1), b1 = std::min(b + 1, n - 1);
        const f32 u = fx[x], w = fy[y];
        const f32 c00 = m[static_cast<usize>(b) * n + a], c10 = m[static_cast<usize>(b) * n + a1];
        const f32 c01 = m[static_cast<usize>(b1) * n + a], c11 = m[static_cast<usize>(b1) * n + a1];
        const f32 top = c00 + (c10 - c00) * u, bot = c01 + (c11 - c01) * u;
        v = top + (bot - top) * w;
        gx = ((c10 - c00) * (1.0f - w) + (c11 - c01) * w) * sx;
        gy = (bot - top) * sy;
    }
};

/// Média em caixa (raio r, janela recortada na borda), separável, planar.
/// `src` e `dst` podem ser o mesmo buffer.
void box_mean(const f32* src, f32* dst, u32 w, u32 h, u32 r, std::vector<f32>& tmp, std::vector<f32>& col) {
    tmp.resize(static_cast<usize>(w) * h);
    for (u32 y = 0; y < h; ++y) {
        const f32* s = src + static_cast<usize>(y) * w;
        f32* t = tmp.data() + static_cast<usize>(y) * w;
        f32 acc = 0;
        const u32 first = std::min(r, w - 1);
        for (u32 x = 0; x <= first; ++x) acc += s[x];
        for (u32 x = 0; x < w; ++x) {
            const u32 lo = x >= r ? x - r : 0, hi = std::min(w - 1, x + r);
            t[x] = acc / static_cast<f32>(hi - lo + 1);
            if (x + r + 1 < w) acc += s[x + r + 1];
            if (x >= r) acc -= s[x - r];
        }
    }
    col.assign(w, 0.0f);
    const u32 first = std::min(r, h - 1);
    for (u32 y = 0; y <= first; ++y) {
        const f32* t = tmp.data() + static_cast<usize>(y) * w;
        for (u32 x = 0; x < w; ++x) col[x] += t[x];
    }
    for (u32 y = 0; y < h; ++y) {
        const u32 lo = y >= r ? y - r : 0, hi = std::min(h - 1, y + r);
        const f32 inv = 1.0f / static_cast<f32>(hi - lo + 1);
        f32* d = dst + static_cast<usize>(y) * w;
        for (u32 x = 0; x < w; ++x) d[x] = col[x] * inv;
        if (y + r + 1 < h) { const f32* t = tmp.data() + static_cast<usize>(y + r + 1) * w; for (u32 x = 0; x < w; ++x) col[x] += t[x]; }
        if (y >= r) { const f32* t = tmp.data() + static_cast<usize>(y - r) * w; for (u32 x = 0; x < w; ++x) col[x] -= t[x]; }
    }
}

/// 3×3 simétrica (a00 a01 a02 a11 a12 a22) · x = b.
bool solve_sym3(f32 a00, f32 a01, f32 a02, f32 a11, f32 a12, f32 a22, const f32 b[3], f32 x[3]) noexcept {
    const f32 c00 = a11 * a22 - a12 * a12, c01 = a02 * a12 - a01 * a22, c02 = a01 * a12 - a02 * a11;
    const f32 det = a00 * c00 + a01 * c01 + a02 * c02;
    if (!(std::fabs(det) > 1e-18f)) return false;
    const f32 c11 = a00 * a22 - a02 * a02, c12 = a01 * a02 - a00 * a12, c22 = a00 * a11 - a01 * a01;
    const f32 inv = 1.0f / det;
    x[0] = (c00 * b[0] + c01 * b[1] + c02 * b[2]) * inv;
    x[1] = (c01 * b[0] + c11 * b[1] + c12 * b[2]) * inv;
    x[2] = (c02 * b[0] + c12 * b[1] + c22 * b[2]) * inv;
    return true;
}

/// Os buffers de um bloco (reaproveitados de bloco a bloco).
struct Work {
    // Resolução cheia (região do bloco com margem).
    std::vector<f32> I;          // 3 por px
    std::vector<f32> v, wband;   // recorte ampliado, peso da faixa
    std::vector<u8> cls;         // 0 incerto, 1 objeto certo, 2 fundo certo
    std::vector<f32> p, alpha;
    std::vector<f32> fuse;       // Blur-Fusion em resolução cheia (planar)
    // Grade reduzida (planar, um plano por canal).
    std::vector<f32> low;
    std::vector<f32> tmp, col;
};

/// Bilinear numa grade planar lw×lh com células de s px a partir de (ox, oy).
struct LowSampler {
    u32 lw = 0, lh = 0, s = 1;
    std::vector<u32> i0, j0;
    std::vector<f32> fi, fj;
    void init(u32 regionW, u32 regionH, u32 cellsW, u32 cellsH, u32 step) {
        lw = cellsW; lh = cellsH; s = step;
        auto table = [&](u32 count, u32 cells, std::vector<u32>& i, std::vector<f32>& f) {
            i.resize(count); f.resize(count);
            for (u32 k = 0; k < count; ++k) {
                const f32 g = std::clamp((static_cast<f32>(k) + 0.5f) / static_cast<f32>(s) - 0.5f, 0.0f,
                                         static_cast<f32>(cells - 1));
                const u32 a = std::min(static_cast<u32>(g), cells >= 2 ? cells - 2 : 0u);
                i[k] = a; f[k] = g - static_cast<f32>(a);
            }
        };
        table(regionW, cellsW, i0, fi);
        table(regionH, cellsH, j0, fj);
    }
    f32 at(const f32* plane, u32 x, u32 y) const noexcept {
        const u32 a = i0[x], b = j0[y], a1 = std::min(a + 1, lw - 1), b1 = std::min(b + 1, lh - 1);
        const f32 u = fi[x], w = fj[y];
        const f32 top = plane[static_cast<usize>(b) * lw + a] + (plane[static_cast<usize>(b) * lw + a1] - plane[static_cast<usize>(b) * lw + a]) * u;
        const f32 bot = plane[static_cast<usize>(b1) * lw + a] + (plane[static_cast<usize>(b1) * lw + a1] - plane[static_cast<usize>(b1) * lw + a]) * u;
        return top + (bot - top) * w;
    }
};

f32 sample_u8(const std::vector<u8>& a, u32 w, u32 h, f32 x, f32 y) noexcept {
    x = std::clamp(x, 0.0f, static_cast<f32>(w - 1));
    y = std::clamp(y, 0.0f, static_cast<f32>(h - 1));
    const u32 x0 = static_cast<u32>(x), y0 = static_cast<u32>(y);
    const u32 x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
    const f32 fx = x - static_cast<f32>(x0), fy = y - static_cast<f32>(y0);
    const f32 c00 = a[static_cast<usize>(y0) * w + x0], c10 = a[static_cast<usize>(y0) * w + x1];
    const f32 c01 = a[static_cast<usize>(y1) * w + x0], c11 = a[static_cast<usize>(y1) * w + x1];
    return ((c00 + (c10 - c00) * fx) * (1 - fy) + (c01 + (c11 - c01) * fx) * fy) * (1.0f / 255.0f);
}

u8 to_u8(f32 v) noexcept { return static_cast<u8>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

} // namespace

u64 RotoRefineSettings::hash() const noexcept {
    if (!on()) return 0;
    u64 h = 0xCBF29CE484222325ull;
    auto mix = [&](u64 v) { for (u32 i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 0x100000001B3ull; } };
    mix(std::bit_cast<u32>(radius));
    mix(std::bit_cast<u32>(chatter));
    mix(decontaminate ? 1u : 0u);
    mix(2);   // versão do algoritmo: muda o resultado, muda a chave
    return h ? h : 1;
}

void roto_refine_size(u32 w, u32 h, u32 maxSide, u32& outW, u32& outH) noexcept {
    w = std::max(1u, w); h = std::max(1u, h);
    const u32 side = std::max(w, h);
    if (!maxSide || side <= maxSide) { outW = w; outH = h; return; }
    const f64 k = static_cast<f64>(maxSide) / static_cast<f64>(side);
    outW = std::max(1u, static_cast<u32>(std::lround(w * k)));
    outH = std::max(1u, static_cast<u32>(std::lround(h * k)));
}

void roto_image_from(const u8* pixels, u32 width, u32 height, u32 stride, u32 channels, u32 maxSide, RotoImage& out) {
    out = RotoImage{};
    if (!pixels || !width || !height || (channels != 3 && channels != 4)) return;
    u32 ow = 0, oh = 0;
    roto_refine_size(width, height, maxSide, ow, oh);
    out.width = ow; out.height = oh;
    out.rgb.resize(static_cast<usize>(ow) * oh * 3);
    if (ow == width && oh == height) {
        for (u32 y = 0; y < oh; ++y) {
            const u8* s = pixels + static_cast<usize>(y) * stride;
            u8* d = out.rgb.data() + static_cast<usize>(y) * ow * 3;
            if (channels == 3) std::memcpy(d, s, static_cast<usize>(ow) * 3);
            else for (u32 x = 0; x < ow; ++x) { d[x * 3] = s[x * 4]; d[x * 3 + 1] = s[x * 4 + 1]; d[x * 3 + 2] = s[x * 4 + 2]; }
        }
        return;
    }
    // Redução por área (janela inteira que cobre cada pixel de saída).
    for (u32 y = 0; y < oh; ++y) {
        const u32 sy0 = static_cast<u32>(static_cast<u64>(y) * height / oh);
        const u32 sy1 = std::max(sy0 + 1, static_cast<u32>(static_cast<u64>(y + 1) * height / oh));
        for (u32 x = 0; x < ow; ++x) {
            const u32 sx0 = static_cast<u32>(static_cast<u64>(x) * width / ow);
            const u32 sx1 = std::max(sx0 + 1, static_cast<u32>(static_cast<u64>(x + 1) * width / ow));
            u32 sum[3]{}, cnt = 0;
            for (u32 yy = sy0; yy < sy1 && yy < height; ++yy) {
                const u8* row = pixels + static_cast<usize>(yy) * stride;
                for (u32 xx = sx0; xx < sx1 && xx < width; ++xx) {
                    const u8* p = row + static_cast<usize>(xx) * channels;
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; ++cnt;
                }
            }
            u8* d = out.rgb.data() + (static_cast<usize>(y) * ow + x) * 3;
            for (u32 c = 0; c < 3; ++c) d[c] = static_cast<u8>((sum[c] + cnt / 2) / std::max(1u, cnt));
        }
    }
}

void roto_grid_rgb(const RotoImage& image, u32 n, std::vector<u8>& out) {
    out.assign(static_cast<usize>(n) * n * 3, 0);
    if (!image.width || !image.height || image.rgb.size() < static_cast<usize>(image.width) * image.height * 3) return;
    const u32 W = image.width, H = image.height;
    for (u32 gy = 0; gy < n; ++gy) {
        const u32 y0 = static_cast<u32>(static_cast<u64>(gy) * H / n);
        const u32 y1 = std::max(y0 + 1, static_cast<u32>(static_cast<u64>(gy + 1) * H / n));
        for (u32 gx = 0; gx < n; ++gx) {
            const u32 x0 = static_cast<u32>(static_cast<u64>(gx) * W / n);
            const u32 x1 = std::max(x0 + 1, static_cast<u32>(static_cast<u64>(gx + 1) * W / n));
            u32 sum[3]{}, cnt = 0;
            for (u32 y = y0; y < y1 && y < H; ++y) for (u32 x = x0; x < x1 && x < W; ++x) {
                const u8* p = &image.rgb[(static_cast<usize>(y) * W + x) * 3];
                sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; ++cnt;
            }
            u8* d = &out[(static_cast<usize>(gy) * n + gx) * 3];
            for (u32 c = 0; c < 3; ++c) d[c] = static_cast<u8>((sum[c] + cnt / 2) / std::max(1u, cnt));
        }
    }
}

namespace {

/// O que todos os blocos de um quadro leem (só leitura: os blocos rodam em paralelo).
struct RefineJob {
    const RotoImage* frame = nullptr;
    const Coarse* coarse = nullptr;
    const std::vector<f32>* dist = nullptr;   // n·n, px até a borda 0,5
    const RotoTemporal* temporal = nullptr;   // nulo = sem trepidação
    RotoRefined* out = nullptr;
    u32 W = 0, H = 0, n = 0;
    f32 R = 0;
    u32 s = 1, rwL = 1, rgL = 1, rdL = 0, margin = 0;
    f32 chatter = 0;
    bool decon = false;
};

f32 node_distance(const RefineJob& j, u32 x, u32 y) noexcept {
    const Coarse& C = *j.coarse;
    const std::vector<f32>& d = *j.dist;
    const u32 n = j.n;
    const u32 a = C.x0[x], b = C.y0[y], a1 = std::min(a + 1, n - 1), b1 = std::min(b + 1, n - 1);
    const f32 u = C.fx[x], w = C.fy[y];
    const f32 top = d[static_cast<usize>(b) * n + a] + (d[static_cast<usize>(b) * n + a1] - d[static_cast<usize>(b) * n + a]) * u;
    const f32 bot = d[static_cast<usize>(b1) * n + a] + (d[static_cast<usize>(b1) * n + a1] - d[static_cast<usize>(b1) * n + a]) * u;
    return top + (bot - top) * w;
}

/// O deslocamento (px do quadro) do fluxo da grade num pixel.
Vec2 flow_at(const RefineJob& j, u32 x, u32 y) noexcept {
    const RotoTemporal& t = *j.temporal;
    const Coarse& C = *j.coarse;
    const f32 gx = std::clamp((static_cast<f32>(x) + 0.5f) * C.sx - 0.5f, 0.0f, static_cast<f32>(t.n - 1));
    const f32 gy = std::clamp((static_cast<f32>(y) + 0.5f) * C.sy - 0.5f, 0.0f, static_cast<f32>(t.n - 1));
    const u32 i0 = std::min(static_cast<u32>(gx), t.n - 2), j0 = std::min(static_cast<u32>(gy), t.n - 2);
    const f32 u = gx - static_cast<f32>(i0), w = gy - static_cast<f32>(j0);
    const usize tn = t.n;
    const Vec2 f00 = t.flow[j0 * tn + i0], f10 = t.flow[j0 * tn + i0 + 1];
    const Vec2 f01 = t.flow[(j0 + 1) * tn + i0], f11 = t.flow[(j0 + 1) * tn + i0 + 1];
    return Vec2{((f00.x + (f10.x - f00.x) * u) * (1 - w) + (f01.x + (f11.x - f01.x) * u) * w) / C.sx,
                ((f00.y + (f10.y - f00.y) * u) * (1 - w) + (f01.y + (f11.y - f01.y) * u) * w) / C.sy};
}

/// Média da grade reduzida: soma/contagem por célula e caixa no raio pedido.
void finish_planes(Work& wk, u32 first, u32 count, usize lN, u32 lw, u32 lh, u32 radius, const std::vector<f32>& cnt) {
    for (u32 ch = first; ch < first + count; ++ch) {
        f32* plane = &wk.low[ch * lN];
        for (usize c = 0; c < lN; ++c) plane[c] /= std::max(1.0f, cnt[c]);
        box_mean(plane, plane, lw, lh, radius, wk.tmp, wk.col);
    }
}

void refine_tile(const RefineJob& j, u32 tx, u32 ty, u32 tx1, u32 ty1, Work& wk) {
    const u32 W = j.W, H = j.H, s = j.s;
    const f32 R = j.R;
    const Coarse& C = *j.coarse;
    const std::vector<u8>& rgb = j.frame->rgb;
    RotoRefined& out = *j.out;
    const u32 margin = j.margin;
    const u32 rx0 = tx >= margin ? tx - margin : 0, ry0 = ty >= margin ? ty - margin : 0;
    const u32 rx1 = std::min(W, tx1 + margin), ry1 = std::min(H, ty1 + margin);
    const u32 rW = rx1 - rx0, rH = ry1 - ry0;
    const usize rN = static_cast<usize>(rW) * rH;
    const u32 lw = (rW + s - 1) / s, lh = (rH + s - 1) / s;
    const usize lN = static_cast<usize>(lw) * lh;
    LowSampler ls;
    ls.init(rW, rH, lw, lh, s);
    std::vector<f32> cnt(lN, 0.0f);

    // 1) Quadro, recorte ampliado e trimapa na região (bloco + margem).
    wk.I.resize(rN * 3); wk.v.resize(rN); wk.wband.resize(rN); wk.cls.resize(rN);
    for (u32 y = 0; y < rH; ++y) {
        const u8* src = &rgb[(static_cast<usize>(ry0 + y) * W + rx0) * 3];
        f32* I = &wk.I[static_cast<usize>(y) * rW * 3];
        for (u32 x = 0; x < rW * 3; ++x) I[x] = src[x] * (1.0f / 255.0f);
        for (u32 x = 0; x < rW; ++x) {
            f32 v, gx, gy;
            C.at(rx0 + x, ry0 + y, v, gx, gy);
            // Na transição, o gradiente dá a distância fina; fora dela, o chanfro.
            const f32 g = std::sqrt(gx * gx + gy * gy);
            const f32 d = std::min(std::fabs(v - 0.5f) / std::max(g, 1e-6f), node_distance(j, rx0 + x, ry0 + y));
            const bool soft = v > kSoftLo && v < kSoftHi;
            const usize i = static_cast<usize>(y) * rW + x;
            wk.v[i] = v;
            if (soft || d < R) { wk.cls[i] = 0; wk.wband[i] = soft ? 1.0f : 1.0f - smooth01(0.7f * R, R, d); }
            else { wk.cls[i] = v >= 0.5f ? 1 : 2; wk.wband[i] = 0.0f; }
            cnt[static_cast<usize>(y / s) * lw + x / s] += 1.0f;
        }
    }
    // 2) Cores certas vizinhas: 0-2 I·[obj], 3 [obj], 4-6 I·[fundo], 7 [fundo].
    enum : u32 { kFgI = 0, kFg = 3, kBgI = 4, kBg = 7, kModel = 8 };
    wk.low.assign(lN * kModel, 0.0f);
    for (u32 y = 0; y < rH; ++y) for (u32 x = 0; x < rW; ++x) {
        const usize i = static_cast<usize>(y) * rW + x;
        if (!wk.cls[i]) continue;
        const usize c = static_cast<usize>(y / s) * lw + x / s;
        const u32 base = wk.cls[i] == 1 ? kFgI : kBgI;
        for (u32 k = 0; k < 3; ++k) wk.low[(base + k) * lN + c] += wk.I[i * 3 + k];
        wk.low[(base + 3) * lN + c] += 1.0f;
    }
    finish_planes(wk, 0, kModel, lN, lw, lh, j.rwL, cnt);
    // Estimativa do alfa: projeção na reta fundo→objeto; fora da reta (cabelo
    // de outra cor), a distância até o fundo. Confiança pela separação das
    // cores e pela presença das duas na janela; sem confiança, o recorte.
    wk.p.resize(rN);
    for (u32 y = 0; y < rH; ++y) for (u32 x = 0; x < rW; ++x) {
        const usize i = static_cast<usize>(y) * rW + x;
        if (wk.cls[i]) { wk.p[i] = wk.cls[i] == 1 ? 1.0f : 0.0f; continue; }
        const f32 fw = ls.at(&wk.low[kFg * lN], x, y), bw = ls.at(&wk.low[kBg * lN], x, y);
        f32 F[3], B[3];
        for (u32 k = 0; k < 3; ++k) {
            F[k] = ls.at(&wk.low[(kFgI + k) * lN], x, y) / std::max(fw, 1e-6f);
            B[k] = ls.at(&wk.low[(kBgI + k) * lN], x, y) / std::max(bw, 1e-6f);
        }
        const f32* I = &wk.I[i * 3];
        const f32 d0 = F[0] - B[0], d1 = F[1] - B[1], d2 = F[2] - B[2];
        const f32 b0 = I[0] - B[0], b1 = I[1] - B[1], b2 = I[2] - B[2];
        const f32 sep2 = d0 * d0 + d1 * d1 + d2 * d2, sep = std::sqrt(sep2);
        const f32 a = std::clamp((b0 * d0 + b1 * d1 + b2 * d2) / std::max(sep2, 1e-6f), 0.0f, 1.0f);
        const f32 e0 = b0 - a * d0, e1 = b1 - a * d1, e2 = b2 - a * d2;
        const f32 res2 = e0 * e0 + e1 * e1 + e2 * e2;
        const f32 ab = std::clamp(std::sqrt(b0 * b0 + b1 * b1 + b2 * b2) / std::max(sep, 1e-3f), 0.0f, 1.0f);
        const f32 off = res2 / (res2 + kOffLine * sep2 + 1e-6f);
        f32 est = a + (std::max(a, ab) - a) * off;
        est = std::clamp((est - kTextureFloor) / (1.0f - kTextureFloor), 0.0f, 1.0f);
        const f32 conf = smooth01(0.04f, 0.15f, sep) * smooth01(0.01f, 0.08f, std::min(fw, bw));
        wk.p[i] = wk.v[i] + (est - wk.v[i]) * conf;
    }
    // 3) Filtro guiado rápido (guia = cor) na grade reduzida.
    enum : u32 { gI = 0, gP = 3, gIP = 4, gII = 7, kGuide = 13 };   // II: rr rg rb gg gb bb
    wk.low.assign(lN * kGuide, 0.0f);
    for (u32 y = 0; y < rH; ++y) for (u32 x = 0; x < rW; ++x) {
        const usize i = static_cast<usize>(y) * rW + x;
        const usize c = static_cast<usize>(y / s) * lw + x / s;
        const f32* I = &wk.I[i * 3];
        const f32 p = wk.p[i];
        wk.low[(gI + 0) * lN + c] += I[0]; wk.low[(gI + 1) * lN + c] += I[1]; wk.low[(gI + 2) * lN + c] += I[2];
        wk.low[gP * lN + c] += p;
        wk.low[(gIP + 0) * lN + c] += I[0] * p; wk.low[(gIP + 1) * lN + c] += I[1] * p; wk.low[(gIP + 2) * lN + c] += I[2] * p;
        wk.low[(gII + 0) * lN + c] += I[0] * I[0]; wk.low[(gII + 1) * lN + c] += I[0] * I[1];
        wk.low[(gII + 2) * lN + c] += I[0] * I[2]; wk.low[(gII + 3) * lN + c] += I[1] * I[1];
        wk.low[(gII + 4) * lN + c] += I[1] * I[2]; wk.low[(gII + 5) * lN + c] += I[2] * I[2];
    }
    finish_planes(wk, 0, kGuide, lN, lw, lh, j.rgL, cnt);
    // a = (Σ + εU)⁻¹·cov(I, p); b = p̄ − a·Ī (nos planos 0-3), depois a média deles.
    for (usize c = 0; c < lN; ++c) {
        const f32 m0 = wk.low[(gI + 0) * lN + c], m1 = wk.low[(gI + 1) * lN + c], m2 = wk.low[(gI + 2) * lN + c];
        const f32 mp = wk.low[gP * lN + c];
        const f32 cov[3] = {wk.low[(gIP + 0) * lN + c] - m0 * mp, wk.low[(gIP + 1) * lN + c] - m1 * mp, wk.low[(gIP + 2) * lN + c] - m2 * mp};
        const f32 s00 = wk.low[(gII + 0) * lN + c] - m0 * m0 + kGuidedEps, s01 = wk.low[(gII + 1) * lN + c] - m0 * m1;
        const f32 s02 = wk.low[(gII + 2) * lN + c] - m0 * m2, s11 = wk.low[(gII + 3) * lN + c] - m1 * m1 + kGuidedEps;
        const f32 s12 = wk.low[(gII + 4) * lN + c] - m1 * m2, s22 = wk.low[(gII + 5) * lN + c] - m2 * m2 + kGuidedEps;
        f32 a[3]{0, 0, 0};
        if (!solve_sym3(s00, s01, s02, s11, s12, s22, cov, a)) a[0] = a[1] = a[2] = 0.0f;
        wk.low[0 * lN + c] = a[0]; wk.low[1 * lN + c] = a[1]; wk.low[2 * lN + c] = a[2];
        wk.low[3 * lN + c] = mp - a[0] * m0 - a[1] * m1 - a[2] * m2;
    }
    for (u32 ch = 0; ch < 4; ++ch) box_mean(&wk.low[ch * lN], &wk.low[ch * lN], lw, lh, j.rgL, wk.tmp, wk.col);

    // 4) Alfa: q = ā·I + b̄ na faixa; certo fora dela; costura suave na beira.
    //    Com descontaminar, a região inteira (a mistura precisa do alfa em volta).
    wk.alpha.resize(rN);
    const u32 ax0 = j.decon ? 0 : tx - rx0, ay0 = j.decon ? 0 : ty - ry0;
    const u32 ax1 = j.decon ? rW : tx1 - rx0, ay1 = j.decon ? rH : ty1 - ry0;
    const bool temporal = j.temporal != nullptr;
    for (u32 y = ay0; y < ay1; ++y) for (u32 x = ax0; x < ax1; ++x) {
        const usize i = static_cast<usize>(y) * rW + x;
        const f32 hard = wk.v[i] >= 0.5f ? 1.0f : 0.0f;
        if (wk.cls[i]) { wk.alpha[i] = hard; continue; }
        const f32* I = &wk.I[i * 3];
        f32 q = std::clamp(ls.at(&wk.low[0], x, y) * I[0] + ls.at(&wk.low[lN], x, y) * I[1]
                           + ls.at(&wk.low[2 * lN], x, y) * I[2] + ls.at(&wk.low[3 * lN], x, y), 0.0f, 1.0f);
        // Névoa da textura (alfa de poucos %): rampa quadrática até kHaze,
        // contínua — some o véu, o fio de cabelo fica.
        if (q < kHaze) q = q * q / kHaze;
        else if (q > 1.0f - kHaze) q = 1.0f - (1.0f - q) * (1.0f - q) / kHaze;
        f32 a = hard + (q - hard) * wk.wband[i];
        if (temporal) {
            // O quadro anterior levado pelo fluxo; só a diferença pequena (ruído) mistura.
            const Vec2 f = flow_at(j, rx0 + x, ry0 + y);
            const f32 warped = sample_u8(j.temporal->prev->alpha, W, H, static_cast<f32>(rx0 + x) + f.x, static_cast<f32>(ry0 + y) + f.y);
            const f32 diff = (a - warped) / kChatterTau;
            a += (warped - a) * j.chatter * std::exp(-diff * diff);
        }
        wk.alpha[i] = to_u8(a) * (1.0f / 255.0f);   // o valor guardado (u8) é o da cadeia
    }
    for (u32 y = ty; y < ty1; ++y) for (u32 x = tx; x < tx1; ++x)
        out.alpha[static_cast<usize>(y) * W + x] = to_u8(wk.alpha[static_cast<usize>(y - ry0) * rW + (x - rx0)]);
    if (!j.decon) return;

    // 5) Descontaminar: Blur-Fusion ×2. Primeiro F̂/B̂ largos (grade reduzida),
    //    F1/B1 = F̂ + α(I − αF̂ − (1−α)B̂) e o simétrico; depois as médias
    //    ponderadas de F1/B1 numa janela pequena em resolução cheia.
    enum : u32 { dA = 0, dAI = 1, dBI = 4, kDecon = 7 };
    wk.low.assign(lN * kDecon, 0.0f);
    for (u32 y = 0; y < rH; ++y) for (u32 x = 0; x < rW; ++x) {
        const usize i = static_cast<usize>(y) * rW + x;
        const usize c = static_cast<usize>(y / s) * lw + x / s;
        const f32 a = wk.alpha[i];
        const f32* I = &wk.I[i * 3];
        wk.low[dA * lN + c] += a;
        for (u32 k = 0; k < 3; ++k) { wk.low[(dAI + k) * lN + c] += a * I[k]; wk.low[(dBI + k) * lN + c] += (1.0f - a) * I[k]; }
    }
    finish_planes(wk, 0, kDecon, lN, lw, lh, j.rdL, cnt);
    // F1/B1 no bloco + kFuseRadius (planos cheios: α·F1 (3), (1−α)·B1 (3), α).
    const u32 fr = kFuseRadius;
    const u32 fx0 = tx > rx0 + fr ? tx - fr - rx0 : 0, fy0 = ty > ry0 + fr ? ty - fr - ry0 : 0;
    const u32 fx1 = std::min(rW, tx1 - rx0 + fr), fy1 = std::min(rH, ty1 - ry0 + fr);
    const u32 fW = fx1 - fx0, fH = fy1 - fy0;
    const usize fN = static_cast<usize>(fW) * fH;
    wk.fuse.assign(fN * 7, 0.0f);
    for (u32 y = fy0; y < fy1; ++y) for (u32 x = fx0; x < fx1; ++x) {
        const usize i = static_cast<usize>(y) * rW + x;
        const usize o = static_cast<usize>(y - fy0) * fW + (x - fx0);
        const f32 a = wk.alpha[i];
        const f32 sa = ls.at(&wk.low[dA * lN], x, y), sb = 1.0f - sa;
        const f32* I = &wk.I[i * 3];
        for (u32 k = 0; k < 3; ++k) {
            const f32 Fh = sa > 1e-4f ? ls.at(&wk.low[(dAI + k) * lN], x, y) / sa : I[k];
            const f32 Bh = sb > 1e-4f ? ls.at(&wk.low[(dBI + k) * lN], x, y) / sb : I[k];
            const f32 resid = I[k] - a * Fh - (1.0f - a) * Bh;
            const f32 F1 = std::clamp(Fh + a * resid, 0.0f, 1.0f), B1 = std::clamp(Bh + (1.0f - a) * resid, 0.0f, 1.0f);
            wk.fuse[k * fN + o] = a * F1;
            wk.fuse[(3 + k) * fN + o] = (1.0f - a) * B1;
        }
        wk.fuse[6 * fN + o] = a;
    }
    for (u32 ch = 0; ch < 7; ++ch) box_mean(&wk.fuse[ch * fN], &wk.fuse[ch * fN], fW, fH, fr, wk.tmp, wk.col);
    for (u32 y = ty; y < ty1; ++y) for (u32 x = tx; x < tx1; ++x) {
        const usize o = static_cast<usize>(y) * W + x;
        const u8 a8 = out.alpha[o];
        if (a8 == 0 || a8 == 255) continue;
        const u32 lx = x - rx0, ly = y - ry0;
        const usize i = static_cast<usize>(ly) * rW + lx;
        const usize q = static_cast<usize>(ly - fy0) * fW + (lx - fx0);
        const f32 a = a8 * (1.0f / 255.0f);
        const f32 sa = wk.fuse[6 * fN + q], sb = 1.0f - sa;
        const f32* I = &wk.I[i * 3];
        for (u32 k = 0; k < 3; ++k) {
            const f32 Fh = sa > 1e-4f ? wk.fuse[k * fN + q] / sa : I[k];
            const f32 Bh = sb > 1e-4f ? wk.fuse[(3 + k) * fN + q] / sb : I[k];
            out.fg[o * 3 + k] = to_u8(Fh + a * (I[k] - a * Fh - (1.0f - a) * Bh));
        }
    }
}

} // namespace

u32 g_rotoRefineThreads = 0;

void roto_refine(const f32* coarse, u32 n, const RotoImage& frame, const RotoRefineSettings& settings,
                 const RotoTemporal* temporal, RotoRefined& out) {
    const u32 W = frame.width, H = frame.height;
    out.width = W; out.height = H;
    out.alpha.assign(static_cast<usize>(W) * H, 0);
    out.fg.clear();
    if (!coarse || n < 2 || !W || !H || frame.rgb.size() < static_cast<usize>(W) * H * 3) return;
    const f32 R = std::clamp(settings.radius, 0.5f, 256.0f);
    RefineJob job;
    job.decon = settings.decontaminate;
    if (job.decon) out.fg.assign(static_cast<usize>(W) * H * 3, 0);
    Coarse C;
    C.init(coarse, n, W, H);

    // Células da grade com borda (0,5 atravessa) ou incerteza: soma de prefixo
    // para saber num O(1) se um bloco corta a faixa.
    const u32 cn = n - 1;
    std::vector<u32> edgePre(static_cast<usize>(cn + 1) * (cn + 1), 0);
    for (u32 jj = 0; jj < cn; ++jj) for (u32 i = 0; i < cn; ++i) {
        const f32 a = coarse[static_cast<usize>(jj) * n + i], b = coarse[static_cast<usize>(jj) * n + i + 1];
        const f32 c = coarse[static_cast<usize>(jj + 1) * n + i], d = coarse[static_cast<usize>(jj + 1) * n + i + 1];
        const f32 lo = std::min({a, b, c, d}), hi = std::max({a, b, c, d});
        const bool edge = (lo < 0.5f && hi >= 0.5f) || (hi > kSoftLo && lo < kSoftHi);
        edgePre[static_cast<usize>(jj + 1) * (cn + 1) + i + 1] = (edge ? 1u : 0u)
            + edgePre[static_cast<usize>(jj) * (cn + 1) + i + 1] + edgePre[static_cast<usize>(jj + 1) * (cn + 1) + i]
            - edgePre[static_cast<usize>(jj) * (cn + 1) + i];
    }
    auto cells_edge = [&](i32 px0, i32 py0, i32 px1, i32 py1) {
        // px → célula da grade (a célula i vai do nó i ao i+1).
        auto cell = [&](f32 p, f32 sc) { return static_cast<i32>(std::floor((p + 0.5f) * sc - 0.5f)); };
        const i32 i0 = std::clamp(cell(static_cast<f32>(px0), C.sx) - 1, 0, static_cast<i32>(cn) - 1);
        const i32 i1 = std::clamp(cell(static_cast<f32>(px1), C.sx) + 1, 0, static_cast<i32>(cn) - 1);
        const i32 j0 = std::clamp(cell(static_cast<f32>(py0), C.sy) - 1, 0, static_cast<i32>(cn) - 1);
        const i32 j1 = std::clamp(cell(static_cast<f32>(py1), C.sy) + 1, 0, static_cast<i32>(cn) - 1);
        const usize st = cn + 1;
        return edgePre[(j1 + 1) * st + i1 + 1] - edgePre[j0 * st + i1 + 1] - edgePre[(j1 + 1) * st + i0] + edgePre[j0 * st + i0] > 0;
    };

    // Distância (px) de cada nó da grade até a borda 0,5: semente no ponto em
    // que a reta entre dois nós vizinhos cruza 0,5; depois chanfro 8-vizinhos
    // com os passos em px (a grade é anisotrópica: 1920/320 × 1080/320).
    // O gradiente local sozinho só vale dentro da transição — longe dela o
    // recorte é plano (0 ou 1) e a distância sumia.
    std::vector<f32> dist(static_cast<usize>(n) * n, 1e30f);
    {
        const f32 wx = static_cast<f32>(W) / static_cast<f32>(n), wy = static_cast<f32>(H) / static_cast<f32>(n);
        const f32 wd = std::sqrt(wx * wx + wy * wy);
        for (u32 jj = 0; jj < n; ++jj) for (u32 i = 0; i < n; ++i) {
            const usize at = static_cast<usize>(jj) * n + i;
            const f32 v = coarse[at];
            f32& d = dist[at];
            if (v > kSoftLo && v < kSoftHi) { d = 0.0f; continue; }
            auto cross = [&](usize k, f32 step) {
                const f32 u = coarse[k];
                if ((u >= 0.5f) == (v >= 0.5f)) return;
                d = std::min(d, std::fabs((0.5f - v) / (u - v)) * step);
            };
            if (i) cross(at - 1, wx);
            if (i + 1 < n) cross(at + 1, wx);
            if (jj) cross(at - n, wy);
            if (jj + 1 < n) cross(at + n, wy);
        }
        for (u32 pass = 0; pass < 2; ++pass) {
            for (u32 jj = 0; jj < n; ++jj) for (u32 i = 0; i < n; ++i) {
                const usize at = static_cast<usize>(jj) * n + i;
                f32 d = dist[at];
                if (i) d = std::min(d, dist[at - 1] + wx);
                if (jj) {
                    d = std::min(d, dist[at - n] + wy);
                    if (i) d = std::min(d, dist[at - n - 1] + wd);
                    if (i + 1 < n) d = std::min(d, dist[at - n + 1] + wd);
                }
                dist[at] = d;
            }
            for (u32 jj = n; jj-- > 0;) for (u32 i = n; i-- > 0;) {
                const usize at = static_cast<usize>(jj) * n + i;
                f32 d = dist[at];
                if (i + 1 < n) d = std::min(d, dist[at + 1] + wx);
                if (jj + 1 < n) {
                    d = std::min(d, dist[at + n] + wy);
                    if (i + 1 < n) d = std::min(d, dist[at + n + 1] + wd);
                    if (i) d = std::min(d, dist[at + n - 1] + wd);
                }
                dist[at] = d;
            }
        }
    }

    // Grade reduzida conforme o raio: o custo por bloco fica parecido do
    // raio 2 ao 60 (o filtro rápido de He: coeficientes em 1/s, detalhe da guia).
    job.frame = &frame; job.coarse = &C; job.dist = &dist; job.out = &out;
    job.W = W; job.H = H; job.n = n; job.R = R;
    job.s = R <= 6.0f ? 1u : (R <= 16.0f ? 2u : (R <= 48.0f ? 4u : 8u));
    const f32 sf = static_cast<f32>(job.s);
    job.rwL = std::max(1u, static_cast<u32>(std::ceil((2.0f * R + 2.0f) / sf)));      // janela das cores certas
    job.rgL = std::max(1u, static_cast<u32>(std::lround(R * kGuidedRadius / sf)));    // janela do filtro guiado
    job.rdL = job.decon ? std::max(1u, static_cast<u32>(std::ceil(kDeconRadius / sf))) : 0u;
    job.margin = job.s * (job.rwL + 2 * job.rgL + job.rdL + 2) + (job.decon ? kFuseRadius : 0u);
    const bool useTemporal = temporal && temporal->prev && temporal->flow && temporal->n >= 2 && settings.chatter > 0.0f
        && temporal->prev->width == W && temporal->prev->height == H && temporal->prev->alpha.size() == out.alpha.size();
    job.temporal = useTemporal ? temporal : nullptr;
    job.chatter = std::clamp(settings.chatter, 0.0f, 1.0f) * kChatterMax;

    // Bloco ~2× a margem (a margem refeita pesa pouco), entre 64 e 256 px.
    const u32 tile = std::clamp((2 * job.margin + 63) / 64 * 64, 64u, 256u);
    struct Tile { u32 x0, y0, x1, y1; };
    std::vector<Tile> tiles;
    const i32 pad = static_cast<i32>(std::ceil(R)) + 1;
    for (u32 ty = 0; ty < H; ty += tile) for (u32 tx = 0; tx < W; tx += tile) {
        const u32 tx1 = std::min(W, tx + tile), ty1 = std::min(H, ty + tile);
        if (cells_edge(static_cast<i32>(tx) - pad, static_cast<i32>(ty) - pad, static_cast<i32>(tx1) + pad, static_cast<i32>(ty1) + pad)) {
            tiles.push_back({tx, ty, tx1, ty1});
            continue;
        }
        // Bloco longe da borda: objeto ou fundo inteiro.
        f32 v, gx, gy;
        C.at((tx + tx1) / 2, (ty + ty1) / 2, v, gx, gy);
        const u8 a = v >= 0.5f ? 255 : 0;
        for (u32 y = ty; y < ty1; ++y) std::memset(&out.alpha[static_cast<usize>(y) * W + tx], a, tx1 - tx);
    }
    // Blocos independentes (escrevem só o próprio retângulo): em paralelo, o
    // resultado não depende da ordem.
    u32 threads = g_rotoRefineThreads ? g_rotoRefineThreads
                                      : std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u);
    threads = std::min<u32>(threads, std::max<usize>(1, tiles.size() / 2));
    // Memória: cada thread guarda a região do bloco (~32 B por px, mais a grade
    // reduzida); juntas, até kThreadBudget (32 bits: raio grande = uma thread).
    const usize region = static_cast<usize>(tile + 2 * job.margin) * (tile + 2 * job.margin);
    threads = std::clamp<u32>(static_cast<u32>(kThreadBudget / std::max<usize>(1, region * 32)), 1u, threads);
    std::atomic<u32> next{0};
    auto run = [&] {
        Work wk;
        for (u32 t; (t = next.fetch_add(1, std::memory_order_relaxed)) < tiles.size();)
            refine_tile(job, tiles[t].x0, tiles[t].y0, tiles[t].x1, tiles[t].y1, wk);
    };
    std::vector<std::thread> pool;
    try {
        for (u32 t = 1; t < threads; ++t) pool.emplace_back(run);
    } catch (const std::system_error&) {
        // Sem thread nova: os blocos que sobram rodam aqui mesmo.
    }
    run();
    for (std::thread& t : pool) t.join();
}

// ----------------------------------------------------------- compactar ----

namespace {
void packbits(const u8* data, usize n, std::vector<u8>& out) {
    usize i = 0;
    while (i < n) {
        usize run = 1;
        while (i + run < n && run < 129 && data[i + run] == data[i]) ++run;
        if (run >= 2) {
            out.push_back(static_cast<u8>(126 + run));   // 128..255 → repete 2..129
            out.push_back(data[i]);
            i += run;
            continue;
        }
        // Literal até a próxima repetição (ou 128 bytes).
        const usize start = i;
        usize len = 0;
        while (i < n && len < 128) {
            if (i + 1 < n && data[i + 1] == data[i]) break;
            ++i; ++len;
        }
        out.push_back(static_cast<u8>(len - 1));
        out.insert(out.end(), data + start, data + start + len);
    }
}
bool unpackbits(const u8*& at, const u8* end, u8* dst, usize n) {
    usize o = 0;
    while (o < n) {
        if (at >= end) return false;
        const u8 c = *at++;
        if (c < 128) {
            const usize len = static_cast<usize>(c) + 1;
            if (o + len > n || static_cast<usize>(end - at) < len) return false;
            std::memcpy(dst + o, at, len); at += len; o += len;
        } else {
            const usize len = static_cast<usize>(c) - 126;
            if (o + len > n || at >= end) return false;
            std::memset(dst + o, *at++, len); o += len;
        }
    }
    return true;
}
void put32(std::vector<u8>& out, u32 v) { for (u32 i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (i * 8))); }
u32 get32(const u8* p) { return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 | static_cast<u32>(p[3]) << 24; }
} // namespace

void roto_refined_pack(const RotoRefined& r, std::vector<u8>& out) {
    out.clear();
    const usize n = static_cast<usize>(r.width) * r.height;
    if (!n || r.alpha.size() != n) return;
    const bool fg = r.fg.size() == n * 3;
    out.push_back('R'); out.push_back('F'); out.push_back(1); out.push_back(fg ? 1 : 0);
    put32(out, r.width); put32(out, r.height);
    packbits(r.alpha.data(), n, out);
    if (fg) for (usize i = 0; i < n; ++i) {
        const u8 a = r.alpha[i];
        if (a == 0 || a == 255) continue;
        out.insert(out.end(), &r.fg[i * 3], &r.fg[i * 3] + 3);
    }
}

bool roto_refined_unpack(const std::vector<u8>& packed, RotoRefined& out) {
    out = RotoRefined{};
    if (packed.size() < 12 || packed[0] != 'R' || packed[1] != 'F' || packed[2] != 1) return false;
    const bool fg = packed[3] != 0;
    const u32 w = get32(&packed[4]), h = get32(&packed[8]);
    if (!w || !h || w > 16384 || h > 16384) return false;
    const usize n = static_cast<usize>(w) * h;
    out.alpha.resize(n);
    const u8* at = packed.data() + 12;
    const u8* end = packed.data() + packed.size();
    if (!unpackbits(at, end, out.alpha.data(), n)) { out = RotoRefined{}; return false; }
    if (fg) {
        out.fg.assign(n * 3, 0);
        for (usize i = 0; i < n; ++i) {
            const u8 a = out.alpha[i];
            if (a == 0 || a == 255) continue;
            if (static_cast<usize>(end - at) < 3) { out = RotoRefined{}; return false; }
            std::memcpy(&out.fg[i * 3], at, 3); at += 3;
        }
    }
    if (at != end) { out = RotoRefined{}; return false; }
    out.width = w; out.height = h;
    return true;
}

} // namespace aurea::ai
