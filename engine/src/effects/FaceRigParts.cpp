// =============================================================================
//  Aurea / effects / FaceRigParts.cpp
//
//  Rig 2D de rosto v2 (effects/FaceRigParts.hpp): separar a imagem em partes
//  como um artista de rig, preencher o que cada parte esconde e o movimento de
//  cada parte. Sem IA, determinístico, tudo na CPU:
//
//   1. Cores em Lab; modelo de pele pelas amostras das bochechas/queixo (âncoras
//      do v1); paleta do cabelo pela faixa do topo da silhueta (k-médias).
//   2. Rótulos por prioridade: óculos (estrutura fina de cor própria que cruza
//      os dois olhos, achada pela ponte entre eles), olhos (branco neutro,
//      íris colorida/escura num disco, cílios escuros acima), boca (buraco na
//      pele perto da âncora, com os buracos dela preenchidos: dentes),
//      sobrancelhas, nariz, cabelo (núcleo grosso: o contorno fino não conta),
//      pele, corpo abaixo do queixo; o resto (contornos, fios soltos) herda do
//      vizinho mais perto.
//   3. Topologia: cabelo dentro do contorno do rosto = franja; cabelo abaixo
//      do queixo na frente do corpo = mechas; o resto = cabelo de trás. Pele
//      abaixo do queixo = pescoço (na coluna dele) ou corpo (mãos).
//   4. Pertinência suave (3×3) e alfa de cada parte resolvido de trás para a
//      frente para que a composição "sobre" devolva a imagem; atrás das partes
//      da frente, a parte de trás é preenchida (empurra-puxa + membrana) e a
//      cor da borda da parte da frente é separada da de trás (sem fantasma).
// =============================================================================
#include "aurea/effects/FaceRigParts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>

