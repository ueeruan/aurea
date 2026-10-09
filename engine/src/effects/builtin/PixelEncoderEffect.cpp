// Independently drawn procedural cell patterns; no vendor pattern assets.
#include "BuiltinEffects.hpp"
#include <algorithm>

namespace aurea::builtin {
namespace {
class PixelEncoder final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.pixel_encoder","Pixel Encoder","Estilizar",EffectClass::Neighborhood};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const patterns[]={"Detalhe alto","Números e letras","Linhas horizontais","Pontos","Linhas verticais","Grunge","Anéis","Sólido","Diagonal esquerda","Diagonal direita"};
        static const char* const tones[]={"Luminância Rec. 601","Média RGB","Vermelho","Verde","Azul","Luminância Rec. 709"};
        p.add_point2("scale","Tamanho das células XY",{12,12},1,256,kParamAnimatable | kParamPixels);
        p.add_bool("lock_scale","Vincular tamanho XY",true);
        p.add_enum("pattern","Padrão",patterns,10,0);
        p.add_float("brightness","Brilho",0,-100,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("contrast","Contraste",100,0,400,kParamAnimatable | kParamPercent,"%");
        p.add_color("foreground","Cor dos pixels",{1,1,1,1});
        p.add_color("background","Cor de fundo",{0,0,0,1});
        p.add_bool("original_colors","Cores originais",false);
        p.add_bool("transparent_background","Fundo transparente",false);
        p.add_float("chaos","Intensidade do caos",0,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_int("seed","Semente",1,0,65535);
        p.add_bool("random_pattern","Padrão aleatório por célula",false);
        p.add_int("shading_detail","Níveis de detalhe",8,1,16);
        p.add_point2("offset","Deslocamento",{0,0},-2000,2000,kParamAnimatable | kParamPixels);
        p.add_bool("invert","Inverter",false);
        p.add_enum("tone_detection","Detecção de tons",tones,6,0);
    }
    bool needs_full_input() const noexcept override { return true; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[0] = ParamValue::vec2(6, 6);
        values[2] = ParamValue::scalar(1);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat format) const override {out.push_back(PipelineKey::fullscreen(ShaderId::effects_pixel_encoder_frag,format));}
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32,LayerImage& out) const override {
        struct Uniforms {EffectUniforms base;Vec4 background,region;} u{};
        static_assert(sizeof(Uniforms)==144);
        u.base=base_uniforms(input);
        const Vec2 scale=e.p2(0),offset=e.p2(13);
        u.base.p0={std::clamp(scale.x,1.f,256.f),std::clamp(e.b(1)?scale.x:scale.y,1.f,256.f),e.f(3)*.01f,e.f(4)*.01f};
        u.base.p1={static_cast<f32>(e.e(2)),static_cast<f32>(std::clamp(e.value(12).as_int(),1,16)),e.f(9)*.01f,static_cast<f32>(e.value(10).as_int())};
        u.base.p2={offset.x,offset.y,static_cast<f32>(e.e(15)),e.b(7)?1.f:0.f};
        u.base.p3={e.b(14)?1.f:0.f,e.b(8)?1.f:0.f,e.b(11)?1.f:0.f,0};
        u.base.color=e.color(5);u.background=e.color(6);
        u.region={input.region.x,input.region.y,input.region.w,input.region.h};
        out=input;out.texture=ctx.texture("pixel-encoder",input.width,input.height);
        return ctx.fullscreen_pass("pixel-encoder",PassStage::Effects,out.texture,ShaderId::effects_pixel_encoder_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex ? Status{Errc::PipelineCompileFailed}:OkStatus;
    }
};
}
void register_pixel_encoder(EffectRegistry& r) {(void)r.add(std::make_unique<PixelEncoder>());}
}
