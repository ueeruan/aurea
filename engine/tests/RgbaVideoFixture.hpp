#pragma once

#include "aurea/media/VideoTypes.hpp"

#include <array>
#include <vector>

namespace aurea::test {

// Padded coded rows and a green coded border surrounding four distinct visible
// colors. The native handle is deliberately unimportable on a host backend.
class RgbaVideoFrame final : public DecodedFrame {
public:
    inline static constexpr std::array<std::array<u8, 3>, 4> colors{{
        {{128, 32, 16}}, {{16, 192, 32}}, {{32, 16, 224}}, {{160, 128, 96}}
    }};

    explicit RgbaVideoFrame(bool native = false) {
        width = 48; height = 32;
        cropLeft = cropTop = 8;
        visibleWidth = 32; visibleHeight = 16;
        format = PixelFormat::RGBA8;
        rowStride = width * 4 + 12;
        bytes.assign(static_cast<usize>(rowStride) * height, 0);
        for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
            auto* pixel = bytes.data() + static_cast<usize>(y) * rowStride + x * 4;
            pixel[1] = pixel[3] = 255;
            if (x >= cropLeft && x < cropLeft + visibleWidth && y >= cropTop && y < cropTop + visibleHeight) {
                const u32 quadrant = (x - cropLeft >= visibleWidth / 2 ? 1u : 0u)
                    + (y - cropTop >= visibleHeight / 2 ? 2u : 0u);
                for (u32 c = 0; c < 3; ++c) pixel[c] = colors[quadrant][c];
            }
        }
        if (native) hardwareBuffer = this;
        else publish_planes();
    }

    ~RgbaVideoFrame() override { if (deaths) ++*deaths; }

    bool prepare_cpu_planes() noexcept override {
        ++prepareCalls;
        if (!allowCpuFallback) return false;
        publish_planes();
        return true;
    }

    std::vector<u8> bytes;
    u32 rowStride = 0;
    u32 prepareCalls = 0;
    bool allowCpuFallback = true;
    u32* deaths = nullptr;

private:
    void publish_planes() noexcept {
        planes[0] = bytes.data(); strides[0] = rowStride; planeCount = 1;
    }
};

} // namespace aurea::test
