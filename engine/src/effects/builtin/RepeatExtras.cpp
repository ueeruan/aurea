#include "BuiltinEffects.hpp"
#include "aurea/effects/ShakeMotion.hpp"
#include "aurea/scene3d/RepeatLayout.hpp"
#include "aurea/timeline/Layer.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace aurea::repeat_layout {
namespace {
enum : u32 { Line, Grid, Orbit, ScatterLayout };

void toon_parameters(ParameterRegistry& p) {
    p.add_int("bands", "Faixas de luz", 3, 2, 6);
}

f32 percent(const ParamValue& value) noexcept { return value.as_float() * 0.01f; }

Vec3 layout_position(u32 mode, Vec3 step, u32 columns, f32 radius, u32 n, u32 i, Vec3 noise) noexcept {
    if (mode == Grid) {
        const u32 rows = (n + columns - 1) / columns;
        const u32 row = i / columns;
        const u32 inRow = std::min(columns, n - row * columns);
        return {step.x * (static_cast<f32>(i % columns) - (inRow - 1) * 0.5f),
                step.y * (static_cast<f32>(row) - (rows - 1) * 0.5f),
                step.z * (static_cast<f32>(row) - (rows - 1) * 0.5f)};
    }
    if (mode == Orbit) {
        const f32 angle = 6.28318530718f * i / n;
        return {radius * std::cos(angle), radius * std::sin(angle), 0};
    }
    if (mode == ScatterLayout) return {step.x * noise.x, step.y * noise.y, step.z * noise.z};
    return step * (static_cast<f32>(i) - (n - 1) * 0.5f);
}
} // namespace

