#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
#if defined(__ANDROID__)
#include <android/hardware_buffer.h>

namespace {
struct ExternalImportBuffer {
    AHardwareBuffer* buffer = nullptr;
    ~ExternalImportBuffer() { if (buffer) AHardwareBuffer_release(buffer); }
    bool initialize() {
        AHardwareBuffer_Desc desc{};
        desc.width = desc.height = 64;
        desc.layers = 1;
        desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
        if (AHardwareBuffer_allocate(&desc, &buffer) != 0) return false;
        AHardwareBuffer_describe(buffer, &desc);
        void* mapped = nullptr;
        if (AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &mapped) != 0)
            return false;
        for (u32 y = 0; y < 64; ++y) for (u32 x = 0; x < 64; ++x) {
            auto* pixel = static_cast<u8*>(mapped) + (static_cast<usize>(y) * desc.stride + x) * 4;
            pixel[0] = 37; pixel[1] = 113; pixel[2] = 219; pixel[3] = 255;
        }
        return AHardwareBuffer_unlock(buffer, nullptr) == 0;
    }
};

// Observe actual driver destruction while delaying only the CPU's fence poll.
// The submission remains valid and runs normally, unlike an unsignaled VkEvent.
struct ExternalImageDestructionObservation {
    PFN_vkDestroyImage original = vk::vkDestroyImage;
    VkImage watched = VK_NULL_HANDLE;
    bool destroyed = false;
    inline static ExternalImageDestructionObservation* active = nullptr;
    explicit ExternalImageDestructionObservation(VkImage image) : watched(image) {
        active = this;
        vk::vkDestroyImage = &destroy;
    }
    ~ExternalImageDestructionObservation() { vk::vkDestroyImage = original; active = nullptr; }
    static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice device, VkImage image,
                                               const VkAllocationCallbacks* allocator) {
        auto& observation = *active;
        if (image == observation.watched) observation.destroyed = true;
        observation.original(device, image, allocator);
    }
};

// Reimporting the same immutable decoder frame after sampling must leave its
// existing read layout intact. Observe real recorded barriers, forwarding all
// calls to the driver so both layout validation and pixel checks still run.
struct ExternalImageBarrierObservation {
    PFN_vkCmdPipelineBarrier original = vk::vkCmdPipelineBarrier;
    VkImage watched = VK_NULL_HANDLE;
    u32 count = 0;
    inline static ExternalImageBarrierObservation* active = nullptr;
    explicit ExternalImageBarrierObservation(VkImage image) : watched(image) {
        active = this; vk::vkCmdPipelineBarrier = &barrier;
    }
    ~ExternalImageBarrierObservation() { vk::vkCmdPipelineBarrier = original; active = nullptr; }
    static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer command, VkPipelineStageFlags source,
        VkPipelineStageFlags destination, VkDependencyFlags dependencies, u32 memoryCount,
        const VkMemoryBarrier* memory, u32 bufferCount, const VkBufferMemoryBarrier* buffers,
        u32 imageCount, const VkImageMemoryBarrier* images) {
        auto& observation = *active;
        for (u32 i = 0; i < imageCount; ++i)
            if (images[i].image == observation.watched) ++observation.count;
        observation.original(command, source, destination, dependencies,
            memoryCount, memory, bufferCount, buffers, imageCount, images);
    }
};

bool copy_external_import(Gpu& g, FrameBegin& frame, const ExternalTexture& imported, TextureHandle target) {
    auto key = PipelineKey::fullscreen(ShaderId::common_copy_frag, SurfaceFormat::RGBA8);
    key.immutableSampler = imported.sampler.id;
    const auto pipeline = g.renderer.shaders().pipeline(key);
    if (!pipeline.ok()) return false;
    auto& commands = *frame.commands;
    commands.barrier(imported.texture, ResourceState::ShaderRead);
    commands.barrier(target, ResourceState::ColorAttachment, true);
    RenderPassBegin pass;
    pass.color = target; pass.load = LoadOp::DontCare;
    commands.begin_render_pass(pass);
    commands.bind_pipeline(*pipeline);
    commands.bind_texture(0, imported.texture, imported.sampler);
    const Vec4 identity{1, 1, 0, 0};
    commands.set_uniforms(&identity, sizeof(identity));
    commands.draw(3);
    commands.end_render_pass();
    return true;
}
}
#endif

