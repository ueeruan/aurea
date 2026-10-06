#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
// Independent keyers; colors remain linear, while key distances use encoded RGB.
class KeyExtra final : public Effect {
    u32 mode_;
public:
    explicit KeyExtra(u32 mode):mode_(mode){}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[]={
            {"aurea.key.chroma_basic","Chroma Key (Basic)","Recorte",EffectClass::Domain},
            {"aurea.key.color_luma","Color Luma Key","Recorte",EffectClass::Domain},
            {"aurea.key.solid_matte","Solid Matte","Recorte",EffectClass::Domain},
            {"aurea.transform.offset","Offset","Distorcer",EffectClass::Domain}
        }; return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        if(mode_==3) {
            p.add_point2("offset","Deslocamento",{0,0},-10000,10000,kParamAnimatable|kParamPixels); return;
        }
        p.add_color("color",mode_==2?"Cor do fundo":"Cor-chave",mode_==2?Vec4{0,0,0,1}:Vec4{0,1,0,1});
        if(mode_==2) { p.add_float("opacity","Opacidade",100,0,100,kParamAnimatable|kParamPercent,"%"); return; }
        p.add_float("threshold","Tolerância",.1f,0,1);
        p.add_float("feather","Suavidade",.05f,0,1);
        if(mode_==0) p.add_bool("defringe","Remover contaminação de cor",false);
        else {
            static const char* const channels[]={"RGB","Luminância","Vermelho","Verde","Azul"};
            p.add_enum("channel","Canal",channels,5,1);
        }
        p.add_bool("invert","Inverter",false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return mode_==3?(e.p2(0).x==0&&e.p2(0).y==0):(mode_==2&&(e.f(1)<=0||e.color(0).w<=0));
    }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat f) const override { out.push_back(PipelineKey::fullscreen(ShaderId::effects_key_extra_frag,f)); }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,float,LayerImage& out) const override {
        auto u=base_uniforms(in); u.p0={float(mode_),0,0,0};
        u.p2={in.region.x,in.region.y,in.region.w,in.region.h};
        u.p3={e.placement?float(e.placement->layerWidth):in.region.w,e.placement?float(e.placement->layerHeight):in.region.h,0,0};
        if(mode_==3) { const auto offset=e.p2(0); u.p1={offset.x,offset.y,0,0}; }
        else {
            u.color=e.color(0);
            if(mode_==2) u.p0.y=e.f(1)*.01f;
            else u.p1={e.f(1),e.f(2),mode_==0?(e.b(3)?1.f:0.f):float(e.e(3)),e.b(4)?1.f:0.f};
        }
        return single_pass(ctx,ShaderId::effects_key_extra_frag,in,u,info().key,out);
    }
};
}
void register_keying_extras(EffectRegistry& r) { for(u32 m=0;m<4;++m)(void)r.add(std::make_unique<KeyExtra>(m)); }
}
