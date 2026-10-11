// Included in Renderer.cpp. A completed pure scene needs only its final pixels
// beside later 2D effects, rather than all shadow/MSAA/post intermediate targets.
namespace {
bool pure_unblurred_scene(const scene3d::SceneFrame& scene) noexcept {
    return scene.blurFrames.empty() && scene.planeLayers.empty() && scene.particleLayers.empty();
}
u32 unblurred_scene_target_count(const FrameSnapshot& snap, u32 depth) noexcept {
    u32 count = 0;
    for (const auto& scene : snap.scenes) if (pure_unblurred_scene(scene)) ++count;
    if (depth < 8) for (const auto& child : snap.nested)
        if (child) count += unblurred_scene_target_count(*child, depth + 1);
    return count;
}
}

Status Renderer::reclaim_failed_transients(u64 timeoutNs) noexcept {
    if (!backend_ || !failedGraphFrame_) return OkStatus;
    const u64 completed = failedGraphFrame_;
    const Status ready = backend_->wait_frame(completed, timeoutNs);
    if (!ready.ok()) return ready;
    // The render call which installed this frame already released the graph
    // and returned through all held-output RAII. Its uploads may still have
    // been submitted, so neither graph reset nor physical retirement preceded
    // the fence. Quarantined/held pool entries remain inUse and cannot be freed.
    graph_.reset();
    arena_.reset();
    const u64 before = pool_.stats().bytes;
    const auto gpuBefore = backend_->memory_stats();
    const u32 removed = pool_.trim_unreferenced(completed);
    failedGraphFrame_ = 0;
    const auto gpuAfter = backend_->memory_stats();
    AUREA_LOG_INFO("GPU OOM: transitorios concluidos soltos (quadro=%llu, texturas=%u, pool=%llu->%llu, GPU=%llu->%llu, reservado=%llu->%llu)",
        static_cast<unsigned long long>(completed), removed,
        static_cast<unsigned long long>(before), static_cast<unsigned long long>(pool_.stats().bytes),
        static_cast<unsigned long long>(gpuBefore.usedBytes), static_cast<unsigned long long>(gpuAfter.usedBytes),
        static_cast<unsigned long long>(gpuBefore.reservedBytes), static_cast<unsigned long long>(gpuAfter.reservedBytes));
    // Actual backend used/reserved statistics remain authoritative. Vulkan
    // can retain an empty allocation block until its own safe collection.
    return OkStatus;
}

bool Renderer::can_stage_small_scene_capture(const FrameSnapshot& root, u32 width, u32 height) noexcept {
    if (!width || !height || width > 320 || height > 320) return false;
    u32 groups = 0;
    auto eligible = [&](auto&& self, const FrameSnapshot& snap, u32 w, u32 h, u32 depth) -> bool {
        for (const auto& scene : snap.scenes) {
            if (!pure_unblurred_scene(scene) || ++groups > 4) return false;
        }
        if (depth >= 8 && !snap.nested.empty()) return false;
        for (const auto& child : snap.nested) if (child) {
            // Match staging's recursive rounding while rejecting a large
            // nested target before converting an unbounded float to an integer.
            const f32 cw = static_cast<f32>(child->compWidth)
                * (static_cast<f32>(w) / static_cast<f32>(std::max(1u, snap.compWidth)));
            const f32 ch = static_cast<f32>(child->compHeight)
                * (static_cast<f32>(h) / static_cast<f32>(std::max(1u, snap.compHeight)));
            if (!std::isfinite(cw) || !std::isfinite(ch) || cw >= 320.5f || ch >= 320.5f) return false;
            const u32 childWidth = std::max(1u, static_cast<u32>(std::lround(cw)));
            const u32 childHeight = std::max(1u, static_cast<u32>(std::lround(ch)));
            if (!self(self, *child, childWidth, childHeight, depth + 1)) return false;
        }
        return true;
    };
    return eligible(eligible, root, width, height, 0) && groups != 0;
}

