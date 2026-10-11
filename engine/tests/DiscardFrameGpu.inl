#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
namespace aurea::vk {
struct DiscardFrameTestAccess {
    static Status attach_test_wsi(Backend& b) {
        Texture metadata;
        metadata.ownsImage = false;
        metadata.desc.width = metadata.desc.height = 16;
        b.swapTextures_.push_back(b.textures_.add(std::move(metadata)));
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        const VkResult result = vkCreateSemaphore(b.device_, &info, nullptr, &semaphore);
        if (result != VK_SUCCESS) { b.destroy_swapchain(); return Errc::OutOfMemory; }
        b.renderDone_.push_back(semaphore);
        // No native window is available on the headless host. WSI calls are
        // intercepted below; command buffers, queues, binary semaphores and
        // fences remain real Vulkan objects with real driver validation.
        b.swapchain_ = reinterpret_cast<VkSwapchainKHR>(b.device_);
        b.swapExtent_ = {16, 16};
        b.swapFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
        return OkStatus;
    }
    static void mark_unused_backbuffer_present(Backend& b) {
        auto* image = b.textures_.get(b.swapTextures_[b.imageIndex_]);
        // The test image carries metadata only, so it records no image
        // barriers. Actual visible-pixel continuity has separate device tests.
        image->state = ResourceState::Present;
        image->layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    static bool held(const Backend& b) { return b.heldImage_; }
    static VkQueue queue(const Backend& b) { return b.queue_; }
    static void detach_test_wsi(Backend& b) {
        b.wait_idle();
        b.swapchain_ = VK_NULL_HANDLE;
        b.destroy_swapchain();
    }
};
}
namespace {
struct DiscardFrameWsiDispatch {
    vk::Backend& backend;
    PFN_vkAcquireNextImageKHR originalAcquire = vk::vkAcquireNextImageKHR;
    PFN_vkQueuePresentKHR originalPresent = vk::vkQueuePresentKHR;
    PFN_vkQueueSubmit originalSubmit = vk::vkQueueSubmit;
    inline static DiscardFrameWsiDispatch* active = nullptr;
    u32 acquires = 0, presents = 0, frameWaits = 0, frameSignals = 0;
    bool attached = false;
    explicit DiscardFrameWsiDispatch(vk::Backend& b) : backend(b) {
        attached = vk::DiscardFrameTestAccess::attach_test_wsi(b).ok();
        if (!attached) return;
        active = this;
        vk::vkAcquireNextImageKHR = acquire;
        vk::vkQueuePresentKHR = present;
        vk::vkQueueSubmit = submit;
    }
    ~DiscardFrameWsiDispatch() {
        if (!attached) return;
        vk::DiscardFrameTestAccess::detach_test_wsi(backend);
        vk::vkAcquireNextImageKHR = originalAcquire;
        vk::vkQueuePresentKHR = originalPresent;
        vk::vkQueueSubmit = originalSubmit;
        active = nullptr;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice, VkSwapchainKHR, u64,
                                                  VkSemaphore semaphore, VkFence, u32* image) {
        ++active->acquires;
        *image = 0;
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &semaphore;
        return active->originalSubmit(vk::DiscardFrameTestAccess::queue(active->backend), 1, &info, VK_NULL_HANDLE);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue, const VkPresentInfoKHR* info) {
        ++active->presents;
        std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkSubmitInfo consume{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        consume.waitSemaphoreCount = info->waitSemaphoreCount;
        consume.pWaitSemaphores = info->pWaitSemaphores;
        consume.pWaitDstStageMask = stages.data();
        return active->originalSubmit(queue, 1, &consume, VK_NULL_HANDLE);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue queue, u32 count, const VkSubmitInfo* info, VkFence fence) {
        for (u32 i = 0; i < count; ++i) {
            if (!info[i].commandBufferCount) continue;
            active->frameWaits += info[i].waitSemaphoreCount;
            active->frameSignals += info[i].signalSemaphoreCount;
        }
        return active->originalSubmit(queue, count, info, fence);
    }
};
}

AUREA_TEST(PreviewDiscardGpu, FailedFramesReuseOneImageAndConsumeAcquireExactlyOnce) {
    AUREA_REQUIRE_GPU();
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 errors = vk::Backend::validation_errors();
    DiscardFrameWsiDispatch wsi(backend);
    AUREA_CHECK(wsi.attached); if (!wsi.attached) return;
    AUREA_CHECK(backend.can_discard_frame());
    TextureHandle heldImage;
    u32 retired = 0;
    for (u32 retry = 0; retry < 30; ++retry) {
        FrameBegin failed;
        AUREA_CHECK(backend.begin_frame(failed).ok());
        AUREA_CHECK(failed.backbuffer.valid());
        if (!retry) heldImage = failed.backbuffer;
        AUREA_CHECK_EQ(failed.backbuffer.id, heldImage.id);
        vk::DiscardFrameTestAccess::mark_unused_backbuffer_present(backend);
        backend.defer_until_gpu_done([](void* value) { ++*static_cast<u32*>(value); }, &retired);
        AUREA_CHECK(backend.discard_frame().ok());
        AUREA_CHECK(vk::DiscardFrameTestAccess::held(backend));
        AUREA_CHECK(backend.wait_frame(failed.frameNumber, 2'000'000'000ull).ok());

        // Export/capture can run between a failed preview and its replacement.
        // It must neither acquire the window nor release the retained image.
        FrameBegin capture;
        AUREA_CHECK(backend.begin_offscreen_frame(capture).ok());
        AUREA_CHECK(!capture.backbuffer.valid());
        AUREA_CHECK(backend.end_frame().ok());
        AUREA_CHECK(vk::DiscardFrameTestAccess::held(backend));
    }
    AUREA_CHECK_EQ(wsi.acquires, 1u);
    AUREA_CHECK_EQ(wsi.presents, 0u);
    AUREA_CHECK_EQ(wsi.frameWaits, 1u);
    AUREA_CHECK_EQ(wsi.frameSignals, 0u);
    FrameBegin recovered;
    AUREA_CHECK(backend.begin_frame(recovered).ok());
    AUREA_CHECK_EQ(recovered.backbuffer.id, heldImage.id);
    vk::DiscardFrameTestAccess::mark_unused_backbuffer_present(backend);
    AUREA_CHECK(backend.end_frame().ok());
    AUREA_CHECK(!vk::DiscardFrameTestAccess::held(backend));
    AUREA_CHECK_EQ(wsi.acquires, 1u);
    AUREA_CHECK_EQ(wsi.presents, 1u);
    AUREA_CHECK_EQ(wsi.frameWaits, 1u);
    AUREA_CHECK_EQ(wsi.frameSignals, 1u);
    // A subsequent successful picture must return to ordinary acquire/present
    // and legally reuse the same per-image renderDone binary semaphore.
    FrameBegin next;
    AUREA_CHECK(backend.begin_frame(next).ok());
    vk::DiscardFrameTestAccess::mark_unused_backbuffer_present(backend);
    AUREA_CHECK(backend.end_frame().ok());
    backend.wait_idle();
    AUREA_CHECK_EQ(wsi.acquires, 2u);
    AUREA_CHECK_EQ(wsi.presents, 2u);
    AUREA_CHECK_EQ(wsi.frameWaits, 2u);
    AUREA_CHECK_EQ(wsi.frameSignals, 2u);
    AUREA_CHECK_EQ(retired, 30u);
    AUREA_CHECK_EQ(vk::Backend::validation_errors(), errors);
}
#endif
