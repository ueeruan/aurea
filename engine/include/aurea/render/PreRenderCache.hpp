#pragma once
#include "aurea/core/Result.hpp"
#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aurea {
// App-owned temporary composition frames, before zoom/checkerboard/output.
// Pixels are bit-exact little-endian RGBA16F linear premultiplied: alpha/HDR
// do not pass through a video encoder or an 8-bit image conversion.
class PreRenderCache {
public:
    struct Key {
        u64 projectSession = 0, composition = 0, modelRevision = 0;
        u32 width = 0, height = 0, fpsNumerator = 30, fpsDenominator = 1, renderVersion = 1;
        bool operator==(const Key&) const = default;
    };
    struct Limits {
        u64 diskBytes = 8ull << 30; // cumulative private namespace cap; hard-clamped
        u64 reserveFreeBytes = 256ull << 20;
        u64 memoryBytes = 16ull << 20;
        u32 residentFrames = 2; // clamped to 1..2, including the active read
    };
    struct Stats {
        u64 token = 0, framesTotal = 0, framesWritten = 0, diskBytes = 0;
        // Vector bytes plus a conservative fixed compressor/read workspace;
        // excludes source span, caller's moved output and renderer/GPU memory.
        u64 residentBytes = 0, workingBytes = 0, peakOwnedBytes = 0;
        u32 pendingReads = 0, residentFrames = 0;
        bool complete = false;
        Errc lastError = Errc::Ok;
        char errorDetail[192]{}; // owned copy, never a worker's Status pointer
    };
    PreRenderCache();
    ~PreRenderCache();
    PreRenderCache(const PreRenderCache&) = delete;
    PreRenderCache& operator=(const PreRenderCache&) = delete;

    // begin/write/finish are worker operations (filesystem/compression).
    // direction must be +1 or -1; write exactly one frame in that order.
    // Root directory must be the app's private cache, never project/media.
    // Worker admission reclaims marked orphan sessions with no OS lease.
    // Live instances and unrecognized directories are retained and counted.
    // Busy means prior leases/cleanup are pending; caller applies a finite
    // admission deadline. Destroy only after the producer has returned.
    [[nodiscard]] Result<u64> begin(const std::string& cacheRoot, Key key,
                                   u64 frames, i32 direction, Limits limits) noexcept;
    [[nodiscard]] Status write(u64 token, u64 frame, std::span<const u16> rgba,
                              const std::atomic<bool>* cancel = nullptr) noexcept;
    [[nodiscard]] Status finish(u64 token) noexcept;
    // request/take do no I/O or waits. At most two reads can be outstanding.
    // NotFound means not ready; Busy means retry on a later render tick.
    [[nodiscard]] Status request(u64 token, u64 frame) noexcept;
    [[nodiscard]] Status take(u64 token, u64 frame, std::vector<u16>& rgba) noexcept;
    // First invalidates the token atomically, with no lock/join/filesystem I/O.
    // Reader retires only this class's unique private directory after leases end.
    void invalidate() noexcept;
    [[nodiscard]] bool matches(u64 token, const Key& key) const noexcept;
    [[nodiscard]] Stats stats() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace aurea
