#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
f32 optical_value(f32 v,f32 fallback,f32 lo,f32 hi) noexcept {
    return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
}
Vec4 optical_color(Vec4 c) noexcept {
    return {Color::srgb_to_linear(optical_value(c.x,1,0,1)),Color::srgb_to_linear(optical_value(c.y,1,0,1)),
            Color::srgb_to_linear(optical_value(c.z,1,0,1)),optical_value(c.w,1,0,1)};
}
class LiquidGlass final:public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.liquid_glass","Vidro líquido","Distorcer",EffectClass::Domain};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("intensity","Intensidade",100,0,200,kParamAnimatable|kParamPercent,"%");
        p.add_point2("center","Centro",{.5f,.5f},-1,2,kParamAnimatable|kParamRelative);
        p.add_float("width","Largura",60,1,200,kParamAnimatable|kParamPercent,"%");
        p.add_float("height","Altura",50,1,200,kParamAnimatable|kParamPercent,"%");
        p.add_float("corner_radius","Raio dos cantos",32,0,1000,kParamAnimatable|kParamPixels,"px");
        p.add_float("refraction","Refração",24,-200,200,kParamAnimatable|kParamPixels,"px");
        p.add_float("bevel","Espessura da borda",24,1,300,kParamAnimatable|kParamPixels,"px");
        p.add_float("dispersion","Dispersão cromática",1.5f,0,30,kParamAnimatable|kParamPixels,"px");
        p.add_float("frost","Desfoque do vidro",1.5f,0,64,kParamAnimatable|kParamPixels,"px");
        p.add_color("tint","Cor do vidro",{.75f,.9f,1,.08f});
        p.add_float("rim_light","Brilho da borda",50,0,200,kParamAnimatable|kParamPercent,"%");
        p.add_angle("light_direction","Direção da luz",315);
        p.add_float("mix","Mistura",100,0,100,kParamAnimatable|kParamPercent,"%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {return e.f(0)<=0||e.f(12)<=0;}
    bool needs_full_input() const noexcept override {return true;}
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_liquid_glass_frag,work));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,f32,LayerImage& out) const override {
        const auto f=[&](u32 k,f32 lo,f32 hi,f32 fallback=0){return optical_value(e.f(k),fallback,lo,hi);};
        const f32 w=e.placement?static_cast<f32>(e.placement->layerWidth):in.region.w;
        const f32 h=e.placement?static_cast<f32>(e.placement->layerHeight):in.region.h;
        const auto center=e.p2(1);EffectUniforms u{};
        u.uvMap={in.region.x,in.region.y,in.region.w,in.region.h};
        u.texel={optical_value(center.x,.5f,-1,2)*w,optical_value(center.y,.5f,-1,2)*h,f(2,1,200,60)*.005f*w,f(3,1,200,50)*.005f*h};
        u.p0={f(0,0,200,100)*.01f,f(4,0,1000,32),f(5,-200,200,24),f(6,1,300,24)};
        u.p1={f(7,0,30),f(8,0,64),f(10,0,200,50)*.01f,f(12,0,100,100)*.01f};
        const f32 angle=f(11,-360000,360000,315)*kDeg2Rad;u.p2={std::cos(angle),std::sin(angle),0,0};
        u.color=optical_color(e.color(9));
        out=in;out.texture=ctx.texture("liquid-glass",in.width,in.height);
        return ctx.fullscreen_pass("liquid-glass",PassStage::Effects,out.texture,ShaderId::effects_liquid_glass_frag,
            {PassTexture{in.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex?Status{Errc::PipelineCompileFailed}:OkStatus;
    }
};
class NoiseDissolve final:public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.transition.noise_dissolve","Dissolver com ruído","Transição",EffectClass::Neighborhood};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion","Conclusão",50,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("scale","Tamanho do ruído",32,1,512,kParamAnimatable|kParamPixels,"px");
        p.add_int("detail","Detalhe",3,1,4);
        p.add_float("softness","Suavidade",5,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_angle("direction","Direção",0);
        p.add_float("directionality","Influência da direção",100,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_int("seed","Semente",1,0,65535);
        p.add_float("evolution","Evolução",0,-1000,1000,kParamAnimatable);
        p.add_float("edge_width","Largura da borda",5,0,50,kParamAnimatable|kParamPercent,"%");
        p.add_float("edge_intensity","Intensidade da borda",0,0,400,kParamAnimatable|kParamPercent,"%");
        p.add_color("edge_color","Cor da borda",{1,.5f,.05f,1});
        p.add_bool("reverse","Inverter direção",false);
        p.add_float("mix","Mistura",100,0,100,kParamAnimatable|kParamPercent,"%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {return e.f(0)<=0||e.f(12)<=0;}
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_noise_dissolve_frag,work));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,f32,LayerImage& out) const override {
        auto f=[&](u32 k,f32 lo,f32 hi){return optical_value(e.f(k),lo,lo,hi);};
        EffectUniforms u{};u.uvMap={in.region.x,in.region.y,in.region.w,in.region.h};
        u.texel={e.placement?static_cast<f32>(e.placement->layerWidth):in.region.w,e.placement?static_cast<f32>(e.placement->layerHeight):in.region.h,0,0};
        u.p0={f(0,0,100)*.01f,f(1,1,512),static_cast<f32>(std::clamp(e.value(2).as_int(),1,4)),f(3,0,100)*.005f};
        u.p1={f(4,-360000,360000)*kDeg2Rad,f(5,0,100)*.01f,static_cast<f32>(std::clamp(e.value(6).as_int(),0,65535)),f(7,-1000,1000)};
        u.p2={f(8,0,50)*.01f,f(9,0,400)*.01f,f(12,0,100)*.01f,e.b(11)?1.f:0.f};u.color=optical_color(e.color(10));
        out=in;out.texture=ctx.texture("noise-dissolve",in.width,in.height);
        return ctx.fullscreen_pass("noise-dissolve",PassStage::Effects,out.texture,ShaderId::effects_noise_dissolve_frag,
            {PassTexture{in.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex?Status{Errc::PipelineCompileFailed}:OkStatus;
    }
};
class EightBit final:public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.eight_bit","8 bits","Estilizar",EffectClass::Neighborhood};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("pixel_size","Tamanho do pixel",2,1,64,kParamAnimatable|kParamPixels,"px");
        static const char* palettes[]{"RGB 256","RGB limitado","Tons de cinza","4 cores"};p.add_enum("palette","Paleta",palettes,4,3);
        p.add_int("levels","Níveis de cor",4,2,32);
        static const char* dithers[]{"Nenhum","Ordenado","Ruído"};p.add_enum("dither","Pontilhado",dithers,3,1);
        p.add_float("dither_amount","Intensidade do pontilhado",75,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_int("seed","Semente",1,0,65535);
        p.add_color("color_a","Cor 1",{0,0,0,1});p.add_color("color_b","Cor 2",{0,.32f,.6f,1});
        p.add_color("color_c","Cor 3",{0,.7f,1,1});p.add_color("color_d","Cor 4",{1,1,1,1});
        p.add_float("mix","Mistura",100,0,100,kParamAnimatable|kParamPercent,"%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {return e.f(10)<=0;}
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_eight_bit_frag,work));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,f32,LayerImage& out) const override {
        struct Uniforms {EffectUniforms base;Vec4 fourth;};Uniforms u{};static_assert(sizeof(u)==128);
        auto f=[&](u32 k,f32 lo,f32 hi){return optical_value(e.f(k),lo,lo,hi);};
        u.base.uvMap={in.region.x,in.region.y,in.region.w,in.region.h};
        u.base.texel={f(0,1,64),static_cast<f32>(std::clamp(e.value(1).as_int(),0,3)),static_cast<f32>(std::clamp(e.value(2).as_int(),2,32)),static_cast<f32>(std::clamp(e.value(3).as_int(),0,2))};
        u.base.p0={f(4,0,100)*.01f,static_cast<f32>(std::clamp(e.value(5).as_int(),0,65535)),f(10,0,100)*.01f,0};
        u.base.p1=optical_color(e.color(6));u.base.p2=optical_color(e.color(7));u.base.p3=optical_color(e.color(8));u.base.color=optical_color(e.color(9));
        out=in;out.texture=ctx.texture("eight-bit",in.width,in.height);
        return ctx.fullscreen_pass("eight-bit",PassStage::Effects,out.texture,ShaderId::effects_eight_bit_frag,
            {PassTexture{in.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u))==kInvalidIndex?Status{Errc::PipelineCompileFailed}:OkStatus;
    }
};
}
void register_optical_stylize_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<LiquidGlass>());(void)r.add(std::make_unique<NoiseDissolve>());(void)r.add(std::make_unique<EightBit>());
}
}
