// AE reference batch C: one registration point, appended after every other
// builtin, so the registry order of the older effects does not change.
// Extrude (Plugin Everything, PEMUEXTRUDE), Depth Field (ADBE DEPTH FIELD),
// Smear (ADBE SCHMEAR) and Glitchify (CSpice Glitchify).
// Evidence: build/reference/effects-20261115/batch-c/status.json. Behaviour
// only: no native code, table or shader is part of the product.
#include "BuiltinEffects.hpp"
#include "AeEffects2026.hpp"
#include "aurea/effects/ExtrudeReference.hpp"
#include "aurea/effects/GlitchifyReference.hpp"
#include "aurea/effects/SmearReference.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace aurea::builtin {
namespace {
using namespace ae26;

// -----------------------------------------------------------------------------
// Depth of Field (AE 3D Channel › Depth of Field, ADBE DEPTH FIELD).
// REA (batch-c/rea/Depth_Field): SmartRender reads Focal Plane, Maximum
// Radius, Focal Plane Thickness and Focal Bias (bias' = 1 − (bias·0.006 +
// 0.2)); radius ≤ 0 or no depth channel → the layer unchanged. Otherwise a
// pyramid of halved worlds is sampled per pixel at level log2(radius) and the
// two neighbouring levels are mixed (depth_field_reference.frag). The native
// depth is the render's 3D channel; Aurea has no Z pass, so Z comes from the
// AI depth map (the same source as Depth Matte / Fog 3D) mapped to the two
// appended controls Near Z / Far Z.
// -----------------------------------------------------------------------------
class DepthFieldReference final : public Effect {
public:
    enum : u32 { kFocalPlane = 0, kMaxRadius, kThickness, kFocalBias, kNearZ, kFarZ, kSmoothing };
    static constexpr u32 kMaxLevels = 10;
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.ai.depth_field_reference", "Profundidade de campo", "3D", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("focal_plane", "Plano focal", 0.0f, -1000.0f, 1000.0f, kParamAnimatable);
        p.typed_range(-10000000.0f, 10000000.0f);
        p.add_float("max_radius", "Raio máximo", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("focal_thickness", "Espessura do plano focal", 0.0f, 0.0f, 1000.0f, kParamAnimatable);
        p.typed_range(0.0f, 10000.0f);
        p.add_float("focal_bias", "Desvio focal", 50.0f, 0.0f, 100.0f, kParamAnimatable);
        // Aurea: the AI depth stands in for the 3D channel.
        p.add_float("near_z", "Profundidade perto", 0.0f, -1000.0f, 1000.0f, kParamAnimatable);
        p.typed_range(-10000000.0f, 10000000.0f);
        p.add_float("far_z", "Profundidade longe", 1000.0f, -1000.0f, 2000.0f, kParamAnimatable);
        p.typed_range(-10000000.0f, 10000000.0f);
        p.add_float("smoothing", "Suavização", 70.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_section_range("depth", "Profundidade (IA)", kNearZ, kSmoothing);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kMaxRadius] = ParamValue::scalar(12.0f);
        v[kThickness] = ParamValue::scalar(150.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(fin(e.f(kMaxRadius), 0.0f) > 0.0f); }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::clamp(fin(e.f(kMaxRadius), 0.0f), 0.0f, 1000.0f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_depth_field_reference_down_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_depth_field_reference_frag, work));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{0.0f, 1.0f, 0.0f, 0.0f};
        if (!e.resources || is_identity(e)) return;
        DepthMapRequest req;
        req.host = e.layer;
        req.instance = e.instance;
        req.localTime = e.localTime;
        req.smoothing = pct(e, kSmoothing);
        const DepthMapResult r = e.resources->depth_map(req);
        if (!r.texture.valid()) return;
        e.aux = r.texture;
        e.auxInfo = Vec4{r.lo, r.hi, 1.0f, 0.0f};
        e.auxMapX = r.mapX;
        e.auxMapY = r.mapY;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        // Native: no depth channel → the layer as it is.
        if (!e.aux.valid() || is_identity(e) || !input.width || !input.height) { out = input; return OkStatus; }
        const f32 radius = std::clamp(fin(e.f(kMaxRadius), 0.0f), 0.0f, 1000.0f) * input.texel_scale_x();
        const u32 wanted = std::min<u32>(kMaxLevels, static_cast<u32>(std::ceil(std::log2(std::max(radius, 1.0f)))) + 1u);
        PassTexture textures[12];
        textures[0] = PassTexture{input.texture, {}, CommonSampler::LinearBorder};
        textures[1] = PassTexture{{}, e.aux, CommonSampler::LinearClamp};
        FGTexture previous = input.texture;
        u32 pw = input.width, ph = input.height, levels = 0;
        static const char* const kNames[kMaxLevels] = {"dof-ref-l1", "dof-ref-l2", "dof-ref-l3", "dof-ref-l4", "dof-ref-l5",
                                                       "dof-ref-l6", "dof-ref-l7", "dof-ref-l8", "dof-ref-l9", "dof-ref-l10"};
        while (levels < wanted && pw > 2 && ph > 2) {
            const u32 w = std::max(1u, (pw + 1) / 2), h = std::max(1u, (ph + 1) / 2);
            const FGTexture level = ctx.texture(kNames[levels], w, h);
            const Vec4 texel{1.0f / static_cast<f32>(pw), 1.0f / static_cast<f32>(ph), 1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h)};
            if (ctx.fullscreen_pass(kNames[levels], PassStage::Effects, level, ShaderId::effects_depth_field_reference_down_frag,
                    {PassTexture{previous, {}, CommonSampler::LinearBorder}}, &texel, sizeof(texel)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            textures[2 + levels] = PassTexture{level, {}, CommonSampler::LinearBorder};
            previous = level;
            pw = w; ph = h;
            ++levels;
        }
        for (u32 i = 2 + levels; i < 12; ++i) textures[i] = textures[0];
        AeUniforms u = ae_uniforms(e, input, false);
        const f32 bias = 1.0f - (std::clamp(fin(e.f(kFocalBias), 50.0f), 0.0f, 100.0f) * 0.006f + 0.2f);
        u.p[0] = Vec4{fin(e.f(kFocalPlane), 0.0f), radius, std::max(0.0f, fin(e.f(kThickness), 0.0f)), bias};
        u.p[1] = Vec4{fin(e.f(kNearZ), 0.0f), fin(e.f(kFarZ), 1000.0f), static_cast<f32>(levels), 0.0f};
        u.p[8] = Vec4{1.0f, 0.0f, e.auxInfo.x, e.auxInfo.y};
        if (e.auxMapX.w > 0.0f && e.auxMapY.w > 0.0f) {
            u.p[8].y = 1.0f;
            u.p[9] = Vec4{e.auxMapX.x, e.auxMapX.y, e.auxMapY.x, e.auxMapY.y};
            u.p[10] = Vec4{e.auxMapX.w, e.auxMapY.w, e.auxMapX.z, e.auxMapY.z};
        }
        out = input;
        out.texture = ctx.texture("dof-reference", input.width, input.height);
        if (ctx.fullscreen_pass("dof-reference", PassStage::Effects, out.texture, ShaderId::effects_depth_field_reference_frag,
                textures, 12, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_reference_batch_c(EffectRegistry& registry) {
    extrude_reference::register_effect(registry);
    (void)registry.add(std::make_unique<DepthFieldReference>());
    smear_reference::register_effect(registry);
    glitchify_reference::register_effect(registry);
}

} // namespace aurea::builtin
