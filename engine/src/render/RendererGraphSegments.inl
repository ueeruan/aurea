// Included in Renderer.cpp. This is an offline recovery after whole-graph
// admission failed; it preserves the same sorted passes, pixels and callbacks.
Status Renderer::render_segmented_graph(const RenderSettings& settings, FrameBegin& frame,
    RenderTimings& timings, bool& frameOpen) noexcept {
    if (!backend_ || !frameOpen || frame.backbuffer.valid())
        return Status{Errc::InvalidState, "graph recovery requires an offscreen frame"};
    struct ImportedHandleScope {
        GPUBackend& backend;
        explicit ImportedHandleScope(GPUBackend& value) : backend(value) { backend.begin_external_image_scope(); }
        ~ImportedHandleScope() { backend.end_external_image_scope(); }
    } imported{*backend_};
    timings.gpuProfileFragmented = true;
    const u64 started = monotonic_ns();
    constexpr u64 hardCap = 120ull * 1000 * 1000 * 1000;
    const u64 limit = std::clamp<u64>(settings.sceneExposureFenceTimeoutNs, 1, hardCap);
    const auto deadline = [&] {
        return settings.offscreenFenceDeadline
            ? settings.offscreenFenceDeadline(settings.offscreenFenceContext) : started + limit;
    };
    const auto cancelled = [&] { return settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire); };
    u32 batches = 0;
    f32 waitedMs = 0;
    auto wait = [&](u64 pending) {
        const u64 waitStarted = monotonic_ns();
        Status status;
        do {
            if (cancelled()) { status = Errc::Cancelled; break; }
            const u64 now = monotonic_ns(), until = deadline();
            if (now >= until) { status = Errc::Timeout; break; }
            status = backend_->wait_frame(pending, std::min<u64>(100ull * 1000 * 1000, until - now));
            if (status.code() != Errc::Timeout) break;
        } while (true);
        if (status.ok() && settings.offscreenFenceCompleted) {
            settings.offscreenFenceCompleted(settings.offscreenFenceContext, pending);
            if (monotonic_ns() >= deadline()) status = Errc::Timeout;
        }
        waitedMs += static_cast<f32>((monotonic_ns() - waitStarted) * 1e-6);
        // No unfinished physical slot can be recycled by another render. The
        // ordinary next request must poll this exact submitted fence first.
        if (!status.ok()) failedGraphFrame_ = pending;
        return status;
    };
    auto finish = [&](Status status) {
        AUREA_LOG_INFO("graph memoria em etapas: passes=%u lotes=%u espera=%.3f ms resultado=%u GPU-total-indisponivel=perfil-fragmentado",
            static_cast<u32>(graph_.order().size()), batches, waitedMs, status.raw());
        return status;
    };
    // A failed whole compile recorded no graph passes, but source uploads may
    // exist in its command stream. Preserve their actual submission ownership
    // before returning any backing. Do not reset the graph or CPU arena.
    graph_.release(pool_);
    pool_.end_frame();
    const Status initial = backend_->end_frame();
    frameOpen = false;
    if (!initial.ok()) return finish(initial);
    if (const Status ready = wait(frame.frameNumber); !ready.ok()) return finish(ready);
    (void)pool_.trim_unreferenced(frame.frameNumber);
    if (const Status plan = graph_.prepare_segments(); !plan.ok()) return finish(plan);
    const u32 steps = static_cast<u32>(graph_.order().size());
    if (!steps) return finish(Status{Errc::InvalidState, "empty graph recovery"});
    for (u32 first = 0; first < steps; ++first) {
        if (cancelled()) return finish(Errc::Cancelled);
        if (monotonic_ns() >= deadline()) return finish(Errc::Timeout);
        FrameBegin next;
        if (const Status begin = backend_->begin_offscreen_frame(next); !begin.ok()) return finish(begin);
        frame = next;
        frameOpen = true;
        pool_.begin_frame(*backend_, frame.frameNumber);
        if (const Status admitted = graph_.compile_segment(pool_, first, 1); !admitted.ok()) return finish(admitted);
        if (!frame.commands) return finish(Errc::InvalidState);
        // Backend queries report the latest submission repeatedly, without a
        // frame identity. A partial timed segment could contaminate a later
        // normal render's whole-frame profile; keep this recovery untimed.
        graph_.execute(*frame.commands, false);
        ++batches;
        // Leave the final frame open for the original NV12 readback and final
        // end_frame. No intermediate frame acquires or presents a swap image.
        if (first + 1 == steps) {
            timings.sceneExposureWaitMs += waitedMs;
            return finish(OkStatus);
        }
        const Status submitted = backend_->end_frame();
        frameOpen = false;
        if (!submitted.ok()) return finish(submitted);
        if (const Status ready = wait(frame.frameNumber); !ready.ok()) return finish(ready);
        // Lifetime dead != GPU completed. Only now release and destroy slots
        // with no future graph readers; imported/output/live slots stay held.
        graph_.release_completed_segment(pool_);
        pool_.end_frame();
        (void)pool_.trim_unreferenced(frame.frameNumber);
    }
    return finish(Errc::InvalidState);
}
