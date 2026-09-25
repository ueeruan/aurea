#pragma once
#include "aurea/render/GPUBackend.hpp"
#include <memory>

namespace aurea::gles {

// ES 3.1 fallback, using the shared renderer and build-time-translated shaders.
class Backend final : public GPUBackend {
public:
    Backend();
    ~Backend() override;
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    const char* name() const noexcept override { return "OpenGL ES"; }
    const GPUCapabilities& capabilities() const noexcept override;
    Status initialize(const BackendConfig&) noexcept override;
    void shutdown() noexcept override;
    Status attach_surface(const SurfaceDesc&) noexcept override;
    void detach_surface() noexcept override;
    Status resize_surface(u32, u32) noexcept override;
    bool has_surface() const noexcept override;
    Status begin_frame(FrameBegin&) noexcept override;
    Status begin_offscreen_frame(FrameBegin&) noexcept override;
    Status end_frame() noexcept override;
    Result<TextureHandle> create_texture(const TextureDesc&) noexcept override;
    Result<BufferHandle> create_buffer(const BufferDesc&) noexcept override;
    Result<SamplerHandle> create_sampler(const SamplerDesc&) noexcept override;
    Result<ShaderHandle> create_shader(const ShaderDesc&) noexcept override;
    Result<PipelineHandle> create_pipeline(const PipelineDesc&) noexcept override;
    void destroy_texture(TextureHandle) noexcept override;
    void destroy_buffer(BufferHandle) noexcept override;
    void destroy_sampler(SamplerHandle) noexcept override;
    void destroy_shader(ShaderHandle) noexcept override;
    void destroy_pipeline(PipelineHandle) noexcept override;
    TextureDesc texture_desc(TextureHandle) const noexcept override;
    Status upload_texture(TextureHandle, const void*, u32) noexcept override;
    Status upload_texture_level(TextureHandle, u32, u32, const void*, usize) noexcept override;
    Status generate_mipmaps(TextureHandle) noexcept override;
    Status write_buffer(BufferHandle, usize, const void*, usize) noexcept override;
    Status map_buffer(BufferHandle, void*&) noexcept override;
    void unmap_buffer(BufferHandle) noexcept override;
    Status read_texture(TextureHandle, void*, u32) noexcept override;
    Result<ExternalTexture> import_external_image(const ExternalImageDesc&) noexcept override;
    void release_external_image(TextureHandle) noexcept override;
    void defer_until_gpu_done(void (*)(void*), void*) noexcept override;
    void wait_idle() noexcept override;
    u64 last_submitted_frame() const noexcept override;
    Status wait_frame(u64, u64) noexcept override;
    u32 read_gpu_timings(GpuTiming*, u32, f32*) noexcept override;
    bool is_device_lost() const noexcept override;
    u32 frames_in_flight() const noexcept override;
    GpuMemoryStats memory_stats() const noexcept override;
    void save_pipeline_cache() noexcept override;
    PipelineCacheInfo pipeline_cache_info() const noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace aurea::gles
