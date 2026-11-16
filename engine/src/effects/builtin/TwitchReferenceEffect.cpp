#include "BuiltinEffects.hpp"
#include "aurea/effects/TwitchMotion.hpp"
#include "aurea/timeline/Layer.hpp"
#include <array>

namespace aurea::builtin {
namespace {
using namespace twitch_motion;
class TwitchReference final : public Effect {
  public:
    const EffectInfo &info() const noexcept override {
        static const EffectInfo info{"aurea.motion.twitch_reference", "Twitch", "Distorcer",
                                     EffectClass::Temporal};
        return info;
    }
    void declare_parameters(ParameterRegistry &p) const override {
        auto amount = [&](const char *id, const char *label, f32 value) {
            p.add_float(id, label, value, 0, 100);
            p.typed_range(0, 100000);
        };
        auto rate = [&](const char *id, const char *label) {
            p.add_float(id, label, 1, 0, 20, kParamAnimatable, "Hz");
            p.typed_range(0, 10000);
        };
        auto seed = [&](const char *id, const char *label) { p.add_int(id, label, 0, 0, 999); };
        auto percent = [&](const char *id, const char *label, f32 value) {
            p.add_float(id, label, value, 0, 100, kParamAnimatable | kParamPercent, "%");
        };
        static const char *borders[]{"Mirror", "Tile", "Ignore", "Expand"};
        static const char *blends[]{"None", "Normal", "Screen", "Add", "Multiply", "Overlay"};
        static const char *behaviors[]{"Brighter", "Darker", "Both"};
        static const char *directions[]{"Both", "Forward Only", "Backward Only"};
        amount("amount", "Amount", 100);
        p.add_float("speed", "Speed", 5, 0, 20);
        p.typed_range(0, 10000);
        p.add_bool("blur_enabled", "Blur", false);
        p.add_bool("color_enabled", "Color", false);
        p.add_bool("light_enabled", "Light", false);
        p.add_bool("scale_enabled", "Scale", false);
        p.add_bool("slide_enabled", "Slide", false);
        p.add_bool("time_enabled", "Time", false);
        percent("ease_in", "Ease In", 1);
        percent("ease_out", "Ease Out", 1);
        p.add_enum("border", "Border", borders, 4, 0);
        p.add_bool("show_twitch_only", "Show Twitch Only", false);
        seed("seed", "Seed");
        percent("randomize_minimum", "Randomize Minimum", 0);
        amount("blur_amount", "Blur Amount", 50);
        rate("blur_rate", "Blur Twitches / sec");
        p.add_color("blur_tint", "Blur Tint", Vec4{1, 1, 1, 1});
        amount("blur_holdout", "Blur Holdout", 0);
        amount("blur_sharpness", "Blur Holdout Sharpness", 1);
        amount("blur_boost", "Blur Boost", 100);
        percent("blur_opacity", "Blur Opacity", 100);
        p.add_enum("blur_blend", "Blur Transfer Mode", blends, 6, 1);
        p.add_float("blur_aspect", "Blur Aspect", 0, -1, 1);
        p.add_bool("lens_blur", "Use Lens Blur", false);
        seed("blur_seed", "Unique Blur Seed");
        amount("color_amount", "Color Amount", 50);
        rate("color_rate", "Color Twitches / sec");
        p.add_color("colorize", "Colorize", Vec4{1, 162.f / 255, 0, 1});
        percent("color_randomize", "Color Randomize", 0);
        seed("color_seed", "Unique Color Seed");
        amount("light_amount", "Light Amount", 50);
        rate("light_rate", "Light Twitches / sec");
        p.add_enum("light_behavior", "Light Behaviour", behaviors, 3, 0);
        seed("light_seed", "Unique Light Seed");
        amount("scale_amount", "Scale Amount", 50);
        rate("scale_rate", "Scale Twitches / sec");
        p.add_point2("scale_origin", "Scale Origin", Vec2{.5f, .5f}, -10, 10,
                     kParamAnimatable | kParamRelative);
        percent("scale_origin_random", "Scale Origin Randomize", 0);
        percent("scale_motion_blur", "Scale Motion Blur", 0);
        seed("scale_seed", "Unique Scale Seed");
        amount("slide_amount", "Slide Amount", 50);
        rate("slide_rate", "Slide Twitches / sec");
        p.add_angle("slide_direction", "Slide Direction", 0);
        p.add_float("slide_spread", "Slide Spread", 1, 0, 1);
        p.add_float("slide_tendency", "Slide Tendency", 0, -1, 1);
        percent("slide_rgb_split", "Slide RGB Split", 0);
        percent("slide_motion_blur", "Slide Motion Blur", 100);
        seed("slide_seed", "Unique Slide Seed");
        amount("time_amount", "Time Amount", 50);
        rate("time_rate", "Time Twitches / sec");
        p.add_enum("time_direction", "Time Direction", directions, 3, 0);
        seed("time_seed", "Unique Time Seed");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[ColorEnabled]=ParamValue::boolean(true);
        values[LightEnabled]=ParamValue::boolean(true);
        values[ColorAmount]=ParamValue::scalar(100);
        values[LightAmount]=ParamValue::scalar(30);
        values[Speed]=ParamValue::scalar(12);
        values[ColorRate]=ParamValue::scalar(3);
        values[LightRate]=ParamValue::scalar(3);
        values[RandomMinimum]=ParamValue::scalar(100);
        return true;
    }
    bool is_identity(const EffectEval &e) const noexcept override {
        if (e.b(ShowOnly))
            return false;
        return e.f(Amount) <= 0 || !(e.b(BlurEnabled) || e.b(ColorEnabled) || e.b(LightEnabled) ||
                                     e.b(ScaleEnabled) || e.b(SlideEnabled) || e.b(TimeEnabled));
    }
    bool needs_full_input() const noexcept override {
        return true;
    }
    void pipelines(std::vector<PipelineKey> &out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_twitch_reference_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
        out.push_back(PipelineKey::graphics(ShaderId::effects_cartoon_sample_vert,
                                           ShaderId::effects_cartoon_sample_frag, work, true,
                                           BlendMode::Normal));
    }
    void resolve_resources(EffectEval &e) const noexcept override {
        e.temporalSamples = std::make_shared<std::vector<EffectTemporalSample>>();
        const f64 offset = source_offset(e);
        if (offset != 0 && e.layer)
            e.temporalSamples->push_back(
                {e.time_frames() + e.layer->start.value - e.layer->offset.value + offset});
    }
    Status build(EffectBuildContext &ctx, const EffectEval &e, const LayerImage &input, f32 margin,
                 LayerImage &out) const override {
        LayerImage source = input;
        if (e.temporalSamples && !e.temporalSamples->empty()) {
            if (e.temporalSamples->front().snapshot == kInvalidIndex) {
                out = input;
                out.texture = ctx.texture("twitch-outside-source", input.width, input.height);
                (void)ctx.graph().add_raster_pass("twitch-outside-source", PassStage::Effects, out.texture,
                                                  LoadOp::Clear, Vec4{}, [](PassContext &) {});
                return OkStatus;
            }
            Mat4 matrix;
            f32 opacity = 1;
            if (!ctx.temporal_checkout(e.temporalSamples->front().snapshot, source, matrix, opacity))
                return Status{Errc::Busy, "Twitch: temporal source pending"};
            matrix = temporal_sample_matrix(e.temporalSamples->front(), matrix,
                                           e.placement ? e.placement->sourceShift : Vec2{});
            const Status status = rasterize_checkout(ctx, source, matrix, opacity,
                                                    std::max(input.texel_scale_x(), e.texelScale));
            if (!status.ok())
                return status;
        }
        const f64 time = e.time_frames() + .5;
        const f32 blur =
            e.b(BlurEnabled) ? static_cast<f32>(amplitude(e, BlurAmount, BlurRate, BlurSeed, 9000, time)) : 0;
        const f32 angle = (e.f(BlurAspect) + 1.f) * .25f * kPi;
        const Vec2 radii{blur * 1.41421356f * std::cos(angle), blur * 1.41421356f * std::sin(angle)};
        LayerImage blurred = source;
        const f32 extent =
            e.e(Border) == 3
                ? std::max({radii.x, radii.y, e.f(SlideAmount) * e.f(Amount) * .0002f * source.region.w})
                : 0.f;
        const Rect region = spread_region(source.region, extent, extent, e.placement, margin);
        if (blur > 1e-4f && !e.b(LensBlur)) {
            BlurRequest request;
            request.sigmaX = radii.x;
            request.sigmaY = radii.y;
            request.outRegion = region;
            request.repeatEdges = e.e(Border) < 2;
            request.label = "twitch-blur";
            const Status status = build_gaussian(ctx, source, request, blurred);
            if (!status.ok())
                return status;
        }
        struct Uniforms {
            Vec4 region, sourceRegion, blurMap, lightColor, colorize, blurTint, blurParams;
            Vec4 slideNow, slidePrevious, scale, radii, flags, holdout, splitNow, splitPrevious;
            Vec4 blueNow, bluePrevious;
        } u{};
        u.region = {region.x, region.y, region.w, region.h};
        u.sourceRegion = {source.region.x, source.region.y, source.region.w, source.region.h};
        u.blurMap = EffectBuildContext::uv_map(source.region, blurred.region);
        const f32 color =
            e.b(ColorEnabled)
                ? static_cast<f32>(amplitude(e, ColorAmount, ColorRate, ColorSeed, 12000, time)) * .01f
                : 0;
        const f32 light =
            e.b(LightEnabled)
                ? static_cast<f32>(amplitude(e, LightAmount, LightRate, LightSeed, 11000, time)) * .03f
                : 0;
        f32 lightFactor = 1.f + light;
        const i32 lightPeriod = period(e, LightRate);
        const f64 lightNoise =
            lightPeriod ? noise(time, lightPeriod, static_cast<u32>(e.f(Seed) + e.f(LightSeed)) + 11000, 0)
                        : 0;
        if (e.e(LightBehavior) == 1 ||
            (e.e(LightBehavior) == 2 && (static_cast<i32>(lightNoise * 100000) & 1)))
            lightFactor = 1.f / (1.f + light);
        u.lightColor = {lightFactor, std::clamp(color, 0.f, 160.f), e.f(ColorRandom) * .01f, 0};
        u.colorize = e.color(Colorize);
        const i32 colorPeriod = period(e, ColorRate);
        if (colorPeriod)
            for (u32 channel = 0; channel < 3; ++channel) {
                const f32 random = static_cast<f32>(
                    noise(time, colorPeriod,
                          static_cast<u32>(e.f(Seed) + e.f(ColorSeed)) + 16000 + 1000 * channel, 0) *
                        2 -
                    1);
                (&u.colorize.x)[channel] =
                    std::clamp((&u.colorize.x)[channel] + random * e.f(ColorRandom) * .01f, 0.f, 1.f);
            }
        u.blurTint = e.color(BlurTint);
        u.blurParams = {std::min(1.f, blur * .1f), e.f(BlurBoost) * .01f, e.f(BlurOpacity) * .01f,
                        static_cast<f32>(e.e(BlurBlend))};
        const f32 width = e.placement ? static_cast<f32>(e.placement->layerWidth) : source.region.w;
        const f32 height = e.placement ? static_cast<f32>(e.placement->layerHeight) : source.region.h;
        auto slide = [&](f64 t, Vec4 &common, Vec4 &channels, Vec4 &blue) {
            if (!e.b(SlideEnabled))
                return;
            const i32 interval = period(e, SlideRate);
            if (!interval)
                return;
            const u32 seed = static_cast<u32>(e.f(Seed) + e.f(SlideSeed));
            const f64 envelope = pulse(t, interval, seed, e.f(EaseIn), e.f(EaseOut));
            const f64 bias = (1.0 + e.f(SlideTendency)) * .5;
            const f64 minimum = e.f(RandomMinimum) * .01;
            auto displacement = [&](u32 directionStream, u32 magnitudeStream, bool bipolarMinimum) {
                f64 magnitude = noise(t, interval, seed + magnitudeStream, bipolarMinimum ? 0 : minimum);
                if (bipolarMinimum) {
                    const f64 centered = magnitude - .5;
                    const f64 distance = minimum + (1.0 - minimum) * std::abs(centered) * 2.0;
                    magnitude = .5 + (centered > 0 ? .5 : -.5) * distance;
                }
                magnitude = (magnitude - bias) * e.f(Amount) * .01 * e.f(SlideAmount) * 2 * envelope * .01;
                const f64 angle =
                    -e.f(SlideDirection) * kDeg2Rad +
                    (noise(t, interval, seed + directionStream, 0) - .5) * e.f(SlideSpread) * kTwoPi;
                return Vec2{static_cast<f32>(-std::cos(angle) * magnitude * width),
                            static_cast<f32>(-std::sin(angle) * magnitude * height)};
            };
            const Vec2 base = displacement(19000, 8000, true);
            const f32 rgb = e.f(SlideRgb) * .01f;
            auto offset = [&](u32 channel) {
                const Vec2 individual = displacement(20000 + channel * 1000, 23000 + channel * 1000, false);
                return base * (1.f - rgb) + individual * rgb;
            };
            const Vec2 red = offset(0), green = offset(1), b = offset(2);
            common = {base.x, base.y, 0, 0};
            channels = {red.x, red.y, green.x, green.y};
            blue = {b.x, b.y, 0, 0};
        };
        slide(time, u.slideNow, u.splitNow, u.blueNow);
        slide(time - 1, u.slidePrevious, u.splitPrevious, u.bluePrevious);
        const f32 scaleNow =
            e.b(ScaleEnabled)
                ? static_cast<f32>(amplitude(e, ScaleAmount, ScaleRate, ScaleSeed, 13000, time)) * .01f
                : 0;
        const f32 scalePrevious =
            e.b(ScaleEnabled)
                ? static_cast<f32>(amplitude(e, ScaleAmount, ScaleRate, ScaleSeed, 13000, time - 1)) * .01f
                : 0;
        Vec2 origin = e.p2(ScaleOrigin);
        origin = {origin.x * width, origin.y * height};
        const i32 scalePeriod = period(e, ScaleRate);
        if (scalePeriod) {
            const u32 seed = static_cast<u32>(e.f(Seed) + e.f(ScaleSeed));
            origin.x += static_cast<f32>(noise(time, scalePeriod, seed + 26000, 0) - .5) *
                        e.f(ScaleOriginRandom) * .01f * width;
            origin.y += static_cast<f32>(noise(time, scalePeriod, seed + 27000, 0) - .5) *
                        e.f(ScaleOriginRandom) * .01f * height;
        }
        u.scale = {std::max(.25f, 1.f / (1.f + scaleNow)), std::max(.25f, 1.f / (1.f + scalePrevious)),
                   origin.x, origin.y};
        u.radii = {radii.x, radii.y, 0, 0};
        const bool active = blur > 0 || color > 0 || light > 0 || scaleNow > 0 || u.slideNow.x != 0 ||
                            u.slideNow.y != 0 || source_offset(e) != 0;
        u.flags = {static_cast<f32>(e.e(Border)), e.b(ShowOnly) && !active ? 1.f : 0.f,
                   e.f(SlideMotionBlur) * .01f, e.f(ScaleMotionBlur) * .01f};
        u.holdout = {e.f(BlurHoldout), e.f(BlurSharpness), e.b(SlideEnabled) ? e.f(SlideRgb) : 0.f, 0};
        if (blur > 1e-4f && e.b(LensBlur)) {
            LayerImage lens = source;
            lens.texture = ctx.texture("twitch-lens", source.width, source.height);
            Uniforms lensParams = u;
            lensParams.region = u.sourceRegion;
            lensParams.radii.w = 1;
            const u32 lensPass = ctx.fullscreen_pass(
                "twitch-lens", PassStage::Effects, lens.texture, ShaderId::effects_twitch_reference_frag,
                {PassTexture{source.texture}, PassTexture{source.texture}}, &lensParams, sizeof(lensParams));
            if (lensPass == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            blurred = lens;
            u.blurMap = Vec4{1, 1, 0, 0};
            u.radii.z = 0;
        }
        u32 w, h;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        out = {ctx.texture("twitch", w, h), region, w, h};
        const u32 pass = ctx.fullscreen_pass("twitch", PassStage::Effects, out.texture,
                                             ShaderId::effects_twitch_reference_frag,
                                             {PassTexture{source.texture, {}, CommonSampler::LinearBorder},
                                              PassTexture{blurred.texture, {}, CommonSampler::LinearBorder}},
                                             &u, sizeof(u));
        return pass == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }

  private:
    static Status rasterize_checkout(EffectBuildContext& ctx, LayerImage& source, const Mat4& matrix,
                                     f32 opacity, f32 density) {
        const Rect r = source.region;
        f32 minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        for (Vec2 p : {Vec2{r.x,r.y}, Vec2{r.x+r.w,r.y}, Vec2{r.x,r.y+r.h}, Vec2{r.x+r.w,r.y+r.h}}) {
            const Vec4 q = matrix * Vec4{p.x,p.y,0,1};
            if (!std::isfinite(q.w) || std::abs(q.w)<1e-6f)
                return Status{Errc::OutOfRange,"Twitch: unbounded temporal source"};
            minX=std::min(minX,q.x/q.w); minY=std::min(minY,q.y/q.w);
            maxX=std::max(maxX,q.x/q.w); maxY=std::max(maxY,q.y/q.w);
        }
        const Rect region{std::floor(minX),std::floor(minY),
                          std::max(1.f,std::ceil(maxX)-std::floor(minX)),
                          std::max(1.f,std::ceil(maxY)-std::floor(minY))};
        if (std::ceil(region.w*density)>ctx.max_texture_size() ||
            std::ceil(region.h*density)>ctx.max_texture_size())
            return Status{Errc::OutOfDeviceMemory,"Twitch: temporal source exceeds GPU texture extent"};
        auto pipeline=ctx.shaders().pipeline(PipelineKey::graphics(
            ShaderId::effects_cartoon_sample_vert,ShaderId::effects_cartoon_sample_frag,
            ctx.work_format(),true,BlendMode::Normal));
        if (!pipeline.ok()) return pipeline.status();
        u32 width,height;
        ctx.region_size(region,density,width,height);
        const FGTexture target=ctx.texture("twitch-temporal-source",width,height);
        const Mat4 projection=Mat4::scale(Vec3{2.f/region.w,2.f/region.h,1}) *
            Mat4::translation(Vec3{-region.x-region.w*.5f,-region.y-region.h*.5f,0});
        struct Push { Mat4 matrix; Vec4 region,tint,params; };
        const Push push{projection*matrix,Vec4{r.x,r.y,r.w,r.h},Vec4{0,0,0,-1},Vec4{opacity,0,0,0}};
        const FGTexture texture=source.texture;
        const PipelineHandle handle=*pipeline;
        const u64 sampler=ctx.shaders().sampler(source.width==1 && source.height==1
            ? CommonSampler::LinearClamp : CommonSampler::LinearBorder).id;
        const u32 pass=ctx.graph().add_raster_pass("twitch-temporal-source",PassStage::Effects,target,
            LoadOp::Clear,Vec4{},[handle,texture,sampler,push](PassContext& pc) {
                pc.cmds.bind_pipeline(handle);
                pc.cmds.bind_texture(0,pc.texture(texture),SamplerHandle{sampler});
                pc.cmds.push_constants(&push,sizeof(push));
                pc.cmds.draw(6);
            });
        ctx.graph().read(pass,texture);
        source={target,region,width,height};
        return OkStatus;
    }
};
} // namespace
void register_twitch_reference_effect(EffectRegistry &registry) {
    (void)registry.add(std::make_unique<TwitchReference>());
}
} // namespace aurea::builtin