namespace aurea::face_rig {
namespace {

constexpr u8 kNone = 255;

f32 sstep(f32 e0, f32 e1, f32 x) noexcept {
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    const f32 t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

Vec2 rot(Vec2 v, f32 a) noexcept {
    const f32 c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

// --- Cor -------------------------------------------------------------------------

struct LabTable {
    f32 lin[256];
    LabTable() noexcept {
        for (u32 i = 0; i < 256; ++i) {
            const f32 c = static_cast<f32>(i) / 255.0f;
            lin[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
    }
};

f32 srgb_to_linear(u8 v) noexcept {
    static const LabTable table;
    return table.lin[v];
}

u8 linear_to_srgb8(f32 v) noexcept {
    const f32 x = saturate(v);
    const f32 s = x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(std::lround(saturate(s) * 255.0f));
}

f32 lab_f(f32 t) noexcept { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; }

void to_lab(u8 r8, u8 g8, u8 b8, f32& L, f32& A, f32& B) noexcept {
    static const LabTable table;
    const f32 r = table.lin[r8], g = table.lin[g8], b = table.lin[b8];
    const f32 x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f;
    const f32 y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    const f32 z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
    const f32 fx = lab_f(x), fy = lab_f(y), fz = lab_f(z);
    L = 116.0f * fy - 16.0f;
    A = 500.0f * (fx - fy);
    B = 200.0f * (fy - fz);
}

struct Canvas {
    u32 w = 0, h = 0;
    usize n = 0;
    const u8* rgba = nullptr;
    std::vector<f32> L, A, B, alpha;
    std::vector<u8> fg;    ///< figura (alfa ≥ 0,5, ou tudo sem transparência)
    std::vector<u8> vis;   ///< qualquer alfa
    bool hasAlpha = false;
    [[nodiscard]] usize at(i32 x, i32 y) const noexcept { return static_cast<usize>(y) * w + static_cast<usize>(x); }
    [[nodiscard]] f32 chroma(usize i) const noexcept { return std::sqrt(A[i] * A[i] + B[i] * B[i]); }
};

Canvas make_canvas(const u8* rgba, u32 w, u32 h) {
    Canvas c;
    c.w = w;
    c.h = h;
    c.n = static_cast<usize>(w) * h;
    c.rgba = rgba;
    c.L.resize(c.n);
    c.A.resize(c.n);
    c.B.resize(c.n);
    c.alpha.resize(c.n);
    c.fg.assign(c.n, 0);
    c.vis.assign(c.n, 0);
    usize clear = 0;
    for (usize i = 0; i < c.n; ++i) {
        const u8* p = rgba + i * 4;
        to_lab(p[0], p[1], p[2], c.L[i], c.A[i], c.B[i]);
        c.alpha[i] = static_cast<f32>(p[3]) / 255.0f;
        clear += p[3] < 128 ? 1u : 0u;
    }
    c.hasAlpha = clear * 100 > c.n * 3;
    for (usize i = 0; i < c.n; ++i) {
        c.vis[i] = c.alpha[i] > 0.0f ? 1 : 0;
        c.fg[i] = !c.hasAlpha || c.alpha[i] >= 0.5f ? 1 : 0;
    }
    return c;
}

// --- Máscaras --------------------------------------------------------------------

struct Box {
    i32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;   ///< [x0, x1) × [y0, y1)
    [[nodiscard]] bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
};

Box box_of(f32 x0, f32 y0, f32 x1, f32 y1, u32 w, u32 h) noexcept {
    Box b;
    b.x0 = std::clamp(static_cast<i32>(std::floor(x0)), 0, static_cast<i32>(w));
    b.y0 = std::clamp(static_cast<i32>(std::floor(y0)), 0, static_cast<i32>(h));
    b.x1 = std::clamp(static_cast<i32>(std::ceil(x1)), 0, static_cast<i32>(w));
    b.y1 = std::clamp(static_cast<i32>(std::ceil(y1)), 0, static_cast<i32>(h));
    return b;
}

bool in_ellipse(f32 x, f32 y, Vec2 c, Vec2 r) noexcept {
    const f32 a = (x - c.x) / std::max(r.x, 0.5f), b = (y - c.y) / std::max(r.y, 0.5f);
    return a * a + b * b <= 1.0f;
}

struct Comp {
    u32 n = 0;
    i32 x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    f64 sx = 0.0, sy = 0.0;
    [[nodiscard]] f32 cx() const noexcept { return static_cast<f32>(sx / std::max<u32>(n, 1)); }
    [[nodiscard]] f32 cy() const noexcept { return static_cast<f32>(sy / std::max<u32>(n, 1)); }
};

/// Componentes de `mask` (≠ 0) dentro de `b`. `id` (tamanho da imagem) recebe
/// 1..N (0 = fora). Ordem de varredura: determinística.
u32 components(const std::vector<u8>& mask, u32 w, Box b, bool eight, std::vector<i32>& id, std::vector<Comp>& out) {
    id.assign(mask.size(), 0);
    out.clear();
    std::vector<usize> stack;
    for (i32 y = b.y0; y < b.y1; ++y)
        for (i32 x = b.x0; x < b.x1; ++x) {
            const usize s = static_cast<usize>(y) * w + static_cast<usize>(x);
            if (!mask[s] || id[s]) continue;
            Comp c;
            const i32 label = static_cast<i32>(out.size()) + 1;
            id[s] = label;
            stack.push_back(s);
            while (!stack.empty()) {
                const usize k = stack.back();
                stack.pop_back();
                const i32 kx = static_cast<i32>(k % w), ky = static_cast<i32>(k / w);
                ++c.n;
                c.sx += kx;
                c.sy += ky;
                c.x0 = std::min(c.x0, kx); c.x1 = std::max(c.x1, kx);
                c.y0 = std::min(c.y0, ky); c.y1 = std::max(c.y1, ky);
                for (i32 dy = -1; dy <= 1; ++dy)
                    for (i32 dx = -1; dx <= 1; ++dx) {
                        if ((dx == 0 && dy == 0) || (!eight && dx != 0 && dy != 0)) continue;
                        const i32 nx = kx + dx, ny = ky + dy;
                        if (nx < b.x0 || ny < b.y0 || nx >= b.x1 || ny >= b.y1) continue;
                        const usize q = static_cast<usize>(ny) * w + static_cast<usize>(nx);
                        if (!mask[q] || id[q]) continue;
                        id[q] = label;
                        stack.push_back(q);
                    }
            }
            out.push_back(c);
        }
    return static_cast<u32>(out.size());
}

/// Máximo (dilatar) ou mínimo (erodir) numa janela quadrada de raio `r`.
void morph(std::vector<u8>& m, u32 w, u32 h, i32 r, bool dilate) {
    if (r <= 0) return;
    std::vector<u8> tmp(m.size());
    const i32 W = static_cast<i32>(w), H = static_cast<i32>(h);
    const i32 full = 2 * r + 1;
    for (i32 y = 0; y < H; ++y) {
        i32 count = 0;
        const usize row = static_cast<usize>(y) * w;
        for (i32 x = -r; x < W + r; ++x) {
            const i32 add = x + r, sub = x - r - 1;
            if (add >= 0 && add < W && m[row + add]) ++count;
            if (sub >= 0 && sub < W && m[row + sub]) --count;
            if (x >= 0 && x < W) tmp[row + x] = dilate ? (count > 0) : (count == full);
        }
    }
    for (i32 x = 0; x < W; ++x) {
        i32 count = 0;
        for (i32 y = -r; y < H + r; ++y) {
            const i32 add = y + r, sub = y - r - 1;
            if (add >= 0 && add < H && tmp[static_cast<usize>(add) * w + x]) ++count;
            if (sub >= 0 && sub < H && tmp[static_cast<usize>(sub) * w + x]) --count;
            if (y >= 0 && y < H) m[static_cast<usize>(y) * w + x] = dilate ? (count > 0) : (count == full);
        }
    }
}

f32 cross(Vec2 o, Vec2 a, Vec2 b) noexcept { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

/// Fecho convexo (cadeia monótona) rasterizado: 1 dentro.
std::vector<u8> hull_mask(std::vector<Vec2> pts, u32 w, u32 h) {
    std::vector<u8> m(static_cast<usize>(w) * h, 0);
    if (pts.size() < 3) return m;
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<Vec2> hull(2 * pts.size());
    usize k = 0;
    for (usize i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f) --k;
        hull[k++] = pts[i];
    }
    for (usize i = pts.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f) --k;
        hull[k++] = pts[i];
    }
    hull.resize(k > 0 ? k - 1 : 0);
    if (hull.size() < 3) return m;
    f32 ymin = 1e30f, ymax = -1e30f;
    for (const Vec2& p : hull) { ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
    const i32 ya = std::max(0, static_cast<i32>(std::floor(ymin))), yb = std::min(static_cast<i32>(h) - 1, static_cast<i32>(std::ceil(ymax)));
    for (i32 y = ya; y <= yb; ++y) {
        const f32 fy = static_cast<f32>(y) + 0.5f;
        f32 xa = 1e30f, xb = -1e30f;
        for (usize i = 0; i < hull.size(); ++i) {
            const Vec2 a = hull[i], b = hull[(i + 1) % hull.size()];
            if ((a.y <= fy && b.y >= fy) || (b.y <= fy && a.y >= fy)) {
                const f32 t = std::fabs(b.y - a.y) < 1e-6f ? 0.0f : (fy - a.y) / (b.y - a.y);
                const f32 x = a.x + t * (b.x - a.x);
                if (std::fabs(b.y - a.y) < 1e-6f) { xa = std::min({xa, a.x, b.x}); xb = std::max({xb, a.x, b.x}); }
                else { xa = std::min(xa, x); xb = std::max(xb, x); }
            }
        }
        if (xb < xa) continue;
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(xa - 0.5f))), x1 = std::min(static_cast<i32>(w) - 1, static_cast<i32>(std::floor(xb - 0.5f)));
        for (i32 x = x0; x <= x1; ++x) m[static_cast<usize>(y) * w + x] = 1;
    }
    return m;
}

/// Pontos das pontas de cada linha da máscara (o fecho convexo só precisa deles).
void row_extremes(const std::vector<u8>& m, u32 w, u32 h, std::vector<Vec2>& pts) {
    for (u32 y = 0; y < h; ++y) {
        i32 a = -1, b = -1;
        for (u32 x = 0; x < w; ++x)
            if (m[static_cast<usize>(y) * w + x]) { if (a < 0) a = static_cast<i32>(x); b = static_cast<i32>(x); }
        if (a < 0) continue;
        pts.push_back(Vec2{static_cast<f32>(a), static_cast<f32>(y)});
        pts.push_back(Vec2{static_cast<f32>(b) + 1.0f, static_cast<f32>(y)});
        pts.push_back(Vec2{static_cast<f32>(a), static_cast<f32>(y) + 1.0f});
        pts.push_back(Vec2{static_cast<f32>(b) + 1.0f, static_cast<f32>(y) + 1.0f});
    }
}

f32 median(std::vector<f32> v) {
    if (v.empty()) return 0.0f;
    const usize k = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

f32 percentile(std::vector<f32> v, f32 q) {
    if (v.empty()) return 0.0f;
    const usize k = std::min(v.size() - 1, static_cast<usize>(q * static_cast<f32>(v.size())));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

struct LabColor {
    f32 L = 0.0f, a = 0.0f, b = 0.0f;
};

f32 dist_lab(const Canvas& c, usize i, const LabColor& k, f32 wl) noexcept {
    const f32 dl = (c.L[i] - k.L) * wl, da = c.A[i] - k.a, db = c.B[i] - k.b;
    return std::sqrt(dl * dl + da * da + db * db);
}

/// k-médias determinístico (início pelos quantis da luminância).
std::vector<LabColor> kmeans(const std::vector<LabColor>& s, u32 k, f32 wl) {
    std::vector<LabColor> c;
    if (s.empty() || k == 0) return c;
    std::vector<LabColor> sorted = s;
    std::stable_sort(sorted.begin(), sorted.end(), [](const LabColor& a, const LabColor& b) { return a.L < b.L; });
    for (u32 i = 0; i < k; ++i) c.push_back(sorted[std::min(sorted.size() - 1, (2 * i + 1) * sorted.size() / (2 * k))]);
    std::vector<u32> owner(s.size(), 0);
    for (u32 it = 0; it < 10; ++it) {
        std::vector<f64> sum(k * 3, 0.0);
        std::vector<u32> cnt(k, 0);
        for (usize i = 0; i < s.size(); ++i) {
            f32 best = 1e30f;
            for (u32 j = 0; j < k; ++j) {
                const f32 dl = (s[i].L - c[j].L) * wl, da = s[i].a - c[j].a, db = s[i].b - c[j].b;
                const f32 d = dl * dl + da * da + db * db;
                if (d < best) { best = d; owner[i] = j; }
            }
            sum[owner[i] * 3] += s[i].L; sum[owner[i] * 3 + 1] += s[i].a; sum[owner[i] * 3 + 2] += s[i].b;
            ++cnt[owner[i]];
        }
        for (u32 j = 0; j < k; ++j)
            if (cnt[j]) c[j] = LabColor{static_cast<f32>(sum[j * 3] / cnt[j]), static_cast<f32>(sum[j * 3 + 1] / cnt[j]), static_cast<f32>(sum[j * 3 + 2] / cnt[j])};
    }
    // Só os grupos que pesam (≥ 4 % das amostras).
    std::vector<u32> cnt(k, 0);
    for (u32 o : owner) ++cnt[o];
    std::vector<LabColor> kept;
    for (u32 j = 0; j < k; ++j)
        if (cnt[j] * 100 >= s.size() * 4) kept.push_back(c[j]);
    return kept;
}

/// O que está em cada pixel antes da topologia.
enum Kind : u8 {
    kKindHair = 200,   ///< cabelo (franja/mechas/trás decidido depois)
    kKindSkin,         ///< pele (rosto/pescoço/orelha/corpo decidido depois)
};

bool front_of(u32 a, u32 b) noexcept { return a > b; }

/// Pertinência suave de um pixel: até 9 papéis com pesos (soma 1), na ordem de desenho.
struct Mix {
    u32 count = 0;
    u8 role[9]{};
    f32 m[9]{};
};

Mix mix_at(const std::vector<u8>& labels, u32 w, u32 h, i32 x, i32 y) noexcept {
    static constexpr f32 kTent[3] = {1.0f, 2.0f, 1.0f};
    Mix mx;
    f32 total = 0.0f;
    for (i32 dy = -1; dy <= 1; ++dy)
        for (i32 dx = -1; dx <= 1; ++dx) {
            const i32 nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= static_cast<i32>(w) || ny >= static_cast<i32>(h)) continue;
            const u8 r = labels[static_cast<usize>(ny) * w + static_cast<usize>(nx)];
            if (r == kNone) continue;
            const f32 wt = kTent[dx + 1] * kTent[dy + 1];
            total += wt;
            u32 j = 0;
            while (j < mx.count && mx.role[j] != r) ++j;
            if (j == mx.count) { mx.role[j] = r; mx.m[j] = 0.0f; ++mx.count; }
            mx.m[j] += wt;
        }
    if (total <= 0.0f) return mx;
    for (u32 j = 0; j < mx.count; ++j) mx.m[j] /= total;
    // Ordem de desenho (de trás para a frente), estável.
    for (u32 a = 1; a < mx.count; ++a)
        for (u32 b = a; b > 0 && mx.role[b - 1] > mx.role[b]; --b) {
            std::swap(mx.role[b - 1], mx.role[b]);
            std::swap(mx.m[b - 1], mx.m[b]);
        }
    return mx;
}

/// Parte em construção: cor reta (3 floats) e alfa numa caixa da imagem.
struct Layer {
    Box box;
    std::vector<f32> rgb, a;
    [[nodiscard]] bool has(i32 x, i32 y) const noexcept { return x >= box.x0 && y >= box.y0 && x < box.x1 && y < box.y1; }
    [[nodiscard]] usize at(i32 x, i32 y) const noexcept {
        return static_cast<usize>(y - box.y0) * static_cast<usize>(box.x1 - box.x0) + static_cast<usize>(x - box.x0);
    }
};

void report(SplitProgress p, void* ctx, f32 v) {
    if (p) p(ctx, v);
}

} // namespace

// =============================================================================
//  Nomes e propriedades
// =============================================================================

const char* part_name(u32 role) noexcept {
    static const char* const kNames[kPartRoleCount] = {
        "Fundo", "Cabelo de trás", "Corpo", "Pescoço", "Rosto", "Orelhas",
        "Branco do olho esquerdo", "Branco do olho direito", "Íris esquerda", "Íris direita",
        "Cílios esquerdos", "Cílios direitos", "Sobrancelha esquerda", "Sobrancelha direita",
        "Nariz", "Boca", "Franja", "Mechas", "Óculos",
    };
    return role < kPartRoleCount ? kNames[role] : "Parte";
}

f32 part_default_depth(u32 role) noexcept {
    switch (role) {
        case kPartHairBack: case kPartSideLocks: return -0.25f;
        case kPartEars: return -0.1f;
        case kPartEyeWhiteL: case kPartEyeWhiteR: return 0.04f;
        case kPartIrisL: case kPartIrisR: return 0.05f;
        case kPartLashL: case kPartLashR: return 0.06f;
        case kPartBrowL: case kPartBrowR: return 0.08f;
        case kPartNose: return 0.12f;
        case kPartMouth: return 0.03f;
        case kPartHairFront: return 0.06f;
        case kPartGlasses: return 0.3f;
        default: return 0.0f;
    }
}

bool part_is_hair(u32 role) noexcept { return role == kPartHairBack || role == kPartHairFront || role == kPartSideLocks; }

bool part_in_head(u32 role) noexcept {
    return role == kPartFace || role == kPartEars || (role >= kPartEyeWhiteL && role <= kPartMouth) || role == kPartHairFront
        || role == kPartGlasses;
}

// =============================================================================
//  Preenchimento
// =============================================================================

void membrane_fill(std::vector<f32>& rgb, const std::vector<u8>& known, const std::vector<u8>& region, u32 w, u32 h) {
    if (!w || !h || rgb.size() < static_cast<usize>(w) * h * 3) return;
    // Pirâmide empurra-puxa: cada nível guarda cor ponderada e peso (≤ 1).
    struct Level {
        u32 w, h;
        std::vector<f32> c, wt;
    };
    std::vector<Level> pyr;
    {
        Level l0{w, h, std::vector<f32>(static_cast<usize>(w) * h * 3, 0.0f), std::vector<f32>(static_cast<usize>(w) * h, 0.0f)};
        usize any = 0;
        for (usize i = 0; i < static_cast<usize>(w) * h; ++i)
            if (known[i]) {
                l0.wt[i] = 1.0f;
                for (u32 k = 0; k < 3; ++k) l0.c[i * 3 + k] = rgb[i * 3 + k];
                ++any;
            }
        if (!any) return;
        pyr.push_back(std::move(l0));
    }
    while (pyr.back().w > 1 || pyr.back().h > 1) {
        const Level& f = pyr.back();
        Level c{(f.w + 1) / 2, (f.h + 1) / 2, {}, {}};
        c.c.assign(static_cast<usize>(c.w) * c.h * 3, 0.0f);
        c.wt.assign(static_cast<usize>(c.w) * c.h, 0.0f);
        for (u32 y = 0; y < c.h; ++y)
            for (u32 x = 0; x < c.w; ++x) {
                f32 sw = 0.0f, s[3] = {0, 0, 0};
                for (u32 dy = 0; dy < 2; ++dy)
                    for (u32 dx = 0; dx < 2; ++dx) {
                        const u32 fx = std::min(f.w - 1, 2 * x + dx), fy = std::min(f.h - 1, 2 * y + dy);
                        const usize i = static_cast<usize>(fy) * f.w + fx;
                        sw += f.wt[i];
                        for (u32 k = 0; k < 3; ++k) s[k] += f.wt[i] * f.c[i * 3 + k];
                    }
                const usize o = static_cast<usize>(y) * c.w + x;
                if (sw > 0.0f)
                    for (u32 k = 0; k < 3; ++k) c.c[o * 3 + k] = s[k] / sw;
                c.wt[o] = std::min(1.0f, sw);
            }
        pyr.push_back(std::move(c));
    }
    // Puxar: o buraco de cada nível vem do nível de cima (bilinear).
    for (usize li = pyr.size() - 1; li-- > 0;) {
        Level& f = pyr[li];
        const Level& c = pyr[li + 1];
        for (u32 y = 0; y < f.h; ++y)
            for (u32 x = 0; x < f.w; ++x) {
                const usize i = static_cast<usize>(y) * f.w + x;
                const f32 wt = f.wt[i];
                if (wt >= 1.0f) continue;
                const f32 sx = std::clamp((static_cast<f32>(x) + 0.5f) * 0.5f - 0.5f, 0.0f, static_cast<f32>(c.w - 1));
                const f32 sy = std::clamp((static_cast<f32>(y) + 0.5f) * 0.5f - 0.5f, 0.0f, static_cast<f32>(c.h - 1));
                const u32 x0 = static_cast<u32>(sx), y0 = static_cast<u32>(sy);
                const u32 x1 = std::min(c.w - 1, x0 + 1), y1 = std::min(c.h - 1, y0 + 1);
                const f32 tx = sx - static_cast<f32>(x0), ty = sy - static_cast<f32>(y0);
                for (u32 k = 0; k < 3; ++k) {
                    const f32 a = c.c[(static_cast<usize>(y0) * c.w + x0) * 3 + k], b = c.c[(static_cast<usize>(y0) * c.w + x1) * 3 + k];
                    const f32 d = c.c[(static_cast<usize>(y1) * c.w + x0) * 3 + k], e = c.c[(static_cast<usize>(y1) * c.w + x1) * 3 + k];
                    const f32 up = (a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty;
                    f.c[i * 3 + k] = wt * f.c[i * 3 + k] + (1.0f - wt) * up;
                }
                f.wt[i] = 1.0f;
            }
    }
    const Level& top = pyr[0];
    std::vector<usize> todo;
    for (usize i = 0; i < static_cast<usize>(w) * h; ++i)
        if (region[i] && !known[i]) {
            for (u32 k = 0; k < 3; ++k) rgb[i * 3 + k] = top.c[i * 3 + k];
            todo.push_back(i);
        }
    // Membrana: alisa o que veio da pirâmide (Gauss-Seidel em ordem fixa).
    for (u32 it = 0; it < 24; ++it)
        for (usize i : todo) {
            const u32 x = static_cast<u32>(i % w), y = static_cast<u32>(i / w);
            f32 s[3] = {0, 0, 0};
            u32 n = 0;
            const usize nb[4] = {x > 0 ? i - 1 : i, x + 1 < w ? i + 1 : i, y > 0 ? i - w : i, y + 1 < h ? i + w : i};
            for (usize q : nb) {
                if (q == i) continue;
                for (u32 k = 0; k < 3; ++k) s[k] += rgb[q * 3 + k];
                ++n;
            }
            if (n)
                for (u32 k = 0; k < 3; ++k) rgb[i * 3 + k] = s[k] / static_cast<f32>(n);
        }
}

// =============================================================================
//  Separar
// =============================================================================

bool split_parts(const u8* rgba, u32 w, u32 h, const Anchors& anchors, PartSet& out, SplitProgress progress, void* progressCtx) {
    out = PartSet{};
    if (!rgba || w < 8 || h < 8) return false;
    report(progress, progressCtx, 0.02f);
    const Canvas c = make_canvas(rgba, w, h);
    const usize n = c.n;
    const Rig r = rig_of(anchors, static_cast<f32>(w), static_cast<f32>(h));
    const f32 eyeY = 0.5f * (r.eye[0].y + r.eye[1].y), midX = 0.5f * (r.eye[0].x + r.eye[1].x);
    const f32 d = std::max(4.0f, std::fabs(r.eye[1].x - r.eye[0].x));
    const f32 ery = std::max(1.0f, 0.5f * (r.eyeR[0].y + r.eyeR[1].y));
    const f32 chinY = r.neck.y - 0.05f * r.faceR.y;
    const f32 mouthSpan = std::max(r.mouth.y - eyeY, 0.3f * d);
    const i32 W = static_cast<i32>(w), H = static_cast<i32>(h);
    out.width = w;
    out.height = h;

    // --- 1) Pele: amostras nas bochechas, entre os olhos e a boca e no queixo.
    LabColor skin{88.0f, 8.0f, 16.0f};
    {
        const Vec2 seeds[] = {
            {r.eye[0].x, r.eye[0].y + 2.1f * r.eyeR[0].y}, {r.eye[1].x, r.eye[1].y + 2.1f * r.eyeR[1].y},
            {midX, eyeY + 0.5f * mouthSpan},
            {r.mouth.x - 1.6f * r.mouthR.x - 0.12f * d, r.mouth.y}, {r.mouth.x + 1.6f * r.mouthR.x + 0.12f * d, r.mouth.y},
            {midX, r.mouth.y + 0.55f * std::max(chinY - r.mouth.y, 1.0f)},
        };
        const f32 rad = std::max(2.0f, 0.12f * d);
        std::vector<f32> sl, sa, sb;
        for (const Vec2& s : seeds) {
            const Box b = box_of(s.x - rad, s.y - rad, s.x + rad + 1, s.y + rad + 1, w, h);
            for (i32 y = b.y0; y < b.y1; ++y)
                for (i32 x = b.x0; x < b.x1; ++x) {
                    const usize i = c.at(x, y);
                    if (!c.fg[i] || c.L[i] < 45.0f || c.chroma(i) > 45.0f) continue;
                    if ((x - s.x) * (x - s.x) + (y - s.y) * (y - s.y) > rad * rad) continue;
                    sl.push_back(c.L[i]); sa.push_back(c.A[i]); sb.push_back(c.B[i]);
                }
        }
        if (sl.size() >= 10) skin = LabColor{median(sl), median(sa), median(sb)};
    }
    auto skin_dist = [&](usize i) {
        const f32 dL = c.L[i] - skin.L;
        const f32 dl = std::max(0.0f, std::fabs(dL) - 8.0f) * (dL < 0.0f ? 0.45f : 0.8f);
        const f32 da = c.A[i] - skin.a, db = c.B[i] - skin.b;
        return std::sqrt(dl * dl + da * da + db * db);
    };
    std::vector<u8> skinM(n, 0);
    for (usize i = 0; i < n; ++i) skinM[i] = c.fg[i] && skin_dist(i) < 13.0f ? 1 : 0;
    auto is_white = [&](usize i) { return c.L[i] > 62.0f && c.B[i] < 3.0f && c.chroma(i) < 16.0f; };
    report(progress, progressCtx, 0.08f);

    // --- 2) Paleta do cabelo: a faixa do topo da silhueta.
    std::vector<LabColor> hairPalette;
    {
        // A cabeça acima dos olhos (topo + franja): o cabelo todo, sombra e brilho.
        const f32 top = std::max(0.0f, r.hairTop.y);
        const f32 bottom = std::max(top + 2.0f, eyeY - 1.5f * ery);
        const Box b = box_of(r.face.x - 1.1f * r.faceR.x, top, r.face.x + 1.1f * r.faceR.x, bottom, w, h);
        std::vector<LabColor> samples;
        const usize area = static_cast<usize>(std::max(0, b.x1 - b.x0)) * static_cast<usize>(std::max(0, b.y1 - b.y0));
        const i32 stride = std::max<i32>(1, static_cast<i32>(std::sqrt(static_cast<f64>(area) / 20000.0)));
        for (i32 y = b.y0; y < b.y1; y += stride)
            for (i32 x = b.x0; x < b.x1; x += stride) {
                const usize i = c.at(x, y);
                if (c.fg[i] && !skinM[i]) samples.push_back(LabColor{c.L[i], c.A[i], c.B[i]});
            }
        if (samples.size() >= 30) hairPalette = kmeans(samples, 5, 0.6f);
    }
    auto hair_like = [&](usize i) {
        if (!c.fg[i] || skinM[i] || hairPalette.empty()) return false;
        f32 best = 1e30f;
        for (const LabColor& k : hairPalette) best = std::min(best, dist_lab(c, i, k, 0.6f));
        return best < 18.0f;
    };
    std::vector<u8> hairCand(n, 0);
    for (usize i = 0; i < n; ++i) hairCand[i] = hair_like(i) ? 1 : 0;
    // Olhos e boca nunca são cabelo (pupila e cílios são escuros como ele).
    for (u32 e = 0; e < 2; ++e) {
        const Vec2 rr{1.3f * r.eyeR[e].x, 1.35f * r.eyeR[e].y};
        const Box b = box_of(r.eye[e].x - rr.x, r.eye[e].y - rr.y, r.eye[e].x + rr.x + 1, r.eye[e].y + rr.y + 1, w, h);
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x)
                if (in_ellipse(x + 0.5f, y + 0.5f, r.eye[e], rr)) hairCand[c.at(x, y)] = 0;
    }
    // Núcleo grosso: o contorno fino (traço do desenho) não é cabelo.
    const i32 re = std::max(1, static_cast<i32>(std::lround(0.12f * ery)));
    std::vector<u8> hairCore = hairCand;
    morph(hairCore, w, h, re, false);
    {
        std::vector<i32> id;
        std::vector<Comp> comps;
        components(hairCore, w, Box{0, 0, W, H}, true, id, comps);
        u32 largest = 0;
        for (const Comp& k : comps) largest = std::max(largest, k.n);
        for (usize i = 0; i < n; ++i)
            if (id[i] && comps[static_cast<usize>(id[i] - 1)].n * 10 < largest) hairCore[i] = 0;
    }
    std::vector<u8> hair = hairCore;
    morph(hair, w, h, re + 1, true);
    for (usize i = 0; i < n; ++i) hair[i] = hair[i] && hairCand[i] ? 1 : 0;
    report(progress, progressCtx, 0.18f);

    std::vector<u8> labels(n, kNone);

    // --- 3) Óculos: a ponte entre os olhos tem uma cor que não é pele.
    {
        const f32 bx0 = r.eye[0].x + 0.9f * r.eyeR[0].x, bx1 = r.eye[1].x - 0.9f * r.eyeR[1].x;
        if (bx1 - bx0 >= 3.0f) {
            const Box strip = box_of(bx0, eyeY - 0.9f * ery, bx1, eyeY + 0.9f * ery, w, h);
            std::vector<f32> gl, ga, gb;
            std::vector<usize> seeds;
            for (i32 y = strip.y0; y < strip.y1; ++y)
                for (i32 x = strip.x0; x < strip.x1; ++x) {
                    const usize i = c.at(x, y);
                    if (!c.fg[i] || skinM[i] || hairCore[i] || is_white(i)) continue;
                    if (c.chroma(i) <= 10.0f && c.L[i] >= 40.0f) continue;
                    gl.push_back(c.L[i]); ga.push_back(c.A[i]); gb.push_back(c.B[i]);
                    seeds.push_back(i);
                }
            if (static_cast<f32>(seeds.size()) >= 0.6f * (bx1 - bx0)) {
                const LabColor g{median(gl), median(ga), median(gb)};
                const Box band = box_of(r.eye[0].x - 2.4f * r.eyeR[0].x, eyeY - 2.2f * ery, r.eye[1].x + 2.4f * r.eyeR[1].x,
                                        eyeY + 2.6f * ery, w, h);
                std::vector<u8> mask(n, 0);
                std::vector<usize> stack;
                const f32 strong = 16.0f;
                for (usize i : seeds)
                    if (dist_lab(c, i, g, 0.5f) < strong) { mask[i] = 1; stack.push_back(i); }
                while (!stack.empty()) {
                    const usize k = stack.back();
                    stack.pop_back();
                    const i32 kx = static_cast<i32>(k % w), ky = static_cast<i32>(k / w);
                    for (i32 dy = -1; dy <= 1; ++dy)
                        for (i32 dx = -1; dx <= 1; ++dx) {
                            const i32 nx = kx + dx, ny = ky + dy;
                            if (nx < band.x0 || ny < band.y0 || nx >= band.x1 || ny >= band.y1) continue;
                            const usize q = c.at(nx, ny);
                            if (mask[q] || !c.fg[q] || hairCore[q]) continue;
                            const f32 dg = dist_lab(c, q, g, 0.5f);
                            const bool near = std::fabs(c.L[q] - g.L) < 22.0f && !skinM[q] && !is_white(q);
                            if (dg < strong || (dg < 2.0f * strong && near)) {
                                mask[q] = 1;
                                stack.push_back(q);
                            }
                        }
                }
                usize total = 0, near[2] = {0, 0};
                for (i32 y = band.y0; y < band.y1; ++y)
                    for (i32 x = band.x0; x < band.x1; ++x) {
                        if (!mask[c.at(x, y)]) continue;
                        ++total;
                        for (u32 e = 0; e < 2; ++e) {
                            const f32 rr = std::max(r.eyeR[e].x, r.eyeR[e].y);
                            const f32 dd = std::hypot(x + 0.5f - r.eye[e].x, y + 0.5f - r.eye[e].y);
                            if (dd >= 0.6f * rr && dd <= 2.4f * rr) ++near[e];
                        }
                    }
                const usize bandArea = static_cast<usize>(band.x1 - band.x0) * static_cast<usize>(band.y1 - band.y0);
                const f32 needed = 0.8f * std::min(r.eyeR[0].x, r.eyeR[1].x);
                if (static_cast<f32>(near[0]) >= needed && static_cast<f32>(near[1]) >= needed && total * 4 < bandArea) {
                    // Um pixel a mais na borda (a mistura com a pele não fica no rosto).
                    std::vector<u8> grown = mask;
                    morph(grown, w, h, 1, true);
                    for (usize i = 0; i < n; ++i)
                        if (grown[i] && c.vis[i] && (mask[i] || skin_dist(i) > 6.0f) && !hairCore[i]) labels[i] = kPartGlasses;
                    out.glasses = true;
                }
            }
        }
    }
    report(progress, progressCtx, 0.24f);

    // --- 4) Olhos: branco neutro, íris num disco, cílios escuros acima.
    struct EyeShape {
        Vec2 iris{}, irisR{};
        bool found = false;
    } eyeShape[2];
    for (u32 e = 0; e < 2; ++e) {
        const Vec2 ce = r.eye[e], s = r.eyeR[e];
        const Vec2 er{1.25f * s.x, 1.3f * s.y};
        const Box b = box_of(ce.x - er.x, ce.y - er.y, ce.x + er.x + 1, ce.y + er.y + 1, w, h);
        auto inside = [&](i32 x, i32 y) { return in_ellipse(x + 0.5f, y + 0.5f, ce, er); };
        std::vector<f32> xs, ys;
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x) {
                const usize i = c.at(x, y);
                if (!inside(x, y) || !c.fg[i] || labels[i] != kNone || skinM[i]) continue;
                if (c.chroma(i) >= 16.0f && c.L[i] < 90.0f && !is_white(i)) { xs.push_back(x + 0.5f); ys.push_back(y + 0.5f); }
            }
        if (xs.size() < std::max<usize>(6, static_cast<usize>(0.02f * er.x * er.y))) {
            xs.clear();
            ys.clear();
            for (i32 y = b.y0; y < b.y1; ++y)
                for (i32 x = b.x0; x < b.x1; ++x) {
                    const usize i = c.at(x, y);
                    if (!inside(x, y) || !c.fg[i] || labels[i] != kNone || c.L[i] >= 32.0f) continue;
                    if (y + 0.5f < ce.y - 0.5f * s.y) continue;   // os cílios de cima não entram no disco
                    xs.push_back(x + 0.5f);
                    ys.push_back(y + 0.5f);
                }
        }
        if (xs.size() >= 4) {
            f64 sx = 0, sy = 0;
            for (usize k = 0; k < xs.size(); ++k) { sx += xs[k]; sy += ys[k]; }
            eyeShape[e].iris = Vec2{static_cast<f32>(sx / xs.size()), static_cast<f32>(sy / ys.size())};
            const f32 rx = 0.5f * (percentile(xs, 0.98f) - percentile(xs, 0.02f)) * 1.08f + 0.5f;
            const f32 ry = 0.5f * (percentile(ys, 0.98f) - percentile(ys, 0.02f)) * 1.08f + 0.5f;
            eyeShape[e].irisR = Vec2{std::clamp(rx, 0.15f * s.x, 1.1f * s.x), std::clamp(std::max(ry, 0.6f * rx), 0.15f * s.y, 1.2f * s.y)};
            eyeShape[e].found = true;
        }
        const u8 white = static_cast<u8>(kPartEyeWhiteL + e), iris = static_cast<u8>(kPartIrisL + e), lash = static_cast<u8>(kPartLashL + e);
        const EyeShape& es = eyeShape[e];
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x) {
                const usize i = c.at(x, y);
                if (!inside(x, y) || !c.fg[i] || labels[i] != kNone || hairCore[i]) continue;
                const f32 fx = x + 0.5f, fy = y + 0.5f;
                const bool inDisc = es.found && in_ellipse(fx, fy, es.iris, es.irisR);
                const bool dark = c.L[i] < 42.0f;
                const bool colored = c.chroma(i) >= 16.0f && !skinM[i];
                if (inDisc && (dark || colored || is_white(i) || !skinM[i])) labels[i] = iris;
                else if (is_white(i)) labels[i] = white;
                else if (dark || (!skinM[i] && c.L[i] < 60.0f)) {
                    const f32 split = es.found ? es.iris.y : ce.y;
                    if (fy < split) labels[i] = lash;
                    else if (es.found && in_ellipse(fx, fy, es.iris, es.irisR * 1.35f)) labels[i] = iris;   // fundo do olho
                    else labels[i] = static_cast<u8>(kPartFace);   // traço de baixo fica no rosto
                } else if (colored && es.found && in_ellipse(fx, fy, es.iris, es.irisR * 1.25f)) labels[i] = iris;
                else if (!skinM[i] && c.L[i] > 60.0f && c.B[i] < 6.0f) labels[i] = white;   // sombra do branco
            }
        // Cílios além da elipse: o traço escuro ligado a eles (rabo do delineado).
        const Box lb = box_of(ce.x - 1.7f * s.x, ce.y - 1.6f * s.y, ce.x + 1.7f * s.x + 1, ce.y + 0.2f * s.y + 1, w, h);
        std::vector<usize> stack;
        for (i32 y = lb.y0; y < lb.y1; ++y)
            for (i32 x = lb.x0; x < lb.x1; ++x)
                if (labels[c.at(x, y)] == lash) stack.push_back(c.at(x, y));
        while (!stack.empty()) {
            const usize k = stack.back();
            stack.pop_back();
            const i32 kx = static_cast<i32>(k % w), ky = static_cast<i32>(k / w);
            for (i32 dy = -1; dy <= 1; ++dy)
                for (i32 dx = -1; dx <= 1; ++dx) {
                    const i32 nx = kx + dx, ny = ky + dy;
                    if (nx < lb.x0 || ny < lb.y0 || nx >= lb.x1 || ny >= lb.y1) continue;
                    const usize q = c.at(nx, ny);
                    if (labels[q] != kNone || !c.fg[q] || hairCore[q] || c.L[q] >= 62.0f || skinM[q] || is_white(q)) continue;
                    labels[q] = lash;
                    stack.push_back(q);
                }
        }
    }
    report(progress, progressCtx, 0.32f);

    // --- 5) Boca: o buraco na pele perto da âncora, com os buracos dele (dentes).
    {
        const f32 hx = std::max(2.2f * r.mouthR.x, 0.32f * d);
        const Box b = box_of(r.mouth.x - hx, r.mouth.y - std::max(2.5f * r.mouthR.y, 0.1f * d), r.mouth.x + hx + 1,
                             r.mouth.y + std::max(5.0f * r.mouthR.y, 0.5f * std::max(chinY - r.mouth.y, 2.0f)) + 1, w, h);
        std::vector<u8> cand(n, 0);
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x) {
                const usize i = c.at(x, y);
                cand[i] = c.fg[i] && !skinM[i] && labels[i] == kNone && !hair[i] ? 1 : 0;
            }
        std::vector<i32> id;
        std::vector<Comp> comps;
        components(cand, w, b, true, id, comps);
        const f32 reach = std::max(1.2f * r.mouthR.x, 0.12f * d);
        std::vector<u8> pick(comps.size(), 0);
        bool any = false;
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x) {
                const usize i = c.at(x, y);
                if (!id[i]) continue;
                if (std::hypot(x + 0.5f - r.mouth.x, (y + 0.5f - r.mouth.y) * 1.5f) <= reach) { pick[static_cast<usize>(id[i] - 1)] = 1; any = true; }
            }
        if (!any && !comps.empty()) {
            f32 best = 0.0f;
            usize bi = 0;
            for (usize k = 0; k < comps.size(); ++k) {
                const f32 dx = (comps[k].cx() - r.mouth.x) / d, dy = (comps[k].cy() - r.mouth.y) / d;
                const f32 score = static_cast<f32>(comps[k].n) / (1.0f + 25.0f * (dx * dx + dy * dy));
                if (score > best) { best = score; bi = k; }
            }
            pick[bi] = 1;
            any = true;
        }
        if (any) {
            // Nada grande demais: um traço comprido do queixo não é boca.
            for (usize k = 0; k < comps.size(); ++k)
                if (pick[k] && (comps[k].x1 - comps[k].x0 > 1.2f * d || comps[k].y1 - comps[k].y0 > 0.9f * d)) pick[k] = 0;
            std::vector<u8> m(n, 0);
            Box mb{W, H, 0, 0};
            for (i32 y = b.y0; y < b.y1; ++y)
                for (i32 x = b.x0; x < b.x1; ++x) {
                    const usize i = c.at(x, y);
                    if (id[i] && pick[static_cast<usize>(id[i] - 1)]) {
                        m[i] = 1;
                        mb.x0 = std::min(mb.x0, x); mb.y0 = std::min(mb.y0, y);
                        mb.x1 = std::max(mb.x1, x + 1); mb.y1 = std::max(mb.y1, y + 1);
                    }
                }
            if (!mb.empty()) {
                // Buracos (dentes, brilho): o que a borda da caixa não alcança por fora da boca.
                std::vector<u8> outside(n, 0);
                std::vector<usize> stack;
                for (i32 y = mb.y0; y < mb.y1; ++y)
                    for (i32 x = mb.x0; x < mb.x1; ++x) {
                        const bool edge = x == mb.x0 || y == mb.y0 || x == mb.x1 - 1 || y == mb.y1 - 1;
                        const usize i = c.at(x, y);
                        if (edge && !m[i]) { outside[i] = 1; stack.push_back(i); }
                    }
                while (!stack.empty()) {
                    const usize k = stack.back();
                    stack.pop_back();
                    const i32 kx = static_cast<i32>(k % w), ky = static_cast<i32>(k / w);
                    const i32 nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (const auto& o : nb) {
                        const i32 nx = kx + o[0], ny = ky + o[1];
                        if (nx < mb.x0 || ny < mb.y0 || nx >= mb.x1 || ny >= mb.y1) continue;
                        const usize q = c.at(nx, ny);
                        if (m[q] || outside[q]) continue;
                        outside[q] = 1;
                        stack.push_back(q);
                    }
                }
                for (i32 y = mb.y0; y < mb.y1; ++y)
                    for (i32 x = mb.x0; x < mb.x1; ++x) {
                        const usize i = c.at(x, y);
                        if ((m[i] || !outside[i]) && labels[i] == kNone && c.vis[i]) labels[i] = kPartMouth;
                    }
            }
        }
    }
    report(progress, progressCtx, 0.38f);

