#include "aurea/effects/FastBlurReference.hpp"
#include "BuiltinEffects.hpp"
#include <limits>

namespace aurea::fast_blur_reference {
namespace {
struct GpuSegment {
    Vec4 range, polynomial;
};
struct Uniforms {
    // The first seven vec4 values match reference_blur.glsl. The compute
    // variant uses this prefix only and excludes the raster kernel intervals.
    Vec4 geometry{}, sizes{}, settings{}, control{}, uvMap{}, weights{}, edge{};
    GpuSegment segments[kMaxSegments]{};
};
struct ComputeUniforms {
    Vec4 geometry, sizes, settings, control, uvMap, weights, edge;
};
static_assert(sizeof(ComputeUniforms) == 112);
static_assert(sizeof(Uniforms) == 112 + 32 * kMaxSegments);
static_assert(sizeof(Uniforms) <= binding::kMaxUniformBytes);
enum class RasterOperation : u32 { Copy, Gaussian, Moments, ApplyMoments };
enum class ComputeOperation : u32 { Rolling, ZeroFirstMoments, SecondMoment };
struct Grid {
    FGTexture texture;
    u32 width = 0, height = 0;
    i32 x = 0, y = 0;
};

FGTexture texture(EffectBuildContext &ctx, u32 width, u32 height, bool storage) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = SurfaceFormat::RGBA32F;
    desc.sampled = true;
    desc.renderTarget = true;
    desc.storage = storage;
    return ctx.graph().create_texture("reference-blur", desc);
}
Uniforms uniforms(const Grid &source, const Grid &target, u32 axis) {
    Uniforms value;
    value.geometry = {static_cast<f32>(source.x), static_cast<f32>(source.y), static_cast<f32>(target.x),
                      static_cast<f32>(target.y)};
    value.sizes = {static_cast<f32>(source.width), static_cast<f32>(source.height),
                   static_cast<f32>(target.width), static_cast<f32>(target.height)};
    value.settings.x = static_cast<f32>(axis);
    return value;
}
Status raster(EffectBuildContext &ctx, const Uniforms &value, FGTexture target, FGTexture source,
              FGTexture m0 = {}, FGTexture m1 = {}, FGTexture m2 = {}) {
    if (!m0.valid())
        m0 = source;
    if (!m1.valid())
        m1 = source;
    if (!m2.valid())
        m2 = source;
    const u32 pass = ctx.fullscreen_pass("reference-blur", PassStage::Effects, target,
                                         ShaderId::effects_reference_blur_frag,
                                         {{source, {}, CommonSampler::NearestClamp},
                                          {m0, {}, CommonSampler::NearestClamp},
                                          {m1, {}, CommonSampler::NearestClamp},
                                          {m2, {}, CommonSampler::NearestClamp}},
                                         &value, sizeof(value));
    return pass == kInvalidIndex
               ? Status{Errc::PipelineCompileFailed, "reference blur raster pipeline pending"}
               : OkStatus;
}
void compute(EffectBuildContext &ctx, PipelineHandle pipeline, const Uniforms &value, FGTexture source,
             FGTexture firstMoment, FGTexture original, FGTexture target, FGTexture auxiliary, u32 lines) {
    const ComputeUniforms params{value.geometry, value.sizes,   value.settings, value.control,
                                 value.uvMap,    value.weights, value.edge};
    const SamplerHandle sampler = ctx.shaders().sampler(CommonSampler::NearestClamp);
    const u32 pass =
        ctx.graph().add_compute_pass("reference-blur", PassStage::Effects,
                                     [pipeline, params, source, firstMoment, original, target, auxiliary,
                                      lines, sampler](PassContext &pc) {
                                         pc.cmds.bind_pipeline(pipeline);
                                         pc.cmds.bind_texture(0, pc.texture(source), sampler);
                                         pc.cmds.bind_texture(1, pc.texture(firstMoment), sampler);
                                         pc.cmds.bind_texture(2, pc.texture(original), sampler);
                                         pc.cmds.bind_storage_image(0, pc.texture(target));
                                         pc.cmds.bind_storage_image(1, pc.texture(auxiliary));
                                         pc.cmds.set_uniforms(&params, sizeof(params));
                                         pc.cmds.dispatch((lines + 63) / 64, 1, 1);
                                     });
    ctx.graph().read(pass, source);
    ctx.graph().read(pass, firstMoment);
    ctx.graph().read(pass, original);
    ctx.graph().write_storage(pass, target);
    if (!(auxiliary == target))
        ctx.graph().write_storage(pass, auxiliary);
}
Grid axis_grid(const Grid &source, u32 axis, i32 origin, u32 length) {
    Grid target = source;
    if (axis == 0) {
        target.x = origin;
        target.width = length;
    } else {
        target.y = origin;
        target.height = length;
    }
    return target;
}
Status tree_blur(EffectBuildContext &ctx, const Grid &source, Grid &target, u32 axis, f64 radius, bool repeat,
                 Result<PipelineHandle> pipeline) {
    u32 power = 1;
    const u32 length = axis == 0 ? source.width : source.height;
    while (power < length)
        power *= 2;
    if (power > ctx.max_texture_size())
        return Status{Errc::OutOfRange, "reference blur moment tree exceeds GPU texture extent"};
    Grid tree = axis_grid(source, axis, 0, power);
    FGTexture moments[3];
    for (auto &image : moments)
        image = texture(ctx, tree.width, tree.height, pipeline.ok());
    Uniforms value = uniforms(source, tree, axis);
    value.control.x = static_cast<f32>(power);
    if (pipeline.ok()) {
        value.settings.w = static_cast<f32>(ComputeOperation::ZeroFirstMoments);
        compute(ctx, *pipeline, value, source.texture, source.texture, source.texture, moments[0], moments[1],
                axis == 0 ? source.height : source.width);
        value.settings.w = static_cast<f32>(ComputeOperation::SecondMoment);
        compute(ctx, *pipeline, value, moments[0], moments[1], source.texture, moments[2], moments[2],
                axis == 0 ? source.height : source.width);
    } else {
        // Exact raster fallback for devices/drivers without this compute
        // pipeline. Trees retain all source samples and local moments.
        for (auto image : moments)
            (void)ctx.graph().add_raster_pass("reference-blur-tree-clear", PassStage::Effects, image,
                                              LoadOp::Clear, Vec4{}, [](PassContext &) {});
        for (u32 level = power / 2; level; level /= 2) {
            FGTexture next[3];
            for (u32 moment = 0; moment < 3; ++moment) {
                next[moment] = texture(ctx, tree.width, tree.height, false);
                value.settings.w = static_cast<f32>(RasterOperation::Moments);
                value.control.z = static_cast<f32>(level);
                value.control.w = static_cast<f32>(moment);
                const Status status =
                    raster(ctx, value, next[moment], source.texture, moments[0], moments[1], moments[2]);
                if (!status.ok())
                    return status;
            }
            std::copy(std::begin(next), std::end(next), std::begin(moments));
        }
    }
    value = uniforms(source, target, axis);
    value.settings.z = repeat ? 1.f : 0.f;
    value.settings.w = static_cast<f32>(RasterOperation::ApplyMoments);
    value.control.x = static_cast<f32>(power);
    const auto segments = box_segments(radius);
    if (segments.size() > kMaxSegments)
        return Status{Errc::InvalidState, "reference blur kernel interval count exceeds its exact layout"};
    value.control.y = static_cast<f32>(segments.size());
    for (u32 index = 0; index < segments.size(); ++index) {
        const Segment &segment = segments[index];
        value.segments[index] = {Vec4{static_cast<f32>(segment.low), static_cast<f32>(segment.high),
                                      static_cast<f32>(segment.anchor), static_cast<f32>(segment.direction)},
                                 Vec4{static_cast<f32>(segment.a), static_cast<f32>(segment.b),
                                      static_cast<f32>(segment.c), static_cast<f32>(segment.scale)}};
    }
    target.texture = texture(ctx, target.width, target.height, false);
    return raster(ctx, value, target.texture, source.texture, moments[0], moments[1], moments[2]);
}
Status axis_blur(EffectBuildContext &ctx, const Grid &source, Grid &target, u32 axis, f64 radius, bool repeat,
                 bool forceBox, bool clampEachPass = false) {
    if (!(radius > 0)) {
        target = source;
        return OkStatus;
    }
    if (!forceBox && radius / 2.71 <= 1) {
        Uniforms value = uniforms(source, target, axis);
        value.settings.z = repeat ? 1.f : 0.f;
        value.settings.w = static_cast<f32>(RasterOperation::Gaussian);
        const GaussianKernel kernel = gaussian_kernel(radius);
        value.weights = {kernel.weights[0], kernel.weights[1], kernel.weights[2], kernel.weights[3]};
        value.control.z = static_cast<f32>(kernel.radius);
        target.texture = texture(ctx, target.width, target.height, false);
        return raster(ctx, value, target.texture, source.texture);
    }
    const f64 boxRadius = radius / 2.71;
    if (boxRadius > std::numeric_limits<i32>::max() / 16.)
        return Status{Errc::OutOfRange, "reference blur texel radius exceeds exact integer addressing"};
    const u32 support = static_cast<u32>(std::ceil(boxRadius));
    const u32 targetLength = axis == 0 ? target.width : target.height;
    const u32 sourceLength = axis == 0 ? source.width : source.height;
    const u64 expanded = static_cast<u64>(targetLength) + 4ull * support;
    const auto pipeline =
        ctx.shaders().pipeline(PipelineKey::compute_shader(ShaderId::effects_reference_blur_comp));
    if (!pipeline.ok() && pipeline.code() != Errc::NotSupported &&
        pipeline.code() != Errc::ShaderCompileFailed && pipeline.code() != Errc::PipelineCompileFailed)
        return pipeline.status();
    // The common case uses six rolling passes across both axes. Large support
    // switches to local normalized moment trees instead of giant padded images.
    const bool rolling = pipeline.ok() && expanded <= ctx.max_texture_size() &&
                         expanded <= 4ull * std::max(sourceLength, targetLength);
    if (!rolling)
        return tree_blur(ctx, source, target, axis, boxRadius, repeat, pipeline);
    Grid current = source;
    const i32 finalOrigin = axis == 0 ? target.x : target.y;
    // Per-pass clamping keeps every intermediate on the source extent.
    const bool perPass = clampEachPass && repeat;
    for (u32 pass = 0; pass < 3; ++pass) {
        const u32 remaining = perPass ? 0 : 2 - pass;
        Grid next = perPass ? axis_grid(source, axis, axis == 0 ? source.x : source.y, sourceLength)
                            : axis_grid(source, axis, finalOrigin - static_cast<i32>(remaining * support),
                                        targetLength + 2 * remaining * support);
        if (perPass && pass == 2)
            next = axis_grid(source, axis, finalOrigin, targetLength);
        next.texture = texture(ctx, next.width, next.height, true);
        Uniforms value = uniforms(current, next, axis);
        value.settings.y = static_cast<f32>(boxRadius);
        value.settings.z = (pass == 0 || perPass) && repeat ? 1.f : 0.f;
        compute(ctx, *pipeline, value, current.texture, current.texture, current.texture, next.texture,
                next.texture, axis == 0 ? source.height : source.width);
        current = next;
    }
    target = current;
    return OkStatus;
}
} // namespace

