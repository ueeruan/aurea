#pragma once
#include "aurea/timeline/Layer.hpp"
#include <array>

namespace aurea::tracking {
// Caller holds Engine's model mutex. A live camera can override its recorded
// transform while preparing a preview snapshot, without editing any saved key.
// Offline export never enters this scope and evaluates ordinary project tracks.
class RealCameraPreviewScope final {
public:
    explicit RealCameraPreviewScope(Layer* camera) noexcept {
        if (!camera) return;
        const TrackProperty properties[]{TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ,
                                         TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ};
        for (u32 i = 0; i < 6; ++i) {
            entries_[i].track = camera->tracks.find(properties[i]);
            if (Track* track = entries_[i].track) {
                entries_[i].expressionEnabled = track->expressionEnabled;
                entries_[i].keys.swap(track->keys);
                track->expressionEnabled = false;
            }
        }
    }
    ~RealCameraPreviewScope() {
        for (auto& entry : entries_) if (entry.track) {
            entry.keys.swap(entry.track->keys);
            entry.track->expressionEnabled = entry.expressionEnabled;
        }
    }
    RealCameraPreviewScope(const RealCameraPreviewScope&) = delete;
    RealCameraPreviewScope& operator=(const RealCameraPreviewScope&) = delete;
private:
    struct Entry { Track* track = nullptr; std::vector<Keyframe> keys; bool expressionEnabled = false; };
    std::array<Entry, 6> entries_{};
};
} // namespace aurea::tracking
