#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/tracking/RealCameraCapture.hpp"
#include "aurea/tracking/RealCameraPreviewScope.hpp"
#include <array>
#include <limits>

using namespace aurea;
namespace {
std::array<f32, 16> camera_pose(Vec3 position = {}, Vec3 rotation = {}) {
    const Mat4 m = Mat4::translation(position) * Mat4::from_quat(Quat::from_euler_zyx(rotation.x * kDeg2Rad, rotation.y * kDeg2Rad, rotation.z * kDeg2Rad));
    std::array<f32, 16> out{};
    for (u32 c = 0; c < 4; ++c) for (u32 r = 0; r < 4; ++r) out[c * 4 + r] = (&m.col[c].x)[r];
    return out;
}
void settle(tracking::RealCameraCapture& capture, const std::array<f32, 16>& pose, u64& clock, bool positional = true, f32 sensitivity = 1) {
    for (u32 i = 0; i < 30; ++i) AUREA_CHECK(capture.sample(pose.data(), clock += 33'333'333, positional, sensitivity, false));
}
EngineConfig camera_config() {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull << 20;
    cfg.disableAutosave = true;
    return cfg;
}
Layer* camera_layer(Engine& e, u64 id) {
    auto& timeline = e.project()->timeline();
    return timeline.composition(timeline.current())->layer(LayerId::unpack(id));
}
} // namespace