Status Renderer::render_staged_scene_targets(const FrameSnapshot& root, const RenderSettings& settings,
    u32 width, u32 height, std::span<StagedSceneTarget> targets,
    FrameStats& stats, RenderTimings& timings) noexcept {
    if (targets.empty()) return OkStatus;
    if (!settings.stageUnblurredScenes || !settings.finalQuality)
        return Status{Errc::InvalidState, "etapas 3D exigem qualidade final"};
    const auto cancelled = [&] { return settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire); };
    const u64 started = monotonic_ns();
    constexpr u64 hardCap = 120ull * 1000 * 1000 * 1000;
    const u64 limit = std::clamp<u64>(settings.sceneExposureFenceTimeoutNs, 1, hardCap);
    auto wait = [&](u64 frame) {
        const u64 waitStarted = monotonic_ns();
        Status status;
        do {
            if (cancelled()) { status = Errc::Cancelled; break; }
            const u64 elapsed = monotonic_ns() - started;
            if (elapsed >= limit) { status = Errc::Timeout; break; }
            status = backend_->wait_frame(frame, std::min<u64>(100ull * 1000 * 1000, limit - elapsed));
            if (status.code() != Errc::Timeout) break;
        } while (true);
        timings.sceneExposureWaitMs += static_cast<f32>((monotonic_ns() - waitStarted) * 1e-6);
        return status;
    };
    u32 next = 0;
    bool incompleteStage = false;
    auto stage = [&](auto&& self, const FrameSnapshot& snap, u32 w, u32 h, u32 depth) -> Status {
        // Same recursive integer rounding as compose_layers, including odd
        // precomp dimensions. An index 0 in a child never aliases parent 0.
        if (depth < 8) for (const auto& child : snap.nested) if (child) {
            const f32 kx = static_cast<f32>(w) / static_cast<f32>(std::max(1u, snap.compWidth));
            const f32 ky = static_cast<f32>(h) / static_cast<f32>(std::max(1u, snap.compHeight));
            const u32 cw = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(child->compWidth) * kx)));
            const u32 ch = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(child->compHeight) * ky)));
            if (auto status = self(self, *child, cw, ch, depth + 1); !status.ok()) return status;
        }
        for (u32 scene = 0; scene < snap.scenes.size(); ++scene) {
            const auto& group = snap.scenes[scene];
            if (!pure_unblurred_scene(group)) continue;
            if (cancelled()) return Errc::Cancelled;
            if (next >= targets.size()) return Errc::InvalidState;
            auto& saved = targets[next++]; saved.owner = &snap; saved.scene = scene;
            const u64 previous = backend_->last_submitted_frame();
            FrameBegin frame;
            if (auto status = backend_->begin_offscreen_frame(frame); !status.ok()) return status;
            const u64 recorded = monotonic_ns();
            pool_.begin_frame(*backend_, frame.frameNumber); graph_.reset(); arena_.reset();
            bool executed = false;
            auto finish = [&](Status status) {
                graph_.release(pool_); pool_.end_frame();
                const auto submitted = backend_->end_frame();
                timings.sceneExposureRecordMs += static_cast<f32>((monotonic_ns() - recorded) * 1e-6);
                ++timings.sceneExposureBatches;
                if (executed) saved.pendingFrame = frame.frameNumber;
                if (!submitted.ok()) return submitted;
                if (!status.ok()) return status;
                if (auto completed = wait(frame.frameNumber); !completed.ok()) return completed;
                saved.pendingFrame = 0;
                // All free intermediate reads have completed. Held outputs
                // remain inUse; actual backend residency still gates admission.
                (void)pool_.trim_unreferenced(frame.frameNumber);
                return OkStatus;
            };
            // The previous graph can leave idle targets of different extents.
            // A fence, not logical cache eviction, proves those reads finished.
            if (previous) {
                if (auto completed = wait(previous); !completed.ok()) return finish(completed);
                (void)pool_.trim_unreferenced(previous);
            }
            configure_scene_quality(group);
            scene3d_.finish_environment(group.environment);
            FGTexture output;
            if (!scene3d_.build(graph_, arena_, group, w, h, frame.frameNumber, output)) {
                incompleteStage = true;
                incomplete_ = true;
                return finish(OkStatus);
            }
            TextureDesc desc; desc.width = w; desc.height = h; desc.format = kWorkFormat;
            desc.sampled = desc.renderTarget = desc.transferSrc = true;
            saved.texture = pool_.acquire(desc);
            if (!saved.texture.valid()) return finish(Status{Errc::OutOfDeviceMemory, "resultado 3D da captura"});
            const FGTexture target = graph_.import_texture("3d-captura-concluida", saved.texture, desc);
            EffectBuildContext ctx(graph_, shaders_, arena_, *this, kWorkFormat,
                std::min<u32>(backend_->capabilities().maxTexture2D, 8192));
            const Vec4 identity{1, 1, 0, 0};
            if (ctx.fullscreen_pass("3d-captura-preservar", PassStage::Composite, target, ShaderId::common_copy_frag,
                {PassTexture{output, {}, CommonSampler::NearestClamp}}, &identity, sizeof(identity)) == kInvalidIndex)
                return finish(Status{Errc::PipelineCompileFailed, "copia 3D da captura"});
            graph_.set_output(target, ResourceState::ShaderRead);
            Status status = graph_.compile(pool_);
            if (scene3d_.incomplete()) incomplete_ = true;
            if (status.ok() && cancelled()) status = Errc::Cancelled;
            if (status.ok() && !frame.commands) status = Errc::InvalidState;
            if (status.ok()) {
                graph_.execute(*frame.commands, settings.gpuTimers); executed = true;
                stats.passesExecuted += graph_.stats().passesExecuted;
                stats.passesCulled += graph_.stats().passesCulled;
                const auto& st = scene3d_.stats();
                stats.drawCalls += st.drawCalls + st.shadowDrawCalls + 1;
                heavyStats_.lastSceneDrawCalls += st.drawCalls;
                heavyStats_.lastSceneShadowDrawCalls += st.shadowDrawCalls;
                heavyStats_.lastSceneInstancedDraws += st.instancedDraws;
                heavyStats_.lastSceneVisible += st.visiblePrimitives;
                heavyStats_.lastSceneCulled += st.culledPrimitives;
                heavyStats_.lastSceneTriangles += st.triangles;
                heavyStats_.lastShadowMapSize = std::max(heavyStats_.lastShadowMapSize, st.shadowMapSize);
            }
            if (auto completed = finish(status); !completed.ok()) return completed;
        }
        return OkStatus;
    };
    const Status status = stage(stage, root, width, height, 0);
    return status.ok() && !incompleteStage && next != targets.size()
        ? Status{Errc::InvalidState, "resultados 3D incompletos"} : status;
}
