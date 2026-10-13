#pragma once
#include "aurea/render/Renderer.hpp"
#include <condition_variable>

namespace aurea {
// A session owns every temporal render history and source decoder. Only the
// immutable composition and the existing compositor implementation are shared.
class OfflineFrameRenderer final {
public:
    ~OfflineFrameRenderer() { shutdown(); }
    Status initialize(GPUBackend& gpu, const EffectRegistry& effects,
                      VideoSourceFactory* sources, MemoryManager* memory,
                      u64 transientBudget, u64 fenceTimeoutNs) noexcept;
    Renderer& compositor() noexcept { return renderer_; }
    Status render(const Project& project, CompositionId composition,
                  FrameIndex time, const OffscreenTarget& target,
                  const ImagePixels* (*images)(void*, AssetId), void* imageContext,
                  const RenderSettings& settings, u64& submittedFrame) noexcept;
    void shutdown() noexcept;
    // Transfer opens/decoders to their owner retirement worker and poll only.
    // The caller must retain this renderer/factory until this returns true.
    [[nodiscard]] bool retire_sources() noexcept;
    void detach_device() noexcept; // Invalidates handles without joining a platform opener.
    void forget_device() noexcept;
private:
    Status wait_gpu(u64 frame, const std::atomic<bool>* cancel) noexcept;
    static void source_ready(void* context) noexcept;
    GPUBackend* gpu_ = nullptr;
    Renderer renderer_;
    MediaManager media_;
    FrameSnapshot snapshot_;
    u64 sequence_ = 0, fenceTimeoutNs_ = 120'000'000'000ull;
    std::atomic<u64> generation_{0};
    std::mutex wakeMutex_;
    std::condition_variable wake_;
};
} // namespace aurea
