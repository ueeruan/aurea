// Independently authored transform displacement; public Displacer Pro reference.
// Transform integration and filter weights are AUREA estimates, not vendor code.
#include "BuiltinEffects.hpp"
#include <algorithm>
namespace aurea::builtin {
namespace {
class DisplaceTransform final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.displace_transform","Displace","Distorcer",EffectClass::Domain};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const channels[]={"Luminância","Vermelho","Verde","Azul","Alfa"};
        static const char* const edges[]={"Transparente","Repetir","Refletir","Repetir pixels"};
        p.add_layer_ref("map","Mapa de deformação");
        p.add_point2("translation","Deslocamento XY",{64,0},-500,500,kParamAnimatable | kParamPixels);
        p.add_point2("scale","Deformação de escala XY",{0,0},-200,200,kParamAnimatable | kParamPercent);
        p.add_angle("rotation","Deformação de rotação",0);
        p.add_float("strength","Intensidade",100,-200,200,kParamAnimatable | kParamPercent,"%");
        p.add_point2("center","Centro",{.5f,.5f},0,1,kParamAnimatable | kParamRelative);
        p.add_enum("x_channel","Canal horizontal",channels,5,1);
        p.add_enum("y_channel","Canal vertical",channels,5,2);
        p.add_enum("transform_channel","Canal de escala e rotação",channels,5,0);
        p.add_float("neutral","Valor neutro",50,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("map_blur","Suavidade do mapa",0,0,100,kParamAnimatable | kParamPixels,"px");
        p.add_point2("map_offset","Deslocamento do mapa",{0,0},-1000,1000,kParamAnimatable | kParamPixels);
        p.add_float("chromatic","Aberração cromática",0,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_int("iterations","Iterações",1,1,16);
        p.add_enum("edges","Bordas",edges,4,2);
        p.add_bool("filter","Filtragem adaptativa",true);
        p.add_float("mix","Mistura",100,0,100,kParamAnimatable | kParamPercent,"%");
    }
    i32 input_layer_param() const noexcept override {return 0;}
    bool needs_full_input() const noexcept override {return true;}
    bool is_identity(const EffectEval& e) const noexcept override {return e.f(4)==0||e.f(16)<=0;}
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_displace_transform_frag,format));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag,format));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32,LayerImage& out) const override {
        const auto ref=e.value(0).ref;
        const LayerImage* external=ref?ctx.layer_input(ref):nullptr;
        if(ref&&!external) {out=input;return OkStatus;}
        LayerImage map=external?*external:input;
        if(e.f(10)>0) {
            BlurRequest request;request.sigmaX=request.sigmaY=e.f(10);request.repeatEdges=true;request.outRegion=map.region;request.label="displace-map-blur";
            LayerImage blurred;const Status status=build_gaussian(ctx,map,request,blurred);
            if(!status.ok()) return status;map=blurred;
        }
        struct Uniforms {EffectUniforms base;Vec4 mapRegion,region,size,options;Mat4 compFromLayer;} u{};
        static_assert(sizeof(Uniforms)==240);
        u.base=base_uniforms(input);
        const auto translation=e.p2(1),scale=e.p2(2),center=e.p2(5),offset=e.p2(11);
        u.base.p0={translation.x,translation.y,scale.x*.01f,scale.y*.01f};
        u.base.p1={e.f(3)*kDeg2Rad,e.f(4)*.01f,e.f(9)*.01f,e.f(12)*.01f};
        u.base.p2={static_cast<f32>(e.e(6)),static_cast<f32>(e.e(7)),static_cast<f32>(e.e(8)),static_cast<f32>(e.value(13).as_int())};
        u.base.p3={center.x,center.y,offset.x,offset.y};
        u.mapRegion={map.region.x,map.region.y,map.region.w,map.region.h};
        u.region={input.region.x,input.region.y,input.region.w,input.region.h};
        u.size={e.placement?static_cast<f32>(e.placement->layerWidth):input.region.w,e.placement?static_cast<f32>(e.placement->layerHeight):input.region.h,external?1.f:0.f,0};
        u.options={static_cast<f32>(e.e(14)),e.b(15)?1.f:0.f,e.f(16)*.01f,0};
        u.compFromLayer=e.placement?e.placement->compFromLayer:Mat4::identity();
        out=input;out.texture=ctx.texture("displace-transform",input.width,input.height);
        return ctx.fullscreen_pass("displace-transform",PassStage::Effects,out.texture,ShaderId::effects_displace_transform_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp},PassTexture{map.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex?Status{Errc::PipelineCompileFailed}:OkStatus;
    }
};
}
void register_displace_transform(EffectRegistry& r) {(void)r.add(std::make_unique<DisplaceTransform>());}
}
