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

#include <cstring>
#include <string>
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
    u32 shadersAttempted = 0;
    u32 shadersDestroyed = 0;
    bool deferredShaders = false;
    u32 framesSubmitted = 0;
    u32 framesDiscarded = 0;
    bool supportsFrameDiscard = false;
    bool supportsPresentableLease = false;
    bool presentableLeaseActive = false;
    u32 presentableLeases = 0, presentableLeasesEnded = 0, leasedFrames = 0;
    u64 surfaceGeneration = 1;
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
    /// Recusa só o shader com este nome (driver GLES que não compila um shader).
    const char* failShader = nullptr;
    bool deviceLost = false;
    bool recordingFault = false;
    bool recoveryReady = true;
    u32 initializations = 0;
    u32 failInitializations = 0;
    u32 recoveryPolls = 0;
    bool mapBuffers = false;
    std::unordered_map<u64, std::vector<u8>> mappedBuffers;
    std::function<void()> beforeBeginFrame;
    std::function<Status()> beforeBeginFrameStatus;
    std::function<void()> beforeBeginOffscreenFrame;
    std::function<void()> beforeBeginLeasedFrame;
    std::function<void()> beforeWaitIdle;
    std::function<Status()> beforeExportWorkPoll;
    Status completedExportWorkFailure = OkStatus;
    std::function<Status()> beforeEndFrame;
    std::function<Status(const TextureDesc&)> beforeCreateTexture;
    std::function<Status(const BufferDesc&)> beforeCreateBuffer;
    std::function<Status(u64, u64)> beforeWaitFrame;
    std::function<Status(TextureHandle, const void*, u32)> beforeTextureUpload;
    // Opt-in mocked readback sink. No pixels are fabricated by default.
    std::function<Status(TextureHandle, void*, u32)> beforeReadTexture;
    std::function<void(const void*, u32)> beforeSetUniforms;
    std::function<Result<ExternalTexture>(const ExternalImageDesc&)> beforeImportExternalImage;
    // surface-direct (ExportDirectSurface.inl): alvo R'G'B' "com AHardwareBuffer".
    bool sharedTargetsSupported = false;
    u32 sharedTargetsCreated = 0;
    std::vector<u64> sharedTargetIds;
    std::vector<void*> sharedBuffers;
    std::atomic<u32> bufferCopies{0};   ///< copy_texture_to_buffer (readback NV12 do export)
    std::function<void(void (*)(void*), void*)> beforeDeferUntilGpuDone;
    std::function<u32()> beforeTrimExternalImages;
    std::function<GpuMemoryStats()> queryMemoryStats;
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
    Status initialize(const BackendConfig&) noexcept override {
        ++initializations;
        if (failInitializations) { --failInitializations; return Errc::OutOfMemory; }
        deviceLost = recordingFault = false;
        return OkStatus;
    }
    void shutdown() noexcept override {}
    Status attach_surface(const SurfaceDesc&) noexcept override {
        ++surfaceGeneration;
        surfaceAttached = true;
        return OkStatus;
    }
    void detach_surface() noexcept override { ++surfaceGeneration; surfaceAttached = false; }
    Status resize_surface(u32 w, u32 h) noexcept override {
        if (w != surfaceWidth || h != surfaceHeight) ++surfaceGeneration;
        surfaceWidth = w;
        surfaceHeight = h;
        return OkStatus;
    }
    bool has_surface() const noexcept override { return surfaceAttached; }

    Status begin_frame(FrameBegin& out) noexcept override {
        if (beforeBeginFrame) beforeBeginFrame();
        if (beforeBeginFrameStatus) {
            const Status ready = beforeBeginFrameStatus();
            if (!ready.ok()) return ready;
        }
        if (presentableLeaseActive) return Errc::Busy;
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
        presentableFrame_ = out;
        return OkStatus;
    }
    Status begin_offscreen_frame(FrameBegin& out) noexcept override {
        if (beforeBeginOffscreenFrame) beforeBeginOffscreenFrame();
        out = FrameBegin{};
        out.commands = this;
        out.frameNumber = ++frame_;
        frameOpen = true;
        frameHadBackbuffer = false;
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
        return beforeEndFrame ? beforeEndFrame() : OkStatus;
    }
    bool can_discard_frame() const noexcept override { return supportsFrameDiscard; }
    Status discard_frame() noexcept override {
        if (!supportsFrameDiscard) return end_frame();
        ++framesDiscarded;
        frameHadBackbuffer = false;
        return end_frame();
    }
    bool can_lease_presentable_frame() const noexcept override { return supportsPresentableLease; }
    Status begin_presentable_frame_lease() noexcept override {
        if (!supportsPresentableLease || !frameOpen || !frameHadBackbuffer || !surfaceAttached
            || presentableLeaseActive || !presentableFrame_.backbuffer.valid()) return Errc::InvalidState;
        presentableLeaseActive = true; ++presentableLeases;
        leasedFrame_ = presentableFrame_; leasedGeneration_ = surfaceGeneration;
        return OkStatus;
    }
    Status begin_leased_frame(FrameBegin& out) noexcept override {
        if (beforeBeginLeasedFrame) beforeBeginLeasedFrame();
        out = {};
        if (!presentableLeaseActive || frameOpen) return Errc::InvalidState;
        if (deviceLost) return Errc::DeviceLost;
        if (!surfaceAttached || leasedGeneration_ != surfaceGeneration
            || leasedFrame_.backbufferWidth != surfaceWidth || leasedFrame_.backbufferHeight != surfaceHeight
            || leasedFrame_.rotation != surfaceRotation) return Errc::SurfaceLost;
        out = leasedFrame_; out.commands = this; out.frameNumber = ++frame_;
        frameOpen = frameHadBackbuffer = true; ++leasedFrames;
        return OkStatus;
    }
    void end_presentable_frame_lease() noexcept override {
        if (presentableLeaseActive) ++presentableLeasesEnded;
        presentableLeaseActive = false; leasedFrame_ = {}; leasedGeneration_ = 0;
    }

    Result<TextureHandle> create_texture(const TextureDesc& d) noexcept override {
        if (beforeCreateTexture) {
            const Status status = beforeCreateTexture(d);
            if (!status.ok()) return status;
        }
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
    bool defer_shader_creation() const noexcept override { return deferredShaders; }
    Result<ShaderHandle> create_shader(const ShaderDesc& d) noexcept override {
        ++shadersAttempted;
        if (!d.spirv || d.spirvBytes < 20) return Status{Errc::InvalidArgument};
        if (failShader && d.debugName && std::strcmp(failShader, d.debugName) == 0) return Status{Errc::ShaderCompileFailed};
        ++shadersCreated;
        return ShaderHandle{++ids_};
    }
    /// Shaders de plugin (RuntimeShaders.hpp): contados à parte; `failRuntimeShaders`
    /// simula o driver que recusa o shader traduzido.
    u32 runtimeShadersCreated = 0;
    bool failRuntimeShaders = false;
    Result<ShaderHandle> create_runtime_shader(const ShaderDesc& d, std::string* error) noexcept override {
        if (failRuntimeShaders) {
            if (error) *error = "driver falso: shader recusado";
            return Status{Errc::ShaderCompileFailed};
        }
        auto created = create_shader(d);
        if (created.ok()) ++runtimeShadersCreated;
        else if (error) *error = "SPIR-V invalido";
        return created;
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
    void destroy_shader(ShaderHandle) noexcept override { ++shadersDestroyed; }
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
    Status read_texture(TextureHandle texture, void* output, u32 rowBytes) noexcept override {
        return beforeReadTexture ? beforeReadTexture(texture, output, rowBytes) : Status{Errc::NotSupported};
    }
    Status upload_texture_level(TextureHandle texture, u32 mip, u32, const void* data, usize) noexcept override {
        const auto desc = texture_desc(texture);
        const u32 rowBytes = std::max(1u, desc.width >> mip) * desc.bytes_per_pixel();
        return beforeTextureUpload ? beforeTextureUpload(texture, data, rowBytes) : OkStatus;
    }
    Status generate_mipmaps(TextureHandle) noexcept override { return OkStatus; }
    Result<ExternalTexture> import_external_image(const ExternalImageDesc& desc) noexcept override {
        return beforeImportExternalImage ? beforeImportExternalImage(desc) : Result<ExternalTexture>{Status{Errc::NotSupported}};
    }
    void release_external_image(TextureHandle) noexcept override {}
    Result<TextureHandle> create_shared_render_target(u32 width, u32 height, void** nativeBuffer) noexcept override {
        if (nativeBuffer) *nativeBuffer = nullptr;
        if (!sharedTargetsSupported || !nativeBuffer) return Status{Errc::NotSupported, "mock sem alvo compartilhado"};
        TextureDesc d;
        d.width = width; d.height = height;
        d.format = SurfaceFormat::RGBA8;
        d.sampled = true; d.renderTarget = true;
        textures.push_back(d);
        textureAlive.push_back(true);
        ++texturesCreated; ++sharedTargetsCreated;
        sharedTargetIds.push_back(textures.size());
        // Endereço único e estável no papel do AHardwareBuffer (nunca lido).
        sharedBuffers.push_back(reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000 + textures.size() * 16)));
        *nativeBuffer = sharedBuffers.back();
        return TextureHandle{textures.size()};
    }
    u32 trim_external_images() noexcept override {
        return beforeTrimExternalImages ? beforeTrimExternalImages() : 0;
    }
    void defer_until_gpu_done(void (*fn)(void*), void* ctx) noexcept override {
        if (beforeDeferUntilGpuDone) { beforeDeferUntilGpuDone(fn, ctx); return; }
        if (frameOpen) deferred_.push_back({fn, ctx});
        else fn(ctx);
    }
    void wait_idle() noexcept override {
        if (beforeWaitIdle) beforeWaitIdle();
        ++idleWaits;
    }
    Status poll_export_work(Status* completedFailure = nullptr) noexcept override {
        if (completedFailure) *completedFailure = completedExportWorkFailure;
        if (frameOpen) return Status{Errc::Timeout};
        if (beforeExportWorkPoll) {
            const Status status = beforeExportWorkPoll();
            if (!status.ok()) return status;
        }
        return wait_frame(last_submitted_frame(), 0);
    }
    u64 last_submitted_frame() const noexcept override { return framesSubmitted ? frame_ : 0; }
    Status wait_frame(u64 frame, u64 timeout) noexcept override {
        return beforeWaitFrame ? beforeWaitFrame(frame, timeout) : OkStatus;
    }
    u32 read_gpu_timings(GpuTiming*, u32, f32*) noexcept override { return 0; }
    bool is_device_lost() const noexcept override { return deviceLost; }
    bool requires_reinitialization() const noexcept override { return deviceLost || recordingFault; }
    Status prepare_reinitialization(u64 timeout) noexcept override {
        ++recoveryPolls;
        if (timeout != 0) return Errc::InvalidArgument;
        return deviceLost || recoveryReady ? OkStatus : Status{Errc::Timeout};
    }
    u32 frames_in_flight() const noexcept override { return 2; }
    GpuMemoryStats memory_stats() const noexcept override { return queryMemoryStats ? queryMemoryStats() : GpuMemoryStats{}; }
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
    void set_uniforms(const void* bytes, u32 size) noexcept override {
        if (beforeSetUniforms) beforeSetUniforms(bytes, size);
    }
    void push_constants(const void*, u32) noexcept override {}
    void set_viewport(f32, f32, f32, f32) noexcept override {}
    void set_scissor(i32, i32, u32, u32) noexcept override {}
    void draw(u32, u32, u32) noexcept override { events.push_back({Event::Draw}); }
    void bind_vertex_buffer(u32, BufferHandle, u64) noexcept override {}
    void bind_index_buffer(BufferHandle, u64, IndexType) noexcept override {}
    void draw_indexed(u32, u32, u32, i32, u32) noexcept override { events.push_back({Event::Draw}); }
    void dispatch(u32, u32, u32) noexcept override {}
    void copy_texture(TextureHandle, TextureHandle) noexcept override {}
    void copy_texture_to_buffer(TextureHandle, BufferHandle) noexcept override { ++bufferCopies; }
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
    FrameBegin presentableFrame_{}, leasedFrame_{};
    u64 leasedGeneration_ = 0;
};

} // namespace aurea::test
