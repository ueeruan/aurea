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
        // The bright image is half density. Extract each source sample before
        // averaging, so a small highlight is not erased by the threshold.
        u.base.texel.x=region.w/static_cast<f32>(width)/input.region.w;
        u.base.texel.y=region.h/static_cast<f32>(height)/input.region.h;
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

enum class ScatteringKind : u32 { Glint = 1, Glare, EdgeRays, Prismatic };

struct ScatteringUniforms {
    EffectUniforms base;
    Vec4 rays[8], colors[15], mapTypes, region, size, circle;
};
static_assert(sizeof(ScatteringUniforms) == 544);

class ScatteringLight final : public Effect {
public:
    explicit ScatteringLight(ScatteringKind kind) : kind_(kind) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {"aurea.light.spectral_glint", "Reflexos espectrais", "Luz", EffectClass::Neighborhood},
            {"aurea.light.diffraction_glare", "Difração luminosa", "Luz", EffectClass::Neighborhood},
            {"aurea.light.outline_rays", "Raios de contorno", "Luz", EffectClass::Domain},
            {"aurea.light.prismatic_glints", "Reflexos prismáticos", "Luz", EffectClass::Neighborhood},
        };
        return infos[static_cast<u32>(kind_) - 1];
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("brightness", "Intensidade da luz", edges() ? 2.0f : 1.0f, 0, 16, kParamAnimatable);
        p.add_float("size", edges() ? "Comprimento dos raios" : "Tamanho",
                    edges() ? .25f : glint() ? 2.0f : .8f, edges() ? -5.0f : 0.0f, 8, kParamAnimatable);
        p.add_color("tint", "Cor da luz", {1, 1, 1, 1});
        p.add_float("source_opacity", "Opacidade da fonte", 1, 0, 1, kParamAnimatable);
        p.add_float("background_gain", "Brilho do fundo", 1, 0, 8, kParamAnimatable);
        p.add_float("affect_alpha", "Propagação no alfa", 1, 0, 8, kParamAnimatable);
        p.add_float("under_source", "Luz sob a fonte", 0, 0, 1, kParamAnimatable);
        if (edges()) {
            static const char* const views[] = {"Resultado", "Contornos", "Raios"};
            p.add_point2("center", "Centro", {.5f, .5f}, -2, 3, kParamAnimatable | kParamRelative);
            p.add_float("edge_thickness", "Espessura do contorno", 2.42f, 0, 40, kParamAnimatable | kParamPixels, "px");
            p.add_float("edge_gain", "Intensidade do contorno", 1, 0, 10, kParamAnimatable);
            p.add_float("outer_bias", "Brilho na extremidade", 0, 0, 1, kParamAnimatable);
            p.add_float("shimmer", "Cintilação", .5f, 0, 4, kParamAnimatable);
            p.add_float("shimmer_frequency", "Frequência da cintilação", 40, .1f, 160, kParamAnimatable);
            p.add_enum("view", "Mostrar", views, 3, 0);
            p.add_float("length_red", "Comprimento do vermelho", 1, 0, 4, kParamAnimatable);
            p.add_float("length_green", "Comprimento do verde", 1, 0, 4, kParamAnimatable);
            p.add_float("length_blue", "Comprimento do azul", 1, 0, 4, kParamAnimatable);
        } else {
            p.add_float("threshold", "Limiar", glint() ? .7f : .8f, 0, 2, kParamAnimatable);
            p.add_float("threshold_blur", "Suavidade do limiar", glint() ? 2.15f : 12.0f, 0, 40,
                        kParamAnimatable | kParamPixels, "px");
            p.add_float("blur", "Desfoque da luz", 0, 0, 120, kParamAnimatable | kParamPixels, "px");
            p.add_color("threshold_color", "Cor do limiar", {0, 0, 0, 1});
            p.add_angle("angle", "Rotação da luz", 0);
            p.add_float("from_alpha", "Gerar do alfa", 0, 0, 1, kParamAnimatable);
            p.add_float("length_red", "Comprimento do vermelho", glint() ? .5f : 1.0f, 0, 4, kParamAnimatable);
            p.add_float("length_green", "Comprimento do verde", 1, 0, 4, kParamAnimatable);
            p.add_float("length_blue", "Comprimento do azul", glint() ? 1.5f : 1.0f, 0, 4, kParamAnimatable);
            if (glint()) {
                p.add_float("horizontal", "Comprimento horizontal", 1, 0, 4, kParamAnimatable);
                p.add_float("vertical", "Comprimento vertical", 1, 0, 4, kParamAnimatable);
                p.add_float("diagonal_1", "Comprimento da primeira diagonal", .75f, 0, 4, kParamAnimatable);
                p.add_float("diagonal_2", "Comprimento da segunda diagonal", .75f, 0, 4, kParamAnimatable);
                if (prismatic()) {
                    p.add_float("shift_out", "Afastar raios", 1, -4, 4);
                    p.add_float("shift_red", "Deslocamento do vermelho", .3f, -4, 4);
                    p.add_float("shift_blue", "Deslocamento do azul", -.3f, -4, 4);
                }
            } else {
                p.add_float("ray_length", "Comprimento dos raios", 1, 0, 4, kParamAnimatable);
                p.add_float("diffuse", "Intensidade da luz difusa", 1, 0, 4, kParamAnimatable);
                p.add_float("saturation", "Saturação", 1, -2, 8, kParamAnimatable);
            }
        }
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kBrightness) == 0 && e.f(kOpacity) == 1 && e.f(kBackground) == 1
            && (!edges() || e.e(kStart + 6) == 0);
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_starglow_frag, f));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, f));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, f));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const auto setting = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            const f32 value = e.f(index);
            return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
        };
        ScatteringUniforms u{};
        u.base.p3.w = static_cast<f32>(kind_);
        const auto pass = [&](const char* label, f32 stage, const LayerImage& first,
                              const LayerImage& second, u32 w, u32 h, LayerImage& target) -> Status {
            u.base.p3.x = stage;
            target = {ctx.texture(label, w, h), input.region, w, h};
            if (ctx.fullscreen_pass(label, PassStage::Effects, target.texture, ShaderId::effects_starglow_frag,
                {PassTexture{first.texture, {}, CommonSampler::LinearBorder},
                 PassTexture{second.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u)) == kInvalidIndex) {
                return Errc::PipelineCompileFailed;
            }
            return OkStatus;
        };
        const auto soften = [&](const LayerImage& source, f32 sigma, LayerImage& target) {
            BlurRequest request;
            request.sigmaX = request.sigmaY = sigma;
            request.repeatEdges = true;
            return build_gaussian(ctx, source, request, target);
        };

        LayerImage encoded, smooth, field, light;
        if (const auto s = pass("luz-scatter-codificar", 0, input, input, input.width, input.height, encoded); !s.ok()) return s;
        const f32 smoothing = edges() ? setting(kStart + 1, 2.42f, 0, 40)
            : setting(kStart + 1, glint() ? 2.15f : 12.0f, 0, 40);
        if (const auto s = soften(encoded, smoothing, smooth); !s.ok()) return s;
        const f32 regionWidth = std::max(input.region.w, 1.0f);
        const f32 regionHeight = std::max(input.region.h, 1.0f);
        f32 postBlur = 0;
        if (edges()) {
            u.base.p0.x = setting(kStart + 2, 1, 0, 10) * 9.19f;
            u.base.texel = {1.0f / regionWidth, 1.0f / regionHeight, 0, 0};
            if (const auto s = pass("raios-campo-contorno", 1, smooth, smooth, input.width, input.height, field);
                !s.ok()) return s;
            const u32 view = std::min<u32>(e.e(kStart + 6), 2);
            if (view == 1) {
                u.base.p0 = {};
                return pass("raios-mostrar-contornos", 5, field, field, input.width, input.height, out);
            }
            Vec2 center = e.p2(kStart);
            center.x = std::isfinite(center.x) ? center.x : .5f;
            center.y = std::isfinite(center.y) ? center.y : .5f;
            if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
                center.x = (center.x * e.placement->layerWidth - input.region.x) / regionWidth;
                center.y = (center.y * e.placement->layerHeight - input.region.y) / regionHeight;
            }
            u.circle = {center.x, center.y, setting(kSize, .25f, -5, 3) * 1.028f, setting(kStart + 3, 0, 0, 1)};
            u.rays[0] = {setting(kStart + 7, 1, 0, 4), setting(kStart + 8, 1, 0, 4),
                         setting(kStart + 9, 1, 0, 4), 0};
            u.base.p1 = {setting(kStart + 4, .5f, 0, 4), setting(kStart + 5, 40, .1f, 160), 0, 0};
            if (const auto s = pass("raios-integrar-contornos", 2, field, field, input.width, input.height, light);
                !s.ok()) return s;
            if (view == 2) {
                u.base.p0 = {setting(kBrightness, 2, 0, 16), 1, 0, 0};
                return pass("raios-mostrar-luz", 5, light, light, input.width, input.height, out);
            }
        } else {
            u.base.p0 = {setting(kStart, glint() ? .7f : .8f, 0, 2), setting(kStart + 5, 0, 0, 1), 0, 0};
            u.base.color = e.color(kStart + 3);
            const u32 width = std::max(1u, (input.width + 1) / 2);
            const u32 height = std::max(1u, (input.height + 1) / 2);
            if (const auto s = pass("luz-scatter-extrair", 1, smooth, smooth, width, height, field); !s.ok()) return s;
            const f32 size = setting(kSize, glint() ? 2.0f : .8f, 0, 8);
            const f32 angle = setting(kStart + 4, 0, -360000, 360000) * kDeg2Rad;
            const f32 lengths[]{setting(kStart + 6, glint() ? .5f : 1.0f, 0, 4),
                                setting(kStart + 7, 1, 0, 4),
                                setting(kStart + 8, glint() ? 1.5f : 1.0f, 0, 4)};
            postBlur = setting(kStart + 2, 0, 0, 120);
            const u32 axisCount = glint() ? 4u : 2u;
            for (u32 axis = 0; axis < axisCount; ++axis) {
                const f32 orientation = angle + (glint() ? axis * kPi * .25f : (1.0f + 2.0f * axis) * kPi * .25f);
                const f32 nominal = glint() ? (axis % 2 ? .75f : 1.0f) : 1.0f;
                const u32 slot = kStart + (glint() ? axis == 0 ? 9 : axis == 2 ? 10 : axis == 1 ? 11 : 12 : 9);
                const f32 length = setting(slot, nominal, 0, 4);
                const f32 scale = (size / (glint() ? 2.0f : .8f)) * length / nominal;
                const f32 spacing[] = {1.0f, 4.0f, 16.0f, 52.0f, 138.0f};
                const f32 reference[]{prismatic() ? 82.0f : glint() ? 80.0f : 24.0f,
                                      prismatic() ? 90.0f : glint() ? 158.0f : 34.0f,
                                      prismatic() ? 94.0f : glint() ? 219.0f : 48.0f};
                const f32 defaultLength[]{glint() ? .5f : 1.0f, 1.0f, glint() ? 1.5f : 1.0f};
                const f32 pixelScale = prismatic() ? std::min(regionWidth, regionHeight) / 480.f : 1.f;
                const f32 axisScale = glint() ? (axis % 2 ? (prismatic() ? 1.05f : 1.10f) : 1.0f) : .24f;
                LayerImage axisImage = field;
                // Successive short convolutions integrate continuous narrow
                // lines without a long sparse sampling loop losing emitters.
                for (u32 stage = 0; stage < 5; ++stage) {
                    const f32 px = spacing[stage] * scale * axisScale * pixelScale;
                    u.rays[0] = {std::cos(orientation) * px / regionWidth, std::sin(orientation) * px / regionHeight, 0, 0};
                    for (u32 c = 0; c < 3; ++c) {
                        const f32 sigma = reference[c] * lengths[c] / defaultLength[c];
                        const f32 effective = sigma / (glint() ? 148.6f : 35.66f);
                        const f32 w1 = effective > .01f ? std::exp(-.5f / (effective * effective)) : 0.0f;
                        const f32 w2 = w1*w1*w1*w1;
                        const f32 divisor = 1 + 2*w1 + 2*w2;
                        u.colors[c] = {1 / divisor, w1 / divisor, w2 / divisor, 0};
                    }
                    LayerImage filtered;
                    if (const auto s = pass("luz-scatter-eixo", 2, axisImage, axisImage, width, height, filtered); !s.ok()) return s;
                    axisImage = filtered;
                }
                if (prismatic() && scale > 0) {
                    const f32 shift = setting(kStart + 13, 1, -4, 4);
                    const f32 red = setting(kStart + 14, .3f, -4, 4);
                    const f32 blue = setting(kStart + 15, -.3f, -4, 4);
                    // The three impulse peaks sit far from the emitter along
                    // each ray. Express that distance in units of layer size.
                    const f32 separation = .52f * scale * std::min(regionWidth, regionHeight);
                    u.rays[1] = {std::cos(orientation) * separation / regionWidth,
                                 std::sin(orientation) * separation / regionHeight, 0, 0};
                    u.base.p2 = {shift + red, shift + (red + blue) * .5f, shift + blue, 0};
                    LayerImage dispersed;
                    if (const auto s = pass("glint-dispersar-raio", 6, axisImage, axisImage, width, height, dispersed);
                        !s.ok()) return s;
                    axisImage = dispersed;
                }
                if (axis == 0) light = axisImage;
                else {
                    LayerImage combined;
                    u.base.p1 = {1, 1, 0, 0};
                    if (const auto s = pass("luz-scatter-somar-eixos", 3, axisImage, light, width, height, combined); !s.ok()) return s;
                    light = combined;
                }
            }
            if (!glint()) {
                LayerImage halo, combined;
                // The outer skirt reaches much farther than the narrow
                // diffraction lobes; keep their energy separate.
                if (const auto s = soften(field, 70.0f * size / .8f, halo); !s.ok()) return s;
                u.base.p1 = {setting(kStart + 10, 1, 0, 4) * 1.18f, .065f, 0, 0};
                if (const auto s = pass("difracao-luz-difusa", 3, halo, light, width, height, combined); !s.ok()) return s;
                light = combined;
            }
            if (postBlur > 0) {
                LayerImage softened;
                if (const auto s = soften(light, postBlur, softened); !s.ok()) return s;
                light = softened;
            }
        }
        u.base.p0 = {setting(kBrightness, edges() ? 2.0f : 1.0f, 0, 16),
                     setting(kOpacity, 1, 0, 1), setting(kBackground, 1, 0, 8), setting(kAlpha, 1, 0, 8)};
        u.base.p1 = {setting(kUnder, 0, 0, 1), !glint() && !edges() ? setting(kStart + 11, 1, -2, 8) : 1, 0, 0};
        u.base.color = e.color(kTint);
        return pass("luz-scatter-compor", 4, encoded, light, input.width, input.height, out);
    }
