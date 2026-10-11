#if !defined(AUREA_TEST_GLES)
namespace {
struct HostRingGpuResources {
    vk::Backend& backend;
    vk::HostRing ring;
    BufferHandle readback;
    explicit HostRingGpuResources(vk::Backend& b) : backend(b) {}
    ~HostRingGpuResources() {
        // Even an assertion/timeout path must not free submitted copy sources.
        backend.wait_idle();
        ring.shutdown();
        if (readback.valid()) backend.destroy_buffer(readback);
    }
};
}

AUREA_TEST(HostRingGpu, AlignedUploadsSurviveFenceAndConsolidateWithinMemoryBudget) {
    AUREA_REQUIRE_GPU();
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 validationErrors = vk::Backend::validation_errors();
    constexpr VkDeviceSize mib = 1024 * 1024;
    constexpr VkDeviceSize payload = 384 * 512 * 4;
    constexpr VkDeviceSize marker = 16;
    constexpr VkDeviceSize destinationBytes = marker + 2 * payload;
    // This workload needs <2 MiB including its deliberately large alignment
    // hole and growth reserve. Keeping 1+2 MiB of chunk tails would consolidate
    // to 3.75 MiB and exceed this real allocator/backing envelope.
    constexpr VkDeviceSize budget = 5 * mib / 2;
    HostRingGpuResources resources(backend);
    BufferDesc desc;
    desc.bytes = destinationBytes;
    desc.usage = BufferUsage::TransferDst;
    desc.access = MemoryAccess::Readback;
    desc.debugName = "host-ring-byte-verification";
    const auto destination = backend.create_buffer(desc);
    AUREA_CHECK(destination.ok()); if (!destination.ok()) return;
    resources.readback = *destination;
    const VkBuffer nativeDestination = backend.buffer(destination->id)->buffer;
    const auto base = backend.memory_stats();
    resources.ring.initialize(&backend, mib, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "host-ring-budget");
    const auto initial = resources.ring.stats();
    AUREA_CHECK_EQ(initial.chunks, 1u);
    if (!initial.chunks) return;
    std::vector<u8> expected(static_cast<usize>(destinationBytes));
    VkDeviceSize consolidatedBacking = 0;
    u64 steadyUsed = 0, steadyReserved = 0;
    for (u32 cycle = 0; cycle < 8; ++cycle) {
        for (usize i = 0; i < expected.size(); ++i)
            expected[i] = static_cast<u8>((i * 73 + (i >> 8) * 19 + cycle * 47) % 251);
        std::array<VkBuffer, 3> sources{};
        std::array<VkDeviceSize, 3> offsets{};
        constexpr std::array<VkDeviceSize, 3> lengths{marker, payload, payload};
        constexpr std::array<VkDeviceSize, 3> alignments{1, 65536, 4096};
        VkDeviceSize destinationOffset = 0;
        for (u32 slice = 0; slice < sources.size(); ++slice) {
            void* mapped = nullptr;
            const bool allocated = resources.ring.allocate(lengths[slice], alignments[slice],
                sources[slice], offsets[slice], mapped);
            AUREA_CHECK(allocated); if (!allocated) return;
            AUREA_CHECK(mapped != nullptr); if (!mapped) return;
            AUREA_CHECK_EQ(offsets[slice] % alignments[slice], 0u);
            std::memcpy(mapped, expected.data() + destinationOffset, static_cast<usize>(lengths[slice]));
            destinationOffset += lengths[slice];
        }
        if (cycle == 0) {
            AUREA_CHECK(sources[0] == sources[1]);
            AUREA_CHECK(sources[1] != sources[2]); // Actual cold-frame growth.
        } else {
            AUREA_CHECK(sources[0] == sources[1] && sources[1] == sources[2]);
        }
        FrameBegin frame;
        const Status begin = backend.begin_offscreen_frame(frame);
        AUREA_CHECK(begin.ok()); if (!begin.ok()) return;
        const VkCommandBuffer command = static_cast<vk::CommandListImpl*>(frame.commands)->handle();
        destinationOffset = 0;
        for (u32 slice = 0; slice < sources.size(); ++slice) {
            const VkBufferCopy copy{offsets[slice], destinationOffset, lengths[slice]};
            backend.device_dispatch().vkCmdCopyBuffer(command, sources[slice], nativeDestination, 1, &copy);
            destinationOffset += lengths[slice];
        }
        VkBufferMemoryBarrier visible{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        visible.srcQueueFamilyIndex = visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        visible.buffer = nativeDestination; visible.size = destinationBytes;
        backend.device_dispatch().vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &visible, 0, nullptr);
        const Status submitted = backend.end_frame();
        AUREA_CHECK(submitted.ok()); if (!submitted.ok()) return;
        const Status completed = backend.wait_frame(frame.frameNumber, 5'000'000'000ull);
        AUREA_CHECK(completed.ok()); if (!completed.ok()) return;
        // Completion is a real frame fence, not a fake copy or CPU-only check.
        void* actual = nullptr;
        const Status mapped = backend.map_buffer(*destination, actual);
        AUREA_CHECK(mapped.ok()); if (!mapped.ok()) return;
        AUREA_CHECK(actual != nullptr); if (!actual) return;
        AUREA_CHECK_EQ(std::memcmp(actual, expected.data(), expected.size()), 0);
        backend.unmap_buffer(*destination);
        resources.ring.reset(); // Never recycle until the copy's fence completed.
        backend.allocator().trim_empty_blocks();
        const auto ring = resources.ring.stats();
        const auto gpuMemory = backend.memory_stats();
        std::printf("\n    host-ring cycle=%u capacity=%llu backing=%llu peak_aligned=%llu gpu_used=%llu gpu_reserved=%llu base_used=%llu base_reserved=%llu\n",
            cycle, static_cast<unsigned long long>(ring.capacity), static_cast<unsigned long long>(ring.backing),
            static_cast<unsigned long long>(ring.peakAlignedDemand),
            static_cast<unsigned long long>(gpuMemory.usedBytes), static_cast<unsigned long long>(gpuMemory.reservedBytes),
            static_cast<unsigned long long>(base.usedBytes), static_cast<unsigned long long>(base.reservedBytes));
        AUREA_CHECK_EQ(ring.chunks, 1u);
        AUREA_CHECK_EQ(ring.payloadUsed, 0u);
        AUREA_CHECK(ring.capacity <= budget && ring.backing <= budget);
        AUREA_CHECK(gpuMemory.usedBytes <= base.usedBytes + budget);
        AUREA_CHECK_EQ(gpuMemory.allocationCount, base.allocationCount + 1);
        if (cycle == 0) {
            consolidatedBacking = ring.backing;
            steadyUsed = gpuMemory.usedBytes; steadyReserved = gpuMemory.reservedBytes;
        } else {
            AUREA_CHECK_EQ(ring.backing, consolidatedBacking);
            AUREA_CHECK_EQ(gpuMemory.usedBytes, steadyUsed);
            AUREA_CHECK_EQ(gpuMemory.reservedBytes, steadyReserved);
        }
    }
    backend.wait_idle();
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - validationErrors, 0u);
}
#endif
