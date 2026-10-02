#pragma once
#include "aurea/core/Math.hpp"
#include "aurea/core/Result.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace aurea {
struct CubeLut {
    u32 size = 0, dimensions = 3;
    Vec3 domainMin{0, 0, 0}, domainMax{1, 1, 1};
    std::vector<Vec4> values; // Red changes fastest, then green, then blue.
    std::string title;
};
inline constexpr usize kMaxCubeFileBytes = 32u << 20;
[[nodiscard]] Result<CubeLut> parse_cube_lut(std::string_view text);
[[nodiscard]] Result<CubeLut> read_cube_lut(const std::string& path);
}