AUREA_TEST(RealCamera, MeasuredForwardBackwardAndUpUseExistingPhysicalSceneScale) {
    tracking::RealCameraCapture capture;
    capture.reset({10, 20, -1200}, {}, 540);
    u64 clock = 1;
    auto origin = camera_pose({2, 3, 4});
    AUREA_CHECK(capture.sample(origin.data(), clock, true, 1, false));
    settle(capture, camera_pose({2, 3, 3.5f}), clock);
    AUREA_CHECK_NEAR(capture.position().z, -930.f, .01f);
    AUREA_CHECK_NEAR(capture.position().x, 10.f, .01f);
    settle(capture, camera_pose({2, 3, 4.5f}), clock);
    AUREA_CHECK_NEAR(capture.position().z, -1470.f, .01f);
    settle(capture, camera_pose({2, 3.5f, 4}), clock);
    AUREA_CHECK_NEAR(capture.position().y, -250.f, .01f);
}
AUREA_TEST(RealCamera, PositionUsesInitialDeviceBasisAndCameraOrientation) {
    tracking::RealCameraCapture capture;
    capture.reset({100, 200, -500}, {0, 90, 0}, 200);
    u64 clock = 1;
    AUREA_CHECK(capture.sample(camera_pose({}, {0, 90, 0}).data(), clock, true, 1, false));
    // Initial phone forward is -X in AR world. Virtual camera forward is +X.
    settle(capture, camera_pose({-.5f, 0, 0}, {0, 90, 0}), clock);
    AUREA_CHECK_NEAR(capture.position().x, 200.f, .01f);
    AUREA_CHECK_NEAR(capture.position().z, -500.f, .01f);
}
AUREA_TEST(RealCamera, EulerWrapAndSensorToARTransitionDoNotJump) {
    tracking::RealCameraCapture capture;
    capture.reset({0, 0, -1000}, {0, 0, 179}, 500);
    u64 clock = 1;
    AUREA_CHECK(capture.sample(camera_pose().data(), clock, false, 1, false));
    settle(capture, camera_pose({}, {0, 0, -4}), clock, false);
    AUREA_CHECK_NEAR(capture.rotation().z, 183.f, .01f);
    const Vec3 before = capture.rotation();
    AUREA_CHECK(capture.sample(camera_pose({30, 10, -20}, {20, 45, 60}).data(), clock += 33'333'333, true, 1, false));
    AUREA_CHECK_NEAR(capture.rotation().z, before.z, .001f);
    AUREA_CHECK_NEAR(capture.position().z, -1000.f, .001f);
    capture.recenter();
    AUREA_CHECK(capture.sample(camera_pose().data(), ++clock, true, 1, false));
    AUREA_CHECK_NEAR(capture.rotation().z, 179.f, .001f);
}
AUREA_TEST(RealCamera, InvalidStaleAndNonRigidSamplesCannotChangePose) {
    tracking::RealCameraCapture capture;
    capture.reset({1, 2, 3}, {}, 200);
    auto m = camera_pose();
    AUREA_CHECK(capture.sample(m.data(), 100, false, 1, false));
    AUREA_CHECK(!capture.sample(m.data(), 99, false, 1, false));
    AUREA_CHECK(!capture.sample(m.data(), 100, false, 1, false));
    m[0] = -1;
    AUREA_CHECK(!capture.sample(m.data(), 101, false, 1, false));
    m[0] = std::numeric_limits<f32>::quiet_NaN();
    AUREA_CHECK(!capture.sample(m.data(), 102, false, 1, false));
    AUREA_CHECK_EQ(capture.position(), (Vec3{1, 2, 3}));
}
AUREA_TEST(RealCamera, FractionalSensitivityIsContinuousAcrossHalfTurn) {
    tracking::RealCameraCapture capture;
    capture.reset({}, {}, 200);
    u64 clock = 1;
    AUREA_CHECK(capture.sample(camera_pose().data(), clock, false, 1.25f, false));
    settle(capture, camera_pose({}, {0, 0, -179}), clock, false, 1.25f);
    const f32 before = capture.rotation().z;
    settle(capture, camera_pose({}, {0, 0, -181}), clock, false, 1.25f);
    AUREA_CHECK_NEAR(capture.rotation().z - before, 2.5f, .01f);
}
AUREA_TEST(RealCamera, AndroidFirstKeysAreCreatedBeforeAnyUICachedPlaybackUpdateAndUndoTogether) {
    Engine e;
    AUREA_CHECK(e.initialize(camera_config()).ok());
    AUREA_CHECK(e.new_project(800, 600, 30, "camera capture").ok());
    const auto added = e.add_camera();
    AUREA_CHECK(added.ok());
    if (!added.ok()) return;
    const u64 id = *added;
    AUREA_CHECK(e.real_camera_start(id).ok());
    AUREA_CHECK(e.real_camera_record(true).ok());
    AUREA_CHECK(e.real_camera_status().recording);
    const auto identity = camera_pose();
    AUREA_CHECK(e.real_camera_sample(identity.data(), 1, false, 1, false).ok());
    const TrackProperty rotations[]{TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ};
    for (auto property : rotations) {
        const Track* track = camera_layer(e, id)->tracks.find(property);
        AUREA_CHECK(track != nullptr);
        if (track) { AUREA_CHECK_EQ(track->keys.size(), usize{1}); AUREA_CHECK_EQ(track->keys[0].time.value, 0); }
    }
    // A second sensor event during the same displayed frame adds no second key.
    AUREA_CHECK(e.real_camera_sample(camera_pose({}, {3, 5, 2}).data(), 33'333'334, false, 1, false).ok());
    e.playback().seek(FrameIndex{1}, monotonic_ns());
    AUREA_CHECK(e.real_camera_sample(camera_pose({}, {3, 5, 2}).data(), 66'666'667, false, 1, false).ok());
    for (auto property : rotations) AUREA_CHECK_EQ(camera_layer(e, id)->tracks.find(property)->keys.size(), usize{2});
    AUREA_CHECK_EQ(e.real_camera_status().lastFrame, i64{1});
    AUREA_CHECK(e.real_camera_record(false).ok());
    AUREA_CHECK(!e.history().in_group());
    Command command{}; command.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(command).ok());
    for (auto property : rotations) AUREA_CHECK(camera_layer(e, id)->tracks.find(property) == nullptr);
    command.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(command).ok());
    for (auto property : rotations) AUREA_CHECK_EQ(camera_layer(e, id)->tracks.find(property)->keys.size(), usize{2});
    e.real_camera_stop();
}
AUREA_TEST(RealCamera, PositionalRecordingCreatesSixOrdinaryTracksAndProjectReplacementEndsSession) {
    Engine e;
    AUREA_CHECK(e.initialize(camera_config()).ok());
    AUREA_CHECK(e.new_project(800, 600, 30, "position capture").ok());
    const auto added = e.add_camera(); AUREA_CHECK(added.ok()); if (!added.ok()) return;
    const u64 id = *added;
    AUREA_CHECK(e.real_camera_start(id).ok()); AUREA_CHECK(e.real_camera_record(true).ok());
    AUREA_CHECK(e.real_camera_sample(camera_pose().data(), 1, true, 1, false).ok());
    e.playback().seek(FrameIndex{1}, monotonic_ns());
    AUREA_CHECK(e.real_camera_sample(camera_pose({0, 0, -.5f}).data(), 100'000'001, true, 1, false).ok());
    AUREA_CHECK_EQ(camera_layer(e, id)->tracks.size(), 6u);
    const Track* z = camera_layer(e, id)->tracks.find(TrackProperty::PositionZ);
    AUREA_CHECK(z && z->keys.size() == 2);
    if (z && z->keys.size() == 2) AUREA_CHECK(z->keys[1].value > z->keys[0].value + 140.f);
    AUREA_CHECK(e.new_project(800, 600, 30, "replacement").ok());
    AUREA_CHECK(!e.real_camera_status().active);
    AUREA_CHECK(!e.real_camera_status().recording);
}
AUREA_TEST(RealCamera, LostTrackingRecordsConstantPoseUntilTrackingReturns) {
    Engine e;
    AUREA_CHECK(e.initialize(camera_config()).ok());
    AUREA_CHECK(e.new_project(800, 600, 30, "lost tracking").ok());
    const auto added = e.add_camera(); AUREA_CHECK(added.ok()); if (!added.ok()) return;
    const u64 id = *added;
    AUREA_CHECK(e.real_camera_start(id).ok()); AUREA_CHECK(e.real_camera_record(true).ok());
    AUREA_CHECK(!e.real_camera_hold().ok());
    AUREA_CHECK(e.real_camera_sample(camera_pose().data(), 1, true, 1, false).ok());
    e.playback().seek(FrameIndex{1}, monotonic_ns());
    AUREA_CHECK(e.real_camera_sample(camera_pose({0, 0, -.5f}).data(), 100'000'001, true, 1, false).ok());
    e.playback().seek(FrameIndex{2}, monotonic_ns()); AUREA_CHECK(e.real_camera_hold().ok());
    e.playback().seek(FrameIndex{3}, monotonic_ns()); AUREA_CHECK(e.real_camera_hold().ok());
    const Track* z = camera_layer(e, id)->tracks.find(TrackProperty::PositionZ);
    AUREA_CHECK(z && z->keys.size() == 4);
    if (z && z->keys.size() == 4) {
        AUREA_CHECK_EQ(z->keys[1].value, z->keys[2].value);
        AUREA_CHECK_EQ(z->keys[2].value, z->keys[3].value);
    }
    AUREA_CHECK(e.real_camera_record(false).ok());
}
AUREA_TEST(RealCamera, LivePreviewAfterRecordingDoesNotEditAnyRecordedKey) {
    Engine e;
    AUREA_CHECK(e.initialize(camera_config()).ok());
    AUREA_CHECK(e.new_project(800, 600, 30, "preview pose").ok());
    const auto added = e.add_camera(); AUREA_CHECK(added.ok()); if (!added.ok()) return;
    const u64 id = *added;
    Layer* camera = camera_layer(e, id);
    Track& track = camera->tracks.get_or_create(TrackProperty::RotationY);
    track.set(FrameIndex{0}, 90); track.set(FrameIndex{1}, 120);
    camera->transform.rotation.y = 15;
    const auto keys = track.keys;
    {
        tracking::RealCameraPreviewScope scope(camera);
        AUREA_CHECK(!track.driven());
        AUREA_CHECK_NEAR(track.value_or(FrameIndex{0}, camera->transform.rotation.y), 15.f, .001f);
    }
    AUREA_CHECK_EQ(track.keys.size(), keys.size());
    AUREA_CHECK_NEAR(track.value_or(FrameIndex{0}, camera->transform.rotation.y), 90.f, .001f);
    for (usize i = 0; i < keys.size(); ++i) {
        AUREA_CHECK_EQ(track.keys[i].time, keys[i].time);
        AUREA_CHECK_EQ(track.keys[i].value, keys[i].value);
        AUREA_CHECK_EQ(track.keys[i].interp, keys[i].interp);
    }
    AUREA_CHECK(track.expressionEnabled);
}
