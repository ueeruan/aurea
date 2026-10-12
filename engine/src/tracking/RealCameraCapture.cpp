#include "aurea/tracking/RealCameraCapture.hpp"

namespace aurea::tracking {
namespace {
Quat conjugate(Quat q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }
Vec3 rotate(Quat q, Vec3 v) noexcept {
    const Quat r = q * Quat{v.x, v.y, v.z, 0} * conjugate(q);
    return {r.x, r.y, r.z};
}
Quat rotation_of(const f32* m) noexcept {
    const f32 trace = m[0] + m[5] + m[10];
    Quat q;
    if (trace > 0) {
        const f32 s = 2 * std::sqrt(trace + 1);
        q = {(m[6] - m[9]) / s, (m[8] - m[2]) / s, (m[1] - m[4]) / s, s * .25f};
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const f32 s = 2 * std::sqrt(1 + m[0] - m[5] - m[10]);
        q = {s * .25f, (m[4] + m[1]) / s, (m[8] + m[2]) / s, (m[6] - m[9]) / s};
    } else if (m[5] > m[10]) {
        const f32 s = 2 * std::sqrt(1 + m[5] - m[0] - m[10]);
        q = {(m[4] + m[1]) / s, s * .25f, (m[9] + m[6]) / s, (m[8] - m[2]) / s};
    } else {
        const f32 s = 2 * std::sqrt(1 + m[10] - m[0] - m[5]);
        q = {(m[8] + m[2]) / s, (m[9] + m[6]) / s, s * .25f, (m[1] - m[4]) / s};
    }
    return q.normalized();
}
f32 nearest(f32 radians, f32 previous) noexcept {
    return radians + std::round((previous - radians) / kTwoPi) * kTwoPi;
}
Vec3 continuous_euler(Quat q, Vec3 previousDegrees) noexcept {
    const Mat4 m = Mat4::from_quat(q);
    const Vec3 p = previousDegrees * kDeg2Rad;
    const f32 y = std::asin(clampf(-m.col[0].z, -1, 1));
    Vec3 a;
    if (std::fabs(std::cos(y)) < 1e-5f) {
        const f32 zMinusSignedX = std::atan2(-m.col[1].x, m.col[1].y);
        a = {p.x, y, zMinusSignedX + (y > 0 ? p.x : -p.x)};
    } else {
        a = {std::atan2(m.col[1].z, m.col[2].z), y, std::atan2(m.col[0].y, m.col[0].x)};
    }
    Vec3 b{a.x + kPi, kPi - a.y, a.z + kPi};
    a = {nearest(a.x, p.x), nearest(a.y, p.y), nearest(a.z, p.z)};
    b = {nearest(b.x, p.x), nearest(b.y, p.y), nearest(b.z, p.z)};
    return ((b - p).length_sq() < (a - p).length_sq() ? b : a) * kRad2Deg;
}
// Right/up/-Z over a Y-up world (AR) -> right/down/+Z over a Y-down world
// (composition): conjugate by 180° X, for rotations and for vectors.
Quat to_composition(Quat q) noexcept { return {q.x, -q.y, -q.z, q.w}; }
Vec3 to_composition(Vec3 v) noexcept { return {v.x, -v.y, -v.z}; }
/// Heading of a composition-space camera: rotation about the vertical (Y).
/// The forward axis, or the right axis when the camera looks straight up/down.
f32 heading_of(Quat q) noexcept {
    const Vec3 f = rotate(q, {0, 0, 1});
    if (f.x * f.x + f.z * f.z > .01f) return std::atan2(f.x, f.z);
    const Vec3 r = rotate(q, {1, 0, 0});
    return std::atan2(-r.z, r.x);
}
bool rigid_pose(const f32* m) noexcept {
    if (!m) return false;
    for (u32 i = 0; i < 16; ++i) if (!std::isfinite(m[i])) return false;
    if (std::fabs(m[3]) > .01f || std::fabs(m[7]) > .01f || std::fabs(m[11]) > .01f || std::fabs(m[15] - 1) > .01f) return false;
    const Vec3 x{m[0], m[1], m[2]}, y{m[4], m[5], m[6]}, z{m[8], m[9], m[10]};
    return std::fabs(x.length_sq() - 1) < .05f && std::fabs(y.length_sq() - 1) < .05f
        && std::fabs(z.length_sq() - 1) < .05f && std::fabs(x.dot(y)) < .05f
        && std::fabs(x.dot(z)) < .05f && std::fabs(y.dot(z)) < .05f && x.cross(y).dot(z) > .95f;
}
} // namespace

void RealCameraCapture::reset(Vec3 position, Vec3 rotationDegrees, f32 pixelsPerMeter) noexcept {
    initialPosition_ = position;
    initialRotation_ = rotationDegrees;
    initialRotationQ_ = Quat::from_euler_zyx(rotationDegrees.x * kDeg2Rad, rotationDegrees.y * kDeg2Rad, rotationDegrees.z * kDeg2Rad);
    pixelsPerMeter_ = std::max(1.0f, pixelsPerMeter);
    recenter();
}
void RealCameraCapture::recenter() noexcept {
    basePosition_ = outputPosition_ = initialPosition_;
    outputRotation_ = initialRotation_;
    baseRotation_ = initialRotationQ_;
    referenceValid_ = false;
    filteredRotation_ = Quat::identity();
    filteredPosition_ = {};
    relativeEuler_ = {};
    walkOffset_ = {};
    lastTimestamp_ = 0;
}
Vec3 RealCameraCapture::deepen(Vec3 world) const noexcept {
    if (std::fabs(depthScale_ - 1) < 1e-4f) return world;
    const f32 h = heading_of(baseRotation_);
    const Vec3 forward{std::sin(h), 0, std::cos(h)};
    return world + forward * ((depthScale_ - 1) * world.dot(forward));
}
bool RealCameraCapture::sample(const f32* m, u64 timestampNs, bool positional, f32 sensitivity, bool inverted) noexcept {
    const u64 previous = lastTimestamp_;
    const bool continuing = referenceValid_ && positional == positional_ && previous;
    if (!sample_pose(m, timestampNs, positional, sensitivity, inverted)) return false;
    // Andar: soma ao longo do rumo da câmera (no plano do chão, como uma
    // pessoa andando), por cima do que o AR mediu. O reposicionamento da
    // referência já embutiu o andado na base.
    if (continuing && walkSpeed_ != 0) {
        const f32 dt = static_cast<f32>(std::min<u64>(timestampNs - previous, 100'000'000)) * 1e-9f;
        const f32 h = heading_of(Quat::from_euler_zyx(outputRotation_.x * kDeg2Rad, outputRotation_.y * kDeg2Rad, outputRotation_.z * kDeg2Rad));
        walkOffset_ += Vec3{std::sin(h), 0, std::cos(h)} * (walkSpeed_ * dt * pixelsPerMeter_);
    }
    if (positional) { if (continuing) outputPosition_ += walkOffset_; }
    else outputPosition_ = basePosition_ + walkOffset_;
    return true;
}
bool RealCameraCapture::sample_pose(const f32* m, u64 timestampNs, bool positional, f32 sensitivity, bool inverted) noexcept {
    if (!timestampNs || !std::isfinite(sensitivity) || sensitivity < .25f || sensitivity > 3 || !rigid_pose(m)) return false;
    if (lastTimestamp_ && timestampNs <= lastTimestamp_) return false;
    const Quat q = rotation_of(m);
    const Vec3 t{m[12], m[13], m[14]};
    if (!referenceValid_ || positional != positional_) {
        // Sensor/AR coordinate systems differ. Switching sources rebases at
        // the displayed pose, retaining the camera's position and orientation.
        if (referenceValid_) {
            basePosition_ = outputPosition_;
            walkOffset_ = {};
            baseRotation_ = Quat::from_euler_zyx(outputRotation_.x * kDeg2Rad, outputRotation_.y * kDeg2Rad, outputRotation_.z * kDeg2Rad);
        }
        referenceRotation_ = q;
        referencePosition_ = t;
        filteredRotation_ = Quat::identity();
        filteredPosition_ = {};
        relativeEuler_ = {};
        referenceValid_ = true;
        positional_ = positional;
        lastTimestamp_ = timestampNs;
        return true;
    }
    const f32 dt = static_cast<f32>(std::min<u64>(timestampNs - lastTimestamp_, 100'000'000)) * 1e-9f;
    lastTimestamp_ = timestampNs;
    if (std::fabs(sensitivity - 1) < .02f && !inverted) {
        // 100 %: move the phone exactly as when filming. The phone's motion
        // since the take started is measured in the WORLD (gravity is the
        // vertical) and applied to the virtual camera in the scene's world:
        // a pan turns about the scene's vertical and a tilt about the camera's
        // horizontal axis, even when the take starts with the phone tilted
        // (the local-axis delta rolled the horizon there). The heading offset
        // maps the phone's forward onto the virtual camera's; the virtual
        // framing at the start is kept. No smoothing: each pose lands as measured.
        const Quat phone = to_composition(q), reference = to_composition(referenceRotation_);
        const f32 offset = heading_of(baseRotation_) - heading_of(reference);
        const Quat heading = Quat::from_axis_angle({0, 1, 0}, offset);
        const Quat world = (heading * phone * conjugate(reference) * conjugate(heading)).normalized();
        outputRotation_ = continuous_euler((world * baseRotation_).normalized(), outputRotation_);
        if (positional) {
            const Vec3 moved = to_composition(t - referencePosition_);
            outputPosition_ = basePosition_ + deepen(rotate(heading, moved * pixelsPerMeter_));
        }
        return true;
    }
    const f32 alpha = 1 - std::exp(-dt / .025f);
    const Quat delta = (conjugate(referenceRotation_) * q).normalized();
    // Right/up/-Z (AR) -> right/down/+Z (composition): conjugate by 180° X.
    const Quat converted{delta.x, -delta.y, -delta.z, delta.w};
    filteredRotation_ = Quat::slerp(filteredRotation_, converted, alpha).normalized();
    // Scale continuous pan/tilt/roll, not a quaternion's canonical +/-180°
    // axis-angle: a fractional sensitivity otherwise jumps at that boundary.
    relativeEuler_ = continuous_euler(filteredRotation_, relativeEuler_);
    const Vec3 scaledAngles = relativeEuler_ * (kDeg2Rad * sensitivity * (inverted ? -1 : 1));
    const Quat scaled = Quat::from_euler_zyx(scaledAngles.x, scaledAngles.y, scaledAngles.z);
    outputRotation_ = continuous_euler((baseRotation_ * scaled).normalized(), outputRotation_);
    if (positional) {
        const Vec3 local = rotate(conjugate(referenceRotation_), t - referencePosition_);
        const Vec3 convertedPosition{local.x, -local.y, -local.z};
        filteredPosition_ += (convertedPosition - filteredPosition_) * alpha;
        outputPosition_ = basePosition_ + deepen(rotate(baseRotation_, filteredPosition_ * (pixelsPerMeter_ * sensitivity)));
    }
    return true;
}
} // namespace aurea::tracking
