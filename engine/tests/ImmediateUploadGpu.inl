#if !defined(AUREA_TEST_GLES)
namespace aurea::vk {
struct ImmediateSubmissionTestAccess {
    static Status submit(Backend& backend) {
        return backend.submit_immediate([](Backend&, VkCommandBuffer, void*) {}, nullptr, 0);
    }
    static void collect(Backend& backend) { backend.collect_immediate(); }
    static usize pending(const Backend& backend) { return backend.pendingImmediate_.size(); }
    static VkFence frame_fence(const Backend& backend, u64 frameNumber) {
        for (u32 i = 0; i < backend.framesInFlight_; ++i)
            if (backend.frames_[i].frameNumber == frameNumber) return backend.frames_[i].fence;
        return VK_NULL_HANDLE;
    }
    static VkImage native_image(const Backend& backend, TextureHandle texture) {
        const auto* object = backend.textures_.get(texture.id);
        return object ? object->image : VK_NULL_HANDLE;
    }
    static usize deferred(const Backend& backend) {
        usize count = 0;
        for (const auto& submission : backend.pendingImmediate_) count += submission.deferred.size();
        return count;
    }
    static bool buffer_needs_staging(const Backend& backend, BufferHandle buffer) {
        const auto* object = backend.buffers_.get(buffer.id);
        return object && !object->alloc.mapped;
    }
};
}

namespace {
// The command buffer, submission and fence are real Vulkan objects. Only the
// CPU's observation of completion is delayed: a successful/timeout poll for one
// chosen fence reports TIMEOUT until release(). Never block the GPU with an
// unsignaled host event (vkSetEvent while its wait is pending is invalid), or
// depend on GPU speed to hit the timeout. Tests run serially; other fences and
// all Vulkan calls still go through the real validation/driver dispatch.
struct DelayedFenceObservation {
    PFN_vkWaitForFences original = vk::vkWaitForFences;
    VkDevice device = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    inline static DelayedFenceObservation* active = nullptr;
    explicit DelayedFenceObservation(VkDevice target) : device(target) {
        active = this;
        vk::vkWaitForFences = &wait;
    }
    ~DelayedFenceObservation() { release(); }
    DelayedFenceObservation(const DelayedFenceObservation&) = delete;
    DelayedFenceObservation& operator=(const DelayedFenceObservation&) = delete;
    void release() {
        if (active == this) {
            vk::vkWaitForFences = original;
            active = nullptr;
        }
    }
    VkResult await_real_completion() const {
        return fence ? original(device, 1, &fence, VK_TRUE, 1'000'000'000ull) : VK_ERROR_UNKNOWN;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice device, u32 count, const VkFence* fences,
                                               VkBool32 all, u64 timeout) {
        auto& gate = *active;
        const VkResult result = gate.original(device, count, fences, all, timeout);
        if (device == gate.device && count == 1 && timeout == 0) {
            if (!gate.fence) gate.fence = fences[0];
            if (fences[0] == gate.fence && (result == VK_SUCCESS || result == VK_TIMEOUT)) return VK_TIMEOUT;
        }
        return result;
    }
};
}

AUREA_TEST(UploadLifetimeGpu, TimedOutSubmissionKeepsItsFenceAndDeferredResourcesUntilCompletion) {
    AUREA_REQUIRE_GPU();
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 validationErrors = vk::Backend::validation_errors();
    DelayedFenceObservation completion(backend.device());
    const auto status = vk::ImmediateSubmissionTestAccess::submit(backend);
    AUREA_CHECK(status.code() == Errc::Timeout);
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 1u);
    bool released = false;
    backend.defer_until_gpu_done([](void* flag) { *static_cast<bool*>(flag) = true; }, &released);
    AUREA_CHECK(!released);
    // More immediate work must apply backpressure instead of adding submissions.
    AUREA_CHECK(vk::ImmediateSubmissionTestAccess::submit(backend).code() == Errc::Timeout);
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 1u);
    completion.release();
    AUREA_CHECK_EQ(completion.await_real_completion(), VK_SUCCESS);
    vk::ImmediateSubmissionTestAccess::collect(backend);
    AUREA_CHECK(released);
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 0u);
    backend.wait_idle();
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
}