AUREA_TEST(ExternalImportsGpu, TrimRetiresNativeBuffersOnTheirLastFenceAndReimportPreservesPixels) {
#if defined(__ANDROID__)
    AUREA_REQUIRE_GPU();
    ExternalImportBuffer native;
    const bool initialized = native.initialize();
    AUREA_CHECK(initialized); if (!initialized) return;
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 errors = vk::Backend::validation_errors();
    TextureDesc targetDesc;
    targetDesc.width = targetDesc.height = 64;
    targetDesc.format = SurfaceFormat::RGBA8;
    targetDesc.sampled = targetDesc.renderTarget = targetDesc.transferSrc = true;
    const auto firstTarget = backend.create_texture(targetDesc);
    const auto secondTarget = backend.create_texture(targetDesc);
    AUREA_CHECK(firstTarget.ok() && secondTarget.ok());
    if (!firstTarget.ok() || !secondTarget.ok()) return;
    ExternalImageDesc desc;
    desc.nativeHandle = native.buffer;
    desc.width = desc.height = 64;
    desc.format = PixelFormat::RGBA8;
    desc.fullRange = true;

    FrameBegin first;
    const bool firstOpened = backend.begin_offscreen_frame(first).ok();
    AUREA_CHECK(firstOpened); if (!firstOpened) return;
    const auto original = backend.import_external_image(desc);
    AUREA_CHECK_MSG(original.ok(), "Android RGBA8 AHardwareBuffer import must succeed");
    if (!original.ok()) { (void)backend.end_frame(); return; }
    const auto reused = backend.import_external_image(desc);
    AUREA_CHECK(reused.ok());
    if (reused.ok()) AUREA_CHECK_EQ(reused->texture.id, original->texture.id);
    AUREA_CHECK(original->rgb);
    AUREA_CHECK(copy_external_import(isolated, first, *original, *firstTarget));
    {
        ExternalImageBarrierObservation barriers(vk::ImmediateSubmissionTestAccess::native_image(backend, original->texture));
        const auto sampledAgain = backend.import_external_image(desc);
        AUREA_CHECK(sampledAgain.ok());
        if (sampledAgain.ok()) {
            AUREA_CHECK_EQ(sampledAgain->texture.id, original->texture.id);
            AUREA_CHECK(copy_external_import(isolated, first, *sampledAgain, *secondTarget));
        }
        AUREA_CHECK_MSG(barriers.count == 0, "reimport within one frame must preserve acquired shader-read layout");
    }
    // Trimming cannot invalidate command recording in the current frame.
    AUREA_CHECK_EQ(backend.trim_external_images(), 0u);
    AUREA_CHECK_EQ(backend.texture_desc(original->texture).width, 64u);
    AUREA_CHECK(backend.end_frame().ok());

    ExternalImageDestructionObservation destruction(vk::ImmediateSubmissionTestAccess::native_image(backend, original->texture));
    AUREA_CHECK(destruction.watched != VK_NULL_HANDLE);
    DelayedFenceObservation completion(backend.device());
    completion.fence = vk::ImmediateSubmissionTestAccess::frame_fence(backend, first.frameNumber);
    AUREA_CHECK(completion.fence != VK_NULL_HANDLE);
    AUREA_CHECK_EQ(backend.trim_external_images(), 1u);
    AUREA_CHECK_EQ(backend.texture_desc(original->texture).width, 0u);
    AUREA_CHECK(!destruction.destroyed);
    AUREA_CHECK_EQ(backend.trim_external_images(), 0u);

    // The original native buffer is still alive on its fence, but the cache
    // lookup is gone. Reimport must create a new handle without changing color.
    FrameBegin second;
    const bool secondOpened = backend.begin_offscreen_frame(second).ok();
    AUREA_CHECK(secondOpened);
    TextureHandle replacement{};
    if (secondOpened) {
        const auto reimported = backend.import_external_image(desc);
        AUREA_CHECK(reimported.ok());
        if (reimported.ok()) {
            replacement = reimported->texture;
            AUREA_CHECK(replacement.id != original->texture.id);
            AUREA_CHECK(copy_external_import(isolated, second, *reimported, *secondTarget));
        }
        AUREA_CHECK(backend.end_frame().ok());
    }
    AUREA_CHECK(!destruction.destroyed);
    completion.release();
    AUREA_CHECK_EQ(completion.await_real_completion(), VK_SUCCESS);
    backend.wait_idle();
    AUREA_CHECK(destruction.destroyed);
    AUREA_CHECK_EQ(backend.texture_desc(replacement).width, 64u);
    std::vector<u8> before(64 * 64 * 4), after(before.size());
    AUREA_CHECK(backend.read_texture(*firstTarget, before.data(), 64 * 4).ok());
    AUREA_CHECK(backend.read_texture(*secondTarget, after.data(), 64 * 4).ok());
    AUREA_CHECK(before == after);
    bool expected = true;
    constexpr u8 color[]{37, 113, 219, 255};
    for (usize i = 0; i < before.size(); ++i)
        expected &= std::abs(static_cast<int>(before[i]) - color[i % 4]) <= 1;
    AUREA_CHECK_MSG(expected, "native buffer sampling must preserve its actual RGBA values");
    AUREA_CHECK_EQ(backend.trim_external_images(), 1u);
    AUREA_CHECK_EQ(backend.texture_desc(replacement).width, 0u);
    AUREA_CHECK_EQ(backend.texture_desc(*firstTarget).width, 64u);
    AUREA_CHECK_EQ(backend.trim_external_images(), 0u);
    backend.destroy_texture(*firstTarget);
    backend.destroy_texture(*secondTarget);
    backend.wait_idle();
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
#else
    std::printf("(Android AHardwareBuffer required: not executed on this host) ");
#endif
}
#endif
