// =============================================================================
//  Aurea / vector / VectorModifiers.cpp
//
//  Operadores de caminho do grupo vetorial, no comportamento dos operadores da
//  shape layer do After Effects (a referência é só o comportamento descrito na
//  documentação; nada aqui vem de código de outro programa):
//
//   - Deslocar caminhos (Offset Paths): o caminho cresce/encolhe `amount` px.
//     Feito com o recorte do motor: o contorno de largura 2·|amount| (com a
//     junta escolhida) unido ao preenchimento (cresce) ou tirado dele
//     (encolhe). Cópias = caminhos a amount·(k + 1 + desloc. das cópias).
//   - Franzir e inflar (Pucker & Bloat): cada vértice anda `amount` % até o
//     centro dos vértices; as alças andam o mesmo para FORA (a tangente vira).
//   - Aspereza / Ondular caminhos (Roughen / Wiggle Paths): `detail` pontos
//     novos por segmento (espaçados pelo comprimento), cada ponto deslocado até
//     `size` px pelo ruído suave do animador de texto (text::pro_wiggle) —
//     frequência 0 = parado; correlação, fases e semente iguais às do AE.
//   - Arredondar cantos (Round Corners): cada canto sem alças vira um arco de
//     raio até `radius` (limitado à metade dos lados).
//   - Torcer (Twist): os pontos giram em volta do centro proporcionalmente à
//     distância (o mais longe gira `angle`); os segmentos são subdivididos antes.
//   - Zigue-zague (Zig Zag): `ridges` pontos novos por segmento, alternando
//     para cada lado da normal `size` px; canto ou suave.
//   - Ondular transform (Wiggle Transform): uma afim aleatória (âncora,
//     posição, escala, giro) sobre os caminhos, animada no tempo.
// =============================================================================
#include "aurea/vector/Vector.hpp"

#include "aurea/text/TextAnimator.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>

