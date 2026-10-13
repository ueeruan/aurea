#include "aurea/Engine.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"

namespace aurea {
void Engine::real_camera_end_locked(bool pause) noexcept {
    if (realCameraState_.recording) {
        history_.end_group();
        realCameraState_.recording = false;
    }
    if (pause && project_) {
        Command command{};
        command.type = CommandType::PlaybackPause;
        (void)apply_command_internal(command, nullptr, false);
    }
    realCameraPlaybackStarted_ = false;
}
Status Engine::real_camera_start(u64 packedLayer) noexcept {
    std::lock_guard lock(modelMutex_);
    drain_commands_locked();
    auto* comp = project_ ? current_composition() : nullptr;
    const LayerId id = LayerId::unpack(packedLayer);
    const Layer* layer = comp ? comp->layer(id) : nullptr;
    if (!layer || layer->kind != LayerKind::Camera || layer->locked) return Errc::InvalidArgument;
    real_camera_end_locked(true);
    realCameraComp_ = project_->timeline().current();
    realCameraLayer_ = id;
    const FrameIndex local = layer->local_time(playback_.current());
    const auto value = [&](TrackProperty p, f32 fallback) { return layer->tracks.sample_or(p, local, fallback); };
    const Vec3 position{value(TrackProperty::PositionX, layer->transform.position.x),
                        value(TrackProperty::PositionY, layer->transform.position.y),
                        value(TrackProperty::PositionZ, layer->transform.position.z)};
    const Vec3 rotation{value(TrackProperty::RotationX, layer->transform.rotation.x),
                        value(TrackProperty::RotationY, layer->transform.rotation.y),
                        value(TrackProperty::RotationZ, layer->transform.rotation.z)};
    realCameraPose_.reset(position, rotation, scene3d::scene_pixels_per_meter(comp->height()));
    realCameraState_ = {true, false, -1, false};
    request_render();
    return OkStatus;
}
Status Engine::real_camera_record(bool recording) noexcept {
    std::lock_guard lock(modelMutex_);
    drain_commands_locked();
    auto* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(realCameraLayer_) : nullptr;
    if (!realCameraState_.active || !layer || layer->locked || project_->timeline().current() != realCameraComp_) return Errc::InvalidState;
    if (!recording) {
        real_camera_end_locked(true);
        request_render();
        return OkStatus;
    }
    if (realCameraState_.recording) return OkStatus;
    if (history_.in_group()) return Status{Errc::Busy, "termine o gesto antes de gravar a camera"};
    history_.begin_group("gravar camera real");
    realCameraState_.recording = true;
    realCameraState_.lastFrame = -1;
    realCameraPlaybackStarted_ = false;
    Command command{};
    command.type = CommandType::PlaybackPlay;
    const Status played = apply_command_internal(command, nullptr, false);
    if (!played.ok()) real_camera_end_locked(false);
    request_render();
    return played;
}
Status Engine::real_camera_sample(const f32* pose, u64 timestampNs, bool positional, f32 sensitivity, bool inverted) noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(realCameraLayer_) : nullptr;
    if (!realCameraState_.active || !layer || layer->kind != LayerKind::Camera || layer->locked
        || project_->timeline().current() != realCameraComp_) {
        real_camera_end_locked(false);
        realCameraState_.active = false;
        return Errc::InvalidState;
    }
    if (!realCameraPose_.sample(pose, timestampNs, positional, sensitivity, inverted)) return Errc::InvalidArgument;
    realCameraState_.positional = realCameraPose_.positional();
    return real_camera_commit_locked();
}
Status Engine::real_camera_hold() noexcept {
    std::lock_guard lock(modelMutex_);
    if (!realCameraPose_.ready()) return Errc::InvalidState;
    return real_camera_commit_locked();
}
Status Engine::real_camera_commit_locked() noexcept {
    auto* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(realCameraLayer_) : nullptr;
    if (!realCameraState_.active || !layer || layer->kind != LayerKind::Camera || layer->locked
        || project_->timeline().current() != realCameraComp_) {
        real_camera_end_locked(false);
        realCameraState_.active = false;
        return Errc::InvalidState;
    }
    const bool positional = realCameraPose_.positional();
    if (realCameraState_.recording) {
        if (playback_.playing()) realCameraPlaybackStarted_ = true;
        else if (realCameraPlaybackStarted_ && !previewBuffering_) real_camera_end_locked(false);
    }
    const FrameIndex frame = playback_.current();
    const FrameIndex local = layer->local_time(frame);
    const Vec3 rotation = realCameraPose_.rotation(), position = realCameraPose_.position();
    if (realCameraState_.recording) {
        if (realCameraState_.lastFrame == frame.value || !layer->contains_time(frame)) return OkStatus;
        history_.before_mutation(*comp, realCameraComp_, "gravar camera real");
        const TrackProperty properties[]{TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ,
                                         TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ};
        const f32 values[]{rotation.x, rotation.y, rotation.z, position.x, position.y, position.z};
        for (u32 i = 0; i < (positional ? 6u : 3u); ++i) {
            // Create the track and the first key. Editing an absent key was the
            // Android failure; both bridges now execute this same native path.
            Track& track = layer->tracks.get_or_create(properties[i]);
            track.expressionEnabled = false;
            track.set(local, values[i], Interpolation::Linear);
        }
        realCameraState_.lastFrame = frame.value;
    } else {
        layer->transform.rotation = rotation;
        const TrackProperty properties[]{TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ};
        const f32 values[]{rotation.x, rotation.y, rotation.z};
        for (u32 i = 0; i < 3; ++i) {
            Track* track = layer->tracks.find(properties[i]);
            if (track && !track->driven()) track->set_static(values[i]);
        }
        if (positional) {
            layer->transform.position = position;
            const TrackProperty positions[]{TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ};
            const f32 valuesPosition[]{position.x, position.y, position.z};
            for (u32 i = 0; i < 3; ++i) {
                Track* track = layer->tracks.find(positions[i]);
                if (track && !track->driven()) track->set_static(valuesPosition[i]);
            }
        }
    }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return OkStatus;
}
Status Engine::real_camera_recenter() noexcept {
    std::lock_guard lock(modelMutex_);
    if (!realCameraState_.active) return Errc::InvalidState;
    realCameraPose_.recenter();
    return OkStatus;
}
void Engine::real_camera_stop() noexcept {
    std::lock_guard lock(modelMutex_);
    real_camera_end_locked(true);
    realCameraState_.active = false;
    request_render();
}
Engine::RealCameraStatus Engine::real_camera_status() noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    const Layer* layer = comp ? comp->layer(realCameraLayer_) : nullptr;
    if (realCameraState_.active && (!layer || layer->kind != LayerKind::Camera || project_->timeline().current() != realCameraComp_)) {
        real_camera_end_locked(false);
        realCameraState_.active = false;
    }
    if (realCameraState_.recording) {
        if (playback_.playing()) realCameraPlaybackStarted_ = true;
        else if (realCameraPlaybackStarted_ && !previewBuffering_) real_camera_end_locked(false);
    }
    return realCameraState_;
}
Layer* Engine::real_camera_preview_layer_locked() noexcept {
    if (!realCameraState_.active || realCameraState_.recording || !realCameraPose_.ready() || !project_
        || project_->timeline().current() != realCameraComp_) return nullptr;
    auto* comp = current_composition();
    Layer* layer = comp ? comp->layer(realCameraLayer_) : nullptr;
    return layer && layer->kind == LayerKind::Camera ? layer : nullptr;
}
} // namespace aurea
