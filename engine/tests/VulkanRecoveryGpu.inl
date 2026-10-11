#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
namespace aurea::vk {
struct SwapchainFailureTestAccess {
    static Status create(Backend& b) { return b.create_swapchain(16, 16); }
    static void clear(Backend& b) { b.destroy_swapchain(); }
    static bool empty(const Backend& b) {
        return !b.swapchain_ && b.swapTextures_.empty() && b.renderDone_.empty();
    }
    static bool ready(const Backend& b) {
        if (!b.swapchain_ || b.swapTextures_.size() != 2 || b.renderDone_.size() != 2) return false;
        for (const auto sem : b.renderDone_) if (!sem) return false;
        return !b.swapchainDirty_;
    }
    static bool dirty(const Backend& b) { return b.swapchainDirty_; }
    static VkSurfaceKHR surface(Backend& b, VkSurfaceKHR replacement) {
        const auto previous = b.surface_; b.surface_ = replacement; return previous;
    }
};
}
namespace {
struct RequiredMapFailureObservation {
    vk::Backend& backend;
    PFN_vkMapMemory original;
    u32 failures = 1;
    inline static RequiredMapFailureObservation* active = nullptr;
    explicit RequiredMapFailureObservation(vk::Backend& b)
        : backend(b), original(b.device_dispatch().vkMapMemory) {
        active = this; backend.device_dispatch().vkMapMemory = &map;
    }
    ~RequiredMapFailureObservation() { backend.device_dispatch().vkMapMemory = original; active = nullptr; }
    static VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice device, VkDeviceMemory memory,
        VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** pointer) {
        if (active->failures) { --active->failures; *pointer = nullptr; return VK_ERROR_MEMORY_MAP_FAILED; }
        return active->original(device, memory, offset, size, flags, pointer);
    }
};
struct RecoveryGpuBuffers {
    vk::Backend& backend;
    BufferHandle source, target;
    ~RecoveryGpuBuffers() {
        backend.wait_idle();
        if (source.valid()) backend.destroy_buffer(source);
        if (target.valid()) backend.destroy_buffer(target);
    }
};
// Headless WSI enumeration/creation is intercepted. Image views, memory and
// binary semaphores are real Vulkan objects; this is not native-display proof.
struct SwapchainFailureObservation {
    enum class Failure { None, Formats, Create, Count, Images, Empty, View, Semaphore };
    vk::Backend& backend;
    vk::InstanceDispatch instance;
    vk::DeviceDispatch device;
    VkSurfaceKHR previousSurface = VK_NULL_HANDLE;
    std::array<TextureHandle, 2> sources{};
    std::array<VkImage, 2> images{};
    Failure failure = Failure::None;
    VkResult injected = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkSwapchainKHR lastOld = VK_NULL_HANDLE;
    u32 createdViews = 0, destroyedViews = 0, createdSemaphores = 0, destroyedSemaphores = 0;
    u32 chains = 0, destroyedChains = 0, viewCalls = 0, semaphoreCalls = 0;
    bool installed = false;
    inline static SwapchainFailureObservation* active = nullptr;
    explicit SwapchainFailureObservation(vk::Backend& b)
        : backend(b), instance(b.instance_dispatch()), device(b.device_dispatch()) {
        TextureDesc desc;
        desc.width = desc.height = 16; desc.format = SurfaceFormat::RGBA8;
        desc.renderTarget = true;
        for (u32 i = 0; i < sources.size(); ++i) {
            const auto result = backend.create_texture(desc);
            if (!result.ok()) return;
            sources[i] = *result; images[i] = backend.texture(result->id)->image;
        }
#if VK_USE_64_BIT_PTR_DEFINES
        const auto surface = reinterpret_cast<VkSurfaceKHR>(backend.device());
#else
        const auto surface = static_cast<VkSurfaceKHR>(reinterpret_cast<uintptr_t>(backend.device()));
#endif
        previousSurface = vk::SwapchainFailureTestAccess::surface(backend, surface);
        active = this; installed = true;
        backend.instance_dispatch().vkGetPhysicalDeviceSurfaceCapabilitiesKHR = &capabilities;
        backend.instance_dispatch().vkGetPhysicalDeviceSurfaceFormatsKHR = &formats;
        backend.device_dispatch().vkCreateSwapchainKHR = &create;
        backend.device_dispatch().vkDestroySwapchainKHR = &destroy;
        backend.device_dispatch().vkGetSwapchainImagesKHR = &get_images;
        backend.device_dispatch().vkCreateImageView = &view;
        backend.device_dispatch().vkDestroyImageView = &destroy_view;
        backend.device_dispatch().vkCreateSemaphore = &semaphore;
        backend.device_dispatch().vkDestroySemaphore = &destroy_semaphore;
    }
    ~SwapchainFailureObservation() {
        backend.wait_idle();
        if (installed) {
            vk::SwapchainFailureTestAccess::clear(backend);
            vk::SwapchainFailureTestAccess::surface(backend, previousSurface);
            backend.instance_dispatch() = instance; backend.device_dispatch() = device;
            active = nullptr;
        }
        for (const auto source : sources) if (source.valid()) backend.destroy_texture(source);
    }
    void inject(Failure value, VkResult result = VK_ERROR_OUT_OF_DEVICE_MEMORY) {
        failure = value; injected = result; viewCalls = semaphoreCalls = 0;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL capabilities(VkPhysicalDevice, VkSurfaceKHR,
        VkSurfaceCapabilitiesKHR* out) {
        *out = {}; out->minImageCount = out->maxImageCount = 2;
        out->currentExtent = out->minImageExtent = out->maxImageExtent = {16, 16};
        out->supportedTransforms = out->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        out->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        out->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        return VK_SUCCESS;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL formats(VkPhysicalDevice, VkSurfaceKHR,
        u32* count, VkSurfaceFormatKHR* out) {
        if (active->failure == Failure::Formats) return active->injected;
        *count = 1; if (out) *out = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        return VK_SUCCESS;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice, const VkSwapchainCreateInfoKHR* info,
        const VkAllocationCallbacks*, VkSwapchainKHR* out) {
        active->lastOld = info->oldSwapchain;
        if (active->failure == Failure::Create) { *out = VK_NULL_HANDLE; return active->injected; }
        ++active->chains;
#if VK_USE_64_BIT_PTR_DEFINES
        *out = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(active->chains));
#else
        *out = static_cast<VkSwapchainKHR>(active->chains);
#endif
        return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*) {
        ++active->destroyedChains;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL get_images(VkDevice, VkSwapchainKHR,
        u32* count, VkImage* out) {
        if (active->failure == Failure::Count && !out) return active->injected;
        if (active->failure == Failure::Images && out) return active->injected;
        if (active->failure == Failure::Empty) { *count = 0; return VK_SUCCESS; }
        *count = 2;
        if (out) std::copy(active->images.begin(), active->images.end(), out);
        return VK_SUCCESS;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL view(VkDevice device, const VkImageViewCreateInfo* info,
        const VkAllocationCallbacks* callbacks, VkImageView* out) {
        if (++active->viewCalls == 2 && active->failure == Failure::View) {
            *out = VK_NULL_HANDLE; return active->injected;
        }
        const auto result = active->device.vkCreateImageView(device, info, callbacks, out);
        if (result == VK_SUCCESS) ++active->createdViews;
        return result;
    }
    static VKAPI_ATTR void VKAPI_CALL destroy_view(VkDevice device, VkImageView view,
        const VkAllocationCallbacks* callbacks) {
        ++active->destroyedViews; active->device.vkDestroyImageView(device, view, callbacks);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL semaphore(VkDevice device, const VkSemaphoreCreateInfo* info,
        const VkAllocationCallbacks* callbacks, VkSemaphore* out) {
        if (++active->semaphoreCalls == 2 && active->failure == Failure::Semaphore) {
            *out = VK_NULL_HANDLE; return active->injected;
        }
        const auto result = active->device.vkCreateSemaphore(device, info, callbacks, out);
        if (result == VK_SUCCESS) ++active->createdSemaphores;
        return result;
    }
    static VKAPI_ATTR void VKAPI_CALL destroy_semaphore(VkDevice device, VkSemaphore semaphore,
        const VkAllocationCallbacks* callbacks) {
        ++active->destroyedSemaphores; active->device.vkDestroySemaphore(device, semaphore, callbacks);
    }
};
}

AUREA_TEST(VulkanRecoveryGpu, MappingFailureReleasesRealBackingAndRecoveryCopiesExactBytesAfterFence) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const auto validationErrors = vk::Backend::validation_errors();
    RecoveryGpuBuffers resources{backend};
    RequiredMapFailureObservation failure(backend);
    const auto reserved = backend.allocator().reserved_bytes();
    const auto used = backend.allocator().used_bytes();
    const auto allocations = backend.allocator().allocation_count();
    BufferDesc desc;
    desc.bytes = 3u << 20; // Dedicated backing; an already-mapped slab cannot mask the fault.
    desc.usage = BufferUsage::TransferSrc; desc.access = MemoryAccess::Upload;
    const auto rejected = backend.create_buffer(desc);
    AUREA_CHECK(!rejected.ok());
    AUREA_CHECK_EQ(rejected.code(), Errc::OutOfMemory);
    AUREA_CHECK_EQ(failure.failures, 0u);
    AUREA_CHECK_EQ(backend.allocator().reserved_bytes(), reserved);
    AUREA_CHECK_EQ(backend.allocator().used_bytes(), used);
    AUREA_CHECK_EQ(backend.allocator().allocation_count(), allocations);
    const auto source = backend.create_buffer(desc);
    AUREA_CHECK(source.ok()); if (!source.ok()) return;
    resources.source = *source;
    std::array<u8, 4096> expected{};
    for (usize i = 0; i < expected.size(); ++i) expected[i] = static_cast<u8>((i * 73 + (i >> 5)) % 251);
    AUREA_CHECK(backend.write_buffer(*source, 0, expected.data(), expected.size()).ok());
    desc.access = MemoryAccess::Readback; desc.usage = BufferUsage::TransferDst;
    const auto target = backend.create_buffer(desc);
    AUREA_CHECK(target.ok()); if (!target.ok()) return;
    resources.target = *target;
    FrameBegin frame;
    const auto begun = backend.begin_offscreen_frame(frame);
    AUREA_CHECK(begun.ok()); if (!begun.ok()) return;
    const auto command = static_cast<vk::CommandListImpl*>(frame.commands)->handle();
    const VkBufferCopy region{0, 0, expected.size()};
    backend.device_dispatch().vkCmdCopyBuffer(command, backend.buffer(source->id)->buffer,
        backend.buffer(target->id)->buffer, 1, &region);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = backend.buffer(target->id)->buffer; barrier.size = expected.size();
    backend.device_dispatch().vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    AUREA_CHECK(backend.end_frame().ok());
    const auto completed = backend.wait_frame(frame.frameNumber, 5'000'000'000ull);
    AUREA_CHECK(completed.ok()); if (!completed.ok()) return;
    void* actual = nullptr;
    AUREA_CHECK(backend.map_buffer(*target, actual).ok());
    AUREA_CHECK(actual != nullptr);
    if (actual) for (usize i = 0; i < expected.size(); ++i) AUREA_CHECK_EQ(static_cast<u8*>(actual)[i], expected[i]);
    backend.unmap_buffer(*target);
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
}

AUREA_TEST(VulkanRecoveryGpu, PartialSwapchainFailuresReleaseRealViewsAndSemaphoresAndRemainRetryable) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const auto validationErrors = vk::Backend::validation_errors();
    SwapchainFailureObservation fault(backend);
    AUREA_CHECK(fault.installed); if (!fault.installed) return;
    using Failure = SwapchainFailureObservation::Failure;
    for (const auto stage : {Failure::Formats, Failure::Count, Failure::Images,
                            Failure::Empty, Failure::View, Failure::Semaphore}) {
        fault.inject(stage);
        const auto failed = vk::SwapchainFailureTestAccess::create(backend);
        AUREA_CHECK(!failed.ok());
        AUREA_CHECK(vk::SwapchainFailureTestAccess::empty(backend));
        AUREA_CHECK(vk::SwapchainFailureTestAccess::dirty(backend));
        AUREA_CHECK_EQ(fault.createdViews, fault.destroyedViews);
        AUREA_CHECK_EQ(fault.createdSemaphores, fault.destroyedSemaphores);
        AUREA_CHECK_EQ(fault.chains, fault.destroyedChains);
        fault.inject(Failure::None);
        AUREA_CHECK(vk::SwapchainFailureTestAccess::create(backend).ok());
        AUREA_CHECK(vk::SwapchainFailureTestAccess::ready(backend));
        AUREA_CHECK(fault.lastOld == VK_NULL_HANDLE);
        vk::SwapchainFailureTestAccess::clear(backend);
    }
    // Failed replacement retires the previous chain even when it creates no new one.
    fault.inject(Failure::None);
    AUREA_CHECK(vk::SwapchainFailureTestAccess::create(backend).ok());
    fault.inject(Failure::Create);
    AUREA_CHECK_EQ(vk::SwapchainFailureTestAccess::create(backend).code(), Errc::OutOfDeviceMemory);
    AUREA_CHECK(fault.lastOld != VK_NULL_HANDLE);
    AUREA_CHECK(vk::SwapchainFailureTestAccess::empty(backend));
    AUREA_CHECK_EQ(fault.chains, fault.destroyedChains);
    fault.inject(Failure::None);
    AUREA_CHECK(vk::SwapchainFailureTestAccess::create(backend).ok());
    AUREA_CHECK(fault.lastOld == VK_NULL_HANDLE);
    vk::SwapchainFailureTestAccess::clear(backend);
    // The driver error must retain its category instead of being mislabeled SurfaceLost.
    fault.inject(Failure::View, VK_ERROR_DEVICE_LOST);
    AUREA_CHECK_EQ(vk::SwapchainFailureTestAccess::create(backend).code(), Errc::DeviceLost);
    AUREA_CHECK(vk::SwapchainFailureTestAccess::empty(backend));
    AUREA_CHECK_EQ(fault.createdViews, fault.destroyedViews);
    AUREA_CHECK_EQ(fault.createdSemaphores, fault.destroyedSemaphores);
    AUREA_CHECK_EQ(fault.chains, fault.destroyedChains);
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
}
#endif