    // --- 6) Sobrancelhas: traço escuro alongado acima de cada olho (fora do cabelo grosso).
    for (u32 e = 0; e < 2; ++e) {
        const Vec2 ce = r.eye[e], s = r.eyeR[e];
        const Box b = box_of(ce.x - 1.7f * s.x, ce.y - 3.4f * s.y, ce.x + 1.7f * s.x + 1, ce.y - 0.95f * s.y, w, h);
        if (b.empty()) continue;
        std::vector<u8> cand(n, 0);
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x) {
                const usize i = c.at(x, y);
                cand[i] = c.fg[i] && labels[i] == kNone && !hair[i] && !skinM[i] && c.L[i] < skin.L - 8.0f ? 1 : 0;
            }
        std::vector<i32> id;
        std::vector<Comp> comps;
        components(cand, w, b, true, id, comps);
        f32 best = 0.0f;
        i32 pickId = 0;
        for (usize k = 0; k < comps.size(); ++k) {
            const Comp& q = comps[k];
            const f32 bw = static_cast<f32>(q.x1 - q.x0 + 1), bh = static_cast<f32>(q.y1 - q.y0 + 1);
            if (bw < 0.6f * s.x || bh > 0.9f * bw || q.n < 4) continue;
            const f32 score = static_cast<f32>(q.n) * std::min(bw / bh, 6.0f) / (1.0f + std::fabs(q.cx() - ce.x) / s.x);
            if (score > best) { best = score; pickId = static_cast<i32>(k) + 1; }
        }
        if (!pickId) continue;
        const u8 brow = static_cast<u8>(kPartBrowL + e);
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x)
                if (id[c.at(x, y)] == pickId) labels[c.at(x, y)] = brow;
    }

    // --- 7) Nariz: marca pequena mais escura que a pele, entre os olhos e a boca.
    {
        const Box b = box_of(midX - 0.25f * d, eyeY + 0.3f * mouthSpan, midX + 0.25f * d + 1, eyeY + 0.85f * mouthSpan, w, h);
        if (!b.empty()) {
            std::vector<u8> cand(n, 0);
            for (i32 y = b.y0; y < b.y1; ++y)
                for (i32 x = b.x0; x < b.x1; ++x) {
                    const usize i = c.at(x, y);
                    cand[i] = c.fg[i] && labels[i] == kNone && !hair[i] && (c.L[i] < skin.L - 10.0f || skin_dist(i) > 14.0f) ? 1 : 0;
                }
            std::vector<i32> id;
            std::vector<Comp> comps;
            components(cand, w, b, true, id, comps);
            const Vec2 center{midX, eyeY + 0.6f * mouthSpan};
            f32 best = 1e30f;
            i32 pickId = 0;
            for (usize k = 0; k < comps.size(); ++k) {
                const Comp& q = comps[k];
                if (static_cast<f32>(q.n) > 0.02f * d * d || q.n < 2) continue;
                const f32 dd = std::hypot(q.cx() - center.x, q.cy() - center.y);
                if (dd < best) { best = dd; pickId = static_cast<i32>(k) + 1; }
            }
            if (pickId) {
                std::vector<u8> m(n, 0);
                for (i32 y = b.y0; y < b.y1; ++y)
                    for (i32 x = b.x0; x < b.x1; ++x)
                        if (id[c.at(x, y)] == pickId) m[c.at(x, y)] = 1;
                morph(m, w, h, 1, true);
                for (usize i = 0; i < n; ++i)
                    if (m[i] && labels[i] == kNone && c.vis[i]) labels[i] = kPartNose;
            }
        }
    }
    report(progress, progressCtx, 0.44f);

    // --- 8) Contorno do rosto: a pele ligada às bochechas (acima do queixo),
    //        com a testa (meia elipse sobre a linha dos olhos) que a franja esconde.
    std::vector<u8> faceShape;
    f32 halfW = r.faceR.x;
    {
        std::vector<u8> m(n, 0);
        const Vec2 fr{1.25f * r.faceR.x, 1.25f * r.faceR.y};
        for (i32 y = 0; y < H && static_cast<f32>(y) <= chinY; ++y)
            for (i32 x = 0; x < W; ++x) {
                const usize i = c.at(x, y);
                m[i] = skinM[i] && in_ellipse(x + 0.5f, y + 0.5f, r.face, fr) ? 1 : 0;
            }
        std::vector<i32> id;
        std::vector<Comp> comps;
        components(m, w, Box{0, 0, W, H}, false, id, comps);
        std::vector<u8> keep(comps.size(), 0);
        const Vec2 probes[] = {{r.eye[0].x, r.eye[0].y + 2.1f * r.eyeR[0].y}, {r.eye[1].x, r.eye[1].y + 2.1f * r.eyeR[1].y},
                               {midX, eyeY + 0.5f * mouthSpan}};
        for (const Vec2& p : probes) {
            const f32 rad = std::max(2.0f, 0.12f * d);
            const Box b = box_of(p.x - rad, p.y - rad, p.x + rad + 1, p.y + rad + 1, w, h);
            for (i32 y = b.y0; y < b.y1; ++y)
                for (i32 x = b.x0; x < b.x1; ++x)
                    if (id[c.at(x, y)]) keep[static_cast<usize>(id[c.at(x, y)] - 1)] = 1;
        }
        // Pele da testa entre as mechas da franja: pedaços com o centro na parte
        // de cima do rosto também são rosto.
        for (usize k = 0; k < comps.size(); ++k)
            if (!keep[k] && comps[k].n >= 6 && comps[k].cy() < eyeY
                && in_ellipse(comps[k].cx() + 0.5f, comps[k].cy() + 0.5f, r.face, Vec2{0.8f * r.faceR.x, 1.15f * r.faceR.y}))
                keep[k] = 1;
        // Sem a sonda (rosto muito coberto): a maior pele dentro da elipse.
        if (std::find(keep.begin(), keep.end(), 1) == keep.end() && !comps.empty()) {
            usize bi = 0;
            for (usize k = 1; k < comps.size(); ++k)
                if (comps[k].n > comps[bi].n) bi = k;
            keep[bi] = 1;
        }
        std::vector<u8> face(n, 0);
        {
            f32 lo = 1e30f, hi = -1e30f;
            for (usize i = 0; i < n; ++i) {
                if (!id[i] || !keep[static_cast<usize>(id[i] - 1)]) continue;
                face[i] = 1;
                const f32 y = static_cast<f32>(i / w) + 0.5f;
                if (std::fabs(y - eyeY) <= std::max(1.5f * ery, 2.0f)) {
                    const f32 x = static_cast<f32>(i % w) + 0.5f;
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
            }
            halfW = hi > lo ? 0.5f * (hi - lo) : r.faceR.x;
        }
        // Os olhos, a boca e o nariz ficam dentro (eles furam a pele).
        for (usize i = 0; i < n; ++i)
            if (labels[i] != kNone && labels[i] != kPartGlasses && static_cast<f32>(i / w) <= chinY) face[i] = 1;
        std::vector<Vec2> pts;
        row_extremes(face, w, h, pts);
        const f32 cx = r.face.x;
        const f32 rx = std::min(0.95f * halfW, 0.9f * r.faceR.x);
        const f32 ry = std::max(eyeY - (r.face.y - r.faceR.y), 0.5f * r.faceR.y);
        for (u32 k = 0; k <= 32; ++k) {
            const f32 t = kPi + kPi * static_cast<f32>(k) / 32.0f;
            pts.push_back(Vec2{cx + rx * std::cos(t), eyeY + ry * std::sin(t)});
        }
        faceShape = hull_mask(std::move(pts), w, h);
        for (i32 y = 0; y < H; ++y)
            if (static_cast<f32>(y) > chinY + 1.0f)
                std::fill(faceShape.begin() + static_cast<std::ptrdiff_t>(c.at(0, y)), faceShape.begin() + static_cast<std::ptrdiff_t>(c.at(0, y) + w), u8{0});
    }
    report(progress, progressCtx, 0.5f);

    // --- 9) Fundo (foto sem transparência): fora da cabeça e do tronco.
    if (!c.hasAlpha) {
        const Vec2 head{r.face.x, 0.5f * (r.hairTop.y + chinY)};
        const Vec2 headR{1.7f * r.faceR.x, std::max(0.5f * (chinY - r.hairTop.y), r.faceR.y) * 1.15f};
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x) {
                const usize i = c.at(x, y);
                if (labels[i] != kNone) continue;
                const f32 fx = x + 0.5f, fy = y + 0.5f;
                const f32 t = saturate((fy - chinY) / std::max(r.height - chinY, 1.0f));
                const bool torso = fy > chinY && std::fabs(fx - r.neck.x) < (0.8f + 1.9f * t) * r.faceR.x;
                if (!in_ellipse(fx, fy, head, headR) && !torso) labels[i] = kPartBackground;
            }
    }

    // --- 10) Cabelo, pele e corpo; o resto herda do vizinho. As pontas finas da
    //         franja sobre a testa (cor de cabelo acima dos olhos) são cabelo.
    const f32 browLine = eyeY - 1.1f * ery;
    for (usize i = 0; i < n; ++i) {
        if (labels[i] != kNone || !c.vis[i]) continue;
        if (hair[i] || (hairCand[i] && faceShape[i] && static_cast<f32>(i / w) < browLine)) labels[i] = kKindHair;
        else if (skinM[i]) labels[i] = kKindSkin;
        else if (c.fg[i] && static_cast<f32>(i / w) > chinY + 0.12f * r.faceR.y && !hairCand[i]) labels[i] = kPartBody;
    }
    {
        std::deque<usize> q;
        for (usize i = 0; i < n; ++i)
            if (labels[i] != kNone) q.push_back(i);
        while (!q.empty()) {
            const usize k = q.front();
            q.pop_front();
            const i32 kx = static_cast<i32>(k % w), ky = static_cast<i32>(k / w);
            const i32 nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& o : nb) {
                const i32 nx = kx + o[0], ny = ky + o[1];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                const usize p = c.at(nx, ny);
                if (labels[p] != kNone || !c.vis[p]) continue;
                labels[p] = labels[k];
                q.push_back(p);
            }
        }
        for (usize i = 0; i < n; ++i)
            if (c.vis[i] && labels[i] == kNone) labels[i] = static_cast<f32>(i / w) < chinY ? static_cast<u8>(kPartHairBack) : static_cast<u8>(kPartBody);
    }
    report(progress, progressCtx, 0.56f);

    // --- 11) Topologia: franja, mechas, cabelo de trás; rosto, orelha, pescoço, corpo.
    // Meia largura do pescoço: ~0,45 da meia largura do rosto na linha dos olhos.
    const f32 neckHalf = std::clamp(0.45f * halfW, 0.12f * r.faceR.x, 0.6f * r.faceR.x);
    std::vector<i32> bodyLo(h, W), bodyHi(h, -1);
    for (i32 y = 0; y < H; ++y)
        for (i32 x = 0; x < W; ++x)
            if (labels[c.at(x, y)] == kPartBody) { bodyLo[y] = std::min(bodyLo[y], x); bodyHi[y] = std::max(bodyHi[y], x); }
    {
        // Cabelo abaixo do queixo: componente a componente, na frente do corpo = mechas.
        std::vector<u8> below(n, 0);
        const i32 cy = std::clamp(static_cast<i32>(std::ceil(chinY)), 0, H);
        for (i32 y = cy; y < H; ++y)
            for (i32 x = 0; x < W; ++x) below[c.at(x, y)] = labels[c.at(x, y)] == kKindHair ? 1 : 0;
        std::vector<i32> id;
        std::vector<Comp> comps;
        components(below, w, Box{0, cy, W, H}, true, id, comps);
        std::vector<u32> front(comps.size(), 0);
        for (i32 y = cy; y < H; ++y)
            for (i32 x = 0; x < W; ++x) {
                const i32 k = id[c.at(x, y)];
                if (k && x > bodyLo[y] && x < bodyHi[y]) ++front[static_cast<usize>(k - 1)];
            }
        // Franja: dentro do rosto ou na faixa logo acima dele (coluna a coluna).
        std::vector<i32> colTop(w, H);
        i32 faceTop = H;
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x)
                if (faceShape[c.at(x, y)] && colTop[x] == H) { colTop[x] = y; faceTop = std::min(faceTop, y); }
        const f32 bangs = 0.45f * std::max(eyeY - static_cast<f32>(faceTop), 2.0f * ery);
        for (usize i = 0; i < n; ++i) {
            if (labels[i] != kKindHair) continue;
            const i32 x = static_cast<i32>(i % w), y = static_cast<i32>(i / w);
            const bool above = colTop[x] < H && y < colTop[x] && static_cast<f32>(y) >= static_cast<f32>(colTop[x]) - bangs;
            if (faceShape[i] || above) labels[i] = kPartHairFront;
            else if (id[i] && front[static_cast<usize>(id[i] - 1)] * 4 >= comps[static_cast<usize>(id[i] - 1)].n) labels[i] = kPartSideLocks;
            else labels[i] = kPartHairBack;
        }
    }
    // Pescoço: a coluna logo abaixo do queixo (a sombra dele nem sempre é "pele").
    const f32 neckBottom = chinY + 0.45f * r.faceR.y;
    std::vector<u8> loose(n, 0);
    for (usize i = 0; i < n; ++i) {
        const f32 x = static_cast<f32>(i % w) + 0.5f, y = static_cast<f32>(i / w) + 0.5f;
        const bool column = y > chinY && y < neckBottom && std::fabs(x - r.neck.x) < neckHalf;
        if (column && (labels[i] == kKindSkin || labels[i] == kPartBody) && c.B[i] > 4.0f && c.A[i] > -2.0f && c.L[i] > 40.0f) {
            labels[i] = kPartNeck;
            continue;
        }
        if (labels[i] != kKindSkin) continue;
        if (y <= chinY) {
            if (faceShape[i]) labels[i] = kPartFace;
            else if (y >= eyeY - 0.2f * r.faceR.y && std::fabs(x - r.face.x) > 0.5f * r.faceR.x) labels[i] = kPartEars;
            else { labels[i] = kNone; loose[i] = 1; }   // brilho no cabelo: herda do vizinho
        } else labels[i] = column ? static_cast<u8>(kPartNeck) : static_cast<u8>(kPartBody);
    }
    {
        std::vector<u8> ears(n, 0);
        for (usize i = 0; i < n; ++i) ears[i] = labels[i] == kPartEars ? 1 : 0;
        std::vector<i32> id;
        std::vector<Comp> comps;
        components(ears, w, Box{0, 0, W, H}, true, id, comps);
        const f32 minEar = 0.04f * halfW * halfW;
        for (usize i = 0; i < n; ++i)
            if (id[i] && static_cast<f32>(comps[static_cast<usize>(id[i] - 1)].n) < minEar) { labels[i] = kNone; loose[i] = 1; }
    }
    {
        std::deque<usize> q;
        for (usize i = 0; i < n; ++i)
            if (labels[i] != kNone && labels[i] < kPartRoleCount) q.push_back(i);
        while (!q.empty()) {
            const usize k = q.front();
            q.pop_front();
            const i32 kx = static_cast<i32>(k % w), ky = static_cast<i32>(k / w);
            const i32 nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& o : nb) {
                const i32 nx = kx + o[0], ny = ky + o[1];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                const usize p2 = c.at(nx, ny);
                if (!loose[p2] || labels[p2] != kNone) continue;
                labels[p2] = labels[k];
                q.push_back(p2);
            }
        }
        for (usize i = 0; i < n; ++i)
            if (loose[i] && labels[i] == kNone) labels[i] = kPartFace;
    }
    // Fresta de pele entre mechas (a maior parte da vizinhança é cabelo): o
    // artista pinta junto com o cabelo — vai com a mecha, não fica solta no rosto.
    {
        auto hairRole = [](u8 l) { return l == kPartHairFront || l == kPartHairBack || l == kPartSideLocks; };
        std::vector<u32> sum(static_cast<usize>(w + 1) * (h + 1), 0), front(static_cast<usize>(w + 1) * (h + 1), 0);
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x) {
                const usize o = static_cast<usize>(y + 1) * (w + 1) + static_cast<usize>(x + 1);
                const u8 l = labels[c.at(x, y)];
                sum[o] = (hairRole(l) ? 1u : 0u) + sum[o - 1] + sum[o - (w + 1)] - sum[o - (w + 1) - 1];
                front[o] = (l == kPartHairFront ? 1u : 0u) + front[o - 1] + front[o - (w + 1)] - front[o - (w + 1) - 1];
            }
        const i32 rad = std::max(3, static_cast<i32>(std::lround(0.03f * halfW)));
        std::vector<u8> next = labels;
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x) {
                const usize i = c.at(x, y);
                if (labels[i] != kPartFace && labels[i] != kPartEars) continue;
                const i32 x0 = std::max(0, x - rad), y0 = std::max(0, y - rad), x1 = std::min(W, x + rad + 1), y1 = std::min(H, y + rad + 1);
                auto box = [&](const std::vector<u32>& t) {
                    return t[static_cast<usize>(y1) * (w + 1) + x1] - t[static_cast<usize>(y0) * (w + 1) + x1]
                         - t[static_cast<usize>(y1) * (w + 1) + x0] + t[static_cast<usize>(y0) * (w + 1) + x0];
                };
                const u32 area = static_cast<u32>((x1 - x0) * (y1 - y0));
                const u32 hairs = box(sum);
                if (hairs * 10 < area * 6) continue;
                next[i] = box(front) > 0 ? static_cast<u8>(kPartHairFront) : static_cast<u8>(kPartHairBack);
            }
        labels.swap(next);
    }
    // O contorno (escuro ou alaranjado) das mechas da frente é delas: até 3 px
    // de traço que não é pele, colado na franja/mechas, vai junto com o cabelo
    // (a pele por baixo é preenchida, sem traço solto quando a cabeça vira).
    for (u32 pass = 0; pass < 3; ++pass) {
        std::vector<u8> next = labels;
        for (i32 y = 1; y + 1 < H; ++y)
            for (i32 x = 1; x + 1 < W; ++x) {
                const usize i = c.at(x, y);
                const bool faceLine = (labels[i] == kPartFace || labels[i] == kPartEars) && skin_dist(i) >= 10.0f;
                const bool bodyLine = labels[i] == kPartBody && (hairCand[i] || c.L[i] < 45.0f);
                if (!faceLine && !bodyLine) continue;
                for (i32 dy = -1; dy <= 1 && next[i] == labels[i]; ++dy)
                    for (i32 dx = -1; dx <= 1; ++dx) {
                        const u8 nl = labels[c.at(x + dx, y + dy)];
                        if (nl == kPartHairFront || nl == kPartSideLocks || (bodyLine && nl == kPartHairBack)) { next[i] = nl; break; }
                    }
            }
        labels.swap(next);
    }
    report(progress, progressCtx, 0.6f);

    // --- 12) Camadas: alfa de cada parte de trás para a frente (a soma devolve a imagem).
    std::array<Box, kPartRoleCount> boxes;
    for (Box& b : boxes) b = Box{W, H, 0, 0};
    auto grow = [&](u32 role, i32 x, i32 y) {
        Box& b = boxes[role];
        b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y);
        b.x1 = std::max(b.x1, x + 1); b.y1 = std::max(b.y1, y + 1);
    };
    for (i32 y = 0; y < H; ++y)
        for (i32 x = 0; x < W; ++x) {
            const u8 l = labels[c.at(x, y)];
            if (l < kPartRoleCount) {
                grow(l, std::max(0, x - 1), std::max(0, y - 1));
                grow(l, std::min(W - 1, x + 1), std::min(H - 1, y + 1));
            }
        }
    // Formas de preenchimento (onde a parte de trás continua por baixo das da frente).
    std::array<std::vector<u8>, kPartRoleCount> shape;
    auto role_hull = [&](std::initializer_list<u32> roles, f32 yMin, f32 yMax) {
        std::vector<u8> m(n, 0);
        for (usize i = 0; i < n; ++i) {
            const f32 y = static_cast<f32>(i / w);
            if (y < yMin || y > yMax) continue;
            for (u32 role : roles)
                if (labels[i] == role) { m[i] = 1; break; }
        }
        std::vector<Vec2> pts;
        row_extremes(m, w, h, pts);
        return hull_mask(std::move(pts), w, h);
    };
    shape[kPartFace] = faceShape;
    shape[kPartHairBack] = role_hull({kPartHairBack, kPartSideLocks, kPartFace, kPartHairFront, kPartEars}, -1.0f, static_cast<f32>(h));
    shape[kPartBody] = role_hull({kPartBody}, chinY - 0.2f * r.faceR.y, static_cast<f32>(h));
    {
        std::vector<i32> top(w, H);
        std::vector<u8> seen(w, 0);
        for (i32 x = 0; x < W; ++x) {
            for (i32 y = 0; y < H; ++y)
                if (labels[c.at(x, y)] == kPartBody) { top[x] = y; break; }
            if (top[x] >= H) continue;
            const u8 above = top[x] > 0 ? labels[c.at(x, top[x] - 1)] : kNone;
            seen[x] = above != kPartHairBack && above != kPartSideLocks && above != kPartHairFront ? 1 : 0;
        }
        std::vector<f32> env(w, static_cast<f32>(H));
        for (i32 x = 0; x < W; ++x) {
            if (top[x] >= H) continue;
            if (seen[x]) { env[x] = static_cast<f32>(top[x]); continue; }
            i32 a = x - 1, b = x + 1;
            while (a >= 0 && !seen[a]) --a;
            while (b < W && !seen[b]) ++b;
            if (a >= 0 && b < W) env[x] = static_cast<f32>(top[a]) + (static_cast<f32>(top[b] - top[a])) * static_cast<f32>(x - a) / static_cast<f32>(b - a);
            else if (a >= 0) env[x] = static_cast<f32>(top[a]);
            else if (b < W) env[x] = static_cast<f32>(top[b]);
            else env[x] = static_cast<f32>(top[x]);
        }
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x)
                if (static_cast<f32>(y) < env[x] - 1.0f) shape[kPartBody][c.at(x, y)] = 0;
    }
    {
        std::vector<u8> m(n, 0);
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x) {
                const f32 fy = y + 0.5f, fx = x + 0.5f;
                m[c.at(x, y)] = std::fabs(fx - r.neck.x) < neckHalf && fy >= chinY - 0.45f * r.faceR.y && fy <= chinY + 0.15f * r.faceR.y;
            }
        shape[kPartNeck] = std::move(m);
    }
    if (!c.hasAlpha) shape[kPartBackground].assign(n, 1);
    for (u32 e = 0; e < 2 && out.glasses; ++e) {
        if (!eyeShape[e].found) continue;
        std::vector<u8> eye = role_hull({kPartEyeWhiteL + e, kPartIrisL + e}, -1.0f, static_cast<f32>(h));
        std::vector<u8> iris(n, 0), white(n, 0);
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x) {
                const usize i = c.at(x, y);
                if (!eye[i]) continue;
                if (in_ellipse(x + 0.5f, y + 0.5f, eyeShape[e].iris, eyeShape[e].irisR)) iris[i] = 1;
                else white[i] = 1;
            }
        shape[kPartIrisL + e] = std::move(iris);
        shape[kPartEyeWhiteL + e] = std::move(white);
    }
    for (u32 role = 0; role < kPartRoleCount; ++role) {
        if (shape[role].empty()) continue;
        for (i32 y = 0; y < H; ++y)
            for (i32 x = 0; x < W; ++x)
                if (shape[role][c.at(x, y)]) grow(role, x, y);
    }
    struct Seam {
        usize i;
        u8 role;
        f32 a;
    };
    std::vector<Seam> seams;
    {
        const i32 band = std::max(4, static_cast<i32>(std::lround(0.08f * r.faceR.y)));
        const i32 dirs[3][2] = {{0, -1}, {-1, 0}, {1, 0}};
        for (const auto& dv : dirs)
            for (i32 y = 0; y < H; ++y)
                for (i32 x = 0; x < W; ++x) {
                    const u8 l = labels[c.at(x, y)];
                    const i32 px = x + dv[0], py = y + dv[1];
                    if ((l != kPartSideLocks && l != kPartHairFront) || px < 0 || py < 0 || px >= W || py >= H
                        || labels[c.at(px, py)] != kPartHairBack)
                        continue;
                    for (i32 d = 1; d <= band; ++d) {
                        const i32 sx = x + d * dv[0], sy = y + d * dv[1];
                        if (sx < 0 || sy < 0 || sx >= W || sy >= H) break;
                        const usize i = c.at(sx, sy);
                        if (labels[i] != kPartHairBack || c.alpha[i] < 0.999f) break;
                        // Só cabelo (de trás e esta mecha) no pixel: a soma fica exata.
                        const Mix mx = mix_at(labels, w, h, sx, sy);
                        bool hairOnly = true;
                        for (u32 j = 0; j < mx.count; ++j) hairOnly = hairOnly && (mx.role[j] == kPartHairBack || mx.role[j] == l);
                        if (!hairOnly) continue;
                        seams.push_back(Seam{i, l, 1.0f - static_cast<f32>(d) / static_cast<f32>(band + 1)});
                        grow(l, sx, sy);
                    }
                }
    }
    std::array<Layer, kPartRoleCount> layers;
    for (u32 role = 0; role < kPartRoleCount; ++role) {
        Layer& l = layers[role];
        l.box = boxes[role];
        if (l.box.empty()) continue;
        const usize area = static_cast<usize>(l.box.x1 - l.box.x0) * static_cast<usize>(l.box.y1 - l.box.y0);
        l.rgb.assign(area * 3, 0.0f);
        l.a.assign(area, 0.0f);
    }
    // A composição do renderer é em luz LINEAR: o alfa das partes, o recorte da
    // borda e o preenchimento são calculados nela (a soma na GPU dá a imagem).
    auto color = [&](usize i, u32 k) { return srgb_to_linear(c.rgba[i * 4 + k]); };
    for (i32 y = 0; y < H; ++y)
        for (i32 x = 0; x < W; ++x) {
            const usize i = c.at(x, y);
            if (!c.vis[i]) continue;
            const Mix mx = mix_at(labels, w, h, x, y);
            f32 T = 1.0f;
            const f32 alpha = c.alpha[i];
            for (u32 j = mx.count; j-- > 0;) {
                Layer& l = layers[mx.role[j]];
                if (!l.has(x, y)) continue;
                const f32 v = alpha * mx.m[j];
                const f32 a = T > 1e-6f ? std::min(1.0f, v / T) : 0.0f;
                T = std::max(0.0f, T - v);
                const usize o = l.at(x, y);
                l.a[o] = a;
                for (u32 k = 0; k < 3; ++k) l.rgb[o * 3 + k] = color(i, k);
            }
        }
    report(progress, progressCtx, 0.68f);

    // --- 13) Preencher atrás: a parte de trás continua por baixo das da frente.
    const u32 fillOrder[] = {kPartGlasses, kPartIrisL, kPartIrisR, kPartEyeWhiteL, kPartEyeWhiteR, kPartFace, kPartNeck,
                             kPartBody, kPartHairBack, kPartBackground};
    u32 done = 0;
    for (u32 role : fillOrder) {
        ++done;
        if (shape[role].empty()) continue;
        Layer& l = layers[role];
        if (l.box.empty()) continue;
        const i32 bw = l.box.x1 - l.box.x0, bh = l.box.y1 - l.box.y0;
        std::vector<u8> region(static_cast<usize>(bw) * bh, 0), known(static_cast<usize>(bw) * bh, 0);
        std::vector<f32> rgb(static_cast<usize>(bw) * bh * 3, 0.0f);
        std::vector<Mix> mixes(static_cast<usize>(bw) * bh);
        usize regionCount = 0, knownCount = 0;
        for (i32 y = l.box.y0; y < l.box.y1; ++y)
            for (i32 x = l.box.x0; x < l.box.x1; ++x) {
                const usize i = c.at(x, y), o = l.at(x, y);
                const u8 lab = labels[i];
                if (lab == role && c.vis[i]) {
                    const Mix mx = mix_at(labels, w, h, x, y);
                    const bool pure = mx.count == 1;
                    // Fonte do preenchimento: a pele (o rosto) ou a cor da parte sem o
                    // traço escuro do contorno (que borraria por baixo de tudo).
                    const bool source = role == kPartFace ? skinM[i] != 0
                                      : (role == kPartBody || role == kPartNeck) ? c.L[i] > 40.0f
                                      : role == kPartHairBack ? (hairCand[i] || c.L[i] < 50.0f) && !skinM[i] : true;
                    if (pure && source && c.alpha[i] >= 0.999f) {
                        known[o] = 1;
                        ++knownCount;
                        for (u32 k = 0; k < 3; ++k) rgb[o * 3 + k] = color(i, k);
                    }
                }
                if (!shape[role][i] || c.alpha[i] < 0.999f) continue;
                const Mix mx = mix_at(labels, w, h, x, y);
                bool behind = false, inFront = false;
                for (u32 j = 0; j < mx.count; ++j) {
                    if (mx.role[j] < role) behind = true;
                    if (mx.role[j] > role) inFront = true;
                }
                if (behind || !inFront) continue;
                if (role == kPartBody) {
                    bool head = false;
                    for (u32 j = 0; j < mx.count; ++j) head = head || part_in_head(mx.role[j]);
                    if (head) continue;
                }
                region[o] = 1;
                mixes[o] = mx;
                ++regionCount;
            }
        if (!regionCount) continue;
        if (!knownCount) {
            // Sem fonte: a cor média do que a parte mostra.
            f64 s[3] = {0, 0, 0};
            f64 wsum = 0;
            for (usize o = 0; o < l.a.size(); ++o) {
                s[0] += l.rgb[o * 3] * l.a[o]; s[1] += l.rgb[o * 3 + 1] * l.a[o]; s[2] += l.rgb[o * 3 + 2] * l.a[o];
                wsum += l.a[o];
            }
            if (wsum <= 0.0) continue;
            for (usize o = 0; o < region.size(); ++o)
                if (region[o])
                    for (u32 k = 0; k < 3; ++k) rgb[o * 3 + k] = static_cast<f32>(s[k] / wsum);
        } else {
            membrane_fill(rgb, known, region, static_cast<u32>(bw), static_cast<u32>(bh));
            if (role == kPartHairBack) {
                const f32 reach = std::max(4.0f, 0.8f * r.faceR.y);
                for (i32 x = 0; x < bw; ++x) {
                    f32 last[3] = {0, 0, 0};
                    i32 lastY = -1;
                    for (i32 y = 0; y < bh; ++y) {
                        const usize o = static_cast<usize>(y) * static_cast<usize>(bw) + static_cast<usize>(x);
                        if (known[o]) {
                            // Média curta da coluna: a mecha, não o pixel isolado.
                            for (u32 k = 0; k < 3; ++k) last[k] = lastY < 0 ? rgb[o * 3 + k] : 0.6f * last[k] + 0.4f * rgb[o * 3 + k];
                            lastY = y;
                        } else if (region[o] && lastY >= 0) {
                            const f32 wv = 0.7f * std::exp(-static_cast<f32>(y - lastY) / reach);
                            for (u32 k = 0; k < 3; ++k) rgb[o * 3 + k] += wv * (last[k] - rgb[o * 3 + k]);
                        }
                    }
                }
            }
        }
        // Borda macia do que foi preenchido (até 3 px): sem linha reta quando a
        // parte da frente sai de cima. Só onde a parte de trás estava coberta.
        std::vector<u8> rim(region.size(), 0);
        {
            std::vector<u8> m(region.size(), 0);
            for (usize o = 0; o < region.size(); ++o) m[o] = region[o] || l.a[o] > 0.0f ? 1 : 0;
            for (u32 step = 1; step <= 3; ++step) {
                std::vector<u8> e(m.size(), 0);
                for (i32 y = 0; y < bh; ++y)
                    for (i32 x = 0; x < bw; ++x) {
                        const usize o = static_cast<usize>(y) * static_cast<usize>(bw) + static_cast<usize>(x);
                        if (!m[o]) continue;
                        bool inner = x > 0 && y > 0 && x + 1 < bw && y + 1 < bh;
                        inner = inner && m[o - 1] && m[o + 1] && m[o - static_cast<usize>(bw)] && m[o + static_cast<usize>(bw)];
                        e[o] = inner ? 1 : 0;
                        if (!inner && region[o] && !rim[o]) rim[o] = static_cast<u8>(step);
                    }
                m.swap(e);
            }
        }
        for (i32 y = l.box.y0; y < l.box.y1; ++y)
            for (i32 x = l.box.x0; x < l.box.x1; ++x) {
                const usize o = l.at(x, y);
                if (!region[o]) continue;
                const usize i = c.at(x, y);
                const Mix& mx = mixes[o];
                f32 mk = 0.0f;
                for (u32 j = 0; j < mx.count; ++j)
                    if (mx.role[j] == role) mk = mx.m[j];
                f32 B[3] = {rgb[o * 3], rgb[o * 3 + 1], rgb[o * 3 + 2]};
                const f32 front = std::max(1.0f - mk, 1e-4f);
                if (mk <= 0.0f && rim[o]) {
                    // Coberta por inteiro em repouso: o alfa da borda não muda a soma.
                    l.a[o] = static_cast<f32>(rim[o]) / 4.0f;
                    for (u32 k = 0; k < 3; ++k) l.rgb[o * 3 + k] = B[k];
                    continue;
                }
                // Borda: recorte de verdade (como um matte). Com o fundo B
                // preenchido e a cor pura F das partes da frente ao lado, o alfa
                // delas é a posição de C entre B e F; a de trás fica inteira com
                // B (sem fantasma da borda quando a da frente anda).
                if (mk > 0.0f && mx.role[0] == role) {
                    bool frontHas = true;
                    for (u32 j = 1; j < mx.count; ++j) frontHas = frontHas && layers[mx.role[j]].has(x, y);
                    f32 F[3] = {0, 0, 0};
                    f32 fn = 0.0f;
                    for (i32 dy = -2; dy <= 2; ++dy)
                        for (i32 dx = -2; dx <= 2; ++dx) {
                            const i32 nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                            const u8 nl = labels[c.at(nx, ny)];
                            bool ours = false;
                            for (u32 j = 1; j < mx.count; ++j) ours = ours || nl == mx.role[j];
                            if (!ours || mix_at(labels, w, h, nx, ny).count != 1) continue;
                            for (u32 k = 0; k < 3; ++k) F[k] += color(c.at(nx, ny), k);
                            fn += 1.0f;
                        }
                    f32 den = 0.0f, num = 0.0f;
                    if (fn > 0.0f)
                        for (u32 k = 0; k < 3; ++k) {
                            F[k] /= fn;
                            den += (F[k] - B[k]) * (F[k] - B[k]);
                            num += (color(i, k) - B[k]) * (F[k] - B[k]);
                        }
                    if (frontHas && den > 0.02f) {
                        // O alfa nunca abaixo do que deixa a cor da frente dentro de 0..1
                        // (aí a soma é exata).
                        f32 m = saturate(num / den);
                        for (u32 k = 0; k < 3; ++k) {
                            const f32 C = color(i, k);
                            if (B[k] > 1e-4f) m = std::max(m, (B[k] - C) / B[k]);
                            if (B[k] < 1.0f - 1e-4f) m = std::max(m, (C - B[k]) / (1.0f - B[k]));
                        }
                        m = saturate(m);
                        f32 Fp[3];
                        for (u32 k = 0; k < 3; ++k) Fp[k] = m > 0.0f ? saturate((color(i, k) - (1.0f - m) * B[k]) / m) : 0.0f;
                        // As da frente dividem a cobertura m na proporção delas, mesma cor.
                        f32 T = 1.0f;
                        for (u32 j = mx.count; j-- > 1;) {
                            Layer& f = layers[mx.role[j]];
                            const usize q = f.at(x, y);
                            const f32 v = m * mx.m[j] / std::max(1.0f - mk, 1e-6f);
                            f.a[q] = m < 1.0f / 255.0f ? 0.0f : std::min(1.0f, v / std::max(T, 1e-6f));
                            T = std::max(0.0f, T - v);
                            for (u32 k = 0; k < 3; ++k) f.rgb[q * 3 + k] = Fp[k];
                        }
                        l.a[o] = 1.0f;
                        for (u32 k = 0; k < 3; ++k) l.rgb[o * 3 + k] = B[k];
                        continue;
                    }
                }
                if (mk > 0.0f) {
                    // A parte de trás aparece na borda (peso mk) com a cor de fundo e
                    // a cor das da frente compensa. Sem passar de 0..1: a cor de
                    // fundo chega só até onde a compensação cabe (a soma fica exata).
                    f32 t = 1.0f;
                    const f32 g = mk / front;
                    for (u32 k = 0; k < 3; ++k) {
                        const f32 C = color(i, k), dB = B[k] - C;
                        if (std::fabs(dB) < 1e-6f) continue;
                        const f32 limit = dB > 0.0f ? C / (g * dB) : (C - 1.0f) / (g * dB);
                        t = std::min(t, std::max(0.0f, limit));
                    }
                    for (u32 k = 0; k < 3; ++k) B[k] = color(i, k) + t * (B[k] - color(i, k));
                }
                l.a[o] = 1.0f;
                for (u32 k = 0; k < 3; ++k) l.rgb[o * 3 + k] = B[k];
                if (mk <= 0.0f) continue;
                for (u32 j = 0; j < mx.count; ++j) {
                    if (mx.role[j] <= role) continue;
                    Layer& f = layers[mx.role[j]];
                    if (!f.has(x, y)) continue;
                    const usize q = f.at(x, y);
                    for (u32 k = 0; k < 3; ++k) {
                        const f32 C = color(i, k);
                        f.rgb[q * 3 + k] = saturate(C - (mk / front) * (B[k] - C));
                    }
                }
            }
        report(progress, progressCtx, 0.68f + 0.27f * static_cast<f32>(done) / static_cast<f32>(std::size(fillOrder)));
    }

    for (const Seam& sm : seams) {
        Layer& f = layers[sm.role];
        const i32 x = static_cast<i32>(sm.i % w), y = static_cast<i32>(sm.i / w);
        if (!f.has(x, y)) continue;
        const usize o = f.at(x, y);
        if (f.a[o] >= sm.a) continue;
        f.a[o] = sm.a;
        for (u32 k = 0; k < 3; ++k) f.rgb[o * 3 + k] = color(sm.i, k);
        // Por baixo, o cabelo de trás inteiro com a mesma cor.
        Layer& b = layers[kPartHairBack];
        if (b.has(x, y)) {
            const usize q = b.at(x, y);
            b.a[q] = 1.0f;
            for (u32 k = 0; k < 3; ++k) b.rgb[q * 3 + k] = color(sm.i, k);
        }
    }

    // Abertura de cada olho (branco + íris na coluna do meio): a pálpebra desce
    // do topo dela até a linha de baixo.
    for (u32 e = 0; e < 2; ++e) {
        const Vec2 ce = r.eye[e], s = Vec2{std::max(r.eyeR[e].x, 0.5f), std::max(r.eyeR[e].y, 0.5f)};
        const Box b = box_of(ce.x - 0.35f * s.x, ce.y - 1.5f * s.y, ce.x + 0.35f * s.x + 1, ce.y + 1.5f * s.y + 1, w, h);
        i32 top = H, low = -1;
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x) {
                const u8 l = labels[c.at(x, y)];
                if (l == kPartEyeWhiteL + e || l == kPartIrisL + e) { top = std::min(top, y); low = std::max(low, y + 1); }
            }
        if (low > top && low - top >= 2)
            out.lid[e] = Vec2{std::clamp((static_cast<f32>(top) - ce.y) / s.y, -1.5f, 0.5f),
                              std::clamp((static_cast<f32>(low) - ce.y) / s.y, -0.5f, 1.5f)};
    }

    // --- 14) Saída: RGBA8 de alfa reto, recortado no que a parte tem.
    for (u32 role = 0; role < kPartRoleCount; ++role) {
        const Layer& l = layers[role];
        if (l.box.empty()) continue;
        Box b{W, H, 0, 0};
        for (i32 y = l.box.y0; y < l.box.y1; ++y)
            for (i32 x = l.box.x0; x < l.box.x1; ++x)
                if (l.a[l.at(x, y)] * 255.0f >= 0.5f) {
                    b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y);
                    b.x1 = std::max(b.x1, x + 1); b.y1 = std::max(b.y1, y + 1);
                }
        if (b.empty()) continue;
        Part p;
        p.role = role;
        p.x0 = static_cast<u32>(b.x0);
        p.y0 = static_cast<u32>(b.y0);
        p.w = static_cast<u32>(b.x1 - b.x0);
        p.h = static_cast<u32>(b.y1 - b.y0);
        p.rgba.assign(static_cast<usize>(p.w) * p.h * 4, 0);
        for (i32 y = b.y0; y < b.y1; ++y)
            for (i32 x = b.x0; x < b.x1; ++x) {
                const usize o = l.at(x, y);
                u8* dst = &p.rgba[(static_cast<usize>(y - b.y0) * p.w + static_cast<usize>(x - b.x0)) * 4];
                const f32 a = saturate(l.a[o]);
                const u8 a8 = static_cast<u8>(std::lround(a * 255.0f));
                if (!a8) continue;
                for (u32 k = 0; k < 3; ++k) dst[k] = linear_to_srgb8(l.rgb[o * 3 + k]);
                dst[3] = a8;
            }
        out.parts.push_back(std::move(p));
    }
    out.labels = std::move(labels);
    for (u8& l : out.labels)
        if (l >= kPartRoleCount) l = kNone;
    report(progress, progressCtx, 1.0f);
    return !out.parts.empty();
}