void declare_parameters(ParameterRegistry& p, bool spatial) {
    static const char* const layouts[]{"Linha", "Grade", "Anel", "Dispersão"};
    p.add_int("count", "Quantidade", 5, 1, kMaxCopies, kParamNone);
    p.add_enum("layout", "Distribuição", layouts, 4, Line);
    if (spatial) p.add_point3("spacing", "Espaçamento", {150, 150, 0}, -4000, 4000);
    else p.add_point2("spacing", "Espaçamento", {100, 100}, -4000, 4000, kParamAnimatable | kParamPixels);
    p.add_int("columns", "Colunas", 3, 1, kMaxCopies, kParamNone);
    p.add_float("radius", "Raio", 200, 0, 4000, kParamAnimatable | kParamPixels, "px");
    p.add_enum("end_layout", "Distribuição de destino", layouts, 4, Orbit);
    p.add_float("morph", "Transição de distribuição", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
    if (spatial) p.add_point3("rotation", "Rotação por cópia", {}, -360, 360);
    else p.add_angle("rotation", "Rotação por cópia", 0);
    p.add_float("end_scale", "Escala da última cópia", 100, 1, 400, kParamAnimatable | kParamPercent, "%");
    if (spatial) p.add_point3("scatter", "Variação de posição", {}, 0, 2000);
    else p.add_point2("scatter", "Variação de posição", {}, 0, 2000, kParamAnimatable | kParamPixels);
    p.add_float("random_rotation", "Variação de rotação", 0, 0, 360, kParamAnimatable, "°");
    p.add_int("seed", "Semente", 1, 0, 999999, kParamNone);
    if (spatial) p.add_point3("focus", "Centro de influência", {}, -4000, 4000);
    else p.add_point2("focus", "Centro de influência", {}, -4000, 4000, kParamAnimatable | kParamPixels);
    p.add_float("focus_radius", "Alcance", 250, 1, 4000, kParamAnimatable | kParamPixels, "px");
    p.add_float("focus_scale", "Escala perto do centro", 100, 1, 400, kParamAnimatable | kParamPercent, "%");
    p.add_float("progress", "Revelação", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
    p.add_float("stagger", "Intervalo entre cópias", 55, 0, 95, kParamAnimatable | kParamPercent, "%");
    p.add_color("color_start", "Cor inicial", {0.18f, 0.63f, 1.0f, 1.0f});
    p.add_color("color_end", "Cor final", {1.0f, 0.28f, 0.55f, 1.0f});
    p.add_float("color_mix", "Intensidade da cor", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
    p.add_bool("use_data", "Usar curva por cópia", false);
    p.add_curve("scale_data", "Dados de escala");
    p.set_enabled_by(ScaleData, UseData, 1);
    p.add_section("repeat_distribution", "Distribuição", {Spacing, Columns, Radius, EndLayout, Morph, Rotation, EndScale});
    p.add_section("repeat_variation", "Aleatoriedade", {Scatter, RandomRotation, Seed}, kInvalidIndex, false);
    p.add_section("repeat_proximity", "Proximidade", {Focus, FocusRadius, FocusScale}, kInvalidIndex, false);
    p.add_section("repeat_stagger", "Escalonar", {Progress, Stagger}, kInvalidIndex, false);
    p.add_section("repeat_colors", "Cores das cópias", {ColorStart, ColorEnd, ColorMix}, kInvalidIndex, false);
    p.add_section("repeat_data", "Dados por cópia", {UseData, ScaleData}, kInvalidIndex, false);
}

const ParameterRegistry& model_parameters() {
    static const ParameterRegistry reg = [] { ParameterRegistry p; declare_parameters(p, true); return p; }();
    return reg;
}

u32 count(const ParamValue* p) noexcept {
    return static_cast<u32>(std::clamp(p[Count].as_int(), 1, static_cast<i32>(kMaxCopies)));
}

Copy sample(const ParamValue* p, const EffectInstance* instance, u32 index) noexcept {
    const u32 n = count(p);
    const u32 i = std::min(index, n - 1);
    const f32 fraction = n > 1 ? static_cast<f32>(i) / (n - 1) : 0.0f;
    const Vec3 spacing = p[Spacing].as_vec3();
    const Vec3 jitter = p[Scatter].as_vec3();
    const u32 columns = std::clamp<u32>(p[Columns].as_enum(), 1, n);
    const u32 seed = p[Seed].as_enum();
    const auto noise = [&](u32 channel) { return shake::random(seed, i, channel); };
    const Vec3 random{noise(0), noise(1), noise(2)};
    Copy out;
    if (n > 1) {
        const auto position = [&](u32 param) {
            return layout_position(p[param].as_enum(), spacing, columns, p[Radius].as_float(), n, i, random);
        };
        const Vec3 from = position(Layout);
        const f32 morph = percent(p[Morph]);
        out.position = from + (position(EndLayout) - from) * morph;
    }
    out.position += Vec3{jitter.x * noise(3), jitter.y * noise(4), jitter.z * noise(5)};
    out.rotation = p[Rotation].as_vec3() * static_cast<f32>(i);
    out.rotation.z += p[RandomRotation].as_float() * noise(6);
    const f32 proximity = 1.0f - std::clamp((out.position - p[Focus].as_vec3()).length() / p[FocusRadius].as_float(), 0.0f, 1.0f);
    out.scale = (1 + (percent(p[EndScale]) - 1) * fraction) *
        (1 + (percent(p[FocusScale]) - 1) * proximity * proximity * (3 - 2 * proximity));
    if (p[UseData].as_bool() && instance && p[ScaleData].ref < instance->curves.size())
        out.scale *= std::max(0.0f, instance->curves[p[ScaleData].ref].evaluate(0, fraction)) * 2.0f;
    out.scale = std::clamp(out.scale, 0.001f, 32.0f);
    const f32 stagger = percent(p[Stagger]);
    const f32 alpha = std::clamp((percent(p[Progress]) - fraction * stagger) / (1 - stagger), 0.0f, 1.0f);
    out.opacity = alpha * alpha * (3 - 2 * alpha);
    const Vec4 first = p[ColorStart].as_color(), last = p[ColorEnd].as_color();
    const auto linear = [fraction](f32 a, f32 b) {
        const f32 x = Color::srgb_to_linear(a), y = Color::srgb_to_linear(b);
        return x + (y - x) * fraction;
    };
    out.tint = {linear(first.x, last.x), linear(first.y, last.y), linear(first.z, last.z),
                percent(p[ColorMix]) * (first.w + (last.w - first.w) * fraction)};
    return out;
}

const EffectInstance* active(const Layer& layer, const char* key) noexcept {
    const auto type = effect_type_id(key);
    for (const auto& effect : layer.effects) if (effect.enabled && effect.type == type) return &effect;
    return nullptr;
}

u32 model_count(const Layer& layer, const EffectInstance& effect, f64 time) noexcept {
    const auto value = evaluate_param_f(layer.tracks, effect, Count, model_parameters().at(Count), time);
    return count(&value);
}

Copy sample_model(const Layer& layer, const EffectInstance& effect, f64 time, u32 index) noexcept {
    std::array<ParamValue, ParameterCount> values;
    const auto& p = model_parameters();
    for (u32 i = 0; i < ParameterCount; ++i) values[i] = evaluate_param_f(layer.tracks, effect, i, p.at(i), time);
    return sample(values.data(), &effect, index);
}

u32 toon_bands(const Layer& layer, f64 time) noexcept {
    const auto* effect = active(layer, kToonKey);
    if (!effect) return 0;
    static const ParameterRegistry reg = [] { ParameterRegistry p; toon_parameters(p); return p; }();
    return std::clamp(evaluate_param_f(layer.tracks, *effect, 0, reg.at(0), time).as_int(), 2, 6);
}
} // namespace aurea::repeat_layout

namespace aurea::builtin {
namespace {
constexpr u32 kCopyLimit = repeat_layout::kMaxCopies;
struct RepeatUniforms {
    Vec4 region{}, input{};
    struct Rows { Vec4 x{}, y{}; } copies[kCopyLimit];
    Vec4 tints[kCopyLimit]{};
};
static_assert(sizeof(RepeatUniforms)==32+kCopyLimit*48);
enum RepeatKind { Basic, Linear, Grid, Radial, Path, Scatter, Arrange, Replicate3D, Toon3D };
class Repeater final : public Effect {
    RepeatKind kind_;
public:
    explicit Repeater(RepeatKind kind):kind_(kind){}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[]={
            {"aurea.repeat.basic","Repeat (Basic)","Distorcer",EffectClass::Domain},
            {"aurea.repeat.linear","Linear Repeat","Distorcer",EffectClass::Domain},
            {"aurea.repeat.grid","Grid Repeat","Distorcer",EffectClass::Domain},
            {"aurea.repeat.radial","Radial Repeat","Distorcer",EffectClass::Domain},
            {"aurea.repeat.path","Repeat Along Path","Distorcer",EffectClass::Domain},
            {"aurea.repeat.scatter","Scatter Repeat","Distorcer",EffectClass::Domain},
            {repeat_layout::kPlaneKey, "Distribuir cópias", "Repetir", EffectClass::Domain},
            {repeat_layout::kModelKey, "Repetidor 3D", "3D", EffectClass::Domain},
            {repeat_layout::kToonKey, "Material Toon", "3D", EffectClass::Domain},
        };return infos[kind_];
    }
    enum :u32{Count,Spacing,Angle,Scale,Opacity,Columns,Radius,Arc,Seed,Orient,Phase};
    void declare_parameters(ParameterRegistry& p) const override {
        if (kind_ == Toon3D) { repeat_layout::toon_parameters(p); return; }
        if (kind_ == Arrange || kind_ == Replicate3D) {
            repeat_layout::declare_parameters(p, kind_ == Replicate3D);
            return;
        }
        p.add_int("count","Quantidade",kind_==Grid?9:5,1,kCopyLimit);
        p.add_point2("spacing",kind_==Scatter?"Dispersão":"Espaçamento",{100,kind_==Grid||kind_==Scatter?100.f:0.f},-4000,4000,
            kParamAnimatable|kParamPixels|((kind_==Radial||kind_==Path)?kParamHidden:0));
        p.add_angle("angle","Rotação por cópia",0);
        p.add_float("scale","Escala por cópia",100,10,200,kParamAnimatable|kParamPercent,"%");
        p.add_float("end_opacity","Opacidade final",100,0,100,kParamAnimatable|kParamPercent,"%");
        p.add_int("columns","Colunas",3,1,kCopyLimit,kind_==Grid?kParamAnimatable:kParamHidden);
        p.add_float("radius","Raio",150,0,4000,kParamAnimatable|kParamPixels|(kind_==Radial?0:kParamHidden),"px");
        p.add_float("arc","Arco",360,0,360,kind_==Radial?kParamAnimatable:kParamHidden,"°");
        p.add_int("seed","Semente",1,0,999999,kind_==Scatter?kParamAnimatable:kParamHidden);
        p.add_bool("orient","Orientar as cópias",true,kind_==Radial||kind_==Path?kParamAnimatable:kParamHidden);
        p.add_float("phase","Deslocamento no caminho",0,-1,1,kind_==Path?kParamAnimatable:kParamHidden);
    }
    bool is_identity(const EffectEval&) const noexcept override {
        return kind_ == Replicate3D || kind_ == Toon3D;
    }
    std::span<const EffectPreset> presets() const noexcept override {
        if (kind_ != Arrange && kind_ != Replicate3D) return {};
        using namespace repeat_layout;
        static constexpr EffectPresetValue line[]{{repeat_layout::Layout, 0}, {repeat_layout::Count, 5}};
        static constexpr EffectPresetValue orbit[]{{repeat_layout::Layout, 2}, {repeat_layout::Count, 8}};
        static constexpr EffectPresetValue magnet[]{{repeat_layout::Layout, 1}, {repeat_layout::Count, 9}, {FocusScale, 170}};
        static constexpr EffectPresetValue field[]{{repeat_layout::Layout, 3}, {repeat_layout::Count, 24}, {RandomRotation, 35}};
        static const EffectPreset options[]{{"copies_line", "Linha", line}, {"copies_orbit", "Órbita", orbit},
                                           {"copies_magnet", "Ímã", magnet}, {"copies_field", "Campo", field}};
        return options;
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        if(kind_==Path&&e.resources) e.pathSamples=std::make_shared<const std::vector<Vec4>>(
            e.resources->repeat_path(e.layer,std::clamp(e.value(Count).as_int(),1,int(kCopyLimit)),e.f(Phase)));
    }
    void pipelines(std::vector<PipelineKey>& out,SurfaceFormat f) const override {
        if (kind_ == Replicate3D || kind_ == Toon3D) return;
        auto p=PipelineKey::fullscreen(ShaderId::effects_repeat_copies_frag,f);
        p.vertex=ShaderId::effects_repeat_copies_vert;
        p.blendEnabled=true;
        out.push_back(p);
    }
    Status build(EffectBuildContext& ctx,const EffectEval& e,const LayerImage& in,float margin,LayerImage& out) const override {
        if (kind_ == Replicate3D || kind_ == Toon3D) { out = in; return OkStatus; }
        const bool arrange = kind_ == Arrange;
        const u32 count = arrange ? repeat_layout::count(e.values) : std::clamp(e.value(Count).as_int(), 1, int(kCopyLimit));
        if(kind_==Path&&(!e.pathSamples||e.pathSamples->empty())) { out=in;return OkStatus; }
        const Vec2 pivot{e.placement?e.placement->layerWidth*.5f:in.region.w*.5f,e.placement?e.placement->layerHeight*.5f:in.region.h*.5f};
        RepeatUniforms u; u.input={in.region.x,in.region.y,in.region.w,in.region.h};
        float minX=1e30f,minY=1e30f,maxX=-1e30f,maxY=-1e30f;
        const Vec2 step = arrange ? Vec2{} : e.p2(Spacing);
        const u32 columns = arrange ? 1u : std::clamp(e.value(Columns).as_int(), 1, int(kCopyLimit));
        const u32 seed = arrange ? 0u : u32(std::max(0, e.value(Seed).as_int()));
        Vec2 accumulated{};
        for(u32 i=0;i<count;++i) {
            const float fraction=count>1?float(i)/float(count-1):0;
            Vec2 offset{};
            f32 angle = 0, scale = 1, opacity = 1;
            if (arrange) {
                const auto copy = repeat_layout::sample(e.values, e.instance, i);
                offset = {copy.position.x, copy.position.y};
                angle = (copy.rotation.x + copy.rotation.z) * kDeg2Rad;
                scale = copy.scale;
                opacity = copy.opacity;
                u.tints[i] = copy.tint;
            } else {
                angle = e.f(Angle) * i * kDeg2Rad;
                scale = std::clamp(std::pow(std::clamp(e.f(Scale) * .01f, .1f, 2.f), float(i)), .001f, 1000.f);
                opacity = 1 + (e.f(Opacity) * .01f - 1) * fraction;
            }
            if(kind_==Grid)offset={step.x*float(i%columns),step.y*float(i/columns)};
            else if(kind_==Radial) {
                const float theta=e.f(Arc)*kDeg2Rad*float(i)/float(e.f(Arc)>=359.999f?count:std::max(1u,count-1));
                offset={std::cos(theta)*e.f(Radius),std::sin(theta)*e.f(Radius)};
                if(e.b(Orient))angle+=theta;
            } else if(kind_==Scatter) {
                offset={step.x*shake::random(seed,i,0),step.y*shake::random(seed,i,1)};
                angle=e.f(Angle)*kDeg2Rad*shake::random(seed,i,2);
                scale=std::max(.001f,1+(e.f(Scale)*.01f-1)*(shake::random(seed,i,3)+1)*.5f);
            } else if(kind_==Path) {
                const auto pos=(*e.pathSamples)[std::min<usize>(i,e.pathSamples->size()-1)];
                offset={pos.x-pivot.x,pos.y-pivot.y}; if(e.b(Orient))angle+=pos.z;
            } else if(kind_==Basic) {
                offset=accumulated;
                const float c=std::cos(angle)*scale,s=std::sin(angle)*scale;
                accumulated=accumulated+Vec2{c*step.x-s*step.y,s*step.x+c*step.y};
            } else if (!arrange) offset={step.x*i,step.y*i};
            const float c=std::cos(angle)*scale,s=std::sin(angle)*scale;
            const float x=pivot.x+offset.x-c*pivot.x+s*pivot.y,y=pivot.y+offset.y-s*pivot.x-c*pivot.y;
            u.copies[i]={{c,-s,x,opacity},{s,c,y,0}};
            for(float px:{in.region.x,in.region.x+in.region.w})for(float py:{in.region.y,in.region.y+in.region.h}) {
                const float tx=c*px-s*py+x,ty=s*px+c*py+y;
                minX=std::min(minX,tx);minY=std::min(minY,ty);maxX=std::max(maxX,tx);maxY=std::max(maxY,ty);
            }
        }
        const Rect region=spread_region({minX,minY,maxX-minX,maxY-minY},0,0,e.placement,margin);
        u.region={region.x,region.y,region.w,region.h}; u32 w,h;ctx.region_size(region,in.texel_scale_x(),w,h);
        out={ctx.texture(info().key,w,h),region,w,h};
        if(ctx.geometry_pass(info().key,PassStage::Effects,out.texture,ShaderId::effects_repeat_copies_vert,ShaderId::effects_repeat_copies_frag,
            {PassTexture{in.texture,{},CommonSampler::LinearBorder}},&u,sizeof(u),count*6,false,true)==kInvalidIndex)return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
}
void register_repeat_extras(EffectRegistry& r) {
    for (int k = Basic; k <= Toon3D; ++k) (void)r.add(std::make_unique<Repeater>(RepeatKind(k)));
}
}
