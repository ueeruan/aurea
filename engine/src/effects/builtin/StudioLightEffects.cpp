#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>
namespace aurea::builtin {
namespace {
class DeepGlow2 final : public Effect {
public:
    const EffectInfo& info() const noexcept override {static const EffectInfo i{effect_keys::kDeepGlow2,"Deep Glow 2","Light",EffectClass::Neighborhood};return i;}
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius","Radius",80,0,2000,kParamAnimatable|kParamPixels,"px");
        p.add_float("exposure","Exposure",1,0,10);
        p.add_float("threshold","Threshold",70,0,1000,kParamAnimatable|kParamPercent,"%");
        p.add_float("softness","Threshold Smooth",50,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("spread","Spread",33,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("aspect","Aspect Ratio",1,.1f,10);
        p.add_float("aberration","Chromatic Radius",0,-100,100,kParamAnimatable|kParamPercent,"%");
        p.add_color("inner_tint","Inner Tint",{1,1,1,1});
        p.add_color("outer_tint","Outer Tint",{1,1,1,1});
        p.add_float("tint","Tint Amount",0,0,100,kParamAnimatable|kParamPercent,"%");
        static const char* blend[]={"Add","Screen"};p.add_enum("blend","Blend",blend,2,0);
        static const char* tone[]={"None (HDR)","Reinhard","ACES","Filmic","Exponential","Clamp"};p.add_enum("tonemap","Tone Mapping",tone,6,0);
        static const char* quality[]={"Draft","Balanced","High"};p.add_enum("quality","Quality",quality,3,1);
        static const char* view[]={"Composite","Glow Only","Glow Input"};p.add_enum("view","View",view,3,0);
        p.add_float("saturation_bias","Saturation Bias",0,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("mix","Mix",100,0,100,kParamAnimatable|kParamPercent,"%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {return e.f(15)<=0 || (e.f(1)<=0&&e.e(13)==0);}
    f32 input_margin(const EffectEval& e) const noexcept override {return e.f(0)*std::max(e.f(5),1.f/e.f(5));}
    void pipelines(std::vector<PipelineKey>& v,SurfaceFormat f) const override {
        for(auto shader:{ShaderId::effects_glow2_frag,ShaderId::effects_gaussian_blur_frag,ShaderId::effects_downsample_frag})v.push_back(PipelineKey::fullscreen(shader,f));
    }
    Status build(EffectBuildContext& c,const EffectEval& e,const LayerImage& in,f32 margin,LayerImage& out) const override {
        const f32 radius=std::max(0.f,e.f(0)),aspect=std::clamp(e.f(5),.1f,10.f);
        const Rect region=spread_region(in.region,radius*std::max(aspect,1.f),radius*std::max(1.f/aspect,1.f),e.placement,margin);
        u32 w,h;c.region_size(region,in.texel_scale_x(),w,h);
        EffectUniforms u{};u.uvMap=EffectBuildContext::uv_map(region,in.region);
        u.texel={e.f(2)*.01f,e.f(3)*.01f,e.f(14)*.01f,0};
        // One threshold source, reused by every normalized lobe. Radius zero
        // remains valid; no divide by the radius or alpha anywhere in the graph.
        LayerImage bright{c.texture("glow2-threshold",w,h),region,w,h};
        if(c.fullscreen_pass("glow2-threshold",PassStage::Effects,bright.texture,ShaderId::effects_glow2_frag,
            {{in.texture,{},CommonSampler::LinearBorder},{in.texture,{},CommonSampler::LinearBorder},{in.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
        const u32 levels=e.e(12)==0?3:(e.e(12)==1?5:7);
        f32 total[3]{};
        f32 weights[7][3]{};
        for(u32 j=0;j<levels;++j)for(u32 ch=0;ch<3;++ch){
            const f32 power=.25f+1.5f*(1.f-e.f(4)*.01f)+(f32(ch)-1.f)*e.f(6)*.01f;
            weights[j][ch]=std::pow(2.f,f32(j)*power);total[ch]+=weights[j][ch];
        }
        LayerImage sum=bright;
        for(u32 j=0;j<levels;++j){
            const f32 sigma=radius/(3.f*std::pow(2.f,f32(j)));
            LayerImage lobe;BlurRequest req;req.sigmaX=sigma*aspect;req.sigmaY=sigma/aspect;req.outRegion=region;req.label="glow2-lobe";
            if(auto s=build_gaussian(c,bright,req,lobe);!s.ok())return s;
            u={};u.p3.x=1;u.p0={weights[j][0]/total[0],weights[j][1]/total[1],weights[j][2]/total[2],j==0?0.f:1.f};
            const f32 inner=levels>1?f32(j)/f32(levels-1):1.f;
            const Vec4 a=e.color(7),b=e.color(8);const f32 tint=e.f(9)*.01f;
            u.color={1.f-tint+tint*(b.x+(a.x-b.x)*inner),1.f-tint+tint*(b.y+(a.y-b.y)*inner),1.f-tint+tint*(b.z+(a.z-b.z)*inner),1};
            const u32 sw=std::max(lobe.width,j?sum.width:1u),sh=std::max(lobe.height,j?sum.height:1u);
            LayerImage next{c.texture("glow2-sum",sw,sh),region,sw,sh};
            if(c.fullscreen_pass("glow2-sum",PassStage::Effects,next.texture,ShaderId::effects_glow2_frag,
                {{lobe.texture,{},CommonSampler::LinearBorder},{sum.texture,{},CommonSampler::LinearBorder},{bright.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
            sum=next;
        }
        u={};u.p3.x=2;u.uvMap=EffectBuildContext::uv_map(region,in.region);
        u.p0={e.f(1),f32(e.e(10)),f32(e.e(11)),f32(e.e(13))};u.p1.x=e.f(15)*.01f;
        out={c.texture("glow2-output",w,h),region,w,h};
        if(c.fullscreen_pass("glow2-output",PassStage::Effects,out.texture,ShaderId::effects_glow2_frag,
            {{in.texture,{},CommonSampler::LinearBorder},{sum.texture,{},CommonSampler::LinearBorder},{bright.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

class ShadowStudio3 final : public Effect {
public:
    const EffectInfo& info() const noexcept override {static const EffectInfo i{effect_keys::kShadowStudio3,"Shadow Studio 3","Light",EffectClass::Neighborhood};return i;}
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* modes[]={"Drop","Long","Radial"};p.add_enum("mode","Mode",modes,3,1);
        p.add_float("length","Length",80,0,2000,kParamAnimatable|kParamPixels,"px");
        p.add_angle("angle","Angle",45);
        p.add_float("softness","Softness",12,0,400,kParamAnimatable|kParamPixels,"px");
        p.add_float("opacity","Opacity",70,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_color("near_color","Near Color",{0,0,0,1});
        p.add_color("far_color","Far Color",{0,0,0,0});
        p.add_point2("light","Light Position",{.5f,.25f},-5,5,kParamAnimatable|kParamRelative);
        p.add_bool("inner","Inner Shadow",false);
        p.add_bool("inverse","Inverse Direction",false);
        p.add_bool("source_rgba","Source Color",false);
        p.add_float("falloff","Falloff",0,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("spread","Shadow Spread",0,0,100,kParamAnimatable|kParamPercent,"%");
        static const char* view[]={"Composite","Shadow Only"};p.add_enum("view","View",view,2,0);
        static const char* quality[]={"Draft","Balanced","High"};p.add_enum("quality","Quality",quality,3,1);
        p.add_float("noise","Texture Noise",0,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_int("seed","Texture Seed",1,0,9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override {return e.f(4)<=0&&e.e(13)==0;}
    f32 input_margin(const EffectEval& e) const noexcept override {return e.b(8)?e.f(3):e.f(1)+e.f(3);}
    void pipelines(std::vector<PipelineKey>& v,SurfaceFormat f) const override {
        for(auto shader:{ShaderId::effects_shadow_studio_frag,ShaderId::effects_gaussian_blur_frag,ShaderId::effects_downsample_frag})v.push_back(PipelineKey::fullscreen(shader,f));
    }
    Status build(EffectBuildContext& c,const EffectEval& e,const LayerImage& in,f32 margin,LayerImage& out) const override {
        const f32 reach=e.b(8)?0:e.f(1)+e.f(3);
        const Rect region=spread_region(in.region,reach,reach,e.placement,margin);
        LayerImage blur;BlurRequest request;request.sigmaX=request.sigmaY=e.f(3)/3.f;request.outRegion=region;request.label="shadow-penumbra";
        if(auto s=build_gaussian(c,in,request,blur);!s.ok())return s;
        struct Uniform{EffectUniforms base;Vec4 farColor;Vec4 blurMap;Vec4 dimensions;} u{};
        u.base.uvMap=EffectBuildContext::uv_map(region,in.region);
        u.blurMap=EffectBuildContext::uv_map(region,blur.region);
        u.base.texel={region.x,region.y,region.w,region.h};u.dimensions={in.region.x,in.region.y,in.region.w,in.region.h};
        u.base.p0={f32(e.e(0)),e.f(1),e.f(2)*kDeg2Rad,e.f(4)*.01f};
        u.base.p1={e.p2(7).x,e.p2(7).y,e.b(8)?1.f:0.f,e.b(9)?-1.f:1.f};
        u.base.p2={e.b(10)?1.f:0.f,e.f(11)*.01f,e.f(12)*.01f,f32(e.e(13))};
        const u32 steps=e.e(14)==0?24:(e.e(14)==1?64:128);
        u.base.p3={f32(steps),e.f(15)*.01f,e.f(16),e.f(3)};u.base.color=e.color(5);u.farColor=e.color(6);
        u32 w,h;c.region_size(region,in.texel_scale_x(),w,h);out={c.texture("shadow-studio",w,h),region,w,h};
        if(c.fullscreen_pass("shadow-studio",PassStage::Effects,out.texture,ShaderId::effects_shadow_studio_frag,
            {{in.texture,{},CommonSampler::LinearBorder},{blur.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
} // namespace
void register_studio_light_effects(EffectRegistry& r){(void)r.add(std::make_unique<DeepGlow2>());(void)r.add(std::make_unique<ShadowStudio3>());}
} // namespace aurea::builtin
