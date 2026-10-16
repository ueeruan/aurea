// Included in Renderer.cpp. The display target and immutable graph survive
// render turns; only a completed graph may present the leased target.
bool Renderer::preview_graph_settings_match(const RenderSettings& value) const noexcept {
    if (!previewGraph_.active) return true;
    const auto& saved = previewGraph_.settings;
    return saved.previewNumerator == value.previewNumerator
        && saved.previewDenominator == value.previewDenominator
        && saved.heavyScale == value.heavyScale && saved.flowPolicyScale == value.flowPolicyScale
        && saved.viewportZoom == value.viewportZoom
        && saved.viewportPan.x == value.viewportPan.x && saved.viewportPan.y == value.viewportPan.y
        && saved.dither == value.dither && saved.editorBackground == value.editorBackground
        && saved.pasteboard == value.pasteboard && saved.finalQuality == value.finalQuality
        && saved.rawPlayback == value.rawPlayback && saved.mediaGeneration == value.mediaGeneration
        && saved.sceneEditor.enabled == value.sceneEditor.enabled
        && saved.sceneEditor.yaw == value.sceneEditor.yaw && saved.sceneEditor.pitch == value.sceneEditor.pitch
        && saved.sceneEditor.distance == value.sceneEditor.distance
        && saved.quality.motionBlurSamples == value.quality.motionBlurSamples
        && saved.quality.flowResolution == value.quality.flowResolution
        && saved.quality.ssao == value.quality.ssao
        && saved.quality.shadowResolution == value.quality.shadowResolution
        && saved.quality.particles == value.quality.particles
        && saved.quality.blurSamples == value.quality.blurSamples
        && saved.quality.lodBias == value.quality.lodBias
        && (!saved.heavyExplicit || (saved.heavy.particles == value.heavy.particles
            && saved.heavy.flow == value.heavy.flow && saved.heavy.flowBlurSamples == value.heavy.flowBlurSamples
            && saved.heavy.shadowMapSize == value.heavy.shadowMapSize && saved.heavy.shadowFilter == value.heavy.shadowFilter
            && saved.heavy.lodBias == value.heavy.lodBias && saved.heavy.effects == value.heavy.effects
            && saved.heavy.exportFrame == value.heavy.exportFrame && saved.heavy.msaaSamples == value.heavy.msaaSamples
            && saved.heavy.fxaa == value.heavy.fxaa && saved.heavy.bloomStartDiv == value.heavy.bloomStartDiv
            && saved.heavy.bloomLevels == value.heavy.bloomLevels))
        && saved.heavyExplicit == value.heavyExplicit;
}

void Renderer::release_preview_graph_scope() noexcept {
    auto& pending = previewGraph_;
    if (!pending.active) return;
    const u64 fence = backend_ ? backend_->last_submitted_frame() : 0;
    for (u32 i = 0; i < pending.exposureCount; ++i) if (pending.exposureTextures[i].valid()) {
        if (fence) pool_.retire(pending.exposureTextures[i], fence);
        else pool_.release(pending.exposureTextures[i]);
    }
    for (u32 i = 0; i < pending.stagedCount; ++i) if (pending.stagedTargets[i].texture.valid()) {
        if (fence) pool_.retire(pending.stagedTargets[i].texture, fence);
        else pool_.release(pending.stagedTargets[i].texture);
    }
    if (backend_) {
        backend_->end_presentable_frame_lease();
        backend_->end_external_image_scope();
    }
    boundedSceneTextures_ = {};
    stagedSceneTargets_ = {};
    pending = {};
}

void Renderer::cancel_preview_graph() noexcept {
    auto& pending = previewGraph_;
    if (!pending.active) return;
    graph_.release(pool_);
    pool_.end_frame();
    if (backend_ && pending.waitingFrame) failedGraphFrame_ = pending.waitingFrame;
    if (pending.completion.cached) {
        pending.completion.cached->complete = false;
        pending.completion.cached->time = -1;
        publish_preview_buffer_ranges();
    }
    for (FrameRef& frame : framesInFlight_) {
        DecodedFrame* raw = frame.detach();
        if (raw && backend_) backend_->defer_until_gpu_done(
            [](void* value) { static_cast<DecodedFrame*>(value)->release(); }, raw);
        else if (raw) raw->release();
    }
    framesInFlight_.clear();
    // TimeSlice: faixas convertidas neste grafo nunca rodaram na GPU. Ficar no
    // cache seria mostrar uma faixa vazia; saem e o decoder as refaz.
    if (sliceGraphFrame_ != ~0ull) {
        std::lock_guard<std::mutex> sl(sliceMutex_);
        for (SliceTexture& e : sliceCache_) {
            if (e.created != sliceGraphFrame_ || !e.tex.valid()) continue;
            if (backend_) backend_->destroy_texture(e.tex);
            e.tex = TextureHandle{};
        }
        sliceCache_.erase(std::remove_if(sliceCache_.begin(), sliceCache_.end(),
            [](const SliceTexture& e) { return !e.tex.valid(); }), sliceCache_.end());
        sliceGraphFrame_ = ~0ull;
    }
    if (pending.snapshot) pending.snapshot->release_video_frames();
    release_preview_graph_scope();
}

