#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

// Original mobile implementations. Desktop plug-in binaries are not linked or
// shipped. Each intermediate belongs to the frame graph, including coefficients
// with negative values (the graph's floating-point working format).
namespace aurea::builtin {
namespace {
class JpegGlitch final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kJpegGlitch,"JPEG Glitch","Glitch",EffectClass::Neighborhood}; return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("quality","JPEG Quality",35,1,100);
        p.add_float("damage","Coefficient Damage",8,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("strength","Damage Strength",20,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_int("seed","Seed",1,0,9999);
        p.add_float("speed","Changes per Second",10,0,60,kParamAnimatable,"Hz");
        p.add_float("mix","Mix",100,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_bool("table_override","Edit Quantization Table",false);
        p.add_int("table_position","Table Position",0,0,63);
        p.add_float("table_value","Table Value",16,1,255);
        p.add_float("table_random","Random Table Entries",0,0,64);
        p.add_float("table_max","Maximum Random Value",100,1,255);
        static const char* sampling[]={"4:4:4","4:2:2","4:2:0"};
        p.add_enum("sampling","Chroma Subsampling",sampling,3,2);
        p.add_float("dc_shift","DC Drift",0,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("frequency_start","First Frequency",0,0,63);
        p.add_float("frequency_end","Last Frequency",63,0,63);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(5)<=0; }
    void pipelines(std::vector<PipelineKey>& v,SurfaceFormat f) const override {
        v.push_back(PipelineKey::fullscreen(ShaderId::effects_jpeg_codec_frag,f));
    }
    Status build(EffectBuildContext& c,const EffectEval& e,const LayerImage& in,f32,LayerImage& out) const override {
        EffectUniforms u=base_uniforms(in);
        // Pad coefficient grids to 8x8 without stretching image coordinates.
        const u32 w=std::min(c.max_texture_size(),(in.width+7u)&~7u),h=std::min(c.max_texture_size(),(in.height+7u)&~7u);
        u.texel={f32(in.width),f32(in.height),f32(w),f32(h)};
        const f32 tick=static_cast<f32>(std::floor(e.localTime.value/e.framesPerSecond*e.f(4)));
        u.p0={e.f(0),e.f(1)*.01f,e.f(2)*.01f,e.f(3)};
        u.p1={tick,e.b(6)?1.f:0.f,e.f(7),e.f(8)};
        u.p2={e.f(9),e.f(10),e.f(11),e.f(12)*.01f};
        u.color={e.f(5)*.01f,std::min(e.f(13),e.f(14)),std::max(e.f(13),e.f(14)),0};
        FGTexture previous=in.texture;
        for(u32 stage=0;stage<4;++stage) {
            u.p3.x=f32(stage);
            const bool final=stage==3;
            const FGTexture target=c.texture("jpeg-codec",final?in.width:w,final?in.height:h);
            if(c.fullscreen_pass("jpeg-codec",PassStage::Effects,target,ShaderId::effects_jpeg_codec_frag,
                {{previous,{},CommonSampler::NearestClamp},{in.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex) return Errc::PipelineCompileFailed;
            previous=target;
        }
        out={previous,in.region,in.width,in.height}; return OkStatus;
    }
};

class AnalogSignal final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kAnalogSignal,"Signal Analog","Glitch",EffectClass::Neighborhood}; return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("noise","Signal Noise",6,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("bandwidth","Chroma Bandwidth",65,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("bend","Horizontal Bend",4,0,100,kParamAnimatable|kParamPixels,"px");
        p.add_float("chroma_gain","Chroma Gain",100,0,300,kParamAnimatable|kParamPercent,"%");
        p.add_float("mix","Mix",100,0,100,kParamAnimatable|kParamPercent,"%");
        static const char* standard[]={"NTSC","PAL"}; p.add_enum("standard","Standard",standard,2,0);
        p.add_angle("phase","Chroma Phase",0);
        p.add_float("crosstalk","Y/C Crosstalk",25,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("head_switch","Head Switching",8,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("roll","Vertical Roll",0,-100,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("scanlines","Scanlines",12,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_float("chroma_delay","Chroma Delay",1,-32,32,kParamAnimatable|kParamPixels,"px");
        p.add_float("speed","Speed",1,0,10);
        p.add_int("seed","Seed",7,0,9999);
        p.add_float("dropouts","Dropouts",3,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_bool("freeze","Freeze",false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(4)<=0; }
    void pipelines(std::vector<PipelineKey>& v,SurfaceFormat f) const override {v.push_back(PipelineKey::fullscreen(ShaderId::effects_analog_signal_frag,f));}
    Status build(EffectBuildContext& c,const EffectEval& e,const LayerImage& in,f32,LayerImage& out) const override {
        EffectUniforms u=base_uniforms(in);
        // Stable raster independent of preview scale. The modulation carrier is
        // sampled at four samples/cycle on this intermediate, not on screen UVs.
        const u32 w=std::min(1440u,c.max_texture_size()),h=std::min(e.e(5)?576u:480u,c.max_texture_size());
        u.uvMap={f32(in.width),f32(in.height),f32(w),f32(h)};
        u.texel={e.f(0)*.01f,e.f(1)*.01f,e.f(2)*f32(w)/std::max(1.f,in.region.w),e.f(3)*.01f};
        u.p0={e.f(4)*.01f,f32(e.e(5)),e.f(6)*kDeg2Rad,e.f(7)*.01f};
        u.p1={e.f(8)*.01f,e.f(9)*.01f,e.f(10)*.01f,e.f(11)*f32(w)/std::max(1.f,in.region.w)};
        u.p2={e.b(15)?0.f:f32(e.localTime.value/e.framesPerSecond)*e.f(12),e.f(13),e.f(14)*.01f,0};
        const FGTexture encoded=c.texture("analog-modulated-signal",w,h);
        if(c.fullscreen_pass("analog-encode",PassStage::Effects,encoded,ShaderId::effects_analog_signal_frag,
            {{in.texture,{},CommonSampler::LinearClamp},{in.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex) return Errc::PipelineCompileFailed;
        u.p3.x=1;
        out={c.texture("analog-decoded",in.width,in.height),in.region,in.width,in.height};
        if(c.fullscreen_pass("analog-decode",PassStage::Effects,out.texture,ShaderId::effects_analog_signal_frag,
            {{encoded,{},CommonSampler::NearestClamp},{in.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex) return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
} // namespace
void register_media_lab_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<JpegGlitch>());
    (void)r.add(std::make_unique<AnalogSignal>());
}
} // namespace aurea::builtin
