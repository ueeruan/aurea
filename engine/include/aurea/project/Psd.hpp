#pragma once
#include "aurea/core/Result.hpp"
#include <span>
#include <string>
#include <vector>

namespace aurea::psd {
// Layer pixels are straight RGBA8. Photoshop-only paint/text/style data is
// rasterized by Photoshop in these channels; editing happens on Aurea layers.
struct Layer {
    std::string name;
    i32 left = 0, top = 0;
    u32 width = 0, height = 0;
    i32 parent = -1;
    bool group = false, visible = true;
    f32 opacity = 1;
    BlendMode blend = BlendMode::Normal;
    std::vector<u8> rgba;
};
struct Document {
    u32 width = 0, height = 0;
    std::vector<Layer> layers; // Photoshop order: front to back.
    bool approximated = false;
};
inline constexpr usize kMaxFileBytes = 128u * 1024u * 1024u;
inline constexpr usize kMaxDecodedBytes = 128u * 1024u * 1024u;
[[nodiscard]] Status read(std::span<const u8> bytes, Document& out);
}
