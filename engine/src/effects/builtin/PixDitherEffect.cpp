// Ordered thresholds and true error diffusion. A single compute workgroup
// follows x+3y wavefronts, so every predecessor is complete before it is read.
// This preserves global diffusion across the entire image without tile seams.
#include "BuiltinEffects.hpp"
#include <algorithm>

namespace aurea::builtin {
namespace {
class PixDither final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.pix_dither","PixDither","Estilizar",EffectClass::Neighborhood};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const methods[]={"Threshold","Floyd Steinberg","False Floyd Steinberg","Filter Lite","Zhigang Fan","Shiau-Fan","Jarvis, Judice & Ninke","Stucki","Burkes","Sierra","Two Row Sierra","Atkinson","Bayer 2x2","Bayer 3x3","Bayer 5x3","Bayer 4x4","Bayer 8x8","Cluster Dot 4x4","Cluster Dot 8x8","Horizontal 2x2","Horizontal 8x1","Horizontal 12x4","Vertical 2x2","Vertical 1x8","Vertical 4x12","Diagonal 5x5"};
        static const char* const palettes[]={"Cinza 1 bit","Cinza 2 bits","Cinza 4 bits","RGB 3 bits","RGB 6 bits","RGB 9 bits","PICO-8","Gameboy","CGA","ZX Spectrum","Web Safe","Duas cores"};
        p.add_enum("method","Método de pontilhado",methods,26,12);
        p.add_enum("palette","Paleta",palettes,12,0);
        p.add_float("amount","Intensidade do pontilhado",100,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_color("color_a","Cor 1",{0,0,0,1});
        p.add_color("color_b","Cor 2",{1,1,1,1});
    }
    bool needs_full_input() const noexcept override {return true;}
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_pix_dither_frag,format));
        out.push_back(PipelineKey::compute_shader(ShaderId::effects_pix_diffusion_comp));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32,LayerImage& out) const override {
        struct Uniforms {EffectUniforms base;Vec4 second;} u{};
        u.base=base_uniforms(input);
        u.base.p0={static_cast<f32>(e.e(0)),static_cast<f32>(e.e(1)),e.f(2)*.01f,0};
        u.base.p1={static_cast<f32>(input.width),static_cast<f32>(input.height),0,0};
        u.base.p2={input.region.x,input.region.y,input.region.w,input.region.h};
        u.base.color=e.color(3);u.second=e.color(4);
        if(e.e(0)==0 || e.e(0)>=12 || e.f(2)<=0) {
            out=input;out.texture=ctx.texture("pix-dither",input.width,input.height);
            return ctx.fullscreen_pass("pix-dither",PassStage::Effects,out.texture,ShaderId::effects_pix_dither_frag,
                {PassTexture{input.texture,{},CommonSampler::NearestClamp}},&u,sizeof(u))==kInvalidIndex ? Status{Errc::PipelineCompileFailed}:OkStatus;
        }
        auto pipeline=ctx.shaders().pipeline(PipelineKey::compute_shader(ShaderId::effects_pix_diffusion_comp));
        if(!pipeline.ok()) return pipeline.status();
        TextureDesc description;
        description.width=input.width;description.height=input.height;description.sampled=true;description.storage=true;
        description.format=SurfaceFormat::RGBA32F;
        const FGTexture errors=ctx.graph().create_texture("pix-diffusion-errors",description);
        description.format=SurfaceFormat::RGBA16F;
        out=input;out.texture=ctx.graph().create_texture("pix-diffusion",description);
        const auto sampler=ctx.shaders().sampler(CommonSampler::NearestClamp);
        const auto target=out.texture,source=input.texture;
        const auto pipe=*pipeline;
        const u32 pass=ctx.graph().add_compute_pass("pix-diffusion",PassStage::Effects,
            [u,source,target,errors,sampler,pipe](PassContext& pc) {
                pc.cmds.bind_pipeline(pipe);
                pc.cmds.bind_texture(0,pc.texture(source),sampler);
                pc.cmds.bind_storage_image(0,pc.texture(errors));
                pc.cmds.bind_storage_image(1,pc.texture(target));
                pc.cmds.set_uniforms(&u,sizeof(u));
                pc.cmds.dispatch(1,1,1);
            });
        ctx.graph().read(pass,source);
        ctx.graph().write_storage(pass,errors);
        ctx.graph().write_storage(pass,target);
        return OkStatus;
    }
};
}
void register_pix_dither(EffectRegistry& r) {(void)r.add(std::make_unique<PixDither>());}
}
