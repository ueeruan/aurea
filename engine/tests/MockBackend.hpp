// =============================================================================
//  Backend falso para os testes sem GPU.
//
//  Não desenha nada: GRAVA o que o motor pediu (barreiras, render passes,
//  draws, criações de textura). Os testes do FrameGraph e do renderer
//  conferem a SEQUÊNCIA — "a textura foi para ColorAttachment antes do render
//  pass e para ShaderRead antes de ser lida" — sem depender de driver.
// =============================================================================
#pragma once

#include "aurea/render/GPUBackend.hpp"

#include <vector>
#include <functional>
#include <unordered_map>
#include <atomic>

namespace aurea::test {

class MockBackend final : public GPUBackend, public CommandList {
public:
    struct Event {
        enum Kind { Barrier, BeginPass, EndPass, Draw, BindPipeline, BindTexture, Timer } kind;
        u64 texture = 0;
        ResourceState state = ResourceState::Undefined;
        bool discard = false;
        LoadOp load = LoadOp::Clear;
        const char* label = nullptr;
        u32 slot = 0;
    };

    std::vector<Event> events;
    std::vector<TextureDesc> textures;   ///< índice = id - 1
    std::vector<bool> textureAlive;
    u32 texturesCreated = 0;
    u32 texturesDestroyed = 0;
    u32 pipelinesCreated = 0;
    u32 shadersCreated = 0;
    u32 framesSubmitted = 0;
    u32 offscreenFrames = 0;
    /// Quantas vezes o swapchain foi adquirido e quantas o quadro foi para a
    /// tela. Só contam com superfície anexada — é o que a prévia de efeito NÃO
    /// pode fazer, porque roda fora da thread de render.
    u32 acquires = 0;
    u32 presents = 0;
    u32 surfaceWidth = 1920;
    u32 surfaceHeight = 1080;
    /// Pré-rotação que a "superfície" pede (o Android em paisagem): o quadro
    /// com backbuffer a informa, como o backend de verdade.
    SurfaceRotation surfaceRotation = SurfaceRotation::None;
    bool frameHadBackbuffer = false;
    bool surfaceAttached = false;
    bool frameOpen = false;
    bool failPipelines = false;
    bool deviceLost = false;
    bool mapBuffers = false;
    std::unordered_map<u64, std::vector<u8>> mappedBuffers;
    std::function<void()> beforeBeginFrame;
    std::function<Status(const BufferDesc&)> beforeCreateBuffer;
    std::function<Status(u64, u64)> beforeWaitFrame;
    std::function<Status(TextureHandle, const void*, u32)> beforeTextureUpload;
    std::atomic<u32> idleWaits{0};
    GPUCapabilities caps;

    MockBackend() {
        caps.apiName = "Mock";
        caps.deviceName = "backend de teste";
        caps.maxTexture2D = 16384;
    }

    // --- GPUBackend -----------------------------------------------------------
    const char* name() const noexcept override { return "Mock"; }
    const GPUCapabilities& capabilities() const noexcept override { return caps; }
    Status initialize(const BackendConfig&) noexcept override { deviceLost = false; return OkStatus; }
    void shutdown() noexcept override {}
    Status attach_surface(const SurfaceDesc&) noexcept override {
        surfaceAttached = true;
        return OkStatus;
    }
    void detach_surface() noexcept override { surfaceAttached = false; }
    Status resize_surface(u32 w, u32 h) noexcept override {
        surfaceWidth = w;
        surfaceHeight = h;
        return OkStatus;
    }
    bool has_surface() const noexcept override { return surfaceAttached; }

    Status begin_frame(FrameBegin& out) noexcept override {
        if (beforeBeginFrame) beforeBeginFrame();
        out = FrameBegin{};
        out.commands = this;
        out.frameNumber = ++frame_;
        frameOpen = true;
        if (surfaceAttached) {
            ++acquires;
            out.backbuffer = TextureHandle{++ids_};
            out.backbufferWidth = surfaceWidth;
            out.backbufferHeight = surfaceHeight;
            out.rotation = surfaceRotation;
            frameHadBackbuffer = true;
        }
        return OkStatus;
    }
    Status begin_offscreen_frame(FrameBegin& out) noexcept override {
        out = FrameBegin{};
        out.commands = this;
        out.frameNumber = ++frame_;
        frameOpen = true;
        ++offscreenFrames;
        return OkStatus;
    }
    Status end_frame() noexcept override {
        frameOpen = false;
        ++framesSubmitted;
        if (frameHadBackbuffer) ++presents;
        frameHadBackbuffer = false;
        for (auto& d : deferred_) d.fn(d.ctx);
        deferred_.clear();
        return OkStatus;
    }

