// Included where Scene and ExportRig are available. The optional evidence
// directory captures synthetic HDR pixels and real export NV12 for comparing
// the original compositor binary with a rebuilt binary, without a runtime flag.
namespace {
void normal_composite_stack(Composition& comp, const EffectRegistry& effects, u32 variant) {
    comp.set_background(Color{.12f, .2f, .35f, .4f});
    comp.set_transparent_background(variant == 2);
    comp.motion_blur().enabled = true;
    comp.motion_blur().samples = comp.motion_blur().adaptiveLimit = 8;
    comp.motion_blur().previewSamples = 8;
    constexpr f32 alphas[] = {.02f, .15f, .25f, .37f, .5f, .68f, .82f, .97f};
    for (u32 i = 0; i < 8; ++i) {
        const auto id = comp.add_layer(LayerKind::Shape, "ordered alpha/HDR source");
        auto* layer = comp.layer(id);
        const f32 w = static_cast<f32>(comp.width()), h = static_cast<f32>(comp.height());
        layer->shape.bounds = Rect{0, 0, w * .7f, h * .65f};
        const f32 hdr = variant == 1 ? (i % 2 ? 4.f : 16.f) : 1.f;
        layer->shape.fillColor = Vec4{hdr * (i % 3 == 0 ? .9f : .1f),
                                     hdr * (i % 3 == 1 ? .7f : .15f),
                                     hdr * (i % 3 == 2 ? .8f : .2f), alphas[i]};
        layer->transform.anchor = Vec3{w * .35f, h * .325f, 0};
        layer->transform.position = Vec3{w * (.3f + .05f * i), h * (.35f + .03f * i), 0};
        layer->transform.rotation.z = -15.f + 4.f * i;
        layer->transform.opacity = .3f + .08f * i;
        layer->motionBlur = i % 2 == 0;
        auto& track = layer->tracks.get_or_create(TrackProperty::PositionX);
        (void)track.set(FrameIndex{0}, layer->transform.position.x);
        (void)track.set(FrameIndex{60}, layer->transform.position.x + 60.f);
        auto add = [&](const char* key) -> EffectInstance& {
            EffectInstance effect; effect.id = layer->alloc_effect_id(); effect.type = effect_type_id(key);
            initialize_instance(effect, *effects.params(effect.type));
            layer->effects.push_back(std::move(effect));
            return layer->effects.back();
        };
        if (variant != 0) {
            auto& tile = add(effect_keys::kMotionTile);
            tile.params[motion_tile::kOutputWidth].constant.v[0] = 150;
            tile.params[motion_tile::kOutputHeight].constant.v[0] = 150;
            tile.params[motion_tile::kMirror].constant.v[0] = 1;
            tile.params[motion_tile::kScale].constant.v[0] = 70;
            auto& blur = add(effect_keys::kGaussianBlur);
            blur.params[0].constant.v[0] = 3;
            auto& glow = add(effect_keys::kGlow);
            glow.params[0].constant.v[0] = 20;
        }
    }
    if (variant == 2) {
        const auto order = comp.order();
        auto* subject = comp.layer(order.at(3));
        subject->matteSource = order.at(7);
        subject->matteMode = MatteMode::Alpha;
    }
}

void normal_composite_evidence(const char* name, const void* pixels, usize bytes) {
    const char* directory = std::getenv("AUREA_NORMAL_COMPOSITE_EVIDENCE_DIR");
    if (!directory || !*directory) return;
    const auto path = std::filesystem::path(directory) / name;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    AUREA_CHECK(file.good());
    if (!file.good()) return;
    file.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(bytes));
    file.close();
    AUREA_CHECK(file.good());
}
}