void pipelines(std::vector<PipelineKey> &out, SurfaceFormat work) {
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_reference_blur_frag, work));
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_reference_blur_frag, SurfaceFormat::RGBA32F));
    out.push_back(PipelineKey::compute_shader(ShaderId::effects_reference_blur_comp));
}
Status build(EffectBuildContext &ctx, const LayerImage &input, const Request &request, LayerImage &output) {
    if (!input.valid() || !std::isfinite(request.radiusX) || !std::isfinite(request.radiusY) ||
        request.radiusX < 0 || request.radiusY < 0 || !(request.density > 0) ||
        !std::isfinite(request.density))
        return Status{Errc::InvalidArgument,
                      "reference blur requires finite radii and a positive raster density"};
    const Rect region = request.outRegion.w > 0 && request.outRegion.h > 0 ? request.outRegion : input.region;
    const f64 density = request.density;
    auto grid = [&](Rect bounds, Grid &result) -> Status {
        const f64 x = std::floor(bounds.x * density), y = std::floor(bounds.y * density);
        const f64 endX = std::ceil((static_cast<f64>(bounds.x) + bounds.w) * density);
        const f64 endY = std::ceil((static_cast<f64>(bounds.y) + bounds.h) * density);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(endX) || !std::isfinite(endY) ||
            x < std::numeric_limits<i32>::min() / 2. || y < std::numeric_limits<i32>::min() / 2. ||
            endX > std::numeric_limits<i32>::max() / 2. || endY > std::numeric_limits<i32>::max() / 2.)
            return Status{Errc::OutOfRange, "reference blur region exceeds exact integer addressing"};
        if (!(endX > x && endY > y) || endX - x > ctx.max_texture_size() || endY - y > ctx.max_texture_size())
            return Status{Errc::OutOfDeviceMemory,
                          "reference blur source or output exceeds GPU texture extent"};
        result.width = static_cast<u32>(endX - x);
        result.height = static_cast<u32>(endY - y);
        result.x = static_cast<i32>(x);
        result.y = static_cast<i32>(y);
        return OkStatus;
    };
    Grid original, destination;
    if (const Status status = grid(input.region, original); !status.ok())
        return status;
    if (const Status status = grid(region, destination); !status.ok())
        return status;
    original.texture = texture(ctx, original.width, original.height, false);
    Uniforms prepare = uniforms(original, original, 0);
    const Rect originalRegion{static_cast<f32>(original.x / density), static_cast<f32>(original.y / density),
                              static_cast<f32>(original.width / density),
                              static_cast<f32>(original.height / density)};
    prepare.uvMap = EffectBuildContext::uv_map(originalRegion, input.region);
    prepare.edge = {static_cast<f32>(input.region.w * density), static_cast<f32>(input.region.h * density), 0,
                    0};
    prepare.settings.z = request.repeatEdges ? 1.f : 0.f;
    if (const Status status = raster(ctx, prepare, original.texture, input.texture); !status.ok())
        return status;
    Grid horizontal = axis_grid(original, 0, destination.x, destination.width);
    if (const Status status = axis_blur(ctx, original, horizontal, 0, request.radiusX * density,
                                        request.repeatEdges, request.forceBox, request.clampEachPass);
        !status.ok())
        return status;
    Grid vertical = axis_grid(horizontal, 1, destination.y, destination.height);
    if (const Status status = axis_blur(ctx, horizontal, vertical, 1, request.radiusY * density,
                                        request.repeatEdges, request.forceBox, request.clampEachPass);
        !status.ok())
        return status;
    const u32 width = static_cast<u32>(std::ceil(region.w * density));
    const u32 height = static_cast<u32>(std::ceil(region.h * density));
    const Rect filteredRegion{static_cast<f32>(vertical.x / density), static_cast<f32>(vertical.y / density),
                              static_cast<f32>(vertical.width / density),
                              static_cast<f32>(vertical.height / density)};
    Uniforms finish = uniforms(vertical, destination, 0);
    finish.uvMap = EffectBuildContext::uv_map(region, filteredRegion);
    finish.edge = {static_cast<f32>(vertical.width), static_cast<f32>(vertical.height), 0, 0};
    finish.settings.z = request.repeatEdges ? 1.f : 0.f;
    output = input;
    output.texture = ctx.texture("reference-blur-output", width, height);
    output.region = region;
    output.width = width;
    output.height = height;
    return raster(ctx, finish, output.texture, vertical.texture);
}
} // namespace aurea::fast_blur_reference

