// Original gradient-refraction implementation. Public reference: S_Distort.
// Blur/amount calibration and sampling weights remain AUREA estimates.
#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
class LensGradient final : public Effect {
public:
    enum : u32 { Lens, Matte, Amount, Fine, BlurLens, Rotation, Relative,
        WrapX, WrapY, Filter, BlurMatte, InvertMatte, MatteUse, Opacity,
        CropLeft, CropTop, CropRight, CropBottom };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.lens_gradient", "S_Distort", "Distorcer", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const wraps[]={"Sem repetição","Repetir","Refletir"};
        static const char* const mattes[]={"Luminância","Alfa"};
        static const char* const opacity[]={"Normal","Tudo opaco","Pré-multiplicado"};
        p.add_layer_ref("lens", "Camada da lente");
        p.add_layer_ref("matte", "Camada do matte");
        p.add_float("amount", "Intensidade", 1,-10,10); p.typed_range(-1000,1000);
        p.add_bool("fine", "Ajuste fino", false);
        p.add_float("blur_lens", "Suavidade da lente", .4f,0,10);
        p.add_angle("rotate_warp", "Rotação da deformação", 0);
        p.add_point2("amount_rel", "Intensidade relativa XY", {1,1},0,10);
        p.add_enum("wrap_x", "Bordas horizontais", wraps,3,2);
        p.add_enum("wrap_y", "Bordas verticais", wraps,3,2);
        p.add_bool("filter", "Filtragem adaptativa", true);
        p.add_float("blur_matte", "Suavidade do matte", 0,0,10);
        p.add_bool("invert_matte", "Inverter matte", false);
        p.add_enum("matte_use", "Canal do matte", mattes,2,0);
        p.add_enum("opacity", "Processamento de alfa", opacity,3,0);
        p.add_float("crop_left", "Recortar esquerda", 0,0,1000,kParamAnimatable | kParamPixels,"px");
        p.add_float("crop_top", "Recortar topo", 0,0,1000,kParamAnimatable | kParamPixels,"px");
        p.add_float("crop_right", "Recortar direita", 0,0,1000,kParamAnimatable | kParamPixels,"px");
        p.add_float("crop_bottom", "Recortar base", 0,0,1000,kParamAnimatable | kParamPixels,"px");
    }
    i32 input_layer_param() const noexcept override { return Lens; }
    u32 input_layer_count() const noexcept override { return 2; }
    i32 input_layer_param_at(u32 slot) const noexcept override { return slot<2 ? static_cast<i32>(slot) : -1; }
    bool needs_full_input() const noexcept override { return true; }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(Amount)==0 && e.e(Opacity)==0 && e.f(CropLeft)==0 && e.f(CropTop)==0 && e.f(CropRight)==0 && e.f(CropBottom)==0;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_lens_gradient_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32,LayerImage& out) const override {
        const u64 lensRef=e.value(Lens).ref, matteRef=e.value(Matte).ref;
        const LayerImage* external=lensRef ? ctx.layer_input(lensRef) : nullptr;
        if (lensRef && !external) { out=input; return OkStatus; }
        const LayerImage* matte=matteRef ? ctx.layer_input(matteRef) : nullptr;
        // A selected unavailable matte contributes zero, rather than silently
        // applying refraction everywhere. An unselected matte is unrestricted.
        LayerImage lensImage=external ? *external : input;
        LayerImage matteImage=matte ? *matte : input;
        const f32 h=e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        for (u32 pass=0;pass<2;++pass) {
            if (pass==1 && !matte) continue;
            LayerImage& image=pass==0 ? lensImage : matteImage;
            const f32 blur=std::clamp(e.f(pass==0?BlurLens:BlurMatte),0.f,10.f);
            if (blur<=0) continue;
            BlurRequest r;
            r.sigmaX=r.sigmaY=blur*h*.01f;
            r.repeatEdges=true; r.outRegion=image.region; r.label="lens-gradient-blur";
            LayerImage blurred;
            const Status s=build_gaussian(ctx,image,r,blurred);
            if(!s.ok()) return s;
            image=blurred;
        }
        struct Uniforms { EffectUniforms base; Vec4 lensRegion,matteRegion,crop,modes; Mat4 compFromLayer; } u{};
        static_assert(sizeof(Uniforms)==240);
        u.base=base_uniforms(input);
        const Vec2 rel=e.p2(Relative);
        u.base.p0={e.f(Amount)*(e.b(Fine)?.01f:1.f),e.f(Rotation)*kDeg2Rad,rel.x,rel.y};
        u.base.p1={input.region.x,input.region.y,input.region.w,input.region.h};
        u.base.p2={static_cast<f32>(e.e(WrapX)),static_cast<f32>(e.e(WrapY)),e.b(Filter)?1.f:0.f,static_cast<f32>(e.e(Opacity))};
        u.base.p3={external?1.f:0.f,matteRef?(matte?1.f:-1.f):0.f,e.b(InvertMatte)?1.f:0.f,static_cast<f32>(e.e(MatteUse))};
        u.lensRegion={lensImage.region.x,lensImage.region.y,lensImage.region.w,lensImage.region.h};
        u.matteRegion={matteImage.region.x,matteImage.region.y,matteImage.region.w,matteImage.region.h};
        const f32 w=e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        u.crop={std::max(0.f,e.f(CropLeft)),std::max(0.f,e.f(CropTop)),std::max(0.f,w-e.f(CropRight)),std::max(0.f,h-e.f(CropBottom))};
        u.modes={std::max(w,1.f),std::max(h,1.f),0,0};
        u.compFromLayer=e.placement ? e.placement->compFromLayer : Mat4::identity();
        out=input;out.texture=ctx.texture("lens-gradient",input.width,input.height);
        return ctx.fullscreen_pass("lens-gradient",PassStage::Effects,out.texture,ShaderId::effects_lens_gradient_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp},PassTexture{lensImage.texture,{},CommonSampler::LinearClamp},PassTexture{matteImage.texture,{},CommonSampler::LinearBorder}},
            &u,sizeof(u))==kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};
}
void register_lens_gradient(EffectRegistry& r) { (void)r.add(std::make_unique<LensGradient>()); }
}