private:
    enum : u32 { kBrightness, kSize, kTint, kOpacity, kBackground, kAlpha, kUnder, kStart };
    ScatteringKind kind_;
    bool edges() const noexcept { return kind_ == ScatteringKind::EdgeRays; }
    bool prismatic() const noexcept { return kind_ == ScatteringKind::Prismatic; }
    bool glint() const noexcept { return kind_ == ScatteringKind::Glint || prismatic(); }
};

class LightLeak final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.light.light_leak", "Vazamento de luz", "Luz", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Tela", "Somar", "Só o vazamento"};
        p.add_float("brightness", "Intensidade da luz", 1, 0, 8);
        p.add_color("tint", "Cor da luz", {1, 1, 1, 1});
        p.add_point2("center", "Centro", {.5f, .5f}, -2, 3, kParamAnimatable | kParamRelative);
        p.add_float("size", "Tamanho", 1, .025f, 4);
        p.add_float("height", "Altura relativa", 1, .025f, 4);
        p.add_angle("angle", "Ângulo do movimento", 175);
        p.add_float("speed", "Velocidade", 1, -8, 8);
        p.add_float("flicker", "Cintilação", .2f, 0, 1);
        p.add_float("frequency", "Frequência da cintilação", 4, 0, 60);
        p.add_float("background", "Brilho do fundo", 1, 0, 4);
        p.add_float("affect_alpha", "Propagação no alfa", 1, 0, 8);
        p.add_enum("mode", "Combinação", modes, 3, 0);
        p.add_float("seed", "Semente", .123f, 0, 1000);
        p.add_section("shape", "Forma e movimento", {Center, Size, Height, Angle, Speed});
        p.add_section("flicker", "Cintilação", {Flicker, Frequency, Seed});
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(Brightness) <= 0 && e.f(Background) == 1 && e.e(Mode) != 2;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_starglow_frag, f));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const auto setting = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            const f32 value = e.f(index);
            return std::clamp(std::isfinite(value) ? value : fallback, lo, hi);
        };
        const f64 seconds = e.time_frames() / std::max(1.0, e.framesPerSecond);
        const f32 speed = setting(Speed, 1, -8, 8);
        const f32 motion = (std::clamp(e.clipProgress, 0.f, 1.f) - .5f) * speed * .3f;
        const f32 angle = setting(Angle, 175, -360000, 360000) * kDeg2Rad;
        const Vec2 point = e.p2(Center);
        const f32 centerX = std::isfinite(point.x) ? point.x : .5f;
        const f32 centerY = std::isfinite(point.y) ? point.y : .5f;
        const Rect natural = input.virtualSourceRegion.w > 0 ? input.virtualSourceRegion :
            e.placement && e.placement->layerWidth && e.placement->layerHeight
                ? Rect{0, 0, static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)}
                : input.region;
        const f64 flickerPhase = (std::isfinite(seconds) ? seconds : 0.0) * setting(Frequency, 4, 0, 60);

        ScatteringUniforms u{};
        u.base.p3 = {7, 0, 0, 4};
        u.base.p0 = {setting(Brightness, 1, 0, 8), setting(Background, 1, 0, 4),
                     setting(Alpha, 1, 0, 8), static_cast<f32>(std::min(e.e(Mode), 2u))};
        u.base.p1 = {centerX + std::cos(angle) * motion, centerY + std::sin(angle) * motion,
                     setting(Size, 1, .025f, 4), setting(Height, 1, .025f, 4)};
        u.base.p2 = {static_cast<f32>(std::remainder(flickerPhase, 128.0)),
                     setting(Flicker, .2f, 0, 1), setting(Seed, .123f, 0, 1000), motion};
        u.base.color = e.color(Tint);
        u.base.uvMap = {input.region.w / natural.w, input.region.h / natural.h,
                        (input.region.x - natural.x) / natural.w, (input.region.y - natural.y) / natural.h};
        out = input;
        out.texture = ctx.texture("light-leak", input.width, input.height);
        return ctx.fullscreen_pass("light-leak", PassStage::Effects, out.texture, ShaderId::effects_starglow_frag,
            {PassTexture{input.texture}}, &u, sizeof(u)) == kInvalidIndex
            ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }

private:
    enum : u32 { Brightness, Tint, Center, Size, Height, Angle, Speed, Flicker, Frequency,
                 Background, Alpha, Mode, Seed };
};
}
void register_starglow(EffectRegistry& r) {
    (void)r.add(std::make_unique<Starglow>());
    for (const auto kind : {ScatteringKind::Glint, ScatteringKind::Glare, ScatteringKind::EdgeRays,
                            ScatteringKind::Prismatic}) {
        (void)r.add(std::make_unique<ScatteringLight>(kind));
    }
    (void)r.add(std::make_unique<LightLeak>());
}
}
