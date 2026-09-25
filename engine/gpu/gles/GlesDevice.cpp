#include "GlesInternal.hpp"
#include <limits>
#include <mutex>

namespace aurea::gles {
namespace {
std::mutex displayMutex;
EGLDisplay sharedDisplay = EGL_NO_DISPLAY;
u32 displayUsers = 0;
EGLDisplay acquire_display() {
    std::lock_guard<std::mutex> lock(displayMutex);
    if (!displayUsers) {
        sharedDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (sharedDisplay == EGL_NO_DISPLAY || !eglInitialize(sharedDisplay, nullptr, nullptr)) {
            sharedDisplay = EGL_NO_DISPLAY; return sharedDisplay;
        }
    }
    ++displayUsers; return sharedDisplay;
}
void release_display(EGLDisplay display) {
    std::lock_guard<std::mutex> lock(displayMutex);
    if (display != sharedDisplay || !displayUsers) return;
    if (--displayUsers == 0) { eglTerminate(display); sharedDisplay = EGL_NO_DISPLAY; }
}
}
Backend::Backend() : impl_(std::make_unique<Impl>(*this)) {}
Backend::~Backend() { shutdown(); }
const GPUCapabilities& Backend::capabilities() const noexcept { return impl_->caps; }

Status Backend::initialize(const BackendConfig& settings) noexcept {
    auto& d = *impl_;
    if (d.context != EGL_NO_CONTEXT) return OkStatus;
    d.caps = {}; d.deviceLost = false;
    d.settings = settings; d.frameCount = std::clamp(settings.framesInFlight, 1u, 3u);
    d.cacheDirectory = settings.cacheDirectory ? settings.cacheDirectory : "";
    d.display = acquire_display();
    if (d.display == EGL_NO_DISPLAY) return Errc::UnsupportedFeature;
    const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT | EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(d.display, attrs, &d.config, 1, &count) || count != 1) { shutdown(); return Errc::UnsupportedFeature; }
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint pbAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    d.context = eglCreateContext(d.display, d.config, EGL_NO_CONTEXT, ctxAttrs);
    d.pbuffer = eglCreatePbufferSurface(d.display, d.config, pbAttrs);
    if (d.context == EGL_NO_CONTEXT || d.pbuffer == EGL_NO_SURFACE || !d.make_current()) { shutdown(); return Errc::UnsupportedFeature; }
    auto integer = [](GLenum key) { GLint value = 0; glGetIntegerv(key, &value); return static_cast<u32>(std::max(0, value)); };
    d.caps.apiName = "OpenGL ES"; d.caps.apiMajor = integer(GL_MAJOR_VERSION); d.caps.apiMinor = integer(GL_MINOR_VERSION);
    if (d.caps.apiMajor < 3 || (d.caps.apiMajor == 3 && d.caps.apiMinor < 1)) { shutdown(); return Status{Errc::UnsupportedFeature, "OpenGL ES 3.1 required"}; }
    d.caps.deviceName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    d.caps.driverInfo = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const u32 extCount = integer(GL_NUM_EXTENSIONS);
    for (u32 i = 0; i < extCount; ++i) d.caps.extensions.emplace_back(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)));
    d.borderClamp = d.caps.apiMinor >= 2 || d.extension("GL_EXT_texture_border_clamp") || d.extension("GL_OES_texture_border_clamp");
    d.caps.rgba16fRenderable = d.extension("GL_EXT_color_buffer_float") || d.extension("GL_EXT_color_buffer_half_float");
    if (!d.caps.rgba16fRenderable) { shutdown(); return Status{Errc::UnsupportedFeature, "GLES requires half-float render targets"}; }
    d.caps.rgba16fFilterable = d.caps.rgba16fStorage = true;
    d.caps.r16UnormSampled = d.extension("GL_EXT_texture_norm16");
    d.caps.maxTexture2D = integer(GL_MAX_TEXTURE_SIZE);
    d.caps.maxUniformBufferRange = integer(GL_MAX_UNIFORM_BLOCK_SIZE);
    d.caps.minUniformBufferOffsetAlignment = std::max(16u, integer(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT));
    d.caps.maxComputeWorkGroupInvocations = integer(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
    d.caps.maxComputeSharedMemoryBytes = integer(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE);
    for (u32 i = 0; i < 3; ++i) { GLint value = 0; glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i, &value); d.caps.maxComputeWorkGroupSize[i] = value; }
    d.caps.maxPerStageSampledImages = integer(GL_MAX_TEXTURE_IMAGE_UNITS);
    d.caps.maxPerStageStorageImages = integer(GL_MAX_COMPUTE_IMAGE_UNIFORMS);
    d.caps.maxColorAttachments = integer(GL_MAX_COLOR_ATTACHMENTS);
    d.caps.maxVertexInputAttributes = integer(GL_MAX_VERTEX_ATTRIBS);
    d.caps.depth24Attachment = d.caps.depth32fAttachment = d.caps.depth32fSampled = true;
    d.caps.textureCompressionETC2 = true;
    d.caps.textureCompressionASTC = d.extension("GL_KHR_texture_compression_astc_ldr");
    d.caps.textureCompressionBC = d.extension("GL_EXT_texture_compression_bptc");
    d.caps.unifiedMemory = true; d.caps.deviceType = GpuDeviceType::Integrated;
    if (d.extension("GL_EXT_texture_filter_anisotropic")) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &d.caps.maxSamplerAnisotropy);
    d.queryCounter = reinterpret_cast<PFNGLQUERYCOUNTEREXTPROC>(eglGetProcAddress("glQueryCounterEXT"));
    d.queryResult = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
    d.caps.timestampQueries = d.extension("GL_EXT_disjoint_timer_query") && d.queryCounter && d.queryResult;
    d.caps.timestampPeriodNs = 1;
    if (d.extension("GL_KHR_debug")) {
        d.pushLabel = reinterpret_cast<PFNGLPUSHDEBUGGROUPKHRPROC>(eglGetProcAddress("glPushDebugGroupKHR"));
        d.popLabel = reinterpret_cast<PFNGLPOPDEBUGGROUPKHRPROC>(eglGetProcAddress("glPopDebugGroupKHR"));
    }
    glGenVertexArrays(1, &d.vao); glBindVertexArray(d.vao);
    glGenFramebuffers(1, &d.framebuffer); glGenFramebuffers(1, &d.readFramebuffer);
    const u8 clear[4]{};
    glGenTextures(1, &d.dummy2D); glBindTexture(GL_TEXTURE_2D, d.dummy2D);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, clear);
    glGenTextures(1, &d.dummyCube); glBindTexture(GL_TEXTURE_CUBE_MAP, d.dummyCube);
    glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, GL_RGBA8, 1, 1);
    for (u32 i = 0; i < 6; ++i) glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, clear);
    glGenSamplers(1, &d.dummySampler); glSamplerParameteri(d.dummySampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(d.dummySampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(d.dummySampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glSamplerParameteri(d.dummySampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const std::array<u8, 256> zeros{};
    glGenBuffers(1, &d.dummyBuffer); glBindBuffer(GL_SHADER_STORAGE_BUFFER, d.dummyBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, zeros.size(), zeros.data(), GL_STATIC_DRAW);
    d.bytesBuffers = zeros.size(); d.bytesTextures = 28;
    const Status result = d.check("initialize");
    eglMakeCurrent(d.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (!result.ok()) { shutdown(); return result; }
    AUREA_LOG_INFO("GLES: %s", d.caps.summary().c_str());
    return OkStatus;
}

Status Backend::Impl::wait(Frame& f, u64 timeout) {
    if (!f.fence) return OkStatus;
    const GLenum result = glClientWaitSync(f.fence, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
    if (result == GL_TIMEOUT_EXPIRED) return Errc::Timeout;
    if (result == GL_WAIT_FAILED) { deviceLost = true; return Errc::DeviceLost; }
    return OkStatus;
}
void Backend::Impl::retire(Frame& f) {
    if (caps.timestampQueries && f.timerCount) {
        GLint disjoint = 0; glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        completedTimings.clear(); completedGpuMs = 0;
        GLuint64 first = std::numeric_limits<GLuint64>::max(), lastTime = 0;
        if (!disjoint) for (u32 i = 0; i < f.timerCount; ++i) {
            GLuint64 a = 0, b = 0; queryResult(f.timers[i].begin, GL_QUERY_RESULT, &a); queryResult(f.timers[i].end, GL_QUERY_RESULT, &b);
            if (b < a) continue;
            completedTimings.push_back({f.timers[i].label, i, static_cast<f32>((b - a) * 1e-6)});
            first = std::min(first, a); lastTime = std::max(lastTime, b);
        }
        if (lastTime >= first) completedGpuMs = static_cast<f32>((lastTime - first) * 1e-6);
    }
    if (f.fence) { glDeleteSync(f.fence); f.fence = nullptr; }
    auto callbacks = std::move(f.deferred); f.deferred.clear();
    for (const auto& callback : callbacks) if (callback.fn) callback.fn(callback.context);
    // Retain at most 1 MiB of inactive uniform pages per frame context; keep
    // the live working set when a large frame actually needs more than that.
    const usize keep = std::max<usize>(4, f.page + 1);
    while (f.uniforms.size() > keep) {
        glDeleteBuffers(1, &f.uniforms.back().id); f.uniforms.pop_back(); bytesBuffers -= 256 * 1024;
    }
    f.timerCount = 0; f.timerStack.clear(); f.page = f.offset = 0;
}
Status Backend::Impl::begin(FrameBegin& out, bool present) {
    Scope scope(*this);
    if (!scope.valid || frameOpen || deviceLost) return Errc::InvalidState;
    Frame& f = frames[sequence % frameCount];
    if (const auto status = wait(f, 5'000'000'000ull); !status.ok()) return status;
    retire(f); current = &f; f.number = ++sequence;
    frameStatus = OkStatus; uploadBytes = 0;
    presenting = present && window != EGL_NO_SURFACE;
    if (presenting && !backbuffer.valid()) {
        TextureDesc desc; desc.width = surfaceWidth; desc.height = surfaceHeight; desc.renderTarget = true;
        auto texture = owner.create_texture(desc); if (!texture.ok()) { current = nullptr; return texture.status(); }
        backbuffer = *texture;
    }
    glBindVertexArray(vao); glEnable(GL_SCISSOR_TEST); pipeline = nullptr;
    for (u32 i = 0; i < binding::kTextureSlots; ++i) {
        borderState[i][0] = borderState[i][1] = 0;
        glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, dummy2D); glBindTexture(GL_TEXTURE_CUBE_MAP, dummyCube); glBindSampler(i, dummySampler);
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, dummyBuffer); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, dummyBuffer);
    out = {}; out.commands = this; out.frameNumber = sequence;
    if (presenting) { out.backbuffer = backbuffer; out.backbufferWidth = surfaceWidth; out.backbufferHeight = surfaceHeight; out.backbufferFormat = SurfaceFormat::RGBA8; }
    frameOpen = true; return check("begin_frame");
}
Status Backend::begin_frame(FrameBegin& out) noexcept { return impl_->begin(out, true); }
Status Backend::begin_offscreen_frame(FrameBegin& out) noexcept { return impl_->begin(out, false); }
Status Backend::end_frame() noexcept {
    auto& d = *impl_;
    if (!d.frameOpen || !d.current) return Errc::InvalidState;
    while (!d.current->timerStack.empty()) d.end_timer();
    if (d.presenting && d.frameStatus.ok()) {
        auto* t = find(d.textures, d.backbuffer.id);
        if (!eglMakeCurrent(d.display, d.window, d.window, d.context)) d.fail(Errc::SurfaceLost, "EGL window make-current failed");
        else {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, d.readFramebuffer); d.attach(GL_READ_FRAMEBUFFER, t, nullptr);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); glDisable(GL_SCISSOR_TEST);
            glBlitFramebuffer(0, 0, d.surfaceWidth, d.surfaceHeight, 0, d.surfaceHeight, d.surfaceWidth, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            if (!eglSwapBuffers(d.display, d.window)) d.fail(Errc::SurfaceLost, "EGL swap failed");
        }
    }
    (void)d.check("end_frame");
    d.current->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); glFlush();
    if (!d.current->fence) d.fail(Errc::DeviceLost, "GLES frame fence failed");
    d.last = d.current; d.current = nullptr; d.frameOpen = false;
    eglMakeCurrent(d.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return d.frameStatus;
}
Status Backend::attach_surface(const SurfaceDesc& surface) noexcept {
    auto& d = *impl_; if (!surface.nativeWindow || !surface.width || !surface.height) return Errc::InvalidArgument;
    detach_surface(); Impl::Scope scope(d); if (!scope.valid) return Errc::InvalidState;
    d.window = eglCreateWindowSurface(d.display, d.config, static_cast<EGLNativeWindowType>(surface.nativeWindow), nullptr);
    if (d.window == EGL_NO_SURFACE) return Errc::SurfaceLost;
    d.surfaceWidth = surface.width; d.surfaceHeight = surface.height;
    if (!eglMakeCurrent(d.display, d.window, d.window, d.context)) return Errc::SurfaceLost;
    eglSwapInterval(d.display, surface.vsync ? 1 : 0);
    return OkStatus;
}
void Backend::detach_surface() noexcept {
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid) return;
    wait_idle(); if (d.backbuffer.valid()) { destroy_texture(d.backbuffer); d.backbuffer = {}; }
    eglMakeCurrent(d.display, d.pbuffer, d.pbuffer, d.context);
    if (d.window != EGL_NO_SURFACE) { eglDestroySurface(d.display, d.window); d.window = EGL_NO_SURFACE; }
}
Status Backend::resize_surface(u32 w, u32 h) noexcept {
    if (!w || !h) return Errc::InvalidArgument;
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid) return Errc::InvalidState;
    if (w != d.surfaceWidth || h != d.surfaceHeight) {
        wait_idle(); if (d.backbuffer.valid()) { destroy_texture(d.backbuffer); d.backbuffer = {}; }
        d.surfaceWidth = w; d.surfaceHeight = h;
    }
    return OkStatus;
}
bool Backend::has_surface() const noexcept { return impl_->window != EGL_NO_SURFACE; }
void Backend::defer_until_gpu_done(void (*fn)(void*), void* context) noexcept {
    if (!fn) return;
    auto& d = *impl_; Frame* f = d.current ? d.current : d.last;
    if (f && (d.frameOpen || f->fence)) f->deferred.push_back({fn, context}); else fn(context);
}
void Backend::wait_idle() noexcept {
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid) return;
    glFinish(); (void)d.check("wait_idle");
    for (auto& f : d.frames) if (&f != d.current) d.retire(f);
}
u64 Backend::last_submitted_frame() const noexcept { return impl_->last ? impl_->last->number : 0; }
Status Backend::wait_frame(u64 number, u64 timeout) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid) return Errc::InvalidState;
    for (auto& f : d.frames) if (f.number == number) return d.wait(f, timeout);
    return OkStatus; // A recycled frame was already waited by begin_frame.
}
u32 Backend::read_gpu_timings(GpuTiming* output, u32 capacity, f32* total) noexcept {
    auto& d = *impl_; if (total) *total = d.completedGpuMs;
    const u32 count = output ? std::min<u32>(capacity, d.completedTimings.size()) : 0;
    for (u32 i = 0; i < count; ++i) output[i] = d.completedTimings[i];
    return count;
}
bool Backend::is_device_lost() const noexcept { return impl_->deviceLost; }
u32 Backend::frames_in_flight() const noexcept { return impl_->frameCount; }
GpuMemoryStats Backend::memory_stats() const noexcept {
    auto& d = *impl_; GpuMemoryStats out;
    out.usedBytes = d.bytesTextures + d.bytesBuffers;
    // ES readback/upload buffers have an owned CPU mirror. These bytes consume
    // the same Android RAM budget as the unified GPU allocations.
    for (const auto& [id, buffer] : d.buffers) out.usedBytes += buffer.cpu.capacity();
    out.usedBytes += d.uploadScratch.capacity();
    out.reservedBytes = out.usedBytes;
    out.textureCount = d.textures.size(); out.bufferCount = d.buffers.size();
    out.allocationCount = out.textureCount + out.bufferCount; out.uploadBytesThisFrame = d.uploadBytes;
    return out;
}
GPUBackend::PipelineCacheInfo Backend::pipeline_cache_info() const noexcept { return impl_->cacheInfo; }
void Backend::shutdown() noexcept {
    auto& d = *impl_;
    if (d.context != EGL_NO_CONTEXT && d.make_current()) {
        d.frameOpen = false; d.current = nullptr; wait_idle(); save_pipeline_cache();
        for (auto& [id, p] : d.pipelines) glDeleteProgram(p.id);
        for (auto& [id, s] : d.shaders) glDeleteShader(s.id);
        for (auto& [id, s] : d.samplers) glDeleteSamplers(1, &s.id);
        for (auto& [id, b] : d.buffers) glDeleteBuffers(1, &b.id);
        for (auto& [id, t] : d.textures) glDeleteTextures(1, &t.id);
        for (auto& f : d.frames) {
            for (auto& page : f.uniforms) glDeleteBuffers(1, &page.id);
            for (auto& timer : f.timers) { glDeleteQueries(1, &timer.begin); glDeleteQueries(1, &timer.end); }
            f = {};
        }
        glDeleteTextures(1, &d.dummy2D); glDeleteTextures(1, &d.dummyCube); glDeleteSamplers(1, &d.dummySampler); glDeleteBuffers(1, &d.dummyBuffer);
        glDeleteVertexArrays(1, &d.vao); glDeleteFramebuffers(1, &d.framebuffer); glDeleteFramebuffers(1, &d.readFramebuffer);
    }
    d.textures.clear(); d.buffers.clear(); d.pipelines.clear(); d.shaders.clear(); d.samplers.clear();
    std::vector<u8>().swap(d.uploadScratch);
    if (d.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(d.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (d.window != EGL_NO_SURFACE) eglDestroySurface(d.display, d.window);
        if (d.pbuffer != EGL_NO_SURFACE) eglDestroySurface(d.display, d.pbuffer);
        if (d.context != EGL_NO_CONTEXT) eglDestroyContext(d.display, d.context);
        release_display(d.display);
    }
    d.context = EGL_NO_CONTEXT; d.window = d.pbuffer = EGL_NO_SURFACE; d.display = EGL_NO_DISPLAY;
    // If a lost context could not be made current, EGL destruction above is
    // the completion boundary for callbacks whose fences cannot be queried.
    for (auto& f : d.frames) { for (const auto& callback : f.deferred) if (callback.fn) callback.fn(callback.context); f = {}; }
    d.bytesBuffers = d.bytesTextures = 0; d.last = d.current = nullptr; d.backbuffer = {};
}
} // namespace aurea::gles
