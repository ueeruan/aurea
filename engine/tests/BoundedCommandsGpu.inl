#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
namespace aurea::vk {
struct BoundedCommandsTestAccess {
    static void limit(Backend& b, u32 passes) { b.passesPerCommandBuffer_ = passes; }
    static u32 submitted_buffers(const Backend& b) { return b.lastSubmitted_ ? b.lastSubmitted_->commandBufferCount : 0; }
    static std::vector<VkCommandBuffer> buffers(const Backend& b) {
        return b.lastSubmitted_ ? b.lastSubmitted_->commandBuffers : std::vector<VkCommandBuffer>{};
    }
};
}
namespace {
// All unaffected calls use the real validation/driver dispatch. These hooks
// inject recording/allocation failure, never submit malformed GPU work.
struct BoundedRecordingDispatch {
    enum class Fault { None, Begin, End, Allocate, Submit, Descriptor, Uniform };
    PFN_vkBeginCommandBuffer begin = vk::vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer end = vk::vkEndCommandBuffer;
    PFN_vkAllocateCommandBuffers allocate = vk::vkAllocateCommandBuffers;
    PFN_vkQueueSubmit submit = vk::vkQueueSubmit;
    PFN_vkAllocateDescriptorSets descriptor = vk::vkAllocateDescriptorSets;
    PFN_vkCreateBuffer buffer = vk::vkCreateBuffer;
    inline static BoundedRecordingDispatch* active = nullptr;
    Fault fault = Fault::None;
    u32 failedCalls = 0, submitCalls = 0, submittedBuffers = 0;
    BoundedRecordingDispatch() {
        active = this;
        vk::vkBeginCommandBuffer = begin_call; vk::vkEndCommandBuffer = end_call;
        vk::vkAllocateCommandBuffers = allocate_call; vk::vkQueueSubmit = submit_call;
        vk::vkAllocateDescriptorSets = descriptor_call; vk::vkCreateBuffer = buffer_call;
    }
    ~BoundedRecordingDispatch() { restore(); }
    void restore() {
        if (active != this) return;
        vk::vkBeginCommandBuffer = begin; vk::vkEndCommandBuffer = end;
        vk::vkAllocateCommandBuffers = allocate; vk::vkQueueSubmit = submit;
        vk::vkAllocateDescriptorSets = descriptor; vk::vkCreateBuffer = buffer;
        active = nullptr;
    }
    bool fail(Fault kind) { if (fault != kind) return false; ++failedCalls; return true; }
    static VKAPI_ATTR VkResult VKAPI_CALL begin_call(VkCommandBuffer c, const VkCommandBufferBeginInfo* i) {
        return active->fail(Fault::Begin) ? VK_ERROR_OUT_OF_HOST_MEMORY : active->begin(c, i);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL end_call(VkCommandBuffer c) {
        return active->fail(Fault::End) ? VK_ERROR_OUT_OF_HOST_MEMORY : active->end(c);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL allocate_call(VkDevice d, const VkCommandBufferAllocateInfo* i, VkCommandBuffer* c) {
        return active->fail(Fault::Allocate) ? VK_ERROR_OUT_OF_HOST_MEMORY : active->allocate(d, i, c);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL submit_call(VkQueue q, u32 n, const VkSubmitInfo* i, VkFence f) {
        ++active->submitCalls;
        for (u32 j = 0; j < n; ++j) active->submittedBuffers += i[j].commandBufferCount;
        return active->fail(Fault::Submit) ? VK_ERROR_OUT_OF_HOST_MEMORY : active->submit(q, n, i, f);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL descriptor_call(VkDevice d, const VkDescriptorSetAllocateInfo* i, VkDescriptorSet* s) {
        return active->fail(Fault::Descriptor) ? VK_ERROR_OUT_OF_DEVICE_MEMORY : active->descriptor(d, i, s);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL buffer_call(VkDevice d, const VkBufferCreateInfo* i, const VkAllocationCallbacks* a, VkBuffer* b) {
        return active->fail(Fault::Uniform) ? VK_ERROR_OUT_OF_DEVICE_MEMORY : active->buffer(d, i, a, b);
    }
};
}

AUREA_TEST(BoundedCommandsGpu, SixHundredPassesPreservePixelsBindingsTimersAndReuseOneFencedSubmission) {
    AUREA_REQUIRE_GPU();
    Gpu isolated;
    AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& b = isolated.backend;
    const u32 errors = vk::Backend::validation_errors();
    constexpr u32 width = 16, height = 16, passes = 600;
    TextureDesc d; d.width = d.height = 4; d.format = SurfaceFormat::RGBA16F;
    d.sampled = d.transferDst = true;
    const auto input = b.create_texture(d); AUREA_CHECK(input.ok()); if (!input.ok()) return;
    std::vector<u16> source(4 * 4 * 4);
    for (u32 i = 0; i < 16; ++i) {
        source[i * 4] = scene3d::float_to_half((i % 4 + 1) * .2f);
        source[i * 4 + 1] = scene3d::float_to_half((i / 4 + 1) * .2f);
        source[i * 4 + 2] = scene3d::float_to_half(.25f);
        source[i * 4 + 3] = scene3d::float_to_half(1.f);
    }
    AUREA_CHECK(b.upload_texture(*input, source.data(), 4 * 8).ok());
    const auto target = isolated.target(width, height);
    const auto pipeline = isolated.renderer.shaders().pipeline(PipelineKey::fullscreen(ShaderId::common_copy_frag, d.format));
    AUREA_CHECK(pipeline.ok()); if (!pipeline.ok()) return;
    std::vector<u16> baseline;
    for (const u32 limit : {0u, 128u, 17u}) {
        vk::BoundedCommandsTestAccess::limit(b, limit);
        std::array<std::vector<VkCommandBuffer>, 2> retained;
        // Four frames cycle both pools twice, proving buffers remain reusable
        // after their real fence without allocating a new pool per checkpoint.
        for (u32 frame = 0; frame < 4; ++frame) {
            FrameBegin fb; AUREA_CHECK(b.begin_offscreen_frame(fb).ok()); if (!fb.commands) return;
            auto& cmd = *fb.commands;
            cmd.barrier(*input, ResourceState::ShaderRead);
            cmd.barrier(target, ResourceState::ColorAttachment, true);
            cmd.bind_pipeline(*pipeline);
            cmd.bind_texture(0, *input, isolated.renderer.shaders().sampler(CommonSampler::NearestClamp));
            BoundedRecordingDispatch dispatch;
            for (u32 pass = 0; pass < passes; ++pass) {
                cmd.begin_label("bounded copy"); cmd.begin_timer("bounded copy");
                RenderPassBegin rp; rp.color = target; rp.load = pass ? LoadOp::Load : LoadOp::Clear;
                cmd.begin_render_pass(rp);
                // Reuse pipeline/texture bindings, change only UBO offsets.
                // Some adjacent passes retain the same descriptor set at a cut.
                const Vec4 map{.75f, .75f, (pass % 3) * .1f, (pass % 5) * .05f};
                cmd.set_uniforms(&map, sizeof(map));
                if (pass < 550) cmd.bind_texture(0, *input, isolated.renderer.shaders().sampler(CommonSampler::NearestClamp));
                cmd.set_scissor(static_cast<i32>(pass % width), 0, 1, height);
                cmd.draw(3);
                cmd.end_render_pass(); cmd.end_timer(); cmd.end_label();
                AUREA_CHECK(cmd.finish_pass().ok());
            }
            AUREA_CHECK(b.end_frame().ok());
            const u32 expected = limit ? 1 + passes / limit : 1;
            AUREA_CHECK_EQ(dispatch.submitCalls, 1u);
            AUREA_CHECK_EQ(dispatch.submittedBuffers, expected);
            AUREA_CHECK_EQ(vk::BoundedCommandsTestAccess::submitted_buffers(b), expected);
            dispatch.restore();
            const auto buffers = vk::BoundedCommandsTestAccess::buffers(b);
            if (frame < 2) retained[frame] = buffers;
            else AUREA_CHECK(buffers == retained[frame % 2]);
            AUREA_CHECK(b.wait_frame(fb.frameNumber, 5'000'000'000ull).ok());
            std::vector<u16> pixels(width * height * 4);
            AUREA_CHECK(b.read_texture(target, pixels.data(), width * 8).ok());
            AUREA_CHECK(half_to_float(pixels[0]) > .1f);
            AUREA_CHECK(half_to_float(pixels[3]) > .99f);
            if (baseline.empty()) baseline = pixels;
            else AUREA_CHECK(pixels == baseline);
            // wait_frame guarantees completion; timing collection is exposed
            // by slot reuse/wait_idle, not by that fence-only API.
            b.wait_idle();
            std::array<GpuTiming, 600> timings; f32 total = 0;
            const u32 count = b.read_gpu_timings(timings.data(), static_cast<u32>(timings.size()), &total);
            AUREA_CHECK_EQ(count, 512u); AUREA_CHECK(std::isfinite(total));
        }
    }
    b.destroy_texture(*input); b.wait_idle();
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
}

AUREA_TEST(BoundedCommandsGpu, IndexedMrtDrawRetainsTheTailOfPartiallyUpdatedPushConstants) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    auto& b = isolated.backend;
    const u32 errors = vk::Backend::validation_errors();
    TextureDesc d; d.width = d.height = 8; d.format = SurfaceFormat::RGBA16F;
    d.renderTarget = d.sampled = d.transferDst = d.transferSrc = true;
    const auto source = b.create_texture(d), display = b.create_texture(d), hdr = b.create_texture(d);
    AUREA_CHECK(source.ok() && display.ok() && hdr.ok());
    if (!source.ok() || !display.ok() || !hdr.ok()) return;
    TextureDesc z = d; z.format = SurfaceFormat::Depth32F;
    z.sampled = z.transferDst = z.transferSrc = false;
    const auto depth = b.create_texture(z); AUREA_CHECK(depth.ok()); if (!depth.ok()) return;
    std::vector<u16> data(8 * 8 * 4);
    for (usize i = 0; i < data.size(); i += 4) {
        data[i] = scene3d::float_to_half(.4f); data[i + 1] = scene3d::float_to_half(.6f);
        data[i + 2] = scene3d::float_to_half(.8f); data[i + 3] = scene3d::float_to_half(1.f);
    }
    AUREA_CHECK(b.upload_texture(*source, data.data(), 8 * 8).ok());
    BufferDesc ib; ib.bytes = 3 * sizeof(u16); ib.usage = BufferUsage::Index; ib.access = MemoryAccess::Upload;
    const auto indices = b.create_buffer(ib); AUREA_CHECK(indices.ok()); if (!indices.ok()) return;
    const u16 triangle[] = {0, 1, 2};
    AUREA_CHECK(b.write_buffer(*indices, 0, triangle, sizeof(triangle)).ok());
    auto key = PipelineKey::fullscreen(ShaderId::scene3d_plane_frag, SurfaceFormat::RGBA16F);
    key.hasDepth = key.hasColor1 = true;
    const auto pipeline = isolated.renderer.shaders().pipeline(key);
    AUREA_CHECK(pipeline.ok()); if (!pipeline.ok()) return;
    std::vector<u16> baseline;
    for (const u32 limit : {0u, 1u}) {
        vk::BoundedCommandsTestAccess::limit(b, limit);
        FrameBegin fb; AUREA_CHECK(b.begin_offscreen_frame(fb).ok()); if (!fb.commands) return;
        auto& cmd = *fb.commands;
        cmd.barrier(*source, ResourceState::ShaderRead);
        cmd.bind_pipeline(*pipeline);
        cmd.bind_texture(0, *source, isolated.renderer.shaders().sampler(CommonSampler::NearestClamp));
        cmd.bind_index_buffer(*indices, 0, IndexType::U16);
        // Actual plane shader: its alpha mode is in the last vec4 (offset96).
        // Later writes replace only an unused first vec4, retaining that tail.
        std::array<f32, 28> push{}; push[24] = .5f; push[25] = 1.f;
        cmd.push_constants(push.data(), static_cast<u32>(sizeof(push)));
        for (u32 pass = 0; pass < 3; ++pass) {
            RenderPassBegin rp; rp.color = *display; rp.color1 = *hdr; rp.depth = *depth;
            rp.load = pass ? LoadOp::Load : LoadOp::Clear;
            cmd.begin_render_pass(rp); cmd.draw_indexed(3); cmd.end_render_pass();
            const Vec4 prefix{static_cast<f32>(pass), 2, 3, 4};
            cmd.push_constants(&prefix, sizeof(prefix));
            AUREA_CHECK(cmd.finish_pass().ok());
        }
        AUREA_CHECK(b.end_frame().ok()); b.wait_idle();
        std::vector<u16> pixels(data.size() * 2);
        AUREA_CHECK(b.read_texture(*display, pixels.data(), 8 * 8).ok());
        AUREA_CHECK(b.read_texture(*hdr, pixels.data() + data.size(), 8 * 8).ok());
        AUREA_CHECK_NEAR(half_to_float(pixels[0]), .2f, .001f);
        AUREA_CHECK_NEAR(half_to_float(pixels[3]), .5f, .001f);
        AUREA_CHECK_NEAR(half_to_float(pixels[data.size() + 3]), .5f, .001f);
        if (baseline.empty()) baseline = pixels;
        else AUREA_CHECK(pixels == baseline);
    }
    for (const auto t : {*source, *display, *hdr, *depth}) b.destroy_texture(t);
    b.destroy_buffer(*indices); b.wait_idle();
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
}

AUREA_TEST(BoundedCommandsGpu, RecordingFailuresAbortWithoutPublishingOrReleasingPendingFrameResources) {
    AUREA_REQUIRE_GPU();
    using Fault = BoundedRecordingDispatch::Fault;
    for (const auto fault : {Fault::Begin, Fault::End, Fault::Allocate, Fault::Submit, Fault::Descriptor, Fault::Uniform}) {
        Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
        auto& b = isolated.backend;
        const u32 errors = vk::Backend::validation_errors();
        vk::BoundedCommandsTestAccess::limit(b, 1);
        const auto target = isolated.target(16, 16);
        const auto pipeline = isolated.renderer.shaders().pipeline(PipelineKey::fullscreen(ShaderId::common_copy_frag, SurfaceFormat::RGBA16F));
        AUREA_CHECK(pipeline.ok()); if (!pipeline.ok()) return;
        FrameBegin prior; AUREA_CHECK(b.begin_offscreen_frame(prior).ok());
        AUREA_CHECK(b.end_frame().ok());
        bool priorReleased = false, abortedReleased = false;
        b.defer_until_gpu_done([](void* p) { *static_cast<bool*>(p) = true; }, &priorReleased);
        DelayedFenceObservation observation(b.device());
        FrameBegin fb; AUREA_CHECK(b.begin_offscreen_frame(fb).ok()); if (!fb.commands) return;
        b.defer_until_gpu_done([](void* p) { *static_cast<bool*>(p) = true; }, &abortedReleased);
        fb.commands->barrier(target, ResourceState::ColorAttachment, true);
        RenderPassBegin rp; rp.color = target;
        fb.commands->begin_render_pass(rp);
        fb.commands->bind_pipeline(*pipeline);
        BoundedRecordingDispatch dispatch; dispatch.fault = fault;
        Status status;
        if (fault == Fault::Descriptor) {
            fb.commands->draw(3);
            status = fb.commands->finish_pass();
        } else if (fault == Fault::Uniform) {
            std::vector<u8> largeUniforms(300 * 1024);
            fb.commands->set_uniforms(largeUniforms.data(), static_cast<u32>(largeUniforms.size()));
            status = fb.commands->finish_pass();
        } else {
            fb.commands->end_render_pass();
            status = fault == Fault::Submit ? b.end_frame() : fb.commands->finish_pass();
        }
        AUREA_CHECK(!status.ok()); AUREA_CHECK(dispatch.failedCalls > 0);
        AUREA_CHECK_EQ(b.last_submitted_frame(), prior.frameNumber);
        AUREA_CHECK_EQ(dispatch.submitCalls, fault == Fault::Submit ? 1u : 0u);
        AUREA_CHECK(!b.is_device_lost());
        AUREA_CHECK(b.end_frame().code() == status.code());
        FrameBegin retry; AUREA_CHECK(b.begin_offscreen_frame(retry).code() == status.code());
        AUREA_CHECK(retry.commands == nullptr);
        // A stale CommandList must be harmless and resource entry points must
        // reject layouts changed by the aborted frame, even after hooks clear.
        dispatch.restore();
        fb.commands->begin_render_pass(rp); fb.commands->bind_pipeline(*pipeline);
        fb.commands->set_viewport(0, 0, 16, 16); fb.commands->set_scissor(0, 0, 16, 16);
        fb.commands->draw(3); fb.commands->end_render_pass();
        std::vector<u16> pixels(16 * 16 * 4);
        AUREA_CHECK(b.read_texture(target, pixels.data(), 16 * 8).code() == status.code());
        AUREA_CHECK(b.upload_texture(target, pixels.data(), 16 * 8).code() == status.code());
        AUREA_CHECK(!priorReleased); AUREA_CHECK(!abortedReleased);
        observation.release();
        b.wait_idle();
        AUREA_CHECK(priorReleased); AUREA_CHECK(abortedReleased);
        AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
    }
}
#endif
