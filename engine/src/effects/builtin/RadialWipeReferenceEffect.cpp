#include "BuiltinEffects.hpp"
#include "aurea/effects/FastBlurReference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aurea::builtin {
namespace {

constexpr const char* kRadialWipeReference = "aurea.transition.radial_wipe_reference";

f64 finite_value(f32 value, f64 fallback) noexcept {
    return std::isfinite(value) ? static_cast<f64>(value) : fallback;
}

f64 fixed_value(f64 value) noexcept {
    return std::round(value * 65536.) / 65536.;
}

struct RadialUniforms {
    std::array<i32, 4> normals{};
    std::array<i32, 4> controls{};
};

class RadialWipeReference final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{kRadialWipeReference, "Radial Wipe", "Transição", EffectClass::Domain};
        return info;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão da transição", 0, 0, 100,
                    kParamAnimatable | kParamPercent, "%");
        p.add_angle("angle", "Ângulo inicial", 0);
        p.add_point2("center", "Centro", {.5f, .5f}, -10, 10, kParamAnimatable | kParamRelative);
        static const char* const directions[] = {"Clockwise", "Counterclockwise", "Both"};
        p.add_enum("direction", "Direção", directions, 3, 0);
        p.add_float("feather", "Suavizar borda", 0, 0, 100, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0, 32000);
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return fixed_value(std::clamp(finite_value(e.f(0), 0), 0., 100.)) <= 0;
    }

    bool needs_full_input() const noexcept override { return true; }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[0] = ParamValue::scalar(65);
        return true;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_radial_wipe_reference_frag, format));
        fast_blur_reference::pipelines(out, format);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32, LayerImage& out) const override {
        const f64 completion = fixed_value(std::clamp(finite_value(e.f(0), 0), 0., 100.));
        out = input;
        out.texture = ctx.texture("radial-wipe-reference", input.width, input.height);
        if (completion * 65536. >= 0x63ffce) {
            (void)ctx.graph().add_raster_pass("radial-wipe-empty", PassStage::Effects, out.texture,
                LoadOp::Clear, Vec4{}, [](PassContext&) {});
            return OkStatus;
        }

        RadialUniforms uniforms;
        const f64 densityX = input.texel_scale_x(), densityY = input.texel_scale_y();
        const f64 width = e.placement ? e.placement->layerWidth : input.region.w;
        const f64 height = e.placement ? e.placement->layerHeight : input.region.h;
        const Vec2 center = e.p2(2);
        const f64 centerX = fixed_value(finite_value(center.x, .5) * width * densityX) - input.region.x * densityX;
        const f64 centerY = fixed_value(finite_value(center.y, .5) * height * densityY) - input.region.y * densityY;
        const u32 direction = std::min(e.e(3), 2u);
        f64 start = fixed_value(finite_value(e.f(1), 0));
        if (direction == 1) start += 180;
        if (direction == 2) start -= completion * 1.8;
        const f64 sweep = direction == 1 ? 360 - completion * 3.6 : completion * 3.6;
        for (u32 plane = 0; plane < 2; ++plane) {
            const f64 angle = std::fmod(start + plane * sweep, 360.) * (3.14159265358979323846 / 180.);
            // The shared renderer uses square source pixels. The density ratio
            // keeps the same physical boundary in anisotropic preview rasters.
            const f64 nx = std::cos(angle) * densityY / densityX;
            const f64 ny = std::sin(angle);
            const f64 threshold = std::trunc(((centerX - .5) * nx + (centerY - .5) * ny) * 65536.);
            const f64 nxFixed = std::round(nx * 65536.), nyFixed = std::round(ny * 65536.);
            const f64 extent = std::abs(nxFixed) * (input.width + 1.) + std::abs(nyFixed) * (input.height + 1.);
            if (!std::isfinite(threshold) || std::abs(threshold) >= std::numeric_limits<i32>::max() ||
                extent >= std::numeric_limits<i32>::max())
                return Status{Errc::OutOfRange, "Radial Wipe exceeds fixed-point raster addressing"};
            uniforms.normals[plane * 2] = static_cast<i32>(nxFixed);
            uniforms.normals[plane * 2 + 1] = static_cast<i32>(nyFixed);
            uniforms.controls[plane] = static_cast<i32>(threshold);
        }
        uniforms.controls[2] = completion > 50 ? 1 : 0;
        const auto pass = [&](const char* label, FGTexture target, PassTexture mask) {
            return ctx.fullscreen_pass(label, PassStage::Effects, target,
                ShaderId::effects_radial_wipe_reference_frag,
                {{input.texture, {}, CommonSampler::NearestClamp}, mask}, &uniforms, sizeof(uniforms)) != kInvalidIndex;
        };
        const f32 feather = static_cast<f32>(std::clamp(finite_value(e.f(4), 0), 0., 32000.));
        if (!(feather > 0)) {
            uniforms.controls[3] = 2;
            return pass("radial-wipe-reference", out.texture, {input.texture, {}, CommonSampler::NearestClamp})
                ? OkStatus : Status{Errc::PipelineCompileFailed};
        }
        LayerImage mask;
        mask.region = {0, 0, static_cast<f32>(input.width), static_cast<f32>(input.height)};
        mask.width = input.width;
        mask.height = input.height;
        mask.texture = ctx.texture("radial-wipe-mask", mask.width, mask.height);
        if (!pass("radial-wipe-mask", mask.texture, {input.texture, {}, CommonSampler::NearestClamp}))
            return Errc::PipelineCompileFailed;
        fast_blur_reference::Request blur;
        blur.radiusX = blur.radiusY = feather * static_cast<f32>((densityX + densityY) * .5);
        blur.forceBox = true;
        blur.repeatEdges = true;
        LayerImage softened;
        if (const auto status = fast_blur_reference::build(ctx, mask, blur, softened); !status.ok()) return status;
        uniforms.controls[3] = 1;
        return pass("radial-wipe-compose", out.texture, {softened.texture, {}, CommonSampler::NearestClamp})
            ? OkStatus : Status{Errc::PipelineCompileFailed};
    }
};
}

void register_radial_wipe_reference(EffectRegistry& registry) {
    (void)registry.add(std::make_unique<RadialWipeReference>());
}
}
