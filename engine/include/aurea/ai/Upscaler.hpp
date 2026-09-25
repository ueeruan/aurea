#pragma once

#include "aurea/core/Result.hpp"
#include <atomic>
#include <memory>

namespace aurea::ai {

/// Offline SDR RGB8 inference. One instance belongs to one processing worker.
/// The bundled model is Real-ESRGAN animevideov3, trained for animation.
class Upscaler {
public:
    struct Tile {
        const u8* rgb = nullptr;
        u32 x = 0, y = 0, width = 0, height = 0, stride = 0;
        f32 progress = 0;
    };
    /// Output coordinates are pixels of the upscaled image. Bytes are valid
    /// only inside the callback. Returning false cancels further processing.
    using TileCallback = bool (*)(void* context, const Tile& tile);

    Upscaler() noexcept;
    ~Upscaler();
    Upscaler(const Upscaler&) = delete;
    Upscaler& operator=(const Upscaler&) = delete;
    /// Output scales 2 or 4 (2x uses upstream's neural 4x + bicubic output).
    /// Threads 1..4, input tile side 32..128.
    [[nodiscard]] Status load(u32 scale, u32 threads = 2, u32 tileSize = 64);
    [[nodiscard]] Status run(const u8* rgb, u32 width, u32 height, u32 stride,
                             const std::atomic<bool>& cancel, TileCallback callback,
                             void* context);
    [[nodiscard]] u32 scale() const noexcept;
    /// Activation/workspace peak from the most recent run (excludes weights).
    [[nodiscard]] u64 peak_working_bytes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aurea::ai
