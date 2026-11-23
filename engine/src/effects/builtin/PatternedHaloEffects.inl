class PatternedHalo final : public Effect {
public:
    enum class Model { Aura, Rainbow, Rings, Distance };

    explicit PatternedHalo(Model model) noexcept : model_(model) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo entries[] = {
            {"aurea.light.spectral_aura", "Aura espectral", "Luz", EffectClass::Neighborhood},
            {"aurea.light.rainbow_halo", "Halo arco-íris", "Luz", EffectClass::Neighborhood},
            {"aurea.light.glow_rings", "Anéis de brilho", "Luz", EffectClass::Neighborhood},
            {"aurea.light.displaced_halo", "Halo por distância", "Luz", EffectClass::Neighborhood},
        };
        return entries[static_cast<u32>(model_)];
    }

    void declare_parameters(ParameterRegistry& p) const override {
        const bool rings = model_ == Model::Rings;
        const bool rainbow = model_ == Model::Rainbow;
        constexpr auto pct = kParamAnimatable | kParamPercent;
        p.add_float("brightness", "Brilho", rings ? 1.f : 0.8f, 0, 10);
        p.add_float("width", "Largura do halo", distance() ? .1f : rainbow ? .42f : .4f, 0, 4);
        p.add_float("threshold", "Limite", distance() || rainbow ? 50.f : 0.f, 0, 100, pct, "%");
        p.add_color("tint", "Cor do halo", Vec4{1, 1, 1, 1});
        p.add_float("saturation", "Saturação", 100, -200, 800, pct, "%");
        p.add_float("alpha_gain", "Intensidade no alfa", 100, 0, 500, pct, "%");
        p.add_bool("glow_only", "Só o halo", false);
        p.add_bool("add", "Modo somar", false);
        p.add_color("threshold_color", "Cor adicional do limite", Vec4{0, 0, 0, 1});
        p.add_bool("expand", "Expandir bordas", false);
        if (distance()) {
            p.add_float("width_red", "Largura do vermelho", 1.f, 0, 4);
            p.add_float("width_green", "Largura do verde", 1.2f, 0, 4);
            p.add_float("width_blue", "Largura do azul", 1.4f, 0, 4);
            return;
        }
        if (rings) {
            p.add_float("width_x", "Largura X", 1, 0, 4);
            p.add_float("width_y", "Largura Y", 1, 0, 4);
            p.add_float("width_red", "Largura do vermelho", .5f, 0, 4);
            p.add_float("width_green", "Largura do verde", .75f, 0, 4);
            p.add_float("width_blue", "Largura do azul", 1, 0, 4);
            p.add_float("thickness_red", "Espessura do vermelho", 50, 0, 100, pct, "%");
            p.add_float("thickness_green", "Espessura do verde", 50, 0, 100, pct, "%");
            p.add_float("thickness_blue", "Espessura do azul", 50, 0, 100, pct, "%");
        } else {
            p.add_float("frequency", "Frequência", rainbow ? 12.f : 8.f, 0, 64);
            p.add_float("frequency_red", "Frequência do vermelho", 1, 0, 4);
            p.add_float("frequency_green", "Frequência do verde", rainbow ? .9f : 1.f, 0, 4);
            p.add_float("frequency_blue", "Frequência do azul", rainbow ? .8f : 1.f, 0, 4);
            p.add_float("phase", "Fase", 0, -16, 16);
            p.add_float("phase_speed", "Velocidade da fase", 1, -16, 16);
            p.add_float("phase_red", "Fase do vermelho", rainbow ? 0.f : .2f, -8, 8);
            p.add_float("phase_green", "Fase do verde", rainbow ? 0.f : .1f, -8, 8);
            p.add_float("phase_blue", "Fase do azul", 0, -8, 8);
            if (!rainbow) {
                p.add_float("twist", "Torção", 1, -8, 8);
                p.add_float("outer_brightness", "Brilho externo", 1, 0, 8);
            }
        }
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return !e.b(kOnly) && value(e, kGain, 1.f, 0.f, 10.f) <= 0.f;
    }

    f32 input_margin(const EffectEval& e) const noexcept override {
        if (!e.placement) return 0.f;
        const f32 width = static_cast<f32>(std::max(e.placement->layerWidth, 1u));
        return support(e, width);
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sapphire_halo_frag, work));
        if (model_ == Model::Rainbow || distance()) return;
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32 margin, LayerImage& out) const override {
        const f32 width = input.virtualSourceRegion.w > 0 ? input.virtualSourceRegion.w : input.region.w;
        const f32 reach = support(e, width);
        const Rect output = e.b(kExpand)
            ? spread_region(input.region, reach, reach, e.placement, margin) : input.region;
        if (model_ == Model::Rainbow || distance()) return build_distance_field(ctx, e, input, output, width, out);
        return build_lobes(ctx, e, input, output, width, reach, out);
    }

