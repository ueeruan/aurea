#pragma once
#include "GlesBackend.hpp"
#include "aurea/core/Log.hpp"
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurea::gles {
const char* shader_source(const u32*, usize) noexcept;
struct Format { GLenum internal = 0, external = 0, type = 0; u32 bytes = 0; bool compressed = false; };
Format format_of(SurfaceFormat) noexcept;
struct Texture { GLuint id = 0; GLenum target = GL_TEXTURE_2D; TextureDesc desc; ResourceState state{}; };
struct Buffer {
    GLuint id = 0; BufferDesc desc; usize gpuBytes = 0;
    std::vector<u8> cpu;
    u32 readWidth = 0, readHeight = 0, readComponents = 0;
    bool rgbaReadback = false, pendingRead = false;
};
struct Shader { GLuint id = 0; u64 hash = 0; };
struct Pipeline {
    GLuint id = 0; PipelineDesc desc; u64 cacheKey = 0; bool persisted = false;
    GLint borderLocations[3][binding::kTextureSlots]{};
};
struct Sampler { GLuint id = 0; GLint borderMask = 0, nearest = 0; };
struct Deferred { void (*fn)(void*) = nullptr; void* context = nullptr; };
struct UniformPage { GLuint id = 0; };
struct Timer { GLuint begin = 0, end = 0; const char* label = nullptr; };
struct Frame {
    GLsync fence = nullptr; u64 number = 0;
    std::vector<Deferred> deferred;
    std::vector<UniformPage> uniforms;
    u32 page = 0, offset = 0;
    std::vector<Timer> timers;
    std::vector<u32> timerStack;
    u32 timerCount = 0;
};
template<class T> T* find(std::unordered_map<u64, T>& table, u64 id) {
    auto it = table.find(id); return it == table.end() ? nullptr : &it->second;
}
struct Backend::Impl final : CommandList {
    Backend& owner;
    explicit Impl(Backend& b) : owner(b) {}
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config{};
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface pbuffer = EGL_NO_SURFACE, window = EGL_NO_SURFACE;
    GPUCapabilities caps;
    BackendConfig settings;
    PipelineCacheInfo cacheInfo;
    std::string cacheDirectory;
    std::unordered_map<u64, Texture> textures;
    std::unordered_map<u64, Buffer> buffers;
    std::unordered_map<u64, Shader> shaders;
    std::unordered_map<u64, Pipeline> pipelines;
    std::unordered_map<u64, Sampler> samplers;
    std::vector<u8> uploadScratch;
    std::array<Frame, 3> frames;
    Frame* current = nullptr;
    Frame* last = nullptr;
    u64 sequence = 0, nextId = 1, bytesTextures = 0, bytesBuffers = 0, uploadBytes = 0;
    u32 frameCount = 3, surfaceWidth = 0, surfaceHeight = 0;
    bool frameOpen = false, presenting = false, deviceLost = false, borderClamp = false;
    TextureHandle backbuffer{};
    GLuint vao = 0, framebuffer = 0, readFramebuffer = 0;
    GLuint dummy2D = 0, dummyCube = 0, dummyBuffer = 0, dummySampler = 0;
    Status frameStatus = OkStatus;
    PFNGLQUERYCOUNTEREXTPROC queryCounter = nullptr;
    PFNGLGETQUERYOBJECTUI64VEXTPROC queryResult = nullptr;
    PFNGLPUSHDEBUGGROUPKHRPROC pushLabel = nullptr;
    PFNGLPOPDEBUGGROUPKHRPROC popLabel = nullptr;
    std::vector<GpuTiming> completedTimings;
    f32 completedGpuMs = 0;
    Pipeline* pipeline = nullptr;
    u64 vertexBuffers[VertexLayout::kMaxBindings]{};
    u64 vertexOffsets[VertexLayout::kMaxBindings]{};
    u64 indexBuffer = 0, indexOffset = 0;
    IndexType indexType = IndexType::U16;
    u32 targetWidth = 0, targetHeight = 0;
    RenderPassBegin pass{};
    GLint borderState[binding::kTextureSlots][2]{};

    bool make_current() {
        if (context == EGL_NO_CONTEXT) return false;
        if (eglGetCurrentContext() == context) return true;
        if (eglMakeCurrent(display, pbuffer, pbuffer, context)) return true;
        deviceLost = eglGetError() == EGL_CONTEXT_LOST;
        return false;
    }
    struct Scope {
        Impl& d; bool acquired, valid;
        explicit Scope(Impl& state) : d(state), acquired(eglGetCurrentContext() != state.context), valid(state.make_current()) {}
        ~Scope() { if (valid && acquired && !d.frameOpen) eglMakeCurrent(d.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); }
    };
    Status check(const char* operation) {
        GLenum first = glGetError();
        if (first == GL_NO_ERROR) return OkStatus;
        AUREA_LOG_ERROR("GLES %s: 0x%x", operation, first);
        while (glGetError() != GL_NO_ERROR) {}
        const Errc code = first == GL_OUT_OF_MEMORY ? Errc::OutOfDeviceMemory
                        : first == GL_CONTEXT_LOST_KHR ? Errc::DeviceLost : Errc::InvalidState;
        if (code == Errc::DeviceLost) deviceLost = true;
        const Status result{code, operation};
        if (frameOpen && frameStatus.ok()) frameStatus = result;
        return result;
    }
    void fail(Errc code, const char* operation) {
        AUREA_LOG_ERROR("GLES: %s", operation);
        if (frameStatus.ok()) frameStatus = Status{code, operation};
    }
    bool extension(const char* name) const {
        return std::find(caps.extensions.begin(), caps.extensions.end(), name) != caps.extensions.end();
    }
    Status begin(FrameBegin&, bool present);
    Status wait(Frame&, u64 timeout);
    void retire(Frame&);
    void uniforms(u32 binding, const void*, u32);
    void vertices(i32 baseVertex);
    void update_border(u32 slot);
    void attach(GLenum target, Texture* color, Texture* depth);
    std::string cache_path(u64 key) const;
    void barrier(TextureHandle, ResourceState, bool) noexcept override;
    void begin_render_pass(const RenderPassBegin&) noexcept override;
    void end_render_pass() noexcept override;
    void bind_pipeline(PipelineHandle) noexcept override;
    void bind_texture(u32, TextureHandle, SamplerHandle) noexcept override;
    void bind_storage_image(u32, TextureHandle) noexcept override;
    void bind_storage_buffer(BufferHandle h) noexcept override { bind_storage_buffer_at(0, h); }
    void bind_storage_buffer_at(u32, BufferHandle) noexcept override;
    void set_uniforms(const void* data, u32 bytes) noexcept override { uniforms(0, data, bytes); }
    void push_constants(const void* data, u32 bytes) noexcept override { uniforms(1, data, bytes); }
    void set_viewport(f32 x, f32 y, f32 w, f32 h) noexcept override { glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(w), static_cast<GLsizei>(h)); }
    void set_scissor(i32 x, i32 y, u32 w, u32 h) noexcept override { glScissor(x, y, w, h); }
    void draw(u32, u32, u32) noexcept override;
    void bind_vertex_buffer(u32 slot, BufferHandle h, u64 offset) noexcept override {
        if (slot < VertexLayout::kMaxBindings) { vertexBuffers[slot] = h.id; vertexOffsets[slot] = offset; }
    }
    void bind_index_buffer(BufferHandle h, u64 offset, IndexType type) noexcept override { indexBuffer = h.id; indexOffset = offset; indexType = type; }
    void draw_indexed(u32, u32, u32, i32, u32) noexcept override;
    void dispatch(u32 x, u32 y, u32 z) noexcept override { glDispatchCompute(x, y, z); (void)check("dispatch"); }
    void copy_texture(TextureHandle, TextureHandle) noexcept override;
    void copy_texture_to_buffer(TextureHandle, BufferHandle) noexcept override;
    void begin_timer(const char*) noexcept override;
    void end_timer() noexcept override;
    void begin_label(const char* label) noexcept override { if (pushLabel && settings.enableValidation) pushLabel(GL_DEBUG_SOURCE_APPLICATION_KHR, 0, -1, label ? label : ""); }
    void end_label() noexcept override { if (popLabel && settings.enableValidation) popLabel(); }
};
} // namespace aurea::gles
