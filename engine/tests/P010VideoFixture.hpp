#pragma once

#include "aurea/media/VideoTypes.hpp"

#include <array>
#include <vector>

namespace aurea::test {

/// CPU-only P010 fixture: no codec and no synthetic native handle. Coded
/// border/padding differs deliberately from the visible four quadrants.
class P010VideoFrame final : public DecodedFrame {
public:
    inline static constexpr std::array<u32, 4> limitedCodes{64, 313, 697, 940};
    inline static constexpr std::array<u32, 4> fullCodes{0, 277, 777, 1023};

    explicit P010VideoFrame(bool fullRange = false, bool chromatic = false, bool oddStride = false) {
        width = 48; height = 32; cropLeft = cropTop = 8;
        visibleWidth = 32; visibleHeight = 16;
        format = PixelFormat::P010; color.bitDepth = 10;
        color.fullRange = fullRange; color.matrix = YCbCrMatrix::BT709;
        color.transfer = TransferFunction::SRGB; color.primaries = ColorPrimaries::BT709;
        // A one-byte offset exercises byte loads, not u16 casts. The half-only
        // case also accepts odd padding, which raw backend uploads reject.
        strides[0] = width * 2 + (oddStride ? 3 : 12); strides[1] = width * 2 + (oddStride ? 7 : 20);
        yBytes.assign(1 + static_cast<usize>(strides[0]) * height, 0xDD);
        uvBytes.assign(1 + static_cast<usize>(strides[1]) * (height / 2), 0xEE);
        planes[0] = yBytes.data() + 1; planes[1] = uvBytes.data() + 1; planeCount = 2;
        for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
            const bool visible = inside(x, y);
            const u32 quadrant = visible ? (x - cropLeft >= visibleWidth / 2 ? 1u : 0u)
                + (y - cropTop >= visibleHeight / 2 ? 2u : 0u) : 0;
            write(yBytes.data() + 1 + y * strides[0] + x * 2, visible ? (fullRange ? fullCodes : limitedCodes)[quadrant] : 460);
        }
        for (u32 y = 0; y < height / 2; ++y) for (u32 x = 0; x < width / 2; ++x) {
            const bool visible = inside(x * 2, y * 2);
            const u32 quadrant = visible ? (x * 2 - cropLeft >= visibleWidth / 2 ? 1u : 0u)
                + (y * 2 - cropTop >= visibleHeight / 2 ? 2u : 0u) : 0;
            const u32 cb = visible ? (chromatic ? 380 + quadrant * 73 : 512) : 16;
            const u32 cr = visible ? (chromatic ? 650 - quadrant * 57 : 512) : 16;
            write(uvBytes.data() + 1 + y * strides[1] + x * 4, cb);
            write(uvBytes.data() + 1 + y * strides[1] + x * 4 + 2, cr);
        }
    }

    std::vector<u8> yBytes, uvBytes;

private:
    bool inside(u32 x, u32 y) const {
        return x >= cropLeft && x < cropLeft + visibleWidth && y >= cropTop && y < cropTop + visibleHeight;
    }
    static void write(u8* out, u32 code) {
        const u32 word = code << 6;
        out[0] = static_cast<u8>(word); out[1] = static_cast<u8>(word >> 8);
    }
};

} // namespace aurea::test