private:
    enum : u32 { kGain, kWidth, kThreshold, kTint, kSaturation, kAlphaGain, kOnly, kAdd,
                 kThresholdColor, kExpand, kFrequency, kFrequencyR, kFrequencyG, kFrequencyB,
                 kPhase, kSpeed, kPhaseR, kPhaseG, kPhaseB, kTwist, kOuterGain };
    enum : u32 { kWidthX = kFrequency, kWidthY, kWidthR, kWidthG, kWidthB,
                 kThicknessR, kThicknessG, kThicknessB };

    static f32 value(const EffectEval& e, u32 index, f32 fallback, f32 low, f32 high) noexcept {
        const f32 param = e.f(index);
        return std::clamp(std::isfinite(param) ? param : fallback, low, high);
    }

    f32 size(const EffectEval& e, f32 width) const noexcept {
        return width * value(e, kWidth, distance() ? .1f : model_ == Model::Rainbow ? .42f : .4f, 0.f, 4.f);
    }

    bool distance() const noexcept { return model_ == Model::Distance; }

    static Vec3 distance_widths(const EffectEval& e, f32 base) noexcept {
        return Vec3{std::min(3000.f, 2.f * base * value(e, kWidthX, 1.f, 0.f, 4.f)),
                    std::min(3000.f, 2.f * base * value(e, kWidthY, 1.2f, 0.f, 4.f)),
                    std::min(3000.f, 2.f * base * value(e, kWidthR, 1.4f, 0.f, 4.f))};
    }

    f32 support(const EffectEval& e, f32 width) const noexcept {
        const f32 base = size(e, width);
        if (distance()) {
            const Vec3 radii = distance_widths(e, base);
            return std::max({radii.x, radii.y, radii.z});
        }
        if (model_ == Model::Rainbow) return std::min(base * 2.f, 3000.f);
        if (model_ == Model::Aura) return std::min(base * .72f, 3000.f);
        const auto channels = ring_widths(e, base);
        const auto axes = ring_axes(e);
        return std::min(.89f * *std::max_element(channels.begin(), channels.end()) * std::max(axes.x, axes.y),
                        3000.f);
    }

    static std::array<f32, 3> ring_widths(const EffectEval& e, f32 base) noexcept {
        return {base * value(e, kWidthR, .5f, 0.f, 4.f),
                base * value(e, kWidthG, .75f, 0.f, 4.f),
                base * value(e, kWidthB, 1.f, 0.f, 4.f)};
    }

    static Vec2 ring_axes(const EffectEval& e) noexcept {
        return Vec2{value(e, kWidthX, 1.f, 0.f, 4.f), value(e, kWidthY, 1.f, 0.f, 4.f)};
    }

    // The clock follows the project's fractional frame position. Reduce the
    // cycles on the CPU to retain visual phase precision on long timelines.
    void spectral_parameters(const EffectEval& e, f32 scale, EffectUniforms& u) const noexcept {
        const bool rainbow = model_ == Model::Rainbow;
        const f32 frequency = value(e, kFrequency, rainbow ? 12.f : 8.f, 0.f, 64.f) * scale;
        u.p2 = Vec4{frequency * value(e, kFrequencyR, 1.f, 0.f, 4.f),
                    frequency * value(e, kFrequencyG, rainbow ? .9f : 1.f, 0.f, 4.f),
                    frequency * value(e, kFrequencyB, rainbow ? .8f : 1.f, 0.f, 4.f), 0};
        const f64 frame = e.time_frames();
        const f64 seconds = (std::isfinite(frame) ? frame : 0.0) / std::max(1.0, e.framesPerSecond);
        const f64 phase = (rainbow ? 0.0 : .25) - value(e, kPhase, 0.f, -16.f, 16.f) -
                          seconds * value(e, kSpeed, 1.f, -16.f, 16.f);
        const auto phase_channel = [&](u32 slot, f32 fallback) {
            return static_cast<f32>(std::remainder(phase - value(e, slot, fallback, -8.f, 8.f), 1.0));
        };
        u.p3 = Vec4{phase_channel(kPhaseR, rainbow ? 0.f : .2f),
                    phase_channel(kPhaseG, rainbow ? 0.f : .1f),
                    phase_channel(kPhaseB, 0.f), 0};
    }

    EffectUniforms base_parameters(const EffectEval& e) const noexcept {
        EffectUniforms u{};
        u.p0.z = value(e, kThreshold, model_ == Model::Rainbow || distance() ? 50.f : 0.f, 0.f, 200.f) * .01f;
        u.p0.w = value(e, kGain, model_ == Model::Rings ? 1.f : .8f, 0.f, 10.f);
        u.p1 = Vec4{e.b(kOnly) ? 1.f : 0.f, e.b(kAdd) ? 1.f : 0.f,
                    value(e, kSaturation, 100.f, -200.f, 800.f) * .01f,
                    value(e, kAlphaGain, 100.f, 0.f, 500.f) * .01f};
        u.p3 = e.color(kThresholdColor);
        u.color = e.color(kTint);
        return u;
    }

    static f32 sigma_after_prefilter(f32 sigma, f32 texelScale) noexcept {
        // Each 4x4 pyramid box contributes 1.25 texel² on an axis. The Aura
        // phase exposes the added variance, so subtract it from the kernel.
        const f32 desired = sigma * texelScale;
        f32 step = 1.f;
        f32 variance = 0.f;
        for (u32 level = 0; level < 7 && desired > 8.f * step; ++level) {
            const f32 candidate = variance + 1.25f * step * step;
            if (std::sqrt(std::max(desired * desired - candidate, 0.f)) <= 8.f * step) break;
            variance = candidate;
            step *= 2.f;
        }
        return std::sqrt(std::max(desired * desired - variance, 0.f)) / texelScale;
    }

    static Status pass(EffectBuildContext& ctx, const char* label, const LayerImage& out,
                       std::initializer_list<PassTexture> reads, const EffectUniforms& u) {
        return ctx.fullscreen_pass(label, PassStage::Effects, out.texture, ShaderId::effects_sapphire_halo_frag,
                                   reads, &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }

    Status build_distance_field(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                                const Rect& output, f32 width, LayerImage& out) const {
        u32 w = 0, h = 0;
        // Preview density is already imposed by the renderer. A maximum axis
        // of 2K bounds the number of pixels in the distance-field scratch.
        const f32 density = std::min(input.texel_scale_x(), 2048.f / std::max({output.w, output.h, 1.f}));
        ctx.region_size(output, density, w, h);
        LayerImage nearest{ctx.texture("rainbow-seeds", w, h), output, w, h};
        EffectUniforms u = base_parameters(e);
        u.p0.x = 2;
        u.uvMap = EffectBuildContext::uv_map(output, input.region);
        u.p2 = Vec4{static_cast<f32>(w), static_cast<f32>(h), 0, 0};
        if (const Status s = pass(ctx, "rainbow-seeds", nearest,
                                  {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, u); !s.ok()) return s;
        const f32 sx = output.w / static_cast<f32>(std::max(w, 1u));
        const f32 sy = output.h / static_cast<f32>(std::max(h, 1u));
        u.p0.x = 3;
        u.p2 = Vec4{1, sx, sy, 0};
        const auto jump = [&](u32 stride) -> Status {
            LayerImage next{ctx.texture("rainbow-distance", w, h), output, w, h};
            u.p2.x = static_cast<f32>(stride);
            const Status s = pass(ctx, "rainbow-distance", next, {PassTexture{nearest.texture}}, u);
            if (s.ok()) nearest = next;
            return s;
        };
        u32 stride = 1;
        while (stride < std::max(w, h)) stride *= 2;
        for (stride /= 2; stride > 0; stride /= 2)
            if (const Status s = jump(stride); !s.ok()) return s;
        // One final local relaxation removes isolated diagonal jump errors.
        if (const Status s = jump(1); !s.ok()) return s;

        ctx.region_size(output, input.texel_scale_x(), w, h);
        const char* label = distance() ? "luminance-distance-halo" : "rainbow-halo";
        out = LayerImage{ctx.texture(label, w, h), output, w, h};
        u.p0.x = distance() ? 8.f : 4.f;
        u.p0.y = sx; u.p0.z = sy;
        u.texel = Vec4{1, 1, 0, 0};
        if (distance()) {
            const Vec3 radii = distance_widths(e, size(e, width));
            u.p2 = Vec4{radii.x, radii.y, radii.z, 0.f};
        } else {
            spectral_parameters(e, 2.f / std::max(width, 1.f), u);
            u.p2.w = support(e, width);
        }
        return pass(ctx, label, out,
                    {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                     PassTexture{nearest.texture, {}, CommonSampler::NearestClamp}}, u);
    }

    Status build_lobes(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                       const Rect& output, f32 width, f32 reach, LayerImage& out) const {
        const bool rings = model_ == Model::Rings;
        const f32 base = size(e, width);
        const f32 sx = input.texel_scale_x(), sy = input.texel_scale_y();
        // Align the padded extent before extracting threshold energy.
        const f32 left = input.region.x + std::floor((output.x - input.region.x - reach) * sx) / sx;
        const f32 top = input.region.y + std::floor((output.y - input.region.y - reach) * sy) / sy;
        const f32 right = input.region.x + std::ceil((output.right() - input.region.x + reach) * sx) / sx;
        const f32 bottom = input.region.y + std::ceil((output.bottom() - input.region.y + reach) * sy) / sy;
        const Rect work{left, top, right - left, bottom - top};
        u32 w = 0, h = 0;
        ctx.region_size(work, sx, w, h);
        LayerImage field{ctx.texture("pattern-halo-extract", w, h), work, w, h};
        EffectUniforms u = base_parameters(e);
        u.p0.x = rings ? 0.f : 5.f;
        u.uvMap = EffectBuildContext::uv_map(work, input.region);
        const CommonSampler edge = e.b(kExpand) ? CommonSampler::LinearBorder : CommonSampler::LinearMirror;
        if (const Status s = pass(ctx, "pattern-halo-extract", field,
                                  {PassTexture{input.texture, {}, edge}}, u); !s.ok()) return s;
        LayerImage halo;
        BlurRequest blur;
        blur.repeatEdges = true;
        blur.outRegion = work;
        blur.label = "glow-pattern";
        if (rings) {
            if (const Status s = build_rings(ctx, e, field, work, base, blur, halo); !s.ok()) return s;
            u.p0.x = 1;
        } else {
            const f32 sigma = std::min(base * .174f, 750.f);
            blur.sigmaX = sigma_after_prefilter(sigma, field.texel_scale_x());
            blur.sigmaY = sigma_after_prefilter(sigma, field.texel_scale_y());
            // The gradient angle makes the aura sensitive to sampling density.
            // Keep a finer Gaussian working grid than an ordinary diffuse halo.
            blur.maxSigmaTexels = 8.f;
            if (const Status s = build_gaussian(ctx, field, blur, halo); !s.ok()) return s;
            u.p0.x = 6;
            u.p0.y = 1.f / static_cast<f32>(halo.width);
            u.p0.z = 1.f / static_cast<f32>(halo.height);
            spectral_parameters(e, 1.f, u);
            u.p2.w = value(e, kTwist, 1.f, -8.f, 8.f);
            u.p3.w = value(e, kOuterGain, 1.f, 0.f, 8.f);
        }
        ctx.region_size(output, sx, w, h);
        out = LayerImage{ctx.texture("pattern-halo", w, h), output, w, h};
        u.uvMap = EffectBuildContext::uv_map(output, input.region);
        u.texel = EffectBuildContext::uv_map(output, work);
        return pass(ctx, "pattern-halo", out,
                    {PassTexture{input.texture, {}, CommonSampler::LinearBorder}, PassTexture{halo.texture},
                     PassTexture{halo.texture}, PassTexture{halo.texture}}, u);
    }

    static Status build_rings(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& field,
                              const Rect& work, f32 base, BlurRequest& blur, LayerImage& out) {
        const auto widths = ring_widths(e, base);
        const auto axes = ring_axes(e);
        const std::array<f32, 3> thickness{
            .01f * value(e, kThicknessR, 50.f, 0.f, 100.f),
            .01f * value(e, kThicknessG, 50.f, 0.f, 100.f),
            .01f * value(e, kThicknessB, 50.f, 0.f, 100.f),
        };
        std::array<LayerImage, 3> core{};
        for (u32 ch = 0; ch < core.size(); ++ch) {
            const f32 sigma = widths[ch] * .08f * (2.f - 2.f * thickness[ch]);
            blur.sigmaX = std::min(sigma * axes.x, 750.f);
            blur.sigmaY = std::min(sigma * axes.y, 750.f);
            if (const Status s = build_gaussian(ctx, field, blur, core[ch]); !s.ok()) return s;
        }
        u32 w = 0, h = 0;
        const f32 density = field.texel_scale_x();
        const f32 spread = .148f * *std::max_element(widths.begin(), widths.end()) * std::max(axes.x, axes.y);
        // The annulus pass only contains the low-frequency halo.
        ctx.region_size(work, std::min(density, 10.f / std::max(spread, 1.f)), w, h);
        out = LayerImage{ctx.texture("annular-halo", w, h), work, w, h};
        EffectUniforms u{};
        u.p0.x = 7;
        u.uvMap = Vec4{1, 1, 0, 0};
        u.p1 = Vec4{widths[0], widths[1], widths[2], 0};
        u.p2 = Vec4{axes.x / std::max(work.w, 1.f), axes.y / std::max(work.h, 1.f), 0, 0};
        u.p3 = Vec4{thickness[0], thickness[1], thickness[2], 0};
        return pass(ctx, "annular-halo", out,
                    {PassTexture{field.texture}, PassTexture{core[0].texture}, PassTexture{core[1].texture},
                     PassTexture{core[2].texture}}, u);
    }

    Model model_;
};
