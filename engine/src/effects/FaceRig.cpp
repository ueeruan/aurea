// =============================================================================
//  Aurea / effects / FaceRig.cpp
//
//  Rig 2D de rosto (effects/FaceRig.hpp): o campo de deformação e a busca das
//  âncoras nos pixels. Tudo em px da camada.
//
//  O campo soma, no ponto de REPOUSO:
//   - olhos: a pálpebra de cima desce até a linha de baixo (o miolo do olho
//     encolhe nela e a pele logo acima estica por cima — olho fechado de
//     verdade, não achatado);
//   - boca: a linha da boca estica para baixo (o fragmento escurece o miolo),
//     o lábio de cima sobe um pouco; sorriso levanta os cantos;
//   - sobrancelhas: faixa acima de cada olho sobe/desce;
//   - virar/acenar: paralaxe numa elipsoide em volta do rosto (nariz, olhos e
//     boca mais perto andam mais; o contorno quase nada; o cabelo de trás anda
//     um pouco ao contrário);
//  depois gira a cabeça em volta do queixo (inclinar) e o corpo em volta da
//  base (balanço), com a respiração subindo o tronco. Toda transição usa
//  faixa suave com largura que cresce com o deslocamento: nada rasga nem dobra.
// =============================================================================
#include "aurea/effects/FaceRig.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace aurea::face_rig {
namespace {

f32 sstep(f32 e0, f32 e1, f32 x) noexcept {
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    const f32 t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

Vec2 rot(Vec2 v, f32 a) noexcept {
    const f32 c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

f32 gauss(Vec2 p, Vec2 c, Vec2 sigma) noexcept {
    const f32 x = (p.x - c.x) / std::max(sigma.x, 1e-3f), y = (p.y - c.y) / std::max(sigma.y, 1e-3f);
    return std::exp(-0.5f * (x * x + y * y));
}

} // namespace

EyeLines eye_lines(Vec2 c, Vec2 s) noexcept {
    const f32 sy = std::max(s.y, 0.5f);
    return {c.y - sy - 0.9f * sy, c.y - sy, c.y + 0.7f * sy};
}

namespace {

/// Linhas da boca: faixa da linha (o que estica), alcance acima e o queixo.
struct MouthLines {
    f32 lineTop, lineBot, up, chin, below;
};
MouthLines mouth_lines(const Rig& r) noexcept {
    const f32 mh = std::max(r.mouthR.y, 0.5f);
    const f32 half = std::max(0.35f * mh, 0.75f);
    const f32 chin = std::max(r.neck.y, r.mouth.y + 3.0f * mh);
    const f32 span = chin - r.mouth.y;
    return {r.mouth.y - half, r.mouth.y + half, std::max(3.0f * mh, 0.25f * span), chin, chin + 0.5f * span};
}

/// Centro da sobrancelha e raios da faixa que ela move.
void brow_of(Vec2 c, Vec2 s, Vec2& center, Vec2& radius) noexcept {
    const f32 sy = std::max(s.y, 0.5f);
    center = Vec2{c.x, c.y - 2.0f * sy};
    radius = Vec2{1.6f * std::max(s.x, 0.5f), 1.4f * sy};
}

f32 eye_close_dy(const Rig& r, u32 i, f32 k, Vec2 p) noexcept {
    if (!(k > 0.0f)) return 0.0f;
    const Vec2 c = r.eye[i], s = r.eyeR[i];
    const f32 wx = 1.0f - sstep(0.85f, 1.35f, std::fabs(p.x - c.x) / std::max(s.x, 0.5f));
    if (wx <= 0.0f) return 0.0f;
    const EyeLines L = eye_lines(c, s);
    f32 a = 0.0f;
    if (p.y <= L.band || p.y >= L.low) return 0.0f;
    if (p.y < L.top) a = (p.y - L.band) / (L.top - L.band) * (L.low - L.top);
    else a = L.low - p.y;
    return std::min(k, 1.0f) * wx * a;
}

} // namespace

f32 mouth_dy(const Rig& r, f32 open, f32 smile, Vec2 p) noexcept {
    const f32 mw = std::max(r.mouthR.x, 0.5f), mh = std::max(r.mouthR.y, 0.5f);
    const f32 ax = std::fabs(p.x - r.mouth.x) / mw;
    f32 dy = 0.0f;
    if (open > 0.0f) {
        const MouthLines L = mouth_lines(r);
        const f32 span = L.chin - r.mouth.y;
        const f32 d = std::min(open, 1.0f) * std::max(0.4f * span, 2.0f * mh);
        const f32 up = 0.15f * d, down = 0.85f * d;
        f32 v = 0.0f;
        if (p.y < L.lineTop) v = -up * sstep(L.lineTop - L.up, L.lineTop, p.y);
        else if (p.y <= L.lineBot) v = -up + (p.y - L.lineTop) / (L.lineBot - L.lineTop) * (up + down);
        else v = down * (1.0f - sstep(L.lineBot, L.below, p.y));
        dy += v * (1.0f - sstep(0.9f, 1.5f, ax));
    }
    if (smile != 0.0f) {
        const f32 lift = clampf(smile, -1.0f, 1.0f) * 0.35f * mw;
        const f32 ux = (p.x - r.mouth.x) / (1.7f * mw), uy = (p.y - r.mouth.y) / std::max(1.4f * mw, 2.0f * mh);
        const f32 w = 1.0f - sstep(0.45f, 1.0f, std::sqrt(ux * ux + uy * uy));
        dy -= lift * std::min(ax * ax, 1.0f) * w;
    }
    return dy;
}

f32 brow_dy(const Rig& r, u32 i, f32 brow, Vec2 p) noexcept {
    if (brow == 0.0f) return 0.0f;
    Vec2 c, rad;
    brow_of(r.eye[i], r.eyeR[i], c, rad);
    const f32 ux = (p.x - c.x) / rad.x, uy = (p.y - c.y) / rad.y;
    const f32 w = 1.0f - sstep(0.25f, 1.0f, std::sqrt(ux * ux + uy * uy));
    return -clampf(brow, -1.0f, 1.0f) * 0.5f * std::max(r.eyeR[i].y, 0.5f) * w;
}

namespace {

/// Alcance do cabelo (em raios do rosto) acima do centro.
f32 hair_reach(const Rig& r) noexcept {
    return std::max(1.35f, (r.face.y - r.hairTop.y) / std::max(r.faceR.y, 1.0f) + 0.15f);
}

} // namespace

/// Profundidade (fração do raio) para a paralaxe de virar/acenar.
f32 depth_at(const Rig& r, Vec2 p) noexcept {
    const Vec2 q{(p.x - r.face.x) / std::max(r.faceR.x, 1.0f), (p.y - r.face.y) / std::max(r.faceR.y, 1.0f)};
    const f32 rho2 = q.length_sq(), rho = std::sqrt(rho2);
    f32 z = 0.0f;
    if (rho < 1.0f) {
        z = 1.0f - rho2;
        const f32 inside = 1.0f - sstep(0.75f, 1.0f, rho);
        const Vec2 eyes = (r.eye[0] + r.eye[1]) * 0.5f;
        const Vec2 nose{0.5f * eyes.x + 0.5f * r.mouth.x, eyes.y + 0.6f * (r.mouth.y - eyes.y)};
        f32 bump = 0.30f * gauss(p, nose, r.faceR * 0.2f) + 0.10f * gauss(p, r.mouth, Vec2{1.2f * r.mouthR.x, 2.0f * r.mouthR.y});
        for (u32 i = 0; i < 2; ++i) bump += 0.08f * gauss(p, r.eye[i], r.eyeR[i] * 1.2f);
        z += inside * bump;
    } else {
        // Cabelo de trás: anda um pouco ao contrário (acima do queixo).
        const f32 outer = hair_reach(r) + 0.3f;
        if (rho < outer) {
            const f32 t = (rho - 1.0f) / (outer - 1.0f);
            z = -0.12f * std::sin(kPi * t) * sstep(r.neck.y, r.face.y, p.y);
        }
    }
    return z;
}

Anchors proportional_anchors(f32 x0, f32 y0, f32 x1, f32 y1) noexcept {
    const f32 bw = std::max(x1 - x0, 1e-3f), bh = std::max(y1 - y0, 1e-3f);
    const f32 cx = 0.5f * (x0 + x1);
    Anchors a;
    a.face = Vec2{cx, 0.5f * (y0 + y1)};
    a.faceR = Vec2{0.5f * bw, 0.5f * bh};
    a.eye[0] = Vec2{x0 + 0.30f * bw, y0 + 0.42f * bh};
    a.eye[1] = Vec2{x0 + 0.70f * bw, y0 + 0.42f * bh};
    a.eyeR[0] = a.eyeR[1] = Vec2{0.10f * bw, 0.065f * bh};
    a.mouth = Vec2{cx, y0 + 0.70f * bh};
    a.mouthR = Vec2{0.085f * bw, 0.025f * bh};
    a.neck = Vec2{cx, y1 + 0.03f * bh};
    a.hairTop = Vec2{cx, std::max(0.0f, y0 - 0.12f * bh)};
    return a;
}

Rig rig_of(const Anchors& a, f32 width, f32 height) noexcept {
    const f32 w = std::max(width, 1.0f), h = std::max(height, 1.0f);
    auto px = [&](Vec2 v) { return Vec2{v.x * w, v.y * h}; };
    auto size = [&](Vec2 v) { return Vec2{std::max(std::fabs(v.x) * w, 0.5f), std::max(std::fabs(v.y) * h, 0.5f)}; };
    Rig r;
    r.width = w;
    r.height = h;
    r.face = px(a.face);
    r.faceR = Vec2{std::max(std::fabs(a.faceR.x) * w, 2.0f), std::max(std::fabs(a.faceR.y) * h, 2.0f)};
    for (u32 i = 0; i < 2; ++i) { r.eye[i] = px(a.eye[i]); r.eyeR[i] = size(a.eyeR[i]); }
    r.mouth = px(a.mouth);
    r.mouthR = size(a.mouthR);
    r.neck = px(a.neck);
    r.hairTop = px(a.hairTop);
    return r;
}

bool is_neutral(const Pose& p) noexcept {
    return p.turn == 0.0f && p.nod == 0.0f && p.tilt == 0.0f && p.eyeOpen[0] >= 1.0f && p.eyeOpen[1] >= 1.0f
        && p.mouthOpen <= 0.0f && p.smile == 0.0f && p.brow[0] == 0.0f && p.brow[1] == 0.0f && p.sway == 0.0f
        && p.breath == 0.0f;
}

f32 breath_at(f32 amount, f32 perMinute, f64 seconds) noexcept {
    if (!(amount > 0.0f) || !std::isfinite(seconds)) return 0.0f;
    const f64 phase = 2.0 * 3.14159265358979323846 * static_cast<f64>(std::max(perMinute, 0.0f)) / 60.0 * seconds;
    // Começa em 0 (sobe e volta): 0.05 do raio y do rosto no máximo.
    return std::min(amount, 1.0f) * 0.05f * static_cast<f32>(0.5 - 0.5 * std::cos(phase));
}

Vec2 displace(const Rig& r, const Pose& pose, Vec2 p) noexcept {
    if (is_neutral(pose)) return p;
    // Cada passo é uma deformação sem dobra e o seguinte parte do ponto já
    // movido (composição, não soma): a composição também não dobra — somar
    // olho fechado (área zero) com o cisalhamento do giro virava triângulos.
    Vec2 q = p;
    // 1) Partes do rosto: olhos, sobrancelhas, boca.
    for (u32 i = 0; i < 2; ++i) q.y += eye_close_dy(r, i, 1.0f - saturate(pose.eyeOpen[i]), q);
    for (u32 i = 0; i < 2; ++i) q.y += brow_dy(r, i, pose.brow[i], q);
    q.y += mouth_dy(r, saturate(pose.mouthOpen), pose.smile, q);
    // 2) Virar/acenar: paralaxe na elipsoide.
    if (pose.turn != 0.0f || pose.nod != 0.0f) {
        const f32 z = depth_at(r, q);
        q.x += 0.55f * std::sin(pose.turn) * r.faceR.x * z;
        q.y += 0.55f * std::sin(pose.nod) * r.faceR.y * z;
    }
    const f32 rr = std::max(r.faceR.x, r.faceR.y);
    // 3) Inclinar: a cabeça gira em volta do queixo/pescoço.
    if (pose.tilt != 0.0f) {
        const f32 a = std::fabs(pose.tilt);
        const f32 ext = 1.5f * a * std::fabs(q.x - r.neck.x);
        const f32 wv = sstep(r.neck.y + 0.15f * r.faceR.y + 0.3f * ext, r.neck.y - 0.25f * r.faceR.y - 0.7f * ext, q.y);
        const Vec2 n{(q.x - r.face.x) / r.faceR.x, (q.y - r.face.y) / r.faceR.y};
        const f32 reach = hair_reach(r) + 0.25f;
        const f32 band = 0.5f + 1.5f * a * (q - r.neck).length() / rr;
        const f32 w = wv * (1.0f - sstep(reach, reach + band, n.length()));
        if (w > 0.0f) q = r.neck + rot(q - r.neck, pose.tilt * w);
    }
    // 4) Corpo: balanço em volta da base, depois a respiração.
    const Vec2 pivot{r.neck.x, r.height};
    auto body_weight = [&](Vec2 v, f32 b) {
        const f32 reachX = 2.6f * r.faceR.x;
        const f32 wh = 1.0f - sstep(reachX, reachX + r.faceR.x + 2.0f * b * std::max(pivot.y - v.y, 0.0f), std::fabs(v.x - r.neck.x));
        const f32 bandTop = 0.5f * r.faceR.y + 2.0f * b * std::max(pivot.y - r.hairTop.y, 0.0f);
        return wh * sstep(r.hairTop.y - bandTop, r.hairTop.y, v.y);
    };
    if (pose.sway != 0.0f) {
        const f32 w = body_weight(q, std::fabs(pose.sway));
        if (w > 0.0f) q = pivot + rot(q - pivot, pose.sway * w);
    }
    if (pose.breath != 0.0f) {
        const f32 wb = sstep(r.height, r.neck.y + 0.4f * r.faceR.y, q.y);
        q.y -= pose.breath * r.faceR.y * wb * body_weight(q, 0.0f);
    }
    if (!std::isfinite(q.x) || !std::isfinite(q.y)) return p;
    return q;
}

void grid_lines(const Rig& r, u32 uniform, std::vector<f32>& xs, std::vector<f32>& ys) {
    xs.clear();
    ys.clear();
    const u32 n = std::max(2u, uniform);
    for (u32 i = 0; i <= n; ++i) {
        xs.push_back(r.width * static_cast<f32>(i) / static_cast<f32>(n));
        ys.push_back(r.height * static_cast<f32>(i) / static_cast<f32>(n));
    }
    for (u32 i = 0; i < 2; ++i) {
        const Vec2 c = r.eye[i], s = r.eyeR[i];
        for (f32 k : {-1.35f, -1.1f, -0.85f, -0.4f, 0.0f, 0.4f, 0.85f, 1.1f, 1.35f}) xs.push_back(c.x + k * s.x);
        const EyeLines L = eye_lines(c, s);
        for (f32 y : {L.band, 0.5f * (L.band + L.top), L.top, c.y - 0.5f * s.y, c.y, L.low}) ys.push_back(y);
        Vec2 bc, br;
        brow_of(c, s, bc, br);
        for (f32 k : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) ys.push_back(bc.y + k * br.y);
    }
    const f32 mw = r.mouthR.x;
    for (f32 k : {-1.5f, -1.2f, -0.9f, -0.5f, 0.0f, 0.5f, 0.9f, 1.2f, 1.5f}) xs.push_back(r.mouth.x + k * mw);
    const MouthLines L = mouth_lines(r);
    for (f32 y : {L.lineTop - L.up, L.lineTop - 0.5f * L.up, L.lineTop, r.mouth.y, L.lineBot, 0.5f * (L.lineBot + L.chin),
                  L.chin, L.below})
        ys.push_back(y);
    xs.push_back(r.face.x);
    ys.push_back(r.face.y);
    ys.push_back(r.neck.y);
    auto finish = [](std::vector<f32>& v, f32 limit) {
        for (f32& x : v) x = std::isfinite(x) ? clampf(x, 0.0f, limit) : 0.0f;
        std::sort(v.begin(), v.end());
        std::vector<f32> out;
        out.reserve(v.size());
        for (f32 x : v)
            if (out.empty() || x - out.back() > 0.5f) out.push_back(x);
        // A borda da camada fica exata.
        if (out.back() < limit) { if (limit - out.back() <= 0.5f) out.back() = limit; else out.push_back(limit); }
        v.swap(out);
    };
    finish(xs, r.width);
    finish(ys, r.height);
}

// =============================================================================
//  Busca das âncoras
// =============================================================================
namespace {

struct Small {
    u32 w = 0, h = 0;
    std::vector<f32> r, g, b, a, lum;
    [[nodiscard]] usize at(u32 x, u32 y) const noexcept { return static_cast<usize>(y) * w + x; }
};

/// Reduz para ~200 px no lado maior (média; cor pesada pelo alfa).
Small shrink(const u8* rgba, u32 w, u32 h) {
    Small s;
    const f32 scale = std::max(1.0f, static_cast<f32>(std::max(w, h)) / 200.0f);
    s.w = std::max(1u, static_cast<u32>(std::ceil(static_cast<f32>(w) / scale)));
    s.h = std::max(1u, static_cast<u32>(std::ceil(static_cast<f32>(h) / scale)));
    const usize n = static_cast<usize>(s.w) * s.h;
    s.r.assign(n, 0.0f); s.g.assign(n, 0.0f); s.b.assign(n, 0.0f); s.a.assign(n, 0.0f); s.lum.assign(n, 0.0f);
    std::vector<f32> count(n, 0.0f);
    for (u32 y = 0; y < h; ++y) {
        const u32 sy = std::min(s.h - 1, static_cast<u32>(static_cast<f32>(y) / scale));
        for (u32 x = 0; x < w; ++x) {
            const u32 sx = std::min(s.w - 1, static_cast<u32>(static_cast<f32>(x) / scale));
            const u8* p = rgba + (static_cast<usize>(y) * w + x) * 4;
            const usize i = s.at(sx, sy);
            const f32 al = p[3] / 255.0f;
            s.r[i] += p[0] / 255.0f * al; s.g[i] += p[1] / 255.0f * al; s.b[i] += p[2] / 255.0f * al;
            s.a[i] += al;
            count[i] += 1.0f;
        }
    }
    for (usize i = 0; i < n; ++i) {
        const f32 al = s.a[i];
        if (al > 1e-4f) { s.r[i] /= al; s.g[i] /= al; s.b[i] /= al; }
        s.a[i] = count[i] > 0.0f ? al / count[i] : 0.0f;
        s.lum[i] = 0.299f * s.r[i] + 0.587f * s.g[i] + 0.114f * s.b[i];
    }
    return s;
}

bool skin_like(f32 r, f32 g, f32 b) noexcept {
    const f32 R = r * 255.0f, G = g * 255.0f, B = b * 255.0f;
    const f32 Y = 0.299f * R + 0.587f * G + 0.114f * B;
    const f32 cb = 128.0f - 0.168736f * R - 0.331264f * G + 0.5f * B;
    const f32 cr = 128.0f + 0.5f * R - 0.418688f * G - 0.081312f * B;
    if (Y > 60.0f && cr > 135.0f && cr < 180.0f && cb > 80.0f && cb < 135.0f) return true;
    // Pele clara de desenho (pêssego quase branco).
    return r >= g && g >= b && r > 0.6f && (r - b) > 0.05f && (r - b) < 0.45f && Y > 120.0f;
}

struct Blob {
    f32 n = 0.0f, sx = 0.0f, sy = 0.0f, sv = 0.0f;
    u32 x0 = ~0u, y0 = ~0u, x1 = 0, y1 = 0;
    [[nodiscard]] f32 cx() const noexcept { return sx / std::max(n, 1.0f); }
    [[nodiscard]] f32 cy() const noexcept { return sy / std::max(n, 1.0f); }
    [[nodiscard]] f32 bw() const noexcept { return static_cast<f32>(x1 - x0 + 1); }
    [[nodiscard]] f32 bh() const noexcept { return static_cast<f32>(y1 - y0 + 1); }
    [[nodiscard]] f32 mean() const noexcept { return sv / std::max(n, 1.0f); }
};

/// Manchas (4-vizinhança) de `map` ≥ `t` dentro do retângulo.
std::vector<Blob> blobs(const Small& s, const std::vector<f32>& map, f32 t, u32 x0, u32 y0, u32 x1, u32 y1) {
    std::vector<Blob> out;
    std::vector<u8> seen(map.size(), 0);
    std::queue<std::pair<u32, u32>> q;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x) {
            if (seen[s.at(x, y)] || map[s.at(x, y)] < t) continue;
            Blob b;
            q.push({x, y});
            seen[s.at(x, y)] = 1;
            while (!q.empty()) {
                const auto [cx, cy] = q.front();
                q.pop();
                const f32 v = map[s.at(cx, cy)];
                b.n += 1.0f; b.sx += static_cast<f32>(cx); b.sy += static_cast<f32>(cy); b.sv += v;
                b.x0 = std::min(b.x0, cx); b.x1 = std::max(b.x1, cx);
                b.y0 = std::min(b.y0, cy); b.y1 = std::max(b.y1, cy);
                const i32 nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& o : nb) {
                    const i32 nx = static_cast<i32>(cx) + o[0], ny = static_cast<i32>(cy) + o[1];
                    if (nx < static_cast<i32>(x0) || ny < static_cast<i32>(y0) || nx >= static_cast<i32>(x1) || ny >= static_cast<i32>(y1))
                        continue;
                    const usize k = s.at(static_cast<u32>(nx), static_cast<u32>(ny));
                    if (seen[k] || map[k] < t) continue;
                    seen[k] = 1;
                    q.push({static_cast<u32>(nx), static_cast<u32>(ny)});
                }
            }
            out.push_back(b);
        }
    return out;
}

/// Limiar: o maior entre o mínimo, o percentil `pct` e uma fração do máximo.
f32 threshold_of(const Small& s, const std::vector<f32>& map, u32 x0, u32 y0, u32 x1, u32 y1, f32 floor, f32 pct, f32 ofMax) {
    std::vector<f32> v;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x) v.push_back(map[s.at(x, y)]);
    if (v.empty()) return 1e9f;
    const f32 mx = *std::max_element(v.begin(), v.end());
    const usize k = std::min(v.size() - 1, static_cast<usize>(pct * static_cast<f32>(v.size())));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return std::max({floor, v[k], ofMax * mx});
}

