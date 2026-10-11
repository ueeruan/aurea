// Included in Renderer.cpp: shared Vulkan/Metal/GLES implementation.
void Renderer::configure_scene_quality(const scene3d::SceneFrame& group) noexcept {
    HeavyQuality q = heavyQ_;
    apply_scene3d_quality(q, static_cast<Scene3DQuality>(group.post.quality));
    scene3d_.set_shadow_map_cap(heavyQ_.exportFrame || !lowMemoryDevice_ ? 4096u : 2048u);
    scene3d_.set_quality(q.shadowMapSize, q.shadowFilter, heavyQ_.lodBias, !heavyQ_.exportFrame);
    scene3d_.set_post_quality(q.msaaSamples, q.fxaa, q.bloomStartDiv, q.bloomLevels);
    scene3d_.set_dof_quality(heavyQ_.exportFrame ? 96u
        : static_cast<Scene3DQuality>(group.post.quality) == Scene3DQuality::Low ? 16u : 32u);
    const auto tier = static_cast<Scene3DQuality>(group.post.quality);
    auto preview = scene3d::EnvironmentQuality::preview();
    if (tier == Scene3DQuality::Low) preview = scene3d::EnvironmentQuality{128u, 256u, 2u};
    else if (tier == Scene3DQuality::Ultra) preview = scene3d::EnvironmentQuality::final_quality();
    scene3d_.set_environment_quality(preview, scene3d::EnvironmentQuality::final_quality());
    scene3d_.set_environment_wait(heavyQ_.exportFrame);
}

