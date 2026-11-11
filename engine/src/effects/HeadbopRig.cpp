// =============================================================================
//  Aurea / effects / HeadbopRig.cpp
//
//  Rig e deformação do Headbop (effects/Headbop.hpp): pontos de corpo →
//  cabeça/pescoço/ombros, pesos analíticos (o MESMO cálculo de
//  shaders/effects/headbop.vert), uniforms do passe e a trilha por quadro no
//  cache do motor.
// =============================================================================
#include "aurea/effects/Headbop.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace aurea::headbop {
namespace {

constexpr f32 kPi = 3.14159265358979f;
constexpr f32 kDeg = kPi / 180.0f;

f32 smooth(f32 a, f32 b, f32 x) noexcept {
    if (a == b) return x < a ? 0.0f : 1.0f;
    const f32 t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
f32 len(Vec2 v) noexcept { return std::sqrt(v.x * v.x + v.y * v.y); }
f32 dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
Vec2 rot(Vec2 v, f32 a) noexcept {
    const f32 c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}
f32 fin(f32 v, f32 def = 0.0f) noexcept { return std::isfinite(v) ? v : def; }
Vec2 lerp(Vec2 a, Vec2 b, f32 t) noexcept { return a + (b - a) * t; }

std::atomic<bool> gLowEnd{false};

} // namespace

// ---- Canais -------------------------------------------------------------------

std::array<f32, kPoseChannels> pose_channels(const Pose& p) noexcept {
    return {p.frameDy, p.frameZoom, p.torsoDx, p.torsoDy, p.torsoRoll, p.headDx,
            p.headDy, p.headPitch, p.headRoll, p.headYaw, p.extremityLag, p.frameDx,
            p.hairDx, p.hairDy, p.hairRoll, p.hairPitch, p.clothDx, p.clothDy, p.clothRoll,
            p.flutterX, p.flutterY, p.wavePhase, p.waveAmp};
}

Pose pose_from_channels(const f32* c) noexcept {
    Pose p;
    if (!c) return p;
    p.frameDy = fin(c[0]); p.frameZoom = fin(c[1]); p.torsoDx = fin(c[2]); p.torsoDy = fin(c[3]);
    p.torsoRoll = fin(c[4]); p.headDx = fin(c[5]); p.headDy = fin(c[6]); p.headPitch = fin(c[7]);
    p.headRoll = fin(c[8]); p.headYaw = fin(c[9]); p.extremityLag = fin(c[10]); p.frameDx = fin(c[11]);
    p.hairDx = fin(c[12]); p.hairDy = fin(c[13]); p.hairRoll = fin(c[14]); p.hairPitch = fin(c[15]);
    p.clothDx = fin(c[16]); p.clothDy = fin(c[17]); p.clothRoll = fin(c[18]);
    p.flutterX = fin(c[19]); p.flutterY = fin(c[20]); p.wavePhase = fin(c[21]); p.waveAmp = fin(c[22]);
    return p;
}

// ---- Rig a partir dos pontos -----------------------------------------------------

bool face_from_landmarks(const f32* pts, f32 aspect, RigFace& out, f32 minConf) noexcept {
    if (!pts || !(aspect > 0.0f) || !std::isfinite(aspect)) return false;
    // Espaço isotrópico: unidade = altura da imagem.
    auto P = [&](u32 i) { return Vec2{fin(pts[i * 4]) * aspect, fin(pts[i * 4 + 1])}; };
    auto C = [&](u32 i) { const f32 c = pts[i * 4 + 3]; return std::isfinite(c) ? c : 0.0f; };
    auto ok = [&](u32 i) { return C(i) >= minConf; };
    // Olhos: o centro; sem ele, a média dos cantos.
    auto eye = [&](u32 mid, u32 inner, u32 outer, Vec2& e, f32& c) {
        if (ok(mid)) { e = P(mid); c = C(mid); return true; }
        if (ok(inner) && ok(outer)) { e = (P(inner) + P(outer)) * 0.5f; c = std::min(C(inner), C(outer)); return true; }
        return false;
    };
    Vec2 a, b;
    f32 ca = 0, cb = 0;
    if (!eye(2, 1, 3, a, ca) || !eye(5, 4, 6, b, cb)) return false;
    if (a.x > b.x) std::swap(a, b);
    const f32 d = len(b - a);
    if (!(d > 1e-4f)) return false;
    const Vec2 ex = (b - a) / d;                 // ao longo dos olhos, para a direita da imagem
    const Vec2 down{-ex.y, ex.x};                // perpendicular, para baixo (y para baixo)
    const Vec2 E = (a + b) * 0.5f;
    const f32 roll = std::atan2(ex.y, ex.x);
    // Largura da cabeça pelas orelhas (projetada na linha dos olhos).
    const bool ears = ok(7) && ok(8);
    Vec2 M = E;
    f32 w = 2.3f * d;
    if (ears) {
        M = (P(7) + P(8)) * 0.5f;
        w = std::max(1.6f * d, std::fabs(dot(P(8) - P(7), ex)));
        w = std::min(w, 3.6f * d);
    }
    const f32 rx = 0.5f * w * 1.08f, ry = rx * 1.3f;
    // Giro: o nariz em relação ao meio das orelhas (ou dos olhos).
    f32 yaw = 0.0f;
    if (ok(0)) {
        const f32 s = dot(P(0) - M, ex) / std::max(1e-5f, 0.5f * w);
        yaw = std::asin(std::clamp(s / 0.75f, -1.0f, 1.0f)) / kDeg;
        yaw = std::clamp(yaw, -60.0f, 60.0f);
    }
    // Inclinação (aproximada): nariz→olhos sobre boca→olhos.
    f32 pitch = 0.0f;
    if (ok(0) && ok(9) && ok(10)) {
        const Vec2 mouth = (P(9) + P(10)) * 0.5f;
        const f32 dn = dot(P(0) - E, down), dm = dot(mouth - E, down);
        if (dm > 0.2f * d) pitch = std::clamp((dn / dm - 0.55f) * 110.0f, -35.0f, 35.0f);
    }
    // Centro da elipse: o meio das orelhas na horizontal da cabeça, um pouco acima dos olhos.
    const Vec2 base = E + ex * dot(M - E, ex);
    const Vec2 center = base - down * (0.1f * ry);
    const Vec2 chin = center + down * ry;
    Vec2 sa, sb;
    if (ok(11) && ok(12)) {
        sa = P(11); sb = P(12);
        if (sa.x > sb.x) std::swap(sa, sb);
        // Ombro estreito demais (pessoa de lado): pelo menos a cabeça.
        const f32 half = 0.5f * len(sb - sa);
        if (half < rx * 1.1f) {
            const Vec2 mid = (sa + sb) * 0.5f;
            sa = mid - Vec2{rx * 1.1f, 0.0f};
            sb = mid + Vec2{rx * 1.1f, 0.0f};
        }
    } else {
        const Vec2 mid = center + down * (ry * 2.0f);
        sa = mid - Vec2{rx * 2.1f, 0.0f};
        sb = mid + Vec2{rx * 2.1f, 0.0f};
    }
    const Vec2 smid = (sa + sb) * 0.5f;
    const Vec2 neck = lerp(chin, smid, 0.25f);
    auto frac = [&](Vec2 v) { return Vec2{v.x / aspect, v.y}; };
    out = RigFace{};
    out.center = frac(center);
    out.axes = Vec2{rx / aspect, ry};
    out.roll = roll / kDeg;
    out.yaw = yaw;
    out.pitch = pitch;
    out.depth = 0.85f;
    out.neck = frac(neck);
    out.shoulderA = frac(sa);
    out.shoulderB = frac(sb);
    out.confidence = std::min(ca, cb);
    return std::isfinite(out.center.x) && std::isfinite(out.center.y) && out.axes.x > 0.0f && out.axes.y > 0.0f;
}

void frame_from_people(const std::vector<RigFace>& people, RigFrame& out) noexcept {
    std::vector<RigFace> kept;
    // Mais confiáveis primeiro; o mesmo rosto pelos dois detectores vira um só.
    std::vector<RigFace> sorted = people;
    std::stable_sort(sorted.begin(), sorted.end(), [](const RigFace& a, const RigFace& b) { return a.confidence > b.confidence; });
    for (const RigFace& f : sorted) {
        bool dup = false;
        for (const RigFace& k : kept) {
            const f32 r = std::max(k.axes.x, f.axes.x);
            if (std::fabs(k.center.x - f.center.x) < r && std::fabs(k.center.y - f.center.y) < std::max(k.axes.y, f.axes.y)) {
                dup = true;
                break;
            }
        }
        if (!dup && kept.size() < kMaxFaces) kept.push_back(f);
    }
    std::sort(kept.begin(), kept.end(), [](const RigFace& a, const RigFace& b) { return a.center.x < b.center.x; });
    out.count = static_cast<u32>(kept.size());
    for (u32 i = 0; i < kMaxFaces; ++i) out.faces[i] = i < kept.size() ? kept[i] : RigFace{};
}

namespace {

constexpr u32 kFaceFloats = 16;

void face_to(const RigFace& f, f32* o) noexcept {
    const f32 v[kFaceFloats] = {f.center.x, f.center.y, f.axes.x, f.axes.y, f.roll, f.yaw, f.pitch, f.depth,
                                f.neck.x, f.neck.y, f.shoulderA.x, f.shoulderA.y, f.shoulderB.x, f.shoulderB.y,
                                f.confidence, 0.0f};
    std::memcpy(o, v, sizeof v);
}
RigFace face_from(const f32* o) noexcept {
    RigFace f;
    f.center = {o[0], o[1]}; f.axes = {o[2], o[3]}; f.roll = o[4]; f.yaw = o[5]; f.pitch = o[6]; f.depth = o[7];
    f.neck = {o[8], o[9]}; f.shoulderA = {o[10], o[11]}; f.shoulderB = {o[12], o[13]}; f.confidence = o[14];
    return f;
}

} // namespace

RigFrame sample_track(const RigTrack& track, f64 seconds) noexcept {
    const auto& fr = track.frames;
    if (fr.empty()) return RigFrame{};
    if (!std::isfinite(seconds) || seconds <= fr.front().time) return fr.front();
    if (seconds >= fr.back().time) return fr.back();
    const auto it = std::upper_bound(fr.begin(), fr.end(), seconds, [](f64 t, const RigFrame& f) { return t < f.time; });
    const RigFrame& b = *it;
    const RigFrame& a = *(it - 1);
    const f64 span = b.time - a.time;
    const f32 t = span > 1e-9 ? static_cast<f32>((seconds - a.time) / span) : 0.0f;
    if (a.count != b.count) return t < 0.5f ? a : b;
    RigFrame out = a;
    out.time = seconds;
    for (u32 i = 0; i < a.count; ++i) {
        f32 va[kFaceFloats], vb[kFaceFloats], vo[kFaceFloats];
        face_to(a.faces[i], va);
        face_to(b.faces[i], vb);
        for (u32 k = 0; k < kFaceFloats; ++k) vo[k] = va[k] + (vb[k] - va[k]) * t;
        out.faces[i] = face_from(vo);
    }
    return out;
}

RigFrame median_frame(const RigTrack& track) noexcept {
    RigFrame out;
    if (track.frames.empty()) return out;
    u32 hist[kMaxFaces + 1]{};
    for (const RigFrame& f : track.frames) ++hist[std::min(f.count, kMaxFaces)];
    u32 best = 0;
    for (u32 c = 1; c <= kMaxFaces; ++c) if (hist[c] > 0 && hist[c] >= hist[best]) best = c;
    out.count = best;
    std::vector<f32> col;
    for (u32 i = 0; i < best; ++i) {
        f32 res[kFaceFloats]{};
        for (u32 k = 0; k < kFaceFloats; ++k) {
            col.clear();
            for (const RigFrame& f : track.frames) {
                if (f.count != best) continue;
                f32 v[kFaceFloats];
                face_to(f.faces[i], v);
                col.push_back(v[k]);
            }
            if (col.empty()) continue;
            std::nth_element(col.begin(), col.begin() + col.size() / 2, col.end());
            res[k] = col[col.size() / 2];
        }
        out.faces[i] = face_from(res);
    }
    return out;
}

void smooth_track(RigTrack& track) noexcept {
    auto& fr = track.frames;
    if (fr.size() < 3) return;
    const std::vector<RigFrame> src = fr;
    constexpr i32 kHalf = 2;
    constexpr f32 kW[5] = {1, 2, 3, 2, 1};
    for (usize i = 0; i < src.size(); ++i) {
        for (u32 f = 0; f < src[i].count; ++f) {
            f32 acc[kFaceFloats]{};
            f32 total = 0.0f;
            for (i32 o = -kHalf; o <= kHalf; ++o) {
                const i64 j = static_cast<i64>(i) + o;
                if (j < 0 || j >= static_cast<i64>(src.size()) || src[j].count != src[i].count) continue;
                f32 v[kFaceFloats];
                face_to(src[j].faces[f], v);
                const f32 w = kW[o + kHalf];
                for (u32 k = 0; k < kFaceFloats; ++k) acc[k] += v[k] * w;
                total += w;
            }
            if (total <= 0.0f) continue;
            for (f32& v : acc) v /= total;
            fr[i].faces[f] = face_from(acc);
        }
    }
}

// ---- Pesos e deformação ------------------------------------------------------------

Weights weights(const Uniforms& u, u32 fi, Vec2 p) noexcept {
    Weights w;
    if (fi >= kMaxFaces) return w;
    const GpuFace& F = u.face[fi];
    const f32 W = u.layer.x, H = u.layer.y;
    const f32 m = u.window.w;   // margem: com o recorte a grade passa da caixa e a borda da camada também mexe
    const f32 edge = std::min(std::min(p.x + m, W + m - p.x), std::min(p.y + m, H + m - p.y));
    const f32 win = u.window.x > 0.0f ? smooth(0.0f, u.window.x, edge) : 1.0f;
    if (u.mode.y > 0.5f) {   // quadro inteiro
        w.torso = win;
        return w;
    }
    const Vec2 c{F.head.x, F.head.y};
    const f32 rx = std::max(1.0f, F.head.z), ry = std::max(1.0f, F.head.w);
    const Vec2 d = p - c;
    const Vec2 q{F.axis.x * d.x + F.axis.y * d.y, -F.axis.y * d.x + F.axis.x * d.y};
    const f32 e = len(Vec2{q.x / rx, q.y / ry});
    const f32 fall = F.axis.w;
    const f32 head = 1.0f - smooth(1.0f, 1.0f + fall, e);
    const f32 headExt = 1.0f - smooth(1.0f, 1.0f + fall * 1.6f, e);
    const f32 half = std::max(1.0f, F.body.x * 1.15f), tl = std::max(1.0f, F.body.y);
    const f32 hor = 1.0f - smooth(0.7f, 1.7f, std::fabs(p.x - F.neck.z) / half);
    const f32 vert = 1.0f - smooth(0.15f, 1.0f, (p.y - F.neck.w) / tl);
    const f32 top = smooth(c.y - ry * 3.4f, c.y - ry * 1.2f, p.y);
    w.head = head * win;
    w.torso = std::max(hor * vert * top, headExt) * win;
    return w;
}

Looseness looseness(const Uniforms& u, u32 fi, Vec2 p, f32 mask) noexcept {
    Looseness l;
    const bool hairOn = u.window.y > 0.5f, clothOn = u.window.z > 0.5f;
    if (fi >= kMaxFaces || u.mode.y > 0.5f || (!hairOn && !clothOn) || !(mask > 0.0f)) return l;
    const GpuFace& F = u.face[fi];
    const f32 W = u.layer.x, H = u.layer.y;
    const f32 m = u.window.w;   // margem: com o recorte a grade passa da caixa e a borda da camada também mexe
    const f32 edge = std::min(std::min(p.x + m, W + m - p.x), std::min(p.y + m, H + m - p.y));
    const f32 win = u.window.x > 0.0f ? smooth(0.0f, u.window.x, edge) : 1.0f;
    const Vec2 c{F.head.x, F.head.y};
    const f32 rx = std::max(1.0f, F.head.z), ry = std::max(1.0f, F.head.w);
    const Vec2 d = p - c;
    const Vec2 q{F.axis.x * d.x + F.axis.y * d.y, -F.axis.y * d.x + F.axis.x * d.y};
    const f32 e = len(Vec2{q.x / rx, q.y / ry});
    if (hairOn) {
        // Cabelo: acima da linha dos olhos (testa até o topo) e dos lados até o
        // queixo; fora do miolo do rosto; até ~1,5 altura de cabeça acima do centro.
        const f32 up = smooth(0.5f * ry, -0.5f * ry, q.y);
        const f32 side = smooth(0.62f, 1.0f, std::fabs(q.x) / rx) * (1.0f - smooth(1.2f, 2.8f, std::fabs(q.x) / rx)) *
                         smooth(1.4f * ry, 0.35f * ry, q.y);
        const f32 region = std::max(up, side);
        const f32 core = 1.0f - smooth(0.62f, 0.9f, e);
        // Volume do cabelo: até ~1,5 altura de cabeça acima do centro, mais estreito dos lados; borda larga e suave.
        const f32 eh = len(Vec2{q.x / (1.15f * rx), q.y / ry});
        const f32 ext = 1.0f - smooth(1.5f, 3.2f, eh);
        l.hairRegion = region * (1.0f - core) * ext * mask * win;
        // Pontas (longe do couro) mais soltas.
        l.hair = l.hairRegion * smooth(0.75f, 1.7f, e);
    }
    if (clothOn) {
        const f32 half = std::max(1.0f, F.body.x), tl = std::max(1.0f, F.body.y);
        const f32 v = (p.y - F.neck.w) / tl;
        const f32 below = smooth(-0.02f, 0.22f, v);
        const f32 hem = smooth(0.05f, 0.85f, v);
        const f32 sideX = smooth(0.35f, 1.15f, std::fabs(p.x - F.neck.z) / half);
        const f32 headFree = smooth(1.0f, 1.6f, e);
        l.cloth = below * std::clamp(0.2f + 0.8f * std::max(hem, sideX), 0.0f, 1.0f) * headFree * mask * win;
    }
    return l;
}

Vec2 deform_point(const Uniforms& u, Vec2 p, bool previous, f32 mask) noexcept {
    Vec2 sum = p;
    const bool whole = u.mode.y > 0.5f;
    const u32 n = whole ? 1u : std::min<u32>(kMaxFaces, static_cast<u32>(u.mode.x + 0.5f));
    for (u32 fi = 0; fi < n; ++fi) {
        const GpuFace& F = u.face[fi];
        const Weights w = weights(u, fi, p);
        const Looseness l = looseness(u, fi, p, mask);
        const f32 wh = std::max(w.head, l.hairRegion);
        if (w.torso <= 0.0f && wh <= 0.0f && l.cloth <= 0.0f) continue;
        const Vec4 T = previous ? F.prevTorso : F.torso;
        const Vec2 hip{F.neck.z, F.neck.w + F.body.y};
        auto torsoOf = [&](Vec2 x, const Vec4& t) { return hip + rot(x - hip, t.z) + Vec2{t.x, t.y}; };
        auto torso = [&](Vec2 x) { return torsoOf(x, T); };
        const Vec2 tp = torso(p);
        Vec2 q1 = p + (tp - p) * w.torso;
        if (l.cloth > 0.0f) {
            const Vec4 C = previous ? F.prevClothT : F.clothT;
            const Vec4 FL = previous ? F.prevFlutter : F.flutter;
            const Vec2 cp = torsoOf(p, Vec4{T.x + C.x, T.y + C.y, T.z + C.z, 0.0f});
            q1 += (cp - tp) * l.cloth;
            const f32 v = (p.y - F.neck.w) / std::max(1.0f, F.body.y);
            q1.x += l.cloth * C.w * std::sin(15.7f * v - FL.z);
        }
        Vec2 q2 = q1;
        if (!whole && wh > 0.0f) {
            Vec4 HT = previous ? F.prevHeadT : F.headT;
            const f32 baseRoll = HT.z;
            const Vec4 HR = previous ? F.prevHeadR : F.headR;
            const Vec4 HD = previous ? F.prevHairT : F.hairT;
            HT = Vec4{HT.x + l.hair * HD.x, HT.y + l.hair * HD.y, HT.z + l.hair * HD.z, HT.w + l.hair * HD.w};
            const Vec2 c{F.head.x, F.head.y};
            const f32 rx = std::max(1.0f, F.head.z), ry = std::max(1.0f, F.head.w);
            const Vec2 d = p - c;
            const Vec2 q{F.axis.x * d.x + F.axis.y * d.y, -F.axis.y * d.x + F.axis.x * d.y};
            const f32 e2 = (q.x / rx) * (q.x / rx) + (q.y / ry) * (q.y / ry);
            const f32 z = F.axis.z * std::max(0.0f, 1.0f - e2);   // paraboloide: inclinação finita na borda (sem dobra)
            const Vec2 n1 = torso(Vec2{F.neck.x, F.neck.y});
            const Vec2 c1 = torso(c);
            const Vec2 ax = rot(Vec2{F.axis.x, F.axis.y}, T.z);
            const Vec2 ay{-ax.y, ax.x};
            const f32 xh = dot(q1 - c1, ax);
            const f32 dx = xh * std::cos(HR.x) + z * std::sin(HR.x) - xh;
            const f32 yh = dot(q1 - n1, ay);
            const f32 dy = yh * std::cos(HT.w) + z * std::sin(HT.w) - yh;
            const Vec2 local = (q1 - n1) + ax * dx + ay * dy;
            Vec2 h = n1 + rot(local, HT.z) + Vec2{HT.x, HT.y};
            if (u.window.y > 0.5f && l.hairRegion > 0.0f && e2 > 1.0f) {
                // Fora do crânio o cabelo PENDE da raiz: a raiz segue a cabeça e o fio
                // gira só metade do aceno (+ o atraso dele) — sem alavanca longa, sem dobra.
                const f32 e = std::sqrt(e2);
                const Vec2 root = c + d / e;
                const Vec2 r1 = torso(root);
                const f32 xr = dot(r1 - c1, ax), yr = dot(r1 - n1, ay);
                const Vec2 rl = (r1 - n1) + ax * (xr * std::cos(HR.x) - xr) + ay * (yr * std::cos(HT.w) - yr);
                const Vec2 hr = n1 + rot(rl, HT.z) + Vec2{HT.x, HT.y};
                const Vec2 hh = hr + rot(q1 - r1, 0.5f * baseRoll + l.hair * HD.z);
                h = h + (hh - h) * smooth(1.0f, 1.35f, e);
            }
            if (l.hair > 0.0f) {
                const Vec4 FL = previous ? F.prevFlutter : F.flutter;
                const f32 wob = 0.6f + 0.4f * std::sin(1.1f * q.x / rx + 0.7f * q.y / ry + FL.z);
                h += Vec2{FL.x, FL.y} * (l.hair * wob);
            }
            q2 = q1 + (h - q1) * wh;
        }
        sum += q2 - p;
    }
    return sum;
}

Vec2 final_point(const Uniforms& u, Vec2 p, bool previous, f32 mask) noexcept {
    const Vec2 q = deform_point(u, p, previous, mask);
    const f32 z = previous ? u.frame.z : u.frame.x;
    const f32 dy = previous ? u.frame.w : u.frame.y;
    const f32 dx = previous ? u.frameShift.y : u.frameShift.x;
    const Vec2 c{u.layer.x * 0.5f, u.layer.y * 0.5f};
    return c + (q - c) * z + Vec2{dx, dy};
}

namespace {

/// Linhas da grade mais densas perto das cabeças (a cabeça pequena ainda tem células).
void grid_lines(f32 size, u32 count, const std::vector<std::pair<f32, f32>>& bumps, Vec4* out) noexcept {
    constexpr u32 kSamples = 512;
    f32 cdf[kSamples + 1];
    cdf[0] = 0.0f;
    for (u32 i = 0; i < kSamples; ++i) {
        const f32 x = (static_cast<f32>(i) + 0.5f) / kSamples * size;
        f32 g = 1.0f;
        for (const auto& [c, s] : bumps) {
            const f32 t = (x - c) / std::max(1.0f, s);
            g += 2.5f * std::exp(-t * t);
        }
        cdf[i + 1] = cdf[i] + g;
    }
    f32* v = reinterpret_cast<f32*>(out);
    u32 j = 0;
    for (u32 k = 0; k < count; ++k) {
        const f32 target = cdf[kSamples] * static_cast<f32>(k) / static_cast<f32>(count - 1);
        while (j < kSamples && cdf[j + 1] < target) ++j;
        const f32 seg = cdf[std::min(j + 1, kSamples)] - cdf[j];
        const f32 frac = seg > 0.0f ? std::clamp((target - cdf[j]) / seg, 0.0f, 1.0f) : 0.0f;
        v[k] = (static_cast<f32>(j) + frac) / kSamples * size;
    }
    v[0] = 0.0f;
    v[count - 1] = size;
}

} // namespace

void person_offset(u32 seed, u32 person, f64& phase, f32& gain) noexcept {
    phase = 0.0;
    gain = 1.0f;
    if (person == 0) return;
    u64 h = 0x9E3779B97F4A7C15ull ^ (static_cast<u64>(seed) << 8) ^ person;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull; h ^= h >> 33;
    const f64 a = static_cast<f64>(h & 0xFFFFFF) / 16777215.0;
    const f64 b = static_cast<f64>((h >> 24) & 0xFFFFFF) / 16777215.0;
    phase = (a - 0.5) * 0.4;                       // ±0,2 batida
    gain = static_cast<f32>(0.85 + 0.3 * b);       // 85..115 %
}

Params motion_params(const f32* v, u32 count) noexcept {
    Params p;
    if (!v || count < kSeed + 1) return p;
    auto pct = [&](u32 i, f32 def) { return std::isfinite(v[i]) ? v[i] * 0.01f : def; };
    const u32 preset = v[kPreset] <= 0.0f ? 0u : static_cast<u32>(v[kPreset] + 0.5f);
    p.preset = static_cast<Preset>(std::min<u32>(preset, static_cast<u32>(Preset::Count) - 1));
    p.intensity = std::max(0.0f, pct(kIntensity, 1.0f));
    p.head = std::max(0.0f, pct(kHead, 1.0f));
    p.body = std::max(0.0f, pct(kBody, 1.0f));
    p.jolt = std::max(0.0f, pct(kJolt, 1.0f));
    p.sway = std::max(0.0f, pct(kSway, 1.0f));
    p.turn3d = std::clamp(pct(kTurn3d, 0.0f), 0.0f, 1.0f);
    p.delay = std::max(0.0f, pct(kDelay, 1.0f));
    p.smoothness = std::max(0.05f, pct(kSmoothness, 1.0f));
    p.variation = std::max(0.0f, pct(kVariation, 1.0f));
    p.phaseBeats = std::isfinite(v[kPhase]) ? v[kPhase] : 0.0;
    const u32 sub = v[kSubdivision] <= 0.0f ? 0u : static_cast<u32>(v[kSubdivision] + 0.5f);
    p.subdivision = sub == 0 ? 0.5f : sub == 2 ? 2.0f : 1.0f;
    p.beatZoom = std::max(0.0f, pct(kBeatZoom, 0.0f));
    const u32 src = v[kBeatSource] <= 0.0f ? 0u : static_cast<u32>(v[kBeatSource] + 0.5f);
    p.manualBpm = src == static_cast<u32>(BeatSource::Manual) && std::isfinite(v[kBpm]) ? std::clamp<f64>(v[kBpm], 20.0, 400.0) : 0.0;
    p.seed = std::isfinite(v[kSeed]) ? static_cast<u32>(std::max(0.0f, v[kSeed])) : 1u;
    return p;
}

void apply_secondary(Params& p, f32 hairPct, f32 clothPct, f32 physicsPct) noexcept {
    p.hair = std::clamp(fin(hairPct) * 0.01f, 0.0f, 2.0f);
    p.cloth = std::clamp(fin(clothPct) * 0.01f, 0.0f, 2.0f);
    p.physics = std::clamp(fin(physicsPct, 100.0f) * 0.01f, 0.0f, 2.0f);
}

void fill_uniforms(const BuildInput& in, Uniforms& u) noexcept {
    u = Uniforms{};
    const f32 W = std::max(1.0f, in.width), H = std::max(1.0f, in.height);
    const bool portrait = H >= W;
    // Cabelo/roupa só com a máscara (sem ela o fundo em volta balançaria junto).
    const bool loose = (in.hair || in.cloth) && in.mask && in.rig.count > 0;
    const u32 ga = loose ? kGridXLoose : kGridX, gb = loose ? kGridYLoose : kGridY;
    const u32 gx = portrait ? ga : gb, gy = portrait ? gb : ga;
    u.layer = {W, H, static_cast<f32>(gx), static_cast<f32>(gy)};
    const u32 n = std::min(in.rig.count, kMaxFaces);
    const bool whole = n == 0;
    u32 bits = 0;
    if (in.mask && !whole) bits |= 1u;
    if (in.plate && !whole && in.mask) bits |= 2u;
    u.mode = {static_cast<f32>(n), whole ? 1.0f : 0.0f, static_cast<f32>(static_cast<u32>(in.output)), static_cast<f32>(bits)};
    const Vec4 s = in.solid;
    u.color = {s.x * s.w, s.y * s.w, s.z * s.w, s.w};
    u.blur = {static_cast<f32>(in.blurSamples), 0.0f, 0.0f, 0.0f};
    // Com o recorte o fundo é lido no ponto da tela (nunca pela grade): a grade pode
    // passar da caixa da camada e a borda presa fica FORA dela — a cabeça encostada
    // no topo da foto mexe junto, sem esticar o que está na borda.
    const f32 margin = in.mask && !whole ? 0.15f * std::min(W, H) : 0.0f;
    u.window = {(whole ? 0.08f : 0.03f) * std::min(W, H), loose && in.hair ? 1.0f : 0.0f, loose && in.cloth ? 1.0f : 0.0f, margin};
    // Zoom/solavanco globais: o zoom cobre o solavanco (a borda nunca aparece).
    // O solavanco é diagonal (frameDx, frameDy): o zoom cobre os dois eixos.
    auto global = [&](const Pose& p, f32& zoom, f32& dy, f32& dx) {
        dy = std::clamp(fin(p.frameDy) * W, -0.2f * H, 0.2f * H);
        dx = std::clamp(fin(p.frameDx) * W, -0.2f * W, 0.2f * W);
        zoom = (1.0f + std::clamp(fin(p.frameZoom), 0.0f, 0.5f)) * std::max(1.0f + 2.0f * std::fabs(dy) / H, 1.0f + 2.0f * std::fabs(dx) / W);
    };
    global(in.pose[0], u.frame.x, u.frame.y, u.frameShift.x);
    global(in.prevPose[0], u.frame.z, u.frame.w, u.frameShift.y);

    std::vector<std::pair<f32, f32>> bx, by;
    if (whole) {
        GpuFace& F = u.face[0];
        F.head = {W * 0.5f, H * 0.4f, W * 0.25f, H * 0.2f};
        F.axis = {1, 0, 0, 0.45f};
        F.neck = {W * 0.5f, H * 0.55f, W * 0.5f, H * 0.55f};
        F.body = {W * 0.5f, H * 0.45f, 0, 0};
        auto torso = [&](const Pose& p) {
            return Vec4{fin(p.torsoDx) * W, fin(p.torsoDy) * W, std::clamp(fin(p.torsoRoll), -20.0f, 20.0f) * kDeg, 0.0f};
        };
        F.torso = torso(in.pose[0]);
        F.prevTorso = torso(in.prevPose[0]);
    }
    for (u32 i = 0; i < n; ++i) {
        const RigFace& r = in.rig.faces[i];
        GpuFace& F = u.face[i];
        const f32 rx = std::max(2.0f, fin(r.axes.x, 0.1f) * W), ry = std::max(2.0f, fin(r.axes.y, 0.1f) * H);
        const Vec2 c{fin(r.center.x, 0.5f) * W, fin(r.center.y, 0.35f) * H};
        const f32 roll = std::clamp(fin(r.roll), -80.0f, 80.0f) * kDeg;
        F.head = {c.x, c.y, rx, ry};
        F.axis = {std::cos(roll), std::sin(roll), std::clamp(fin(r.depth, 0.85f), 0.2f, 1.5f) * rx, 0.9f};   // queda larga: o aceno de 14° não dobra a grade ao lado da cabeça
        Vec2 neck{fin(r.neck.x, 0.5f) * W, fin(r.neck.y, 0.5f) * H};
        if (in.pivotManual && i == 0) neck = Vec2{fin(in.pivot.x, 0.5f) * W, fin(in.pivot.y, 0.5f) * H};
        const Vec2 sa{fin(r.shoulderA.x) * W, fin(r.shoulderA.y) * H}, sb{fin(r.shoulderB.x) * W, fin(r.shoulderB.y) * H};
        const Vec2 smid = (sa + sb) * 0.5f;
        const f32 half = std::max(rx * 1.2f, 0.5f * len(sb - sa));
        F.neck = {neck.x, neck.y, smid.x, std::max(smid.y, neck.y + 1.0f)};
        F.body = {half, 3.0f * half, 0.0f, 0.0f};
        // Amplitudes da pose (fração da largura) na escala da pessoa: a pose foi
        // afinada para uma cabeça de ~25 % da largura do quadro.
        const f32 scale = std::clamp(2.0f * rx / (0.25f * W), 0.35f, 2.0f);
        const f32 k = W * scale;
        auto torso = [&](const Pose& p) {
            return Vec4{fin(p.torsoDx) * k, fin(p.torsoDy) * k, std::clamp(fin(p.torsoRoll), -20.0f, 20.0f) * kDeg, 0.0f};
        };
        auto headT = [&](const Pose& p) {
            return Vec4{fin(p.headDx) * k, (fin(p.headDy) + fin(p.extremityLag) * 0.25f) * k,
                        std::clamp(fin(p.headRoll), -30.0f, 30.0f) * kDeg, std::clamp(fin(p.headPitch), -25.0f, 25.0f) * kDeg};
        };
        // Giro 3D limitado a ±15° (o elipsoide aproximado não aguenta mais).
        auto headR = [&](const Pose& p) { return Vec4{std::clamp(fin(p.headYaw), -15.0f, 15.0f) * kDeg, 0, 0, 0}; };
        F.torso = torso(in.pose[i]); F.prevTorso = torso(in.prevPose[i]);
        F.headT = headT(in.pose[i]); F.prevHeadT = headT(in.prevPose[i]);
        F.headR = headR(in.pose[i]); F.prevHeadR = headR(in.prevPose[i]);
        if (loose) {
            auto hairT = [&](const Pose& p) {
                return Vec4{std::clamp(fin(p.hairDx) * k, -0.35f * rx, 0.35f * rx), std::clamp(fin(p.hairDy) * k, -0.35f * ry, 0.35f * ry),
                            std::clamp(fin(p.hairRoll), -30.0f, 30.0f) * kDeg, std::clamp(fin(p.hairPitch), -25.0f, 25.0f) * kDeg};
            };
            auto clothT = [&](const Pose& p) {
                return Vec4{std::clamp(fin(p.clothDx) * k, -half, half), std::clamp(fin(p.clothDy) * k, -half, half),
                            std::clamp(fin(p.clothRoll), -15.0f, 15.0f) * kDeg, std::clamp(fin(p.waveAmp) * k, 0.0f, 0.2f * half)};
            };
            // Tremulação: até ~6 % da largura da cabeça.
            auto flutter = [&](const Pose& p) {
                return Vec4{std::clamp(fin(p.flutterX), -2.0f, 2.0f) * 0.06f * rx, std::clamp(fin(p.flutterY), -2.0f, 2.0f) * 0.04f * rx,
                            fin(p.wavePhase), 0.0f};
            };
            F.hairT = hairT(in.pose[i]); F.prevHairT = hairT(in.prevPose[i]);
            F.clothT = clothT(in.pose[i]); F.prevClothT = clothT(in.prevPose[i]);
            F.flutter = flutter(in.pose[i]); F.prevFlutter = flutter(in.prevPose[i]);
        }
        bx.push_back({c.x, (loose ? 2.4f : 1.6f) * rx});
        by.push_back({c.y + (loose ? -0.4f : 0.4f) * ry, (loose ? 2.6f : 1.8f) * ry});
    }
    if (margin > 0.0f) {
        for (auto& b : bx) b.first += margin;
        for (auto& b : by) b.first += margin;
    }
    grid_lines(W + 2.0f * margin, gx, bx, u.gridX);
    grid_lines(H + 2.0f * margin, gy, by, u.gridY);
    if (margin > 0.0f) {
        f32* lx = reinterpret_cast<f32*>(u.gridX);
        f32* ly = reinterpret_cast<f32*>(u.gridY);
        for (u32 i = 0; i < gx; ++i) lx[i] -= margin;
        for (u32 i = 0; i < gy; ++i) ly[i] -= margin;
    }
}

BeatGrid beat_grid(u32 source, f32 autoBpm, f32 autoPhase, f32 tapBpm, f32 tapPhase, const StoreEntry* entry) {
    BeatGrid g;
    if (source == static_cast<u32>(BeatSource::Tap)) {
        g.bpm = std::clamp<f64>(fin(tapBpm), 0.0, 400.0);
        g.phaseSec = fin(tapPhase);
    } else if (source == static_cast<u32>(BeatSource::Automatic)) {
        if (entry && !entry->grid.beats.empty()) return entry->grid;
        g.bpm = std::clamp<f64>(fin(autoBpm), 0.0, 400.0);
        g.phaseSec = fin(autoPhase);
    } else {
        g.phaseSec = fin(autoPhase);
    }
    if (!(g.bpm > 0.0) && g.beats.empty()) g.bpm = 120.0;
    return g;
}

// ---- Armazenamento ------------------------------------------------------------------

namespace {

template <class T> void put_raw(std::string& s, const T& v) { s.append(reinterpret_cast<const char*>(&v), sizeof v); }
template <class T> bool get_raw(const std::string& s, usize& at, T& v) {
    if (at + sizeof v > s.size()) return false;
    std::memcpy(&v, s.data() + at, sizeof v);
    at += sizeof v;
    return true;
}

std::string entry_path(const std::string& dir, u32 token) {
    if (dir.empty()) return {};
    char name[32];
    std::snprintf(name, sizeof name, "%06x.hbr", token & 0xFFFFFFu);
    return (std::filesystem::path(dir) / name).string();
}

} // namespace

std::string serialize_entry(const StoreEntry& e) {
    std::string s = "HBR1";
    put_raw(s, u32{1});
    put_raw(s, static_cast<u32>(e.track.frames.size()));
    for (const RigFrame& f : e.track.frames) {
        put_raw(s, f.time);
        put_raw(s, f.count);
        for (u32 i = 0; i < f.count && i < kMaxFaces; ++i) {
            f32 v[kFaceFloats];
            face_to(f.faces[i], v);
            s.append(reinterpret_cast<const char*>(v), sizeof v);
        }
    }
    const std::string grid = serialize(e.grid);
    put_raw(s, static_cast<u32>(grid.size()));
    s += grid;
    return s;
}

bool deserialize_entry(const std::string& blob, StoreEntry& out) {
    if (blob.size() < 12 || blob.compare(0, 4, "HBR1") != 0) return false;
    usize at = 4;
    u32 version = 0, frames = 0;
    if (!get_raw(blob, at, version) || version != 1 || !get_raw(blob, at, frames)) return false;
    if (frames > 1u << 20) return false;
    StoreEntry e;
    e.track.frames.reserve(frames);
    for (u32 i = 0; i < frames; ++i) {
        RigFrame f;
        if (!get_raw(blob, at, f.time) || !get_raw(blob, at, f.count) || f.count > kMaxFaces) return false;
        for (u32 k = 0; k < f.count; ++k) {
            f32 v[kFaceFloats];
            if (at + sizeof v > blob.size()) return false;
            std::memcpy(v, blob.data() + at, sizeof v);
            at += sizeof v;
            f.faces[k] = face_from(v);
        }
        e.track.frames.push_back(f);
    }
    u32 gridBytes = 0;
    if (!get_raw(blob, at, gridBytes) || at + gridBytes > blob.size()) return false;
    if (gridBytes && !deserialize(blob.substr(at, gridBytes), e.grid)) return false;
    out = std::move(e);
    return true;
}

RigStore& RigStore::shared() {
    static RigStore s;
    return s;
}

void RigStore::set_directory(std::string dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dir == dir_) return;
    dir_ = std::move(dir);
    missing_.clear();
}