/// Fração de pele no anel em volta da caixa da mancha (meia caixa para fora).
f32 skin_ring(const Small& s, const std::vector<u8>& fg, const Blob& b) {
    const i32 mx = std::max(2, static_cast<i32>(0.5f * b.bw())), my = std::max(2, static_cast<i32>(0.5f * b.bh()));
    const i32 x0 = std::max(0, static_cast<i32>(b.x0) - mx), x1 = std::min(static_cast<i32>(s.w) - 1, static_cast<i32>(b.x1) + mx);
    const i32 y0 = std::max(0, static_cast<i32>(b.y0) - my), y1 = std::min(static_cast<i32>(s.h) - 1, static_cast<i32>(b.y1) + my);
    f32 ring = 0.0f, skin = 0.0f;
    for (i32 y = y0; y <= y1; ++y)
        for (i32 x = x0; x <= x1; ++x) {
            if (x >= static_cast<i32>(b.x0) && x <= static_cast<i32>(b.x1) && y >= static_cast<i32>(b.y0) && y <= static_cast<i32>(b.y1)) continue;
            const usize i = s.at(static_cast<u32>(x), static_cast<u32>(y));
            ring += 1.0f;
            if (fg[i] && skin_like(s.r[i], s.g[i], s.b[i])) skin += 1.0f;
        }
    return ring > 0.0f ? skin / ring : 0.0f;
}