    Result<TextureHandle> create_texture(const TextureDesc& d) noexcept override {
        textures.push_back(d);
        textureAlive.push_back(true);
        ++texturesCreated;
        return TextureHandle{textures.size()};
    }
    Result<BufferHandle> create_buffer(const BufferDesc& desc) noexcept override {
        if (beforeCreateBuffer) {
            const Status status = beforeCreateBuffer(desc);
            if (!status.ok()) return status;
        }
        const BufferHandle handle{++ids_};
        if (mapBuffers) mappedBuffers[handle.id].resize(desc.bytes);
        return handle;
    }
    Result<SamplerHandle> create_sampler(const SamplerDesc&) noexcept override { return SamplerHandle{++ids_}; }
    Result<ShaderHandle> create_shader(const ShaderDesc& d) noexcept override {
        if (!d.spirv || d.spirvBytes < 20) return Status{Errc::InvalidArgument};
        ++shadersCreated;
        return ShaderHandle{++ids_};
    }
    Result<PipelineHandle> create_pipeline(const PipelineDesc&) noexcept override {
        if (failPipelines) return Status{Errc::PipelineCompileFailed};
        ++pipelinesCreated;
        return PipelineHandle{++ids_};
    }
    void destroy_texture(TextureHandle h) noexcept override {
        if (h.id && h.id <= textureAlive.size()) textureAlive[h.id - 1] = false;
        ++texturesDestroyed;
    }
    void destroy_buffer(BufferHandle handle) noexcept override { mappedBuffers.erase(handle.id); }
    void destroy_sampler(SamplerHandle) noexcept override {}
    void destroy_shader(ShaderHandle) noexcept override {}
    void destroy_pipeline(PipelineHandle) noexcept override {}
    TextureDesc texture_desc(TextureHandle h) const noexcept override {
        return h.id && h.id <= textures.size() ? textures[h.id - 1] : TextureDesc{};
    }
    Status upload_texture(TextureHandle texture, const void* data, u32 stride) noexcept override {
        return beforeTextureUpload ? beforeTextureUpload(texture, data, stride) : OkStatus;
    }
    Status write_buffer(BufferHandle, usize, const void*, usize) noexcept override { return OkStatus; }
    Status map_buffer(BufferHandle handle, void*& out) noexcept override {
        const auto found = mappedBuffers.find(handle.id);
        if (found == mappedBuffers.end()) return Errc::NotSupported;
        out = found->second.data();
        return OkStatus;
    }
    void unmap_buffer(BufferHandle) noexcept override {}
    Status read_texture(TextureHandle, void*, u32) noexcept override { return Errc::NotSupported; }
    Status upload_texture_level(TextureHandle texture, u32 mip, u32, const void* data, usize) noexcept override {
        const auto desc = texture_desc(texture);
        const u32 rowBytes = std::max(1u, desc.width >> mip) * desc.bytes_per_pixel();
        return beforeTextureUpload ? beforeTextureUpload(texture, data, rowBytes) : OkStatus;
    }
    Status generate_mipmaps(TextureHandle) noexcept override { return OkStatus; }
    Result<ExternalTexture> import_external_image(const ExternalImageDesc&) noexcept override {
        return Status{Errc::NotSupported};
    }
    void release_external_image(TextureHandle) noexcept override {}
    void defer_until_gpu_done(void (*fn)(void*), void* ctx) noexcept override {
        if (frameOpen) deferred_.push_back({fn, ctx});
        else fn(ctx);
    }
    void wait_idle() noexcept override { ++idleWaits; }
    u64 last_submitted_frame() const noexcept override { return framesSubmitted ? frame_ : 0; }
    Status wait_frame(u64 frame, u64 timeout) noexcept override {
        return beforeWaitFrame ? beforeWaitFrame(frame, timeout) : OkStatus;
    }
    u32 read_gpu_timings(GpuTiming*, u32, f32*) noexcept override { return 0; }
    bool is_device_lost() const noexcept override { return deviceLost; }
    u32 frames_in_flight() const noexcept override { return 2; }
    GpuMemoryStats memory_stats() const noexcept override { return {}; }
    void save_pipeline_cache() noexcept override {}

    // --- CommandList -----------------------------------------------------------
    void barrier(TextureHandle t, ResourceState s, bool discard) noexcept override {
        events.push_back({Event::Barrier, t.id, s, discard});
    }
    void begin_render_pass(const RenderPassBegin& p) noexcept override {
        Event e{Event::BeginPass, p.color.id};
        e.load = p.load;
        events.push_back(e);
    }
    void end_render_pass() noexcept override { events.push_back({Event::EndPass}); }
    void bind_pipeline(PipelineHandle) noexcept override { events.push_back({Event::BindPipeline}); }
    void bind_texture(u32 slot, TextureHandle t, SamplerHandle) noexcept override {
        Event event{Event::BindTexture, t.id};
        event.slot = slot;
        events.push_back(event);
    }
    void bind_storage_image(u32, TextureHandle) noexcept override {}
    void bind_storage_buffer(BufferHandle) noexcept override {}
    void bind_storage_buffer_at(u32, BufferHandle) noexcept override {}
    void set_uniforms(const void*, u32) noexcept override {}
    void push_constants(const void*, u32) noexcept override {}
    void set_viewport(f32, f32, f32, f32) noexcept override {}
    void set_scissor(i32, i32, u32, u32) noexcept override {}
    void draw(u32, u32, u32) noexcept override { events.push_back({Event::Draw}); }
    void bind_vertex_buffer(u32, BufferHandle, u64) noexcept override {}
    void bind_index_buffer(BufferHandle, u64, IndexType) noexcept override {}
    void draw_indexed(u32, u32, u32, i32, u32) noexcept override { events.push_back({Event::Draw}); }
    void dispatch(u32, u32, u32) noexcept override {}
    void copy_texture(TextureHandle, TextureHandle) noexcept override {}
    void copy_texture_to_buffer(TextureHandle, BufferHandle) noexcept override {}
    void begin_timer(const char* l) noexcept override {
        Event e{Event::Timer};
        e.label = l;
        events.push_back(e);
    }
    void end_timer() noexcept override {}
    void begin_label(const char*) noexcept override {}
    void end_label() noexcept override {}

    [[nodiscard]] u32 count(Event::Kind k) const {
        u32 n = 0;
        for (const Event& e : events) n += e.kind == k ? 1u : 0u;
        return n;
    }

private:
    struct Deferred { void (*fn)(void*); void* ctx; };
    std::vector<Deferred> deferred_;
    u64 ids_ = 1'000'000;
    u64 frame_ = 0;
};

} // namespace aurea::test
