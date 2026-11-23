class MultiscaleGlow final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.light.multiscale_glow", "Brilho multiescala", "Luz", EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("brightness", "Intensidade da luz", 1.8f, 0, 20);
        p.add_float("threshold", "Limiar", .4f, 0, 2);
        p.add_float("width", "Largura do halo", .371f, 0, 2);
        p.add_float("falloff", "Decaimento", .35f, -2, 2);
        p.add_float("width_x", "Largura X", 1, 0, 4);
        p.add_float("width_y", "Largura Y", 1, 0, 4);
        p.add_color("color", "Cor do brilho", {1, 1, 1, 1});
        p.add_float("after_width", "Largura do halo secundário", .808f, 0, 2);
        p.add_float("after_gain", "Intensidade do halo secundário", .55f, 0, 6);
        p.add_float("after_stretch_x", "Alongamento secundário X", .3f, 0, 4);
        p.add_float("after_stretch_y", "Alongamento secundário Y", .1f, 0, 4);
        p.add_color("after_color", "Cor do halo secundário", {1, 1, 1, 1});
        p.add_float("affect_alpha", "Propagação no alfa", 1, 0, 8);
        p.add_bool("only_glow", "Só o brilho", false);
        p.add_bool("add", "Modo somar", false);
        p.add_bool("expand", "Expandir bordas", true);
        p.add_section("secondary", "Halo secundário", {AfterWidth, AfterGain, AfterX, AfterY, AfterColor});
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return !e.b(Only) && value(e, Gain, 1.8f, 0, 20) <= 0;
    }

    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.placement ? support(geometry(e, static_cast<f32>(e.placement->layerWidth))) : 0.f;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sapphire_halo_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 naturalWidth = input.virtualSourceRegion.w > 0 ? input.virtualSourceRegion.w : input.region.w;
        const Geometry size = geometry(e, naturalWidth);
        const f32 reach = support(size);
        const Rect region = e.b(Expand) ? spread_region(input.region, reach, reach, e.placement, margin) : input.region;
        const f32 density = input.texel_scale_x();
        const f32 workDensity = density * (std::min(size.core.x, size.core.y) * density >= 4 ? .5f : 1.f);
        u32 w, h;
        ctx.region_size(region, workDensity, w, h);
        LayerImage field{ctx.texture("multiscale-emission", w, h), region, w, h};
        EffectUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.p0.z = value(e, Threshold, .4f, 0, 2);
        if (ctx.fullscreen_pass("multiscale-emission", PassStage::Effects, field.texture, ShaderId::effects_sapphire_halo_frag,
                {PassTexture{input.texture, {}, e.b(Expand) ? CommonSampler::LinearBorder : CommonSampler::LinearMirror}},
                &u, sizeof(u)) == kInvalidIndex) return Errc::PipelineCompileFailed;

        LayerImage core, broad, secondary;
        const auto blur = [&](const LayerImage& source, Vec2 sigma, const char* label, LayerImage& target) {
            BlurRequest request;
            request.sigmaX = sigma.x;
            request.sigmaY = sigma.y;
            request.outRegion = region;
            request.repeatEdges = !e.b(Expand);
            request.label = label;
            return build_gaussian(ctx, source, request, target);
        };
        if (const auto status = blur(field, size.core, "multiscale-core", core); !status.ok()) return status;
        if (const auto status = blur(field, size.broad, "multiscale-broad", broad); !status.ok()) return status;
        const f32 after = value(e, AfterGain, .55f, 0, 6);
        secondary = broad;
        if (after > 0) {
            if (const auto status = blur(broad, size.secondary, "multiscale-secondary", secondary); !status.ok()) return status;
        }

        const f32 broadWeight = std::clamp(.24f + value(e, Falloff, .35f, -2, 2) * .25f, 0.f, 1.f);
        u.p0 = {9, 0, 0, value(e, Gain, 1.8f, 0, 20)};
        u.p1 = {e.b(Only) ? 1.f : 0.f, e.b(Add) ? 1.f : 0.f, 1, value(e, Alpha, 1, 0, 8)};
        u.p2 = {1.f - broadWeight, broadWeight, after, 0};
        u.p3 = e.color(AfterColor);
        u.color = e.color(Tint);
        u.texel = {1, 1, 0, 0};
        ctx.region_size(region, density, w, h);
        out = {ctx.texture("multiscale-glow", w, h), region, w, h};
        return ctx.fullscreen_pass("multiscale-glow", PassStage::Effects, out.texture, ShaderId::effects_sapphire_halo_frag,
            {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
             PassTexture{core.texture, {}, CommonSampler::LinearBorder},
             PassTexture{broad.texture, {}, CommonSampler::LinearBorder},
             PassTexture{secondary.texture, {}, CommonSampler::LinearBorder}},
            &u, sizeof(u)) == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }

private:
    enum : u32 { Gain, Threshold, Width, Falloff, WidthX, WidthY, Tint, AfterWidth, AfterGain,
                 AfterX, AfterY, AfterColor, Alpha, Only, Add, Expand };
    struct Geometry { Vec2 core, broad, secondary; };

    static f32 value(const EffectEval& e, u32 index, f32 fallback, f32 low, f32 high) noexcept {
        const f32 v = e.f(index);
        return std::clamp(std::isfinite(v) ? v : fallback, low, high);
    }

    static Geometry geometry(const EffectEval& e, f32 layerWidth) noexcept {
        const f32 radius = layerWidth * value(e, Width, .371f, 0, 2);
        const f32 outer = layerWidth * value(e, AfterWidth, .808f, 0, 2);
        const f32 x = value(e, WidthX, 1, 0, 4);
        const f32 y = value(e, WidthY, 1, 0, 4);
        const f32 after = value(e, AfterGain, .55f, 0, 6) > 0 ? outer : 0.f;
        return {{std::min(800.f, radius * x * .075f), std::min(800.f, radius * y * .075f)},
                {std::min(800.f, radius * x * .18f), std::min(800.f, radius * y * .18f)},
                {std::min(800.f, after * value(e, AfterX, .3f, 0, 4) * .22f),
                 std::min(800.f, after * value(e, AfterY, .1f, 0, 4) * .22f)}};
    }

    static f32 support(const Geometry& g) noexcept {
        return std::min(3000.f, 3.f * std::max(std::hypot(g.broad.x, g.secondary.x),
                                            std::hypot(g.broad.y, g.secondary.y)));
    }
};
