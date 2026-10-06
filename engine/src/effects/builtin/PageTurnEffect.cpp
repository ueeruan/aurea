#include "BuiltinEffects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aurea::builtin {
namespace {

constexpr f32 kPagePi = 3.14159265358979323846f;
f32 page_value(f32 value, f32 fallback) noexcept { return std::isfinite(value) ? value : fallback; }

// Orthographic paper: a stationary half-plane, a half-cylinder and a flat
// returned sheet. The fragment shader solves all visible inverse branches;
// there is no simulation state, tessellation or dependency on playback order.
class PageTurn final : public Effect {
public:
    enum : u32 { kProgress, kDirection, kRadius, kPosition, kLightDirection,
                 kLighting, kBackOpacity, kPaperColor, kMix };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.distort.page_turn", "Virar página", "Distorcer", EffectClass::Domain};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("progress", "Progresso", 25, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_angle("direction", "Direção", 0);
        p.add_float("radius", "Raio", 40, .5f, 1000, kParamAnimatable | kParamPixels, "px");
        p.typed_range(.5f, 5000);
        p.add_point2("fold_position", "Posição da dobra", Vec2{.5f, .5f}, -1, 2, kParamAnimatable | kParamRelative);
        p.add_angle("light_direction", "Direção da luz", -45);
        p.add_float("light_amount", "Iluminação", 60, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("back_opacity", "Opacidade do verso", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_color("paper_color", "Cor do papel", Vec4{.95f, .95f, .95f, 1});
        p.add_float("mix", "Mistura", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_page_turn_frag, format));
    }
    bool needs_full_input() const noexcept override { return true; }
    bool is_identity(const EffectEval& e) const noexcept override {
        return page_value(e.f(kProgress), 0) <= 0 || page_value(e.f(kMix), 0) <= 0;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        // A distant source can return into view. This bounds displacement for
        // the editable anchor range, while needs_full_input preserves its pixels.
        return e.placement ? 8.f * std::max(e.placement->layerWidth, e.placement->layerHeight) : 0.f;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32 margin, LayerImage& out) const override {
        const Rect in = input.region;
        const f32 width = e.placement ? static_cast<f32>(std::max(1u, e.placement->layerWidth)) : std::max(1.f, in.w);
        const f32 height = e.placement ? static_cast<f32>(std::max(1u, e.placement->layerHeight)) : std::max(1.f, in.h);
        const f32 angle = std::remainder(page_value(e.f(kDirection), 0), 360.f) * kDeg2Rad;
        const Vec2 n{std::cos(angle), std::sin(angle)}, tangent{-n.y, n.x};
        const f32 progress = std::clamp(page_value(e.f(kProgress), 0) / 100.f, 0.f, 1.f);
        const f32 radius = std::clamp(page_value(e.f(kRadius), 40), .5f, 5000.f);
        const f32 span = .5f * (std::fabs(n.x) * width + std::fabs(n.y) * height);
        const Vec2 position = e.p2(kPosition);
        const f32 offset = (1.f - 2.f * progress) * span - progress * kPagePi * radius;
        const Vec2 anchor{std::clamp(page_value(position.x, .5f), -1.f, 2.f) * width + n.x * offset,
                          std::clamp(page_value(position.y, .5f), -1.f, 2.f) * height + n.y * offset};
        f32 lo = std::numeric_limits<f32>::max(), hi = -lo, sideLo = lo, sideHi = hi;
        const std::array<Vec2, 4> corners{{{in.x, in.y}, {in.x + in.w, in.y},
                                         {in.x, in.y + in.h}, {in.x + in.w, in.y + in.h}}};
        for (const auto p : corners) {
            const f32 x = p.x - anchor.x, y = p.y - anchor.y;
            const f32 along = x * n.x + y * n.y, across = x * tangent.x + y * tangent.y;
            lo = std::min(lo, along); hi = std::max(hi, along);
            sideLo = std::min(sideLo, across); sideHi = std::max(sideHi, across);
        }
        auto bend = [radius](f32 s) {
            if (s <= 0) return s;
            if (s >= kPagePi * radius) return kPagePi * radius - s;
            return radius * std::sin(s / radius);
        };
        f32 bentLo = std::min(bend(lo), bend(hi)), bentHi = std::max(bend(lo), bend(hi));
        if (lo <= 0 && hi >= 0) { bentLo = std::min(bentLo, 0.f); bentHi = std::max(bentHi, 0.f); }
        if (lo <= .5f * kPagePi * radius && hi >= .5f * kPagePi * radius) bentHi = std::max(bentHi, radius);
        if (lo <= kPagePi * radius && hi >= kPagePi * radius) bentLo = std::min(bentLo, 0.f);
        f32 x0 = std::numeric_limits<f32>::max(), y0 = x0, x1 = -x0, y1 = -x0;
        for (const f32 d : {bentLo, bentHi}) for (const f32 side : {sideLo, sideHi}) {
            const f32 x = anchor.x + n.x * d + tangent.x * side;
            const f32 y = anchor.y + n.y * d + tangent.y * side;
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        const f32 mix = std::clamp(page_value(e.f(kMix), 100) / 100.f, 0.f, 1.f);
        if (mix < 1) { x0 = std::min(x0, in.x); y0 = std::min(y0, in.y); x1 = std::max(x1, in.x + in.w); y1 = std::max(y1, in.y + in.h); }
        const f32 edge = 1.f / std::max(.01f, input.texel_scale_x());
        Rect region = spread_region(Rect{x0, y0, std::max(1.f, x1-x0), std::max(1.f, y1-y0)},
                                    edge, edge, e.placement, margin);
        // Keep stationary paper on the existing texel grid. A fractional
        // allocation origin would resample even pixels far from the fold twice.
        const f32 density = std::max(.01f, input.texel_scale_x());
        const f32 right = in.x + std::ceil((region.x + region.w - in.x) * density) / density;
        const f32 bottom = in.y + std::ceil((region.y + region.h - in.y) * density) / density;
        region.x = in.x + std::floor((region.x - in.x) * density) / density;
        region.y = in.y + std::floor((region.y - in.y) * density) / density;
        region.w = std::max(1.f / density, right - region.x);
        region.h = std::max(1.f / density, bottom - region.y);
        u32 w = 0, h = 0; ctx.region_size(region, input.texel_scale_x(), w, h);
        EffectUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(region, in);
        u.texel = Vec4{in.x, in.y, std::max(in.w, .001f), std::max(in.h, .001f)};
        u.p0 = Vec4{anchor.x, anchor.y, n.x, n.y};
        const f32 light = std::remainder(page_value(e.f(kLightDirection), -45), 360.f) * kDeg2Rad;
        u.p1 = Vec4{radius, std::clamp(page_value(e.f(kBackOpacity), 100) / 100.f, 0.f, 1.f),
                    .5f * (std::cos(light) * n.x + std::sin(light) * n.y),
                    std::clamp(page_value(e.f(kLighting), 60) / 100.f, 0.f, 1.f)};
        u.p2 = Vec4{.8660254f, mix, 0, 0};
        u.p3 = Vec4{region.w / std::max(1u, w), region.h / std::max(1u, h), 0, 0};
        const Vec4 color = e.color(kPaperColor);
        u.color = Vec4{Color::srgb_to_linear(std::clamp(page_value(color.x, 1), 0.f, 1.f)),
                       Color::srgb_to_linear(std::clamp(page_value(color.y, 1), 0.f, 1.f)),
                       Color::srgb_to_linear(std::clamp(page_value(color.z, 1), 0.f, 1.f)),
                       std::clamp(page_value(color.w, 1), 0.f, 1.f)};
        out = LayerImage{ctx.texture("page-turn", w, h), region, w, h};
        return ctx.fullscreen_pass("page-turn", PassStage::Transform, out.texture, ShaderId::effects_page_turn_frag,
            {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};
} // namespace
void register_page_turn_effect(EffectRegistry& r) { (void)r.add(std::make_unique<PageTurn>()); }
} // namespace aurea::builtin
