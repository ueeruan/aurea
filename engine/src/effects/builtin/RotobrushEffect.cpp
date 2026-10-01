#include "BuiltinEffects.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include <algorithm>
namespace aurea::builtin {
namespace {
class Rotobrush final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRotobrush,"Rotobrush IA","Keying",EffectClass::Neighborhood};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("threshold","Limite",50,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("feather","Suavidade da borda",10,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("expand","Ajuste do recorte",0,-49,49,kParamAnimatable|kParamPercent,"%");
        p.add_bool("invert","Inverter recorte",false);
        p.add_float("mix","Mistura",100,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_bool("matte","Mostrar máscara",false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {return e.f(4)<=0;}
    void pipelines(std::vector<PipelineKey>& p,SurfaceFormat work) const override {p.push_back(PipelineKey::fullscreen(ShaderId::effects_rotobrush_frag,work));}
    void resolve_resources(EffectEval& e) const noexcept override {
        if(!e.resources)return;DepthMapRequest req;req.host=e.layer;req.instance=e.instance;req.localTime=e.localTime;req.foreground=true;req.smoothing=0;
        const auto r=e.resources->depth_map(req);e.aux=r.texture;e.auxInfo.w=r.failed?1.f:0.f;
        e.foregroundSourceTimeUs=r.sourceTimeUs;
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32,LayerImage& out) const override {
        if(e.auxInfo.w>0) return Status{Errc::MediaSourceMissing,"Rotobrush: modelo ou quadro indisponivel"};
        auto u=base_uniforms(input);
        u.p0={std::clamp((e.f(0)-e.f(2))*.01f,0.f,1.f),std::max(.001f,e.f(1)*.005f),e.b(3)?1.f:0.f,e.f(4)*.01f};
        u.p1={e.b(5)?1.f:0.f,e.aux.valid()?1.f:0.f,0,0};
        u.p2={input.region.x,input.region.y,input.region.w,input.region.h};
        u.p3={e.placement?static_cast<f32>(e.placement->layerWidth):input.region.w,e.placement?static_cast<f32>(e.placement->layerHeight):input.region.h,0,0};
        out=input;out.texture=ctx.texture("Rotobrush",input.width,input.height);
        const PassTexture mask=e.aux.valid()?PassTexture{{},e.aux,CommonSampler::LinearClamp}:PassTexture{input.texture,{},CommonSampler::LinearClamp};
        if(ctx.fullscreen_pass("Rotobrush",PassStage::Effects,out.texture,ShaderId::effects_rotobrush_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp},mask},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
}
void register_rotobrush_effect(EffectRegistry& r){(void)r.add(std::make_unique<Rotobrush>());}
}
