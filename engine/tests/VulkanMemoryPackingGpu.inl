#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
#include "aurea/render/TrackedResourceBudget.hpp"

namespace {
// Lavapipe exposes device-local storage as host-visible too. Exercise the
// discrete/device-only slab policy without inventing a memory type: hide only
// CPU mapping capabilities in this test allocator's copy of the real table.
// The normal backend/readback allocator, requirements, memory indices, binds,
// submissions and driver allocation calls all retain their actual behavior.
struct DeviceOnlyAllocatorProperties {
    PFN_vkGetInstanceProcAddr originalProc = vk::vkGetInstanceProcAddr;
    PFN_vkGetPhysicalDeviceMemoryProperties originalProperties = vk::vkGetPhysicalDeviceMemoryProperties;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    bool deviceOnly = false;
    u32 maskedTypes = 0;
    inline static DeviceOnlyAllocatorProperties* active = nullptr;
    DeviceOnlyAllocatorProperties() {
        active = this; vk::vkGetInstanceProcAddr = &proc;
    }
    ~DeviceOnlyAllocatorProperties() {
        vk::vkGetInstanceProcAddr = originalProc;
        vk::vkGetPhysicalDeviceMemoryProperties = originalProperties;
        active = nullptr;
    }
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL proc(VkInstance instance, const char* name) {
        auto& observation = *active;
        const auto actual = observation.originalProc(instance, name);
        if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0) {
            observation.originalProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(actual);
            return reinterpret_cast<PFN_vkVoidFunction>(&properties);
        }
        return actual;
    }
    static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice physical,
                                                 VkPhysicalDeviceMemoryProperties* out) {
        auto& observation = *active;
        observation.originalProperties(physical, out);
        observation.physical = physical;
        if (!observation.deviceOnly) return;
        for (u32 i = 0; i < out->memoryTypeCount; ++i) {
            auto& flags = out->memoryTypes[i].propertyFlags;
            if (!(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) continue;
            flags &= ~(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                       | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            ++observation.maskedTypes;
        }
    }
};

// Count actual VkDeviceMemory handles and allocation/free time, not logical
// resources/suballocations. Forward every call to the real driver.
struct NativeMemoryObservation {
    PFN_vkAllocateMemory originalAllocate = vk::vkAllocateMemory;
    PFN_vkFreeMemory originalFree = vk::vkFreeMemory;
    VkDevice device;
    std::unordered_map<VkDeviceMemory, VkDeviceSize> live;
    u32 allocations = 0, frees = 0, peakHandles = 0;
    f64 allocateMs = 0, freeMs = 0;
    inline static NativeMemoryObservation* active = nullptr;
    explicit NativeMemoryObservation(VkDevice target) : device(target) {
        active = this; vk::vkAllocateMemory = &allocate; vk::vkFreeMemory = &free;
    }
    ~NativeMemoryObservation() {
        vk::vkAllocateMemory = originalAllocate; vk::vkFreeMemory = originalFree; active = nullptr;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice device, const VkMemoryAllocateInfo* info,
        const VkAllocationCallbacks* callbacks, VkDeviceMemory* memory) {
        auto& observation = *active;
        const auto start = std::chrono::steady_clock::now();
        const VkResult result = observation.originalAllocate(device, info, callbacks, memory);
        if (device == observation.device && result == VK_SUCCESS) {
            ++observation.allocations;
            observation.live[*memory] = info->allocationSize;
            observation.peakHandles = std::max(observation.peakHandles, static_cast<u32>(observation.live.size()));
            observation.allocateMs += std::chrono::duration<f64, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
        return result;
    }
    static VKAPI_ATTR void VKAPI_CALL free(VkDevice device, VkDeviceMemory memory,
                                            const VkAllocationCallbacks* callbacks) {
        auto& observation = *active;
        const auto start = std::chrono::steady_clock::now();
        observation.originalFree(device, memory, callbacks);
        if (device == observation.device && observation.live.erase(memory)) {
            ++observation.frees;
            observation.freeMs += std::chrono::duration<f64, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    }
};

struct MemoryPackingResources {
    struct Entry {
        VkImage image = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        vk::Allocation allocation;
        VkDeviceSize requirementBytes = 0;
        bool alive = false;
    };
    struct Retirement { MemoryPackingResources* owner; u32 index; };
    vk::Backend& backend;
    vk::MemoryAllocator allocator;
    std::array<Entry, 6> entries{};
    BufferHandle readback;
    u32 retired = 0;
    bool initialized = false, frameOpen = false;
    explicit MemoryPackingResources(vk::Backend& b) : backend(b) {}
    ~MemoryPackingResources() {
        if (frameOpen) (void)backend.end_frame();
        backend.wait_idle(); // Runs deferred callbacks while their owner is alive.
        for (u32 i = 0; i < entries.size(); ++i) release(i);
        if (initialized) allocator.shutdown();
        if (readback.valid()) backend.destroy_buffer(readback);
        backend.wait_idle();
    }
    bool image(u32 index) {
        Entry& entry = entries[index];
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {1536, 1024, 1}; info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                   | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vk::vkCreateImage(backend.device(), &info, nullptr, &entry.image) != VK_SUCCESS) return false;
        VkMemoryRequirements requirement{};
        vk::vkGetImageMemoryRequirements(backend.device(), entry.image, &requirement);
        entry.requirementBytes = requirement.size;
        entry.allocation = allocator.allocate(requirement, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0, false, "memory-packing-live-image", false);
        entry.alive = true;
        return entry.allocation.valid() && vk::vkBindImageMemory(backend.device(), entry.image,
            entry.allocation.memory, entry.allocation.offset) == VK_SUCCESS;
    }
    bool pin(u32 index) {
        Entry& entry = entries[index];
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = 4096; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vk::vkCreateBuffer(backend.device(), &info, nullptr, &entry.buffer) != VK_SUCCESS) return false;
        VkMemoryRequirements requirement{};
        vk::vkGetBufferMemoryRequirements(backend.device(), entry.buffer, &requirement);
        entry.requirementBytes = requirement.size;
        entry.allocation = allocator.allocate(requirement, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            0, false, "memory-packing-small-pin", false);
        entry.alive = true;
        return entry.allocation.valid() && vk::vkBindBufferMemory(backend.device(), entry.buffer,
            entry.allocation.memory, entry.allocation.offset) == VK_SUCCESS;
    }
    void release(u32 index) {
        Entry& entry = entries[index];
        if (!entry.alive) return;
        if (entry.image) vk::vkDestroyImage(backend.device(), entry.image, nullptr);
        if (entry.buffer) vk::vkDestroyBuffer(backend.device(), entry.buffer, nullptr);
        allocator.free(entry.allocation); entry = Entry{};
    }
    void defer(u32 index) {
        backend.defer_until_gpu_done([](void* context) {
            auto* retirement = static_cast<Retirement*>(context);
            retirement->owner->release(retirement->index);
            ++retirement->owner->retired;
            delete retirement;
        }, new Retirement{this, index});
    }
};

void packing_copy_corners(VkCommandBuffer command, VkImage image, VkBuffer destination, u32 region) {
    for (u32 corner = 0; corner < 2; ++corner) {
        VkBufferImageCopy copy{};
        copy.bufferOffset = static_cast<VkDeviceSize>(region + corner) * 32 * 32 * 4;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = corner ? VkOffset3D{1536 - 32, 1024 - 32, 0} : VkOffset3D{0, 0, 0};
        copy.imageExtent = {32, 32, 1};
        vk::vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination, 1, &copy);
    }
}