void composite_parts(const PartSet& set, std::vector<f32>& pm) {
    const usize n = static_cast<usize>(set.width) * set.height;
    pm.assign(n * 4, 0.0f);
    for (const Part& p : set.parts)
        for (u32 y = 0; y < p.h; ++y)
            for (u32 x = 0; x < p.w; ++x) {
                const u8* s = &p.rgba[(static_cast<usize>(y) * p.w + x) * 4];
                if (!s[3]) continue;
                const u32 gx = p.x0 + x, gy = p.y0 + y;
                if (gx >= set.width || gy >= set.height) continue;
                f32* d = &pm[(static_cast<usize>(gy) * set.width + gx) * 4];
                const f32 a = static_cast<f32>(s[3]) / 255.0f;
                for (u32 k = 0; k < 3; ++k) d[k] = srgb_to_linear(s[k]) * a + d[k] * (1.0f - a);
                d[3] = a + d[3] * (1.0f - a);
            }
}

// =============================================================================
//  Movimento
// =============================================================================

namespace {

/// Raio do olho e as linhas de abertura (topo da abertura e linha de baixo, em
/// px de repouso) a partir do formato medido `lid` (frações do raio y).
struct EyeOpen {
    Vec2 c, s;
    f32 top, low;
};
EyeOpen eye_open(const Rig& r, u32 eye, Vec2 lid) noexcept {
    EyeOpen o;
    o.c = r.eye[eye & 1u];
    o.s = Vec2{std::max(r.eyeR[eye & 1u].x, 0.5f), std::max(r.eyeR[eye & 1u].y, 0.5f)};
    o.top = o.c.y + lid.x * o.s.y;
    o.low = o.c.y + std::max(lid.y, lid.x + 0.1f) * o.s.y;
    return o;
}

/// Quanto a pálpebra desce na coluna `x` (fechamento k): mais no meio, nada nos cantos.
f32 lid_drop(const EyeOpen& o, f32 k, f32 x) noexcept {
    if (!(k > 0.0f)) return 0.0f;
    const f32 u = (x - o.c.x) / (1.1f * o.s.x);
    const f32 core = 1.0f - 0.45f * std::min(u * u, 1.0f);
    const f32 fade = 1.0f - sstep(1.9f, 2.6f, std::fabs(u));   // o rabo do delineado desce junto
    return std::min(k, 1.0f) * (o.low - o.top) * core * fade;
}

f32 eye_mean_y(const Rig& r) noexcept { return 0.5f * (r.eye[0].y + r.eye[1].y); }

} // namespace