/// Fração de cor viva (íris de desenho) na caixa da mancha um pouco aumentada.
f32 vivid_fraction(const Small& s, const std::vector<u8>& fg, const Blob& b) {
    const i32 m = std::max(1, static_cast<i32>(0.25f * std::max(b.bw(), b.bh())));
    const i32 x0 = std::max(0, static_cast<i32>(b.x0) - m), x1 = std::min(static_cast<i32>(s.w) - 1, static_cast<i32>(b.x1) + m);
    const i32 y0 = std::max(0, static_cast<i32>(b.y0) - m), y1 = std::min(static_cast<i32>(s.h) - 1, static_cast<i32>(b.y1) + m);
    f32 all = 0.0f, hit = 0.0f;
    for (i32 y = y0; y <= y1; ++y)
        for (i32 x = x0; x <= x1; ++x) {
            const usize i = s.at(static_cast<u32>(x), static_cast<u32>(y));
            if (!fg[i]) continue;
            all += 1.0f;
            const f32 mx = std::max({s.r[i], s.g[i], s.b[i]}), mn = std::min({s.r[i], s.g[i], s.b[i]});
            if (mx > 0.3f && (mx - mn) / mx > 0.45f && !skin_like(s.r[i], s.g[i], s.b[i])) hit += 1.0f;
        }
    return all > 0.0f ? hit / all : 0.0f;
}

