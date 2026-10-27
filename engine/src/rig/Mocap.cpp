// Motion Capture (BETA): pose detectada → rotações por osso (ver rig/Mocap.hpp).
#include "aurea/rig/Mocap.hpp"

#include "aurea/rig/Ik.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace aurea::rig {
namespace {

constexpr usize N = kHumanoidBoneCount;
using HB = HumanoidBone;
constexpr usize ix(HB b) noexcept { return static_cast<usize>(b); }

bool finite3(Vec3 v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Quat swing_between(Vec3 from, Vec3 to) noexcept { return rotation_between(from, to); }

/// q^k pelo eixo-ângulo (k > 1 exagera, k < 1 atenua).
Quat qpow(Quat q, f32 k) noexcept {
    q = q.normalized();
    if (q.w < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};
    const f32 w = std::clamp(q.w, -1.0f, 1.0f);
    const f32 s = std::sqrt(std::max(0.0f, 1.0f - w * w));
    if (s < 1e-6f) return Quat::identity();
    const f32 angle = 2.0f * std::acos(w);
    return Quat::from_axis_angle(Vec3{q.x / s, q.y / s, q.z / s}, angle * k);
}

f32 qangle(Quat q) noexcept {
    q = q.normalized();
    return 2.0f * std::acos(std::clamp(std::fabs(q.w), 0.0f, 1.0f));
}

/// Quaternion da matriz de colunas (c0, c1, c2) ortonormais.
Quat from_basis(Vec3 c0, Vec3 c1, Vec3 c2) noexcept {
    const f32 m00 = c0.x, m10 = c0.y, m20 = c0.z;
    const f32 m01 = c1.x, m11 = c1.y, m21 = c1.z;
    const f32 m02 = c2.x, m12 = c2.y, m22 = c2.z;
    const f32 trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        const f32 s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const f32 s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const f32 s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const f32 s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return q.normalized();
}

Vec3 any_perpendicular(Vec3 v) noexcept {
    const Vec3 a = std::fabs(v.x) < 0.8f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return v.cross(a).normalized();
}

/// Referencial com `up` principal e `left` secundário (colunas left, up, frente).
Quat frame_up(Vec3 up, Vec3 left) noexcept {
    const Vec3 u = up.normalized();
    if (u.length_sq() < 0.5f) return Quat::identity();
    Vec3 l = left - u * left.dot(u);
    l = l.length_sq() > 1e-10f ? l.normalized() : any_perpendicular(u);
    return from_basis(l, u, l.cross(u));
}

/// Referencial com `left` principal e `forward` secundário (cabeça: linha das orelhas e nariz).
Quat frame_left(Vec3 left, Vec3 forward) noexcept {
    const Vec3 l = left.normalized();
    if (l.length_sq() < 0.5f) return Quat::identity();
    Vec3 f = forward - l * forward.dot(l);
    f = f.length_sq() > 1e-10f ? f.normalized() : any_perpendicular(l);
    return from_basis(l, f.cross(l), f);
}

/// Ângulo com sinal de `a` para `b` em volta de `axis` (os dois projetados no plano).
f32 signed_angle(Vec3 a, Vec3 b, Vec3 axis) noexcept {
    const Vec3 n = axis.normalized();
    const Vec3 pa = a - n * a.dot(n), pb = b - n * b.dot(n);
    if (pa.length_sq() < 1e-10f || pb.length_sq() < 1e-10f) return 0.0f;
    return std::atan2(pa.cross(pb).dot(n), pa.dot(pb));
}

f32 smoothstep(f32 a, f32 b, f32 x) noexcept {
    const f32 t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// Mira o osso: `rest` (direção de repouso) vai para `target`; depois gira em
/// volta da própria direção para levar `restSec` a `targetSec` (peso `w`).
Quat aim(Quat parent, Vec3 rest, Vec3 restSec, Vec3 target, Vec3 targetSec, f32 w) noexcept {
    const Vec3 d0 = rotate(parent, rest);
    Quat q = (swing_between(d0, target) * parent).normalized();
    if (w > 1e-3f && targetSec.length_sq() > 1e-10f) {
        const Vec3 cur = rotate(q, restSec);
        const f32 angle = signed_angle(cur, targetSec, target);
        q = (Quat::from_axis_angle(target.normalized(), angle * w) * q).normalized();
    }
    return q;
}

/// Limite de articulação: a rotação relativa ao pai fica até `maxRad`.
Quat limit_relative(Quat child, Quat parent, f32 maxRad) noexcept {
    const Quat r = (conj(parent) * child).normalized();
    const f32 a = qangle(r);
    if (a <= maxRad || a < 1e-6f) return child;
    return (parent * qpow(r, maxRad / a)).normalized();
}

f32 percentile(std::vector<f32>& v, f32 p) noexcept {
    if (v.empty()) return 0.0f;
    const usize k = static_cast<usize>(std::clamp(p, 0.0f, 1.0f) * static_cast<f32>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

/// Membros (para a confiança): pontos de cada um.
constexpr std::array<std::array<u32, 4>, kLimbCount> kLimbPoints{{
    {lm::LeftShoulder, lm::RightShoulder, lm::LeftHip, lm::RightHip},
    {lm::Nose, lm::LeftEar, lm::RightEar, lm::Nose},
    {lm::LeftShoulder, lm::LeftElbow, lm::LeftWrist, lm::LeftWrist},
    {lm::RightShoulder, lm::RightElbow, lm::RightWrist, lm::RightWrist},
    {lm::LeftHip, lm::LeftKnee, lm::LeftAnkle, lm::LeftAnkle},
    {lm::RightHip, lm::RightKnee, lm::RightAnkle, lm::RightAnkle},
}};

/// Segmentos do esqueleto de captura, pais antes dos filhos. `prior` = sinal
/// de profundidade quando o detector não diz (0 = no plano da imagem);
/// `mirror` = o segmento do outro lado (comprimentos simétricos).
struct Segment { u32 parent, child; i8 prior; i8 mirror; };
constexpr std::array<Segment, 26> kSegments{{
    {kMidHip, lm::LeftHip, 0, 1},              // 0
    {kMidHip, lm::RightHip, 0, 0},             // 1
    {kMidHip, kMidShoulder, 0, -1},            // 2
    {kMidShoulder, lm::LeftShoulder, 0, 4},    // 3
    {kMidShoulder, lm::RightShoulder, 0, 3},   // 4
    {kMidShoulder, lm::Nose, 1, -1},           // 5
    {lm::Nose, lm::LeftEar, -1, 7},            // 6
    {lm::Nose, lm::RightEar, -1, 6},           // 7
    {lm::LeftShoulder, lm::LeftElbow, 1, 9},   // 8
    {lm::RightShoulder, lm::RightElbow, 1, 8}, // 9
    {lm::LeftElbow, lm::LeftWrist, 1, 11},     // 10
    {lm::RightElbow, lm::RightWrist, 1, 10},   // 11
    {lm::LeftWrist, lm::LeftPinky, 1, 13},     // 12
    {lm::RightWrist, lm::RightPinky, 1, 12},   // 13
    {lm::LeftWrist, lm::LeftIndex, 1, 15},     // 14
    {lm::RightWrist, lm::RightIndex, 1, 14},   // 15
    {lm::LeftWrist, lm::LeftThumb, 1, 17},     // 16
    {lm::RightWrist, lm::RightThumb, 1, 16},   // 17
    {lm::LeftHip, lm::LeftKnee, 1, 19},        // 18
    {lm::RightHip, lm::RightKnee, 1, 18},      // 19
    {lm::LeftKnee, lm::LeftAnkle, -1, 21},     // 20
    {lm::RightKnee, lm::RightAnkle, -1, 20},   // 21
    {lm::LeftAnkle, lm::LeftHeel, -1, 23},     // 22
    {lm::RightAnkle, lm::RightHeel, -1, 22},   // 23
    {lm::LeftAnkle, lm::LeftFootIndex, 1, 25}, // 24
    {lm::RightAnkle, lm::RightFootIndex, 1, 24}, // 25
}};
constexpr usize kLeftThigh = 18, kRightThigh = 19, kLeftShin = 20, kRightShin = 21;

Vec3 capture_point(const PoseLandmark& p, bool world, f32 aspect) noexcept {
    if (world) return Vec3{p.x, -p.y, -p.z};
    return Vec3{(p.x - 0.5f) * aspect, -(p.y - 0.5f), -p.z * aspect};
}

f32 point_conf(const PoseFrame& f, u32 j) noexcept {
    if (j == kMidHip) return std::min(f.pts[lm::LeftHip].confidence, f.pts[lm::RightHip].confidence);
    if (j == kMidShoulder) return std::min(f.pts[lm::LeftShoulder].confidence, f.pts[lm::RightShoulder].confidence);
    return f.pts[j].confidence;
}

} // namespace

// ---- Utilidades ------------------------------------------------------------------------

void unpack_frames(const f32* data, u32 count, std::vector<PoseFrame>& out) {
    if (!data) return;
    out.reserve(out.size() + count);
    for (u32 i = 0; i < count; ++i) {
        const f32* f = data + static_cast<usize>(i) * kMocapFrameFloats;
        if (!std::isfinite(f[0])) continue;
        PoseFrame p;
        p.time_s = f[0];
        p.has_world = f[1] > 0.5f;
        for (u32 j = 0; j < kPoseLandmarks; ++j) {
            const f32* q = f + 2 + j * 4;
            PoseLandmark& l = p.pts[j];
            const bool ok = std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) && std::isfinite(q[3]);
            l = ok ? PoseLandmark{q[0], q[1], q[2], std::clamp(q[3], 0.0f, 1.0f)} : PoseLandmark{};
        }
        out.push_back(p);
    }
}

MocapParams MocapParams::sanitized() const noexcept {
    MocapParams p = *this;
    auto fix = [](f32 v, f32 lo, f32 hi, f32 def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    p.intensity = fix(intensity, 0.0f, 1.5f, 1.0f);
    p.smoothing = fix(smoothing, 0.0f, 1.0f, 0.5f);
    p.speed = fix(speed, 0.25f, 4.0f, 1.0f);
    p.yawDeg = fix(yawDeg, -180.0f, 180.0f, 0.0f);
    p.floorOffset = fix(floorOffset, -0.5f, 0.5f, 0.0f);
    p.minConfidence = fix(minConfidence, 0.05f, 0.95f, 0.5f);
    p.maxGapSeconds = fix(maxGapSeconds, 0.0f, 3.0f, 0.6f);
    p.imageAspect = fix(imageAspect, 0.1f, 10.0f, 1.0f);
    return p;
}

// ---- One Euro ---------------------------------------------------------------------------

f64 OneEuro::alpha(f64 cutoff, f64 dt) noexcept {
    const f64 tau = 1.0 / (2.0 * 3.14159265358979323846 * std::max(cutoff, 1e-4));
    return 1.0 / (1.0 + tau / std::max(dt, 1e-5));
}

f64 OneEuro::filter(f64 x, f64 dt, f64 cutoffScale) noexcept {
    if (!started_) {
        started_ = true;
        x_ = x;
        dx_ = 0.0;
        return x;
    }
    dt = std::max(dt, 1e-5);
    const f64 dx = (x - x_) / dt;
    dx_ += alpha(dCutoff_, dt) * (dx - dx_);
    const f64 cutoff = (minCutoff_ + beta_ * std::fabs(dx_)) * std::clamp(cutoffScale, 0.05, 1.0);
    x_ += alpha(cutoff, dt) * (x - x_);
    return x_;
}

// ---- 1. Limpeza ---------------------------------------------------------------------------

bool clean_frames(const std::vector<PoseFrame>& in, const MocapParams& params, std::vector<PoseFrame>& out,
                  MocapReport& report, const MocapControl& control) {
    const MocapParams p = params.sanitized();
    report = MocapReport{};
    out.clear();
    out.reserve(in.size());
    for (const PoseFrame& f : in)
        if (std::isfinite(f.time_s)) out.push_back(f);
    std::stable_sort(out.begin(), out.end(), [](const PoseFrame& a, const PoseFrame& b) { return a.time_s < b.time_s; });
    const usize n = out.size();
    if (n == 0) return false;
    // Tempos estritamente crescentes, a partir de 0.
    const f64 t0 = out.front().time_s;
    for (usize f = 0; f < n; ++f) {
        out[f].time_s -= t0;
        if (f > 0 && out[f].time_s <= out[f - 1].time_s) out[f].time_s = out[f - 1].time_s + 1e-4;
    }
    report.frames = static_cast<u32>(n);
    report.duration = out.back().time_s;
    usize world = 0;
    for (const PoseFrame& f : out) world += f.has_world ? 1u : 0u;
    report.hasWorld = world * 2 > n;
    for (PoseFrame& f : out) f.has_world = report.hasWorld && f.has_world;
    // Membros incertos (pela confiança crua).
    std::array<u32, kLimbCount> low{};
    for (const PoseFrame& f : out)
        for (usize m = 0; m < kLimbCount; ++m) {
            f32 c = 1.0f;
            for (const u32 j : kLimbPoints[m]) c = std::min(c, f.pts[j].confidence);
            if (c < p.minConfidence) ++low[m];
        }
    // Lacunas: curtas viram interpolação; longas seguram o vizinho (confiança 0).
    std::array<u8, kPoseLandmarks> missing{};
    std::vector<u8> valid(n);
    usize filled = 0;
    for (u32 j = 0; j < kPoseLandmarks; ++j) {
        if (control.cancelled()) return false;
        usize first = n;
        for (usize f = 0; f < n; ++f) {
            const PoseLandmark& l = out[f].pts[j];
            valid[f] = l.confidence >= p.minConfidence && std::isfinite(l.x) && std::isfinite(l.y) && std::isfinite(l.z);
            if (valid[f] && first == n) first = f;
        }
        if (first == n) {
            missing[j] = 1;
            for (PoseFrame& f : out) f.pts[j].confidence = 0.0f;
            continue;
        }
        auto hold = [&](usize dst, usize src) {
            PoseLandmark l = out[src].pts[j];
            l.confidence = 0.0f;
            out[dst].pts[j] = l;
        };
        for (usize f = 0; f < first; ++f) hold(f, first);
        usize last = first;
        for (usize f = first + 1; f < n; ++f) {
            if (!valid[f]) continue;
            if (f - last > 1) {
                const f64 span = out[f].time_s - out[last].time_s;
                const PoseLandmark a = out[last].pts[j], b = out[f].pts[j];
                for (usize g = last + 1; g < f; ++g) {
                    if (span <= p.maxGapSeconds) {
                        const f32 s = static_cast<f32>((out[g].time_s - out[last].time_s) / std::max(span, 1e-9));
                        out[g].pts[j] = PoseLandmark{lerpf(a.x, b.x, s), lerpf(a.y, b.y, s), lerpf(a.z, b.z, s),
                                                      0.5f * std::min(a.confidence, b.confidence)};
                        ++filled;
                    } else {
                        hold(g, g - last <= f - g ? last : f);
                    }
                }
            }
            last = f;
        }
        for (usize f = last + 1; f < n; ++f) hold(f, last);
        control.report(0.5f * static_cast<f32>(j + 1) / static_cast<f32>(kPoseLandmarks));
    }
    report.interpolatedFraction = static_cast<f32>(filled) / static_cast<f32>(n * kPoseLandmarks);
    // One Euro para frente e para trás (média = sem atraso de fase).
    const f32 s = p.smoothing;
    if (s > 1e-3f && n > 2) {
        const f64 minCutoff = lerpf(3.0f, 0.3f, s);
        const f64 beta = lerpf(1.5f, 0.05f, s);
        std::vector<f64> fwd(n);
        for (u32 j = 0; j < kPoseLandmarks; ++j) {
            if (missing[j]) continue;
            if (control.cancelled()) return false;
            for (u32 c = 0; c < 3; ++c) {
                auto value = [&](usize f) -> f32& { PoseLandmark& l = out[f].pts[j]; return c == 0 ? l.x : c == 1 ? l.y : l.z; };
                auto scale = [&](usize f) { return 0.3 + 0.7 * static_cast<f64>(std::clamp(out[f].pts[j].confidence, 0.0f, 1.0f)); };
                OneEuro a(minCutoff, beta), b(minCutoff, beta);
                for (usize f = 0; f < n; ++f) {
                    const f64 dt = f ? out[f].time_s - out[f - 1].time_s : 1.0 / 30.0;
                    fwd[f] = a.filter(value(f), dt, scale(f));
                }
                for (usize k = n; k-- > 0;) {
                    const f64 dt = k + 1 < n ? out[k + 1].time_s - out[k].time_s : 1.0 / 30.0;
                    const f64 back = b.filter(value(k), dt, scale(k));
                    value(k) = static_cast<f32>(0.5 * (fwd[k] + back));
                }
            }
        }
    }
    for (usize m = 0; m < kLimbCount; ++m) {
        u32 gone = 0, total = 0;
        for (const u32 j : kLimbPoints[m]) { ++total; gone += missing[j]; }
        // Cabeça: basta o nariz ou uma orelha; os outros: todos os pontos.
        report.missing[m] = m == static_cast<usize>(Limb::Head) ? (gone == total) : (gone > 0);
        report.lowFraction[m] = report.missing[m] ? 1.0f : static_cast<f32>(low[m]) / static_cast<f32>(n);
        if (report.lowFraction[m] > 0.25f) report.lowConfidence = true;
    }
    control.report(1.0f);
    return true;
}

// ---- 2. 3D com comprimentos constantes --------------------------------------------------------

f32 lift_frames(const std::vector<PoseFrame>& frames, const MocapParams& params, std::vector<CapturePose>& out,
                const MocapControl& control) {
    const MocapParams p = params.sanitized();
    const usize n = frames.size();
    out.assign(n, CapturePose{});
    if (n == 0) return 0.0f;
    const bool world = frames.front().has_world;
    std::vector<CapturePose> raw(n);
    std::array<u8, kCapturePoints> seen{};
    for (usize f = 0; f < n; ++f) {
        CapturePose& r = raw[f];
        for (u32 j = 0; j < kPoseLandmarks; ++j) {
            r[j] = capture_point(frames[f].pts[j], world, p.imageAspect);
            if (frames[f].pts[j].confidence > 0.0f) seen[j] = 1;
        }
        r[kMidHip] = (r[lm::LeftHip] + r[lm::RightHip]) * 0.5f;
        r[kMidShoulder] = (r[lm::LeftShoulder] + r[lm::RightShoulder]) * 0.5f;
    }
    seen[kMidHip] = seen[lm::LeftHip] && seen[lm::RightHip];
    seen[kMidShoulder] = seen[lm::LeftShoulder] && seen[lm::RightShoulder];
    // Comprimento de cada osso: no 2D, o maior visto (percentil 95: a projeção
    // nunca é maior que o osso); no mundo, a mediana. Lados iguais.
    std::array<f32, kSegments.size()> length{};
    std::vector<f32> samples;
    samples.reserve(n);
    for (usize s = 0; s < kSegments.size(); ++s) {
        const Segment& seg = kSegments[s];
        if (seg.parent == seg.child) continue;
        samples.clear();
        for (usize f = 0; f < n; ++f) {
            if (point_conf(frames[f], seg.parent) <= 0.0f || point_conf(frames[f], seg.child) <= 0.0f) continue;
            Vec3 d = raw[f][seg.child] - raw[f][seg.parent];
            if (!world) d.z = 0.0f;
            const f32 l = d.length();
            if (std::isfinite(l)) samples.push_back(l);
        }
        length[s] = percentile(samples, world ? 0.5f : 0.95f);
    }
    for (usize s = 0; s < kSegments.size(); ++s) {
        const i8 m = kSegments[s].mirror;
        if (m < 0) continue;
        const f32 a = length[s], b = length[static_cast<usize>(m)];
        const f32 v = world ? (a > 0 && b > 0 ? 0.5f * (a + b) : std::max(a, b)) : std::max(a, b);
        length[s] = v;
    }
    // Sinal da profundidade com histerese (sem piscar quando o osso cruza o plano).
    std::array<i8, kSegments.size()> sign{};
    for (usize s = 0; s < kSegments.size(); ++s) sign[s] = kSegments[s].prior;
    for (usize f = 0; f < n; ++f) {
        if ((f & 63u) == 0) {
            if (control.cancelled()) return 0.0f;
            control.report(static_cast<f32>(f) / static_cast<f32>(n));
        }
        const CapturePose& r = raw[f];
        CapturePose& q = out[f];
        q = r;
        q[kMidHip] = world ? r[kMidHip] : Vec3{r[kMidHip].x, r[kMidHip].y, 0.0f};
        // Sem profundidade: o lado do nariz diz para onde o tronco girou.
        i8 torsoHint = 0;
        if (!world && frames[f].pts[lm::Nose].confidence > 0.0f) {
            const f32 off = r[lm::Nose].x - r[kMidShoulder].x;
            const f32 width = (r[lm::LeftShoulder] - r[lm::RightShoulder]).length();
            if (std::fabs(off) > 0.08f * std::max(width, 1e-3f) + 1e-4f) torsoHint = off > 0 ? -1 : 1;
        }
        for (usize s = 0; s < kSegments.size(); ++s) {
            const Segment& seg = kSegments[s];
            if (seg.parent == seg.child) continue;
            const f32 L = length[s];
            const Vec3 d = r[seg.child] - r[seg.parent];
            if (world) {
                const f32 l = d.length();
                q[seg.child] = q[seg.parent] + (L > 0.0f && l > 1e-6f ? d * (L / l) : d);
                continue;
            }
            const f32 planar2 = d.x * d.x + d.y * d.y;
            const f32 mag = std::sqrt(std::max(L * L - planar2, 0.0f));
            const f32 zd = d.z;
            if (std::fabs(zd) > 0.1f * L && L > 0.0f) sign[s] = static_cast<i8>(zd > 0 ? 1 : -1);
            else if (s <= 4 && s != 2 && torsoHint != 0) {
                // Quadril/ombro esquerdo: para trás quando o nariz vai para a esquerda da pessoa.
                const bool leftSide = seg.child == lm::LeftHip || seg.child == lm::LeftShoulder;
                sign[s] = static_cast<i8>(leftSide ? torsoHint : -torsoHint);
            }
            q[seg.child] = Vec3{r[seg.child].x, r[seg.child].y, q[seg.parent].z + static_cast<f32>(sign[s]) * mag};
        }
        // Pontos sem segmento (olhos, boca) acompanham o nariz.
        for (const u32 j : {lm::LeftEyeInner, lm::LeftEye, lm::LeftEyeOuter, lm::RightEyeInner, lm::RightEye,
                            lm::RightEyeOuter, lm::MouthLeft, lm::MouthRight}) {
            if (!world) q[j] = Vec3{r[j].x, r[j].y, q[lm::Nose].z};
        }
        for (Vec3& v : q)
            if (!finite3(v)) v = Vec3{0, 0, 0};
    }
    const f32 left = length[kLeftThigh] + length[kLeftShin], right = length[kRightThigh] + length[kRightShin];
    control.report(1.0f);
    return std::max(0.5f * (left + right), 1e-4f);
}

// ---- 3. Solver ----------------------------------------------------------------------------------

ModelBasis model_basis(const HumanoidRest& rest) noexcept {
    ModelBasis b;
    auto has = [&](HB x) { return rest.present[ix(x)] != 0; };
    auto pos = [&](HB x) { return rest.position[ix(x)]; };
    Vec3 up{0, 1, 0};
    if (has(HB::Hips)) {
        if (has(HB::Neck)) up = pos(HB::Neck) - pos(HB::Hips);
        else if (has(HB::Head)) up = pos(HB::Head) - pos(HB::Hips);
        else if (has(HB::Chest)) up = pos(HB::Chest) - pos(HB::Hips);
        else if (has(HB::LeftUpperArm) && has(HB::RightUpperArm))
            up = (pos(HB::LeftUpperArm) + pos(HB::RightUpperArm)) * 0.5f - pos(HB::Hips);
    }
    up = up.length_sq() > 1e-10f ? up.normalized() : Vec3{0, 1, 0};
    Vec3 left{1, 0, 0};
    if (has(HB::LeftUpperArm) && has(HB::RightUpperArm)) left = pos(HB::LeftUpperArm) - pos(HB::RightUpperArm);
    else if (has(HB::LeftUpperLeg) && has(HB::RightUpperLeg)) left = pos(HB::LeftUpperLeg) - pos(HB::RightUpperLeg);
    left = left - up * left.dot(up);
    left = left.length_sq() > 1e-10f ? left.normalized() : any_perpendicular(up);
    b.up = up;
    b.left = left;
    b.forward = left.cross(up).normalized();
    return b;
}

namespace {

/// Osso mapeado mais próximo subindo pelos pais canônicos (Count = nenhum).
HB anchor_of(const HumanoidRest& rest, HB b) noexcept {
    for (HB p = parent_of(b); p != HB::Count; p = parent_of(p))
        if (rest.present[ix(p)]) return p;
    return HB::Count;
}

struct Solver {
    const HumanoidRest& rest;
    MocapParams p;
    ModelBasis B;
    std::array<HB, N> anchor{};
    // Repouso (direções de modelo).
    Quat restHips, restChest, restHead;
    Vec3 restNeck, restUpper[2], restLower[2], restThigh[2], restShin[2];
    Vec3 restFootPos[2];

    explicit Solver(const HumanoidRest& r, const MocapParams& params) : rest(r), p(params), B(model_basis(r)) {
        for (usize b = 0; b < N; ++b) anchor[b] = anchor_of(rest, static_cast<HB>(b));
        const Vec3 torsoUp = has(HB::LeftUpperArm) && has(HB::RightUpperArm) && has(HB::Hips)
            ? ((pos(HB::LeftUpperArm) + pos(HB::RightUpperArm)) * 0.5f - pos(HB::Hips)) : B.up;
        const Vec3 hipsLeft = has(HB::LeftUpperLeg) && has(HB::RightUpperLeg) ? pos(HB::LeftUpperLeg) - pos(HB::RightUpperLeg) : B.left;
        const Vec3 chestLeft = has(HB::LeftUpperArm) && has(HB::RightUpperArm) ? pos(HB::LeftUpperArm) - pos(HB::RightUpperArm) : B.left;
        restHips = frame_up(torsoUp, hipsLeft);
        restChest = frame_up(torsoUp, chestLeft);
        // Nariz um pouco abaixo das orelhas olhando para a frente.
        constexpr f32 kPitch = 12.0f * kDeg2Rad;
        restHead = frame_left(B.left, B.forward * std::cos(kPitch) - B.up * std::sin(kPitch));
        restNeck = has(HB::Head) && has(HB::Neck) ? pos(HB::Head) - pos(HB::Neck) : B.up;
        if (restNeck.length_sq() < 1e-12f) restNeck = B.up;
        const HB ua[2]{HB::LeftUpperArm, HB::RightUpperArm}, la[2]{HB::LeftLowerArm, HB::RightLowerArm},
            hd[2]{HB::LeftHand, HB::RightHand}, ul[2]{HB::LeftUpperLeg, HB::RightUpperLeg},
            ll[2]{HB::LeftLowerLeg, HB::RightLowerLeg}, ft[2]{HB::LeftFoot, HB::RightFoot};
        for (u32 s = 0; s < 2; ++s) {
            const Vec3 sideDir = s == 0 ? B.left : -B.left;
            restUpper[s] = has(ua[s]) && has(la[s]) ? pos(la[s]) - pos(ua[s]) : sideDir;
            restLower[s] = has(la[s]) && has(hd[s]) ? pos(hd[s]) - pos(la[s]) : restUpper[s];
            restThigh[s] = has(ul[s]) && has(ll[s]) ? pos(ll[s]) - pos(ul[s]) : -B.up;
            restShin[s] = has(ll[s]) && has(ft[s]) ? pos(ft[s]) - pos(ll[s]) : -B.up;
            restFootPos[s] = has(ft[s]) ? pos(ft[s]) : pos(HB::Hips) - B.up;
        }
    }

    [[nodiscard]] bool has(HB b) const noexcept { return rest.present[ix(b)] != 0; }
    [[nodiscard]] Vec3 pos(HB b) const noexcept { return rest.position[ix(b)]; }
    [[nodiscard]] Vec3 to_model(Vec3 v) const noexcept { return B.left * v.x + B.up * v.y + B.forward * v.z; }

    /// Deltas de mundo de um quadro (sem giro de alinhamento nem intensidade).
    void deltas(const CapturePose& Q, const std::array<u8, kCapturePoints>& seen, std::array<Quat, N>& d) const {
        d.fill(Quat::identity());
        auto P = [&](u32 j) { return to_model(Q[j]); };
        auto ok = [&](std::initializer_list<u32> js) {
            for (const u32 j : js) if (!seen[j]) return false;
            return true;
        };
        const bool torso = ok({lm::LeftHip, lm::RightHip, lm::LeftShoulder, lm::RightShoulder});
        const Vec3 up = P(kMidShoulder) - P(kMidHip);
        Quat hips = Quat::identity(), chest = Quat::identity();
        if (torso) {
            hips = (frame_up(up, P(lm::LeftHip) - P(lm::RightHip)) * conj(restHips)).normalized();
            chest = (frame_up(up, P(lm::LeftShoulder) - P(lm::RightShoulder)) * conj(restChest)).normalized();
        }
        // Coluna: metade do caminho entre o quadril e o peito; peito até 50° além do quadril.
        chest = limit_relative(chest, hips, 50.0f * kDeg2Rad);
        d[ix(HB::Hips)] = hips;
        d[ix(HB::Spine)] = Quat::slerp(hips, chest, 0.5f);
        d[ix(HB::Chest)] = chest;
        // Pescoço e cabeça.
        Quat neck = chest, head = chest;
        if (ok({lm::LeftEar, lm::RightEar}) && torso) {
            const Vec3 midEar = (P(lm::LeftEar) + P(lm::RightEar)) * 0.5f;
            neck = (swing_between(rotate(chest, restNeck), midEar - P(kMidShoulder)) * chest).normalized();
            neck = limit_relative(neck, chest, 45.0f * kDeg2Rad);
            if (seen[lm::Nose]) {
                head = (frame_left(P(lm::LeftEar) - P(lm::RightEar), P(lm::Nose) - midEar) * conj(restHead)).normalized();
                head = limit_relative(head, neck, 60.0f * kDeg2Rad);
            } else {
                head = neck;
            }
        }
        d[ix(HB::Neck)] = neck;
        d[ix(HB::Head)] = head;
        // Braços: mira do braço com o giro pelo plano do cotovelo; o cotovelo é
        // uma dobradiça de 0 a 150°; mão até 80° do antebraço.
        const u32 sh[2]{lm::LeftShoulder, lm::RightShoulder}, el[2]{lm::LeftElbow, lm::RightElbow},
            wr[2]{lm::LeftWrist, lm::RightWrist}, pk[2]{lm::LeftPinky, lm::RightPinky}, id[2]{lm::LeftIndex, lm::RightIndex};
        const HB ua[2]{HB::LeftUpperArm, HB::RightUpperArm}, la[2]{HB::LeftLowerArm, HB::RightLowerArm}, hd[2]{HB::LeftHand, HB::RightHand};
        for (u32 s = 0; s < 2; ++s) {
            Quat upper = chest, lower = chest, hand = chest;
            if (ok({sh[s], el[s], wr[s]})) {
                const Vec3 cu = P(el[s]) - P(sh[s]), cl = P(wr[s]) - P(el[s]);
                const f32 bend = std::acos(std::clamp(cu.normalized().dot(cl.normalized()), -1.0f, 1.0f));
                const Vec3 bendDir = cl - cu.normalized() * cl.dot(cu.normalized());
                upper = aim(chest, restUpper[s], B.forward, cu, bendDir, smoothstep(5.0f * kDeg2Rad, 20.0f * kDeg2Rad, bend));
                // Dobradiça: eixo = cima (esquerdo: −cima) girado pelo braço.
                const Vec3 axis = rotate(upper, s == 0 ? -B.up : B.up);
                const Vec3 armDir = rotate(upper, restUpper[s]).normalized();
                const f32 phi = std::clamp(signed_angle(armDir, cl, axis), 0.0f, 150.0f * kDeg2Rad);
                const Vec3 hinged = rotate(Quat::from_axis_angle(axis, phi), armDir);
                const f32 w = smoothstep(5.0f * kDeg2Rad, 20.0f * kDeg2Rad, bend);
                const Vec3 lowerDir = (cl.normalized() * (1.0f - w) + hinged * w);
                lower = (swing_between(rotate(upper, restLower[s]), lowerDir) * upper).normalized();
                hand = lower;
                if (ok({pk[s], id[s]})) {
                    const Vec3 tip = (P(pk[s]) + P(id[s])) * 0.5f - P(wr[s]);
                    hand = aim(lower, restLower[s], B.forward, tip, P(id[s]) - P(pk[s]), 0.7f);
                    hand = limit_relative(hand, lower, 80.0f * kDeg2Rad);
                }
            }
            d[ix(ua[s])] = upper;
            d[ix(la[s])] = lower;
            d[ix(hd[s])] = hand;
        }
        // Pernas: o joelho aponta para onde a canela NÃO vai (dobra para trás),
        // ou para a ponta do pé com a perna reta; joelho de 0 a 150°; pé até 50°.
        const u32 hp[2]{lm::LeftHip, lm::RightHip}, kn[2]{lm::LeftKnee, lm::RightKnee}, an[2]{lm::LeftAnkle, lm::RightAnkle},
            he[2]{lm::LeftHeel, lm::RightHeel}, to[2]{lm::LeftFootIndex, lm::RightFootIndex};
        const HB ul[2]{HB::LeftUpperLeg, HB::RightUpperLeg}, ll[2]{HB::LeftLowerLeg, HB::RightLowerLeg}, ft[2]{HB::LeftFoot, HB::RightFoot};
        for (u32 s = 0; s < 2; ++s) {
            Quat thigh = hips, shin = hips, foot = hips;
            if (ok({hp[s], kn[s], an[s]})) {
                const Vec3 ct = P(kn[s]) - P(hp[s]), cs = P(an[s]) - P(kn[s]);
                const Vec3 tn = ct.normalized();
                const f32 bend = std::acos(std::clamp(tn.dot(cs.normalized()), -1.0f, 1.0f));
                const Vec3 bendDir = cs - tn * cs.dot(tn);
                Vec3 footDir{0, 0, 0};
                const bool hasFoot = ok({he[s], to[s]});
                if (hasFoot) {
                    footDir = P(to[s]) - P(he[s]);
                    footDir = footDir - tn * footDir.dot(tn);
                }
                f32 w = smoothstep(8.0f * kDeg2Rad, 25.0f * kDeg2Rad, bend);
                const Vec3 kneeFwd = bendDir.length_sq() > 1e-12f ? -bendDir.normalized() : Vec3{0, 0, 0};
                // Joelho contra a ponta do pé = dobra impossível: confia no pé.
                if (hasFoot && footDir.length_sq() > 1e-12f && kneeFwd.dot(footDir.normalized()) < -0.2f) w = 0.0f;
                Vec3 sec = kneeFwd * w + (footDir.length_sq() > 1e-12f ? footDir.normalized() : Vec3{0, 0, 0}) * (1.0f - w);
                thigh = aim(hips, restThigh[s], B.forward, ct, sec, sec.length_sq() > 1e-8f ? 1.0f : 0.0f);
                const Vec3 axis = rotate(thigh, B.left);
                const Vec3 legDir = rotate(thigh, restThigh[s]).normalized();
                const f32 phi = std::clamp(signed_angle(legDir, cs, axis), 0.0f, 150.0f * kDeg2Rad);
                const Vec3 hinged = rotate(Quat::from_axis_angle(axis, phi), legDir);
                const f32 hw = smoothstep(4.0f * kDeg2Rad, 15.0f * kDeg2Rad, bend);
                const Vec3 shinDir = cs.normalized() * (1.0f - hw) + hinged * hw;
                shin = (swing_between(rotate(thigh, restShin[s]), shinDir) * thigh).normalized();
                foot = shin;
                if (hasFoot) {
                    foot = (swing_between(rotate(shin, B.forward), P(to[s]) - P(he[s])) * shin).normalized();
                    foot = limit_relative(foot, shin, 50.0f * kDeg2Rad);
                }
            }
            d[ix(ul[s])] = thigh;
            d[ix(ll[s])] = shin;
            d[ix(ft[s])] = foot;
        }
    }

    /// Posição de mundo (modelo) do osso pela cadeia de deltas.
    void positions(const std::array<Quat, N>& d, Vec3 hipsDisp, std::array<Vec3, N>& out) const {
        for (usize b = 0; b < N; ++b) {
            const HB a = anchor[b];
            if (a == HB::Count) { out[b] = rest.position[b] + hipsDisp; continue; }
            out[b] = out[ix(a)] + rotate(d[ix(a)], rest.position[b] - rest.position[ix(a)]);
        }
    }
};

} // namespace

bool solve(const std::vector<PoseFrame>& frames, const HumanoidRest& rest, const MocapParams& params,
           MocapResult& out, const MocapControl& control) {
    out = MocapResult{};
    const MocapParams p = params.sanitized();
    std::vector<PoseFrame> clean;
    MocapControl stage = control;
    stage.base = control.base;
    stage.span = control.span * 0.5f;
    if (!clean_frames(frames, p, clean, out.report, stage)) return false;
    const usize n = clean.size();
    std::array<u8, kCapturePoints> seen{};
    for (const PoseFrame& f : clean)
        for (u32 j = 0; j < kPoseLandmarks; ++j) seen[j] |= f.pts[j].confidence > 0.0f ? 1 : 0;
    seen[kMidHip] = seen[lm::LeftHip] && seen[lm::RightHip];
    seen[kMidShoulder] = seen[lm::LeftShoulder] && seen[lm::RightShoulder];
    std::vector<CapturePose> cap;
    stage.base = control.base + control.span * 0.5f;
    stage.span = control.span * 0.15f;
    const f32 capLeg = lift_frames(clean, p, cap, stage);
    if (control.cancelled() || cap.size() != n) return false;

    const Solver S(rest, p);
    out.times.resize(n);
    out.world.resize(n);
    out.local.resize(n);
    out.hipsLocal.assign(n, Vec3{0, 0, 0});
    out.footLocked.assign(n, {0, 0});
    for (usize f = 0; f < n; ++f) out.times[f] = clean[f].time_s;

    // Deltas, intensidade (relativa ao pai, da raiz para as pontas) e giro de alinhamento.
    const Quat yaw = Quat::from_axis_angle(S.B.up, p.yawDeg * kDeg2Rad);
    for (usize f = 0; f < n; ++f) {
        if ((f & 63u) == 0) {
            if (control.cancelled()) return false;
            control.report(0.65f + 0.15f * static_cast<f32>(f) / static_cast<f32>(n));
        }
        std::array<Quat, N> raw;
        S.deltas(cap[f], seen, raw);
        std::array<Quat, N>& d = out.world[f];
        for (usize b = 0; b < N; ++b) {
            const HB par = parent_of(static_cast<HB>(b));
            const Quat parentRaw = par == HB::Count ? Quat::identity() : raw[ix(par)];
            const Quat parentNew = par == HB::Count ? Quat::identity() : d[ix(par)];
            const Quat rel = (conj(parentRaw) * raw[b]).normalized();
            d[b] = (parentNew * (std::fabs(p.intensity - 1.0f) < 1e-4f ? rel : qpow(rel, p.intensity))).normalized();
        }
        for (Quat& q : d) q = (yaw * q).normalized();
    }

    // Translação do quadril: proporções pela altura da perna.
    const auto& R = rest;
    auto has = [&](HB b) { return R.present[ix(b)] != 0; };
    f32 modelLeg = 0.0f;
    u32 legs = 0;
    for (const auto& chain : {std::array<HB, 3>{HB::LeftUpperLeg, HB::LeftLowerLeg, HB::LeftFoot},
                              std::array<HB, 3>{HB::RightUpperLeg, HB::RightLowerLeg, HB::RightFoot}}) {
        if (!has(chain[0]) || !has(chain[1]) || !has(chain[2])) continue;
        modelLeg += (R.position[ix(chain[1])] - R.position[ix(chain[0])]).length() +
                    (R.position[ix(chain[2])] - R.position[ix(chain[1])]).length();
        ++legs;
    }
    modelLeg = legs ? modelLeg / static_cast<f32>(legs) : (R.height > 0 ? R.height * 0.48f : 1.0f);
    out.legLength = modelLeg;
    const f32 scale = modelLeg / std::max(capLeg, 1e-4f);
    const bool feetSeen = seen[lm::LeftAnkle] && seen[lm::RightAnkle];
    f32 floorY = 0.0f, ankleH = 0.0f;
    {
        std::vector<f32> lows, ankles;
        lows.reserve(n);
        for (usize f = 0; f < n; ++f) {
            const CapturePose& q = cap[f];
            f32 lowest = 1e30f;
            for (const u32 j : {lm::LeftAnkle, lm::RightAnkle, lm::LeftHeel, lm::RightHeel, lm::LeftFootIndex, lm::RightFootIndex})
                if (seen[j]) lowest = std::min(lowest, q[j].y);
            if (lowest < 1e29f) lows.push_back(lowest);
            for (u32 s = 0; s < 2; ++s) {
                const u32 an = s ? lm::RightAnkle : lm::LeftAnkle, he = s ? lm::RightHeel : lm::LeftHeel,
                          to = s ? lm::RightFootIndex : lm::LeftFootIndex;
                if (seen[he] && seen[to]) ankles.push_back(std::max(0.0f, q[an].y - std::min(q[he].y, q[to].y)));
            }
        }
        floorY = percentile(lows, 0.05f);
        ankleH = ankles.empty() ? 0.05f * capLeg : percentile(ankles, 0.5f);
    }
    const Vec3 restFootMid = (S.restFootPos[0] + S.restFootPos[1]) * 0.5f;
    // Altura das juntas do quadril (as coxas, como o meio do quadril da captura) acima do tornozelo.
    const f32 restHipsAbove = has(HB::LeftUpperLeg) && has(HB::RightUpperLeg)
        ? ((R.position[ix(HB::LeftUpperLeg)] + R.position[ix(HB::RightUpperLeg)]) * 0.5f - restFootMid).dot(S.B.up)
        : has(HB::Hips) ? (R.position[ix(HB::Hips)] - restFootMid).dot(S.B.up) : modelLeg;
    const f32 ankleFloor = restFootMid.dot(S.B.up);   // altura do tornozelo em repouso = no chão
    std::vector<Vec3> disp(n, Vec3{0, 0, 0});
    for (usize f = 0; f < n; ++f) {
        Vec3 v = S.B.up * (p.floorOffset * modelLeg);
        if (p.rootMotion && seen[kMidHip]) {
            const Vec3 h = cap[f][kMidHip] - cap[0][kMidHip];
            v += rotate(yaw, S.to_model(Vec3{h.x, 0.0f, h.z})) * scale;
            if (feetSeen) v += S.B.up * (((cap[f][kMidHip].y - floorY) - ankleH) * scale - restHipsAbove);
        }
        disp[f] = v;
    }

    // Foot lock + IK dos pés (e chão), IK das mãos (alcance proporcional ao braço).
    const HB ul[2]{HB::LeftUpperLeg, HB::RightUpperLeg}, ll[2]{HB::LeftLowerLeg, HB::RightLowerLeg}, ft[2]{HB::LeftFoot, HB::RightFoot};
    const u32 anP[2]{lm::LeftAnkle, lm::RightAnkle}, heP[2]{lm::LeftHeel, lm::RightHeel}, toP[2]{lm::LeftFootIndex, lm::RightFootIndex};
    std::vector<std::array<Vec3, N>> pos(n);
    for (usize f = 0; f < n; ++f) S.positions(out.world[f], disp[f], pos[f]);
    usize lockedFrames = 0;
    for (u32 s = 0; s < 2; ++s) {
        if (!has(ul[s]) || !has(ll[s]) || !has(ft[s]) || !seen[anP[s]]) continue;
        // Contato: pé devagar e perto do chão (com histerese).
        std::vector<u8> lock(n, 0);
        if (p.footLock && n > 2) {
            bool on = false;
            for (usize f = 0; f < n; ++f) {
                const usize a = f ? f - 1 : f, b = f + 1 < n ? f + 1 : f;
                auto footPt = [&](usize k) {
                    Vec3 c = cap[k][anP[s]];
                    u32 m = 1;
                    if (seen[heP[s]]) { c += cap[k][heP[s]]; ++m; }
                    if (seen[toP[s]]) { c += cap[k][toP[s]]; ++m; }
                    return c / static_cast<f32>(m);
                };
                const f64 dt = std::max(out.times[b] - out.times[a], 1e-3);
                const f32 speed = (footPt(b) - footPt(a)).length() / static_cast<f32>(dt) / std::max(capLeg, 1e-4f);
                f32 low = cap[f][anP[s]].y - ankleH;
                if (seen[heP[s]]) low = std::min(low, cap[f][heP[s]].y);
                if (seen[toP[s]]) low = std::min(low, cap[f][toP[s]].y);
                const f32 height = (low - floorY) / std::max(capLeg, 1e-4f);
                if (!on && speed < 0.45f && height < 0.10f) on = true;
                else if (on && (speed > 0.8f || height > 0.16f)) on = false;
                lock[f] = on ? 1 : 0;
            }
        }
        // Âncora por trecho preso; entra e sai em 0,12 s.
        constexpr f64 kBlend = 0.12;
        std::vector<Vec3> anchorPos(n, Vec3{0, 0, 0});
        std::vector<f32> weight(n, 0.0f);
        for (usize f = 0; f < n;) {
            if (!lock[f]) { ++f; continue; }
            usize e = f;
            while (e + 1 < n && lock[e + 1]) ++e;
            Vec3 a = pos[f][ix(ft[s])];
            if (p.footIk) {
                const f32 h = a.dot(S.B.up);
                if (h < ankleFloor) a += S.B.up * (ankleFloor - h);
            }
            for (usize k = f; k <= e; ++k) { anchorPos[k] = a; weight[k] = 1.0f; }
            for (usize k = f; k-- > 0 && out.times[f] - out.times[k] < kBlend;) {
                const f32 w = 1.0f - static_cast<f32>((out.times[f] - out.times[k]) / kBlend);
                if (w > weight[k]) { weight[k] = w; anchorPos[k] = a; }
            }
            for (usize k = e + 1; k < n && out.times[k] - out.times[e] < kBlend; ++k) {
                const f32 w = 1.0f - static_cast<f32>((out.times[k] - out.times[e]) / kBlend);
                if (w > weight[k]) { weight[k] = w; anchorPos[k] = a; }
            }
            lockedFrames += e - f + 1;
            f = e + 1;
        }
        for (usize f = 0; f < n; ++f) {
            out.footLocked[f][s] = lock[f];
            const Vec3 fk = pos[f][ix(ft[s])];
            Vec3 target = Vec3::lerp(fk, anchorPos[f], weight[f]);
            if (p.footIk) {
                const f32 h = target.dot(S.B.up);
                if (h < ankleFloor) target += S.B.up * (ankleFloor - h);
            }
            if ((target - fk).length_sq() < 1e-10f * modelLeg * modelLeg) continue;
            // Polo à frente do joelho (o joelho dobra para a frente).
            const Vec3 knee = pos[f][ix(ll[s])];
            const Vec3 pole = knee + rotate(out.world[f][ix(ul[s])], S.B.forward) * (0.5f * modelLeg);
            const TwoBoneIk ik = solve_two_bone(pos[f][ix(ul[s])], knee, fk, target, pole, 1.0f);
            auto& d = out.world[f];
            d[ix(ul[s])] = (ik.rootDelta * d[ix(ul[s])]).normalized();
            d[ix(ll[s])] = (ik.midDelta * ik.rootDelta * d[ix(ll[s])]).normalized();
            // O pé mantém a orientação de mundo (delta inalterado).
            S.positions(d, disp[f], pos[f]);
        }
    }
    out.report.footLockedFraction = n ? static_cast<f32>(lockedFrames) / static_cast<f32>(2 * n) : 0.0f;
    if (p.handIk) {
        const HB ua[2]{HB::LeftUpperArm, HB::RightUpperArm}, la[2]{HB::LeftLowerArm, HB::RightLowerArm}, hd[2]{HB::LeftHand, HB::RightHand};
        const u32 shP[2]{lm::LeftShoulder, lm::RightShoulder}, elP[2]{lm::LeftElbow, lm::RightElbow}, wrP[2]{lm::LeftWrist, lm::RightWrist};
        for (u32 s = 0; s < 2; ++s) {
            if (!has(ua[s]) || !has(la[s]) || !has(hd[s]) || !seen[shP[s]] || !seen[elP[s]] || !seen[wrP[s]]) continue;
            const f32 armModel = (R.position[ix(la[s])] - R.position[ix(ua[s])]).length() +
                                 (R.position[ix(hd[s])] - R.position[ix(la[s])]).length();
            for (usize f = 0; f < n; ++f) {
                const CapturePose& q = cap[f];
                const f32 armCap = (q[elP[s]] - q[shP[s]]).length() + (q[wrP[s]] - q[elP[s]]).length();
                if (armCap < 1e-5f) continue;
                const f32 reach = std::clamp((q[wrP[s]] - q[shP[s]]).length() / armCap, 0.0f, 1.0f);
                const Vec3 root = pos[f][ix(ua[s])], hand = pos[f][ix(hd[s])];
                const Vec3 dir = (hand - root).normalized();
                if (dir.length_sq() < 0.5f) continue;
                const Vec3 target = root + dir * (armModel * reach);
                if ((target - hand).length_sq() < 1e-8f * armModel * armModel) continue;
                // Polo atrás do cotovelo (o antebraço dobra para a frente do braço).
                const Vec3 elbow = pos[f][ix(la[s])];
                const Vec3 pole = elbow - rotate(out.world[f][ix(ua[s])], S.B.forward) * (0.5f * armModel);
                const TwoBoneIk ik = solve_two_bone(root, elbow, hand, target, pole, 1.0f);
                auto& d = out.world[f];
                d[ix(ua[s])] = (ik.rootDelta * d[ix(ua[s])]).normalized();
                d[ix(la[s])] = (ik.midDelta * ik.rootDelta * d[ix(la[s])]).normalized();
                S.positions(d, disp[f], pos[f]);
            }
        }
    }

    // Rotações locais por osso (params 0..2) e translação do quadril (3..5).
    for (usize f = 0; f < n; ++f) {
        if ((f & 63u) == 0) {
            if (control.cancelled()) return false;
            control.report(0.85f + 0.15f * static_cast<f32>(f) / static_cast<f32>(n));
        }
        const auto& d = out.world[f];
        auto& l = out.local[f];
        for (usize b = 0; b < N; ++b) {
            if (!R.present[b]) { l[b] = Quat::identity(); continue; }
            const HB a = S.anchor[b];
            l[b] = offset_from_world_delta(R, static_cast<HB>(b), a == HB::Count ? Quat::identity() : d[ix(a)], d[b]).normalized();
        }
        out.hipsLocal[f] = hips_local_translation(R, disp[f]);
    }
    control.report(1.0f);
    return true;
}

// ---- Saída --------------------------------------------------------------------------------------

void resample(const MocapResult& result, const MocapParams& params, f64 fps, i64 startFrame, MocapTimeline& out) {
    out = MocapTimeline{};
    out.startFrame = startFrame;
    const usize n = result.times.size();
    if (n == 0 || result.local.size() != n || !(fps > 0.0)) return;
    const f64 speed = static_cast<f64>(params.sanitized().speed);
    const f64 duration = result.times.back();
    const usize frames = static_cast<usize>(std::floor(duration * fps / speed + 1e-6)) + 1;
    out.local.resize(frames);
    out.hips.resize(frames);
    usize i = 0;
    for (usize k = 0; k < frames; ++k) {
        const f64 t = std::min(static_cast<f64>(k) * speed / fps, duration);
        while (i + 1 < n && result.times[i + 1] <= t) ++i;
        const usize j = std::min(i + 1, n - 1);
        const f64 span = result.times[j] - result.times[i];
        const f32 s = span > 1e-9 ? static_cast<f32>(std::clamp((t - result.times[i]) / span, 0.0, 1.0)) : 0.0f;
        for (usize b = 0; b < N; ++b) out.local[k][b] = Quat::slerp(result.local[i][b], result.local[j][b], s).normalized();
        out.hips[k] = Vec3::lerp(result.hipsLocal[i], result.hipsLocal[j], s);
    }
}

void reduce_keys(const std::vector<f32>& values, u32 channels, const std::vector<f32>& tolerance, std::vector<u8>& keep) {
    keep.clear();
    if (channels == 0) return;
    const usize n = values.size() / channels;
    keep.assign(n, 0);
    if (n == 0) return;
    keep[0] = 1;
    keep[n - 1] = 1;
    std::vector<std::pair<usize, usize>> stack;
    if (n > 2) stack.emplace_back(0, n - 1);
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        f32 worst = 0.0f;
        usize at = a;
        for (usize i = a + 1; i < b; ++i) {
            const f32 s = static_cast<f32>(i - a) / static_cast<f32>(b - a);
            for (u32 c = 0; c < channels; ++c) {
                const f32 va = values[a * channels + c], vb = values[b * channels + c];
                const f32 tol = c < tolerance.size() && tolerance[c] > 0 ? tolerance[c] : 1e-4f;
                const f32 e = std::fabs(values[i * channels + c] - lerpf(va, vb, s)) / tol;
                if (e > worst) { worst = e; at = i; }
            }
        }
        if (worst > 1.0f) {
            keep[at] = 1;
            stack.emplace_back(a, at);
            stack.emplace_back(at, b);
        }
    }
}

void bake_keys(const MocapTimeline& timeline, const HumanoidMap& map, const HumanoidRest& rest, bool rootMotion,
               f32 toleranceDeg, std::vector<BoneKey>& out) {
    out.clear();
    const usize frames = timeline.local.size();
    if (frames == 0) return;
    f32 leg = 0.0f;
    if (rest.present[ix(HB::LeftUpperLeg)] && rest.present[ix(HB::LeftFoot)])
        leg = (rest.position[ix(HB::LeftFoot)] - rest.position[ix(HB::LeftUpperLeg)]).length();
    if (leg <= 0.0f) leg = rest.height > 0 ? rest.height * 0.5f : 1.0f;
    const Vec3 legLocal = hips_local_translation(rest, model_basis(rest).up * leg);
    const f32 posTol = std::max(legLocal.length() * 0.002f, 1e-5f);
    const f32 degTol = std::max(toleranceDeg, 1e-3f);
    std::vector<f32> values;
    std::vector<u8> keep;
    for (usize b = 0; b < N; ++b) {
        const i32 node = map.joint[b];
        if (node < 0 || !rest.present[b]) continue;
        const bool hips = static_cast<HB>(b) == HB::Hips && rootMotion;
        const u32 ch = hips ? 6u : 3u;
        values.assign(frames * ch, 0.0f);
        Vec3 prev{0, 0, 0};
        for (usize k = 0; k < frames; ++k) {
            const Vec3 e = euler_deg_from_quat(timeline.local[k][b], prev);
            prev = e;
            values[k * ch + 0] = e.x;
            values[k * ch + 1] = e.y;
            values[k * ch + 2] = e.z;
            if (hips) {
                const Vec3 t = k < timeline.hips.size() ? timeline.hips[k] : Vec3{0, 0, 0};
                values[k * ch + 3] = t.x;
                values[k * ch + 4] = t.y;
                values[k * ch + 5] = t.z;
            }
        }
        std::vector<f32> tol(ch, degTol);
        for (u32 c = 3; c < ch; ++c) tol[c] = posTol;
        reduce_keys(values, ch, tol, keep);
        for (usize k = 0; k < frames; ++k) {
            if (!keep[k]) continue;
            BoneKey key;
            key.node = node;
            key.frame = timeline.startFrame + static_cast<i64>(k);
            key.eulerDeg = Vec3{values[k * ch], values[k * ch + 1], values[k * ch + 2]};
            key.hasTranslation = hips;
            if (hips) key.translation = Vec3{values[k * ch + 3], values[k * ch + 4], values[k * ch + 5]};
            out.push_back(key);
        }
    }
}

MocapPreview make_preview(const MocapTimeline& timeline, const HumanoidMap& map, bool rootMotion) {
    MocapPreview pv;
    pv.startFrame = timeline.startFrame;
    std::vector<usize> bones;
    for (usize b = 0; b < N; ++b)
        if (map.joint[b] >= 0) { bones.push_back(b); pv.nodes.push_back(map.joint[b]); }
    const usize frames = timeline.local.size();
    pv.rotation.resize(frames * bones.size());
    for (usize k = 0; k < frames; ++k)
        for (usize c = 0; c < bones.size(); ++c) pv.rotation[k * bones.size() + c] = timeline.local[k][bones[c]];
    if (rootMotion && map.joint[ix(HB::Hips)] >= 0) {
        pv.hipsNode = map.joint[ix(HB::Hips)];
        pv.hips = timeline.hips;
    }
    return pv;
}

namespace {
std::atomic<const MocapPreview*> g_activePreview{nullptr};
}

void set_active_preview(const MocapPreview* preview) noexcept { g_activePreview.store(preview, std::memory_order_release); }

const MocapPreview* active_preview(const std::shared_ptr<const MocapPreview>& candidate) noexcept {
    const MocapPreview* p = candidate.get();
    return p && p == g_activePreview.load(std::memory_order_acquire) ? p : nullptr;
}

void MocapPreview::sample(f64 local, usize column, Quat& rot) const noexcept {
    const u32 n = frames();
    if (n == 0 || column >= nodes.size()) { rot = Quat::identity(); return; }
    const f64 f = std::clamp(local - static_cast<f64>(startFrame), 0.0, static_cast<f64>(n - 1));
    const usize i0 = static_cast<usize>(f);
    const usize i1 = std::min<usize>(i0 + 1, n - 1);
    const f32 s = static_cast<f32>(f - static_cast<f64>(i0));
    rot = Quat::slerp(rotation[i0 * nodes.size() + column], rotation[i1 * nodes.size() + column], s).normalized();
}

Vec3 MocapPreview::sample_hips(f64 local) const noexcept {
    if (hips.empty()) return Vec3{0, 0, 0};
    const f64 f = std::clamp(local - static_cast<f64>(startFrame), 0.0, static_cast<f64>(hips.size() - 1));
    const usize i0 = static_cast<usize>(f);
    const usize i1 = std::min<usize>(i0 + 1, hips.size() - 1);
    return Vec3::lerp(hips[i0], hips[i1], static_cast<f32>(f - static_cast<f64>(i0)));
}

} // namespace aurea::rig
