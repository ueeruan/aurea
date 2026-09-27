#include "BuiltinEffects.hpp"
#include "aurea/tracking/MotionGeometry.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
class CornerPin final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{effect_keys::kCornerPin,"Corner Pin","Distort",EffectClass::Domain};return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        const char* names[]={"Top left X","Top left Y","Top right X","Top right Y","Bottom right X","Bottom right Y","Bottom left X","Bottom left Y"};
        const char* keys[]={"tl_x","tl_y","tr_x","tr_y","br_x","br_y","bl_x","bl_y"};
        const f32 defaults[]={0,0,100,0,100,100,0,100};
        for(int i=0;i<8;++i)p.add_float(keys[i],names[i],defaults[i],-5000,5000,kParamAnimatable|kParamPercent,"%");
        p.add_bool("crop","Crop to layer",false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const f32 defaults[]={0,0,100,0,100,100,0,100};
        for(int i=0;i<8;++i)if(std::abs(e.f(i)-defaults[i])>.0001f)return false;return true;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.placement?static_cast<f32>(std::max(e.placement->layerWidth,e.placement->layerHeight)):4096;
    }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_corner_pin_frag,work));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32 margin,LayerImage& out) const override {
        const f32 w=e.placement?static_cast<f32>(e.placement->layerWidth):input.region.w;
        const f32 h=e.placement?static_cast<f32>(e.placement->layerHeight):input.region.h;
        std::array<Vec2,4> corners;for(int i=0;i<4;++i)corners[i]={e.f(i*2)*w/100,e.f(i*2+1)*h/100};
        tracking::Homography map,inv;const bool valid=tracking::quad_map(corners,map)&&map.inverse(inv);
        Rect region=input.region;
        if(valid && e.f(8)<.5f) {
            f32 x=corners[0].x,y=corners[0].y,r=x,b=y;
            for(auto p:corners){x=std::min(x,p.x);y=std::min(y,p.y);r=std::max(r,p.x);b=std::max(b,p.y);}
            region=spread_region({x,y,r-x,b-y},0,0,e.placement,margin);
        }
        u32 ow=0,oh=0;ctx.region_size(region,input.texel_scale_x(),ow,oh);
        if(!ow||!oh){out=input;return OkStatus;}
        auto u=base_uniforms(input);
        u.uvMap={region.x,region.y,region.w,region.h};
        u.p0={static_cast<f32>(inv.m[0]),static_cast<f32>(inv.m[1]),static_cast<f32>(inv.m[2]),0};
        u.p1={static_cast<f32>(inv.m[3]),static_cast<f32>(inv.m[4]),static_cast<f32>(inv.m[5]),0};
        u.p2={static_cast<f32>(inv.m[6]),static_cast<f32>(inv.m[7]),static_cast<f32>(inv.m[8]),valid?1.f:0.f};
        u.p3={input.region.x,input.region.y,input.region.w,input.region.h};u.color={w,h,0,0};
        out={ctx.texture("corner pin",ow,oh),region,ow,oh};
        if(ctx.fullscreen_pass("corner pin",PassStage::Effects,out.texture,ShaderId::effects_corner_pin_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
}
void register_corner_pin_effect(EffectRegistry& r){(void)r.add(std::make_unique<CornerPin>());}
}