std::string RigStore::directory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dir_;
}

bool RigStore::put(u32 token, std::shared_ptr<const StoreEntry> entry) {
    if (!token || !entry) return false;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        map_[token] = entry;
        missing_.erase(token);
        path = entry_path(dir_, token);
    }
    if (path.empty()) return true;
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        const std::string blob = serialize_entry(*entry);
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
            if (!f) return false;
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec) { std::filesystem::remove(tmp, ec); return false; }
        return true;
    }
}

std::shared_ptr<const StoreEntry> RigStore::get(u32 token) {
    if (!token) return nullptr;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = map_.find(token); it != map_.end()) return it->second;
        if (missing_.count(token)) return nullptr;
        path = entry_path(dir_, token);
    }
    std::shared_ptr<StoreEntry> e;
    if (!path.empty()) {
        {
            std::ifstream f(path, std::ios::binary);
            if (f) {
                const std::string blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                auto loaded = std::make_shared<StoreEntry>();
                if (deserialize_entry(blob, *loaded)) e = std::move(loaded);
            }
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (e) map_[token] = e;
    else missing_[token] = true;   // apagado pela limpeza: o rig parado do efeito vale
    return e;
}

u32 RigStore::new_token() {
    static std::atomic<u64> counter{0};
    for (u32 attempt = 0; attempt < 64; ++attempt) {
        u64 h = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ (counter.fetch_add(1) * 0x9E3779B97F4A7C15ull);
        h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 29;
        const u32 token = static_cast<u32>(h & 0xFFFFFFu);
        if (!token) continue;
        std::string path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (map_.count(token)) continue;
            path = entry_path(dir_, token);
        }
        std::error_code ec;
        if (!path.empty() && std::filesystem::exists(path, ec)) continue;
        return token;
    }
    return 0;
}

void RigStore::clear_memory() {
    std::lock_guard<std::mutex> lock(mutex_);
    map_.clear();
    missing_.clear();
}

void set_low_end_device(bool low) noexcept { gLowEnd.store(low, std::memory_order_relaxed); }
bool low_end_device() noexcept { return gLowEnd.load(std::memory_order_relaxed); }

} // namespace aurea::headbop
