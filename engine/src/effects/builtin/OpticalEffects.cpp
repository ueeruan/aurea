#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>
namespace aurea::builtin {
namespace {
// Spatial channel separation, independent of temporal RGB offsets.
class ChannelSplit final : public Effect {
public:
    explicit ChannelSplit(bool radial) : radial_(radial) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo split{effect_keys::kRgbSplit,"RGB Split","Color",EffectClass::Domain};
        static const EffectInfo radial{effect_keys::kChromaticAberration,"Chromatic Aberration","Color",EffectClass::Domain};
        return radial_ ? radial : split;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Deslocamentos digitados até ±20000 px (10x o slider): uma leitura por
        // canal, custo fixo; a margem cresce, mas a textura é presa ao quadro
        // visível e ao teto do aparelho.
        if (radial_) {
            p.add_float("amount","Amount",10,-2000,2000,kParamAnimatable|kParamPixels,"px");
            p.typed_range(-20000,20000);
            p.add_float("center_x","Center X",50,-1000,1000,kParamAnimatable|kParamPercent,"%");
            p.add_float("center_y","Center Y",50,-1000,1000,kParamAnimatable|kParamPercent,"%");
        } else {
            const char* keys[]={"red_x","red_y","green_x","green_y","blue_x","blue_y"};
            const char* names[]={"Red X","Red Y","Green X","Green Y","Blue X","Blue Y"};
            const f32 defaults[]={10,0,0,0,-10,0};
            for(u32 i=0;i<6;++i){
                p.add_float(keys[i],names[i],defaults[i],-2000,2000,kParamAnimatable|kParamPixels,"px");
                p.typed_range(-20000,20000);
            }
        }
        p.add_float("mix","Mix",100,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_bool("repeat_edges","Repeat edges",false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if(e.f(radial_?3:6)<=0)return true;
        for(u32 i=0;i<(radial_?1u:6u);++i)if(std::abs(e.f(i))>.0001f)return false;
        return true;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        f32 amount=0;for(u32 i=0;i<(radial_?1u:6u);++i)amount=std::max(amount,std::abs(e.f(i)));
        return amount+1;
    }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_rgb_split_frag,work));
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& input,f32 margin,LayerImage& out) const override {
        const f32 w=e.placement?static_cast<f32>(e.placement->layerWidth):input.region.w;
        const f32 h=e.placement?static_cast<f32>(e.placement->layerHeight):input.region.h;
        auto u=base_uniforms(input);
        if(radial_)u.p0={e.f(0),e.f(1)*w/100,e.f(2)*h/100,std::max(std::min(w,h)*.5f,1.f)};
        else {u.p0={e.f(0),e.f(1),e.f(2),e.f(3)};u.p1={e.f(4),e.f(5),0,0};}
        u.p1.z=e.f(radial_?3:6)/100;u.p1.w=e.f(radial_?4:7);
        u.p2={input.region.x,input.region.y,input.region.w,input.region.h};
        u.p3.x=radial_?1.f:0.f;
        // Keep channel tails outside the source bounds, clipped only to the visible region.
        const f32 extent=input_margin(e);
        const Rect region=spread_region(input.region,extent,extent,e.placement,margin);
        u.uvMap={region.x,region.y,region.w,region.h};
        u32 ow=0,oh=0;ctx.region_size(region,input.texel_scale_x(),ow,oh);
        if(!ow||!oh){out=input;return OkStatus;}
        out={ctx.texture("channel split",ow,oh),region,ow,oh};
        if(ctx.fullscreen_pass("channel split",PassStage::Effects,out.texture,ShaderId::effects_rgb_split_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp}},&u,sizeof(u))==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
private:
    bool radial_;
};
class Optical final : public Effect {
public:
    explicit Optical(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {effect_keys::kLensFlare, "Reflexo de lente", "Luz", EffectClass::Domain},
            {effect_keys::kRipple, "Ondulação radial", "Distorcer", EffectClass::Domain},
            {effect_keys::kOpticsCompensation, "Compensação óptica", "Distorcer", EffectClass::Domain},
            {effect_keys::kSceneFlare, "Flare 3D", "Luz", EffectClass::Domain}
        };
        return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Faixas digitadas: o passe é uma amostra por pixel (o laço de 6 do
        // shader é fixo), então os valores alargam ~5-10x o slider.
        const u32 centerFlags = mode_ == 3 ? kParamHidden : kParamAnimatable | kParamPercent;
        p.add_float("center_x", "Centro X", 50.f, -100.f, 200.f, centerFlags, "%");
        p.typed_range(-1000.f, 1000.f);
        p.add_float("center_y", "Centro Y", 50.f, -100.f, 200.f, centerFlags, "%");
        p.typed_range(-1000.f, 1000.f);
        if (mode_ == 0 || mode_ == 3) {
            p.add_float("brightness", "Brilho", 100.f, 0.f, 500.f, kParamAnimatable | kParamPercent, "%");
            p.typed_range(0.f, 2000.f);
            p.add_float("size", "Tamanho", 100.f, 1.f, 400.f, kParamAnimatable | kParamPercent, "%");
            p.typed_range(1.f, 2000.f);
            p.add_float("ghosts", "Reflexos internos", 60.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
            p.add_color("tint", "Cor", {1.f, .7f, .4f, 1.f});
        } else if (mode_ == 1) {
            p.add_float("amplitude", "Amplitude", 12.f, 0.f, 128.f, kParamAnimatable | kParamPixels, "px");
            p.typed_range(0.f, 1000.f);
            p.add_float("wavelength", "Comprimento da onda", 80.f, 2.f, 2000.f, kParamAnimatable | kParamPixels, "px");
            p.typed_range(2.f, 20000.f);
            p.add_angle("phase", "Fase", 0.f);
            p.add_float("decay", "Atenuação", 0.f, 0.f, 10.f);
            p.typed_range(0.f, 100.f);
        } else {
            // Campo de visão NÃO alarga: 180° é o limite físico (tan 90° = ∞).
            p.add_float("fov", "Campo de visão", 60.f, 0.f, 160.f, kParamAnimatable, "°");
            p.add_bool("reverse", "Inverter distorção", false);
        }
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(2) < .0001f || (mode_ == 3 && (!e.placement || !e.placement->sceneFlare));
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return mode_ == 1 ? e.f(2) : 0.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_optical_frag, work));
    }
    /// Reflexo de lente é LUZ: o clarão, o risco e os reflexos internos passam
    /// da caixa da layer (num texto ela é justa nos glifos e o reflexo saía
    /// cortado num retângulo). A saída cobre o clarão até cair abaixo de
    /// 1/255 e a linha dos reflexos; no 2D, recortada ao quadro visível.
    static Rect flare_region(const Rect& in, Vec2 light, f32 w, f32 h, f32 brightness, f32 size,
                             const LayerPlacement* placement, f32 margin) noexcept {
        const bool clipped = placement && !placement->inScene3d && !placement->preserveFullExtent
                          && placement->compWidth && placement->compHeight;
        const f32 unit = std::max(std::min(w, h) * size / 100.f, 1.f);
        // núcleo .004/r² e risco e^(−5|x|)·.5, vezes o brilho, abaixo de 1/255.
        const f32 gain = std::clamp(brightness / 100.f, 0.f, 20.f);
        f32 reach = unit * std::min(std::max(1.25f, std::sqrt(1.1f * gain)), 8.f);
        // Sem recorte (cena 3D): o mesmo alcance do 2D numa camada pequena — o
        // teste 2D × 3D cobra isso —, mas uma mídia grande não passa da caixa.
        if (!clipped) reach = std::min(reach, std::max(.5f * std::max(w, h), 512.f));
        if (!std::isfinite(reach) || !std::isfinite(light.x) || !std::isfinite(light.y)) return in;
        // Reflexos: de luz + .4·eixo até o espelho do ponto pelo centro.
        const f32 ghost = .06f * unit;
        const f32 mx = w - light.x, my = h - light.y;
        const f32 left = std::min({in.x, light.x - reach, std::min(light.x, mx) - ghost});
        const f32 right = std::max({in.x + in.w, light.x + reach, std::max(light.x, mx) + ghost});
        const f32 top = std::min({in.y, light.y - reach, std::min(light.y, my) - ghost});
        const f32 bottom = std::max({in.y + in.h, light.y + reach, std::max(light.y, my) + ghost});
        return spread_region(Rect{left, top, right - left, bottom - top}, 0.f, 0.f, placement, margin);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin, LayerImage& out) const override {
        auto u = base_uniforms(input);
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        u.p0 = {e.f(0)*w/100.f, e.f(1)*h/100.f, e.f(2), e.f(3)};
        u.p1 = {static_cast<f32>(mode_), mode_ == 2 ? 0.f : e.f(4), mode_ == 1 ? e.f(5) : 0.f, 0.f};
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = {w, h, 0.f, 0.f};
        if (mode_ == 0 || mode_ == 3) u.color = e.color(5);
        if (mode_ == 3 && e.placement) {
            u.p0.x = e.placement->flarePosition.x;
            u.p0.y = e.placement->flarePosition.y;
            u.p1.x = 0;
        }
        if (mode_ != 0) return single_pass(ctx, ShaderId::effects_optical_frag, input, u, "optical", out);
        const Rect region = flare_region(input.region, Vec2{u.p0.x, u.p0.y}, w, h, e.f(2), e.f(3), e.placement, margin);
        u32 ow = 0, oh = 0;
        ctx.region_size(region, input.texel_scale_x(), ow, oh);
        // O shader lê o pixel da layer pela região da SAÍDA (p2) e a imagem
        // de entrada pelo uvMap; fora dela o amostrador é transparente.
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.p2 = {region.x, region.y, region.w, region.h};
        out = LayerImage{ctx.texture("optical", ow, oh), region, ow, oh};
        if (ctx.fullscreen_pass("optical", PassStage::Effects, out.texture, ShaderId::effects_optical_frag,
                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
private:
    u32 mode_;
};
}
void register_optical_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 4; ++mode) (void)r.add(std::make_unique<Optical>(mode));
    (void)r.add(std::make_unique<ChannelSplit>(false));
    (void)r.add(std::make_unique<ChannelSplit>(true));
}
}
