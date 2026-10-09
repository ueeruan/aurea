// Original implementations. Time is evaluated from the layer, never a mutable RNG.
#include "BuiltinEffects.hpp"
#include "aurea/effects/ShakeMotion.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
float safe(float v, float fallback = 0) { return std::isfinite(v) ? v : fallback; }
double time_of(const EffectEval& e) { return double(e.localTime.value) / (e.framesPerSecond > 0 ? e.framesPerSecond : 30); }
Mat4 rotate(float degrees) { return Mat4::from_quat(Quat::from_axis_angle({0,0,1}, degrees*kDeg2Rad)); }
Vec2 size_of(const EffectEval& e) {
    return e.placement ? Vec2{float(e.placement->layerWidth), float(e.placement->layerHeight)} : Vec2{1,1};
}
void frequency(ParameterRegistry& p, float initial = 2) { p.add_float("frequency", "Frequência", initial, 0, 16, kParamAnimatable, "Hz"); }
void phase(ParameterRegistry& p) { p.add_float("phase", "Fase", 0, -10, 10, kParamAnimatable, "ciclos"); }
void percent(ParameterRegistry& p, const char* key, const char* name, float initial, float lo = 0, float hi = 100) {
    p.add_float(key, name, initial, lo, hi, kParamAnimatable|kParamPercent, "%");
}