/// Meia largura e meia altura do olho em volta de (cx, cy): linhas e colunas
/// sem pele, numa caixa de ±0,45·d por ±0,55·d. Aceita até 2 linhas de pele
/// no meio (o aro dos óculos atravessa o olho). Um lado que bate na caixa
/// (cabelo, franja) vale o espelho do lado oposto.
Vec2 eye_hole(const Small& s, const std::vector<u8>& fg, f32 cx, f32 cy, f32 d) {
    const i32 bx0 = std::max(0, static_cast<i32>(cx - 0.45f * d)), bx1 = std::min(static_cast<i32>(s.w) - 1, static_cast<i32>(cx + 0.45f * d));
    const i32 by0 = std::max(0, static_cast<i32>(cy - 0.55f * d)), by1 = std::min(static_cast<i32>(s.h) - 1, static_cast<i32>(cy + 0.55f * d));
    const i32 sx = static_cast<i32>(cx), sy = static_cast<i32>(cy);
    if (sx < bx0 || sx > bx1 || sy < by0 || sy > by1) return Vec2{};
    auto open = [&](i32 x, i32 y) {
        const usize i = s.at(static_cast<u32>(x), static_cast<u32>(y));
        return fg[i] && !skin_like(s.r[i], s.g[i], s.b[i]);
    };
    const i32 cw = std::max(1, static_cast<i32>(0.25f * d));
    auto row = [&](i32 y) {
        i32 n = 0, k = 0;
        for (i32 x = std::max(bx0, sx - cw); x <= std::min(bx1, sx + cw); ++x, ++n) k += open(x, y) ? 1 : 0;
        return n ? static_cast<f32>(k) / static_cast<f32>(n) : 0.0f;
    };
    // Vai até a última linha "de olho", aceitando 2 linhas de pele no meio.
    auto walk = [&](i32 from, i32 step, i32 limit, bool& hit) {
        i32 last = from, gap = 0;
        for (i32 y = from; step > 0 ? y <= limit : y >= limit; y += step) {
            if (row(y) > 0.35f) { last = y; gap = 0; }
            else if (++gap > 2) return last;
        }
        hit = true;
        return last;
    };
    bool hitT = false, hitB = false;
    const i32 t = walk(sy, -1, by0, hitT), b = walk(sy, 1, by1, hitB);
    if (hitT && hitB) return Vec2{};   // não é um olho: é cabelo/fundo
    auto col = [&](i32 x) {
        i32 n = 0, k = 0;
        for (i32 y = t; y <= b; ++y, ++n) k += open(x, y) ? 1 : 0;
        return n ? static_cast<f32>(k) / static_cast<f32>(n) : 0.0f;
    };
    auto walkx = [&](i32 step, i32 limit, bool& hit) {
        i32 last = sx, gap = 0;
        for (i32 x = sx; step > 0 ? x <= limit : x >= limit; x += step) {
            if (col(x) > 0.3f) { last = x; gap = 0; }
            else if (++gap > 1) return last;
        }
        hit = true;
        return last;
    };
    bool hitL = false, hitR = false;
    const i32 l = walkx(-1, bx0, hitL), r = walkx(1, bx1, hitR);
    if (hitL && hitR) return Vec2{};
    f32 left = cx - static_cast<f32>(l), right = static_cast<f32>(r + 1) - cx;
    f32 up = cy - static_cast<f32>(t), down = static_cast<f32>(b + 1) - cy;
    if (hitL) left = right;
    if (hitR) right = left;
    if (hitT) up = down;
    if (hitB) down = up;
    return Vec2{0.5f * (left + right), 0.5f * (up + down)};
}

} // namespace