namespace aurea::vector {
namespace {

constexpr f32 kKappa = 0.5522847498f;
/// Teto de vértices que um operador pode criar num grupo (memória/tempo).
constexpr usize kMaxModifierVertices = 200000;

struct Cubic {
    Vec2 p0, p1, p2, p3;
};

inline Vec2 lerp2(Vec2 a, Vec2 b, f32 t) noexcept { return a + (b - a) * t; }
inline Vec2 left_normal(Vec2 d) noexcept { return Vec2{-d.y, d.x}; }

usize segment_count(const BezierPath& p) noexcept {
    const usize n = p.v.size();
    if (n < 2) return 0;
    return p.closed ? n : n - 1;
}

Cubic segment(const BezierPath& p, usize i) noexcept {
    const usize j = (i + 1) % p.v.size();
    return Cubic{p.v[i].p, p.v[i].p + p.v[i].out, p.v[j].p + p.v[j].in, p.v[j].p};
}

Vec2 eval(const Cubic& c, f32 t) noexcept {
    const f32 u = 1.0f - t;
    return c.p0 * (u * u * u) + c.p1 * (3.0f * u * u * t) + c.p2 * (3.0f * u * t * t) + c.p3 * (t * t * t);
}

Vec2 deriv(const Cubic& c, f32 t) noexcept {
    const f32 u = 1.0f - t;
    return (c.p1 - c.p0) * (3.0f * u * u) + (c.p2 - c.p1) * (6.0f * u * t) + (c.p3 - c.p2) * (3.0f * t * t);
}

/// Direção da curva em t (cai para a corda quando a derivada some, nas pontas
/// de um segmento reto sem alças).
Vec2 direction(const Cubic& c, f32 t) noexcept {
    Vec2 d = deriv(c, t);
    if (d.length_sq() < 1e-10f) d = deriv(c, std::clamp(t, 0.01f, 0.99f));
    if (d.length_sq() < 1e-10f) d = c.p3 - c.p0;
    return d.normalized();
}

void split(const Cubic& c, f32 t, Cubic& a, Cubic& b) noexcept {
    const Vec2 p01 = lerp2(c.p0, c.p1, t), p12 = lerp2(c.p1, c.p2, t), p23 = lerp2(c.p2, c.p3, t);
    const Vec2 q0 = lerp2(p01, p12, t), q1 = lerp2(p12, p23, t), m = lerp2(q0, q1, t);
    a = Cubic{c.p0, p01, q0, m};
    b = Cubic{m, q1, p23, c.p3};
}

/// Tabela comprimento → parâmetro de um segmento (o espaçamento pelo
/// comprimento: um segmento reto sem alças NÃO é uniforme no parâmetro).
struct ArcTable {
    static constexpr u32 kSamples = 48;
    f32 len[kSamples + 1]{};
    explicit ArcTable(const Cubic& c) noexcept {
        Vec2 prev = c.p0;
        for (u32 i = 1; i <= kSamples; ++i) {
            const Vec2 q = eval(c, static_cast<f32>(i) / kSamples);
            len[i] = len[i - 1] + (q - prev).length();
            prev = q;
        }
    }
    [[nodiscard]] f32 total() const noexcept { return len[kSamples]; }
    /// Parâmetro na fração `u` (0..1) do comprimento.
    [[nodiscard]] f32 t_at(f32 u) const noexcept {
        const f32 L = total();
        if (L <= 1e-9f) return u;
        const f32 s = std::clamp(u, 0.0f, 1.0f) * L;
        u32 i = 1;
        while (i < kSamples && len[i] < s) ++i;
        const f32 a = len[i - 1], b = len[i];
        const f32 f = b > a ? (s - a) / (b - a) : 0.0f;
        return (static_cast<f32>(i - 1) + f) / kSamples;
    }
};

/// Um ponto amostrado ao longo do caminho (para Aspereza e Zigue-zague).
struct Sample {
    Vec2 p, dir;
    f32 piece = 0.0f;   ///< comprimento do pedaço vizinho (tamanho das alças suaves)
};

/// `pieces` pedaços por segmento, espaçados pelo comprimento. Fechado: cada
/// segmento entra com o ponto inicial; aberto: mais o último vértice.
void sample_path(const BezierPath& p, u32 pieces, std::vector<Sample>& out) {
    out.clear();
    const usize segs = segment_count(p);
    if (segs == 0) return;
    pieces = std::max<u32>(1, pieces);
    for (usize s = 0; s < segs; ++s) {
        const Cubic c = segment(p, s);
        const ArcTable tab(c);
        const f32 piece = tab.total() / static_cast<f32>(pieces);
        for (u32 k = 0; k < pieces; ++k) {
            const f32 t = tab.t_at(static_cast<f32>(k) / static_cast<f32>(pieces));
            Vec2 dir = direction(c, t);
            if (k == 0) {
                // No vértice: a média das direções de chegada e saída (bissetriz).
                const bool hasPrev = p.closed || s > 0;
                if (hasPrev) {
                    const Cubic pc = segment(p, (s + segs - 1) % segs);
                    const Vec2 m = direction(pc, 1.0f) + dir;
                    if (m.length_sq() > 1e-8f) dir = m.normalized();
                }
            }
            out.push_back(Sample{eval(c, t), dir, piece});
        }
    }
    if (!p.closed) {
        const Cubic c = segment(p, segs - 1);
        const ArcTable tab(c);
        out.push_back(Sample{c.p3, direction(c, 1.0f), tab.total() / static_cast<f32>(pieces)});
    }
}

/// Pontos amostrados → caminho: cantos (sem alças) ou suaves (alças ao longo da direção).
BezierPath from_samples(const std::vector<Sample>& s, const std::vector<Vec2>& pos, bool closed, bool smooth) {
    BezierPath b;
    b.closed = closed;
    b.v.resize(s.size());
    for (usize i = 0; i < s.size(); ++i) {
        b.v[i].p = pos[i];
        if (!smooth) continue;
        // Alças ao longo da direção local, com o comprimento de um terço da
        // distância até os vizinhos deslocados.
        const usize n = s.size();
        const bool hasPrev = closed || i > 0, hasNext = closed || i + 1 < n;
        const Vec2 d = s[i].dir;
        if (hasPrev) b.v[i].in = d * (-(pos[i] - pos[(i + n - 1) % n]).length() / 3.0f);
        if (hasNext) b.v[i].out = d * ((pos[(i + 1) % n] - pos[i]).length() / 3.0f);
    }
    return b;
}

/// Subdivide cada segmento em `n` pedaços iguais no parâmetro (forma igual).
BezierPath subdivide(const BezierPath& p, u32 n) {
    const usize segs = segment_count(p);
    if (segs == 0 || n <= 1) return p;
    std::vector<Cubic> pieces;
    pieces.reserve(segs * n);
    for (usize s = 0; s < segs; ++s) {
        Cubic rest = segment(p, s);
        for (u32 k = 0; k < n; ++k) {
            if (k + 1 == n) { pieces.push_back(rest); break; }
            Cubic a, b;
            split(rest, 1.0f / static_cast<f32>(n - k), a, b);
            pieces.push_back(a);
            rest = b;
        }
    }
    BezierPath r;
    r.closed = p.closed;
    r.v.resize(p.closed ? pieces.size() : pieces.size() + 1);
    for (usize k = 0; k < pieces.size(); ++k) {
        r.v[k].p = pieces[k].p0;
        r.v[k].out = pieces[k].p1 - pieces[k].p0;
        if (k > 0) r.v[k].in = pieces[k - 1].p2 - pieces[k].p0;
    }
    if (p.closed) {
        r.v[0].in = pieces.back().p2 - pieces[0].p0;
    } else {
        r.v[0].in = p.v.front().in;
        BezierVertex& last = r.v.back();
        last.p = pieces.back().p3;
        last.in = pieces.back().p2 - pieces.back().p3;
        last.out = p.v.back().out;
    }
    return r;
}

/// Pedaços por segmento sem passar do teto de vértices.
u32 capped_pieces(const std::vector<BezierPath>& paths, u32 wanted) noexcept {
    usize segs = 0;
    for (const BezierPath& p : paths) segs += segment_count(p);
    if (segs == 0) return std::max<u32>(1, wanted);
    const usize most = std::max<usize>(1, kMaxModifierVertices / segs);
    return static_cast<u32>(std::clamp<usize>(wanted, 1, most));
}

bool bbox_of(const std::vector<BezierPath>& paths, Vec2& mn, Vec2& mx) noexcept {
    bool any = false;
    for (const BezierPath& p : paths) {
        for (const BezierVertex& v : p.v) {
            if (!any) { mn = mx = v.p; any = true; continue; }
            mn = Vec2{std::min(mn.x, v.p.x), std::min(mn.y, v.p.y)};
            mx = Vec2{std::max(mx.x, v.p.x), std::max(mx.y, v.p.y)};
        }
    }
    return any;
}

Vec2 rotate_about(Vec2 p, Vec2 c, f32 radians) noexcept {
    const f32 cs = std::cos(radians), sn = std::sin(radians);
    const Vec2 d = p - c;
    return Vec2{c.x + d.x * cs - d.y * sn, c.y + d.x * sn + d.y * cs};
}

u32 seed_of(f32 v) noexcept { return static_cast<u32>(std::clamp(std::lround(v), 0l, 1000000l)); }

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

} // namespace

// =============================================================================
// Padrões e faixas
// =============================================================================
VectorModifier make_modifier(VectorModifierType type) noexcept {
    VectorModifier m;
    m.type = type;
    f32* p = m.params;
    switch (type) {
        case VectorModifierType::OffsetPaths:
            p[kVecModOffsetAmount] = 10.0f; p[kVecModOffsetJoin] = 0.0f; p[kVecModOffsetMiter] = 4.0f;
            p[kVecModOffsetCopies] = 1.0f; p[kVecModOffsetCopyOffset] = 0.0f;
            break;
        case VectorModifierType::PuckerBloat: p[kVecModPuckerAmount] = 30.0f; break;
        case VectorModifierType::Roughen:
        case VectorModifierType::WigglePaths:
            p[kVecModRoughSize] = 10.0f; p[kVecModRoughDetail] = 3.0f; p[kVecModRoughPoints] = 1.0f;
            p[kVecModRoughFreq] = type == VectorModifierType::WigglePaths ? 2.0f : 0.0f;
            p[kVecModRoughCorrelation] = 50.0f;
            break;
        case VectorModifierType::RoundCorners: p[kVecModRoundRadius] = 10.0f; break;
        case VectorModifierType::Twist: p[kVecModTwistAngle] = 45.0f; break;
        case VectorModifierType::ZigZag:
            p[kVecModZigSize] = 10.0f; p[kVecModZigRidges] = 5.0f; p[kVecModZigPoints] = 0.0f;
            break;
        case VectorModifierType::WiggleTransform:
            p[kVecModWigPosX] = 20.0f; p[kVecModWigPosY] = 20.0f; p[kVecModWigFreq] = 2.0f; p[kVecModWigCorrelation] = 50.0f;
            break;
        default: break;
    }
    return m;
}

bool modifier_animated(const VectorModifier& m) noexcept {
    if (!m.enabled) return false;
    switch (m.type) {
        case VectorModifierType::Roughen:
        case VectorModifierType::WigglePaths: return m.params[kVecModRoughFreq] > 0.0f && m.params[kVecModRoughSize] != 0.0f;
        case VectorModifierType::WiggleTransform: return m.params[kVecModWigFreq] > 0.0f;
        default: return false;
    }
}

f32 clamp_modifier_param(VectorModifierType type, u32 param, f32 v) noexcept {
    v = finite_or(v, 0.0f);
    if (param == kVecModSeed) return std::round(std::clamp(v, 0.0f, 1000000.0f));
    switch (type) {
        case VectorModifierType::OffsetPaths:
            switch (param) {
                case kVecModOffsetAmount: return std::clamp(v, -10000.0f, 10000.0f);
                case kVecModOffsetJoin: return std::round(std::clamp(v, 0.0f, 2.0f));
                case kVecModOffsetMiter: return std::clamp(v, 1.0f, 100.0f);
                case kVecModOffsetCopies: return std::round(std::clamp(v, 1.0f, 100.0f));
                case kVecModOffsetCopyOffset: return std::clamp(v, -100.0f, 100.0f);
                default: break;
            }
            break;
        case VectorModifierType::PuckerBloat: return std::clamp(v, -1000.0f, 1000.0f);
        case VectorModifierType::Roughen:
        case VectorModifierType::WigglePaths:
            switch (param) {
                case kVecModRoughSize: return std::clamp(v, 0.0f, 10000.0f);
                case kVecModRoughDetail: return std::round(std::clamp(v, 0.0f, 100.0f));
                case kVecModRoughPoints: return std::round(std::clamp(v, 0.0f, 1.0f));
                case kVecModRoughFreq: return std::clamp(v, 0.0f, 100.0f);
                case kVecModRoughCorrelation: return std::clamp(v, 0.0f, 100.0f);
                default: break;
            }
            break;
        case VectorModifierType::RoundCorners: return std::clamp(v, 0.0f, 10000.0f);
        case VectorModifierType::Twist:
            return param == kVecModTwistAngle ? std::clamp(v, -36000.0f, 36000.0f) : std::clamp(v, -100000.0f, 100000.0f);
        case VectorModifierType::ZigZag:
            switch (param) {
                case kVecModZigSize: return std::clamp(v, -10000.0f, 10000.0f);
                case kVecModZigRidges: return std::round(std::clamp(v, 0.0f, 100.0f));
                case kVecModZigPoints: return std::round(std::clamp(v, 0.0f, 1.0f));
                default: break;
            }
            break;
        case VectorModifierType::WiggleTransform:
            switch (param) {
                case kVecModWigScaleX: case kVecModWigScaleY: return std::clamp(v, -1000.0f, 1000.0f);
                case kVecModWigRotation: return std::clamp(v, -36000.0f, 36000.0f);
                case kVecModWigFreq: return std::clamp(v, 0.0f, 100.0f);
                case kVecModWigCorrelation: return std::clamp(v, 0.0f, 100.0f);
                default: break;
            }
            break;
        default: break;
    }
    return std::clamp(v, -1000000.0f, 1000000.0f);
}

// =============================================================================
// Operadores
// =============================================================================
void offset_paths(std::vector<BezierPath>& paths, f32 amount, u8 join, f32 miterLimit, f32 copies, f32 copyOffset,
                  f32 tolerance, std::vector<u8>* tags) {
    const u32 n = static_cast<u32>(std::clamp(std::lround(copies), 1l, 100l));
    const f32 tol = std::max(tolerance, 0.01f);
    std::vector<BezierPath> out;
    std::vector<u8> outTags;
    for (usize pi = 0; pi < paths.size(); ++pi) {
        const BezierPath& src = paths[pi];
        Contour c;
        flatten(src, tol, c);
        if (c.pts.empty()) continue;
        c.closed = true;   // o deslocamento vale para a área (aberto = fechado implícito, como o preenchimento)
        std::vector<Contour> base;
        resolve_fill({c}, FillRule::NonZero, base);
        for (u32 k = 0; k < n; ++k) {
            const f32 d = amount * (static_cast<f32>(k + 1) + copyOffset);
            std::vector<Contour> rings;
            if (std::fabs(d) < 1e-4f) {
                rings = base;
            } else {
                std::vector<Contour> band;
                stroke_to_rings({c}, 2.0f * std::fabs(d), 0, std::min<u8>(join, 2), std::max(1.0f, miterLimit), tol, band);
                if (d > 0.0f) boolean_op({base, band}, FillRule::NonZero, BoolOp::Union, rings);
                else if (!base.empty()) boolean_op({base, band}, FillRule::NonZero, BoolOp::Subtract, rings);
            }
            for (const Contour& r : rings) {
                if (r.pts.size() < 3) continue;
                BezierPath b;
                b.closed = true;
                b.v.reserve(r.pts.size());
                for (Vec2 q : r.pts) b.v.push_back(BezierVertex{q, {}, {}});
                out.push_back(std::move(b));
                outTags.push_back(tags && pi < tags->size() ? (*tags)[pi] : u8{0});
            }
        }
    }
    paths = std::move(out);
    if (tags) *tags = std::move(outTags);
}

void pucker_bloat(std::vector<BezierPath>& paths, f32 amountPercent) {
    const f32 a = amountPercent * 0.01f;
    if (std::fabs(a) < 1e-6f) return;
    for (BezierPath& p : paths) {
        if (p.v.empty()) continue;
        Vec2 c{0.0f, 0.0f};
        for (const BezierVertex& v : p.v) c += v.p;
        c = c / static_cast<f32>(p.v.size());
        for (BezierVertex& v : p.v) {
            // Alças em coordenadas absolutas: andam para o lado contrário.
            const Vec2 hin = v.p + v.in, hout = v.p + v.out;
            const Vec2 np = v.p + (c - v.p) * a;
            const Vec2 nin = hin + (c - hin) * -a;
            const Vec2 nout = hout + (c - hout) * -a;
            v.p = np;
            v.in = nin - np;
            v.out = nout - np;
        }
    }
}

void roughen(std::vector<BezierPath>& paths, f32 size, f32 detail, bool smooth, f64 timeSec, f32 frequency,
             f32 correlationPercent, f32 temporalPhase, f32 spatialPhase, u32 seed) {
    if (!(size > 0.0f)) return;
    const u32 pieces = capped_pieces(paths, static_cast<u32>(std::clamp(std::lround(detail), 0l, 100l)) + 1u);
    const f32 corr = std::clamp(correlationPercent * 0.01f, 0.0f, 1.0f);
    std::vector<Sample> s;
    std::vector<Vec2> pos;
    u32 unit = 0;   // índice do ponto em todo o grupo (cada ponto sorteia o seu)
    for (BezierPath& p : paths) {
        sample_path(p, pieces, s);
        if (s.empty()) continue;
        pos.resize(s.size());
        for (usize i = 0; i < s.size(); ++i, ++unit) {
            const f32 dx = text::pro_wiggle(seed, unit, 0, timeSec, frequency, corr, temporalPhase, spatialPhase, -1.0f, 1.0f);
            const f32 dy = text::pro_wiggle(seed, unit, 1, timeSec, frequency, corr, temporalPhase, spatialPhase, -1.0f, 1.0f);
            pos[i] = s[i].p + Vec2{dx, dy} * size;
        }
        p = from_samples(s, pos, p.closed, smooth);
    }
}

void round_corners(std::vector<BezierPath>& paths, f32 radius) {
    if (!(radius > 0.0f)) return;
    for (BezierPath& p : paths) {
        const usize n = p.v.size();
        if (n < 3) continue;
        BezierPath r;
        r.closed = p.closed;
        r.v.reserve(n * 2);
        for (usize i = 0; i < n; ++i) {
            const BezierVertex& v = p.v[i];
            const bool end = !p.closed && (i == 0 || i + 1 == n);
            const bool corner = v.in.length_sq() < 1e-8f && v.out.length_sq() < 1e-8f;
            if (end || !corner) { r.v.push_back(v); continue; }
            const BezierVertex& a = p.v[(i + n - 1) % n];
            const BezierVertex& b = p.v[(i + 1) % n];
            // Direção de cada lado: a alça do vizinho (lado curvo) ou o vizinho.
            Vec2 toA = (a.p + a.out) - v.p, toB = (b.p + b.in) - v.p;
            if (toA.length_sq() < 1e-8f) toA = a.p - v.p;
            if (toB.length_sq() < 1e-8f) toB = b.p - v.p;
            const f32 la = (a.p - v.p).length(), lb = (b.p - v.p).length();
            const f32 ra = std::min(radius, la * 0.5f), rb = std::min(radius, lb * 0.5f);
            const Vec2 da = toA.normalized(), db = toB.normalized();
            if (ra <= 1e-4f || rb <= 1e-4f || (std::fabs(da.x * db.y - da.y * db.x) < 1e-4f && da.dot(db) < 0.0f)) {
                r.v.push_back(v);   // lado nulo ou colinear: não há canto a arredondar
                continue;
            }
            const Vec2 pa = v.p + da * ra, pb = v.p + db * rb;
            r.v.push_back(BezierVertex{pa, {}, (v.p - pa) * kKappa});
            r.v.push_back(BezierVertex{pb, (v.p - pb) * kKappa, {}});
        }
        p = std::move(r);
    }
}

void twist(std::vector<BezierPath>& paths, f32 angle, Vec2 centerOffset) {
    if (std::fabs(angle) < 1e-4f) return;
    Vec2 mn, mx;
    if (!bbox_of(paths, mn, mx)) return;
    const Vec2 c = (mn + mx) * 0.5f + centerOffset;
    const u32 n = capped_pieces(paths, static_cast<u32>(std::clamp(std::fabs(angle) / 12.0f, 1.0f, 24.0f)) + 2u);
    for (BezierPath& p : paths) p = subdivide(p, n);
    f32 R = 0.0f;
    for (const BezierPath& p : paths) for (const BezierVertex& v : p.v) R = std::max(R, (v.p - c).length());
    if (R <= 1e-6f) return;
    const f32 k = angle * kDeg2Rad / R;
    auto tw = [&](Vec2 q) { return rotate_about(q, c, k * (q - c).length()); };
    for (BezierPath& p : paths) {
        for (BezierVertex& v : p.v) {
            const Vec2 np = tw(v.p);
            const Vec2 nin = tw(v.p + v.in), nout = tw(v.p + v.out);
            v.in = v.in.length_sq() > 0.0f ? nin - np : Vec2{};
            v.out = v.out.length_sq() > 0.0f ? nout - np : Vec2{};
            v.p = np;
        }
    }
}

void zigzag(std::vector<BezierPath>& paths, f32 size, f32 ridges, bool smooth) {
    if (std::fabs(size) < 1e-6f) return;
    const u32 pieces = capped_pieces(paths, static_cast<u32>(std::clamp(std::lround(ridges), 0l, 100l)) + 1u);
    std::vector<Sample> s;
    std::vector<Vec2> pos;
    for (BezierPath& p : paths) {
        sample_path(p, pieces, s);
        if (s.empty()) continue;
        pos.resize(s.size());
        for (usize i = 0; i < s.size(); ++i) {
            const f32 side = (i % 2 == 0) ? 1.0f : -1.0f;
            pos[i] = s[i].p + left_normal(s[i].dir) * (size * side);
        }
        p = from_samples(s, pos, p.closed, smooth);
    }
}

void wiggle_transform(std::vector<BezierPath>& paths, Vec2 anchor, Vec2 position, Vec2 scalePercent, f32 rotation,
                      f64 timeSec, f32 frequency, f32 correlationPercent, f32 temporalPhase, f32 spatialPhase, u32 seed) {
    Vec2 mn, mx;
    if (!bbox_of(paths, mn, mx)) return;
    const Vec2 pivot = (mn + mx) * 0.5f;
    const f32 corr = std::clamp(correlationPercent * 0.01f, 0.0f, 1.0f);
    f32 n[7];
    for (u32 d = 0; d < 7; ++d) n[d] = text::pro_wiggle(seed, 1u, d, timeSec, frequency, corr, temporalPhase, spatialPhase, -1.0f, 1.0f);
    const Vec2 a{anchor.x * n[0], anchor.y * n[1]};
    const Vec2 t{position.x * n[2], position.y * n[3]};
    const Vec2 s{1.0f + scalePercent.x * 0.01f * n[4], 1.0f + scalePercent.y * 0.01f * n[5]};
    const Affine2 M = Affine2::translate(pivot + t) * Affine2::rotate(rotation * n[6]) * Affine2::scale(s)
                    * Affine2::translate(-(pivot + a));
    for (BezierPath& p : paths) p = transform_path(p, M);
}

void apply_modifiers(const std::vector<VectorModifier>& mods, std::vector<BezierPath>& paths, f32 tolerance,
                     std::vector<u8>* tags) {
    if (tags && tags->size() != paths.size()) tags->resize(paths.size(), 0);
    for (const VectorModifier& m : mods) {
        if (!m.enabled || paths.empty()) continue;
        f32 P[kVecModParams];
        for (u32 i = 0; i < kVecModParams; ++i) P[i] = clamp_modifier_param(m.type, i, m.params[i]);
        const u32 seed = seed_of(P[kVecModSeed]);
        const f64 time = std::isfinite(m.time) ? m.time : 0.0;
        switch (m.type) {
            case VectorModifierType::OffsetPaths:
                offset_paths(paths, P[kVecModOffsetAmount], static_cast<u8>(P[kVecModOffsetJoin]), P[kVecModOffsetMiter],
                             P[kVecModOffsetCopies], P[kVecModOffsetCopyOffset], tolerance, tags);
                break;
            case VectorModifierType::PuckerBloat: pucker_bloat(paths, P[kVecModPuckerAmount]); break;
            case VectorModifierType::Roughen:
            case VectorModifierType::WigglePaths:
                roughen(paths, P[kVecModRoughSize], P[kVecModRoughDetail], P[kVecModRoughPoints] > 0.5f, time, P[kVecModRoughFreq],
                        P[kVecModRoughCorrelation], P[kVecModRoughTemporalPhase], P[kVecModRoughSpatialPhase], seed);
                break;
            case VectorModifierType::RoundCorners: round_corners(paths, P[kVecModRoundRadius]); break;
            case VectorModifierType::Twist:
                twist(paths, P[kVecModTwistAngle], Vec2{P[kVecModTwistCenterX], P[kVecModTwistCenterY]});
                break;
            case VectorModifierType::ZigZag: zigzag(paths, P[kVecModZigSize], P[kVecModZigRidges], P[kVecModZigPoints] > 0.5f); break;
            case VectorModifierType::WiggleTransform:
                wiggle_transform(paths, Vec2{P[kVecModWigAnchorX], P[kVecModWigAnchorY]}, Vec2{P[kVecModWigPosX], P[kVecModWigPosY]},
                                 Vec2{P[kVecModWigScaleX], P[kVecModWigScaleY]}, P[kVecModWigRotation], time, P[kVecModWigFreq],
                                 P[kVecModWigCorrelation], P[kVecModWigTemporalPhase], P[kVecModWigSpatialPhase], seed);
                break;
            default: break;
        }
    }
}

// =============================================================================
// After Effects (match names)
// =============================================================================
bool modifier_from_ae_match_name(const std::string& name, VectorModifierType& out) noexcept {
    struct Entry { const char* name; VectorModifierType type; };
    static constexpr Entry kTable[] = {
        {"ADBE Vector Filter - Offset", VectorModifierType::OffsetPaths},
        {"ADBE Vector Filter - PB", VectorModifierType::PuckerBloat},
        {"ADBE Vector Filter - Roughen", VectorModifierType::WigglePaths},
        {"ADBE Vector Filter - RC", VectorModifierType::RoundCorners},
        {"ADBE Vector Filter - Twist", VectorModifierType::Twist},
        {"ADBE Vector Filter - Zigzag", VectorModifierType::ZigZag},
        {"ADBE Vector Filter - Wiggler", VectorModifierType::WiggleTransform},
    };
    for (const Entry& e : kTable) {
        if (name == e.name) { out = e.type; return true; }
    }
    return false;
}

i32 modifier_param_from_ae_match_name(VectorModifierType type, const std::string& name) noexcept {
    struct Entry { const char* name; i32 param; };
    auto find = [&](std::initializer_list<Entry> list) -> i32 {
        for (const Entry& e : list) if (name == e.name) return e.param;
        return -1;
    };
    // Propriedades comuns dos operadores aleatórios.
    if (name == "ADBE Vector Random Seed") {
        return type == VectorModifierType::Roughen || type == VectorModifierType::WigglePaths || type == VectorModifierType::WiggleTransform
            ? static_cast<i32>(kVecModSeed) : -1;
    }
    switch (type) {
        case VectorModifierType::OffsetPaths:
            return find({{"ADBE Vector Offset Amount", kVecModOffsetAmount}, {"ADBE Vector Offset Line Join", kVecModOffsetJoin},
                         {"ADBE Vector Offset Miter Limit", kVecModOffsetMiter}, {"ADBE Vector Offset Copies", kVecModOffsetCopies},
                         {"ADBE Vector Offset Copy Offset", kVecModOffsetCopyOffset}});
        case VectorModifierType::PuckerBloat: return find({{"ADBE Vector PuckerBloat Amount", kVecModPuckerAmount}});
        case VectorModifierType::Roughen:
        case VectorModifierType::WigglePaths:
            return find({{"ADBE Vector Roughen Size", kVecModRoughSize}, {"ADBE Vector Roughen Detail", kVecModRoughDetail},
                         {"ADBE Vector Roughen Points", kVecModRoughPoints}, {"ADBE Vector Temporal Freq", kVecModRoughFreq},
                         {"ADBE Vector Correlation", kVecModRoughCorrelation}, {"ADBE Vector Temporal Phase", kVecModRoughTemporalPhase},
                         {"ADBE Vector Spatial Phase", kVecModRoughSpatialPhase}});
        case VectorModifierType::RoundCorners: return find({{"ADBE Vector RoundCorner Radius", kVecModRoundRadius}});
        case VectorModifierType::Twist:
            return find({{"ADBE Vector Twist Angle", kVecModTwistAngle}, {"ADBE Vector Twist Center", kVecModTwistCenterX}});
        case VectorModifierType::ZigZag:
            return find({{"ADBE Vector Zigzag Size", kVecModZigSize}, {"ADBE Vector Zigzag Detail", kVecModZigRidges},
                         {"ADBE Vector Zigzag Points", kVecModZigPoints}});
        case VectorModifierType::WiggleTransform:
            return find({{"ADBE Vector Wiggler Anchor", kVecModWigAnchorX}, {"ADBE Vector Wiggler Position", kVecModWigPosX},
                         {"ADBE Vector Wiggler Scale", kVecModWigScaleX}, {"ADBE Vector Wiggler Rotation", kVecModWigRotation},
                         {"ADBE Vector Xform Temporal Freq", kVecModWigFreq}, {"ADBE Vector Correlation", kVecModWigCorrelation},
                         {"ADBE Vector Temporal Phase", kVecModWigTemporalPhase}, {"ADBE Vector Spatial Phase", kVecModWigSpatialPhase}});
        default: return -1;
    }
}

} // namespace aurea::vector
