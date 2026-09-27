#pragma once

#include "aurea/tracking/CameraTracker.hpp"
#include <array>

namespace aurea::tracking {

/// Source-pixel -> destination-pixel projective transform, row major.
struct Homography {
    std::array<f64, 9> m{1,0,0, 0,1,0, 0,0,1};
    [[nodiscard]] Vec2 project(Vec2 p) const noexcept;
    [[nodiscard]] bool inverse(Homography& out) const noexcept;
    [[nodiscard]] Homography operator*(const Homography& rhs) const noexcept;
};

enum class MotionModel : u32 { Auto, Position, Similarity, Perspective };
struct MotionEstimate {
    Homography transform;
    std::vector<u8> inlier;
    u32 count = 0;
    f32 rms = 0, confidence = 0;
    bool valid = false;
};
/// Unit square -> ordered TL/TR/BR/BL quad. Rejects folded/degenerate quads.
[[nodiscard]] bool quad_map(const std::array<Vec2, 4>& corners, Homography& out) noexcept;
/// Robust consensus followed by an inlier fit. Does not accept a fit supported
/// only by its minimal random sample. Coordinates are analysis pixels.
[[nodiscard]] MotionEstimate estimate_motion(const std::vector<Vec2>& from,
    const std::vector<Vec2>& to, MotionModel model, f32 threshold = 2.0f);

struct MotionFrame {
    Homography path; // reference -> current frame
    f32 confidence = 0, rms = 0;
    u32 inliers = 0;
    bool valid = false;
};
/// Optional reference-space polygon limits analysis to one tracked surface.
/// An empty polygon measures global camera motion for stabilization.
[[nodiscard]] std::vector<MotionFrame> estimate_path(const Tracks2D& tracks,
    MotionModel model, const std::vector<Vec2>& polygon = {},
    const std::atomic<bool>* cancel = nullptr);

enum class CropMode : u32 { None, Static, Dynamic };
struct StabilizationOptions {
    bool lock = false;
    f32 smoothSeconds = 0.5f;
    f32 maxScale = 1.15f;
    f32 strength = 1.0f;
    CropMode crop = CropMode::Static;
};
struct StabilizedFrame {
    Homography correction; // current frame -> stabilized output
    f32 scale = 1, strength = 1;
    bool valid = false;
};
/// Smooths a measured trajectory and computes its inverse correction. Crop is
/// bounded; if necessary the correction is reduced instead of exceeding the
/// chosen zoom. Dynamic crop anticipates changes to avoid frame-wise pumping.
[[nodiscard]] std::vector<StabilizedFrame> stabilize_path(
    const std::vector<MotionFrame>& path, u32 width, u32 height, f64 fps,
    const StabilizationOptions& options);

} // namespace aurea::tracking