Status Renderer::continue_preview_graph(FrameStats& stats, RenderTimings& timings) noexcept {
    if (!previewGraph_.active || !backend_) return Errc::InvalidState;
    auto& pending = previewGraph_;
    stats = {};
    timings = pending.timings;
    read_timings(timings);
    const u64 turnStarted = monotonic_ns();
    const u64 turnDeadline = turnStarted + 8'000'000ull;
    const u64 totalLimit = std::clamp<u64>(pending.settings.sceneExposureFenceTimeoutNs, 1, 120'000'000'000ull);
    auto fail = [&](Status status) {
        cancel_preview_graph();
        return status;
    };
    do {
        if (pending.settings.cancelFlag && pending.settings.cancelFlag->load(std::memory_order_acquire))
            return fail(Errc::Cancelled);
        if (monotonic_ns() - pending.started >= totalLimit) return fail(Errc::Timeout);
        if (pending.waitingFrame) {
            // No CPU wait. Older display pixels remain visible until a later
            // turn observes the real fence and can reclaim physical backing.
            const Status ready = backend_->wait_frame(pending.waitingFrame, 0);
            if (ready.code() == Errc::Timeout) return Status{Errc::Busy, "preview GPU segment pending"};
            if (!ready.ok()) return fail(ready);
            if (pending.initial) {
                const Status plan = graph_.prepare_segments();
                if (!plan.ok()) return fail(plan);
                pending.initial = false;
            } else graph_.release_completed_segment(pool_);
            pool_.end_frame();
            (void)pool_.trim_unreferenced(pending.waitingFrame);
            pending.waitingFrame = 0;
        }
        const u32 count = static_cast<u32>(graph_.order().size());
        if (!count || pending.nextPass >= count) return fail(Errc::InvalidState);
        const bool final = pending.nextPass + 1 == count;
        const u64 recordStarted = monotonic_ns();
        FrameBegin frame;
        const Status begun = final ? backend_->begin_leased_frame(frame) : backend_->begin_offscreen_frame(frame);
        if (!begun.ok()) return fail(begun);
        pending.completion.frame = frame;
        pool_.begin_frame(*backend_, frame.frameNumber);
        const Status admitted = graph_.compile_segment(pool_, pending.nextPass, 1);
        if (!admitted.ok() || !frame.commands) {
            const Status original = admitted.ok() ? Status{Errc::InvalidState} : admitted;
            graph_.release(pool_);
            pool_.end_frame();
            const Status ended = final ? backend_->discard_frame() : backend_->end_frame();
            pending.waitingFrame = backend_->last_submitted_frame();
            return fail(ended.ok() ? original : ended);
        }
        graph_.execute(*frame.commands, false);
        ++pending.nextPass;
        pending.activeRecordNs += monotonic_ns() - recordStarted;
        if (final) {
            if (pending.completion.cached) pending.completion.cached->gpuFrame = frame.frameNumber;
            timings = pending.timings;
            timings.gpuProfileFragmented = true;
            const u64 activeNs = pending.activeRecordNs;
            const Status completed = finish_render(*pending.snapshot, pending.settings, nullptr,
                OkStatus, true, pending.completion, stats, timings);
            timings.cpuRecordMs = static_cast<f32>(activeNs * 1e-6);
            AUREA_LOG_INFO("preview graph concluido: passos=%u tempo-parede=%.3f ms CPU-ativo=%.3f ms GPU-total-indisponivel=perfil-fragmentado",
                count, (monotonic_ns() - pending.started) * 1e-6, static_cast<double>(timings.cpuRecordMs));
            release_preview_graph_scope();
            return completed;
        }
        const Status submitted = backend_->end_frame();
        pending.waitingFrame = backend_->last_submitted_frame();
        if (!submitted.ok()) return fail(submitted);
        // Small ready segments may advance in this same short CPU turn. A
        // genuine pending fence immediately yields, without reducing quality.
    } while (monotonic_ns() < turnDeadline);
    return Status{Errc::Busy, "preview graph continuation pending"};
}
