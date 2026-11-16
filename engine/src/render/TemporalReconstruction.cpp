#include "aurea/render/TemporalReconstruction.hpp"

#include <limits>

namespace aurea {

Vec2 TemporalReconstruction::jitter(i64 frame) noexcept {
    // Eight centered Halton samples. Absolute timeline indices make scrubbing
    // repeatable and keep the preview clock independent of submission count.
    constexpr Vec2 samples[] = {{0.f, -1.f / 6.f}, {-.25f, 1.f / 6.f},
        {.25f, -7.f / 18.f}, {-.375f, -1.f / 18.f}, {.125f, 5.f / 18.f},
        {-.125f, -5.f / 18.f}, {.375f, 1.f / 18.f}, {-.4375f, 7.f / 18.f}};
    const u32 index = static_cast<u32>((frame % 8 + 8) % 8);
    return samples[index];
}

void TemporalReconstruction::clear(GPUBackend* gpu) noexcept {
    for (auto& color : colors_) {
        if (gpu && color.valid()) gpu->destroy_texture(color);
        color = {};
    }
    for (auto& depth : depths_) {
        if (gpu && depth.valid()) gpu->destroy_texture(depth);
        depth = {};
    }
    bytes_ = 0;
    pending_ = valid_ = false;
    previousSlot_ = 0;
}

bool TemporalReconstruction::admit(GPUBackend& gpu, ShaderLibrary& shaders,
    u32 width, u32 height, u64 budgetBytes) noexcept {
    const auto& caps = gpu.capabilities();
    if (pending_ || !width || !height || width > caps.maxTexture2D || height > caps.maxTexture2D || !caps.rgba16fRenderable
        || !caps.rgba16fFilterable || !caps.depth32fSampled) return false;
    const u64 pixels = static_cast<u64>(width) * height;
    constexpr u64 bytesPerPixel = 2 * (8 + 4);
    if (pixels > budgetBytes / bytesPerPixel) {
        clear(&gpu);
        return false;
    }
    if (bytes_ && colorDesc_.width == width && colorDesc_.height == height) return true;
    clear(&gpu);
    // Both render formats are checked against actual backend creation. A failed
    // format/pipeline leaves the ordinary spatial preview available.
    if (!shaders.pipeline(PipelineKey::fullscreen(ShaderId::scene3d_post_temporal_frag, SurfaceFormat::RGBA16F))
        || !shaders.pipeline(PipelineKey::fullscreen(ShaderId::scene3d_post_temporal_depth_frag, SurfaceFormat::R32F))) return false;
    colorDesc_ = {};
    colorDesc_.width = width; colorDesc_.height = height;
    colorDesc_.format = SurfaceFormat::RGBA16F;
    colorDesc_.sampled = colorDesc_.renderTarget = true;
    colorDesc_.transferSrc = true;
    colorDesc_.debugName = "preview-temporal-color";
    depthDesc_ = colorDesc_;
    depthDesc_.format = SurfaceFormat::R32F;
    depthDesc_.debugName = "preview-temporal-depth";
    for (u32 slot = 0; slot < 2; ++slot) {
        const auto color = gpu.create_texture(colorDesc_);
        if (!color) { clear(&gpu); return false; }
        colors_[slot] = *color;
        const auto depth = gpu.create_texture(depthDesc_);
        if (!depth) { clear(&gpu); return false; }
        depths_[slot] = *depth;
    }
    bytes_ = pixels * bytesPerPixel;
    return true;
}

bool TemporalReconstruction::can_reuse(const Frame& frame) const noexcept {
    return valid_ && previous_.time != std::numeric_limits<i64>::max()
        && frame.time == previous_.time + 1 && frame.composition == previous_.composition
        && frame.revision == previous_.revision && frame.quality == previous_.quality;
}

FGTexture TemporalReconstruction::build(FrameGraph& graph, ShaderLibrary& shaders,
    FGTexture color, FGTexture depth, const Frame& frame) noexcept {
    if (!bytes_ || pending_ || !color.valid() || !depth.valid()) return color;
    const auto resolve = shaders.pipeline(PipelineKey::fullscreen(
        ShaderId::scene3d_post_temporal_frag, SurfaceFormat::RGBA16F));
    const auto saveDepth = shaders.pipeline(PipelineKey::fullscreen(
        ShaderId::scene3d_post_temporal_depth_frag, SurfaceFormat::R32F));
    if (!resolve || !saveDepth) return color;
    const bool reuse = can_reuse(frame);
    const u32 writeSlot = 1u - previousSlot_;
    const FGTexture output = graph.import_texture("preview-temporal-color", colors_[writeSlot], colorDesc_);
    const FGTexture nextDepth = graph.import_texture("preview-temporal-depth", depths_[writeSlot], depthDesc_);
    // Bind current inputs on a reset, avoiding any read of uninitialized memory.
    const FGTexture history = reuse ? graph.import_texture("preview-temporal-history", colors_[previousSlot_], colorDesc_) : color;
    const FGTexture historyDepth = reuse ? graph.import_texture("preview-temporal-history-depth", depths_[previousSlot_], depthDesc_) : depth;
    struct Params {
        Mat4 inverseCurrent, previous;
        Vec4 texel, options;
    } params{frame.inverseJitteredViewProjection, previous_.viewProjection,
        Vec4{1.f / colorDesc_.width, 1.f / colorDesc_.height,
            frame.jitterPixels.x / colorDesc_.width, frame.jitterPixels.y / colorDesc_.height},
        Vec4{reuse ? .85f : 0.f, .01f, 0.f, 0.f}};
    const auto nearest = shaders.sampler(CommonSampler::NearestClamp);
    const auto linear = shaders.sampler(CommonSampler::LinearClamp);
    const u32 pass = graph.add_raster_pass("preview-temporal-reconstruct", PassStage::Scene3D,
        output, LoadOp::DontCare, Vec4{}, [color, depth, history, historyDepth,
            pipeline = *resolve, nearest, linear, params](PassContext& context) {
            context.cmds.bind_pipeline(pipeline);
            context.cmds.bind_texture(0, context.texture(color), linear);
            context.cmds.bind_texture(1, context.texture(depth), nearest);
            context.cmds.bind_texture(2, context.texture(history), linear);
            context.cmds.bind_texture(3, context.texture(historyDepth), nearest);
            context.cmds.set_uniforms(&params, sizeof(params));
            context.cmds.draw(3);
        });
    graph.read(pass, color); graph.read(pass, depth);
    if (reuse) { graph.read(pass, history); graph.read(pass, historyDepth); }
    const u32 depthPass = graph.add_raster_pass("preview-temporal-save-depth", PassStage::Scene3D,
        nextDepth, LoadOp::DontCare, Vec4{}, [depth, nearest, pipeline = *saveDepth, texel = params.texel](PassContext& context) {
            context.cmds.bind_pipeline(pipeline);
            context.cmds.bind_texture(0, context.texture(depth), nearest);
            context.cmds.set_uniforms(&texel, sizeof(texel));
            context.cmds.draw(3);
        });
    graph.read(depthPass, depth);
    if (pass == kInvalidIndex || depthPass == kInvalidIndex) return color;
    graph.set_output(output, ResourceState::ShaderRead);
    graph.set_output(nextDepth, ResourceState::ShaderRead);
    pendingFrame_ = frame;
    pending_ = true;
    return output;
}

void TemporalReconstruction::complete(bool success) noexcept {
    if (!pending_) return;
    valid_ = success;
    if (success) {
        previous_ = pendingFrame_;
        previousSlot_ = 1u - previousSlot_;
    }
    pending_ = false;
}

} // namespace aurea