void packing_readback_visible(VkCommandBuffer command, VkBuffer destination) {
    VkBufferMemoryBarrier visible{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    visible.srcQueueFamilyIndex = visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    visible.buffer = destination; visible.size = VK_WHOLE_SIZE;
    vk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &visible, 0, nullptr);
}
}

AUREA_TEST(VulkanMemoryPackingGpu, FenceRetirementPreservesPixelsAndBoundsPhysicalBacking) {
    AUREA_REQUIRE_GPU();
    DeviceOnlyAllocatorProperties properties;
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 validationErrors = vk::Backend::validation_errors();
    AUREA_CHECK(properties.physical != VK_NULL_HANDLE);
    if (!properties.physical) return;
    VkPhysicalDeviceProperties physical{};
    vk::vkGetPhysicalDeviceProperties(properties.physical, &physical);
    NativeMemoryObservation nativeMemory(backend.device());
    MemoryPackingResources resources(backend);
    properties.deviceOnly = true;
    resources.allocator.initialize(properties.physical, backend.device());
    properties.deviceOnly = false;
    resources.initialized = true;
    AUREA_CHECK(properties.maskedTypes > 0);
    if (!properties.maskedTypes) return;
    BufferDesc desc;
    desc.bytes = 8 * 32 * 32 * 4; desc.usage = BufferUsage::TransferDst;
    desc.access = MemoryAccess::Readback; desc.debugName = "memory-packing-pixel-proof";
    const auto readback = backend.create_buffer(desc);
    AUREA_CHECK(readback.ok()); if (!readback.ok()) return;
    resources.readback = *readback;
    const VkBuffer destination = backend.buffer(readback->id)->buffer;
    const auto clearColorImage = reinterpret_cast<PFN_vkCmdClearColorImage>(
        vk::vkGetDeviceProcAddr(backend.device(), "vkCmdClearColorImage"));
    AUREA_CHECK(clearColorImage != nullptr); if (!clearColorImage) return;
    backend.wait_idle();
    const auto base = backend.memory_stats();
    const usize observedBaseHandles = nativeMemory.live.size();
    constexpr u64 mib = 1024 * 1024;
    // A real resident CPU cache remains charged to the same admission helper.
    const std::vector<u8> knownCache(8 * mib, 73);
    constexpr std::array<u32, 4> images{0, 1, 3, 4};
    constexpr std::array<std::array<u8, 4>, 4> colors{{
        {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}}};
    u64 steadyReserved = 0;
    for (u32 cycle = 0; cycle < 4; ++cycle) {
        // Two medium images and a tiny pin per group reproduce partially live
        // slabs: trim_empty_blocks alone cannot release a 16 MiB reservation.
        for (u32 i = 0; i < resources.entries.size(); ++i) {
            const bool created = i == 2 || i == 5 ? resources.pin(i) : resources.image(i);
            AUREA_CHECK(created); if (!created) return;
        }
        for (const auto index : images) {
            AUREA_CHECK(resources.entries[index].requirementBytes > 2 * mib);
            AUREA_CHECK(resources.entries[index].requirementBytes <= 8 * mib);
        }
        const u64 survivingBytes = resources.entries[1].allocation.size + resources.entries[4].allocation.size
                                 + resources.entries[2].allocation.size + resources.entries[5].allocation.size;
        const u64 budget = base.reservedBytes + survivingBytes + 8 * mib + knownCache.size();
        auto fits = [&]() {
            const auto backendMemory = backend.memory_stats();
            return fits_tracked_resource_budget(budget, resources.allocator.used_bytes(),
                backendMemory.usedBytes + resources.allocator.used_bytes(),
                backendMemory.reservedBytes + resources.allocator.reserved_bytes(), knownCache.size(), 0);
        };
        FrameBegin frame;
        const Status begin = backend.begin_offscreen_frame(frame);
        AUREA_CHECK(begin.ok()); if (!begin.ok()) return;
        resources.frameOpen = true;
        const VkCommandBuffer command = static_cast<vk::CommandListImpl*>(frame.commands)->handle();
        VkBufferMemoryBarrier reusable{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        reusable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
        reusable.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        reusable.srcQueueFamilyIndex = reusable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        reusable.buffer = destination; reusable.size = VK_WHOLE_SIZE;
        vk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &reusable, 0, nullptr);
        for (u32 i = 0; i < images.size(); ++i) {
            const VkImage image = resources.entries[images[i]].image;
            VkImageMemoryBarrier writable{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            writable.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            writable.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; writable.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            writable.srcQueueFamilyIndex = writable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            writable.image = image; writable.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &writable);
            VkClearColorValue clear{};
            for (u32 c = 0; c < 4; ++c) clear.float32[c] = colors[i][c] / 255.0f;
            clearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &clear, 1, &writable.subresourceRange);
            auto readable = writable;
            readable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; readable.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            readable.oldLayout = writable.newLayout; readable.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            vk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &readable);
            packing_copy_corners(command, image, destination, i * 2);
        }
        packing_readback_visible(command, destination);
        const u64 pendingUsed = resources.allocator.used_bytes(), pendingReserved = resources.allocator.reserved_bytes();
        const u32 previousRetirements = resources.retired;
        resources.defer(0); resources.defer(3);
        resources.allocator.trim_empty_blocks();
        AUREA_CHECK_EQ(resources.retired, previousRetirements);
        AUREA_CHECK_EQ(resources.allocator.used_bytes(), pendingUsed);
        AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), pendingReserved);
        AUREA_CHECK(!fits()); // Pending resources cannot be credited to fit.
        const Status submitted = backend.end_frame();
        resources.frameOpen = false;
        AUREA_CHECK(submitted.ok()); if (!submitted.ok()) return;
        {
            DelayedFenceObservation completion(backend.device());
            const Status pending = backend.wait_frame(frame.frameNumber, 0);
            AUREA_CHECK(pending.code() == Errc::Timeout);
            AUREA_CHECK_EQ(resources.retired, previousRetirements);
            AUREA_CHECK_EQ(resources.allocator.used_bytes(), pendingUsed);
            AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), pendingReserved);
            AUREA_CHECK(!fits());
        }
        const Status completed = backend.wait_frame(frame.frameNumber, 5'000'000'000ull);
        AUREA_CHECK(completed.ok()); if (!completed.ok()) return;
        AUREA_CHECK_EQ(resources.retired, previousRetirements + 2);
        resources.allocator.trim_empty_blocks();
        AUREA_CHECK_EQ(resources.allocator.used_bytes(), survivingBytes);
        AUREA_CHECK(resources.allocator.reserved_bytes() < pendingReserved);
        AUREA_CHECK(fits());
        auto verify = [&](const std::array<u32, 4>& imageNumbers, u32 count) {
            void* pixels = nullptr;
            const Status mapped = backend.map_buffer(*readback, pixels);
            AUREA_CHECK(mapped.ok() && pixels != nullptr);
            if (!mapped.ok() || !pixels) return false;
            bool exact = true;
            for (u32 image = 0; image < count; ++image) for (u32 corner = 0; corner < 2; ++corner) {
                const auto* region = static_cast<const u8*>(pixels) + (image * 2 + corner) * 32 * 32 * 4;
                for (u32 p = 0; p < 32 * 32; ++p)
                    exact &= std::memcmp(region + p * 4, colors[imageNumbers[image]].data(), 4) == 0;
            }
            backend.unmap_buffer(*readback);
            AUREA_CHECK(exact);
            return exact;
        };
        if (!verify({0, 1, 2, 3}, 4)) return;
        // Existing images must remain usable after their neighbors and backing
        // allocations retired. Copy them again in a new real submission.
        FrameBegin surviving;
        const Status reopened = backend.begin_offscreen_frame(surviving);
        AUREA_CHECK(reopened.ok()); if (!reopened.ok()) return;
        resources.frameOpen = true;
        const auto nextCommand = static_cast<vk::CommandListImpl*>(surviving.commands)->handle();
        VkBufferMemoryBarrier overwrite{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        overwrite.srcAccessMask = VK_ACCESS_HOST_READ_BIT; overwrite.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        overwrite.srcQueueFamilyIndex = overwrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        overwrite.buffer = destination; overwrite.size = VK_WHOLE_SIZE;
        vk::vkCmdPipelineBarrier(nextCommand, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &overwrite, 0, nullptr);
        packing_copy_corners(nextCommand, resources.entries[1].image, destination, 0);
        packing_copy_corners(nextCommand, resources.entries[4].image, destination, 2);
        packing_readback_visible(nextCommand, destination);
        const Status resubmitted = backend.end_frame();
        resources.frameOpen = false;
        AUREA_CHECK(resubmitted.ok()); if (!resubmitted.ok()) return;
        const Status survivorComplete = backend.wait_frame(surviving.frameNumber, 5'000'000'000ull);
        AUREA_CHECK(survivorComplete.ok()); if (!survivorComplete.ok()) return;
        if (!verify({1, 3, 0, 0}, 2)) return;
        const u64 reserved = resources.allocator.reserved_bytes();
        std::printf("\n    memory-packing cycle=%u pending_used=%llu pending_reserved=%llu live=%llu reserved=%llu budget=%llu cache=%zu native_calls=%u native_frees=%u observed_peak_new_handles=%u observed_base_handles=%zu base_logical_upper_bound=%u native_limit=%u alloc_ms=%.3f free_ms=%.3f\n",
            cycle, static_cast<unsigned long long>(pendingUsed), static_cast<unsigned long long>(pendingReserved),
            static_cast<unsigned long long>(survivingBytes), static_cast<unsigned long long>(reserved),
            static_cast<unsigned long long>(budget), knownCache.size(), nativeMemory.allocations,
            nativeMemory.frees, nativeMemory.peakHandles, observedBaseHandles, base.allocationCount,
            physical.limits.maxMemoryAllocationCount,
            nativeMemory.allocateMs, nativeMemory.freeMs);
        if (!cycle) steadyReserved = reserved;
        else AUREA_CHECK_EQ(reserved, steadyReserved);
        // Base logical allocations conservatively bound the already-live
        // native handles; observed calls give an exact bound for this workload.
        AUREA_CHECK(base.allocationCount + nativeMemory.peakHandles <= physical.limits.maxMemoryAllocationCount);
        for (u32 i = 0; i < resources.entries.size(); ++i) resources.release(i);
        resources.allocator.trim_empty_blocks();
        AUREA_CHECK_EQ(resources.allocator.used_bytes(), 0u);
        AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), 0u);
        AUREA_CHECK_EQ(resources.allocator.allocation_count(), 0u);
        AUREA_CHECK_EQ(nativeMemory.live.size(), observedBaseHandles);
        AUREA_CHECK_EQ(knownCache.front(), 73);
    }
    backend.wait_idle();
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
}
#endif
