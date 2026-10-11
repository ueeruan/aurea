// Renderer recovery lifecycle is separate from the GPU pixel-equivalence
// fixtures. A real-size ellipse fits; its unchanged blur cannot be admitted
// with it under this fixed 12 MiB pool limit. Stop submitted recovery stages
// deliberately and require fence ownership, no partial presentation or retry.
AUREA_TEST(FrameGraphSegments, OfflineRecoveryStopsOnCancellationTimeoutAndDeviceLossWithoutPresenting) {
    enum class Stop { CancelInitial, TimeoutInitial, CancelAfterFirstPass, DeviceLostAfterFirstPass };
    for (const auto stop : {Stop::CancelInitial, Stop::TimeoutInitial,
                           Stop::CancelAfterFirstPass, Stop::DeviceLostAfterFirstPass}) {
        RenderFixture f;
        f.backend.mapBuffers = true;
        f.backend.surfaceAttached = true;
        f.backend.surfaceRotation = SurfaceRotation::Rotate90;
        const auto layerId = f.solid("unchanged recovery source", Vec4{.3f, .6f, .9f, .65f});
        auto* layer = f.comp->layer(layerId);
        layer->shape.bounds = Rect{0, 0, 1024, 1024}; layer->shape.shapeType = 1;
        auto blur = make_effect(f.effects, effect_keys::kGaussianBlur, 0);
        blur.params[0].constant.v[0] = 3;
        layer->effects.push_back(std::move(blur));
        f.renderer.set_transient_allocation_limit(12ull << 20);
        f.renderer.set_transient_cache_budget(256ull << 20);
        TextureDesc desc; desc.width = 320; desc.height = 180; desc.format = SurfaceFormat::RGBA16F;
        desc.sampled = desc.renderTarget = true;
        const auto texture = f.backend.create_texture(desc);
        AUREA_CHECK(texture.ok()); if (!texture) continue;
        OffscreenTarget target{*texture, desc.width, desc.height};
        RenderSettings settings; settings.finalQuality = true; settings.dither = false;
        settings.allowGraphSegments = true;
        std::atomic<bool> cancelled{false};
        settings.cancelFlag = &cancelled;
        settings.sceneExposureFenceTimeoutNs = stop == Stop::TimeoutInitial ? 1 : 5'000'000'000;
        FrameSnapshot snapshot;
        f.renderer.prepare(*f.comp, f.project, FrameIndex{0}, nullptr, nullptr, nullptr,
            settings, 1, 0, DecodeMode::Still, 1, snapshot);
        bool complete = false;
        u32 waits = 0, destroyedAtPending = 0;
        u32 stoppedSubmission = 0;
        u64 pendingFrame = 0;
        f.backend.beforeWaitFrame = [&](u64 frame, u64) {
            ++waits;
            if (complete) return OkStatus;
            if (stop != Stop::CancelInitial && (frame == 1 || !f.renderer.pool_stats().bytes)) return OkStatus;
            pendingFrame = frame; destroyedAtPending = f.backend.texturesDestroyed;
            stoppedSubmission = f.backend.framesSubmitted;
            cancelled = true;
            return Status{Errc::Timeout};
        };
        f.backend.beforeEndFrame = [&] {
            if (stop == Stop::DeviceLostAfterFirstPass && f.backend.framesSubmitted > 1
                && f.renderer.pool_stats().bytes) {
                stoppedSubmission = f.backend.framesSubmitted;
                f.backend.deviceLost = true;
                return Status{Errc::DeviceLost};
            }
            return OkStatus;
        };
        FrameStats stats; RenderTimings timings;
        const auto result = f.renderer.render(snapshot, settings, &target, stats, timings);
        const Errc expected = stop == Stop::TimeoutInitial ? Errc::Timeout
            : stop == Stop::DeviceLostAfterFirstPass ? Errc::DeviceLost : Errc::Cancelled;
        std::printf("\n    graph recovery stop=%u result=%u submissions=%u waits=%u pool=%llu\n",
            static_cast<u32>(stop), result.raw(), f.backend.framesSubmitted, waits,
            static_cast<unsigned long long>(f.renderer.pool_stats().bytes));
        AUREA_CHECK_EQ(result.code(), expected);
        AUREA_CHECK(timings.gpuProfileFragmented); AUREA_CHECK(!timings.gpuMeasured);
        AUREA_CHECK_EQ(timings.gpuTotalMs, 0.f);
        AUREA_CHECK(!f.backend.frameOpen);
        AUREA_CHECK_EQ(f.backend.acquires, 0u); AUREA_CHECK_EQ(f.backend.presents, 0u);
        const u32 expectedSubmissions = stop == Stop::CancelInitial || stop == Stop::TimeoutInitial ? 1u : stoppedSubmission;
        if (stop == Stop::CancelAfterFirstPass || stop == Stop::DeviceLostAfterFirstPass)
            AUREA_CHECK(stoppedSubmission > 1);
        AUREA_CHECK_EQ(f.backend.framesSubmitted, expectedSubmissions);
        AUREA_CHECK_EQ(f.backend.offscreenFrames, expectedSubmissions);
        AUREA_CHECK_EQ(f.renderer.pool_stats().inUse, 0u);
        AUREA_CHECK(f.renderer.pool_stats().bytes > 0);
        AUREA_CHECK(f.backend.textureAlive[texture->id - 1]);
        AUREA_CHECK_EQ(snapshot.compWidth, 1920u); AUREA_CHECK_EQ(snapshot.compHeight, 1080u);
        AUREA_CHECK_EQ(layer->shape.bounds.w, 1024.f);
        AUREA_CHECK_EQ(layer->effects[0].params[0].constant.v[0], 3.f);
        if (pendingFrame) {
            AUREA_CHECK_EQ(f.backend.texturesDestroyed, destroyedAtPending);
            const u64 bytes = f.renderer.pool_stats().bytes;
            // An incomplete fence remains a hard gate even after cancellation.
            f.backend.beforeWaitFrame = [&](u64 frame, u64) {
                AUREA_CHECK_EQ(frame, pendingFrame);
                return complete ? OkStatus : Status{Errc::Timeout};
            };
            AUREA_CHECK_EQ(f.renderer.reclaim_failed_transients(0).code(), Errc::Timeout);
            AUREA_CHECK_EQ(f.renderer.pool_stats().bytes, bytes);
            AUREA_CHECK_EQ(f.backend.texturesDestroyed, destroyedAtPending);
        }
        complete = true; cancelled = false;
        f.backend.beforeEndFrame = {};
        f.backend.beforeWaitFrame = {};
        if (stop != Stop::DeviceLostAfterFirstPass) {
            AUREA_CHECK(f.renderer.reclaim_failed_transients(1'000'000).ok());
            AUREA_CHECK_EQ(f.renderer.pool_stats().bytes, 0ull);
            AUREA_CHECK(f.backend.textureAlive[texture->id - 1]);
        }
        f.backend.destroy_texture(*texture);
    }
}