enum MotionKind { Blink, Flicker, Pulse, Displacement, Jitter, Swing, Spin, Stretch, ScaleAssist, Raster };
class MotionExtra final : public Effect {
    MotionKind kind_;
public:
    explicit MotionExtra(MotionKind k) : kind_(k) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info[] = {
            {"aurea.motion.blink", "Blink", "Distorcer", EffectClass::Domain},
            {"aurea.motion.flicker", "Flicker", "Distorcer", EffectClass::Domain},
            {"aurea.motion.pulse_size", "Pulse Size", "Distorcer", EffectClass::Domain},
            {"aurea.motion.random_displacement", "Random Displacement", "Distorcer", EffectClass::Domain},
            {"aurea.motion.random_jitter", "Random Jitter", "Distorcer", EffectClass::Domain},
            {"aurea.motion.swing_range", "Swing", "Distorcer", EffectClass::Domain},
            {"aurea.motion.spin", "Spin", "Distorcer", EffectClass::Domain},
            {"aurea.transform.stretch_axis", "Stretch Axis", "Distorcer", EffectClass::Domain},
            {"aurea.transform.scale_assist", "Scale Assist", "Distorcer", EffectClass::Domain},
            {"aurea.transform.raster", "Raster Transform", "Distorcer", EffectClass::Domain},
        };
        return info[kind_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const waves[] = {"Seno", "Triângulo"};
        switch (kind_) {
        case Blink:
            frequency(p); phase(p); percent(p,"duty","Tempo visível",50); break;
        case Flicker:
            frequency(p,8); percent(p,"minimum","Opacidade mínima",0); percent(p,"maximum","Opacidade máxima",100);
            p.add_int("seed","Semente",1,0,999999); phase(p); break;
        case Pulse:
            frequency(p); percent(p,"grow","Crescer",25,0,1000); percent(p,"shrink","Encolher",25,0,99.9f);
            phase(p); p.add_enum("wave","Forma da onda",waves,2,0); break;
        case Displacement:
        case Jitter:
            p.add_float("magnitude","Amplitude",25,0,4000,kParamAnimatable|kParamPixels,"px");
            frequency(p, kind_ == Jitter ? 8 : 2); phase(p); p.add_int("seed","Semente",1,0,999999);
            if (kind_ == Jitter) p.add_angle("angle","Ângulo",0);
            else percent(p,"scatter","Dispersão vertical",100);
            break;
        case Swing:
            p.add_angle("angle1","Ângulo inicial",-30); p.add_angle("angle2","Ângulo final",30);
            frequency(p); phase(p); p.add_enum("wave","Forma da onda",waves,2,0); break;
        case Spin:
            p.add_float("speed","Velocidade",90,-3600,3600,kParamAnimatable,"°/s"); p.add_angle("phase","Ângulo inicial",0); break;
        case Stretch:
            percent(p,"scale","Escala",150,1,1000); p.add_angle("angle","Ângulo",0); break;
        case ScaleAssist:
            percent(p,"scale","Escala",100,0.1f,1000); percent(p,"width","Largura",100,0.1f,1000);
            percent(p,"height","Altura",100,0.1f,1000); break;
        case Raster:
            p.add_point2("offset","Deslocamento",{0,0},-10000,10000,kParamAnimatable|kParamPixels);
            percent(p,"scale","Escala",100,0.1f,1000); p.add_angle("angle","Ângulo",0);
            percent(p,"opacity","Opacidade",100); break;
        }
        p.add_point2("pivot","Pivô",{0.5f,0.5f},-2,3,kParamAnimatable|kParamRelative);
    }
    void evaluate(const EffectEval& e, Mat4& matrix, float& opacity) const noexcept {
        matrix = Mat4::identity(); opacity = 1;
        float sx=1, sy=1, angle=0, axis=0; Vec2 offset{};
        const auto value = [&](u32 i){ return safe(e.f(i)); };
        const double t = time_of(e);
        const auto wave = [&](double cycles, u32 shape) {
            const double u = cycles-std::floor(cycles);
            return shape == 1 ? float(1-4*std::abs(u-.5)) : float(std::sin(u*6.283185307179586));
        };
        switch(kind_) {
        case Blink: {
            const double cycles=t*value(0)+value(1), u=cycles-std::floor(cycles);
            opacity = u < value(2)*.01 ? 1.f : 0.f; break;
        }
        case Flicker: {
            const float r=(shake::random(u32(value(3)),i64(std::floor(t*value(0)+value(4))),0)+1)*.5f;
            opacity=(value(1)+(value(2)-value(1))*r)*.01f; break;
        }
        case Pulse: {
            const float w=wave(t*value(0)+value(3),e.e(4)); sx=sy=1+w*(w>=0?value(1):value(2))*.01f; break;
        }
        case Displacement: {
            const double tick=t*value(1)+value(2); const u32 seed=u32(value(3));
            offset={value(0)*shake::noise(tick,seed,0,1),value(0)*value(4)*.01f*shake::noise(tick,seed,1,1)}; break;
        }
        case Jitter: {
            const float d=value(0)*shake::random(u32(value(3)),i64(std::floor(t*value(1)+value(2))),0);
            offset={d*std::cos(value(4)*kDeg2Rad),d*std::sin(value(4)*kDeg2Rad)}; break;
        }
        case Swing: angle=value(0)+(value(1)-value(0))*(wave(t*value(2)+value(3),e.e(4))+1)*.5f; break;
        case Spin: angle=float(std::remainder(t*value(0)+value(1),360.)); break;
        case Stretch: sx=value(0)*.01f; axis=value(1); break;
        case ScaleAssist: sx=value(0)*value(1)*.0001f; sy=value(0)*value(2)*.0001f; break;
        case Raster: offset=e.p2(0); sx=sy=value(1)*.01f; angle=value(2); opacity=value(3)*.01f; break;
        }
        const Vec2 s=size_of(e), pivot=e.p2(e.count-1);
        const Vec3 center{pivot.x*s.x,pivot.y*s.y,0};
        matrix=Mat4::translation({center.x+offset.x,center.y+offset.y,0})*rotate(angle+axis)
            *Mat4::scale({std::max(.001f,sx),std::max(.001f,sy),1})*rotate(-axis)
            *Mat4::translation({-center.x,-center.y,0});
        opacity=std::clamp(opacity,0.f,1.f);
    }
    bool fold_into_composite(const EffectEval& e, Mat4& m, float& opacity) const noexcept override {
        if(kind_==Raster)return false; // Resample the pixels at this position in the effect stack.
        evaluate(e,m,opacity); return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        Mat4 m; float a; evaluate(e,m,a); const auto id=Mat4::identity();
        if (std::abs(a-1)>1e-6f) return false;
        for(int i=0;i<4;++i) { const auto d=m.col[i]-id.col[i]; if(std::abs(d.x)+std::abs(d.y)+std::abs(d.z)+std::abs(d.w)>1e-5f) return false; }
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override { out.push_back(PipelineKey::fullscreen(ShaderId::effects_affine_resample_frag,f)); }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,float margin,LayerImage& out) const override {
        Mat4 m; float a; evaluate(e,m,a); return affine_pass(ctx,in,m,e.placement,a,info().key,margin,out,e.texelScale);
    }
    bool demo_values(EffectInstance&,std::vector<ParamValue>& v) const noexcept override {
        if(kind_==Pulse) v[3]=ParamValue::scalar(.25f);
        else if(kind_==Spin) v[1]=ParamValue::scalar(25);
        else if(kind_==Raster) v[2]=ParamValue::scalar(15);
        else if(kind_==ScaleAssist) v[1]=ParamValue::scalar(70);
        return true;
    }
};

