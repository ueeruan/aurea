// Original event scheduling and damage operations. Reference: BCC+ Video Glitch.
// Random distributions/patterns are authored here, not vendor algorithms.
#include "BuiltinEffects.hpp"
#include "aurea/timeline/Layer.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
class VideoGlitch final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.glitch.video_glitch","BCC Glitch","Glitch",EffectClass::Domain};return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const triggers[]={"Automático","Cruzamento de intensidade"};
        static const char* const patterns[]={"Blocos","Faixas horizontais","Faixas verticais","Fragmentos"};
        static const char* const edges[]={"Repetir","Refletir","Repetir pixels","Transparente"};
        static const char* const views[]={"Desligado","Sobre preto","Sobre o efeito"};
        p.add_float("intensity","Intensidade",100,0,200,kParamAnimatable | kParamPercent,"%");
        p.add_float("interval","Intervalo do glitch",60,1,600,kParamAnimatable,"quadros");
        p.add_float("duration","Duração do glitch",25,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_int("seed","Semente global",1,0,65535);
        p.add_int("start_frame","Quadro inicial",0,-600,600);
        p.add_enum("trigger","Disparo",triggers,2,0);
        p.add_float("threshold","Limiar de intensidade",50,0,200,kParamAnimatable | kParamPercent,"%");
        p.add_float("intensity_randomness","Variação da intensidade",25,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("interval_randomness","Variação do intervalo",0,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_float("duration_randomness","Variação da duração",0,0,100,kParamAnimatable | kParamPercent,"%");
        static const char* const ids[4][8]={
            {"block_enable","block_intensity","block_interval","block_minimum","block_peak","block_randomness","block_offset","block_seed"},
            {"shift_enable","shift_intensity","shift_interval","shift_minimum","shift_peak","shift_randomness","shift_offset","shift_seed"},
            {"shake_enable","shake_intensity","shake_interval","shake_minimum","shake_peak","shake_randomness","shake_offset","shake_seed"},
            {"flicker_enable","flicker_intensity","flicker_interval","flicker_minimum","flicker_peak","flicker_randomness","flicker_offset","flicker_seed"}};
        static const char* const labels[4][8]={
            {"Ativar dano em blocos","Intensidade dos blocos","Intervalo dos blocos","Mínimo dos blocos","Pico dos blocos","Variação dos blocos","Atraso dos blocos","Semente dos blocos"},
            {"Ativar deslocamento","Intensidade do deslocamento","Intervalo do deslocamento","Mínimo do deslocamento","Pico do deslocamento","Variação do deslocamento","Atraso do deslocamento","Semente do deslocamento"},
            {"Ativar tremor","Intensidade do tremor","Intervalo do tremor","Mínimo do tremor","Pico do tremor","Variação do tremor","Atraso do tremor","Semente do tremor"},
            {"Ativar flicker","Intensidade do flicker","Intervalo do flicker","Mínimo do flicker","Pico do flicker","Variação do flicker","Atraso do flicker","Semente do flicker"}};
        for(u32 g=0;g<4;++g) {
            p.add_bool(ids[g][0],labels[g][0],true);
            p.add_float(ids[g][1],labels[g][1],100,0,200,kParamAnimatable | kParamPercent,"%");
            p.add_float(ids[g][2],labels[g][2],1,.1f,4);
            p.add_float(ids[g][3],labels[g][3],0,0,100,kParamAnimatable | kParamPercent,"%");
            p.add_float(ids[g][4],labels[g][4],50,1,99,kParamAnimatable | kParamPercent,"%");
            p.add_float(ids[g][5],labels[g][5],100,0,100,kParamAnimatable | kParamPercent,"%");
            p.add_float(ids[g][6],labels[g][6],0,-600,600,kParamAnimatable,"quadros");
            p.add_int(ids[g][7],labels[g][7],static_cast<i32>(g),0,65535);
        }
        p.add_float("block_size","Tamanho do bloco",16,2,256,kParamAnimatable | kParamPixels,"px");
        p.add_enum("damage_pattern","Padrão do dano",patterns,4,0);
        p.add_float("shift_x","Deslocamento horizontal",64,-500,500,kParamAnimatable | kParamPixels,"px");
        p.add_float("shift_y","Deslocamento vertical",8,-500,500,kParamAnimatable | kParamPixels,"px");
        p.add_int("shift_bands","Faixas de deslocamento",8,1,64);
        p.add_float("duplicate","Duplicar faixas",0,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_angle("skew","Inclinação das faixas",0);
        p.add_point2("shake_xy","Tremor XY",{8,8},0,256,kParamAnimatable | kParamPixels);
        p.add_angle("shake_rotation","Rotação do tremor",2);
        p.add_float("rgb_split","Separação RGB",6,0,100,kParamAnimatable | kParamPixels,"px");
        p.add_float("flicker_brightness","Variação de brilho",20,0,200,kParamAnimatable | kParamPercent,"%");
        p.add_float("flicker_saturation","Variação de saturação",50,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_enum("edges","Bordas",edges,4,1);
        p.add_float("mix","Mistura",100,0,100,kParamAnimatable | kParamPercent,"%");
        p.add_enum("curve_view","Visualização das curvas",views,3,0);
        p.add_float("time_view","Janela das curvas",30,1,60,kParamAnimatable,"s");
        p.add_bool("freeze_source","Congelar fonte dos blocos",true);
    }
    struct Event {f32 amount=0;f64 start=0;};
    static u32 hash(u32 x) noexcept {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
    static f32 random(const EffectEval& e,i64 event,u32 group,u32 salt) noexcept {
        const u32 seed=static_cast<u32>(e.value(3).as_int()) ^ hash(static_cast<u32>(e.value(17+group*8).as_int()));
        return static_cast<f32>(hash(static_cast<u32>(event)^hash(seed)^hash(group)^salt)&0xffffffu)/16777215.f;
    }
    static const ParameterRegistry& specs() {
        static const ParameterRegistry registry=[] {ParameterRegistry result;VideoGlitch instance;instance.declare_parameters(result);return result;}();
        return registry;
    }
    static f64 crossing(const EffectEval& e) noexcept {
        if(!e.layer||!e.instance) return -1e30;
        const i64 now=static_cast<i64>(std::floor(e.time_frames()));
        const i64 start=e.value(4).as_int();
        const i64 horizon=static_cast<i64>(std::ceil(std::clamp(e.f(1),1.f,600.f)*4.f));
        const auto& spec=specs().at(0);
        f32 value=e.f(0);
        for(i64 frame=now;frame>=std::max(start,now-horizon);--frame) {
            const f32 previous=frame==start ? 0.f : evaluate_param(e.layer->tracks,*e.instance,0,spec,FrameIndex{frame-1}).v[0];
            if(previous<=e.f(6)&&value>e.f(6)) return static_cast<f64>(frame);
            value=previous;
        }
        return -1e30;
    }
    static Event event(const EffectEval& e,u32 group,f64 time,f64 manualStart) noexcept {
        const u32 base=10+group*8;
        if(!e.b(base)) return {};
        const f64 interval=std::clamp(static_cast<f64>(e.f(1)*e.f(base+2)),.1,2400.0);
        const f64 offset=static_cast<f64>(e.value(4).as_int())+e.f(base+6);
        const i64 cell=static_cast<i64>(std::floor((time-offset)/interval));
        Event best;
        for(i64 candidate=cell-1;candidate<=cell+1;++candidate) {
            if(e.e(5)==1 && candidate!=cell) continue;
            if(e.e(5)==0 && candidate<0) continue;
            const i64 identity=e.e(5)==1?static_cast<i64>(manualStart>-1e20?manualStart:0):candidate;
            const f64 start=e.e(5)==1 ? manualStart+e.f(base+6) : offset+static_cast<f64>(candidate)*interval+(random(e,candidate,group,11)-.5)*interval*e.f(8)*.008;
            const f64 duration=interval*std::clamp(e.f(2)*.01f,0.f,1.f)*(1+(random(e,identity,group,17)-.5)*e.f(9)*.01);
            const f64 phase=duration>0?(time-start)/duration:-1;
            if(phase<0||phase>1) continue;
            const f64 peak=std::clamp(e.f(base+4)*.01,.01,.99);
            f32 envelope=static_cast<f32>(phase<peak?phase/peak:(1-phase)/(1-peak));
            envelope=envelope*envelope*(3-2*envelope);
            const f32 variation=e.f(7)*.01f*e.f(base+5)*.01f;
            const f32 maximum=std::max(0.f,1+(random(e,identity,group,23)*2-1)*variation);
            const f32 amount=(e.f(base+3)*.01f+(maximum-e.f(base+3)*.01f)*envelope)*e.f(0)*.01f*e.f(base+1)*.01f;
            if(amount>=best.amount) best={amount,start};
        }
        return best;
    }
    bool wants_history() const noexcept override {return true;}
    f64 history_delay_frames(const EffectEval& e) const noexcept override {
        if(!e.b(58)) return 0;
        const auto block=event(e,0,e.time_frames(),e.e(5)==1?crossing(e):0);
        return block.amount>0 ? std::max(0.0,e.time_frames()-block.start) : 0;
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        const f64 manual=e.e(5)==1?crossing(e):0;
        e.auxTimeFrames=manual;
        e.auxInfo={event(e,0,e.time_frames(),manual).amount,event(e,1,e.time_frames(),manual).amount,event(e,2,e.time_frames(),manual).amount,event(e,3,e.time_frames(),manual).amount};
    }
    bool needs_full_input() const noexcept override {return true;}
    bool is_identity(const EffectEval& e) const noexcept override {return e.e(56)==0&&(e.f(55)<=0||e.f(0)<=0);}
    bool demo_values(EffectInstance&,std::vector<ParamValue>& values) const noexcept override {values[2]=ParamValue::scalar(100);values[4]=ParamValue::scalar(-30);return true;}
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat format) const override {out.push_back(PipelineKey::fullscreen(ShaderId::effects_video_glitch_frag,format));}
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32,LayerImage& out) const override {
        struct Uniforms {EffectUniforms base;Vec4 q0,q1,q2,q3,region,curves[64];} u{};
        u.base=base_uniforms(input);
        const LayerImage& history=ctx.history();
        u.base.p0={static_cast<f32>(e.time_frames()),static_cast<f32>(e.value(3).as_int()),history.valid()&&e.b(58)?1.f:0.f,e.f(42)};
        u.base.p1=e.auxInfo;
        u.base.p2={static_cast<f32>(e.e(43)),e.f(44),e.f(45),static_cast<f32>(e.value(46).as_int())};
        u.base.p3={e.f(47)*.01f,e.f(48)*kDeg2Rad,e.f(50)*kDeg2Rad,e.f(51)};
        const Vec2 shake=e.p2(49);
        u.q0={shake.x,shake.y,e.f(52)*.01f,e.f(53)*.01f};
        u.q1={static_cast<f32>(e.e(54)),e.f(55)*.01f,static_cast<f32>(e.e(56)),e.f(57)};
        const f32 width=e.placement?static_cast<f32>(e.placement->layerWidth):input.region.w;
        const f32 height=e.placement?static_cast<f32>(e.placement->layerHeight):input.region.h;
        u.q2={width,height,std::max(1.f,e.f(1)),0};
        u.q3=history.valid()?EffectBuildContext::uv_map(input.region,history.region):Vec4{1,1,0,0};
        u.region={input.region.x,input.region.y,input.region.w,input.region.h};
        if(e.e(56)!=0) for(u32 sample=0;sample<64;++sample) {
            const f64 time=static_cast<f64>(sample)/63*e.f(57)*e.framesPerSecond;
            // Manual curves use the captured active event; automatic curves
            // show the independently scheduled groups over the chosen window.
            const f64 manual=e.e(5)==1 ? e.auxTimeFrames : 0;
            u.curves[sample]={event(e,0,time,manual).amount,event(e,1,time,manual).amount,event(e,2,time,manual).amount,event(e,3,time,manual).amount};
        }
        out=input;out.texture=ctx.texture("video-glitch",input.width,input.height);
        return ctx.fullscreen_pass("video-glitch",PassStage::Effects,out.texture,ShaderId::effects_video_glitch_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp},PassTexture{history.valid()?history.texture:input.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex ? Status{Errc::PipelineCompileFailed}:OkStatus;
    }
};
}
void register_video_glitch(EffectRegistry& r) {(void)r.add(std::make_unique<VideoGlitch>());}
}
