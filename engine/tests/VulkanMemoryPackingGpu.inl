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
    bool pin(u32 index, u64 bytes = 4096) {
        Entry& entry = entries[index];
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bytes; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
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

AUREA_TEST(VulkanMemoryPackingGpu, SmallPinsBoundActualSlabsWithoutEarlyFenceCreditOrMovingLiveBuffers) {
    AUREA_REQUIRE_GPU();
    DeviceOnlyAllocatorProperties properties;
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    AUREA_CHECK(properties.physical != VK_NULL_HANDLE); if (!properties.physical) return;
    VkPhysicalDeviceProperties physical{};
    vk::vkGetPhysicalDeviceProperties(properties.physical, &physical);
    const auto fill = reinterpret_cast<PFN_vkCmdFillBuffer>(vk::vkGetDeviceProcAddr(backend.device(), "vkCmdFillBuffer"));
    AUREA_CHECK(fill != nullptr); if (!fill) return;
    const u32 validationErrors = vk::Backend::validation_errors();
    NativeMemoryObservation observed(backend.device());
    constexpr u64 mib = 1ull << 20;
    for (const bool deviceOnly : {false, true}) for (const u64 requested : {4096ull, 768ull << 10, 1536ull << 10, 3ull << 20})
        for (u32 cycle = 0; cycle < 3; ++cycle) {
        MemoryPackingResources resources(backend);
        properties.deviceOnly = deviceOnly;
        resources.allocator.initialize(properties.physical, backend.device());
        properties.deviceOnly = false; resources.initialized = true;
        BufferDesc desc; desc.bytes = 8192; desc.usage = BufferUsage::TransferDst;
        desc.access = MemoryAccess::Readback;
        const auto readback = backend.create_buffer(desc);
        AUREA_CHECK(readback.ok()); if (!readback) return;
        resources.readback = *readback;
        backend.wait_idle();
        const auto base = backend.memory_stats();
        const usize baseHandles = observed.live.size();
        AUREA_CHECK(resources.pin(0, requested)); AUREA_CHECK(resources.pin(2));
        if (!resources.entries[0].alive || !resources.entries[0].allocation.valid()
            || !resources.entries[2].allocation.valid()) return;
        const auto& large = resources.entries[0];
        const auto pin = resources.entries[2].allocation;
        AUREA_CHECK(large.requirementBytes >= requested);
        AUREA_CHECK(resources.entries[2].requirementBytes <= (512ull << 10));
        const u64 requirement = large.requirementBytes;
        const u64 tier = requirement <= (512ull << 10) ? mib : requirement <= mib ? 2 * mib : 4 * mib;
        const u64 granularity = std::max<u64>(1, physical.limits.bufferImageGranularity);
        const u64 rounded = (requirement + granularity - 1) / granularity * granularity;
        const u64 expectedMain = requirement > 2 * mib ? requirement : std::max(tier, rounded);
        AUREA_CHECK_EQ(observed.live.at(large.allocation.memory), expectedMain);
        if (requirement > 2 * mib) AUREA_CHECK_EQ(large.allocation.block, kInvalidIndex);
        else AUREA_CHECK(large.allocation.block != kInvalidIndex);
        // Exact physical backing, not a changed logical texture estimate.
        const u64 pendingUsed = resources.allocator.used_bytes();
        const u64 pendingReserved = resources.allocator.reserved_bytes();
        AUREA_CHECK(pendingReserved >= pendingUsed);
        if (requirement <= mib) AUREA_CHECK(pendingReserved < 4 * mib);
        const std::vector<u8> knownCpuCache(mib, 91);
        const u64 survivingReservation = observed.live.at(pin.memory);
        const u64 budget = base.reservedBytes + survivingReservation + knownCpuCache.size();
        const auto fits = [&] {
            const auto actual = backend.memory_stats();
            return fits_tracked_resource_budget(budget, resources.allocator.used_bytes(),
                actual.usedBytes + resources.allocator.used_bytes(),
                actual.reservedBytes + resources.allocator.reserved_bytes(), knownCpuCache.size(), 0);
        };
        FrameBegin frame; AUREA_CHECK(backend.begin_offscreen_frame(frame).ok()); resources.frameOpen = true;
        const auto command = static_cast<vk::CommandListImpl*>(frame.commands)->handle();
        const auto destination = backend.buffer(readback->id)->buffer;
        const std::array<u32, 2> values{0x12abcd34u + cycle, 0xa9f06721u + cycle};
        fill(command, large.buffer, 0, 4096, values[0]);
        fill(command, resources.entries[2].buffer, 0, 4096, values[1]);
        std::array<VkBufferMemoryBarrier, 2> readable{};
        for (u32 i = 0; i < 2; ++i) {
            readable[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            readable[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; readable[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            readable[i].srcQueueFamilyIndex = readable[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            readable[i].buffer = i ? resources.entries[2].buffer : large.buffer; readable[i].size = 4096;
        }
        vk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 2, readable.data(), 0, nullptr);
        for (u32 i = 0; i < 2; ++i) {
            const VkBufferCopy copy{0, i * 4096ull, 4096};
            vk::vkCmdCopyBuffer(command, readable[i].buffer, destination, 1, &copy);
        }
        packing_readback_visible(command, destination);
        resources.defer(0); resources.allocator.trim_empty_blocks();
        AUREA_CHECK_EQ(resources.retired, 0u);
        AUREA_CHECK_EQ(resources.allocator.used_bytes(), pendingUsed);
        AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), pendingReserved);
        AUREA_CHECK(backend.end_frame().ok()); resources.frameOpen = false;
        {
            DelayedFenceObservation hold(backend.device());
            AUREA_CHECK_EQ(backend.wait_frame(frame.frameNumber, 0).code(), Errc::Timeout);
            AUREA_CHECK_EQ(resources.retired, 0u);
            AUREA_CHECK_EQ(resources.allocator.used_bytes(), pendingUsed);
            AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), pendingReserved);
        }
        AUREA_CHECK(backend.wait_frame(frame.frameNumber, 5'000'000'000ull).ok());
        AUREA_CHECK_EQ(resources.retired, 1u);
        resources.allocator.trim_empty_blocks();
        AUREA_CHECK_EQ(resources.entries[2].allocation.memory, pin.memory);
        AUREA_CHECK_EQ(resources.entries[2].allocation.offset, pin.offset);
        AUREA_CHECK_EQ(resources.allocator.used_bytes(), pin.size);
        AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), survivingReservation);
        AUREA_CHECK(fits());
        void* pixels = nullptr; AUREA_CHECK(backend.map_buffer(*readback, pixels).ok());
        if (!pixels) return;
        for (u32 i = 0; i < 2048; ++i) AUREA_CHECK_EQ(static_cast<const u32*>(pixels)[i], values[i / 1024]);
        backend.unmap_buffer(*readback);
        // Read the still-live pin again after its neighbour's real retirement.
        FrameBegin next; AUREA_CHECK(backend.begin_offscreen_frame(next).ok()); resources.frameOpen = true;
        const auto nextCommand = static_cast<vk::CommandListImpl*>(next.commands)->handle();
        VkBufferMemoryBarrier writable{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        writable.srcAccessMask = VK_ACCESS_HOST_READ_BIT; writable.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        writable.srcQueueFamilyIndex = writable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        writable.buffer = destination; writable.size = VK_WHOLE_SIZE;
        vk::vkCmdPipelineBarrier(nextCommand, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &writable, 0, nullptr);
        const VkBufferCopy copy{0, 0, 4096};
        vk::vkCmdCopyBuffer(nextCommand, resources.entries[2].buffer, destination, 1, &copy);
        packing_readback_visible(nextCommand, destination);
        AUREA_CHECK(backend.end_frame().ok()); resources.frameOpen = false;
        AUREA_CHECK(backend.wait_frame(next.frameNumber, 5'000'000'000ull).ok());
        pixels = nullptr; AUREA_CHECK(backend.map_buffer(*readback, pixels).ok());
        if (!pixels) return;
        for (u32 i = 0; i < 1024; ++i) AUREA_CHECK_EQ(static_cast<const u32*>(pixels)[i], values[1]);
        backend.unmap_buffer(*readback);
        std::printf("\n    small-slab device_only=%u cycle=%u requested=%llu actual=%llu pending_live=%llu pending_reserved=%llu pin_live=%llu pin_reserved=%llu native_peak_new_handles=%u logical_base_bound=%u native_limit=%u exact_channels=12288\n",
            deviceOnly, cycle, static_cast<unsigned long long>(requested), static_cast<unsigned long long>(requirement),
            static_cast<unsigned long long>(pendingUsed), static_cast<unsigned long long>(pendingReserved),
            static_cast<unsigned long long>(pin.size), static_cast<unsigned long long>(survivingReservation),
            observed.peakHandles, base.allocationCount, physical.limits.maxMemoryAllocationCount);
        AUREA_CHECK(base.allocationCount + observed.peakHandles <= physical.limits.maxMemoryAllocationCount);
        resources.release(2); resources.allocator.trim_empty_blocks();
        AUREA_CHECK_EQ(resources.allocator.used_bytes(), 0u); AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), 0u);
        AUREA_CHECK_EQ(observed.live.size(), baseHandles);
        AUREA_CHECK_EQ(knownCpuCache.front(), 91);
    }
    backend.wait_idle(); AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
}
AUREA_TEST(VulkanMemoryPackingGpu, PersistentTinyPinsCannotStrandRetiredLargerSizeClasses) {
    AUREA_REQUIRE_GPU();
    DeviceOnlyAllocatorProperties properties;
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    AUREA_CHECK(properties.physical != VK_NULL_HANDLE); if (!properties.physical) return;
    VkPhysicalDeviceProperties physical{};
    vk::vkGetPhysicalDeviceProperties(properties.physical, &physical);
    const auto fill = reinterpret_cast<PFN_vkCmdFillBuffer>(vk::vkGetDeviceProcAddr(backend.device(), "vkCmdFillBuffer"));
    AUREA_CHECK(fill != nullptr); if (!fill) return;
    const u32 validationErrors = vk::Backend::validation_errors();
    NativeMemoryObservation observed(backend.device());
    constexpr u64 mib = 1ull << 20;
    // The four persistent payload sizes match the tiny survivors observed in
    // separate 2 MiB slabs in the physical heavy capture. Real driver memory
    // requirements are checked below, rather than substituting fake geometry.
    constexpr std::array<u64, 6> requested{768ull << 10, 1536ull << 10, 5760, 73728, 71616, 73728};
    constexpr std::array<u32, 6> values{0x31bda4f0u, 0xa95406c3u, 0x951337afu,
                                      0x79b10dc2u, 0xe4c39025u, 0x123da679u};
    u64 fullBytes = 0, pinBytes = 0;
    for (u32 i = 0; i < requested.size(); ++i) {
        fullBytes += requested[i];
        if (i >= 2) pinBytes += requested[i];
    }
    u64 comparedBytes = 0;
    for (const bool deviceOnly : {false, true}) {
        MemoryPackingResources resources(backend);
        properties.deviceOnly = deviceOnly;
        resources.allocator.initialize(properties.physical, backend.device());
        properties.deviceOnly = false; resources.initialized = true;
        BufferDesc desc; desc.bytes = fullBytes; desc.usage = BufferUsage::TransferDst;
        desc.access = MemoryAccess::Readback;
        const auto readback = backend.create_buffer(desc);
        AUREA_CHECK(readback.ok()); if (!readback) return;
        resources.readback = *readback;
        backend.wait_idle();
        const auto base = backend.memory_stats();
        const usize baseHandles = observed.live.size();
        const auto destination = backend.buffer(readback->id)->buffer;
        const std::vector<u8> knownCpuCache(8 * mib, 83);
        std::array<vk::Allocation, 4> originalPins{};
        u64 pinReserved = 0, pinUsed = 0;
        usize pinHandles = 0;
        for (u32 cycle = 0; cycle < 4; ++cycle) {
            // Open each larger class BEFORE allocating the tiny survivors.
            // Cross-class first-fit would place those pins in the first 2 MiB
            // slab and keep it alive. Subsequent cycles keep all pins resident.
            AUREA_CHECK(resources.pin(0, requested[0]));
            if (!cycle) AUREA_CHECK(resources.pin(2, requested[2]));
            AUREA_CHECK(resources.pin(1, requested[1]));
            if (!cycle) for (u32 i = 3; i < requested.size(); ++i)
                AUREA_CHECK(resources.pin(i, requested[i]));
            for (const auto& entry : resources.entries) {
                AUREA_CHECK(entry.alive && entry.allocation.valid());
                if (!entry.alive || !entry.allocation.valid()) return;
            }
            const auto& smallTransient = resources.entries[0];
            const auto& largeTransient = resources.entries[1];
            AUREA_CHECK(smallTransient.requirementBytes > (512ull << 10)
                        && smallTransient.requirementBytes <= mib);
            AUREA_CHECK(largeTransient.requirementBytes > mib
                        && largeTransient.requirementBytes <= 2 * mib);
            // Distinct driver handles prove actual class separation. Logical
            // accounting alone cannot make a mostly empty slab retire.
            AUREA_CHECK(smallTransient.allocation.memory != largeTransient.allocation.memory);
            AUREA_CHECK_EQ(observed.live.at(smallTransient.allocation.memory), 2 * mib);
            AUREA_CHECK_EQ(observed.live.at(largeTransient.allocation.memory), 4 * mib);
            for (u32 i = 2; i < requested.size(); ++i) {
                const auto& pin = resources.entries[i];
                AUREA_CHECK(pin.requirementBytes <= (512ull << 10));
                AUREA_CHECK(pin.allocation.memory != smallTransient.allocation.memory);
                AUREA_CHECK(pin.allocation.memory != largeTransient.allocation.memory);
                AUREA_CHECK_EQ(pin.allocation.memory, resources.entries[2].allocation.memory);
                if (!cycle) {
                    originalPins[i - 2] = pin.allocation;
                    pinUsed += pin.allocation.size;
                } else {
                    AUREA_CHECK_EQ(pin.allocation.memory, originalPins[i - 2].memory);
                    AUREA_CHECK_EQ(pin.allocation.offset, originalPins[i - 2].offset);
                    AUREA_CHECK_EQ(pin.allocation.block, originalPins[i - 2].block);
                    AUREA_CHECK_EQ(pin.allocation.size, originalPins[i - 2].size);
                }
            }
            if (!cycle) {
                pinReserved = observed.live.at(originalPins[0].memory);
                AUREA_CHECK_EQ(pinReserved, mib);
                pinHandles = baseHandles + 1;
            }
            const u64 pendingUsed = resources.allocator.used_bytes();
            const u64 pendingReserved = resources.allocator.reserved_bytes();
            AUREA_CHECK_EQ(pendingReserved, pinReserved + 6 * mib);
            AUREA_CHECK_EQ(observed.live.size(), pinHandles + 2);
            const u64 budget = base.reservedBytes + pinReserved + knownCpuCache.size();
            auto fits = [&] {
                const auto actual = backend.memory_stats();
                return fits_tracked_resource_budget(budget, resources.allocator.used_bytes(),
                    actual.usedBytes + resources.allocator.used_bytes(),
                    actual.reservedBytes + resources.allocator.reserved_bytes(), knownCpuCache.size(), 0);
            };
            AUREA_CHECK(!fits());
            FrameBegin frame; const auto begun = backend.begin_offscreen_frame(frame);
            AUREA_CHECK(begun.ok()); if (!begun.ok()) return;
            resources.frameOpen = true;
            const auto command = static_cast<vk::CommandListImpl*>(frame.commands)->handle();
            VkBufferMemoryBarrier writable{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            writable.srcAccessMask = VK_ACCESS_HOST_READ_BIT; writable.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            writable.srcQueueFamilyIndex = writable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            writable.buffer = destination; writable.size = VK_WHOLE_SIZE;
            vk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 1, &writable, 0, nullptr);
            u64 offset = 0;
            for (u32 i = 0; i < requested.size(); ++i) {
                const auto buffer = resources.entries[i].buffer;
                if (i < 2 || !cycle) {
                    fill(command, buffer, 0, requested[i], values[i]);
                    VkBufferMemoryBarrier readable{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                    readable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; readable.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    readable.srcQueueFamilyIndex = readable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    readable.buffer = buffer; readable.size = requested[i];
                    vk::vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 1, &readable, 0, nullptr);
                }
                const VkBufferCopy copy{0, offset, requested[i]};
                vk::vkCmdCopyBuffer(command, buffer, destination, 1, &copy); offset += requested[i];
            }
            packing_readback_visible(command, destination);
            const u32 beforeRetired = resources.retired;
            resources.defer(0); resources.defer(1); resources.allocator.trim_empty_blocks();
            AUREA_CHECK_EQ(resources.retired, beforeRetired);
            AUREA_CHECK_EQ(resources.allocator.used_bytes(), pendingUsed);
            AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), pendingReserved);
            AUREA_CHECK(backend.end_frame().ok()); resources.frameOpen = false;
            {
                DelayedFenceObservation held(backend.device());
                AUREA_CHECK_EQ(backend.wait_frame(frame.frameNumber, 0).code(), Errc::Timeout);
                AUREA_CHECK_EQ(resources.retired, beforeRetired);
                AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), pendingReserved);
                AUREA_CHECK_EQ(observed.live.size(), pinHandles + 2);
                AUREA_CHECK(!fits());
            }
            AUREA_CHECK(backend.wait_frame(frame.frameNumber, 5'000'000'000ull).ok());
            AUREA_CHECK_EQ(resources.retired, beforeRetired + 2);
            resources.allocator.trim_empty_blocks();
            AUREA_CHECK_EQ(resources.allocator.used_bytes(), pinUsed);
            AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), pinReserved);
            AUREA_CHECK_EQ(observed.live.size(), pinHandles);
            AUREA_CHECK(fits());
            void* bytes = nullptr; AUREA_CHECK(backend.map_buffer(*readback, bytes).ok());
            if (!bytes) return;
            u64 word = 0, differences = 0;
            for (u32 i = 0; i < requested.size(); ++i)
                for (u64 k = 0; k < requested[i] / 4; ++k, ++word)
                    differences += static_cast<const u32*>(bytes)[word] != values[i];
            backend.unmap_buffer(*readback);
            AUREA_CHECK_EQ(differences, 0u); comparedBytes += fullBytes;
            // A second genuine submission reads every persistent byte after
            // both transient slabs have actually been freed. Pins are never
            // rewritten after cycle zero, so reuse corruption cannot hide.
            FrameBegin after; const auto afterBegin = backend.begin_offscreen_frame(after);
            AUREA_CHECK(afterBegin.ok()); if (!afterBegin.ok()) return;
            resources.frameOpen = true;
            const auto afterCommand = static_cast<vk::CommandListImpl*>(after.commands)->handle();
            vk::vkCmdPipelineBarrier(afterCommand, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 1, &writable, 0, nullptr);
            offset = 0;
            for (u32 i = 2; i < requested.size(); ++i) {
                const VkBufferCopy copy{0, offset, requested[i]};
                vk::vkCmdCopyBuffer(afterCommand, resources.entries[i].buffer, destination, 1, &copy);
                offset += requested[i];
            }
            packing_readback_visible(afterCommand, destination);
            AUREA_CHECK(backend.end_frame().ok()); resources.frameOpen = false;
            AUREA_CHECK(backend.wait_frame(after.frameNumber, 5'000'000'000ull).ok());
            bytes = nullptr; AUREA_CHECK(backend.map_buffer(*readback, bytes).ok()); if (!bytes) return;
            word = differences = 0;
            for (u32 i = 2; i < requested.size(); ++i)
                for (u64 k = 0; k < requested[i] / 4; ++k, ++word)
                    differences += static_cast<const u32*>(bytes)[word] != values[i];
            backend.unmap_buffer(*readback);
            AUREA_CHECK_EQ(differences, 0u); comparedBytes += pinBytes;
            std::printf("\n    size-class-pins device_only=%u cycle=%u pending_used=%llu pending_reserved=%llu pin_live=%llu pin_reserved=%llu reclaimed_reserved=%llu persistent_buffers=4 exact_bytes=%llu observed_peak_new_handles=%u logical_base_bound=%u native_limit=%u native_calls=%u native_frees=%u alloc_ms=%.3f free_ms=%.3f\n",
                deviceOnly, cycle, static_cast<unsigned long long>(pendingUsed), static_cast<unsigned long long>(pendingReserved),
                static_cast<unsigned long long>(pinUsed), static_cast<unsigned long long>(pinReserved),
                static_cast<unsigned long long>(pendingReserved - pinReserved),
                static_cast<unsigned long long>(fullBytes + pinBytes), observed.peakHandles, base.allocationCount,
                physical.limits.maxMemoryAllocationCount, observed.allocations, observed.frees,
                observed.allocateMs, observed.freeMs);
            AUREA_CHECK(base.allocationCount + observed.peakHandles <= physical.limits.maxMemoryAllocationCount);
        }
        for (u32 i = 2; i < requested.size(); ++i) resources.release(i);
        resources.allocator.trim_empty_blocks();
        AUREA_CHECK_EQ(resources.allocator.used_bytes(), 0u); AUREA_CHECK_EQ(resources.allocator.reserved_bytes(), 0u);
        AUREA_CHECK_EQ(resources.allocator.allocation_count(), 0u); AUREA_CHECK_EQ(observed.live.size(), baseHandles);
        AUREA_CHECK_EQ(knownCpuCache.front(), 83);
    }
    backend.wait_idle(); AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
    std::printf("\n    size-class-pins complete actual_readback_bytes=%llu\n", static_cast<unsigned long long>(comparedBytes));
}
#endif