f32 lid_line(const Rig& rig, u32 eye, f32 k, f32 x, Vec2 lid) noexcept {
    // Espelho de shaders/effects/face_rig_part.frag (lid_line).
    const EyeOpen o = eye_open(rig, eye, lid);
    const f32 u = (x - o.c.x) / (1.1f * o.s.x);
    const f32 uu = std::min(u * u, 1.0f);
    // Topo da abertura em arco (cantos mais baixos) descendo até a linha de baixo.
    const f32 arc = o.top + (o.c.y - o.top) * 0.8f * uu - 0.15f * o.s.y;
    const f32 lowArc = o.low - (o.low - o.c.y) * 0.3f * uu;
    return arc + saturate(k) * (lowArc - arc);
}

f32 hair_hang(const Rig& r, u32 role, Vec2 p) noexcept {
    if (role == kPartHairFront) {
        const f32 eyeY = eye_mean_y(r);
        const f32 root = r.hairTop.y + 0.35f * std::max(eyeY - r.eyeR[0].y - r.hairTop.y, 1.0f);
        return sstep(root, eyeY, p.y);
    }
    if (role == kPartHairBack || role == kPartSideLocks) {
        const f32 root = r.face.y - 0.6f * r.faceR.y;
        const f32 tip = std::max(r.neck.y + 1.2f * r.faceR.y, root + 1.0f);
        const f32 t = saturate((p.y - root) / (tip - root));
        return t * t * (3.0f - 2.0f * t) * 0.6f + 0.4f * t;
    }
    return 0.0f;
}

