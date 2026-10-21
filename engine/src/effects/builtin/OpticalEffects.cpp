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
// Estilos do Reflexo de lente / Flare 3D (Optical Flares). 0 = Clássico (o
// shader de sempre); 1.. = receitas do optical_flares.frag. Cores lineares.
struct FlareStyle {
    Vec4 core, coreColor, streak, streakColor, rays, rayColor, ring;
    u32 ghosts; f32 ghostGain;
    Vec4 ghost[8];        // t na reta, tamanho, forma (0 círculo, 1 hexágono), suavidade
    Vec3 ghostColor[8];
};
constexpr u32 kFlareStyles = 9;
const FlareStyle& flare_style(u32 i) noexcept {
    static const FlareStyle styles[kFlareStyles] = {
        {},
        // 1 Anamórfico azul
        {{.15f, 6, .02f, .05f}, {.7f, .85f, 1, 0}, {.9f, 2.5f, .006f, 0}, {.3f, .55f, 1, 0}, {}, {}, {.04f, .5f, .03f, .02f},
         5, .12f, {{.3f, .03f, 0, .4f}, {.55f, .05f, 0, .5f}, {.8f, .02f, 0, .3f}, {1.1f, .08f, 0, .6f}, {1.4f, .04f, 0, .4f}},
         {{.3f, .5f, 1}, {.2f, .4f, .9f}, {.5f, .7f, 1}, {.15f, .3f, .8f}, {.4f, .6f, 1}}},
        // 2 Sol de verão
        {{.35f, 3, .03f, .08f}, {1, .8f, .5f, 0}, {.25f, .8f, .02f, 0}, {1, .7f, .4f, 0}, {.25f, 12, 6, 2.5f}, {1, .75f, .45f, 0},
         {.08f, .35f, .04f, .03f}, 6, .1f,
         {{.25f, .04f, 1, .3f}, {.45f, .07f, 1, .4f}, {.7f, .03f, 0, .3f}, {.95f, .1f, 1, .5f}, {1.25f, .05f, 0, .3f}, {1.55f, .14f, 1, .6f}},
         {{1, .6f, .2f}, {.9f, .8f, .3f}, {.5f, 1, .4f}, {1, .45f, .15f}, {.4f, .8f, 1}, {1, .7f, .3f}}},
        // 3 Estrela
        {{.2f, 5, .03f, .05f}, {1, 1, 1, 0}, {.4f, 1.2f, .006f, 1}, {1, 1, 1, 0}, {.5f, 6, 18, 1.8f}, {1, .97f, .92f, 0}, {},
         3, .08f, {{.5f, .03f, 0, .4f}, {.9f, .05f, 0, .5f}, {1.3f, .025f, 0, .4f}}, {{.8f, .9f, 1}, {1, .9f, .8f}, {.8f, 1, .9f}}},
        // 4 Neon
        {{.25f, 4, .02f, .05f}, {1, .2f, .8f, 0}, {.8f, 2, .008f, 0}, {.1f, .9f, 1, 0}, {.15f, 8, 10, 3}, {1, .3f, .85f, 0},
         {.06f, .4f, .03f, .04f}, 6, .14f,
         {{.3f, .04f, 1, .3f}, {.5f, .06f, 1, .4f}, {.75f, .03f, 1, .3f}, {1, .09f, 1, .5f}, {1.3f, .05f, 1, .3f}, {1.6f, .12f, 1, .5f}},
         {{.1f, .9f, 1}, {1, .2f, .8f}, {.5f, .3f, 1}, {.1f, 1, .7f}, {1, .3f, .6f}, {.2f, .6f, 1}}},
        // 5 Cinema 50 mm
        {{.18f, 5, .025f, .05f}, {1, .92f, .82f, 0}, {.35f, 1.4f, .01f, 0}, {1, .85f, .7f, 0}, {.12f, 10, 8, 3}, {1, .9f, .8f, 0},
         {.05f, .3f, .02f, .03f}, 8, .1f,
         {{.2f, .02f, 1, .3f}, {.35f, .05f, 1, .4f}, {.5f, .03f, 1, .3f}, {.7f, .09f, 1, .5f}, {.9f, .04f, 1, .3f}, {1.1f, .13f, 1, .6f},
          {1.35f, .06f, 1, .4f}, {1.6f, .2f, 1, .7f}},
         {{.6f, .8f, 1}, {1, .6f, .3f}, {.5f, 1, .6f}, {.9f, .5f, 1}, {1, .8f, .4f}, {.4f, .7f, 1}, {1, .5f, .4f}, {.6f, .9f, .8f}}},
        // 6 Arco-íris
        {{.15f, 6, .02f, .05f}, {1, 1, 1, 0}, {}, {}, {}, {}, {.35f, .45f, .05f, .06f}, 2, .08f,
         {{.7f, .06f, 0, .5f}, {1.3f, .1f, 0, .6f}}, {{.6f, .8f, 1}, {1, .8f, .6f}}},
        // 7 Ficção científica
        {{.2f, 5, .03f, .04f}, {.3f, 1, .9f, 0}, {1, 3, .004f, .5f}, {.3f, 1, .95f, 0}, {.2f, 4, 30, 2}, {.4f, 1, .9f, 0},
         {.05f, .25f, .015f, .02f}, 4, .12f, {{.4f, .03f, 0, .3f}, {.75f, .06f, 0, .4f}, {1.1f, .02f, 0, .3f}, {1.45f, .09f, 0, .5f}},
         {{.2f, 1, .9f}, {.3f, .8f, 1}, {.5f, 1, .8f}, {.2f, .7f, 1}}},
        // 8 Pôr do sol
        {{.45f, 2.5f, .04f, .1f}, {1, .55f, .2f, 0}, {.3f, 1, .015f, 0}, {1, .5f, .2f, 0}, {.1f, 16, 4, 2}, {1, .6f, .3f, 0},
         {.04f, .6f, .06f, .03f}, 6, .1f,
         {{.3f, .05f, 0, .5f}, {.5f, .08f, 0, .6f}, {.75f, .04f, 0, .4f}, {1, .12f, 0, .6f}, {1.3f, .06f, 0, .5f}, {1.6f, .16f, 0, .7f}},
         {{1, .45f, .15f}, {.8f, .3f, .7f}, {1, .6f, .2f}, {.6f, .25f, .8f}, {1, .5f, .3f}, {.9f, .35f, .5f}}},
    };
    return styles[std::min(i, kFlareStyles - 1)];
}
struct FlareUniforms {
    EffectUniforms base;
    Vec4 core, coreColor, streak, streakColor, rays, rayColor, ring, ghostInfo;
    Vec4 ghost[8], ghostColor[8];
};
static_assert(sizeof(FlareUniforms) <= 1024, "uniforms do optical_flares.frag");

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
            // Optical Flares (no fim: os índices de antes continuam valendo).
            static const char* const styles[] = {"Clássico", "Anamórfico azul", "Sol de verão", "Estrela", "Neon",
                                                 "Cinema 50 mm", "Arco-íris", "Ficção científica", "Pôr do sol"};
            p.add_enum("style", "Estilo", styles, kFlareStyles, 0);
            p.add_float("tint_amount", "Tingir o estilo", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
            p.add_angle("rotation", "Giro do risco", 0.f);
            p.add_float("streak", "Risco", 100.f, 0.f, 400.f, kParamAnimatable | kParamPercent, "%");
            p.add_float("rays", "Raios", 100.f, 0.f, 400.f, kParamAnimatable | kParamPercent, "%");
            p.add_float("flicker", "Cintilação", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
            // Seções do painel (só apresentação; índices intactos).
            p.add_section("color", "Cor", {5, 7});
            p.add_section("elements", "Elementos", {4, 8, 9, 10, 11}, kInvalidIndex, false);
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
    f32 input_margin(const EffectEval& e) const noexcept override {
        if (mode_ == 1) return e.f(2);
        if (mode_ != 2 || !e.placement) return 0.f;
        // Compensação óptica (0.2.7): a bolha LÊ pixels fora da camada (num
        // 16:9 a 96°, centenas de px acima e abaixo). Com margem 0 o Motion
        // Tile antes dela só ladrilhava o quadro visível e a borda saía preta.
        // O alcance é a mesma conta do shader (optical.frag) nos pontos da
        // borda da camada: o mapeamento é radial e monótono, então o ponto
        // mais longe lido em cada direção vem da borda.
        const f32 w = static_cast<f32>(e.placement->layerWidth);
        const f32 h = static_cast<f32>(e.placement->layerHeight);
        if (!(w >= 1.f && h >= 1.f)) return 0.f;
        const f32 cx = e.f(0) * w / 100.f, cy = e.f(1) * h / 100.f;
        const f32 halfWidth = std::max(w * .5f, 1.f);
        const f32 theta = std::clamp(e.f(2), .001f, 160.f) * .5f * 0.017453292519943295f;
        const bool reverse = e.f(3) > .5f;
        const f32 limit = 2.f * std::max(w, h);
        f32 reach = 0.f;
        constexpr u32 kSide = 48;
        for (u32 i = 0; i <= kSide * 4; ++i) {
            const f32 s = static_cast<f32>(i % kSide) / kSide;
            const u32 side = std::min(i / kSide, 3u);
            const f32 px = side == 0 ? s * w : side == 1 ? w : side == 2 ? (1.f - s) * w : 0.f;
            const f32 py = side == 0 ? 0.f : side == 1 ? s * h : side == 2 ? h : (1.f - s) * h;
            const f32 dx = px - cx, dy = py - cy;
            const f32 radius = std::sqrt(dx * dx + dy * dy);
            if (radius < .001f) continue;
            const f32 r = radius / halfWidth;
            f32 mapped;
            if (reverse) {
                if (r * theta >= 1.5707f) continue;   // o shader devolve transparente ali
                mapped = std::tan(r * theta) / std::tan(theta);
            } else {
                mapped = std::atan(r * std::tan(theta)) / theta;
            }
            const f32 k = mapped / r;
            const f32 sx = cx + dx * k, sy = cy + dy * k;
            const f32 out = std::max({-sx, sx - w, -sy, sy - h, 0.f});
            if (std::isfinite(out)) reach = std::max(reach, out);
            else reach = limit;
        }
        return reach > 0.f ? std::min(reach + 2.f, limit) : 0.f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_optical_frag, work));
        if (mode_ == 0 || mode_ == 3) out.push_back(PipelineKey::fullscreen(ShaderId::effects_optical_flares_frag, work));
    }
    /// Reflexo de lente é LUZ: o clarão, o risco e os reflexos internos passam
    /// da caixa da layer (num texto ela é justa nos glifos e o reflexo saía
    /// cortado num retângulo). A saída cobre o clarão até cair abaixo de
    /// 1/255 e a linha dos reflexos; no 2D, recortada ao quadro visível.
    static Rect flare_region(const Rect& in, Vec2 light, f32 w, f32 h, f32 brightness, f32 size,
                             const LayerPlacement* placement, f32 margin, f32 styleReach = 0.f) noexcept {
        const bool clipped = placement && !placement->inScene3d && !placement->preserveFullExtent
                          && placement->compWidth && placement->compHeight;
        const f32 unit = std::max(std::min(w, h) * size / 100.f, 1.f);
        // núcleo .004/r² e risco e^(−5|x|)·.5, vezes o brilho, abaixo de 1/255.
        const f32 gain = std::clamp(brightness / 100.f, 0.f, 20.f);
        f32 reach = unit * std::min(std::max({1.25f, std::sqrt(1.1f * gain), styleReach}), 8.f);
        // Sem recorte (cena 3D): o mesmo alcance do 2D numa camada pequena — o
        // teste 2D × 3D cobra isso —, mas uma mídia grande não passa da caixa.
        if (!clipped) reach = std::min(reach, std::max(.5f * std::max(w, h), 512.f));
        if (!std::isfinite(reach) || !std::isfinite(light.x) || !std::isfinite(light.y)) return in;
        // Reflexos: de luz + .4·eixo até o espelho do ponto pelo centro.
        const f32 ghost = (styleReach > 0.f ? .25f : .06f) * unit;
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
        const u32 style = (mode_ == 0 || mode_ == 3) ? std::min(e.e(6), kFlareStyles - 1) : 0u;
        if (style == 0 && mode_ != 0) return single_pass(ctx, ShaderId::effects_optical_frag, input, u, "optical", out);
        const FlareStyle& fs = flare_style(style);
        // O risco longo e os reflexos do estilo vão além do clarão clássico.
        const f32 styleReach = style ? std::max(1.6f, fs.streak.y * 2.5f) : 0.f;
        const Rect region = flare_region(input.region, Vec2{u.p0.x, u.p0.y}, w, h, e.f(2), e.f(3), e.placement, margin, styleReach);
        u32 ow = 0, oh = 0;
        ctx.region_size(region, input.texel_scale_x(), ow, oh);
        // O shader lê o pixel da layer pela região da SAÍDA (p2) e a imagem
        // de entrada pelo uvMap; fora dela o amostrador é transparente.
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.p2 = {region.x, region.y, region.w, region.h};
        if (style) {
            FlareUniforms f{};
            f.base = u;
            const f32 unit = std::max(std::min(w, h) * e.f(3) / 100.f, 1.f);
            // Cintilação: ruído fixo por quadro (mesmo quadro = mesmo brilho, prévia = export).
            const f32 t = static_cast<f32>(e.time_frames());
            const f32 n = std::sin(t * 12.9898f) * 43758.5453f;
            const f32 flicker = 1.f - std::clamp(e.f(11) * .01f, 0.f, 1.f) * .45f * (n - std::floor(n));
            f.base.p0 = {u.p0.x, u.p0.y, unit, e.f(2) / 100.f};
            f.base.p1 = {w * .5f, h * .5f, e.f(8) * kDeg2Rad, flicker};
            f.base.p3 = {e.f(4) / 60.f, e.f(9) / 100.f, e.f(10) / 100.f, 0.f};
            const Vec4 tint = e.color(5);
            f.base.color = {tint.x, tint.y, tint.z, std::clamp(e.f(7) * .01f, 0.f, 1.f)};
            f.core = fs.core; f.coreColor = fs.coreColor; f.streak = fs.streak; f.streakColor = fs.streakColor;
            f.rays = fs.rays; f.rayColor = fs.rayColor; f.ring = fs.ring;
            f.ghostInfo = {static_cast<f32>(fs.ghosts), fs.ghostGain, 0.f, 0.f};
            for (u32 i = 0; i < 8; ++i) {
                f.ghost[i] = fs.ghost[i];
                f.ghostColor[i] = {fs.ghostColor[i].x, fs.ghostColor[i].y, fs.ghostColor[i].z, 0.f};
            }
            out = LayerImage{ctx.texture("optical-flares", ow, oh), region, ow, oh};
            if (ctx.fullscreen_pass("optical-flares", PassStage::Effects, out.texture, ShaderId::effects_optical_flares_frag,
                    {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &f, sizeof(f)) == kInvalidIndex)
                return Errc::PipelineCompileFailed;
            return OkStatus;
        }
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
