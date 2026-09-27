#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>
namespace aurea::builtin {
namespace {
// GPU color-connected regions: no CPU readback, history-dependent IDs or worker
// races. A low-resolution label graph is compacted by an exact integer prefix
// scan (two base-128 channels remain exact in RGBA16F).
class Tracery final : public Effect {
public:
    const EffectInfo& info() const noexcept override {static const EffectInfo i{effect_keys::kTracery,"Tracery","Generate",EffectClass::Global};return i;}
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("key_color","Key Color",{1,1,1,1});
        p.add_float("threshold","Color Tolerance",20,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("minimum_area","Minimum Area",.05f,0,25,kParamAnimatable|kParamPercent,"%");
        p.add_int("targets","Maximum Regions",24,1,64);
        p.add_float("stroke","Box Stroke",1.5f,0,20,kParamAnimatable|kParamPixels,"px");
        p.add_color("overlay_color","Overlay Color",{.02f,.8f,1,1});
        p.add_float("marker_size","Marker Size",4,0,50,kParamAnimatable|kParamPixels,"px");
        static const char* lines[]={"None","Sequential","Star","Full Mesh (16 regions)","Minimum Spanning Tree"};p.add_enum("connections","Connections",lines,5,1);
        p.add_float("fill","Box Fill",0,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("corners","Corner Length",25,0,100,kParamAnimatable|kParamPercent,"%");
        static const char* markers[]={"Dot","Plus","Cross","None"};p.add_enum("marker","Marker",markers,4,1);
        p.add_bool("arrows","Arrows",false);
        static const char* labels[]={"None","Index","Coordinates"};p.add_enum("labels","Labels",labels,3,1);
        p.add_bool("overlay_only","Overlay Only",false);
        p.add_bool("mask","Show Detection Mask",false);
        static const char* quality[]={"Draft (64)","Balanced (96)","High (128)"};p.add_enum("quality","Detection Resolution",quality,3,1);
        p.add_float("blur","Detection Blur",0,0,4,kParamAnimatable|kParamPixels,"px");
        p.add_float("padding","Box Padding",2,0,100,kParamAnimatable|kParamPixels,"px");
        p.add_float("opacity","Overlay Opacity",100,0,100,kParamAnimatable|kParamPercent,"%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {return !e.b(13)&&!e.b(14)&&e.f(18)<=0;}
    void pipelines(std::vector<PipelineKey>& v,SurfaceFormat f) const override {v.push_back(PipelineKey::fullscreen(ShaderId::effects_tracery_detect_frag,f));v.push_back(PipelineKey::fullscreen(ShaderId::effects_tracery_overlay_frag,f));}
    Status build(EffectBuildContext& c,const EffectEval& e,const LayerImage& in,f32,LayerImage& out) const override {
        const u32 longest=e.e(15)==0?64:(e.e(15)==1?96:128);
        const f32 ratio=in.region.w/std::max(1.f,in.region.h);
        const u32 w=std::max(8u,u32(std::round(longest*std::min(ratio,1.f))));
        const u32 h=std::max(8u,u32(std::round(longest/std::max(ratio,1.f))));
        const u32 limit=std::min(e.e(7)==3?16u:64u,std::max(1u,e.e(3)));
        EffectUniforms u{};u.texel={f32(w),f32(h),in.region.w,in.region.h};
        u.p0={e.f(1)*.01f,e.f(16),e.f(2)*.01f,f32(limit)};u.color=e.color(0);
        const auto pass=[&](const char* name,u32 stage,u32 width,u32 height,std::initializer_list<PassTexture> inputs,FGTexture& target)->Status {
            u.p3.x=f32(stage);target=c.texture(name,width,height);
            const auto a=*inputs.begin(),b=inputs.size()>1?*(inputs.begin()+1):a;
            if(c.fullscreen_pass(name,PassStage::Effects,target,ShaderId::effects_tracery_detect_frag,{a,b},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
            return OkStatus;
        };
        FGTexture labels,next;
        if(auto s=pass("tracery-key",0,w,h,{{in.texture,{},CommonSampler::LinearClamp}},labels);!s.ok())return s;
        // Hook to a lower neighboring root and compress parent pointers. Labels
        // never cross an unselected pixel, so separate color regions stay apart.
        for(u32 j=0;j<32;++j){if(auto s=pass("tracery-connect",1,w,h,{{labels,{},CommonSampler::NearestClamp}},next);!s.ok())return s;labels=next;}
        FGTexture prefix;
        if(auto s=pass("tracery-roots",2,w,h,{{labels,{},CommonSampler::NearestClamp}},prefix);!s.ok())return s;
        for(u32 step=1;step<w*h;step<<=1){u.p3.y=f32(step);if(auto s=pass("tracery-scan",3,w,h,{{prefix,{},CommonSampler::NearestClamp}},next);!s.ok())return s;prefix=next;}
        FGTexture roots,rows,boxes,edges;
        if(auto s=pass("tracery-index",4,limit,1,{{prefix,{},CommonSampler::NearestClamp}},roots);!s.ok())return s;
        if(auto s=pass("tracery-row-bounds",5,limit,h,{{labels,{},CommonSampler::NearestClamp},{roots,{},CommonSampler::NearestClamp}},rows);!s.ok())return s;
        if(auto s=pass("tracery-boxes",6,limit,2,{{rows,{},CommonSampler::NearestClamp}},boxes);!s.ok())return s;
        u.p1.x=f32(e.e(7));
        if(auto s=pass("tracery-connections",7,limit,1,{{boxes,{},CommonSampler::NearestClamp}},edges);!s.ok())return s;
        struct Uniform {EffectUniforms base;Vec4 extra;Vec4 display;} data{};
        data.base.texel={in.region.w,in.region.h,f32(w),f32(h)};
        data.base.p0={f32(limit),e.f(4),e.f(6),f32(e.e(7))};
        data.base.p1={e.f(8)*.01f,e.f(9)*.01f,f32(e.e(10)),e.b(11)?1.f:0.f};
        data.base.p2={f32(e.e(12)),e.b(13)?1.f:0.f,e.b(14)?1.f:0.f,e.f(17)};
        data.base.color=e.color(5);data.extra={e.f(18)*.01f,in.texel_scale_x(),in.texel_scale_y(),0};
        out={c.texture("tracery-overlay",in.width,in.height),in.region,in.width,in.height};
        if(c.fullscreen_pass("tracery-overlay",PassStage::Effects,out.texture,ShaderId::effects_tracery_overlay_frag,
            {{in.texture,{},CommonSampler::LinearClamp},{boxes,{},CommonSampler::NearestClamp},{edges,{},CommonSampler::NearestClamp},{labels,{},CommonSampler::NearestClamp}},&data,sizeof(data))==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
}
void register_tracery_effect(EffectRegistry& r){(void)r.add(std::make_unique<Tracery>());}
}
