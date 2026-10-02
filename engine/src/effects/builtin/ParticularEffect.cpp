// =============================================================================
//  Particular — o sistema de partículas do app antigo, nativo no motor.
//
//  O dono pediu: "remova as partículas do Aurea e coloque o Particular do app
//  antigo". A simulação, o visual e os parâmetros são os de lá (emissor em
//  caixa/esfera medido em fração do quadro, pré-rolagem, velocidade com cone
//  e mira 3D, gravidade, vento, arrasto do ar, turbulência, vida, tamanho e
//  cor ao longo da vida, surgir/sumir, esticar no movimento, mistura aditiva);
//  a interface é a do Aurea (as linhas genéricas do painel de efeitos).
//
//  Como tudo aqui é FECHADO (particular.vert), o quadro depende só de
//  (parâmetros, tempo, semente): prévia = export, determinístico.
//
//  Passes:
//    1. geometria: `slots` × 6 vértices, cada slot um quadrado, misturados no
//       alvo (soma na aditiva, "sobre" na normal);
//    2. composição: partículas presas em 0..1, por cima da camada quando
//       "Mostrar camada" está ligado.
//
//  Unidades guardadas: percentuais em % (o app antigo guardava frações com
//  escala de exibição 100 — os presets convertem), tempos em ms, ângulos em
//  graus, distâncias em px da composição.
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/effects/Particular.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aurea::builtin {
namespace {

using namespace particular;

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
f32 pct(const EffectEval& e, u32 i, f32 lo, f32 hi) noexcept {
    return std::clamp(finite_or(e.f(i), 0.0f), lo, hi) * 0.01f;
}

constexpr u16 kAnim = kParamAnimatable;
constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;
/// Teto de slots (6 vértices cada) e de partículas por segundo — os do app antigo.
constexpr f32 kMaxSlots = 4000.0f;
constexpr f32 kMaxRate = 1000.0f;

struct ParticularUniforms {
    Vec4 region, view, clock, frame, emitPos, emitSize, launch, cone, forces, motion, lifeSize, look, color0, color1;
    Mat4 worldFromLayer, compFromWorld;   // camada do Particular (LayerPlacement::particleSpace)
    Vec4 camRight, camUp;                 // camRight.w = 1 liga o espaço 3D
    Mat4 previousParticleProjection;
};
static_assert(sizeof(ParticularUniforms) == 448, "layout std140 do particular.vert");

Vec4 linear(Vec4 c) noexcept {
    return Vec4{Color::srgb_to_linear(std::clamp(c.x, 0.0f, 1.0f)), Color::srgb_to_linear(std::clamp(c.y, 0.0f, 1.0f)),
                Color::srgb_to_linear(std::clamp(c.z, 0.0f, 1.0f)), 1.0f};
}

class Particular final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kParticular, "Particular", "Gerar", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        // Emissor
        p.add_float("rate", "Partículas/seg", 60.0f, 0.0f, kMaxRate, kAnim, "/s");
        p.add_float("pre_roll", "Pré-rolagem", 1000.0f, 0.0f, 30000.0f, kAnim, "ms");
        p.add_float("position_x", "Posição X", 0.0f, -400.0f, 400.0f, kAnimPct, "%");
        p.add_float("position_y", "Posição Y", 0.0f, -400.0f, 400.0f, kAnimPct, "%");
        p.add_float("position_z", "Posição Z", 0.0f, -400.0f, 400.0f, kAnimPct, "%");
        p.add_float("emitter_size_x", "Tamanho do emissor X", 0.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.add_float("emitter_size_y", "Tamanho do emissor Y", 0.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.add_float("emitter_size_z", "Tamanho do emissor Z", 0.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.add_bool("emitter_sphere", "Emissor esférico", false);
        // Velocidade e direção
        p.add_float("velocity", "Velocidade", 320.0f, 0.0f, 6000.0f, kAnimPx, "px/s");
        p.add_float("velocity_random", "Variação da velocidade", 45.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("direction_tilt", "Inclinação da direção", 0.0f, 0.0f, 180.0f, kAnim, "°");
        p.add_float("direction_spin", "Giro da direção", 0.0f, 0.0f, 360.0f, kAnim, "°");
        p.add_float("spread", "Abertura", 32.0f, 0.0f, 180.0f, kAnim, "°");
        p.add_float("outwards", "Para fora", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        // Física
        p.add_float("gravity", "Gravidade", 260.0f, -8000.0f, 8000.0f, kAnimPx, "px/s²");
        p.add_float("wind_x", "Vento X", 0.0f, -4000.0f, 4000.0f, kAnimPx, "px/s");
        p.add_float("wind_y", "Vento Y", 0.0f, -4000.0f, 4000.0f, kAnimPx, "px/s");
        p.add_float("wind_z", "Vento Z", 0.0f, -4000.0f, 4000.0f, kAnimPx, "px/s");
        p.add_float("air_drag", "Resistência do ar", 0.0f, 0.0f, 800.0f, kAnimPct, "%");
        p.add_float("turbulence", "Turbulência", 0.0f, 0.0f, 4000.0f, kAnimPx, "px");
        p.add_float("turbulence_speed", "Velocidade da turbulência", 100.0f, 0.0f, 1000.0f, kAnimPct, "%");
        // Partícula
        p.add_float("life", "Vida", 2000.0f, 50.0f, 30000.0f, kAnim, "ms");
        p.add_float("life_random", "Variação da vida", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("size", "Tamanho", 26.0f, 0.0f, 2000.0f, kAnimPx, "px");
        p.add_float("size_random", "Variação do tamanho", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("size_end", "Tamanho final", 20.0f, 0.0f, 800.0f, kAnimPct, "%");
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("fade_in", "Surgir", 8.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("fade_out", "Sumir", 45.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("feather", "Suavidade da borda", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("stretch", "Esticar no movimento", 0.0f, 0.0f, 600.0f, kAnimPct, "%");
        p.add_color("color", "Cor da partícula", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_color("color_end", "Cor final", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("color_random", "Cor aleatória", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        // Render
        p.add_bool("add_mode", "Mistura aditiva", true);
        p.add_bool("show_source", "Mostrar camada", false);
        p.add_float("seed", "Semente", 0.0f, 0.0f, 9999.0f, kAnim);
        // Desfoque de movimento por partícula (o do app antigo): rastro na
        // direção do movimento visível, do tamanho do trajeto no obturador.
        // Também liga sozinho com a chave de desfoque de movimento da camada
        // (aí vale o obturador da composição).
        p.add_bool("motion_blur", "Desfoque de movimento", false);
        p.add_float("shutter_angle", "Ângulo do obturador", 180.0f, 0.0f, 720.0f, kAnim, "°");
    }

    bool is_identity(const EffectEval&) const noexcept override { return false; }

    // A prévia do catálogo: a neve mansa que o app antigo mostrava no cartão.
    bool demo_values(EffectInstance& instance, std::vector<ParamValue>& values) const noexcept override {
        (void)instance;
        if (values.size() < kParamCount) return false;
        auto set = [&](u32 i, f32 v) { values[i] = ParamValue::scalar(v); };
        set(kRate, 70.0f); set(kPreRoll, 9000.0f);
        set(kEmitterW, 160.0f); set(kEmitterH, 240.0f); set(kEmitterD, 200.0f);
        set(kVelocity, 24.0f); set(kVelocityRandom, 60.0f); set(kDirectionTilt, 180.0f); set(kSpread, 30.0f);
        set(kGravity, 12.0f); set(kWindX, 9.0f); set(kAirDrag, 60.0f);
        set(kTurbulence, 20.0f); set(kTurbulenceSpeed, 35.0f);
        set(kLife, 7000.0f); set(kLifeRandom, 45.0f);
        set(kSize, 6.0f); set(kSizeRandom, 70.0f); set(kSizeEnd, 100.0f);
        set(kOpacity, 90.0f); set(kFadeIn, 6.0f); set(kFadeOut, 12.0f);
        values[kAddMode] = ParamValue::boolean(false);
        return true;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (BlendMode m : {BlendMode::Add, BlendMode::Normal}) {
            out.push_back(PipelineKey::graphics(ShaderId::effects_particular_vert, ShaderId::effects_particular_frag,
                                                work, true, m));
        }
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_particular_compose_frag, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        // A saída cobre o que o quadro mostra: as partículas voam para fora
        // da caixa da camada.
        Rect region = input.region;
        Vec2 comp{input.region.w, input.region.h};
        Vec2 layerSize{input.region.w, input.region.h};
        if (e.placement) {
            if (e.placement->compWidth && e.placement->compHeight) {
                comp = Vec2{static_cast<f32>(e.placement->compWidth), static_cast<f32>(e.placement->compHeight)};
            }
            if (e.placement->layerWidth && e.placement->layerHeight) {
                layerSize = Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
            }
            const Rect vis = visible_layer_rect(*e.placement);
            if (vis.w > 0.5f && vis.h > 0.5f) {
                region = Rect{vis.x - margin - 1.0f, vis.y - margin - 1.0f,
                              vis.w + 2.0f * margin + 2.0f, vis.h + 2.0f * margin + 2.0f};
            }
        }
        const f32 texel = std::max(input.texel_scale_x(), 1e-4f);
        u32 w = 0, h = 0;
        ctx.region_size(region, texel, w, h);
        if (!w || !h) return Status{Errc::InvalidArgument, "particular: regiao vazia"};

        // --- A grade de slots (a conta do app antigo) --------------------------
        const f32 rate = std::clamp(finite_or(e.f(kRate), 0.0f), 0.0f, kMaxRate);
        const f32 lifeMs = std::clamp(finite_or(e.f(kLife), 2000.0f), 50.0f, 30000.0f);
        const f32 lifeRandom = pct(e, kLifeRandom, 0.0f, 100.0f);
        const f32 preRoll = std::clamp(finite_or(e.f(kPreRoll), 0.0f), 0.0f, 30000.0f) * 0.001f;
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        const f32 now = static_cast<f32>(static_cast<f64>(e.localTime.value) / fps) + preRoll;
        const f32 span = std::max((lifeRandom + 1.0f) * lifeMs * 0.001f * 1.05f, 1e-3f);
        const f32 grid = std::min(kMaxRate, kMaxSlots / span);
        const u32 slots = static_cast<u32>(std::clamp(std::ceil(span * grid), 1.0f, kMaxSlots));
        const bool draw = rate > 0.0f && now >= 0.0f;

        ParticularUniforms u{};
        u.region = Vec4{region.x, region.y, region.w, region.h};
        // Câmera padrão da composição: 40° na vertical, olhando o centro.
        const f32 focal = comp.y * 0.5f / std::tan(20.0f * 0.01745329252f);
        u.view = Vec4{layerSize.x * 0.5f, layerSize.y * 0.5f, focal, 0.0f};
        u.clock = Vec4{now, grid, static_cast<f32>(slots), std::min(rate, grid)};
        u.frame = Vec4{comp.x, comp.y, layerSize.x * 0.5f, layerSize.y * 0.5f};
        u.emitPos = Vec4{pct(e, kPositionX, -400.0f, 400.0f), pct(e, kPositionY, -400.0f, 400.0f),
                         pct(e, kPositionZ, -400.0f, 400.0f), e.b(kEmitterSphere) ? 1.0f : 0.0f};
        u.emitSize = Vec4{pct(e, kEmitterW, 0.0f, 400.0f), pct(e, kEmitterH, 0.0f, 400.0f),
                          pct(e, kEmitterD, 0.0f, 400.0f), std::clamp(finite_or(e.f(kSeed), 0.0f), 0.0f, 9999.0f)};
        constexpr f32 kDeg = 0.01745329252f;
        u.launch = Vec4{std::clamp(finite_or(e.f(kVelocity), 0.0f), 0.0f, 60000.0f), pct(e, kVelocityRandom, 0.0f, 100.0f),
                        std::clamp(finite_or(e.f(kDirectionTilt), 0.0f), 0.0f, 180.0f) * kDeg,
                        std::clamp(finite_or(e.f(kDirectionSpin), 0.0f), 0.0f, 360.0f) * kDeg};
        u.cone = Vec4{std::clamp(finite_or(e.f(kSpread), 0.0f), 0.0f, 180.0f), pct(e, kOutwards, 0.0f, 100.0f),
                      pct(e, kStretch, 0.0f, 600.0f), pct(e, kFeather, 0.0f, 100.0f)};
        u.forces = Vec4{finite_or(e.f(kGravity), 0.0f), finite_or(e.f(kWindX), 0.0f), finite_or(e.f(kWindY), 0.0f),
                        finite_or(e.f(kWindZ), 0.0f)};
        // Obturador: o do efeito, ou o da composição quando a camada está com
        // a chave de desfoque de movimento ligada. Meia janela, em segundos.
        f32 shutterDeg = 0.0f;
        if (e.b(kMotionBlur)) shutterDeg = std::clamp(finite_or(e.f(kShutterAngle), 0.0f), 0.0f, 720.0f);
        else if (e.placement) shutterDeg = std::clamp(finite_or(e.placement->shutterAngle, 0.0f), 0.0f, 2880.0f);
        const f32 halfShutter = shutterDeg / 360.0f / static_cast<f32>(fps) * 0.5f;
        u.motion = Vec4{pct(e, kAirDrag, 0.0f, 800.0f), std::clamp(finite_or(e.f(kTurbulence), 0.0f), 0.0f, 40000.0f),
                        pct(e, kTurbulenceSpeed, 0.0f, 1000.0f), halfShutter};
        u.lifeSize = Vec4{lifeMs, lifeRandom, std::clamp(finite_or(e.f(kSize), 0.0f), 0.0f, 20000.0f),
                          pct(e, kSizeRandom, 0.0f, 100.0f)};
        u.look = Vec4{pct(e, kSizeEnd, 0.0f, 800.0f), pct(e, kOpacity, 0.0f, 100.0f), pct(e, kFadeIn, 0.0f, 100.0f),
                      pct(e, kFadeOut, 0.0f, 100.0f)};
        u.color0 = linear(e.color(kColor));
        u.color0.w = pct(e, kColorRandom, 0.0f, 100.0f);
        u.color1 = linear(e.color(kColorEnd));
        u.color1.w = 0.0f;
        if (e.placement && e.placement->particleSpace) {
            // Camada do Particular: a rotação da camada gira o espaço das
            // partículas; a câmera é a da composição.
            u.worldFromLayer = e.placement->worldFromLayer;
            u.compFromWorld = e.placement->compFromWorld;
            u.camRight = Vec4{e.placement->camRight.x, e.placement->camRight.y, e.placement->camRight.z, 1.0f};
            u.camUp = Vec4{e.placement->camUp.x, e.placement->camUp.y, e.placement->camUp.z, static_cast<f32>(fps)};
            u.previousParticleProjection = e.placement->previousParticleProjection;
        }

        const bool additive = e.b(kAddMode);
        const FGTexture sprites = ctx.texture("particular-particulas", w, h);
        if (ctx.geometry_pass("particular", PassStage::Effects, sprites, ShaderId::effects_particular_vert,
                              ShaderId::effects_particular_frag, {}, &u, sizeof(u), draw ? slots * 6u : 0u, false,
                              true, additive ? BlendMode::Add : BlendMode::Normal) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        struct ComposeUniforms {
            Vec4 uvMap;
            Vec4 mode;
        } cu{};
        cu.uvMap = EffectBuildContext::uv_map(region, input.region);
        cu.mode = Vec4{additive ? 1.0f : 0.0f, e.b(kShowSource) ? 1.0f : 0.0f, 0.0f, 0.0f};
        out.region = region;
        out.width = w;
        out.height = h;
        out.texture = ctx.texture("particular", w, h);
        if (ctx.fullscreen_pass("particular-composicao", PassStage::Effects, out.texture,
                                ShaderId::effects_particular_compose_frag,
                                {PassTexture{sprites, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &cu, sizeof(cu)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// --- Presets do app antigo -----------------------------------------------------
// Valores nas unidades guardadas (%, ms, graus, px). Cores em sRGB, como o
// usuário escolhe no seletor.
struct PresetValue { u32 param = kParamCount; f32 v = 0.0f; };   // kParamCount = fim da lista
struct PresetColor { f32 r, g, b; };
struct Preset {
    PresetValue values[32];
    PresetColor color, colorEnd;
};

const Preset& preset_table(u32 i) noexcept {
    static const Preset kPresets[kPresetCount] = {
        // 0 Padrão: os padrões da declaração.
        {{}, {1, 1, 1}, {1, 1, 1}},
        // 1 Chuva
        {{{kRate, 500}, {kPreRoll, 3000}, {kPositionY, -10}, {kEmitterW, 160}, {kEmitterH, 240}, {kEmitterD, 160},
          {kVelocity, 1800}, {kVelocityRandom, 25}, {kDirectionTilt, 180}, {kSpread, 4}, {kGravity, 0},
          {kWindX, 160}, {kAirDrag, 0}, {kTurbulence, 0}, {kLife, 2200}, {kLifeRandom, 25}, {kSize, 7},
          {kSizeRandom, 50}, {kSizeEnd, 100}, {kOpacity, 42}, {kFadeIn, 3}, {kFadeOut, 8}, {kFeather, 100},
          {kStretch, 320}, {kAddMode, 0}},
         {0.78f, 0.86f, 1.0f}, {0.78f, 0.86f, 1.0f}},
        // 2 Neve
        {{{kRate, 180}, {kPreRoll, 9000}, {kEmitterW, 160}, {kEmitterH, 240}, {kEmitterD, 200}, {kVelocity, 70},
          {kVelocityRandom, 60}, {kDirectionTilt, 180}, {kSpread, 30}, {kGravity, 35}, {kWindX, 45}, {kAirDrag, 60},
          {kTurbulence, 140}, {kTurbulenceSpeed, 35}, {kLife, 9000}, {kLifeRandom, 45}, {kSize, 15}, {kSizeRandom, 70},
          {kSizeEnd, 100}, {kOpacity, 90}, {kFadeIn, 6}, {kFadeOut, 12}, {kFeather, 100}, {kStretch, 0}, {kAddMode, 0}},
         {1, 1, 1}, {1, 1, 1}},
        // 3 Fogo
        {{{kRate, 300}, {kPreRoll, 1500}, {kEmitterW, 18}, {kEmitterH, 3}, {kEmitterD, 10}, {kVelocity, 420},
          {kVelocityRandom, 50}, {kDirectionTilt, 0}, {kSpread, 22}, {kGravity, -260}, {kAirDrag, 120},
          {kTurbulence, 90}, {kTurbulenceSpeed, 160}, {kLife, 1400}, {kLifeRandom, 40}, {kSize, 46}, {kSizeRandom, 50},
          {kSizeEnd, 15}, {kOpacity, 85}, {kFadeIn, 8}, {kFadeOut, 55}, {kFeather, 100}, {kStretch, 60}, {kAddMode, 1}},
         {1.0f, 0.85f, 0.35f}, {0.95f, 0.15f, 0.03f}},
        // 4 Faíscas
        {{{kRate, 220}, {kPreRoll, 800}, {kEmitterW, 2}, {kEmitterH, 2}, {kEmitterD, 2}, {kVelocity, 1100},
          {kVelocityRandom, 70}, {kDirectionTilt, 35}, {kSpread, 45}, {kGravity, 900}, {kAirDrag, 100},
          {kTurbulence, 40}, {kTurbulenceSpeed, 200}, {kLife, 1200}, {kLifeRandom, 50}, {kSize, 8}, {kSizeRandom, 60},
          {kSizeEnd, 5}, {kOpacity, 100}, {kFadeIn, 2}, {kFadeOut, 50}, {kFeather, 85}, {kStretch, 260},
          {kColorRandom, 15}, {kAddMode, 1}},
         {1.0f, 0.9f, 0.55f}, {1.0f, 0.25f, 0.05f}},
        // 5 Fogos de artifício
        {{{kRate, 60}, {kPreRoll, 0}, {kEmitterW, 0}, {kEmitterH, 0}, {kEmitterD, 0}, {kVelocity, 900},
          {kVelocityRandom, 35}, {kSpread, 180}, {kGravity, 380}, {kAirDrag, 160}, {kTurbulence, 0}, {kLife, 2200},
          {kLifeRandom, 30}, {kSize, 12}, {kSizeRandom, 60}, {kSizeEnd, 10}, {kOpacity, 100}, {kFadeIn, 2},
          {kFadeOut, 60}, {kFeather, 100}, {kStretch, 140}, {kColorRandom, 60}, {kAddMode, 1}},
         {1.0f, 0.95f, 0.7f}, {1.0f, 0.3f, 0.1f}},
        // 6 Poeira
        {{{kRate, 120}, {kPreRoll, 8000}, {kEmitterW, 140}, {kEmitterH, 140}, {kEmitterD, 160}, {kVelocity, 30},
          {kVelocityRandom, 80}, {kSpread, 180}, {kGravity, 0}, {kWindX, 25}, {kAirDrag, 40}, {kTurbulence, 70},
          {kTurbulenceSpeed, 25}, {kLife, 10000}, {kLifeRandom, 50}, {kSize, 8}, {kSizeRandom, 80}, {kSizeEnd, 100},
          {kOpacity, 50}, {kFadeIn, 15}, {kFadeOut, 30}, {kFeather, 100}, {kStretch, 0}, {kAddMode, 1}},
         {1.0f, 0.97f, 0.9f}, {1.0f, 0.97f, 0.9f}},
        // 7 Bokeh
        {{{kRate, 40}, {kPreRoll, 12000}, {kEmitterW, 160}, {kEmitterH, 160}, {kEmitterD, 240}, {kVelocity, 12},
          {kVelocityRandom, 100}, {kSpread, 180}, {kGravity, 0}, {kAirDrag, 0}, {kTurbulence, 25},
          {kTurbulenceSpeed, 15}, {kLife, 14000}, {kLifeRandom, 50}, {kSize, 10}, {kSizeRandom, 90}, {kSizeEnd, 120},
          {kOpacity, 90}, {kFadeIn, 20}, {kFadeOut, 30}, {kFeather, 100}, {kStretch, 0}, {kColorRandom, 25},
          {kAddMode, 1}},
         {0.85f, 0.92f, 1.0f}, {1.0f, 0.95f, 0.85f}},
    };
    return kPresets[std::min(i, kPresetCount - 1)];
}

} // namespace

void register_particular_effect(EffectRegistry& r) {
    (void)r.add(std::make_unique<Particular>());
}

} // namespace aurea::builtin

namespace aurea::particular {

bool apply_preset(EffectInstance& instance, const ParameterRegistry& params, u32 preset) noexcept {
    if (preset >= kPresetCount || params.count() < kParamCount) return false;
    const ParamValue seed = instance.params.size() > kSeed ? instance.params[kSeed].constant : ParamValue::scalar(0.0f);
    initialize_instance(instance, params);
    instance.params[kSeed].constant = seed;
    const builtin::Preset& p = builtin::preset_table(preset);
    for (const builtin::PresetValue& v : p.values) {
        if (v.param >= kParamCount) break;
        if (v.param < instance.params.size()) instance.params[v.param].constant = ParamValue::scalar(v.v);
    }
    instance.params[kColor].constant = ParamValue::color(p.color.r, p.color.g, p.color.b, 1.0f);
    instance.params[kColorEnd].constant = ParamValue::color(p.colorEnd.r, p.colorEnd.g, p.colorEnd.b, 1.0f);
    return true;
}

} // namespace aurea::particular