AUREA_TEST(UploadLifetimeGpu, RejectedRetriesDoNotAllocateStagingAndOpenFrameUploadsStillWork) {
    AUREA_REQUIRE_GPU();
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    TextureDesc desc;
    desc.width = desc.height = 128;
    desc.format = SurfaceFormat::RGBA8;
    desc.sampled = desc.transferSrc = desc.transferDst = true;
    const auto texture = backend.create_texture(desc), levelTexture = backend.create_texture(desc);
    BufferDesc bufferDesc;
    bufferDesc.bytes = 128 * 128 * 4;
    bufferDesc.usage = BufferUsage::Storage;
    bufferDesc.access = MemoryAccess::GpuOnly;
    const auto buffer = backend.create_buffer(bufferDesc);
    AUREA_CHECK(texture.ok() && levelTexture.ok() && buffer.ok());
    if (!texture.ok() || !levelTexture.ok() || !buffer.ok()) return;
    std::vector<u8> pixels(bufferDesc.bytes, 173), readback(bufferDesc.bytes);
    const bool bufferNeedsStaging = vk::ImmediateSubmissionTestAccess::buffer_needs_staging(backend, *buffer);
    backend.wait_idle();
    const u32 validationErrors = vk::Backend::validation_errors();
    DelayedFenceObservation completion(backend.device());
    AUREA_CHECK(vk::ImmediateSubmissionTestAccess::submit(backend).code() == Errc::Timeout);
    const auto before = backend.memory_stats();
    const auto deferredBefore = vk::ImmediateSubmissionTestAccess::deferred(backend);
    u32 rejected = 0, writtenDirectly = 0;
    for (u32 attempt = 0; attempt < 24; ++attempt) {
        rejected += backend.upload_texture(*texture, pixels.data(), 128 * 4).code() == Errc::Timeout;
        rejected += backend.upload_texture_level(*levelTexture, 0, 0, pixels.data(), pixels.size()).code() == Errc::Timeout;
        rejected += backend.read_texture(*texture, readback.data(), 128 * 4).code() == Errc::Timeout;
        const auto write = backend.write_buffer(*buffer, 0, pixels.data(), pixels.size());
        if (bufferNeedsStaging) rejected += write.code() == Errc::Timeout;
        else writtenDirectly += write.ok(); // UMA can map device-local memory directly.
    }
    const auto after = backend.memory_stats();
    AUREA_CHECK_EQ(rejected, 24u * (bufferNeedsStaging ? 4u : 3u));
    AUREA_CHECK_EQ(writtenDirectly, bufferNeedsStaging ? 0u : 24u);
    AUREA_CHECK_EQ(after.usedBytes, before.usedBytes);
    AUREA_CHECK_EQ(after.allocationCount, before.allocationCount);
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::deferred(backend), deferredBefore);
    // The preceding submission's completion is still unobserved while valid
    // uploads are recorded in a later frame, without adding immediate staging.
    FrameBegin frame;
    const bool frameOpened = backend.begin_offscreen_frame(frame).ok();
    AUREA_CHECK(frameOpened);
    if (frameOpened) {
        AUREA_CHECK(backend.upload_texture(*texture, pixels.data(), 128 * 4).ok());
        AUREA_CHECK(backend.upload_texture_level(*levelTexture, 0, 0, pixels.data(), pixels.size()).ok());
        AUREA_CHECK(backend.end_frame().ok());
    }
    completion.release();
    AUREA_CHECK_EQ(completion.await_real_completion(), VK_SUCCESS);
    vk::ImmediateSubmissionTestAccess::collect(backend);
    backend.wait_idle();
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 0u);
    if (frameOpened) {
        AUREA_CHECK(backend.read_texture(*texture, readback.data(), 128 * 4).ok());
        AUREA_CHECK(readback == pixels);
        AUREA_CHECK(backend.read_texture(*levelTexture, readback.data(), 128 * 4).ok());
        AUREA_CHECK(readback == pixels);
    }
    backend.destroy_buffer(*buffer);
    backend.destroy_texture(*texture);
    backend.destroy_texture(*levelTexture);
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
}
#endif

AUREA_TEST(UploadLifetimeGpu, ColdEnvironmentAndMipUploadsUseTheOpenExportFrame) {
    AUREA_REQUIRE_GPU();
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    TextureDesc desc;
    desc.width = desc.height = 32; desc.mipLevels = 6;
    desc.format = SurfaceFormat::RGBA8;
    desc.sampled = desc.transferSrc = desc.transferDst = true;
    const auto target = backend.create_texture(desc);
    AUREA_CHECK(target.ok()); if (!target.ok()) return;
    std::vector<u8> pixels(32 * 32 * 4);
    for (usize i = 0; i < pixels.size(); i += 4) { pixels[i] = 173; pixels[i+1] = 91; pixels[i+2] = 38; pixels[i+3] = 255; }
    FrameBegin frame;
    AUREA_CHECK(backend.begin_offscreen_frame(frame).ok());
    AUREA_CHECK(backend.upload_texture_level(*target, 0, 0, pixels.data(), pixels.size()).ok());
    AUREA_CHECK_EQ(backend.memory_stats().uploadBytesThisFrame, pixels.size());
    // Mips must follow the upload in the same command buffer, never overtake it.
    AUREA_CHECK(backend.generate_mipmaps(*target).ok());
    auto& scene = isolated.renderer.scene_renderer();
    scene.set_environment_quality({16, 16, 1}, {16, 16, 1});
    scene3d::SceneEnvironment environment;
    scene.finish_environment(environment);
    AUREA_CHECK_EQ(scene.environment_uploads(), 1u);
    AUREA_CHECK(backend.memory_stats().uploadBytesThisFrame > pixels.size());
    AUREA_CHECK(backend.end_frame().ok());
    backend.wait_idle();
    std::vector<u8> actual(pixels.size());
    AUREA_CHECK(backend.read_texture(*target, actual.data(), 32 * 4).ok());
    AUREA_CHECK(actual == pixels);
    backend.destroy_texture(*target);
}
