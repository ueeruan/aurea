#include "BuiltinEffects.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace aurea::builtin {
namespace {
f32 bounded(f32 value, f32 fallback, f32 low, f32 high) noexcept {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
class SurfaceDeform final : public Effect {
public:
    explicit SurfaceDeform(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo items[]{
            {"aurea.distort.bender", "Curvar entre pontos", "Distorcer", EffectClass::Domain},
            {"aurea.distort.bend", "Dobrar", "Distorcer", EffectClass::Domain},
            {"aurea.distort.curl", "Enrolar", "Distorcer", EffectClass::Domain}};
        return items[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        if (mode_ == 0) {
            p.add_float("amount", "Curvatura", 0, -1000, 1000, kParamAnimatable | kParamPixels, "px");
            p.typed_range(-10000,10000);
            p.add_point2("start", "Ponto inicial", {.25f,.5f}, -1, 2, kParamAnimatable | kParamRelative);
            p.add_point2("end", "Ponto final", {.75f,.5f}, -1, 2, kParamAnimatable | kParamRelative);
        } else {
            p.add_angle("angle", "Ângulo", 0, mode_ == 1 ? -180.f : 0.f, 180);
            if (mode_ == 2) {
                p.add_float("radius", "Raio", 120, 1, 2000, kParamAnimatable | kParamPixels, "px");
                p.typed_range(1,20000);
            }
            p.add_point2("anchor", "Âncora da dobra", {.5f,.5f}, -1, 2, kParamAnimatable | kParamRelative);
            p.add_angle("direction", "Direção", 0);
            p.add_color("back_color", "Cor do verso", {1,1,1,1});
            p.add_float("shading", "Iluminação", 50, 0, 100, kParamAnimatable | kParamPercent, "%");
        }
        p.add_float("mix", "Mistura", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
    }
    bool needs_full_input() const noexcept override { return true; }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (std::fabs(bounded(e.f(0),0,-10000,10000)) < .00001f || e.f(mix_index()) <= 0) return true;
        if (mode_ == 0) {
            const Vec2 a=e.p2(1), b=e.p2(2);
            return std::hypot(a.x-b.x,a.y-b.y)<1e-6f;
        }
        return false;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        if (mode_ == 0) return std::fabs(bounded(e.f(0),0,-10000,10000));
        // Conservative planning bound. The build below uses the exact finite
        // source extent and the cylinder's extrema to allocate the output.
        const f32 w=e.placement?static_cast<f32>(e.placement->layerWidth):0;
        const f32 h=e.placement?static_cast<f32>(e.placement->layerHeight):0;
        const auto anchor=e.p2(mode_==1?1:2);
        const f32 ax=bounded(anchor.x,.5f,-1,2), ay=bounded(anchor.y,.5f,-1,2);
        return 2*std::hypot(w*std::max(std::fabs(ax),std::fabs(1-ax)),
                            h*std::max(std::fabs(ay),std::fabs(1-ay)));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[0]=ParamValue::scalar(mode_==0?35.f:mode_==1?65.f:145.f);
        if(mode_==2) v[1]=ParamValue::scalar(80);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_surface_deform_frag,work));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32 margin,LayerImage& out) const override {
        const f32 w=e.placement&&e.placement->layerWidth?static_cast<f32>(e.placement->layerWidth):input.region.w;
        const f32 h=e.placement&&e.placement->layerHeight?static_cast<f32>(e.placement->layerHeight):input.region.h;
        const auto point=[&](u32 index){const auto p=e.p2(index);return Vec2{bounded(p.x,.5f,-1,2)*w,bounded(p.y,.5f,-1,2)*h};};
        const Vec2 anchor=point(mode_==0||mode_==1?1:2);
        const Vec2 end=mode_==0?point(2):anchor;
        const f32 value=bounded(e.f(0),0,mode_==0?-10000.f:mode_==1?-180.f:0.f,mode_==0?10000.f:180.f);
        const f32 angle=value*kDeg2Rad;
        const f32 direction=mode_==0?0:std::remainder(bounded(e.f(mode_==1?2:3),0,-360000,360000),360.f)*kDeg2Rad;
        const Vec2 axis{std::cos(direction),std::sin(direction)}, side{-axis.y,axis.x};
        const f32 radius=mode_==2?bounded(e.f(1),120,1,20000):1;
        Rect region=input.region;
        if(mode_==0) {
            const f32 length=std::hypot(end.x-anchor.x,end.y-anchor.y);
            const f32 dx=length>1e-5f?std::fabs((end.y-anchor.y)/length*value):0;
            const f32 dy=length>1e-5f?std::fabs((end.x-anchor.x)/length*value):0;
            region=spread_region(input.region,dx,dy,e.placement,margin);
        } else {
            f32 s0=std::numeric_limits<f32>::max(), s1=-s0,t0=s0,t1=-s0;
            for(const auto& p:std::array<Vec2,4>{{{region.x,region.y},{region.x+region.w,region.y},
                {region.x,region.y+region.h},{region.x+region.w,region.y+region.h}}}) {
                const f32 x=p.x-anchor.x,y=p.y-anchor.y,s=x*axis.x+y*axis.y,t=x*side.x+y*side.y;
                s0=std::min(s0,s);s1=std::max(s1,s);t0=std::min(t0,t);t1=std::max(t1,t);
            }
            f32 x0=region.x,y0=region.y,x1=region.x+region.w,y1=region.y+region.h;
            auto projected=[&](f32 s){
                if(s<=0)return s;
                if(mode_==1)return s*std::cos(angle);
                const f32 arc=radius*angle;
                return s<arc?radius*std::sin(s/radius):radius*std::sin(angle)+(s-arc)*std::cos(angle);
            };
            for(const f32 s: {s0,s1,0.f,radius*angle,radius*1.57079632679f}) {
                if(s<s0||s>s1)continue;
                for(const f32 t:{t0,t1}) {
                    const f32 n=projected(s),x=anchor.x+axis.x*n+side.x*t,y=anchor.y+axis.y*n+side.y*t;
                    x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
                }
            }
            region=spread_region({x0,y0,x1-x0,y1-y0},0,0,e.placement,margin);
        }
        // Preserve the source texel lattice in stationary parts of the sheet.
        // Fractional output bounds must not introduce a second resampling.
        const f32 dx=std::max(.001f,input.texel_scale_x()),dy=std::max(.001f,input.texel_scale_y());
        const f32 x0=input.region.x+std::floor((region.x-input.region.x)*dx-1e-3f)/dx;
        const f32 y0=input.region.y+std::floor((region.y-input.region.y)*dy-1e-3f)/dy;
        const f32 x1=input.region.x+std::ceil((region.x+region.w-input.region.x)*dx+1e-3f)/dx;
        const f32 y1=input.region.y+std::ceil((region.y+region.h-input.region.y)*dy+1e-3f)/dy;
        region={x0,y0,x1-x0,y1-y0};
        u32 tw=0,th=0;ctx.region_size(region,input.texel_scale_x(),tw,th);
        EffectUniforms u;
        u.uvMap={region.x,region.y,region.w,region.h};
        u.texel={input.region.x,input.region.y,input.region.w,input.region.h};
        u.p0={anchor.x,anchor.y,end.x,end.y};
        u.p1={static_cast<f32>(mode_),mode_==0?value:angle,radius,bounded(e.f(mix_index()),100,0,100)*.01f};
        u.p2={axis.x,axis.y,mode_==0?0:bounded(e.f(mode_==1?4:5),50,0,100)*.01f,0};
        u.color=mode_==0?Vec4{1,1,1,1}:e.color(mode_==1?3:4);
        u.color={Color::srgb_to_linear(bounded(u.color.x,1,0,1)),Color::srgb_to_linear(bounded(u.color.y,1,0,1)),
                 Color::srgb_to_linear(bounded(u.color.z,1,0,1)),bounded(u.color.w,1,0,1)};
        u.p3={region.w/static_cast<f32>(std::max(1u,tw)),region.h/static_cast<f32>(std::max(1u,th)),0,0};
        out={ctx.texture("surface-deform",tw,th),region,tw,th};
        if(ctx.fullscreen_pass("surface-deform",PassStage::Effects,out.texture,ShaderId::effects_surface_deform_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex) return Errc::PipelineCompileFailed;
        return OkStatus;
    }
private:
    u32 mode_;
    u32 mix_index() const noexcept {return mode_==0?3:mode_==1?5:6;}
};
}
void register_surface_deform_effects(EffectRegistry& r) {
    for(u32 mode=0;mode<3;++mode) (void)r.add(std::make_unique<SurfaceDeform>(mode));
}
}
