#if !defined(AUREA_TEST_GLES)
namespace {
struct RetirementBackend {
    vk::Backend backend;
    bool ok = false;
    RetirementBackend() {
        BackendConfig config;
        config.enableValidation = true;
        config.framesInFlight = 3;
        ok = backend.initialize(config).ok();
    }
    ~RetirementBackend() { backend.wait_idle(); backend.shutdown(); }
};

TextureDesc retirement_texture(u32 width) {
    TextureDesc desc;
    desc.width = width; desc.height = 512;
    desc.format = SurfaceFormat::RGBA16F;
    desc.sampled = desc.renderTarget = desc.transferSrc = true;
    return desc;
}

void retirement_clear(FrameBegin& frame, TextureHandle texture) {
    frame.commands->barrier(texture, ResourceState::ColorAttachment, true);
    RenderPassBegin pass;
    pass.color = texture; pass.load = LoadOp::Clear;
    pass.clear[0] = .25f; pass.clear[1] = .5f; pass.clear[2] = .75f; pass.clear[3] = 1;
    frame.commands->begin_render_pass(pass);
    frame.commands->end_render_pass();
}
}

AUREA_TEST(MemoryPressureGpu, DynamicTargetsRetireOnTheirLastCompletedUse) {
    AUREA_REQUIRE_GPU();
    RetirementBackend isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 errors = vk::Backend::validation_errors();
    const auto baseline = backend.memory_stats();
    TransientTexturePool pool;
    pool.set_budget(0); // retain a live target, retire obsolete dimensions immediately
    u64 peakUsed = 0;
    for (u32 frameIndex = 0; frameIndex < 24; ++frameIndex) {
        FrameBegin frame;
        AUREA_CHECK(backend.begin_offscreen_frame(frame).ok());
        pool.begin_frame(backend, frame.frameNumber);
        const auto texture = pool.acquire(retirement_texture(512 + frameIndex));
        AUREA_CHECK(texture.valid());
        // Each prior fence was observed complete, even while this new frame is
        // open. Its obsolete allocation must not wait for the new frame's fence.
        const auto memory = backend.memory_stats();
        AUREA_CHECK_EQ(memory.allocationCount, baseline.allocationCount + 1);
        AUREA_CHECK_EQ(pool.stats().alive, 1u);
        peakUsed = std::max(peakUsed, memory.usedBytes - baseline.usedBytes);
        retirement_clear(frame, texture);
        pool.release(texture); pool.end_frame();
        AUREA_CHECK(backend.end_frame().ok());
        AUREA_CHECK(backend.wait_frame(frame.frameNumber, 1'000'000'000ull).ok());
    }
    std::printf("dynamic targets peak bytes=%llu ", static_cast<unsigned long long>(peakUsed));
    pool.clear(); backend.wait_idle();
    AUREA_CHECK_EQ(backend.memory_stats().usedBytes, baseline.usedBytes);
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
}

AUREA_TEST(MemoryPressureGpu, RetirementWaitsForOldUseWithoutWaitingForANewerFrame) {
    AUREA_REQUIRE_GPU();
    RetirementBackend isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 errors = vk::Backend::validation_errors();
    const auto baseline = backend.memory_stats();
    TransientTexturePool pool;
    pool.set_budget(0);
    FrameBegin first;
    AUREA_CHECK(backend.begin_offscreen_frame(first).ok());
    pool.begin_frame(backend, first.frameNumber);
    const auto a = pool.acquire(retirement_texture(512));
    AUREA_CHECK(a.valid()); retirement_clear(first, a);
    pool.release(a); pool.end_frame(); AUREA_CHECK(backend.end_frame().ok());

    FrameBegin second;
    AUREA_CHECK(backend.begin_offscreen_frame(second).ok());
    pool.begin_frame(backend, second.frameNumber);
    DelayedFenceObservation oldCompletion(backend.device());
    oldCompletion.fence = vk::ImmediateSubmissionTestAccess::frame_fence(backend, first.frameNumber);
    const auto b = pool.acquire(retirement_texture(640));
    AUREA_CHECK(b.valid());
    // Completion of A has not been observed. Its bytes must still be alive.
    AUREA_CHECK_EQ(backend.memory_stats().allocationCount, baseline.allocationCount + 2);
    retirement_clear(second, b);
    pool.release(b); pool.end_frame(); AUREA_CHECK(backend.end_frame().ok());
    oldCompletion.release();
    AUREA_CHECK_EQ(oldCompletion.await_real_completion(), VK_SUCCESS);

    // The third slot is unused. Keep the newer frame unobserved: the ready
    // first frame must release A without recycling either submitted slot.
    DelayedFenceObservation newerCompletion(backend.device());
    newerCompletion.fence = vk::ImmediateSubmissionTestAccess::frame_fence(backend, second.frameNumber);
    FrameBegin third;
    AUREA_CHECK(backend.begin_offscreen_frame(third).ok());
    AUREA_CHECK_EQ(backend.memory_stats().allocationCount, baseline.allocationCount + 1);
    AUREA_CHECK(backend.end_frame().ok());
    newerCompletion.release();
    pool.clear(); backend.wait_idle();
    AUREA_CHECK_EQ(backend.memory_stats().usedBytes, baseline.usedBytes);
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
}
#endif