u32 auto_place(const u8* rgba, u32 w, u32 h, Anchors& out) {
    out = proportional_anchors(0.2f, 0.12f, 0.8f, 0.78f);
    if (!rgba || w < 8 || h < 8) return 0;
    const Small s = shrink(rgba, w, h);
    const usize n = static_cast<usize>(s.w) * s.h;
    // 1) Silhueta: alfa, se a imagem tem transparência de verdade.
    usize clear = 0;
    for (usize i = 0; i < n; ++i) clear += s.a[i] < 0.5f ? 1u : 0u;
    const bool hasAlpha = clear * 100 > n * 3;
    std::vector<u8> fg(n, 1);
    if (hasAlpha) for (usize i = 0; i < n; ++i) fg[i] = s.a[i] >= 0.5f ? 1 : 0;
    u32 fx0 = s.w, fy0 = s.h, fx1 = 0, fy1 = 0;
    for (u32 y = 0; y < s.h; ++y)
        for (u32 x = 0; x < s.w; ++x)
            if (fg[s.at(x, y)]) { fx0 = std::min(fx0, x); fx1 = std::max(fx1, x); fy0 = std::min(fy0, y); fy1 = std::max(fy1, y); }
    if (fx1 < fx0 || fy1 < fy0) return 0;
    const f32 W = static_cast<f32>(s.w), H = static_cast<f32>(s.h);
    // 2) Cabeça: com alfa, até o pescoço (a linha mais estreita antes dos ombros).
    f32 hx0 = static_cast<f32>(fx0), hx1 = static_cast<f32>(fx1 + 1), hy0 = static_cast<f32>(fy0), hy1 = static_cast<f32>(fy1 + 1);
    if (hasAlpha) {
        const u32 fh = fy1 - fy0 + 1;
        std::vector<u32> width(s.h, 0);
        for (u32 y = fy0; y <= fy1; ++y)
            for (u32 x = fx0; x <= fx1; ++x) width[y] += fg[s.at(x, y)];
        u32 widest = 0, neck = 0, neckW = ~0u;
        for (u32 y = fy0 + fh / 4; y < fy0 + fh * 4 / 5; ++y) {
            widest = std::max(widest, width[y - fh / 8]);
            if (width[y] < neckW && static_cast<f32>(width[y]) < 0.8f * static_cast<f32>(widest)) { neckW = width[y]; neck = y; }
        }
        // Precisa voltar a alargar abaixo (ombros); senão a silhueta é só a cabeça.
        bool shoulders = false;
        if (neck) for (u32 y = neck; y <= fy1 && !shoulders; ++y) shoulders = width[y] > neckW + neckW / 3 + 2;
        if (neck && shoulders) hy1 = static_cast<f32>(neck);
        else hy1 = std::min(hy1, hy0 + 1.3f * (hx1 - hx0));
        std::vector<u32> xsIn;
        for (u32 y = static_cast<u32>(hy0); y < static_cast<u32>(hy1); ++y)
            for (u32 x = fx0; x <= fx1; ++x)
                if (fg[s.at(x, y)]) xsIn.push_back(x);
        if (xsIn.size() > 16) {
            std::sort(xsIn.begin(), xsIn.end());
            hx0 = static_cast<f32>(xsIn[xsIn.size() * 3 / 100]);
            hx1 = static_cast<f32>(xsIn[xsIn.size() * 97 / 100] + 1);
        }
    }
    // 3) Rosto: a pele dentro da cabeça (percentis); senão a cabeça sem o cabelo.
    f32 rx0 = hx0 + 0.12f * (hx1 - hx0), rx1 = hx1 - 0.12f * (hx1 - hx0);
    f32 ry0 = hy0 + (hasAlpha ? 0.3f : 0.12f) * (hy1 - hy0), ry1 = hy1;
    if (!hasAlpha) { rx0 = 0.2f * W; rx1 = 0.8f * W; ry0 = 0.12f * H; ry1 = 0.78f * H; }
    {
        std::vector<u32> sxv, syv;
        const u32 by0 = static_cast<u32>(hy0), by1 = static_cast<u32>(std::min(H, hy1));
        for (u32 y = by0; y < by1; ++y)
            for (u32 x = static_cast<u32>(hx0); x < static_cast<u32>(hx1); ++x) {
                const usize i = s.at(x, y);
                if (fg[i] && skin_like(s.r[i], s.g[i], s.b[i])) { sxv.push_back(x); syv.push_back(y); }
            }
        const f32 area = std::max(1.0f, (hx1 - hx0) * (hy1 - hy0));
        if (static_cast<f32>(sxv.size()) > 0.04f * area) {
            std::sort(sxv.begin(), sxv.end());
            std::sort(syv.begin(), syv.end());
            const f32 kx0 = static_cast<f32>(sxv[sxv.size() * 4 / 100]), kx1 = static_cast<f32>(sxv[sxv.size() * 96 / 100] + 1);
            const f32 ky0 = static_cast<f32>(syv[syv.size() * 2 / 100]), ky1 = static_cast<f32>(syv[syv.size() * 98 / 100] + 1);
            if (kx1 - kx0 > 0.15f * (hx1 - hx0) && ky1 - ky0 > 0.15f * (hy1 - hy0)) {
                rx0 = kx0; rx1 = kx1; ry0 = ky0;
                // Pele do pescoço/colo continua abaixo: o rosto vai até ~1.3 da largura.
                ry1 = std::min(ky1, ky0 + 1.35f * (kx1 - kx0));
            }
        }
    }
    const f32 fw = std::max(rx1 - rx0, 4.0f), fhh = std::max(ry1 - ry0, 4.0f);
    // 4) Escuridão relativa: média local menos o pixel (integral da luminância do que é figura).
    std::vector<f64> sum((s.w + 1) * static_cast<usize>(s.h + 1), 0.0), cnt(sum.size(), 0.0);
    for (u32 y = 0; y < s.h; ++y)
        for (u32 x = 0; x < s.w; ++x) {
            const usize i = s.at(x, y), o = static_cast<usize>(y + 1) * (s.w + 1) + (x + 1);
            const f64 v = fg[i] ? s.lum[i] : 0.0, c = fg[i] ? 1.0 : 0.0;
            sum[o] = v + sum[o - 1] + sum[o - (s.w + 1)] - sum[o - (s.w + 1) - 1];
            cnt[o] = c + cnt[o - 1] + cnt[o - (s.w + 1)] - cnt[o - (s.w + 1) - 1];
        }
    const i32 R = std::max(2, static_cast<i32>(fw / 4.0f));
    std::vector<f32> dark(n, 0.0f), red(n, 0.0f);
    for (u32 y = 0; y < s.h; ++y)
        for (u32 x = 0; x < s.w; ++x) {
            const usize i = s.at(x, y);
            if (!fg[i]) continue;
            const i32 ax = std::max(0, static_cast<i32>(x) - R), ay = std::max(0, static_cast<i32>(y) - R);
            const i32 bx = std::min(static_cast<i32>(s.w), static_cast<i32>(x) + R + 1), by = std::min(static_cast<i32>(s.h), static_cast<i32>(y) + R + 1);
            auto box = [&](const std::vector<f64>& t) {
                const usize W1 = s.w + 1;
                return t[static_cast<usize>(by) * W1 + bx] - t[static_cast<usize>(ay) * W1 + bx] - t[static_cast<usize>(by) * W1 + ax]
                     + t[static_cast<usize>(ay) * W1 + ax];
            };
            const f64 c = box(cnt);
            const f32 mean = c > 0.0 ? static_cast<f32>(box(sum) / c) : s.lum[i];
            dark[i] = std::max(0.0f, mean - s.lum[i]);
            red[i] = std::max(0.0f, (s.r[i] - std::max(s.g[i], s.b[i])) - 0.2f) * 2.5f;
        }
    // 5) Olhos: o par de manchas escuras mais simétrico no alto do rosto.
    const u32 ex0 = static_cast<u32>(clampf(rx0, 0.0f, W)), ex1 = static_cast<u32>(clampf(rx1, 0.0f, W));
    const u32 ey0 = static_cast<u32>(clampf(ry0 + 0.15f * fhh, 0.0f, H)), ey1 = static_cast<u32>(clampf(ry0 + 0.62f * fhh, 0.0f, H));
    const Blob* eyeA = nullptr;
    const Blob* eyeB = nullptr;
    std::vector<Blob> eyeBlobs;
    if (ex1 > ex0 + 4 && ey1 > ey0 + 2) {
        const f32 t = threshold_of(s, dark, ex0, ey0, ex1, ey1, 0.05f, 0.90f, 0.35f);
        eyeBlobs = blobs(s, dark, t, ex0, ey0, ex1, ey1);
        const f32 minArea = std::max(3.0f, 0.03f * fw * 0.03f * fw);
        const f32 fcx = 0.5f * (rx0 + rx1);
        // 1.0.8 (anime de óculos e franja): uma mecha na lateral do rosto é escura
        // e tem o tamanho de um olho. O olho tem pele em volta (embaixo e dos
        // lados) e, no desenho, íris saturada; a mecha tem cabelo de um lado.
        std::vector<f32> around(eyeBlobs.size(), 0.0f), vivid(eyeBlobs.size(), 0.0f);
        for (usize k = 0; k < eyeBlobs.size(); ++k) {
            const Blob& b = eyeBlobs[k];
            around[k] = skin_ring(s, fg, b);
            vivid[k] = vivid_fraction(s, fg, b);
        }
        f32 best = 0.0f;
        for (usize ia = 0; ia < eyeBlobs.size(); ++ia) {
            const Blob& a = eyeBlobs[ia];
            if (a.n < minArea || a.bw() > 0.45f * fw || a.bh() > 0.4f * fhh || around[ia] < 0.2f) continue;
            for (usize ib = 0; ib < eyeBlobs.size(); ++ib) {
                const Blob& b = eyeBlobs[ib];
                if (ia == ib || b.n < minArea || b.bw() > 0.45f * fw || b.bh() > 0.4f * fhh || around[ib] < 0.2f) continue;
                const f32 dx = (b.cx() - a.cx()) / fw;
                if (dx < 0.18f || dx > 0.8f) continue;
                const f32 dy = std::fabs(a.cy() - b.cy()) / fw;
                const f32 off = std::fabs(0.5f * (a.cx() + b.cx()) - fcx) / fw;
                const f32 sim = std::min(a.n, b.n) / std::max(a.n, b.n);
                if (sim < 0.2f) continue;
                // Olho: nem fino como a sobrancelha, nem alto como uma mecha de cabelo.
                auto shape = [](const Blob& k) { const f32 r = k.bh() / k.bw(); return clampf(r / 0.35f, 0.2f, 1.0f) * clampf(2.0f / r, 0.2f, 1.0f); };
                const f32 fa = shape(a), fb = shape(b);
                const f32 low = 1.0f + 0.5f * (0.5f * (a.cy() + b.cy()) - static_cast<f32>(ey0)) / std::max(1.0f, static_cast<f32>(ey1 - ey0));
                const f32 score = std::pow(sim, 0.7f) * std::exp(-(dy / 0.07f) * (dy / 0.07f)) * std::exp(-(off / 0.15f) * (off / 0.15f))
                                * fa * fb * (a.mean() + b.mean()) * std::sqrt(a.n + b.n) * low
                                * sstep(0.2f, 0.55f, std::min(around[ia], around[ib]))
                                * (1.0f + 2.0f * std::min(vivid[ia], vivid[ib]));
                if (score > best) { best = score; eyeA = &a; eyeB = &b; }
            }
        }
    }
    if (!eyeA || !eyeB) {
        out = proportional_anchors(rx0 / W, ry0 / H, rx1 / W, ry1 / H);
        if (hasAlpha) out.hairTop = Vec2{out.face.x, hy0 / H};
        return 0;
    }
    // O limiar alto pega só o miolo: a mancha cresce com metade dele (o olho inteiro).
    Blob grown[2] = {*eyeA, *eyeB};
    {
        const f32 t = threshold_of(s, dark, ex0, ey0, ex1, ey1, 0.05f, 0.90f, 0.35f);
        const f32 reach = 0.3f * fw;
        for (Blob& g : grown) {
            const u32 gx0 = static_cast<u32>(clampf(g.cx() - reach, 0.0f, W)), gx1 = static_cast<u32>(clampf(g.cx() + reach, 0.0f, W));
            const u32 gy0 = static_cast<u32>(clampf(g.cy() - reach, 0.0f, H)), gy1 = static_cast<u32>(clampf(g.cy() + reach, 0.0f, H));
            const u32 cx = static_cast<u32>(g.cx()), cy = static_cast<u32>(g.cy());
            for (const Blob& b : blobs(s, dark, 0.45f * t, gx0, gy0, gx1, gy1))
                if (b.x0 <= cx && cx <= b.x1 && b.y0 <= cy && cy <= b.y1 && b.n >= g.n && b.bw() <= 0.5f * fw && b.bh() <= 0.45f * fhh
                    && skin_ring(s, fg, b) >= 0.2f   // crescer até a franja não vale
                    && b.bh() <= 1.8f * b.bw() && std::fabs(b.cx() - g.cx()) <= 0.35f * b.bw()) {
                    g = b;
                    break;
                }
        }
        eyeA = &grown[0];
        eyeB = &grown[1];
    }
    const f32 d = eyeB->cx() - eyeA->cx();
    const f32 eyeY = 0.5f * (eyeA->cy() + eyeB->cy());
    const f32 mid = 0.5f * (eyeA->cx() + eyeB->cx());
    // 6) Boca: mancha escura/vermelha, horizontal, abaixo do meio dos olhos.
    u32 quality = 1;
    Vec2 mouth{mid, eyeY + 0.95f * d}, mouthR{0.2f * d, 0.06f * d};
    {
        std::vector<f32> m(n, 0.0f);
        for (usize i = 0; i < n; ++i) m[i] = std::max(dark[i], 0.8f * red[i]);
        const u32 mx0 = static_cast<u32>(clampf(mid - 0.45f * d, 0.0f, W)), mx1 = static_cast<u32>(clampf(mid + 0.45f * d, 0.0f, W));
        const u32 my0 = static_cast<u32>(clampf(eyeY + 0.45f * d, 0.0f, H)), my1 = static_cast<u32>(clampf(eyeY + 1.35f * d, 0.0f, H));
        if (mx1 > mx0 + 2 && my1 > my0 + 2) {
            const f32 t = threshold_of(s, m, mx0, my0, mx1, my1, 0.05f, 0.90f, 0.4f);
            const std::vector<Blob> found = blobs(s, m, t, mx0, my0, mx1, my1);
            f32 best = 0.0f;
            const Blob* pick = nullptr;
            for (const Blob& b : found) {
                if (b.bw() < 2.0f || b.bw() > 0.9f * d) continue;
                const f32 elong = clampf(b.bw() / b.bh(), 0.5f, 4.0f);
                const f32 off = (b.cx() - mid) / (0.2f * d);
                const f32 score = std::sqrt(b.n) * elong * std::exp(-off * off) * b.mean();
                if (score > best) { best = score; pick = &b; }
            }
            if (pick) {
                quality = 2;
                mouth = Vec2{pick->cx(), pick->cy()};
                mouthR = Vec2{std::max(0.55f * pick->bw(), 0.12f * d), std::max(0.55f * pick->bh(), 0.035f * d)};
            }
        }
    }
    // 7) Montagem pelas proporções do rosto a partir dos olhos e da boca.
    const f32 dd = std::max(mouth.y - eyeY, 0.3f * d);
    const f32 top = eyeY - 1.2f * dd, chin = mouth.y + 0.55f * dd;
    f32 rxp = 1.15f * d;
    if (fw * 0.5f > 0.9f * d && fw * 0.5f < 1.6f * d) rxp = 0.5f * fw;
    Anchors a;
    a.face = Vec2{mid / W, 0.5f * (top + chin) / H};
    a.faceR = Vec2{rxp / W, 0.5f * (chin - top) / H};
    const Blob* eyes[2] = {eyeA, eyeB};
    for (u32 i = 0; i < 2; ++i) {
        a.eye[i] = Vec2{eyes[i]->cx() / W, eyes[i]->cy() / H};
        // Olho de verdade: ~metade da distância entre os olhos de largura.
        f32 hw = std::max(0.6f * eyes[i]->bw(), 0.22f * d), hh = std::max(0.6f * eyes[i]->bh(), 0.12f * d);
        // O buraco na pele em volta do centro (olho grande de desenho: íris,
        // branco e cílios). Lado que encosta no limite (franja) vale o espelho.
        const Vec2 hole = eye_hole(s, fg, eyes[i]->cx(), eyes[i]->cy(), d);
        hw = clampf(std::max(hw, hole.x), 0.0f, 0.45f * d);
        hh = clampf(std::max(hh, 1.15f * hole.y), 0.0f, 0.6f * d);   // + os cílios de cima
        a.eyeR[i] = Vec2{hw / W, hh / H};
    }
    a.mouth = Vec2{mouth.x / W, mouth.y / H};
    a.mouthR = Vec2{mouthR.x / W, mouthR.y / H};
    a.neck = Vec2{mid / W, std::min(1.0f, (chin + 0.12f * 0.5f * (chin - top)) / H)};
    a.hairTop = Vec2{mid / W, hasAlpha ? hy0 / H : std::max(0.0f, (top - 0.35f * 0.5f * (chin - top)) / H)};
    out = a;
    return quality;
}

} // namespace aurea::face_rig