namespace aurea::builtin {
namespace {
class FastBlurReference final : public Effect {
  public:
    const EffectInfo &info() const noexcept override {
        static const EffectInfo value{fast_blur_reference::kKey, "Fast Blur", "Desfoque",
                                      EffectClass::Neighborhood};
        return value;
    }
    void declare_parameters(ParameterRegistry &params) const override {
        params.add_float("radius", "Blurriness", 0, 0, 100);
        params.typed_range(0, 30000);
        static const char *dimensions[]{"Horizontal and Vertical", "Horizontal", "Vertical"};
        params.add_enum("dimensions", "Blur Dimensions", dimensions, 3, 0);
        params.add_bool("repeat_edges", "Repeat Edge Pixels", true);
    }
    bool needs_full_input() const noexcept override {
        return true;
    }
    bool is_identity(const EffectEval &eval) const noexcept override {
        return eval.f(0) <= 0;
    }
    // AE starts at Blurriness 0 (an identity); previews use a visible radius.
    bool demo_values(EffectInstance &, std::vector<ParamValue> &values) const noexcept override {
        values[0] = ParamValue::scalar(10.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey> &out, SurfaceFormat work) const override {
        fast_blur_reference::pipelines(out, work);
    }
    f32 input_margin(const EffectEval &eval) const noexcept override {
        const f64 radius = eval.f(0);
        if (!std::isfinite(radius) || radius <= 0) return 0;
        return static_cast<f32>(radius / 2.71 <= 1 ? std::ceil(radius) : 3 * std::ceil(radius / 2.71));
    }
    Status build(EffectBuildContext &ctx, const EffectEval &eval, const LayerImage &input, f32 margin,
                 LayerImage &out) const override {
        fast_blur_reference::Request request;
        request.radiusX = eval.e(1) == 2 ? 0.f : eval.f(0);
        request.radiusY = eval.e(1) == 1 ? 0.f : eval.f(0);
        request.repeatEdges = eval.b(2);
        request.density = std::max(eval.texelScale, input.texel_scale_x());
        request.outRegion = input.region;
        if (!request.repeatEdges)
            request.outRegion =
                spread_region(input.region, request.radiusX > 0 ? input_margin(eval) : 0,
                              request.radiusY > 0 ? input_margin(eval) : 0, eval.placement, margin);
        return fast_blur_reference::build(ctx, input, request, out);
    }
};
} // namespace
void register_fast_blur_reference(EffectRegistry &registry) {
    (void)registry.add(std::make_unique<FastBlurReference>());
}
} // namespace aurea::builtin
