#pragma once
#include <cmath>
#include <cstdint>
namespace aurea::windows {
// Every temporal item shares this origin: ruler, clips, keys, markers and playhead.
struct TimelineGeometry {
 double origin = 0, scrollFrames = 0, pixelsPerFrame = 4;
 double x(double frame) const { return origin + (frame - scrollFrames) * pixelsPerFrame; }
 std::int64_t frame(double px) const { return std::llround(scrollFrames + (px-origin)/pixelsPerFrame); }
 double key_x(std::int64_t local, std::int64_t start, std::int64_t offset) const { return x(double(local + start - offset)); }
 std::int64_t key_time(double px, std::int64_t start, std::int64_t offset) const { return frame(px) - start + offset; }
 void zoom_at(double px, double next) { const double anchor = scrollFrames + (px-origin)/pixelsPerFrame; pixelsPerFrame=next; scrollFrames=anchor-(px-origin)/pixelsPerFrame; }
};
}
