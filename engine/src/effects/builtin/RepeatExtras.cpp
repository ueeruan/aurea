#include "BuiltinEffects.hpp"
#include "aurea/effects/ShakeMotion.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace aurea::builtin {
namespace {
constexpr u32 kCopyLimit=64;
struct RepeatUniforms {
    Vec4 region{}, input{};
    struct Rows { Vec4 x{}, y{}; } copies[kCopyLimit];
};
static_assert(sizeof(RepeatUniforms)==32+kCopyLimit*32);
enum RepeatKind { Basic, Linear, Grid, Radial, Path, Scatter };
class Repeater final : public Effect {
    RepeatKind kind_;
public:
    explicit Repeater(RepeatKind kind):kind_(kind){}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[]={
            {"aurea.repeat.basic","Repeat (Basic)","Distorcer",EffectClass::Domain},
            {"aurea.repeat.linear","Linear Repeat","Distorcer",EffectClass::Domain},
            {"aurea.repeat.grid","Grid Repeat","Distorcer",EffectClass::Domain},
            {"aurea.repeat.radial","Radial Repeat","Distorcer",EffectClass::Domain},
            {"aurea.repeat.path","Repeat Along Path","Distorcer",EffectClass::Domain},
            {"aurea.repeat.scatter","Scatter Repeat","Distorcer",EffectClass::Domain}
        };return infos[kind_];
    }
    enum :u32{Count,Spacing,Angle,Scale,Opacity,Columns,Radius,Arc,Seed,Orient,Phase};
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("count","Quantidade",kind_==Grid?9:5,1,kCopyLimit);
        p.add_point2("spacing",kind_==Scatter?"Dispersão":"Espaçamento",{100,kind_==Grid||kind_==Scatter?100.f:0.f},-4000,4000,
            kParamAnimatable|kParamPixels|((kind_==Radial||kind_==Path)?kParamHidden:0));
        p.add_angle("angle","Rotação por cópia",0);
        p.add_float("scale","Escala por cópia",100,10,200,kParamAnimatable|kParamPercent,"%");
        p.add_float("end_opacity","Opacidade final",100,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_int("columns","Colunas",3,1,kCopyLimit,kind_==Grid?kParamAnimatable:kParamHidden);
        p.add_float("radius","Raio",150,0,4000,kParamAnimatable|kParamPixels|(kind_==Radial?0:kParamHidden),"px");
        p.add_float("arc","Arco",360,0,360,kind_==Radial?kParamAnimatable:kParamHidden,"°");
        p.add_int("seed","Semente",1,0,999999,kind_==Scatter?kParamAnimatable:kParamHidden);
        p.add_bool("orient","Orientar as cópias",true,kind_==Radial||kind_==Path?kParamAnimatable:kParamHidden);
        p.add_float("phase","Deslocamento no caminho",0,-1,1,kind_==Path?kParamAnimatable:kParamHidden);
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        if(kind_==Path&&e.resources) e.pathSamples=std::make_shared<const std::vector<Vec4>>(
            e.resources->repeat_path(e.layer,std::clamp(e.value(Count).as_int(),1,int(kCopyLimit)),e.f(Phase)));
    }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat f) const override {
        auto p=PipelineKey::fullscreen(ShaderId::effects_repeat_copies_frag,f);
        p.vertex=ShaderId::effects_repeat_copies_vert;
        p.blendEnabled=true;
        out.push_back(p);
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,float margin,LayerImage& out) const override {
        const u32 count=std::clamp(e.value(Count).as_int(),1,int(kCopyLimit));
        if(kind_==Path&&(!e.pathSamples||e.pathSamples->empty())) { out=in;return OkStatus; }
        const Vec2 pivot{e.placement?e.placement->layerWidth*.5f:in.region.w*.5f,e.placement?e.placement->layerHeight*.5f:in.region.h*.5f};
        RepeatUniforms u; u.input={in.region.x,in.region.y,in.region.w,in.region.h};
        float minX=1e30f,minY=1e30f,maxX=-1e30f,maxY=-1e30f;
        const Vec2 step=e.p2(Spacing); const u32 columns=std::clamp(e.value(Columns).as_int(),1,int(kCopyLimit));
        const u32 seed=u32(std::max(0,e.value(Seed).as_int()));
        Vec2 accumulated{};
        for(u32 i=0;i<count;++i) {
            const float fraction=count>1?float(i)/float(count-1):0;
            Vec2 offset{}; float angle=e.f(Angle)*i*kDeg2Rad;
            float scale=std::clamp(std::pow(std::clamp(e.f(Scale)*.01f,.1f,2.f),float(i)),.001f,1000.f);
            if(kind_==Grid)offset={step.x*float(i%columns),step.y*float(i/columns)};
            else if(kind_==Radial) {
                const float theta=e.f(Arc)*kDeg2Rad*float(i)/float(e.f(Arc)>=359.999f?count:std::max(1u,count-1));
                offset={std::cos(theta)*e.f(Radius),std::sin(theta)*e.f(Radius)};
                if(e.b(Orient))angle+=theta;
            } else if(kind_==Scatter) {
                offset={step.x*shake::random(seed,i,0),step.y*shake::random(seed,i,1)};
                angle=e.f(Angle)*kDeg2Rad*shake::random(seed,i,2);
                scale=std::max(.001f,1+(e.f(Scale)*.01f-1)*(shake::random(seed,i,3)+1)*.5f);
            } else if(kind_==Path) {
                const auto pos=(*e.pathSamples)[std::min<usize>(i,e.pathSamples->size()-1)];
                offset={pos.x-pivot.x,pos.y-pivot.y}; if(e.b(Orient))angle+=pos.z;
            } else if(kind_==Basic) {
                offset=accumulated;
                const float c=std::cos(angle)*scale,s=std::sin(angle)*scale;
                accumulated=accumulated+Vec2{c*step.x-s*step.y,s*step.x+c*step.y};
            } else offset={step.x*i,step.y*i};
            const float c=std::cos(angle)*scale,s=std::sin(angle)*scale;
            const float x=pivot.x+offset.x-c*pivot.x+s*pivot.y,y=pivot.y+offset.y-s*pivot.x-c*pivot.y;
            u.copies[i]={{c,-s,x,1+(e.f(Opacity)*.01f-1)*fraction},{s,c,y,0}};
            for(float px:{in.region.x,in.region.x+in.region.w})for(float py:{in.region.y,in.region.y+in.region.h}) {
                const float tx=c*px-s*py+x,ty=s*px+c*py+y;
                minX=std::min(minX,tx);minY=std::min(minY,ty);maxX=std::max(maxX,tx);maxY=std::max(maxY,ty);
            }
        }
        const Rect region=spread_region({minX,minY,maxX-minX,maxY-minY},0,0,e.placement,margin);
        u.region={region.x,region.y,region.w,region.h}; u32 w,h;ctx.region_size(region,in.texel_scale_x(),w,h);
        out={ctx.texture(info().key,w,h),region,w,h};
        if(ctx.geometry_pass(info().key,PassStage::Effects,out.texture,ShaderId::effects_repeat_copies_vert,ShaderId::effects_repeat_copies_frag,
            {PassTexture{in.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u),count*6,false,true)==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
}
void register_repeat_extras(EffectRegistry& r) { for(int k=Basic;k<=Scatter;++k)(void)r.add(std::make_unique<Repeater>(RepeatKind(k))); }
}