AUREA_TEST(Gpu, NormalCompositePreservesHdrAlphaAndMotionAgainstTheFullStack) {
    AUREA_REQUIRE_GPU();
    for (const u32 variant : {0u, 1u, 2u}) for (const u32 denominator : {1u, 2u}) {
        Scene scene(192, 108);
        normal_composite_stack(*scene.comp, gpu().effects, variant);
        for (const i64 frame : {0ll, 1ll}) {
            const FloatImage streamed = scene.render(FrameIndex{frame}, denominator, true);
            const auto adjustment = scene.comp->add_layer(LayerKind::Adjustment, "identity full-stack reference");
            const FloatImage reference = scene.render(FrameIndex{frame}, denominator, true);
            AUREA_CHECK(scene.comp->remove_layer(adjustment));
            AUREA_CHECK(!streamed.px.empty());
            AUREA_CHECK_EQ(streamed.px.size(), reference.px.size());
            if (streamed.px.size() != reference.px.size()) continue;
            f32 maximum = 0, peak = 0;
            bool finite = true;
            f64 sumSquares = 0;
            std::vector<f32> errors; errors.reserve(streamed.px.size());
            for (usize i = 0; i < streamed.px.size(); ++i) {
                finite = finite && std::isfinite(streamed.px[i]) && std::isfinite(reference.px[i]);
                const f32 error = std::fabs(streamed.px[i] - reference.px[i]);
                maximum = std::max(maximum, error); peak = std::max(peak, streamed.px[i]);
                sumSquares += static_cast<f64>(error) * error;
                errors.push_back(error);
            }
            std::sort(errors.begin(), errors.end());
            const f32 p99 = errors.empty() ? 0 : errors[errors.size() * 99 / 100];
            const f64 rms = errors.empty() ? 0 : std::sqrt(sumSquares / errors.size());
            std::printf("\n    Normal variant=%u den=%u frame=%lld peak=%.6f max=%.9f rms=%.9f p99=%.9f",
                        variant, denominator, static_cast<long long>(frame), peak, maximum, rms, p99);
            // Splitting a Normal render pass must not change the existing
            // RGBA16F blend result, including HDR values above one.
            AUREA_CHECK_EQ(maximum, 0.f);
            AUREA_CHECK(finite);
            if (variant == 1) AUREA_CHECK(peak > 1.f);
            char name[128];
            std::snprintf(name, sizeof(name), "normal-v%u-d%u-f%lld.f32le", variant, denominator,
                          static_cast<long long>(frame));
            normal_composite_evidence(name, streamed.px.data(), streamed.px.size() * sizeof(f32));
        }
    }
}

AUREA_TEST(Gpu, NormalCompositeExportKeepsRealNv12FramesAndTimestamps) {
    AUREA_REQUIRE_GPU();
    for (const u32 variant : {0u, 1u, 2u}) {
        SyntheticConfig config; config.width = 192; config.height = 108;
        ExportRig rig(config, 0);
        auto* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        while (comp->order().size()) AUREA_CHECK(comp->remove_layer(comp->order().at(0)));
        normal_composite_stack(*comp, rig.e.effects(), variant);
        set_duration(rig.e, 2);
        ExportSettings settings; settings.height = 108; settings.dither = false;
        AUREA_CHECK(rig.e.start_export(settings, "unused-normal-composite.mp4").ok());
        AUREA_CHECK(wait_export(rig.e));
        AUREA_CHECK_EQ(rig.e.export_progress().result, Errc::Ok);
        AUREA_CHECK(rig.cap.finished && !rig.cap.aborted);
        AUREA_CHECK_EQ(rig.cap.video.width, 192u); AUREA_CHECK_EQ(rig.cap.video.height, 108u);
        AUREA_CHECK_EQ(rig.cap.y.size(), usize{2}); AUREA_CHECK_EQ(rig.cap.uv.size(), usize{2});
        AUREA_CHECK_EQ(rig.cap.pts.size(), usize{2});
        for (usize frame = 0; frame < rig.cap.y.size() && frame < rig.cap.uv.size(); ++frame) {
            AUREA_CHECK_EQ(rig.cap.pts[frame], static_cast<i64>(std::llround(frame * 1e6 / 30.)));
            AUREA_CHECK_EQ(rig.cap.y[frame].size(), usize{192 * 108});
            AUREA_CHECK_EQ(rig.cap.uv[frame].size(), usize{192 * 54});
            char name[128];
            std::snprintf(name, sizeof(name), "normal-v%u-f%u.y8", variant, static_cast<u32>(frame));
            normal_composite_evidence(name, rig.cap.y[frame].data(), rig.cap.y[frame].size());
            std::snprintf(name, sizeof(name), "normal-v%u-f%u.uv8", variant, static_cast<u32>(frame));
            normal_composite_evidence(name, rig.cap.uv[frame].data(), rig.cap.uv[frame].size());
        }
    }
}
