#include "BuiltinEffects.hpp"
#include "aurea/effects/CartoonMotion.hpp"
#include "aurea/timeline/LayerWorld.hpp"
#include <limits>

namespace aurea::builtin {
namespace {
class CartoonMotion final : public Effect {
  public:
    const EffectInfo &info() const noexcept override {
        static const EffectInfo info{"aurea.motion.cartoon_moblur", "Cartoon Moblur", "Tempo",
                                     EffectClass::Temporal};
        return info;
    }
    void declare_parameters(ParameterRegistry &p) const override {
        static const char *sources[]{"Single Layer", "All Sources", "Fixed Samples"};
        static const char *fills[]{"Solid", "Gradient", "None"};
        static const char *easing[]{"In", "Out", "In and Out", "None"};
        p.add_enum("motion_source", "Motion Source", sources, 3, 1);
        p.add_layer_ref("motion_layer", "Motion Layer");
        p.add_float("shutter_angle", "Shutter Angle", 360, 0, 3600, kParamAnimatable, "deg");
        p.typed_range(0, 100000);
        p.add_int("samples", "Samples", 50, 0, 10000);
        p.add_float("motion_threshold", "Motion Threshold", 1, 0, 20, kParamAnimatable, "px");
        p.add_float("time_threshold", "Time Threshold", 20, 1, 1024, kParamAnimatable, "steps");
        p.add_float("samples_scale", "Samples Scale", 1, 0, 5);
        p.add_float("samples_rotation", "Samples Rotation", 1, 0, 5);
        p.add_enum("fill_mode", "Fill Mode", fills, 3, 2);
        p.add_color("solid_color", "Color", Vec4{128.f / 255, 128.f / 255, 128.f / 255, 1});
        p.add_color("start_color", "Start Color", Vec4{1, 1, 1, 1});
        p.add_color("end_color", "End Color", Vec4{0, 0, 0, 1});
        p.add_float("start_opacity", "Start Opacity", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("end_opacity", "End Opacity", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_enum("easing", "Easing", easing, 4, 3);
        p.add_float("opacity_master", "Opacity Master", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_bool("composite_original", "Composite Original", true);
        p.add_float("left", "Left", 0, -1000, 1000);
        p.add_float("top", "Top", 0, -1000, 1000);
        p.add_float("right", "Right", 0, -1000, 1000);
        p.add_float("bottom", "Bottom", 0, -1000, 1000);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        using namespace cartoon_motion;
        values[MotionSource]=ParamValue::scalar(2);
        values[FixedSamples]=ParamValue::scalar(4);
        values[Fill]=ParamValue::scalar(1);
        values[StartColor]=ParamValue::color(1,.15f,.08f,1);
        values[EndColor]=ParamValue::color(.05f,.3f,1,1);
        values[Original]=ParamValue::boolean(false);
        return true;
    }
    bool needs_full_input() const noexcept override {
        return true;
    }
    void resolve_resources(EffectEval &e) const noexcept override {
        e.auxInfo.w=e.layer ? 0.f : 1.f;
        e.temporalSamples = std::make_shared<std::vector<EffectTemporalSample>>(cartoon_motion::samples(e));
    }
    void pipelines(std::vector<PipelineKey> &out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_cartoon_sample_vert,
                                            ShaderId::effects_cartoon_sample_frag, work, true,
                                            BlendMode::Normal));
    }
    Status build(EffectBuildContext &ctx, const EffectEval &e, const LayerImage &input, f32,
                 LayerImage &out) const override {
        using namespace cartoon_motion;
        struct Image {
            LayerImage image;
            Mat4 matrix;
            Vec4 tint;
            f32 opacity;
        };
        std::vector<Image> images;
        if (e.temporalSamples) {
            const u32 count = static_cast<u32>(e.temporalSamples->size());
            images.reserve(count + 1);
            for (u32 i = 0; i < count; ++i) {
                Image sample;
                if ((*e.temporalSamples)[i].snapshot==kInvalidIndex) {
                    // The catalogue's still-image demonstration has a constant source at every time.
                    if (e.auxInfo.w<.5f) continue;
                    sample.image=input;sample.matrix=Mat4::identity();sample.opacity=1;
                } else if (!ctx.temporal_checkout((*e.temporalSamples)[i].snapshot, sample.image, sample.matrix, sample.opacity)) {
                    return Status{Errc::Busy,"Cartoon Moblur: temporal source pending"};
                }
                if ((*e.temporalSamples)[i].snapshot!=kInvalidIndex)
                    sample.matrix=temporal_sample_matrix((*e.temporalSamples)[i],sample.matrix,
                        e.placement ? e.placement->sourceShift : Vec2{});
                const f32 progress = ease(static_cast<f32>(i) / count, e.e(Easing));
                sample.tint = e.e(Fill) == 0
                                  ? e.color(SolidColor)
                                  : e.color(EndColor) * (1.f - progress) + e.color(StartColor) * progress;
                if (e.e(Fill) == 2)
                    sample.tint = Vec4{0, 0, 0, -1};
                sample.opacity *= (e.f(EndOpacity) + (e.f(StartOpacity) - e.f(EndOpacity)) * progress) * .01f;
                images.push_back(sample);
            }
        }
        f32 x0 = input.region.x, y0 = input.region.y, x1 = x0 + input.region.w, y1 = y0 + input.region.h;
        for (const auto &sample : images) {
            const Rect r = sample.image.region;
            for (Vec2 p :
                 {Vec2{r.x, r.y}, Vec2{r.x + r.w, r.y}, Vec2{r.x, r.y + r.h}, Vec2{r.x + r.w, r.y + r.h}}) {
                const Vec4 q = sample.matrix * Vec4{p.x, p.y, 0, 1};
                if (!std::isfinite(q.w) || std::abs(q.w) < 1e-6f)
                    return Status{Errc::OutOfRange, "Cartoon Moblur: unbounded projection"};
                x0 = std::min(x0, q.x / q.w);
                y0 = std::min(y0, q.y / q.w);
                x1 = std::max(x1, q.x / q.w);
                y1 = std::max(y1, q.y / q.w);
            }
        }
        // Utilities extend the source checkout bounds, retaining negative offsets.
        const Rect region{std::floor(x0 - e.f(Left)), std::floor(y0 - e.f(Top)),
                          std::ceil(x1 + e.f(Right)) - std::floor(x0 - e.f(Left)),
                          std::ceil(y1 + e.f(Bottom)) - std::floor(y0 - e.f(Top))};
        if (!(region.w > 0 && region.h > 0))
            return Status{Errc::OutOfRange, "Cartoon Moblur: empty bounds"};
        const f32 density = std::max(input.texel_scale_x(),e.texelScale);
        if (std::ceil(region.w * density) > ctx.max_texture_size() ||
            std::ceil(region.h * density) > ctx.max_texture_size())
            return Status{Errc::OutOfDeviceMemory, "Cartoon Moblur: output exceeds GPU texture extent"};
        u32 width, height;
        ctx.region_size(region, density, width, height);
        const FGTexture trail = ctx.texture("cartoon-trail", width, height);
        auto pipeline = ctx.shaders().pipeline(PipelineKey::graphics(
            ShaderId::effects_cartoon_sample_vert, ShaderId::effects_cartoon_sample_frag, ctx.work_format(),
            true, BlendMode::Normal));
        if (!pipeline.ok())
            return pipeline.status();
        const Mat4 projection =
            Mat4::scale(Vec3{2.f / region.w, 2.f / region.h, 1}) *
            Mat4::translation(Vec3{-region.x - region.w * .5f, -region.y - region.h * .5f, 0});
        auto draw = [&](FGTexture target, const Image &image, LoadOp load) {
            struct Push {
                Mat4 matrix;
                Vec4 region;
                Vec4 tint;
                Vec4 params;
            };
            struct Capture {
                PipelineHandle pipeline;
                FGTexture texture;
                u64 sampler;
                Push push;
            } cap{
                *pipeline,
                image.image.texture,
                ctx.shaders().sampler(image.image.width==1 && image.image.height==1 ? CommonSampler::LinearClamp : CommonSampler::LinearBorder).id,
                {projection * image.matrix,
                 Vec4{image.image.region.x, image.image.region.y, image.image.region.w, image.image.region.h},
                 image.tint, Vec4{image.opacity, 0, 0, 0}}};
            const u32 pass = ctx.graph().add_raster_pass(
                "cartoon-sample", PassStage::Effects, target, load, Vec4{}, [cap](PassContext &pc) {
                    pc.cmds.bind_pipeline(cap.pipeline);
                    pc.cmds.bind_texture(0, pc.texture(cap.texture), SamplerHandle{cap.sampler});
                    pc.cmds.push_constants(&cap.push, sizeof(cap.push));
                    pc.cmds.draw(6);
                });
            ctx.graph().read(pass, image.image.texture);
        };
        bool first = true;
        for (const auto &image : images) {
            draw(trail, image, first ? LoadOp::Clear : LoadOp::Load);
            first = false;
        }
        // Master opacity applies to the accumulated trail, before the present frame.
        const FGTexture result = ctx.texture("cartoon-result", width, height);
        LayerImage accumulated = input;
        accumulated.texture = trail;
        accumulated.region = region;
        accumulated.width = width;
        accumulated.height = height;
        if (first) {
            const u32 pass = ctx.graph().add_raster_pass("cartoon-empty", PassStage::Effects, trail,
                                                         LoadOp::Clear, Vec4{}, [](PassContext &) {});
            (void)pass;
        }
        draw(result, Image{accumulated, Mat4::identity(), Vec4{0, 0, 0, -1}, e.f(MasterOpacity) * .01f},
             LoadOp::Clear);
        if (e.b(Original))
            draw(result, Image{input, Mat4::identity(), Vec4{0, 0, 0, -1}, 1}, LoadOp::Load);
        out = input;
        out.texture = result;
        out.region = region;
        out.width = width;
        out.height = height;
        return OkStatus;
    }
};
} // namespace
void register_cartoon_motion_effect(EffectRegistry &r) {
    (void)r.add(std::make_unique<CartoonMotion>());
}
} // namespace aurea::builtin
