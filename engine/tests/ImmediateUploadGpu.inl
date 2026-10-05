#if !defined(AUREA_TEST_GLES)
namespace aurea::vk {
struct ImmediateSubmissionTestAccess {
    static Status wait_event(Backend& backend, VkEvent event) {
        return backend.submit_immediate([](Backend& backend, VkCommandBuffer command, void* context) {
            const auto event = *static_cast<VkEvent*>(context);
            const auto waitEvents = reinterpret_cast<PFN_vkCmdWaitEvents>(
                vkGetDeviceProcAddr(backend.device(), "vkCmdWaitEvents"));
            waitEvents(command, 1, &event, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, nullptr, 0, nullptr, 0, nullptr);
        }, &event, 0);
    }
    static usize pending(const Backend& backend) { return backend.pendingImmediate_.size(); }
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

AUREA_TEST(UploadLifetimeGpu, TimedOutSubmissionKeepsItsFenceAndDeferredResourcesUntilCompletion) {
    AUREA_REQUIRE_GPU();
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const auto createEvent = reinterpret_cast<PFN_vkCreateEvent>(vk::vkGetDeviceProcAddr(backend.device(), "vkCreateEvent"));
    const auto setEvent = reinterpret_cast<PFN_vkSetEvent>(vk::vkGetDeviceProcAddr(backend.device(), "vkSetEvent"));
    const auto destroyEvent = reinterpret_cast<PFN_vkDestroyEvent>(vk::vkGetDeviceProcAddr(backend.device(), "vkDestroyEvent"));
    VkEventCreateInfo info{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
    VkEvent event = VK_NULL_HANDLE;
    AUREA_CHECK_EQ(createEvent(backend.device(), &info, nullptr, &event), VK_SUCCESS);
    if (!event) return;
    // Poll one real blocked submission with a zero timeout, then signal it.
    // It must remain valid and later complete, rather than destroy in-flight
    // command memory as the old VK_TIMEOUT path did on a heavy export. Never
    // hold the desktop GPU for seconds (which could trigger its watchdog).
    const auto status = vk::ImmediateSubmissionTestAccess::wait_event(backend, event);
    AUREA_CHECK(status.code() == Errc::Timeout);
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 1u);
    bool released = false;
    backend.defer_until_gpu_done([](void* flag) { *static_cast<bool*>(flag) = true; }, &released);
    AUREA_CHECK(!released);
    // More immediate work must apply backpressure instead of adding submissions.
    AUREA_CHECK(vk::ImmediateSubmissionTestAccess::wait_event(backend, event).code() == Errc::Timeout);
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 1u);
    AUREA_CHECK_EQ(setEvent(backend.device(), event), VK_SUCCESS);
    backend.wait_idle();
    AUREA_CHECK(released);
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 0u);
    destroyEvent(backend.device(), event, nullptr);
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
    const auto createEvent = reinterpret_cast<PFN_vkCreateEvent>(vk::vkGetDeviceProcAddr(backend.device(), "vkCreateEvent"));
    const auto setEvent = reinterpret_cast<PFN_vkSetEvent>(vk::vkGetDeviceProcAddr(backend.device(), "vkSetEvent"));
    const auto destroyEvent = reinterpret_cast<PFN_vkDestroyEvent>(vk::vkGetDeviceProcAddr(backend.device(), "vkDestroyEvent"));
    VkEventCreateInfo eventInfo{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
    VkEvent event = VK_NULL_HANDLE;
    AUREA_CHECK_EQ(createEvent(backend.device(), &eventInfo, nullptr, &event), VK_SUCCESS);
    if (!event) return;
    AUREA_CHECK(vk::ImmediateSubmissionTestAccess::wait_event(backend, event).code() == Errc::Timeout);
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
    // The preceding submission may be blocked while valid uploads are recorded
    // in a later frame. Queue ordering, not CPU waiting, protects those uploads.
    FrameBegin frame;
    const bool frameOpened = backend.begin_offscreen_frame(frame).ok();
    AUREA_CHECK(frameOpened);
    if (frameOpened) {
        AUREA_CHECK(backend.upload_texture(*texture, pixels.data(), 128 * 4).ok());
        AUREA_CHECK(backend.upload_texture_level(*levelTexture, 0, 0, pixels.data(), pixels.size()).ok());
        AUREA_CHECK(backend.end_frame().ok());
    }
    // Always release the event, including failed checks: no desktop GPU hang.
    AUREA_CHECK_EQ(setEvent(backend.device(), event), VK_SUCCESS);
    backend.wait_idle();
    AUREA_CHECK_EQ(vk::ImmediateSubmissionTestAccess::pending(backend), 0u);
    if (frameOpened) {
        AUREA_CHECK(backend.read_texture(*texture, readback.data(), 128 * 4).ok());
        AUREA_CHECK(readback == pixels);
        AUREA_CHECK(backend.read_texture(*levelTexture, readback.data(), 128 * 4).ok());
        AUREA_CHECK(readback == pixels);
    }
    destroyEvent(backend.device(), event, nullptr);
    backend.destroy_buffer(*buffer);
    backend.destroy_texture(*texture);
    backend.destroy_texture(*levelTexture);
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