class LensExtra final : public Effect {
    bool squeeze_;
public:
    explicit LensExtra(bool squeeze):squeeze_(squeeze){}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo s{"aurea.distort.squeeze","Squeeze","Distorcer",EffectClass::Domain};
        static const EffectInfo f{"aurea.distort.fisheye","Fish Eye","Distorcer",EffectClass::Domain}; return squeeze_?s:f;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("strength","Força",squeeze_?25:60,squeeze_?-95:0,squeeze_?95:160,kParamAnimatable,squeeze_?"%":"°");
        p.add_point2("center","Centro",{.5f,.5f},-1,2,kParamAnimatable|kParamRelative);
        if(squeeze_) p.add_angle("angle","Eixo",0); else p.add_bool("reverse","Inverter distorção",false);
        // Squeeze novo (beta 07/10: "não parece squeeze"): o antigo era um
        // esticar uniforme (igual ao Stretch Axis). O novo aperta a cintura no
        // eixo e estufa no outro, com queda suave até a borda da layer.
        // Instância salva sem o slot abre com 0 = o esticar de antes.
        if(squeeze_) p.add_float("algorithm","Algoritmo",1.0f,0.0f,1.0f,kParamHidden|kParamLegacyZero);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return std::abs(e.f(0))<.00001f; }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat f) const override { out.push_back(PipelineKey::fullscreen(ShaderId::effects_lens_extra_frag,f)); }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,float,LayerImage& out) const override {
        auto u=base_uniforms(in); const Vec2 sz=size_of(e), c=e.p2(1);
        if(squeeze_ && e.count>3 && e.f(3)>.5f) return build_pinch(ctx,e,in,sz,c,out);
        u.p0={in.region.x,in.region.y,in.region.w,in.region.h}; u.p1={sz.x,sz.y,c.x*sz.x,c.y*sz.y};
        u.p2={squeeze_?0.f:1.f,safe(e.f(0)),squeeze_?e.f(2)*kDeg2Rad:(e.b(2)?1.f:0.f),0};
        return single_pass(ctx,ShaderId::effects_lens_extra_frag,in,u,info().key,out);
    }
private:
    // Squeeze com queda suave (ver lens_extra.frag, modo 2). Em coordenadas
    // normalizadas pelo meio-tamanho da layer no eixo girado:
    //   x' = x·(1 − k·b(y)),  y' = y·(1 + k·b(x)),  b(t) = (1 − t²)² em |t| < 1.
    // Os cantos não se movem; a cintura entra k e o meio do outro lado sai k,
    // então a saída cresce até k·meio-tamanho em volta da entrada.
    Status build_pinch(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,Vec2 sz,Vec2 c,LayerImage& out) const {
        const float k=std::clamp(safe(e.f(0))/100.f,-.95f,.95f), ang=safe(e.f(2))*kDeg2Rad;
        const float cs=std::fabs(std::cos(ang)), sn=std::fabs(std::sin(ang));
        const float rx=std::max(.5f,.5f*(cs*sz.x+sn*sz.y)), ry=std::max(.5f,.5f*(sn*sz.x+cs*sz.y));
        const float grow=std::fabs(k)*std::max(rx,ry)+2.f;
        Rect region{in.region.x-grow,in.region.y-grow,in.region.w+2*grow,in.region.h+2*grow};
        u32 w=0,h=0; ctx.region_size(region,in.texel_scale_x(),w,h);
        EffectUniforms u=base_uniforms(in);
        u.p0={region.x,region.y,region.w,region.h}; u.p1={sz.x,sz.y,c.x*sz.x,c.y*sz.y};
        u.p2={2.f,k,ang,0}; u.p3={in.region.x,in.region.y,in.region.w,in.region.h}; u.color={rx,ry,0,0};
        const FGTexture tex=ctx.texture(info().key,w,h);
        if(ctx.fullscreen_pass(info().key,PassStage::Effects,tex,ShaderId::effects_lens_extra_frag,
                               {PassTexture{in.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex)
            return Errc::PipelineCompileFailed;
        out=LayerImage{tex,region,w,h};
        return OkStatus;
    }
};
}
void register_motion_extras(EffectRegistry& r) {
    for(int k=Blink;k<=Raster;++k) (void)r.add(std::make_unique<MotionExtra>(MotionKind(k)));
    (void)r.add(std::make_unique<LensExtra>(true)); (void)r.add(std::make_unique<LensExtra>(false));
}
}
