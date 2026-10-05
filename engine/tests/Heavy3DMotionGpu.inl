// Opt-in diagnostic for the Android 96-layer stall. These synthetic primitives
// isolate graph/descriptor/timer pressure; NVIDIA success does not validate Adreno.
#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(Heavy3DMotionGpu, NinetySixLayersCompareEightSeparateOneGroupAndOneObject) {
    if (!std::getenv("AUREA_HEAVY_96")) { std::printf("(set AUREA_HEAVY_96=1 for the heavy GPU diagnostic) "); return; }
    AUREA_REQUIRE_GPU();
    CaptureValidationGuard validation;
    const auto directory = std::filesystem::absolute("../build/reference/heavy-stress-20261005/host-mb96");
    std::filesystem::create_directories(directory);
    std::ofstream report(directory / "measurements.jsonl");
    std::array<std::vector<u16>, 2> separatedPixels;
    u64 frameNumber = 1;
    for (u32 mode = 0; mode < 3; ++mode) {
        const char* label = mode == 0 ? "eight-separated" : mode == 1 ? "eight-contiguous" : "one-object";
        Engine e;
        EngineConfig config; config.backend = new vk::Backend(); config.backendConfig.enableValidation = true;
        config.backendConfig.framesInFlight = 2; config.workerCount = 2; config.disableAutosave = true;
        AUREA_CHECK(e.initialize(config).ok()); AUREA_CHECK(e.new_project(1920, 1080, 30., label).ok());
        std::vector<LayerId> objects;
        for (u32 i = 0; i < 96; ++i) {
            const bool object = i % 12 == 1 && (mode != 2 || i == 1);
            const auto added = object ? e.add_shape3d(1, "stress sphere") : e.add_shape(i % 5);
            AUREA_CHECK(added.ok()); if (!added.ok()) { e.shutdown(); return; }
            auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
            const auto id = LayerId::unpack(*added);
            auto* layer = comp->layer(id);
            layer->end = FrameIndex{90}; layer->motionBlur = true;
            layer->transform.scale = Vec3{.3f, .3f, .3f};
            const f32 x = 80.f + i % 12 * 155.f, y = 90.f + i / 12 * 125.f;
            layer->transform.position = Vec3{x, y, 0};
            if (object) objects.push_back(id);
            else layer->shape.fillColor = Vec4{i % 3 == 0 ? 1.f : .2f, i % 3 == 1 ? 1.f : .2f, i % 3 == 2 ? 1.f : .2f, 1};
            for (i64 time : {0, 30, 60, 89}) {
                layer->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{time}, x + (time == 30 || time == 60 ? 100.f : 0.f));
                layer->tracks.get_or_create(TrackProperty::RotationZ).set(FrameIndex{time}, time * 3.f);
            }
            // Keep exactly the Android diagnostic's 176 effects. The seven
            // replaced objects in the one-object control do not gain effects.
            if (i % 12 != 1) for (const char* key : {effect_keys::kGaussianBlur, effect_keys::kGlow}) {
                EffectInstance fx; fx.id = layer->alloc_effect_id(); fx.type = e.effects().find_key(key);
                initialize_instance(fx, *e.effects().params(fx.type));
                fx.params[0].constant = ParamValue::scalar(std::strcmp(key, effect_keys::kGaussianBlur) == 0 ? 3.f : 25.f);
                layer->effects.push_back(std::move(fx));
            }
        }
        auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        comp->set_duration(FrameIndex{90});
        comp->set_background(Color{0, 0, 0, 1});
        auto& mb = comp->motion_blur(); mb.enabled = true; mb.samples = mb.previewSamples = 16;
        mb.shutterAngle = 360; mb.shutterPhase = -90; mb.adaptiveLimit = 128;
        if (mode == 1) for (u32 i = 0; i < objects.size(); ++i) AUREA_CHECK(comp->reorder_layer(objects[i], i));
        for (u32 sizeIndex = 0; sizeIndex < 2; ++sizeIndex) {
            const u32 width = sizeIndex ? 960 : 320, height = width * 9 / 16;
            TextureDesc desc; desc.width = width; desc.height = height; desc.format = SurfaceFormat::RGBA16F;
            desc.renderTarget = true; desc.transferSrc = true;
            const auto target = e.gpu()->create_texture(desc); AUREA_CHECK(target.ok()); if (!target.ok()) { e.shutdown(); return; }
            RenderSettings settings; settings.previewDenominator = 1920 / width;
            settings.gpuTimers = true; settings.dither = false; settings.heavyScale = 1;
            for (i64 time : {0, 15, 45}) {
                FrameSnapshot snapshot;
                FrameStats stats;
                RenderTimings timings;
                bool incomplete = true;
                Status status{Errc::InvalidState};
                u32 attempts = 0, sampleTotal = 0, sampleMin = 0, sampleMax = 0;
                f64 elapsedMs = 0;
                while (incomplete && attempts < 4) {
                    const auto start = std::chrono::steady_clock::now();
                    e.renderer().prepare(*comp, *e.project(), FrameIndex{time}, nullptr, nullptr, nullptr,
                        settings, ++frameNumber, 0, DecodeMode::Still, 1.f, snapshot);
                    sampleTotal = sampleMax = 0; sampleMin = 0xffffffffu;
                    for (const auto& scene : snapshot.scenes) {
                        const u32 n = std::max(1u, static_cast<u32>(scene.blurFrames.size()));
                        sampleTotal += n; sampleMin = std::min(sampleMin, n); sampleMax = std::max(sampleMax, n);
                    }
                    OffscreenTarget output{*target, width, height};
                    status = e.renderer().render(snapshot, settings, &output, stats, timings);
                    incomplete = e.renderer().take_incomplete();
                    e.gpu()->wait_idle();
                    elapsedMs = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
                    ++attempts;
                    if (!status.ok()) break;
                }
                AUREA_CHECK(status.ok()); AUREA_CHECK(!incomplete);
                AUREA_CHECK_EQ(snapshot.scenes.size(), mode == 0 ? 8u : 1u);
                AUREA_CHECK(sampleMax >= 16); // the diagnostic really exercises temporal 3D
                const auto graph = e.renderer().graph_stats();
                const auto memory = e.gpu()->memory_stats();
                const auto scene = e.renderer().scene_stats();
                std::array<GpuTiming, 600> measured{}; f32 gpuMs = 0;
                const u32 timerCount = e.gpu()->read_gpu_timings(measured.data(), static_cast<u32>(measured.size()), &gpuMs);
                AUREA_CHECK(timerCount > 0 && timerCount <= 512);
                f32 passSumMs = 0; for (u32 i = 0; i < timerCount; ++i) passSumMs += measured[i].ms;
                std::printf("    MB96 %s %ux%u frame=%lld groups=%zu samples=%u (%u..%u) passes=%u draws=%u sceneDraws=%u timers=%u GPU=%.2fms timedSum=%.2fms wall=%.2fms allocs=%u used=%.2fMiB reserved=%.2fMiB transient=%.2fMiB physical=%u attempts=%u\n",
                    label, width, height, static_cast<long long>(time), snapshot.scenes.size(), sampleTotal, sampleMin, sampleMax,
                    graph.passesExecuted, stats.drawCalls, scene.drawCalls, timerCount, gpuMs, passSumMs, elapsedMs,
                    memory.allocationCount, memory.usedBytes / 1048576., memory.reservedBytes / 1048576., graph.transientBytes / 1048576., graph.physicalTextures, attempts);
                std::fflush(stdout);
                report << "{\"mode\":\"" << label << "\",\"width\":" << width << ",\"height\":" << height << ",\"frame\":" << time
                    << ",\"groups\":" << snapshot.scenes.size() << ",\"samples\":" << sampleTotal << ",\"sampleMin\":" << sampleMin << ",\"sampleMax\":" << sampleMax
                    << ",\"passes\":" << graph.passesExecuted << ",\"draws\":" << stats.drawCalls << ",\"sceneDraws\":" << scene.drawCalls
                    << ",\"timers\":" << timerCount << ",\"gpuMs\":" << gpuMs << ",\"timedPassSumMs\":" << passSumMs << ",\"wallMs\":" << elapsedMs
                    << ",\"allocations\":" << memory.allocationCount << ",\"gpuUsed\":" << memory.usedBytes << ",\"gpuReserved\":" << memory.reservedBytes
                    << ",\"transientBytes\":" << graph.transientBytes << ",\"physicalTextures\":" << graph.physicalTextures << "}\n";
                report.flush();
                std::vector<u16> pixels(usize(width) * height * 4);
                AUREA_CHECK(e.gpu()->read_texture(*target, pixels.data(), width * 8).ok());
                FloatImage image; image.width = width; image.height = height; image.px.resize(pixels.size());
                u32 lit = 0;
                for (usize i = 0; i < pixels.size(); ++i) {
                    image.px[i] = half_to_float(pixels[i]);
                    if (i % 4 != 3 && image.px[i] > .01f) ++lit;
                }
                AUREA_CHECK(all_finite(image)); AUREA_CHECK(lit > 100);
                if (time == 45) {
                    AUREA_CHECK(write_png((directory / (std::string(label) + "-" + std::to_string(width) + ".png")).generic_string(), image.encoded()));
                    if (mode == 0) separatedPixels[sizeIndex] = pixels;
                    if (mode == 1 && separatedPixels[sizeIndex].size() == pixels.size()) {
                        f32 maximum = 0; f64 total = 0; u32 changed = 0;
                        for (usize i = 0; i < pixels.size(); ++i) {
                            const f32 delta = std::abs(half_to_float(pixels[i]) - half_to_float(separatedPixels[sizeIndex][i]));
                            maximum = std::max(maximum, delta); total += delta; changed += delta > .004f;
                        }
                        // Grouping can change shadow coverage/occlusion. Record
                        // actual pixels rather than assuming order-independent lighting.
                        std::printf("    MB96 contiguous/separate %u: max=%.6f mean=%.6f changed=%u/%zu\n", width, maximum, total / pixels.size(), changed, pixels.size());
                    }
                }
            }
            e.gpu()->destroy_texture(*target);
        }
        e.shutdown();
    }
}
#endif
