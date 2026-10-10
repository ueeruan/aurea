// Original AUREA implementation, specified from public documentation/videos.
// The procedural fields are our own; numerical equivalence to Sapphire has
// not been established. References: docs/reference/effects-request-2026-10-09.md.
#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
class ProceduralWipe final : public Effect {
public:
    explicit ProceduralWipe(bool plasma) : plasma_(plasma) {}
    enum : u32 { Completion, Direction, Auto, Frequency, FrequencyX, Seed, Octaves,
        Shift, Phase, Speed, ShapeA, ShapeB, ShapeC, Gradient, GradientAngle,
        Feather, BorderWidth, BorderColor, BorderOpacity, BorderSoftness, BorderShift,
        Glow, GlowWidth, GlowWidths, GlowColor, Noise, NoiseFrequency, NoiseSpeed,
        OpacityMode, Background };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo flux{effect_keys::kWipeFlux, "WipeFlux", "Transição", EffectClass::Domain};
        static const EffectInfo plasma{effect_keys::kWipePlasma, "WipePlasma", "Transição", EffectClass::Domain};
        return plasma_ ? plasma : flux;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const directions[] = {"Remover camada", "Revelar camada"};
        static const char* const opacity[] = {"Normal", "Tudo opaco", "Pré-multiplicado"};
        p.add_float("wipe_percent", "Progresso", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_enum("transition_dir", "Direção", directions, 2, 0);
        p.add_bool("auto_transition", "Transição automática", false);
        p.add_float("frequency", "Frequência", plasma_ ? 4.f : 6.f, plasma_ ? .05f : .1f, 40);
        p.add_float("frequency_rel_x", "Frequência relativa X", 1, .01f, 10);
        p.add_float("seed", "Semente", plasma_ ? .12f : .432f, 0, 1);
        p.add_int("octaves", "Oitavas", plasma_ ? 4 : 2, 1, 10);
        p.add_point2("shift", "Deslocamento", {0,0}, -10, 10, kParamAnimatable | kParamRelative);
        p.add_float(plasma_ ? "phase_start" : "morph_start", plasma_ ? "Fase inicial" : "Morph inicial", 0, -100, 100, kParamAnimatable, "voltas");
        p.add_float(plasma_ ? "phase_speed" : "morph_speed", plasma_ ? "Velocidade da fase" : "Velocidade do morph", plasma_ ? 2.f : .3f, -20, 20);
        p.add_float(plasma_ ? "plasma_grad" : "bubble_amount", plasma_ ? "Gradiente do plasma" : "Intensidade das bolhas", plasma_ ? 0.f : 1.f, -10, 10);
        if (plasma_) {
            p.add_angle("plasma_grad_angle", "Ângulo do plasma", 0);
            p.add_int("plasma_layers", "Camadas de plasma", 8, 1, 16);
        } else {
            p.add_float("bubble_smooth", "Suavidade das bolhas", .25f, .008f, 1);
            p.add_angle("rotate_warp", "Rotação da deformação", 0);
        }
        p.add_float("grad_add", "Intensidade do gradiente", plasma_ ? .5f : 0.f, -10, 10);
        p.add_angle("grad_angle", "Ângulo do gradiente", 0);
        p.add_float("edge_softness", "Suavidade da borda", 0, 0, 1);
        p.add_float("border_width", "Largura da borda", 0, 0, 1);
        p.add_color("border_color", "Cor da borda", {.75f,0,0,1});
        p.add_float("border_opacity", "Opacidade da borda", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("border_softness", "Suavidade da faixa", 0, 0, 1);
        p.add_float("border_shift", "Deslocamento da borda", 0, -1, 1);
        p.add_float("border_glow", "Brilho da borda", 0, 0, 10);
        p.add_float("glow_width", "Largura do brilho", .1f, .001f, 1);
        p.add_point3("glow_width_rgb", "Largura por canal RGB", {1,1.2f,1.4f}, .01f, 10);
        p.add_color("glow_color", "Cor do brilho", {1,1,1,1});
        p.add_float("glow_noise_amp", "Ruído do brilho", 1, 0, 4);
        p.add_float("glow_noise_freq", "Frequência do ruído", 16, .1f, 20);
        p.add_float("glow_noise_speed", "Velocidade do ruído", 2, -20, 20);
        p.add_enum("opacity", "Processamento de alfa", opacity, 3, 0);
        p.add_layer_ref("background", "Camada de fundo");
    }
    i32 input_layer_param() const noexcept override { return Background; }
    static f32 safe(f32 v, f32 fallback = 0) noexcept { return std::isfinite(v) ? v : fallback; }
    static f32 completion(const EffectEval& e) noexcept {
        return std::clamp(e.b(Auto) ? e.clipProgress : safe(e.f(Completion)) / 100.f, 0.f, 1.f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return completion(e) == (e.e(Direction) == 0 ? 0.f : 1.f) && e.e(OpacityMode) == 0;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[Completion] = ParamValue::scalar(50);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_procedural_wipe_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        // Extended block: field, border and glow settings cannot fit in p0..p3.
        struct Uniforms {
            EffectUniforms base;
            Vec4 q0, q1, q2, q3, q4, q5, q6;
            Mat4 compFromLayer;
        } u{};
        static_assert(sizeof(Uniforms) == 288);
        u.base = base_uniforms(input);
        const Vec2 shift = e.p2(Shift);
        const auto widths = e.value(GlowWidths).as_vec3();
        const f32 seconds = safe(static_cast<f32>(e.time_frames() / e.framesPerSecond));
        const f32 width = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 height = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        u.base.p0 = {completion(e), safe(e.f(Feather)), static_cast<f32>(e.e(Direction)), plasma_ ? 1.f : 0.f};
        u.base.p1 = {std::clamp(safe(e.f(Frequency),4), .05f, 40.f), std::clamp(safe(e.f(FrequencyX),1), .01f,10.f), safe(e.f(Seed)), static_cast<f32>(std::clamp(e.value(Octaves).as_int(),1,10))};
        u.base.p2 = {safe(shift.x), safe(shift.y), safe(e.f(Phase)) + seconds * safe(e.f(Speed)), seconds};
        u.base.p3 = {safe(e.f(ShapeA)), safe(e.f(ShapeB)), safe(e.f(ShapeC)), safe(e.f(Gradient))};
        u.base.color = e.color(BorderColor);
        u.q0 = {safe(e.f(GradientAngle)) * kDeg2Rad, safe(e.f(BorderWidth)), safe(e.f(BorderOpacity)) / 100.f, safe(e.f(BorderSoftness))};
        u.q1 = {safe(e.f(BorderShift)), safe(e.f(Glow)), std::max(.001f,safe(e.f(GlowWidth),.1f)), safe(e.f(Noise))};
        u.q2 = {safe(widths.x,1),safe(widths.y,1.2f),safe(widths.z,1.4f),safe(e.f(NoiseFrequency),16)};
        u.q3 = e.color(GlowColor);
        u.q4 = {input.region.x,input.region.y,input.region.w,input.region.h};
        const u64 ref = e.value(Background).ref;
        const LayerImage* bg = ref ? ctx.layer_input(ref) : nullptr;
        u.q5 = {std::max(width,1.f),std::max(height,1.f),static_cast<f32>(e.e(OpacityMode)),bg ? 1.f : 0.f};
        u.q6 = {bg ? std::max(bg->region.w,1.f) : 1.f,bg ? std::max(bg->region.h,1.f) : 1.f,safe(e.f(NoiseSpeed),2),0};
        u.compFromLayer = e.placement ? e.placement->compFromLayer : Mat4::identity();
        out = input;
        out.texture = ctx.texture("procedural-wipe", input.width, input.height);
        return ctx.fullscreen_pass("procedural-wipe", PassStage::Effects, out.texture,
            ShaderId::effects_procedural_wipe_frag,
            {PassTexture{input.texture,{},CommonSampler::LinearClamp},
             PassTexture{bg ? bg->texture : input.texture,{},CommonSampler::LinearBorder}},
            &u,sizeof(u)) == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
private:
    bool plasma_;
};
}
void register_procedural_wipes(EffectRegistry& r) {
    (void)r.add(std::make_unique<ProceduralWipe>(false));
    (void)r.add(std::make_unique<ProceduralWipe>(true));
}
}
