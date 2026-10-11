#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
namespace {
// Exercise the production allocator's error paths without allocating gigabytes
// or claiming GPU execution. Only the Vulkan calls below are fault-injected.
struct AllocationFailureDriver {
    vk::InstanceDispatch instance{};
    vk::DeviceDispatch device{};
    vk::MemoryAllocator allocator;
    u32 allocated = 0, freed = 0, mapped = 0, unmapped = 0, failMaps = 0;
    std::array<u8, 4096> bytes{};
    inline static AllocationFailureDriver* active = nullptr;
    AllocationFailureDriver() {
        active = this;
        instance.vkGetPhysicalDeviceMemoryProperties = &memory_properties;
        instance.vkGetPhysicalDeviceProperties = &properties;
        device.vkAllocateMemory = &allocate;
        device.vkFreeMemory = &free;
        device.vkMapMemory = &map;
        device.vkUnmapMemory = &unmap;
        allocator.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE, instance, device);
    }
    ~AllocationFailureDriver() { allocator.shutdown(); active = nullptr; }
    static VKAPI_ATTR void VKAPI_CALL memory_properties(VkPhysicalDevice,
        VkPhysicalDeviceMemoryProperties* out) {
        *out = {};
        out->memoryTypeCount = out->memoryHeapCount = 1;
        out->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        out->memoryHeaps[0].size = 1ull << 30;
    }
    static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice, VkPhysicalDeviceProperties* out) {
        *out = {}; out->limits.bufferImageGranularity = 64;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice, const VkMemoryAllocateInfo*,
        const VkAllocationCallbacks*, VkDeviceMemory* out) {
#if VK_USE_64_BIT_PTR_DEFINES
        *out = reinterpret_cast<VkDeviceMemory>(static_cast<uintptr_t>(++active->allocated));
#else
        *out = static_cast<VkDeviceMemory>(++active->allocated);
#endif
        return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL free(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) {
        ++active->freed;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice, VkDeviceMemory, VkDeviceSize,
        VkDeviceSize, VkMemoryMapFlags, void** out) {
        ++active->mapped;
        *out = nullptr;
        if (active->failMaps) { --active->failMaps; return VK_ERROR_MEMORY_MAP_FAILED; }
        *out = active->bytes.data(); return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL unmap(VkDevice, VkDeviceMemory) { ++active->unmapped; }
    vk::Allocation request(bool dedicated = false, bool cpu = true) {
        const VkMemoryRequirements req{64, 64, 1};
        return allocator.allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, dedicated, "injected-map-failure", cpu);
    }
};
}

AUREA_TEST(VulkanAllocationFailure, DedicatedMappingFailureRejectsAndReleasesBacking) {
    AllocationFailureDriver driver;
    driver.failMaps = 1;
    const auto failed = driver.request(true);
    AUREA_CHECK(!failed.valid());
    AUREA_CHECK(failed.mapped == nullptr);
    AUREA_CHECK_EQ(driver.freed, 1u);
    AUREA_CHECK_EQ(driver.allocator.reserved_bytes(), 0u);
    AUREA_CHECK_EQ(driver.allocator.used_bytes(), 0u);
    AUREA_CHECK_EQ(driver.allocator.allocation_count(), 0u);
    // A transient failure must permit a later valid allocation and CPU write.
    const auto recovered = driver.request(true);
    AUREA_CHECK(recovered.valid() && recovered.mapped != nullptr);
    if (recovered.mapped) static_cast<u8*>(recovered.mapped)[0] = 173;
    AUREA_CHECK_EQ(driver.bytes[0], 173u);
    driver.allocator.free(recovered);
    AUREA_CHECK_EQ(driver.allocator.reserved_bytes(), 0u);
}

AUREA_TEST(VulkanAllocationFailure, NewSlabMappingFailureDoesNotPublishUnmappedAllocation) {
    AllocationFailureDriver driver;
    driver.failMaps = 1;
    const auto failed = driver.request();
    AUREA_CHECK(!failed.valid());
    AUREA_CHECK_EQ(driver.freed, 1u);
    AUREA_CHECK_EQ(driver.allocator.reserved_bytes(), 0u);
    AUREA_CHECK_EQ(driver.allocator.used_bytes(), 0u);
    AUREA_CHECK_EQ(driver.allocator.allocation_count(), 0u);
    const auto recovered = driver.request();
    AUREA_CHECK(recovered.valid() && recovered.mapped != nullptr);
    AUREA_CHECK_EQ(recovered.offset, 0u);
    driver.allocator.free(recovered);
    driver.allocator.trim_empty_blocks();
    AUREA_CHECK_EQ(driver.allocator.reserved_bytes(), 0u);
}

AUREA_TEST(VulkanAllocationFailure, ExistingGpuSlabSurvivesRequiredMappingFailureAndRetry) {
    AllocationFailureDriver driver;
    // Optional eager mapping on 64-bit may fail without making an image's
    // device memory unusable. On 32-bit it is intentionally not attempted.
    driver.failMaps = 1;
    const auto image = driver.request(false, false);
    AUREA_CHECK(image.valid());
    AUREA_CHECK(image.mapped == nullptr);
    const auto reserved = driver.allocator.reserved_bytes();
    const auto used = driver.allocator.used_bytes();
    driver.failMaps = 1;
    const auto failedUpload = driver.request();
    AUREA_CHECK(!failedUpload.valid());
    AUREA_CHECK_EQ(driver.allocator.reserved_bytes(), reserved);
    AUREA_CHECK_EQ(driver.allocator.used_bytes(), used);
    AUREA_CHECK_EQ(driver.allocator.allocation_count(), 1u);
    AUREA_CHECK_EQ(driver.freed, 0u);
    const auto recovered = driver.request();
    AUREA_CHECK(recovered.valid() && recovered.mapped != nullptr);
    AUREA_CHECK_EQ(recovered.memory, image.memory);
    AUREA_CHECK_EQ(recovered.offset, 64u);
    if (recovered.mapped) static_cast<u8*>(recovered.mapped)[0] = 197;
    AUREA_CHECK_EQ(driver.bytes[64], 197u);
    driver.allocator.free(image);
    driver.allocator.free(recovered);
    driver.allocator.trim_empty_blocks();
    AUREA_CHECK_EQ(driver.allocator.reserved_bytes(), 0u);
    AUREA_CHECK_EQ(driver.allocator.used_bytes(), 0u);
    AUREA_CHECK_EQ(driver.freed, 1u);
    AUREA_CHECK_EQ(driver.unmapped, 1u);
}
#endif
