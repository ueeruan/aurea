// Included in Engine.cpp. The render mutex owns the immutable snapshot and
// graph; the atomic hint only lets external captures return Busy immediately.
void Engine::cancel_preview_graph_locked() noexcept {
    renderer_.cancel_preview_graph();
    livePreviewGraphActive_.store(false, std::memory_order_release);
}

bool Engine::continue_preview_graph_locked(u64 frameStart, Status& result) noexcept {
    if (!renderer_.preview_graph_pending()) {
        livePreviewGraphActive_.store(false, std::memory_order_release);
        return false;
    }
    bool playing = false;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        const bool refine = refineNow_;
        refineNow_ = livePreviewGraphRefine_;
        const RenderSettings settings = current_render_settings();
        refineNow_ = refine;
        const bool changed = !project_ || !surfaceAttached_ || pendingSurfaceSize_.load(std::memory_order_acquire)
            || state() != EngineState::Ready || gpuGeneration_ != livePreviewGraphGpuGeneration_
            || projectSession_ != livePreviewGraphKey_.session
            || modelRevision_.load(std::memory_order_acquire) != livePreviewGraphKey_.revision
            || project_->timeline().current().pack() != livePreviewGraphKey_.composition
            || playback_.generation() != livePreviewGraphPlaybackGeneration_
            || playback_.media_epoch() != livePreviewGraphKey_.mediaEpoch
            || playback_.playing() != livePreviewGraphPlaying_
            || !renderer_.preview_graph_settings_match(settings);
        if (changed || !renderer_.preview_graph_pending()) {
            cancel_preview_graph_locked();
            forceRender_.store(true, std::memory_order_release);
            previewRetryDueNs_.store(0, std::memory_order_release);
            return false;
        }
        // Playback/audio keep their actual clocks. Natural time advancement
        // does not replace the frozen graph or reset its media epoch.
        if (Composition* composition = current_composition()) {
            configure_playback_locked(composition);
            sync_audio_locked(*composition);
            project_->timeline().set_playhead(playback_.update(frameStart));
            sync_audio_locked(*composition);
        }
        playing = playback_.playing();
        // A natural loop/end can change identity inside update itself. Do
        // not publish the previous interval after this boundary transition.
        if (playback_.generation() != livePreviewGraphPlaybackGeneration_
            || playback_.media_epoch() != livePreviewGraphKey_.mediaEpoch
            || playing != livePreviewGraphPlaying_) {
            cancel_preview_graph_locked();
            forceRender_.store(true, std::memory_order_release);
            previewRetryDueNs_.store(0, std::memory_order_release);
            return false;
        }
    }
    FrameStats stats;
    RenderTimings timings;
    const Status continued = renderer_.continue_preview_graph(stats, timings);
    livePreviewGraphActive_.store(renderer_.preview_graph_pending(), std::memory_order_release);
    if (continued.code() == Errc::Busy && renderer_.preview_graph_pending()) {
        lastSkipped_ = true;
        nextFrameDueNs_ = monotonic_ns() + 2'000'000ull;
        previewRetryDueNs_.store(nextFrameDueNs_, std::memory_order_release);
        result = OkStatus; // internal backpressure is not an editor error
        return true;
    }
    if (!continued.ok()) {
        set_last_error(continued, "continuar preview");
        lastSkipped_ = lastIncomplete_ = true;
        nextFrameDueNs_ = monotonic_ns() + 50'000'000ull;
        previewRetryDueNs_.store(nextFrameDueNs_, std::memory_order_release);
        forceRender_.store(true, std::memory_order_release);
        result = continued;
        return true;
    }
    lastRenderedFrame_ = livePreviewGraphKey_.playhead;
    lastRenderedRevision_ = static_cast<u32>(livePreviewGraphKey_.revision);
    lastIncomplete_ = renderer_.take_incomplete() || snapshot_.missingVideoFrames || snapshot_.staleVideoFrames;
    lastSkipped_ = false;
    incompleteRetries_ = 0;
    lastCulledLayers_.store(snapshot_.culledLayers, std::memory_order_relaxed);
    frameScheduler_.presented(FrameIndex{livePreviewGraphKey_.playhead}, playing);
    stats.frameIndex = static_cast<u32>(frameCounter_);
    stats.cpuMs = timings.cpuPrepareMs + timings.cpuRecordMs;
    stats.decodeMs = media_.stats().decodeMsAvg;
    stats.droppedFrames = frameScheduler_.dropped_total();
    stats.cpuMemoryBytes = memory_.total_used();
    stats.memoryPressure = memory_.pressure();
    // Fragmented GPU timestamps and time between turns cannot drive AUTO.
    // The completed picture kept its frozen density/samples/effects. A real
    // admission failure can still lower the NEXT automatic picture, without
    // fabricating GPU timings or overriding an explicit manual selection.
    if (adapt().notify_gpu_budget_exceeded(caps_.thermal()))
        forceRender_.store(true, std::memory_order_release);
    update_perf(stats, timings, snapshot_, frameStart);
    refinePending_ = false;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        const PreviewIdleKey current{projectSession_, modelRevision_.load(std::memory_order_acquire),
            project_ ? project_->timeline().current().pack() : 0, playback_.media_epoch(), playback_.current().value};
        const bool eligible = !playing && !lastIncomplete_ && !previewBuffering_
            && playback_.mode() == PlaybackMode::Paused && !rawPlaybackLayer_.valid()
            && surfaceAttached_ && state() == EngineState::Ready
            && !offscreenReaders_.load(std::memory_order_acquire)
            && !caps_.thermal().should_degrade() && memory_.pressure() < .85f && !preview_memory_hold();
        previewIdle_.observe(eligible, current, monotonic_ns(), false);
    }
    media_.collect(frameCounter_);
    result = OkStatus;
    return true;
}
