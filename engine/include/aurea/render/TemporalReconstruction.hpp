#pragma once

#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/ShaderLibrary.hpp"

namespace aurea {

// Preview reprojection in world space for static geometry or in model space
// when all visible geometry shares one animated transform and a fixed pose.
class TemporalReconstruction {
public:
    struct Frame {
        u64 composition = 0;
        u64 revision = 0;
        u64 quality = 0;
        i64 time = 0;
        bool modelSpace = false;
        // Matrices map the selected reprojection space to/from clip space.
        Mat4 viewProjection = Mat4::identity();
        Mat4 inverseJitteredViewProjection = Mat4::identity();
        Vec2 jitterPixels{};
    };

    [[nodiscard]] bool admit(GPUBackend& gpu, ShaderLibrary& shaders,
                             u32 width, u32 height, u64 budgetBytes) noexcept;
    [[nodiscard]] FGTexture build(FrameGraph& graph, ShaderLibrary& shaders,
                                  FGTexture color, FGTexture depth, const Frame& frame) noexcept;
    [[nodiscard]] bool can_reuse(const Frame& frame) const noexcept;
    // Publish only after the entire graph was successfully submitted.
    void complete(bool success) noexcept;
    // Native destruction follows backend fence retirement. nullptr forgets
    // invalid handles after device loss without calling the lost backend.
    void clear(GPUBackend* gpu) noexcept;
    [[nodiscard]] u64 bytes() const noexcept { return bytes_; }
    [[nodiscard]] bool pending() const noexcept { return pending_; }
    [[nodiscard]] static Vec2 jitter(i64 frame) noexcept;
    static void warmup_pipelines(const GPUCapabilities& caps, std::vector<PipelineKey>& keys);

private:
    TextureHandle colors_[2]{}, depths_[2]{};
    PipelineHandle resolve_{}, saveDepth_{};
    TextureDesc colorDesc_{}, depthDesc_{};
    Frame previous_{}, pendingFrame_{};
    u64 bytes_ = 0;
    u32 previousSlot_ = 0;
    bool valid_ = false, pending_ = false;
};

} // namespace aurea
