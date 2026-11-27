#pragma once

namespace aurea {
class EffectRegistry;

namespace tracking {
// Keep the original catalog IDs so existing favorites still open these tools.
inline constexpr char kCameraTrackerEffect[] = "aurea.tool.camera_track";
inline constexpr char kMotionTrackerEffect[] = "aurea.tool.tracking";

void register_tracker_effects(EffectRegistry& registry);
} // namespace tracking
} // namespace aurea
