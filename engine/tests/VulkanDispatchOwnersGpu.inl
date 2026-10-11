#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
namespace {
// Use the function resolver for this exact owner. Different owners may return
// equal addresses; the contract concerns their dispatchable handles, not PFN
// inequality. Keep optional/platform entrypoints nullable as their driver does.
void check_dispatch_owner(vk::Backend& backend,
                          const vk::InstanceDispatch& savedInstance,
                          const vk::DeviceDispatch& savedDevice) {
#define AUREA_CHECK_INSTANCE_OWNER(name) \
    AUREA_CHECK(backend.instance_dispatch().name == savedInstance.name); \
    AUREA_CHECK(backend.instance_dispatch().name == reinterpret_cast<PFN_##name>( \
        vk::vkGetInstanceProcAddr(backend.instance(), #name)));
    AUREA_VK_INSTANCE_FUNCTIONS(AUREA_CHECK_INSTANCE_OWNER)
    AUREA_VK_PLATFORM_INSTANCE_FUNCTIONS(AUREA_CHECK_INSTANCE_OWNER)
#undef AUREA_CHECK_INSTANCE_OWNER
#define AUREA_CHECK_DEVICE_OWNER(name) \
    AUREA_CHECK(backend.device_dispatch().name == savedDevice.name); \
    AUREA_CHECK(backend.device_dispatch().name == reinterpret_cast<PFN_##name>( \
        backend.instance_dispatch().vkGetDeviceProcAddr(backend.device(), #name)));
    AUREA_VK_DEVICE_FUNCTIONS(AUREA_CHECK_DEVICE_OWNER)
    AUREA_VK_PLATFORM_DEVICE_FUNCTIONS(AUREA_CHECK_DEVICE_OWNER)
#undef AUREA_CHECK_DEVICE_OWNER
}

bool copy_owner_pixels(Gpu& owner, u32 seed, u32& realFences, u32& exactChannels) {
    constexpr u32 width = 64, height = 32;
    auto& backend = owner.backend;
    TextureDesc desc;
    desc.width = width; desc.height = height; desc.mipLevels = 2;
    desc.format = SurfaceFormat::RGBA16F;
    desc.sampled = desc.transferDst = desc.transferSrc = true;
    const auto input = backend.create_texture(desc);
    AUREA_CHECK(input.ok()); if (!input.ok()) return false;
    struct InputOwner {
        vk::Backend& backend; TextureHandle input;
        ~InputOwner() { backend.wait_idle(); backend.destroy_texture(input); }
    } inputOwner{backend, *input};
    std::vector<u16> expected(width * height * 4);
    for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
        const usize at = (static_cast<usize>(y) * width + x) * 4;
        expected[at] = scene3d::float_to_half((1.f + x % 4) / 8.f);
        expected[at + 1] = scene3d::float_to_half((1.f + seed % 5) / 8.f);
        expected[at + 2] = scene3d::float_to_half((1.f + (x / 4 + seed) % 4) / 8.f);
        expected[at + 3] = scene3d::float_to_half(1.f);
    }
    AUREA_CHECK(backend.upload_texture(*input, expected.data(), width * 8).ok());
    // Exercises real immediate record callbacks, their owner-specific command
    // dispatch, and generated mip barriers without modifying level-zero bytes.
    AUREA_CHECK(backend.generate_mipmaps(*input).ok());
    const auto target = owner.target(width, height);
    const auto pipeline = owner.renderer.shaders().pipeline(
        PipelineKey::fullscreen(ShaderId::common_copy_frag, SurfaceFormat::RGBA16F));
    AUREA_CHECK(pipeline.ok()); if (!pipeline.ok()) return false;
    FrameBegin frame;
    const auto begun = backend.begin_offscreen_frame(frame);
    AUREA_CHECK(begun.ok()); if (!begun.ok()) return false;
    auto& commands = *frame.commands;
    commands.barrier(*input, ResourceState::ShaderRead, false);
    commands.barrier(target, ResourceState::ColorAttachment, true);
    RenderPassBegin pass; pass.color = target; pass.load = LoadOp::DontCare;
    commands.begin_render_pass(pass);
    commands.bind_pipeline(*pipeline);
    commands.bind_texture(0, *input, owner.renderer.shaders().sampler(CommonSampler::NearestClamp));
    const Vec4 identity{1, 1, 0, 0}; commands.set_uniforms(&identity, sizeof(identity));
    commands.draw(3); commands.end_render_pass();
    commands.barrier(target, ResourceState::ShaderRead, false);
    const auto ended = backend.end_frame();
    AUREA_CHECK(ended.ok()); if (!ended.ok()) return false;
    const auto waited = backend.wait_frame(frame.frameNumber, 2'000'000'000ull);
    AUREA_CHECK(waited.ok()); if (!waited.ok()) return false;
    ++realFences;
    std::vector<u16> output(expected.size()), source(expected.size());
    AUREA_CHECK(backend.read_texture(target, output.data(), width * 8).ok());
    AUREA_CHECK(backend.read_texture(*input, source.data(), width * 8).ok());
    AUREA_CHECK(source == expected); // The independent input remains immutable.
    AUREA_CHECK(output == expected); // Every component, not a screenshot sample.
    if (source != expected || output != expected) return false;
    exactChannels += static_cast<u32>(output.size());
    return true;
}

void mixed_dispatch_owners(bool destroyOlder) {
    AUREA_REQUIRE_GPU();
    const u32 errors = vk::Backend::validation_errors();
    const u32 warnings = vk::Backend::validation_warnings();
    for (const bool firstValidation : {true, false}) {
        u32 realFences = 0, exactChannels = 0;
        auto older = std::make_unique<Gpu>(false, firstValidation);
        AUREA_CHECK(older->ok); if (!older->ok) return;
        const auto olderInstance = older->backend.instance_dispatch();
        const auto olderDevice = older->backend.device_dispatch();
        auto younger = std::make_unique<Gpu>(false, !firstValidation);
        AUREA_CHECK(younger->ok); if (!younger->ok) return;
        const auto youngerInstance = younger->backend.instance_dispatch();
        const auto youngerDevice = younger->backend.device_dispatch();
        AUREA_CHECK_EQ(older->backend.capabilities().validationEnabled, firstValidation);
        AUREA_CHECK_EQ(younger->backend.capabilities().validationEnabled, !firstValidation);
        check_dispatch_owner(older->backend, olderInstance, olderDevice);
        check_dispatch_owner(younger->backend, youngerInstance, youngerDevice);
        AUREA_CHECK(copy_owner_pixels(*older, 1, realFences, exactChannels));
        AUREA_CHECK(copy_owner_pixels(*younger, 4, realFences, exactChannels));
        auto& retiring = destroyOlder ? older : younger;
        auto& survivor = destroyOlder ? younger : older;
        retiring->renderer.shutdown(); retiring->backend.shutdown();
        retiring->backend.shutdown(); // Guarded repeat teardown must stay safe.
        retiring.reset();
        check_dispatch_owner(survivor->backend,
            destroyOlder ? youngerInstance : olderInstance,
            destroyOlder ? youngerDevice : olderDevice);
        const bool survivorVerified = copy_owner_pixels(*survivor, 3, realFences, exactChannels);
        AUREA_CHECK(survivorVerified);
        survivor->backend.wait_idle();
        survivor.reset();
        AUREA_CHECK_EQ(realFences, 3u);
        AUREA_CHECK_EQ(exactChannels, 24576u);
        AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
        AUREA_CHECK_EQ(vk::Backend::validation_warnings() - warnings, 0u);
        std::printf("\n    dispatch owners: first_validation=%u destroyed=%s real_fences=%u exact_rgba16f_channels=%u survivor_verified=%u validation_errors=%u validation_warnings=%u",
            firstValidation, destroyOlder ? "older" : "younger", realFences, exactChannels,
            survivorVerified, vk::Backend::validation_errors() - errors,
            vk::Backend::validation_warnings() - warnings);
    }
}
}

AUREA_TEST(VulkanDispatchOwnersGpu, MixedValidationOwnersKeepIndependentPixelsAndDispatchAfterYoungerShutdown) {
    mixed_dispatch_owners(false);
}
AUREA_TEST(VulkanDispatchOwnersGpu, MixedValidationOwnersKeepIndependentPixelsAndDispatchAfterOlderShutdown) {
    mixed_dispatch_owners(true);
}
#endif