Vec2 part_displace(const Rig& r, const Pose& pose, u32 role, f32 depth, Vec2 swing, Vec2 lid, Vec2 p) noexcept {
    Vec2 q = p;
    // 1) O próprio movimento da parte.
    switch (role) {
        case kPartFace: case kPartMouth: q.y += mouth_dy(r, saturate(pose.mouthOpen), pose.smile, q); break;
        case kPartBrowL: case kPartBrowR: q.y += brow_dy(r, role - kPartBrowL, pose.brow[role - kPartBrowL], q); break;
        case kPartLashL: case kPartLashR: {
            // Fechando, a linha dos cílios desce até a de baixo e achata (o rabo
            // do delineado deita junto, sem ficar um "^" no canto).
            const u32 e = role - kPartLashL;
            const f32 k = 1.0f - saturate(pose.eyeOpen[e]);
            if (k > 0.0f) {
                const EyeOpen o = eye_open(r, e, lid);
                const f32 u = (q.x - o.c.x) / (1.1f * o.s.x);
                const f32 arc = o.top + (o.c.y - o.top) * 0.8f * std::min(u * u, 1.0f);
                q.y = arc + (q.y - arc) * (1.0f - 0.7f * k) + lid_drop(o, k, q.x);
            }
            break;
        }
        default: break;
    }
    // 2) Virar/acenar: paralaxe pela profundidade (a superfície do rosto + a da parte).
    if (pose.turn != 0.0f || pose.nod != 0.0f) {
        f32 z = 0.0f;
        const f32 eyeY = eye_mean_y(r);
        switch (role) {
            case kPartFace: case kPartEyeWhiteL: case kPartEyeWhiteR: case kPartIrisL: case kPartIrisR: case kPartLashL:
            case kPartLashR: case kPartBrowL: case kPartBrowR: case kPartNose: case kPartMouth:
                z = depth_at(r, q) + depth;
                break;
            case kPartGlasses: z = depth_at(r, Vec2{0.5f * (r.eye[0].x + r.eye[1].x), eyeY}) + depth; break;
            case kPartHairFront: {
                // Deitada na testa: segue a superfície do rosto e ganha a própria
                // profundidade descendo da raiz (no alto ela é o topo da cabeça).
                const f32 top = r.face.y - r.faceR.y;
                z = depth_at(r, q) + depth * sstep(top, top + 0.5f * std::max(eyeY - top, 1.0f), q.y);
                break;
            }
            case kPartEars: z = depth; break;
            case kPartHairBack: case kPartSideLocks: {
                // No topo da cabeça anda com ela (a mesma conta da franja: sem
                // costura); dos lados fica atrás; pendurado nos ombros, quase parado.
                // Colado no contorno do rosto anda com ele (o traço da mecha não
                // se solta da pele); mais longe, a profundidade própria.
                const f32 top = r.face.y - r.faceR.y;
                const Vec2 e{(q.x - r.face.x) / std::max(r.faceR.x, 1.0f), (q.y - r.face.y) / std::max(r.faceR.y, 1.0f)};
                const f32 behind = sstep(top, top + 0.5f * r.faceR.y, q.y) * sstep(1.0f, 1.4f, e.length());
                z = (depth_at(r, q) * (1.0f - behind) + depth * behind) * (1.0f - sstep(r.neck.y, r.neck.y + 1.2f * r.faceR.y, q.y));
                break;
            }
            default: z = 0.0f; break;
        }
        q.x += 0.55f * std::sin(pose.turn) * r.faceR.x * z;
        q.y += 0.55f * std::sin(pose.nod) * r.faceR.y * z;
    }
    // 3) Inclinar: a cabeça gira em volta do pescoço; o cabelo comprido menos.
    if (pose.tilt != 0.0f) {
        f32 wt = 0.0f;
        if (part_in_head(role)) wt = 1.0f;
        else if (role == kPartHairBack || role == kPartSideLocks)
            wt = 1.0f - 0.75f * sstep(r.face.y, r.neck.y + 0.5f * r.faceR.y, q.y);   // nos ombros, quase parado
        else if (role == kPartNeck) wt = 0.35f * sstep(r.neck.y + 0.5f * r.faceR.y, r.neck.y - 0.3f * r.faceR.y, q.y);
        if (wt > 0.0f) q = r.neck + rot(q - r.neck, pose.tilt * wt);
    }
    // 4) Física do cabelo: a ponta fica para trás do movimento da cabeça.
    if (part_is_hair(role) && (swing.x != 0.0f || swing.y != 0.0f)) {
        // A franja (sobre o rosto) balança menos e nunca atravessa o rosto; o
        // cabelo comprido pode ir mais longe.
        const f32 gain = role == kPartHairFront ? 0.45f : 1.0f;
        const f32 cap = (role == kPartHairFront ? 0.15f : 0.45f) * std::max(r.faceR.x, 1.0f);
        Vec2 sw = swing * gain;
        const f32 len = sw.length();
        if (len > cap) sw = sw * (cap / len);
        q += sw * hair_hang(r, role, p);
    }
    if (role == kPartBackground) return q;
    // 5) Corpo: o boneco inteiro balança em volta da base e respira.
    if (pose.sway != 0.0f) {
        const Vec2 pivot{r.neck.x, r.height};
        q = pivot + rot(q - pivot, pose.sway);
    }
    if (pose.breath != 0.0f) q.y -= pose.breath * r.faceR.y * sstep(r.height, r.neck.y + 0.4f * r.faceR.y, q.y);
    if (!std::isfinite(q.x) || !std::isfinite(q.y)) return p;
    return q;
}

Vec2 head_drive(const Rig& r, const Pose& pose) noexcept {
    Pose head = pose;
    head.eyeOpen[0] = head.eyeOpen[1] = 1.0f;
    head.mouthOpen = 0.0f;
    head.smile = 0.0f;
    head.brow[0] = head.brow[1] = 0.0f;
    const Vec2 root{r.face.x, r.face.y - 0.6f * r.faceR.y};
    return part_displace(r, head, kPartHairFront, 0.0f, Vec2{}, Vec2{-0.6f, 0.7f}, root) - root;
}

} // namespace aurea::face_rig
