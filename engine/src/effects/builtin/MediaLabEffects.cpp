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

class JpegCompressionDamage final : public Effect {
public:
    enum : u32 {
        kQuality, kReduction, kReductionX, kAll, kFrequencyX, kFrequencyY, kLow, kMid, kHigh,
        kLuma, kChroma, kErrorRate, kErrorDensity, kErrorAmplitude, kSeed, kFrameInterval,
        kGain, kOffset, kSaturation,
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.jpeg_compression_damage", "Compressão JPEG",
                                  "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("quality", "Qualidade", 10.0f, 1.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("resolution_factor", "Fator de redução", 1, 1, 64);
        p.add_float("resolution_x", "Redução relativa X", 1.0f, 0.01f, 64.0f, kParamAnimatable);
        p.add_float("frequency_all", "Todas as frequências", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("frequency_x", "Frequências horizontais", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("frequency_y", "Frequências verticais", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("frequency_low", "Frequências baixas", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("frequency_mid", "Frequências médias", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("frequency_high", "Frequências altas", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("affect_luma", "Influência na luma", 1.0f, 0.0f, 4.0f, kParamAnimatable);
        p.add_float("affect_chroma", "Influência na croma", 0.5f, 0.0f, 4.0f, kParamAnimatable);
        p.add_float("error_rate", "Taxa de erros", 0.0f, 0.0f, 64.0f, kParamAnimatable);
        p.add_float("error_density", "Densidade dos erros", 0.75f, 0.0f, 1.0f, kParamAnimatable);
        p.add_float("error_amplitude", "Amplitude dos erros", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("seed", "Semente", 0.123f, 0.0f, 1024.0f, kParamAnimatable);
        p.add_int("frame_interval", "Intervalo entre quadros", 1, 0, 300);
        p.add_float("scale_lights", "Ganho das luzes", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("offset_darks", "Deslocamento das sombras", 0.0f, -2.0f, 2.0f, kParamAnimatable);
        p.add_float("saturation", "Saturação", 1.0f, -2.0f, 10.0f, kParamAnimatable);
    }
    void pipelines(std::vector<PipelineKey>& v, SurfaceFormat f) const override {
        v.push_back(PipelineKey::fullscreen(ShaderId::effects_jpeg_codec_frag, f));
    }
    Status build(EffectBuildContext& c, const EffectEval& e, const LayerImage& in, f32,
                 LayerImage& out) const override {
        auto read = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            const f32 value = e.f(index);
            return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
        };
        const f32 reduction = read(kReduction, 1.0f, 1.0f, 64.0f);
        const f32 reductionX = std::max(1.0f, reduction * read(kReductionX, 1.0f, 0.01f, 64.0f));
        const u32 maxPaddedSize = c.max_texture_size() & ~15u;
        const u32 width = std::min(maxPaddedSize, static_cast<u32>(std::ceil(in.width / reductionX)));
        const u32 height = std::min(maxPaddedSize, static_cast<u32>(std::ceil(in.height / reduction)));
        if (width == 0 || height == 0) return Errc::PipelineCompileFailed;
        // Chroma is stored in the upper-left half of the same intermediate
        // surface. Aligning the luma plane to 16 also aligns the chroma DCT to 8.
        const u32 w = (width + 15u) & ~15u;
        const u32 h = (height + 15u) & ~15u;
        EffectUniforms u = base_uniforms(in);
        u.texel = {f32(width), f32(height), f32(w), f32(h)};
        u.p0 = {read(kQuality, 10, 1, 100), read(kAll, 1, 0, 8),
                read(kFrequencyX, 1, 0, 8), read(kFrequencyY, 1, 0, 8)};
        u.p1 = {read(kLow, 1, 0, 8), read(kMid, 1, 0, 8), read(kHigh, 1, 0, 8), read(kLuma, 1, 0, 4)};
        u.p2 = {read(kChroma, .5f, 0, 4), read(kErrorRate, 0, 0, 64),
                read(kErrorDensity, .75f, 0, 1), read(kErrorAmplitude, 1, 0, 8)};
        const u32 interval = static_cast<u32>(read(kFrameInterval, 1, 0, 300));
        const u64 frame = interval ? static_cast<u64>(std::max<i64>(0, e.localTime.value)) / interval : 0u;
        const u32 seed = static_cast<u32>(std::round(read(kSeed, .123f, 0, 1024) * 1000000.0f))
                         ^ (static_cast<u32>(frame) * 0x9e3779b9u)
                         ^ (static_cast<u32>(frame >> 32) * 0x85ebca6bu);
        u.p3 = {0.0f, static_cast<f32>(seed & 0xffffu), static_cast<f32>(seed >> 16), 0.0f};
        u.color = {read(kGain, 1, 0, 8), read(kOffset, 0, -2, 2), read(kSaturation, 1, -2, 10), 1.0f};

        FGTexture previous = in.texture;
        for (u32 stage = 0; stage < 5; ++stage) {
            u.p3.x = static_cast<f32>(stage);
            const bool final = stage == 4;
            const auto target = c.texture("compressao-jpeg", final ? in.width : w, final ? in.height : h);
            if (c.fullscreen_pass("compressao-jpeg", PassStage::Effects, target, ShaderId::effects_jpeg_codec_frag,
                                  {PassTexture{previous, {}, final ? CommonSampler::LinearClamp : CommonSampler::NearestClamp},
                                   PassTexture{in.texture, {}, CommonSampler::LinearClamp}},
                                  &u, sizeof(u)) == kInvalidIndex) return Errc::PipelineCompileFailed;
            previous = target;
        }
        out = {previous, in.region, in.width, in.height};
        return OkStatus;
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

// The reference contract comes from the selected Signal 1.2.3 artifact.
// The rendering implementation uses the shared GPU graph and owns every pass.
class SignalReference final : public Effect {
public:
    enum : u32 {
        kSeed, kStrength, kAmplification, kCutoff, kLumaNoise, kChromaNoise, kHueNoise, kChromaLoss,
        kSharpen, kConnections, kVhs, kDestruction, kLumaModulation, kFrequency, kPhase, kLightness,
        kQuantize, kTapeErrors, kTapeStart, kTapeEnd, kTapeProbability, kTapeFade, kTapeGamma,
        kThickDistortion, kFineDistortion, kDistortionSpeed, kRollSpeed, kLowPassIn, kLowPassOut,
        kPhaseShift, kPhaseOffset, kSVideo, kChromaVerticalBlend, kSharpenEnabled, kScanlineBlur, kCount
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{effect_keys::kSignalReference, "Signal", "Glitch", EffectClass::Global};
        return info;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("seed", "Random seed", 1, 1, 1000000);
        p.add_float("signal_strength", "Signal strength", 40, 1, 200);
        p.add_float("amplification", "Signal amplification", 1, 0, 100);
        p.add_float("cutoff", "Cutoff filter", 88, 1, 100000);
        p.add_float("luma_noise", "Luma noise", 1, 0, 400);
        p.add_float("chroma_noise", "Chroma noise", 1, 0, 400);
        p.add_float("hue_noise", "Hue Noise", 15, 0, 200);
        p.add_float("chroma_loss", "Chroma loss", .24f, 0, 1);
        p.add_float("sharpen", "Sharpen", 2, 0, 100);
        p.add_float("connections", "Device-to-device connections", 0, 0, 100);
        static const char* const vhs[] = {"VHS SP", "VHS LP", "VHS EP"};
        p.add_enum("vhs", "Emulating VHS", vhs, 3, 0);
        p.add_bool("destruction", "Destruction button", false);
        p.add_bool("luma_modulation", "Luma modulation", true);
        p.add_float("frequency", "Frequency", .62f, .001f, 40);
        p.add_float("phase", "Phase", 2, .001f, 40);
        p.add_float("lightness", "Lightness", .62f, 0, 1);
        p.add_float("quantize", "Quantize", 0, 0, 100);
        p.add_bool("tape_errors", "Tape Errors", true);
        p.add_float("tape_start", "TE start", .92f, 0, 1);
        p.add_float("tape_end", "TE end", 1, 0, 1);
        p.add_float("tape_probability", "TE probability", .1f, 0, 1);
        p.add_float("tape_fade", "TE fade", 5, .01f, 5);
        p.add_float("tape_gamma", "TE gamma", 3, .01f, 14);
        p.add_float("thick_distortion", "Thick distortion", 0, 0, 100);
        p.add_float("fine_distortion", "Fine distortion", 0, 0, 10);
        p.add_float("distortion_speed", "Distortion speed", 0, 0, 200);
        p.add_float("roll_speed", "Roll speed", 0, 0, 200);
        p.add_bool("low_pass_in", "Low-Pass filter in", true);
        static const char* const lowPass[] = {"Off", "Lite", "Strong"};
        p.add_enum("low_pass_out", "Low-Pass filter out", lowPass, 3, 2);
        p.add_float("phase_shift", "Phase shift", 0, 0, 360);
        p.add_float("phase_offset", "Phase shift offset", 0, 0, 360);
        p.add_bool("s_video", "S-video out", true);
        p.add_bool("chroma_vertical_blend", "Chroma vertical blend", true);
        p.add_bool("sharpen_enabled", "Sharpen On/Off", true);
        p.add_float("scanline_blur", "Scanlines blur", 1.5f, 0, 100);
        p.add_section_range("luma", "Luma modulation", kFrequency, kTapeGamma, kLumaModulation, false);
        p.add_section_range("distortion", "TV distortion", kThickDistortion, kRollSpeed, kInvalidIndex, false);
        p.add_section_range("experimental", "Experimental parameters", kLowPassIn, kScanlineBlur, kInvalidIndex, false);
    }

    bool needs_full_input() const noexcept override { return true; }

    void pipelines(std::vector<PipelineKey>& pipelines, SurfaceFormat work) const override {
        pipelines.push_back(PipelineKey::fullscreen(ShaderId::effects_signal_reference_frag, SurfaceFormat::RGBA32F));
        pipelines.push_back(PipelineKey::fullscreen(ShaderId::effects_signal_reference_frag, work));
        pipelines.push_back(PipelineKey::compute_shader(ShaderId::effects_signal_reference_comp));
    }

    struct Uniforms {
        f32 controls[36]{};
        Vec4 raster{};
        Vec4 context{};
    };

    static FGTexture intermediate(EffectBuildContext& ctx, const char* name, u32 width, u32 height) {
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        // Composite samples retain the native integer signal range. Half-float
        // loses the small chroma residual at low transmission strength.
        desc.format = SurfaceFormat::RGBA32F;
        desc.sampled = true;
        desc.storage = true;
        desc.renderTarget = true;
        desc.debugName = name;
        return ctx.graph().create_texture(name, desc);
    }

    static Status filter_pass(EffectBuildContext& ctx, Uniforms uniforms, FGTexture source, FGTexture target,
                              u32 height, u32 stage) {
        const auto pipeline = ctx.shaders().pipeline(PipelineKey::compute_shader(ShaderId::effects_signal_reference_comp));
        if (!pipeline.ok()) return pipeline.status();
        const auto sampler = ctx.shaders().sampler(CommonSampler::NearestClamp);
        const auto handle = *pipeline;
        uniforms.raster.w = static_cast<f32>(stage);
        const u32 pass = ctx.graph().add_compute_pass("signal-row-filter", PassStage::Effects,
            [uniforms, source, target, sampler, handle, height](PassContext& pc) {
                pc.cmds.bind_pipeline(handle);
                pc.cmds.bind_texture(0, pc.texture(source), sampler);
                pc.cmds.bind_storage_image(0, pc.texture(target));
                pc.cmds.set_uniforms(&uniforms, sizeof(uniforms));
                pc.cmds.dispatch((height + 63u) / 64u, 1, 1);
            });
        ctx.graph().read(pass, source);
        ctx.graph().write_storage(pass, target);
        return OkStatus;
    }

    static Status raster_pass(EffectBuildContext& ctx, Uniforms uniforms, u32 stage,
                              FGTexture source, FGTexture reference, u32 width, u32 height,
                              const char* label, FGTexture& target) {
        target = intermediate(ctx, label, width, height);
        uniforms.raster.w = static_cast<f32>(stage);
        if (ctx.fullscreen_pass(label, PassStage::Effects, target, ShaderId::effects_signal_reference_frag,
                {{source, {}, CommonSampler::NearestClamp}, {reference, {}, CommonSampler::NearestClamp}},
                &uniforms, sizeof(uniforms)) == kInvalidIndex) return Errc::PipelineCompileFailed;
        return OkStatus;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        if (!input.width || !input.height) { out = input; return OkStatus; }
        u32 width = 0, height = 0;
        ctx.region_size(input.region, 1, width, height);
        Uniforms uniforms;
        for (u32 index = 0; index < kCount; ++index) {
            uniforms.controls[index] = e.f(index);
        }
        uniforms.raster = {static_cast<f32>(width), static_cast<f32>(height),
                          static_cast<f32>(e.time_frames() / std::max(e.framesPerSecond, 1e-6)), 0};
        uniforms.context = {static_cast<f32>(e.localTime.value), 0, 0, 0};
        FGTexture decoded = intermediate(ctx, "signal-source-yiq", width, height);
        if (ctx.fullscreen_pass("signal-prepare", PassStage::Effects, decoded,
                ShaderId::effects_signal_reference_frag,
                {{input.texture, {}, CommonSampler::LinearClamp}, {input.texture, {}, CommonSampler::LinearClamp}},
                &uniforms, sizeof(uniforms)) == kInvalidIndex) return Errc::PipelineCompileFailed;
        if (e.b(kLowPassIn)) {
            const FGTexture filtered = intermediate(ctx, "signal-input-filter", width, height);
            if (const Status status = filter_pass(ctx, uniforms, decoded, filtered, height, 9); !status.ok()) return status;
            FGTexture compensated;
            if (const Status status = raster_pass(ctx, uniforms, 10, filtered, decoded, width, height,
                    "signal-input-delay", compensated); !status.ok()) return status;
            decoded = compensated;
        }
        const u32 cycles = 1u + static_cast<u32>(std::clamp(e.f(kConnections), 0.0f, 100.0f)) + (e.b(kSVideo) ? 0u : 1u);
        for (u32 cycle = 0; cycle < cycles; ++cycle) {
            uniforms.context.y = static_cast<f32>(cycle);
            const FGTexture encoded = intermediate(ctx, "signal-composite-wave", width, height);
            if (const Status status = filter_pass(ctx, uniforms, decoded, encoded, height, 1); !status.ok()) return status;
            uniforms.raster.w = 2;
            const FGTexture demodulated = intermediate(ctx, "signal-demodulated-yiq", width, height);
            if (ctx.fullscreen_pass("signal-demodulate", PassStage::Effects, demodulated,
                    ShaderId::effects_signal_reference_frag,
                    {{encoded, {}, CommonSampler::NearestClamp}, {input.texture, {}, CommonSampler::LinearClamp}},
                    &uniforms, sizeof(uniforms)) == kInvalidIndex) return Errc::PipelineCompileFailed;
            decoded = demodulated;
            if (cycle == 0) {
                const FGTexture transmitted = intermediate(ctx, "signal-vhs-filter", width, height);
                if (const Status status = filter_pass(ctx, uniforms, demodulated, transmitted, height, 3); !status.ok()) return status;
                if (const Status status = raster_pass(ctx, uniforms, 11, transmitted, demodulated, width, height,
                        "signal-vhs-delay", decoded); !status.ok()) return status;
                if (e.b(kChromaVerticalBlend)) {
                    FGTexture blended;
                    if (const Status status = raster_pass(ctx, uniforms, 12, decoded, decoded, width, height,
                            "signal-chroma-blend", blended); !status.ok()) return status;
                    decoded = blended;
                }
                if (e.b(kSharpenEnabled)) {
                    const FGTexture sharpened = intermediate(ctx, "signal-luma-sharpen", width, height);
                    if (const Status status = filter_pass(ctx, uniforms, decoded, sharpened, height, 13); !status.ok()) return status;
                    decoded = sharpened;
                }
            }
        }
        FGTexture filtered = decoded;
        if (e.e(kLowPassOut) != 0) {
            const FGTexture lowPass = intermediate(ctx, "signal-output-filter", width, height);
            if (const Status status = filter_pass(ctx, uniforms, decoded, lowPass, height, 5); !status.ok()) return status;
            if (const Status status = raster_pass(ctx, uniforms, 14, lowPass, decoded, width, height,
                    "signal-output-delay", filtered); !status.ok()) return status;
        }
        uniforms.raster.w = 4;
        out = {ctx.texture("signal", input.width, input.height), input.region, input.width, input.height};
        const bool blur = e.f(kScanlineBlur) > .001f;
        const FGTexture composed = blur ? intermediate(ctx, "signal-color", width, height) : out.texture;
        if (ctx.fullscreen_pass("signal-compose", PassStage::Effects, composed,
                ShaderId::effects_signal_reference_frag,
                {{filtered, {}, CommonSampler::LinearClamp}, {input.texture, {}, CommonSampler::LinearClamp}},
                &uniforms, sizeof(uniforms)) == kInvalidIndex) return Errc::PipelineCompileFailed;
        if (blur) {
            uniforms.raster.w = 6;
            const FGTexture horizontal = intermediate(ctx, "signal-blur-horizontal", width, height);
            if (ctx.fullscreen_pass("signal-blur-horizontal", PassStage::Effects, horizontal,
                    ShaderId::effects_signal_reference_frag,
                    {{composed, {}, CommonSampler::LinearBorder}, {input.texture, {}, CommonSampler::LinearClamp}},
                    &uniforms, sizeof(uniforms)) == kInvalidIndex) return Errc::PipelineCompileFailed;
            uniforms.raster.w = 7;
            if (ctx.fullscreen_pass("signal-blur-vertical", PassStage::Effects, out.texture,
                    ShaderId::effects_signal_reference_frag,
                    {{horizontal, {}, CommonSampler::LinearBorder}, {input.texture, {}, CommonSampler::LinearClamp}},
                    &uniforms, sizeof(uniforms)) == kInvalidIndex) return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};
} // namespace
void register_media_lab_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<JpegGlitch>());
    (void)r.add(std::make_unique<JpegCompressionDamage>());
    (void)r.add(std::make_unique<AnalogSignal>());
    (void)r.add(std::make_unique<SignalReference>());
}
} // namespace aurea::builtin
