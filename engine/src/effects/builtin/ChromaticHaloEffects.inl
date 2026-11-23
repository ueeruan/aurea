class ChromaticHalo final : public Effect {
public:
    enum class Model { Highlights, Shadows, Outline };

    explicit ChromaticHalo(Model model) noexcept : model_(model) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo effects[] = {
            {"aurea.light.chromatic_halo", "Halo cromático", "Luz", EffectClass::Neighborhood},
            {"aurea.light.shadow_halo", "Halo de sombras", "Luz", EffectClass::Neighborhood},
            {"aurea.light.outline_halo", "Halo de contorno", "Luz", EffectClass::Neighborhood},
        };
        return effects[static_cast<u32>(model_)];
    }

    void declare_parameters(ParameterRegistry& p) const override {
        const bool dark = model_ == Model::Shadows;
        const f32 width = dark ? 1.f : model_ == Model::Outline ? 0.224f : 0.4f;
        const auto pct = kParamAnimatable | kParamPercent;
        p.add_float("brightness", dark ? "Escurecimento" : "Brilho", dark ? 0.5f : 2.f, 0, 10);
        p.add_float("width", "Largura do halo", width, 0, 4);
        p.add_float("threshold", "Limite", 50, 0, 100, pct, "%");
        p.add_color("tint", "Cor do halo", Vec4{1, 1, 1, 1});
        p.add_float("width_x", "Largura X", 1, 0, 4);
        p.add_float("width_y", "Largura Y", 1, 0, 4);
        p.add_float("width_red", "Largura do vermelho", 1, 0, 4);
        p.add_float("width_green", "Largura do verde", dark ? 1.f : 1.2f, 0, 4);
        p.add_float("width_blue", "Largura do azul", dark ? 1.f : 1.4f, 0, 4);
        p.add_float("saturation", "Saturação", 100, -200, 800, pct, "%");
        p.add_float("alpha_gain", "Intensidade no alfa", 100, 0, 500, dark ? pct | kParamHidden : pct, "%");
        p.add_bool("glow_only", "Só o halo", false);
        p.add_bool("add", "Modo somar", false, dark ? kParamHidden : kParamAnimatable);
        static const char* const wrap[] = {"Refletir", "Estender borda", "Transparente"};
        p.add_enum("wrap", "Bordas", wrap, 3, 0);
        if (model_ == Model::Outline) p.add_float("edge_strength", "Força do contorno", 1, 0, 10);
        else p.add_color("threshold_color", "Cor adicional do limite", Vec4{0, 0, 0, 1});
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return !e.b(kOnly) && parameter(e, kGain, 0.f, 10.f) <= 0.f;
    }

    f32 input_margin(const EffectEval& e) const noexcept override {
        if (!e.placement) return 0.f;
        const f32 width = static_cast<f32>(e.placement->layerWidth > 0 ?
                                           e.placement->layerWidth : e.placement->compWidth);
        return std::min(3000.f, 4.f * sigma_scale(e, width) * max_channel(e) *
            std::max(parameter(e, kWidthX, 1.f, 4.f), parameter(e, kWidthY, 1.f, 4.f)));
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (ShaderId shader : {ShaderId::effects_sapphire_halo_frag, ShaderId::effects_gaussian_blur_frag,
                                ShaderId::effects_downsample_frag})
            out.push_back(PipelineKey::fullscreen(shader, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin,
                 LayerImage& out) const override {
        const bool dark = model_ == Model::Shadows;
        const f32 imageWidth = in.virtualSourceRegion.w > 0 ? in.virtualSourceRegion.w : in.region.w;
        const f32 scale = sigma_scale(e, imageWidth);
        const f32 sigmaX = scale * parameter(e, kWidthX, 1.f, 4.f);
        const f32 sigmaY = scale * parameter(e, kWidthY, 1.f, 4.f);
        const std::array<f32, 3> channels{
            parameter(e, kWidthRed, 1.f, 4.f),
            parameter(e, kWidthGreen, dark ? 1.f : 1.2f, 4.f),
            parameter(e, kWidthBlue, dark ? 1.f : 1.4f, 4.f),
        };
        const f32 extentX = std::min(3000.f, 4.f * sigmaX * max_channel(e));
        const f32 extentY = std::min(3000.f, 4.f * sigmaY * max_channel(e));
        const u32 border = std::min(e.e(kWrap), 2u);
        Rect output = in.region;
        if (border == 2 && !dark) output = spread_region(in.region, extentX, extentY, e.placement, margin);
        const f32 scaleX = in.texel_scale_x(), scaleY = in.texel_scale_y();
        // Keep the extraction samples aligned to the source pixel grid. A
        // fractional padding offset would soften the edge detector first.
        const f32 left = in.region.x + std::floor((output.x - in.region.x - extentX) * scaleX) / scaleX;
        const f32 top = in.region.y + std::floor((output.y - in.region.y - extentY) * scaleY) / scaleY;
        const f32 right = in.region.x + std::ceil((output.right() - in.region.x + extentX) * scaleX) / scaleX;
        const f32 bottom = in.region.y + std::ceil((output.bottom() - in.region.y + extentY) * scaleY) / scaleY;
        const Rect work{left, top, right - left, bottom - top};
        const CommonSampler sampler = border == 0 ? CommonSampler::LinearMirror :
                                      border == 1 ? CommonSampler::LinearClamp : CommonSampler::LinearBorder;
        // Bound padded allocations while retaining source detail for narrow halos.
        const f32 density = std::min({in.texel_scale_x(), ctx.max_texture_size() / std::max(work.w, 1.f),
                                      ctx.max_texture_size() / std::max(work.h, 1.f)});
        u32 w = 0, h = 0;
        ctx.region_size(work, density, w, h);
        LayerImage field{ctx.texture("halo-extract", w, h), work, w, h};
        EffectUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(work, in.region);
        const f32 gain = parameter(e, kGain, dark ? 0.5f : 2.f, 10.f);
        u.p0 = Vec4{0, static_cast<f32>(model_), parameter(e, kThreshold, 50.f, 200.f) / 100.f,
                    dark ? 2.f * gain : gain};
        u.p1 = Vec4{e.b(kOnly) ? 1.f : 0.f, e.b(kAdd) ? 1.f : 0.f,
                    std::clamp(std::isfinite(e.f(kSaturation)) ? e.f(kSaturation) : 100.f, -200.f, 800.f) / 100.f,
                    parameter(e, kAlphaGain, 100.f, 500.f) / 100.f};
        const bool outline = model_ == Model::Outline;
        u.p2 = Vec4{1.f / std::max(in.region.w, 1.f), 1.f / std::max(in.region.h, 1.f),
                    outline ? parameter(e, kSourceAdjust, 1.f, 10.f) : 1.f, imageWidth};
        u.p3 = outline ? Vec4{} : e.color(kSourceAdjust);
        u.color = e.color(kTint);
        const ShaderId shader = ShaderId::effects_sapphire_halo_frag;
        if (ctx.fullscreen_pass("halo-extract", PassStage::Effects, field.texture, shader,
                                {PassTexture{in.texture, {}, sampler}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;

        std::array<LayerImage, 3> halos{};
        for (u32 i = 0; i < halos.size(); ++i) {
            u32 duplicate = i;
            for (u32 prior = 0; prior < i; ++prior)
                if (channels[prior] == channels[i]) { duplicate = prior; break; }
            if (duplicate != i) { halos[i] = halos[duplicate]; continue; }
            BlurRequest req;
            req.sigmaX = std::min(750.f, sigmaX * channels[i]);
            req.sigmaY = std::min(750.f, sigmaY * channels[i]);
            req.repeatEdges = true;
            req.outRegion = work;
            req.label = "glow-chromatic";
            if (const Status result = build_gaussian(ctx, field, req, halos[i]); !result.ok()) return result;
        }
        ctx.region_size(output, in.texel_scale_x(), w, h);
        out = LayerImage{ctx.texture("halo-composite", w, h), output, w, h};
        u.uvMap = EffectBuildContext::uv_map(output, in.region);
        u.texel = EffectBuildContext::uv_map(output, work);
        u.p0.x = 1;
        if (ctx.fullscreen_pass("halo-composite", PassStage::Effects, out.texture, shader,
                                {PassTexture{in.texture, {}, CommonSampler::LinearBorder},
                                 PassTexture{halos[0].texture}, PassTexture{halos[1].texture},
                                 PassTexture{halos[2].texture}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }

private:
    enum : u32 { kGain, kWidth, kThreshold, kTint, kWidthX, kWidthY, kWidthRed, kWidthGreen,
                 kWidthBlue, kSaturation, kAlphaGain, kOnly, kAdd, kWrap, kSourceAdjust };

    static f32 parameter(const EffectEval& e, u32 index, f32 fallback, f32 maximum) noexcept {
        const f32 value = e.f(index);
        return std::clamp(std::isfinite(value) ? value : fallback, 0.f, maximum);
    }
    f32 sigma_scale(const EffectEval& e, f32 imageWidth) const noexcept {
        const f32 fallback = model_ == Model::Shadows ? 1.f : model_ == Model::Outline ? 0.224f : 0.4f;
        return 0.1725f * imageWidth * parameter(e, kWidth, fallback, 4.f);
    }
    f32 max_channel(const EffectEval& e) const noexcept {
        const bool dark = model_ == Model::Shadows;
        return std::max({parameter(e, kWidthRed, 1.f, 4.f), parameter(e, kWidthGreen, dark ? 1.f : 1.2f, 4.f),
                         parameter(e, kWidthBlue, dark ? 1.f : 1.4f, 4.f)});
    }

    Model model_;
};