Status Renderer::render_bounded_scene_exposures(const FrameSnapshot& snap, const RenderSettings& settings,
    u32 width, u32 height, std::span<TextureHandle> textures,
    FrameStats& stats, RenderTimings& timings) noexcept {
    if (snap.sceneExposureBatches.empty()) return OkStatus;
    if (!settings.boundedSceneExposure || !settings.finalQuality || !snap.nested.empty())
        return Status{Errc::InvalidState, "plano de exposicao limitado incompativel"};
    if (textures.size() != snap.scenes.size())
        return Status{Errc::InvalidArgument, "resultados de exposicao 3D incompletos"};
    TextureDesc desc; desc.width = width; desc.height = height; desc.format = kWorkFormat;
    desc.sampled = true; desc.renderTarget = true; desc.transferSrc = true; desc.transferDst = true;
    const auto additive = shaders_.pipeline(PipelineKey::graphics(
        ShaderId::composite_layer_vert, ShaderId::composite_layer_frag, kWorkFormat, true, BlendMode::Add));
    if (!additive) return additive.status();
    const u64 sampler = shaders_.sampler(CommonSampler::LinearClamp).id;
    const f32 compW = static_cast<f32>(snap.compWidth), compH = static_cast<f32>(snap.compHeight);
    const auto cancelled = [&] { return settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire); };

    for (const auto& batch : snap.sceneExposureBatches) {
        if (!batch.samplesPerBatch || batch.scene >= snap.scenes.size())
            return Status{Errc::OutOfMemory, "uma amostra 3D excede o orcamento de exposicao"};
        const auto& group = snap.scenes[batch.scene];
        if (group.blurFrames.empty() || !group.planeLayers.empty() || !group.particleLayers.empty())
            return Status{Errc::InvalidState, "exposicao progressiva exige grupo 3D puro"};
        configure_scene_quality(group);
        scene3d_.finish_environment(group.environment);
        if (scene3d_.incomplete()) return Status{Errc::InvalidState, "ambiente 3D incompleto"};
        struct Accumulators {
            TransientTexturePool& pool;
            TextureHandle display{}, hdr{};
            u64 pendingFrame = 0;
            ~Accumulators() {
                for (const auto texture : {display, hdr}) if (texture.valid()) {
                    if (pendingFrame) pool.retire(texture, pendingFrame); else pool.release(texture);
                }
            }
        } held{pool_};
        const u32 count = static_cast<u32>(group.blurFrames.size());
        bool hdrUsed = false;
        for (u32 first = 0; first < count; first += batch.samplesPerBatch) {
            if (cancelled()) return Errc::Cancelled;
            FrameBegin frame;
            if (auto status = backend_->begin_offscreen_frame(frame); !status.ok()) return status;
            const u64 started = monotonic_ns();
            pool_.begin_frame(*backend_, frame.frameNumber);
            graph_.reset(); arena_.reset();
            bool executed = false;
            u32 integrationDraws = 0;
            auto finish = [&](Status status) {
                graph_.release(pool_); pool_.end_frame();
                const auto submitted = backend_->end_frame();
                timings.sceneExposureRecordMs += static_cast<f32>((monotonic_ns() - started) * 1e-6);
                ++timings.sceneExposureBatches;
                if (executed) held.pendingFrame = frame.frameNumber;
                auto abandon = [&](Status failure) {
                    if (textures[batch.scene].valid()) {
                        pool_.retire(textures[batch.scene], frame.frameNumber); textures[batch.scene] = {};
                    }
                    return failure;
                };
                if (!submitted.ok()) {
                    // Device loss at submission must reach the recovery owner,
                    // even if an earlier recording/allocation also failed.
                    AUREA_LOG_ERROR("exposicao 3D: submissao falhou (%d), gravacao %d",
                        submitted.raw(), status.raw());
                    return abandon(submitted);
                }
                if (!status.ok()) return abandon(status);
                // No upload pool entry may return to the CPU before this fence.
                const u64 waitStarted = monotonic_ns();
                Status waited;
                constexpr u64 hardCap = 120ull * 1000 * 1000 * 1000;
                const u64 limit = std::clamp<u64>(settings.sceneExposureFenceTimeoutNs, 1, hardCap);
                do {
                    if (cancelled()) { waited = Errc::Cancelled; break; }
                    const u64 elapsed = monotonic_ns() - waitStarted;
                    const u64 slice = std::min<u64>(100ull * 1000 * 1000, elapsed < limit ? limit - elapsed : 1);
                    waited = backend_->wait_frame(frame.frameNumber, slice);
                    if (waited.code() != Errc::Timeout || monotonic_ns() - waitStarted >= limit) break;
                } while (true);
                timings.sceneExposureWaitMs += static_cast<f32>((monotonic_ns() - waitStarted) * 1e-6);
                if (!waited.ok()) return abandon(waited);
                held.pendingFrame = 0;
                return OkStatus;
            };
            if (!held.display.valid()) held.display = pool_.acquire(desc);
            if (!held.hdr.valid()) held.hdr = pool_.acquire(desc);
            if (!held.display.valid() || !held.hdr.valid())
                return finish(Status{Errc::OutOfDeviceMemory, "acumulador de exposicao 3D"});
            const FGTexture display = graph_.import_texture("3d-exposicao-display", held.display, desc);
            const FGTexture radiance = graph_.import_texture("3d-exposicao-linear", held.hdr, desc);
            const u32 stop = std::min(count, first + batch.samplesPerBatch);
            auto integrate = [&](FGTexture destination, FGTexture source, LoadOp load, bool hdr) {
                const u32 pass = graph_.add_raster_pass("3d-desfoque", PassStage::Composite,
                    destination, load, Vec4{}, [source, pipeline = *additive, sampler, compW, compH, count, hdr](PassContext& pc) {
                        pc.cmds.bind_pipeline(pipeline);
                        pc.cmds.bind_texture(0, pc.texture(source), SamplerHandle{sampler});
                        LayerPush push; push.clipFromLayer = clip_from_comp(compW, compH);
                        push.region = Vec4{0, 0, compW, compH}; push.uvRect = Vec4{0, 0, 1, 1};
                        push.params = Vec4{1.f / count, 0, 0, hdr ? -1.f : 0.f};
                        pc.cmds.push_constants(&push, sizeof(push)); pc.cmds.draw(6);
                    });
                graph_.read(pass, source); graph_.mark_side_effect(pass);
                if (pass != kInvalidIndex) ++integrationDraws;
                return pass != kInvalidIndex;
            };
            for (u32 sample = first; sample < stop; ++sample) {
                if (cancelled()) return finish(Errc::Cancelled);
                FGTexture color, hdr;
                if (!scene3d_.build(graph_, arena_, group.blurFrames[sample], width, height,
                        frame.frameNumber, color, nullptr, nullptr, 0, nullptr, &hdr))
                    return finish(Status{Errc::InvalidState, "amostra de exposicao 3D incompleta"});
                if (!integrate(display, color, sample == 0 ? LoadOp::Clear : LoadOp::Load, false))
                    return finish(Status{Errc::OutOfMemory, "passe de exposicao 3D"});
                if (hdr.valid()) {
                    if (!integrate(radiance, hdr, hdrUsed ? LoadOp::Load : LoadOp::Clear, true))
                        return finish(Status{Errc::OutOfMemory, "passe HDR de exposicao 3D"});
                    hdrUsed = true;
                }
            }
            FGTexture output = display;
            if (stop == count) {
                FGTexture encoded;
                if (hdrUsed) {
                    encoded = graph_.create_texture("3d-exposicao-resolve", desc);
                    const u32 pass = graph_.add_raster_pass("3d-exposicao-resolve", PassStage::Composite,
                        encoded, LoadOp::Clear, Vec4{}, [radiance, pipeline = *additive, sampler, compW, compH](PassContext& pc) {
                            pc.cmds.bind_pipeline(pipeline);
                            pc.cmds.bind_texture(0, pc.texture(radiance), SamplerHandle{sampler});
                            LayerPush push; push.clipFromLayer = clip_from_comp(compW, compH);
                            push.region = Vec4{0, 0, compW, compH}; push.uvRect = Vec4{0, 0, 1, 1};
                            push.params = Vec4{1, 0, 0, -2};
                            pc.cmds.push_constants(&push, sizeof(push)); pc.cmds.draw(6);
                        });
                    if (pass == kInvalidIndex) return finish(Status{Errc::OutOfMemory, "resolve HDR 3D"});
                    graph_.read(pass, radiance);
                    ++integrationDraws;
                }
                output = scene3d_.finish_exposure(graph_, group, width, height, display, encoded);
                if (!output.valid()) return finish(Status{Errc::InvalidState, "pos-processo de exposicao 3D"});
                const TextureHandle result = pool_.acquire(desc);
                if (!result.valid()) return finish(Status{Errc::OutOfDeviceMemory, "resultado de exposicao 3D"});
                textures[batch.scene] = result;
                const FGTexture saved = graph_.import_texture("3d-exposicao-concluida", result, desc);
                // Post targets need not have TransferSrc usage. Preserve them
                // through the same linear quad path used by the compositor.
                const u32 copy = graph_.add_raster_pass("3d-exposicao-preservar", PassStage::Composite,
                    saved, LoadOp::Clear, Vec4{}, [output, pipeline = *additive, sampler, compW, compH](PassContext& pc) {
                        pc.cmds.bind_pipeline(pipeline);
                        pc.cmds.bind_texture(0, pc.texture(output), SamplerHandle{sampler});
                        LayerPush push; push.clipFromLayer = clip_from_comp(compW, compH);
                        push.region = Vec4{0, 0, compW, compH}; push.uvRect = Vec4{0, 0, 1, 1};
                        push.params = Vec4{1, 0, 0, 0};
                        pc.cmds.push_constants(&push, sizeof(push)); pc.cmds.draw(6);
                    });
                if (copy == kInvalidIndex) return finish(Status{Errc::OutOfMemory, "preservar exposicao 3D"});
                graph_.read(copy, output); output = saved; ++integrationDraws;
            }
            graph_.set_output(output, ResourceState::ShaderRead);
            Status status = graph_.compile(pool_);
            if (status.ok() && cancelled()) status = Errc::Cancelled;
            if (status.ok() && !frame.commands) status = Errc::InvalidState;
            if (status.ok()) {
                graph_.execute(*frame.commands, settings.gpuTimers);
                executed = true;
                stats.passesExecuted += graph_.stats().passesExecuted;
                stats.passesCulled += graph_.stats().passesCulled;
            }
            const auto& st = scene3d_.stats();
            // SceneRenderer accumulates all builds with the same frame number.
            // Transfer/compute passes are reported as passes, never as draws.
            stats.drawCalls += st.drawCalls + st.shadowDrawCalls + integrationDraws;
            heavyStats_.lastSceneDrawCalls += st.drawCalls;
            heavyStats_.lastSceneShadowDrawCalls += st.shadowDrawCalls;
            heavyStats_.lastSceneInstancedDraws += st.instancedDraws;
            heavyStats_.lastSceneVisible += st.visiblePrimitives;
            heavyStats_.lastSceneCulled += st.culledPrimitives;
            heavyStats_.lastSceneTriangles += st.triangles;
            heavyStats_.lastShadowMapSize = std::max(heavyStats_.lastShadowMapSize, st.shadowMapSize);
            if (scene3d_.incomplete() && status.ok()) status = Status{Errc::InvalidState, "exposicao 3D incompleta"};
            if (auto done = finish(status); !done.ok()) return done;
        }
    }
    return OkStatus;
}
