// Eight independent one-sided rays, with three editable five-stop color maps.
// Original kernels and shimmer field. Vendor-equivalent calibration is pending.
#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
class Starglow final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.light.starglow","Starglow","Luz",EffectClass::Neighborhood};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const channels[]={"Luminosidade","Luminância","Vermelho","Verde","Azul","Alfa"};
        static const char* const blends[]={"Tela","Adicionar","Normal","Máximo"};
        static const char* const maps[]={"Mapa 1","Mapa 2","Mapa 3"};
        static const char* const stops[]={"1 cor","3 cores","5 cores"};
        p.add_float("length","Comprimento dos raios",100,0,1000,kParamAnimatable | kParamPixels,"px");
        p.add_float("boost","Intensidade da luz",1,0,10);
        p.add_float("threshold","Limiar",70,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("threshold_softness","Suavidade do limiar",10,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_enum("channel","Canal de entrada",channels,6,1);
        p.add_bool("circle_mask","Máscara circular",false);
        p.add_point2("mask_center","Centro da máscara",{.5f,.5f},-1,2,kParamAnimatable | kParamRelative);
        p.add_float("mask_radius","Raio da máscara",100,0,200,kParamAnimatable | kParamPercent,"%");
        p.add_float("mask_feather","Suavidade da máscara",10,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("shimmer_amount","Cintilação",50,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("shimmer_detail","Detalhe da cintilação",4,.1f,40);
        p.add_float("shimmer_phase","Fase da cintilação",0,-100,100,kParamAnimatable,"voltas");
        p.add_bool("shimmer_loop","Repetir cintilação",false);
        p.add_float("shimmer_loop_length","Duração do ciclo",1,.01f,100,kParamAnimatable,"voltas");
        p.add_float("source_opacity","Opacidade da fonte",100,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("glow_opacity","Opacidade do brilho",100,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_enum("blend","Mistura do brilho",blends,4,0);
        static const char* const lengthIds[]={"right_length","down_right_length","down_length","down_left_length","left_length","up_left_length","up_length","up_right_length"};
        static const char* const lengthLabels[]={"Raio à direita","Raio abaixo à direita","Raio abaixo","Raio abaixo à esquerda","Raio à esquerda","Raio acima à esquerda","Raio acima","Raio acima à direita"};
        for(u32 i=0;i<8;++i) p.add_float(lengthIds[i],lengthLabels[i],1,0,4);
        static const char* const mapIds[]={"right_map","down_right_map","down_map","down_left_map","left_map","up_left_map","up_map","up_right_map"};
        static const char* const mapLabels[]={"Cores à direita","Cores abaixo à direita","Cores abaixo","Cores abaixo à esquerda","Cores à esquerda","Cores acima à esquerda","Cores acima","Cores acima à direita"};
        for(u32 i=0;i<8;++i) p.add_enum(mapIds[i],mapLabels[i],maps,3,i%3);
        static const char* const typeIds[]={"map1_type","map2_type","map3_type"};
        static const char* const typeLabels[]={"Cores do mapa 1","Cores do mapa 2","Cores do mapa 3"};
        static const char* const colorIds[]={"map1_color1","map1_color2","map1_color3","map1_color4","map1_color5","map2_color1","map2_color2","map2_color3","map2_color4","map2_color5","map3_color1","map3_color2","map3_color3","map3_color4","map3_color5"};
        static const char* const colorLabels[]={"Mapa 1: cor 1","Mapa 1: cor 2","Mapa 1: cor 3","Mapa 1: cor 4","Mapa 1: cor 5","Mapa 2: cor 1","Mapa 2: cor 2","Mapa 2: cor 3","Mapa 2: cor 4","Mapa 2: cor 5","Mapa 3: cor 1","Mapa 3: cor 2","Mapa 3: cor 3","Mapa 3: cor 4","Mapa 3: cor 5"};
        const Vec4 defaults[15]={{1,1,1,1},{1,.7f,.2f,1},{1,.1f,.1f,1},{.5f,0,.3f,1},{.1f,0,.2f,1},{1,1,1,1},{.2f,.8f,1,1},{.1f,.2f,1,1},{.3f,0,.7f,1},{.1f,0,.3f,1},{1,1,1,1},{.6f,1,.3f,1},{.1f,1,.2f,1},{0,.4f,.3f,1},{0,.1f,.1f,1}};
        for(u32 map=0;map<3;++map) {
            p.add_enum(typeIds[map],typeLabels[map],stops,3,2);
            for(u32 stop=0;stop<5;++stop) p.add_color(colorIds[map*5+stop],colorLabels[map*5+stop],defaults[map*5+stop]);
        }
    }
    bool needs_full_input() const noexcept override {return true;}
    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[0] = ParamValue::scalar(36);
        values[1] = ParamValue::scalar(1.5f);
        values[2] = ParamValue::scalar(82);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {return (e.f(0)<=0||e.f(1)<=0||e.f(15)<=0)&&e.f(14)>=100;}
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat format) const override {out.push_back(PipelineKey::fullscreen(ShaderId::effects_starglow_frag,format));}
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32 margin,LayerImage& out) const override {
        struct Uniforms {EffectUniforms base;Vec4 rays[8],colors[15],mapTypes,region,size,circle;} u{};
        static_assert(sizeof(Uniforms)==544);
        u.base=base_uniforms(input);
        u.base.p0={std::clamp(e.f(0),0.f,1000.f),std::max(0.f,e.f(1)),e.f(2)*.01f,e.f(3)*.01f};
        const f32 phase=e.b(12) ? std::fmod(e.f(11),std::max(.01f,e.f(13)))/std::max(.01f,e.f(13)) : e.f(11);
        u.base.p1={e.f(9)*.01f,e.f(10),phase,0};
        u.base.p2={e.f(14)*.01f,e.f(15)*.01f,static_cast<f32>(e.e(16)),static_cast<f32>(e.e(4))};
        const f32 w=e.placement?static_cast<f32>(e.placement->layerWidth):input.region.w;
        const f32 h=e.placement?static_cast<f32>(e.placement->layerHeight):input.region.h;
        const Vec2 center=e.p2(6);
        u.size={w,h,0,0};u.circle={center.x*w,center.y*h,e.f(7)*.005f*h,e.f(8)*.005f*h};
        f32 reach=0;
        for(u32 i=0;i<8;++i) {
            const f32 angle=static_cast<f32>(i)*kPi*.25f;
            const f32 length=u.base.p0.x*std::clamp(e.f(17+i),0.f,4.f);
            u.rays[i]={std::cos(angle),std::sin(angle),length,static_cast<f32>(e.e(25+i))};
            reach=std::max(reach,length);
        }
        for(u32 map=0;map<3;++map) for(u32 stop=0;stop<5;++stop) u.colors[map*5+stop]=e.color(34+map*6+stop);
        u.mapTypes={static_cast<f32>(e.e(33)),static_cast<f32>(e.e(39)),static_cast<f32>(e.e(45)),0};
        const Rect region=spread_region(input.region,reach,reach,e.placement,margin);
        u.region={region.x,region.y,region.w,region.h};
        u.base.uvMap=EffectBuildContext::uv_map(region,input.region);
        u.base.p3={0,0,e.b(5)?1.f:0.f,0};
        u32 width=0,height=0;ctx.region_size(region,input.texel_scale_x()*.5f,width,height);
        LayerImage bright{ctx.texture("starglow-bright",width,height),region,width,height};
        auto pass=[&](const char* name,FGTexture target,FGTexture primary,FGTexture second)->Status {
            return ctx.fullscreen_pass(name,PassStage::Effects,target,ShaderId::effects_starglow_frag,
                {PassTexture{primary,{},CommonSampler::LinearBorder},PassTexture{second,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex ? Status{Errc::PipelineCompileFailed}:OkStatus;
        };
        if(const Status s=pass("starglow-bright",bright.texture,input.texture,input.texture);!s.ok()) return s;
        LayerImage sum=bright;
        for(u32 ray=0;ray<8;++ray) {
            u.base.p3.x=1;u.base.p3.y=static_cast<f32>(ray);
            LayerImage next{ctx.texture("starglow-ray",width,height),region,width,height};
            if(const Status s=pass("starglow-ray",next.texture,bright.texture,sum.texture);!s.ok()) return s;
            sum=next;
        }
        u.base.p3.x=2;
        ctx.region_size(region,input.texel_scale_x(),width,height);
        out={ctx.texture("starglow-composite",width,height),region,width,height};
        return pass("starglow-composite",out.texture,input.texture,sum.texture);
    }
};
}
void register_starglow(EffectRegistry& r) {(void)r.add(std::make_unique<Starglow>());}
}
