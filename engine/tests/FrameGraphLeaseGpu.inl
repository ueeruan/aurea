#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
namespace {
struct LeaseSurfaceCapabilitiesDispatch {
    vk::Backend& backend;
    VkSurfaceKHR previousSurface;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR original = vk::vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    inline static LeaseSurfaceCapabilitiesDispatch* active = nullptr;
    u32 queries = 0;
    explicit LeaseSurfaceCapabilitiesDispatch(vk::Backend& value) : backend(value),
        previousSurface(vk::DiscardFrameTestAccess::install_lease_test_surface(value)) {
        active = this; vk::vkGetPhysicalDeviceSurfaceCapabilitiesKHR = capabilities;
    }
    ~LeaseSurfaceCapabilitiesDispatch() {
        vk::vkGetPhysicalDeviceSurfaceCapabilitiesKHR = original;
        vk::DiscardFrameTestAccess::restore_lease_test_surface(backend, previousSurface);
        active = nullptr;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL capabilities(VkPhysicalDevice, VkSurfaceKHR,
                                                       VkSurfaceCapabilitiesKHR* output) {
        ++active->queries;
        *output = {};
        output->currentTransform = vk::DiscardFrameTestAccess::expected_lease_transform(active->backend);
        return VK_SUCCESS;
    }
};
}
// WSI acquisition/presentation is intercepted on this headless fixture.
// Queue submissions, binary semaphores, fences, texture clears and readback
// pixels are real. This is not proof of a native display or SurfaceFlinger.
AUREA_TEST(FrameGraphSegmentsLeaseGpu, InterceptedWsiLeaseKeepsRealIntermediatePixelsAndOneFinalPresent) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    const u32 errors = vk::Backend::validation_errors();
    DiscardFrameWsiDispatch wsi(backend);
    AUREA_CHECK(wsi.attached); if (!wsi.attached) return;
    LeaseSurfaceCapabilitiesDispatch surfaceCapabilities(backend);
    AUREA_CHECK(backend.can_lease_presentable_frame());
    const auto target = isolated.target(64, 32);
    FrameBegin initial;
    AUREA_CHECK(backend.begin_frame(initial).ok());
    AUREA_CHECK(initial.backbuffer.valid());
    const auto held = initial.backbuffer;
    vk::DiscardFrameTestAccess::mark_unused_backbuffer_present(backend);
    AUREA_CHECK(backend.begin_presentable_frame_lease().ok());
    u32 callbacks = 0;
    backend.defer_until_gpu_done([](void* context) { ++*static_cast<u32*>(context); }, &callbacks);
    AUREA_CHECK(backend.discard_frame().ok());
    AUREA_CHECK(backend.wait_frame(initial.frameNumber, 2'000'000'000ull).ok());
    for (u32 stage = 0; stage < 6; ++stage) {
        FrameBegin intermediate;
        AUREA_CHECK(backend.begin_offscreen_frame(intermediate).ok());
        AUREA_CHECK(!intermediate.backbuffer.valid());
        const std::array<f32, 4> clear{(stage + 1.f) / 8.f, .25f, .5f, 1.f};
        intermediate.commands->barrier(target, ResourceState::ColorAttachment, true);
        RenderPassBegin pass; pass.color = target; pass.load = LoadOp::Clear;
        std::copy(clear.begin(), clear.end(), pass.clear);
        intermediate.commands->begin_render_pass(pass);
        intermediate.commands->end_render_pass();
        intermediate.commands->barrier(target, ResourceState::ShaderRead, false);
        backend.defer_until_gpu_done([](void* context) { ++*static_cast<u32*>(context); }, &callbacks);
        AUREA_CHECK(backend.end_frame().ok());
        AUREA_CHECK(backend.wait_frame(intermediate.frameNumber, 2'000'000'000ull).ok());
        std::vector<u16> pixels(64 * 32 * 4);
        AUREA_CHECK(backend.read_texture(target, pixels.data(), 64 * 8).ok());
        for (usize p = 0; p < pixels.size(); ++p)
            AUREA_CHECK_EQ(pixels[p], scene3d::float_to_half(clear[p % 4]));
        AUREA_CHECK_EQ(wsi.acquires, 1u); AUREA_CHECK_EQ(wsi.presents, 0u);
    }
    FrameBegin final;
    AUREA_CHECK(backend.begin_leased_frame(final).ok());
    AUREA_CHECK_EQ(final.backbuffer.id, held.id);
    AUREA_CHECK_EQ(final.backbufferWidth, initial.backbufferWidth);
    AUREA_CHECK_EQ(final.backbufferHeight, initial.backbufferHeight);
    AUREA_CHECK_EQ(final.rotation, initial.rotation);
    vk::DiscardFrameTestAccess::mark_unused_backbuffer_present(backend);
    backend.defer_until_gpu_done([](void* context) { ++*static_cast<u32*>(context); }, &callbacks);
    AUREA_CHECK(backend.end_frame().ok());
    backend.end_presentable_frame_lease();
    backend.wait_idle();
    AUREA_CHECK_EQ(callbacks, 8u);
    AUREA_CHECK_EQ(wsi.acquires, 1u); AUREA_CHECK_EQ(wsi.presents, 1u);
    AUREA_CHECK_EQ(wsi.frameWaits, 1u); AUREA_CHECK_EQ(wsi.frameSignals, 1u);
    AUREA_CHECK_EQ(surfaceCapabilities.queries, 1u);
    AUREA_CHECK_EQ(vk::Backend::validation_errors(), errors);
    std::printf("\n    lease proof: WSI/capabilities=intercepted queue/fences/pixels=real stages=6 rgba16f_channels=49152 acquire=1 intermediate_present=0 final_present=1");
}
#endif

#if defined(AUREA_TEST_VULKAN) && defined(AUREA_TEST_GLES)
AUREA_TEST(FrameGraphSegmentsLeaseGpu, GlesWithoutWindowRejectsLeaseAndKeepsRealOffscreenFramesUsable) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& backend = isolated.backend;
    AUREA_CHECK(!backend.has_surface());
    for (u32 round = 0; round < 4; ++round) {
        FrameBegin frame; AUREA_CHECK(backend.begin_offscreen_frame(frame).ok());
        AUREA_CHECK_EQ(backend.begin_presentable_frame_lease().code(), Errc::InvalidState);
        AUREA_CHECK(backend.end_frame().ok());
        AUREA_CHECK(backend.wait_frame(frame.frameNumber, 2'000'000'000ull).ok());
        FrameBegin leased;
        AUREA_CHECK_EQ(backend.begin_leased_frame(leased).code(), Errc::InvalidState);
        AUREA_CHECK(!leased.backbuffer.valid());
        backend.end_presentable_frame_lease();
    }
    std::printf("\n    GLES lease proof: offscreen rejection and real fence reuse only; native EGL window not exercised");
}
#endif
